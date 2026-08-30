#pragma once

#include <QString>

namespace qucs_core {

bool tryLoadCorePrimitiveSymbol(const QString &compName, QString &symbolSection);

/*! Resolve hierarchical cell schematic CORE path (lib/cell → …/cell.schematic.core).
 *  \param hintDocPath optional parent schematic/CORE path for sibling-cell fallback. */
bool tryResolveCoreSchematic(const QString &libName, const QString &compName, QString &schematicCorePath,
                             const QString &hintDocPath = QString());

} // namespace qucs_core
