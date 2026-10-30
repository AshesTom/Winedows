// SPDX-License-Identifier: LGPL-2.1-or-later
#include "frames.h"

#include "ipcfile.h"
#include "x11.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QRegion>
#include <QSet>
#include <QtMath>

using w4w::Part;

namespace {

// Window part states: 1 active, 2 inactive; the caption buttons have four of each.
enum { NormalButton = 1, HotButton = 2, PressedButton = 3 };
const int ICON_SIZE = 16;
// At 96 DPI: the system icon's place in the frame, and the room before the title
const int ICON_X = 6, ICON_Y = 8, TITLE_GAP = 4;

Part windowPart(const char *name, int state)
{
    return Part{QStringLiteral("window"), QLatin1String(name), {}, state, {}};
}

bool shown(FrameButton button, const FrameLook &look)
{
    switch (button) {
    case FrameButton::Maximize: return look.canMaximize;
    case FrameButton::Minimize: return look.canMinimize;
    case FrameButton::Help: return look.canHelp;
    default: return true;
    }
}

const char *buttonName(FrameButton button, bool maximized)
{
    switch (button) {
    case FrameButton::Minimize: return "minbutton";
    case FrameButton::Maximize: return maximized ? "restorebutton" : "maxbutton";
    case FrameButton::Close: return "closebutton";
    case FrameButton::Help: return "helpbutton";
    default: return "";
    }
}

// Marlett's glyphs of Windows Classic's caption buttons
QChar marlett(FrameButton button, bool maximized)
{
    switch (button) {
    case FrameButton::Minimize: return QLatin1Char('0');
    case FrameButton::Maximize: return QLatin1Char(maximized ? '2' : '1');
    default: return QLatin1Char('r');
    }
}

} // namespace

QRect frameButtonRect(FrameButton button, const QRect &rect, const FrameStyle &style, bool maximized)
{
    if (button == FrameButton::None || button == FrameButton::System)
        return QRect();
    // Next to Close: XP has no other button on a window with this one.
    if (button == FrameButton::Help) {
        const QRect place = frameButtonRect(FrameButton::Maximize, rect, style, maximized);
        return place;
    }
    if (!style.renderer) {
        const int width = 16 * style.top / 23, height = 14 * style.top / 23;
        const int step = button == FrameButton::Close ? 2 : button == FrameButton::Maximize ? 2 + width + 2 : 2 + 2 * width + 2;
        return QRect(rect.right() + 1 - style.border - step - width + 2, rect.y() + style.border + 1, width, height);
    }
    const int side = style.top - 9;
    QSize offset(-25, 5);
    w4w::parseSize(style.renderer->property(windowPart(buttonName(button, maximized), 1), QStringLiteral("offset")).value_or(QString()), &offset);
    // A maximized frame's edges are off the screen; its buttons stay 2 pixels inside it.
    const int inset = maximized ? 2 : 0;
    return QRect(rect.right() + 1 + offset.width() - inset, rect.y() + offset.height() + inset, side, side);
}

void drawFrame(QPainter &painter, const QRect &rect, const FrameStyle &style, const FrameLook &look)
{
    const int top = style.top, border = style.border;
    const QRect caption(rect.x(), rect.y(), rect.width(), top);
    QFont font = style.captionFont;
    const FrameButton buttons[] = {FrameButton::Close, FrameButton::Maximize, FrameButton::Minimize, FrameButton::Help};
    const int titleEnd = frameButtonRect(FrameButton::Minimize, rect, style, look.maximized).left() - 4;
    const int titleX = look.icon.isNull() ? ICON_X : ICON_X + ICON_SIZE + TITLE_GAP;
    const QRect titleRect(rect.x() + titleX, rect.y(), qMax(0, titleEnd - rect.x() - titleX), top);
    const QString title = QFontMetrics(font).elidedText(look.title, Qt::ElideRight, titleRect.width());
    painter.setFont(font);

    if (w4w::Renderer *renderer = style.renderer.get()) {
        const int state = look.active ? 1 : 2;
        renderer->draw(&painter, caption, windowPart(look.maximized ? "maxcaption" : "caption", state));
        if (!look.maximized) {
            renderer->draw(&painter, QRect(rect.x(), rect.y() + top, border, rect.height() - top - border), windowPart("frameleft", state));
            renderer->draw(&painter, QRect(rect.right() + 1 - border, rect.y() + top, border, rect.height() - top - border), windowPart("frameright", state));
            renderer->draw(&painter, QRect(rect.x(), rect.bottom() + 1 - border, rect.width(), border), windowPart("framebottom", state));
        }
        if (!look.icon.isNull())
            painter.drawImage(rect.x() + ICON_X, rect.y() + ICON_Y, look.icon);
        if (look.active) {
            const QColor shadow = renderer->colour(windowPart("caption", 1), QStringLiteral("textshadowcolor")).value_or(QColor(10, 24, 131));
            painter.setPen(shadow);
            painter.drawText(titleRect.translated(1, 1), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, title);
        }
        painter.setPen(look.active ? style.activeText : style.inactiveText);
        painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, title);
        for (FrameButton button : buttons) {
            if (!shown(button, look))
                continue;
            int frame = look.pressed == button ? PressedButton : look.hot == button ? HotButton : NormalButton;
            if (!look.active)
                frame += 4;
            renderer->draw(&painter, frameButtonRect(button, rect, style, look.maximized), windowPart(buttonName(button, look.maximized), frame));
        }
        return;
    }

    // Windows Classic: raised edges on the face colour, a gradient for the caption
    painter.fillRect(rect, style.face);
    const QRect edge = rect.adjusted(0, 0, -1, -1);
    painter.setPen(style.light);
    painter.drawLine(edge.topLeft() + QPoint(1, 1), edge.topRight() + QPoint(-1, 1));
    painter.drawLine(edge.topLeft() + QPoint(1, 1), edge.bottomLeft() + QPoint(1, -1));
    painter.setPen(style.face);
    painter.drawLine(edge.topLeft(), edge.topRight());
    painter.drawLine(edge.topLeft(), edge.bottomLeft());
    painter.setPen(style.darkShadow);
    painter.drawLine(edge.bottomLeft(), edge.bottomRight());
    painter.drawLine(edge.topRight(), edge.bottomRight());
    painter.setPen(style.shadow);
    painter.drawLine(edge.bottomLeft() + QPoint(1, -1), edge.bottomRight() + QPoint(-1, -1));
    painter.drawLine(edge.topRight() + QPoint(-1, 1), edge.bottomRight() + QPoint(-1, -1));
    const QRect bar(rect.x() + border, rect.y() + border, rect.width() - 2 * border, top - border - 1);
    QLinearGradient gradient(bar.topLeft(), bar.topRight());
    gradient.setColorAt(0, look.active ? style.activeCaption : style.inactiveCaption);
    gradient.setColorAt(1, look.active ? style.activeGradient : style.inactiveGradient);
    painter.fillRect(bar, gradient);
    if (!look.icon.isNull())
        painter.drawImage(bar.x() + 1, bar.y() + (bar.height() - ICON_SIZE) / 2, look.icon);
    painter.setPen(look.active ? style.activeText : style.inactiveText);
    // A dialog has no icon: its title starts where the icon would.
    const int titleX = look.icon.isNull() ? 2 : ICON_SIZE + 3;
    painter.drawText(QRect(bar.x() + titleX, bar.y(), titleEnd - bar.x() - titleX, bar.height()),
                     Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                     QFontMetrics(font).elidedText(look.title, Qt::ElideRight, titleEnd - bar.x() - titleX));
    QFont glyphs(QStringLiteral("Marlett"));
    glyphs.setPixelSize(bar.height() - 7);
    for (FrameButton button : buttons) {
        if (!shown(button, look))
            continue;
        const QRect r = frameButtonRect(button, rect, style, look.maximized);
        const bool down = look.pressed == button;
        painter.fillRect(r, style.face);
        painter.setPen(down ? style.darkShadow : style.light);
        painter.drawLine(r.topLeft(), r.topRight());
        painter.drawLine(r.topLeft(), r.bottomLeft());
        painter.setPen(down ? style.light : style.darkShadow);
        painter.drawLine(r.bottomLeft(), r.bottomRight());
        painter.drawLine(r.topRight(), r.bottomRight());
        painter.setFont(glyphs);
        painter.setPen(Qt::black);
        painter.drawText(r.translated(down ? 1 : 0, down ? 1 : 0), Qt::AlignCenter, QString(marlett(button, look.maximized)));
    }
}

QRegion frameShape(const QSize &size, const FrameStyle &style, bool maximized)
{
    QRegion region(QRect(QPoint(0, 0), size));
    if (!style.renderer || maximized || size.width() < 32)
        return region;
    // Each row's transparent pixels at both ends of the rendered caption
    QImage art(size.width(), style.top, QImage::Format_ARGB32_Premultiplied);
    art.fill(Qt::transparent);
    {
        QPainter painter(&art);
        style.renderer->draw(&painter, art.rect(), windowPart("caption", 1));
    }
    for (int y = 0; y < art.height(); ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(art.constScanLine(y));
        int left = 0, right = 0;
        while (left < 16 && qAlpha(row[left]) == 0)
            ++left;
        while (right < 16 && qAlpha(row[art.width() - 1 - right]) == 0)
            ++right;
        if (left)
            region -= QRect(0, y, left, 1);
        if (right)
            region -= QRect(art.width() - right, y, right, 1);
        if (!left && !right)
            break;
    }
    return region;
}

FrameWindow::FrameWindow(FrameManager *manager, quint32 client, const QString &title)
    : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint)
    , m_manager(manager)
    , m_client(client)
{
    m_look.title = title;
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setWindowTitle(title);
    readHints();
    // bridge-wm pairs it with its client by these, and shows it itself.
    x11::setString(winId(), "_WIN4WINE_CLASS", "Win4WineProxy");
    x11::setProperty32(winId(), "_WIN4WINE_HWND", XCB_ATOM_CARDINAL, {quint32(winId())});
}

void FrameWindow::readHints()
{
    // WM_NORMAL_HINTS: flags, 4 unused, the minimum size, the maximum size
    const QList<quint32> hints = x11::property32(m_client, XCB_ATOM_WM_NORMAL_HINTS);
    const FrameStyle &style = ShellTheme::instance()->frame();
    m_minimum = QSize(qMax(112, 2 * style.border + 1), style.top + style.border + 1);
    if (hints.size() >= 9) {
        if (hints[0] & 16)
            m_minimum = m_minimum.expandedTo(QSize(int(hints[5]) + 2 * style.border, int(hints[6]) + style.top + style.border));
        m_fixedSize = (hints[0] & 16) && (hints[0] & 32) && hints[5] == hints[7] && hints[6] == hints[8];
    }
    // XP's dialogs have only Close, and "?" when they ask for it (Qt's WindowContextHelpButtonHint).
    bool dialog = false;
    for (quint32 type : x11::property32(m_client, x11::atom("_NET_WM_WINDOW_TYPE")))
        dialog = dialog || type == x11::atom("_NET_WM_WINDOW_TYPE_DIALOG");
    bool help = false;
    for (quint32 protocol : x11::property32(m_client, x11::atom("WM_PROTOCOLS")))
        help = help || protocol == x11::atom("_NET_WM_CONTEXT_HELP");
    m_look.canMaximize = !m_fixedSize && !dialog;
    m_look.canMinimize = !dialog;
    m_look.canHelp = help;
}

void FrameWindow::setTitle(const QString &title)
{
    if (title == m_look.title)
        return;
    m_look.title = title;
    setWindowTitle(title);
    update();
}

void FrameWindow::setActive(bool active)
{
    if (active == m_look.active)
        return;
    m_look.active = active;
    update();
}

void FrameWindow::setMaximized(bool maximized)
{
    if (maximized == m_look.maximized)
        return;
    m_look.maximized = maximized;
    setMask(frameShape(size(), ShellTheme::instance()->frame(), m_look.maximized));
    update();
}

void FrameWindow::setIcon(const QImage &icon)
{
    m_look.icon = icon;
    update();
}

void FrameWindow::themeChanged()
{
    readHints();
    setMask(frameShape(size(), ShellTheme::instance()->frame(), m_look.maximized));
    update();
}

void FrameWindow::place(const QRect &client)
{
    if (busy())
        return;
    const FrameStyle &style = ShellTheme::instance()->frame();
    const QRect wanted = client.adjusted(-style.border, -style.top, style.border, style.border);
    if (wanted != geometry())
        setGeometry(wanted);
}

void FrameWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    drawFrame(painter, rect(), ShellTheme::instance()->frame(), m_look);
}

void FrameWindow::resizeEvent(QResizeEvent *)
{
    setMask(frameShape(size(), ShellTheme::instance()->frame(), m_look.maximized));
}

FrameButton FrameWindow::buttonAt(QPoint at) const
{
    const FrameStyle &style = ShellTheme::instance()->frame();
    for (FrameButton button : {FrameButton::Close, FrameButton::Maximize, FrameButton::Minimize, FrameButton::Help}) {
        if (!shown(button, m_look))
            continue;
        if (frameButtonRect(button, rect(), style, m_look.maximized).contains(at))
            return button;
    }
    const int side = ShellTheme::instance()->scaled(ICON_SIZE + 4);
    if (QRect(ICON_X - 2, ICON_Y - 2, side, side).contains(at))
        return FrameButton::System;
    return FrameButton::None;
}

int FrameWindow::edgesAt(QPoint at) const
{
    if (m_look.maximized || m_fixedSize)
        return 0;
    const int border = ShellTheme::instance()->frame().border;
    // The corners are 16 pixels along the frame, as XP's.
    const int corner = ShellTheme::instance()->scaled(16);
    int edges = 0;
    if (at.x() < border || (at.y() < border && at.x() < corner) || (at.y() >= height() - border && at.x() < corner))
        edges |= int(Drag::Left);
    if (at.x() >= width() - border || (at.y() < border && at.x() >= width() - corner) || (at.y() >= height() - border && at.x() >= width() - corner))
        edges |= int(Drag::Right);
    if (at.y() < border || (at.x() < border && at.y() < corner) || (at.x() >= width() - border && at.y() < corner))
        edges |= int(Drag::Top);
    if (at.y() >= height() - border || (at.x() < border && at.y() >= height() - corner) || (at.x() >= width() - border && at.y() >= height() - corner))
        edges |= int(Drag::Bottom);
    return edges;
}

void FrameWindow::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint at = event->position().toPoint();
    const QPoint global = event->globalPosition().toPoint();
    if (m_drag == Drag::Move) {
        move(m_pressGeometry.topLeft() + global - m_pressGlobal);
        return;
    }
    if (m_edges && m_drag != Drag::Button && m_drag != Drag::None) {
        QRect r = m_pressGeometry;
        const QPoint delta = global - m_pressGlobal;
        if (m_edges & int(Drag::Left))
            r.setLeft(qMin(r.left() + delta.x(), r.right() + 1 - m_minimum.width()));
        if (m_edges & int(Drag::Right))
            r.setRight(qMax(r.right() + delta.x(), r.left() - 1 + m_minimum.width()));
        if (m_edges & int(Drag::Top))
            r.setTop(qMin(r.top() + delta.y(), r.bottom() + 1 - m_minimum.height()));
        if (m_edges & int(Drag::Bottom))
            r.setBottom(qMax(r.bottom() + delta.y(), r.top() - 1 + m_minimum.height()));
        setGeometry(r);
        return;
    }
    if (m_drag == Drag::Button) {
        const FrameButton hit = buttonAt(at);
        const FrameButton pressed = hit == m_look.hot ? hit : FrameButton::None;
        if (pressed != m_look.pressed) {
            m_look.pressed = pressed;
            update();
        }
        return;
    }
    const int edges = edgesAt(at);
    if (edges == (int(Drag::Left) | int(Drag::Top)) || edges == (int(Drag::Right) | int(Drag::Bottom)))
        setCursor(Qt::SizeFDiagCursor);
    else if (edges == (int(Drag::Right) | int(Drag::Top)) || edges == (int(Drag::Left) | int(Drag::Bottom)))
        setCursor(Qt::SizeBDiagCursor);
    else if (edges & (int(Drag::Left) | int(Drag::Right)))
        setCursor(Qt::SizeHorCursor);
    else if (edges & (int(Drag::Top) | int(Drag::Bottom)))
        setCursor(Qt::SizeVerCursor);
    else
        setCursor(Qt::ArrowCursor);
    const FrameButton hot = edges ? FrameButton::None : buttonAt(at);
    if (hot != m_look.hot) {
        m_look.hot = hot;
        update();
    }
}

void FrameWindow::leaveEvent(QEvent *)
{
    if (m_look.hot != FrameButton::None) {
        m_look.hot = FrameButton::None;
        update();
    }
}

void FrameWindow::mousePressEvent(QMouseEvent *event)
{
    const QPoint at = event->position().toPoint();
    if (event->button() == Qt::RightButton) {
        showSystemMenu(event->globalPosition().toPoint());
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    m_pressGlobal = event->globalPosition().toPoint();
    m_pressGeometry = geometry();
    m_edges = edgesAt(at);
    const FrameButton button = m_edges ? FrameButton::None : buttonAt(at);
    if (button == FrameButton::System) {
        showSystemMenu(mapToGlobal(QPoint(ICON_X, ICON_Y + ICON_SIZE)));
    } else if (button != FrameButton::None) {
        m_drag = Drag::Button;
        m_look.hot = m_look.pressed = button;
        update();
    } else if (m_edges) {
        m_drag = Drag::Left;
    } else if (!m_look.maximized && at.y() < ShellTheme::instance()->frame().top) {
        m_drag = Drag::Move;
    }
}

void FrameWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const Drag drag = m_drag;
    m_drag = Drag::None;
    m_edges = 0;
    if (drag == Drag::Button) {
        const FrameButton button = m_look.pressed;
        m_look.pressed = FrameButton::None;
        update();
        if (button == FrameButton::Close)
            m_manager->command("close", m_client);
        else if (button == FrameButton::Minimize)
            m_manager->command("minimize", m_client);
        else if (button == FrameButton::Maximize)
            m_manager->command(m_look.maximized ? "restore" : "maximize", m_client);
        else if (button == FrameButton::Help) {
            // The window's own "What's This?" mode
            xcb_client_message_event_t message{};
            message.response_type = XCB_CLIENT_MESSAGE;
            message.format = 32;
            message.window = m_client;
            message.type = x11::atom("WM_PROTOCOLS");
            message.data.data32[0] = x11::atom("_NET_WM_CONTEXT_HELP");
            message.data.data32[1] = XCB_CURRENT_TIME;
            xcb_send_event(x11::connection(), false, m_client, XCB_EVENT_MASK_NO_EVENT, reinterpret_cast<const char *>(&message));
            xcb_flush(x11::connection());
        }
    } else if (drag != Drag::None) {
        // The WM followed each step; this is for what it could not, as proxy.exe does.
        const FrameStyle &style = ShellTheme::instance()->frame();
        m_manager->geometryCommand(m_client, geometry().adjusted(style.border, style.top, -style.border, -style.border));
    }
}

void FrameWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const QPoint at = event->position().toPoint();
    const FrameButton button = buttonAt(at);
    if (button == FrameButton::System) {
        m_manager->command("close", m_client);
    } else if (button == FrameButton::None && !edgesAt(at) && m_look.canMaximize && at.y() < ShellTheme::instance()->frame().top) {
        m_manager->command(m_look.maximized ? "restore" : "maximize", m_client);
    } else {
        mousePressEvent(event);
    }
}

void FrameWindow::showSystemMenu(QPoint at)
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->addAction(tr("&Restore"), this, [this] { m_manager->command("restore", m_client); })->setEnabled(m_look.maximized);
    menu->addAction(tr("Mi&nimize"), this, [this] { m_manager->command("minimize", m_client); });
    menu->addAction(tr("Ma&ximize"), this, [this] { m_manager->command("maximize", m_client); })->setEnabled(!m_look.maximized && m_look.canMaximize);
    menu->addSeparator();
    QAction *close = menu->addAction(tr("&Close"), this, [this] { m_manager->command("close", m_client); });
    QFont bold = close->font();
    bold.setBold(true);
    close->setFont(bold);
    menu->popup(at);
}

FrameManager::FrameManager(const QString &ipc)
    : m_ipc(ipc)
{
    m_watcher.addPath(ipc);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &FrameManager::sync);
    connect(ShellTheme::instance(), &ShellTheme::changed, this, [this] {
        for (FrameWindow *frame : std::as_const(m_frames))
            frame->themeChanged();
        // The clients keep their rectangles; the frames around them change size. The WM
        // first takes the new sizes, or it would size the clients to follow the frames.
        framesChanged();
        QTimer::singleShot(150, this, [this] {
            m_geometry.clear();
            readGeometry();
        });
    });
    sync();
}

void FrameManager::command(const char *op, quint32 client)
{
    const QByteArray line = QByteArray(op) + ' ' + QByteArray::number(client, 16) + '\n';
    ipcfile::write(m_ipc, QStringLiteral("%1-%2.cmd").arg(QCoreApplication::applicationPid()).arg(++m_commands), line);
}

void FrameManager::geometryCommand(quint32 client, const QRect &rect)
{
    QByteArray line = "geometry " + QByteArray::number(client, 16);
    for (int value : {rect.x(), rect.y(), rect.width(), rect.height()})
        line += ' ' + QByteArray::number(value);
    ipcfile::write(m_ipc, QStringLiteral("%1-%2.cmd").arg(QCoreApplication::applicationPid()).arg(++m_commands), line + '\n');
}

void FrameManager::sync()
{
    readWindows();
    readGeometry();
    readIcons();
}

void FrameManager::readWindows()
{
    const QByteArray data = ipcfile::read(m_ipc.filePath(QStringLiteral("windows.tsv")));
    if (data == m_windows)
        return;
    m_windows = data;
    QSet<quint32> seen;
    for (const QByteArray &line : data.split('\n')) {
        // XID, hidden, active, title
        const QList<QByteArray> fields = line.split('\t');
        if (fields.size() < 4)
            continue;
        bool ok = false;
        const quint32 client = fields[0].toUInt(&ok, 16);
        if (!ok)
            continue;
        // XP gives these no caption (splash screens, notifications, menus).
        bool bare = false;
        for (quint32 type : x11::property32(client, x11::atom("_NET_WM_WINDOW_TYPE")))
            for (const char *name : {"_NET_WM_WINDOW_TYPE_SPLASH", "_NET_WM_WINDOW_TYPE_NOTIFICATION", "_NET_WM_WINDOW_TYPE_TOOLTIP",
                                     "_NET_WM_WINDOW_TYPE_DND", "_NET_WM_WINDOW_TYPE_COMBO", "_NET_WM_WINDOW_TYPE_POPUP_MENU",
                                     "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", "_NET_WM_WINDOW_TYPE_MENU"})
                bare = bare || type == x11::atom(name);
        if (bare)
            continue;
        seen.insert(client);
        const QString title = QString::fromUtf8(fields[3]);
        FrameWindow *frame = m_frames.value(client);
        if (!frame) {
            frame = new FrameWindow(this, client, title);
            m_frames.insert(client, frame);
            // bridge-wm keeps it unmapped until frames.tsv pairs it with its client.
            frame->show();
            // Its client may not move again: its place comes from the file as it is.
            m_geometry.clear();
        }
        frame->setTitle(title);
        frame->setActive(fields[2] == "1");
    }
    for (auto it = m_frames.begin(); it != m_frames.end();) {
        if (seen.contains(it.key())) {
            ++it;
        } else {
            m_iconTimes.remove(it.key());
            delete it.value();
            it = m_frames.erase(it);
        }
    }
    framesChanged();
}

void FrameManager::readGeometry()
{
    const QByteArray data = ipcfile::read(m_ipc.filePath(QStringLiteral("geometry.tsv")));
    if (data == m_geometry)
        return;
    m_geometry = data;
    bool changed = false;
    for (const QByteArray &line : data.split('\n')) {
        // XID x y width height maximized and the work area
        const QList<QByteArray> fields = line.split(' ');
        if (fields.size() < 6)
            continue;
        FrameWindow *frame = m_frames.value(fields[0].toUInt(nullptr, 16));
        if (!frame)
            continue;
        const bool maximized = fields[5] == "1";
        if (frame->maximized() != maximized) {
            frame->setMaximized(maximized);
            changed = true;
        }
        frame->place(QRect(fields[1].toInt(), fields[2].toInt(), fields[3].toInt(), fields[4].toInt()));
    }
    if (changed)
        framesChanged();
}

void FrameManager::readIcons()
{
    for (auto it = m_frames.cbegin(); it != m_frames.cend(); ++it) {
        const QString path = m_ipc.filePath(QStringLiteral("icon-%1-small.argb").arg(it.key(), 0, 16));
        const QDateTime time = QFileInfo(path).lastModified();
        if (m_iconTimes.contains(it.key()) && m_iconTimes[it.key()] == time)
            continue;
        m_iconTimes[it.key()] = time;
        const int size = ShellTheme::instance()->scaled(ICON_SIZE);
        QImage icon = ipcfile::readIcon(path);
        // XP's dialogs have no icon of their own, and none in their title bar.
        bool dialog = false;
        for (quint32 type : x11::property32(it.key(), x11::atom("_NET_WM_WINDOW_TYPE")))
            dialog = dialog || type == x11::atom("_NET_WM_WINDOW_TYPE_DIALOG");
        if (icon.isNull() && !dialog)
            icon = ShellTheme::instance()->defaultIcon(size);
        else if (icon.width() != size)
            icon = icon.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        it.value()->setIcon(icon);
    }
}

void FrameManager::framesChanged()
{
    const FrameStyle &style = ShellTheme::instance()->frame();
    QByteArray text;
    for (auto it = m_frames.cbegin(); it != m_frames.cend(); ++it) {
        // Frame, client, the frame's four sides, maximized, minimized and how far a maximized frame goes off the screen
        text += QByteArray::number(quint32(it.value()->winId()), 16) + ' ' + QByteArray::number(it.key(), 16);
        for (int value : {style.border, style.border, style.top, style.border, int(it.value()->maximized()), 0, style.border, style.border})
            text += ' ' + QByteArray::number(value);
        text += '\n';
    }
    if (text != m_written) {
        m_written = text;
        ipcfile::write(m_ipc, QStringLiteral("frames.tsv"), text);
    }
}
