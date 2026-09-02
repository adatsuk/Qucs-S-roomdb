#pragma once

#include "core_schematic_io.h"

class QucsApp;
class Schematic;

namespace qucs_core {

struct CoreLockOpenResult
{
    bool    viewOnly      = false;
    bool    lockHeld      = false;
    QString statusMessage;
};

CoreLockOpenResult applyCoreLockOnOpen(const QString &corePath, bool sameFileOpenElsewhere);

void releaseCoreLockOnClose(const QString &corePath);

IoResult verifyCoreLockForSave(const QString &corePath);

void finalizeCoreDocumentLock(Schematic *schematic, QucsApp *app);

void refreshCoreDocumentLock(Schematic *schematic, QucsApp *app);

} // namespace qucs_core
