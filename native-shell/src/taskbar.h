// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's taskbar: the Start button, a button per window, the notification area
// and the clock, at the bottom of the primary monitor. Drawn by the visual
// style's own rules or, under Windows Classic, from the system colours. The
// rectangles are those of explorer.exe's at 96 DPI (docs/native-shell.md).
#pragma once

#include "taskbarproperties.h"
#include "msstyles.h"

#include <QFileSystemWatcher>

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

class QMenu;
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
        enum Kind { None, Start, Task, TrayIcon, Clock, Band, Quick, QuickBand, Chevron } kind = None;
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

    // Quick Launch: the shortcuts of its folder, a toolbar band between the Start button and the
    // tasks (browseui's ISF band, themed TaskBar::Toolbar)
    struct QuickItem
    {
        QString path, name;
        QImage icon;
        QRect rect;
    };
    void loadQuickLaunch();
    void runQuickItem(int index);
    void toggleDesktop();
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
    // BAND: on Quick Launch's gripper or title, its band's items come first
    void showTaskbarMenu(QPoint at, bool tray, bool band = false);
    void showQuickItemMenu(int index, QPoint at);
    void addBandItems(QMenu *menu, const w4w::PeResources &browselc);
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void tickClock();
    // Auto-hide, as explorer.exe's tray does it: a 500 ms check hides the bar once the pointer is off it
    // (timer 2), leaving SM_CYFRAME / 2 rows on the screen; a move on those rows wider than the
    // double-click rectangle brings it back 50 ms later (timer 3). It slides for 400 ms going and
    // 200 ms coming (the tray's 0x4f0 and 0x4f4), a frame each 20 ms.
    QRect shownRect() const;
    int sizingHeight() const;
    void setOnTop();
    int barHeight() const { return height() - m_top; }
    QRect hiddenRect() const;
    void checkAutoHide();
    void setHidden(bool hidden, bool animate = true);
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
    TaskbarSettings m_settings = TaskbarSettings::load();
    QFileSystemWatcher m_settingsWatcher;
    QSet<QString> m_grouped;   // the programs whose windows share a button
    QList<QRect> m_trayIconRects;
    QList<QuickItem> m_quickItems;
    QList<QRect> m_grippers;
    QRect m_quickBand, m_quickTitle;
    // Hide inactive icons: when each icon last changed or was clicked, the ones hidden, the chevron
    // (TrayNotifyHoriz::Button) and whether it has them all shown
    QHash<quint32, qint64> m_trayActive;
    QHash<quint32, QPair<QString, qint64>> m_traySeen;   // tooltip and image key, to see a change
    QSet<quint32> m_trayHidden;
    QRect m_chevronRect;
    bool m_trayOpen = false;
    QTimer m_demoteTimer;
    void trayChanged();
    void updateTrayHiding();
    QString chevronTip() const;
    static QString trayName(const TrayIcon &icon);
    int m_top = 0;   // the sizing bar's rows above the bar, unlocked
    QFileSystemWatcher m_quickWatcher;
    arrange::Undo m_undo;
    int m_undoString = 0;      // explorer.exe's name for what Undo puts back
    Hit m_hover, m_pressed;
    QPointer<StartPanel> m_startPanel;
    QElapsedTimer m_startClosed;
    QTimer m_autoHideTimer, m_unhideTimer, m_slideTimer;
    bool m_hidden = false;
    QPoint m_hiddenMouse;
    QRect m_slideFrom, m_slideTo;
    QElapsedTimer m_slideClock;
    int m_slideDuration = 0;
    xcb_window_t m_activeAtPress = 0;
    QString m_clock;
    QTimer m_clockTimer;
};
