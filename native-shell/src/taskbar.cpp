// SPDX-License-Identifier: LGPL-2.1-or-later
#include "taskbar.h"
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
#include "xpprogram.h"

#include <QApplication>
#include <QFileInfo>
#include <QCoreApplication>
#include <QSettings>
#include <QCursor>
#include <QDateTime>
#include <QFile>
#include <QGuiApplication>
#include <QHelpEvent>
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

#include <functional>

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
const int GROUP_ARROW_WIDTH = 5, GROUP_ARROW_GAP = 4;
// explorer.exe's menus: the taskbar's (205) and a group's (210), their commands, and Undo's words
const int MENU_TASKBAR = 205, MENU_GROUP = 210;
// browselc.dll's band site menu (Toolbars) and its Quick Launch
const int MENU_BANDS = 268, CMD_BAND_QUICK_LAUNCH = 4;
enum {
    CMD_CASCADE = 403, CMD_TILE_HORIZONTALLY = 404, CMD_TILE_VERTICALLY = 405, CMD_SHOW_DESKTOP = 407, CMD_ADJUST_TIME = 408,
    CMD_PROPERTIES = 413, CMD_UNDO = 416, CMD_TASK_MANAGER = 420, CMD_CUSTOMIZE_NOTIFICATIONS = 421, CMD_LOCK = 424,
    CMD_MINIMIZE_GROUP = 311, CMD_CLOSE_GROUP = 48913,
};
const int STRING_UNDO = 534, STRING_CASCADE = 535, STRING_TILE = 536, STRING_SHOW_OPEN_WINDOWS = 850;
const int TEXT_RAISE = 1;           // the taskbar's small text sits a pixel above the centre
const int TURN_OFF_DELAY_MS = 200;
// The notification area as explorer.exe lays it out: the style's content margins (19 and 7 with Luna),
// a toolbar of 18-pixel buttons with the icons 1 inside, then the clock. The clock is as wide as the
// longer of 11:00 and 23:00 in the user's format with "00" after it, whatever the time, and centres
// the time in it (measured with 4 and 5 icons: the area at 868 and 850, the time's middle at 987).
// Without a visual style, 6 at each end and 7 before the clock.
const int TRAY_BUTTON = 18, TRAY_ICON_INSET = 1, CLASSIC_TRAY_END = 6, CLASSIC_CLOCK_GAP = 7;
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
const int ICON_SHOW_DESKTOP = 3;    // explorer.exe's fourth icon, Show Desktop's
const Part TASK_FLASH{QStringLiteral("taskband"), QStringLiteral("flashbutton"), {}, 1, {}};
const Part BACKGROUND{QStringLiteral("taskbar"), QStringLiteral("backgroundbottom"), {}, 1, {}};
const Part TRAY_BACKGROUND{QStringLiteral("traynotify"), QStringLiteral("background"), {}, 1, QStringLiteral("traynotifyhoriz")};
const Part CLOCK{QStringLiteral("clock"), {}, {}, 0, QStringLiteral("traynotify")};
const Part GROUP_COUNT{QStringLiteral("taskband"), QStringLiteral("groupcount"), {}, 0, {}};
const Part GROUP_LIST{QStringLiteral("toolbar"), {}, {}, 0, QStringLiteral("taskbandgroupmenu")};
const Part GROUP_LIST_ITEM{QStringLiteral("toolbar"), QStringLiteral("button"), {}, 2, QStringLiteral("taskbandgroupmenu")};

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
    static constexpr int ROW = 22, INSET = 9, ICON_X = 9, TEXT_GAP = 7, END = 9;

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
        int widest = 0;
        for (const Item &item : m_items)
            widest = qMax(widest, fontMetrics().horizontalAdvance(item.title));
        const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
        const int width = theme()->scaled(INSET + ICON_X + ICON_SIZE + TEXT_GAP + END) + widest + frameWidth() * 2;
        resize(qBound(minWidth, width, screen.width() / 2), theme()->scaled(ROW) * int(m_items.size()) + frameWidth() * 2);
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
        return QRect(f + theme()->scaled(theme()->renderer() ? INSET : 0), f + row * theme()->scaled(ROW),
                     width() - 2 * f - theme()->scaled(theme()->renderer() ? INSET : 0), theme()->scaled(ROW));
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
            const int iconX = row.x() + theme()->scaled(renderer ? ICON_X : 3);
            painter.drawImage(QRect(iconX, row.y() + (row.height() - icon) / 2, icon, icon), m_items[i].icon);
            const int textX = iconX + icon + theme()->scaled(TEXT_GAP);
            const QRect words(textX, row.y(), row.right() - theme()->scaled(END) - textX, row.height());
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
        relayout();
        update();
    });
    connect(m_tray, &Tray::changed, this, [this] {
        relayout();
        update();
    });
    m_tray->balloons()->setLocator([this](quint32 id) {
        const QList<TrayIcon> &icons = m_tray->icons();
        for (int i = 0; i < icons.size() && i < m_trayIconRects.size(); ++i)
            if (icons[i].id == id && isVisible())
                return QRect(mapToGlobal(m_trayIconRects[i].topLeft()), m_trayIconRects[i].size());
        return QRect();
    });
    connect(m_tray, &Tray::startRequested, this, [this] { showStartMenu(true); });
    connect(m_tray, &Tray::hotkey, this, [](int key) {
        // XP's hotkeys with the Windows key, by their virtual keys
        if (key == 'R')
            showRunDialog();
        else if (key == 'E')
            FileBrowser::open(places::Computer);
        else if (key == 'F')
            FileBrowser::open(places::Search);
    });
    connect(theme(), &ShellTheme::changed, this, &Taskbar::themeChanged);

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
        if (settings.group == m_settings.group && settings.clock == m_settings.clock && settings.autoHide == m_settings.autoHide
            && settings.quickLaunch == m_settings.quickLaunch)
            return;
        const bool autoHide = settings.autoHide != m_settings.autoHide;
        m_settings = settings;
        if (autoHide) {
            setHidden(false, false);
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
    // bridge-wm and the session's supervisor know XP's taskbar by this class.
    x11::setString(winId(), "_WIN4WINE_CLASS", "Shell_TrayWnd");
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

QRect Taskbar::shownRect() const
{
    const QRect monitor = QGuiApplication::primaryScreen()->geometry();
    const int height = theme()->scaled(theme()->classic() ? CLASSIC_BAR_HEIGHT : BAR_HEIGHT);
    return QRect(monitor.x(), monitor.bottom() + 1 - height, monitor.width(), height);
}

QRect Taskbar::hiddenRect() const
{
    // explorer.exe's hidden rectangle: the bar's top at the monitor's bottom less SM_CYFRAME / 2
    const QRect monitor = QGuiApplication::primaryScreen()->geometry();
    const int frame = theme()->scaled(4);   // SM_CYFRAME on XP
    QRect rect = shownRect();
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
    setHidden(true);
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
    const int height = theme()->scaled(theme()->classic() ? CLASSIC_BAR_HEIGHT : BAR_HEIGHT);
    m_slideTimer.stop();
    setGeometry(m_hidden && m_settings.autoHide ? hiddenRect() : shownRect());
    if (m_settings.autoHide && !m_hidden)
        m_autoHideTimer.start();
    // The screen's bottom edge may be another monitor's: struts count from the root's. An
    // auto-hidden bar keeps no room: windows have the whole screen, as XP's work area.
    const int bottom = m_settings.autoHide ? 0 : screen->virtualGeometry().bottom() - monitor.bottom() + height;
    const quint32 start = monitor.left(), end = monitor.right();
    x11::setProperty32(winId(), "_NET_WM_STRUT", XCB_ATOM_CARDINAL, {0, 0, 0, quint32(bottom)});
    x11::setProperty32(winId(), "_NET_WM_STRUT_PARTIAL", XCB_ATOM_CARDINAL,
                       {0, 0, 0, quint32(bottom), 0, 0, 0, 0, 0, 0, start, end});
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
        m_startRect = QRect(theme()->scaled(2), theme()->scaled(4), startWidth, height() - theme()->scaled(6));
        bandStart = m_startRect.right() + 1 + theme()->scaled(6);
    }
    // Quick Launch's band first, its buttons from the band's start
    for (int i = 0; i < m_quickItems.size(); ++i)
        m_quickItems[i].rect = m_settings.quickLaunch
            ? QRect(bandStart + i * theme()->scaled(QUICK_WIDTH), renderer ? 0 : (height() - theme()->scaled(22)) / 2,
                    theme()->scaled(QUICK_WIDTH), renderer ? height() : theme()->scaled(22))
            : QRect();
    if (m_settings.quickLaunch)
        bandStart += int(m_quickItems.size()) * theme()->scaled(QUICK_WIDTH) + theme()->scaled(QUICK_BAND_END);

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
    const int first = theme()->scaled(margins.left());
    const int end = theme()->scaled(margins.right());
    const int gap = renderer || !m_settings.clock ? 0 : theme()->scaled(CLASSIC_CLOCK_GAP);
    const int step = theme()->scaled(TRAY_BUTTON);
    const int trayWidth = first + icons * step + gap + clockWidth + end;
    if (renderer)
        m_trayRect = QRect(width() - trayWidth, 0, trayWidth, height());
    else
        m_trayRect = QRect(width() - trayWidth - theme()->scaled(2), theme()->scaled(4), trayWidth, height() - theme()->scaled(6));
    m_trayIconRects.clear();
    for (int i = 0; i < icons; ++i)
        m_trayIconRects.append(QRect(m_trayRect.x() + first + i * step + theme()->scaled(TRAY_ICON_INSET),
                                     m_trayRect.y() + (m_trayRect.height() - theme()->scaled(ICON_SIZE)) / 2, theme()->scaled(ICON_SIZE), theme()->scaled(ICON_SIZE)));
    m_clockRect = QRect(m_trayRect.x() + first + icons * step + gap, m_trayRect.y(), clockWidth, m_trayRect.height());

    layoutButtons(bandStart, m_trayRect.x() - theme()->scaled(BAND_END));
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
    m_bandRect = QRect(bandStart, 0, band, height());
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
    for (auto it = m_grouped.begin(); it != m_grouped.end();)
        it = windowsOf.value(*it).size() < 2 ? m_grouped.erase(it) : std::next(it);
    // Group similar taskbar buttons (TaskbarGlomming) unchecked: every window its own button
    if (!m_settings.group)
        m_grouped.clear();
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
        return qMin(maxWidth, band / count - (count > 1 ? spacing : 0));
    };
    int width = maxWidth;
    for (int pass = 0; pass <= programs.size(); ++pass) {
        const int count = shown();
        width = ideal(count);
        if (width <= groupWidth) {
            QString oldest;
            for (const QString &program : std::as_const(programs))
                if (m_settings.group && !m_grouped.contains(program) && windowsOf.value(program).size() > 1
                    && (oldest.isEmpty() || chosen(program) < chosen(oldest)))
                    oldest = program;
            if (!oldest.isEmpty()) {
                m_grouped.insert(oldest);
                continue;
            }
            width = qMax(width, minWidth);
            break;
        }
        const int room = (band - count * groupWidth) / groupWidth;
        QString newest;
        if (room >= 2)
            for (const QString &program : std::as_const(m_grouped))
                if (windowsOf.value(program).size() < room && (newest.isEmpty() || chosen(program) > chosen(newest)))
                    newest = program;
        if (newest.isEmpty())
            break;
        m_grouped.remove(newest);
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
    // Those that do not fit are not shown (XP gives the band more rows and arrows to scroll them).
    for (int i = 0; i < m_buttons.size(); ++i) {
        QRect rect(bandStart + i * (width + spacing), 0, width, height());
        if (!renderer)
            rect = QRect(rect.x(), theme()->scaled(4), width, height() - theme()->scaled(6));
        m_buttons[i].rect = rect.right() < bandEnd + theme()->scaled(BAND_END) ? rect : QRect();
    }
}

QRect Taskbar::buttonRect(quint32 window) const
{
    const QList<Task> &tasks = m_windows->tasks();
    for (const Button &button : m_buttons)
        for (int i : button.tasks)
            if (i < tasks.size() && tasks[i].window == window && !button.rect.isNull())
                return QRect(mapToGlobal(button.rect.topLeft()), button.rect.size());
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
    painter.drawImage(content.x(), (height() - m_flag.height()) / 2, m_flag);
    painter.setFont(m_startFont);
    // (A pixel above the middle, as XP's)
    drawShadowedText(painter, QRect(content.x() + m_flag.width() + theme()->scaled(START_TEXT_X), -1, content.width() - m_flag.width(), height()), m_startText);
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
        if (attention && !active)
            part = TASK_FLASH;
        renderer->draw(&painter, rect, part);
    } else {
        const bool down = active || pressed;
        if (attention && !active)
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
    const QColor text = renderer ? renderer->colour(TASK_TOOLBAR, QStringLiteral("textcolor")).value_or(Qt::white) : QColor(attention && !active ? Qt::white : Qt::black);
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
        const int arrowWidth = theme()->scaled(GROUP_ARROW_WIDTH);
        const int arrowX = textEnd - arrowWidth + 1 + down, arrowY = rect.y() + (rect.height() - arrowWidth / 2 - 1) / 2 + textRaise;
        for (int row = 0; row <= arrowWidth / 2; ++row)
            painter.fillRect(arrowX + row, arrowY + row, arrowWidth - 2 * row, 1, text);
        textEnd = arrowX - theme()->scaled(GROUP_ARROW_GAP) - down;
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

void Taskbar::drawTray(QPainter &painter)
{
    w4w::Renderer *renderer = theme()->renderer();
    if (renderer)
        renderer->draw(&painter, m_trayRect, TRAY_BACKGROUND);
    else
        theme()->sunken(painter, m_trayRect);
    const QList<TrayIcon> &icons = m_tray->icons();
    for (int i = 0; i < icons.size() && i < m_trayIconRects.size(); ++i)
        painter.drawImage(m_trayIconRects[i], icons[i].image);
    painter.setFont(m_clockFont);
    painter.setPen(renderer ? renderer->colour(CLOCK, QStringLiteral("textcolor")).value_or(Qt::white) : QColor(Qt::black));
    if (m_settings.clock)
        painter.drawText(m_clockRect.translated(0, renderer ? -theme()->scaled(TEXT_RAISE) : 0), Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextSingleLine, m_clock);
}

void Taskbar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    if (w4w::Renderer *renderer = theme()->renderer()) {
        renderer->draw(&painter, rect(), BACKGROUND);
    } else {
        painter.fillRect(rect(), theme()->system(QStringLiteral("btnface"), Qt::lightGray));
        painter.setPen(theme()->system(QStringLiteral("btnhighlight"), Qt::white));
        painter.drawLine(0, 1, width(), 1);
    }
    drawStart(painter);
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
        const QPoint at = theme()->renderer() ? item.rect.topLeft() + QPoint(theme()->scaled(QUICK_ICON_X), theme()->scaled(QUICK_ICON_Y))
                                              : item.rect.topLeft() + QPoint((item.rect.width() - 16) / 2, (item.rect.height() - 16) / 2);
        painter.drawImage(QRect(at + QPoint(shift, shift), QSize(theme()->scaled(ICON_SIZE), theme()->scaled(ICON_SIZE))), item.icon);
    }
    for (int i = 0; i < m_buttons.size(); ++i)
        drawButton(painter, i);
    drawTray(painter);
}

Taskbar::Hit Taskbar::hitAt(QPoint at) const
{
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
    for (int i = 0; i < m_buttons.size(); ++i)
        if (m_buttons[i].rect.contains(at)) {
            hit.kind = Hit::Task;
            hit.index = i;
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
}

void Taskbar::mousePressEvent(QMouseEvent *event)
{
    const Hit hit = hitAt(event->position().toPoint());
    m_activeAtPress = m_windows->activeAtPress();
    if (event->button() == Qt::LeftButton && hit.kind == Hit::Start) {
        // The press that closes the open menu must not open it again.
        if (!m_startClosed.isValid() || m_startClosed.elapsed() > 200)
            showStartMenu(false);
        return;
    }
    if (event->button() == Qt::LeftButton && (hit.kind == Hit::Task || hit.kind == Hit::Quick)) {
        m_pressed = hit;
        update();
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
    const Hit pressed = m_pressed;
    m_pressed = Hit();
    update();
    if (hit.kind == Hit::Quick && hit.index < m_quickItems.size()) {
        if (event->button() == Qt::LeftButton && pressed == hit)
            runQuickItem(hit.index);
        else if (event->button() == Qt::RightButton)
            showTaskbarMenu(event->globalPosition().toPoint(), false);
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
    } else if ((hit.kind == Hit::Band || hit.kind == Hit::Clock || (hit.kind == Hit::None && m_trayRect.contains(at)))
               && event->button() == Qt::RightButton) {
        // The notification area's, the clock's included, has the date's and the icons' items too.
        showTaskbarMenu(event->globalPosition().toPoint(), hit.kind != Hit::Band);
    } else if (hit.kind == Hit::TrayIcon && hit.index < m_tray->icons().size()) {
        const quint32 id = m_tray->icons()[hit.index].id;
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
        else if (hit.kind == Hit::Quick && hit.index < m_quickItems.size())
            text = m_quickItems[hit.index].name;
        else if (hit.kind == Hit::Clock)
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
    QAction *close = menu->addAction(tr("&Close"), this, [window] { WindowList::close(window); });
    QFont bold = close->font();
    bold.setBold(true);
    close->setFont(bold);
    menu->popup(at - QPoint(0, menu->sizeHint().height()));
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
    list->popUp(QRect(mapToGlobal(button.rect.topLeft()), button.rect.size()));
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
    menu->popup(at - QPoint(0, menu->sizeHint().height()));
}

void Taskbar::showTaskbarMenu(QPoint at, bool tray)
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
        customize->setEnabled(false);   // no icon can be hidden here
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
                it.value()->setEnabled(it.key() == CMD_BAND_QUICK_LAUNCH);
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
            QAction *first = menu->actions().value(0);
            menu->insertMenu(first, toolbars);
            menu->insertSeparator(first);
        }
    }
    // This taskbar cannot move or change its size.
    if (QAction *lock = actions.value(CMD_LOCK)) {
        lock->setCheckable(true);
        lock->setChecked(true);
        lock->setEnabled(false);
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
    menu->popup(at - QPoint(0, menu->sizeHint().height()));
}

void Taskbar::showStartMenu(bool keyboard)
{
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
    const int size = theme()->scaled(ICON_SIZE);
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

void Taskbar::toggleDesktop()
{
    const QList<quint32> showing = x11::property32(x11::root(), x11::atom("_NET_SHOWING_DESKTOP"));
    const bool shown = !showing.isEmpty() && showing.first();
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
