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

/*! Resolve …/cell.<view>.room (view = schematic|symbol|emmodel|…). */
bool tryResolveRoomView(const QString &libName, const QString &compName, const QString &viewName,
                        QString &roomPath, const QString &hintDocPath = QString());

bool tryResolveRoomSymbol(const QString &libName, const QString &compName, QString &symbolRoomPath,
                          const QString &hintDocPath = QString());

bool tryResolveRoomEmModel(const QString &libName, const QString &compName, QString &emModelRoomPath,
                           const QString &hintDocPath = QString());

/*! Published EM model + lookalike flag for LibComp Touchstone netlisting. */
struct RoomEmModelNetlistInfo {
    QString emModelPath;
    QString snpPath;
    double  z0 = 50.0;
    int     portCount = 0;
    bool    lookalike = false;
};

/*! True when cell.symbol.room has property em.lookalike=1. */
bool roomCellSymbolIsLookalike(const QString &libName, const QString &cellName,
                               const QString &hintDocPath = QString());

/*! Load .emmodel.room payload (snpPath/z0/ports) and lookalike flag from sibling symbol. */
bool tryLoadRoomEmModelNetlist(const QString &libName, const QString &cellName,
                               RoomEmModelNetlistInfo &out,
                               const QString &hintDocPath = QString());

} // namespace qucs_room
