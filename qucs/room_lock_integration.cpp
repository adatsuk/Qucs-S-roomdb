#include "room_lock_integration.h"

#include "room_file_lock.h"
#include "qucs.h"
#include "schematic.h"

#include <QFileInfo>
#include <QMessageBox>
#include <QObject>

namespace qucs_room {

RoomLockOpenResult applyRoomLockOnOpen(const QString &roomPath, bool sameFileOpenElsewhere)
{
    RoomLockOpenResult result;
    const QString absPath = QFileInfo(roomPath).absoluteFilePath();

    if (sameFileOpenElsewhere && roomLockRefCount(absPath) > 0) {
        const QString lockPath = lockFilePathForRoom(absPath);
        if (QFileInfo::exists(lockPath)) {
            roomLockAddRef(absPath);
            result.viewOnly = false;
            result.lockHeld = true;
            result.statusMessage = QObject::tr("Editing (lock held)");
            return result;
        }
        // Lock sidecar was removed while another tab still holds the ref — re-acquire.
    }

    RoomFileLockInfo info = readRoomLockFile(absPath);
    if (info.present && !isStaleRoomLock(info) && !isRoomLockHeldByCurrentProcess(info)) {
        result.viewOnly = true;
        result.lockHeld = false;
        result.statusMessage = formatRoomLockStatusLine(info);
        return result;
    }

    if (info.present && isStaleRoomLock(info)) {
        releaseRoomLock(absPath);
    }

    QString errorMessage;
    const RoomLockAcquireResult acquired = tryAcquireRoomLock(absPath, &errorMessage);
    if (acquired == RoomLockAcquireResult::ForeignLock) {
        info = readRoomLockFile(absPath);
        result.viewOnly = true;
        result.lockHeld = false;
        result.statusMessage = formatRoomLockStatusLine(info);
        return result;
    }

    if (acquired == RoomLockAcquireResult::Error) {
        result.viewOnly = true;
        result.lockHeld = false;
        result.statusMessage = errorMessage.isEmpty()
                                   ? QObject::tr("Could not acquire ROOM lock.")
                                   : errorMessage;
        return result;
    }

    if (acquired == RoomLockAcquireResult::Acquired
        || acquired == RoomLockAcquireResult::AlreadyHeld) {
        roomLockAddRef(absPath);
        result.viewOnly = false;
        result.lockHeld = true;
        result.statusMessage = QObject::tr("Editing (lock held)");
        return result;
    }

    result.viewOnly = true;
    result.lockHeld = false;
    result.statusMessage = QObject::tr("Could not acquire ROOM lock.");
    return result;
}

void releaseRoomLockOnClose(const QString &roomPath)
{
    const QString absPath = QFileInfo(roomPath).absoluteFilePath();
    if (roomLockReleaseRef(absPath) > 0) {
        return;
    }

    releaseRoomLock(absPath);
}

IoResult verifyRoomLockForSave(const QString &roomPath)
{
    IoResult result;
    const QString absPath = QFileInfo(roomPath).absoluteFilePath();
    const RoomFileLockInfo info = readRoomLockFile(absPath);

    if (!info.present || isStaleRoomLock(info)) {
        result.ok = true;
        return result;
    }

    if (isRoomLockHeldByCurrentProcess(info) || roomLockRefCount(absPath) > 0) {
        result.ok = true;
        return result;
    }

    result.message = QObject::tr("Cannot save: %1").arg(formatRoomLockStatusLine(info));
    return result;
}

void finalizeRoomDocumentLock(Schematic *schematic, QucsApp *app)
{
    if (schematic == nullptr || app == nullptr) {
        return;
    }

    const QString roomPath = QFileInfo(schematic->getDocName()).absoluteFilePath();
    if (!isRoomViewPath(roomPath)) {
        return;
    }

    int matchingTabs = 0;
    for (int i = 0; i < app->DocumentTab->count(); ++i) {
        QWidget *widget = app->DocumentTab->widget(i);
        if (QucsApp::isTextDocument(widget)) {
            continue;
        }

        const auto *other = static_cast<const Schematic *>(widget);
        if (QFileInfo(other->getDocName()).absoluteFilePath() == roomPath) {
            ++matchingTabs;
        }
    }

    const bool sameFileOpenElsewhere = matchingTabs > 1 && roomLockRefCount(roomPath) > 0;
    const RoomLockOpenResult lockResult = applyRoomLockOnOpen(roomPath, sameFileOpenElsewhere);
    schematic->configureRoomLockState(lockResult.viewOnly, lockResult.lockHeld, roomPath,
                                      lockResult.statusMessage);

    app->watchRoomLockFile(roomPath);

    if (lockResult.viewOnly) {
        const RoomFileLockInfo info = readRoomLockFile(roomPath);
        QMessageBox::information(app, QObject::tr("ROOM file locked"),
                                 QObject::tr("This ROOM view is opened read-only.\n\n%1")
                                     .arg(formatRoomLockStatusLine(info)));
    }

    app->updateRoomLockUi(schematic);
}

void refreshRoomDocumentLock(Schematic *schematic, QucsApp *app)
{
    if (schematic == nullptr || app == nullptr) {
        return;
    }

    const QString roomPath = QFileInfo(schematic->getDocName()).absoluteFilePath();
    if (!isRoomViewPath(roomPath)) {
        return;
    }

    const bool wasViewOnly = schematic->isRoomViewOnly();
    const bool wasLockHeld = schematic->holdsRoomLock();

    RoomFileLockInfo info = readRoomLockFile(roomPath);
    if (!info.present || isStaleRoomLock(info)) {
        if (wasViewOnly) {
            QString errorMessage;
            const RoomLockAcquireResult acquired = tryAcquireRoomLock(roomPath, &errorMessage);
            if (acquired == RoomLockAcquireResult::Acquired
                || acquired == RoomLockAcquireResult::AlreadyHeld) {
                roomLockAddRef(roomPath);
                schematic->configureRoomLockState(false, true, roomPath,
                                                  QObject::tr("Editing (lock held)"));
            }
        }
    } else if (!isRoomLockHeldByCurrentProcess(info) && roomLockRefCount(roomPath) == 0) {
        if (!wasViewOnly) {
            if (wasLockHeld) {
                schematic->dropRoomLockOwnership();
            }
            schematic->configureRoomLockState(true, false, roomPath, formatRoomLockStatusLine(info));
            if (schematic->getDocChanged()) {
                QMessageBox::warning(app, QObject::tr("ROOM lock acquired by another tool"),
                                     QObject::tr("This document is now read-only and cannot be saved."));
            }
        }
    } else if (wasViewOnly && isRoomLockHeldByCurrentProcess(info)) {
        roomLockAddRef(roomPath);
        schematic->configureRoomLockState(false, true, roomPath, QObject::tr("Editing (lock held)"));
    }

    if (wasViewOnly != schematic->isRoomViewOnly() || wasLockHeld != schematic->holdsRoomLock()) {
        app->updateRoomLockUi(schematic);
    }
}

} // namespace qucs_room
