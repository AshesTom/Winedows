// SPDX-License-Identifier: LGPL-2.1-or-later
#include "balloon.h"

#include "sounds.h"
#include "theme.h"
#include "x11.h"
#include "xpfiles.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>

namespace {

using w4w::Part;

// Shell_NotifyIcon's bounds for uTimeout
const int MINIMUM_TIMEOUT = 10000, MAXIMUM_TIMEOUT = 30000;
// The countdown waits while the user has not touched the keyboard or the mouse for this long.
const int AWAY_AFTER = 10000, TICK = 250;
// At 96 DPI, measured on XP's own balloons (Winedows' session). The body's corners are cut at
// 45 degrees: where each of their rows starts.
const int CORNER[] = {4, 3, 2, 1};
const int CORNER_ROWS = 4;
// The stem would meet its straight edge 20 rows below the body's border, one column a row,
// 15 columns in from the body's far side, at 7 pixels into the icon and 6 into it from the
// taskbar's side; the window ends 16 rows down, leaving the stem open over the icon.
const int STEM = 20, STEM_SHOWN = 16, STEM_INSET = 15, ANCHOR_X = 7, ANCHOR_Y = 6;
// The icon and the text 12 in from the border, the title row 7 down, the title 8 after its
// icon, the text 5 under the title row and 9 over the border; the close button 4 down and
// 5 in from the right, 8 after the title; the text keeps 12 from the right.
const int EDGE = 12, TOP = 7, ICON = 16, TITLE_GAP = 8, TEXT_GAP = 5, BOTTOM = 9;
const int CLOSE = 18, CLOSE_TOP = 4, CLOSE_RIGHT = 5, CLOSE_GAP = 8, TEXT_RIGHT = 12;
// The widest the text grows before it wraps
const int TEXT_WIDTH = 250;
// user32.dll's small icons: OIC_INFORMATION, OIC_WARNING, OIC_ERROR
const int ICON_INFO = 104, ICON_WARNING = 101, ICON_ERROR = 103;

ShellTheme *theme()
{
    return ShellTheme::instance();
}

int scaled(int pixels)
{
    return theme()->scaled(pixels);
}

QImage systemIcon(int group, int size)
{
    static const auto user32 = xpfiles::resources(xpfiles::find(QStringLiteral("user32.dll")));
    return user32 ? xpfiles::icon(*user32, group, size) : QImage();
}

// A row's inset from the body's side: the corners' rows, scaled.
int inset(int row, int height)
{
    const int fromEdge = qMin(row, height - 1 - row) * 96 / theme()->dpi();
    return fromEdge < CORNER_ROWS ? scaled(CORNER[fromEdge]) : 0;
}

} // namespace

Balloons::Balloons(QObject *parent)
    : QObject(parent)
{
    m_ticks.setInterval(TICK);
    connect(&m_ticks, &QTimer::timeout, this, &Balloons::tick);
}

Balloons::~Balloons()
{
    delete m_window;
}

quint64 Balloons::show(const BalloonTip &tip)
{
    const Entry entry{++m_serials, tip};
    for (Entry &queued : m_queue)
        if (queued.tip.icon == tip.icon) {
            if (queued.tip.closed)
                queued.tip.closed(BalloonTip::Cancelled);
            queued = entry;
            return entry.serial;
        }
    if (m_window && m_current.tip.icon == tip.icon) {
        m_queue.prepend(entry);
        finish(BalloonTip::Cancelled);
    } else {
        m_queue.append(entry);
        if (!m_window)
            next();
    }
    return entry.serial;
}

void Balloons::cancel(quint64 serial)
{
    if (m_window && m_current.serial == serial) {
        finish(BalloonTip::Cancelled);
        return;
    }
    for (int i = 0; i < m_queue.size(); ++i)
        if (m_queue[i].serial == serial) {
            const BalloonTip tip = m_queue.takeAt(i).tip;
            if (tip.closed)
                tip.closed(BalloonTip::Cancelled);
            return;
        }
}

void Balloons::iconsChanged()
{
    if (!m_window)
        return;
    const QRect icon = m_locator ? m_locator(m_current.tip.icon) : QRect();
    if (icon.isEmpty())
        finish(BalloonTip::Cancelled);
    else
        m_window->place(icon);
}

void Balloons::next()
{
    while (!m_window && !m_queue.isEmpty()) {
        m_current = m_queue.takeFirst();
        const QRect icon = m_locator ? m_locator(m_current.tip.icon) : QRect();
        if (icon.isEmpty()) {
            if (m_current.tip.closed)
                m_current.tip.closed(BalloonTip::Cancelled);
            continue;
        }
        m_window = new BalloonWindow(m_current.tip);
        connect(m_window, &BalloonWindow::clicked, this, [this] { finish(BalloonTip::Clicked); });
        connect(m_window, &BalloonWindow::dismissed, this, [this] { finish(BalloonTip::Dismissed); });
        m_window->place(icon);
        m_window->show();
        if (m_current.tip.sound)
            sounds::play(QStringLiteral("Windows XP Balloon.wav"));
        m_shownFor = 0;
        m_lastTick.start();
        m_ticks.start();
    }
}

void Balloons::finish(BalloonTip::Reason reason)
{
    if (!m_window)
        return;
    m_ticks.stop();
    m_window->hide();
    m_window->deleteLater();
    m_window = nullptr;
    const BalloonTip tip = m_current.tip;
    m_current = Entry{};
    if (tip.closed)
        tip.closed(reason);
    // The next one once this one's handlers have run (they may queue another of the same icon).
    QTimer::singleShot(0, this, &Balloons::next);
}

void Balloons::tick()
{
    const qint64 elapsed = m_lastTick.restart();
    if (x11::idleMilliseconds() < AWAY_AFTER)
        m_shownFor += elapsed;
    if (m_shownFor >= qBound(MINIMUM_TIMEOUT, m_current.tip.timeout, MAXIMUM_TIMEOUT))
        finish(BalloonTip::TimedOut);
}

BalloonWindow::BalloonWindow(const BalloonTip &tip)
    : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus)
    , m_tip(tip)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setMouseTracking(true);
    setWindowTitle(tip.title);
    const int size = scaled(ICON);
    switch (tip.kind) {
    case BalloonTip::Info: m_icon = systemIcon(ICON_INFO, size); break;
    case BalloonTip::Warning: m_icon = systemIcon(ICON_WARNING, size); break;
    case BalloonTip::Error: m_icon = systemIcon(ICON_ERROR, size); break;
    case BalloonTip::User: m_icon = tip.image.isNull() ? QImage() : tip.image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation); break;
    case BalloonTip::None: break;
    }
    // The style's status font, which XP's tooltips use; the title in its bold.
    QString spec = QStringLiteral("Tahoma, 8");
    if (w4w::Renderer *renderer = theme()->renderer())
        spec = renderer->theme().sysMetric(QStringLiteral("StatusFont")).value_or(spec);
    m_font = theme()->font(spec);
    // Its bold through the style's font reader, which smooths each face as its gasp table asks
    m_titleFont = theme()->font(spec.contains(QLatin1String("bold"), Qt::CaseInsensitive) ? spec : spec + QStringLiteral(", bold"));
    layout();
}

void BalloonWindow::layout()
{
    const QFontMetrics text(m_font), title(m_titleFont);
    const QRect wrapped = text.boundingRect(QRect(0, 0, scaled(TEXT_WIDTH), 100000), Qt::AlignLeft | Qt::TextWordWrap | Qt::TextExpandTabs, m_tip.text);
    const int closeSpace = scaled(CLOSE_GAP + CLOSE + CLOSE_RIGHT);
    int width = scaled(EDGE) + wrapped.width() + scaled(TEXT_RIGHT);
    int y = scaled(TOP);
    if (!m_tip.title.isEmpty()) {
        const int titleX = scaled(EDGE) + (m_icon.isNull() ? 0 : scaled(ICON + TITLE_GAP));
        const int rowHeight = qMax(m_icon.isNull() ? 0 : scaled(ICON), title.height());
        m_titleRect = QRect(titleX, y + (rowHeight - title.height()) / 2, title.horizontalAdvance(m_tip.title), title.height());
        width = qMax(width, m_titleRect.right() + 1 + closeSpace);
        y += rowHeight + scaled(TEXT_GAP);
    } else {
        // Without a title, the text's first lines keep clear of the close button.
        width = qMax(width, scaled(EDGE) + wrapped.width() + closeSpace);
    }
    m_textRect = QRect(scaled(EDGE), y, wrapped.width(), wrapped.height());
    m_body = QSize(width, m_textRect.bottom() + 1 + scaled(BOTTOM) + 1);
}

void BalloonWindow::place(const QRect &icon)
{
    const QScreen *screen = QGuiApplication::screenAt(icon.center());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect room = screen->geometry();
    m_stemDown = icon.center().y() >= room.center().y();
    m_stemRight = icon.center().x() >= room.center().x();
    const QPoint anchor(icon.left() + scaled(ANCHOR_X), m_stemDown ? icon.top() + scaled(ANCHOR_Y) : icon.bottom() - scaled(ANCHOR_Y));
    // The body beside the stem's straight edge, then kept on the screen, the stem moving along it.
    int left = m_stemRight ? anchor.x() + scaled(STEM_INSET) - (m_body.width() - 1) : anchor.x() - scaled(STEM_INSET);
    left = qBound(room.left(), left, room.right() + 1 - m_body.width());
    const int lowest = scaled(CORNER_ROWS) + (m_stemRight ? scaled(STEM) : 0);
    const int highest = m_body.width() - 1 - scaled(CORNER_ROWS) - (m_stemRight ? 0 : scaled(STEM));
    m_stemX = qBound(lowest, anchor.x() - left, qMax(lowest, highest));
    const int top = m_stemDown ? anchor.y() - scaled(STEM) - (m_body.height() - 1) : anchor.y() + scaled(STEM - STEM_SHOWN);
    setGeometry(anchor.x() - m_stemX, top, m_body.width(), m_body.height() + scaled(STEM_SHOWN));
    setMask(shape() & rect());
    update();
}

// The whole balloon, its stem to the tip (beyond the window)
QRegion BalloonWindow::shape() const
{
    const int stem = scaled(STEM);
    const int bodyTop = m_stemDown ? 0 : scaled(STEM_SHOWN);
    QRegion region;
    for (int row = 0; row < m_body.height(); ++row) {
        const int in = inset(row, m_body.height());
        region += QRect(in, bodyTop + row, m_body.width() - 2 * in, 1);
    }
    // One column nearer the straight edge on each row away from the body
    for (int k = 1; k <= stem; ++k) {
        const int y = m_stemDown ? m_body.height() - 1 + k : bodyTop - k;
        const int slanted = m_stemRight ? m_stemX - stem + k : m_stemX + stem - k;
        region += QRect(qMin(slanted, m_stemX), y, qAbs(slanted - m_stemX) + 1, 1);
    }
    return region;
}

QRect BalloonWindow::closeRect() const
{
    const int bodyTop = m_stemDown ? 0 : scaled(STEM_SHOWN);
    return QRect(m_body.width() - scaled(CLOSE_RIGHT + CLOSE), bodyTop + scaled(CLOSE_TOP), scaled(CLOSE), scaled(CLOSE));
}

void BalloonWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    const QColor background = theme()->system(QStringLiteral("infowindow"), QColor(255, 255, 225));
    const QColor foreground = theme()->system(QStringLiteral("infotext"), Qt::black);
    const QRegion region = shape();
    // FrameRgn's one-pixel frame: the pixels of the region with a side outside it
    const QRegion inner = region & region.translated(1, 0) & region.translated(-1, 0) & region.translated(0, 1) & region.translated(0, -1);
    for (const QRect &rect : inner)
        painter.fillRect(rect, background);
    for (const QRect &rect : region - inner)
        painter.fillRect(rect, foreground);

    const QPoint body(0, m_stemDown ? 0 : scaled(STEM_SHOWN));
    if (!m_tip.title.isEmpty()) {
        if (!m_icon.isNull())
            painter.drawImage(body + QPoint(scaled(EDGE), scaled(TOP) + (qMax(scaled(ICON), QFontMetrics(m_titleFont).height()) - scaled(ICON)) / 2), m_icon);
        painter.setFont(m_titleFont);
        painter.setPen(foreground);
        painter.drawText(m_titleRect.translated(body), Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, m_tip.title);
    }
    painter.setFont(m_font);
    painter.setPen(foreground);
    painter.drawText(m_textRect.translated(body), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap | Qt::TextExpandTabs, m_tip.text);
    drawClose(painter);
}

void BalloonWindow::drawClose(QPainter &painter) const
{
    const QRect rect = closeRect();
    // Pressed, then dragged off it: back to normal until the mouse comes back
    const int state = m_hot ? (m_pressed ? 3 : 2) : 1;
    if (w4w::Renderer *renderer = theme()->renderer())
        if (renderer->draw(&painter, rect, Part{QStringLiteral("tooltip"), QStringLiteral("close"), {}, state, {}}))
            return;
    // Windows Classic: a flat button that rises under the mouse and sinks when pressed
    const QRect button = rect.adjusted(scaled(2), scaled(2), -scaled(2), -scaled(2));
    if (state == 3)
        theme()->sunken(painter, button);
    else if (state == 2)
        theme()->raised(painter, button);
    QFont marlett(QStringLiteral("Marlett"));
    marlett.setPixelSize(button.height() - scaled(5));
    painter.setFont(marlett);
    painter.setPen(theme()->system(QStringLiteral("infotext"), Qt::black));
    painter.drawText(button.translated(state == 3 ? QPoint(1, 1) : QPoint()), Qt::AlignCenter, QStringLiteral("r"));
}

void BalloonWindow::mouseMoveEvent(QMouseEvent *event)
{
    const bool hot = closeRect().contains(event->position().toPoint());
    if (hot != m_hot) {
        m_hot = hot;
        update(closeRect());
    }
}

void BalloonWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && closeRect().contains(event->position().toPoint())) {
        m_pressed = true;
        update(closeRect());
    }
}

void BalloonWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const bool onClose = closeRect().contains(event->position().toPoint());
    if (m_pressed) {
        m_pressed = false;
        update(closeRect());
        if (onClose)
            Q_EMIT dismissed();
    } else if (shape().contains(event->position().toPoint())) {
        Q_EMIT clicked();
    }
}

void BalloonWindow::leaveEvent(QEvent *)
{
    if (m_hot) {
        m_hot = false;
        update(closeRect());
    }
}
