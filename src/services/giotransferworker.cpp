#include "services/giotransferworker.h"

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

        emit itemStarted(item.sourcePath, item.targetPath);

        GFile *targetFile = g_file_new_for_path(item.targetPath.toUtf8().constData());
        GFile *targetParent = g_file_get_parent(targetFile);
        if (targetParent) {
            GError *dirErr = nullptr;
            g_file_make_directory_with_parents(targetParent, nullptr, &dirErr);
            if (dirErr) {
                // Ignore "already exists"
                if (dirErr->code != G_IO_ERROR_EXISTS)
                    qWarning("Failed to create parent dir: %s", dirErr->message);
                g_error_free(dirErr);
            }
            g_object_unref(targetParent);
        }

        // Handle backup if overwrite + backupPath set
        if (item.overwrite && !item.backupPath.isEmpty()) {
            GFile *existingTarget = g_file_new_for_path(item.targetPath.toUtf8().constData());
            GFile *backupFile = g_file_new_for_path(item.backupPath.toUtf8().constData());
            GFile *backupParent = g_file_get_parent(backupFile);
            if (backupParent) {
                GError *bpErr = nullptr;
                g_file_make_directory_with_parents(backupParent, nullptr, &bpErr);
                if (bpErr)
                    g_error_free(bpErr);
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
        GFile *sourceFile = g_file_new_for_path(item.sourcePath.toUtf8().constData());
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
            const char *slTarget = g_file_info_get_symlink_target(slInfo);
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
                g_object_unref(sourceFile);
                g_object_unref(targetFile);
                break;
            }
        } else {
            // Regular file
            GError *cpErr = nullptr;
            gboolean ok = g_file_copy(sourceFile, targetFile, flags,
                m_cancellable, progressCallback, this, &cpErr);
            if (!ok) {
                success = false;
                errorMsg = gErrorToUserMessage(cpErr);
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
        GFile *file = g_file_new_for_path(item.sourcePath.toUtf8().constData());
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
    // Create target directory
    GError *mkErr = nullptr;
    g_file_make_directory_with_parents(destination, m_cancellable, &mkErr);
    if (mkErr) {
        if (mkErr->code != G_IO_ERROR_EXISTS) {
            *error = gErrorToUserMessage(mkErr);
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
            const char *slTarget = g_file_info_get_symlink_target(childInfo);
            GError *slErr = nullptr;
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
            gboolean cpOk = g_file_copy(childSrc, childDst, flags,
                m_cancellable, progressCallback, this, &cpErr);
            if (!cpOk) {
                *error = gErrorToUserMessage(cpErr);
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
