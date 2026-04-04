#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class PreviewService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool pdfPreviewAvailable READ pdfPreviewAvailable CONSTANT)

public:
    explicit PreviewService(QObject *parent = nullptr);

    bool pdfPreviewAvailable() const;

    Q_INVOKABLE QVariantMap loadTextPreview(const QString &path, int maxBytes = 131072,
                                            int maxLines = 400) const;
    Q_INVOKABLE QVariantMap loadDirectoryPreview(const QString &path, int maxEntries = 40) const;
    Q_INVOKABLE QVariantMap loadPdfPreview(const QString &path) const;
    Q_INVOKABLE QString localPreviewPath(const QString &path) const;

private:
    QByteArray readPathBytes(const QString &path, qint64 maxBytes, bool *truncated,
                             QString *error) const;
    QStringList listDirectoryEntries(const QString &path, int maxEntries, bool *truncated,
                                     QString *error) const;
    static bool isTrashUri(const QString &path);
    static bool looksBinary(const QByteArray &data);
    static QString decodeText(const QByteArray &data);
};
