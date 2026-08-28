#include "core_primitive_symbol.h"

#include "core_schematic_io.h"
#include "database.h"
#include "primitive_resolver.h"
#include "qucs_exporter.h"

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
    if (qEnvironmentVariableIsSet("CORE_PRIMITIVE_LIBS")) {
        const QStringList paths =
            qEnvironmentVariable("CORE_PRIMITIVE_LIBS").split(QRegularExpression(QStringLiteral("[;:]")),
                                                              Qt::SkipEmptyParts);
        for (const QString &path : paths) {
            const QString trimmed = path.trimmed();
            if (!trimmed.isEmpty()) {
                options.primitiveCorePaths.push_back(trimmed.toStdString());
            }
        }
    } else if (qEnvironmentVariableIsSet("CORE_PRIMITIVE_LIB")) {
        options.primitiveCorePaths.push_back(qEnvironmentVariable("CORE_PRIMITIVE_LIB").toStdString());
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

} // namespace qucs_core
