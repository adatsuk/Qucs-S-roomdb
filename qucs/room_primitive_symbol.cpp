#include "room_primitive_symbol.h"

#include "room_schematic_io.h"
#include "database.h"
#include "primitive_resolver.h"
#include "qucs_exporter.h"

#include <QDir>
#include <QFileInfo>

#include "room_schematic_io.h"
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

} // namespace

bool tryLoadRoomCellSymbol(const QString &libName, const QString &cellName, QString &symbolSection,
                           const QString &hintDocPath)
{
    symbolSection.clear();
    if (cellName.trimmed().isEmpty()) {
        return false;
    }

    QString schematicPath;
    if (!tryResolveRoomSchematic(libName, cellName, schematicPath, hintDocPath)) {
        return false;
    }

    QString symbolPath = schematicPath;
    symbolPath.replace(QStringLiteral(".schematic.room"), QStringLiteral(".symbol.room"), Qt::CaseInsensitive);
    if (!QFileInfo::exists(symbolPath)) {
        return false;
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

bool tryResolveRoomSchematic(const QString &libName, const QString &compName, QString &schematicRoomPath,
                             const QString &hintDocPath)
{
    schematicRoomPath.clear();
    if (compName.trimmed().isEmpty()) {
        return false;
    }

    auto acceptIfExists = [&](QString path) -> bool {
        path = QDir::cleanPath(path);
        if (!path.endsWith(QStringLiteral(".schematic.room"), Qt::CaseInsensitive)) {
            if (path.endsWith(QStringLiteral(".symbol.room"), Qt::CaseInsensitive)) {
                path.replace(QStringLiteral(".symbol.room"), QStringLiteral(".schematic.room"));
            } else if (path.endsWith(QStringLiteral(".sym.room"), Qt::CaseInsensitive)) {
                path.replace(QStringLiteral(".sym.room"), QStringLiteral(".schematic.room"));
            }
        }
        if (QFileInfo::exists(path)) {
            schematicRoomPath = QDir::toNativeSeparators(path);
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
    if (resolved.found && !resolved.roomPath.empty() && acceptIfExists(QString::fromStdString(resolved.roomPath))) {
        return true;
    }

    // Sibling-cell fallback: .../lib/inverter_tb/foo.room → .../lib/inverter/inverter.schematic.room
    if (!hintDocPath.trimmed().isEmpty()) {
        QDir cellDir = QFileInfo(hintDocPath).absoluteDir();
        const QString cell = compName.trimmed();
        if (acceptIfExists(cellDir.filePath(cell + QLatin1Char('/') + cell + QStringLiteral(".schematic.room")))) {
            return true;
        }
        if (cellDir.cdUp()) {
            if (acceptIfExists(cellDir.filePath(cell + QLatin1Char('/') + cell + QStringLiteral(".schematic.room")))) {
                return true;
            }
            if (!libName.trimmed().isEmpty() && cellDir.dirName().compare(libName.trimmed(), Qt::CaseInsensitive) != 0) {
                // hint may be deeper; try libName/cell under parent chain once more
                if (cellDir.cdUp()
                    && acceptIfExists(cellDir.filePath(libName.trimmed() + QLatin1Char('/') + cell + QLatin1Char('/')
                                                       + cell + QStringLiteral(".schematic.room")))) {
                    return true;
                }
            }
        }
    }

    return false;
}

} // namespace qucs_room
