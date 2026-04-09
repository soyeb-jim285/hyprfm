#ifndef DRAGHELPER_H
#define DRAGHELPER_H

#include <QObject>
#include <QStringList>

class IconProvider;

class DragHelper : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY dragStateChanged)
    Q_PROPERTY(QStringList activePaths READ activePaths NOTIFY dragStateChanged)

public:
    explicit DragHelper(IconProvider *iconProvider, QObject *parent = nullptr);

    bool active() const;
    QStringList activePaths() const;

    Q_INVOKABLE void startDrag(const QStringList &filePaths,
                               const QString &iconName = QString(),
                               int fileCount = 1);

signals:
    void dragStateChanged();
    void dragStarted();
    void dragFinished();

private:
    IconProvider *m_iconProvider;
    QStringList m_activePaths;
    bool m_active = false;
};

#endif // DRAGHELPER_H
