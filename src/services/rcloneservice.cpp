#include "services/rcloneservice.h"

#include "services/cloudmounts.h"

#include <QDir>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QDebug>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

RcloneService::RcloneService(QObject *parent)
    : QObject(parent)
{
    m_rcloneAvailable = checkRcloneAvailable();
    m_mountsBaseDir = cloudMountsBaseDir();
    ensureMountsBaseDirExists();
}

RcloneService::~RcloneService()
{
    // unmountRemote() drives fusermount through the event loop, and by the
    // time we are destroyed there is no event loop left to run it: the
    // unmount would never happen and ~QProcess would SIGKILL rclone with the
    // FUSE mount still attached, leaving a dead mount point behind. Detach
    // fusermount so it outlives us, then ask rclone to go away.
    const QStringList remotes = m_processes.keys();
    for (const QString &remote : remotes) {
        const QString mountPath = getMountPath(remote);
        if (!QProcess::startDetached(QStringLiteral("fusermount"), {QStringLiteral("-u"), mountPath}))
            QProcess::startDetached(QStringLiteral("fusermount3"), {QStringLiteral("-u"), mountPath});

        QProcess *proc = m_processes.value(remote);
        if (proc && proc->state() != QProcess::NotRunning)
            proc->terminate();
    }
    m_processes.clear();
}

bool RcloneService::rcloneAvailable() const
{
    return m_rcloneAvailable;
}

QStringList RcloneService::activeMounts() const
{
    return m_processes.keys();
}

bool RcloneService::checkRcloneAvailable() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("rclone")).isEmpty();
}

void RcloneService::ensureMountsBaseDirExists() const
{
    QDir().mkpath(m_mountsBaseDir);
}

bool RcloneService::isRclonePath(const QString &path) const
{
    return isCloudMountPath(path);
}

bool RcloneService::isMounted(const QString &remoteName) const
{
    return m_processes.contains(remoteName) && m_mountSuccessEmitted.value(remoteName);
}

bool RcloneService::isMounting(const QString &remoteName) const
{
    return m_processes.contains(remoteName) && !m_mountSuccessEmitted.value(remoteName);
}

bool RcloneService::isMountedForPath(const QString &path) const
{
    const QString remote = getRemoteNameFromPath(path);
    if (remote.isEmpty())
        return false;
    return isMounted(remote);
}

QString RcloneService::getRemoteNameFromPath(const QString &path) const
{
    if (!isRclonePath(path))
        return {};

    QString sub = path.mid(m_mountsBaseDir.length());
    if (sub.startsWith(QLatin1Char('/'))) {
        sub = sub.mid(1);
    }
    int slashIdx = sub.indexOf(QLatin1Char('/'));
    if (slashIdx != -1) {
        return sub.left(slashIdx);
    }
    return sub;
}

QString RcloneService::getMountPath(const QString &remoteName) const
{
    return m_mountsBaseDir + QStringLiteral("/") + remoteName;
}

void RcloneService::mountRemote(const QString &remoteName)
{
    if (!m_rcloneAvailable) {
        emit mountFinished(remoteName, false, QStringLiteral("rclone executable not found"));
        return;
    }

    if (isMounted(remoteName)) {
        emit mountFinished(remoteName, true, QString());
        return;
    }

    if (isMounting(remoteName)) {
        return;
    }

    const QString mountPath = getMountPath(remoteName);
    QDir().mkpath(mountPath);

    // Clear any stale mount left behind by an earlier run before starting.
    releaseMountPoint(mountPath, [this, remoteName, mountPath]() {
        startRcloneMountProcess(remoteName, mountPath);
    });
}

// fusermount(1) belongs to libfuse2 and fusermount3(1) to libfuse3; a distro
// ships one, the other, or neither. Try both and continue either way: a
// cleanup that cannot run is no reason to strand the caller, and a missing
// binary emits errorOccurred rather than finished, so a chain hung off
// finished alone would simply stop here and never call anybody back.
void RcloneService::releaseMountPoint(const QString &mountPath, const std::function<void()> &then)
{
    runUnmountTool(QStringLiteral("fusermount"), mountPath, [this, mountPath, then](bool ok) {
        if (ok) {
            then();
            return;
        }
        runUnmountTool(QStringLiteral("fusermount3"), mountPath, [then](bool) { then(); });
    });
}

void RcloneService::runUnmountTool(const QString &tool, const QString &mountPath,
                                   const std::function<void(bool)> &done)
{
    QProcess *proc = new QProcess(this);
    const auto settle = [proc, done](bool ok) {
        proc->disconnect();
        proc->deleteLater();
        done(ok);
    };

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [settle](int exitCode, QProcess::ExitStatus status) {
        settle(status == QProcess::NormalExit && exitCode == 0);
    });
    connect(proc, &QProcess::errorOccurred, this, [settle](QProcess::ProcessError) {
        settle(false);
    });

    proc->start(tool, {QStringLiteral("-u"), mountPath});
}

void RcloneService::startRcloneMountProcess(const QString &remoteName, const QString &mountPath)
{
    QProcess *proc = new QProcess(this);
    const quint64 generation = ++m_mountGeneration[remoteName];
    m_processes.insert(remoteName, proc);
    m_mountSuccessEmitted[remoteName] = false;
    emit activeMountsChanged();

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, remoteName, mountPath, proc](int exitCode, QProcess::ExitStatus) {
        const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        m_processes.remove(remoteName);
        emit activeMountsChanged();

        if (!m_mountSuccessEmitted.value(remoteName)) {
            if (QDir(mountPath).exists())
                QDir(mountPath).removeRecursively();
            emit mountFinished(remoteName, false, err.isEmpty() ? QStringLiteral("rclone mount exited unexpectedly.") : err);
        }
        m_mountSuccessEmitted.remove(remoteName);
        proc->deleteLater();
    });

    connect(proc, &QProcess::errorOccurred, this, [this, remoteName, mountPath, proc](QProcess::ProcessError) {
        const QString err = proc->errorString();
        m_processes.remove(remoteName);
        emit activeMountsChanged();

        if (!m_mountSuccessEmitted.value(remoteName)) {
            if (QDir(mountPath).exists())
                QDir(mountPath).removeRecursively();
            emit mountFinished(remoteName, false, err);
        }
        m_mountSuccessEmitted.remove(remoteName);
        proc->deleteLater();
    });

    proc->start(QStringLiteral("rclone"), {
        QStringLiteral("mount"),
        remoteName + QStringLiteral(":"),
        mountPath,
        QStringLiteral("--vfs-cache-mode"),
        QStringLiteral("full"),
        QStringLiteral("--dir-cache-time"),
        QStringLiteral("24h"),
        QStringLiteral("--attr-timeout"),
        QStringLiteral("24h"),
        QStringLiteral("--vfs-cache-max-age"),
        QStringLiteral("168h"),
        QStringLiteral("--vfs-read-ahead"),
        QStringLiteral("128M"),
        QStringLiteral("--no-checksum"),
        QStringLiteral("--vfs-fast-fingerprint"),
        QStringLiteral("--vfs-read-chunk-size"),
        QStringLiteral("1M"),
        QStringLiteral("--vfs-read-chunk-size-limit"),
        QStringLiteral("off"),
        QStringLiteral("--buffer-size"),
        QStringLiteral("32M"),
        QStringLiteral("--poll-interval"),
        QStringLiteral("15s")
    });

    // Poll mountpoint checks every 100ms to verify FUSE mount is active
    QTimer *mountTimer = new QTimer(this);
    connect(mountTimer, &QTimer::timeout, this, [this, remoteName, mountPath, mountTimer]() {
        if (!m_processes.contains(remoteName) || m_processes.value(remoteName)->state() != QProcess::Running) {
            mountTimer->stop();
            mountTimer->deleteLater();
            return;
        }

        struct stat st_dir, st_parent;
        QString parentPath = QDir(mountPath).filePath(QStringLiteral(".."));
        if (stat(mountPath.toLocal8Bit().constData(), &st_dir) == 0 &&
            stat(parentPath.toLocal8Bit().constData(), &st_parent) == 0) {

            if (st_dir.st_dev != st_parent.st_dev) {
                mountTimer->stop();
                mountTimer->deleteLater();

                m_mountSuccessEmitted[remoteName] = true;
                emit mountFinished(remoteName, true, QString());
            }
        }
    });

    // Timeout safety: if it doesn't mount in 10 seconds, abort. The poll timer
    // deletes itself as soon as the process is gone, so hold it weakly, and
    // bail out when a newer attempt has taken over this remote -- otherwise a
    // remount inside the timeout window would tear down the fresh mount.
    QPointer<QTimer> pollTimer(mountTimer);
    QTimer::singleShot(10000, this, [this, remoteName, generation, pollTimer]() {
        if (m_mountGeneration.value(remoteName) != generation)
            return;
        if (!m_processes.contains(remoteName) || m_mountSuccessEmitted.value(remoteName))
            return;

        if (pollTimer) {
            pollTimer->stop();
            pollTimer->deleteLater();
        }

        unmountRemote(remoteName);
        emit mountFinished(remoteName, false, QStringLiteral("Mount operation timed out. Verify your rclone remote or network connection."));
    });

    mountTimer->start(100);
}

void RcloneService::unmountRemote(const QString &remoteName)
{
    if (!m_processes.contains(remoteName)) {
        emit unmountFinished(remoteName, true);
        return;
    }

    QProcess *proc = m_processes.value(remoteName);
    if (proc) {
        disconnect(proc, nullptr, this, nullptr);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), proc, &QObject::deleteLater);
        proc->terminate();
        QTimer::singleShot(2000, proc, [proc]() {
            if (proc->state() != QProcess::NotRunning) {
                proc->kill();
            }
        });
    }

    m_processes.remove(remoteName);
    m_mountSuccessEmitted.remove(remoteName);
    emit activeMountsChanged();

    // Release the FUSE mount point without blocking the UI.
    releaseMountPoint(getMountPath(remoteName), [this, remoteName]() {
        emit unmountFinished(remoteName, true);
    });
}
