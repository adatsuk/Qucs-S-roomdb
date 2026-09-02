#pragma once

#include "core_schematic_io.h"

class Schematic;

namespace qucs_core {

void repairCoreSchematicConnectivity(Schematic *schematic);

IoResult loadCoreFileDirect(const QString &corePath, Schematic *schematic);
IoResult saveSchematicToCoreFileDirect(Schematic *schematic, const QString &corePath);

} // namespace qucs_core
