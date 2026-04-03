#pragma once

#include <QObject>
#include <QStringList>

class ClipboardManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasContent READ hasContent NOTIFY changed)
    Q_PROPERTY(bool isCut READ isCut NOTIFY changed)
    Q_PROPERTY(QStringList paths READ paths NOTIFY changed)

public:
    explicit ClipboardManager(QObject *parent = nullptr);

    bool hasContent() const;
    bool isCut() const;
    QStringList paths() const;

    Q_INVOKABLE void copy(const QStringList &paths);
    Q_INVOKABLE void cut(const QStringList &paths);
    Q_INVOKABLE void clear();
    Q_INVOKABLE bool contains(const QString &path) const;
    Q_INVOKABLE QStringList take();

signals:
    void changed();

private:
    QStringList m_paths;
    bool m_isCut = false;
};
