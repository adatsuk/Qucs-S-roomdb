#pragma once

#include <QString>

namespace qucs_room {

struct IoResult {
    bool    ok = false;
    QString message;
};

extern bool g_roomBridgeActive;

bool isRoomViewPath(const QString &path);
bool isRoomSchematicPath(const QString &path);
bool isRoomSymbolPath(const QString &path);
QString cellNameFromRoomPath(const QString &path);
QString documentBaseName(const QString &path);

IoResult exportRoomToSchFile(const QString &roomPath, const QString &schPath);
IoResult exportRoomSymbolToSchFile(const QString &roomPath, const QString &schPath);
IoResult exportRoomViewToFile(const QString &roomPath, const QString &schPath);
IoResult exportRoomViewToString(const QString &roomPath, QString &schText);
IoResult importSchFileToRoom(const QString &schPath, const QString &roomPath);
IoResult importSchStringToRoom(const QString &schText, const QString &roomPath);

qint64 normalizeSchCoordinatesForDisplay(const QString &schPath);
qint64 normalizeSchCoordinatesInMemory(QString &schText);
void denormalizeSchCoordinates(const QString &schPath, qint64 divisor);
void denormalizeSchCoordinatesInMemory(QString &schText, qint64 divisor);

} // namespace qucs_room
