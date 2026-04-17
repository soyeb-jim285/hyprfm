#pragma once

#include <QObject>
#include <QVariantMap>

class MetadataExtractor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasExifSupport READ hasExifSupport NOTIFY supportChanged)
    Q_PROPERTY(bool hasTagLibSupport READ hasTagLibSupport NOTIFY supportChanged)
    Q_PROPERTY(bool hasVideoSupport READ hasVideoSupport NOTIFY supportChanged)
    Q_PROPERTY(bool hasPdfSupport READ hasPdfSupport NOTIFY supportChanged)

public:
    explicit MetadataExtractor(QObject *parent = nullptr);

    bool hasExifSupport() const;
    bool hasTagLibSupport() const;
    bool hasVideoSupport() const;
    bool hasPdfSupport() const;

    Q_INVOKABLE QVariantMap extract(const QString &path) const;
    Q_INVOKABLE QString missingDepsHint(const QString &mimeType) const;

public slots:
    void refreshSupport();

signals:
    void supportChanged();

private:
    QVariantMap extractImage(const QString &path) const;
    QVariantMap extractAudio(const QString &path) const;
    QVariantMap extractVideo(const QString &path) const;
    QVariantMap extractPdf(const QString &path) const;
};
