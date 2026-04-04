#include "services/giotransferworker.h"

GioTransferWorker::GioTransferWorker(QObject *parent)
    : QObject(parent)
{
}

void GioTransferWorker::execute(const QList<TransferItem> &items, bool moveOperation)
{
    Q_UNUSED(items)
    Q_UNUSED(moveOperation)
    emit finished(false, "Not yet implemented");
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
    Q_UNUSED(items)
    return 0;
}

qint64 GioTransferWorker::scanPathBytes(GFile *file)
{
    Q_UNUSED(file)
    return 0;
}

bool GioTransferWorker::copyRecursive(GFile *source, GFile *destination, GFileCopyFlags flags, QString *error)
{
    Q_UNUSED(source)
    Q_UNUSED(destination)
    Q_UNUSED(flags)
    Q_UNUSED(error)
    return false;
}

bool GioTransferWorker::deleteRecursive(GFile *file, QString *error)
{
    Q_UNUSED(file)
    Q_UNUSED(error)
    return false;
}

void GioTransferWorker::progressCallback(goffset currentBytes, goffset totalBytes, gpointer userData)
{
    auto *self = static_cast<GioTransferWorker *>(userData);
    self->handleProgressCallback(currentBytes, totalBytes);
}

void GioTransferWorker::handleProgressCallback(goffset currentBytes, goffset totalBytes)
{
    Q_UNUSED(currentBytes)
    Q_UNUSED(totalBytes)
}

void GioTransferWorker::emitProgress()
{
}

QString GioTransferWorker::gErrorToUserMessage(GError *error)
{
    if (!error)
        return {};
    return QString::fromUtf8(error->message);
}
