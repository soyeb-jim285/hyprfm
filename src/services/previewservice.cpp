#include "services/previewservice.h"
#include "services/archivepassword.h"

#include "services/metadataextractor.h"

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
#include <QThreadPool>
#include <QUrl>

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

} // namespace

QString PreviewService::ansiToHtml(const QByteArray &ansiText)
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
        if (nextEscape == index) {
            // ESC that is not a CSI colour sequence (e.g. ESC ( B): drop
            // the byte and keep going, otherwise the loop never advances.
            ++index;
            continue;
        }
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

namespace {

// Collects stdout until the process exits or readLimit bytes arrive, then
// kills it. Keeps `gio cat` of a multi-GB file from buffering it all.
QByteArray readBoundedOutput(QProcess &proc, qint64 readLimit)
{
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
    return data;
}

// bat highlights `data` (already capped by the caller) rather than the file
// itself: one 300 MB line would otherwise come back as 300 MB of HTML.
QByteArray batPreview(const QString &path, const QByteArray &data, int maxLines, QString *error)
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
    args.append(QStringLiteral("--file-name"));
    args.append(QUrl(path).fileName().isEmpty() ? path : QUrl(path).fileName());

    QProcess proc;
    proc.start(executable, args);
    proc.write(data);
    proc.closeWriteChannel();
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

// A Markdown document is rendered to HTML rather than run through bat, so the
// preview pane shows a styled document instead of highlighted raw source.
bool isMarkdownPath(const QString &path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == QLatin1String("md") || ext == QLatin1String("markdown")
        || ext == QLatin1String("mdown") || ext == QLatin1String("mkd");
}

// Render Markdown to HTML via `md2html` (md4c). Reads the data on stdin so the
// path never has to be quoted, and never touches a shell. Returns empty and
// sets `error` when md2html is missing, times out, or fails.
QByteArray markdownToHtml(const QByteArray &markdown, QString *error)
{
    if (error)
        error->clear();

    const QString executable = QStandardPaths::findExecutable(QStringLiteral("md2html"));
    if (executable.isEmpty()) {
        if (error)
            *error = QStringLiteral("md2html not found");
        return {};
    }

    QProcess proc;
    // GFM (--github) enables GitHub-flavoured tables plus strikethrough,
    // autolinks and task lists. Qt's rich-text engine renders all of them
    // (minus the checkbox glyphs on task list items).
    proc.start(executable, {QStringLiteral("--github")});
    if (!proc.waitForStarted(2000)) {
        if (error)
            *error = proc.errorString();
        return {};
    }
    proc.write(markdown);
    proc.closeWriteChannel();
    if (!proc.waitForFinished(5000)) {
        proc.kill();
        if (error)
            *error = QStringLiteral("md2html preview timed out");
        return {};
    }
    if (proc.exitCode() != 0) {
        if (error)
            *error = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        return {};
    }

    QString html = QString::fromUtf8(proc.readAllStandardOutput());
    // Qt's rich-text engine only draws an HTML <table> as a bordered grid when
    // the tag carries a border attribute; otherwise it flattens the cells into
    // run-on text. md2html emits a bare <table>, so inject the attributes here
    // to get real column layout and gridlines in the preview.
    html.replace(QStringLiteral("<table>"),
                 QStringLiteral("<table border=\"1\" cellpadding=\"4\" cellspacing=\"0\">"));
    return html.toUtf8();
}

}

PreviewService::PreviewService(QObject *parent)
    : QObject(parent)
    , m_pool(new QThreadPool(this))
{
    // Two threads: one preview is in flight at a time, but a request that
    // hits loadArchivePreview's 10 s timeout must not delay the next one.
    m_pool->setMaxThreadCount(2);
}

PreviewService::~PreviewService()
{
    // Workers capture `this`. Drain them before the members they touch die.
    {
        QMutexLocker locker(&m_generationMutex);
        for (auto it = m_generations.begin(); it != m_generations.end(); ++it)
            ++it.value();
    }
    m_pool->waitForDone();
}

void PreviewService::setMetadataExtractor(MetadataExtractor *extractor)
{
    m_metadata = extractor;
}

quint64 PreviewService::bumpGeneration(const QString &requester)
{
    QMutexLocker locker(&m_generationMutex);
    return ++m_generations[requester];
}

bool PreviewService::isCurrent(const QString &requester, quint64 generation) const
{
    QMutexLocker locker(&m_generationMutex);
    return m_generations.value(requester) == generation;
}

void PreviewService::cancelPreview(const QString &requester)
{
    bumpGeneration(requester);
}

void PreviewService::requestPreview(const QString &requester, const QString &path,
                                    const QString &kind, const QString &password)
{
    const quint64 generation = bumpGeneration(requester);

    if (path.isEmpty()) {
        emit previewReady(requester, path, QVariantMap());
        return;
    }

    m_pool->start([this, requester, path, kind, password, generation]() {
        const QVariantMap data = buildPreview(path, kind, password);

        // Cheap early-out so a superseded worker doesn't queue a no-op onto
        // the GUI thread. The authoritative check is the one inside the
        // lambda below: the generation can still move while this is queued.
        if (!isCurrent(requester, generation))
            return;

        QMetaObject::invokeMethod(this, [this, requester, path, data, generation]() {
            if (!isCurrent(requester, generation))
                return;
            emit previewReady(requester, path, data);
        }, Qt::QueuedConnection);
    });
}

QVariantMap PreviewService::buildPreview(const QString &path, const QString &kind,
                                         const QString &password) const
{
    QVariantMap data;

    if (kind == QLatin1String("text"))
        data["text"] = loadTextPreview(path);
    else if (kind == QLatin1String("pdf"))
        data["pdf"] = loadPdfPreview(path);
    else if (kind == QLatin1String("archive"))
        data["archive"] = loadArchivePreview(path, 200, password);
    else if (kind == QLatin1String("directory"))
        data["directory"] = loadDirectoryPreview(path);

    // exiftool/ffprobe cost ~120 ms each and used to run on the GUI thread
    // for every image, video, audio and PDF selection.
    if (m_metadata)
        data["metadata"] = m_metadata->extract(path);

    return data;
}

bool PreviewService::pdfPreviewAvailable() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("pdftoppm")).isEmpty()
        && !QStandardPaths::findExecutable(QStringLiteral("pdfinfo")).isEmpty();
}

void PreviewService::refreshSupport()
{
    emit supportChanged();
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

    if (!binary && isMarkdownPath(path)) {
        // Rendered Markdown document (md2html), not bat highlighting.
        QString mdError;
        const QByteArray html = markdownToHtml(data, &mdError);
        if (!html.isEmpty()) {
            result["html"] = QString::fromUtf8(html);
            result["usesBat"] = true;   // QML routes this into its RichText branch
            result["markdown"] = true;  // lets QML choose wrap/font for a document
            return result;
        }
        // md2html missing/failed: fall through to bat so the user still sees
        // the raw source, highlighted, rather than a blank pane.
    }

    if (!binary) {
        QString batError;
        const QByteArray coloredOutput = batPreview(path, data, maxLines, &batError);
        if (!coloredOutput.isEmpty()) {
            result["html"] = ansiToHtml(coloredOutput);
            result["usesBat"] = true;
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

QVariantMap PreviewService::loadArchivePreview(const QString &path, int maxEntries,
                                               const QString &password) const
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
    } else if (lower.endsWith(".tar.zst") || lower.endsWith(".tzst")) {
        program = "tar";
        args = {"--zstd", "-tf", path};
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
        // Always pass -p so 7z never waits for interactive input: an archive
        // without a password ignores it, an encrypted one fails immediately.
        program = "7z";
        args = {"l", "-slt", "-p" + effectiveArchivePassword(password), path};
    } else {
        result["error"] = "Unsupported archive format";
        return result;
    }

    QProcess proc;
    proc.start(program, args);
    if (!proc.waitForFinished(10000)) {
        result["error"] = "Could not list archive contents";
        return result;
    }

    if (proc.exitCode() != 0) {
        const QString errorText = QString::fromUtf8(proc.readAllStandardError());
        if (errorText.contains(QLatin1String("password"), Qt::CaseInsensitive)
            || errorText.contains(QLatin1String("passphrase"), Qt::CaseInsensitive)
            || errorText.contains(QLatin1String("encrypted"), Qt::CaseInsensitive)) {
            result["requiresPassword"] = true;
            result["error"] = "This archive is password-protected.";
        } else {
            result["error"] = "Could not list archive contents";
        }
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

    // The cache only ever needs the file being previewed right now; drop the
    // previous copy so trashed files don't pile up in plaintext forever.
    QString cacheRoot = QDir::homePath() + "/.cache/hyprfm/preview-cache";
    QDir(cacheRoot).removeRecursively();
    QDir().mkpath(cacheRoot);
    QFile::setPermissions(cacheRoot, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

    const QString suffix = QFileInfo(QUrl(path).fileName()).suffix();
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex());
    const QString cachedPath = QDir(cacheRoot).filePath(suffix.isEmpty() ? hash : hash + "." + suffix);

    // ponytail: 64 MB hard cap; larger trashed files simply get no preview.
    constexpr qint64 kMaxPreviewCopyBytes = 64 * 1024 * 1024;
    QProcess proc;
    startGioCat(proc, encodedUri(path));
    if (!proc.waitForStarted(2000))
        return {};
    const QByteArray data = readBoundedOutput(proc, kMaxPreviewCopyBytes + 1);
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0
            || data.size() > kMaxPreviewCopyBytes)
        return {};

    QFile cacheFile(cachedPath);
    if (!cacheFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};

    cacheFile.write(data);
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

    if (path.isEmpty() || !QFileInfo(path).isFile()) {
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

    if (!pdfPreviewAvailable()) {
        result["error"] = QStringLiteral("Install poppler-utils for PDF preview");
        return result;
    }

    QProcess proc;
    proc.start(QStringLiteral("pdfinfo"), {localPath});
    if (!proc.waitForFinished(5000) || proc.exitCode() != 0) {
        result["error"] = QStringLiteral("Unable to open PDF document");
        return result;
    }

    const QString out = QString::fromUtf8(proc.readAllStandardOutput());
    static const QRegularExpression pagesRe(QStringLiteral(R"(^Pages:\s*(\d+))"),
                                            QRegularExpression::MultilineOption);
    const auto m = pagesRe.match(out);
    if (!m.hasMatch()) {
        result["error"] = QStringLiteral("Unable to read PDF page count");
        return result;
    }

    result["localPath"] = localPath;
    result["pageCount"] = m.captured(1).toInt();
    return result;
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

        QByteArray data = readBoundedOutput(proc, readLimit);

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

    // FIFOs, devices and sockets block open()/read() indefinitely.
    if (!QFileInfo(path).isFile()) {
        if (error)
            *error = QStringLiteral("Not a regular file");
        return {};
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
