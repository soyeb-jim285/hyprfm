#pragma once

#include <QByteArray>
#include <QHash>
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class MetadataExtractor;

class PreviewService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool pdfPreviewAvailable READ pdfPreviewAvailable NOTIFY supportChanged)

public:
    explicit PreviewService(QObject *parent = nullptr);

    // Optional: when set, metadata is gathered in the same worker pass as the
    // preview body, so QML gets one signal and repaints once instead of
    // blocking the GUI thread on exiftool/ffprobe (~120 ms each).
    void setMetadataExtractor(MetadataExtractor *extractor);

    bool pdfPreviewAvailable() const;

    // Async entry point. `kind` is one of "text", "pdf", "archive",
    // "directory", "font", or "" for metadata only. Results arrive on previewReady;
    // anything superseded by a newer request is dropped, never emitted.
    //
    // `requester` identifies the panel asking. Staleness is tracked per
    // requester: the quick-preview overlay and the Miller preview column
    // share this one service, and a global counter would let either cancel
    // the other's in-flight work and leave it blank forever.
    // `password` only applies to encrypted archives; every other kind
    // ignores it.
    Q_INVOKABLE void requestPreview(const QString &requester, const QString &path,
                                    const QString &kind,
                                    const QString &password = QString(), bool lightTheme = false);

    // Invalidates that requester's in-flight work without starting more.
    Q_INVOKABLE void cancelPreview(const QString &requester);

    Q_INVOKABLE QVariantMap loadTextPreview(const QString &path, int maxBytes = 131072,
                                            int maxLines = 400, bool lightTheme = false) const;
    Q_INVOKABLE QVariantMap loadDirectoryPreview(const QString &path, int maxEntries = 40) const;
    Q_INVOKABLE QVariantMap loadArchivePreview(const QString &path, int maxEntries = 200,
                                               const QString &password = QString()) const;
    Q_INVOKABLE QVariantMap loadPdfPreview(const QString &path) const;
    Q_INVOKABLE QString localPreviewPath(const QString &path) const;

    // Converts bat's ANSI-coloured output to escaped HTML. Public so it can
    // be unit-tested against hostile byte sequences.
    static QString ansiToHtml(const QByteArray &ansiText);

public slots:
    // Re-check availability of external tools (pdftoppm/pdfinfo). Called
    // when the user clicks Re-check in the Missing Dependencies dialog
    // after installing a package.
    void refreshSupport();

signals:
    void supportChanged();
    void previewReady(const QString &requester, const QString &path, const QVariantMap &data);

private:
    // Only reading a font happens on the worker. Qt registers the bytes on
    // the GUI thread because QFontDatabase belongs to the GUI.
    QVariantMap registerFontPreview(const QString &path, const QByteArray &bytes);
    QVariantMap buildPreview(const QString &path, const QString &kind,
                             const QString &password, bool lightTheme) const;

    QByteArray readPathBytes(const QString &path, qint64 maxBytes, bool *truncated,
                             QString *error) const;
    QStringList listDirectoryEntries(const QString &path, int maxEntries, bool *truncated,
                                     QString *error) const;
    static bool isTrashUri(const QString &path);
    static bool looksBinary(const QByteArray &data);
    static QString decodeText(const QByteArray &data);

    int m_activeFontPreviewId = -1;
    QString m_activeFontPreviewPath;

    QHash<QString, QFutureWatcher<QVariantMap> *> m_requests;
    bool m_extractMetadata = false;
};
