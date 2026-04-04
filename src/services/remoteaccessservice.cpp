#include "services/remoteaccessservice.h"

#include <QDir>
#include <QUrl>

RemoteAccessService::RemoteAccessService(QObject *parent)
    : QObject(parent)
{
}

bool RemoteAccessService::busy() const
{
    return m_process != nullptr;
}

QString RemoteAccessService::buildUri(const QString &protocol, const QString &host,
                                      const QString &remotePath, const QString &user,
                                      int port, const QString &share) const
{
    if (host.trimmed().isEmpty())
        return {};

    QUrl url;
    url.setScheme(protocol.trimmed().toLower());
    url.setHost(host.trimmed());
    if (!user.trimmed().isEmpty())
        url.setUserName(user.trimmed());
    if (port > 0)
        url.setPort(port);

    QString path = remotePath.trimmed();
    if (url.scheme() == QStringLiteral("smb")) {
        const QString trimmedShare = share.trimmed();
        if (!trimmedShare.isEmpty())
            path = trimmedShare + (path.isEmpty() ? QString() : QStringLiteral("/") + path);
    }

    if (path.isEmpty())
        path = QStringLiteral("/");
    if (!path.startsWith('/'))
        path.prepend('/');

    url.setPath(QDir::cleanPath(path));
    return url.toString(QUrl::FullyEncoded);
}

void RemoteAccessService::connectToLocation(const QString &uri)
{
    const QString trimmedUri = uri.trimmed();
    if (trimmedUri.isEmpty()) {
        emit connectionFinished(false, QString(), QStringLiteral("Enter a remote location"));
        return;
    }

    if (m_process) {
        m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }

    m_pendingUri = trimmedUri;
    m_process = new QProcess(this);
    emit busyChanged();

    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus) {
        const QString error = exitCode == 0
            ? QString()
            : QString::fromUtf8(m_process->readAllStandardError()).trimmed();
        emit connectionFinished(exitCode == 0, m_pendingUri, error);
        m_process->deleteLater();
        m_process = nullptr;
        m_pendingUri.clear();
        emit busyChanged();
    });

    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        const QString error = m_process ? m_process->errorString() : QStringLiteral("Failed to start gio mount");
        emit connectionFinished(false, m_pendingUri, error);
        if (m_process) {
            m_process->deleteLater();
            m_process = nullptr;
        }
        m_pendingUri.clear();
        emit busyChanged();
    });

    m_process->start(QStringLiteral("gio"), {QStringLiteral("mount"), trimmedUri});
}
