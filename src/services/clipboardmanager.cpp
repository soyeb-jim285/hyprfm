#include "services/clipboardmanager.h"

#include <QClipboard>
#include <QGuiApplication>

ClipboardManager::ClipboardManager(QObject *parent)
    : QObject(parent)
{
}

bool ClipboardManager::hasContent() const { return !m_paths.isEmpty(); }
bool ClipboardManager::isCut() const { return m_isCut; }
QStringList ClipboardManager::paths() const { return m_paths; }

void ClipboardManager::copy(const QStringList &paths)
{
    m_paths = paths;
    m_isCut = false;
    emit changed();
}

void ClipboardManager::cut(const QStringList &paths)
{
    m_paths = paths;
    m_isCut = true;
    emit changed();
}

void ClipboardManager::clear()
{
    m_paths.clear();
    m_isCut = false;
    emit changed();
}

bool ClipboardManager::contains(const QString &path) const
{
    return m_paths.contains(path);
}

QStringList ClipboardManager::take()
{
    QStringList result = m_paths;
    if (m_isCut) {
        m_paths.clear();
        m_isCut = false;
        emit changed();
    }
    return result;
}

void ClipboardManager::copyText(const QString &text)
{
    if (QClipboard *cb = QGuiApplication::clipboard())
        cb->setText(text);
}
