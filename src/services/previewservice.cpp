#include "services/previewservice.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QColor>
#include <QCryptographicHash>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QRawFont>
#include <QStandardPaths>
#include <QUrl>

#ifdef HYPRFM_HAS_POPPLER_QT6
#include <poppler-qt6.h>
#endif

namespace {

QString encodedUri(const QString &path)
{
    return QUrl(path).toString(QUrl::FullyEncoded);
}

bool runningInFlatpak()
{
    static const bool inSandbox = QFile::exists(QStringLiteral("/.flatpak-info"));
    return inSandbox;
}

// Spawn `gio cat <uri>` for reading trash:// URIs. Inside a Flatpak we
// route through `flatpak-spawn --host` so the host's gio reads from the
// host's real trash (the sandbox's gio sees only an empty per-app trash).
void startGioCat(QProcess &proc, const QString &uri)
{
    if (runningInFlatpak()) {
        proc.start(QStringLiteral("flatpak-spawn"),
                   {QStringLiteral("--host"), QStringLiteral("gio"),
                    QStringLiteral("cat"), uri});
    } else {
        proc.start(QStringLiteral("gio"), {QStringLiteral("cat"), uri});
    }
}

QString batExecutable()
{
    static const QString executable = []() {
        const QString bat = QStandardPaths::findExecutable(QStringLiteral("bat"));
        if (!bat.isEmpty())
            return bat;
        return QStandardPaths::findExecutable(QStringLiteral("batcat"));
    }();

    return executable;
}

QColor ansiColor(int code, bool bright)
{
    static const QColor normalColors[] = {
        QColor(QStringLiteral("#1e1e2e")), QColor(QStringLiteral("#f38ba8")),
        QColor(QStringLiteral("#a6e3a1")), QColor(QStringLiteral("#f9e2af")),
        QColor(QStringLiteral("#89b4fa")), QColor(QStringLiteral("#cba6f7")),
        QColor(QStringLiteral("#94e2d5")), QColor(QStringLiteral("#bac2de"))
    };
    static const QColor brightColors[] = {
        QColor(QStringLiteral("#45475a")), QColor(QStringLiteral("#eba0ac")),
        QColor(QStringLiteral("#a6e3a1")), QColor(QStringLiteral("#f9e2af")),
        QColor(QStringLiteral("#89dceb")), QColor(QStringLiteral("#f5c2e7")),
        QColor(QStringLiteral("#94e2d5")), QColor(QStringLiteral("#f5e0dc"))
    };

    if (code < 0 || code > 7)
        return {};
    return bright ? brightColors[code] : normalColors[code];
}

QColor ansi256Color(int index)
{
    if (index < 0)
        return {};
    if (index < 8)
        return ansiColor(index, false);
    if (index < 16)
        return ansiColor(index - 8, true);
    if (index < 232) {
        const int base = index - 16;
        const int r = base / 36;
        const int g = (base / 6) % 6;
        const int b = base % 6;
        auto scale = [](int value) { return value == 0 ? 0 : 55 + value * 40; };
        return QColor(scale(r), scale(g), scale(b));
    }
    if (index < 256) {
        const int gray = 8 + (index - 232) * 10;
        return QColor(gray, gray, gray);
    }
    return {};
}

struct AnsiState {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    QColor fg;
    QColor bg;
};

QString htmlStyle(const AnsiState &state)
{
    QStringList style;
    if (state.fg.isValid())
        style.append(QStringLiteral("color:%1").arg(state.fg.name()));
    if (state.bg.isValid())
        style.append(QStringLiteral("background-color:%1").arg(state.bg.name()));
    if (state.bold)
        style.append(QStringLiteral("font-weight:700"));
    if (state.italic)
        style.append(QStringLiteral("font-style:italic"));
    if (state.underline)
        style.append(QStringLiteral("text-decoration:underline"));
    return style.join(QStringLiteral(";"));
}

void applyAnsiCode(AnsiState &state, const QList<int> &codes)
{
    QList<int> values = codes;
    if (values.isEmpty())
        values.append(0);

    for (int i = 0; i < values.size(); ++i) {
        const int code = values.at(i);
        if (code == 0) {
            state = {};
        } else if (code == 1) {
            state.bold = true;
        } else if (code == 3) {
            state.italic = true;
        } else if (code == 4) {
            state.underline = true;
        } else if (code == 22) {
            state.bold = false;
        } else if (code == 23) {
            state.italic = false;
        } else if (code == 24) {
            state.underline = false;
        } else if (code >= 30 && code <= 37) {
            state.fg = ansiColor(code - 30, false);
        } else if (code >= 90 && code <= 97) {
            state.fg = ansiColor(code - 90, true);
        } else if (code == 39) {
            state.fg = QColor();
        } else if (code >= 40 && code <= 47) {
            state.bg = ansiColor(code - 40, false);
        } else if (code >= 100 && code <= 107) {
            state.bg = ansiColor(code - 100, true);
        } else if (code == 49) {
            state.bg = QColor();
        } else if ((code == 38 || code == 48) && i + 1 < values.size()) {
            QColor color;
            const int mode = values.at(++i);
            if (mode == 5 && i + 1 < values.size()) {
                color = ansi256Color(values.at(++i));
            } else if (mode == 2 && i + 3 < values.size()) {
                color = QColor(values.at(i + 1), values.at(i + 2), values.at(i + 3));
                i += 3;
            }

            if (code == 38)
                state.fg = color;
            else
                state.bg = color;
        }
    }
}

QString ansiToHtml(const QByteArray &ansiText)
{
    QString html = QStringLiteral("<pre style=\"margin:0;font-family:monospace;white-space:pre;\">");
    AnsiState state;
    bool spanOpen = false;

    auto updateSpan = [&]() {
        if (spanOpen) {
            html += QStringLiteral("</span>");
            spanOpen = false;
        }
        const QString style = htmlStyle(state);
        if (!style.isEmpty()) {
            html += QStringLiteral("<span style=\"") + style.toHtmlEscaped() + QStringLiteral("\">");
            spanOpen = true;
        }
    };

    int index = 0;
    while (index < ansiText.size()) {
        if (ansiText.at(index) == '\x1b' && index + 1 < ansiText.size() && ansiText.at(index + 1) == '[') {
            const int seqStart = index + 2;
            int seqEnd = seqStart;
            while (seqEnd < ansiText.size() && ansiText.at(seqEnd) != 'm')
                ++seqEnd;

            if (seqEnd < ansiText.size() && ansiText.at(seqEnd) == 'm') {
                const QByteArray params = ansiText.mid(seqStart, seqEnd - seqStart);
                QList<int> codes;
                const QList<QByteArray> parts = params.split(';');
                for (const QByteArray &part : parts) {
                    if (part.isEmpty())
                        codes.append(0);
                    else
                        codes.append(part.toInt());
                }
                applyAnsiCode(state, codes);
                updateSpan();
                index = seqEnd + 1;
                continue;
            }
        }

        int nextEscape = ansiText.indexOf('\x1b', index);
        if (nextEscape < 0)
            nextEscape = ansiText.size();
        QString chunk = QString::fromUtf8(ansiText.mid(index, nextEscape - index));
        chunk.replace(QStringLiteral("\t"), QStringLiteral("    "));
        html += chunk.toHtmlEscaped();
        index = nextEscape;
    }

    if (spanOpen)
        html += QStringLiteral("</span>");
    html += QStringLiteral("</pre>");
    return html;
}

QByteArray batPreview(const QString &path, int maxLines, QString *error)
{
    if (error)
        error->clear();

    const QString executable = batExecutable();
    if (executable.isEmpty())
        return {};

    QStringList args = {
        QStringLiteral("--color=always"),
        QStringLiteral("--paging=never"),
        QStringLiteral("--style=plain"),
        QStringLiteral("--wrap=never")
    };
    if (maxLines > 0)
        args.append(QStringLiteral("--line-range=:%1").arg(maxLines));
    args.append(QStringLiteral("--"));
    args.append(path);

    QProcess proc;
    proc.start(executable, args);
    if (!proc.waitForFinished(10000)) {
        if (error)
            *error = QStringLiteral("bat preview timed out");
        return {};
    }
    if (proc.exitCode() != 0) {
        if (error)
            *error = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        return {};
    }

    return proc.readAllStandardOutput();
}

}

PreviewService::PreviewService(QObject *parent)
    : QObject(parent)
{
}

bool PreviewService::pdfPreviewAvailable() const
{
#ifdef HYPRFM_HAS_POPPLER_QT6
    return true;
#else
    return false;
#endif
}

QVariantMap PreviewService::loadTextPreview(const QString &path, int maxBytes, int maxLines) const
{
    QVariantMap result;
    bool truncated = false;
    QString error;
    const QByteArray data = readPathBytes(path, maxBytes, &truncated, &error);

    if (!error.isEmpty()) {
        result["content"] = QString();
        result["html"] = QString();
        result["truncated"] = false;
        result["isBinary"] = false;
        result["usesBat"] = false;
        result["error"] = error;
        return result;
    }

    const bool binary = looksBinary(data);
    QString text;
    if (!binary)
        text = decodeText(data);

    QStringList lines = text.split('\n');
    if (maxLines > 0 && lines.size() > maxLines) {
        lines = lines.mid(0, maxLines);
        truncated = true;
    }

    const QString plainText = lines.join('\n');
    result["content"] = plainText;
    result["html"] = QString();
    result["truncated"] = truncated;
    result["isBinary"] = binary;
    result["usesBat"] = false;
    result["error"] = QString();
    result["lineCount"] = lines.size();

    if (!binary) {
        const QString previewPath = localPreviewPath(path);
        if (!previewPath.isEmpty()) {
            QString batError;
            const QByteArray coloredOutput = batPreview(previewPath, maxLines, &batError);
            if (!coloredOutput.isEmpty()) {
                result["html"] = ansiToHtml(coloredOutput);
                result["usesBat"] = true;
            }
        }
    }

    return result;
}

QVariantMap PreviewService::loadDirectoryPreview(const QString &path, int maxEntries) const
{
    QVariantMap result;
    bool truncated = false;
    QString error;
    const QStringList entries = listDirectoryEntries(path, maxEntries, &truncated, &error);

    result["entries"] = entries;
    result["truncated"] = truncated;
    result["error"] = error;
    result["count"] = entries.size();
    return result;
}

QVariantMap PreviewService::loadArchivePreview(const QString &path, int maxEntries) const
{
    QVariantMap result;
    result["entries"] = QStringList();
    result["truncated"] = false;
    result["error"] = QString();
    result["count"] = 0;

    // Determine list command based on archive type
    // Reuse the same detection as fileoperations
    const QString lower = path.toLower();
    QString program;
    QStringList args;

    if (lower.endsWith(".zip")) {
        program = "unzip";
        args = {"-Z1", path};
    } else if (lower.endsWith(".tar.gz") || lower.endsWith(".tgz")) {
        program = "tar";
        args = {"-tzf", path};
    } else if (lower.endsWith(".tar.xz") || lower.endsWith(".txz")) {
        program = "tar";
        args = {"-tJf", path};
    } else if (lower.endsWith(".tar.bz2") || lower.endsWith(".tbz2")) {
        program = "tar";
        args = {"-tjf", path};
    } else if (lower.endsWith(".tar")) {
        program = "tar";
        args = {"-tf", path};
    } else if (lower.endsWith(".7z") || lower.endsWith(".rar")) {
        program = "7z";
        args = {"l", "-slt", path};
    } else {
        result["error"] = "Unsupported archive format";
        return result;
    }

    QProcess proc;
    proc.start(program, args);
    if (!proc.waitForFinished(10000) || proc.exitCode() != 0) {
        result["error"] = "Could not list archive contents";
        return result;
    }

    const QString output = QString::fromUtf8(proc.readAllStandardOutput());
    QStringList entries;
    bool truncated = false;

    if (program == "7z") {
        // 7z -slt output: "Path = filename" lines
        static const QRegularExpression pathRe(R"(^Path = (.+)$)", QRegularExpression::MultilineOption);
        auto it = pathRe.globalMatch(output);
        while (it.hasNext()) {
            const QString entry = it.next().captured(1).trimmed();
            if (entry.isEmpty() || entry == path)
                continue;
            if (entries.size() >= maxEntries) { truncated = true; break; }
            entries.append(entry);
        }
    } else {
        const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty())
                continue;
            if (entries.size() >= maxEntries) { truncated = true; break; }
            entries.append(trimmed);
        }
    }

    result["entries"] = entries;
    result["truncated"] = truncated;
    result["count"] = entries.size();
    return result;
}

QString PreviewService::localPreviewPath(const QString &path) const
{
    if (path.isEmpty())
        return {};

    if (!isTrashUri(path))
        return QFileInfo::exists(path) ? path : QString();

    QString cacheRoot = QDir::homePath() + "/.cache/hyprfm/preview-cache";
    QDir().mkpath(cacheRoot);

    const QString suffix = QFileInfo(QUrl(path).fileName()).suffix();
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex());
    const QString cachedPath = QDir(cacheRoot).filePath(suffix.isEmpty() ? hash : hash + "." + suffix);

    QProcess proc;
    startGioCat(proc, encodedUri(path));
    if (!proc.waitForFinished(10000) || proc.exitCode() != 0)
        return {};

    QFile cacheFile(cachedPath);
    if (!cacheFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};

    cacheFile.write(proc.readAllStandardOutput());
    cacheFile.close();

    return cachedPath;
}

QVariantMap PreviewService::loadFontPreview(const QString &path)
{
    QVariantMap result;
    result["family"] = QString();
    result["styleName"] = QString();
    result["weight"] = static_cast<int>(QFont::Normal);
    result["italic"] = false;
    result["valid"] = false;
    result["error"] = QString();

    if (path.isEmpty() || !QFileInfo::exists(path)) {
        result["error"] = QStringLiteral("Font file not found");
        return result;
    }

    // Short-circuit when the same path is already loaded so repeated reads
    // (e.g. preview refresh on selection change) don't thrash the database.
    const bool alreadyLoaded = m_activeFontPreviewId >= 0 && m_activeFontPreviewPath == path;

    if (!alreadyLoaded) {
        if (m_activeFontPreviewId >= 0) {
            QFontDatabase::removeApplicationFont(m_activeFontPreviewId);
            m_activeFontPreviewId = -1;
            m_activeFontPreviewPath.clear();
        }

        const int id = QFontDatabase::addApplicationFont(path);
        if (id < 0) {
            result["error"] = QStringLiteral("Unable to load font file");
            return result;
        }
        m_activeFontPreviewId = id;
        m_activeFontPreviewPath = path;
    }

    const QStringList families = QFontDatabase::applicationFontFamilies(m_activeFontPreviewId);
    if (families.isEmpty()) {
        result["error"] = QStringLiteral("Font contains no usable families");
        return result;
    }

    const QString family = families.first();

    // Pull exact face metadata straight from the file so variants of the
    // same family (e.g. MapleMono-Bold vs MapleMono-Italic) don't alias.
    QRawFont raw(path, 16.0);
    QString styleName = raw.isValid() ? raw.styleName() : QString();
    int weight = raw.isValid() ? raw.weight() : static_cast<int>(QFont::Normal);
    const bool italic = raw.isValid() ? (raw.style() != QFont::StyleNormal) : false;

    if (styleName.isEmpty()) {
        const QStringList styles = QFontDatabase::styles(family);
        if (!styles.isEmpty())
            styleName = styles.first();
    }

    result["family"] = family;
    result["styleName"] = styleName;
    result["weight"] = weight;
    result["italic"] = italic;
    result["valid"] = true;
    return result;
}

QVariantMap PreviewService::loadPdfPreview(const QString &path) const
{
    QVariantMap result;
    result["localPath"] = QString();
    result["pageCount"] = 0;
    result["error"] = QString();

    const QString localPath = localPreviewPath(path);
    if (localPath.isEmpty()) {
        result["error"] = QStringLiteral("Unable to prepare PDF preview");
        return result;
    }

#ifdef HYPRFM_HAS_POPPLER_QT6
    std::unique_ptr<Poppler::Document> document = Poppler::Document::load(localPath);
    if (!document) {
        result["error"] = QStringLiteral("Unable to open PDF document");
        return result;
    }

    result["localPath"] = localPath;
    result["pageCount"] = document->numPages();
    return result;
#else
    result["error"] = QStringLiteral("PDF preview support is unavailable in this build");
    return result;
#endif
}

QByteArray PreviewService::readPathBytes(const QString &path, qint64 maxBytes, bool *truncated,
                                         QString *error) const
{
    if (truncated)
        *truncated = false;
    if (error)
        error->clear();

    if (path.isEmpty()) {
        if (error)
            *error = QStringLiteral("No file selected");
        return {};
    }

    const qint64 readLimit = qMax<qint64>(1, maxBytes) + 1;

    if (isTrashUri(path)) {
        QProcess proc;
        startGioCat(proc, encodedUri(path));
        if (!proc.waitForStarted(2000)) {
            if (error)
                *error = QStringLiteral("Failed to start preview reader");
            return {};
        }

        QByteArray data;
        while (proc.state() != QProcess::NotRunning) {
            if (!proc.waitForReadyRead(100))
                proc.waitForFinished(100);
            data += proc.readAllStandardOutput();
            if (data.size() >= readLimit) {
                proc.kill();
                proc.waitForFinished(1000);
                break;
            }
        }
        data += proc.readAllStandardOutput();

        if (proc.exitStatus() != QProcess::NormalExit && data.isEmpty()) {
            if (error)
                *error = QStringLiteral("Failed to read preview data");
            return {};
        }

        if (data.size() > maxBytes) {
            if (truncated)
                *truncated = true;
            data.truncate(maxBytes);
        }
        return data;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return {};
    }

    QByteArray data = file.read(readLimit);
    if (data.size() > maxBytes) {
        if (truncated)
            *truncated = true;
        data.truncate(maxBytes);
    }
    return data;
}

QStringList PreviewService::listDirectoryEntries(const QString &path, int maxEntries, bool *truncated,
                                                QString *error) const
{
    if (truncated)
        *truncated = false;
    if (error)
        error->clear();

    if (path.isEmpty()) {
        if (error)
            *error = QStringLiteral("No folder selected");
        return {};
    }

    if (isTrashUri(path)) {
        QProcess proc;
        proc.start("gio", {"list", "-h", encodedUri(path)});
        if (!proc.waitForFinished(5000) || proc.exitCode() != 0) {
            if (error)
                *error = QString::fromUtf8(proc.readAllStandardError()).trimmed();
            return {};
        }

        const QStringList allEntries = QString::fromUtf8(proc.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
        if (truncated)
            *truncated = maxEntries > 0 && allEntries.size() > maxEntries;
        return maxEntries > 0 ? allEntries.mid(0, maxEntries) : allEntries;
    }

    QDir dir(path);
    if (!dir.exists()) {
        if (error)
            *error = QStringLiteral("Folder does not exist");
        return {};
    }

    const QFileInfoList allEntries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                                                       QDir::DirsFirst | QDir::IgnoreCase | QDir::Name);
    QStringList names;
    const int count = maxEntries > 0 ? qMin(maxEntries, allEntries.size()) : allEntries.size();
    for (int i = 0; i < count; ++i) {
        const QFileInfo &info = allEntries.at(i);
        names.append(info.isDir() ? info.fileName() + "/" : info.fileName());
    }

    if (truncated)
        *truncated = maxEntries > 0 && allEntries.size() > maxEntries;
    return names;
}

bool PreviewService::isTrashUri(const QString &path)
{
    return QUrl(path).scheme() == QStringLiteral("trash");
}

bool PreviewService::looksBinary(const QByteArray &data)
{
    if (data.contains('\0'))
        return true;

    const int sampleSize = qMin(data.size(), 4096);
    if (sampleSize <= 0)
        return false;

    int suspicious = 0;
    for (int i = 0; i < sampleSize; ++i) {
        const unsigned char ch = static_cast<unsigned char>(data.at(i));
        const bool isWhitespace = ch == '\n' || ch == '\r' || ch == '\t' || ch == '\f';
        if (!isWhitespace && ch < 0x20)
            ++suspicious;
    }

    return suspicious * 10 > sampleSize;
}

QString PreviewService::decodeText(const QByteArray &data)
{
    const QString utf8 = QString::fromUtf8(data);
    if (!utf8.contains(QChar::ReplacementCharacter))
        return utf8;
    return QString::fromLocal8Bit(data);
}
