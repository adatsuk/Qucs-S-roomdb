#include "core_lock_integration.h"

#include "core_file_lock.h"
#include "qucs.h"
#include "schematic.h"

#include <QFileInfo>
#include <QMessageBox>
#include <QObject>

namespace qucs_core {

CoreLockOpenResult applyCoreLockOnOpen(const QString &corePath, bool sameFileOpenElsewhere)
{
    CoreLockOpenResult result;
    const QString absPath = QFileInfo(corePath).absoluteFilePath();

    if (sameFileOpenElsewhere && coreLockRefCount(absPath) > 0) {
        const QString lockPath = lockFilePathForCore(absPath);
        if (QFileInfo::exists(lockPath)) {
            coreLockAddRef(absPath);
            result.viewOnly = false;
            result.lockHeld = true;
            result.statusMessage = QObject::tr("Editing (lock held)");
            return result;
        }
        // Lock sidecar was removed while another tab still holds the ref — re-acquire.
    }

    CoreFileLockInfo info = readCoreLockFile(absPath);
    if (info.present && !isStaleCoreLock(info) && !isCoreLockHeldByCurrentProcess(info)) {
        result.viewOnly = true;
        result.lockHeld = false;
        result.statusMessage = formatCoreLockStatusLine(info);
        return result;
    }

    if (info.present && isStaleCoreLock(info)) {
        releaseCoreLock(absPath);
    }

    QString errorMessage;
    const CoreLockAcquireResult acquired = tryAcquireCoreLock(absPath, &errorMessage);
    if (acquired == CoreLockAcquireResult::ForeignLock) {
        info = readCoreLockFile(absPath);
        result.viewOnly = true;
        result.lockHeld = false;
        result.statusMessage = formatCoreLockStatusLine(info);
        return result;
    }

    if (acquired == CoreLockAcquireResult::Error) {
        result.viewOnly = true;
        result.lockHeld = false;
        result.statusMessage = errorMessage.isEmpty()
                                   ? QObject::tr("Could not acquire CORE lock.")
                                   : errorMessage;
        return result;
    }

    if (acquired == CoreLockAcquireResult::Acquired
        || acquired == CoreLockAcquireResult::AlreadyHeld) {
        coreLockAddRef(absPath);
        result.viewOnly = false;
        result.lockHeld = true;
        result.statusMessage = QObject::tr("Editing (lock held)");
        return result;
    }

    result.viewOnly = true;
    result.lockHeld = false;
    result.statusMessage = QObject::tr("Could not acquire CORE lock.");
    return result;
}

void releaseCoreLockOnClose(const QString &corePath)
{
    const QString absPath = QFileInfo(corePath).absoluteFilePath();
    if (coreLockReleaseRef(absPath) > 0) {
        return;
    }

    releaseCoreLock(absPath);
}

IoResult verifyCoreLockForSave(const QString &corePath)
{
    IoResult result;
    const QString absPath = QFileInfo(corePath).absoluteFilePath();
    const CoreFileLockInfo info = readCoreLockFile(absPath);

    if (!info.present || isStaleCoreLock(info)) {
        result.ok = true;
        return result;
    }

    if (isCoreLockHeldByCurrentProcess(info) || coreLockRefCount(absPath) > 0) {
        result.ok = true;
        return result;
    }

    result.message = QObject::tr("Cannot save: %1").arg(formatCoreLockStatusLine(info));
    return result;
}

void finalizeCoreDocumentLock(Schematic *schematic, QucsApp *app)
{
    if (schematic == nullptr || app == nullptr) {
        return;
    }

    const QString corePath = QFileInfo(schematic->getDocName()).absoluteFilePath();
    if (!isCoreViewPath(corePath)) {
        return;
    }

    int matchingTabs = 0;
    for (int i = 0; i < app->DocumentTab->count(); ++i) {
        QWidget *widget = app->DocumentTab->widget(i);
        if (QucsApp::isTextDocument(widget)) {
            continue;
        }

        const auto *other = static_cast<const Schematic *>(widget);
        if (QFileInfo(other->getDocName()).absoluteFilePath() == corePath) {
            ++matchingTabs;
        }
    }

    const bool sameFileOpenElsewhere = matchingTabs > 1 && coreLockRefCount(corePath) > 0;
    const CoreLockOpenResult lockResult = applyCoreLockOnOpen(corePath, sameFileOpenElsewhere);
    schematic->configureCoreLockState(lockResult.viewOnly, lockResult.lockHeld, corePath,
                                      lockResult.statusMessage);

    app->watchCoreLockFile(corePath);

    if (lockResult.viewOnly) {
        const CoreFileLockInfo info = readCoreLockFile(corePath);
        QMessageBox::information(app, QObject::tr("CORE file locked"),
                                 QObject::tr("This CORE view is opened read-only.\n\n%1")
                                     .arg(formatCoreLockStatusLine(info)));
    }

    app->updateCoreLockUi(schematic);
}

void refreshCoreDocumentLock(Schematic *schematic, QucsApp *app)
{
    if (schematic == nullptr || app == nullptr) {
        return;
    }

    const QString corePath = QFileInfo(schematic->getDocName()).absoluteFilePath();
    if (!isCoreViewPath(corePath)) {
        return;
    }

    const bool wasViewOnly = schematic->isCoreViewOnly();
    const bool wasLockHeld = schematic->holdsCoreLock();

    CoreFileLockInfo info = readCoreLockFile(corePath);
    if (!info.present || isStaleCoreLock(info)) {
        if (wasViewOnly) {
            QString errorMessage;
            const CoreLockAcquireResult acquired = tryAcquireCoreLock(corePath, &errorMessage);
            if (acquired == CoreLockAcquireResult::Acquired
                || acquired == CoreLockAcquireResult::AlreadyHeld) {
                coreLockAddRef(corePath);
                schematic->configureCoreLockState(false, true, corePath,
                                                  QObject::tr("Editing (lock held)"));
            }
        }
    } else if (!isCoreLockHeldByCurrentProcess(info) && coreLockRefCount(corePath) == 0) {
        if (!wasViewOnly) {
            if (wasLockHeld) {
                schematic->dropCoreLockOwnership();
            }
            schematic->configureCoreLockState(true, false, corePath, formatCoreLockStatusLine(info));
            if (schematic->getDocChanged()) {
                QMessageBox::warning(app, QObject::tr("CORE lock acquired by another tool"),
                                     QObject::tr("This document is now read-only and cannot be saved."));
            }
        }
    } else if (wasViewOnly && isCoreLockHeldByCurrentProcess(info)) {
        coreLockAddRef(corePath);
        schematic->configureCoreLockState(false, true, corePath, QObject::tr("Editing (lock held)"));
    }

    if (wasViewOnly != schematic->isCoreViewOnly() || wasLockHeld != schematic->holdsCoreLock()) {
        app->updateCoreLockUi(schematic);
    }
}

} // namespace qucs_core
