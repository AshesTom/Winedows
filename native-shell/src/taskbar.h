// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's taskbar: the Start button, a button per window, the notification area
// and the clock, at the bottom of the primary monitor. Drawn by the visual
// style's own rules or, under Windows Classic, from the system colours. The
// rectangles are those of explorer.exe's at 96 DPI (docs/native-shell.md).
#pragma once

#include "arrange.h"
#include "tray.h"
#include "windowlist.h"

#include <QElapsedTimer>
#include <QFont>
#include <QImage>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QWidget>

class StartPanel;

class Taskbar : public QWidget
{
    Q_OBJECT
public:
    Taskbar(WindowList *windows, Tray *tray);
    void placeOnScreen();
    // A window's button on the screen; null when it has none shown
    QRect buttonRect(quint32 window) const;

public Q_SLOTS:
    // KEYBOARD: opened with the keys, the panel underlines its access keys, as XP's.
    void showStartMenu(bool keyboard = true);

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
        // Task: a button, a window's or a group's
        enum Kind { None, Start, Task, TrayIcon, Clock, Band } kind = None;
        int index = -1;
        bool operator==(const Hit &o) const { return kind == o.kind && index == o.index; }
    };
    // A taskbar button: one window, or a program's windows grouped
    struct Button
    {
        QList<int> tasks;   // indexes in the window list
        bool group = false;
        QRect rect;         // null when it does not fit
    };

    void themeChanged();
    void relayout();
    void layoutButtons(int bandStart, int bandEnd);
    Hit hitAt(QPoint at) const;
    void setHover(Hit hit);
    void drawStart(QPainter &painter);
    void drawButton(QPainter &painter, int index);
    void drawTray(QPainter &painter);
    void drawShadowedText(QPainter &painter, const QRect &rect, const QString &text);
    void showTaskMenu(int task, QPoint at);
    void showGroupList(int button);
    void showGroupMenu(int button, QPoint at);
    void showTaskbarMenu(QPoint at, bool tray);
    void tickClock();
    void endSession(const char *request);
    void turnOff();
    QString clockText() const;

    WindowList *m_windows;
    Tray *m_tray;

    QImage m_flag, m_smallFlag;
    QString m_startText, m_classicStartText;
    QFont m_startFont, m_taskFont, m_activeTaskFont, m_clockFont, m_groupCountFont;

    QRect m_startRect, m_trayRect, m_clockRect, m_bandRect;
    QList<Button> m_buttons;
    QSet<QString> m_grouped;   // the programs whose windows share a button
    QList<QRect> m_trayIconRects;
    arrange::Undo m_undo;
    int m_undoString = 0;      // explorer.exe's name for what Undo puts back
    Hit m_hover, m_pressed;
    QPointer<StartPanel> m_startPanel;
    QElapsedTimer m_startClosed;
    xcb_window_t m_activeAtPress = 0;
    QString m_clock;
    QTimer m_clockTimer;
};
