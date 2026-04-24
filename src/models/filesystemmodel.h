#pragma once

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QFileInfo>
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
    Q_INVOKABLE QVariantList pathSuggestions(const QString &input, int limit = 8) const;

    void setGitStatusService(GitStatusService *service);

signals:
    void rootPathChanged();
    void showHiddenChanged();
    void countsChanged();
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
        mutable bool hasImagePreview = false;
        mutable bool hasVideoPreview = false;
        mutable bool populated = false;
    };

    void ensurePopulated(const Entry &entry) const;

    void reload();
    void reloadLocal();
    void reloadRemote();
    void reloadTrash();
    void cancelRemoteReload();
    void applyRemoteReload(const QString &rootPath, const QByteArray &output);
    QList<Entry> currentLocalEntries() const;
    void updateLocalCounts();
    bool applyLocalDiff(const QList<Entry> &newEntries);
    bool isTrashRoot() const;
    bool isRemoteRoot() const;
    QVariantMap remoteFileProperties(const QString &path) const;
    QVariantMap trashFileProperties(const QString &path) const;

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
    QFileSystemWatcher m_watcher;
    QDir::SortFlags m_sortFlags = QDir::Name | QDir::DirsFirst | QDir::IgnoreCase;
    QString m_sortColumn = "name";
    bool m_sortAscending = true;
};
