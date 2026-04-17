#include "services/metadataextractor.h"

#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMimeDatabase>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {

QString formatDuration(double seconds)
{
    if (seconds <= 0)
        return {};
    const int total = static_cast<int>(seconds);
    const int h = total / 3600;
    const int m = (total % 3600) / 60;
    const int s = total % 60;
    return h > 0
        ? QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'))
        : QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

QString formatChannels(int channels)
{
    if (channels <= 0)
        return {};
    if (channels == 1) return QStringLiteral("Mono");
    if (channels == 2) return QStringLiteral("Stereo");
    return QString::number(channels);
}

// Preserve nice values from JSON: numbers pass through as strings without
// quoting, while non-empty strings get trimmed and dropped when blank.
QString jsonToString(const QJsonValue &v)
{
    if (v.isString())
        return v.toString().trimmed();
    if (v.isDouble())
        return QString::number(v.toDouble());
    return {};
}

bool hasExec(const QString &name)
{
    return !QStandardPaths::findExecutable(name).isEmpty();
}

}

MetadataExtractor::MetadataExtractor(QObject *parent)
    : QObject(parent)
{
}

bool MetadataExtractor::hasExifSupport() const
{
    return hasExec(QStringLiteral("exiftool"));
}

bool MetadataExtractor::hasTagLibSupport() const
{
    return hasExec(QStringLiteral("ffprobe"));
}

bool MetadataExtractor::hasVideoSupport() const
{
    return hasExec(QStringLiteral("ffprobe"));
}

bool MetadataExtractor::hasPdfSupport() const
{
    return hasExec(QStringLiteral("pdfinfo"));
}

void MetadataExtractor::refreshSupport()
{
    emit supportChanged();
}

QVariantMap MetadataExtractor::extract(const QString &path) const
{
    QMimeDatabase mimeDb;
    const QString mime = mimeDb.mimeTypeForFile(path).name();

    if (mime.startsWith("image/"))
        return extractImage(path);
    if (mime.startsWith("audio/"))
        return extractAudio(path);
    if (mime.startsWith("video/"))
        return extractVideo(path);
    if (mime == "application/pdf")
        return extractPdf(path);

    return {};
}

QString MetadataExtractor::missingDepsHint(const QString &mimeType) const
{
    if (mimeType.startsWith("image/") && !hasExifSupport())
        return QStringLiteral("Install exiftool for camera metadata (make/model, ISO, aperture, GPS)");
    if (mimeType.startsWith("audio/") && !hasTagLibSupport())
        return QStringLiteral("Install ffmpeg for audio metadata (artist, album, genre, duration)");
    if (mimeType.startsWith("video/") && !hasVideoSupport())
        return QStringLiteral("Install ffmpeg for video metadata (duration, codec, resolution)");
    if (mimeType == "application/pdf" && !hasPdfSupport())
        return QStringLiteral("Install poppler-utils for PDF metadata (author, title, page count)");
    return {};
}

// ── Image ──

QVariantMap MetadataExtractor::extractImage(const QString &path) const
{
    QVariantMap meta;

    QImageReader reader(path);
    reader.setAutoTransform(false);
    const QSize size = reader.size();
    if (size.isValid()) {
        meta["Dimensions"] = QString("%1 x %2").arg(size.width()).arg(size.height());
        const double mp = (size.width() * static_cast<double>(size.height())) / 1e6;
        if (mp >= 0.1)
            meta["Megapixels"] = QString("%1 MP").arg(mp, 0, 'f', 1);
    }

    const QImage::Format fmt = reader.imageFormat();
    if (fmt != QImage::Format_Invalid) {
        const int depth = QImage::toPixelFormat(fmt).bitsPerPixel();
        if (depth > 0)
            meta["Bit depth"] = QString("%1-bit").arg(depth);
    }

    if (!hasExifSupport())
        return meta;

    QProcess proc;
    // `-json` returns structured data, `-n` keeps numeric values numeric,
    // and explicit tags keep the output small and stable.
    proc.start(QStringLiteral("exiftool"), {
        QStringLiteral("-json"),
        QStringLiteral("-n"),
        QStringLiteral("-Make"),
        QStringLiteral("-Model"),
        QStringLiteral("-LensModel"),
        QStringLiteral("-DateTimeOriginal"),
        QStringLiteral("-ExposureTime"),
        QStringLiteral("-FNumber"),
        QStringLiteral("-ISO"),
        QStringLiteral("-FocalLength"),
        QStringLiteral("-Flash"),
        QStringLiteral("-WhiteBalance"),
        QStringLiteral("-MeteringMode"),
        QStringLiteral("-Software"),
        QStringLiteral("-GPSLatitude"),
        QStringLiteral("-GPSLatitudeRef"),
        QStringLiteral("-GPSLongitude"),
        QStringLiteral("-GPSLongitudeRef"),
        path,
    });
    if (!proc.waitForFinished(5000) || proc.exitCode() != 0)
        return meta;

    const QJsonDocument doc = QJsonDocument::fromJson(proc.readAllStandardOutput());
    if (!doc.isArray() || doc.array().isEmpty())
        return meta;

    const QJsonObject obj = doc.array().at(0).toObject();
    auto put = [&](const QString &label, const QString &key) {
        const QString v = jsonToString(obj.value(key));
        if (!v.isEmpty())
            meta[label] = v;
    };

    put("Camera make", "Make");
    put("Camera model", "Model");
    put("Lens model", "LensModel");
    put("Date taken", "DateTimeOriginal");

    if (obj.contains("ExposureTime")) {
        const double v = obj.value("ExposureTime").toDouble();
        if (v > 0) {
            meta["Exposure"] = v >= 1.0
                ? QString::number(v, 'f', 1) + " s"
                : "1/" + QString::number(static_cast<int>(1.0 / v + 0.5)) + " s";
        }
    }
    if (obj.contains("FNumber"))
        meta["Aperture"] = "f/" + QString::number(obj.value("FNumber").toDouble(), 'f', 1);
    put("ISO", "ISO");
    if (obj.contains("FocalLength"))
        meta["Focal length"] = QString::number(obj.value("FocalLength").toDouble(), 'f', 0) + " mm";
    put("Flash", "Flash");
    put("White balance", "WhiteBalance");
    put("Metering mode", "MeteringMode");
    put("Software", "Software");

    const QString lat = jsonToString(obj.value("GPSLatitude"));
    const QString lon = jsonToString(obj.value("GPSLongitude"));
    if (!lat.isEmpty() && !lon.isEmpty()) {
        const QString latRef = jsonToString(obj.value("GPSLatitudeRef"));
        const QString lonRef = jsonToString(obj.value("GPSLongitudeRef"));
        meta["GPS"] = QString("%1 %2, %3 %4").arg(lat, latRef, lon, lonRef);
    }

    return meta;
}

// ── ffprobe-backed audio / video ──

namespace {

QJsonObject runFfprobe(const QString &path)
{
    QProcess proc;
    proc.start(QStringLiteral("ffprobe"), {
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-show_format"),
        QStringLiteral("-show_streams"),
        QStringLiteral("-of"), QStringLiteral("json"),
        path,
    });
    if (!proc.waitForFinished(5000) || proc.exitCode() != 0)
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(proc.readAllStandardOutput());
    return doc.isObject() ? doc.object() : QJsonObject();
}

QJsonObject firstStreamOfType(const QJsonObject &probe, const QString &type)
{
    for (const QJsonValue &s : probe.value("streams").toArray()) {
        const QJsonObject o = s.toObject();
        if (o.value("codec_type").toString() == type)
            return o;
    }
    return {};
}

}

QVariantMap MetadataExtractor::extractAudio(const QString &path) const
{
    QVariantMap meta;
    if (!hasTagLibSupport())
        return meta;

    const QJsonObject probe = runFfprobe(path);
    if (probe.isEmpty())
        return meta;

    const QJsonObject fmt = probe.value("format").toObject();
    const QJsonObject tags = fmt.value("tags").toObject();
    const QJsonObject audio = firstStreamOfType(probe, QStringLiteral("audio"));

    // ffprobe lowercases some container tag names and uppercases others.
    // Normalise by looking up case-insensitively.
    auto tagValue = [&](std::initializer_list<const char *> keys) -> QString {
        for (auto it = tags.constBegin(); it != tags.constEnd(); ++it) {
            const QString k = it.key().toLower();
            for (const char *wanted : keys) {
                if (k == QLatin1String(wanted))
                    return it.value().toString().trimmed();
            }
        }
        return {};
    };

    auto putTag = [&](const QString &label, std::initializer_list<const char *> keys) {
        const QString v = tagValue(keys);
        if (!v.isEmpty())
            meta[label] = v;
    };

    putTag("Title",  {"title"});
    putTag("Artist", {"artist", "author"});
    putTag("Album",  {"album"});
    putTag("Genre",  {"genre"});
    putTag("Year",   {"date", "year"});
    putTag("Track",  {"track"});
    putTag("Comment",{"comment"});

    const double duration = fmt.value("duration").toString().toDouble();
    const QString d = formatDuration(duration);
    if (!d.isEmpty())
        meta["Duration"] = d;

    const int bitrate = fmt.value("bit_rate").toString().toInt();
    if (bitrate > 0)
        meta["Bitrate"] = QStringLiteral("%1 kbps").arg(bitrate / 1000);

    if (!audio.isEmpty()) {
        const int sampleRate = audio.value("sample_rate").toString().toInt();
        if (sampleRate > 0)
            meta["Sample rate"] = QStringLiteral("%1 Hz").arg(sampleRate);
        const int channels = audio.value("channels").toInt();
        const QString chStr = formatChannels(channels);
        if (!chStr.isEmpty())
            meta["Channels"] = chStr;
    }

    return meta;
}

QVariantMap MetadataExtractor::extractVideo(const QString &path) const
{
    QVariantMap meta;
    if (!hasVideoSupport())
        return meta;

    const QJsonObject probe = runFfprobe(path);
    if (probe.isEmpty())
        return meta;

    const QJsonObject fmt = probe.value("format").toObject();

    const double duration = fmt.value("duration").toString().toDouble();
    const QString d = formatDuration(duration);
    if (!d.isEmpty())
        meta["Duration"] = d;

    const qint64 bitrate = fmt.value("bit_rate").toString().toLongLong();
    if (bitrate > 0) {
        const double mbps = bitrate / 1e6;
        meta["Bitrate"] = mbps >= 1.0
            ? QString("%1 Mbps").arg(mbps, 0, 'f', 1)
            : QString("%1 kbps").arg(bitrate / 1000);
    }

    const QJsonObject video = firstStreamOfType(probe, QStringLiteral("video"));
    if (!video.isEmpty()) {
        const QString codec = video.value("codec_name").toString();
        if (!codec.isEmpty())
            meta["Video codec"] = codec;
        const int w = video.value("width").toInt();
        const int h = video.value("height").toInt();
        if (w > 0 && h > 0)
            meta["Resolution"] = QStringLiteral("%1 x %2").arg(w).arg(h);
        const QString fpsStr = video.value("avg_frame_rate").toString();
        if (fpsStr.contains('/')) {
            const auto parts = fpsStr.split('/');
            const double num = parts[0].toDouble();
            const double den = parts[1].toDouble();
            if (num > 0 && den > 0)
                meta["Frame rate"] = QStringLiteral("%1 fps").arg(num / den, 0, 'f', 1);
        }
    }

    const QJsonObject audio = firstStreamOfType(probe, QStringLiteral("audio"));
    if (!audio.isEmpty()) {
        const QString codec = audio.value("codec_name").toString();
        if (!codec.isEmpty())
            meta["Audio codec"] = codec;
        const int sampleRate = audio.value("sample_rate").toString().toInt();
        if (sampleRate > 0)
            meta["Sample rate"] = QStringLiteral("%1 Hz").arg(sampleRate);
        const int channels = audio.value("channels").toInt();
        if (channels > 0) {
            meta["Audio channels"] = channels == 1 ? QStringLiteral("Mono")
                                   : channels == 2 ? QStringLiteral("Stereo")
                                   : QStringLiteral("%1.%2").arg(channels - 1).arg(channels > 5 ? 1 : 0);
        }
    }

    const QJsonObject tags = fmt.value("tags").toObject();
    for (auto it = tags.constBegin(); it != tags.constEnd(); ++it) {
        const QString key = it.key().toLower();
        const QString val = it.value().toString().trimmed();
        if (val.isEmpty()) continue;
        if (key == "title") meta["Title"] = val;
        else if (key == "artist" || key == "author") meta["Artist"] = val;
        else if (key == "album") meta["Album"] = val;
        else if (key == "encoder" || key == "encoding_tool") meta["Encoder"] = val;
    }

    return meta;
}

// ── PDF ──

QVariantMap MetadataExtractor::extractPdf(const QString &path) const
{
    QVariantMap meta;
    if (!hasPdfSupport())
        return meta;

    QProcess proc;
    proc.start(QStringLiteral("pdfinfo"), {path});
    if (!proc.waitForFinished(5000) || proc.exitCode() != 0)
        return meta;

    const QString out = QString::fromUtf8(proc.readAllStandardOutput());

    // pdfinfo lines look like "Key: value" with key-column padding; split on
    // the first colon to be tolerant of values that themselves contain ':'.
    QHash<QString, QString> kv;
    for (const QString &line : out.split('\n', Qt::SkipEmptyParts)) {
        const int colon = line.indexOf(':');
        if (colon < 0) continue;
        const QString key = line.left(colon).trimmed();
        const QString val = line.mid(colon + 1).trimmed();
        kv.insert(key, val);
    }

    auto put = [&](const QString &label, const QString &key) {
        const QString v = kv.value(key);
        if (!v.isEmpty())
            meta[label] = v;
    };

    put("Title",    "Title");
    put("Author",   "Author");
    put("Subject",  "Subject");
    put("Creator",  "Creator");
    put("Producer", "Producer");
    put("Created",  "CreationDate");
    put("Modified", "ModDate");
    put("Pages",    "Pages");

    // Page size: "595.28 x 841.89 pts (A4)" → "210 x 297 mm"
    const QString rawSize = kv.value("Page size");
    static const QRegularExpression sizeRe(QStringLiteral(R"(([0-9.]+)\s*x\s*([0-9.]+)\s*pts)"));
    const auto sm = sizeRe.match(rawSize);
    if (sm.hasMatch()) {
        const double w = sm.captured(1).toDouble() * 25.4 / 72.0;
        const double h = sm.captured(2).toDouble() * 25.4 / 72.0;
        meta["Page size"] = QString("%1 x %2 mm").arg(w, 0, 'f', 0).arg(h, 0, 'f', 0);
    }

    put("PDF version", "PDF version");

    return meta;
}
