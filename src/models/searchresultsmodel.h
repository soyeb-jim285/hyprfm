#pragma once

#include <QAbstractListModel>
#include <QFileInfo>
#include <QList>

class SearchResultsModel : public QAbstractListModel
{
    Q_OBJECT

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

    explicit SearchResultsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QString filePath(int row) const;
    Q_INVOKABLE bool isDir(int row) const;
    Q_INVOKABLE QString fileName(int row) const;
    Q_INVOKABLE void clear();

    void addResults(const QList<QFileInfo> &entries);

private:
    static QString formatSize(qint64 bytes);
    QList<QFileInfo> m_entries;
};
