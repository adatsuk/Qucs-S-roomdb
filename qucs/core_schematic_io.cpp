#include "core_schematic_io.h"

#include "core_paths.h"
#include "database.h"
#include "primitive_resolver.h"
#include "qucs_exporter.h"
#include "qucs_importer.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace qucs_core {

bool g_coreBridgeActive = false;

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

QString normalizedViewSuffix(const QString &suffix)
{
    const QString lower = suffix.trimmed().toLower();
    if (lower == QStringLiteral("sch")) {
        return QStringLiteral("schematic");
    }
    return lower;
}

qint64 absCoord(qint64 value)
{
    return value < 0 ? -value : value;
}

QString stripSchLineBody(const QString &line)
{
    QString body = line.trimmed();
    if (body.startsWith(QLatin1Char('<'))) {
        body = body.mid(1);
    }
    if (body.endsWith(QLatin1Char('>'))) {
        body.chop(1);
    }
    return body.trimmed();
}

QString formatSchLine(const QStringList &fields)
{
    return QStringLiteral("<") + fields.join(QLatin1Char(' ')) + QStringLiteral(">");
}

void scaleCoordinateFields(QStringList &fields, const QList<int> &indices, qint64 factor, bool multiply)
{
    for (const int index : indices) {
        if (index < 0 || index >= fields.size()) {
            continue;
        }
        bool ok = false;
        const qint64 value = fields.at(index).toLongLong(&ok);
        if (!ok) {
            continue;
        }
        fields[index] = QString::number(multiply ? value * factor : value / factor);
    }
}

} // namespace

qint64 normalizeSchCoordinatesInMemory(QString &schContent)
{
    const QStringList lines = schContent.split(QLatin1Char('\n'));
    enum class Section { None, Components, Wires, Properties };
    Section section = Section::None;
    qint64 maxAbs = 0;

    for (const QString &line : lines) {
        if (line == QStringLiteral("<Components>")) {
            section = Section::Components;
            continue;
        }
        if (line == QStringLiteral("<Wires>")) {
            section = Section::Wires;
            continue;
        }
        if (line == QStringLiteral("<Properties>")) {
            section = Section::Properties;
            continue;
        }
        if (line.startsWith(QStringLiteral("</"))) {
            section = Section::None;
            continue;
        }

        if (!line.startsWith(QLatin1Char('<'))) {
            continue;
        }

        QStringList fields = stripSchLineBody(line).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (fields.isEmpty()) {
            continue;
        }

        if (section == Section::Components && fields.size() >= 7) {
            for (int index = 3; index <= 6; ++index) {
                bool ok = false;
                const qint64 value = fields.at(index).toLongLong(&ok);
                if (ok) {
                    maxAbs = std::max(maxAbs, absCoord(value));
                }
            }
        } else if (section == Section::Wires && fields.size() >= 4) {
            for (int index = 0; index < 4; ++index) {
                bool ok = false;
                const qint64 value = fields.at(index).toLongLong(&ok);
                if (ok) {
                    maxAbs = std::max(maxAbs, absCoord(value));
                }
            }
        }
    }

    if (maxAbs <= 5000) {
        return 1;
    }

    const qint64 divisor = (maxAbs > 50000) ? 1000 : 10;
    QStringList normalized;
    section = Section::None;

    for (const QString &line : lines) {
        if (line == QStringLiteral("<Components>")) {
            section = Section::Components;
            normalized.append(line);
            continue;
        }
        if (line == QStringLiteral("<Wires>")) {
            section = Section::Wires;
            normalized.append(line);
            continue;
        }
        if (line == QStringLiteral("<Properties>")) {
            section = Section::Properties;
            normalized.append(line);
            continue;
        }
        if (line.startsWith(QStringLiteral("</"))) {
            section = Section::None;
            normalized.append(line);
            continue;
        }

        if (!line.startsWith(QLatin1Char('<'))) {
            normalized.append(line);
            continue;
        }

        QStringList fields = stripSchLineBody(line).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (fields.isEmpty()) {
            normalized.append(line);
            continue;
        }

        if (section == Section::Components && fields.size() >= 7) {
            scaleCoordinateFields(fields, {3, 4, 5, 6}, divisor, false);
            normalized.append(formatSchLine(fields));
            continue;
        }
        if (section == Section::Wires && fields.size() >= 4) {
            scaleCoordinateFields(fields, {0, 1, 2, 3}, divisor, false);
            normalized.append(formatSchLine(fields));
            continue;
        }
        if (section == Section::Properties && fields.front().startsWith(QStringLiteral("View="))) {
            normalized.append(QStringLiteral("<View=0,0,800,600,1,0,0>"));
            continue;
        }

        normalized.append(line);
    }

    schContent = normalized.join(QStringLiteral("\n"));
    return divisor;
}

qint64 normalizeSchCoordinatesForDisplay(const QString &schPath)
{
    QFile file(schPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return 1;
    }

    QString schContent = QString::fromUtf8(file.readAll());
    file.close();

    const qint64 divisor = normalizeSchCoordinatesInMemory(schContent);
    if (divisor <= 1) {
        return 1;
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return 1;
    }
    file.write(schContent.toUtf8());
    return divisor;
}

void denormalizeSchCoordinatesInMemory(QString &schContent, qint64 divisor)
{
    if (divisor <= 1) {
        return;
    }

    const QStringList lines = schContent.split(QLatin1Char('\n'));
    enum class Section { None, Components, Wires };
    Section section = Section::None;
    QStringList normalized;

    for (const QString &line : lines) {
        if (line == QStringLiteral("<Components>")) {
            section = Section::Components;
            normalized.append(line);
            continue;
        }
        if (line == QStringLiteral("<Wires>")) {
            section = Section::Wires;
            normalized.append(line);
            continue;
        }
        if (line.startsWith(QStringLiteral("</"))) {
            section = Section::None;
            normalized.append(line);
            continue;
        }

        if (!line.startsWith(QLatin1Char('<'))) {
            normalized.append(line);
            continue;
        }

        QStringList fields = stripSchLineBody(line).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (fields.isEmpty()) {
            normalized.append(line);
            continue;
        }

        if (section == Section::Components && fields.size() >= 7) {
            scaleCoordinateFields(fields, {3, 4, 5, 6}, divisor, true);
            normalized.append(formatSchLine(fields));
            continue;
        }
        if (section == Section::Wires && fields.size() >= 4) {
            scaleCoordinateFields(fields, {0, 1, 2, 3}, divisor, true);
            normalized.append(formatSchLine(fields));
            continue;
        }

        normalized.append(line);
    }

    schContent = normalized.join(QStringLiteral("\n"));
}

void denormalizeSchCoordinates(const QString &schPath, qint64 divisor)
{
    if (divisor <= 1) {
        return;
    }

    QFile file(schPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QString schContent = QString::fromUtf8(file.readAll());
    file.close();
    denormalizeSchCoordinatesInMemory(schContent, divisor);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return;
    }
    file.write(schContent.toUtf8());
}

bool isCoreViewPath(const QString &path)
{
    const QString fileName = QFileInfo(path).fileName();
    if (!fileName.endsWith(QStringLiteral(".core"), Qt::CaseInsensitive)) {
        return false;
    }

    const QString stem = fileName.left(fileName.size() - QStringLiteral(".core").size());
    const int dot = stem.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0) {
        return true;
    }

    const QString viewName = normalizedViewSuffix(stem.mid(dot + 1));
    return viewName == QStringLiteral("schematic") || viewName == QStringLiteral("symbol")
        || viewName == QStringLiteral("core") || viewName == QStringLiteral("layout");
}

bool isCoreSchematicPath(const QString &path)
{
    const QString fileName = QFileInfo(path).fileName();
    if (!fileName.endsWith(QStringLiteral(".core"), Qt::CaseInsensitive)) {
        return false;
    }

    const QString stem = fileName.left(fileName.size() - QStringLiteral(".core").size());
    const int dot = stem.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0) {
        return true;
    }

    const QString viewName = normalizedViewSuffix(stem.mid(dot + 1));
    return viewName == QStringLiteral("schematic") || viewName == QStringLiteral("core")
        || viewName == QStringLiteral("layout");
}

bool isCoreSymbolPath(const QString &path)
{
    const QString fileName = QFileInfo(path).fileName();
    if (!fileName.endsWith(QStringLiteral(".core"), Qt::CaseInsensitive)) {
        return false;
    }

    const QString stem = fileName.left(fileName.size() - QStringLiteral(".core").size());
    const int dot = stem.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0) {
        return false;
    }

    return normalizedViewSuffix(stem.mid(dot + 1)) == QStringLiteral("symbol");
}

QString documentBaseName(const QString &path)
{
    if (isCoreViewPath(path)) {
        const QString cellName = cellNameFromCorePath(path);
        if (!cellName.isEmpty()) {
            return cellName;
        }
    }
    return QFileInfo(path).completeBaseName();
}

QString cellNameFromCorePath(const QString &path)
{
    const QFileInfo fi(path);
    const QString fileName = fi.fileName();
    if (!fileName.endsWith(QStringLiteral(".core"), Qt::CaseInsensitive)) {
        return fi.completeBaseName();
    }

    const QString stem = fileName.left(fileName.size() - QStringLiteral(".core").size());
    const int dot = stem.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0) {
        return stem.trimmed();
    }

    const QString viewName = normalizedViewSuffix(stem.mid(dot + 1));
    if (viewName == QStringLiteral("schematic")
        || viewName == QStringLiteral("symbol")
        || viewName == QStringLiteral("layout")
        || viewName == QStringLiteral("abstract")
        || viewName == QStringLiteral("core")) {
        return stem.left(dot).trimmed();
    }

    return stem.trimmed();
}

IoResult exportCoreToSchFile(const QString &corePath, const QString &schPath)
{
    IoResult result;
    try {
        const core::Database db = core::Database::loadFromFile(corePath.toStdString());
        const QString cellName = cellNameFromCorePath(corePath);
        if (cellName.isEmpty()) {
            result.message = QObject::tr("Failed to determine cell name from CORE file.");
            return result;
        }

        const core::Cell *cell = db.lib().findCell(cellName.toStdString());
        const std::string exportCellName =
            cell ? cellName.toStdString() : db.lib().cells().empty() ? std::string() : db.lib().cells().front().name();
        if (exportCellName.empty()) {
            result.message = QObject::tr("CORE file contains no cells.");
            return result;
        }

        core::QucsExporter exporter(exporterOptionsFromEnvironment());
        exporter.exportCell(db, exportCellName, schPath.toStdString());
        for (const std::string &warning : exporter.warnings()) {
            qWarning() << "CORE export warning:" << QString::fromStdString(warning);
        }
        if (!exporter.errors().empty()) {
            result.message = QString::fromStdString(exporter.errors().front());
            return result;
        }

        if (!QFileInfo::exists(schPath)) {
            result.message = QObject::tr("CORE export did not create a schematic file.");
            return result;
        }

        result.ok = true;
        return result;
    } catch (const std::exception &ex) {
        result.message = QString::fromStdString(ex.what());
        return result;
    }
}

IoResult exportCoreSymbolToSchFile(const QString &corePath, const QString &schPath)
{
    IoResult result;
    try {
        const core::Database db = core::Database::loadFromFile(corePath.toStdString());
        const QString cellName = cellNameFromCorePath(corePath);
        if (cellName.isEmpty()) {
            result.message = QObject::tr("Failed to determine cell name from CORE file.");
            return result;
        }

        const core::Cell *cell = db.lib().findCell(cellName.toStdString());
        const std::string exportCellName =
            cell ? cellName.toStdString() : db.lib().cells().empty() ? std::string() : db.lib().cells().front().name();
        if (exportCellName.empty()) {
            result.message = QObject::tr("CORE file contains no cells.");
            return result;
        }

        core::QucsExporter exporter;
        exporter.exportSymbolCell(db, exportCellName, schPath.toStdString());
        for (const std::string &warning : exporter.warnings()) {
            qWarning() << "CORE symbol export warning:" << QString::fromStdString(warning);
        }
        if (!exporter.errors().empty()) {
            result.message = QString::fromStdString(exporter.errors().front());
            return result;
        }

        if (!QFileInfo::exists(schPath)) {
            result.message = QObject::tr("CORE export did not create a symbol file.");
            return result;
        }

        result.ok = true;
        return result;
    } catch (const std::exception &ex) {
        result.message = QString::fromStdString(ex.what());
        return result;
    }
}

IoResult exportCoreViewToFile(const QString &corePath, const QString &schPath)
{
    if (isCoreSymbolPath(corePath)) {
        return exportCoreSymbolToSchFile(corePath, schPath);
    }
    return exportCoreToSchFile(corePath, schPath);
}

IoResult exportCoreViewToString(const QString &corePath, QString &schText)
{
    IoResult result;
    try {
        const core::Database db = core::Database::loadFromFile(corePath.toStdString());
        const QString cellName = cellNameFromCorePath(corePath);
        if (cellName.isEmpty()) {
            result.message = QObject::tr("Failed to determine cell name from CORE file.");
            return result;
        }

        const core::Cell *cell = db.lib().findCell(cellName.toStdString());
        const std::string exportCellName =
            cell ? cellName.toStdString() : db.lib().cells().empty() ? std::string() : db.lib().cells().front().name();
        if (exportCellName.empty()) {
            result.message = QObject::tr("CORE file contains no cells.");
            return result;
        }

        core::QucsExporter exporter(exporterOptionsFromEnvironment());
        const std::string text = isCoreSymbolPath(corePath)
                                     ? exporter.exportSymbolCellToString(db, exportCellName)
                                     : exporter.exportCellToString(db, exportCellName);
        for (const std::string &warning : exporter.warnings()) {
            qWarning() << "CORE export warning:" << QString::fromStdString(warning);
        }
        if (!exporter.errors().empty()) {
            result.message = QString::fromStdString(exporter.errors().front());
            return result;
        }
        if (text.empty()) {
            result.message = QObject::tr("CORE export produced empty schematic data.");
            return result;
        }

        schText = QString::fromStdString(text);
        result.ok = true;
        return result;
    } catch (const std::exception &ex) {
        result.message = QString::fromStdString(ex.what());
        return result;
    }
}

IoResult importSchStringToCore(const QString &schText, const QString &corePath)
{
    IoResult result;
    try {
        core::QucsImporter::Options opts;
        opts.libName = "qucs_s";
        opts.cellName = cellNameFromCorePath(corePath).toStdString();
        core::QucsImporter importer(opts);
        core::Database db = importer.importText(schText.toStdString(), opts.cellName);
        for (const std::string &warning : importer.warnings()) {
            qWarning() << "CORE import warning:" << QString::fromStdString(warning);
        }
        if (!importer.errors().empty()) {
            result.message = QString::fromStdString(importer.errors().front());
            return result;
        }

        const core::ParsedCorePath parsed = core::parseCoreFilePath(corePath.toStdString());
        const core::ViewType viewType =
            parsed.valid ? parsed.view : isCoreSymbolPath(corePath) ? core::ViewType::Symbol : core::ViewType::Schematic;

        db.setGenerator("CORE qucs_s");
        db.setTechnology("qucs");
        db.saveToFile(corePath.toStdString(), viewType);

        if (!QFileInfo::exists(corePath)) {
            result.message = QObject::tr("CORE save did not create an output file.");
            return result;
        }

        result.ok = true;
        return result;
    } catch (const std::exception &ex) {
        result.message = QString::fromStdString(ex.what());
        return result;
    }
}

IoResult importSchFileToCore(const QString &schPath, const QString &corePath)
{
    IoResult result;
    try {
        core::QucsImporter::Options opts;
        opts.libName = "qucs_s";
        opts.cellName = cellNameFromCorePath(corePath).toStdString();
        core::QucsImporter importer(opts);
        core::Database db = importer.importFile(schPath.toStdString());
        for (const std::string &warning : importer.warnings()) {
            qWarning() << "CORE import warning:" << QString::fromStdString(warning);
        }
        if (!importer.errors().empty()) {
            result.message = QString::fromStdString(importer.errors().front());
            return result;
        }

        const core::ParsedCorePath parsed = core::parseCoreFilePath(corePath.toStdString());
        const core::ViewType viewType =
            parsed.valid ? parsed.view : isCoreSymbolPath(corePath) ? core::ViewType::Symbol : core::ViewType::Schematic;

        db.setGenerator("CORE qucs_s");
        db.setTechnology("qucs");
        db.saveToFile(corePath.toStdString(), viewType);

        if (!QFileInfo::exists(corePath)) {
            result.message = QObject::tr("CORE save did not create an output file.");
            return result;
        }

        result.ok = true;
        return result;
    } catch (const std::exception &ex) {
        result.message = QString::fromStdString(ex.what());
        return result;
    }
}

} // namespace qucs_core
