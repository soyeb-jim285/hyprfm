#pragma once

#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QHash>
#include <QPromise>
#include <QProcess>
#include <QPointer>
#include <QThreadPool>
#include <type_traits>
#include <utility>

// Mount metadata comes from procfs. Resolving a path through the VFS here
// could itself wait for the mount we are trying to isolate.
class MountTable
{
public:
    MountTable()
    {
        QFile file(QStringLiteral("/proc/self/mountinfo"));
        if (!file.open(QIODevice::ReadOnly))
            return;
        for (const QByteArray &line : file.readAll().split('\n')) {
            const auto fields = line.split(' ');
            if (fields.size() < 6)
                continue;
            QByteArray path = fields.at(4);
            path.replace("\\040", " ").replace("\\011", "\t").replace("\\012", "\n").replace("\\134", "\\");
            m_ids.insert(QFile::decodeName(path), fields.at(0));
        }
    }

    bool isMountPoint(const QString &path) const { return m_ids.contains(path); }

    QString mountPointForPath(QString path) const
    {
        if (!path.startsWith(QLatin1Char('/')))
            return {};
        path = QDir::cleanPath(path);
        while (!path.isEmpty()) {
            if (m_ids.contains(path))
                return path;
            if (path == QStringLiteral("/"))
                break;
            path = path.left(qMax(1, path.lastIndexOf(QLatin1Char('/'))));
        }
        return {};
    }

    QByteArray forPath(const QString &path) const { return m_ids.value(mountPointForPath(path)); }

private:
    QHash<QString, QByteArray> m_ids;
};

inline QThreadPool *filesystemTaskPool(const QByteArray &mount)
{
    // Used from the GUI thread. A blocked kernel call must neither occupy
    // another mount's worker slots nor make Qt join the global pool on exit.
    // Keep one pool for each mount that still has work; idle pools can expire.
    static auto *pools = new QHash<QByteArray, QThreadPool *>;
    for (auto it = pools->begin(); it != pools->end();) {
        if (it.key() != mount && it.value()->activeThreadCount() == 0) {
            delete it.value();
            it = pools->erase(it);
        } else {
            ++it;
        }
    }
    QThreadPool *&pool = (*pools)[mount];
    if (!pool) {
        pool = new QThreadPool;
        pool->setMaxThreadCount(4);
    }
    return pool;
}

inline void stopFilesystemProcess(QProcess *&process)
{
    if (!process)
        return;
    auto *stopped = std::exchange(process, nullptr);
    stopped->disconnect();
    if (stopped->state() == QProcess::NotRunning) {
        stopped->deleteLater();
        return;
    }
    // QProcess destruction waits for the child, even after sending SIGKILL.
    stopped->setParent(nullptr);
    QObject::connect(stopped, &QProcess::finished, stopped, &QObject::deleteLater);
    QObject::connect(stopped, &QProcess::errorOccurred, stopped, [stopped](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            stopped->deleteLater();
    });
    stopped->kill();
}

template <typename Job, typename Finished>
auto watchFilesystemTask(QObject *owner, const QByteArray &mount, Job job, Finished finished)
{
    using Result = std::invoke_result_t<Job>;
    QPromise<Result> promise;
    promise.start();
    auto future = promise.future();
    auto *watcher = new QFutureWatcher<Result>(owner);
    QObject::connect(watcher, &QObject::destroyed, [future]() mutable { future.cancel(); });
    QObject::connect(watcher, &QFutureWatcherBase::finished, owner, [watcher, finished]() mutable {
        const auto result = watcher->future();
        watcher->deleteLater();
        if (!result.isCanceled())
            finished(result.result());
    });
    watcher->setFuture(future);
    filesystemTaskPool(mount)->start([job = std::move(job), promise = std::move(promise)]() mutable {
        if (!promise.isCanceled())
            promise.addResult(job());
        promise.finish();
    });
    return watcher;
}
