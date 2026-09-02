#include "core_file_lock.h"

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

QString absoluteCorePath(const QString &corePath)
{
    return QFileInfo(corePath).absoluteFilePath();
}

} // namespace

QString lockFilePathForCore(const QString &corePath)
{
    return absoluteCorePath(corePath) + QStringLiteral(".lck");
}

CoreFileLockInfo readCoreLockFile(const QString &corePath)
{
    CoreFileLockInfo info;
    info.corePath = absoluteCorePath(corePath);
    info.lockPath = lockFilePathForCore(info.corePath);
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
    info.corePath = readJsonString(root, QStringLiteral("corePath"));
    if (info.corePath.isEmpty()) {
        info.corePath = absoluteCorePath(corePath);
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

bool isLocalToolCoreLock(const CoreFileLockInfo &info)
{
    return info.tool.isEmpty()
        || info.tool.compare(QStringLiteral("qucs-s"), Qt::CaseInsensitive) == 0;
}

bool isStaleCoreLock(const CoreFileLockInfo &info)
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

    if (isCoreLockHeldByCurrentProcess(info)) {
        return false;
    }

    // xschem (and other WSL tools) store Linux PIDs — check via wsl.exe.
    if (!isLocalToolCoreLock(info)) {
        return !isWslProcessAlive(info.pid);
    }

    return !isWindowsProcessAlive(info.pid);
}

bool isCoreLockHeldByCurrentProcess(const CoreFileLockInfo &info)
{
    if (!info.present || !info.parseOk) {
        return false;
    }

    return info.pid == static_cast<int>(currentProcessId())
        && info.user.compare(currentUserName(), Qt::CaseInsensitive) == 0
        && info.host.compare(currentHostName(), Qt::CaseInsensitive) == 0;
}

CoreLockAcquireResult tryAcquireCoreLock(const QString &corePath, QString *errorMessage)
{
    const QString absPath = absoluteCorePath(corePath);
    const QString lockPath = lockFilePathForCore(absPath);

    CoreFileLockInfo existing = readCoreLockFile(absPath);
    if (existing.present) {
        if (isStaleCoreLock(existing)) {
            QFile::remove(lockPath);
        } else if (isCoreLockHeldByCurrentProcess(existing)) {
            return CoreLockAcquireResult::AlreadyHeld;
        } else {
            if (errorMessage != nullptr) {
                *errorMessage = formatCoreLockStatusLine(existing);
            }
            return CoreLockAcquireResult::ForeignLock;
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
    root.insert(QStringLiteral("corePath"), absPath);
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
        return CoreLockAcquireResult::Error;
    }

    if (tempFile.write(payload) != payload.size()) {
        tempFile.remove();
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Could not write lock file.");
        }
        return CoreLockAcquireResult::Error;
    }
    tempFile.close();

    if (QFile::exists(lockPath) && !QFile::remove(lockPath)) {
        QFile::remove(tempPath);
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Could not replace existing lock file.");
        }
        return CoreLockAcquireResult::ForeignLock;
    }

    if (!QFile::rename(tempPath, lockPath)) {
        QFile::remove(tempPath);
        if (errorMessage != nullptr) {
            *errorMessage = QObject::tr("Could not finalize lock file.");
        }
        return CoreLockAcquireResult::Error;
    }

    return CoreLockAcquireResult::Acquired;
}

bool releaseCoreLock(const QString &corePath)
{
    const QString lockPath = lockFilePathForCore(corePath);
    if (!QFileInfo::exists(lockPath)) {
        return true;
    }

    const CoreFileLockInfo info = readCoreLockFile(corePath);
    if (info.present && info.parseOk && !isCoreLockHeldByCurrentProcess(info) && !isStaleCoreLock(info)) {
        return false;
    }

    return QFile::remove(lockPath);
}

QString formatCoreLockStatusLine(const CoreFileLockInfo &info)
{
    if (!info.present) {
        return QObject::tr("CORE lock: none");
    }

    if (!info.parseOk) {
        return QObject::tr("CORE lock: present but unreadable");
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

QString formatCoreLockInfoBlock(const CoreFileLockInfo &info)
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

int coreLockAddRef(const QString &corePath)
{
    const QString absPath = absoluteCorePath(corePath);
    return ++lockRefCounts()[absPath];
}

int coreLockReleaseRef(const QString &corePath)
{
    const QString absPath = absoluteCorePath(corePath);
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

int coreLockRefCount(const QString &corePath)
{
    return lockRefCounts().value(absoluteCorePath(corePath), 0);
}
