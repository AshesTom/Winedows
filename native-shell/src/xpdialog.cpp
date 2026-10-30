// SPDX-License-Identifier: LGPL-2.1-or-later
#include "xpdialog.h"

#include "theme.h"
#include "xpfiles.h"

#include <QGuiApplication>
#include <QScreen>

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QSlider>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionTab>
#include <QVBoxLayout>
#include <QPushButton>
#include <QRadioButton>
#include <QtEndian>

#include <algorithm>

namespace xpdialog {
namespace {

// MS Shell Dlg (Tahoma 8) has dialog base units of 6 x 13 pixels at 96 DPI.
int unitX(int value) { return (value * 6 + 2) / 4; }
int unitY(int value) { return (value * 13 + 4) / 8; }

class Reader
{
public:
    explicit Reader(const QByteArray &data) : m_data(data) {}
    bool ok() const { return m_ok; }
    int at() const { return m_at; }
    void seek(int at) { m_at = at; }
    void align() { m_at = (m_at + 3) & ~3; }
    quint16 u16()
    {
        if (m_at + 2 > m_data.size()) {
            m_ok = false;
            return 0;
        }
        const quint16 value = qFromLittleEndian<quint16>(m_data.constData() + m_at);
        m_at += 2;
        return value;
    }
    quint32 u32()
    {
        const quint32 low = u16();
        return low | quint32(u16()) << 16;
    }
    // A sz_Or_Ord: text, or a number as "#N" for the standard classes' atoms.
    QString text(int *ordinal = nullptr)
    {
        const quint16 first = u16();
        if (first == 0xffff) {
            const quint16 number = u16();
            if (ordinal)
                *ordinal = number;
            return QString();
        }
        QString out;
        for (quint16 c = first; c && m_ok; c = u16())
            out += QChar(c);
        return out;
    }

private:
    const QByteArray &m_data;
    int m_at = 0;
    bool m_ok = true;
};

} // namespace

Page build(const w4w::PeResources &pe, int id, QWidget *parent)
{
    Page page;
    const QByteArray data = pe.find(QStringLiteral("5"), QString::number(id));
    Reader r(data);
    if (data.size() < 18)
        return page;
    // DLGTEMPLATEEX starts with its version and 0xffff; DLGTEMPLATE with its style.
    const bool extended = qFromLittleEndian<quint16>(data.constData()) == 1 && qFromLittleEndian<quint16>(data.constData() + 2) == 0xffff;
    r.seek(extended ? 12 : 0);
    const quint32 style = r.u32();
    if (!extended)
        r.u32();
    const int count = r.u16();
    r.u16();
    r.u16();
    const int width = qint16(r.u16());
    const int height = qint16(r.u16());
    r.text();
    r.text();
    page.caption = r.text();
    if (style & 0x40) {
        r.seek(r.at() + (extended ? 6 : 2));
        r.text();
    }
    parent->setFixedSize(unitX(width), unitY(height));
    QWidget *previous = nullptr;
    for (int i = 0; i < count && r.ok(); ++i) {
        r.align();
        quint32 controlStyle = 0;
        if (extended) {
            r.u32();
            r.u32();
            controlStyle = r.u32();
        } else {
            controlStyle = r.u32();
            r.u32();
        }
        const int x = qint16(r.u16()), y = qint16(r.u16()), cx = qint16(r.u16()), cy = qint16(r.u16());
        const int control = extended ? int(r.u32()) : int(r.u16());
        int ordinal = 0, titleOrdinal = 0;
        const QString className = r.text(&ordinal);
        const QString text = r.text(&titleOrdinal);
        const int extra = r.u16();
        r.seek(r.at() + extra);
        // 0x80 Button, 0x81 Edit, 0x82 Static, 0x85 ComboBox
        QWidget *widget = nullptr;
        if (ordinal == 0x80 || className.compare(QLatin1String("Button"), Qt::CaseInsensitive) == 0) {
            const int kind = controlStyle & 0xf;
            if (kind == 7)
                widget = new QGroupBox(text, parent);
            else if (kind == 2 || kind == 3)
                widget = new QCheckBox(text, parent);
            else if (kind == 4 || kind == 9)
                widget = new QRadioButton(text, parent);
            else
                widget = new QPushButton(text, parent);
        } else if (ordinal == 0x82 || className.compare(QLatin1String("Static"), Qt::CaseInsensitive) == 0) {
            auto *label = new QLabel(text, parent);
            label->setWordWrap(true);
            label->setAlignment(controlStyle & 1 ? Qt::AlignHCenter | Qt::AlignTop : Qt::AlignLeft | Qt::AlignTop);
            // A picture of the file, by its number: SS_BITMAP or SS_ICON
            const int kind = controlStyle & 0x1f;
            if (titleOrdinal && (kind == 0x0e || kind == 0x03)) {
                QImage image = kind == 0x0e ? xpfiles::bitmap(pe, titleOrdinal) : xpfiles::icon(pe, titleOrdinal, 32);
                if (!image.isNull())
                    label->setPixmap(QPixmap::fromImage(image));
            }
            widget = label;
        } else if (ordinal == 0x85 || className.compare(QLatin1String("ComboBox"), Qt::CaseInsensitive) == 0) {
            auto *combo = new QComboBox(parent);
            // CBS_DROPDOWN has an edit; CBS_DROPDOWNLIST does not.
            combo->setEditable((controlStyle & 3) == 2);
            combo->setInsertPolicy(QComboBox::NoInsert);
            // A combo's height is its list's in the resource.
            widget = combo;
        } else if (ordinal == 0x81 || className.compare(QLatin1String("Edit"), Qt::CaseInsensitive) == 0) {
            // Read-only without WS_BORDER, an edit is a line of text.
            if ((controlStyle & 0x800) && !(controlStyle & 0x00800000)) {
                auto *line = new QLabel(text, parent);
                line->setAlignment(Qt::AlignLeft | Qt::AlignTop);
                line->setTextInteractionFlags(Qt::TextSelectableByMouse);
                widget = line;
            } else {
                auto *edit = new QLineEdit(text, parent);
                edit->setReadOnly(controlStyle & 0x800);
                widget = edit;
            }
        } else if (className.compare(QLatin1String("Link Window"), Qt::CaseInsensitive) == 0 || className.compare(QLatin1String("SysLink"), Qt::CaseInsensitive) == 0) {
            QString words = text;
            words.remove(QRegularExpression(QStringLiteral("</?[Aa][^>]*>")));
            auto *label = new QLabel(words, parent);
            label->setWordWrap(true);
            label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
            widget = label;
        } else if (className.compare(QLatin1String("msctls_trackbar32"), Qt::CaseInsensitive) == 0) {
            auto *slider = new QSlider(Qt::Horizontal, parent);
            slider->setTickPosition(QSlider::TicksBelow);
            widget = slider;
        } else {
            widget = new QWidget(parent);
        }
        if (auto *label = qobject_cast<QLabel *>(previous); label && (qobject_cast<QComboBox *>(widget) || qobject_cast<QLineEdit *>(widget) || qobject_cast<QSlider *>(widget)))
            label->setBuddy(widget);
        widget->setGeometry(unitX(x), unitY(y), unitX(cx), qobject_cast<QComboBox *>(widget) ? 21 : unitY(cy));
        // WS_VISIBLE and WS_DISABLED
        widget->setVisible(controlStyle & 0x10000000);
        widget->setEnabled(!(controlStyle & 0x08000000));
        page.controls.insert(control, widget);
        previous = widget;
    }
    // A label's "&" only means something with a control to go to; without, it would be shown.
    for (QWidget *control : std::as_const(page.controls))
        if (auto *label = qobject_cast<QLabel *>(control); label && !label->buddy() && label->text().contains(QLatin1Char('&')))
            label->setText(QString(label->text()).replace(QStringLiteral("&&"), QStringLiteral("\x01")).remove(QLatin1Char('&')).replace(QLatin1Char('\x01'), QLatin1Char('&')));
    return page;
}

void greyControls(const Page &page)
{
    for (QWidget *control : page.controls)
        if (qobject_cast<QAbstractButton *>(control) || qobject_cast<QComboBox *>(control) || qobject_cast<QLineEdit *>(control) || qobject_cast<QSlider *>(control))
            control->setEnabled(false);
}

namespace {
// At 96 DPI, by XP's tabs: their words and 6 pixels each side, 21 tall, a row 18 above the next
const int TAB_HEIGHT = 21, ROW_STEP = 18, TAB_PADDING = 12, PANE_MARGIN = 3;
}

SheetTabs::SheetTabs(QWidget *parent)
    : QWidget(parent)
    , m_pages(new QStackedWidget(this))
{
    setMouseTracking(true);
}

void SheetTabs::addTab(QWidget *page, const QString &text)
{
    m_pages->addWidget(page);
    // (A page of XP's has a fixed size and no layout: no size hint.)
    m_pageSize = m_pageSize.expandedTo(page->sizeHint()).expandedTo(page->minimumSize());
    // "&&" is an "&"; a single one marks the shortcut.
    m_texts.append(QString(text).replace(QStringLiteral("&&"), QStringLiteral("\x01")).remove(QLatin1Char('&')).replace(QLatin1Char('\x01'), QLatin1Char('&')));
    arrange();
    updateGeometry();
}

void SheetTabs::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_texts.size())
        return;
    m_current = index;
    m_pages->setCurrentIndex(index);
    arrange();
    update();
}

int SheetTabs::widthOf(int tab) const
{
    return fontMetrics().horizontalAdvance(m_texts[tab]) + TAB_PADDING;
}

// The fewest rows that hold the tabs, as even as they can be
QList<QList<int>> SheetTabs::rowsFor(int width) const
{
    int total = 0;
    for (int i = 0; i < m_texts.size(); ++i)
        total += widthOf(i);
    const int wanted = qMax(1, (total + width - 1) / qMax(1, width));
    for (int limit = qMin(width, total / wanted);; limit += 4) {
        QList<QList<int>> rows{QList<int>()};
        int used = 0;
        for (int i = 0; i < m_texts.size(); ++i) {
            if (used + widthOf(i) > limit && !rows.last().isEmpty()) {
                rows.append(QList<int>());
                used = 0;
            }
            rows.last().append(i);
            used += widthOf(i);
        }
        if (rows.size() <= wanted || limit >= width)
            return rows;
    }
}

QSize SheetTabs::sizeHint() const
{
    const QSize page = m_pageSize;
    const int width = page.width() + 2 * PANE_MARGIN;
    const int rows = int(rowsFor(width).size());
    return QSize(width, (rows - 1) * ROW_STEP + TAB_HEIGHT - 1 + page.height() + 2 * PANE_MARGIN);
}

void SheetTabs::arrange()
{
    if (m_texts.isEmpty())
        return;
    QList<QList<int>> rows = rowsFor(width());
    // The rows turn so that the selected tab's is at the bottom, by the pane.
    for (int i = 0; i < rows.size(); ++i)
        if (rows[i].contains(m_current)) {
            for (int turn = 0; turn < i; ++turn)
                rows.append(rows.takeFirst());
            break;
        }
    std::reverse(rows.begin(), rows.end());
    m_rects = QList<QRect>(m_texts.size());
    for (int r = 0; r < rows.size(); ++r) {
        int used = 0;
        for (int tab : rows[r])
            used += widthOf(tab);
        // Several rows fill the width, each tab taking its share of what is left.
        const int spare = rows.size() > 1 ? qMax(0, width() - used) : 0;
        int x = 0;
        for (int i = 0; i < rows[r].size(); ++i) {
            const int tab = rows[r][i];
            const int w = widthOf(tab) + spare * (i + 1) / int(rows[r].size()) - spare * i / int(rows[r].size());
            m_rects[tab] = QRect(x, r * ROW_STEP, w, TAB_HEIGHT);
            x += w;
        }
    }
    const int top = (int(rows.size()) - 1) * ROW_STEP + TAB_HEIGHT - 1;
    m_pane = QRect(0, top, width(), height() - top);
    m_pages->setGeometry(m_pane.adjusted(PANE_MARGIN, PANE_MARGIN, -PANE_MARGIN, -PANE_MARGIN));
}

void SheetTabs::resizeEvent(QResizeEvent *)
{
    arrange();
}

void SheetTabs::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    QStyleOptionTabWidgetFrame frame;
    frame.initFrom(this);
    frame.rect = m_pane;
    frame.shape = QTabBar::RoundedNorth;
    style()->drawPrimitive(QStyle::PE_FrameTabWidget, &frame, &painter, this);
    const auto draw = [&](int i) {
        QStyleOptionTab tab;
        tab.initFrom(this);
        tab.rect = m_rects[i];
        tab.shape = QTabBar::RoundedNorth;
        tab.text = m_texts[i];
        tab.position = m_rects[i].x() == 0 ? QStyleOptionTab::Beginning : QStyleOptionTab::Middle;
        tab.state &= ~(QStyle::State_MouseOver | QStyle::State_HasFocus);
        if (i == m_current)
            tab.state |= QStyle::State_Selected;
        else if (i == m_hot)
            tab.state |= QStyle::State_MouseOver;
        style()->drawControl(QStyle::CE_TabBarTabShape, &tab, &painter, this);
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(m_rects[i].adjusted(0, i == m_current ? 0 : 2, 0, 0), Qt::AlignCenter | Qt::TextSingleLine, m_texts[i]);
    };
    for (int i = 0; i < m_rects.size(); ++i)
        if (i != m_current)
            draw(i);
    // Over its neighbours and the pane's edge
    if (m_current < m_rects.size())
        draw(m_current);
}

void SheetTabs::mouseMoveEvent(QMouseEvent *event)
{
    int hot = -1;
    for (int i = 0; i < m_rects.size(); ++i)
        if (m_rects[i].contains(event->position().toPoint()))
            hot = i;
    if (hot != m_hot) {
        m_hot = hot;
        update();
    }
}

void SheetTabs::leaveEvent(QEvent *)
{
    m_hot = -1;
    update();
}

void SheetTabs::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    for (int i = 0; i < m_rects.size(); ++i)
        if (m_rects[i].contains(event->position().toPoint())) {
            setCurrentIndex(i);
            return;
        }
}

void placeLowerLeft(QWidget *window)
{
    const FrameStyle &frame = ShellTheme::instance()->frame();
    const QRect room = QGuiApplication::primaryScreen()->availableGeometry();
    window->move(room.left() + frame.border, room.bottom() + 1 - frame.border - window->height());
}

void placeUpperLeft(QWidget *window)
{
    const FrameStyle &frame = ShellTheme::instance()->frame();
    const QRect room = QGuiApplication::primaryScreen()->availableGeometry();
    window->move(room.left() + frame.border, room.top() + frame.top);
}

void arrangeSheet(QDialog *dialog, SheetTabs *tabs, const QList<QPushButton *> &buttons)
{
    auto *layout = new QVBoxLayout(dialog);
    layout->setSizeConstraint(QLayout::SetFixedSize);
    layout->setContentsMargins(unitX(4), unitY(4), unitX(4), unitY(4));
    layout->setSpacing(unitY(4));
    layout->addWidget(tabs);
    auto *row = new QHBoxLayout;
    row->setSpacing(unitX(4));
    row->addStretch(1);
    for (QPushButton *button : buttons) {
        button->setFixedSize(unitX(50), unitY(14));
        row->addWidget(button);
    }
    layout->addLayout(row);
    // XP's sheets have the title bar's "?" and no minimize or maximize.
    dialog->setWindowFlag(Qt::WindowContextHelpButtonHint, true);
}

} // namespace xpdialog
