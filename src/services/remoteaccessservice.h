#pragma once

#include <QObject>
#include <QProcess>

class RemoteAccessService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit RemoteAccessService(QObject *parent = nullptr);

    bool busy() const;

    Q_INVOKABLE QString buildUri(const QString &protocol, const QString &host, const QString &remotePath,
                                 const QString &user = QString(), int port = -1,
                                 const QString &share = QString()) const;
    Q_INVOKABLE void connectToLocation(const QString &uri);

signals:
    void busyChanged();
    void connectionFinished(bool success, const QString &uri, const QString &error);

private:
    QProcess *m_process = nullptr;
    QString m_pendingUri;
};
