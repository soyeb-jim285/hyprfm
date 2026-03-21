#pragma once

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QFileInfo>
#include <QDir>
#include <QList>
#include <QString>

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
    };
    Q_ENUM(Roles)

    explicit FileSystemModel(QObject *parent = nullptr);

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

signals:
    void rootPathChanged();
    void showHiddenChanged();
    void countsChanged();

private:
    void reload();

    QString m_rootPath;
    bool m_showHidden = false;
    QList<QFileInfo> m_entries;
    int m_fileCount = 0;
    int m_folderCount = 0;
    QFileSystemWatcher m_watcher;
    QDir::SortFlags m_sortFlags = QDir::Name | QDir::DirsFirst | QDir::IgnoreCase;
};
