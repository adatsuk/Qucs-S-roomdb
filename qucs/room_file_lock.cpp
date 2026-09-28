#include "room_file_lock.h"

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QProcess>
#include <QSysInfo>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <unistd.h>
#endif

namespace {

QString readJsonString(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    if (!value.isString()) {
        return QString();
    }
    return value.toString().trimmed();
}

QString currentUserName()
{
    QString user = qEnvironmentVariable("USER");
    if (user.isEmpty()) {
        user = qEnvironmentVariable("USERNAME");
    }
    if (user.isEmpty()) {
        user = QStringLiteral("unknown");
    }
    return user;
}

QString currentHostName()
{
    const QString host = QSysInfo::machineHostName().trimmed();
    return host.isEmpty() ? QStringLiteral("localhost") : host;
}

qint64 currentProcessId()
{
    return static_cast<qint64>(QCoreApplication::applicationPid());
}

bool isWindowsProcessAlive(qint64 pid)
{
    if (pid <= 0) {
        return false;
    }

#ifdef Q_OS_WIN
    const HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (handle == nullptr) {
        return false;
    }

    DWORD exitCode = 0;
    const bool alive = GetExitCodeProcess(handle, &exitCode) != 0 && exitCode == STILL_ACTIVE;
    CloseHandle(handle);
    return alive;
#else
    return ::kill(static_cast<pid_t>(pid), 0) == 0;
#endif
}

bool isWslProcessAlive(qint64 pid)
{
    if (pid <= 0) {
        return false;
    }

#ifdef Q_OS_WIN
    QProcess process;
    process.setProgram(QStringLiteral("wsl.exe"));
    process.setArguments(QStringList()
                         << QStringLiteral("-e")
                         << QStringLiteral("kill")
                         << QStringLiteral("-0")
                         << QString::number(pid));
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    if (!process.waitForFinished(3000)) {
        process.kill();
        process.waitForFinished(1000);
        // WSL unavailable/hung — treat remote Linux holder as gone.
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
#else
    return ::kill(static_cast<pid_t>(pid), 0) == 0;
#endif
}

bool isProcessAlive(qint64 pid)
{
    return isWindowsProcessAlive(pid);
}

QHash<QString, int> &lockRefCounts()
{
    static QHash<QString, int> counts;
    return counts;
}

QString absoluteRoomPath(const QString &roomPath)
{
    return QFileInfo(roomPath).absoluteFilePath();
}

} // namespace

QString lockFilePathForRoom(const QString &roomPath)
{
    return absoluteRoomPath(roomPath) + QStringLiteral(".lck");
}

RoomFileLockInfo readRoomLockFile(const QString &roomPath)
{
    RoomFileLockInfo info;
    info.roomPath = absoluteRoomPath(roomPath);
    info.lockPath = lockFilePathForRoom(info.roomPath);
    info.present = QFileInfo::exists(info.lockPath);
    if (!info.present) {
        return info;
    }

    QFile file(info.lockPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        info.parseOk = false;
        info.parseError = QStringLiteral("Could not read lock file.");
        return info;
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        info.parseOk = false;
        info.parseError = QStringLiteral("Lock file is not valid JSON.");
        return info;
    }

    const QJsonObject root = document.object();
    info.parseOk = true;
    info.createdAt = readJsonString(root, QStringLiteral("createdAt"));
    info.roomPath = readJsonString(root, QStringLiteral("roomPath"));
    if (info.roomPath.isEmpty()) {
        info.roomPath = absoluteRoomPath(roomPath);
    }

    const QJsonObject holder = root.value(QStringLiteral("holder")).toObject();
    info.user = readJsonString(holder, QStringLiteral("user"));
    info.host = readJsonString(holder, QStringLiteral("host"));
    info.tool = readJsonString(holder, QStringLiteral("tool"));
    info.toolVersion = readJsonString(holder, QStringLiteral("toolVersion"));
    if (holder.value(QStringLiteral("pid")).isDouble()) {
        info.pid = holder.value(QStringLiteral("pid")).toInt();
    }

    return info;
}

bool isLocalToolRoomLock(const RoomFileLockInfo &info)
{
    return info.tool.isEmpty()
        || info.tool.compare(QStringLiteral("qucs-s"), Qt::CaseInsensitive) == 0;
}

bool isStaleRoomLock(const RoomFileLockInfo &info)
{
    if (!info.present) {
        return false;
    }

    if (!info.parseOk) {
        return true;
    }

    if (info.pid <= 0) {
        return true;
    }

    if (isRoomLockHeldByCurrentProcess(info)) {
        return false;
    }

    // xschem (and other WSL tools) store Linux PIDs — check via wsl.exe.
    if (!isLocalToolRoomLock(info)) {
        return !isWslProcessAlive(info.pid);
    }

    return !isWindowsProcessAlive(info.pid);
}

bool isRoomLockHeldByCurrentProcess(const RoomFileLockInfo &info)
{
    if (!info.present || !info.parseOk) {
        return false;
    }

    return info.pid == static_cast<int>(currentProcessId())
        && info.user.compare(currentUserName(), Qt::CaseInsensitive) == 0
        && info.host.compare(currentHostName(), Qt::CaseInsensitive) == 0;
}

RoomLockAcquireResult tryAcquireRoomLock(const QString &roomPath, QString *errorMessage)
{
    const QString absPath = absoluteRoomPath(roomPath);
    const QString lockPath = lockFilePathForRoom(absPath);

    RoomFileLockInfo existing = readRoomLockFile(absPath);
    if (existing.present) {
        if (isStaleRoomLock(existing)) {
            QFile::remove(lockPath);
        } else if (isRoomLockHeldByCurrentProcess(existing)) {
            return RoomLockAcquireResult::AlreadyHeld;
        } else {
            if (errorMessage != nullptr) {
                *errorMessage = formatRoomLockStatusLine(existing);
            }
            return RoomLockAcquireResult::ForeignLock;
        }
    }

    QJsonObject holder;
    holder.insert(QStringLiteral("user"), currentUserName());
    holder.insert(QStringLiteral("host"), currentHostName());
    holder.insert(QStringLiteral("pid"), static_cast<int>(currentProcessId()));
    holder.insert(QStringLiteral("tool"), QStringLiteral("qucs-s"));
    holder.insert(QStringLiteral("toolVersion"), QStringLiteral(PACKAGE_VERSION));

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("roomPath"), absPath);
    root.insert(QStringLiteral("holder"), holder);
    root.insert(QStringLiteral("createdAt"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    const QString tempPath = lockPath + QStringLiteral(".tmp");

    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Could not create lock file.");
        }
        return RoomLockAcquireResult::Error;
    }

    if (tempFile.write(payload) != payload.size()) {
        tempFile.remove();
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Could not write lock file.");
        }
        return RoomLockAcquireResult::Error;
    }
    tempFile.close();

    if (QFile::exists(lockPath) && !QFile::remove(lockPath)) {
        QFile::remove(tempPath);
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Could not replace existing lock file.");
        }
        return RoomLockAcquireResult::ForeignLock;
    }

    if (!QFile::rename(tempPath, lockPath)) {
        QFile::remove(tempPath);
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Could not finalize lock file.");
        }
        return RoomLockAcquireResult::Error;
    }

    return RoomLockAcquireResult::Acquired;
}

bool releaseRoomLock(const QString &roomPath)
{
    const QString lockPath = lockFilePathForRoom(roomPath);
    if (!QFileInfo::exists(lockPath)) {
        return true;
    }

    const RoomFileLockInfo info = readRoomLockFile(roomPath);
    if (info.present && info.parseOk && !isRoomLockHeldByCurrentProcess(info) && !isStaleRoomLock(info)) {
        return false;
    }

    return QFile::remove(lockPath);
}

QString formatRoomLockStatusLine(const RoomFileLockInfo &info)
{
    if (!info.present) {
        return QObject::tr("ROOM lock: none");
    }

    if (!info.parseOk) {
        return QObject::tr("ROOM lock: present but unreadable");
    }

    QString line = QObject::tr("Read-only");
    if (!info.user.isEmpty() || !info.host.isEmpty()) {
        line += QStringLiteral(" — ");
        line += info.user;
        if (!info.host.isEmpty()) {
            line += QStringLiteral("@") + info.host;
        }
    }
    if (!info.tool.isEmpty()) {
        line += QStringLiteral(" (") + info.tool;
        if (!info.toolVersion.isEmpty()) {
            line += QStringLiteral(" ") + info.toolVersion;
        }
        line += QLatin1Char(')');
    }
    return line;
}

QString formatRoomLockInfoBlock(const RoomFileLockInfo &info)
{
    QString block = QStringLiteral("\tLock: ");
    if (!info.present) {
        block += QStringLiteral("none\n");
        return block;
    }

    block += QStringLiteral("active\n");
    block += QStringLiteral("\tLock file: ") + info.lockPath + QStringLiteral("\n");

    if (!info.parseOk) {
        block += QStringLiteral("\tLock details: ") + info.parseError + QStringLiteral("\n");
        return block;
    }

    if (!info.user.isEmpty() || !info.host.isEmpty()) {
        block += QStringLiteral("\tLocked by: ") + info.user;
        if (!info.host.isEmpty()) {
            block += QStringLiteral(" @ ") + info.host;
        }
        block += QStringLiteral("\n");
    }

    if (!info.tool.isEmpty()) {
        block += QStringLiteral("\tTool: ") + info.tool;
        if (!info.toolVersion.isEmpty()) {
            block += QStringLiteral(" ") + info.toolVersion;
        }
        block += QStringLiteral("\n");
    }

    if (info.pid > 0) {
        block += QStringLiteral("\tPID: ") + QString::number(info.pid) + QStringLiteral("\n");
    }

    if (!info.createdAt.isEmpty()) {
        block += QStringLiteral("\tSince: ") + info.createdAt + QStringLiteral("\n");
    }

    return block;
}

int roomLockAddRef(const QString &roomPath)
{
    const QString absPath = absoluteRoomPath(roomPath);
    return ++lockRefCounts()[absPath];
}

int roomLockReleaseRef(const QString &roomPath)
{
    const QString absPath = absoluteRoomPath(roomPath);
    auto it = lockRefCounts().find(absPath);
    if (it == lockRefCounts().end()) {
        return 0;
    }

    const int next = it.value() - 1;
    if (next <= 0) {
        lockRefCounts().erase(it);
        return 0;
    }

    it.value() = next;
    return next;
}

int roomLockRefCount(const QString &roomPath)
{
    return lockRefCounts().value(absoluteRoomPath(roomPath), 0);
}
