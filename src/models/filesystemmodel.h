#pragma once

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QFileInfo>
#include <QStorageInfo>
#include <QFutureWatcher>
#include <QDir>
#include <QList>
#include <QProcess>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class GitStatusService;

class FileSystemModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString rootPath READ rootPath NOTIFY rootPathChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY countsChanged)
    Q_PROPERTY(int folderCount READ folderCount NOTIFY countsChanged)
    // Space on the filesystem holding rootPath, or -1 where a filesystem is
    // not what the listing is. countsChanged is the notify signal because the
    // listing and the disk figures are refreshed by the same events.
    Q_PROPERTY(qint64 diskFree READ diskFree NOTIFY countsChanged)
    Q_PROPERTY(qint64 diskTotal READ diskTotal NOTIFY countsChanged)
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)

public:
    enum Roles {
        FileNameRole = Qt::UserRole + 1,
        FilePathRole,
        FileSizeRole,
        FileSizeTextRole,
        FileTypeRole,
        FileModifiedRole,
        FileModifiedTextRole,
        FilePermissionsRole,
        IsDirRole,
        IsSymlinkRole,
        FileIconNameRole,
        GitStatusRole,
        GitStatusIconRole,
        // True when the file is an image / video that the thumbnailer can
        // render. Computed via QMimeDatabase so it correctly handles
        // ambiguous extensions like .ts (TypeScript vs MPEG-TS).
        HasImagePreviewRole,
        HasVideoPreviewRole,
        HasPdfPreviewRole,
        // Optional detailed-view columns.
        FileOwnerRole,
        FileGroupRole,
        FileCreatedTextRole,
        FileAccessedTextRole,
        FileExtensionRole,
        MimeTypeRole,
        SymlinkTargetRole,
    };
    Q_ENUM(Roles)

    explicit FileSystemModel(QObject *parent = nullptr);
    ~FileSystemModel() override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString rootPath() const;
    bool showHidden() const;
    int fileCount() const;
    int folderCount() const;
    bool isLoading() const;

    Q_INVOKABLE void setRootPath(const QString &path);
    Q_INVOKABLE void setShowHidden(bool show);
    Q_INVOKABLE QString filePath(int row) const;
    Q_INVOKABLE bool isDir(int row) const;
    Q_INVOKABLE QString fileName(int row) const;
    Q_INVOKABLE void sortByColumn(const QString &column, bool ascending);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap fileProperties(const QString &path) const;
    Q_INVOKABLE QVariantMap folderItemCounts(const QStringList &paths) const;
    Q_INVOKABLE QVariantList availableApps(const QString &mimeType) const;
    Q_INVOKABLE QString defaultApp(const QString &mimeType) const;
    Q_INVOKABLE void setDefaultApp(const QString &mimeType, const QString &desktopFile);
    Q_INVOKABLE QVariantList allInstalledApps() const;
    Q_INVOKABLE bool setFilePermissions(const QString &path, int ownerAccess, int groupAccess, int otherAccess);
    Q_INVOKABLE QString homePath() const;
    Q_INVOKABLE QString standardPath(const QString &key) const;
    Q_INVOKABLE QVariantList pathSuggestions(const QString &input, int limit = 8) const;
    Q_INVOKABLE void invalidateRemoteCache(const QString &path = QString());

    // Tests need a predictable "rowCount is correct right after setRootPath()"
    // guarantee, so expose a switch that runs local scans on the calling
    // thread. Production code leaves this false and benefits from the
    // non-blocking QtConcurrent path.
    void setSynchronousReload(bool on) { m_synchronousReload = on; }

    void setGitStatusService(GitStatusService *service);

    qint64 diskFree() const;
    qint64 diskTotal() const;

signals:
    void rootPathChanged();
    void showHiddenChanged();
    void countsChanged();
    void isLoadingChanged();
    void watchedDirectoryChanged(const QString &path);

private:
    // Lazy per-row display cache. QMimeDatabase / QLocale / permission-string
    // work is deferred until data() actually asks for it, so navigation into
    // a directory with thousands of files doesn't pay for rows the view will
    // never render. First access populates all derived fields together.
    struct Entry {
        QFileInfo info;
        mutable QString iconName;
        mutable QString fileType;
        mutable QString sizeText;
        mutable QString modifiedText;
        mutable QString permissionsText;
        mutable QString owner;
        mutable QString group;
        mutable QString createdText;
        mutable QString accessedText;
        mutable QString mimeType;   // filled on first MimeTypeRole read only
        mutable bool hasImagePreview = false;
        mutable bool hasVideoPreview = false;
        mutable bool hasPdfPreview = false;
        mutable bool populated = false;
    };

    // Packaged so the worker can carry its own generation number back to
    // the handler, letting us drop results from scans the user has already
    // navigated away from even if the QFutureWatcher fired.
    struct LocalReloadResult {
        quint64 generation = 0;
        QList<Entry> entries;
    };

    void ensurePopulated(const Entry &entry) const;

    void reload();
    void reloadLocal();
    void reloadRemote();
    void reloadTrash();
    void cancelRemoteReload();
    void applyRemoteReload(const QString &rootPath, const QByteArray &output);
    bool applyRemoteDiff(const QList<QVariantMap> &newEntries);
    struct RemoteParsingResult {
        QList<QVariantMap> entries;
        int fileCount = 0;
        int folderCount = 0;
    };
    RemoteParsingResult parseRemoteOutput(const QString &rootPath, const QByteArray &output) const;
    void applyRemoteParsedEntries(const QString &rootPath, const QList<QVariantMap> &entries, int fileCount, int folderCount);
    void prefetchSubdirectories();
    void clearPrefetchWatchers();
    QList<QFutureWatcher<LocalReloadResult>*> m_prefetchWatchers;
    // Local scans go through a QtConcurrent future so the GUI thread never
    // blocks on QDir::entryInfoList. Generation counter ensures stale
    // results (user navigated away mid-scan) are discarded.
    void scheduleLocalReload(bool tryDiff);
    void cancelLocalReload();
    void applyLocalReload(LocalReloadResult result, bool tryDiff);
    static LocalReloadResult scanLocalEntries(quint64 generation,
                                              const QString &rootPath,
                                              bool showHidden,
                                              QDir::SortFlags sortFlags);
    QList<Entry> currentLocalEntries() const;
    void updateLocalCounts();
    bool applyLocalDiff(const QList<Entry> &newEntries);
    bool isTrashRoot() const;
    bool isRemoteRoot() const;
    QVariantMap remoteFileProperties(const QString &path) const;
    QVariantMap trashFileProperties(const QString &path) const;
    const QVariantMap *findTrashEntry(const QString &path) const;
    void setIsLoading(bool loading);

    struct CachedRemoteDirectory {
        qint64 timestamp = 0;
        QList<QVariantMap> entries;
        int fileCount = 0;
        int folderCount = 0;
    };

    struct CachedLocalDirectory {
        qint64 timestamp = 0;
        QList<Entry> entries;
        int fileCount = 0;
        int folderCount = 0;
    };



    mutable QHash<QString, CachedRemoteDirectory> m_remoteDirCache;
    mutable QHash<QString, CachedLocalDirectory> m_slowPathCache;

    // QStorageInfo for rootPath, or an invalid one where reporting a disk
    // would mislead.
    QStorageInfo rootStorage() const;

    QString m_rootPath;
    bool m_showHidden = false;
    QList<Entry> m_entries;
    QList<QVariantMap> m_remoteEntries;
    QList<QVariantMap> m_trashEntries;
    int m_fileCount = 0;
    int m_folderCount = 0;
    GitStatusService *m_gitService = nullptr;
    QProcess *m_remoteReloadProcess = nullptr;
    int m_remoteReloadGeneration = 0;
    QFutureWatcher<LocalReloadResult> *m_localReloadWatcher = nullptr;
    quint64 m_localReloadGeneration = 0;
    bool m_localReloadTryDiff = false;
    bool m_synchronousReload = false;
    QFileSystemWatcher m_watcher;
    QTimer m_refreshDebounce;
    QDir::SortFlags m_sortFlags = QDir::Name | QDir::DirsFirst | QDir::IgnoreCase;
    QString m_sortColumn = "name";
    bool m_sortAscending = true;
    bool m_isLoading = false;
};
