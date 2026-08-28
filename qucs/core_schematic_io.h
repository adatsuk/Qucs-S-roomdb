#pragma once

#include <QString>

namespace qucs_core {

struct IoResult {
    bool    ok = false;
    QString message;
};

extern bool g_coreBridgeActive;

bool isCoreViewPath(const QString &path);
bool isCoreSchematicPath(const QString &path);
bool isCoreSymbolPath(const QString &path);
QString cellNameFromCorePath(const QString &path);
QString documentBaseName(const QString &path);

IoResult exportCoreToSchFile(const QString &corePath, const QString &schPath);
IoResult exportCoreSymbolToSchFile(const QString &corePath, const QString &schPath);
IoResult exportCoreViewToFile(const QString &corePath, const QString &schPath);
IoResult exportCoreViewToString(const QString &corePath, QString &schText);
IoResult importSchFileToCore(const QString &schPath, const QString &corePath);
IoResult importSchStringToCore(const QString &schText, const QString &corePath);

qint64 normalizeSchCoordinatesForDisplay(const QString &schPath);
qint64 normalizeSchCoordinatesInMemory(QString &schText);
void denormalizeSchCoordinates(const QString &schPath, qint64 divisor);
void denormalizeSchCoordinatesInMemory(QString &schText, qint64 divisor);

} // namespace qucs_core
