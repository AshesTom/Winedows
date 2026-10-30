// SPDX-License-Identifier: LGPL-2.1-or-later
#include "desktop.h"

#include "desktopicons.h"
#include "session.h"
#include "theme.h"
#include "x11.h"
#include "xpfiles.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QScreen>

Desktop::Desktop()
    : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_X11NetWmWindowTypeDesktop);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);
    setWindowTitle(QStringLiteral("Program Manager"));
    placeOnScreens();
    reload();
    // bridge-wm keeps XP's desktop at the bottom by this class.
    x11::setString(winId(), "_WIN4WINE_CLASS", "Progman");
    m_icons = new DesktopIcons(this);
}

void Desktop::placeOnScreens()
{
    setGeometry(QGuiApplication::primaryScreen()->virtualGeometry());
    if (m_icons)
        m_icons->reload();
}

void Desktop::reload()
{
    const w4w::Look &look = w4w::Session::instance()->look();
    m_background = ShellTheme::instance()->system(QStringLiteral("background"), QColor(0, 78, 152));
    // XP's own default until something chooses: Bliss, stretched.
    QString path = xpfiles::find(QStringLiteral("bliss.jpg"));
    m_style = 2;
    m_tile = false;
    if (look.haveWallpaper) {
        path = look.wallpaper;
        m_style = look.wallpaperStyle;
        m_tile = look.tileWallpaper;
    }
    if (path != m_wallpaperPath || m_wallpaper.isNull()) {
        m_wallpaperPath = path;
        m_wallpaper = path.isEmpty() ? QImage() : QImage(path);
        if (!m_wallpaper.isNull())
            m_wallpaper = m_wallpaper.convertToFormat(QImage::Format_RGB32);
    }
    if (m_icons)
        m_icons->reload();
    update();
}

void Desktop::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), m_background);
    if (!m_wallpaper.isNull()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        // Each monitor shows the whole picture, as XP's desktop does.
        for (const QScreen *screen : QGuiApplication::screens()) {
            const QRect target = screen->geometry().translated(-geometry().topLeft());
            if (!target.intersects(event->rect()))
                continue;
            if (m_tile)
                painter.drawTiledPixmap(target, QPixmap::fromImage(m_wallpaper));
            else if (m_style == 2)
                painter.drawImage(target, m_wallpaper);
            else {
                QRect picture = m_wallpaper.rect();
                picture.moveCenter(target.center());
                painter.setClipRect(target);
                painter.drawImage(picture.topLeft(), m_wallpaper);
                painter.setClipping(false);
            }
        }
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    m_icons->paint(painter, event->rect());
}

void Desktop::mousePressEvent(QMouseEvent *event)
{
    m_icons->press(event->position().toPoint(), event->button(), event->globalPosition().toPoint());
}

void Desktop::mouseMoveEvent(QMouseEvent *event)
{
    m_icons->move(event->position().toPoint());
}

void Desktop::mouseReleaseEvent(QMouseEvent *)
{
    m_icons->release();
}

void Desktop::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_icons->doubleClick(event->position().toPoint());
}

void Desktop::keyPressEvent(QKeyEvent *event)
{
    if (!m_icons->key(event->key()))
        QWidget::keyPressEvent(event);
}
