#include "core_block_mapper.h"

#include "cell_content.h"
#include "coord_scale.h"
#include "core_paths.h"
#include "database.h"
#include "qucs_block_codec.h"
#include "qucs_exporter.h"
#include "qucs_importer.h"
#include "primitive_resolver.h"

#include "components/component.h"
#include "node.h"
#include "schematic.h"
#include "wire.h"

#include <QDebug>
#include <QFileInfo>
#include <QMessageBox>
#include <QRegularExpression>
#include <QTextStream>

#include <functional>

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

} // namespace

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

        if (!isCoreSymbolPath(corePath)) {
            for (const core::Instance &inst : content->block().instances()) {
                if (core::isQucsSchematicDecoration(inst.cellName())) {
                    continue;
                }
                std::string line = exporter.exportInstanceAsLine(inst, dbuPerEditorUnit, sourceFormat);
                if (coordDivisor > 1) {
                    line = core::qucs_codec::scaleComponentLineCoordinates(line, coordDivisor, false);
                }
                QString qline = QString::fromStdString(line);
                Component *component = getComponentFromName(qline, schematic);
                if (component == nullptr) {
                    g_coreBridgeActive = false;
                    const QString compType = qline.section(QLatin1Char(' '), 0, 0).mid(1);
                    result.message = QObject::tr("Failed to instantiate CORE component: %1").arg(compType);
                    return result;
                }
                schematic->insertCoreComponent(component);
            }

            for (const std::string &line :
                 exporter.exportBlockWiresAsLines(content->block(), content->layers(), dbuPerEditorUnit, sourceFormat)) {
                std::string scaled = coordDivisor > 1
                                         ? core::qucs_codec::scaleWireLineCoordinates(line, coordDivisor, false)
                                         : line;
                Wire *wire = new Wire();
                if (!wire->load(QString::fromStdString(scaled))) {
                    delete wire;
                    g_coreBridgeActive = false;
                    result.message = QObject::tr("Failed to load CORE wire.");
                    return result;
                }
                schematic->insertCoreWire(wire);
            }

            if (!loadPropertySection(schematic, content->properties(), "section.Diagrams", QStringLiteral("<Diagrams>"),
                                     [&](QTextStream *stream) { return schematic->loadCoreDiagrams(stream); })) {
                g_coreBridgeActive = false;
                result.message = QObject::tr("Failed to load CORE diagrams.");
                return result;
            }

            if (!loadPropertySection(schematic, content->properties(), "section.Paintings", QStringLiteral("<Paintings>"),
                                     [&](QTextStream *stream) {
                                         return schematic->loadCorePaintings(stream, &schematic->a_DocPaints);
                                     })) {
                g_coreBridgeActive = false;
                result.message = QObject::tr("Failed to load CORE paintings.");
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
        if (QFileInfo::exists(corePath)) {
            const core::Database existingDb = core::Database::loadFromFile(corePath.toStdString());
            if (const core::Cell *existingCell = existingDb.lib().findCell(cellName.toStdString())) {
                if (const core::CellContent *existingContent = existingCell->findContent(viewType)) {
                    sourceFormat = sourceFormatFromContent(*existingContent);
                }
            }
        }

        g_coreBridgeActive = true;

        if (!isCoreSymbolPath(corePath)) {
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
        }

        QString schText;
        schematic->saveDocumentToText(schText);
        if (schematic->coreCoordDivisor() > 1) {
            qucs_core::denormalizeSchCoordinatesInMemory(schText, schematic->coreCoordDivisor());
        }
        core::Database mergedDb = importer.importText(schText.toStdString(), cellName.toStdString());
        core::Cell &mergedCell = mergedDb.lib().getOrCreateCell(cellName.toStdString());
        core::CellContent &mergedContent = mergedCell.getOrCreateContent(viewType, dbuPerEditorUnit);
        mergedContent.block() = std::move(block);
        mergedContent.setDbuPerEditorUnit(dbuPerEditorUnit);
        mergedContent.setDbuPerMicron(dbuPerEditorUnit);
        if (!sourceFormat.empty()) {
            mergedContent.sourceInfo().setFormat(sourceFormat);
        }
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
