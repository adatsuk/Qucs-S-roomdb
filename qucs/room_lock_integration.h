#pragma once

#include "room_schematic_io.h"

class QucsApp;
class Schematic;

namespace qucs_room {

struct RoomLockOpenResult
{
    bool    viewOnly      = false;
    bool    lockHeld      = false;
    QString statusMessage;
};

RoomLockOpenResult applyRoomLockOnOpen(const QString &roomPath, bool sameFileOpenElsewhere);

void releaseRoomLockOnClose(const QString &roomPath);

IoResult verifyRoomLockForSave(const QString &roomPath);

void finalizeRoomDocumentLock(Schematic *schematic, QucsApp *app);

void refreshRoomDocumentLock(Schematic *schematic, QucsApp *app);

} // namespace qucs_room
