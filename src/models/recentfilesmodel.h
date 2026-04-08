#pragma once

#include <QAbstractListModel>
#include <QFileInfo>
#include <QString>
#include <QList>
#include <QDateTime>

class RecentFilesModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

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
        // Recent files aren't git-tracked, but the view delegates declare
        // these as required properties.
        GitStatusRole,
        GitStatusIconRole,
        HasImagePreviewRole,
        HasVideoPreviewRole,
    };
    Q_ENUM(Roles)

    explicit RecentFilesModel(const QString &storagePath, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void addRecent(const QString &path);
    Q_INVOKABLE void clear();

signals:
    void countChanged();

private:
    void load();
    void save() const;

    struct RecentEntry {
        QString path;
        QDateTime accessTime;
    };

    QList<RecentEntry> m_entries;
    QString m_storagePath;
    int m_maxEntries = 50;
};
