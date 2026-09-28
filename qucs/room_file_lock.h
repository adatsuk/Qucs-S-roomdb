#pragma once

#include <QString>

struct RoomFileLockInfo
{
    bool    present   = false;
    bool    parseOk   = false;
    QString roomPath;
    QString lockPath;
    QString user;
    QString host;
    QString tool;
    QString toolVersion;
    QString createdAt;
    int     pid       = 0;
    QString parseError;
};

enum class RoomLockAcquireResult
{
    Acquired,
    AlreadyHeld,
    ForeignLock,
    Error
};

QString lockFilePathForRoom(const QString &roomPath);

RoomFileLockInfo readRoomLockFile(const QString &roomPath);

bool isRoomLockHeldByCurrentProcess(const RoomFileLockInfo &info);

bool isLocalToolRoomLock(const RoomFileLockInfo &info);

bool isStaleRoomLock(const RoomFileLockInfo &info);

RoomLockAcquireResult tryAcquireRoomLock(const QString &roomPath, QString *errorMessage = nullptr);

bool releaseRoomLock(const QString &roomPath);

QString formatRoomLockStatusLine(const RoomFileLockInfo &info);

QString formatRoomLockInfoBlock(const RoomFileLockInfo &info);

int roomLockAddRef(const QString &roomPath);

int roomLockReleaseRef(const QString &roomPath);

int roomLockRefCount(const QString &roomPath);
