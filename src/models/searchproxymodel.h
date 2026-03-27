#pragma once

#include <QSortFilterProxyModel>
#include <QRegularExpression>

class SearchProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)
    Q_PROPERTY(QString fileTypeFilter READ fileTypeFilter WRITE setFileTypeFilter NOTIFY fileTypeFilterChanged)
    Q_PROPERTY(QString dateFilter READ dateFilter WRITE setDateFilter NOTIFY dateFilterChanged)
    Q_PROPERTY(QString sizeFilter READ sizeFilter WRITE setSizeFilter NOTIFY sizeFilterChanged)
    Q_PROPERTY(bool searchActive READ searchActive NOTIFY searchActiveChanged)

public:
    explicit SearchProxyModel(QObject *parent = nullptr);

    QString searchQuery() const;
    void setSearchQuery(const QString &query);

    QString fileTypeFilter() const;
    void setFileTypeFilter(const QString &filter);

    QString dateFilter() const;
    void setDateFilter(const QString &filter);

    QString sizeFilter() const;
    void setSizeFilter(const QString &filter);

    bool searchActive() const;
    bool isGlobPattern() const;

    Q_INVOKABLE void clearSearch();
    Q_INVOKABLE void switchSourceModel(QAbstractItemModel *model);
    Q_INVOKABLE QString filePath(int row) const;
    Q_INVOKABLE bool isDir(int row) const;
    Q_INVOKABLE QString fileName(int row) const;

signals:
    void searchQueryChanged();
    void fileTypeFilterChanged();
    void dateFilterChanged();
    void sizeFilterChanged();
    void searchActiveChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    bool matchesText(const QString &fileName) const;
    bool matchesType(const QModelIndex &sourceIndex) const;
    bool matchesSingleType(const QString &type, bool isDirectory,
                           const QString &mime, const QString &filePath) const;
    bool matchesDate(const QModelIndex &sourceIndex) const;
    bool matchesSize(const QModelIndex &sourceIndex) const;

    QString m_searchQuery;
    QString m_fileTypeFilter;
    QString m_dateFilter;
    QString m_sizeFilter;
    QRegularExpression m_regex;
    bool m_isGlob = false;
};
