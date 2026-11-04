// SPDX-License-Identifier: LGPL-2.1-or-later
#include "taskbar.h"
#include "displayproperties.h"
#include "taskbarproperties.h"

#include <QDir>

#include <QTimer>

#include "applets.h"
#include "balloon.h"
#include "filebrowser.h"
#include "fileicons.h"
#include "programs.h"
#include "sessiondialog.h"
#include "sounds.h"
#include "startmenu.h"
#include "theme.h"
#include "x11.h"
#include "xpfiles.h"
#include "fileops.h"
#include "xpprogram.h"

#include <QActionGroup>
#include <QApplication>
#include <QUrl>
#include <QDropEvent>
#include <QMimeData>
#include <QFileInfo>
#include <QFileDialog>
#include <QCoreApplication>
#include <QSettings>
#include <QCursor>
#include <QDateTime>
#include <QFile>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QStandardPaths>
#include <QToolTip>
#include <QWheelEvent>

#include <functional>
#include <iterator>

using w4w::Part;

namespace {

// explorer.exe's string and bitmap resources
const int STRING_START = 578, STRING_CLASSIC_START = 595, BITMAP_FLAG = 143, BITMAP_SMALL_FLAG = 176;

// At 96 DPI. The Luna taskbar's own height, and where its children sit in it.
const int BAR_HEIGHT = 30, CLASSIC_BAR_HEIGHT = 28;
const int BAND_GAP = 5;             // from the Start button to the first task
const int START_TEXT_GAP = 2, START_TEXT_X = 1;     // the room for the gap in its width; where the word starts
// The art of these parts has its edges inside: the Start button is 32 tall, its
// last rows (baked taskbar colours) under the screen's edge, and the task
// buttons' transparent rows leave 25 of the bar's 30 showing, from y = 3.
const int START_HEIGHT = 32;
// explorer.exe's taskband: buttons SM_CXMINIMIZED wide at most and (SM_CYSIZE + 2 SM_CXEDGE) x 1.8
// at least; under that and 8 average characters more, it groups a program's buttons. The band
// ends 3 pixels before the notification area's rectangle (measured with 5, 7, 11 and 12 buttons:
// 743 wide from 104 to the area at 850, or 742 and 4).
const int TASK_MAX_WIDTH = 160, EDGE = 2, GROUP_CHARS = 8, BAND_END = 3;
const double TASK_MIN_FACTOR = 1.8;
const int CLASSIC_SPACING = 3;      // TB_SETMETRICS' cxButtonSpacing without a visual style; 0 with one
const int TASK_ICON_X = 10, TASK_ICON_Y = 7, TASK_TEXT_GAP = 3, TASK_TEXT_END = 7;
const int GROUP_ARROW_WIDTH = 13, GROUP_ARROW_GAP = 4;   // not measured on XP (it never groups under Wine)
// explorer.exe's menus: the taskbar's (205) and a group's (210), their commands, and Undo's words
const int MENU_TASKBAR = 205, MENU_GROUP = 210;
// browselc.dll's band site menu (Toolbars) and its Quick Launch
const int MENU_BANDS = 268, CMD_BAND_QUICK_LAUNCH = 4, CMD_BAND_DESKTOP = 3, CMD_BAND_NEW_TOOLBAR = 1;
enum {
    CMD_CASCADE = 403, CMD_TILE_HORIZONTALLY = 404, CMD_TILE_VERTICALLY = 405, CMD_SHOW_DESKTOP = 407, CMD_ADJUST_TIME = 408,
    CMD_PROPERTIES = 413, CMD_UNDO = 416, CMD_TASK_MANAGER = 420, CMD_CUSTOMIZE_NOTIFICATIONS = 421, CMD_LOCK = 424,
    CMD_MINIMIZE_GROUP = 311, CMD_CLOSE_GROUP = 48913,
};
// DragOver's delays (sub_102f663): 250 ms on a button, 500 on the band's empty part, then 10 s more
const int DRAG_BUTTON_MS = 250, DRAG_BAND_MS = 500, DRAG_AGAIN_MS = 10000;
const int STRING_TASKBAR = 518, STRING_NO_DROP = 711;
const int TRAY_COLLAPSE_MS = 5000;
// (once the desktop is up: XP posts it from the tray's creation)
const int START_BALLOON_DELAY_MS = 1500;
const int STRING_START_TIP = 533, STRING_START_BALLOON_TITLE = 800, STRING_START_BALLOON = 801, START_BALLOON_MS = 10000;
// XP's first-logon balloons are post-boot reminders (sub_101389f, sub_101343b): explorer.exe files them under
// PostBootReminders and shell32's PostBootReminder shows them one after the other, in the keys' order
// (Microsoft.FixScreenResolution, then Microsoft.OfferTour), once the tray starts its shell service objects.
// The tour's ShowTime is 60000; the low-resolution one opens desk.cpl. When those objects start is not read
// here: REMINDERS_DELAY_MS stands for it.
const int REMINDERS_DELAY_MS = 6000;
const int STRING_LOW_RESOLUTION_TITLE = 811, STRING_LOW_RESOLUTION = 812;
const int STRING_TOUR_BALLOON_TITLE = 821, STRING_TOUR_BALLOON = 822, TOUR_SHOW_MS = 60000;
// The chevron's balloon (sub_101d6b6): when the chevron is first shown, 30 s
const int STRING_HIDDEN_BALLOON_TITLE = 590, STRING_HIDDEN_BALLOON = 591, HIDDEN_BALLOON_MS = 30000;
const int STRING_UNDO = 534, STRING_CASCADE = 535, STRING_TILE = 536, STRING_MINIMIZE_ALL = 537, STRING_SHOW_OPEN_WINDOWS = 850;
// explorer.exe's hotkeys: the table at 0x10114c4 (virtual key, modifiers with MOD_WIN), registered as
// IDs 500 to 511 by sub_101146e; WM_HOTKEY goes to sub_1021388's jump table.
enum Hotkey { HOTKEY_RUN, HOTKEY_MINIMIZE_ALL, HOTKEY_UNDO_MINIMIZE_ALL, HOTKEY_HELP, HOTKEY_EXPLORE, HOTKEY_FIND_FILES,
              HOTKEY_FIND_COMPUTERS, HOTKEY_NEXT_TASK, HOTKEY_PREVIOUS_TASK, HOTKEY_SYSTEM, HOTKEY_DESKTOP, HOTKEY_TRAY };
const struct
{
    int key, modifiers;
} HOTKEYS[] = {{'R', 0}, {'M', 0}, {'M', Tray::MOD_SHIFT}, {0x70, 0}, {'E', 0}, {'F', 0}, {'F', Tray::MOD_CONTROL},
               {0x09, 0}, {0x09, Tray::MOD_SHIFT}, {0x13, 0}, {'D', 0}, {'B', 0}};
const int TEXT_RAISE = 1;           // the taskbar's small text sits a pixel above the centre
const int TURN_OFF_DELAY_MS = 200;
// The notification area as explorer.exe lays it out: the style's content margins (19 and 7 with Luna),
// a toolbar of 18-pixel buttons with the icons 1 inside, then the clock. The clock is as wide as the
// longer of 11:00 and 23:00 in the user's format with "00" after it, whatever the time, and centres
// the time in it (measured with 4 and 5 icons: the area at 868 and 850, the time's middle at 987).
// Without a visual style, 6 at each end and 7 before the clock.
const int TRAY_BUTTON = 18, TRAY_ICON_INSET = 1, CLASSIC_TRAY_END = 6, CLASSIC_CLOCK_GAP = 7;
// Hide inactive icons (explorer.exe's TrayNotify defaults: IconDemoteTimerTick 300000, set at
// 0x101811d; TrayIconResidentInterval 3600000, at 0x10180d2). With icons hidden the chevron stands
// on the area's left edge, centred on it, and the icons start 13 after the edge instead of 20
// (explorer.exe's notification preview 180).
const int TRAY_DEMOTE_TICK_MS = 300000;
const qint64 TRAY_RESIDENT_MS = 3600000;
const int TRAY_CHEVRON_ICONS = 13, TRAY_ART_EDGE = 10;   // the area's art starts with 10 transparent columns
const int STRING_CHEVRON_HIDE = 542, STRING_CHEVRON_SHOW = 543;
const Part TRAY_CHEVRON{QStringLiteral("button"), {}, {}, 1, QStringLiteral("traynotifyhoriz")};
const Part TRAY_CHEVRON_OPEN{QStringLiteral("button"), {}, {}, 1, QStringLiteral("traynotifyhorizopen")};
const int ICON_SIZE = 16;

// Toolbar part states and their frames in the style's strips
enum { Normal = 1, Hot = 2, Pressed = 3, Checked = 5, HotChecked = 6 };

const Part START_BUTTON{QStringLiteral("button"), {}, {}, 1, QStringLiteral("start")};
const Part TASK_BUTTON{QStringLiteral("toolbar"), QStringLiteral("button"), {}, 1, QStringLiteral("taskband")};
const Part TASK_TOOLBAR{QStringLiteral("toolbar"), {}, {}, 0, QStringLiteral("taskband")};
// The Quick Launch toolbar's buttons: 23 wide (the icon and comctl32's 7 of padding) and the band's
// height, their art (TaskBar::Toolbar.Button, the task buttons' without an edge) showing rows 3 to 27
// as the task buttons'; measured on XP's with the taskbar locked: icons at 107 and 130, 7 rows down,
// the hot frame from 104, the task band 4 after the last button.
const Part QUICK_BUTTON{QStringLiteral("toolbar"), QStringLiteral("button"), {}, 1, QStringLiteral("taskbar")};
const int QUICK_WIDTH = 23, QUICK_ICON_X = 3, QUICK_ICON_Y = 7, QUICK_BAND_END = 4;
// View > Large Icons, measured on XP's taskbar: the 32-pixel icons, a button every 39 pixels (32 + 7, as 16 + 7
// for the small ones), the icon 3 in and 7 down as the small ones; the bar grows from 30 to 46 rows (the
// icon and 14, as 16 and 14 make 30), under the unlocked bar's 4 sizing rows.
const int QUICK_LARGE_ICON = 32, QUICK_LARGE_WIDTH = 39, QUICK_LARGE_ICON_X = 3, QUICK_LARGE_ICON_Y = 7,
          QUICK_LARGE_BAR_EXTRA = 16;
// Show Text: the name 4 after the icon and 4 before the button's end (XP's at 134 for "Windows Media
// Player"); Show Title: the name at the band's start, then 4 before the buttons
const int QUICK_TEXT_GAP = 4, QUICK_TEXT_END = 4, QUICK_TITLE_GAP = 4;
const Part REBAR{QStringLiteral("rebar"), {}, {}, 0, QStringLiteral("taskbar")};
const Part QUICK_TOOLBAR{QStringLiteral("toolbar"), {}, {}, 0, QStringLiteral("taskbar")};
// browselc.dll's menus for the band: the folder band's (258: View, Open Folder, Show Text), the
// band site's Show Title (256) and Close Toolbar (257)
const int MENU_ISF_BAND = 258, MENU_BAND_TITLE = 256, MENU_BAND_CLOSE = 257;
enum { CMD_VIEW_LOGOS = 3, CMD_VIEW_LARGE = 1, CMD_VIEW_SMALL = 2, CMD_OPEN_FOLDER = 6, CMD_SHOW_TEXT = 4, CMD_SHOW_TITLE = 2,
       CMD_CLOSE_TOOLBAR = 1 };
// shell32.dll's item menu (210) and the commands it carries out here
const int MENU_ITEM = 210;
enum { CMD_ITEM_CUT = 24, CMD_ITEM_COPY = 25, CMD_ITEM_DELETE = 17 };
const int STRING_OPEN = 8496;
const int ICON_SHOW_DESKTOP = 3;
// Unlocked: the sizing bar above (TaskBar.SizingBarBottom, 4 rows on XP's) and each band's gripper
const Part SIZING_BAR{QStringLiteral("taskbar"), QStringLiteral("sizingbarbottom"), {}, 1, {}};
const Part GRIPPER{QStringLiteral("rebar"), QStringLiteral("gripper"), {}, 1, QStringLiteral("taskbar")};
const int BAND_HEADER = 10, SIZING_BAR_HEIGHT = 4;    // explorer.exe's fourth icon, Show Desktop's
const Part TASK_FLASH{QStringLiteral("taskband"), QStringLiteral("flashbutton"), {}, 1, {}};
const Part BACKGROUND{QStringLiteral("taskbar"), QStringLiteral("backgroundbottom"), {}, 1, {}};
const Part TRAY_BACKGROUND{QStringLiteral("traynotify"), QStringLiteral("background"), {}, 1, QStringLiteral("traynotifyhoriz")};
const Part CLOCK{QStringLiteral("clock"), {}, {}, 0, QStringLiteral("traynotify")};
const Part GROUP_COUNT{QStringLiteral("taskband"), QStringLiteral("groupcount"), {}, 0, {}};
const Part GROUP_LIST{QStringLiteral("toolbar"), {}, {}, 0, QStringLiteral("taskbandgroupmenu")};
const Part GROUP_LIST_ITEM{QStringLiteral("toolbar"), QStringLiteral("button"), {}, 2, QStringLiteral("taskbandgroupmenu")};
// explorer.exe's sub_102de3e: a vertical scrollbar at the band's right when the buttons do
// not fit at their least width; its parts are the style's TaskBand::Scrollbar (SBP_ARROWBTN's 16
// states: up 1-4, down 5-8, left 9-12, right 13-16).
const int SCROLL_WIDTH = 17, CLASSIC_SCROLL_WIDTH = 16;

// Quick Launch's folder, the one XP keeps under Application Data\Microsoft\Internet Explorer
QString quickLaunchFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/win4wine/Quick Launch");
}

Part withState(Part part, int state)
{
    part.stateId = state;
    return part;
}

ShellTheme *theme()
{
    return ShellTheme::instance();
}

// Close on a button is EndTask(window, FALSE, Ctrl held) (sub_102fea7): with Ctrl the window's client is
// killed at once; otherwise it gets WM_CLOSE, and only a window that has not gone and has not answered
// with a dialog of its own after HungAppTimeout (5000 in hivedef.inf) is offered to be ended, with
// explorer.exe's strings 603/604. X has no IsHungAppWindow; an owned window stands for the answer.
const int HUNG_APP_TIMEOUT_MS = 5000;

void closeWithForce(xcb_window_t window)
{
    if (QGuiApplication::queryKeyboardModifiers() & Qt::ControlModifier) {
        xcb_kill_client(x11::connection(), window);
        xcb_flush(x11::connection());
        return;
    }
    WindowList::close(window);
    QTimer::singleShot(HUNG_APP_TIMEOUT_MS, qApp, [window] {
        xcb_generic_error_t *error = nullptr;
        xcb_get_window_attributes_reply_t *attributes =
            xcb_get_window_attributes_reply(x11::connection(), xcb_get_window_attributes(x11::connection(), window), &error);
        free(error);
        if (!attributes)
            return;
        free(attributes);
        for (quint32 other : x11::property32(x11::root(), x11::atom("_NET_CLIENT_LIST")))
            if (x11::property32(other, XCB_ATOM_WM_TRANSIENT_FOR).value(0) == window)
                return;
        const auto explorer = xpfiles::resources(xpfiles::find(QStringLiteral("explorer.exe")));
        QString busy = QObject::tr("The application is either too busy or unable to respond to your request to close.");
        QString force = QObject::tr("Forcing it to close may cause that application to lose data.  Do you want to force it to close?");
        if (explorer) {
            if (const QString found = xpfiles::string(*explorer, 603); !found.isEmpty()) busy = found;
            if (const QString found = xpfiles::string(*explorer, 604); !found.isEmpty()) force = found;
        }
        const QString title = QString::fromUtf8(x11::property(window, x11::atom("_NET_WM_NAME")));
        QMessageBox box(QMessageBox::Warning, title, busy + QStringLiteral("\n\n") + force, QMessageBox::Yes | QMessageBox::No);
        if (box.exec() == QMessageBox::Yes)
            xcb_kill_client(x11::connection(), window);
        xcb_flush(x11::connection());
    });
}

} // namespace

// A group's windows, which explorer.exe lists in a menu band drawn by the style's TaskBandGroupMenu:
// at least as wide as the button, over it. Its rows are the buttons' content margins around a small
// icon (3 + 16 + 3), the toolbar's 9 pixels in, then the button's 9.
class GroupList : public QWidget
{
public:
    struct Item
    {
        xcb_window_t window;
        QString title;
        QImage icon;
    };
    static constexpr int TEXT_GAP = 7, END = 9;

    GroupList(const QList<Item> &items, int minWidth, std::function<void(xcb_window_t)> chosen,
              std::function<void(xcb_window_t, QPoint)> context)
        : QWidget(nullptr, Qt::Popup | Qt::FramelessWindowHint)
        , m_items(items)
        , m_chosen(std::move(chosen))
        , m_context(std::move(context))
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setMouseTracking(true);
        w4w::Renderer *renderer = theme()->renderer();
        const QString spec = renderer ? renderer->property(GROUP_LIST, QStringLiteral("font")).value_or(QStringLiteral("Tahoma, 8")) : QStringLiteral("Tahoma, 8");
        setFont(theme()->font(spec));
        // The menu band's toolbar's content margins (Luna: 9, 0, 0, 0) and its buttons' (9, 0, 3, 3),
        // as the style gives them: the row is the icon and those margins around it.
        m_toolbarMargins = renderer ? renderer->contentMargins(GROUP_LIST) : QMargins(0, 0, 0, 0);
        m_buttonMargins = renderer ? renderer->contentMargins(GROUP_LIST_ITEM) : QMargins(3, 3, 0, 3);
        m_row = qMax(theme()->scaled(ICON_SIZE), fontMetrics().height()) + m_buttonMargins.top() + m_buttonMargins.bottom();
        int widest = 0;
        for (const Item &item : m_items)
            widest = qMax(widest, fontMetrics().horizontalAdvance(item.title));
        const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
        const int width = m_toolbarMargins.left() + m_toolbarMargins.right() + m_buttonMargins.left() + theme()->scaled(ICON_SIZE + TEXT_GAP)
            + m_buttonMargins.right() + theme()->scaled(END) + widest + frameWidth() * 2;
        resize(qBound(minWidth, width, screen.width() / 2), m_row * int(m_items.size()) + frameWidth() * 2);
    }

    void popUp(const QRect &button)
    {
        const QRect screen = QGuiApplication::primaryScreen()->geometry();
        QPoint at(button.x(), button.y() - height());
        at.setX(qBound(screen.left(), at.x(), screen.right() + 1 - width()));
        move(at);
        show();
    }

protected:
    // Windows Classic draws it as a menu: a raised edge of 2.
    int frameWidth() const { return theme()->renderer() ? 0 : theme()->scaled(2); }

    QRect rowRect(int row) const
    {
        const int f = frameWidth();
        return QRect(f + m_toolbarMargins.left(), f + row * m_row,
                     width() - 2 * f - m_toolbarMargins.left() - m_toolbarMargins.right(), m_row);
    }

    int rowAt(QPoint at) const
    {
        for (int i = 0; i < m_items.size(); ++i)
            if (rowRect(i).contains(at))
                return i;
        return -1;
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        w4w::Renderer *renderer = theme()->renderer();
        QColor text = Qt::white;
        if (renderer) {
            renderer->draw(&painter, rect(), GROUP_LIST);
            text = renderer->colour(GROUP_LIST, QStringLiteral("textcolor")).value_or(Qt::white);
        } else {
            painter.fillRect(rect(), theme()->system(QStringLiteral("menu"), Qt::lightGray));
            theme()->raised(painter, rect(), false);
            text = Qt::black;
        }
        painter.setFont(font());
        for (int i = 0; i < m_items.size(); ++i) {
            const QRect row = rowRect(i);
            QColor colour = text;
            if (i == m_hover) {
                if (renderer) {
                    renderer->draw(&painter, row, GROUP_LIST_ITEM);
                } else {
                    painter.fillRect(row, theme()->system(QStringLiteral("highlight"), Qt::darkBlue));
                    colour = theme()->system(QStringLiteral("highlighttext"), Qt::white);
                }
            }
            const int icon = theme()->scaled(ICON_SIZE);
            const int iconX = row.x() + m_buttonMargins.left();
            painter.drawImage(QRect(iconX, row.y() + (row.height() - icon) / 2, icon, icon), m_items[i].icon);
            const int textX = iconX + icon + theme()->scaled(TEXT_GAP);
            const QRect words(textX, row.y(), row.right() - m_buttonMargins.right() - theme()->scaled(END) - textX, row.height());
            painter.setPen(colour);
            painter.drawText(words, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                             fontMetrics().elidedText(m_items[i].title, Qt::ElideRight, words.width()));
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const int row = rowAt(event->position().toPoint());
        if (row != m_hover) {
            m_hover = row;
            update();
        }
    }

    void leaveEvent(QEvent *) override
    {
        m_hover = -1;
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const int row = rowAt(event->position().toPoint());
        if (row < 0)
            return;
        const xcb_window_t window = m_items[row].window;
        const QPoint at = event->globalPosition().toPoint();
        const bool context = event->button() == Qt::RightButton;
        close();
        if (context)
            m_context(window, at);
        else if (event->button() == Qt::LeftButton)
            m_chosen(window);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        const int count = int(m_items.size());
        if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up) {
            m_hover = (m_hover + (event->key() == Qt::Key_Down ? 1 : count - 1) + (m_hover < 0 && event->key() == Qt::Key_Up ? 1 : 0)) % count;
            update();
        } else if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && m_hover >= 0) {
            const xcb_window_t window = m_items[m_hover].window;
            close();
            m_chosen(window);
        } else if (event->key() == Qt::Key_Escape) {
            close();
        }
    }

private:
    QList<Item> m_items;
    std::function<void(xcb_window_t)> m_chosen;
    std::function<void(xcb_window_t, QPoint)> m_context;
    QMargins m_toolbarMargins, m_buttonMargins;
    int m_row = 22;
    int m_hover = -1;
};

Taskbar::Taskbar(WindowList *windows, Tray *tray)
    : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint)
    , m_windows(windows)
    , m_tray(tray)
{
    setAttribute(Qt::WA_X11NetWmWindowTypeDock);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    setAcceptDrops(true);
    setWindowTitle(QStringLiteral("Taskbar"));

    if (const auto explorer = xpfiles::resources(xpfiles::find(QStringLiteral("explorer.exe")))) {
        m_startText = xpfiles::string(*explorer, STRING_START);
        m_classicStartText = xpfiles::string(*explorer, STRING_CLASSIC_START);
        m_flag = xpfiles::bitmap(*explorer, BITMAP_FLAG).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        m_smallFlag = xpfiles::bitmap(*explorer, BITMAP_SMALL_FLAG).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    if (m_startText.isEmpty())
        m_startText = QStringLiteral("start");
    if (m_classicStartText.isEmpty())
        m_classicStartText = QStringLiteral("Start");

    connect(m_windows, &WindowList::changed, this, [this] {
        // another window chosen with the mouse takes the keyboard back
        if (m_keyboard && m_windows->active() != m_activeAtKeyboard)
            leaveKeyboard();
        attentionChanged();
        relayout();
        update();
    });
    connect(m_tray, &Tray::changed, this, &Taskbar::trayChanged);
    // Customize Notifications shows each current item's icon.
    traynotify::setIconLookup([this](const QString &name) -> QImage {
        for (const TrayIcon &icon : m_tray->icons())
            if (trayName(icon) == name)
                return icon.image;
        return QImage();
    });
    // explorer.exe's IconDemoteTimerTick: every 5 minutes, icons idle for TrayIconResidentInterval
    // (an hour) go behind the chevron
    m_demoteTimer.setInterval(TRAY_DEMOTE_TICK_MS);
    connect(&m_demoteTimer, &QTimer::timeout, this, &Taskbar::updateTrayHiding);
    m_demoteTimer.start();
    m_trayCollapseTimer.setSingleShot(true);
    m_trayCollapseTimer.setInterval(TRAY_COLLAPSE_MS);
    connect(&m_trayCollapseTimer, &QTimer::timeout, this, [this] {
        if (!m_trayOpen || geometry().contains(QCursor::pos()))
            return;
        m_trayOpen = false;
        relayout();
        update();
    });
    m_tray->balloons()->setLocator([this](quint32 id) {
        const QList<TrayIcon> &icons = m_tray->icons();
        for (int i = 0; i < icons.size() && i < m_trayIconRects.size(); ++i)
            if (icons[i].id == id && isVisible())
                return QRect(mapToGlobal(m_trayIconRects[i].topLeft() + QPoint(0, m_top)), m_trayIconRects[i].size());
        return QRect();
    });
    connect(m_tray, &Tray::startRequested, this, [this] { showStartMenu(true); });
    connect(m_tray, &Tray::hotkey, this, &Taskbar::hotkey);
    QTimer::singleShot(START_BALLOON_DELAY_MS, this, &Taskbar::startButtonBalloon);
    QTimer::singleShot(REMINDERS_DELAY_MS, this, [this] { lowResolutionBalloon([this] { tourBalloon(); }); });
    m_dragTimer.setSingleShot(true);
    connect(&m_dragTimer, &QTimer::timeout, this, [this] {
        if (m_dragHit.kind == Hit::Task && m_dragHit.index < m_buttons.size()) {
            const Button &button = m_buttons[m_dragHit.index];
            if (!button.group)
                m_windows->switchTo(m_windows->tasks()[button.tasks.first()].window);
        } else if (m_dragHit.kind == Hit::Band) {
            // sub_1002d65(1): the desktop shown
            const QList<quint32> showing = x11::property32(x11::root(), x11::atom("_NET_SHOWING_DESKTOP"));
            if (showing.isEmpty() || !showing.first())
                toggleDesktop();
        }
        m_dragTimer.start(DRAG_AGAIN_MS);
    });
    connect(theme(), &ShellTheme::changed, this, &Taskbar::themeChanged);
    // A scrollbar's arrow repeats while it is held.
    m_scrollRepeat.setInterval(100);
    connect(&m_scrollRepeat, &QTimer::timeout, this, [this] {
        if (m_pressed.kind == Hit::ScrollUp)
            scrollStep(-1);
        else if (m_pressed.kind == Hit::ScrollDown)
            scrollStep(1);
        else
            m_scrollRepeat.stop();
    });

    m_autoHideTimer.setInterval(500);
    connect(&m_autoHideTimer, &QTimer::timeout, this, &Taskbar::checkAutoHide);
    m_unhideTimer.setSingleShot(true);
    m_unhideTimer.setInterval(50);
    connect(&m_unhideTimer, &QTimer::timeout, this, [this] { setHidden(false); });
    m_slideTimer.setInterval(20);
    connect(&m_slideTimer, &QTimer::timeout, this, [this] {
        const qint64 elapsed = m_slideClock.elapsed();
        if (elapsed >= m_slideDuration) {
            m_slideTimer.stop();
            setGeometry(m_slideTo);
            return;
        }
        // as the tray moves it: its centre, in proportion to the time gone
        const QPoint shift = (m_slideTo.center() - m_slideFrom.center()) * double(elapsed) / m_slideDuration;
        setGeometry(m_slideFrom.translated(shift));
    });
    m_clockTimer.setSingleShot(true);
    connect(&m_clockTimer, &QTimer::timeout, this, &Taskbar::tickClock);
    themeChanged();
    tickClock();
    const auto reloadSettings = [this] {
        const TaskbarSettings settings = TaskbarSettings::load();
        if (settings.group == m_settings.group && settings.groupSize == m_settings.groupSize
            && settings.clock == m_settings.clock && settings.autoHide == m_settings.autoHide
            && settings.quickLaunch == m_settings.quickLaunch && settings.desktopBand == m_settings.desktopBand
            && settings.customToolbar == m_settings.customToolbar && settings.linksBand == m_settings.linksBand
            && settings.locked == m_settings.locked
            && settings.onTop == m_settings.onTop
            && settings.quickLaunchText == m_settings.quickLaunchText && settings.quickLaunchTitle == m_settings.quickLaunchTitle
            && settings.quickLaunchLargeIcons == m_settings.quickLaunchLargeIcons
            && settings.hideInactive == m_settings.hideInactive) {
            updateTrayHiding();   // the behaviours chosen in Customize Notifications
            return;
        }
        const bool autoHide = settings.autoHide != m_settings.autoHide || settings.locked != m_settings.locked;
        const bool icons = settings.quickLaunchLargeIcons != m_settings.quickLaunchLargeIcons;
        // the bar's height follows Quick Launch's large icons
        const bool height = icons || settings.quickLaunch != m_settings.quickLaunch;
        m_settings = settings;
        if (icons)
            loadQuickLaunch();
        loadDesktopBand();
        loadCustomToolbar();
        loadLinksBand();
        setOnTop();
        updateTrayHiding();
        if (autoHide) {
            setHidden(false, false);
            placeOnScreen();
        } else if (height) {
            placeOnScreen();
        }
        relayout();
        update();
    };
    connect(TaskbarSettingsNotifier::instance(), &TaskbarSettingsNotifier::changed, this, reloadSettings);
    // and from the sheet of another process (explorer --applet taskbar): QSettings replaces the file
    const QString settingsFolder = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine");
    QDir().mkpath(settingsFolder);
    m_settingsWatcher.addPath(settingsFolder);
    connect(&m_settingsWatcher, &QFileSystemWatcher::directoryChanged, this, reloadSettings);
    loadQuickLaunch();
    connect(&m_quickWatcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        loadQuickLaunch();
        relayout();
        update();
    });
    loadDesktopBand();
    m_desktopWatcher.addPath(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    connect(&m_desktopWatcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        loadDesktopBand();
        relayout();
        update();
    });
    loadCustomToolbar();
    connect(&m_customWatcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        loadCustomToolbar();
        relayout();
        update();
    });
    loadLinksBand();
    connect(&m_linksWatcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        loadLinksBand();
        relayout();
        update();
    });
    // bridge-wm and the session's supervisor know XP's taskbar by this class.
    x11::setString(winId(), "_WIN4WINE_CLASS", "Shell_TrayWnd");
    setOnTop();
}

void Taskbar::themeChanged()
{
    w4w::Renderer *renderer = theme()->renderer();
    const auto font = [&](const Part &part, const char *fallback) {
        const QString spec = renderer ? renderer->property(part, QStringLiteral("font")).value_or(QLatin1String(fallback)) : QLatin1String(fallback);
        return theme()->font(spec);
    };
    m_startFont = renderer ? font(START_BUTTON, "Franklin Gothic Medium, 14, italic") : theme()->font(QStringLiteral("Tahoma, 8, bold"));
    if (renderer) {
        m_startFont.setStyleStrategy(QFont::StyleStrategy(QFont::PreferAntialias | QFont::NoSubpixelAntialias));
    }
    m_taskFont = font(TASK_TOOLBAR, "Tahoma, 8");
    // The pressed (active) button's text is not bold on XP either.
    m_activeTaskFont = m_taskFont;
    m_clockFont = font(CLOCK, "Tahoma, 8");
    // A group's count: the style's TaskBand.GroupCount, or the toolbar's font in bold
    m_groupCountFont = m_taskFont;
    m_groupCountFont.setBold(true);
    if (renderer)
        m_groupCountFont = font(GROUP_COUNT, "Tahoma, 8, bold");
    placeOnScreen();
    update();
}

int Taskbar::baseHeight() const
{
    // the bar's own rows, taller for Quick Launch's large icons (Windows Classic's not measured: the same 16)
    const int bar = theme()->scaled(theme()->classic() ? CLASSIC_BAR_HEIGHT : BAR_HEIGHT);
    return bar * qMax(1, m_settings.rows)
        + (m_settings.quickLaunch && m_settings.quickLaunchLargeIcons ? theme()->scaled(QUICK_LARGE_BAR_EXTRA) : 0);
}

QRect Taskbar::shownRect() const
{
    const QRect monitor = QGuiApplication::primaryScreen()->geometry();
    const int height = baseHeight() + sizingHeight();
    if (m_settings.position == 1)
        return QRect(monitor.x(), monitor.y(), monitor.width(), height);
    return QRect(monitor.x(), monitor.bottom() + 1 - height, monitor.width(), height);
}

void Taskbar::setOnTop()
{
    // Not on top: the window manager keeps the bar in the windows' order (bridge-wm's topmost_dock)
    const QList<quint32> below = m_settings.onTop ? QList<quint32>() : QList<quint32>{x11::atom("_NET_WM_STATE_BELOW")};
    x11::setProperty32(winId(), "_NET_WM_STATE", XCB_ATOM_ATOM, below);
}

int Taskbar::sizingHeight() const
{
    // Windows Classic's bar has no sizing bar of its own: its raised top edge is the one
    if (m_settings.locked || !theme()->renderer())
        return 0;
    const QSize size = theme()->renderer()->trueSize(SIZING_BAR);
    return size.height() > 0 ? size.height() : theme()->scaled(SIZING_BAR_HEIGHT);
}

QRect Taskbar::hiddenRect() const
{
    // explorer.exe's hidden rectangle: the bar's top at the monitor's bottom less SM_CYFRAME / 2
    const QRect monitor = QGuiApplication::primaryScreen()->geometry();
    const int frame = theme()->scaled(4);   // SM_CYFRAME on XP
    QRect rect = shownRect();
    if (m_settings.position == 1)
        rect.moveTop(monitor.top() - rect.height() + frame / 2);
    else
        rect.moveTop(monitor.bottom() + 1 - frame / 2);
    return rect;
}

void Taskbar::checkAutoHide()
{
    if (!m_settings.autoHide || m_hidden || m_slideTimer.isActive())
        return;
    // not while the pointer, a menu, the Start panel or the keyboard is on the bar
    if (geometry().contains(QCursor::pos()) || m_startPanel || QApplication::activePopupWidget() || isActiveWindow())
        return;
    // nor while a button that brought it back flashes
    if (!m_unhidFor.isEmpty())
        return;
    setHidden(true);
}

void Taskbar::attentionChanged()
{
    QSet<xcb_window_t> attention;
    for (const Task &task : m_windows->tasks())
        if (task.attention)
            attention.insert(task.window);
    for (xcb_window_t window : std::as_const(attention))
        if (!m_attention.contains(window) && window != m_windows->active()) {
            m_unhidFor.insert(window);
            if (m_hidden)
                setHidden(false);
        }
    // the flag goes with the flashing
    m_unhidFor &= attention;
    m_attention = attention;
    if (attention.isEmpty()) {
        m_flashTimer.stop();
        m_flashOn = true;
    } else if (!m_flashTimer.isActive()) {
        m_flashOn = true;
        m_flashTimer.start();
    }
}

void Taskbar::setHidden(bool hidden, bool animate)
{
    m_unhideTimer.stop();
    if (!m_settings.autoHide)
        hidden = false;
    m_hidden = hidden;
    m_hiddenMouse = QCursor::pos();
    const QRect target = hidden ? hiddenRect() : shownRect();
    if (m_settings.autoHide && !hidden)
        m_autoHideTimer.start();
    else
        m_autoHideTimer.stop();
    if (!animate || !isVisible() || geometry() == target) {
        m_slideTimer.stop();
        setGeometry(target);
        return;
    }
    m_slideFrom = geometry();
    m_slideTo = target;
    m_slideDuration = hidden ? 400 : 200;
    m_slideClock.start();
    m_slideTimer.start();
}

void Taskbar::placeOnScreen()
{
    const QScreen *screen = QGuiApplication::primaryScreen();
    const QRect monitor = screen->geometry();
    m_top = sizingHeight();
    const int height = baseHeight() + m_top;
    m_slideTimer.stop();
    setGeometry(m_hidden && m_settings.autoHide ? hiddenRect() : shownRect());
    if (m_settings.autoHide && !m_hidden)
        m_autoHideTimer.start();
    // The screen's bottom edge may be another monitor's: struts count from the root's. An
    // auto-hidden bar keeps no room: windows have the whole screen, as XP's work area.
    const quint32 start = monitor.left(), end = monitor.right();
    if (m_settings.position == 1) {
        const int top = m_settings.autoHide ? 0 : monitor.top() - screen->virtualGeometry().top() + height;
        x11::setProperty32(winId(), "_NET_WM_STRUT", XCB_ATOM_CARDINAL, {0, 0, quint32(top), 0});
        x11::setProperty32(winId(), "_NET_WM_STRUT_PARTIAL", XCB_ATOM_CARDINAL,
                           {0, 0, quint32(top), 0, 0, 0, 0, 0, start, end, 0, 0});
    } else {
        const int bottom = m_settings.autoHide ? 0 : screen->virtualGeometry().bottom() - monitor.bottom() + height;
        x11::setProperty32(winId(), "_NET_WM_STRUT", XCB_ATOM_CARDINAL, {0, 0, 0, quint32(bottom)});
        x11::setProperty32(winId(), "_NET_WM_STRUT_PARTIAL", XCB_ATOM_CARDINAL,
                           {0, 0, 0, quint32(bottom), 0, 0, 0, 0, 0, 0, start, end});
    }
    relayout();
}

QString Taskbar::clockText() const
{
    return QLocale::system().toString(QTime::currentTime(), QLocale::ShortFormat);
}

void Taskbar::tickClock()
{
    const QString text = clockText();
    if (text != m_clock) {
        m_clock = text;
        relayout();
        update();
    }
    // Asleep until the minute changes.
    const QTime now = QTime::currentTime();
    m_clockTimer.start((60 - now.second()) * 1000 - now.msec() + 50);
}

void Taskbar::relayout()
{
    w4w::Renderer *renderer = theme()->renderer();
    const QFontMetrics startMetrics(m_startFont);
    int startWidth, bandStart;
    if (renderer) {
        const QMargins margins = renderer->contentMargins(START_BUTTON);
        // The style's margins around the flag and the word, as Button's ideal size has them.
        startWidth = margins.left() + m_flag.width() + theme()->scaled(START_TEXT_GAP) + startMetrics.horizontalAdvance(m_startText) + margins.right();
        m_startRect = QRect(0, 0, startWidth, theme()->scaled(START_HEIGHT));
        bandStart = startWidth + theme()->scaled(BAND_GAP);
    } else {
        startWidth = theme()->scaled(4 + ICON_SIZE + 4 + 6) + startMetrics.horizontalAdvance(m_classicStartText);
        m_startRect = QRect(theme()->scaled(2), theme()->scaled(4), startWidth, barHeight() - theme()->scaled(6));
        bandStart = m_startRect.right() + 1 + theme()->scaled(6);
    }
    // Quick Launch's band first, its buttons from the band's start; unlocked, each band begins with
    // its gripper, in a header of 10 (XP's grippers at 104 and 164, the buttons from 114). Show Title
    // puts the folder's name first; Show Text gives every button the widest name's room after its icon.
    const int header = m_settings.locked ? 0 : theme()->scaled(BAND_HEADER);
    m_grippers.clear();
    m_quickBand = m_quickTitle = QRect();
    for (QuickItem &item : m_quickItems)
        item.rect = QRect();
    if (m_settings.quickLaunch) {
        const int first = bandStart;
        if (header)
            m_grippers.append(QRect(bandStart, 0, header, barHeight()));
        int x = bandStart + header;
        const QFontMetrics metrics(m_taskFont);
        if (m_settings.quickLaunchTitle) {
            const int title = metrics.horizontalAdvance(QFileInfo(quickLaunchFolder()).fileName());
            m_quickTitle = QRect(x, 0, title, barHeight());
            x += title + theme()->scaled(QUICK_TITLE_GAP);
        }
        const bool large = m_settings.quickLaunchLargeIcons;
        const int icon = large ? QUICK_LARGE_ICON : ICON_SIZE;
        const int iconX = large ? QUICK_LARGE_ICON_X : QUICK_ICON_X;
        int buttonWidth = theme()->scaled(large ? QUICK_LARGE_WIDTH : QUICK_WIDTH);
        if (m_settings.quickLaunchText) {
            int widest = 0;
            for (const QuickItem &item : std::as_const(m_quickItems))
                widest = qMax(widest, metrics.horizontalAdvance(item.name));
            buttonWidth = theme()->scaled(iconX + icon + QUICK_TEXT_GAP + QUICK_TEXT_END) + widest;
        }
        for (int i = 0; i < m_quickItems.size(); ++i)
            m_quickItems[i].rect = QRect(x + i * buttonWidth, renderer ? 0 : (barHeight() - theme()->scaled(22)) / 2, buttonWidth,
                                         renderer ? barHeight() : theme()->scaled(22));
        x += int(m_quickItems.size()) * buttonWidth;
        m_quickBand = QRect(first, 0, x - first, barHeight());
        bandStart = x + theme()->scaled(QUICK_BAND_END);
    }
    m_desktopBand = QRect();
    for (QuickItem &item : m_desktopItems)
        item.rect = QRect();
    if (m_settings.desktopBand && !m_desktopItems.isEmpty()) {
        const int first = bandStart;
        const int width = theme()->scaled(QUICK_WIDTH);
        for (int i = 0; i < m_desktopItems.size(); ++i) {
            const int height = renderer ? barHeight() : theme()->scaled(22);
            m_desktopItems[i].rect = QRect(first + i * width, renderer ? 0 : (barHeight() - height) / 2, width, height);
        }
        m_desktopBand = QRect(first, 0, int(m_desktopItems.size()) * width, barHeight());
        bandStart = first + int(m_desktopItems.size()) * width + theme()->scaled(QUICK_BAND_END);
    }
    m_customBand = QRect();
    for (QuickItem &item : m_customItems)
        item.rect = QRect();
    if (!m_customItems.isEmpty()) {
        const int first = bandStart;
        const int width = theme()->scaled(QUICK_WIDTH);
        for (int i = 0; i < m_customItems.size(); ++i) {
            const int height = renderer ? barHeight() : theme()->scaled(22);
            m_customItems[i].rect = QRect(first + i * width, renderer ? 0 : (barHeight() - height) / 2, width, height);
        }
        m_customBand = QRect(first, 0, int(m_customItems.size()) * width, barHeight());
        bandStart = first + int(m_customItems.size()) * width + theme()->scaled(QUICK_BAND_END);
    }
    m_linksBand = QRect();
    for (QuickItem &item : m_linksItems)
        item.rect = QRect();
    if (!m_linksItems.isEmpty()) {
        const int first = bandStart;
        const int width = theme()->scaled(QUICK_WIDTH);
        for (int i = 0; i < m_linksItems.size(); ++i) {
            const int height = renderer ? barHeight() : theme()->scaled(22);
            m_linksItems[i].rect = QRect(first + i * width, renderer ? 0 : (barHeight() - height) / 2, width, height);
        }
        m_linksBand = QRect(first, 0, int(m_linksItems.size()) * width, barHeight());
        bandStart = first + int(m_linksItems.size()) * width + theme()->scaled(QUICK_BAND_END);
    }
    if (header) {
        m_grippers.append(QRect(bandStart, 0, header, barHeight()));
        bandStart += header;
    }

    const QFontMetrics clockMetrics(m_clockFont);
    const int icons = int(m_tray->icons().size());
    int clockWidth = 0;
    for (int hour : {11, 23})
        clockWidth = qMax(clockWidth, clockMetrics.horizontalAdvance(QLocale::system().toString(QTime(hour, 0), QLocale::ShortFormat) + QLatin1String("00")));
    // (uxtheme's extent of "11:00 PM00" is a pixel more than these advances: 58 on XP's.)
    clockWidth += theme()->scaled(1);
    // Show the clock, of Taskbar and Start Menu Properties (explorer.exe's HideClock)
    if (!m_settings.clock)
        clockWidth = 0;
    const QMargins margins = renderer ? renderer->contentMargins(TRAY_BACKGROUND) : QMargins(CLASSIC_TRAY_END, 0, CLASSIC_TRAY_END, 0);
    // with icons hidden (or shown again by the chevron), the chevron's room at the left
    const bool chevron = m_settings.hideInactive && !m_trayHidden.isEmpty();
    const QList<TrayIcon> &trayIcons = m_tray->icons();
    int shown = 0;
    for (const TrayIcon &icon : trayIcons)
        shown += m_trayOpen || !chevron || !m_trayHidden.contains(icon.id);
    const int first = chevron && renderer ? theme()->scaled(TRAY_ART_EDGE + TRAY_CHEVRON_ICONS - TRAY_ICON_INSET) : theme()->scaled(margins.left());
    const int end = theme()->scaled(margins.right());
    const int gap = renderer || !m_settings.clock ? 0 : theme()->scaled(CLASSIC_CLOCK_GAP);
    const int step = theme()->scaled(TRAY_BUTTON);
    const QSize chevronSize = renderer && renderer->trueSize(TRAY_CHEVRON).isValid() && !renderer->trueSize(TRAY_CHEVRON).isEmpty()
        ? renderer->trueSize(TRAY_CHEVRON) : QSize(theme()->scaled(16), theme()->scaled(16));
    const int classicChevron = chevron && !renderer ? chevronSize.width() + theme()->scaled(2) : 0;
    const int trayWidth = first + classicChevron + shown * step + gap + clockWidth + end;
    if (renderer)
        m_trayRect = QRect(width() - trayWidth, 0, trayWidth, barHeight());
    else
        m_trayRect = QRect(width() - trayWidth - theme()->scaled(2), theme()->scaled(4), trayWidth, barHeight() - theme()->scaled(6));
    m_trayIconRects.clear();
    int slot = 0;
    for (int i = 0; i < icons; ++i) {
        if (!(m_trayOpen || !chevron || !m_trayHidden.contains(trayIcons[i].id))) {
            m_trayIconRects.append(QRect());
            continue;
        }
        m_trayIconRects.append(QRect(m_trayRect.x() + first + classicChevron + slot * step + theme()->scaled(TRAY_ICON_INSET),
                                     m_trayRect.y() + (m_trayRect.height() - theme()->scaled(ICON_SIZE)) / 2, theme()->scaled(ICON_SIZE), theme()->scaled(ICON_SIZE)));
        ++slot;
    }
    m_clockRect = QRect(m_trayRect.x() + first + classicChevron + shown * step + gap, m_trayRect.y(), clockWidth, m_trayRect.height());
    m_chevronRect = QRect();
    if (chevron)
        m_chevronRect = renderer
            ? QRect(QPoint(m_trayRect.x() + theme()->scaled(TRAY_ART_EDGE) - chevronSize.width() / 2, (barHeight() - chevronSize.height()) / 2),
                    chevronSize)
            : QRect(QPoint(m_trayRect.x() + first, (barHeight() - chevronSize.height()) / 2), chevronSize);

    layoutButtons(bandStart, (m_chevronRect.isNull() || !renderer ? m_trayRect.x() : m_chevronRect.x()) - theme()->scaled(BAND_END));
    m_tray->balloons()->iconsChanged();
}

void Taskbar::layoutButtons(int bandStart, int bandEnd)
{
    // As explorer.exe's taskband sizes its toolbar: the band's width shared by the buttons, at most
    // SM_CXMINIMIZED each. When that comes to less than the least width and 8 average characters,
    // the program whose windows were chosen least recently gets one button for its windows, again
    // until they fit or no program has two windows left; when there is room for two more buttons of
    // that width, the group chosen most recently among those that have fewer windows comes apart.
    // With nothing more to group, the buttons shrink to the least width.
    w4w::Renderer *renderer = theme()->renderer();
    const QList<Task> &tasks = m_windows->tasks();
    const int band = qMax(0, bandEnd - bandStart);
    const int rows = qMax(1, m_settings.rows);
    const int effectiveBand = band * rows;
    const int rowHeight = qMax(1, barHeight() / rows);
    m_bandRect = QRect(bandStart, 0, band, barHeight());
    const int spacing = renderer ? 0 : theme()->scaled(CLASSIC_SPACING);
    const int maxWidth = theme()->scaled(TASK_MAX_WIDTH);
    const FrameStyle &frame = theme()->frame();
    const int cySize = frame.top - frame.border - 1;
    const int minWidth = int((cySize + 2 * theme()->scaled(EDGE)) * TASK_MIN_FACTOR);
    const int groupWidth = minWidth + GROUP_CHARS * QFontMetrics(m_taskFont).averageCharWidth();

    // The programs in the buttons' order (the window list keeps each one's windows together)
    QList<QString> programs;
    QHash<QString, QList<int>> windowsOf;
    for (int i = 0; i < tasks.size(); ++i) {
        if (!windowsOf.contains(tasks[i].app))
            programs.append(tasks[i].app);
        windowsOf[tasks[i].app].append(i);
    }
    // Group similar taskbar buttons (TaskbarGlomming) unchecked: every window its own button
    if (!m_settings.group)
        m_grouped.clear();
    // TaskbarGroupSize (SHRegGetUSValueW in sub_1015ddf, default 0): 0 groups only when the buttons no
    // longer fit, 1 groups the program with the most windows first, 2 or more groups every program with
    // at least that many windows before the width is considered (sub_10050ec, sub_1023b85).
    const int groupSize = m_settings.groupSize;
    const int threshold = m_settings.group && groupSize >= 2 ? groupSize : 2;
    for (auto it = m_grouped.begin(); it != m_grouped.end();)
        it = windowsOf.value(*it).size() < threshold ? m_grouped.erase(it) : std::next(it);
    if (m_settings.group && groupSize >= 2)
        for (const QString &program : std::as_const(programs))
            if (windowsOf.value(program).size() >= groupSize)
                m_grouped.insert(program);
    const auto chosen = [&](const QString &program) {
        qint64 latest = 0;
        for (int i : windowsOf.value(program))
            latest = qMax(latest, tasks[i].lastActive);
        return latest;
    };
    const auto shown = [&] {
        int count = 0;
        for (const QString &program : std::as_const(programs))
            count += m_grouped.contains(program) ? 1 : int(windowsOf.value(program).size());
        return count;
    };
    const auto ideal = [&](int count) {
        if (!count)
            return maxWidth;
        return qMin(maxWidth, effectiveBand / count - (count > 1 ? spacing : 0));
    };
    int width = maxWidth;
    for (int pass = 0; pass <= programs.size(); ++pass) {
        const int count = shown();
        width = ideal(count);
        if (width <= groupWidth) {
            // No program can be grouped any further with TaskbarGroupSize 2 or more: they already are.
            if (groupSize >= 2) {
                width = qMax(width, minWidth);
                break;
            }
            QString candidate;
            for (const QString &program : std::as_const(programs))
                if (m_settings.group && !m_grouped.contains(program) && windowsOf.value(program).size() > 1) {
                    if (candidate.isEmpty())
                        candidate = program;
                    else if (groupSize == 1 ? windowsOf.value(program).size() > windowsOf.value(candidate).size()
                                            : chosen(program) < chosen(candidate))
                        candidate = program;
                    else if (groupSize == 1 && windowsOf.value(program).size() == windowsOf.value(candidate).size()
                             && chosen(program) < chosen(candidate))
                        candidate = program;
                }
            if (!candidate.isEmpty()) {
                m_grouped.insert(candidate);
                continue;
            }
            width = qMax(width, minWidth);
            break;
        }
        if (groupSize >= 2)
            break;
        const int room = (effectiveBand - count * groupWidth) / groupWidth;
        QString candidate;
        if (room >= 2)
            for (const QString &program : std::as_const(m_grouped))
                if (windowsOf.value(program).size() < room) {
                    if (candidate.isEmpty())
                        candidate = program;
                    else if (groupSize == 1 ? windowsOf.value(program).size() < windowsOf.value(candidate).size()
                                            : chosen(program) > chosen(candidate))
                        candidate = program;
                    else if (groupSize == 1 && windowsOf.value(program).size() == windowsOf.value(candidate).size()
                             && chosen(program) > chosen(candidate))
                        candidate = program;
                }
        if (candidate.isEmpty())
            break;
        m_grouped.remove(candidate);
    }

    m_buttons.clear();
    for (const QString &program : std::as_const(programs)) {
        const QList<int> &windows = windowsOf[program];
        if (m_grouped.contains(program))
            m_buttons.append({windows, true, {}});
        else
            for (int i : windows)
                m_buttons.append({{i}, false, {}});
    }
    // The taskband's scrollbar, as sub_102de3e computes it: when the buttons do not fit at
    // their least width, reserve SM_CXVSCROLL at the band's right and show one toolbar row (a page) at
    // a time; the scrollbar's arrows move to the next page.
    const int step = width + spacing;
    const int count = int(m_buttons.size());
    const int total = count > 0 ? count * step - spacing : 0;
    const auto place = [&](int columns, int page) {
        const int capacity = qMax(1, columns * rows);
        for (int i = 0; i < count; ++i) {
            if (i / capacity != page) {
                m_buttons[i].rect = QRect();
                continue;
            }
            const int inPage = i % capacity, column = inPage % columns, row = inPage / columns;
            QRect rect(bandStart + column * step, row * rowHeight, width, rowHeight);
            if (!renderer)
                rect = QRect(rect.x(), rect.y() + theme()->scaled(4), width, rowHeight - theme()->scaled(6));
            m_buttons[i].rect = rect;
        }
    };
    m_scrollRect = m_scrollUp = m_scrollDown = QRect();
    m_scrollPages = 1;
    if (total > effectiveBand && count > 0) {
        const int scrollbar = theme()->scaled(renderer ? SCROLL_WIDTH : CLASSIC_SCROLL_WIDTH);
        const int view = qMax(0, band - scrollbar);
        int columns = qMax(1, view / step);
        while (columns > 1 && columns * step - spacing > view)
            --columns;
        m_scrollColumns = qMax(1, columns * rows);
        m_scrollPages = (count + m_scrollColumns - 1) / m_scrollColumns;
        m_scrollPage = qBound(0, m_scrollPage, m_scrollPages - 1);
        m_scrollRect = QRect(bandStart + view, 0, scrollbar, barHeight());
        const int arrow = qMin(scrollbar, qMax(1, barHeight() / 2));
        m_scrollUp = QRect(m_scrollRect.x(), 0, scrollbar, arrow);
        m_scrollDown = QRect(m_scrollRect.x(), barHeight() - arrow, scrollbar, arrow);
        place(columns, m_scrollPage);
    } else {
        m_scrollPage = 0;
        m_scrollColumns = qMax(1, count);
        place(m_scrollColumns, 0);
    }
}

QRect Taskbar::buttonRect(quint32 window) const
{
    const QList<Task> &tasks = m_windows->tasks();
    for (const Button &button : m_buttons)
        for (int i : button.tasks)
            if (i < tasks.size() && tasks[i].window == window && !button.rect.isNull())
                return QRect(mapToGlobal(button.rect.topLeft() + QPoint(0, m_top)), button.rect.size());
    return QRect();
}

void Taskbar::drawShadowedText(QPainter &painter, const QRect &rect, const QString &text)
{
    w4w::Renderer *renderer = theme()->renderer();
    QPoint offset(2, 2);
    QSize size;
    if (w4w::parseSize(renderer->property(START_BUTTON, QStringLiteral("textshadowoffset")).value_or(QString()), &size))
        offset = QPoint(size.width(), size.height());
    const QColor shadow = renderer->colour(START_BUTTON, QStringLiteral("textshadowcolor")).value_or(Qt::black);
    const QColor colour = renderer->colour(START_BUTTON, QStringLiteral("textcolor")).value_or(Qt::white);

    ShellTheme::drawShadowedText(painter, rect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, text, colour, shadow, offset);
    // XP's "start" is the face emboldened by half a pixel (a fifth more ink than the plain face, in its
    // width): the words again a pixel to the right, at three quarters of their strength.
    painter.save();
    painter.setOpacity(0.75);
    painter.drawText(rect.translated(1, 0), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, text);
    painter.restore();
}

void Taskbar::drawStart(QPainter &painter)
{
    int state = Normal;
    if (m_startPanel || m_pressed.kind == Hit::Start)
        state = Pressed;
    else if (m_hover.kind == Hit::Start)
        state = Hot;
    w4w::Renderer *renderer = theme()->renderer();
    if (!renderer) {
        const bool down = state == Pressed;
        theme()->raised(painter, m_startRect, down);
        const int shift = down ? 1 : 0;
        painter.drawImage(m_startRect.x() + theme()->scaled(4) + shift, m_startRect.y() + (m_startRect.height() - m_smallFlag.height()) / 2 + shift, m_smallFlag);
        painter.setFont(m_startFont);
        painter.setPen(Qt::black);
        painter.drawText(m_startRect.adjusted(theme()->scaled(4 + ICON_SIZE + 3) + shift, shift, 0, 0), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, m_classicStartText);
        return;
    }
    const Part part = withState(START_BUTTON, state);
    renderer->draw(&painter, m_startRect, part);
    const QRect content = renderer->contentRect(part, m_startRect);
    painter.drawImage(content.x(), (barHeight() - m_flag.height()) / 2, m_flag);
    painter.setFont(m_startFont);
    // (A pixel above the middle, as XP's)
    drawShadowedText(painter, QRect(content.x() + m_flag.width() + theme()->scaled(START_TEXT_X), -1, content.width() - m_flag.width(), barHeight()), m_startText);
}

void Taskbar::drawButton(QPainter &painter, int index)
{
    const Button &button = m_buttons[index];
    const QRect rect = button.rect;
    if (rect.isEmpty())
        return;
    const QList<Task> &all = m_windows->tasks();
    const Task &task = all[button.tasks.first()];
    bool active = false, attention = false;
    for (int i : button.tasks) {
        active = active || all[i].window == m_windows->active();
        attention = attention || all[i].attention;
    }
    const bool hover = m_hover.kind == Hit::Task && m_hover.index == index;
    const bool pressed = m_pressed.kind == Hit::Task && m_pressed.index == index;
    int state = active ? (hover ? HotChecked : Checked) : (hover ? Hot : Normal);
    if (pressed)
        state = Pressed;
    w4w::Renderer *renderer = theme()->renderer();
    if (renderer) {
        Part part = withState(TASK_BUTTON, state);
        if (attention && !active && m_flashOn)
            part = TASK_FLASH;
        renderer->draw(&painter, rect, part);
    } else {
        const bool down = active || pressed;
        if (attention && !active && m_flashOn)
            painter.fillRect(rect, theme()->system(QStringLiteral("highlight"), Qt::darkBlue));
        else if (active) {
            // Windows Classic's active button: the face and white, in a checker
            painter.fillRect(rect, QBrush(Qt::white));
            painter.fillRect(rect, QBrush(theme()->system(QStringLiteral("btnface"), Qt::lightGray), Qt::Dense4Pattern));
        }
        theme()->raised(painter, rect, down);
    }

    const int down = pressed || active ? 1 : 0;
    const int top = renderer ? rect.y() + theme()->scaled(TASK_ICON_Y) : rect.y() + (rect.height() - theme()->scaled(ICON_SIZE)) / 2;
    const QImage icon = task.icon.isNull() ? theme()->defaultIcon(theme()->scaled(ICON_SIZE)) : task.icon;
    const int iconX = renderer ? theme()->scaled(TASK_ICON_X) : theme()->scaled(4);
    if (!icon.isNull())
        painter.drawImage(rect.x() + iconX + down, top + down, icon);
    const int textRaise = down - theme()->scaled(renderer ? TEXT_RAISE : 0);
    const QColor text = renderer ? renderer->colour(TASK_TOOLBAR, QStringLiteral("textcolor")).value_or(Qt::white)
                                 : QColor(attention && !active && m_flashOn ? Qt::white : Qt::black);
    int textX = rect.x() + iconX + theme()->scaled(ICON_SIZE + TASK_TEXT_GAP) + down;
    int textEnd = rect.right() - theme()->scaled(renderer ? TASK_TEXT_END : 4);
    QString words = task.title;
    if (button.group) {
        // explorer.exe draws the count centred in the gap before the words, as wide as the count with
        // a digit more, in TaskBand.GroupCount's colours; the words are the program's name, then
        // comctl32's arrow of a button with a whole drop-down.
        const QString count = QString::number(button.tasks.size());
        const int box = QFontMetrics(m_groupCountFont).horizontalAdvance(count + QLatin1Char('0'));
        const QRect countRect(rect.x() + iconX + theme()->scaled(ICON_SIZE) + down, rect.y() + textRaise, box, rect.height());
        painter.setFont(m_groupCountFont);
        painter.setPen(renderer ? renderer->colour(GROUP_COUNT, QStringLiteral("textcolor")).value_or(QColor(255, 204, 109)) : text);
        painter.drawText(countRect, Qt::AlignCenter | Qt::TextSingleLine, count);
        textX = countRect.right() + 1;
        // The drop-down arrow: Marlett's '6' in a 13-pixel rect at the button's right (not checked against
        // XP, whose explorer never groups under Wine).
        const int arrowWidth = theme()->scaled(GROUP_ARROW_WIDTH);
        const QRect arrowRect(textEnd - arrowWidth + 1 + down, rect.y() + textRaise, arrowWidth, rect.height() - textRaise);
        QFont glyphs(QStringLiteral("Marlett"));
        glyphs.setPixelSize(qMax(1, qMin(arrowWidth, arrowRect.height()) - 2));
        painter.setFont(glyphs);
        painter.setPen(text);
        painter.drawText(arrowRect, Qt::AlignCenter, QStringLiteral("6"));
        textEnd = arrowRect.left() - theme()->scaled(GROUP_ARROW_GAP) - down;
        words = task.appName;
    }
    const int textWidth = textEnd - textX;
    if (textWidth <= 0)
        return;
    const QFont &font = active ? m_activeTaskFont : m_taskFont;
    painter.setFont(font);
    painter.setPen(text);
    const QString title = QFontMetrics(font).elidedText(words, Qt::ElideRight, textWidth);
    painter.drawText(QRect(textX, rect.y() + textRaise, textWidth, rect.height()), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, title);
}

void Taskbar::scrollStep(int delta)
{
    if (m_scrollRect.isNull())
        return;
    const int page = qBound(0, m_scrollPage + delta, m_scrollPages - 1);
    if (page == m_scrollPage)
        return;
    m_scrollPage = page;
    relayout();
    update();
}

QRect Taskbar::scrollThumbRect() const
{
    if (m_scrollRect.isNull() || m_scrollPages <= 1)
        return QRect();
    const QRect track(m_scrollRect.x(), m_scrollUp.bottom() + 1, m_scrollRect.width(),
                      qMax(0, m_scrollDown.top() - m_scrollUp.bottom() - 1));
    const int height = qMax(8, track.height() / m_scrollPages);
    const int y = track.y() + (track.height() - height) * m_scrollPage / qMax(1, m_scrollPages - 1);
    return QRect(track.x(), y, track.width(), height);
}

void Taskbar::drawScrollbar(QPainter &painter)
{
    if (m_scrollRect.isNull())
        return;
    w4w::Renderer *renderer = theme()->renderer();
    const auto arrow = [&](const QRect &rect, bool down, bool enabled) {
        const Hit hit{down ? Hit::ScrollDown : Hit::ScrollUp, -1};
        // ABS_UPNORMAL to ABS_UPDISABLED are 1 to 4; ABS_DOWNNORMAL to ABS_DOWNDISABLED 5 to 8.
        int state = enabled ? (m_pressed == hit ? 3 : m_hover == hit ? 2 : 1) : 4;
        if (renderer) {
            const Part part{QStringLiteral("scrollbar"), QStringLiteral("arrowbtn"), {}, (down ? 5 : 1) + state - 1, QStringLiteral("taskband")};
            if (renderer->draw(&painter, rect, part))
                return;
        }
        theme()->raised(painter, rect, state == 3);
        const QColor colour = theme()->system(state == 4 ? QStringLiteral("graytext") : QStringLiteral("btntext"), Qt::black);
        // a 7 x 4 triangle, the standard scrollbar's
        for (int row = 0; row < 4; ++row) {
            const int width = 7 - 2 * row;
            const int x = rect.center().x() - width / 2;
            const int y = down ? rect.center().y() - 1 + row : rect.center().y() + 2 - row;
            painter.fillRect(x, y, width, 1, colour);
        }
    };
    arrow(m_scrollUp, false, m_scrollPage > 0);
    arrow(m_scrollDown, true, m_scrollPage < m_scrollPages - 1);
    const QRect thumb = scrollThumbRect();
    if (!thumb.isNull()) {
        const Hit hit{Hit::ScrollThumb, -1};
        const int state = m_scrollDragging || m_pressed == hit ? 3 : m_hover == hit ? 2 : 1;
        if (renderer) {
            const Part part{QStringLiteral("scrollbar"), QStringLiteral("thumbbtn"), {}, state, QStringLiteral("taskband")};
            if (renderer->draw(&painter, thumb, part))
                return;
        }
        theme()->raised(painter, thumb, state == 3);
    }
}

void Taskbar::wheelEvent(QWheelEvent *event)
{
    if (!m_scrollRect.isNull() && m_bandRect.contains(event->position().toPoint() - QPoint(0, m_top))) {
        const int delta = event->angleDelta().y() != 0 ? event->angleDelta().y() : -event->angleDelta().x();
        if (delta != 0) {
            scrollStep(delta < 0 ? 1 : -1);
            event->accept();
            return;
        }
    }
    QWidget::wheelEvent(event);
}

void Taskbar::drawTray(QPainter &painter)
{
    w4w::Renderer *renderer = theme()->renderer();
    if (renderer)
        renderer->draw(&painter, m_trayRect, TRAY_BACKGROUND);
    else
        theme()->sunken(painter, m_trayRect);
    const QList<TrayIcon> &icons = m_tray->icons();
    for (int i = 0; i < icons.size() && i < m_trayIconRects.size(); ++i)
        if (!m_trayIconRects[i].isNull())
            painter.drawImage(m_trayIconRects[i], icons[i].image);
    // Win+B: the icon with the keyboard has the toolbar's focus rectangle, DrawFocusRect on the
    // button's own 18-pixel rect (not measured against XP).
    if (m_keyboard && m_hover.kind == Hit::TrayIcon && m_hover.index < m_trayIconRects.size()) {
        const QRect focus = m_trayIconRects[m_hover.index];
        painter.save();
        painter.setCompositionMode(QPainter::RasterOp_SourceXorDestination);
        for (int x = focus.left(); x <= focus.right(); ++x)
            for (int y = focus.top(); y <= focus.bottom(); ++y)
                if ((x == focus.left() || x == focus.right() || y == focus.top() || y == focus.bottom()) && (x + y) % 2 == 0)
                    painter.fillRect(x, y, 1, 1, Qt::white);
        painter.restore();
    }
    if (!m_chevronRect.isNull()) {
        const Hit hit{Hit::Chevron, -1};
        const int state = m_pressed == hit && m_hover == hit ? Pressed : m_hover == hit ? Hot : Normal;
        if (!renderer || !renderer->draw(&painter, m_chevronRect, withState(m_trayOpen ? TRAY_CHEVRON_OPEN : TRAY_CHEVRON, state))) {
            // Windows Classic: the notification area's flat toolbar button, an edge only while hot or pressed
            if (state != Normal)
                theme()->raised(painter, m_chevronRect, state == Pressed);
            painter.setPen(theme()->system(QStringLiteral("btntext"), Qt::black));
            painter.drawText(m_chevronRect, Qt::AlignCenter, m_trayOpen ? QStringLiteral("\u00bb") : QStringLiteral("\u00ab"));
        }
    }
    painter.setFont(m_clockFont);
    painter.setPen(renderer ? renderer->colour(CLOCK, QStringLiteral("textcolor")).value_or(Qt::white) : QColor(Qt::black));
    if (m_settings.clock)
        painter.drawText(m_clockRect.translated(0, renderer ? -theme()->scaled(TEXT_RAISE) : 0), Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextSingleLine, m_clock);
}

void Taskbar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    if (m_top && theme()->renderer())
        theme()->renderer()->draw(&painter, QRect(0, 0, width(), m_top), SIZING_BAR);
    painter.translate(0, m_top);
    const QRect bar(0, 0, width(), barHeight());
    if (w4w::Renderer *renderer = theme()->renderer()) {
        renderer->draw(&painter, bar, BACKGROUND);
    } else {
        painter.fillRect(bar, theme()->system(QStringLiteral("btnface"), Qt::lightGray));
        painter.setPen(theme()->system(QStringLiteral("btnhighlight"), Qt::white));
        painter.drawLine(0, 1, width(), 1);
    }
    for (const QRect &gripper : std::as_const(m_grippers)) {
        w4w::Renderer *renderer = theme()->renderer();
        // the art at its own width at the header's left, tiled down the band (TaskbarGripper.bmp is 6)
        const int art = renderer ? renderer->trueSize(GRIPPER).width() : 0;
        if (!renderer || art <= 0 || !renderer->draw(&painter, QRect(gripper.x(), gripper.y(), art, gripper.height()), GRIPPER)) {
            // Windows Classic's: a raised bar 3 wide, 2 from the band's left
            const QRect line(gripper.x() + 2, gripper.y() + 2, 3, gripper.height() - 4);
            theme()->raised(painter, line, false);
        }
    }
    drawStart(painter);
    if (!m_quickTitle.isNull()) {
        const QColor title = theme()->renderer()
            ? theme()->renderer()->colour(REBAR, QStringLiteral("textcolor")).value_or(QColor(123, 182, 232))
            : theme()->system(QStringLiteral("btntext"), Qt::black);
        painter.setPen(title);
        painter.setFont(m_taskFont);
        painter.drawText(m_quickTitle.adjusted(0, -theme()->scaled(TEXT_RAISE), 0, 0), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                         QFileInfo(quickLaunchFolder()).fileName());
    }
    for (int i = 0; i < m_quickItems.size(); ++i) {
        const QuickItem &item = m_quickItems[i];
        if (item.rect.isNull())
            continue;
        const Hit hit{Hit::Quick, i};
        const bool pressed = m_pressed == hit && m_hover == hit;
        if (pressed || m_hover == hit) {
            w4w::Renderer *renderer = theme()->renderer();
            if (!renderer || !renderer->draw(&painter, item.rect, withState(QUICK_BUTTON, pressed ? Pressed : Hot))) {
                // Windows Classic's flat toolbar: a raised edge under the pointer, sunk when pressed
                const QColor light = theme()->system(QStringLiteral("btnhighlight"), Qt::white);
                const QColor dark = theme()->system(QStringLiteral("btnshadow"), Qt::gray);
                painter.setPen(pressed ? dark : light);
                painter.drawLine(item.rect.topLeft(), item.rect.topRight());
                painter.drawLine(item.rect.topLeft(), item.rect.bottomLeft());
                painter.setPen(pressed ? light : dark);
                painter.drawLine(item.rect.bottomLeft(), item.rect.bottomRight());
                painter.drawLine(item.rect.topRight(), item.rect.bottomRight());
            }
        }
        const int shift = pressed ? 1 : 0;
        if (m_settings.quickLaunchText) {
            const QColor text = theme()->renderer()
                ? theme()->renderer()->colour(QUICK_TOOLBAR, QStringLiteral("textcolor")).value_or(QColor(Qt::white))
                : theme()->system(QStringLiteral("btntext"), Qt::black);
            painter.setPen(text);
            painter.setFont(m_taskFont);
            const bool large = m_settings.quickLaunchLargeIcons;
            const int icon = large ? QUICK_LARGE_ICON : ICON_SIZE;
            const int iconX = large ? QUICK_LARGE_ICON_X : QUICK_ICON_X;
            const int left = theme()->scaled(iconX + icon + QUICK_TEXT_GAP);
            painter.drawText(item.rect.adjusted(left + shift, shift - theme()->scaled(TEXT_RAISE), 0, 0), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                             item.name);
        }
        const bool large = m_settings.quickLaunchLargeIcons;
        const int icon = theme()->scaled(large ? QUICK_LARGE_ICON : ICON_SIZE);
        const QPoint at = theme()->renderer()
            ? item.rect.topLeft() + QPoint(theme()->scaled(large ? QUICK_LARGE_ICON_X : QUICK_ICON_X), theme()->scaled(large ? QUICK_LARGE_ICON_Y : QUICK_ICON_Y))
            : item.rect.topLeft() + QPoint((item.rect.width() - icon) / 2, (item.rect.height() - icon) / 2);
        painter.drawImage(QRect(at + QPoint(shift, shift), QSize(icon, icon)), item.icon);
    }
    for (int i = 0; i < m_desktopItems.size(); ++i) {
        const QuickItem &item = m_desktopItems[i];
        if (item.rect.isNull())
            continue;
        const Hit hit{Hit::Desktop, i};
        const bool pressed = m_pressed == hit && m_hover == hit;
        if (pressed || m_hover == hit) {
            if (w4w::Renderer *renderer = theme()->renderer()) {
                if (!renderer->draw(&painter, item.rect, withState(QUICK_BUTTON, pressed ? Pressed : Hot)))
                    theme()->raised(painter, item.rect, pressed);
            } else {
                theme()->raised(painter, item.rect, pressed);
            }
        }
        const int shift = pressed ? 1 : 0;
        const QPoint at = item.rect.topLeft() + QPoint((item.rect.width() - theme()->scaled(ICON_SIZE)) / 2,
                                                       (item.rect.height() - theme()->scaled(ICON_SIZE)) / 2);
        painter.drawImage(QRect(at + QPoint(shift, shift), QSize(theme()->scaled(ICON_SIZE), theme()->scaled(ICON_SIZE))), item.icon);
    }
    for (int i = 0; i < m_customItems.size(); ++i) {
        const QuickItem &item = m_customItems[i];
        if (item.rect.isNull())
            continue;
        const Hit hit{Hit::Custom, i};
        const bool pressed = m_pressed == hit && m_hover == hit;
        if (pressed || m_hover == hit) {
            if (w4w::Renderer *renderer = theme()->renderer()) {
                if (!renderer->draw(&painter, item.rect, withState(QUICK_BUTTON, pressed ? Pressed : Hot)))
                    theme()->raised(painter, item.rect, pressed);
            } else {
                theme()->raised(painter, item.rect, pressed);
            }
        }
        const int shift = pressed ? 1 : 0;
        const QPoint at = item.rect.topLeft() + QPoint((item.rect.width() - theme()->scaled(ICON_SIZE)) / 2,
                                                       (item.rect.height() - theme()->scaled(ICON_SIZE)) / 2);
        painter.drawImage(QRect(at + QPoint(shift, shift), QSize(theme()->scaled(ICON_SIZE), theme()->scaled(ICON_SIZE))), item.icon);
    }
    for (int i = 0; i < m_linksItems.size(); ++i) {
        const QuickItem &item = m_linksItems[i];
        if (item.rect.isNull())
            continue;
        const Hit hit{Hit::Link, i};
        const bool pressed = m_pressed == hit && m_hover == hit;
        if (pressed || m_hover == hit) {
            if (w4w::Renderer *renderer = theme()->renderer()) {
                if (!renderer->draw(&painter, item.rect, withState(QUICK_BUTTON, pressed ? Pressed : Hot)))
                    theme()->raised(painter, item.rect, pressed);
            } else {
                theme()->raised(painter, item.rect, pressed);
            }
        }
        const int shift = pressed ? 1 : 0;
        const QPoint at = item.rect.topLeft() + QPoint((item.rect.width() - theme()->scaled(ICON_SIZE)) / 2,
                                                       (item.rect.height() - theme()->scaled(ICON_SIZE)) / 2);
        painter.drawImage(QRect(at + QPoint(shift, shift), QSize(theme()->scaled(ICON_SIZE), theme()->scaled(ICON_SIZE))), item.icon);
    }
    for (int i = 0; i < m_buttons.size(); ++i)
        drawButton(painter, i);
    drawScrollbar(painter);
    drawTray(painter);
}

Taskbar::Hit Taskbar::hitAt(QPoint at) const
{
    at.ry() -= m_top;   // the bar's own rows start under the sizing bar
    Hit hit;
    if (m_startRect.contains(at)) {
        hit.kind = Hit::Start;
        return hit;
    }
    for (int i = 0; i < m_quickItems.size(); ++i)
        if (m_quickItems[i].rect.contains(at)) {
            hit.kind = Hit::Quick;
            hit.index = i;
            return hit;
        }
    if (m_quickBand.contains(at)) {
        hit.kind = Hit::QuickBand;
        return hit;
    }
    for (int i = 0; i < m_desktopItems.size(); ++i)
        if (m_desktopItems[i].rect.contains(at)) {
            hit.kind = Hit::Desktop;
            hit.index = i;
            return hit;
        }
    for (int i = 0; i < m_customItems.size(); ++i)
        if (m_customItems[i].rect.contains(at)) {
            hit.kind = Hit::Custom;
            hit.index = i;
            return hit;
        }
    for (int i = 0; i < m_linksItems.size(); ++i)
        if (m_linksItems[i].rect.contains(at)) {
            hit.kind = Hit::Link;
            hit.index = i;
            return hit;
        }
    for (int i = 0; i < m_buttons.size(); ++i)
        if (m_buttons[i].rect.contains(at)) {
            hit.kind = Hit::Task;
            hit.index = i;
            return hit;
        }
    if (m_scrollUp.contains(at)) {
        hit.kind = Hit::ScrollUp;
        return hit;
    }
    if (m_scrollDown.contains(at)) {
        hit.kind = Hit::ScrollDown;
        return hit;
    }
    if (!m_scrollRect.isNull() && m_scrollRect.contains(at)) {
        const QRect thumb = scrollThumbRect();
        if (!thumb.isNull() && thumb.contains(at))
            hit.kind = Hit::ScrollThumb;
        else
            hit.kind = at.y() < scrollThumbRect().center().y() ? Hit::ScrollTrackUp : Hit::ScrollTrackDown;
        return hit;
    }
    if (m_chevronRect.contains(at)) {
        hit.kind = Hit::Chevron;
        return hit;
    }
    for (int i = 0; i < m_trayIconRects.size(); ++i)
        if (m_trayIconRects[i].contains(at)) {
            hit.kind = Hit::TrayIcon;
            hit.index = i;
            return hit;
        }
    if (m_clockRect.contains(at))
        hit.kind = Hit::Clock;
    else if (!m_startRect.contains(at) && !m_trayRect.contains(at))
        hit.kind = Hit::Band;
    return hit;
}

void Taskbar::setHover(Hit hit)
{
    if (hit == m_hover)
        return;
    m_hover = hit;
    update();
}

void Taskbar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_scrollDragging && (event->buttons() & Qt::LeftButton)) {
        const QRect track(m_scrollRect.x(), m_scrollUp.bottom() + 1, m_scrollRect.width(),
                          qMax(1, m_scrollDown.top() - m_scrollUp.bottom() - 1));
        const int step = qMax(1, track.height() / qMax(1, m_scrollPages));
        const int page = qBound(0, m_scrollDragPage + (event->globalPosition().toPoint().y() - m_scrollDragFromY) / step,
                                m_scrollPages - 1);
        if (page != m_scrollPage) {
            m_scrollPage = page;
            relayout();
            update();
        }
        return;
    }
    if (m_hidden) {
        // the tray's test: farther than SM_CXDOUBLECLK x SM_CYDOUBLECLK from where it was
        const QPoint moved = event->globalPosition().toPoint() - m_hiddenMouse;
        if (moved.x() * moved.x() + moved.y() * moved.y() > 4 * 4 && !m_unhideTimer.isActive()) {
            m_hiddenMouse = event->globalPosition().toPoint();
            m_unhideTimer.start();
        }
        return;
    }
    setHover(hitAt(event->position().toPoint()));
}

void Taskbar::leaveEvent(QEvent *)
{
    setHover(Hit());
    // TrayNotify's timer 3 (sub_1021370): the hidden icons shown go back 5 s after the pointer has left
    if (m_trayOpen)
        m_trayCollapseTimer.start();
}

void Taskbar::mousePressEvent(QMouseEvent *event)
{
    leaveKeyboard();
    const Hit hit = hitAt(event->position().toPoint());
    // The open chevron closes on a click elsewhere on the bar (not checked against XP).
    if (m_trayOpen && hit.kind != Hit::TrayIcon && hit.kind != Hit::Chevron && hit.kind != Hit::Clock) {
        m_trayOpen = false;
        relayout();
        update();
    }
    m_activeAtPress = m_windows->activeAtPress();
    // The sizing bar moves the unlocked bar between the bottom and top edges.
    if (event->button() == Qt::LeftButton && !m_settings.locked && event->position().y() < m_top) {
        m_movingBar = true;
        m_barDragFromY = event->globalPosition().toPoint().y();
        m_barRowsStart = m_settings.rows;
        return;
    }
    if (event->button() == Qt::LeftButton && hit.kind == Hit::Start) {
        // The press that closes the open menu must not open it again.
        if (!m_startClosed.isValid() || m_startClosed.elapsed() > 200)
            showStartMenu(false);
        return;
    }
    if (event->button() == Qt::LeftButton && (hit.kind == Hit::Task || hit.kind == Hit::Quick || hit.kind == Hit::Desktop || hit.kind == Hit::Custom || hit.kind == Hit::Link || hit.kind == Hit::Chevron
                                               || hit.kind == Hit::ScrollUp || hit.kind == Hit::ScrollDown || hit.kind == Hit::ScrollThumb
                                               || hit.kind == Hit::ScrollTrackUp || hit.kind == Hit::ScrollTrackDown)) {
        m_pressed = hit;
        update();
        if (hit.kind == Hit::ScrollUp || hit.kind == Hit::ScrollDown) {
            scrollStep(hit.kind == Hit::ScrollUp ? -1 : 1);
            m_scrollRepeat.start();
        } else if (hit.kind == Hit::ScrollThumb) {
            m_scrollDragging = true;
            m_scrollDragFromY = event->globalPosition().toPoint().y();
            m_scrollDragPage = m_scrollPage;
        } else if (hit.kind == Hit::ScrollTrackUp || hit.kind == Hit::ScrollTrackDown) {
            scrollStep(hit.kind == Hit::ScrollTrackUp ? -1 : 1);
        }
    }
}

void Taskbar::mouseDoubleClickEvent(QMouseEvent *event)
{
    mousePressEvent(event);
}

void Taskbar::mouseReleaseEvent(QMouseEvent *event)
{
    const QPoint at = event->position().toPoint();
    const Hit hit = hitAt(at);
    const QPoint inBar = at - QPoint(0, m_top);
    const Hit pressed = m_pressed;
    m_pressed = Hit();
    m_scrollDragging = false;
    m_scrollRepeat.stop();
    update();
    if (m_movingBar) {
        m_movingBar = false;
        const QRect monitor = QGuiApplication::primaryScreen()->geometry();
        const int y = event->globalPosition().toPoint().y();
        const int position = y < monitor.center().y() ? 1 : 0;
        // Dragging across the screen moves the bar; dragging from its edge resizes it.
        const int row = theme()->scaled(theme()->classic() ? CLASSIC_BAR_HEIGHT : BAR_HEIGHT);
        const int delta = y - m_barDragFromY;
        const int rows = qBound(1, m_barRowsStart + (m_settings.position == 1 ? delta : -delta) / qMax(1, row), 3);
        TaskbarSettings settings = TaskbarSettings::load();
        bool changed = false;
        const bool move = position != m_settings.position && qAbs(delta) > row;
        if (move) {
            settings.position = position;
            m_settings.position = position;
            changed = true;
        } else if (qAbs(delta) >= row / 2 && rows != m_barRowsStart) {
            settings.rows = rows;
            m_settings.rows = rows;
            changed = true;
        }
        if (changed) {
            settings.save();
            placeOnScreen();
        }
        return;
    }
    if (hit.kind == Hit::Desktop && hit.index < m_desktopItems.size()) {
        if (event->button() == Qt::LeftButton && pressed == hit)
            runDesktopItem(hit.index);
        return;
    }
    if (hit.kind == Hit::Custom && hit.index < m_customItems.size()) {
        if (event->button() == Qt::LeftButton && pressed == hit)
            runCustomItem(hit.index);
        return;
    }
    if (hit.kind == Hit::Link && hit.index < m_linksItems.size()) {
        if (event->button() == Qt::LeftButton && pressed == hit)
            runLinkItem(hit.index);
        return;
    }
    if (hit.kind == Hit::Quick && hit.index < m_quickItems.size()) {
        if (event->button() == Qt::LeftButton && pressed == hit)
            runQuickItem(hit.index);
        else if (event->button() == Qt::RightButton)
            showQuickItemMenu(hit.index, event->globalPosition().toPoint());
        return;
    }
    if (hit.kind == Hit::Chevron) {
        if (event->button() == Qt::LeftButton && pressed == hit) {
            m_trayOpen = !m_trayOpen;
            relayout();
            update();
        } else if (event->button() == Qt::RightButton) {
            showTaskbarMenu(event->globalPosition().toPoint(), true);
        }
        return;
    }
    if (hit.kind == Hit::QuickBand && event->button() == Qt::RightButton) {
        showTaskbarMenu(event->globalPosition().toPoint(), false, true);
        return;
    }
    if (hit.kind == Hit::Task && hit.index < m_buttons.size()) {
        const Button &button = m_buttons[hit.index];
        const QPoint global = event->globalPosition().toPoint();
        if (button.group) {
            if (event->button() == Qt::RightButton)
                showGroupMenu(hit.index, global);
            else if (event->button() == Qt::LeftButton && pressed == hit)
                showGroupList(hit.index);
            return;
        }
        const Task &task = m_windows->tasks()[button.tasks.first()];
        if (event->button() == Qt::RightButton) {
            showTaskMenu(button.tasks.first(), global);
        } else if (event->button() == Qt::LeftButton && pressed == hit) {
            if (task.window == m_activeAtPress && !task.minimized) {
                const xcb_window_t window = task.window;
                WindowList::minimize(window);
                m_windows->activateTopmost(window);
            } else {
                m_windows->switchTo(task.window);
            }
        }
    } else if ((hit.kind == Hit::Band || hit.kind == Hit::Clock || (hit.kind == Hit::None && m_trayRect.contains(inBar)))
               && event->button() == Qt::RightButton) {
        // The notification area's, the clock's included, has the date's and the icons' items too.
        showTaskbarMenu(event->globalPosition().toPoint(), hit.kind != Hit::Band);
    } else if (hit.kind == Hit::TrayIcon && hit.index < m_tray->icons().size()) {
        const quint32 id = m_tray->icons()[hit.index].id;
        m_trayActive.insert(id, QDateTime::currentMSecsSinceEpoch());   // a click keeps it shown
        const QPoint global = event->globalPosition().toPoint();
        if (event->button() == Qt::LeftButton)
            m_tray->click(id, "activate", global);
        else if (event->button() == Qt::RightButton)
            m_tray->click(id, "context", global);
        else if (event->button() == Qt::MiddleButton)
            m_tray->click(id, "secondary", global);
    }
}

bool Taskbar::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(event);
        const Hit hit = hitAt(help->pos());
        QString text;
        if (hit.kind == Hit::Task && hit.index < m_buttons.size()) {
            const Button &button = m_buttons[hit.index];
            const Task &task = m_windows->tasks()[button.tasks.first()];
            text = button.group ? task.appName : task.title;
        }
        else if (hit.kind == Hit::TrayIcon && hit.index < m_tray->icons().size())
            text = m_tray->icons()[hit.index].tooltip;
        else if (hit.kind == Hit::Chevron)
            text = chevronTip();
        else if (hit.kind == Hit::Quick && hit.index < m_quickItems.size())
            text = m_quickItems[hit.index].name;
        else if (hit.kind == Hit::Start) {
            if (const auto explorer = xpfiles::resources(xpfiles::find(QStringLiteral("explorer.exe"))))
                text = xpfiles::string(*explorer, STRING_START_TIP);
        } else if (hit.kind == Hit::Clock)
            text = QLocale::system().toString(QDate::currentDate(), QLocale::LongFormat);
        if (text.isEmpty())
            QToolTip::hideText();
        else
            QToolTip::showText(help->globalPos(), text, this);
        return true;
    }
    return QWidget::event(event);
}

void Taskbar::showTaskMenu(int index, QPoint at)
{
    const Task task = m_windows->tasks()[index];
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const xcb_window_t window = task.window;
    menu->addAction(tr("&Restore"), this, [window] { WindowList::activate(window); })->setEnabled(task.minimized);
    menu->addAction(tr("Mi&nimize"), this, [window] { WindowList::minimize(window); })->setEnabled(!task.minimized);
    menu->addAction(tr("Ma&ximize"), this, [window] { WindowList::maximize(window, true); });
    menu->addSeparator();
    QAction *close = menu->addAction(tr("&Close"), this, [window] { closeWithForce(window); });
    QFont bold = close->font();
    bold.setBold(true);
    close->setFont(bold);
    menu->popup(m_settings.position == 1 ? at : at - QPoint(0, menu->sizeHint().height()));
}

void Taskbar::showGroupList(int index)
{
    const Button button = m_buttons[index];
    QList<GroupList::Item> items;
    for (int i : button.tasks) {
        const Task &task = m_windows->tasks()[i];
        items.append({task.window, task.title, task.icon.isNull() ? theme()->defaultIcon(theme()->scaled(ICON_SIZE)) : task.icon});
    }
    auto *list = new GroupList(items, button.rect.width(), [this](xcb_window_t window) { m_windows->switchTo(window); },
                               [this](xcb_window_t window, QPoint at) {
                                   const QList<Task> &tasks = m_windows->tasks();
                                   for (int i = 0; i < tasks.size(); ++i)
                                       if (tasks[i].window == window)
                                           showTaskMenu(i, at);
                               });
    list->popUp(QRect(mapToGlobal(button.rect.topLeft() + QPoint(0, m_top)), button.rect.size()));
}

void Taskbar::showGroupMenu(int index, QPoint at)
{
    QList<xcb_window_t> windows;
    for (int i : m_buttons[index].tasks)
        windows.append(m_windows->tasks()[i].window);
    // From the top of the stack down, as user32 arranges them
    QList<xcb_window_t> stacked;
    for (xcb_window_t window : m_windows->arrangeable())
        if (windows.contains(window))
            stacked.append(window);

    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    QHash<int, QAction *> actions;
    if (const auto explorer = xpfiles::resources(xpfiles::find(QStringLiteral("explorer.exe"))))
        actions = xpprogram::fillMenu(menu, *explorer, MENU_GROUP);
    if (actions.isEmpty()) {
        actions.insert(CMD_CASCADE, menu->addAction(tr("Ca&scade")));
        actions.insert(CMD_TILE_HORIZONTALLY, menu->addAction(tr("Tile &Horizontally")));
        actions.insert(CMD_TILE_VERTICALLY, menu->addAction(tr("Tile V&ertically")));
        actions.insert(CMD_MINIMIZE_GROUP, menu->addAction(tr("&Minimize Group")));
        menu->addSeparator();
        actions.insert(CMD_CLOSE_GROUP, menu->addAction(tr("&Close Group")));
    }
    for (int command : {CMD_CASCADE, CMD_TILE_HORIZONTALLY, CMD_TILE_VERTICALLY})
        if (QAction *action = actions.value(command))
            action->setEnabled(!stacked.isEmpty());
    connect(menu, &QMenu::triggered, this, [this, actions, windows, stacked](QAction *action) {
        switch (actions.key(action)) {
        case CMD_CASCADE:
            arrange::cascade(stacked);
            break;
        case CMD_TILE_HORIZONTALLY:
        case CMD_TILE_VERTICALLY:
            arrange::tile(stacked, actions.key(action) == CMD_TILE_HORIZONTALLY);
            break;
        case CMD_MINIMIZE_GROUP:
            for (xcb_window_t window : windows)
                WindowList::minimize(window);
            break;
        case CMD_CLOSE_GROUP:
            for (xcb_window_t window : windows)
                WindowList::close(window);
            break;
        }
    });
    menu->popup(m_settings.position == 1 ? at : at - QPoint(0, menu->sizeHint().height()));
}

void Taskbar::showTaskbarMenu(QPoint at, bool tray, bool band)
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const auto explorer = xpfiles::resources(xpfiles::find(QStringLiteral("explorer.exe")));
    if (!explorer)
        return;
    QHash<int, QAction *> actions = xpprogram::fillMenu(menu, *explorer, MENU_TASKBAR);
    if (actions.isEmpty())
        return;
    // As explorer.exe prepares menu 205: Adjust Date/Time, Customize Notifications and their line
    // only on the notification area; Undo names what it puts back, or goes; Show the Desktop becomes Show Open
    // Windows while the desktop shows; Cascade and Tile need a window to arrange.
    const auto remove = [&](int command) {
        if (QAction *action = actions.take(command))
            menu->removeAction(action);
    };
    if (!tray) {
        remove(CMD_ADJUST_TIME);
        remove(CMD_CUSTOMIZE_NOTIFICATIONS);
        const QList<QAction *> all = menu->actions();
        if (!all.isEmpty() && all.first()->isSeparator())
            menu->removeAction(all.first());
    } else if (QAction *customize = actions.value(CMD_CUSTOMIZE_NOTIFICATIONS)) {
        customize->setEnabled(m_settings.hideInactive);
        connect(customize, &QAction::triggered, this, [this] { traynotify::customize(this); });
    }
    if (m_undo.isEmpty() || !m_undoString) {
        remove(CMD_UNDO);
    } else if (QAction *undo = actions.value(CMD_UNDO)) {
        QString what = xpfiles::string(*explorer, m_undoString);
        undo->setText(xpfiles::string(*explorer, STRING_UNDO).replace(QLatin1String("%s"), what));
    }
    const QList<quint32> showing = x11::property32(x11::root(), x11::atom("_NET_SHOWING_DESKTOP"));
    const bool desktopShown = !showing.isEmpty() && showing.first();
    if (QAction *show = actions.value(CMD_SHOW_DESKTOP); show && desktopShown)
        show->setText(xpfiles::string(*explorer, STRING_SHOW_OPEN_WINDOWS));
    const QList<xcb_window_t> windows = m_windows->arrangeable();
    for (int command : {CMD_CASCADE, CMD_TILE_HORIZONTALLY, CMD_TILE_VERTICALLY})
        if (QAction *action = actions.value(command))
            action->setEnabled(!windows.isEmpty());
    // The Linux program that stands for the Task Manager, when there is one
    QString taskManager;
    for (const char *name : {"plasma-systemmonitor", "gnome-system-monitor", "ksysguard", "xfce4-taskmanager", "lxtask", "mate-system-monitor"})
        if (taskManager.isEmpty())
            taskManager = QStandardPaths::findExecutable(QLatin1String(name));
    if (QAction *action = actions.value(CMD_TASK_MANAGER))
        action->setEnabled(!taskManager.isEmpty());
    // Toolbars: browseui's band site menu (browselc.dll's 268) above the rest, as the tray puts it.
    // Of its bands this taskbar has Quick Launch; Desktop, New Toolbar and the deskbands are greyed.
    if (const auto browselc = xpfiles::resources(xpfiles::find(QStringLiteral("browselc.dll")))) {
        // the template's first popup holds the "&Toolbars" popup
        auto *holder = new QMenu(menu);
        const QHash<int, QAction *> bands = xpprogram::fillMenu(holder, *browselc, MENU_BANDS);
        QMenu *toolbars = holder->actions().isEmpty() ? nullptr : holder->actions().first()->menu();
        for (auto it = bands.begin(); it != bands.end(); ++it)
            if (!it.value()->menu())
                it.value()->setEnabled(it.key() == CMD_BAND_QUICK_LAUNCH || it.key() == CMD_BAND_DESKTOP || it.key() == CMD_BAND_NEW_TOOLBAR);
        if (QAction *newToolbar = bands.value(CMD_BAND_NEW_TOOLBAR)) {
            newToolbar->setEnabled(true);
            connect(newToolbar, &QAction::triggered, this, [this] {
                const QString folder = QFileDialog::getExistingDirectory(this, tr("New Toolbar"), QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
                if (folder.isEmpty())
                    return;
                TaskbarSettings settings = TaskbarSettings::load();
                settings.customToolbar = folder;
                settings.save();
            });
        }
        if (QAction *desktop = bands.value(CMD_BAND_DESKTOP)) {
            desktop->setCheckable(true);
            desktop->setChecked(m_settings.desktopBand);
            connect(desktop, &QAction::triggered, this, [](bool on) {
                TaskbarSettings settings = TaskbarSettings::load();
                settings.desktopBand = on;
                settings.save();
            });
        }
        if (QAction *quick = bands.value(CMD_BAND_QUICK_LAUNCH)) {
            quick->setCheckable(true);
            quick->setChecked(m_settings.quickLaunch);
            connect(quick, &QAction::triggered, this, [](bool on) {
                TaskbarSettings settings = TaskbarSettings::load();
                settings.quickLaunch = on;
                settings.save();
            });
        }
        if (toolbars) {
            auto *links = toolbars->addAction(tr("&Links"));
            links->setCheckable(true);
            links->setChecked(m_settings.linksBand);
            connect(links, &QAction::triggered, this, [](bool on) {
                TaskbarSettings settings = TaskbarSettings::load();
                settings.linksBand = on;
                settings.save();
            });
            QAction *first = menu->actions().value(0);
            menu->insertMenu(first, toolbars);
            menu->insertSeparator(first);
        }
        if (band)
            addBandItems(menu, *browselc);
    }
    // Lock the Taskbar: unlocked, the bar shows its sizing bar and grippers (it still cannot move)
    if (QAction *lock = actions.value(CMD_LOCK)) {
        lock->setCheckable(true);
        lock->setChecked(m_settings.locked);
        connect(lock, &QAction::triggered, this, [](bool on) {
            TaskbarSettings settings = TaskbarSettings::load();
            settings.locked = on;
            settings.save();
        });
    }

    connect(menu, &QMenu::triggered, this, [this, actions, windows, desktopShown, taskManager](QAction *action) {
        switch (actions.key(action)) {
        case CMD_ADJUST_TIME:
            applets::open(QStringLiteral("datetime"));
            break;
        case CMD_CASCADE:
            m_undo = arrange::cascade(windows);
            m_undoString = STRING_CASCADE;
            break;
        case CMD_TILE_HORIZONTALLY:
        case CMD_TILE_VERTICALLY:
            m_undo = arrange::tile(windows, actions.key(action) == CMD_TILE_HORIZONTALLY);
            m_undoString = STRING_TILE;
            break;
        case CMD_UNDO:
            // Show Desktop's undo also turns _NET_SHOWING_DESKTOP off.
            if (m_undoString == STRING_MINIMIZE_ALL)
                x11::sendToManager(x11::root(), "_NET_SHOWING_DESKTOP", {0});
            arrange::undo(m_undo);
            m_undo.clear();
            break;
        case CMD_SHOW_DESKTOP:
            toggleDesktop();
            break;
        case CMD_TASK_MANAGER:
            QProcess::startDetached(taskManager, {});
            break;
        case CMD_PROPERTIES:
            TaskbarProperties::showTab(TaskbarProperties::TaskbarTab);
            break;
        }
    });
    menu->popup(m_settings.position == 1 ? at : at - QPoint(0, menu->sizeHint().height()));
}

void Taskbar::showStartMenu(bool keyboard)
{
    if (m_startBalloon) {
        m_startBalloon->close();
        TaskbarSettings settings = TaskbarSettings::load();
        if (settings.startButtonBalloonTip == 0) {
            settings.startButtonBalloonTip = 1;
            settings.save();
        }
    }
    if (m_startPanel) {
        m_startPanel->close();
        return;
    }
    // the panel stands on the shown bar
    if (m_hidden || m_slideTimer.isActive())
        setHidden(false, false);
    // On XP's Luna taskbar the panel stands on the bar's top; under Windows Classic, on the Start button's
    // top left corner (2 in, over the bar's top 4 rows).
    auto *panel = new StartPanel(mapToGlobal(theme()->renderer() ? QPoint(0, 0) : m_startRect.topLeft()), [this] {
        if (sessiondialog::askLogOff())
            endSession("logoff");
    }, [this] { turnOff(); },
                                 [] { FileBrowser::open(places::ControlPanel); });
    panel->setAttribute(Qt::WA_DeleteOnClose);
    connect(panel, &StartPanel::closed, this, [this] {
        m_startClosed.start();
        update();
    });
    m_startPanel = panel;
    if (m_settings.position == 1)
        panel->move(panel->x(), geometry().bottom() + 1);
    panel->setKeyboardCues(keyboard);
    panel->show();
    update();
}

void Taskbar::turnOff()
{
    // Once the Start menu has gone: the screen fading behind the dialog is a picture of it.
    QTimer::singleShot(TURN_OFF_DELAY_MS, this, [this] {
        const bool power = qEnvironmentVariable("WIN4WINE_POWER") == QLatin1String("1");
        const sessiondialog::Choice choice = sessiondialog::askTurnOff(power);
        if (choice == sessiondialog::TurnOff)
            endSession("shutdown");
        else if (choice == sessiondialog::Restart)
            endSession("reboot");
        else if (choice == sessiondialog::StandBy)
            QProcess::startDetached(QStringLiteral("systemctl"), {QStringLiteral("suspend")});
    });
}

void Taskbar::endSession(const char *request)
{
    sounds::playAndWait(qstrcmp(request, "logoff") == 0 ? QStringLiteral("Windows XP Logoff Sound.wav") : QStringLiteral("Windows XP Shutdown.wav"), 4000);
    // What session.sh does next
    if (const QString file = qEnvironmentVariable("WIN4WINE_NATIVE_REQUEST"); !file.isEmpty()) {
        QFile out(file);
        if (out.open(QIODevice::WriteOnly))
            out.write(QByteArray(request) + '\n');
    }
    // WIN4WINE_SESSION_REQUEST: where XP's log off tells the supervisor, as a Wine path.
    QString path = qEnvironmentVariable("WIN4WINE_SESSION_REQUEST");
    if (path.startsWith(QLatin1String("Z:"), Qt::CaseInsensitive)) {
        path = path.mid(2).replace(QLatin1Char('\\'), QLatin1Char('/'));
        QFile file(path);
        if (file.open(QIODevice::WriteOnly))
            file.write(QByteArray(request) + '\n');
    } else if (qEnvironmentVariable("WIN4WINE_POWER") == QLatin1String("1") && qstrcmp(request, "logoff") != 0) {
        // A whole session without the supervisor; xp.sh native's window sets this to 0.
        QProcess::startDetached(QStringLiteral("systemctl"), {qstrcmp(request, "reboot") == 0 ? QStringLiteral("reboot") : QStringLiteral("poweroff")});
    }
    QApplication::quit();
}

void Taskbar::loadDesktopBand()
{
    m_desktopItems.clear();
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (!m_desktopWatcher.directories().contains(folder))
        m_desktopWatcher.addPath(folder);
    const int size = theme()->scaled(ICON_SIZE);
    const QFileInfoList files = QDir(folder).entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &file : files) {
        QuickItem item;
        item.path = file.filePath();
        item.name = file.completeBaseName();
        if (file.suffix() == QLatin1String("desktop")) {
            if (const auto program = programs::fromFile(file.filePath())) {
                item.name = program->name;
                item.icon = programs::iconOf(*program).pixmap(size).toImage();
            }
        }
        if (item.icon.isNull())
            item.icon = fileicons::image(file, size);
        m_desktopItems.append(item);
    }
}

void Taskbar::loadCustomToolbar()
{
    m_customItems.clear();
    const QString folder = m_settings.customToolbar;
    if (folder.isEmpty()) {
        if (!m_customWatcher.directories().isEmpty())
            m_customWatcher.removePaths(m_customWatcher.directories());
        return;
    }
    if (!m_customWatcher.directories().contains(folder)) {
        if (!m_customWatcher.directories().isEmpty())
            m_customWatcher.removePaths(m_customWatcher.directories());
        m_customWatcher.addPath(folder);
    }
    const int size = theme()->scaled(ICON_SIZE);
    const QFileInfoList files = QDir(folder).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &file : files) {
        QuickItem item;
        item.path = file.filePath();
        item.name = file.completeBaseName();
        if (file.suffix() == QLatin1String("desktop")) {
            if (const auto program = programs::fromFile(file.filePath())) {
                item.name = program->name;
                item.icon = programs::iconOf(*program).pixmap(size).toImage();
            }
        }
        if (item.icon.isNull())
            item.icon = fileicons::image(file, size);
        m_customItems.append(item);
    }
}

void Taskbar::loadLinksBand()
{
    m_linksItems.clear();
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/win4wine/Favorites/Links");
    QDir().mkpath(folder);
    if (!m_linksWatcher.directories().contains(folder))
        m_linksWatcher.addPath(folder);
    if (!m_settings.linksBand)
        return;
    const int size = theme()->scaled(ICON_SIZE);
    const QFileInfoList files = QDir(folder).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &file : files) {
        QuickItem item;
        item.path = file.filePath();
        item.name = file.completeBaseName();
        if (file.suffix() == QLatin1String("desktop")) {
            if (const auto program = programs::fromFile(file.filePath())) {
                item.name = program->name;
                item.icon = programs::iconOf(*program).pixmap(size).toImage();
            }
        }
        if (item.icon.isNull())
            item.icon = fileicons::image(file, size);
        m_linksItems.append(item);
    }
}

void Taskbar::runLinkItem(int index)
{
    if (index < 0 || index >= m_linksItems.size())
        return;
    const QuickItem &item = m_linksItems[index];
    const QFileInfo file(item.path);
    if (file.suffix() == QLatin1String("desktop")) {
        if (const auto program = programs::fromFile(item.path))
            programs::launch(*program);
        return;
    }
    programs::open(item.path);
}

void Taskbar::runCustomItem(int index)
{
    if (index < 0 || index >= m_customItems.size())
        return;
    const QuickItem &item = m_customItems[index];
    const QFileInfo file(item.path);
    if (file.suffix().compare(QLatin1String("scf"), Qt::CaseInsensitive) == 0) {
        const QSettings scf(item.path, QSettings::IniFormat);
        if (scf.value(QStringLiteral("Taskbar/Command")).toString().compare(QLatin1String("ToggleDesktop"), Qt::CaseInsensitive) == 0)
            toggleDesktop();
        return;
    }
    if (file.suffix() == QLatin1String("desktop")) {
        if (const auto program = programs::fromFile(item.path))
            programs::launch(*program);
        return;
    }
    programs::open(item.path);
}

void Taskbar::runDesktopItem(int index)
{
    if (index < 0 || index >= m_desktopItems.size())
        return;
    const QuickItem &item = m_desktopItems[index];
    const QFileInfo file(item.path);
    if (file.suffix().compare(QLatin1String("scf"), Qt::CaseInsensitive) == 0) {
        const QSettings scf(item.path, QSettings::IniFormat);
        if (scf.value(QStringLiteral("Taskbar/Command")).toString().compare(QLatin1String("ToggleDesktop"), Qt::CaseInsensitive) == 0)
            toggleDesktop();
        return;
    }
    if (file.suffix() == QLatin1String("desktop")) {
        if (const auto program = programs::fromFile(item.path))
            programs::launch(*program);
        return;
    }
    programs::open(item.path);
}

void Taskbar::loadQuickLaunch()
{
    const QString folder = quickLaunchFolder();
    if (!QFileInfo::exists(folder)) {
        // As XP's setup fills it: Show Desktop (shell.inf's command file), the browser and the player
        QDir().mkpath(folder);
        QFile desktop(QDir(folder).filePath(QStringLiteral("Show Desktop.scf")));
        if (desktop.open(QIODevice::WriteOnly))
            desktop.write("[Shell]\r\nCommand=2\r\nIconFile=explorer.exe,3\r\n[Taskbar]\r\nCommand=ToggleDesktop\r\n");
        desktop.close();
        if (const auto browser = programs::defaultBrowser())
            if (const QString entry = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, browser->id); !entry.isEmpty())
                QFile::copy(entry, QDir(folder).filePath(browser->id));
        QFile player(QDir(folder).filePath(QStringLiteral("Windows Media Player.desktop")));
        if (player.open(QIODevice::WriteOnly))
            player.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=Windows Media Player\nExec=%1\n")
                             .arg(QFileInfo(QCoreApplication::applicationFilePath()).dir().filePath(QStringLiteral("wmplayer")))
                             .toUtf8());
    }
    if (!m_quickWatcher.directories().contains(folder))
        m_quickWatcher.addPath(folder);

    m_quickItems.clear();
    const int size = theme()->scaled(m_settings.quickLaunchLargeIcons ? QUICK_LARGE_ICON : ICON_SIZE);
    const auto explorer = xpfiles::resources(xpfiles::find(QStringLiteral("explorer.exe")));
    QFileInfoList files = QDir(folder).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    // the folder's order: by name, as a new Quick Launch band lists it
    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return QString::localeAwareCompare(a.completeBaseName(), b.completeBaseName()) < 0;
    });
    for (const QFileInfo &file : std::as_const(files)) {
        QuickItem item;
        item.path = file.filePath();
        item.name = file.completeBaseName();
        if (file.suffix().compare(QLatin1String("scf"), Qt::CaseInsensitive) == 0) {
            if (explorer) {
                // ExtractIcon's index: the icon groups in the resource directory's order, by number
                QStringList groups = explorer->names(QStringLiteral("14"));
                std::sort(groups.begin(), groups.end(), [](const QString &a, const QString &b) { return a.toInt() < b.toInt(); });
                item.icon = xpfiles::icon(*explorer, groups.value(ICON_SHOW_DESKTOP), size);
            }
        } else if (file.suffix() == QLatin1String("desktop")) {
            const auto program = programs::fromFile(file.filePath());
            if (!program)
                continue;
            item.name = program->name;
            if (QFileInfo(program->command.value(0)).fileName() == QLatin1String("wmplayer")) {
                // the player's own icon, wmploc.dll's 102
                if (const auto loc = xpfiles::resources(xpfiles::find(QStringLiteral("wmploc.dll"))))
                    item.icon = xpfiles::icon(*loc, 102, size);
            } else {
                item.icon = programs::iconOf(*program).pixmap(size).toImage();
            }
        }
        if (item.icon.isNull())
            item.icon = fileicons::image(file, size);
        m_quickItems.append(item);
    }
}

void Taskbar::addBandItems(QMenu *menu, const w4w::PeResources &browselc)
{
    // As the band site puts them over the taskbar's: the folder band's items, Show Title, Close
    // Toolbar (XP's: View, Open Folder, Show Text, Show Title, Close Toolbar), then a line
    auto *holder = new QMenu(menu);
    QHash<int, QAction *> isf = xpprogram::fillMenu(holder, browselc, MENU_ISF_BAND);
    auto *titleHolder = new QMenu(menu);
    const QHash<int, QAction *> title = xpprogram::fillMenu(titleHolder, browselc, MENU_BAND_TITLE);
    auto *closeHolder = new QMenu(menu);
    const QHash<int, QAction *> close = xpprogram::fillMenu(closeHolder, browselc, MENU_BAND_CLOSE);
    QList<QAction *> items = holder->actions();
    if (QAction *showTitle = title.value(CMD_SHOW_TITLE))
        items.append(showTitle);
    if (QAction *closeToolbar = close.value(CMD_CLOSE_TOOLBAR))
        items.append(closeToolbar);
    // View: Logos is the Links band's; Large Icons shows the 32-pixel icons (measured on XP's taskbar).
    if (QAction *logos = isf.value(CMD_VIEW_LOGOS); logos && logos->parentWidget())
        logos->parentWidget()->removeAction(logos);
    auto *sizes = new QActionGroup(menu);
    sizes->setExclusive(true);
    if (QAction *large = isf.value(CMD_VIEW_LARGE)) {
        large->setCheckable(true);
        large->setChecked(m_settings.quickLaunchLargeIcons);
        sizes->addAction(large);
        connect(large, &QAction::triggered, this, [] {
            TaskbarSettings settings = TaskbarSettings::load();
            settings.quickLaunchLargeIcons = true;
            settings.save();
        });
    }
    if (QAction *small = isf.value(CMD_VIEW_SMALL)) {
        small->setCheckable(true);
        small->setChecked(!m_settings.quickLaunchLargeIcons);
        sizes->addAction(small);
        connect(small, &QAction::triggered, this, [] {
            TaskbarSettings settings = TaskbarSettings::load();
            settings.quickLaunchLargeIcons = false;
            settings.save();
        });
    }
    const auto toggle = [this](QAction *action, bool TaskbarSettings::*setting) {
        if (!action)
            return;
        action->setCheckable(true);
        action->setChecked(m_settings.*setting);
        connect(action, &QAction::triggered, this, [setting](bool on) {
            TaskbarSettings settings = TaskbarSettings::load();
            settings.*setting = on;
            settings.save();
        });
    };
    toggle(isf.value(CMD_SHOW_TEXT), &TaskbarSettings::quickLaunchText);
    toggle(title.value(CMD_SHOW_TITLE), &TaskbarSettings::quickLaunchTitle);
    if (QAction *open = isf.value(CMD_OPEN_FOLDER))
        connect(open, &QAction::triggered, this, [] { programs::open(quickLaunchFolder()); });
    if (QAction *closeToolbar = close.value(CMD_CLOSE_TOOLBAR))
        connect(closeToolbar, &QAction::triggered, this, [] {
            TaskbarSettings settings = TaskbarSettings::load();
            settings.quickLaunch = false;
            settings.save();
        });
    QAction *first = menu->actions().value(0);
    for (QAction *item : std::as_const(items))
        menu->insertAction(first, item);
    menu->insertSeparator(first);
}

void Taskbar::showQuickItemMenu(int index, QPoint at)
{
    // the shortcut's own menu: Open, then shell32's item menu with what is carried out here
    const QString path = m_quickItems[index].path;
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    QAction *open = menu->addAction(xpfiles::shellString(STRING_OPEN, "&Open"));
    QFont bold = open->font();
    bold.setBold(true);
    open->setFont(bold);
    connect(open, &QAction::triggered, this, [this, index] {
        if (index < m_quickItems.size())
            runQuickItem(index);
    });
    menu->addSeparator();
    if (const auto shell32 = xpfiles::resources(xpfiles::find(QStringLiteral("shell32.dll")))) {
        const QHash<int, QAction *> items = xpprogram::fillMenu(menu, *shell32, MENU_ITEM);
        for (auto it = items.begin(); it != items.end(); ++it) {
            if (it.value()->menu())
                continue;
            it.value()->setEnabled(it.key() == CMD_ITEM_CUT || it.key() == CMD_ITEM_COPY || it.key() == CMD_ITEM_DELETE);
        }
        if (QAction *cut = items.value(CMD_ITEM_CUT))
            connect(cut, &QAction::triggered, this, [path] { fileops::copyToClipboard({path}, true); });
        if (QAction *copy = items.value(CMD_ITEM_COPY))
            connect(copy, &QAction::triggered, this, [path] { fileops::copyToClipboard({path}, false); });
        if (QAction *remove = items.value(CMD_ITEM_DELETE))
            connect(remove, &QAction::triggered, this, [this, path] { fileops::remove({path}, false, this); });
        for (QAction *action : menu->actions())
            if (action->menu())
                action->setEnabled(false);   // Send To is the folder windows'
    }
    menu->popup(m_settings.position == 1 ? at : at - QPoint(0, menu->sizeHint().height()));
}

void Taskbar::dragEnterEvent(QDragEnterEvent *event)
{
    // anything dragged: over the task buttons it brings windows forward
    m_dragHit = Hit{Hit::None, -2};
    event->acceptProposedAction();
}

void Taskbar::dragMoveEvent(QDragMoveEvent *event)
{
    const QPoint at = event->position().toPoint();
    // dropped on the Quick Launch band, a shortcut goes into its folder
    if (m_settings.quickLaunch && event->mimeData()->hasUrls()
        && m_quickBand.adjusted(0, 0, theme()->scaled(QUICK_BAND_END), 0).contains(at - QPoint(0, m_top))) {
        dragOverTasks(Hit());
        event->acceptProposedAction();
        return;
    }
    const Hit hit = hitAt(at);
    dragOverTasks(hit.kind == Hit::Task || hit.kind == Hit::Band ? hit : Hit());
    // DROPEFFECT_MOVE over a button (whose drop is then refused), none elsewhere
    if (hit.kind == Hit::Task)
        event->acceptProposedAction();
    else
        event->ignore();
}

void Taskbar::dragLeaveEvent(QDragLeaveEvent *)
{
    dragOverTasks(Hit());
}

void Taskbar::dragOverTasks(Hit hit)
{
    if (hit == m_dragHit)
        return;
    m_dragHit = hit;
    m_dragTimer.stop();
    if (hit.kind == Hit::Task)
        m_dragTimer.start(DRAG_BUTTON_MS);
    else if (hit.kind == Hit::Band)
        m_dragTimer.start(DRAG_BAND_MS);
}

void Taskbar::dropEvent(QDropEvent *event)
{
    const Hit hit = hitAt(event->position().toPoint());
    dragOverTasks(Hit());
    if (hit.kind == Hit::Task) {
        // message 0x436: ShellMessageBox(711, 518 "Taskbar", MB_ICONHAND)
        event->acceptProposedAction();
        QTimer::singleShot(0, this, [] {
            const auto &explorer = xpfiles::resources(xpfiles::find(QStringLiteral("explorer.exe")));
            if (!explorer)
                return;
            QMessageBox::critical(nullptr, xpfiles::string(*explorer, STRING_TASKBAR), xpfiles::string(*explorer, STRING_NO_DROP));
        });
        return;
    }
    const QDir folder(quickLaunchFolder());
    for (const QUrl &url : event->mimeData()->urls()) {
        if (!url.isLocalFile())
            continue;
        const QFileInfo file(url.toLocalFile());
        const QString target = folder.filePath(file.fileName());
        if (QFileInfo::exists(target))
            continue;
        // a program's entry is copied, as XP copies a dropped shortcut; anything else gets a link
        if (file.suffix() == QLatin1String("desktop") || file.suffix().compare(QLatin1String("scf"), Qt::CaseInsensitive) == 0)
            QFile::copy(file.filePath(), target);
        else
            fileops::createShortcut(file.absoluteFilePath(), folder.filePath(file.completeBaseName() + QStringLiteral(".lnk")));
    }
    event->acceptProposedAction();
}

void Taskbar::trayChanged()
{
    // an icon that appears or changes (its picture or its tooltip) is active now
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QStringList names;
    QSet<quint32> present;
    for (const TrayIcon &icon : m_tray->icons()) {
        present.insert(icon.id);
        const QPair<QString, qint64> seen{icon.tooltip, icon.image.cacheKey()};
        if (m_traySeen.value(icon.id) != seen) {
            m_traySeen.insert(icon.id, seen);
            m_trayActive.insert(icon.id, now);
        }
        if (const QString name = trayName(icon); !name.isEmpty() && !names.contains(name))
            names.append(name);
    }
    for (auto it = m_traySeen.begin(); it != m_traySeen.end();)
        it = present.contains(it.key()) ? std::next(it) : m_traySeen.erase(it);
    traynotify::setCurrent(names);
    updateTrayHiding();
}

QString Taskbar::trayName(const TrayIcon &icon)
{
    // the program's name when the tray host gives one, else the tooltip's first line
    return icon.name.isEmpty() ? icon.tooltip.section(QLatin1Char('\n'), 0, 0).trimmed() : icon.name;
}

void Taskbar::updateTrayHiding()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QSet<quint32> hidden;
    if (m_settings.hideInactive)
        for (const TrayIcon &icon : m_tray->icons()) {
            const traynotify::Behavior behavior = traynotify::behavior(trayName(icon));
            if (behavior == traynotify::AlwaysHide
                || (behavior == traynotify::HideWhenInactive && now - m_trayActive.value(icon.id, now) >= TRAY_RESIDENT_MS))
                hidden.insert(icon.id);
        }
    if (hidden.isEmpty())
        m_trayOpen = false;
    const bool chevronShown = m_trayHidden.isEmpty() && !hidden.isEmpty();
    m_trayHidden = hidden;
    relayout();
    update();
    if (chevronShown)
        QTimer::singleShot(0, this, &Taskbar::inactiveIconsBalloon);
}

QString Taskbar::chevronTip() const
{
    // explorer.exe's 543 while icons are hidden, 542 once they show
    const auto explorer = xpfiles::resources(xpfiles::find(QStringLiteral("explorer.exe")));
    const QString tip = explorer ? xpfiles::string(*explorer, m_trayOpen ? STRING_CHEVRON_HIDE : STRING_CHEVRON_SHOW) : QString();
    return tip.isEmpty() ? QString::fromLatin1(m_trayOpen ? "Hide" : "Show hidden icons") : tip;
}

void Taskbar::startButtonBalloon()
{
    TaskbarSettings settings = TaskbarSettings::load();
    // sub_100ebca: the Start panel's first logon until it is 2; the classic menu's while it is 0 (1 becomes 2)
    if (!settings.classicStartMenu) {
        if (settings.startButtonBalloonTip >= 2)
            return;
        settings.startButtonBalloonTip = 2;
        settings.save();
        showStartMenu(false);
        return;
    }
    if (settings.startButtonBalloonTip != 0) {
        if (settings.startButtonBalloonTip == 1) {
            settings.startButtonBalloonTip = 2;
            settings.save();
        }
        return;
    }
    const auto explorer = xpfiles::resources(xpfiles::find(QStringLiteral("explorer.exe")));
    if (!explorer || !isVisible())
        return;
    BalloonTip tip;
    tip.title = xpfiles::string(*explorer, STRING_START_BALLOON_TITLE);
    tip.text = xpfiles::string(*explorer, STRING_START_BALLOON);
    tip.kind = BalloonTip::Info;   // TTI_INFO
    tip.sound = false;
    auto *balloon = new BalloonWindow(tip);
    balloon->setAttribute(Qt::WA_DeleteOnClose);
    // its stem at the middle of the Start button's top
    const QRect start(mapToGlobal(m_startRect.topLeft() + QPoint(0, m_top)), m_startRect.size());
    balloon->place(QRect(start.center().x(), start.top(), 1, 1));
    connect(balloon, &BalloonWindow::clicked, balloon, &QWidget::close);
    connect(balloon, &BalloonWindow::dismissed, balloon, &QWidget::close);
    QTimer::singleShot(START_BALLOON_MS, balloon, &QWidget::close);   // timer 0x16
    m_startBalloon = balloon;
    balloon->show();
}

namespace {
// A reminder's balloon: shell32 shows it from a notification icon of its own, which this shell does
// not add, so its stem is on the notification area's left end.
BalloonWindow *reminderBalloon(const BalloonTip &tip, const QRect &tray)
{
    auto *balloon = new BalloonWindow(tip);
    balloon->setAttribute(Qt::WA_DeleteOnClose);
    balloon->place(QRect(tray.x() + 8, tray.top(), 1, 1));
    QTimer::singleShot(tip.timeout, balloon, &QWidget::close);
    return balloon;
}
}

// Microsoft.FixScreenResolution (strings 811/812, icon explorer.exe,9): XP's check (sub_100e569) compares
// the display's modes; here a screen under 800 x 600. Its click opens Display Properties' Settings.
void Taskbar::lowResolutionBalloon(std::function<void()> next)
{
    const QSize screen = QGuiApplication::primaryScreen()->geometry().size();
    const auto explorer = xpfiles::resources(xpfiles::find(QStringLiteral("explorer.exe")));
    if (!explorer || !isVisible() || (screen.width() >= 800 && screen.height() >= 600)) {
        next();
        return;
    }
    BalloonTip tip;
    tip.title = xpfiles::string(*explorer, STRING_LOW_RESOLUTION_TITLE);
    tip.text = xpfiles::string(*explorer, STRING_LOW_RESOLUTION);
    tip.kind = BalloonTip::Info;
    tip.timeout = 10000;
    BalloonWindow *balloon = reminderBalloon(tip, QRect(mapToGlobal(m_trayRect.topLeft() + QPoint(0, m_top)), m_trayRect.size()));
    connect(balloon, &BalloonWindow::clicked, balloon, [balloon] {
        balloon->close();
        DisplayProperties::showTab(DisplayProperties::Settings);
    });
    connect(balloon, &BalloonWindow::dismissed, balloon, &QWidget::close);
    connect(balloon, &QObject::destroyed, this, [next] { next(); });
    balloon->show();
}

// Microsoft.OfferTour (strings 821/822, icon tourstart.exe,0), once: its click runs tourstart.exe.
void Taskbar::tourBalloon()
{
    const QString key = QStringLiteral("StartMenu/TourBalloonShown");
    QSettings shown(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/native-explorer.conf"),
                    QSettings::IniFormat);
    if (shown.value(key, false).toBool() || !isVisible())
        return;
    shown.setValue(key, true);
    const auto explorer = xpfiles::resources(xpfiles::find(QStringLiteral("explorer.exe")));
    if (!explorer)
        return;
    BalloonTip tip;
    tip.title = xpfiles::string(*explorer, STRING_TOUR_BALLOON_TITLE);
    tip.text = xpfiles::string(*explorer, STRING_TOUR_BALLOON);
    if (tip.title.isEmpty() && tip.text.isEmpty())
        return;
    tip.kind = BalloonTip::Info;
    tip.timeout = TOUR_SHOW_MS;
    BalloonWindow *balloon = reminderBalloon(tip, QRect(mapToGlobal(m_trayRect.topLeft() + QPoint(0, m_top)), m_trayRect.size()));
    connect(balloon, &BalloonWindow::clicked, balloon, [balloon] {
        balloon->close();
        // The tour is an XP program: Wine runs it when it is there, the Start menu's shortcut otherwise.
        const QString tour = xpfiles::find(QStringLiteral("tourstart.exe"));
        const QString wine = QStandardPaths::findExecutable(QStringLiteral("wine"));
        if (!tour.isEmpty() && !wine.isEmpty())
            QProcess::startDetached(wine, {tour}, QDir::homePath());
        else if (const QString link = xpfiles::find(QStringLiteral("Tour Windows XP.lnk")); !link.isEmpty())
            programs::open(link);
    });
    connect(balloon, &BalloonWindow::dismissed, balloon, &QWidget::close);
    balloon->show();
}

// explorer.exe's balloon about the chevron (strings 590/591), once, when it is first shown.
void Taskbar::inactiveIconsBalloon()
{
    if (!m_settings.hideInactive || m_trayHidden.isEmpty() || !isVisible())
        return;
    const QString key = QStringLiteral("TrayNotify/InactiveBalloonShown");
    QSettings shown(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/native-explorer.conf"),
                    QSettings::IniFormat);
    if (shown.value(key, false).toBool())
        return;
    shown.setValue(key, true);
    const auto explorer = xpfiles::resources(xpfiles::find(QStringLiteral("explorer.exe")));
    if (!explorer)
        return;
    BalloonTip tip;
    tip.title = xpfiles::string(*explorer, STRING_HIDDEN_BALLOON_TITLE);
    tip.text = xpfiles::string(*explorer, STRING_HIDDEN_BALLOON);
    tip.kind = BalloonTip::Info;
    tip.timeout = HIDDEN_BALLOON_MS;
    tip.sound = false;
    auto *balloon = new BalloonWindow(tip);
    balloon->setAttribute(Qt::WA_DeleteOnClose);
    const QRect chevron(mapToGlobal(m_chevronRect.topLeft()), m_chevronRect.size());
    balloon->place(QRect(chevron.center().x(), chevron.top(), 1, 1));
    connect(balloon, &BalloonWindow::clicked, balloon, [this, balloon] {
        balloon->close();
        m_trayOpen = true;
        relayout();
        update();
    });
    connect(balloon, &BalloonWindow::dismissed, balloon, &QWidget::close);
    balloon->show();
}

void Taskbar::hotkey(int key, int modifiers)
{
    int id = -1;
    for (int i = 0; i < int(std::size(HOTKEYS)); ++i)
        if (HOTKEYS[i].key == key && HOTKEYS[i].modifiers == modifiers)
            id = i;
    switch (id) {
    case HOTKEY_RUN:
        showRunDialog();   // the Run dialog already open comes forward (FindWindow on string 722)
        break;
    case HOTKEY_MINIMIZE_ALL:
        // the Start menu closes, the taskband minimizes its windows (message 0x435), the desktop has the focus
        if (m_startPanel)
            m_startPanel->close();
        minimizeAll();
        break;
    case HOTKEY_UNDO_MINIMIZE_ALL:
        // sub_10335c0: the placements saved last, whatever saved them (Minimize All, Cascade or Tile)
        if (!m_undo.isEmpty()) {
            arrange::undo(m_undo);
            m_undo.clear();
        }
        break;
    case HOTKEY_EXPLORE:
        // sub_103adad: CSIDL_DRIVES (0x11) with the "explore" verb
        FileBrowser::open(places::Computer);
        break;
    case HOTKEY_FIND_FILES:
        FileBrowser::open(places::Search);
        break;
    case HOTKEY_HELP:
        // command 503: the Help and Support Center; Linux's when there is one
        for (const char *name : {"khelpcenter", "yelp"})
            if (const QString help = QStandardPaths::findExecutable(QLatin1String(name)); !help.isEmpty()) {
                QProcess::startDetached(help, {});
                return;
            }
        break;
    case HOTKEY_FIND_COMPUTERS:
        // Search for Computers: no network browser here, so the Search folder stands in
        FileBrowser::open(places::Search);
        break;
    case HOTKEY_NEXT_TASK:
    case HOTKEY_PREVIOUS_TASK:
        // the tray comes forward, the taskband moves its hot item by one (message 0x43d, +1 or -1)
        keyboardStep(Hit::Task, id == HOTKEY_NEXT_TASK ? 1 : -1);
        break;
    case HOTKEY_SYSTEM:
        // SHRunControlPanel on sysdm.cpl
        applets::open(QStringLiteral("system"));
        break;
    case HOTKEY_DESKTOP:
        toggleDesktop();
        break;
    case HOTKEY_TRAY:
        // SwitchToThisWindow on the tray, then the focus on the notification area (sub_101cdac)
        if (m_hover.kind != Hit::TrayIcon)
            keyboardStep(Hit::TrayIcon, 1);
        break;
    default:
        break;
    }
}

void Taskbar::minimizeAll()
{
    // message 0x434: is there a window the taskband can minimize
    const QList<xcb_window_t> windows = m_windows->arrangeable();
    if (windows.isEmpty())
        return;
    m_undo = arrange::minimizeAll(windows);
    m_undoString = STRING_MINIMIZE_ALL;
}

void Taskbar::keyboardStep(Hit::Kind kind, int step)
{
    QList<int> items;
    if (kind == Hit::Task) {
        for (int i = 0; i < m_buttons.size(); ++i)
            if (!m_buttons[i].rect.isNull())
                items.append(i);
    } else {
        for (int i = 0; i < m_trayIconRects.size() && i < m_tray->icons().size(); ++i)
            if (!m_trayIconRects[i].isNull())
                items.append(i);
    }
    if (items.isEmpty())
        return;
    qsizetype at = m_hover.kind == kind ? items.indexOf(m_hover.index) : -1;
    qsizetype next;
    if (kind == Hit::Task) {
        // message 0x43d: from the hot button, or the first when none is (TB_GETHOTITEM under 1), one on,
        // round the ends; the first press so lands on the second button
        next = items.size() > 1 ? (qMax<qsizetype>(at, 0) + step + items.size()) % items.size() : 0;
    } else {
        next = at < 0 ? (step > 0 ? 0 : items.size() - 1) : (at + step + items.size()) % items.size();
    }
    // The keys reaching a button on another page bring its page in first.
    if (kind == Hit::Task && !m_scrollRect.isNull() && m_scrollColumns > 0) {
        const int page = items[next] / m_scrollColumns;
        if (page != m_scrollPage) {
            m_scrollPage = page;
            relayout();
        }
    }
    if (!m_keyboard) {
        if (m_hidden || m_slideTimer.isActive())
            setHidden(false, false);
        m_keyboard = true;
        m_activeAtKeyboard = m_windows->active();
        grabKeyboard();
    }
    setHover({kind, items[next]});
}

void Taskbar::leaveKeyboard()
{
    if (!m_keyboard)
        return;
    m_keyboard = false;
    releaseKeyboard();
    setHover(Hit());
}

void Taskbar::chooseItem(Hit hit, bool context)
{
    if (hit.kind == Hit::Task && hit.index < m_buttons.size()) {
        const Button &button = m_buttons[hit.index];
        const QPoint at = mapToGlobal(button.rect.center() + QPoint(0, m_top));
        if (button.group)
            context ? showGroupMenu(hit.index, at) : showGroupList(hit.index);
        else if (context)
            showTaskMenu(button.tasks.first(), at);
        else
            m_windows->switchTo(m_windows->tasks()[button.tasks.first()].window);
    } else if (hit.kind == Hit::TrayIcon && hit.index < m_tray->icons().size() && hit.index < m_trayIconRects.size()) {
        const quint32 id = m_tray->icons()[hit.index].id;
        m_trayActive.insert(id, QDateTime::currentMSecsSinceEpoch());
        m_tray->click(id, context ? "context" : "activate", mapToGlobal(m_trayIconRects[hit.index].center() + QPoint(0, m_top)));
    }
}

void Taskbar::keyPressEvent(QKeyEvent *event)
{
    if (!m_keyboard) {
        QWidget::keyPressEvent(event);
        return;
    }
    const Hit hit = m_hover;
    const Hit::Kind kind = hit.kind == Hit::TrayIcon ? Hit::TrayIcon : Hit::Task;
    switch (event->key()) {
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        // with the Windows key still down, the next task; alone, the other toolbar
        if (event->modifiers() & Qt::MetaModifier)
            keyboardStep(Hit::Task, event->key() == Qt::Key_Tab ? 1 : -1);
        else
            keyboardStep(kind == Hit::Task ? Hit::TrayIcon : Hit::Task, 1);
        break;
    case Qt::Key_Left:
    case Qt::Key_Up:
        keyboardStep(kind, -1);
        break;
    case Qt::Key_Right:
    case Qt::Key_Down:
        keyboardStep(kind, 1);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        leaveKeyboard();
        chooseItem(hit, false);
        break;
    case Qt::Key_Menu:
        leaveKeyboard();
        chooseItem(hit, true);
        break;
    case Qt::Key_F10:
        if (event->modifiers() & Qt::ShiftModifier) {
            leaveKeyboard();
            chooseItem(hit, true);
        }
        break;
    case Qt::Key_Escape:
        leaveKeyboard();
        break;
    default:
        break;
    }
}

void Taskbar::toggleDesktop()
{
    const QList<quint32> showing = x11::property32(x11::root(), x11::atom("_NET_SHOWING_DESKTOP"));
    const bool shown = !showing.isEmpty() && showing.first();
    // XP's Minimize All leaves "&Undo Minimize All" behind; the window manager does the minimizing.
    if (shown) {
        m_undo.clear();
    } else {
        m_undo = arrange::remember(m_windows->arrangeable());
        m_undoString = STRING_MINIMIZE_ALL;
    }
    x11::sendToManager(x11::root(), "_NET_SHOWING_DESKTOP", {quint32(shown ? 0 : 1)});
}

void Taskbar::runQuickItem(int index)
{
    const QuickItem &item = m_quickItems[index];
    const QFileInfo file(item.path);
    if (file.suffix().compare(QLatin1String("scf"), Qt::CaseInsensitive) == 0) {
        // a shell command file: [Taskbar] Command=ToggleDesktop is Show Desktop
        const QSettings scf(item.path, QSettings::IniFormat);
        if (scf.value(QStringLiteral("Taskbar/Command")).toString().compare(QLatin1String("ToggleDesktop"), Qt::CaseInsensitive) == 0)
            toggleDesktop();
        return;
    }
    if (file.suffix() == QLatin1String("desktop")) {
        if (const auto program = programs::fromFile(item.path))
            programs::launch(*program);
        return;
    }
    programs::open(file.isSymLink() ? file.symLinkTarget() : item.path);
}
