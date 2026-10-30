// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's taskbar: the Start button, a button per window, the notification area
// and the clock, at the bottom of the primary monitor. Drawn by the visual
// style's own rules or, under Windows Classic, from the system colours. The
// rectangles are those of explorer.exe's at 96 DPI (docs/native-shell.md).
#pragma once

#include "tray.h"
#include "windowlist.h"

#include <QElapsedTimer>
#include <QFont>
#include <QImage>
#include <QPointer>
#include <QTimer>
#include <QWidget>

class StartPanel;

class Taskbar : public QWidget
{
    Q_OBJECT
public:
    Taskbar(WindowList *windows, Tray *tray);
    void placeOnScreen();

public Q_SLOTS:
    void showStartMenu();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool event(QEvent *event) override;

private:
    struct Hit
    {
        enum Kind { None, Start, Task, TrayIcon, Clock } kind = None;
        int index = -1;
        bool operator==(const Hit &o) const { return kind == o.kind && index == o.index; }
    };

    void themeChanged();
    void relayout();
    Hit hitAt(QPoint at) const;
    void setHover(Hit hit);
    void drawStart(QPainter &painter);
    void drawTask(QPainter &painter, int index);
    void drawTray(QPainter &painter);
    void drawShadowedText(QPainter &painter, const QRect &rect, const QString &text);
    void showTaskMenu(int index, QPoint at);
    void tickClock();
    void endSession(const char *request);
    void turnOff();
    QString clockText() const;

    WindowList *m_windows;
    Tray *m_tray;

    QImage m_flag, m_smallFlag;
    QString m_startText, m_classicStartText;
    QFont m_startFont, m_taskFont, m_activeTaskFont, m_clockFont;

    QRect m_startRect, m_trayRect, m_clockRect;
    QList<QRect> m_taskRects, m_trayIconRects;
    Hit m_hover, m_pressed;
    QPointer<StartPanel> m_startPanel;
    QElapsedTimer m_startClosed;
    xcb_window_t m_activeAtPress = 0;
    QString m_clock;
    QTimer m_clockTimer;
};
