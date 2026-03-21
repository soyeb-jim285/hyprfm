#pragma once

#include <QQuickImageProvider>
#include <QIcon>

class IconProvider : public QQuickImageProvider
{
public:
    IconProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}

    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        int sz = requestedSize.width() > 0 ? requestedSize.width() : 32;
        QIcon icon = QIcon::fromTheme(id);
        if (icon.isNull())
            icon = QIcon::fromTheme("text-x-generic");
        QPixmap pix = icon.pixmap(sz, sz);
        if (size)
            *size = pix.size();
        return pix;
    }
};
