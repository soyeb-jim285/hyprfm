#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QList>
#include <QMutex>
#include <QString>
#include <QWaitCondition>

#include <atomic>

// GIO headers use 'signals' as a struct member, which conflicts with Qt's
// signals keyword. Temporarily undefine it before including GIO headers.
#undef signals
#include <gio/gio.h>
#define signals Q_SIGNALS

class GioTransferWorker : public QObject
{
    Q_OBJECT

public:
    struct TransferItem {
        QString sourcePath;
        QString targetPath;
        QString backupPath;
        bool overwrite = false;
    };

    explicit GioTransferWorker(QObject *parent = nullptr);

    void execute(const QList<TransferItem> &items, bool moveOperation);
    void cancel();
    void pause();
    void resume();

signals:
    void progressUpdated(double progress, const QString &speed, const QString &eta);
    void itemStarted(const QString &sourcePath, const QString &targetPath);
    void finished(bool success, const QString &error);

private:
    qint64 scanTotalBytes(const QList<TransferItem> &items);
    qint64 scanPathBytes(GFile *file);
    bool copyRecursive(GFile *source, GFile *destination, GFileCopyFlags flags, QString *error);
    bool deleteRecursive(GFile *file, QString *error);
    static void progressCallback(goffset currentBytes, goffset totalBytes, gpointer userData);
    void handleProgressCallback(goffset currentBytes, goffset totalBytes);
    void emitProgress();
    static QString gErrorToUserMessage(GError *error);

    std::atomic<bool> m_cancelled{false};
    std::atomic<bool> m_paused{false};
    QMutex m_mutex;
    QWaitCondition m_pauseCondition;
    GCancellable *m_cancellable = nullptr;

    qint64 m_totalBytes = 0;
    qint64 m_completedBytes = 0;
    qint64 m_currentItemBytes = 0;
    QElapsedTimer m_elapsed;
    qint64 m_lastEmitMs = 0;
};
