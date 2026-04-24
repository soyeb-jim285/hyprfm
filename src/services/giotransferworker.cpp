#include "services/giotransferworker.h"

#include <QLoggingCategory>
#include <QUrl>

// Turn on with QT_LOGGING_RULES="hyprfm.transfer.debug=true" to trace every
// item. Warnings/errors go out unconditionally under hyprfm.transfer so the
// user sees failure reasons in the terminal.
Q_LOGGING_CATEGORY(lcTransfer, "hyprfm.transfer")

namespace {

bool isUriPath(const QString &path)
{
    const QUrl url(path);
    return url.isValid() && !url.scheme().isEmpty();
}

GFile *gFileForLocation(const QString &path)
{
    const QByteArray utf8 = path.toUtf8();
    if (isUriPath(path))
        return g_file_new_for_uri(utf8.constData());
    return g_file_new_for_path(utf8.constData());
}

struct MountResult {
    bool ok = false;
    int errCode = 0;
    QString errMsg;
    GMainLoop *loop = nullptr;
};

// Generic GIO failures on afc:// / gphoto2:// surface libimobiledevice's
// raw messages ("No device found. Make sure usbmuxd is set up correctly.",
// "Device requested to be paired", etc.). These aren't actionable on their
// own — translate to something the user can do.
QString humanizeMobileDeviceError(const QString &raw)
{
    const QString lower = raw.toLower();
    const bool mentionsMobile = lower.contains(QLatin1String("libimobiledevice"))
                             || lower.contains(QLatin1String("usbmuxd"))
                             || lower.contains(QLatin1String("no device found"))
                             || lower.contains(QLatin1String("not paired"))
                             || lower.contains(QLatin1String("device requested to be paired"))
                             || lower.contains(QLatin1String("pair record"));
    if (!mentionsMobile)
        return raw;

    return QStringLiteral(
        "iPhone / iPad is unreachable. "
        "Plug it in, unlock the screen, tap \"Trust\" when prompted, "
        "and make sure usbmuxd is running (systemctl status usbmuxd).");
}

// Block the calling (worker) thread until `g_file_mount_enclosing_volume`
// completes. Dolphin does the equivalent through KIO's worker, which quietly
// mounts GVfs volumes on first access. Without this, operations against an
// `afc://` / `mtp://` / `smb://` URI whose volume happens to be un-mounted at
// the moment fail with G_IO_ERROR_NOT_MOUNTED even if the user reached that
// URI through the sidebar.
bool mountEnclosingVolumeSync(GFile *file, QString *errorOut, int *errCodeOut)
{
    GMainContext *ctx = g_main_context_new();
    g_main_context_push_thread_default(ctx);

    MountResult result;
    result.loop = g_main_loop_new(ctx, FALSE);

    g_file_mount_enclosing_volume(
        file,
        G_MOUNT_MOUNT_NONE,
        nullptr,   // no GMountOperation — no auth prompts from the worker
        nullptr,
        [](GObject *src, GAsyncResult *res, gpointer data) {
            auto *r = static_cast<MountResult *>(data);
            GError *err = nullptr;
            if (g_file_mount_enclosing_volume_finish(G_FILE(src), res, &err)) {
                r->ok = true;
            } else if (err) {
                // ALREADY_MOUNTED means somebody raced us — still good.
                if (err->code == G_IO_ERROR_ALREADY_MOUNTED) {
                    r->ok = true;
                } else {
                    r->errCode = err->code;
                    r->errMsg = QString::fromUtf8(err->message);
                }
                g_error_free(err);
            }
            g_main_loop_quit(r->loop);
        },
        &result);

    g_main_loop_run(result.loop);
    g_main_loop_unref(result.loop);
    g_main_context_pop_thread_default(ctx);
    g_main_context_unref(ctx);

    if (errorOut)
        *errorOut = result.errMsg;
    if (errCodeOut)
        *errCodeOut = result.errCode;
    return result.ok;
}

} // namespace

GioTransferWorker::GioTransferWorker(QObject *parent)
    : QObject(parent)
{
}

void GioTransferWorker::execute(const QList<TransferItem> &items, bool moveOperation)
{
    // Reset state
    m_cancelled.store(false);
    m_paused.store(false);
    m_completedBytes = 0;
    m_currentItemBytes = 0;
    m_lastEmitMs = 0;
    m_lastEmittedProgress = -1.0;

    m_cancellable = g_cancellable_new();
    m_elapsed.start();

    // Indeterminate pre-scan
    emit progressUpdated(-1.0, {}, {});

    m_totalBytes = scanTotalBytes(items);
    if (m_totalBytes < 1)
        m_totalBytes = 1;

    bool success = true;
    QString errorMsg;

    for (const auto &item : items) {
        if (m_cancelled.load()) {
            success = false;
            errorMsg = {};
            break;
        }

        qCInfo(lcTransfer).nospace() << "item start  src=" << item.sourcePath
                                     << " dst=" << item.targetPath
                                     << (moveOperation ? " (move)" : " (copy)");

        emit itemStarted(item.sourcePath, item.targetPath);

        GFile *targetFile = gFileForLocation(item.targetPath);

        // Don't call mount_enclosing_volume proactively. GVfs volumes opened
        // through the sidebar (click-to-browse) are already mounted, and the
        // afc backend treats a second mount attempt on an already-mounted
        // House-Arrest port (afc://UDID:N/) as a fresh session request —
        // libimobiledevice then confusingly returns "No device found" even
        // though the existing mount is fine. Let the copy use the active
        // mount; we only remount if the copy itself fails with NOT_MOUNTED
        // (handled below — covers gvfs idle-timeout disconnects).

        GFile *targetParent = g_file_get_parent(targetFile);
        if (targetParent) {
            // Skip the create call entirely when the parent already exists —
            // the common case for pasting into an existing folder. Avoids a
            // round-trip on slow GVfs mounts and suppresses spurious
            // "not mounted" warnings from iOS afc:// / MTP backends that
            // refuse mkdir at their root even though the target dir is fine.
            if (!g_file_query_exists(targetParent, nullptr)) {
                GError *dirErr = nullptr;
                g_file_make_directory_with_parents(targetParent, nullptr, &dirErr);
                if (dirErr) {
                    const int code = dirErr->code;
                    const bool harmless = code == G_IO_ERROR_EXISTS
                                       || code == G_IO_ERROR_NOT_MOUNTED
                                       || code == G_IO_ERROR_NOT_SUPPORTED
                                       || code == G_IO_ERROR_READ_ONLY
                                       || code == G_IO_ERROR_PERMISSION_DENIED;
                    if (!harmless) {
                        qCWarning(lcTransfer).nospace()
                            << "make_directory_with_parents failed for "
                            << item.targetPath << " (code " << code << "): "
                            << dirErr->message;
                    }
                    g_error_free(dirErr);
                }
            }
            g_object_unref(targetParent);
        }

        // Handle backup if overwrite + backupPath set
        if (item.overwrite && !item.backupPath.isEmpty()) {
            GFile *existingTarget = gFileForLocation(item.targetPath);
            GFile *backupFile = gFileForLocation(item.backupPath);
            GFile *backupParent = g_file_get_parent(backupFile);
            if (backupParent) {
                if (!g_file_query_exists(backupParent, nullptr)) {
                    GError *bpErr = nullptr;
                    g_file_make_directory_with_parents(backupParent, nullptr, &bpErr);
                    if (bpErr)
                        g_error_free(bpErr);
                }
                g_object_unref(backupParent);
            }
            GError *mvErr = nullptr;
            g_file_move(existingTarget, backupFile, G_FILE_COPY_NONE, nullptr, nullptr, nullptr, &mvErr);
            if (mvErr)
                g_error_free(mvErr);
            g_object_unref(existingTarget);
            g_object_unref(backupFile);
        }

        // Query source file type
        GFile *sourceFile = gFileForLocation(item.sourcePath);
        GError *infoErr = nullptr;
        GFileInfo *info = g_file_query_info(sourceFile,
            G_FILE_ATTRIBUTE_STANDARD_TYPE,
            G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
            m_cancellable, &infoErr);

        if (!info) {
            success = false;
            errorMsg = gErrorToUserMessage(infoErr);
            if (infoErr) g_error_free(infoErr);
            g_object_unref(sourceFile);
            g_object_unref(targetFile);
            break;
        }

        GFileType fileType = g_file_info_get_file_type(info);
        g_object_unref(info);

        GFileCopyFlags flags = static_cast<GFileCopyFlags>(
            G_FILE_COPY_ALL_METADATA | (item.overwrite ? G_FILE_COPY_OVERWRITE : 0));

        if (fileType == G_FILE_TYPE_SYMBOLIC_LINK) {
            // Read symlink target, recreate at destination
            GError *slErr = nullptr;
            GFileInfo *slInfo = g_file_query_info(sourceFile,
                G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET,
                G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                m_cancellable, &slErr);
            if (!slInfo) {
                success = false;
                errorMsg = gErrorToUserMessage(slErr);
                if (slErr) g_error_free(slErr);
                g_object_unref(sourceFile);
                g_object_unref(targetFile);
                break;
            }

            const char *slTarget = g_file_info_has_attribute(
                slInfo, G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET)
                ? g_file_info_get_symlink_target(slInfo)
                : nullptr;
            if (!slTarget) {
                success = false;
                errorMsg = tr("Could not read symbolic link target");
                g_object_unref(slInfo);
                g_object_unref(sourceFile);
                g_object_unref(targetFile);
                break;
            }

            GError *mkErr = nullptr;
            g_file_make_symbolic_link(targetFile, slTarget, m_cancellable, &mkErr);
            if (mkErr) {
                success = false;
                errorMsg = gErrorToUserMessage(mkErr);
                g_error_free(mkErr);
                g_object_unref(slInfo);
                g_object_unref(sourceFile);
                g_object_unref(targetFile);
                break;
            }
            g_object_unref(slInfo);
            m_completedBytes += 1;
        } else if (fileType == G_FILE_TYPE_DIRECTORY) {
            if (!copyRecursive(sourceFile, targetFile, flags, &errorMsg)) {
                success = false;
                qCWarning(lcTransfer).nospace()
                    << "directory copy failed  src=" << item.sourcePath
                    << " dst=" << item.targetPath
                    << " msg=" << errorMsg;
                g_object_unref(sourceFile);
                g_object_unref(targetFile);
                break;
            }
            qCInfo(lcTransfer) << "directory copy done" << item.targetPath;
        } else {
            // Regular file
            GError *cpErr = nullptr;
            gboolean ok = g_file_copy(sourceFile, targetFile, flags,
                m_cancellable, progressCallback, this, &cpErr);

            // Late auto-mount + retry: a volume can become un-mounted
            // between startup and copy (gvfs idle timeouts are common on
            // afc://). Mount once, try again, then give up.
            if (!ok && cpErr && cpErr->code == G_IO_ERROR_NOT_MOUNTED
                && isUriPath(item.targetPath)) {
                qCInfo(lcTransfer) << "copy hit NOT_MOUNTED; attempting remount for"
                                   << item.targetPath;
                g_error_free(cpErr);
                cpErr = nullptr;

                QString mountErr;
                int mountCode = 0;
                if (mountEnclosingVolumeSync(targetFile, &mountErr, &mountCode)) {
                    ok = g_file_copy(sourceFile, targetFile, flags,
                        m_cancellable, progressCallback, this, &cpErr);
                } else {
                    errorMsg = mountErr.isEmpty()
                        ? QStringLiteral("Could not mount destination volume")
                        : mountErr;
                }
            }

            if (!ok) {
                success = false;
                if (errorMsg.isEmpty()) {
                    const QString raw = gErrorToUserMessage(cpErr);
                    errorMsg = humanizeMobileDeviceError(raw);
                }
                qCWarning(lcTransfer).nospace()
                    << "copy failed  src=" << item.sourcePath
                    << " dst=" << item.targetPath
                    << " code=" << (cpErr ? cpErr->code : -1)
                    << " msg=" << (cpErr ? cpErr->message : "(null)");
                if (cpErr) g_error_free(cpErr);
                g_object_unref(sourceFile);
                g_object_unref(targetFile);
                break;
            }
            m_completedBytes += m_currentItemBytes;
            m_currentItemBytes = 0;
        }

        // For moves: delete source after successful copy
        if (success && moveOperation) {
            if (fileType == G_FILE_TYPE_DIRECTORY) {
                QString delErr;
                if (!deleteRecursive(sourceFile, &delErr)) {
                    success = false;
                    errorMsg = delErr;
                    g_object_unref(sourceFile);
                    g_object_unref(targetFile);
                    break;
                }
            } else {
                GError *delErr = nullptr;
                g_file_delete(sourceFile, m_cancellable, &delErr);
                if (delErr) {
                    success = false;
                    errorMsg = gErrorToUserMessage(delErr);
                    g_error_free(delErr);
                    g_object_unref(sourceFile);
                    g_object_unref(targetFile);
                    break;
                }
            }
        }

        g_object_unref(sourceFile);
        g_object_unref(targetFile);
    }

    g_object_unref(m_cancellable);
    m_cancellable = nullptr;

    emit finished(success, errorMsg);
}

void GioTransferWorker::cancel()
{
    m_cancelled.store(true);
    if (m_cancellable)
        g_cancellable_cancel(m_cancellable);
    m_pauseCondition.wakeAll();
}

void GioTransferWorker::pause()
{
    m_paused.store(true);
}

void GioTransferWorker::resume()
{
    m_paused.store(false);
    m_pauseCondition.wakeAll();
}

qint64 GioTransferWorker::scanTotalBytes(const QList<TransferItem> &items)
{
    qint64 total = 0;
    for (const auto &item : items) {
        GFile *file = gFileForLocation(item.sourcePath);
        total += scanPathBytes(file);
        g_object_unref(file);
    }
    return total;
}

qint64 GioTransferWorker::scanPathBytes(GFile *file)
{
    GError *err = nullptr;
    GFileInfo *info = g_file_query_info(file,
        G_FILE_ATTRIBUTE_STANDARD_SIZE "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, nullptr, &err);

    if (!info) {
        if (err) g_error_free(err);
        return 1;
    }

    GFileType type = g_file_info_get_file_type(info);

    if (type == G_FILE_TYPE_SYMBOLIC_LINK) {
        g_object_unref(info);
        return 1;
    }

    if (type == G_FILE_TYPE_REGULAR) {
        goffset size = g_file_info_get_size(info);
        g_object_unref(info);
        return size < 1 ? 1 : size;
    }

    if (type == G_FILE_TYPE_DIRECTORY) {
        g_object_unref(info);
        qint64 total = 1; // 1 byte for the directory entry itself

        GError *enumErr = nullptr;
        GFileEnumerator *enumerator = g_file_enumerate_children(file,
            G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_SIZE "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
            G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, nullptr, &enumErr);

        if (!enumerator) {
            if (enumErr) g_error_free(enumErr);
            return total;
        }

        GFileInfo *childInfo;
        while ((childInfo = g_file_enumerator_next_file(enumerator, nullptr, nullptr)) != nullptr) {
            const char *name = g_file_info_get_name(childInfo);
            GFile *child = g_file_get_child(file, name);
            total += scanPathBytes(child);
            g_object_unref(child);
            g_object_unref(childInfo);
        }

        g_file_enumerator_close(enumerator, nullptr, nullptr);
        g_object_unref(enumerator);
        return total;
    }

    g_object_unref(info);
    return 1;
}

bool GioTransferWorker::copyRecursive(GFile *source, GFile *destination, GFileCopyFlags flags, QString *error)
{
    gchar *dstUriRaw = g_file_get_uri(destination);
    const QString dstUri = QString::fromUtf8(dstUriRaw ? dstUriRaw : "");
    if (dstUriRaw) g_free(dstUriRaw);

    qCDebug(lcTransfer) << "recurse mkdir" << dstUri;

    // Create target directory. If the backing GVfs volume timed out between
    // the user opening it in the sidebar and this transfer firing, we get
    // G_IO_ERROR_NOT_MOUNTED — remount once and try again.
    GError *mkErr = nullptr;
    g_file_make_directory_with_parents(destination, m_cancellable, &mkErr);
    if (mkErr && mkErr->code == G_IO_ERROR_NOT_MOUNTED) {
        qCInfo(lcTransfer) << "recurse mkdir hit NOT_MOUNTED; attempting remount for" << dstUri;
        g_error_free(mkErr);
        mkErr = nullptr;

        QString mountErr;
        int mountCode = 0;
        if (mountEnclosingVolumeSync(destination, &mountErr, &mountCode)) {
            qCInfo(lcTransfer) << "recurse remount ok; retrying mkdir on" << dstUri;
            g_file_make_directory_with_parents(destination, m_cancellable, &mkErr);
        } else {
            qCWarning(lcTransfer).nospace()
                << "recurse remount failed  dst=" << dstUri
                << " code=" << mountCode
                << " msg=" << mountErr;
            *error = humanizeMobileDeviceError(mountErr.isEmpty()
                ? QStringLiteral("Could not mount destination volume") : mountErr);
            return false;
        }
    }
    if (mkErr) {
        if (mkErr->code != G_IO_ERROR_EXISTS) {
            qCWarning(lcTransfer).nospace()
                << "recurse mkdir failed  dst=" << dstUri
                << " code=" << mkErr->code
                << " msg=" << mkErr->message;
            *error = humanizeMobileDeviceError(gErrorToUserMessage(mkErr));
            g_error_free(mkErr);
            return false;
        }
        g_error_free(mkErr);
    }

    m_completedBytes += 1; // directory entry

    // Enumerate children
    GError *enumErr = nullptr;
    GFileEnumerator *enumerator = g_file_enumerate_children(source,
        G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, m_cancellable, &enumErr);

    if (!enumerator) {
        *error = gErrorToUserMessage(enumErr);
        if (enumErr) g_error_free(enumErr);
        return false;
    }

    GFileInfo *childInfo;
    while ((childInfo = g_file_enumerator_next_file(enumerator, m_cancellable, nullptr)) != nullptr) {
        if (m_cancelled.load()) {
            g_object_unref(childInfo);
            break;
        }

        const char *name = g_file_info_get_name(childInfo);
        GFileType childType = g_file_info_get_file_type(childInfo);
        GFile *childSrc = g_file_get_child(source, name);
        GFile *childDst = g_file_get_child(destination, name);

        bool ok = true;

        if (childType == G_FILE_TYPE_DIRECTORY) {
            ok = copyRecursive(childSrc, childDst, flags, error);
        } else if (childType == G_FILE_TYPE_SYMBOLIC_LINK) {
            const char *slTarget = g_file_info_has_attribute(
                childInfo, G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET)
                ? g_file_info_get_symlink_target(childInfo)
                : nullptr;
            if (!slTarget) {
                *error = tr("Could not read symbolic link target");
                ok = false;
            }

            GError *slErr = nullptr;
            if (ok)
                g_file_make_symbolic_link(childDst, slTarget, m_cancellable, &slErr);
            if (slErr) {
                *error = gErrorToUserMessage(slErr);
                g_error_free(slErr);
                ok = false;
            }
            m_completedBytes += 1;
        } else {
            // Regular file
            GError *cpErr = nullptr;
            qCDebug(lcTransfer) << "recurse copy" << name;
            gboolean cpOk = g_file_copy(childSrc, childDst, flags,
                m_cancellable, progressCallback, this, &cpErr);
            if (!cpOk) {
                gchar *cdUri = g_file_get_uri(childDst);
                qCWarning(lcTransfer).nospace()
                    << "recurse copy failed  child=" << (cdUri ? cdUri : name)
                    << " code=" << (cpErr ? cpErr->code : -1)
                    << " msg=" << (cpErr ? cpErr->message : "(null)");
                if (cdUri) g_free(cdUri);
                *error = humanizeMobileDeviceError(gErrorToUserMessage(cpErr));
                if (cpErr) g_error_free(cpErr);
                ok = false;
            } else {
                m_completedBytes += m_currentItemBytes;
                m_currentItemBytes = 0;
            }
        }

        g_object_unref(childSrc);
        g_object_unref(childDst);
        g_object_unref(childInfo);

        if (!ok) {
            g_file_enumerator_close(enumerator, nullptr, nullptr);
            g_object_unref(enumerator);
            return false;
        }
    }

    g_file_enumerator_close(enumerator, nullptr, nullptr);
    g_object_unref(enumerator);

    // Copy directory metadata (permissions, timestamps) from source to destination
    GError *attrErr = nullptr;
    GFileInfo *srcInfo = g_file_query_info(source,
        "unix::mode,time::modified,time::modified-usec,time::access,time::access-usec",
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, m_cancellable, &attrErr);
    if (srcInfo) {
        GError *setErr = nullptr;
        g_file_set_attributes_from_info(destination, srcInfo, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
            m_cancellable, &setErr);
        if (setErr) g_error_free(setErr);
        g_object_unref(srcInfo);
    }
    if (attrErr) g_error_free(attrErr);

    return true;
}

bool GioTransferWorker::deleteRecursive(GFile *file, QString *error)
{
    // Try to enumerate children (if not a dir, this fails — do direct delete)
    GError *enumErr = nullptr;
    GFileEnumerator *enumerator = g_file_enumerate_children(file,
        G_FILE_ATTRIBUTE_STANDARD_NAME,
        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, m_cancellable, &enumErr);

    if (!enumerator) {
        // Not a directory, direct delete
        if (enumErr) g_error_free(enumErr);
        GError *delErr = nullptr;
        gboolean ok = g_file_delete(file, m_cancellable, &delErr);
        if (!ok) {
            *error = gErrorToUserMessage(delErr);
            if (delErr) g_error_free(delErr);
            return false;
        }
        return true;
    }

    // Delete children recursively
    GFileInfo *childInfo;
    while ((childInfo = g_file_enumerator_next_file(enumerator, m_cancellable, nullptr)) != nullptr) {
        const char *name = g_file_info_get_name(childInfo);
        GFile *child = g_file_get_child(file, name);
        g_object_unref(childInfo);

        if (!deleteRecursive(child, error)) {
            g_object_unref(child);
            g_file_enumerator_close(enumerator, nullptr, nullptr);
            g_object_unref(enumerator);
            return false;
        }
        g_object_unref(child);
    }

    g_file_enumerator_close(enumerator, nullptr, nullptr);
    g_object_unref(enumerator);

    // Delete the directory itself
    GError *delErr = nullptr;
    gboolean ok = g_file_delete(file, m_cancellable, &delErr);
    if (!ok) {
        *error = gErrorToUserMessage(delErr);
        if (delErr) g_error_free(delErr);
        return false;
    }
    return true;
}

void GioTransferWorker::progressCallback(goffset currentBytes, goffset totalBytes, gpointer userData)
{
    auto *self = static_cast<GioTransferWorker *>(userData);
    self->handleProgressCallback(currentBytes, totalBytes);
}

void GioTransferWorker::handleProgressCallback(goffset currentBytes, goffset totalBytes)
{
    Q_UNUSED(totalBytes)

    if (m_paused.load()) {
        QMutexLocker lock(&m_mutex);
        while (m_paused.load() && !m_cancelled.load()) {
            m_pauseCondition.wait(&m_mutex);
        }
    }

    if (m_cancelled.load()) {
        g_cancellable_cancel(m_cancellable);
        return;
    }

    // GIO callbacks can report non-monotonic values; clamp per-item progress
    if (currentBytes > m_currentItemBytes)
        m_currentItemBytes = currentBytes;
    emitProgress();
}

void GioTransferWorker::emitProgress()
{
    qint64 transferred = m_completedBytes + m_currentItemBytes;
    qint64 nowMs = m_elapsed.elapsed();

    // Throttle to every 200ms, except for final update
    if (transferred < m_totalBytes && (nowMs - m_lastEmitMs) < 200)
        return;
    m_lastEmitMs = nowMs;

    double progress = static_cast<double>(transferred) / m_totalBytes;
    if (progress > 1.0) progress = 1.0;
    if (progress < m_lastEmittedProgress) progress = m_lastEmittedProgress;
    m_lastEmittedProgress = progress;

    QString speed;
    QString eta;

    if (nowMs >= 2000 && transferred > 0) {
        double bytesPerSec = static_cast<double>(transferred) / (nowMs / 1000.0);

        // Speed formatting
        if (bytesPerSec >= 1e9)
            speed = QString::number(bytesPerSec / 1e9, 'f', 1) + " GB/s";
        else if (bytesPerSec >= 1e6)
            speed = QString::number(bytesPerSec / 1e6, 'f', 1) + " MB/s";
        else if (bytesPerSec >= 1e3)
            speed = QString::number(bytesPerSec / 1e3, 'f', 1) + " KB/s";
        else
            speed = QString::number(static_cast<int>(bytesPerSec)) + " B/s";

        // ETA
        qint64 remaining = m_totalBytes - transferred;
        if (remaining > 0 && bytesPerSec > 0) {
            int secs = static_cast<int>(remaining / bytesPerSec);
            if (secs >= 3600) {
                int h = secs / 3600;
                int m = (secs % 3600) / 60;
                int s = secs % 60;
                eta = QString("%1:%2:%3")
                    .arg(h)
                    .arg(m, 2, 10, QLatin1Char('0'))
                    .arg(s, 2, 10, QLatin1Char('0'));
            } else {
                int m = secs / 60;
                int s = secs % 60;
                eta = QString("%1:%2")
                    .arg(m)
                    .arg(s, 2, 10, QLatin1Char('0'));
            }
        }
    }

    emit progressUpdated(progress, speed, eta);
}

QString GioTransferWorker::gErrorToUserMessage(GError *error)
{
    if (!error)
        return {};

    if (error->domain == G_IO_ERROR) {
        switch (error->code) {
        case G_IO_ERROR_NO_SPACE:
            return QStringLiteral("Not enough disk space");
        case G_IO_ERROR_PERMISSION_DENIED:
            return QStringLiteral("Permission denied");
        case G_IO_ERROR_NOT_FOUND:
            return QStringLiteral("Source file not found");
        case G_IO_ERROR_CANCELLED:
            return {};
        default:
            break;
        }
    }

    return QString::fromUtf8(error->message);
}
