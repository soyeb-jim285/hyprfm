#include "services/diskusageservice.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLocale>
#include <QMetaObject>
#include <QSet>
#include <QTimer>
#include <QUrl>

namespace {

bool isLocalPath(const QString &path)
{
    if (path.isEmpty())
        return false;

    const QUrl url(path);
    return !url.isValid() || url.scheme().isEmpty() || url.isLocalFile();
}

}

DiskUsageWorker::DiskUsageWorker(QObject *parent)
    : QObject(parent)
{
}

void DiskUsageWorker::calculate(int requestId, const QStringList &paths)
{
    m_cancelled.store(false);

    qint64 totalSize = 0;
    int unreadableCount = 0;
    QVariantMap pathSizes;

    for (const QString &path : paths) {
        if (m_cancelled.load())
            break;

        const qint64 size = pathSize(path, &unreadableCount);
        totalSize += size;
        pathSizes.insert(path, size);
    }

    emit finished(requestId, totalSize, pathSizes, unreadableCount, m_cancelled.load());
}

void DiskUsageWorker::cancel()
{
    m_cancelled.store(true);
}

qint64 DiskUsageWorker::pathSize(const QString &path, int *unreadableCount) const
{
    const QFileInfo info(path);
    if (!info.exists()) {
        if (unreadableCount)
            ++(*unreadableCount);
        return 0;
    }

    if (info.isDir()) {
        QString directoryPath = path;
        if (info.isSymLink()) {
            const QString canonicalPath = info.canonicalFilePath();
            if (!canonicalPath.isEmpty())
                directoryPath = canonicalPath;
            else if (!info.symLinkTarget().isEmpty())
                directoryPath = QDir::cleanPath(info.symLinkTarget());
        }

        return directorySize(directoryPath, unreadableCount);
    }

    return info.size();
}

qint64 DiskUsageWorker::directorySize(const QString &path, int *unreadableCount) const
{
    QDir dir(path);
    if (!dir.exists() || !dir.isReadable()) {
        if (unreadableCount)
            ++(*unreadableCount);
        return 0;
    }

    qint64 totalSize = 0;
    QDirIterator it(path,
                    QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);

    while (it.hasNext()) {
        if (m_cancelled.load())
            return totalSize;

        it.next();
        const QFileInfo info = it.fileInfo();

        if (info.isSymLink() && info.isDir())
            continue;

        if (info.isFile())
            totalSize += info.size();
    }

    return totalSize;
}

DiskUsageService::DiskUsageService(QObject *parent)
    : QObject(parent)
{
}

DiskUsageService::~DiskUsageService()
{
    const QList<int> requestIds = m_requests.keys();
    for (int requestId : requestIds)
        cancelRequest(requestId);
}

int DiskUsageService::requestSize(const QVariantList &paths)
{
    QStringList normalizedPaths;
    QSet<QString> seenPaths;
    qint64 cachedSize = 0;

    for (const QVariant &variant : paths) {
        const QString normalizedPath = normalizePath(variant.toString());
        if (normalizedPath.isEmpty() || seenPaths.contains(normalizedPath))
            continue;

        seenPaths.insert(normalizedPath);

        if (m_cache.contains(normalizedPath))
            cachedSize += m_cache.value(normalizedPath);
        else
            normalizedPaths.append(normalizedPath);
    }

    const int requestId = m_nextRequestId++;
    if (normalizedPaths.isEmpty()) {
        QVariantMap result;
        result.insert("size", cachedSize);
        result.insert("sizeText", formattedSize(cachedSize));
        result.insert("sizeTextVerbose", formattedSize(cachedSize, true));
        result.insert("unreadableCount", 0);

        QTimer::singleShot(0, this, [this, requestId, result]() {
            emit requestFinished(requestId, result);
        });
        return requestId;
    }

    QThread *thread = new QThread;
    DiskUsageWorker *worker = new DiskUsageWorker;
    worker->moveToThread(thread);

    ActiveRequest activeRequest;
    activeRequest.thread = thread;
    activeRequest.worker = worker;
    activeRequest.cachedSize = cachedSize;
    m_requests.insert(requestId, activeRequest);

    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(worker, &DiskUsageWorker::finished, thread, &QThread::quit);

    connect(worker, &DiskUsageWorker::finished, this,
            [this](int finishedRequestId, qint64 size, const QVariantMap &pathSizes,
                   int unreadableCount, bool cancelled) {
        if (!m_requests.contains(finishedRequestId))
            return;

        const ActiveRequest activeRequest = m_requests.value(finishedRequestId);
        cleanupRequest(finishedRequestId);

        if (cancelled)
            return;

        for (auto it = pathSizes.constBegin(); it != pathSizes.constEnd(); ++it)
            m_cache.insert(it.key(), it.value().toLongLong());

        const qint64 totalSize = activeRequest.cachedSize + size;

        QVariantMap result;
        result.insert("size", totalSize);
        result.insert("sizeText", formattedSize(totalSize));
        result.insert("sizeTextVerbose", formattedSize(totalSize, true));
        result.insert("unreadableCount", unreadableCount);
        emit requestFinished(finishedRequestId, result);
    });

    connect(thread, &QThread::started, worker,
            [worker, requestId, normalizedPaths]() { worker->calculate(requestId, normalizedPaths); });

    thread->start();
    return requestId;
}

void DiskUsageService::cancelRequest(int requestId)
{
    if (!m_requests.contains(requestId))
        return;

    const ActiveRequest activeRequest = m_requests.value(requestId);
    cleanupRequest(requestId);

    if (activeRequest.worker)
        activeRequest.worker->cancel();
    if (activeRequest.thread)
        activeRequest.thread->quit();
}

void DiskUsageService::clearCache()
{
    m_cache.clear();
}

QString DiskUsageService::normalizePath(const QString &path)
{
    if (!isLocalPath(path))
        return {};

    const QUrl url(path);
    const QString localPath = url.isLocalFile() ? url.toLocalFile() : path;
    return QDir::cleanPath(localPath);
}

QString DiskUsageService::formattedSize(qint64 size, bool verbose)
{
    if (size < 0)
        return {};
    if (size < 1024) {
        return verbose ? QStringLiteral("%1 B (%2 bytes)").arg(size).arg(QLocale().toString(size))
                       : QStringLiteral("%1 B").arg(size);
    }
    if (size < 1024 * 1024) {
        return verbose ? QStringLiteral("%1 KB (%2 bytes)")
                               .arg(size / 1024.0, 0, 'f', 1)
                               .arg(QLocale().toString(size))
                       : QStringLiteral("%1 KB").arg(size / 1024.0, 0, 'f', 1);
    }
    if (size < 1024LL * 1024 * 1024) {
        return verbose ? QStringLiteral("%1 MB (%2 bytes)")
                               .arg(size / (1024.0 * 1024.0), 0, 'f', 1)
                               .arg(QLocale().toString(size))
                       : QStringLiteral("%1 MB").arg(size / (1024.0 * 1024.0), 0, 'f', 1);
    }
    return verbose ? QStringLiteral("%1 GB (%2 bytes)")
                           .arg(size / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2)
                           .arg(QLocale().toString(size))
                   : QStringLiteral("%1 GB").arg(size / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
}

void DiskUsageService::cleanupRequest(int requestId)
{
    m_requests.remove(requestId);
}
