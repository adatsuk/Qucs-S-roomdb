#include "room_primitive_symbol.h"

#include "room_schematic_io.h"
#include "database.h"
#include "enums.h"
#include "primitive_resolver.h"
#include "property.h"
#include "qucs_exporter.h"

#include <QDir>
#include <QFileInfo>

#include <QProcessEnvironment>
#include <QRegularExpression>

namespace qucs_room {
namespace {

room::QucsExporter::Options exporterOptionsFromEnvironment()
{
    room::QucsExporter::Options options;
    if (qEnvironmentVariableIsSet("LIBMAN_TECH_LIBRARY")) {
        options.techLibrary = qEnvironmentVariable("LIBMAN_TECH_LIBRARY").toStdString();
    }
    if (qEnvironmentVariableIsSet("QUCS_PRIMITIVE_LIB")) {
        options.qucsPrimitiveLib = qEnvironmentVariable("QUCS_PRIMITIVE_LIB").toStdString();
    }
    for (const std::string &path : room::PrimitiveResolver::primitiveCorePathsFromEnvironment()) {
        options.primitiveCorePaths.push_back(path);
    }
    return options;
}

room::PrimitiveResolver buildResolver(const room::QucsExporter::Options &options)
{
    room::PrimitiveResolver resolver;
    resolver.setTechLibrary(options.techLibrary);
    if (!options.qucsPrimitiveLib.empty()) {
        resolver.setQucsLibrary(options.qucsPrimitiveLib);
    }
    for (const std::string &path : options.primitiveCorePaths) {
        resolver.addCorePath(path);
    }
    if (options.primitiveCorePaths.empty()) {
        resolver.loadFromEnvironment();
    }
    return resolver;
}

std::vector<std::string> candidateRefs(const QString &compName, const std::string &techLibrary)
{
    const std::string comp = compName.toStdString();
    std::vector<std::string> refs = {comp, comp + ".sym"};
    const QStringList techs =
        QString::fromStdString(techLibrary)
            .split(QRegularExpression(QStringLiteral("[;:]")), Qt::SkipEmptyParts);
    for (const QString &tech : techs) {
        const QString t = tech.trimmed();
        if (t.isEmpty()) {
            continue;
        }
        const std::string techStd = t.toStdString();
        refs.push_back(techStd + "/" + comp);
        refs.push_back(techStd + "/" + comp + ".sym");
    }
    return refs;
}

QString normalizeViewName(const QString &viewName)
{
    const QString lower = viewName.trimmed().toLower();
    if (lower == QStringLiteral("sch")) {
        return QStringLiteral("schematic");
    }
    if (lower == QStringLiteral("sym")) {
        return QStringLiteral("symbol");
    }
    if (lower == QStringLiteral("em_model")) {
        return QStringLiteral("emmodel");
    }
    return lower;
}

QString viewFileSuffix(const QString &viewName)
{
    return QStringLiteral(".") + normalizeViewName(viewName) + QStringLiteral(".room");
}

QString rewriteRoomViewSuffix(QString path, const QString &targetView)
{
    path = QDir::cleanPath(path);
    const QString target = viewFileSuffix(targetView);
    static const QStringList kKnown = {
        QStringLiteral(".schematic.room"), QStringLiteral(".symbol.room"),
        QStringLiteral(".sym.room"),       QStringLiteral(".emmodel.room"),
        QStringLiteral(".layout.room"),    QStringLiteral(".emsetup.room"),
    };
    for (const QString &suf : kKnown) {
        if (path.endsWith(suf, Qt::CaseInsensitive)) {
            path.chop(suf.size());
            return path + target;
        }
    }
    if (path.endsWith(QStringLiteral(".room"), Qt::CaseInsensitive)) {
        // Bare / unknown *.room — append is unsafe; leave unchanged.
        return path;
    }
    return path + target;
}

bool contentHasLookalike(const room::CellContent &content)
{
    for (const room::Property &p : content.properties()) {
        if (p.name == "em.lookalike"
            && (p.value == "1" || p.value == "true" || p.value == "yes")) {
            return true;
        }
    }
    return false;
}

QString resolveSnpPath(const QString &emModelPath, const QString &snpRaw)
{
    if (snpRaw.trimmed().isEmpty()) {
        return {};
    }
    QFileInfo fi(snpRaw);
    if (fi.isAbsolute()) {
        return QDir::toNativeSeparators(fi.absoluteFilePath());
    }
    const QDir base = QFileInfo(emModelPath).absoluteDir();
    return QDir::toNativeSeparators(QFileInfo(base, snpRaw).absoluteFilePath());
}

int portsFromSnpSuffix(const QString &snpPath)
{
    const QString ext = QFileInfo(snpPath).suffix().toLower();
    if (!ext.startsWith(QLatin1Char('s')) || !ext.endsWith(QLatin1Char('p')) || ext.size() < 3) {
        return 0;
    }
    bool ok = false;
    const int n = ext.mid(1, ext.size() - 2).toInt(&ok);
    return (ok && n >= 1) ? n : 0;
}

} // namespace

bool tryResolveRoomView(const QString &libName, const QString &compName, const QString &viewName,
                        QString &roomPath, const QString &hintDocPath)
{
    roomPath.clear();
    if (compName.trimmed().isEmpty() || viewName.trimmed().isEmpty()) {
        return false;
    }

    const QString view = normalizeViewName(viewName);
    const QString suffix = viewFileSuffix(view);

    auto acceptIfExists = [&](QString path) -> bool {
        path = rewriteRoomViewSuffix(path, view);
        path = QDir::cleanPath(path);
        if (!path.endsWith(suffix, Qt::CaseInsensitive)) {
            return false;
        }
        if (QFileInfo::exists(path)) {
            roomPath = QDir::toNativeSeparators(path);
            return true;
        }
        return false;
    };

    const room::QucsExporter::Options options = exporterOptionsFromEnvironment();
    const room::PrimitiveResolver resolver = buildResolver(options);

    std::vector<std::string> refs = candidateRefs(compName, options.techLibrary);
    if (!libName.trimmed().isEmpty()) {
        const std::string lib = libName.trimmed().toStdString();
        const std::string cell = compName.trimmed().toStdString();
        refs.insert(refs.begin(), {lib + "/" + cell, lib + "/" + cell + ".sym", cell, cell + ".sym"});
    }

    room::ResolvedPrimitive resolved;
    for (const std::string &ref : refs) {
        resolved = resolver.resolveReference(ref);
        if (resolved.found) {
            break;
        }
    }
    // Resolver indexes Symbol views; rewrite to the requested view beside the same cell dir.
    if (resolved.found && !resolved.roomPath.empty()
        && acceptIfExists(QString::fromStdString(resolved.roomPath))) {
        return true;
    }

    // Sibling-cell fallback: .../lib/inverter_tb/foo.room → .../lib/inverter/inverter.<view>.room
    if (!hintDocPath.trimmed().isEmpty()) {
        QDir cellDir = QFileInfo(hintDocPath).absoluteDir();
        const QString cell = compName.trimmed();
        if (acceptIfExists(cellDir.filePath(cell + QLatin1Char('/') + cell + suffix))) {
            return true;
        }
        if (cellDir.cdUp()) {
            if (acceptIfExists(cellDir.filePath(cell + QLatin1Char('/') + cell + suffix))) {
                return true;
            }
            if (!libName.trimmed().isEmpty()
                && cellDir.dirName().compare(libName.trimmed(), Qt::CaseInsensitive) != 0) {
                if (cellDir.cdUp()
                    && acceptIfExists(cellDir.filePath(libName.trimmed() + QLatin1Char('/') + cell
                                                       + QLatin1Char('/') + cell + suffix))) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool tryResolveRoomSchematic(const QString &libName, const QString &compName, QString &schematicRoomPath,
                             const QString &hintDocPath)
{
    return tryResolveRoomView(libName, compName, QStringLiteral("schematic"), schematicRoomPath,
                              hintDocPath);
}

bool tryResolveRoomSymbol(const QString &libName, const QString &compName, QString &symbolRoomPath,
                          const QString &hintDocPath)
{
    return tryResolveRoomView(libName, compName, QStringLiteral("symbol"), symbolRoomPath, hintDocPath);
}

bool tryResolveRoomEmModel(const QString &libName, const QString &compName, QString &emModelRoomPath,
                           const QString &hintDocPath)
{
    return tryResolveRoomView(libName, compName, QStringLiteral("emmodel"), emModelRoomPath, hintDocPath);
}

bool tryLoadRoomCellSymbol(const QString &libName, const QString &cellName, QString &symbolSection,
                           const QString &hintDocPath)
{
    symbolSection.clear();
    if (cellName.trimmed().isEmpty()) {
        return false;
    }

    QString symbolPath;
    if (!tryResolveRoomSymbol(libName, cellName, symbolPath, hintDocPath)) {
        // Legacy: derive from schematic path when symbol resolve failed.
        QString schematicPath;
        if (!tryResolveRoomSchematic(libName, cellName, schematicPath, hintDocPath)) {
            return false;
        }
        symbolPath = rewriteRoomViewSuffix(schematicPath, QStringLiteral("symbol"));
        if (!QFileInfo::exists(symbolPath)) {
            return false;
        }
    }

    try {
        const room::Database db = room::Database::loadFromFile(symbolPath.toStdString());
        const QString exportCell = cellNameFromRoomPath(symbolPath);
        if (exportCell.isEmpty()) {
            return false;
        }
        const room::QucsExporter exporter(exporterOptionsFromEnvironment());
        const std::vector<std::string> lines = exporter.symbolLinesForCell(db, exportCell.toStdString());
        if (lines.empty() || !exporter.errors().empty()) {
            return false;
        }

        QStringList out;
        out.reserve(static_cast<int>(lines.size()));
        for (const std::string &line : lines) {
            QString qline = QString::fromStdString(line).trimmed();
            if (qline.isEmpty()) {
                continue;
            }
            if (!qline.startsWith(QLatin1Char('<')) && !qline.endsWith(QLatin1Char('>'))) {
                qline = QStringLiteral("<") + qline + QStringLiteral(">");
            }
            out.append(qline);
        }
        symbolSection = out.join(QStringLiteral("\n"));
        return !symbolSection.trimmed().isEmpty();
    } catch (...) {
        return false;
    }
}

bool tryLoadRoomPrimitiveSymbol(const QString &compName, QString &symbolSection)
{
    if (compName.trimmed().isEmpty()) {
        return false;
    }

    const room::QucsExporter::Options options = exporterOptionsFromEnvironment();
    const room::PrimitiveResolver resolver = buildResolver(options);

    room::ResolvedPrimitive resolved;
    for (const std::string &ref : candidateRefs(compName, options.techLibrary)) {
        resolved = resolver.resolveReference(ref);
        if (resolved.found) {
            break;
        }
    }
    if (!resolved.found) {
        return false;
    }

    try {
        const room::Database db = room::Database::loadFromFile(resolved.roomPath);
        const room::QucsExporter exporter(options);
        const std::vector<std::string> lines = exporter.symbolLinesForCell(db, resolved.cellName);
        if (lines.empty() || !exporter.errors().empty()) {
            return false;
        }

        QStringList out;
        out.reserve(static_cast<int>(lines.size()));
        for (const std::string &line : lines) {
            QString qline = QString::fromStdString(line).trimmed();
            if (qline.isEmpty()) {
                continue;
            }
            if (!qline.startsWith(QLatin1Char('<')) && !qline.endsWith(QLatin1Char('>'))) {
                qline = QStringLiteral("<") + qline + QStringLiteral(">");
            }
            out.append(qline);
        }
        symbolSection = out.join(QStringLiteral("\n"));
        return !symbolSection.trimmed().isEmpty();
    } catch (...) {
        return false;
    }
}

bool roomCellSymbolIsLookalike(const QString &libName, const QString &cellName,
                               const QString &hintDocPath)
{
    QString symbolPath;
    if (!tryResolveRoomSymbol(libName, cellName, symbolPath, hintDocPath)) {
        return false;
    }
    try {
        const room::Database db = room::Database::loadFromFile(symbolPath.toStdString());
        const std::string cellStd = cellName.trimmed().toStdString();
        const room::Cell *cell = db.lib().findCell(cellStd);
        if (!cell && !db.fileSummary().primaryCell.empty()) {
            cell = db.lib().findCell(db.fileSummary().primaryCell);
        }
        if (!cell) {
            return false;
        }
        const room::CellContent *content = cell->findContent(room::ViewType::Symbol);
        if (!content) {
            return false;
        }
        return contentHasLookalike(*content);
    } catch (...) {
        return false;
    }
}

bool tryLoadRoomEmModelNetlist(const QString &libName, const QString &cellName,
                               RoomEmModelNetlistInfo &out, const QString &hintDocPath)
{
    out = RoomEmModelNetlistInfo{};
    if (cellName.trimmed().isEmpty()) {
        return false;
    }

    out.lookalike = roomCellSymbolIsLookalike(libName, cellName, hintDocPath);

    QString emPath;
    if (!tryResolveRoomEmModel(libName, cellName, emPath, hintDocPath)) {
        return false;
    }

    try {
        const room::Database db = room::Database::loadFromFile(emPath.toStdString());
        const std::string cellStd = cellName.trimmed().toStdString();
        const room::Cell *cell = db.lib().findCell(cellStd);
        if (!cell && !db.fileSummary().primaryCell.empty()) {
            cell = db.lib().findCell(db.fileSummary().primaryCell);
        }
        if (!cell) {
            return false;
        }
        const room::CellContent *content = cell->findContent(room::ViewType::EmModel);
        if (!content || !content->hasEmModelPayload()) {
            return false;
        }
        const room::EmModelViewData &model = content->emModel();
        const QString snp = resolveSnpPath(emPath, QString::fromStdString(model.snpPath));
        if (snp.isEmpty() || !QFileInfo::exists(snp)) {
            return false;
        }

        out.emModelPath = emPath;
        out.snpPath = snp;
        out.z0 = model.z0 > 0.0 ? model.z0 : 50.0;
        out.portCount = static_cast<int>(model.ports.size());
        if (out.portCount <= 0) {
            out.portCount = portsFromSnpSuffix(snp);
        }
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace qucs_room
