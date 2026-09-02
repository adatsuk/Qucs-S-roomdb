#include "core_block_mapper.h"

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "cell_content.h"
#include "coord_scale.h"
#include "core_paths.h"
#include "core_schematic_io.h"
#include "database.h"
#include "source_info.h"
#include "qucs_block_codec.h"
#include "qucs_exporter.h"
#include "qucs_importer.h"
#include "primitive_resolver.h"
#include "net_name_propagation.h"
#include "pin_retarget.h"
#include "xschem_io.h"

#include "components/component.h"
#include "components/libcomp.h"
#include "node.h"
#include "schematic.h"
#include "wire.h"

#include <QDebug>
#include <QFileInfo>
#include <QHash>
#include <QMessageBox>
#include <QPoint>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTextStream>

#include <functional>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <list>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace qucs_core {
namespace {

core::QucsExporter::Options exporterOptionsFromEnvironment()
{
    core::QucsExporter::Options options;
    if (qEnvironmentVariableIsSet("LIBMAN_TECH_LIBRARY")) {
        options.techLibrary = qEnvironmentVariable("LIBMAN_TECH_LIBRARY").toStdString();
    }
    if (qEnvironmentVariableIsSet("QUCS_PRIMITIVE_LIB")) {
        options.qucsPrimitiveLib = qEnvironmentVariable("QUCS_PRIMITIVE_LIB").toStdString();
    }
    for (const std::string &path : core::PrimitiveResolver::primitiveCorePathsFromEnvironment()) {
        options.primitiveCorePaths.push_back(path);
    }
    return options;
}

core::QucsImporter::Options importerOptionsFromCorePath(const QString &corePath)
{
    core::QucsImporter::Options options;
    options.libName = "qucs_s";
    options.cellName = cellNameFromCorePath(corePath).toStdString();
    return options;
}

bool loadPropertySection(Schematic *schematic, const std::vector<core::Property> &properties,
                         const std::string &prefix, const QString &sectionName,
                         const std::function<bool(QTextStream *)> &loader)
{
    const std::vector<std::string> lines = core::qucs_codec::collectProperties(properties, prefix);
    if (lines.empty()) {
        return true;
    }

    QString buffer = sectionName + QStringLiteral("\n");
    for (const std::string &line : lines) {
        buffer += QStringLiteral("  ") + QString::fromStdString(line) + QStringLiteral("\n");
    }
    buffer += QStringLiteral("</>") + QStringLiteral("\n");

    QTextStream stream(&buffer, QIODevice::ReadOnly);
    stream.readLine();
    return loader(&stream);
}

void storePropertySection(std::vector<core::Property> &properties, const std::string &prefix,
                          const std::vector<std::string> &lines)
{
    properties.erase(std::remove_if(properties.begin(), properties.end(),
                                    [&](const core::Property &prop) { return prop.name == prefix; }),
                     properties.end());
    for (const std::string &line : lines) {
        properties.push_back({prefix, line});
    }
}

std::string sourceFormatFromContent(const core::CellContent &content)
{
    return content.sourceInfo().format();
}

const core::Property *findInstanceProperty(const std::vector<core::Property> &props, const std::string &name)
{
    for (const core::Property &prop : props) {
        if (prop.name == name) {
            return &prop;
        }
    }
    return nullptr;
}

void copyPropertyIfMissing(std::vector<core::Property> &dest, const std::vector<core::Property> &src,
                           const std::string &name)
{
    if (findInstanceProperty(dest, name) != nullptr) {
        return;
    }
    if (const core::Property *prop = findInstanceProperty(src, name)) {
        dest.push_back(*prop);
    }
}

void mergePreservedSchematicProperties(std::vector<core::Property> &dest, const std::vector<core::Property> &existing)
{
    // section.graph / section.Diagrams are synced explicitly on save — never preserve stale geometry.
    for (const char *key : {"section.launcher", "section.v", "section.s", "section.e",
                            "editor.sourceExt", "editor.source.toolVersion", "editor.source.fileVersion"}) {
        copyPropertyIfMissing(dest, existing, key);
    }
}

std::vector<std::string> diagramLinesFromSchText(const std::string &schText)
{
    std::vector<std::string> lines;
    const std::string open = "<Diagrams>";
    const std::string close = "</Diagrams>";
    const std::size_t start = schText.find(open);
    if (start == std::string::npos) {
        return lines;
    }
    const std::size_t contentStart = start + open.size();
    const std::size_t end = schText.find(close, contentStart);
    if (end == std::string::npos) {
        return lines;
    }
    std::istringstream in(schText.substr(contentStart, end - contentStart));
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t begin = line.find_first_not_of(" \t");
        if (begin == std::string::npos) {
            continue;
        }
        const std::size_t last = line.find_last_not_of(" \t");
        lines.push_back(line.substr(begin, last - begin + 1));
    }
    return lines;
}

void mergeInstanceMetadata(core::Instance &inst, const std::vector<core::Property> &existingProps)
{
    for (const char *key : {"core.primitive", "qucs.type", "qucs.model", "symname", "param.0", "param.1", "lab",
                            "format", "only_toplevel", "tclcommand", "descr", "value"}) {
        if (findInstanceProperty(inst.properties(), key) == nullptr) {
            if (const core::Property *prop = findInstanceProperty(existingProps, key)) {
                inst.properties().push_back(*prop);
            }
        }
    }
}

void setOrReplaceProperty(std::vector<core::Property> &props, const std::string &name, const std::string &value)
{
    for (core::Property &prop : props) {
        if (prop.name == name) {
            prop.value = value;
            return;
        }
    }
    props.push_back({name, value});
}

void removePropertiesWithName(std::vector<core::Property> &props, const std::string &name)
{
    props.erase(std::remove_if(props.begin(), props.end(),
                               [&](const core::Property &prop) { return prop.name == name; }),
                props.end());
}

core::Instance instanceFromEditorLine(const std::string &line, double dbuPerEditorUnit, core::QucsImporter &importer)
{
    core::Instance inst = importer.parseComponentLinePublic(line);
    inst.transform().x = core::editorUnitsToDbu(static_cast<double>(inst.transform().x), dbuPerEditorUnit);
    inst.transform().y = core::editorUnitsToDbu(static_cast<double>(inst.transform().y), dbuPerEditorUnit);
    for (core::Property &prop : inst.properties()) {
        if (prop.name == "textX" || prop.name == "textY") {
            prop.value = std::to_string(core::editorUnitsToDbu(std::stoll(prop.value), dbuPerEditorUnit));
        }
    }
    return inst;
}

/*! Connect isolated LibComp/Port pins to the nearest wire endpoint (one-to-one, small tolerance).
 *  Fixes Xschem-origin CORE loads where wire paths stop a few grid units short of CORE symbol pins. */
void repairIsolatedCorePortNodes(Schematic *schematic)
{
    if (schematic == nullptr) {
        return;
    }

    constexpr int kSnapTol = 22;
    const int tol2 = kSnapTol * kSnapTol;

    struct Candidate {
        Node *portNode = nullptr;
        Node *wireNode = nullptr;
        int d2 = 0;
    };

    std::vector<Node *> isolatedPorts;
    for (Component *pc : schematic->a_DocComps) {
        if (!pc || pc->isActive != COMP_IS_ACTIVE) {
            continue;
        }
        if (pc->Model != QLatin1String("Lib") && pc->Model != QLatin1String("Port")) {
            continue;
        }
        for (Port *pp : pc->Ports) {
            if (!pp || !pp->Connection) {
                continue;
            }
            Node *portNode = pp->Connection;
            if (portNode->wires().empty()) {
                isolatedPorts.push_back(portNode);
            }
        }
    }

    std::vector<Candidate> candidates;
    candidates.reserve(isolatedPorts.size() * 4);
    for (Node *portNode : isolatedPorts) {
        const QPoint portCenter = portNode->center();
        for (Node *wn : schematic->a_DocNodes) {
            if (wn == portNode || wn->wires().empty() || !wn->components().empty()) {
                continue;
            }
            const QPoint wc = wn->center();
            const int dx = portCenter.x() - wc.x();
            const int dy = portCenter.y() - wc.y();
            const int d2 = dx * dx + dy * dy;
            if (d2 <= tol2) {
                candidates.push_back({portNode, wn, d2});
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &a, const Candidate &b) { return a.d2 < b.d2; });

    std::vector<Node *> mergedPorts;
    std::vector<Node *> mergedWireNodes;
    std::list<Node *> toDelete;

    for (const Candidate &c : candidates) {
        const auto usedPort = std::find(mergedPorts.begin(), mergedPorts.end(), c.portNode);
        const auto usedWire = std::find(mergedWireNodes.begin(), mergedWireNodes.end(), c.wireNode);
        if (usedPort != mergedPorts.end() || usedWire != mergedWireNodes.end()) {
            continue;
        }
        c.wireNode->merge(c.portNode);
        mergedPorts.push_back(c.portNode);
        mergedWireNodes.push_back(c.wireNode);
        toDelete.push_back(c.portNode);
    }

    for (Node *dead : toDelete) {
        schematic->a_DocNodes.remove(dead);
        delete dead;
    }
}

QPoint coreTermToEditor(const core::Term &term, double dbuPerEditorUnit, qint64 coordDivisor)
{
    const qint64 divisor = coordDivisor > 0 ? coordDivisor : 1;
    const int x = static_cast<int>(std::llround(core::dbuToEditorUnits(term.position().x, dbuPerEditorUnit) / divisor));
    const int y = static_cast<int>(std::llround(core::dbuToEditorUnits(term.position().y, dbuPerEditorUnit) / divisor));
    return QPoint(x, y);
}

Node *findSchematicNodeNear(Schematic *schematic, const QPoint &pt, int tol)
{
    if (schematic == nullptr) {
        return nullptr;
    }
    const int tol2 = tol * tol;
    Node *best = nullptr;
    int bestD2 = tol2 + 1;
    for (Node *node : schematic->a_DocNodes) {
        if (node == nullptr) {
            continue;
        }
        const QPoint center = node->center();
        const int dx = center.x() - pt.x();
        const int dy = center.y() - pt.y();
        const int d2 = dx * dx + dy * dy;
        if (d2 <= tol2 && d2 < bestD2) {
            best = node;
            bestD2 = d2;
        }
    }
    return best;
}

std::string canonNetKey(const std::string &netName)
{
    std::string key = netName;
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return key;
}

void mergeNodeIntoNetAnchor(std::unordered_map<std::string, Node *> &anchors, const std::string &netName, Node *node)
{
    if (netName.empty() || node == nullptr) {
        return;
    }
    const std::string key = canonNetKey(netName);
    const auto found = anchors.find(key);
    if (found == anchors.end() || found->second == nullptr) {
        anchors[key] = node;
        return;
    }
    if (found->second != node) {
        found->second->merge(node);
    }
}

/*! Bind Qucs nodes to CORE named nets (Vin/Vout/Gnd/Vdd) for hierarchical netlist export.
 *  Uses xschem pin geometry from the CORE block — not Qucs .lib pin artwork. */
void mergeNodesByCoreNamedNets(Schematic *schematic, const core::Block &block, double dbuPerEditorUnit,
                               qint64 coordDivisor, bool requireLibPinsEnv = true)
{
    if (schematic == nullptr
        || (requireLibPinsEnv && qEnvironmentVariableIsEmpty("LIBMAN_NETLIST_LIBPINS"))) {
        return;
    }

    std::unordered_map<std::string, Node *> anchors;

    auto portLabelFromInstance = [](const core::Instance &inst) -> std::string {
        for (const core::Property &prop : inst.properties()) {
            if (prop.name == "lab" && !prop.value.empty()) {
                return prop.value;
            }
        }
        return {};
    };

    // Hierarchical ports (iopin.sym → Qucs Port): bind by CORE lab before FET merges.
    for (const core::Instance &inst : block.instances()) {
        if (inst.cellName().find("iopin") == std::string::npos) {
            continue;
        }
        const std::string lab = portLabelFromInstance(inst);
        if (lab.empty()) {
            continue;
        }
        QString instName;
        for (const core::Property &prop : inst.properties()) {
            if (prop.name == "name") {
                instName = QString::fromStdString(prop.value);
                break;
            }
        }
        if (instName.isEmpty()) {
            continue;
        }
        for (Component *pc : schematic->a_DocComps) {
            if (pc == nullptr || pc->Model != QLatin1String("Port") || pc->Name != instName
                || pc->Ports.isEmpty()) {
                continue;
            }
            Port *port = pc->Ports.first();
            if (port != nullptr && port->Connection != nullptr) {
                mergeNodeIntoNetAnchor(anchors, lab, port->Connection);
            }
        }
    }

    // Qucs Port components (CORE export): bind by Num/lab property when iopin name match missed.
    for (Component *pc : schematic->a_DocComps) {
        if (pc == nullptr || pc->Model != QLatin1String("Port") || pc->Props.isEmpty() || pc->Ports.isEmpty()) {
            continue;
        }
        const std::string lab = pc->Props.first()->Value.toStdString();
        if (lab.empty() || lab == "analog") {
            continue;
        }
        Port *port = pc->Ports.first();
        if (port != nullptr && port->Connection != nullptr) {
            mergeNodeIntoNetAnchor(anchors, lab, port->Connection);
        }
    }

    // Do not snap arbitrary CORE net terms here: Xschem-origin term coords often land on the
    // wrong Qucs node and collapse Gnd/Vdd/Vout.  Boundary ports (iopin) and FET roles below.

    for (const core::Instance &inst : block.instances()) {
        if (!core::isIhpFetModel(core::pinRetargetModelName(inst))) {
            continue;
        }

        QString instName;
        for (const core::Property &prop : inst.properties()) {
            if (prop.name == "name") {
                instName = QString::fromStdString(prop.value);
                break;
            }
        }
        if (instName.isEmpty()) {
            continue;
        }

        LibComp *lib = nullptr;
        for (Component *pc : schematic->a_DocComps) {
            if (pc != nullptr && pc->Model == QLatin1String("Lib") && pc->Name == instName) {
                lib = static_cast<LibComp *>(pc);
                break;
            }
        }
        if (lib == nullptr) {
            continue;
        }

        const std::vector<std::string> pinNets = ihpFetPinNetNames(inst, block, dbuPerEditorUnit);
        for (int portIdx = 0; portIdx < 4; ++portIdx) {
            if (portIdx >= lib->Ports.size() || portIdx >= static_cast<int>(pinNets.size())) {
                break;
            }
            const std::string &netName = pinNets[static_cast<std::size_t>(portIdx)];
            if (netName.empty()) {
                continue;
            }
            Port *port = lib->Ports.at(portIdx);
            if (port == nullptr || !port->avail || port->Connection == nullptr) {
                continue;
            }
            const auto anchorIt = anchors.find(canonNetKey(netName));
            if (anchorIt == anchors.end() || anchorIt->second == nullptr) {
                continue;
            }
            if (anchorIt->second != port->Connection) {
                anchorIt->second->merge(port->Connection);
            }
        }
    }

    // Boundary Port nodes are bound above via iopin.sym lab= in the CORE block (analogLib interop).
    // Props.Type is always "analog" — not a net name.
}

void repairPortNetNamesFromWires(Schematic *schematic)
{
    if (schematic == nullptr) {
        return;
    }
    for (Component *pc : schematic->a_DocComps) {
        if (pc == nullptr || pc->Model != QLatin1String("Port") || pc->Props.isEmpty()) {
            continue;
        }
        if (pc->Ports.isEmpty() || pc->Ports.first() == nullptr) {
            continue;
        }
        Node *node = pc->Ports.first()->Connection;
        if (node == nullptr) {
            continue;
        }
        if (node->hasLabel() && !node->label()->Name.isEmpty()) {
            pc->Props.at(0)->Value = node->label()->Name;
            continue;
        }
        for (Wire *wire : schematic->a_DocWires) {
            if (wire == nullptr || !wire->hasLabel()) {
                continue;
            }
            if (wire->Port1 == node || wire->Port2 == node) {
                pc->Props.at(0)->Value = wire->label()->Name;
                break;
            }
        }
    }
}

void propagateSchematicNetNames(Schematic *schematic)
{
    if (schematic == nullptr) {
        return;
    }

    repairPortNetNamesFromWires(schematic);

    QHash<Node *, QString> namedNodes;
    for (Component *pc : schematic->a_DocComps) {
        if (pc == nullptr) {
            continue;
        }
        if (pc->Model == QLatin1String("Port") && !pc->Props.isEmpty()) {
            const QString portName = pc->Props.at(0)->Value.trimmed();
            if (!portName.isEmpty() && !portName.startsWith(QLatin1String("net"))) {
                if (!pc->Ports.isEmpty() && pc->Ports.first() != nullptr && pc->Ports.first()->Connection != nullptr) {
                    namedNodes.insert(pc->Ports.first()->Connection, portName);
                }
            }
            continue;
        }
        if ((pc->Model == QLatin1String("Vdc") || pc->Model == QLatin1String("Vpulse")) && !pc->Ports.isEmpty()
            && pc->Ports.first() != nullptr && pc->Ports.first()->Connection != nullptr) {
            namedNodes.insert(pc->Ports.first()->Connection, pc->Name);
        }
    }

    auto wireLabelPosition = [](const Wire *wire, int &rootX, int &rootY, int &textX, int &textY) {
        const int midX = (wire->x1 + wire->x2) / 2;
        const int midY = (wire->y1 + wire->y2) / 2;
        textX = midX + 10;
        textY = midY - 10;
        if (wire->y1 == wire->y2) {
            rootX = midX;
            rootY = wire->y1;
            textX = midX + 10;
            textY = wire->y1 - 10;
        } else if (wire->x1 == wire->x2) {
            rootX = wire->x1;
            rootY = midY;
            textX = wire->x1 + 10;
            textY = midY;
        } else {
            rootX = midX;
            rootY = midY;
        }
    };

    for (Wire *wire : schematic->a_DocWires) {
        if (wire == nullptr) {
            continue;
        }
        QString wireName;
        if (wire->hasLabel()) {
            wireName = wire->label()->Name;
        }
        if (wireName.isEmpty() || wireName.startsWith(QLatin1String("net"))) {
            if (wire->Port1 != nullptr && namedNodes.contains(wire->Port1)) {
                wireName = namedNodes.value(wire->Port1);
            } else if (wire->Port2 != nullptr && namedNodes.contains(wire->Port2)) {
                wireName = namedNodes.value(wire->Port2);
            }
        }
        if (wireName.isEmpty()) {
            continue;
        }
        if (!wire->hasLabel()) {
            int rootX = 0;
            int rootY = 0;
            int textX = 0;
            int textY = 0;
            wireLabelPosition(wire, rootX, rootY, textX, textY);
            wire->setName(wireName, QString(), rootX, rootY, textX, textY);
        } else if (wire->label()->Name.startsWith(QLatin1String("net"))) {
            wire->label()->Name = wireName;
        }
    }
}

} // namespace

void repairCoreSchematicConnectivity(Schematic *schematic)
{
    repairIsolatedCorePortNodes(schematic);
    propagateSchematicNetNames(schematic);
}

IoResult loadCoreFileDirect(const QString &corePath, Schematic *schematic)
{
    IoResult result;
    if (schematic == nullptr) {
        result.message = QObject::tr("No schematic document to load into.");
        return result;
    }

    try {
        const core::Database db = core::Database::loadFromFile(corePath.toStdString());
        const QString cellName = cellNameFromCorePath(corePath);
        if (cellName.isEmpty()) {
            result.message = QObject::tr("Failed to determine cell name from CORE file.");
            return result;
        }

        const core::Cell *cell = db.lib().findCell(cellName.toStdString());
        if (cell == nullptr) {
            result.message = QObject::tr("CORE file contains no matching cell.");
            return result;
        }

        const core::ViewType viewType = isCoreSymbolPath(corePath) ? core::ViewType::Symbol : core::ViewType::Schematic;
        const core::CellContent *content = cell->findContent(viewType);
        if (content == nullptr) {
            result.message = QObject::tr("CORE file has no view data for this document.");
            return result;
        }

        if (!isCoreSymbolPath(corePath)) {
            QString schText;
            const IoResult exported = exportCoreViewToString(corePath, schText);
            if (!exported.ok) {
                result.message = exported.message;
                return result;
            }
            schematic->setDocName(corePath);
            if (!schematic->loadDocumentFromText(schText)) {
                result.message = QObject::tr("Failed to load CORE schematic via Qucs export.");
                return result;
            }
            repairCoreSchematicConnectivity(schematic);
            schematic->setFileInfo(corePath);
            schematic->setName(corePath);
            schematic->setCoreCoordDivisor(1);
            result.ok = true;
            return result;
        }

        const double dbuPerEditorUnit = content->dbuPerEditorUnit();
        const std::string sourceFormat = sourceFormatFromContent(*content);
        core::QucsExporter exporter(exporterOptionsFromEnvironment());

        const qint64 coordDivisor = isCoreSymbolPath(corePath)
                                        ? 1
                                        : core::qucs_codec::computeDisplayDivisor(content->block(), content->layers(),
                                                                                   dbuPerEditorUnit);

        g_coreBridgeActive = true;

        if (!loadPropertySection(schematic, content->properties(), "schematic.view", QStringLiteral("<Properties>"),
                                 [&](QTextStream *stream) { return schematic->loadCoreProperties(stream); })) {
            g_coreBridgeActive = false;
            result.message = QObject::tr("Failed to load CORE schematic properties.");
            return result;
        }

        std::vector<std::string> symbolLines =
            core::qucs_codec::collectProperties(content->properties(), "section.Symbol");
        if (symbolLines.empty() && isCoreSymbolPath(corePath)) {
            symbolLines = exporter.symbolLinesForCell(db, cellName.toStdString());
            for (const std::string &warning : exporter.warnings()) {
                qWarning() << "CORE symbol generation warning:" << QString::fromStdString(warning);
            }
            if (!exporter.errors().empty()) {
                g_coreBridgeActive = false;
                result.message = QString::fromStdString(exporter.errors().front());
                return result;
            }
        }
        if (!symbolLines.empty()) {
            QString buffer = QStringLiteral("<Symbol>\n");
            for (const std::string &line : symbolLines) {
                if (!line.empty() && line.front() == '<' && line.back() == '>') {
                    buffer += QStringLiteral("  ") + QString::fromStdString(line) + QStringLiteral("\n");
                } else {
                    buffer += QStringLiteral("  <") + QString::fromStdString(line) + QStringLiteral(">\n");
                }
            }
            buffer += QStringLiteral("</Symbol>\n");
            QTextStream stream(&buffer, QIODevice::ReadOnly);
            stream.readLine();
            if (!schematic->loadCorePaintings(&stream, &schematic->a_SymbolPaints)) {
                g_coreBridgeActive = false;
                result.message = QObject::tr("Failed to load CORE symbol section.");
                return result;
            }
        }

        g_coreBridgeActive = false;
        schematic->setFileInfo(corePath);
        schematic->setName(corePath);
        schematic->setCoreCoordDivisor(coordDivisor);

        for (const std::string &warning : exporter.warnings()) {
            qWarning() << "CORE direct load warning:" << QString::fromStdString(warning);
        }

        result.ok = true;
        return result;
    } catch (const std::exception &ex) {
        g_coreBridgeActive = false;
        result.message = QString::fromStdString(ex.what());
        return result;
    }
}

IoResult saveSchematicToCoreFileDirect(Schematic *schematic, const QString &corePath)
{
    IoResult result;
    if (schematic == nullptr) {
        result.message = QObject::tr("No schematic document to save.");
        return result;
    }

    try {
        const QString cellName = cellNameFromCorePath(corePath);

        core::QucsImporter importer(importerOptionsFromCorePath(corePath));
        const core::ViewType viewType = isCoreSymbolPath(corePath) ? core::ViewType::Symbol : core::ViewType::Schematic;

        core::Block block;
        const double dbuPerEditorUnit = isCoreSymbolPath(corePath) ? core::kXschemDbuPerEditorUnit : core::kQucsDbuPerEditorUnit;
        std::string sourceFormat;
        const core::CellContent *existingContent = nullptr;
        std::unordered_map<std::string, std::vector<core::Property>> existingInstProps;
        if (QFileInfo::exists(corePath)) {
            const core::Database existingDb = core::Database::loadFromFile(corePath.toStdString());
            if (const core::Cell *existingCell = existingDb.lib().findCell(cellName.toStdString())) {
                existingContent = existingCell->findContent(viewType);
                if (existingContent != nullptr) {
                    sourceFormat = sourceFormatFromContent(*existingContent);
                    for (const core::Instance &inst : existingContent->block().instances()) {
                        if (const core::Property *name = findInstanceProperty(inst.properties(), "name")) {
                            existingInstProps[name->value] = inst.properties();
                        }
                    }
                }
            }
        }

        g_coreBridgeActive = true;

        if (!isCoreSymbolPath(corePath)) {
            repairPortNetNamesFromWires(schematic);
            for (Component *component : schematic->a_DocComps) {
                const std::string line = component->save().toStdString();
                std::string scaled = schematic->coreCoordDivisor() > 1
                                         ? core::qucs_codec::scaleComponentLineCoordinates(line, schematic->coreCoordDivisor(),
                                                                                           true)
                                         : line;
                block.instances().push_back(instanceFromEditorLine(scaled, dbuPerEditorUnit, importer));
            }

            std::vector<std::string> wireLines;
            for (Wire *wire : schematic->a_DocWires) {
                std::string line = wire->save().toStdString();
                if (schematic->coreCoordDivisor() > 1) {
                    line = core::qucs_codec::scaleWireLineCoordinates(line, schematic->coreCoordDivisor(), true);
                }
                wireLines.push_back(line);
            }
            for (Node *node : schematic->a_DocNodes) {
                if (!node->hasLabel()) {
                    continue;
                }
                std::string line = node->label()->save().toStdString();
                if (schematic->coreCoordDivisor() > 1) {
                    line = core::qucs_codec::scaleWireLineCoordinates(line, schematic->coreCoordDivisor(), true);
                }
                wireLines.push_back(line);
            }
            importer.importWireLines(block, wireLines, dbuPerEditorUnit);

            for (core::Instance &inst : block.instances()) {
                if (const core::Property *name = findInstanceProperty(inst.properties(), "name")) {
                    const auto it = existingInstProps.find(name->value);
                    if (it != existingInstProps.end()) {
                        mergeInstanceMetadata(inst, it->second);
                    }
                }
            }
            core::xschem::annotateBlockForStorage(block);
        }

        QString schText;
        schematic->saveDocumentToText(schText);
        if (schematic->coreCoordDivisor() > 1) {
            qucs_core::denormalizeSchCoordinatesInMemory(schText, schematic->coreCoordDivisor());
        }
        const std::string schUtf8 = schText.toUtf8().constData();
        core::Database mergedDb = importer.importText(schUtf8, cellName.toStdString());
        core::Cell &mergedCell = mergedDb.lib().getOrCreateCell(cellName.toStdString());
        core::CellContent &mergedContent = mergedCell.getOrCreateContent(viewType, dbuPerEditorUnit);
        mergedContent.block() = std::move(block);
        mergedContent.setDbuPerEditorUnit(dbuPerEditorUnit);
        mergedContent.setDbuPerMicron(dbuPerEditorUnit);
        const std::vector<std::string> diagramLines = [&]() {
            std::vector<std::string> lines = diagramLinesFromSchText(schUtf8);
            if (!lines.empty()) {
                return lines;
            }
            for (Diagram *pd : schematic->a_DocDiags) {
                if (pd == nullptr) {
                    continue;
                }
                const std::string line = pd->save().toStdString();
                if (!line.empty()) {
                    lines.push_back(line);
                }
            }
            return lines;
        }();
        if (!diagramLines.empty()) {
            core::xschem::replaceSectionLines(mergedContent.properties(), "section.Diagrams", diagramLines);
        } else if (existingContent != nullptr) {
            std::vector<std::string> existingDiagrams;
            for (const core::Property &prop : existingContent->properties()) {
                if (prop.name == "section.Diagrams") {
                    existingDiagrams.push_back(prop.value);
                }
            }
            if (!existingDiagrams.empty()) {
                core::xschem::replaceSectionLines(mergedContent.properties(), "section.Diagrams", existingDiagrams);
            } else {
                core::xschem::copyAllSectionLinesIfMissing(mergedContent.properties(), existingContent->properties(),
                                                            "section.Diagrams");
            }
        }
        if (existingContent != nullptr) {
            mergePreservedSchematicProperties(mergedContent.properties(), existingContent->properties());
        }
        core::xschem::removeInvalidGraphProperties(mergedContent.properties());
        // Qucs save: diagram geometry is authoritative — rewrite section.graph to match.
        core::xschem::syncDualToolGraphProperties(mergedContent.block(), mergedContent,
                                                  core::xschem::GraphSyncDirection::FromQucsDiagram);
        removePropertiesWithName(mergedContent.properties(), core::kSourceFormatKey);
        removePropertiesWithName(mergedContent.properties(), core::kSourceToolVersionKey);
        removePropertiesWithName(mergedContent.properties(), core::kSourceFileVersionKey);
        removePropertiesWithName(mergedContent.properties(), core::kSourceCommentsKey);
        mergedContent.sourceInfo().setFormat("qucs_s");
        mergedContent.sourceInfo().setToolVersion(PACKAGE_VERSION);
        {
            core::PrimitiveResolver resolver;
            resolver.loadFromEnvironment();
            core::canonicalizeBlockPrimitives(mergedContent.block(), &resolver);
            core::propagateNetNames(mergedContent.block(), &resolver, dbuPerEditorUnit);
        }
        Q_UNUSED(sourceFormat);
        g_coreBridgeActive = false;

        const core::ParsedCorePath parsed = core::parseCoreFilePath(corePath.toStdString());
        const core::ViewType saveView =
            parsed.valid ? parsed.view : isCoreSymbolPath(corePath) ? core::ViewType::Symbol : core::ViewType::Schematic;

        mergedDb.setGenerator("CORE qucs_s");
        mergedDb.setTechnology("qucs");
        mergedDb.saveToFile(corePath.toStdString(), saveView);

        if (!QFileInfo::exists(corePath)) {
            result.message = QObject::tr("CORE save did not create an output file.");
            return result;
        }

        for (const std::string &warning : importer.warnings()) {
            qWarning() << "CORE direct save warning:" << QString::fromStdString(warning);
        }

        result.ok = true;
        return result;
    } catch (const std::exception &ex) {
        g_coreBridgeActive = false;
        result.message = QString::fromStdString(ex.what());
        return result;
    }
}

} // namespace qucs_core
