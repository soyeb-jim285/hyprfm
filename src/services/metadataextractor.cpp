#include "services/metadataextractor.h"

#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>

#ifdef HYPRFM_HAS_LIBEXIF
#include <libexif/exif-data.h>
#include <libexif/exif-entry.h>
#endif

#ifdef HYPRFM_HAS_TAGLIB
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>
#include <taglib/audioproperties.h>
#endif

#ifdef HYPRFM_HAS_LIBAVFORMAT
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/dict.h>
}
#endif

#ifdef HYPRFM_HAS_POPPLER_QT6
#include <poppler-qt6.h>
#endif

MetadataExtractor::MetadataExtractor(QObject *parent)
    : QObject(parent)
{
}

bool MetadataExtractor::hasExifSupport() const
{
#ifdef HYPRFM_HAS_LIBEXIF
    return true;
#else
    return false;
#endif
}

bool MetadataExtractor::hasTagLibSupport() const
{
#ifdef HYPRFM_HAS_TAGLIB
    return true;
#else
    return false;
#endif
}

bool MetadataExtractor::hasVideoSupport() const
{
#ifdef HYPRFM_HAS_LIBAVFORMAT
    return true;
#else
    return false;
#endif
}

bool MetadataExtractor::hasPdfSupport() const
{
#ifdef HYPRFM_HAS_POPPLER_QT6
    return true;
#else
    return false;
#endif
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
        return QStringLiteral("Install libexif for camera metadata (make/model, ISO, aperture, GPS)");
    if (mimeType.startsWith("audio/") && !hasTagLibSupport())
        return QStringLiteral("Install taglib for audio metadata (artist, album, genre, duration)");
    if (mimeType.startsWith("video/") && !hasVideoSupport())
        return QStringLiteral("Install ffmpeg/libavformat for video metadata (duration, codec, resolution)");
    if (mimeType == "application/pdf" && !hasPdfSupport())
        return QStringLiteral("Install poppler-qt6 for PDF metadata (author, title, page count)");
    return {};
}

// ── Image ──

QVariantMap MetadataExtractor::extractImage(const QString &path) const
{
    QVariantMap meta;

    // Basic dimensions via Qt (always available)
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

#ifdef HYPRFM_HAS_LIBEXIF
    ExifData *ed = exif_data_new_from_file(path.toUtf8().constData());
    if (ed) {
        auto getTag = [ed](ExifIfd ifd, ExifTag tag) -> QString {
            ExifEntry *entry = exif_content_get_entry(ed->ifd[ifd], tag);
            if (!entry) return {};
            char buf[256];
            exif_entry_get_value(entry, buf, sizeof(buf));
            QString val = QString::fromUtf8(buf).trimmed();
            return val.isEmpty() ? QString() : val;
        };

        auto addIfPresent = [&meta, &getTag](const QString &label, ExifIfd ifd, ExifTag tag) {
            const QString val = getTag(ifd, tag);
            if (!val.isEmpty())
                meta[label] = val;
        };

        addIfPresent("Camera make", EXIF_IFD_0, EXIF_TAG_MAKE);
        addIfPresent("Camera model", EXIF_IFD_0, EXIF_TAG_MODEL);
        addIfPresent("Lens model", EXIF_IFD_EXIF, EXIF_TAG_LENS_MODEL);
        addIfPresent("Date taken", EXIF_IFD_EXIF, EXIF_TAG_DATE_TIME_ORIGINAL);
        addIfPresent("Exposure", EXIF_IFD_EXIF, EXIF_TAG_EXPOSURE_TIME);
        addIfPresent("Aperture", EXIF_IFD_EXIF, EXIF_TAG_FNUMBER);
        addIfPresent("ISO", EXIF_IFD_EXIF, EXIF_TAG_ISO_SPEED_RATINGS);
        addIfPresent("Focal length", EXIF_IFD_EXIF, EXIF_TAG_FOCAL_LENGTH);
        addIfPresent("Flash", EXIF_IFD_EXIF, EXIF_TAG_FLASH);
        addIfPresent("White balance", EXIF_IFD_EXIF, EXIF_TAG_WHITE_BALANCE);
        addIfPresent("Metering mode", EXIF_IFD_EXIF, EXIF_TAG_METERING_MODE);
        addIfPresent("Software", EXIF_IFD_0, EXIF_TAG_SOFTWARE);

        // GPS coordinates
        const QString latRef = getTag(EXIF_IFD_GPS, static_cast<ExifTag>(EXIF_TAG_GPS_LATITUDE_REF));
        const QString lat = getTag(EXIF_IFD_GPS, static_cast<ExifTag>(EXIF_TAG_GPS_LATITUDE));
        const QString lonRef = getTag(EXIF_IFD_GPS, static_cast<ExifTag>(EXIF_TAG_GPS_LONGITUDE_REF));
        const QString lon = getTag(EXIF_IFD_GPS, static_cast<ExifTag>(EXIF_TAG_GPS_LONGITUDE));
        if (!lat.isEmpty() && !lon.isEmpty())
            meta["GPS"] = QString("%1 %2, %3 %4").arg(lat, latRef, lon, lonRef);

        exif_data_unref(ed);
    }
#endif

    return meta;
}

// ── Audio ──

QVariantMap MetadataExtractor::extractAudio(const QString &path) const
{
    QVariantMap meta;

#ifdef HYPRFM_HAS_TAGLIB
    TagLib::FileRef f(path.toUtf8().constData());
    if (f.isNull())
        return meta;

    if (auto *tag = f.tag()) {
        auto addIfPresent = [&meta](const QString &label, const TagLib::String &val) {
            if (!val.isEmpty())
                meta[label] = QString::fromStdWString(val.toWString());
        };

        addIfPresent("Title", tag->title());
        addIfPresent("Artist", tag->artist());
        addIfPresent("Album", tag->album());
        addIfPresent("Genre", tag->genre());
        if (tag->year() > 0)
            meta["Year"] = QString::number(tag->year());
        if (tag->track() > 0)
            meta["Track"] = QString::number(tag->track());
        addIfPresent("Comment", tag->comment());
    }

    if (auto *props = f.audioProperties()) {
        const int secs = props->lengthInSeconds();
        if (secs > 0) {
            const int h = secs / 3600;
            const int m = (secs % 3600) / 60;
            const int s = secs % 60;
            meta["Duration"] = h > 0
                ? QString("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'))
                : QString("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
        }
        if (props->bitrate() > 0)
            meta["Bitrate"] = QString("%1 kbps").arg(props->bitrate());
        if (props->sampleRate() > 0)
            meta["Sample rate"] = QString("%1 Hz").arg(props->sampleRate());
        if (props->channels() > 0)
            meta["Channels"] = props->channels() == 1 ? QStringLiteral("Mono")
                             : props->channels() == 2 ? QStringLiteral("Stereo")
                             : QString::number(props->channels());
    }
#endif

    return meta;
}

// ── Video ──

QVariantMap MetadataExtractor::extractVideo(const QString &path) const
{
    QVariantMap meta;

#ifdef HYPRFM_HAS_LIBAVFORMAT
    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) != 0)
        return meta;

    avformat_find_stream_info(fmt, nullptr);

    // Duration
    if (fmt->duration > 0) {
        const int totalSecs = static_cast<int>(fmt->duration / AV_TIME_BASE);
        const int h = totalSecs / 3600;
        const int m = (totalSecs % 3600) / 60;
        const int s = totalSecs % 60;
        meta["Duration"] = h > 0
            ? QString("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'))
            : QString("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
    }

    // Overall bitrate
    if (fmt->bit_rate > 0) {
        const double mbps = fmt->bit_rate / 1e6;
        meta["Bitrate"] = mbps >= 1.0
            ? QString("%1 Mbps").arg(mbps, 0, 'f', 1)
            : QString("%1 kbps").arg(fmt->bit_rate / 1000);
    }

    // Stream info
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        const AVStream *stream = fmt->streams[i];
        const AVCodecParameters *codec = stream->codecpar;

        if (codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            const char *codecName = avcodec_get_name(codec->codec_id);
            if (codecName)
                meta["Video codec"] = QString::fromUtf8(codecName);
            if (codec->width > 0 && codec->height > 0)
                meta["Resolution"] = QString("%1 x %2").arg(codec->width).arg(codec->height);
            if (stream->avg_frame_rate.den > 0 && stream->avg_frame_rate.num > 0) {
                const double fps = static_cast<double>(stream->avg_frame_rate.num) / stream->avg_frame_rate.den;
                meta["Frame rate"] = QString("%1 fps").arg(fps, 0, 'f', 1);
            }
        } else if (codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            const char *codecName = avcodec_get_name(codec->codec_id);
            if (codecName)
                meta["Audio codec"] = QString::fromUtf8(codecName);
            if (codec->sample_rate > 0)
                meta["Sample rate"] = QString("%1 Hz").arg(codec->sample_rate);
            if (codec->ch_layout.nb_channels > 0) {
                const int ch = codec->ch_layout.nb_channels;
                meta["Audio channels"] = ch == 1 ? QStringLiteral("Mono")
                                       : ch == 2 ? QStringLiteral("Stereo")
                                       : QString("%1.%2").arg(ch - 1).arg(ch > 5 ? 1 : 0);
            }
        }
    }

    // Container metadata tags (title, artist, etc.)
    if (fmt->metadata) {
        const AVDictionaryEntry *tag = nullptr;
        while ((tag = av_dict_get(fmt->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
            const QString key = QString::fromUtf8(tag->key).toLower();
            const QString val = QString::fromUtf8(tag->value).trimmed();
            if (val.isEmpty()) continue;
            if (key == "title") meta["Title"] = val;
            else if (key == "artist" || key == "author") meta["Artist"] = val;
            else if (key == "album") meta["Album"] = val;
            else if (key == "encoder" || key == "encoding_tool") meta["Encoder"] = val;
        }
    }

    avformat_close_input(&fmt);
#endif

    return meta;
}

// ── PDF ──

QVariantMap MetadataExtractor::extractPdf(const QString &path) const
{
    QVariantMap meta;

#ifdef HYPRFM_HAS_POPPLER_QT6
    auto doc = Poppler::Document::load(path);
    if (!doc)
        return meta;

    auto addIfPresent = [&meta](const QString &label, const QString &val) {
        if (!val.trimmed().isEmpty())
            meta[label] = val.trimmed();
    };

    addIfPresent("Title", doc->title());
    addIfPresent("Author", doc->author());
    addIfPresent("Subject", doc->subject());
    addIfPresent("Creator", doc->creator());
    addIfPresent("Producer", doc->producer());

    if (doc->creationDate().isValid())
        meta["Created"] = doc->creationDate().toString("yyyy-MM-dd hh:mm");
    if (doc->modificationDate().isValid())
        meta["Modified"] = doc->modificationDate().toString("yyyy-MM-dd hh:mm");

    meta["Pages"] = QString::number(doc->numPages());

    if (doc->numPages() > 0) {
        auto page = doc->page(0);
        if (page) {
            const QSizeF pageSize = page->pageSizeF();
            meta["Page size"] = QString("%1 x %2 mm")
                .arg(pageSize.width() * 25.4 / 72.0, 0, 'f', 0)
                .arg(pageSize.height() * 25.4 / 72.0, 0, 'f', 0);
        }
    }

    const QString version = QStringLiteral("%1.%2")
        .arg(doc->getPdfVersion().major)
        .arg(doc->getPdfVersion().minor);
    meta["PDF version"] = version;
#endif

    return meta;
}
