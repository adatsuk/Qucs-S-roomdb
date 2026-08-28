#pragma once

#include "core_schematic_io.h"

class Schematic;

namespace qucs_core {

IoResult loadCoreFileDirect(const QString &corePath, Schematic *schematic);
IoResult saveSchematicToCoreFileDirect(Schematic *schematic, const QString &corePath);

} // namespace qucs_core
