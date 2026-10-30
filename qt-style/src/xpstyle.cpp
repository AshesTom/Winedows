// SPDX-License-Identifier: LGPL-2.1-or-later
#include "xpstyle.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <qdrawutil.h>
#include <QGroupBox>
#include <QHeaderView>
#include <QIcon>
#include <QLineEdit>
#include <QMenu>
#include <QDialogButtonBox>
#include <QFrame>
#include <QMenuBar>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>

namespace w4w {
namespace {

// A state: its INI name and its number in tmschema.h (the frame to draw).
struct PartState
{
    QString name;
    int id;
};

// Push buttons (PBS_*): normal, hot, pressed, disabled, defaulted.
PartState buttonState(const QStyleOption *o, bool defaulted)
{
    const QStyle::State s = o->state;
    if (!(s & QStyle::State_Enabled))
        return {QStringLiteral("disabled"), 4};
    if (s & (QStyle::State_Sunken | QStyle::State_On))
        return {QStringLiteral("pressed"), 3};
    if (s & QStyle::State_MouseOver)
        return {QStringLiteral("hot"), 2};
    if (defaulted)
        return {QStringLiteral("defaulted"), 5};
    return {QStringLiteral("normal"), 1};
}

// The four states most parts have: normal, hot, pressed, disabled.
PartState fourState(bool enabled, bool pressed, bool hot)
{
    if (!enabled)
        return {QStringLiteral("disabled"), 4};
    if (pressed)
        return {QStringLiteral("pressed"), 3};
    if (hot)
        return {QStringLiteral("hot"), 2};
    return {QStringLiteral("normal"), 1};
}

// Check boxes (12 states) and radio buttons (8): unchecked, checked, mixed,
// each normal, hot, pressed, disabled.
PartState checkState(const QStyleOption *o, bool radio)
{
    const QStyle::State s = o->state;
    QString base = QStringLiteral("unchecked");
    int offset = 0;
    if (s & QStyle::State_On) {
        base = QStringLiteral("checked");
        offset = 4;
    } else if ((s & QStyle::State_NoChange) && !radio) {
        base = QStringLiteral("mixed");
        offset = 8;
    }
    const PartState f = fourState(s & QStyle::State_Enabled, s & QStyle::State_Sunken, s & QStyle::State_MouseOver);
    return {base + f.name, offset + f.id};
}

const QString kButton = QStringLiteral("button");

// A push button with both text and a picture: the Accurate style leaves the picture out.
bool hasTextAndIcon(const QStyleOptionButton &b)
{
    return !b.icon.isNull() && !b.text.isEmpty() && !(b.features & QStyleOptionButton::CommandLinkButton);
}

} // namespace

XpStyle::XpStyle()
    : QProxyStyle(QStyleFactory::create(QStringLiteral("Windows")))
{
    setObjectName(QStringLiteral("Win4Wine-XP"));
    reload();
    connect(Session::instance(), &Session::changed, this, [this] {
        reload();
        applyToApplication();
    });
    // Busy progress bars move: XP's marquee, a few chunks crossing the bar.
    m_busyTimer.setInterval(60);
    connect(&m_busyTimer, &QTimer::timeout, this, [this] {
        m_busyBars.removeAll(nullptr);
        bool any = false;
        for (const QPointer<QProgressBar> &bar : std::as_const(m_busyBars))
            if (bar->minimum() == bar->maximum() && bar->isVisible()) {
                bar->update();
                any = true;
            }
        if (!any)
            m_busyTimer.stop();
    });
}

void XpStyle::reload()
{
    m_look = Session::instance()->look();
    m_renderer.reset();
    if (m_look.themed && qEnvironmentVariable("WIN4WINE_QT_STYLE_CLASSIC") != QLatin1String("1")) {
        QString error;
        if (auto theme = Theme::load(m_look.stylePath, m_look.colourName, m_look.sizeName, &error))
            m_renderer = std::make_shared<Renderer>(theme, m_look.dpi);
        else
            qWarning("win4wine-xp style: %s", qPrintable(error));
    }

    // XP's system colours (Control Panel\Colors), with Luna's as fallbacks.
    // Dialogs are COLOR_3DFACE; edit fields and lists COLOR_WINDOW.
    const QColor face = system(QStringLiteral("buttonface"), QColor(236, 233, 216));
    const QColor window = system(QStringLiteral("window"), Qt::white);
    const QColor text = system(QStringLiteral("windowtext"), Qt::black);
    const QColor buttonText = system(QStringLiteral("buttontext"), Qt::black);
    const QColor gray = system(QStringLiteral("graytext"), QColor(172, 168, 153));
    QPalette p;
    for (QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        p.setColor(group, QPalette::Window, face);
        p.setColor(group, QPalette::WindowText, group == QPalette::Disabled ? gray : text);
        p.setColor(group, QPalette::Base, group == QPalette::Disabled ? face : window);
        p.setColor(group, QPalette::AlternateBase, face.lighter(105));
        p.setColor(group, QPalette::Text, group == QPalette::Disabled ? gray : text);
        p.setColor(group, QPalette::Button, face);
        p.setColor(group, QPalette::ButtonText, group == QPalette::Disabled ? gray : buttonText);
        p.setColor(group, QPalette::BrightText, Qt::white);
        p.setColor(group, QPalette::Light, system(QStringLiteral("buttonhilight"), Qt::white));
        p.setColor(group, QPalette::Midlight, system(QStringLiteral("buttonlight"), face));
        p.setColor(group, QPalette::Mid, system(QStringLiteral("buttonshadow"), QColor(172, 168, 153)));
        p.setColor(group, QPalette::Dark, system(QStringLiteral("buttonshadow"), QColor(172, 168, 153)));
        p.setColor(group, QPalette::Shadow, system(QStringLiteral("buttondkshadow"), QColor(113, 111, 100)));
        p.setColor(group, QPalette::Highlight, group == QPalette::Inactive
                       ? face : system(QStringLiteral("hilight"), QColor(49, 106, 197)));
        p.setColor(group, QPalette::HighlightedText, group == QPalette::Inactive
                       ? text : system(QStringLiteral("hilighttext"), Qt::white));
        p.setColor(group, QPalette::ToolTipBase, system(QStringLiteral("infowindow"), QColor(255, 255, 225)));
        p.setColor(group, QPalette::ToolTipText, system(QStringLiteral("infotext"), Qt::black));
        p.setColor(group, QPalette::Link, system(QStringLiteral("hottrackingcolor"), QColor(0, 0, 128)));
        p.setColor(group, QPalette::LinkVisited, QColor(128, 0, 128));
        p.setColor(group, QPalette::PlaceholderText, gray);
    }
    // Disabled text is not dimmed further; XP draws it in COLOR_GRAYTEXT.
    p.setColor(QPalette::Disabled, QPalette::Highlight, gray);
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, face);
    m_palette = p;
}

QColor XpStyle::system(const QString &name, const QColor &fallback) const
{
    return m_look.colours.value(name, fallback);
}

QColor XpStyle::partColour(const QString &cls, const QString &part, const QString &state, const QString &property,
                           const QColor &fallback) const
{
    if (!m_renderer)
        return fallback;
    return m_renderer->colour({cls, part, state, 0, {}}, property).value_or(fallback);
}

bool XpStyle::draw(QPainter *painter, const QRect &rect, const QString &cls, const QString &part,
                   const QString &state, int stateId) const
{
    return m_renderer && m_renderer->draw(painter, rect, {cls, part, state, stateId, {}});
}

int XpStyle::scrollBarExtent() const
{
    if (m_renderer)
        if (auto v = m_renderer->theme().sysMetric(QStringLiteral("scrollbarwidth"))) {
            bool ok = false;
            const int n = v->trimmed().toInt(&ok);
            if (ok && n > 4 && n < 64)
                return n * m_look.dpi / 96;
        }
    return 17 * m_look.dpi / 96;
}

QPalette XpStyle::standardPalette() const
{
    return m_palette;
}

void XpStyle::polish(QPalette &palette)
{
    palette = m_palette;
}

// KDE's frameworks give an application Breeze's colours unless it names a
// colour scheme (KColorSchemeManager): it is the session's XP colours,
// written as a KDE scheme, which KColorScheme's widgets then use too.
static QString kdeScheme(const QPalette &p, const Look &look)
{
    auto rgb = [](const QColor &c) { return QStringLiteral("%1,%2,%3").arg(c.red()).arg(c.green()).arg(c.blue()); };
    const QColor gray = p.color(QPalette::Disabled, QPalette::Text);
    const QColor link = p.color(QPalette::Link), hot = look.colours.value(QStringLiteral("hottrackingcolor"), link);
    auto set = [&](const QString &name, const QColor &back, const QColor &alternate, const QColor &fore) {
        return QStringLiteral("[Colors:%1]\nBackgroundNormal=%2\nBackgroundAlternate=%3\nForegroundNormal=%4\n"
                              "ForegroundInactive=%5\nForegroundActive=%6\nForegroundLink=%7\nForegroundVisited=128,0,128\n"
                              "ForegroundNegative=192,0,0\nForegroundNeutral=176,128,0\nForegroundPositive=0,128,0\n"
                              "DecorationFocus=%8\nDecorationHover=%9\n\n")
            .arg(name, rgb(back), rgb(alternate), rgb(fore), rgb(gray), rgb(p.color(QPalette::Highlight)), rgb(link),
                 rgb(p.color(QPalette::Active, QPalette::Highlight)), rgb(hot));
    };
    const QColor face = p.color(QPalette::Window), base = p.color(QPalette::Active, QPalette::Base);
    QString text = QStringLiteral("[General]\nName=Win4Wine XP\nColorScheme=Win4Wine XP\n\n");
    text += set(QStringLiteral("View"), base, p.color(QPalette::AlternateBase), p.color(QPalette::Active, QPalette::Text));
    text += set(QStringLiteral("Window"), face, face, p.color(QPalette::Active, QPalette::WindowText));
    text += set(QStringLiteral("Button"), face, face, p.color(QPalette::Active, QPalette::ButtonText));
    text += set(QStringLiteral("Header"), face, face, p.color(QPalette::Active, QPalette::WindowText));
    text += set(QStringLiteral("Selection"), p.color(QPalette::Active, QPalette::Highlight), p.color(QPalette::Active, QPalette::Highlight),
                p.color(QPalette::Active, QPalette::HighlightedText));
    text += set(QStringLiteral("Tooltip"), p.color(QPalette::ToolTipBase), p.color(QPalette::ToolTipBase), p.color(QPalette::ToolTipText));
    text += set(QStringLiteral("Complementary"), look.colours.value(QStringLiteral("activetitle"), QColor(0, 84, 227)),
                look.colours.value(QStringLiteral("activetitle"), QColor(0, 84, 227)),
                look.colours.value(QStringLiteral("titletext"), Qt::white));
    // XP dims nothing for inactive windows; disabled text is COLOR_GRAYTEXT.
    text += QStringLiteral("[ColorEffects:Inactive]\nEnable=false\nChangeSelectionColor=false\n\n"
                           "[ColorEffects:Disabled]\nColor=%1\nColorAmount=1\nColorEffect=0\n"
                           "ContrastAmount=0.55\nContrastEffect=1\nIntensityAmount=0\nIntensityEffect=0\n")
                .arg(rgb(gray));
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/win4wine-qt");
    // One file per palette: KColorScheme keeps a scheme it read by its path.
    const QString name = QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
    const QString path = directory + QStringLiteral("/xp-") + name + QStringLiteral(".colors");
    if (!QFileInfo::exists(path)) {
        QDir().mkpath(directory);
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return QString();
        file.write(text.toUtf8());
        if (!file.commit())
            return QString();
    }
    return path;
}

void XpStyle::polish(QApplication *application)
{
    QProxyStyle::polish(application);
    if (!application)
        return;   // (a style set before the application exists)
    if (qEnvironmentVariable("WIN4WINE_QT_PALETTE") != QLatin1String("0")) {
        const QString scheme = kdeScheme(m_palette, m_look);
        if (!scheme.isEmpty())
            application->setProperty("KDE_COLOR_SCHEME_PATH", scheme);
    }
    application->setPalette(m_palette);
    // The Accurate style: no pictures in menus, as XP's; the choice is undone
    // when the user goes back to Standard.
    static const bool menuIcons = !application->testAttribute(Qt::AA_DontShowIconsInMenus);
    if (menuIcons)
        QCoreApplication::setAttribute(Qt::AA_DontShowIconsInMenus, m_look.accurate);
    // The icon theme chosen in Control Panel's Icons item, else the desktop's.
    static const QString desktopIcons = QIcon::themeName();
    const QString icons = m_look.iconTheme.isEmpty() ? desktopIcons : m_look.iconTheme;
    if (QIcon::themeName() != icons)
        QIcon::setThemeName(icons);
    // XP's fonts (Display Properties > Appearance), unless the user keeps Qt's.
    if (m_look.haveFonts && qEnvironmentVariable("WIN4WINE_QT_FONTS") != QLatin1String("0")) {
        application->setFont(m_look.messageFont);
        application->setFont(m_look.menuFont, "QMenu");
        application->setFont(m_look.menuFont, "QMenuBar");
        application->setFont(m_look.statusFont, "QStatusBar");
    }
}

void XpStyle::applyToApplication()
{
    if (!qApp)
        return;
    polish(qApp);
    const QWidgetList widgets = QApplication::allWidgets();
    for (QWidget *w : widgets) {
        polish(w);
        QEvent event(QEvent::StyleChange);
        QApplication::sendEvent(w, &event);
        w->update();
    }
}

void XpStyle::polish(QWidget *widget)
{
    QProxyStyle::polish(widget);
    // Hot tracking: XP's controls light up under the mouse.
    if (qobject_cast<QAbstractButton *>(widget) || qobject_cast<QComboBox *>(widget)
        || qobject_cast<QAbstractSpinBox *>(widget) || qobject_cast<QScrollBar *>(widget)
        || qobject_cast<QSlider *>(widget) || qobject_cast<QTabBar *>(widget)
        || qobject_cast<QHeaderView *>(widget) || qobject_cast<QGroupBox *>(widget))
        widget->setAttribute(Qt::WA_Hover, true);
    // Menus are COLOR_MENU (white in Luna), the menu bar COLOR_MENUBAR.
    if (qobject_cast<QMenu *>(widget) || qobject_cast<QMenuBar *>(widget)) {
        const bool bar = qobject_cast<QMenuBar *>(widget);
        QPalette p = widget->palette();
        const QColor back = system(bar ? QStringLiteral("menubar") : QStringLiteral("menu"),
                                   bar ? m_palette.color(QPalette::Window) : QColor(Qt::white));
        const QColor text = system(QStringLiteral("menutext"), Qt::black);
        for (QPalette::ColorGroup g : {QPalette::Active, QPalette::Inactive}) {
            p.setColor(g, QPalette::Window, back);
            p.setColor(g, QPalette::Button, back);
            p.setColor(g, QPalette::WindowText, text);
            p.setColor(g, QPalette::ButtonText, text);
            p.setColor(g, QPalette::Text, text);
            p.setColor(g, QPalette::Highlight, system(QStringLiteral("menuhilight"), m_palette.color(QPalette::Active, QPalette::Highlight)));
            p.setColor(g, QPalette::HighlightedText, system(QStringLiteral("hilighttext"), Qt::white));
        }
        p.setColor(QPalette::Disabled, QPalette::Window, back);
        p.setColor(QPalette::Disabled, QPalette::Button, back);
        widget->setPalette(p);
    }
    if (auto *bar = qobject_cast<QProgressBar *>(widget)) {
        if (!m_busyBars.contains(bar))
            m_busyBars.append(bar);
        if (!m_busyTimer.isActive())
            const_cast<QTimer &>(m_busyTimer).start();
    }
}

void XpStyle::unpolish(QWidget *widget)
{
    if (auto *bar = qobject_cast<QProgressBar *>(widget))
        m_busyBars.removeAll(bar);
    if (qobject_cast<QMenu *>(widget) || qobject_cast<QMenuBar *>(widget))
        widget->setPalette(QPalette());
    QProxyStyle::unpolish(widget);
}

void XpStyle::drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                            const QWidget *widget) const
{
    if (!themed()) {
        QProxyStyle::drawPrimitive(element, option, painter, widget);
        return;
    }
    switch (element) {
    case PE_PanelButtonCommand:
    case PE_PanelButtonBevel: {
        const auto *button = qstyleoption_cast<const QStyleOptionButton *>(option);
        const bool flat = button && (button->features & QStyleOptionButton::Flat);
        if (flat && !(option->state & (State_MouseOver | State_Sunken | State_On)))
            return;
        const bool defaulted = (button && (button->features & QStyleOptionButton::DefaultButton))
            || ((option->state & State_HasFocus) && qobject_cast<const QPushButton *>(widget));
        const PartState s = buttonState(option, defaulted);
        if (draw(painter, option->rect, kButton, QStringLiteral("pushbutton"), s.name, s.id))
            return;
        break;
    }
    case PE_FrameDefaultButton:
        return;
    case PE_PanelButtonTool: {
        // Toolbar buttons: nothing at rest, XP's toolbar frames when hot,
        // pressed or checked. A tool button outside a toolbar is a push button.
        const bool autoRaise = (option->state & State_AutoRaise) || qobject_cast<const QToolBar *>(widget ? widget->parentWidget() : nullptr);
        if (!autoRaise) {
            const PartState s = buttonState(option, false);
            if (draw(painter, option->rect, kButton, QStringLiteral("pushbutton"), s.name, s.id))
                return;
            break;
        }
        const bool enabled = option->state & State_Enabled;
        const bool on = option->state & State_On, hot = enabled && (option->state & State_MouseOver);
        PartState s{QStringLiteral("normal"), 1};
        if (on && hot)
            s = {QStringLiteral("hotchecked"), 6};
        else if (on)
            s = {QStringLiteral("checked"), 5};
        else if (enabled && (option->state & State_Sunken))
            s = {QStringLiteral("pressed"), 3};
        else if (hot)
            s = {QStringLiteral("hot"), 2};
        else
            return;
        if (draw(painter, option->rect, QStringLiteral("toolbar"), QStringLiteral("button"), s.name, s.id))
            return;
        break;
    }
    case PE_IndicatorCheckBox:
    case PE_IndicatorRadioButton: {
        const bool radio = element == PE_IndicatorRadioButton;
        const PartState s = checkState(option, radio);
        if (draw(painter, option->rect, kButton, radio ? QStringLiteral("radiobutton") : QStringLiteral("checkbox"), s.name, s.id))
            return;
        break;
    }
    case PE_FrameGroupBox: {
        const bool enabled = option->state & State_Enabled;
        if (draw(painter, option->rect, kButton, QStringLiteral("groupbox"), enabled ? QStringLiteral("normal") : QStringLiteral("disabled"), enabled ? 1 : 2))
            return;
        break;
    }
    case PE_PanelLineEdit: {
        const auto *frame = qstyleoption_cast<const QStyleOptionFrame *>(option);
        PartState s{QStringLiteral("normal"), 1};
        if (!(option->state & State_Enabled))
            s = {QStringLiteral("disabled"), 4};
        else if (option->state & State_ReadOnly)
            s = {QStringLiteral("readonly"), 6};
        if (frame && frame->lineWidth > 0) {
            if (draw(painter, option->rect, QStringLiteral("edit"), QStringLiteral("edittext"), s.name, s.id))
                return;
        } else {
            // Inside a combo or spin box: only the field's fill.
            painter->fillRect(option->rect, partColour(QStringLiteral("edit"), QStringLiteral("edittext"), s.name,
                                                       QStringLiteral("fillcolor"), option->palette.color(QPalette::Base)));
            return;
        }
        break;
    }
    case PE_FrameLineEdit: {
        const QColor border = partColour(QStringLiteral("edit"), QStringLiteral("edittext"),
                                         option->state & State_Enabled ? QStringLiteral("normal") : QStringLiteral("disabled"),
                                         QStringLiteral("bordercolor"), option->palette.color(QPalette::Dark));
        painter->save();
        painter->setPen(border);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(option->rect.adjusted(0, 0, -1, -1));
        painter->restore();
        return;
    }
    case PE_Frame: {
        // Scroll areas (lists, trees, text views) take the list box's border.
        if (option->state & (State_Sunken | State_Raised)) {
            const QColor border = partColour(QStringLiteral("listbox"), {}, {}, QStringLiteral("bordercolor"),
                                             option->palette.color(QPalette::Dark));
            painter->save();
            painter->setPen(border);
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(option->rect.adjusted(0, 0, -1, -1));
            painter->restore();
            return;
        }
        break;
    }
    case PE_FrameTabWidget: {
        // The pane's edge, then the page's body texture inside it (XP's
        // dialogs draw their tab pages with Tab.Body).
        if (draw(painter, option->rect, QStringLiteral("tab"), QStringLiteral("pane"), {}, 0)) {
            const QRect inside = m_renderer->contentRect({QStringLiteral("tab"), QStringLiteral("pane"), {}, 0, {}}, option->rect);
            draw(painter, inside.isValid() ? inside : option->rect.adjusted(1, 1, -3, -3), QStringLiteral("tab"), QStringLiteral("body"), {}, 0);
            return;
        }
        break;
    }
    case PE_FrameTabBarBase:
        return;
    case PE_IndicatorToolBarHandle: {
        const bool horizontal = option->state & State_Horizontal;
        const QString part = horizontal ? QStringLiteral("gripper") : QStringLiteral("grippervert");
        const QSize size = m_renderer->trueSize({QStringLiteral("rebar"), part, {}, 0, {}});
        QRect r = option->rect;
        if (horizontal && size.width() > 0)
            r = QRect(r.center().x() - size.width() / 2, r.top() + 2, size.width(), r.height() - 4);
        else if (!horizontal && size.height() > 0)
            r = QRect(r.left() + 2, r.center().y() - size.height() / 2, r.width() - 4, size.height());
        if (draw(painter, r, QStringLiteral("rebar"), part, {}, 0))
            return;
        break;
    }
    case PE_IndicatorToolBarSeparator: {
        const bool horizontal = option->state & State_Horizontal;
        const QString part = horizontal ? QStringLiteral("separator") : QStringLiteral("separatorvert");
        if (draw(painter, option->rect, QStringLiteral("toolbar"), part, QStringLiteral("normal"), 1))
            return;
        break;
    }
    case PE_PanelMenu:
        painter->fillRect(option->rect, option->palette.color(QPalette::Window));
        return;
    case PE_FrameMenu: {
        // Flat menus (XP's FlatMenus): a single line in COLOR_BTNSHADOW.
        painter->save();
        painter->setPen(system(QStringLiteral("buttonshadow"), option->palette.color(QPalette::Dark)));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(option->rect.adjusted(0, 0, -1, -1));
        painter->restore();
        return;
    }
    case PE_PanelMenuBar:
        painter->fillRect(option->rect, system(QStringLiteral("menubar"), option->palette.color(QPalette::Window)));
        return;
    case PE_PanelTipLabel: {
        painter->fillRect(option->rect, option->palette.color(QPalette::ToolTipBase));
        painter->save();
        painter->setPen(system(QStringLiteral("windowframe"), Qt::black));
        painter->drawRect(option->rect.adjusted(0, 0, -1, -1));
        painter->restore();
        return;
    }
    case PE_IndicatorBranch: {
        // XP's tree: dotted lines in COLOR_GRAYTEXT and its +/- glyphs.
        const QRect r = option->rect;
        const QPoint c = r.center();
        painter->save();
        QPen pen(system(QStringLiteral("graytext"), option->palette.color(QPalette::Dark)));
        pen.setStyle(Qt::DotLine);
        painter->setPen(pen);
        if (option->state & State_Item)
            painter->drawLine(c.x(), c.y(), r.right(), c.y());
        if (option->state & State_Sibling)
            painter->drawLine(c.x(), r.top(), c.x(), r.bottom());
        else if (option->state & (State_Item | State_Children))
            painter->drawLine(c.x(), r.top(), c.x(), c.y());
        painter->restore();
        if (option->state & State_Children) {
            const bool open = option->state & State_Open;
            const w4w::Part glyph{QStringLiteral("treeview"), QStringLiteral("glyph"),
                                  open ? QStringLiteral("opened") : QStringLiteral("closed"), open ? 2 : 1, {}};
            const QSize size = m_renderer->trueSize(glyph);
            if (!size.isEmpty()) {
                const QRect box(c.x() - size.width() / 2, c.y() - size.height() / 2, size.width(), size.height());
                painter->fillRect(box, option->palette.color(QPalette::Base));
                if (m_renderer->draw(painter, box, glyph))
                    return;
            }
            QProxyStyle::drawPrimitive(element, option, painter, widget);
        }
        return;
    }
    case PE_FrameStatusBarItem:
        return;
    default:
        break;
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

void XpStyle::drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                          const QWidget *widget) const
{
    // The Accurate style: XP's push buttons with text and its menu items
    // have no pictures.
    QStyleOptionButton plainButton;
    QStyleOptionMenuItem plainItem;
    if (m_look.accurate) {
        if (element == CE_PushButton || element == CE_PushButtonLabel) {
            if (const auto *b = qstyleoption_cast<const QStyleOptionButton *>(option); b && hasTextAndIcon(*b)) {
                plainButton = *b;
                plainButton.icon = QIcon();
                option = &plainButton;
            }
        } else if (element == CE_MenuItem) {
            if (const auto *m = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
                m && m->menuItemType != QStyleOptionMenuItem::Separator) {
                plainItem = *m;
                plainItem.icon = QIcon();
                plainItem.maxIconWidth = 0;
                option = &plainItem;
            }
        }
    }
    if (!themed()) {
        QProxyStyle::drawControl(element, option, painter, widget);
        return;
    }
    switch (element) {
    case CE_PushButtonLabel:
    case CE_CheckBoxLabel:
    case CE_RadioButtonLabel: {
        // The text in the part's TextColor for its state.
        const QStyleOption *o = option;
        QStyleOptionButton button;
        if (const auto *b = qstyleoption_cast<const QStyleOptionButton *>(option)) {
            button = *b;
            QString part = element == CE_PushButtonLabel ? QStringLiteral("pushbutton")
                : element == CE_CheckBoxLabel ? QStringLiteral("checkbox") : QStringLiteral("radiobutton");
            const PartState s = element == CE_PushButtonLabel ? buttonState(option, false)
                                                          : checkState(option, element == CE_RadioButtonLabel);
            if (option->state & State_Enabled) {
                const QColor c = partColour(kButton, part, s.name, QStringLiteral("textcolor"),
                                            option->palette.color(QPalette::ButtonText));
                button.palette.setColor(QPalette::ButtonText, c);
                button.palette.setColor(QPalette::WindowText, c);
            }
            o = &button;
        }
        QProxyStyle::drawControl(element, o, painter, widget);
        return;
    }
    case CE_TabBarTabShape:
        if (const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option)) {
            if (tab->shape == QTabBar::RoundedNorth || tab->shape == QTabBar::TriangularNorth) {
                drawTab(tab, painter, widget);
                return;
            }
        }
        break;
    case CE_HeaderSection: {
        const bool enabled = option->state & State_Enabled;
        const PartState s = fourState(enabled, option->state & State_Sunken, option->state & State_MouseOver);
        if (draw(painter, option->rect, QStringLiteral("header"), QStringLiteral("headeritem"),
                 s.id == 4 ? QStringLiteral("normal") : s.name, s.id == 4 ? 1 : s.id))
            return;
        break;
    }
    case CE_HeaderEmptyArea:
        if (draw(painter, option->rect, QStringLiteral("header"), QStringLiteral("headeritem"), QStringLiteral("normal"), 1))
            return;
        break;
    case CE_ProgressBarGroove:
    case CE_ProgressBarContents:
        if (const auto *bar = qstyleoption_cast<const QStyleOptionProgressBar *>(option)) {
            if (element == CE_ProgressBarGroove) {
                const bool vertical = !(bar->state & State_Horizontal);
                if (draw(painter, option->rect, QStringLiteral("progress"), vertical ? QStringLiteral("barvert") : QStringLiteral("bar"), {}, 0))
                    return;
            } else {
                drawProgress(bar, painter, widget);
                return;
            }
        }
        break;
    case CE_ToolBar:
        // A rebar band: the style's own, no etched lines
        draw(painter, option->rect, QStringLiteral("rebar"), {}, {}, 0);
        return;
    case CE_MenuBarEmptyArea:
        painter->fillRect(option->rect, system(QStringLiteral("menubar"), option->palette.color(QPalette::Window)));
        return;
    case CE_MenuBarItem:
        if (const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option)) {
            // Flat menus: the selected or open item in COLOR_MENUHILIGHT.
            painter->fillRect(option->rect, system(QStringLiteral("menubar"), option->palette.color(QPalette::Window)));
            const bool active = (option->state & State_Selected) && (option->state & (State_Sunken | State_MouseOver))
                && (option->state & State_Enabled);
            QPalette::ColorRole role = QPalette::ButtonText;
            if (active) {
                painter->fillRect(option->rect, system(QStringLiteral("menuhilight"), option->palette.color(QPalette::Highlight)));
                role = QPalette::HighlightedText;
            }
            const int flags = Qt::AlignCenter | Qt::TextShowMnemonic | Qt::TextDontClip | Qt::TextSingleLine
                | (styleHint(SH_UnderlineShortcut, option, widget) ? 0 : Qt::TextHideMnemonic);
            QPalette palette = item->palette;
            if (active)
                palette.setColor(QPalette::HighlightedText, system(QStringLiteral("hilighttext"), Qt::white));
            drawItemText(painter, option->rect, flags, palette, option->state & State_Enabled, item->text, role);
            return;
        }
        break;
    case CE_ComboBoxLabel:
        if (const auto *combo = qstyleoption_cast<const QStyleOptionComboBox *>(option)) {
            // A focused drop-down list shows its text selected, as XP's does.
            if (!combo->editable && (combo->state & State_HasFocus) && (combo->state & State_Enabled)) {
                QStyleOptionComboBox copy = *combo;
                const QRect field = subControlRect(CC_ComboBox, combo, SC_ComboBoxEditField, widget);
                painter->fillRect(field, combo->palette.color(QPalette::Highlight));
                copy.palette.setColor(QPalette::ButtonText, combo->palette.color(QPalette::HighlightedText));
                copy.palette.setColor(QPalette::Text, combo->palette.color(QPalette::HighlightedText));
                QProxyStyle::drawControl(element, &copy, painter, widget);
                return;
            }
            QStyleOptionComboBox copy = *combo;
            copy.palette.setColor(QPalette::ButtonText, combo->palette.color(QPalette::Text));
            QProxyStyle::drawControl(element, &copy, painter, widget);
            return;
        }
        break;
    case CE_SizeGrip: {
        const w4w::Part grip{QStringLiteral("status"), QStringLiteral("gripper"), {}, 0, {}};
        const QSize size = m_renderer->trueSize(grip);
        if (!size.isEmpty()) {
            const QRect r(option->rect.right() - size.width() + 1, option->rect.bottom() - size.height() + 1,
                          size.width(), size.height());
            if (m_renderer->draw(painter, r, grip))
                return;
        }
        break;
    }
    case CE_ShapedFrame:
        if (const auto *frame = qstyleoption_cast<const QStyleOptionFrame *>(option)) {
            if (frame->frameShape == QFrame::StyledPanel || frame->frameShape == QFrame::Panel
                || frame->frameShape == QFrame::WinPanel || frame->frameShape == QFrame::Box) {
                if (frame->state & (State_Sunken | State_Raised)) {
                    drawPrimitive(PE_Frame, option, painter, widget);
                    return;
                }
            }
        }
        break;
    default:
        break;
    }
    QProxyStyle::drawControl(element, option, painter, widget);
}

void XpStyle::drawTab(const QStyleOptionTab *tab, QPainter *painter, const QWidget *widget) const
{
    // XP's tabs: the selected one (TopTabItem) is 2 px wider on each side and
    // stands 2 px higher; the first tab at the pane's left edge uses the
    // LeftEdge art.
    const bool selected = tab->state & State_Selected;
    const bool first = tab->position == QStyleOptionTab::Beginning || tab->position == QStyleOptionTab::OnlyOneTab;
    QString part = selected ? QStringLiteral("toptabitem") : QStringLiteral("tabitem");
    if (first && !tab->documentMode)
        part += QStringLiteral("leftedge");
    PartState s{QStringLiteral("normal"), 1};
    if (!(tab->state & State_Enabled))
        s = {QStringLiteral("disabled"), 4};
    else if (selected)
        s = {QStringLiteral("selected"), 3};
    else if (tab->state & State_MouseOver)
        s = {QStringLiteral("hot"), 2};
    QRect r = tab->rect;
    if (selected)
        r.adjust(first ? 0 : -2, 0, 2, 1);
    else
        r.adjust(0, 2, 0, 0);
    if (!draw(painter, r, QStringLiteral("tab"), part, s.name, s.id))
        QProxyStyle::drawControl(CE_TabBarTabShape, tab, painter, widget);
}

void XpStyle::drawProgress(const QStyleOptionProgressBar *bar, QPainter *painter, const QWidget *widget) const
{
    const bool vertical = !(bar->state & State_Horizontal);
    const QString barPart = vertical ? QStringLiteral("barvert") : QStringLiteral("bar");
    const QString chunkPart = vertical ? QStringLiteral("chunkvert") : QStringLiteral("chunk");
    QRect inside = m_renderer->contentRect({QStringLiteral("progress"), barPart, {}, 0, {}}, bar->rect);
    if (!inside.isValid())
        inside = bar->rect.adjusted(3, 3, -3, -3);
    const int chunk = qMax(1, m_renderer->integer({QStringLiteral("progress"), chunkPart, {}, 0, {}}, QStringLiteral("progresschunksize")).value_or(8));
    const int space = qMax(0, m_renderer->integer({QStringLiteral("progress"), chunkPart, {}, 0, {}}, QStringLiteral("progressspacesize")).value_or(2));
    const int length = vertical ? inside.height() : inside.width();
    const bool busy = bar->minimum == bar->maximum;
    int from = 0, to = 0;
    if (busy) {
        // Five chunks crossing the bar.
        const int run = 5 * (chunk + space);
        const qint64 step = QDateTime::currentMSecsSinceEpoch() / 60;
        from = int(step * (chunk + space) / 2 % (length + run)) - run;
        to = from + run;
    } else {
        const qint64 range = qint64(bar->maximum) - bar->minimum;
        const qint64 value = qBound(qint64(bar->minimum), qint64(bar->progress), qint64(bar->maximum)) - bar->minimum;
        to = range > 0 ? int(value * length / range) : 0;
    }
    painter->save();
    painter->setClipRect(inside);
    const bool reversed = bar->invertedAppearance != (bar->direction == Qt::RightToLeft && !vertical);
    const w4w::Part piece{QStringLiteral("progress"), chunkPart, {}, 0, {}};
    const QColor fill = partColour(QStringLiteral("progress"), chunkPart, {}, QStringLiteral("fillcolor"),
                                   bar->palette.color(QPalette::Highlight));
    for (int at = from; at < to; at += chunk + space) {
        const int start = qMax(0, at), end = qMin(length, at + chunk);
        if (end <= start)
            continue;
        QRect r;
        if (vertical)
            r = QRect(inside.left(), inside.bottom() + 1 - end, inside.width(), end - start);
        else if (reversed)
            r = QRect(inside.right() + 1 - end, inside.top(), end - start, inside.height());
        else
            r = QRect(inside.left() + start, inside.top(), end - start, inside.height());
        if (!m_renderer->draw(painter, r, piece))
            painter->fillRect(r, fill);
    }
    painter->restore();
    Q_UNUSED(widget);
}

void XpStyle::drawComplexControl(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter,
                                 const QWidget *widget) const
{
    if (!themed()) {
        QProxyStyle::drawComplexControl(control, option, painter, widget);
        return;
    }
    switch (control) {
    case CC_ScrollBar:
        if (const auto *bar = qstyleoption_cast<const QStyleOptionSlider *>(option)) {
            drawScrollBar(bar, painter, widget);
            return;
        }
        break;
    case CC_ComboBox:
        if (const auto *combo = qstyleoption_cast<const QStyleOptionComboBox *>(option)) {
            drawComboBox(combo, painter, widget);
            return;
        }
        break;
    case CC_ToolButton:
        // A button with its own arrow (Back, Forward): the toolbar's split button, in two parts.
        if (const auto *tool = qstyleoption_cast<const QStyleOptionToolButton *>(option);
            tool && (tool->features & QStyleOptionToolButton::MenuButtonPopup) && (tool->state & State_AutoRaise)) {
            const QRect button = subControlRect(CC_ToolButton, tool, SC_ToolButton, widget);
            const QRect arrow = subControlRect(CC_ToolButton, tool, SC_ToolButtonMenu, widget);
            const bool enabled = tool->state & State_Enabled, hot = enabled && (tool->state & State_MouseOver);
            const auto stateOf = [&](SubControl sc) {
                if (!enabled)
                    return PartState{QStringLiteral("disabled"), 4};
                if ((tool->activeSubControls & sc) && (tool->state & State_Sunken))
                    return PartState{QStringLiteral("pressed"), 3};
                if (tool->state & State_On)
                    return hot ? PartState{QStringLiteral("hotchecked"), 6} : PartState{QStringLiteral("checked"), 5};
                return hot ? PartState{QStringLiteral("hot"), 2} : PartState{QStringLiteral("normal"), 1};
            };
            const PartState main = stateOf(SC_ToolButton), menu = stateOf(SC_ToolButtonMenu);
            // At rest the button has no frame; its arrow, a glyph of the part, is always there.
            if (main.id != 1 && main.id != 4)
                draw(painter, button, QStringLiteral("toolbar"), QStringLiteral("splitbutton"), main.name, main.id);
            if (draw(painter, arrow, QStringLiteral("toolbar"), QStringLiteral("splitbuttondropdown"), menu.name, menu.id)) {
                QStyleOptionToolButton label = *tool;
                label.rect = button;
                drawControl(CE_ToolButtonLabel, &label, painter, widget);
                return;
            }
        }
        break;
    case CC_SpinBox:
        if (const auto *spin = qstyleoption_cast<const QStyleOptionSpinBox *>(option)) {
            drawSpinBox(spin, painter, widget);
            return;
        }
        break;
    case CC_Slider:
        if (const auto *slider = qstyleoption_cast<const QStyleOptionSlider *>(option)) {
            drawSlider(slider, painter, widget);
            return;
        }
        break;
    case CC_GroupBox:
        if (const auto *box = qstyleoption_cast<const QStyleOptionGroupBox *>(option)) {
            // The title in the group box's TextColor (Luna's blue).
            QStyleOptionGroupBox copy = *box;
            if (box->state & State_Enabled)
                copy.textColor = partColour(kButton, QStringLiteral("groupbox"), QStringLiteral("normal"),
                                            QStringLiteral("textcolor"), box->textColor);
            QProxyStyle::drawComplexControl(control, &copy, painter, widget);
            return;
        }
        break;
    default:
        break;
    }
    QProxyStyle::drawComplexControl(control, option, painter, widget);
}

void XpStyle::drawScrollBar(const QStyleOptionSlider *bar, QPainter *painter, const QWidget *widget) const
{
    const bool horizontal = bar->orientation == Qt::Horizontal;
    const bool enabled = (bar->state & State_Enabled) && bar->maximum > bar->minimum;
    auto stateOf = [&](SubControl sc) {
        const bool active = bar->activeSubControls & sc;
        return fourState(enabled, active && (bar->state & State_Sunken), active && (bar->state & State_MouseOver));
    };
    const QString cls = QStringLiteral("scrollbar");
    // The tracks: page up/left is the lower track, page down/right the upper.
    for (SubControl sc : {SC_ScrollBarSubPage, SC_ScrollBarAddPage}) {
        const QRect r = subControlRect(CC_ScrollBar, bar, sc, widget);
        const PartState s = stateOf(sc);
        const QString part = QString(sc == SC_ScrollBarSubPage ? QStringLiteral("lower") : QStringLiteral("upper"))
            + QStringLiteral("track") + (horizontal ? QStringLiteral("horz") : QStringLiteral("vert"));
        if (!draw(painter, r, cls, part, s.name, s.id))
            painter->fillRect(r, system(QStringLiteral("scrollbar"), bar->palette.color(QPalette::Window)));
    }
    // The arrows: up 1-4, down 5-8, left 9-12, right 13-16.
    for (SubControl sc : {SC_ScrollBarSubLine, SC_ScrollBarAddLine}) {
        const QRect r = subControlRect(CC_ScrollBar, bar, sc, widget);
        const PartState s = stateOf(sc);
        const bool sub = sc == SC_ScrollBarSubLine;
        const QString direction = horizontal ? (sub ? QStringLiteral("left") : QStringLiteral("right"))
                                             : (sub ? QStringLiteral("up") : QStringLiteral("down"));
        const int base = horizontal ? (sub ? 8 : 12) : (sub ? 0 : 4);
        if (!draw(painter, r, cls, QStringLiteral("arrowbtn"), direction + s.name, base + s.id)) {
            QStyleOptionSlider copy = *bar;
            copy.subControls = sc;
            QProxyStyle::drawComplexControl(CC_ScrollBar, &copy, painter, widget);
        }
    }
    // The thumb, with its gripper glyph when there is room for it.
    if (enabled) {
        const QRect r = subControlRect(CC_ScrollBar, bar, SC_ScrollBarSlider, widget);
        const PartState s = stateOf(SC_ScrollBarSlider);
        if (draw(painter, r, cls, horizontal ? QStringLiteral("thumbbtnhorz") : QStringLiteral("thumbbtnvert"), s.name, s.id)) {
            const w4w::Part gripper{cls, horizontal ? QStringLiteral("gripperhorz") : QStringLiteral("grippervert"), s.name, s.id, {}};
            const QSize size = m_renderer->trueSize(gripper);
            if (!size.isEmpty() && (horizontal ? r.width() : r.height()) > (horizontal ? size.width() : size.height()) + 6) {
                const QRect g(r.center().x() - size.width() / 2 + (size.width() % 2 ? 0 : 1),
                              r.center().y() - size.height() / 2 + (size.height() % 2 ? 0 : 1), size.width(), size.height());
                m_renderer->draw(painter, g.intersected(r), gripper);
            }
        } else {
            QStyleOptionSlider copy = *bar;
            copy.subControls = SC_ScrollBarSlider;
            QProxyStyle::drawComplexControl(CC_ScrollBar, &copy, painter, widget);
        }
    }
}

void XpStyle::drawComboBox(const QStyleOptionComboBox *combo, QPainter *painter, const QWidget *widget) const
{
    const bool enabled = combo->state & State_Enabled;
    const bool hot = combo->state & State_MouseOver;
    // The field: the combo box class's own BorderFill (Combobox(Hot), ...).
    const QString frameState = !enabled ? QStringLiteral("disabled") : hot ? QStringLiteral("hot") : QString();
    const int frameId = !enabled ? 4 : hot ? 2 : 1;
    if (combo->frame) {
        if (!draw(painter, combo->rect, QStringLiteral("combobox"), {}, frameState, frameId))
            draw(painter, combo->rect, QStringLiteral("edit"), QStringLiteral("edittext"),
                 enabled ? QStringLiteral("normal") : QStringLiteral("disabled"), enabled ? 1 : 4);
    } else {
        painter->fillRect(combo->rect, combo->palette.color(QPalette::Base));
    }
    if (combo->subControls & SC_ComboBoxArrow) {
        const QRect arrow = subControlRect(CC_ComboBox, combo, SC_ComboBoxArrow, widget);
        const bool pressed = (combo->activeSubControls & SC_ComboBoxArrow) && (combo->state & State_Sunken);
        const PartState s = fourState(enabled, pressed || (combo->state & State_On && !combo->editable && pressed),
                                  hot && (combo->editable ? bool(combo->activeSubControls & SC_ComboBoxArrow) : true));
        if (!draw(painter, arrow, QStringLiteral("combobox"), QStringLiteral("dropdownbutton"), s.name, s.id)) {
            QStyleOptionComboBox copy = *combo;
            copy.subControls = SC_ComboBoxArrow;
            QProxyStyle::drawComplexControl(CC_ComboBox, &copy, painter, widget);
        }
    }
}

void XpStyle::drawSpinBox(const QStyleOptionSpinBox *spin, QPainter *painter, const QWidget *widget) const
{
    const bool enabled = spin->state & State_Enabled;
    if (spin->frame) {
        const PartState s = enabled ? PartState{QStringLiteral("normal"), 1} : PartState{QStringLiteral("disabled"), 4};
        draw(painter, spin->rect, QStringLiteral("edit"), QStringLiteral("edittext"), s.name, s.id);
    }
    if (spin->buttonSymbols == QAbstractSpinBox::NoButtons)
        return;
    for (SubControl sc : {SC_SpinBoxUp, SC_SpinBoxDown}) {
        const QRect r = subControlRect(CC_SpinBox, spin, sc, widget);
        const bool up = sc == SC_SpinBoxUp;
        const bool stepEnabled = spin->stepEnabled & (up ? QAbstractSpinBox::StepUpEnabled : QAbstractSpinBox::StepDownEnabled);
        const bool active = spin->activeSubControls & sc;
        const PartState s = fourState(enabled && stepEnabled, active && (spin->state & State_Sunken),
                                  active && (spin->state & State_MouseOver));
        if (!draw(painter, r, QStringLiteral("spin"), up ? QStringLiteral("up") : QStringLiteral("down"), s.name, s.id)) {
            QStyleOptionSpinBox copy = *spin;
            copy.subControls = sc;
            copy.frame = false;
            QProxyStyle::drawComplexControl(CC_SpinBox, &copy, painter, widget);
        }
    }
}

void XpStyle::drawSlider(const QStyleOptionSlider *slider, QPainter *painter, const QWidget *widget) const
{
    const bool horizontal = slider->orientation == Qt::Horizontal;
    const bool enabled = slider->state & State_Enabled;
    const QRect groove = subControlRect(CC_Slider, slider, SC_SliderGroove, widget);
    const QRect handle = subControlRect(CC_Slider, slider, SC_SliderHandle, widget);
    const QString cls = QStringLiteral("trackbar");
    if (slider->subControls & SC_SliderGroove) {
        // A 4 px channel through the thumb's middle, inset by half a thumb.
        QRect track;
        if (horizontal)
            track = QRect(groove.left() + handle.width() / 2, handle.center().y() - 1, groove.width() - handle.width(), 4);
        else
            track = QRect(handle.center().x() - 1, groove.top() + handle.height() / 2, 4, groove.height() - handle.height());
        if (!draw(painter, track, cls, horizontal ? QStringLiteral("track") : QStringLiteral("trackvert"), QStringLiteral("normal"), 1))
            qDrawShadePanel(painter, track, slider->palette, true, 1);
    }
    if (slider->subControls & SC_SliderTickmarks && slider->tickPosition != QSlider::NoTicks) {
        const QColor tic = partColour(cls, horizontal ? QStringLiteral("tics") : QStringLiteral("ticsvert"), QStringLiteral("normal"),
                                      QStringLiteral("color"), slider->palette.color(QPalette::Dark));
        painter->save();
        painter->setPen(tic);
        const int interval = slider->tickInterval > 0 ? slider->tickInterval : qMax(1, slider->pageStep);
        const int available = (horizontal ? groove.width() - handle.width() : groove.height() - handle.height());
        for (int v = slider->minimum; v <= slider->maximum; v += interval) {
            const int pos = sliderPositionFromValue(slider->minimum, slider->maximum, v, available, slider->upsideDown)
                + (horizontal ? handle.width() : handle.height()) / 2;
            if (horizontal) {
                const int x = groove.left() + pos;
                if (slider->tickPosition & QSlider::TicksAbove)
                    painter->drawLine(x, handle.top() - 4, x, handle.top() - 2);
                if (slider->tickPosition & QSlider::TicksBelow)
                    painter->drawLine(x, handle.bottom() + 2, x, handle.bottom() + 4);
            } else {
                const int y = groove.top() + pos;
                if (slider->tickPosition & QSlider::TicksLeft)
                    painter->drawLine(handle.left() - 4, y, handle.left() - 2, y);
                if (slider->tickPosition & QSlider::TicksRight)
                    painter->drawLine(handle.right() + 2, y, handle.right() + 4, y);
            }
            if (v > slider->maximum - interval)
                break;
        }
        painter->restore();
    }
    if (slider->subControls & SC_SliderHandle) {
        // Thumb (both sides or none), ThumbBottom (ticks below: it points
        // down), ThumbTop; vertical: ThumbVert, ThumbRight, ThumbLeft.
        QString part;
        const auto ticks = slider->tickPosition;
        if (horizontal)
            part = ticks == QSlider::TicksBelow ? QStringLiteral("thumbbottom")
                : ticks == QSlider::TicksAbove ? QStringLiteral("thumbtop") : QStringLiteral("thumb");
        else
            part = ticks == QSlider::TicksRight ? QStringLiteral("thumbright")
                : ticks == QSlider::TicksLeft ? QStringLiteral("thumbleft") : QStringLiteral("thumbvert");
        const bool active = slider->activeSubControls & SC_SliderHandle;
        PartState s{QStringLiteral("normal"), 1};
        if (!enabled)
            s = {QStringLiteral("disabled"), 5};
        else if (active && (slider->state & State_Sunken))
            s = {QStringLiteral("pressed"), 3};
        else if (active && (slider->state & State_MouseOver))
            s = {QStringLiteral("hot"), 2};
        else if (slider->state & State_HasFocus)
            s = {QStringLiteral("focused"), 4};
        if (!draw(painter, handle, cls, part, s.name, s.id)) {
            QStyleOptionSlider copy = *slider;
            copy.subControls = SC_SliderHandle;
            QProxyStyle::drawComplexControl(CC_Slider, &copy, painter, widget);
        }
    }
}

QRect XpStyle::subControlRect(ComplexControl control, const QStyleOptionComplex *option, SubControl sc,
                              const QWidget *widget) const
{
    if (themed()) {
        if (control == CC_ComboBox) {
            if (const auto *combo = qstyleoption_cast<const QStyleOptionComboBox *>(option)) {
                const QRect inner = combo->rect.adjusted(1, 1, -1, -1);
                const int w = scrollBarExtent();
                switch (sc) {
                case SC_ComboBoxFrame:
                    return combo->rect;
                case SC_ComboBoxArrow:
                    return QRect(inner.right() - w + 1, inner.top(), w, inner.height());
                case SC_ComboBoxEditField:
                    return QRect(inner.left() + 2, inner.top() + 1, inner.width() - w - 3, inner.height() - 2);
                default:
                    break;
                }
            }
        } else if (control == CC_SpinBox) {
            if (const auto *spin = qstyleoption_cast<const QStyleOptionSpinBox *>(option)) {
                const QRect inner = spin->frame ? spin->rect.adjusted(1, 1, -1, -1) : spin->rect;
                const int w = spin->buttonSymbols == QAbstractSpinBox::NoButtons ? 0 : qMax(15, scrollBarExtent() - 2);
                const int half = inner.height() / 2;
                switch (sc) {
                case SC_SpinBoxFrame:
                    return spin->rect;
                case SC_SpinBoxUp:
                    return w ? QRect(inner.right() - w + 1, inner.top(), w, half) : QRect();
                case SC_SpinBoxDown:
                    return w ? QRect(inner.right() - w + 1, inner.top() + half, w, inner.height() - half) : QRect();
                case SC_SpinBoxEditField:
                    return QRect(inner.left() + 2, inner.top() + 1, inner.width() - w - 3, inner.height() - 2);
                default:
                    break;
                }
            }
        }
    }
    return QProxyStyle::subControlRect(control, option, sc, widget);
}

QSize XpStyle::sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size,
                                const QWidget *widget) const
{
    const int scale = m_look.dpi;
    QSize s;
    if (m_look.accurate && type == CT_PushButton) {
        // QPushButton made room for its picture, which the Accurate style leaves out.
        if (const auto *b = qstyleoption_cast<const QStyleOptionButton *>(option); b && hasTextAndIcon(*b)) {
            QStyleOptionButton plain = *b;
            plain.icon = QIcon();
            s = QProxyStyle::sizeFromContents(type, &plain, size - QSize(b->iconSize.width() + 4, 0), widget);
        }
    }
    if (s.isEmpty())
        s = QProxyStyle::sizeFromContents(type, option, size, widget);
    if (m_look.accurate) {
        // XP's menus (user32's mncomput.c): an item is as high as the menu
        // font, CYEDGE and 2 px; a separator half of SM_CYMENUSIZE (19); a
        // menu bar's item SM_CYMENUSIZE, its text and twice the font's
        // average character wide.
        if (const auto *m = qstyleoption_cast<const QStyleOptionMenuItem *>(option)) {
            if (type == CT_MenuItem) {
                if (m->menuItemType == QStyleOptionMenuItem::Separator)
                    return QSize(s.width(), 9 * scale / 96);
                s.setHeight(m->fontMetrics.height() + 4 * scale / 96);
            } else if (type == CT_MenuBarItem) {
                s = QSize(size.width() + 2 * m->fontMetrics.averageCharWidth(), qMax(size.height(), 18 * scale / 96));
            }
        }
    }
    if (!themed())
        return s;
    switch (type) {
    case CT_ComboBox:
    case CT_SpinBox:
    case CT_LineEdit:
        // XP's fields are 21 px high at 96 DPI with 8 pt text.
        s.setHeight(qMax(s.height(), 21 * scale / 96));
        break;
    case CT_PushButton:
        // XP's push button: 50 x 14 dialog units
        s = s.expandedTo(QSize(75 * scale / 96, 23 * scale / 96));
        break;
    case CT_TabBarTab:
        s.setHeight(qMax(s.height(), 21 * scale / 96));
        break;
    default:
        break;
    }
    return s;
}

int XpStyle::pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const
{
    // XP's tabs: their words and 6 pixels on each side
    if (metric == PM_TabBarTabHSpace)
        return 12 * (m_look.dpi > 0 ? m_look.dpi : 96) / 96;
    // The Accurate style: the toolbars of XP's own programs (WordPad,
    // Paint) and common dialogs have 16 x 16 pictures (comctl32's default).
    if (m_look.accurate && metric == PM_ToolBarIconSize)
        return 16 * m_look.dpi / 96;
    if (themed()) {
        switch (metric) {
        case PM_ScrollBarExtent:
            return scrollBarExtent();
        case PM_ScrollBarSliderMin:
            return 8 * m_look.dpi / 96;
        case PM_IndicatorWidth:
        case PM_IndicatorHeight: {
            const QSize t = m_renderer->trueSize({kButton, QStringLiteral("checkbox"), QStringLiteral("uncheckednormal"), 1, {}});
            if (!t.isEmpty())
                return metric == PM_IndicatorWidth ? t.width() : t.height();
            break;
        }
        case PM_ExclusiveIndicatorWidth:
        case PM_ExclusiveIndicatorHeight: {
            const QSize t = m_renderer->trueSize({kButton, QStringLiteral("radiobutton"), QStringLiteral("uncheckednormal"), 1, {}});
            if (!t.isEmpty())
                return metric == PM_ExclusiveIndicatorWidth ? t.width() : t.height();
            break;
        }
        case PM_SliderLength:
        case PM_SliderThickness: {
            const auto *slider = qstyleoption_cast<const QStyleOptionSlider *>(option);
            const bool horizontal = !slider || slider->orientation == Qt::Horizontal;
            const QSize t = m_renderer->trueSize({QStringLiteral("trackbar"), horizontal ? QStringLiteral("thumb") : QStringLiteral("thumbvert"), QStringLiteral("normal"), 1, {}});
            if (!t.isEmpty()) {
                const int length = horizontal ? t.width() : t.height();
                const int thickness = horizontal ? t.height() : t.width();
                return metric == PM_SliderLength ? length : thickness + 6;
            }
            break;
        }
        case PM_DefaultFrameWidth:
        case PM_SpinBoxFrameWidth:
        case PM_ComboBoxFrameWidth:
            return 1;
        // Qt takes this off every button of a dialog (they are all "auto default"), whose art
        // would then be 2 pixels short each way; XP's default button only changes its art.
        case PM_ButtonDefaultIndicator:
            return 0;
        case PM_ButtonShiftHorizontal:
        case PM_ButtonShiftVertical:
            return 0;   // XP's themed buttons do not move their text when pressed
        case PM_MenuPanelWidth:
            return 1;
        case PM_MenuHMargin:
        case PM_MenuVMargin:
            return 2;
        case PM_TabBarTabShiftVertical:
            return 0;
        case PM_ToolBarHandleExtent:
            return 6 * m_look.dpi / 96;
        // XP's dialogs: 7 units from the edges, 4 between controls (6 x 13 pixels to 4 x 8 units)
        case PM_LayoutLeftMargin:
        case PM_LayoutRightMargin:
            return 11 * m_look.dpi / 96;
        case PM_LayoutTopMargin:
        case PM_LayoutBottomMargin:
            return 11 * m_look.dpi / 96;
        case PM_LayoutHorizontalSpacing:
            return 6 * m_look.dpi / 96;
        case PM_LayoutVerticalSpacing:
            return 6 * m_look.dpi / 96;
        default:
            break;
        }
    }
    return QProxyStyle::pixelMetric(metric, option, widget);
}

int XpStyle::styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
                       QStyleHintReturn *returnData) const
{
    switch (hint) {
    case SH_EtchDisabledText:
    case SH_DitherDisabledText:
        return themed() ? 0 : QProxyStyle::styleHint(hint, option, widget, returnData);
    case SH_ComboBox_Popup:
        return 0;   // XP's drop-down lists, not a menu over the field
    case SH_ComboBox_PopupFrameStyle:
        return QFrame::Box | QFrame::Plain;   // with XP's one black line around them
    case SH_DialogButtonLayout:
        return QDialogButtonBox::WinLayout;
    case SH_UnderlineShortcut:
        return 0;   // XP hides its keyboard cues ("Hide underlined letters" is its default)
    case SH_ToolTipLabel_Opacity:
        return 255;
    case SH_Menu_MouseTracking:
    case SH_MenuBar_MouseTracking:
        return 1;
    case SH_DialogButtonBox_ButtonsHaveIcons:
        if (m_look.accurate)
            return 0;   // XP's OK and Cancel have no pictures
        break;
    default:
        break;
    }
    return QProxyStyle::styleHint(hint, option, widget, returnData);
}

} // namespace w4w
