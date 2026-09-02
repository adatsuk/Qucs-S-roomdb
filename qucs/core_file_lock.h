#pragma once

#include <QString>

struct CoreFileLockInfo
{
    bool    present   = false;
    bool    parseOk   = false;
    QString corePath;
    QString lockPath;
    QString user;
    QString host;
    QString tool;
    QString toolVersion;
    QString createdAt;
    int     pid       = 0;
    QString parseError;
};

enum class CoreLockAcquireResult
{
    Acquired,
    AlreadyHeld,
    ForeignLock,
    Error
};

QString lockFilePathForCore(const QString &corePath);

CoreFileLockInfo readCoreLockFile(const QString &corePath);

bool isCoreLockHeldByCurrentProcess(const CoreFileLockInfo &info);

bool isLocalToolCoreLock(const CoreFileLockInfo &info);

bool isStaleCoreLock(const CoreFileLockInfo &info);

CoreLockAcquireResult tryAcquireCoreLock(const QString &corePath, QString *errorMessage = nullptr);

bool releaseCoreLock(const QString &corePath);

QString formatCoreLockStatusLine(const CoreFileLockInfo &info);

QString formatCoreLockInfoBlock(const CoreFileLockInfo &info);

int coreLockAddRef(const QString &corePath);

int coreLockReleaseRef(const QString &corePath);

int coreLockRefCount(const QString &corePath);
