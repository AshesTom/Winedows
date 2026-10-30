// SPDX-License-Identifier: LGPL-2.1-or-later
#include "taskbar.h"

#include "filebrowser.h"
#include "startmenu.h"
#include "theme.h"
#include "x11.h"
#include "xpfiles.h"

#include <QApplication>
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
#include <QToolTip>

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
const int TASK_MAX_WIDTH = 160, TASK_MIN_WIDTH = 40;
const int TASK_ICON_X = 10, TASK_ICON_Y = 7, TASK_TEXT_GAP = 3, TASK_TEXT_END = 7;
const int TEXT_RAISE = 1;           // the taskbar's small text sits a pixel above the centre
// The tray's art starts with 10 transparent pixels.
const int TRAY_ICON_X = 16, TRAY_ICON_STEP = 18, TRAY_CLOCK_GAP = 7, TRAY_END = 17;
const int ICON_SIZE = 16;

// Toolbar part states and their frames in the style's strips
enum { Normal = 1, Hot = 2, Pressed = 3, Checked = 5, HotChecked = 6 };

const Part START_BUTTON{QStringLiteral("button"), {}, {}, 1, QStringLiteral("start")};
const Part TASK_BUTTON{QStringLiteral("toolbar"), QStringLiteral("button"), {}, 1, QStringLiteral("taskband")};
const Part TASK_TOOLBAR{QStringLiteral("toolbar"), {}, {}, 0, QStringLiteral("taskband")};
const Part TASK_FLASH{QStringLiteral("taskband"), QStringLiteral("flashbutton"), {}, 1, {}};
const Part BACKGROUND{QStringLiteral("taskbar"), QStringLiteral("backgroundbottom"), {}, 1, {}};
const Part TRAY_BACKGROUND{QStringLiteral("traynotify"), QStringLiteral("background"), {}, 1, QStringLiteral("traynotifyhoriz")};
const Part CLOCK{QStringLiteral("clock"), {}, {}, 0, QStringLiteral("traynotify")};

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
    connect(m_tray, &Tray::startRequested, this, &Taskbar::showStartMenu);
    connect(theme(), &ShellTheme::changed, this, &Taskbar::themeChanged);

    m_clockTimer.setSingleShot(true);
    connect(&m_clockTimer, &QTimer::timeout, this, &Taskbar::tickClock);
    themeChanged();
    tickClock();
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
    m_taskFont = font(TASK_TOOLBAR, "Tahoma, 8");
    m_activeTaskFont = m_taskFont;
    m_activeTaskFont.setBold(true);
    m_clockFont = font(CLOCK, "Tahoma, 8");
    placeOnScreen();
    update();
}

void Taskbar::placeOnScreen()
{
    const QScreen *screen = QGuiApplication::primaryScreen();
    const QRect monitor = screen->geometry();
    const int height = theme()->scaled(theme()->classic() ? CLASSIC_BAR_HEIGHT : BAR_HEIGHT);
    setGeometry(monitor.x(), monitor.bottom() + 1 - height, monitor.width(), height);
    // The screen's bottom edge may be another monitor's: struts count from the root's.
    const int bottom = screen->virtualGeometry().bottom() - monitor.bottom() + height;
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

    const QFontMetrics clockMetrics(m_clockFont);
    const int icons = int(m_tray->icons().size());
    const int clockWidth = clockMetrics.horizontalAdvance(m_clock);
    const int first = theme()->scaled(renderer ? TRAY_ICON_X : 6);
    const int end = theme()->scaled(renderer ? TRAY_END : 6);
    const int trayWidth = first + icons * theme()->scaled(TRAY_ICON_STEP) + theme()->scaled(TRAY_CLOCK_GAP) + clockWidth + end;
    if (renderer)
        m_trayRect = QRect(width() - trayWidth, 0, trayWidth, height());
    else
        m_trayRect = QRect(width() - trayWidth - theme()->scaled(2), theme()->scaled(4), trayWidth, height() - theme()->scaled(6));
    m_trayIconRects.clear();
    for (int i = 0; i < icons; ++i)
        m_trayIconRects.append(QRect(m_trayRect.x() + first + i * theme()->scaled(TRAY_ICON_STEP),
                                     m_trayRect.y() + (m_trayRect.height() - theme()->scaled(ICON_SIZE)) / 2, theme()->scaled(ICON_SIZE), theme()->scaled(ICON_SIZE)));
    m_clockRect = QRect(m_trayRect.x() + first + icons * theme()->scaled(TRAY_ICON_STEP) + theme()->scaled(TRAY_CLOCK_GAP), m_trayRect.y(),
                        clockWidth, m_trayRect.height());

    // XP shrinks the buttons alike, then groups them (not done here: they stop at the band's end).
    const int band = qMax(0, m_trayRect.x() - bandStart);
    const int count = int(m_windows->tasks().size());
    int buttonWidth = theme()->scaled(TASK_MAX_WIDTH);
    if (count * buttonWidth > band && count)
        buttonWidth = qMax(theme()->scaled(TASK_MIN_WIDTH), band / count);
    m_taskRects.clear();
    for (int i = 0; i < count; ++i) {
        QRect rect(bandStart + i * buttonWidth, 0, buttonWidth, height());
        if (!renderer)
            rect = QRect(bandStart + i * buttonWidth, theme()->scaled(4), buttonWidth - theme()->scaled(3), height() - theme()->scaled(6));
        m_taskRects.append(rect.right() < m_trayRect.x() ? rect : QRect());
    }
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

    // uxtheme's continuous shadow: the text's coverage, softened by a pixel, at its offset.
    QImage mask(rect.size() + QSize(6, 6), QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    {
        QPainter textPainter(&mask);
        textPainter.setFont(m_startFont);
        textPainter.setPen(shadow);
        textPainter.drawText(QRect(QPoint(0, 0), rect.size()), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, text);
    }
    QImage soft(mask.size(), QImage::Format_ARGB32_Premultiplied);
    soft.fill(Qt::transparent);
    {
        QPainter softPainter(&soft);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                softPainter.setOpacity(dx == 0 && dy == 0 ? 0.5 : dx == 0 || dy == 0 ? 0.125 : 0.0);
                softPainter.setCompositionMode(QPainter::CompositionMode_Plus);
                softPainter.drawImage(QPoint(dx, dy), mask);
            }
    }
    painter.drawImage(rect.topLeft() + offset, soft);
    painter.setPen(colour);
    painter.drawText(rect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, text);
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
    drawShadowedText(painter, QRect(content.x() + m_flag.width() + theme()->scaled(START_TEXT_X), 0, content.width() - m_flag.width(), height()), m_startText);
}

void Taskbar::drawTask(QPainter &painter, int index)
{
    const QRect rect = m_taskRects.value(index);
    if (rect.isEmpty())
        return;
    const Task &task = m_windows->tasks()[index];
    const bool active = task.window == m_windows->active();
    const bool hover = m_hover.kind == Hit::Task && m_hover.index == index;
    const bool pressed = m_pressed.kind == Hit::Task && m_pressed.index == index;
    int state = active ? (hover ? HotChecked : Checked) : (hover ? Hot : Normal);
    if (pressed)
        state = Pressed;
    w4w::Renderer *renderer = theme()->renderer();
    if (renderer) {
        Part part = withState(TASK_BUTTON, state);
        if (task.attention && !active)
            part = TASK_FLASH;
        renderer->draw(&painter, rect, part);
    } else {
        const bool down = active || pressed;
        if (task.attention && !active)
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
    const int textX = rect.x() + iconX + theme()->scaled(ICON_SIZE + TASK_TEXT_GAP) + down;
    const int textWidth = rect.right() - theme()->scaled(renderer ? TASK_TEXT_END : 4) - textX;
    if (textWidth <= 0)
        return;
    const QFont &font = active ? m_activeTaskFont : m_taskFont;
    painter.setFont(font);
    const QColor text = renderer ? renderer->colour(TASK_TOOLBAR, QStringLiteral("textcolor")).value_or(Qt::white) : QColor(task.attention && !active ? Qt::white : Qt::black);
    painter.setPen(text);
    const QString title = QFontMetrics(font).elidedText(task.title, Qt::ElideRight, textWidth);
    painter.drawText(QRect(textX, rect.y() + down - theme()->scaled(renderer ? TEXT_RAISE : 0), textWidth, rect.height()), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, title);
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
    painter.drawText(m_clockRect.translated(0, renderer ? -theme()->scaled(TEXT_RAISE) : 0), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, m_clock);
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
    for (int i = 0; i < m_taskRects.size(); ++i)
        drawTask(painter, i);
    drawTray(painter);
}

Taskbar::Hit Taskbar::hitAt(QPoint at) const
{
    Hit hit;
    if (m_startRect.contains(at)) {
        hit.kind = Hit::Start;
        return hit;
    }
    for (int i = 0; i < m_taskRects.size(); ++i)
        if (m_taskRects[i].contains(at)) {
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
            showStartMenu();
        return;
    }
    if (event->button() == Qt::LeftButton && hit.kind == Hit::Task) {
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
    if (hit.kind == Hit::Task && hit.index < m_windows->tasks().size()) {
        const Task &task = m_windows->tasks()[hit.index];
        if (event->button() == Qt::RightButton) {
            showTaskMenu(hit.index, event->globalPosition().toPoint());
        } else if (event->button() == Qt::LeftButton && pressed == hit) {
            if (task.window == m_activeAtPress && !task.minimized) {
                const xcb_window_t window = task.window;
                WindowList::minimize(window);
                m_windows->activateTopmost(window);
            } else {
                WindowList::activate(task.window);
            }
        }
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
        if (hit.kind == Hit::Task && hit.index < m_windows->tasks().size())
            text = m_windows->tasks()[hit.index].title;
        else if (hit.kind == Hit::TrayIcon && hit.index < m_tray->icons().size())
            text = m_tray->icons()[hit.index].tooltip;
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

void Taskbar::showStartMenu()
{
    if (m_startPanel) {
        m_startPanel->close();
        return;
    }
    auto *panel = new StartPanel(mapToGlobal(QPoint(0, 0)), [this] { endSession("logoff"); }, [this] { turnOff(); },
                                 [] { FileBrowser::open(places::ControlPanel); });
    panel->setAttribute(Qt::WA_DeleteOnClose);
    connect(panel, &StartPanel::closed, this, [this] {
        m_startClosed.start();
        update();
    });
    m_startPanel = panel;
    panel->show();
    update();
}

void Taskbar::turnOff()
{
    QMessageBox box(QMessageBox::NoIcon, tr("Turn off computer"), tr("What do you want the computer to do?"));
    QPushButton *off = box.addButton(tr("Turn Off"), QMessageBox::AcceptRole);
    QPushButton *restart = box.addButton(tr("Restart"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == off)
        endSession("shutdown");
    else if (box.clickedButton() == restart)
        endSession("reboot");
}

void Taskbar::endSession(const char *request)
{
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
