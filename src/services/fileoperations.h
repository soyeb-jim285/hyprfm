#pragma once

#include <QObject>
#include <QProcess>
#include <QByteArray>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

class QThread;
class GioTransferWorker;

class FileOperations : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString speed READ speed NOTIFY speedChanged)
    Q_PROPERTY(QString eta READ eta NOTIFY etaChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY currentFileChanged)
    Q_PROPERTY(QVariantList activeTransfers READ activeTransfers NOTIFY activeTransfersChanged)

public:
    explicit FileOperations(QObject *parent = nullptr);

    bool busy() const;
    double progress() const;
    QString statusText() const;
    QString speed() const;
    QString eta() const;
    bool paused() const;
    QString currentFile() const;
    QVariantList activeTransfers() const;

    Q_INVOKABLE void pauseTransfer(int transferId = -1);
    Q_INVOKABLE void resumeTransfer(int transferId = -1);
    Q_INVOKABLE void cancelTransfer(int transferId = -1);

    Q_INVOKABLE void copyFiles(const QStringList &sources, const QString &destination);
    Q_INVOKABLE void copyResolvedItems(const QVariantList &operations);
    Q_INVOKABLE void moveFiles(const QStringList &sources, const QString &destination);
    Q_INVOKABLE void moveResolvedItems(const QVariantList &operations);
    Q_INVOKABLE void trashFiles(const QStringList &paths);
    Q_INVOKABLE void restoreFromTrash(const QStringList &paths);
    Q_INVOKABLE bool isTrashPath(const QString &path) const;
    Q_INVOKABLE QString trashFilesPathFor(const QString &path) const;
    Q_INVOKABLE QVariantList transferPlan(const QStringList &sources, const QString &destination) const;
    Q_INVOKABLE QString uniqueNameForDestination(const QString &destinationDir, const QString &desiredName,
                                                 const QStringList &blockedNames = {}) const;
    QString conflictBackupPath(const QString &targetPath) const;
    Q_INVOKABLE void deleteFiles(const QStringList &paths);
    Q_INVOKABLE bool rename(const QString &path, const QString &newName);
    Q_INVOKABLE QVariantMap renameResolvedItems(const QVariantList &operations);
    Q_INVOKABLE void createFolder(const QString &parentPath, const QString &name);
    Q_INVOKABLE void createFile(const QString &parentPath, const QString &name);
    Q_INVOKABLE void openFile(const QString &path);
    Q_INVOKABLE bool pathExists(const QString &path) const;
    Q_INVOKABLE bool isRemotePath(const QString &path) const;
    Q_INVOKABLE QString parentPath(const QString &path) const;
    Q_INVOKABLE QString displayNameForPath(const QString &path) const;
    Q_INVOKABLE QVariantList breadcrumbSegments(const QString &path) const;
    Q_INVOKABLE void emptyTrash();
    Q_INVOKABLE void openFileWith(const QString &path, const QString &desktopFile);
    Q_INVOKABLE bool hasClipboardImage() const;
    Q_INVOKABLE QString pasteClipboardImage(const QString &destinationDir);
    Q_INVOKABLE void copyPathToClipboard(const QString &path);
    Q_INVOKABLE void openInTerminal(const QString &dirPath);
    Q_INVOKABLE void compressFiles(const QStringList &paths, const QString &format);
    Q_INVOKABLE void extractArchive(const QString &archivePath, const QString &destination);
    Q_INVOKABLE static bool isArchive(const QString &path);
    Q_INVOKABLE QString archiveRootFolder(const QString &archivePath);

signals:
    void busyChanged();
    void progressChanged();
    void statusTextChanged();
    void speedChanged();
    void etaChanged();
    void pausedChanged();
    void currentFileChanged();
    void activeTransfersChanged();
    void pathsChanged(const QStringList &paths);
    void operationFinished(bool success, const QString &error);

private:
    struct ActiveTransfer {
        int id = 0;
        QThread *thread = nullptr;
        GioTransferWorker *worker = nullptr;
        QString statusText;
        double progress = -1.0;
        QString speed;
        QString eta;
        QString currentFile;
        bool paused = false;
        QStringList changedPaths;
    };

    void transferResolvedItems(const QVariantList &operations, bool moveOperation);
    void resetTransferState();
    void setProgressValue(double progress, const QString &speed = {}, const QString &eta = {});
    void setPendingChangedPaths(const QStringList &paths);
    void emitPendingChangedPaths();
    void emitChangedPaths(const QStringList &paths);
    void runProcess(const QString &program, const QStringList &args);
    QByteArray clipboardImageData() const;
    QString uniqueImagePastePath(const QString &destinationDir) const;
    void startGioTransfer(const QVariantList &operations, bool moveOperation);
    using ProgressReporter = std::function<void(int current, int total, const QString &fileName)>;
    void startSimpleOperation(const QString &statusText, const QStringList &changedPaths,
                              std::function<QString(ProgressReporter)> work);
    void cleanupTransfer(int transferId);
    void emitAggregatedState();
    ActiveTransfer *findTransfer(int id);

    QProcess *m_process = nullptr;
    bool m_busy = false;
    double m_progress = 0.0;
    QString m_statusText;
    QString m_speed;
    QString m_eta;
    bool m_paused = false;
    QString m_currentFile;
    QList<ActiveTransfer> m_activeTransfers;
    int m_nextTransferId = 1;
    QByteArray m_processErrorOutput;
    QStringList m_pendingChangedPaths;
};
