#pragma once

#include <QString>

namespace qucs_room {

bool tryLoadRoomPrimitiveSymbol(const QString &compName, QString &symbolSection);

/*! Load PortSym geometry from a hierarchical cell symbol.room (e.g. module_x/cell.symbol.room). */
bool tryLoadRoomCellSymbol(const QString &libName, const QString &cellName, QString &symbolSection,
                           const QString &hintDocPath = QString());

/*! Resolve hierarchical cell schematic ROOM path (lib/cell → …/cell.schematic.room).
 *  \param hintDocPath optional parent schematic/ROOM path for sibling-cell fallback. */
bool tryResolveRoomSchematic(const QString &libName, const QString &compName, QString &schematicRoomPath,
                             const QString &hintDocPath = QString());

} // namespace qucs_room
