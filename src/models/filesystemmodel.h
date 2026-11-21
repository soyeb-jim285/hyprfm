#pragma once

#include <QAbstractListModel>
#include <QTimer>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QSocketNotifier>
#include <QDir>
#include <QList>
#include <QProcess>
#include <QString>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>
#include <limits>
#include <memory>
#include <utility>

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
    // Why the folder could not be listed ("" when it could). Without it an
    // unreadable folder looks the same as an empty one (#74).
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadErrorChanged)

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
    QString loadError() const { return m_loadError; }

    Q_INVOKABLE void setRootPath(const QString &path);
    void setHiddenLast(bool last);
    Q_INVOKABLE void setShowHidden(bool show);
    Q_INVOKABLE QString filePath(int row) const;
    Q_INVOKABLE bool isDir(int row) const;
    Q_INVOKABLE QString fileName(int row) const;
    Q_INVOKABLE void sortByColumn(const QString &column, bool ascending);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap fileProperties(const QString &path) const;
    Q_INVOKABLE QVariantMap folderItemCounts(const QStringList &paths) const { return countFolderItems(paths); }
    // Same counts, computed on a worker; returns the id folderItemCountsReady carries.
    Q_INVOKABLE int requestFolderItemCounts(const QStringList &paths);
    Q_INVOKABLE QVariantList availableApps(const QString &mimeType) const;
    Q_INVOKABLE QString defaultApp(const QString &mimeType) const;
    Q_INVOKABLE void setDefaultApp(const QString &mimeType, const QString &desktopFile);
    Q_INVOKABLE QVariantList allInstalledApps() const;
    Q_INVOKABLE bool setFilePermissions(const QString &path, int ownerAccess, int groupAccess, int otherAccess);
    Q_INVOKABLE QString homePath() const;
    Q_INVOKABLE QString standardPath(const QString &key) const;
    Q_INVOKABLE int requestPathSuggestions(const QString &input, int limit = 8);
    QVariantList pathSuggestions(const QString &input, int limit = 8) const;

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
    void loadErrorChanged();
    void watchedDirectoryChanged(const QString &path);
    // The full properties for a pending fileProperties() response.
    void propertiesReady(const QString &path, const QVariantMap &properties);
    void pathSuggestionsReady(int requestId, const QVariantList &suggestions);
    void folderItemCountsReady(int requestId, const QVariantMap &counts);

private:
    static constexpr qint64 kNoTime = std::numeric_limits<qint64>::min();

    // Lazy per-row display cache. QMimeDatabase / QLocale / permission-string
    // work is deferred until data() actually asks for it, so navigation into
    // a directory with thousands of files doesn't pay for rows the view will
    // never render. First access populates all derived fields together.
    struct Details {
        QString iconName;
        QString fileType;
        QString sizeText;
        QString modifiedText;
        QString permissionsText;
        QString owner;
        QString group;
        QString createdText;
        QString accessedText;
        QString mimeType;   // also filled alone by a MimeTypeRole read
        QString symlinkTarget;
        bool hasImagePreview = false;
        bool hasVideoPreview = false;
        bool hasPdfPreview = false;
        bool populated = false;
    };

    // One row of a local listing: only what every row needs, ~100 bytes.
    // It used to hold a QFileInfo (~900 bytes with its cached paths and
    // metadata) plus the display strings inline, ~12 MB per 10,000 files.
    // The directory is m_entryPrefix; the strings live in Details, allocated
    // the first time a row is shown.
    // size and modifiedMs need a stat, which the scan only has for free when
    // QDir sorted by size or time; otherwise the first read of either stats
    // that one file (see ensureStat), so the scan never stats every entry.
    struct Entry {
        QString name;
        mutable qint64 size = 0;            // 0 for directories
        mutable qint64 modifiedMs = kNoTime;
        bool isDir = false;
        bool isSymLink = false;
        mutable bool statted = false;
        mutable std::shared_ptr<Details> details;
    };
    static QList<Entry> readLocalEntries(const QString &rootPath, bool showHidden, bool wantStat,
                                         int *error = nullptr);
    static void sortEntries(QList<Entry> &entries, QDir::SortFlags flags, bool hiddenLast = false);
    static QVariantMap countFolderItems(const QStringList &paths);
    static void ensureStat(const Entry &entry, const QFileInfo &info);
    QString entryPath(const Entry &entry) const { return m_entryPrefix + entry.name; }
    Details &detailsOf(const Entry &entry) const;
    static void populateEntry(const Entry &entry, const QString &path);
    void requestEntryDetails(const QModelIndex &index, const Entry &entry) const;
    void loadVisibleDetails();

    // Packaged so the worker can carry its own generation number back to
    // the handler, letting us drop results from scans the user has already
    // navigated away from even if the QFutureWatcher fired.
    struct DirectoryWatch;
    struct LocalReloadResult {
        quint64 generation = 0;
        QList<Entry> entries;
        int error = 0;   // errno from opening the folder
        std::shared_ptr<DirectoryWatch> watch;
    };


    void reload();
    void reloadLocal();
    void reloadRemote();
    void startRemoteList(const QString &rootPath, int generation, bool afterMount);
    void startRemoteMount(const QString &rootPath, int generation);
    void reloadTrash();
    static QList<QVariantMap> scanTrash(const QString &sortColumn, bool sortAscending, bool hiddenLast);
    static QVariantMap localFileProperties(const QString &path);
    static QVariantList collectPathSuggestions(const QString &input, int limit, bool showHidden);
    void cancelRemoteReload();
    void applyRemoteReload(const QString &rootPath, const QByteArray &output);
    // Generation counter discards results from a folder the user has left.
    void scheduleLocalReload(bool tryDiff);
    void applyLocalReload(LocalReloadResult result, bool tryDiff);
    void installDirectoryWatch(std::shared_ptr<DirectoryWatch> watch);
    void refreshDiskSpace();
    static LocalReloadResult scanLocalEntries(quint64 generation,
                                              const QString &rootPath,
                                              bool showHidden,
                                              QDir::SortFlags sortFlags,
                                              bool hiddenLast, bool watchDirectory);
    void updateLocalCounts();
    bool applyLocalDiff(const QList<Entry> &newEntries);
    bool isTrashRoot() const;
    bool isRemoteRoot() const;
    QVariantMap remoteFileProperties(const QString &path) const;
    QVariantMap trashFileProperties(const QString &path) const;
    const QVariantMap *findTrashEntry(const QString &path) const;
    void setIsLoading(bool loading);

    QString m_rootPath;
    QString m_entryPrefix;  // m_rootPath as an absolute path ending in '/'
    bool m_showHidden = false;
    bool m_hiddenLast = false;
    QList<Entry> m_entries;
    QList<QVariantMap> m_remoteEntries;
    QSet<QString> m_remotePropertiesPending;
    QSet<QString> m_localPropertiesPending;
    QPointer<QFutureWatcher<QVariantList>> m_suggestionWatcher;
    QPointer<QFutureWatcher<QList<QVariantMap>>> m_trashWatcher;
    int m_suggestionRequest = 0;
    int m_folderCountRequest = 0;
    QList<QVariantMap> m_trashEntries;
    int m_fileCount = 0;
    int m_folderCount = 0;
    GitStatusService *m_gitService = nullptr;
    QProcess *m_remoteReloadProcess = nullptr;
    int m_remoteReloadGeneration = 0;
    QFutureWatcher<LocalReloadResult> *m_localReloadWatcher = nullptr;
    quint64 m_localReloadGeneration = 0;
    bool m_synchronousReload = false;
    std::shared_ptr<DirectoryWatch> m_directoryWatch;
    std::unique_ptr<QSocketNotifier> m_directoryNotifier;
    QList<QPersistentModelIndex> m_pendingDetails;
    QTimer m_detailsDebounce;
    QTimer m_loadDeadline;
    QPointer<QFutureWatcher<std::pair<qint64, qint64>>> m_diskProbe;
    qint64 m_diskFree = -1;
    qint64 m_diskTotal = -1;
    QTimer m_refreshDebounce;
    QDir::SortFlags m_sortFlags = QDir::Name | QDir::DirsFirst | QDir::IgnoreCase;
    QString m_sortColumn = "name";
    bool m_sortAscending = true;
    bool m_isLoading = false;
    QString m_loadError;
    void setLoadError(int error);
};
