// SPDX-License-Identifier: LGPL-2.1-or-later
// A GTK 3 theme made from the XP shell's current visual style: XP's controls
// as images cut from the .msstyles (CSS border-image nine-grids from their
// SizingMargins), the system colours of the session's prefix, over GTK's own
// Adwaita for everything else.
//
//   xpstyle-gtk --out DIR     write the theme into DIR (DIR/gtk-3.0/gtk.css)
//   xpstyle-gtk --watch       for the session: keep ~/.local/share/themes/
//                             Win4Wine-XP-<id> up to date with the shell's
//                             look and name it in XSETTINGS (Net/ThemeName,
//                             Gtk/FontName, Net/IconThemeName), which GTK 3
//                             follows at once
//   xpstyle-gtk --icon-previews  draw the installed icon themes' samples
//                             for Control Panel's Icons item (--watch does too)
//
// Control Panel's GTK/QT Themes item chooses the Standard style or the
// Accurate one (linux-look.conf, session.h): Accurate adds XP's own metrics
// for menus, lists and pop-overs (user32's, comctl32's), and the GTK module
// takes the pictures off buttons and menu items. Each style's theme is kept
// (Win4Wine-XP-<id>, Win4Wine-XP-Accurate-<id>): switching back and forth,
// or a new session, uses the saved one; it is made again only when it is
// missing, when the shell's look changes, or when Rebuild asks for it.
#include "../src/msstyles.h"
#include "../src/session.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QtGui/qguiapplication_platform.h>
#include <QPainter>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QSet>
#include <QTimer>

#include <xcb/xcb.h>

using namespace w4w;

namespace {

QString rgb(const QColor &c)
{
    return QStringLiteral("rgb(%1,%2,%3)").arg(c.red()).arg(c.green()).arg(c.blue());
}

class Generator
{
public:
    Generator(const Look &look, const QString &directory)
        : m_look(look)
        , m_dir(directory)
    {
        if (look.themed)
            if (auto theme = Theme::load(look.stylePath, look.colourName, look.sizeName))
                m_r = std::make_unique<Renderer>(theme, look.dpi);
    }

    bool write();

private:
    QColor sys(const char *name, const QColor &fallback) const
    {
        return m_look.colours.value(QLatin1String(name), fallback);
    }
    QColor partColour(const Part &p, const char *property, const QColor &fallback) const
    {
        return m_r ? m_r->colour(p, QLatin1String(property)).value_or(fallback) : fallback;
    }
    bool has(const Part &p) const { return m_r && m_r->has(p); }
    bool borderFill(const Part &p) const
    {
        return m_r && m_r->property(p, QStringLiteral("bgtype")).value_or(QString()).trimmed()
                          .compare(QLatin1String("borderfill"), Qt::CaseInsensitive) == 0;
    }
    // Save an image as an asset; returns its url() for the CSS.
    QString save(const QString &name, const QImage &image)
    {
        QDir().mkpath(m_dir + QStringLiteral("/gtk-3.0/assets"));
        image.convertToFormat(QImage::Format_ARGB32).save(m_dir + QStringLiteral("/gtk-3.0/assets/") + name + QStringLiteral(".png"));
        return QStringLiteral("url(\"assets/%1.png\")").arg(name);
    }
    // The part drawn at SIZE (with its glyph), as an asset.
    QString render(const QString &name, const Part &p, QSize size)
    {
        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        m_r->draw(&painter, image.rect(), p);
        painter.end();
        return save(name, image);
    }
    // A part's frame as a CSS border-image: its SizingMargins are the slices,
    // stretched or tiled as its SizingType says; BorderOnly parts get an
    // empty middle.
    QString borderImage(const QString &name, const Part &p, bool inset = true, const QString &glyph = QString())
    {
        if (!has(p))
            return QString();
        QImage frame = m_r->stateImage(p);
        if (frame.isNull())
            return QString();
        QMargins m;
        parseMargins(m_r->property(p, QStringLiteral("sizingmargins")).value_or(QString()), &m);
        if (parseBool(m_r->property(p, QStringLiteral("borderonly")).value_or(QString()))) {
            const QRect middle = frame.rect().marginsRemoved(m);
            for (int y = middle.top(); y <= middle.bottom(); ++y)
                for (int x = middle.left(); x <= middle.right(); ++x)
                    frame.setPixel(x, y, 0);
        }
        const bool tile = m_r->property(p, QStringLiteral("sizingtype")).value_or(QString()).trimmed()
                              .compare(QLatin1String("tile"), Qt::CaseInsensitive) == 0;
        const QString url = save(name, frame);
        // GTK 3 leaves a border-image's middle empty: the middle slice is the
        // background, inside the content box (or the padding box for parts
        // without padding), under the border slices, so the corners keep
        // their transparent pixels.
        // GTK smooths a stretched background and fades its edges: the middle
        // is enlarged here (nearest neighbour, as uxtheme samples) so that GTK
        // only scales it a little and the faded edges stay under the borders.
        // GLYPH, if any, is a centred layer over it (a scroll thumb's gripper).
        const QRect middle = frame.rect().marginsRemoved(m);
        QString background = glyph.isEmpty() ? QStringLiteral("background-image: none;")
                                             : QStringLiteral("background-image: %1; background-repeat: no-repeat; background-position: center;").arg(glyph);
        if (!middle.isEmpty()) {
            const QImage big = frame.copy(middle).scaled(qMax(middle.width(), 256), qMax(middle.height(), 256),
                                                         Qt::IgnoreAspectRatio, Qt::FastTransformation);
            const QString box = QStringLiteral("padding-box");
            const QString url = save(name + QStringLiteral("-middle"), big);
            background = glyph.isEmpty()
                ? QStringLiteral("background-image: %1; background-size: 100% 100%; background-repeat: no-repeat; "
                                 "background-clip: %2; background-origin: %2;").arg(url, box)
                : QStringLiteral("background-image: %1, %2; background-size: auto, 100% 100%; background-repeat: no-repeat, no-repeat; "
                                 "background-position: center, 0 0; background-clip: %3, %3; background-origin: %3, %3;").arg(glyph, url, box);
        }
        // INSET: a transparent 2 px border keeps the middle off the rounded
        // corners (the rules take those 2 px off their padding).
        return QStringLiteral("border-image: %1 %2 %3 %4 %5 / %2px %3px %4px %5px %6; border-style: solid; "
                              "border-color: transparent; border-width: %8px; %7 background-color: transparent; "
                              "box-shadow: none; border-radius: 0;")
            .arg(url).arg(m.top()).arg(m.right()).arg(m.bottom()).arg(m.left())
            .arg(tile ? QStringLiteral("repeat") : QStringLiteral("stretch"), background)
            .arg(inset ? 2 : 0);
    }
    // A BorderFill part as CSS border and background colours.
    QString fill(const Part &p, const QColor &border, const QColor &back) const
    {
        const int size = m_r ? m_r->integer(p, QStringLiteral("bordersize")).value_or(1) : 1;
        return QStringLiteral("border: %1px solid %2; background-color: %3; background-image: none; border-radius: 0; box-shadow: none;")
            .arg(size).arg(rgb(partColour(p, "bordercolor", border)), rgb(partColour(p, "fillcolor", back)));
    }

    void colours(QTextStream &css);
    void buttons(QTextStream &css);
    void checks(QTextStream &css);
    void fields(QTextStream &css);
    void scrollbars(QTextStream &css);
    void ranges(QTextStream &css);
    void tabs(QTextStream &css);
    void menus(QTextStream &css);
    void extras(QTextStream &css);
    void accurate(QTextStream &css);

    Look m_look;
    QString m_dir;
    std::unique_ptr<Renderer> m_r;
};

// Selectors and their part, one rule per state.
struct StateRule
{
    const char *selector;
    const char *state;
    int id;
};

void Generator::colours(QTextStream &css)
{
    const QColor face = sys("buttonface", QColor(236, 233, 216)), window = sys("window", Qt::white);
    const QColor text = sys("windowtext", Qt::black), gray = sys("graytext", QColor(172, 168, 153));
    const QColor hilight = sys("hilight", QColor(49, 106, 197)), hilightText = sys("hilighttext", Qt::white);
    // The names GTK 3 themes define and applications use.
    css << "@define-color theme_bg_color " << rgb(face) << ";\n@define-color theme_fg_color " << rgb(text) << ";\n"
        << "@define-color theme_base_color " << rgb(window) << ";\n@define-color theme_text_color " << rgb(text) << ";\n"
        << "@define-color theme_selected_bg_color " << rgb(hilight) << ";\n@define-color theme_selected_fg_color " << rgb(hilightText) << ";\n"
        << "@define-color insensitive_fg_color " << rgb(gray) << ";\n@define-color insensitive_bg_color " << rgb(face) << ";\n"
        << "@define-color borders " << rgb(sys("buttonshadow", QColor(172, 168, 153))) << ";\n"
        << "@define-color theme_tooltip_bg_color " << rgb(sys("infowindow", QColor(255, 255, 225))) << ";\n"
        << "@define-color theme_tooltip_fg_color " << rgb(sys("infotext", Qt::black)) << ";\n\n";
    css << "* { -gtk-outline-radius: 0; outline-color: " << rgb(text) << "; outline-style: dotted; outline-width: 1px; }\n"
        << "window, .background, dialog, messagedialog, assistant, popover, statusbar, actionbar, toolbar, infobar, headerbar, .titlebar"
        << " { background-color: " << rgb(face) << "; background-image: none; color: " << rgb(text) << "; }\n"
        << "headerbar, .titlebar { border-radius: 0; box-shadow: none; border-bottom: 1px solid " << rgb(sys("buttonshadow", gray)) << "; }\n"
        << ".view, view, textview text, treeview.view, iconview, list, row, placessidebar, .sidebar"
        << " { background-color: " << rgb(window) << "; color: " << rgb(text) << "; }\n"
        << "*:disabled, label:disabled { color: " << rgb(gray) << "; }\n"
        << "*:selected, selection, .view:selected, row:selected, treeview.view:selected, iconview:selected,"
        << " entry selection, textview text selection, label selection"
        << " { background-color: " << rgb(hilight) << "; color: " << rgb(hilightText) << "; }\n"
        << "*:selected:backdrop, row:selected:backdrop, treeview.view:selected:backdrop"
        << " { background-color: " << rgb(face) << "; color: " << rgb(text) << "; }\n"
        << "*:link, link, button.link { color: " << rgb(sys("hottrackingcolor", QColor(0, 0, 128))) << "; }\n"
        << "separator { background-color: " << rgb(sys("buttonshadow", gray)) << "; min-width: 1px; min-height: 1px; }\n"
        << "frame > border, scrolledwindow.frame, .frame { border: 1px solid "
        << rgb(partColour({QStringLiteral("listbox"), {}, {}, 0, {}}, "bordercolor", sys("buttonshadow", gray)))
        << "; border-radius: 0; box-shadow: none; }\n"
        << "frame > label { color: " << rgb(partColour({QStringLiteral("button"), QStringLiteral("groupbox"), QStringLiteral("normal"), 1, {}}, "textcolor", text)) << "; }\n"
        << "tooltip, tooltip.background, .tooltip { background-color: " << rgb(sys("infowindow", QColor(255, 255, 225)))
        << "; color: " << rgb(sys("infotext", Qt::black)) << "; border: 1px solid " << rgb(sys("windowframe", Qt::black))
        << "; border-radius: 0; box-shadow: none; text-shadow: none; padding: 1px 2px; }\n"
        << "tooltip * { color: " << rgb(sys("infotext", Qt::black)) << "; }\n"
        << ".csd.popup decoration, tooltip decoration { border-radius: 0; box-shadow: none; }\n\n";
}

void Generator::buttons(QTextStream &css)
{
    const QColor text = sys("buttontext", Qt::black);
    const Part base{QStringLiteral("button"), QStringLiteral("pushbutton"), {}, 0, {}};
    if (has(base)) {
        const StateRule rules[] = {
            {"button", "normal", 1},
            {"button:hover", "hot", 2},
            {"button:active, button:checked, button:hover:active, button:hover:checked", "pressed", 3},
            {"button:disabled", "disabled", 4},
            {"button.default, button:focus:not(:hover):not(:active)", "defaulted", 5},
        };
        for (const StateRule &r : rules) {
            const Part p{base.cls, base.part, QLatin1String(r.state), r.id, {}};
            css << r.selector << " { " << borderImage(QStringLiteral("button-%1").arg(QLatin1String(r.state)), p)
                << " color: " << rgb(partColour(p, "textcolor", text)) << "; text-shadow: none; -gtk-icon-shadow: none; }\n";
        }
        const QMargins c = m_r->contentMargins(base);
        css << "button { padding: " << qMax(0, c.top() - 2) << "px " << qMax(2, c.right() + 1) << "px " << qMax(0, c.bottom() - 2)
            << "px " << qMax(2, c.left() + 1) << "px; min-height: 17px; outline-offset: -2px; }\n"
            << "button.flat:not(:hover):not(:active):not(:checked), button.image-button.flat:not(:hover):not(:active), "
               "toolbar button:not(:hover):not(:active):not(:checked), headerbar button.titlebutton"
            << " { border-image: none; background: transparent; }\n";
    }
    // Toolbar buttons: nothing at rest, XP's toolbar art when hot, pressed or checked.
    const Part tb{QStringLiteral("toolbar"), QStringLiteral("button"), {}, 0, {}};
    if (has(tb)) {
        const StateRule rules[] = {
            {"toolbar button:hover, button.flat:hover", "hot", 2},
            {"toolbar button:active, button.flat:active", "pressed", 3},
            {"toolbar button:checked, button.flat:checked", "checked", 5},
            {"toolbar button:checked:hover, button.flat:checked:hover", "hotchecked", 6},
        };
        for (const StateRule &r : rules)
            css << r.selector << " { " << borderImage(QStringLiteral("toolbar-%1").arg(QLatin1String(r.state)),
                                                    {tb.cls, tb.part, QLatin1String(r.state), r.id, {}})
                << " color: " << rgb(text) << "; }\n";
        css << "toolbar { padding: 2px; border: none; }\ntoolbar button { padding: 1px 2px; border-width: 2px; border-style: solid; border-color: transparent; }\n";
    }
    // Column headers of lists.
    const Part header{QStringLiteral("header"), QStringLiteral("headeritem"), {}, 0, {}};
    if (has(header)) {
        const StateRule rules[] = {
            {"treeview.view header button, column-header button, list > header button", "normal", 1},
            {"treeview.view header button:hover, column-header button:hover", "hot", 2},
            {"treeview.view header button:active, column-header button:active", "pressed", 3},
        };
        for (const StateRule &r : rules)
            css << r.selector << " { " << borderImage(QStringLiteral("header-%1").arg(QLatin1String(r.state)),
                                                    {header.cls, header.part, QLatin1String(r.state), r.id, {}})
                << " color: " << rgb(text) << "; padding: 0 4px; min-height: 15px; font-weight: normal; }\n";
    }
    // Tree expanders: XP's +/- boxes.
    const Part glyph{QStringLiteral("treeview"), QStringLiteral("glyph"), QStringLiteral("closed"), 1, {}};
    if (has(glyph)) {
        const QSize size = m_r->trueSize(glyph);
        css << "treeview.view.expander, treeview.view.expander:hover { -gtk-icon-source: "
            << save(QStringLiteral("tree-closed"), m_r->stateImage(glyph)) << "; color: transparent; min-width: "
            << size.width() << "px; min-height: " << size.height() << "px; }\n"
            << "treeview.view.expander:checked, treeview.view.expander:checked:hover { -gtk-icon-source: "
            << save(QStringLiteral("tree-opened"), m_r->stateImage({glyph.cls, glyph.part, QStringLiteral("opened"), 2, {}})) << "; }\n"
            << "treeview.view { -GtkTreeView-expander-size: " << qMax(size.width(), size.height()) + 4 << "; }\n";
    }
    css << "\n";
}

void Generator::checks(QTextStream &css)
{
    // Check boxes (12 states) and radio buttons (8), as the indicator's icon.
    for (bool radio : {false, true}) {
        const QString part = radio ? QStringLiteral("radiobutton") : QStringLiteral("checkbox");
        const Part probe{QStringLiteral("button"), part, QStringLiteral("uncheckednormal"), 1, {}};
        if (!has(probe))
            continue;
        const QSize size = m_r->trueSize(probe);
        const char *node = radio ? "radio" : "check";
        css << node << " { -gtk-icon-source: none; background-image: none; background-color: transparent; border: none; "
            << "box-shadow: none; border-radius: 0; min-width: " << size.width() << "px; min-height: " << size.height()
            << "px; margin: 0 4px 0 0; padding: 0; }\n";
        const QList<QPair<QString, QString>> values = radio
            ? QList<QPair<QString, QString>>{{QStringLiteral("unchecked"), QStringLiteral(":not(:checked)")},
                                             {QStringLiteral("checked"), QStringLiteral(":checked")}}
            : QList<QPair<QString, QString>>{{QStringLiteral("unchecked"), QStringLiteral(":not(:checked):not(:indeterminate)")},
                                             {QStringLiteral("checked"), QStringLiteral(":checked:not(:indeterminate)")},
                                             {QStringLiteral("mixed"), QStringLiteral(":indeterminate")}};
        const QList<QPair<QString, QString>> states = {{QStringLiteral("normal"), QString()},
                                                       {QStringLiteral("hot"), QStringLiteral(":hover")},
                                                       {QStringLiteral("pressed"), QStringLiteral(":hover:active")},
                                                       {QStringLiteral("disabled"), QStringLiteral(":disabled")}};
        for (int v = 0; v < values.size(); ++v)
            for (int s = 0; s < states.size(); ++s) {
                const QString state = values[v].first + states[s].first;
                const Part p{QStringLiteral("button"), part, state, v * 4 + s + 1, {}};
                const QString url = save(QStringLiteral("%1-%2").arg(QLatin1String(node), state), m_r->stateImage(p));
                css << node << values[v].second << states[s].second << " { -gtk-icon-source: " << url << "; }\n";
            }
    }
    css << "\n";
}

void Generator::fields(QTextStream &css)
{
    const QColor window = sys("window", Qt::white), text = sys("windowtext", Qt::black);
    const QColor border = sys("buttonshadow", QColor(127, 157, 185)), gray = sys("graytext", QColor(172, 168, 153));
    const Part edit{QStringLiteral("edit"), QStringLiteral("edittext"), QStringLiteral("normal"), 1, {}};
    const Part editOff{QStringLiteral("edit"), QStringLiteral("edittext"), QStringLiteral("disabled"), 4, {}};
    if (borderFill(edit)) {
        css << "entry, spinbutton:not(.vertical), textview.frame { " << fill(edit, border, window) << " color: " << rgb(text)
            << "; min-height: 19px; padding: 0 3px; caret-color: " << rgb(text) << "; }\n"
            << "entry:focus, spinbutton:focus { box-shadow: none; }\n"
            << "entry:disabled, spinbutton:disabled { " << fill(editOff, border, sys("buttonface", window))
            << " color: " << rgb(partColour(editOff, "textcolor", gray)) << "; }\n"
            << "spinbutton entry, combobox entry { border: none; background: transparent; box-shadow: none; }\n";
    }
    // Spin buttons: GTK 3 puts them side by side; they take XP's spin art.
    const Part up{QStringLiteral("spin"), QStringLiteral("up"), QStringLiteral("normal"), 1, {}};
    if (has(up)) {
        const StateRule rules[] = {{"spinbutton button", "normal", 1}, {"spinbutton button:hover", "hot", 2},
                                   {"spinbutton button:active", "pressed", 3}, {"spinbutton button:disabled", "disabled", 4}};
        for (const StateRule &r : rules)
            css << r.selector << " { " << borderImage(QStringLiteral("spin-%1").arg(QLatin1String(r.state)),
                                                    {QStringLiteral("button"), QStringLiteral("pushbutton"), QLatin1String(r.state), r.id, {}}, false)
                << " padding: 0 3px; min-width: 13px; min-height: 13px; }\n";
    }
    // Drop-down lists: the field (the combo box class's BorderFill) and its button.
    const Part combo{QStringLiteral("combobox"), {}, {}, 1, {}};
    const Part drop{QStringLiteral("combobox"), QStringLiteral("dropdownbutton"), QStringLiteral("normal"), 1, {}};
    if (has(drop)) {
        const int w = 17 * m_look.dpi / 96, h = 19 * m_look.dpi / 96;
        css << "combobox button.combo, combobox box.linked > button.combo:dir(ltr) { "
            << (borderFill(combo) ? fill(combo, border, window) : fill(edit, border, window))
            << " border-image: none; color: " << rgb(text) << "; padding: 0 0 0 3px; min-height: " << h << "px; }\n"
            << "combobox button.combo:disabled { " << fill({QStringLiteral("combobox"), {}, QStringLiteral("disabled"), 4, {}}, border, sys("buttonface", window)) << " }\n"
            << "combobox arrow { min-width: " << w << "px; min-height: " << h << "px; margin: 0; padding: 0; }\n"
            << "combobox box.linked > entry { border-right: none; }\n"
            << "combobox box.linked > button:not(.combo) { border-image: none; background-color: transparent; padding: 0; border: 1px solid "
            << rgb(partColour(combo, "bordercolor", border)) << "; border-left: none; -gtk-icon-source: none; min-width: " << w << "px; }\n";
        const StateRule rules[] = {{"", "normal", 1}, {":hover", "hot", 2}, {":active", "pressed", 3}, {":disabled", "disabled", 4}};
        for (const StateRule &r : rules) {
            const QString url = render(QStringLiteral("combo-%1").arg(QLatin1String(r.state)),
                                       {drop.cls, drop.part, QLatin1String(r.state), r.id, {}}, QSize(w, h));
            css << "combobox button.combo" << r.selector << " arrow { -gtk-icon-source: " << url << "; }\n"
                << "combobox box.linked > button:not(.combo)" << r.selector << " { background-image: " << url
                << "; background-size: 100% 100%; background-repeat: no-repeat; }\n"
                << "combobox box.linked > button:not(.combo)" << r.selector << " arrow { -gtk-icon-source: none; }\n";
        }
    }
    css << "\n";
}

void Generator::scrollbars(QTextStream &css)
{
    const Part arrow{QStringLiteral("scrollbar"), QStringLiteral("arrowbtn"), QStringLiteral("upnormal"), 1, {}};
    if (!has(arrow))
        return;
    int extent = 17;
    if (auto v = m_r->theme().sysMetric(QStringLiteral("scrollbarwidth")))
        extent = qBound(8, v->trimmed().toInt(), 64);
    extent = extent * m_look.dpi / 96;
    css << "scrollbar { -GtkScrollbar-has-backward-stepper: true; -GtkScrollbar-has-forward-stepper: true; "
        << "background-color: " << rgb(sys("scrollbar", sys("buttonface", Qt::lightGray))) << "; border: none; padding: 0; }\n"
        << "scrollbar slider { margin: 0; border-radius: 0; min-width: " << extent << "px; min-height: " << extent << "px; }\n"
        << "scrollbar.vertical slider { min-height: " << 8 * m_look.dpi / 96 << "px; }\n"
        << "scrollbar.horizontal slider { min-width: " << 8 * m_look.dpi / 96 << "px; }\n"
        << "scrollbar button { min-width: " << extent << "px; min-height: " << extent << "px; padding: 0; margin: 0; "
        << "border: none; border-image: none; border-radius: 0; box-shadow: none; -gtk-icon-source: none; "
        << "background-size: 100% 100%; background-repeat: no-repeat; }\n";
    // Arrows: up 1-4, down 5-8, left 9-12, right 13-16; GTK's steppers are .up (back) and .down (forward).
    const char *states[] = {"normal", "hot", "pressed", "disabled"};
    const char *pseudo[] = {"", ":hover", ":active", ":disabled"};
    struct Direction { const char *orientation, *stepper, *name; int base; };
    const Direction directions[] = {{"vertical", "up", "up", 0}, {"vertical", "down", "down", 4},
                                    {"horizontal", "up", "left", 8}, {"horizontal", "down", "right", 12}};
    for (const Direction &d : directions)
        for (int s = 0; s < 4; ++s) {
            const QString state = QLatin1String(d.name) + QLatin1String(states[s]);
            const QString url = render(QStringLiteral("scroll-%1").arg(state), {arrow.cls, arrow.part, state, d.base + s + 1, {}},
                                       QSize(extent, extent));
            css << "scrollbar." << d.orientation << " button." << d.stepper << pseudo[s] << " { background-image: " << url << "; }\n";
        }
    // The track and the thumb with its gripper (centred over the thumb's art).
    for (bool horizontal : {false, true}) {
        const QString o = horizontal ? QStringLiteral("horz") : QStringLiteral("vert");
        const char *orientation = horizontal ? "horizontal" : "vertical";
        for (int s = 0; s < 4; ++s) {
            const Part track{QStringLiteral("scrollbar"), QStringLiteral("lowertrack") + o, QLatin1String(states[s]), s + 1, {}};
            const QString b = borderImage(QStringLiteral("track-%1-%2").arg(o, QLatin1String(states[s])), track, false);
            if (!b.isEmpty())
                css << "scrollbar." << orientation << pseudo[s] << " trough { " << b << " }\n";
            const Part thumb{QStringLiteral("scrollbar"), QStringLiteral("thumbbtn") + o, QLatin1String(states[s]), s + 1, {}};
            const Part gripper{QStringLiteral("scrollbar"), QStringLiteral("gripper") + o, QLatin1String(states[s]), s + 1, {}};
            const QString g = has(gripper) ? save(QStringLiteral("gripper-%1-%2").arg(o, QLatin1String(states[s])), m_r->stateImage(gripper))
                                           : QString();
            const QString t = borderImage(QStringLiteral("thumb-%1-%2").arg(o, QLatin1String(states[s])), thumb, false, g);
            const char *sliderState[] = {"", ":hover", ":active", ":disabled"};
            if (!t.isEmpty())
                css << "scrollbar." << orientation << " slider" << sliderState[s] << " { " << t << " }\n";
        }
    }
    css << "\n";
}

void Generator::ranges(QTextStream &css)
{
    // Progress bars: the track, and XP's chunks as a repeated tile.
    const Part bar{QStringLiteral("progress"), QStringLiteral("bar"), {}, 0, {}};
    if (has(bar)) {
        const int height = 18 * m_look.dpi / 96;
        const QRect inside = m_r->contentRect(bar, QRect(0, 0, 100, height));
        const QMargins m(inside.left(), inside.top(), 100 - inside.right() - 1, height - inside.bottom() - 1);
        const Part chunk{QStringLiteral("progress"), QStringLiteral("chunk"), {}, 0, {}};
        const int size = qMax(1, m_r->integer(chunk, QStringLiteral("progresschunksize")).value_or(8));
        const int space = qMax(0, m_r->integer(chunk, QStringLiteral("progressspacesize")).value_or(2));
        QImage tile(size + space, inside.height(), QImage::Format_ARGB32_Premultiplied);
        tile.fill(Qt::transparent);
        QPainter painter(&tile);
        if (!m_r->draw(&painter, QRect(0, 0, size, inside.height()), chunk))
            painter.fillRect(QRect(0, 0, size, inside.height()), partColour(chunk, "fillcolor", sys("hilight", Qt::blue)));
        painter.end();
        css << "progressbar trough { " << borderImage(QStringLiteral("progress-bar"), bar) << " min-height: " << height
            << "px; min-width: " << height << "px; padding: " << qMax(0, m.top() - 2) << "px " << qMax(0, m.right() - 2) << "px " << qMax(0, m.bottom() - 2) << "px " << qMax(0, m.left() - 2) << "px; }\n"
            << "progressbar progress { background-image: " << save(QStringLiteral("progress-chunk"), tile)
            << "; background-repeat: repeat-x; background-color: transparent; border: none; border-radius: 0; box-shadow: none; "
            << "min-height: " << inside.height() << "px; }\n"
            << "progressbar.horizontal trough, progressbar.horizontal progress { min-height: " << inside.height() << "px; }\n"
            << "progressbar.horizontal trough { min-height: " << height << "px; }\n";
    }
    // Sliders: the channel and the thumb, at its true size.
    const Part track{QStringLiteral("trackbar"), QStringLiteral("track"), QStringLiteral("normal"), 1, {}};
    const Part thumb{QStringLiteral("trackbar"), QStringLiteral("thumb"), QStringLiteral("normal"), 1, {}};
    if (has(track) && has(thumb)) {
        const QSize t = m_r->trueSize(thumb);
 css << "scale trough { " << borderImage(QStringLiteral("scale-track"), track, false) << " min-height: 4px; min-width: 4px; }\n"
            << "scale highlight, scale fill { background: none; border: none; box-shadow: none; }\n"
            << "scale slider { border: none; box-shadow: none; border-radius: 0; background-color: transparent; "
            << "background-repeat: no-repeat; background-size: 100% 100%; min-width: " << t.width() << "px; min-height: " << t.height()
            << "px; margin: " << -(t.height() / 2 - 2) << "px " << -(t.width() / 2) << "px; }\n";
        const StateRule rules[] = {{"scale slider", "normal", 1}, {"scale slider:hover", "hot", 2},
                                   {"scale slider:active", "pressed", 3}, {"scale slider:disabled", "disabled", 5}};
        for (const StateRule &r : rules)
            css << r.selector << " { background-image: "
                << save(QStringLiteral("scale-thumb-%1").arg(QLatin1String(r.state)), m_r->stateImage({thumb.cls, thumb.part, QLatin1String(r.state), r.id, {}}))
                << "; }\n";
        const Part vthumb{QStringLiteral("trackbar"), QStringLiteral("thumbvert"), QStringLiteral("normal"), 1, {}};
        if (has(vthumb)) {
            const QSize v = m_r->trueSize(vthumb);
            css << "scale.vertical slider { min-width: " << v.width() << "px; min-height: " << v.height() << "px; margin: "
                << -(v.height() / 2) << "px " << -(v.width() / 2 - 2) << "px; }\n";
            for (const StateRule &r : rules)
                css << "scale.vertical " << (r.selector + 6) << " { background-image: "
                    << save(QStringLiteral("scale-thumbvert-%1").arg(QLatin1String(r.state)), m_r->stateImage({vthumb.cls, vthumb.part, QLatin1String(r.state), r.id, {}}))
                    << "; }\n";
        }
    }
    css << "\n";
}

void Generator::tabs(QTextStream &css)
{
    const Part item{QStringLiteral("tab"), QStringLiteral("tabitem"), QStringLiteral("normal"), 1, {}};
    const Part pane{QStringLiteral("tab"), QStringLiteral("pane"), {}, 0, {}};
    if (!has(item) || !has(pane))
        return;
    const QColor text = sys("windowtext", Qt::black);
    css << "notebook > header.top { background: transparent; border: none; padding: 0; box-shadow: none; }\n"
        << "notebook > header.top > tabs { margin: 0 0 -1px 0; }\n"
        << "notebook > header.top tab { padding: 0 4px; min-height: 15px; margin: 2px 0 0 0; color: " << rgb(text)
        << "; outline-offset: -3px; }\n"
        << "notebook > header.top tab:checked { margin: 0 -2px 0 -2px; padding: 1px 6px 1px 6px; }\n";
    const StateRule rules[] = {{"notebook > header.top tab", "normal", 1}, {"notebook > header.top tab:hover", "hot", 2},
                               {"notebook > header.top tab:disabled", "disabled", 4}};
    for (const StateRule &r : rules)
        css << r.selector << " { " << borderImage(QStringLiteral("tab-%1").arg(QLatin1String(r.state)), {item.cls, item.part, QLatin1String(r.state), r.id, {}}) << " }\n";
    css << "notebook > header.top tab:checked { "
        << borderImage(QStringLiteral("tab-selected"), {QStringLiteral("tab"), QStringLiteral("toptabitem"), QStringLiteral("selected"), 3, {}}) << " }\n";
    // The pane's edge with the page's body inside, as one nine-grid.
    QMargins m;
    parseMargins(m_r->property(pane, QStringLiteral("sizingmargins")).value_or(QString()), &m);
    const QSize size(240, 240);
    QImage page(size, QImage::Format_ARGB32_Premultiplied);
    page.fill(Qt::transparent);
    QPainter painter(&page);
    m_r->draw(&painter, page.rect(), pane);
    const QRect inside = m_r->contentRect(pane, page.rect());
    m_r->draw(&painter, inside.isValid() ? inside : page.rect().adjusted(1, 1, -3, -3), {QStringLiteral("tab"), QStringLiteral("body"), {}, 0, {}});
    painter.end();
    css << "notebook > stack:not(:only-child) { border-image: " << save(QStringLiteral("tab-pane"), page) << " "
        << m.top() << " " << m.right() << " " << m.bottom() << " " << m.left() << " / " << m.top() << "px " << m.right() << "px "
        << m.bottom() << "px " << m.left() << "px stretch; border-style: solid; border-width: 0; background: transparent; }\n"
        << "notebook { border: none; background: transparent; padding: 0; }\n\n";
}

void Generator::extras(QTextStream &css)
{
    const QColor window = sys("window", Qt::white), border = sys("buttonshadow", QColor(127, 157, 185));
    const Part edit{QStringLiteral("edit"), QStringLiteral("edittext"), QStringLiteral("normal"), 1, {}};
    const QColor field = partColour(edit, "bordercolor", border);
    // Switches (XP has none): a field with a push button for its knob, the
    // field in the selection colour when on.
    css << "switch { border: 1px solid " << rgb(field) << "; border-radius: 0; background-color: " << rgb(window)
        << "; background-image: none; box-shadow: none; padding: 0; min-width: 34px; min-height: 17px; color: transparent; outline-offset: -3px; }\n"
        << "switch:checked { background-color: " << rgb(sys("hilight", QColor(49, 106, 197))) << "; }\n"
        << "switch:disabled { background-color: " << rgb(sys("buttonface", window)) << "; }\n"
        << "switch image { color: transparent; -gtk-icon-source: none; }\n";
    const StateRule knob[] = {{"switch slider", "normal", 1}, {"switch:hover slider", "hot", 2},
                              {"switch:active slider", "pressed", 3}, {"switch:disabled slider", "disabled", 4}};
    for (const StateRule &r : knob)
        css << r.selector << " { " << borderImage(QStringLiteral("switch-%1").arg(QLatin1String(r.state)),
                                                {QStringLiteral("button"), QStringLiteral("pushbutton"), QLatin1String(r.state), r.id, {}}, false)
            << " min-width: 15px; min-height: 15px; margin: 0; padding: 0; transition: none; }\n";
    // XP's dialog buttons: 75 x 23 at 96 DPI.
    const int wide = qMax(0, 75 * m_look.dpi / 96 - 12);
    css << "dialog .dialog-action-area button, messagedialog .dialog-action-area button, .dialog-action-box button"
        << " { min-width: " << wide << "px; }\n"
        // Frames are XP's (the window manager's); no rounded corners or shadows of GTK's own.
        << "window, window.csd, window.solid-csd, decoration, .titlebar, headerbar"
        << " { border-radius: 0; box-shadow: none; transition: none; }\n"
        << "button, check, radio, entry, scrollbar slider, scale slider, notebook tab, switch slider { transition: none; }\n"
        << "headerbar button.titlebutton { border-image: none; background: transparent; }\n\n";
}

void Generator::menus(QTextStream &css)
{
    // XP's flat menus: COLOR_MENU, a COLOR_BTNSHADOW line, COLOR_MENUHILIGHT items.
    const QColor menu = sys("menu", Qt::white), bar = sys("menubar", sys("buttonface", Qt::lightGray));
    const QColor text = sys("menutext", Qt::black), hilight = sys("menuhilight", sys("hilight", Qt::blue));
    const QColor hilightText = sys("hilighttext", Qt::white), shadow = sys("buttonshadow", Qt::gray);
    css << "menubar, .menubar { background-color: " << rgb(bar) << "; background-image: none; padding: 0; box-shadow: none; border: none; }\n"
        << "menubar > menuitem { padding: 2px 6px; min-height: 15px; color: " << rgb(text) << "; }\n"
        << "menubar > menuitem:hover { background-color: " << rgb(hilight) << "; color: " << rgb(hilightText) << "; box-shadow: none; }\n"
        << "menu, .menu, .context-menu { background-color: " << rgb(menu) << "; color: " << rgb(text) << "; border: 1px solid "
        << rgb(shadow) << "; border-radius: 0; padding: 2px; box-shadow: none; margin: 0; }\n"
        << "menu menuitem, .menu menuitem { padding: 2px 17px 2px 6px; min-height: 15px; color: " << rgb(text) << "; text-shadow: none; }\n"
        << "menu menuitem:hover, .menu menuitem:hover { background-color: " << rgb(hilight) << "; color: " << rgb(hilightText) << "; }\n"
        << "menu menuitem:hover *, .menu menuitem:hover * { color: " << rgb(hilightText) << "; }\n"
        << "menu menuitem:disabled, .menu menuitem:disabled { color: " << rgb(sys("graytext", Qt::gray)) << "; }\n"
        << "menu separator, .menu separator { background-color: " << rgb(shadow) << "; min-height: 1px; margin: 3px 1px; }\n"
        << "menu check, menu radio { min-width: 13px; min-height: 13px; margin-right: 4px; }\n\n";
}

// The Accurate style: what XP's own controls measure, over the rules above.
void Generator::accurate(QTextStream &css)
{
    const auto px = [this](int n) { return n * m_look.dpi / 96; };
    const QColor menu = sys("menu", Qt::white), text = sys("menutext", Qt::black);
    const QColor hilight = sys("menuhilight", sys("hilight", Qt::blue)), hilightText = sys("hilighttext", Qt::white);
    const QColor shadow = sys("buttonshadow", Qt::gray), light = sys("buttonhilight", Qt::white);
    css << "/* Accurate: Windows XP's metrics */\n"
        // Pop-up menus (user32's mncomput.c, flat menus): a COLOR_BTNSHADOW
        // line and 2 px inside it (MNXBORDER); items as high as the menu
        // font and CYEDGE + 2; the text after the check mark's column
        // (MNLEFTMARGIN + 13 + MNXSPACE), the check mark in that column.
        << "menu, .menu, .context-menu { padding: 2px; border: 1px solid " << rgb(shadow) << "; }\n"
        << "menu menuitem, .menu menuitem { min-height: " << px(13) << "px; padding: 2px " << px(17) << "px 2px " << px(17) << "px; }\n"
        << "menu menuitem check, menu menuitem radio, .menu menuitem check, .menu menuitem radio"
        << " { min-width: 13px; min-height: 13px; margin: 0 2px 0 -15px; }\n"
        << "menu menuitem arrow { min-width: 13px; min-height: 13px; margin-right: -13px; }\n"
        // Separators: half of SM_CYMENUSIZE, an etched line (BDR_SUNKENOUTER) in its middle.
        << "menu separator, .menu separator { min-height: 1px; margin: 3px 0 4px 0; background-color: " << rgb(shadow)
        << "; border-bottom: 1px solid " << rgb(light) << "; }\n"
        // XP has no pop-overs: the menus that GTK shows in one are XP's menus.
        << "popover, popover.background, popover.menu { background-color: " << rgb(menu) << "; color: " << rgb(text)
        << "; border: 1px solid " << rgb(shadow) << "; border-radius: 0; box-shadow: none; padding: 2px; }\n"
        << "popover modelbutton { min-height: " << px(13) << "px; padding: 2px " << px(17) << "px; border-radius: 0; color: " << rgb(text)
        << "; background-image: none; border: none; box-shadow: none; }\n"
        << "popover modelbutton:hover, popover modelbutton:selected { background-color: " << rgb(hilight) << "; color: " << rgb(hilightText) << "; }\n"
        << "popover modelbutton:disabled { color: " << rgb(sys("graytext", Qt::gray)) << "; }\n"
        << "popover separator { min-height: 1px; margin: 3px 0 4px 0; background-color: " << rgb(shadow) << "; }\n"
        << "popover modelbutton check, popover modelbutton radio { min-width: 13px; min-height: 13px; margin: 0 2px 0 -15px; }\n"
        // Lists: a report view's rows are as high as its text and 2 px
        // above and below (comctl32's list view), no space between them.
        << "treeview.view { -GtkTreeView-vertical-separator: 0; -GtkTreeView-horizontal-separator: 4; }\n"
        << "treeview.view header button { min-height: " << px(15) << "px; }\n"
        // Tooltips: COLOR_INFOBK in a black line, no rounding (comctl32's tooltips).
        << "tooltip { padding: 1px 2px; border-radius: 0; }\n"
        // Window frames are XP's: header bars are a plain band of the dialog's face.
        << "headerbar { background-color: " << rgb(sys("buttonface", QColor(236, 233, 216))) << "; border: none; min-height: 0; padding: 2px; }\n"
        // The menu bar: SM_CYMENU high, items twice the average character wider than their text.
        << "menubar > menuitem { min-height: " << px(15) << "px; padding: 2px " << px(6) << "px; }\n\n";
}

bool Generator::write()
{
    QDir().mkpath(m_dir + QStringLiteral("/gtk-3.0"));
    QString rules;
    QTextStream css(&rules);
    colours(css);
    if (m_r) {
        buttons(css);
        checks(css);
        fields(css);
        scrollbars(css);
        ranges(css);
        tabs(css);
        extras(css);
    }
    menus(css);
    if (m_look.accurate)
        accurate(css);
    css.flush();
    const QByteArray header = "/* Win4Wine XP: made by xpstyle-gtk from "
        + (m_r ? m_r->theme().path() : QStringLiteral("Windows Classic")).toUtf8()
        + " and the session's system colours. Do not edit: it is made again when the shell's look changes. */\n";
    auto put = [&](const QString &name, const QByteArray &text) {
        QSaveFile file(m_dir + QStringLiteral("/gtk-3.0/") + name);
        return file.open(QIODevice::WriteOnly) && file.write(text) == text.size() && file.commit();
    };
    // The theme: Adwaita under XP's parts. win4wine.css, XP's parts alone,
    // is what the session's GTK module (gtk-module/) lays over the
    // applications' own CSS, which a theme cannot override.
    const bool ok = put(QStringLiteral("win4wine.css"), header + rules.toUtf8())
        && put(QStringLiteral("gtk.css"), header + "@import url(\"resource:///org/gtk/libgtk/theme/Adwaita/gtk-contained.css\");\n\n"
                                               + rules.toUtf8())
        // The same look for applications that prefer a dark theme: XP has none.
        && put(QStringLiteral("gtk-dark.css"), "@import url(\"gtk.css\");\n");
    QSaveFile index(m_dir + QStringLiteral("/index.theme"));
    if (index.open(QIODevice::WriteOnly)) {
        index.write(QByteArray("[Desktop Entry]\nType=X-GNOME-Metatheme\nName=Win4Wine XP")
                    + (m_look.accurate ? " (Accurate)" : "") + "\nComment=The XP shell's visual style\n\n"
                    "[X-GNOME-Metatheme]\nGtkTheme=" + QDir(m_dir).dirName().toUtf8() + "\n");
        index.commit();
    }
    return ok;
}

// An id for a look: a new theme directory (a new name) whenever it changes,
// which is what makes GTK 3 load it again.
QString lookId(const Look &look)
{
    QByteArray key = look.stylePath.toUtf8() + '|' + look.colourName.toUtf8() + '|' + look.sizeName.toUtf8() + '|'
        + QByteArray::number(look.dpi) + '|' + QByteArray::number(look.themed);
    // The Standard style's first build keeps the id it had before the choice existed.
    if (look.accurate || look.themeBuild)
        key += '|' + QByteArray::number(look.accurate) + '|' + QByteArray::number(look.themeBuild);
    QStringList names = look.colours.keys();
    names.sort();
    for (const QString &name : std::as_const(names))
        key += name.toUtf8() + '=' + look.colours.value(name).name().toUtf8() + ';';
    QFileInfo style(look.stylePath);
    key += QByteArray::number(style.lastModified().toSecsSinceEpoch());
    key += "v8";   // bumped when the generator's output changes
    return QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex().left(10));
}

// The theme's directory name: Win4Wine-XP-<id>, Win4Wine-XP-Accurate-<id>
// (the GTK module knows the Accurate style by it).
QString themePrefix(bool accurate)
{
    return accurate ? QStringLiteral("Win4Wine-XP-Accurate-") : QStringLiteral("Win4Wine-XP-");
}

// XSETTINGS: change Net/ThemeName, Gtk/FontName and Net/IconThemeName in
// the settings the session's manager (bridge-wm) publishes, keeping its
// other settings; an empty icon theme takes Net/IconThemeName away (GTK then
// uses its own setting, the desktop's).
void publish(const QString &theme, const QString &font, const QString &icons)
{
    auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    if (!x11)
        return;
    xcb_connection_t *c = x11->connection();
    auto atom = [c](const char *name) {
        xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, strlen(name), name), nullptr);
        const xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
        free(r);
        return a;
    };
    const xcb_atom_t selection = atom("_XSETTINGS_S0"), settings = atom("_XSETTINGS_SETTINGS");
    xcb_get_selection_owner_reply_t *owner = xcb_get_selection_owner_reply(c, xcb_get_selection_owner(c, selection), nullptr);
    const xcb_window_t window = owner ? owner->owner : XCB_NONE;
    free(owner);
    if (window == XCB_NONE)
        return;
    xcb_get_property_reply_t *prop = xcb_get_property_reply(
        c, xcb_get_property(c, 0, window, settings, settings, 0, 1 << 20), nullptr);
    QByteArray data = prop ? QByteArray(static_cast<const char *>(xcb_get_property_value(prop)), xcb_get_property_value_length(prop))
                           : QByteArray();
    free(prop);
    struct Entry { QByteArray name, raw; };
    QList<Entry> entries;
    quint32 serial = 0;
    auto u16 = [&](int at) { return quint16(uchar(data[at]) | uchar(data[at + 1]) << 8); };
    auto u32 = [&](int at) { return quint32(uchar(data[at]) | uchar(data[at + 1]) << 8 | uchar(data[at + 2]) << 16 | uchar(data[at + 3]) << 24); };
    auto pad = [](int n) { return (n + 3) & ~3; };
    if (data.size() >= 12 && data[0] == 0) {   // little-endian, as bridge-wm writes it
        serial = u32(4);
        const quint32 count = u32(8);
        int at = 12;
        for (quint32 i = 0; i < count && at + 8 <= data.size(); ++i) {
            const int start = at;
            const int type = uchar(data[at]);
            const int length = u16(at + 2);
            const QByteArray name = data.mid(at + 4, length);
            at += 4 + pad(length) + 4;
            if (type == 0)
                at += 4;
            else if (type == 1)
                at += 4 + pad(int(u32(at)));
            else
                at += 8;
            if (at > data.size())
                break;
            entries.append({name, data.mid(start, at - start)});
        }
    }
    auto string = [&](const QByteArray &name, const QByteArray &value) {
        QByteArray e;
        e.append(char(1)).append(char(0)).append(char(name.size() & 0xff)).append(char(name.size() >> 8));
        e.append(name).append(QByteArray(pad(name.size()) - name.size(), 0));
        for (quint32 v : {serial + 1, quint32(value.size())})
            for (int b = 0; b < 4; ++b)
                e.append(char((v >> (8 * b)) & 0xff));
        e.append(value).append(QByteArray(pad(value.size()) - value.size(), 0));
        return e;
    };
    QList<QPair<QByteArray, QByteArray>> wanted = {{"Net/ThemeName", theme.toUtf8()}};
    if (!font.isEmpty())
        wanted.append({"Gtk/FontName", font.toUtf8()});
    if (!icons.isEmpty())
        wanted.append({"Net/IconThemeName", icons.toUtf8()});
    else
        entries.removeIf([](const Entry &e) { return e.name == "Net/IconThemeName"; });
    for (const auto &[name, value] : wanted) {
        bool found = false;
        for (Entry &e : entries)
            if (e.name == name) {
                e.raw = string(name, value);
                found = true;
            }
        if (!found)
            entries.append({name, string(name, value)});
    }
    QByteArray out(12, 0);
    for (int b = 0; b < 4; ++b) {
        out[4 + b] = char(((serial + 1) >> (8 * b)) & 0xff);
        out[8 + b] = char((quint32(entries.size()) >> (8 * b)) & 0xff);
    }
    for (const Entry &e : std::as_const(entries))
        out += e.raw;
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, window, settings, settings, 8, out.size(), out.constData());
    xcb_flush(c);
}

QString fontName(const Look &look)
{
    if (!look.haveFonts)
        return QString();
    return QStringLiteral("%1 %2").arg(look.messageFont.family()).arg(qRound(look.messageFont.pointSizeF()));
}

// Samples of the installed icon themes for Control Panel's Icons item
// (session/linuxlook.c), which cannot read SVG: a strip of 32 x 32 icons
// (folder, document, picture, computer, hard disk, Recycle Bin) per theme in
// ~/.cache/win4wine/icon-previews/<folder>.png, drawn again when the theme
// changes.
void iconPreviews()
{
    static const QList<QStringList> icons = {
        {QStringLiteral("folder"), QStringLiteral("inode-directory")},
        {QStringLiteral("text-x-generic"), QStringLiteral("x-office-document"), QStringLiteral("text-plain")},
        {QStringLiteral("image-x-generic"), QStringLiteral("image-png")},
        {QStringLiteral("computer"), QStringLiteral("user-desktop-computer"), QStringLiteral("computer-laptop")},
        {QStringLiteral("drive-harddisk"), QStringLiteral("drive-harddisk-system")},
        {QStringLiteral("user-trash"), QStringLiteral("trashcan_empty")},
    };
    const int size = 32;
    const QString cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/win4wine/icon-previews");
    QDir().mkpath(cache);
    // freedesktop's folders (the offscreen platform names none): ~/.icons, then the data folders'.
    QStringList roots{QDir::homePath() + QStringLiteral("/.icons")};
    for (const QString &data : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        roots << data + QStringLiteral("/icons");
    const QStringList searchPaths = QIcon::themeSearchPaths();
    QIcon::setThemeSearchPaths(roots);
    const QString desktop = QIcon::themeName();
    QSet<QString> done;
    for (const QString &root : std::as_const(roots)) {
        for (const QString &name : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QFileInfo index(root + QLatin1Char('/') + name + QStringLiteral("/index.theme"));
            if (done.contains(name) || !index.isFile() || name == QLatin1String("hicolor"))
                continue;
            done.insert(name);
            const QFileInfo out(cache + QLatin1Char('/') + name + QStringLiteral(".png"));
            if (out.exists() && out.lastModified() >= index.lastModified())
                continue;
            QIcon::setThemeName(name);
            QImage strip(size * icons.size(), size, QImage::Format_ARGB32_Premultiplied);
            strip.fill(Qt::transparent);
            QPainter painter(&strip);
            bool any = false;
            for (int i = 0; i < icons.size(); ++i)
                for (const QString &icon : icons[i])
                    if (QIcon::hasThemeIcon(icon)) {
                        QIcon::fromTheme(icon).paint(&painter, QRect(i * size, 0, size, size));
                        any = true;
                        break;
                    }
            painter.end();
            if (any)
                strip.save(out.filePath());
        }
    }
    QIcon::setThemeName(desktop);
    QIcon::setThemeSearchPaths(searchPaths);
}

} // namespace

int main(int argc, char **argv)
{
    // No window: offscreen unless there is an X display to publish to.
    const QStringList args = [&] { QStringList a; for (int i = 0; i < argc; ++i) a << QString::fromLocal8Bit(argv[i]); return a; }();
    if (!args.contains(QStringLiteral("--watch")) || qEnvironmentVariableIsEmpty("DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    else
        qputenv("QT_QPA_PLATFORM", "xcb");
    QGuiApplication app(argc, argv);

    if (qEnvironmentVariableIsSet("WIN4WINE_QT_DEBUG")) {
        const Look &look = Session::instance()->look();
        fprintf(stderr, "xpstyle-gtk: prefix %s, style %s (%s), %d colours\n", qPrintable(Session::instance()->prefix()),
                qPrintable(look.stylePath), look.themed ? "active" : "classic", int(look.colours.size()));
    }
    if (const int i = args.indexOf(QStringLiteral("--out")); i >= 0 && i + 1 < args.size())
        return Generator(Session::instance()->look(), args[i + 1]).write() ? 0 : 1;
    if (args.contains(QStringLiteral("--icon-previews"))) {
        iconPreviews();
        return 0;
    }
    if (!args.contains(QStringLiteral("--watch"))) {
        fprintf(stderr, "Usage: xpstyle-gtk --out DIR | --watch | --icon-previews\n");
        return 2;
    }
    const QString themes = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/themes");
    auto update = [&] {
        const Look look = Session::instance()->look();
        const QString prefix = themePrefix(look.accurate);
        const QString name = prefix + lookId(look);
        const QString dir = themes + QLatin1Char('/') + name;
        // A saved theme is used as it is: made only when it is missing.
        if (!QFileInfo::exists(dir + QStringLiteral("/gtk-3.0/gtk.css")) && !Generator(look, dir).write())
            return;
        // A stable name for applications given GTK_THEME (the Wayland session).
        const QString link = themes + QStringLiteral("/Win4Wine-XP");
        QFile::remove(link);
        QFile::link(name, link);
        publish(name, fontName(look), look.iconTheme);
        // Earlier looks' themes of this style go, a little later (applications
        // switching read them); the other style's saved theme stays.
        QTimer::singleShot(10000, [themes, name, accurate = look.accurate] {
            const QString accuratePrefix = themePrefix(true);
            for (const QString &old : QDir(themes).entryList({QStringLiteral("Win4Wine-XP-*")}, QDir::Dirs | QDir::NoDotAndDotDot))
                if (old != name && old.startsWith(accuratePrefix) == accurate)
                    QDir(themes + QLatin1Char('/') + old).removeRecursively();
        });
    };
    QObject::connect(Session::instance(), &Session::changed, &app, update);
    update();
    // After the theme: the samples are for Control Panel, which waits for no one.
    QTimer::singleShot(2000, &app, iconPreviews);
    return app.exec();
}
