#include "core_primitive_symbol.h"

#include "core_schematic_io.h"
#include "database.h"
#include "primitive_resolver.h"
#include "qucs_exporter.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>

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

core::PrimitiveResolver buildResolver(const core::QucsExporter::Options &options)
{
    core::PrimitiveResolver resolver;
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

bool tryLoadCorePrimitiveSymbol(const QString &compName, QString &symbolSection)
{
    if (compName.trimmed().isEmpty()) {
        return false;
    }

    const core::QucsExporter::Options options = exporterOptionsFromEnvironment();
    const core::PrimitiveResolver resolver = buildResolver(options);

    core::ResolvedPrimitive resolved;
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
        const core::Database db = core::Database::loadFromFile(resolved.corePath);
        const core::QucsExporter exporter(options);
        const std::vector<std::string> lines = exporter.symbolLinesForCell(db, resolved.cellName);
        if (lines.empty() || !exporter.errors().empty()) {
            return false;
        }

        QStringList out;
        out.reserve(static_cast<int>(lines.size()));
        for (const std::string &line : lines) {
            out.append(QString::fromStdString(line));
        }
        symbolSection = out.join(QStringLiteral("\n"));
        return !symbolSection.trimmed().isEmpty();
    } catch (...) {
        return false;
    }
}

bool tryResolveCoreSchematic(const QString &libName, const QString &compName, QString &schematicCorePath,
                             const QString &hintDocPath)
{
    schematicCorePath.clear();
    if (compName.trimmed().isEmpty()) {
        return false;
    }

    auto acceptIfExists = [&](QString path) -> bool {
        path = QDir::cleanPath(path);
        if (!path.endsWith(QStringLiteral(".schematic.core"), Qt::CaseInsensitive)) {
            if (path.endsWith(QStringLiteral(".symbol.core"), Qt::CaseInsensitive)) {
                path.replace(QStringLiteral(".symbol.core"), QStringLiteral(".schematic.core"));
            } else if (path.endsWith(QStringLiteral(".sym.core"), Qt::CaseInsensitive)) {
                path.replace(QStringLiteral(".sym.core"), QStringLiteral(".schematic.core"));
            }
        }
        if (QFileInfo::exists(path)) {
            schematicCorePath = QDir::toNativeSeparators(path);
            return true;
        }
        return false;
    };

    const core::QucsExporter::Options options = exporterOptionsFromEnvironment();
    const core::PrimitiveResolver resolver = buildResolver(options);

    std::vector<std::string> refs = candidateRefs(compName, options.techLibrary);
    if (!libName.trimmed().isEmpty()) {
        const std::string lib = libName.trimmed().toStdString();
        const std::string cell = compName.trimmed().toStdString();
        refs.insert(refs.begin(), {lib + "/" + cell, lib + "/" + cell + ".sym", cell, cell + ".sym"});
    }

    core::ResolvedPrimitive resolved;
    for (const std::string &ref : refs) {
        resolved = resolver.resolveReference(ref);
        if (resolved.found) {
            break;
        }
    }
    if (resolved.found && !resolved.corePath.empty() && acceptIfExists(QString::fromStdString(resolved.corePath))) {
        return true;
    }

    // Sibling-cell fallback: .../lib/inverter_tb/foo.core → .../lib/inverter/inverter.schematic.core
    if (!hintDocPath.trimmed().isEmpty()) {
        QDir cellDir = QFileInfo(hintDocPath).absoluteDir();
        const QString cell = compName.trimmed();
        if (acceptIfExists(cellDir.filePath(cell + QLatin1Char('/') + cell + QStringLiteral(".schematic.core")))) {
            return true;
        }
        if (cellDir.cdUp()) {
            if (acceptIfExists(cellDir.filePath(cell + QLatin1Char('/') + cell + QStringLiteral(".schematic.core")))) {
                return true;
            }
            if (!libName.trimmed().isEmpty() && cellDir.dirName().compare(libName.trimmed(), Qt::CaseInsensitive) != 0) {
                // hint may be deeper; try libName/cell under parent chain once more
                if (cellDir.cdUp()
                    && acceptIfExists(cellDir.filePath(libName.trimmed() + QLatin1Char('/') + cell + QLatin1Char('/')
                                                       + cell + QStringLiteral(".schematic.core")))) {
                    return true;
                }
            }
        }
    }

    return false;
}

} // namespace qucs_core
