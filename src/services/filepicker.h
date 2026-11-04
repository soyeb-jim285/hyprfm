#pragma once

#include <QSortFilterProxyModel>
#include <QJsonObject>
#include "models/filesystemmodel.h"

// The portal only chooses paths. The requesting application performs the I/O.
class FilePicker : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap options READ options CONSTANT)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(int filterIndex READ filterIndex WRITE setFilterIndex NOTIFY filterIndexChanged)
public:
    explicit FilePicker(const QJsonObject &request, QObject *parent = nullptr);
    QVariantMap options() const { return m_request.toVariantMap(); }
    QString error() const { return m_error; }
    int filterIndex() const { return m_filterIndex; }
    void setFilterIndex(int index);
    QString initialFolder() const;
    Q_INVOKABLE QString filePath(int row) const;
    Q_INVOKABLE QString fileName(int row) const;
    Q_INVOKABLE bool isDir(int row) const;
    Q_INVOKABLE QVariantMap folderItemCounts(const QStringList &paths) const;
    Q_INVOKABLE bool navigate(const QString &path);
    Q_INVOKABLE void setChoice(const QString &id, const QString &value);
    Q_INVOKABLE bool submit(const QString &folder, const QString &name, const QStringList &selection,
                            bool overwrite = false);
signals:
    void errorChanged();
    void filterIndexChanged();
    void overwriteRequested();
    void completed(const QJsonObject &result);

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

private:
    bool fail(const QString &message);
    FileSystemModel *files() const { return qobject_cast<FileSystemModel *>(sourceModel()); }
    QJsonObject m_request;
    QJsonObject m_choices;
    QString m_error;
    int m_filterIndex = 0;
};
