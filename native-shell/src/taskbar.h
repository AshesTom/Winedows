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

#include <functional>
#include "balloon.h"
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
    // Windows Media Player's band window came or went (_WIN4WINE_WMP_DESKBAND), or asks for the band's menu
    void mediaBandChanged();
    void showMediaBandMenu();
    void mediaBandGone();
    quint32 mediaWindow() const { return m_mediaWindow; }
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
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;

private:
    struct Hit
    {
        // Task: a button, a window's or a group's
        enum Kind { None, Start, Task, TrayIcon, Clock, Band, Quick, QuickBand, Desktop, Custom, Link, Chevron, ScrollUp, ScrollDown,
                    ScrollThumb, ScrollTrackUp, ScrollTrackDown } kind = None;
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
    void loadDesktopBand();
    void loadCustomToolbar();
    void loadLinksBand();
    void runQuickItem(int index);
    void runDesktopItem(int index);
    void runCustomItem(int index);
    void runLinkItem(int index);
    void toggleDesktop();
    void themeChanged();
    void relayout();
    void layoutButtons(int bandStart, int bandEnd);
    void scrollStep(int delta);
    Hit hitAt(QPoint at) const;
    void setHover(Hit hit);
    void drawStart(QPainter &painter);
    void drawButton(QPainter &painter, int index);
    void drawScrollbar(QPainter &painter);
    void drawTray(QPainter &painter);
    void drawShadowedText(QPainter &painter, const QRect &rect, const QString &text);
    void showTaskMenu(int task, QPoint at);
    void showGroupList(int button);
    void showGroupMenu(int button, QPoint at);
    // BAND: on Quick Launch's gripper or title, its band's items come first
    void showTaskbarMenu(QPoint at, bool tray, bool band = false);
    void showQuickItemMenu(int index, QPoint at);
    // The Start button's: its folder's verbs, Properties and All Users' (explorer.exe's sub_1033b53)
    void showStartButtonMenu(QPoint at);
    void addBandItems(QMenu *menu, const w4w::PeResources &browselc);
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    // Dragging over the taskband (its DragOver, sub_102f663): a task button held 250 ms brings its window
    // forward, the band's empty part held 500 ms shows the desktop; again only 10 s later. A drop on a
    // button is refused with string 711.
    void dragOverTasks(Hit hit);
    Hit m_dragHit{Hit::None, -2};
    QTimer m_dragTimer;
    void tickClock();
    // At logon (sub_100eb38): the Start panel opens by itself the first time; with the classic menu, balloon
    // 800/801 points at the Start button for 10 s (sub_1030259) until the button has been used
    void startButtonBalloon();
    // The post-boot reminders, one after the other: NEXT runs when this one has gone or was not needed
    void lowResolutionBalloon(std::function<void()> next);
    void tourBalloon();
    // shell32's way of showing one: its own notification icon and that icon's balloon. CLICKED runs when
    // either is clicked; DONE once the icon has gone.
    // TOOLTIP is the icon's.
    void reminder(BalloonTip tip, const QImage &image, int showTime, std::function<void()> clicked, std::function<void()> done,
                  const QString &tooltip = QString());
    // fldrclnr.dll's "There are unused icons on your desktop", when the desktop cleanup is due
    void desktopCleanupBalloon();
    void inactiveIconsBalloon();
    QPointer<QWidget> m_startBalloon;
    // XP's hotkeys with the Windows key (explorer.exe's IDs 500 to 511), by virtual key and RegisterHotKey's modifiers
    void hotkey(int key, int modifiers);
    void minimizeAll();
    // Win+Tab and Win+B: the keyboard on the task buttons or on the notification area's icons, the item
    // that has it hot as on XP's toolbars; arrows move it, Enter chooses, the menu key opens its menu.
    void keyboardStep(Hit::Kind kind, int step);
    void leaveKeyboard();
    void chooseItem(Hit hit, bool context);
    // Auto-hide, as explorer.exe's tray does it: a 500 ms check hides the bar once the pointer is off it
    // (timer 2), leaving SM_CYFRAME / 2 rows on the screen; a move on those rows wider than the
    // double-click rectangle brings it back 50 ms later (timer 3). It slides for 400 ms going and
    // 200 ms coming (the tray's 0x4f0 and 0x4f4), a frame each 20 ms.
    QRect shownRect() const;
    int baseHeight() const;
    int sizingHeight() const;
    void setOnTop();
    int barHeight() const { return height() - m_top; }
    QRect hiddenRect() const;
    void checkAutoHide();
    void setHidden(bool hidden, bool animate = true);
    void endSession(const char *request);
    void turnOff();
    // Start > Log Off: Switch User or Log Off
    void logOff();
    void switchUser();
    QString clockText() const;

    WindowList *m_windows;
    Tray *m_tray;

    QImage m_flag, m_smallFlag;
    QString m_startText, m_classicStartText;
    QFont m_startFont, m_taskFont, m_activeTaskFont, m_clockFont, m_groupCountFont;

    QRect m_startRect, m_trayRect, m_clockRect, m_bandRect;
    // The taskband's scrollbar when the buttons do not fit at their least width (explorer.exe's
    // sub_102de3e: a vertical scrollbar at the band's right, one toolbar row a page)
    QRect m_scrollRect, m_scrollUp, m_scrollDown;
    QRect scrollThumbRect() const;
    bool m_scrollDragging = false;
    int m_scrollDragFromY = 0, m_scrollDragPage = 0;
    int m_scrollPage = 0, m_scrollPages = 1, m_scrollColumns = 1;
    QTimer m_scrollRepeat;
    QTimer m_flashTimer;   // the buttons' flashing, lit while m_flashOn
    bool m_flashOn = true;
    int m_flashToggles = 0;
    QList<Button> m_buttons;
    TaskbarSettings m_settings = TaskbarSettings::load();
    QFileSystemWatcher m_settingsWatcher;
    QSet<QString> m_grouped;   // the programs whose windows share a button
    QList<QRect> m_trayIconRects;
    QList<QuickItem> m_quickItems;
    QList<QRect> m_grippers;
    QRect m_quickBand, m_quickTitle;
    QList<QuickItem> m_desktopItems;
    QRect m_desktopBand;
    QFileSystemWatcher m_desktopWatcher;
    QList<QuickItem> m_customItems;
    QRect m_customBand;
    QFileSystemWatcher m_customWatcher;
    QList<QuickItem> m_linksItems;
    QRect m_linksBand;
    QFileSystemWatcher m_linksWatcher;
    // Windows Media Player's toolbar (wmpband.dll): while the player is minimized, its window for
    // wmploc.dll's taskbar.wsz is a child of the bar, left of the notification area
    QPointer<QWidget> m_mediaContainer;
    quint32 m_mediaWindow = 0;
    QRect m_mediaBand;
    void publishMediaBandSite();
    void releaseMediaWindow();
    // Hide inactive icons: when each icon last changed or was clicked, the ones hidden, the chevron
    // (TrayNotifyHoriz::Button) and whether it has them all shown
    QHash<quint32, qint64> m_trayActive;
    QHash<quint32, QPair<QString, qint64>> m_traySeen;   // tooltip and image key, to see a change
    QSet<quint32> m_trayHidden;
    QRect m_chevronRect;
    bool m_trayOpen = false;
    QTimer m_trayCollapseTimer;
    QTimer m_demoteTimer;
    void trayChanged();
    void updateTrayHiding();
    QString chevronTip() const;
    static QString trayName(const TrayIcon &icon);
    int m_top = 0;   // the sizing bar's rows above the bar, unlocked
    bool m_movingBar = false;
    int m_barDragFromY = 0, m_barRowsStart = 1;
    QFileSystemWatcher m_quickWatcher;
    arrange::Undo m_undo;
    int m_undoString = 0;      // explorer.exe's name for what Undo puts back
    Hit m_hover, m_pressed;
    bool m_keyboard = false;
    // HSHELL_FLASH (sub_1004730): a window that starts flashing while another has the foreground brings the
    // auto-hidden bar back (flag 0x10, sub_101f7be), and the bar stays while it flashes (message 0x439)
    void attentionChanged();
    QSet<xcb_window_t> m_attention, m_unhidFor;
    xcb_window_t m_activeAtKeyboard = 0;
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
