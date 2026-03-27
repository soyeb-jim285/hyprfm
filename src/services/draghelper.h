#ifndef DRAGHELPER_H
#define DRAGHELPER_H

#include <QObject>
#include <QStringList>

class IconProvider;

class DragHelper : public QObject
{
    Q_OBJECT

public:
    explicit DragHelper(IconProvider *iconProvider, QObject *parent = nullptr);

    Q_INVOKABLE void startDrag(const QStringList &filePaths,
                               const QString &iconName = QString(),
                               int fileCount = 1);

signals:
    void dragStarted();
    void dragFinished();

private:
    IconProvider *m_iconProvider;
};

#endif // DRAGHELPER_H
