#pragma once

#include <QObject>
#include <QProcess>
#include <QByteArray>
#include <QHash>
#include <QAtomicInteger>
#include <QSharedPointer>
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
    Q_PROPERTY(QStringList pendingTargetPaths READ pendingTargetPaths NOTIFY activeTransfersChanged)

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
    QStringList pendingTargetPaths() const;

    Q_INVOKABLE void pauseTransfer(int transferId = -1);
    Q_INVOKABLE void resumeTransfer(int transferId = -1);
    Q_INVOKABLE void cancelTransfer(int transferId = -1);

    Q_INVOKABLE int copyFiles(const QStringList &sources, const QString &destination);
    Q_INVOKABLE int copyResolvedItems(const QVariantList &operations);
    Q_INVOKABLE int moveFiles(const QStringList &sources, const QString &destination);
    Q_INVOKABLE int moveResolvedItems(const QVariantList &operations);
    Q_INVOKABLE int trashFiles(const QStringList &paths);
    Q_INVOKABLE int restoreFromTrash(const QStringList &paths);
    Q_INVOKABLE bool isTrashPath(const QString &path) const;
    Q_INVOKABLE QString trashFilesPathFor(const QString &path) const;
    Q_INVOKABLE QVariantList transferPlan(const QStringList &sources, const QString &destination) const;
    Q_INVOKABLE QString uniqueNameForDestination(const QString &destinationDir, const QString &desiredName,
                                                 const QStringList &blockedNames = {}) const;
    QString conflictBackupPath(const QString &targetPath) const;
    Q_INVOKABLE int deleteFiles(const QStringList &paths);
    Q_INVOKABLE bool rename(const QString &path, const QString &newName);
    Q_INVOKABLE QVariantMap renameResolvedItems(const QVariantList &operations);
    Q_INVOKABLE void createFolder(const QString &parentPath, const QString &name);
    Q_INVOKABLE void createFile(const QString &parentPath, const QString &name);
    Q_INVOKABLE void openFile(const QString &path);
    Q_INVOKABLE void openInEditor(const QString &path);
    Q_INVOKABLE bool pathExists(const QString &path) const;
    Q_INVOKABLE bool isRemotePath(const QString &path) const;
    Q_INVOKABLE bool isSlowPath(const QString &path) const;
    Q_INVOKABLE QString parentPath(const QString &path) const;
    Q_INVOKABLE QString displayNameForPath(const QString &path) const;
    Q_INVOKABLE QVariantList breadcrumbSegments(const QString &path) const;
    Q_INVOKABLE int emptyTrash();
    Q_INVOKABLE void openFileWith(const QString &path, const QString &desktopFile);
    static QStringList desktopExecArguments(const QString &execLine, const QString &file);
    Q_INVOKABLE bool hasClipboardImage() const;
    Q_INVOKABLE QString pasteClipboardImage(const QString &destinationDir);
    Q_INVOKABLE void copyPathToClipboard(const QString &path);
    Q_INVOKABLE void openInTerminal(const QString &dirPath);
    Q_INVOKABLE void runCustomAction(const QString &command, const QStringList &paths);
    Q_INVOKABLE void openNewWindow(const QString &dirPath);
    Q_INVOKABLE int compressFiles(const QStringList &paths, const QString &format);
    Q_INVOKABLE int extractArchive(const QString &archivePath, const QString &destination);
    Q_INVOKABLE int extractArchive(const QString &archivePath, const QString &destination,
                                   const QString &password);
    // In-memory only, never persisted, and held only while the archive is in
    // use: an extraction clears it when it finishes, and the preview clears it
    // when it moves off the file. Same scope Ark and File Roller use.
    Q_INVOKABLE QString archivePassword(const QString &archivePath) const;
    Q_INVOKABLE void cacheArchivePassword(const QString &archivePath, const QString &password);
    Q_INVOKABLE void clearArchivePassword(const QString &archivePath);
    Q_INVOKABLE QString newExtractionFolder(const QString &archivePath);
    Q_INVOKABLE static bool isArchive(const QString &path);
    // A reported (current, total) as a fraction for the UI: always 0..1, or
    // -1 when there is no total to measure against. Clamped here because a
    // miscounted total should slow the bar down, never send it past full.
    static double progressFraction(int current, int total);
    Q_INVOKABLE void setWallpaper(const QString &path);
    Q_INVOKABLE void setHyprlandRounding(const QString &windowTitle, int radius);
    Q_INVOKABLE void setHyprlandBorder(const QString &windowTitle, int size);

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
    // An encrypted archive rejected the current password; the UI should ask
    // the user and retry extractArchive() with the password it gets.
    // `retry` is true when a password had already been supplied and was
    // wrong, so the dialog can say so instead of reopening unchanged.
    void passwordRequested(const QString &archivePath, const QString &destination,
                           bool retry);
    // operationId matches the value returned by the operation that started
    // it (-1 for synchronous failures), so callers waiting on one operation
    // are not satisfied by another one finishing first.
    void operationFinished(bool success, const QString &error, int operationId = -1);

private:
    struct ActiveTransfer {
        int id = 0;
        QThread *thread = nullptr;
        GioTransferWorker *worker = nullptr;
        // Shared state for simple (external subprocess) operations: atomics
        // read/written by the GUI and worker threads without touching each
        // other's QObject instances.
        QSharedPointer<QAtomicInt> processId;
        QSharedPointer<QAtomicInt> cancelled;
        QSharedPointer<QAtomicInt> pauseRequested;
        QString statusText;
        double progress = -1.0;
        QString speed;
        QString eta;
        QString currentFile;
        bool paused = false;
        QStringList changedPaths;
        QStringList targetPaths;
    };

    int transferResolvedItems(const QVariantList &operations, bool moveOperation);
    void resetTransferState();
    void setProgressValue(double progress, const QString &speed = {}, const QString &eta = {});
    void setPendingChangedPaths(const QStringList &paths);
    void emitPendingChangedPaths();
    void emitChangedPaths(const QStringList &paths);
    void runProcess(const QString &program, const QStringList &args);
    QByteArray clipboardImageData() const;
    QString uniqueImagePastePath(const QString &destinationDir) const;
    int startGioTransfer(const QVariantList &operations, bool moveOperation);
    using ProgressReporter = std::function<void(int current, int total, const QString &fileName)>;
    int startSimpleOperation(const QString &statusText, const QStringList &changedPaths,
                              std::function<QString(ProgressReporter)> work,
                              const QSharedPointer<QAtomicInt> &processId = {},
                              const QSharedPointer<QAtomicInt> &cancelled = {},
                              const QSharedPointer<QAtomicInt> &pauseRequested = {});
    void cleanupTransfer(int transferId);

    // path -> password, for as long as that archive is in use.
    QHash<QString, QString> m_archivePasswords;

    // Folders newExtractionFolder() made. Only these are ours to remove when
    // an extraction fails without unpacking anything: "Extract Here" targets a
    // directory the user already had.
    QSet<QString> m_ownedExtractionDirs;
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
