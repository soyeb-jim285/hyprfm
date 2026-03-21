#pragma once

#include <QSortFilterProxyModel>
#include <QFileSystemModel>
#include <QString>

class FileSystemModel : public QSortFilterProxyModel
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
    };
    Q_ENUM(Roles)

    explicit FileSystemModel(QObject *parent = nullptr);

    QString rootPath() const;
    bool showHidden() const;
    int fileCount() const;
    int folderCount() const;

    Q_INVOKABLE void setRootPath(const QString &path);
    Q_INVOKABLE void setShowHidden(bool show);
    Q_INVOKABLE QString filePath(int row) const;
    Q_INVOKABLE bool isDir(int row) const;
    Q_INVOKABLE QString fileName(int row) const;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void rootPathChanged();
    void showHiddenChanged();
    void countsChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    void updateCounts();

    QFileSystemModel *m_fsModel;
    QString m_rootPath;
    bool m_showHidden = false;
    int m_fileCount = 0;
    int m_folderCount = 0;
};
