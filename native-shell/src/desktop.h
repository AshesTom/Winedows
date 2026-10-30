// SPDX-License-Identifier: LGPL-2.1-or-later
// The desktop window: XP's background colour and wallpaper on each monitor, and its icons.
#pragma once

#include <QColor>
#include <QImage>
#include <QWidget>

class DesktopIcons;

class Desktop : public QWidget
{
    Q_OBJECT
public:
    Desktop();
    // Read again what the session's look says (Display Properties).
    void reload();
    // The desktop's rectangle again, after a monitor changed.
    void placeOnScreens();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QColor m_background;
    QImage m_wallpaper;
    QString m_wallpaperPath;
    int m_style = 2;
    bool m_tile = false;
    DesktopIcons *m_icons = nullptr;
};
