// SPDX-License-Identifier: LGPL-2.1-or-later
#include "taskpane.h"

#include "theme.h"
#include "xpfiles.h"

#include <QFileInfo>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>

namespace {

// At 96 DPI, from the shell style's UI file
const int MARGIN = 12, GROUP_GAP = 15, HEADER = 25, BODY_PADDING_X = 13, BODY_PADDING_Y = 10, ROW_GAP = 4, ICON = 16, ICON_GAP = 6;
// The header bitmaps and arrows of blue_ss.dll, home_ss.dll and metal_ss.dll
const int BITMAP_PRIMARY_HEADER = 110, BITMAP_HEADER = 112;
const int ARROW_UP = 100, ARROW_UP_HOT = 101, ARROW_DOWN = 102, ARROW_DOWN_HOT = 103;
const int PRIMARY_ARROW_UP = 104, PRIMARY_ARROW_UP_HOT = 105, PRIMARY_ARROW_DOWN = 106, PRIMARY_ARROW_DOWN_HOT = 107;

const int TITLE_ICON = 32;

// Magenta is see-through in these bitmaps.
QImage keyed(const QImage &source)
{
    QImage image = source.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
            if ((row[x] & 0xffffff) == 0xff00ff)
                row[x] = 0;
    }
    return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QColor rgbAfter(const QString &text, const QRegularExpression &pattern, const QColor &fallback)
{
    const auto match = pattern.match(text);
    if (!match.hasMatch())
        return fallback;
    return QColor(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt());
}

} // namespace

TaskPane::TaskPane(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    loadStyle();
    connect(ShellTheme::instance(), &ShellTheme::changed, this, [this] {
        loadStyle();
        layoutGroups();
        update();
    });
}

void TaskPane::loadStyle()
{
    const LookChoice look = ShellTheme::instance()->choice();
    const QString file = ShellTheme::shellStylePath(look);
    m_top = QColor(123, 162, 231);
    m_bottom = QColor(99, 117, 214);
    m_primaryBody = QColor(239, 243, 255);
    m_body = QColor(214, 223, 247);
    m_link = QColor(33, 93, 198);
    m_hotLink = QColor(66, 142, 255);
    for (QImage &arrow : m_arrows)
        arrow = QImage();
    m_primaryHeader = m_header = QImage();
    const auto resources = xpfiles::resources(file);
    if (!resources) {
        // Windows Classic: the system colours
        ShellTheme *theme = ShellTheme::instance();
        m_top = m_bottom = theme->system(QStringLiteral("btnface"), QColor(212, 208, 200));
        m_primaryBody = m_body = theme->system(QStringLiteral("window"), Qt::white);
        m_link = theme->system(QStringLiteral("highlight"), QColor(10, 36, 106));
        m_hotLink = m_link.lighter(150);
        return;
    }
    const QByteArray ui = resources->find(QStringLiteral("UIFILE"), QStringLiteral("1"));
    const QString text = QString::fromLatin1(ui);
    const auto group = QRegularExpression::CaseInsensitiveOption;
    const QRegularExpression gradient(QStringLiteral("sectionlist\\)\\]\\s*\\{\\s*background:\\s*gradient\\(argb\\(0,\\s*(\\d+),\\s*(\\d+),\\s*(\\d+)\\),\\s*argb\\(0,\\s*(\\d+),\\s*(\\d+),\\s*(\\d+)\\)"), group);
    if (const auto match = gradient.match(text); match.hasMatch()) {
        m_top = QColor(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt());
        m_bottom = QColor(match.captured(4).toInt(), match.captured(5).toInt(), match.captured(6).toInt());
    }
    m_primaryBody = rgbAfter(text, QRegularExpression(QStringLiteral("watermark\\)\\]\\s*\\{\\s*background:\\s*rgb\\((\\d+),\\s*(\\d+),\\s*(\\d+)\\)"), group), m_primaryBody);
    m_body = rgbAfter(text, QRegularExpression(QStringLiteral("resid=sectionss>.*?tasklist\\)\\]\\s*\\{\\s*background:\\s*rgb\\((\\d+),\\s*(\\d+),\\s*(\\d+)\\)"), group | QRegularExpression::DotMatchesEverythingOption), m_body);
    m_link = rgbAfter(text, QRegularExpression(QStringLiteral("resid=sectiontaskss>.*?foreground:\\s*rgb\\((\\d+),\\s*(\\d+),\\s*(\\d+)\\)"), group | QRegularExpression::DotMatchesEverythingOption), m_link);
    m_hotLink = rgbAfter(text, QRegularExpression(QStringLiteral("resid=sectiontaskss>.*?mousefocused\\]\\s*\\{\\s*foreground:\\s*rgb\\((\\d+),\\s*(\\d+),\\s*(\\d+)\\)"), group | QRegularExpression::DotMatchesEverythingOption), m_hotLink);
    m_primaryHeader = keyed(xpfiles::bitmap(*resources, BITMAP_PRIMARY_HEADER));
    m_header = keyed(xpfiles::bitmap(*resources, BITMAP_HEADER));
    const int arrows[] = {ARROW_UP, ARROW_UP_HOT, ARROW_DOWN, ARROW_DOWN_HOT, PRIMARY_ARROW_UP, PRIMARY_ARROW_UP_HOT, PRIMARY_ARROW_DOWN, PRIMARY_ARROW_DOWN_HOT};
    for (int i = 0; i < 8; ++i)
        m_arrows[i] = keyed(xpfiles::bitmap(*resources, arrows[i]));
}

QSize TaskPane::sizeHint() const
{
    return QSize(ShellTheme::instance()->scaled(210), m_bodies.isEmpty() ? 100 : m_bodies.last().bottom() + MARGIN);
}

void TaskPane::setGroups(const QList<PaneGroup> &groups)
{
    m_groups = groups;
    m_hot = {-1, -1};
    layoutGroups();
    update();
}

void TaskPane::resizeEvent(QResizeEvent *)
{
    layoutGroups();
}

void TaskPane::layoutGroups()
{
    ShellTheme *theme = ShellTheme::instance();
    const QFont normal = theme->font(QStringLiteral("Tahoma, 8"));
    const QFontMetrics metrics(normal);
    const int margin = theme->scaled(MARGIN), header = theme->scaled(HEADER);
    const int padX = theme->scaled(BODY_PADDING_X), padY = theme->scaled(BODY_PADDING_Y), gap = theme->scaled(ROW_GAP);
    const int icon = theme->scaled(ICON), iconGap = theme->scaled(ICON_GAP);
    const int groupWidth = qMax(50, width() - 2 * margin);
    m_headers.clear();
    m_bodies.clear();
    m_rows.clear();
    int y = margin;
    for (int g = 0; g < m_groups.size(); ++g) {
        const PaneGroup &group = m_groups[g];
        const int headerHeight = group.icon.isNull() ? header : qMax(header, theme->scaled(TITLE_ICON));
        m_headers.append(QRect(margin, y, groupWidth, headerHeight));
        y += headerHeight;
        const QRect body(margin, y, groupWidth, 0);
        if (m_collapsed.value(group.title)) {
            m_bodies.append(body);
        } else {
            int rowY = y + padY;
            for (int t = 0; t < group.tasks.size(); ++t) {
                const PaneTask &task = group.tasks[t];
                const int textX = margin + padX + (task.icon.isNull() ? 0 : icon + iconGap);
                const int textWidth = margin + groupWidth - padX - textX;
                const QRect bounds = metrics.boundingRect(QRect(textX, rowY, textWidth, 1000), Qt::TextWordWrap, task.text);
                const int height = qMax(bounds.height(), task.icon.isNull() ? 0 : icon);
                Row row;
                row.rect = QRect(margin + padX, rowY, groupWidth - 2 * padX, height);
                row.group = g;
                row.task = t;
                m_rows.append(row);
                rowY += height + gap;
            }
            m_bodies.append(QRect(margin, y, groupWidth, rowY - y - gap + padY));
            y = m_bodies.last().bottom() + 1;
        }
        y += theme->scaled(GROUP_GAP);
    }
    setMinimumHeight(y);
}

void TaskPane::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    ShellTheme *theme = ShellTheme::instance();
    QLinearGradient gradient(0, 0, 0, height());
    gradient.setColorAt(0, m_top);
    gradient.setColorAt(1, m_bottom);
    painter.fillRect(rect(), gradient);
    const QFont normal = theme->font(QStringLiteral("Tahoma, 8")), bold = theme->font(QStringLiteral("Tahoma, 8, bold"));
    for (int g = 0; g < m_groups.size(); ++g) {
        const PaneGroup &group = m_groups[g];
        const QRect header = m_headers[g], body = m_bodies[g];
        const QImage &art = group.primary ? m_primaryHeader : m_header;
        const bool hotHeader = m_hot == qMakePair(g, -1);
        if (!art.isNull()) {
            // Its 6 pixels at each end stay, the middle stretches.
            const int edge = 6, w = art.width(), h = art.height();
            painter.drawImage(QRect(header.x(), header.y() + header.height() - h, edge, h), art, QRect(0, 0, edge, h));
            painter.drawImage(QRect(header.x() + edge, header.y() + header.height() - h, header.width() - 2 * edge, h), art, QRect(edge, 0, w - 2 * edge, h));
            painter.drawImage(QRect(header.right() + 1 - edge, header.y() + header.height() - h, edge, h), art, QRect(w - edge, 0, edge, h));
        } else {
            painter.fillRect(header, group.primary ? theme->system(QStringLiteral("highlight"), Qt::darkBlue) : theme->system(QStringLiteral("window"), Qt::white));
        }
        painter.setFont(bold);
        const QColor title = group.primary ? QColor(Qt::white) : m_link;
        painter.setPen(hotHeader ? m_hotLink : title);
        const int titleX = group.icon.isNull() ? theme->scaled(12) : theme->scaled(TITLE_ICON);
        painter.drawText(header.adjusted(titleX, art.isNull() ? 0 : header.height() - art.height(), -theme->scaled(25), 0), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                         painter.fontMetrics().elidedText(group.title, Qt::ElideRight, header.width() - theme->scaled(35)));
        const bool collapsed = m_collapsed.value(group.title);
        const int arrow = (group.primary ? 4 : 0) + (collapsed ? 2 : 0) + (hotHeader ? 1 : 0);
        if (!m_arrows[arrow].isNull())
            painter.drawImage(header.right() - m_arrows[arrow].width() - 1, header.y() + header.height() - m_arrows[arrow].height(), m_arrows[arrow]);
        if (!group.icon.isNull())
            group.icon.paint(&painter, QRect(header.x(), header.y(), theme->scaled(TITLE_ICON), theme->scaled(TITLE_ICON)));
        if (collapsed)
            continue;
        painter.fillRect(body, group.primary ? m_primaryBody : m_body);
        painter.setPen(Qt::white);
        painter.drawLine(body.bottomLeft(), body.bottomRight());
        painter.drawLine(body.topLeft(), body.bottomLeft());
        painter.drawLine(body.topRight(), body.bottomRight());
    }
    const int icon = theme->scaled(ICON), iconGap = theme->scaled(ICON_GAP);
    for (const Row &row : std::as_const(m_rows)) {
        const PaneTask &task = m_groups[row.group].tasks[row.task];
        const bool hot = m_hot == qMakePair(row.group, row.task) && task.action;
        int x = row.rect.x();
        if (!task.icon.isNull()) {
            painter.drawPixmap(x, row.rect.y(), task.icon.pixmap(icon, icon));
            x += icon + iconGap;
        }
        QFont font = task.bold ? bold : normal;
        font.setUnderline(hot);
        painter.setFont(font);
        painter.setPen(task.action ? (hot ? m_hotLink : m_link) : QColor(Qt::black));
        painter.drawText(QRect(x, row.rect.y(), row.rect.right() - x, row.rect.height()), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, task.text);
    }
}

QPair<int, int> TaskPane::hit(QPoint at) const
{
    for (int g = 0; g < m_headers.size(); ++g)
        if (m_headers[g].contains(at))
            return {g, -1};
    for (const Row &row : m_rows)
        if (row.rect.contains(at) && m_groups[row.group].tasks[row.task].action)
            return {row.group, row.task};
    return {-1, -1};
}

void TaskPane::mouseMoveEvent(QMouseEvent *event)
{
    const auto now = hit(event->position().toPoint());
    if (now != m_hot) {
        m_hot = now;
        setCursor(now.first >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void TaskPane::leaveEvent(QEvent *)
{
    m_hot = {-1, -1};
    update();
}

void TaskPane::mouseReleaseEvent(QMouseEvent *event)
{
    const auto at = hit(event->position().toPoint());
    if (at.first < 0 || event->button() != Qt::LeftButton)
        return;
    if (at.second < 0) {
        const QString title = m_groups[at.first].title;
        m_collapsed[title] = !m_collapsed.value(title);
        layoutGroups();
        update();
    } else {
        // (The action may replace the groups.)
        const auto action = m_groups[at.first].tasks[at.second].action;
        action();
    }
}
