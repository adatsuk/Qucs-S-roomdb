#pragma once

#include "room_schematic_io.h"

class Schematic;

namespace qucs_room {

void repairRoomSchematicConnectivity(Schematic *schematic);

IoResult loadRoomFileDirect(const QString &roomPath, Schematic *schematic);
IoResult saveSchematicToRoomFileDirect(Schematic *schematic, const QString &roomPath);

} // namespace qucs_room
