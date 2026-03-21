#pragma once

#include <QQuickImageProvider>
#include <QIcon>
#include <QPainter>

class IconProvider : public QQuickImageProvider
{
public:
    IconProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        int sz = (requestedSize.width() > 0) ? requestedSize.width() : 48;
        QSize iconSize(sz, sz);

        QIcon icon = QIcon::fromTheme(id);
        if (icon.isNull()) {
            // Fallback chain
            icon = QIcon::fromTheme("text-x-generic");
            if (icon.isNull())
                icon = QIcon::fromTheme("application-x-generic");
        }

        QImage img(iconSize, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);

        if (!icon.isNull()) {
            QPainter painter(&img);
            icon.paint(&painter, 0, 0, sz, sz);
            painter.end();
        }

        if (size)
            *size = iconSize;
        return img;
    }
};
