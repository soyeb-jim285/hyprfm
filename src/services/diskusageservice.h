#pragma once

#include <QObject>
#include <QHash>
#include <QStringList>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>

class DiskUsageWorker : public QObject
{
    Q_OBJECT

public:
    explicit DiskUsageWorker(QObject *parent = nullptr);

    void calculate(int requestId, const QStringList &paths);
    void cancel();

signals:
    void finished(int requestId, qint64 size, const QVariantMap &pathSizes, int unreadableCount,
                  bool cancelled);

private:
    qint64 pathSize(const QString &path, int *unreadableCount) const;
    qint64 directorySize(const QString &path, int *unreadableCount) const;

    std::atomic<bool> m_cancelled{false};
};

class DiskUsageService : public QObject
{
    Q_OBJECT

public:
    explicit DiskUsageService(QObject *parent = nullptr);
    ~DiskUsageService() override;

    Q_INVOKABLE int requestSize(const QVariantList &paths);
    Q_INVOKABLE void cancelRequest(int requestId);
    Q_INVOKABLE void clearCache();

signals:
    void requestFinished(int requestId, const QVariantMap &result);

private:
    struct ActiveRequest {
        QThread *thread = nullptr;
        DiskUsageWorker *worker = nullptr;
        qint64 cachedSize = 0;
    };

    static QString normalizePath(const QString &path);
    static QString formattedSize(qint64 size, bool verbose = false);
    void cleanupRequest(int requestId);

    int m_nextRequestId = 1;
    QHash<int, ActiveRequest> m_requests;
    QHash<QString, qint64> m_cache;
};
