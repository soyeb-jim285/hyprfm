#include "draghelper.h"
#include "providers/iconprovider.h"

#include <QDrag>
#include <QGuiApplication>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QTimer>
#include <QUrl>

DragHelper::DragHelper(IconProvider *iconProvider, QObject *parent)
    : QObject(parent)
    , m_iconProvider(iconProvider)
{
}

void DragHelper::startDrag(const QStringList &filePaths,
                           const QString &iconName,
                           int fileCount)
{
    if (filePaths.isEmpty())
        return;

    QStringList paths = filePaths;
    QString icon = iconName;
    int count = fileCount;

    // Defer to next event loop iteration to avoid nested event loop
    // issues when called from QML mouse handlers
    QTimer::singleShot(0, this, [this, paths, icon, count]() {
        emit dragStarted();

        auto *drag = new QDrag(this);
        auto *mimeData = new QMimeData;

        QList<QUrl> urls;
        urls.reserve(paths.size());
        for (const QString &path : paths) {
            const QUrl url(path);
            urls.append(url.isValid() && !url.scheme().isEmpty() ? url : QUrl::fromLocalFile(path));
        }
        mimeData->setUrls(urls);
        drag->setMimeData(mimeData);

        // Build drag pixmap using the same IconProvider as QML
        const int iconSize = 48;
        const int badgeH = 18;
        int totalH = count > 1 ? iconSize + badgeH / 2 : iconSize;
        int totalW = iconSize + (count > 1 ? 12 : 0);

        QPixmap pixmap(totalW, totalH);
        pixmap.fill(Qt::transparent);

        // Render via IconProvider (uses rsvg-convert, same as QML)
        QSize actualSize;
        QImage iconImg;
        if (m_iconProvider && !icon.isEmpty())
            iconImg = m_iconProvider->requestImage(icon, &actualSize, QSize(iconSize, iconSize));

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);

        if (!iconImg.isNull()) {
            int iconY = count > 1 ? badgeH / 2 : 0;
            painter.drawImage(0, iconY, iconImg);
        }

        // Count badge for multiple files
        if (count > 1) {
            QString label = QString::number(count);
            QFont font = painter.font();
            font.setPixelSize(11);
            font.setBold(true);
            painter.setFont(font);

            QFontMetrics fm(font);
            int textWidth = fm.horizontalAdvance(label);
            int badgeW = qMax(textWidth + 8, 18);
            QRect badge(totalW - badgeW, 0, badgeW, badgeH);

            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(220, 50, 50));
            painter.drawRoundedRect(badge, badgeH / 2, badgeH / 2);

            painter.setPen(Qt::white);
            painter.drawText(badge, Qt::AlignCenter, label);
        }

        painter.end();

        drag->setPixmap(pixmap);
        drag->setHotSpot(QPoint(iconSize / 2, count > 1 ? badgeH / 2 + iconSize / 2 : iconSize / 2));

        drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::MoveAction);

        emit dragFinished();
    });
}
