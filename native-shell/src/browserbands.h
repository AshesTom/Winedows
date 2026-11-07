// SPDX-License-Identifier: LGPL-2.1-or-later
// The bands of browseui's frame, which XP's folder windows and Internet Explorer share: the menu
// band with the flag's tile, the rebar's toolbars on the style's art, their buttons and separators.
#pragma once

#include "theme.h"
#include "xpfiles.h"

#include <QMainWindow>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QProxyStyle>
#include <QStyleOption>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

namespace bands {

// shell32.dll's flag: the first 22-pixel frame of its animation, 9 pixels into its white tile of 39 x 22
const int BITMAP_FLAG = 240, FLAG_SIZE = 22, FLAG_X = 9, TILE_WIDTH = 39;
// The animation's pace (not measured on XP)
const int FLAG_INTERVAL = 50;

// The rebar's art is one picture for the menu and the toolbars together, down to the folder itself.
inline void drawBand(QPainter &painter, QWidget *row, w4w::Renderer *renderer)
{
    auto *window = qobject_cast<QMainWindow *>(row->window());
    const int bottom = window && window->centralWidget() ? window->centralWidget()->y() : row->height();
    const int top = row->mapTo(row->window(), QPoint(0, 0)).y();
    renderer->draw(&painter, QRect(0, -top, row->width(), bottom), w4w::Part{QStringLiteral("rebar"), {}, {}, 0, {}});
}

// XP's folder menu bar is no user32 menu bar but shell32's menu band, a comctl32 toolbar of text-only
// buttons with the default padding: each its words and 16 pixels (the words 8 in) and 19 tall, 2 under
// the band's top, the first 2 in from the window's client edge (measured on XP's explorer: File 6 to 37
// from the frame's edge and 31 to 49 from its top, Edit's words at 46, View's at 80).
class MenuBandStyle : public QProxyStyle
{
public:
    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size, const QWidget *widget) const override
    {
        if (type == CT_MenuBarItem)
            return QSize(size.width() + 16, 19);
        return QProxyStyle::sizeFromContents(type, option, size, widget);
    }
    int pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const override
    {
        switch (metric) {
        case PM_MenuBarHMargin:
            return 2;
        case PM_MenuBarVMargin:
            return 1;   // (the bar's own row starts a pixel under the band's top)
        case PM_MenuBarItemSpacing:
        case PM_MenuBarPanelWidth:
            return 0;
        default:
            return QProxyStyle::pixelMetric(metric, option, widget);
        }
    }
};

// The menu bar as XP's: its row, the flag's white tile at its right end, and the band's two lines under
// it (the first in the style's Rebar.Band EdgeShadowColor, as along the tile's left side)
class BrandMenuBar : public QMenuBar
{
public:
    explicit BrandMenuBar(QWidget *parent)
        : QMenuBar(parent)
    {
        static MenuBandStyle *style = new MenuBandStyle;
        setStyle(style);
        if (const auto shell32 = xpfiles::resources(xpfiles::find(QStringLiteral("shell32.dll"))))
            m_strip = xpfiles::bitmap(*shell32, BITMAP_FLAG);
        m_flag = m_strip.copy(0, 0, FLAG_SIZE, FLAG_SIZE);
        m_timer.setInterval(FLAG_INTERVAL);
        QObject::connect(&m_timer, &QTimer::timeout, this, [this] {
            // The strip's frames one under the other; the first is the flag at rest.
            const int frames = qMax(1, m_strip.height() / FLAG_SIZE);
            m_frame = m_frame % (frames - 1) + 1;
            m_flag = m_strip.copy(0, m_frame * FLAG_SIZE, FLAG_SIZE, FLAG_SIZE);
            update(QRect(width() - TILE_WIDTH, 0, TILE_WIDTH, height()));
        });
    }
    // The flag waving while a page loads, as browseui's brand band does; at rest, its first frame
    void setAnimating(bool animating)
    {
        if (animating == m_timer.isActive() || m_strip.height() <= FLAG_SIZE)
            return;
        if (animating) {
            m_timer.start();
        } else {
            m_timer.stop();
            m_frame = 0;
            m_flag = m_strip.copy(0, 0, FLAG_SIZE, FLAG_SIZE);
            update();
        }
    }
    QSize sizeHint() const override
    {
        return QSize(QMenuBar::sizeHint().width(), FLAG_SIZE + 2);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        // Windows Classic's menu bar is Qt's own; it opens its own painter, before this one is.
        w4w::Renderer *renderer = ShellTheme::instance()->renderer();
        if (!renderer)
            QMenuBar::paintEvent(event);
        QPainter painter(this);
        const QColor shadow = edgeColour();
        const int row = height() - 2;
        if (renderer) {
            // The band's art under the menu's names, not the style's flat colour; the item
            // under the pointer as the style draws it.
            drawBand(painter, this, renderer);
            // XP's menu band underlines its letters only once Alt is pressed ("Hide underlined
            // letters for keyboard navigation" is its default): Qt gives the bar the focus then.
            const int cues = style()->styleHint(QStyle::SH_UnderlineShortcut, nullptr, this) || hasFocus() ? 0 : Qt::TextHideMnemonic;
            for (QAction *action : actions()) {
                QStyleOptionMenuItem option;
                initStyleOption(&option, action);
                option.rect = actionGeometry(action);
                if (option.state & (QStyle::State_Selected | QStyle::State_Sunken))
                    style()->drawControl(QStyle::CE_MenuBarItem, &option, &painter, this);
                else
                    style()->drawItemText(&painter, option.rect, Qt::AlignCenter | Qt::TextShowMnemonic | cues | Qt::TextSingleLine, option.palette,
                                          option.state & QStyle::State_Enabled, option.text, QPalette::ButtonText);
            }
        }
        painter.fillRect(QRect(0, row, width(), 1), shadow);
        painter.fillRect(QRect(0, row + 1, width(), 1), Qt::white);
        const QRect tile(width() - TILE_WIDTH, 0, TILE_WIDTH, row);
        painter.fillRect(QRect(tile.x() - 1, 0, 1, row), shadow);
        painter.fillRect(tile, Qt::white);
        painter.drawImage(tile.x() + FLAG_X, (row - m_flag.height()) / 2, m_flag);
    }
    void focusInEvent(QFocusEvent *event) override { QMenuBar::focusInEvent(event); update(); }
    void focusOutEvent(QFocusEvent *event) override { QMenuBar::focusOutEvent(event); update(); }

private:
    static QColor edgeColour()
    {
        ShellTheme *theme = ShellTheme::instance();
        if (w4w::Renderer *renderer = theme->renderer())
            if (const auto colour = renderer->colour(w4w::Part{QStringLiteral("rebar"), QStringLiteral("band"), {}, 0, {}}, QStringLiteral("edgeshadowcolor")))
                return *colour;
        return theme->system(QStringLiteral("btnshadow"), Qt::gray);
    }

    QImage m_strip, m_flag;
    QTimer m_timer;
    int m_frame = 0;
};

// The rebar's bands place their buttons themselves, edge to edge as comctl32's toolbar does.
class BandStyle : public QProxyStyle
{
public:
    int pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const override
    {
        switch (metric) {
        case PM_ToolBarFrameWidth:
        case PM_ToolBarItemMargin:
        case PM_ToolBarItemSpacing:
        case PM_ToolBarHandleExtent:
            return 0;
        default:
            return QProxyStyle::pixelMetric(metric, option, widget);
        }
    }
};

// A toolbar on the visual style's rebar art, as XP's folder windows have it. A band above another one
// ends with the same two lines as the menu's (the rebar's band borders).
class RebarToolBar : public QToolBar
{
public:
    RebarToolBar(const QString &title, QWidget *parent, bool border)
        : QToolBar(title, parent), m_border(border)
    {
        static BandStyle *style = new BandStyle;
        setStyle(style);
        setContentsMargins(0, 0, 0, border ? 2 : 0);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        w4w::Renderer *renderer = ShellTheme::instance()->renderer();
        // (Qt's own toolbar under Windows Classic opens its own painter first.)
        if (!renderer)
            QToolBar::paintEvent(event);
        QPainter painter(this);
        if (renderer)
            drawBand(painter, this, renderer);
        if (m_border) {
            ShellTheme *theme = ShellTheme::instance();
            const QColor shadow = renderer ? renderer->colour(w4w::Part{QStringLiteral("rebar"), QStringLiteral("band"), {}, 0, {}}, QStringLiteral("edgeshadowcolor"))
                                                 .value_or(theme->system(QStringLiteral("btnshadow"), Qt::gray))
                                           : theme->system(QStringLiteral("btnshadow"), Qt::gray);
            painter.fillRect(QRect(0, height() - 2, width(), 1), shadow);
            painter.fillRect(QRect(0, height() - 1, width(), 1), theme->system(QStringLiteral("btnhighlight"), Qt::white));
        }
    }

private:
    bool m_border;
};

// A button of the folder window's toolbars, laid out as on XP (comctl32's list toolbar, as browseui fills
// it), measured there with Tahoma 8: the picture centred in the height; with words, the picture 3 from the
// left, the words 4 after it and 8 after them; without, 4 on each side. Back and Forward have 13 more for
// their arrow, and Back's picture and words are 4 further in (7 after the words), Forward's picture 8 in
// and 6 after. Views' arrow, 5 x 3, is 2 after its picture, 4 from the right.
class BandButton : public QToolButton
{
public:
    enum Kind { Plain, Split, WholeDropDown };
    BandButton(QWidget *parent, int height, int iconSide, Kind kind = Plain)
        : QToolButton(parent), m_height(height), m_kind(kind)
    {
        setAutoRaise(true);
        setIconSize(QSize(iconSide, iconSide));
        if (kind == Split)
            setPopupMode(QToolButton::MenuButtonPopup);
        else if (kind == WholeDropDown)
            setPopupMode(QToolButton::InstantPopup);
    }
    QSize sizeHint() const override
    {
        const int icon = iconSize().width(), words = textWidth();
        int width;
        if (m_kind == Split)
            width = (words ? 7 + icon + 4 + words + 7 : 8 + icon + 6) + ARROW;
        else if (m_kind == WholeDropDown)
            width = 4 + icon + 2 + 5 + 4;
        else
            width = words ? 3 + icon + 4 + words + 8 : 4 + icon + 4;
        return QSize(width, m_height);
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        // The arrow's 13 pixels open the history, as XP's split button.
        if (m_kind == Split && event->button() == Qt::LeftButton && event->position().x() >= width() - ARROW && isEnabled()) {
            m_arrowDown = true;
            update();
            showMenu();
            m_arrowDown = false;
            update();
            return;
        }
        QToolButton::mousePressEvent(event);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        w4w::Renderer *renderer = ShellTheme::instance()->renderer();
        const bool enabled = isEnabled(), hot = enabled && underMouse();
        const auto state = [&](bool down) {
            if (!enabled)
                return 4;
            if (down)
                return 3;
            if (isChecked())
                return hot ? 6 : 5;
            return hot ? 2 : 1;
        };
        const QRect main = m_kind == Split ? QRect(0, 0, width() - ARROW, height()) : rect();
        const int mainState = state(isDown() && !m_arrowDown);
        const auto frame = [&](const QRect &r, const QString &part, int id) {
            if (renderer) {
                renderer->draw(&painter, r, w4w::Part{QStringLiteral("toolbar"), part, {}, id, {}});
            } else if (id == 2 || id == 3 || id == 5 || id == 6) {
                QStyleOption option;
                option.initFrom(this);
                option.rect = r;
                option.state |= (id == 2 ? QStyle::State_Raised : QStyle::State_Sunken);
                style()->drawPrimitive(QStyle::PE_PanelButtonTool, &option, &painter, this);
            }
        };
        // At rest a button has no frame; Back's and Forward's arrow, a picture of the part, always shows.
        if (mainState != 1 && mainState != 4)
            frame(main, m_kind == Split ? QStringLiteral("splitbutton") : QStringLiteral("button"), mainState);
        if (m_kind == Split) {
            const QRect part(main.right() + 1, 0, ARROW, height());
            frame(part, QStringLiteral("splitbuttondropdown"), state(m_arrowDown));
            // Windows Classic has no picture of the arrow: comctl32's own, 5 x 3
            if (!renderer) {
                const QColor ink = palette().color(enabled ? QPalette::Active : QPalette::Disabled, QPalette::ButtonText);
                for (int row = 0; row < 3; ++row)
                    painter.fillRect(QRect(part.x() + 4 + row, (height() - 3) / 2 + row, 5 - 2 * row, 1), ink);
            }
        }

        const int icon = iconSize().width(), words = textWidth();
        int x = words ? (m_kind == Split ? 7 : 3) : (m_kind == Split ? 8 : 4);
        if (m_kind == Plain && !words)
            x = (width() - icon) / 2;
        const QIcon::Mode mode = !enabled ? QIcon::Disabled : hot ? QIcon::Active : QIcon::Normal;
        painter.drawPixmap(x, (height() - icon) / 2, this->icon().pixmap(iconSize(), devicePixelRatio(), mode));
        x += icon;
        if (words) {
            style()->drawItemText(&painter, QRect(x + 4, 0, words, height()), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine | Qt::TextHideMnemonic,
                                  palette(), enabled, text(), QPalette::ButtonText);
        }
        if (m_kind == WholeDropDown) {
            const QColor ink = palette().color(enabled ? QPalette::Active : QPalette::Disabled, QPalette::ButtonText);
            const int ax = x + 2, ay = (height() - 3) / 2;
            for (int row = 0; row < 3; ++row)
                painter.fillRect(QRect(ax + row, ay + row, 5 - 2 * row, 1), ink);
        }
    }

private:
    static constexpr int ARROW = 13;
    int textWidth() const
    {
        return toolButtonStyle() == Qt::ToolButtonIconOnly || text().isEmpty() ? 0 : fontMetrics().horizontalAdvance(text());
    }

    int m_height;
    Kind m_kind;
    bool m_arrowDown = false;
};

// A separator between the toolbar's groups: 6 wide, the style's line 2 in, as on XP
class BandSeparator : public QWidget
{
public:
    BandSeparator(QWidget *parent, int height)
        : QWidget(parent)
    {
        setFixedSize(6, height);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        QStyleOption option;
        option.initFrom(this);
        option.state |= QStyle::State_Horizontal;
        option.rect = rect().translated(-1, 0);   // its line 2 in, as on XP
        style()->drawPrimitive(QStyle::PE_IndicatorToolBarSeparator, &option, &painter, this);
    }
};

} // namespace bands
