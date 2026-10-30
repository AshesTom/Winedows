// SPDX-License-Identifier: LGPL-2.1-or-later
// A Windows XP visual style (.msstyles) read from the user's own file, and
// uxtheme's drawing rules over it, for the Qt style (xpstyle.cpp).
//
// A .msstyles is a resource-only PE file: TEXTFILE resources hold the
// themes.ini and one layout INI per (colour, size) variant; BITMAP resources
// hold the art, named after the INI's ImageFile paths ("Blue\Button.bmp" is
// BLUE_BUTTON_BMP). Every read is bounds-checked: this runs inside every Qt
// application of the session, and a malformed file must only mean "no theme".
#pragma once

#include <QHash>
#include <QImage>
#include <QMargins>
#include <QMutex>
#include <QRect>
#include <QString>
#include <memory>
#include <optional>

class QPainter;

namespace w4w {

// The PE file's resources: (type, name) -> data, first language of each.
// Types and names are upper-cased strings ("TEXTFILE", "2" for RT_BITMAP).
class PeResources
{
public:
    static std::optional<PeResources> parse(const QByteArray &file);
    QByteArray find(const QString &type, const QString &name) const;
    QStringList names(const QString &type) const;

private:
    QHash<QString, QHash<QString, QByteArray>> m_types;
};

// The layout INI: sections [Sub::Class.Part(State)], all lower-cased.
class Ini
{
public:
    static Ini parse(const QString &text);
    // uxtheme's lookup: a Sub::Class's (part, state), (part), (class) first,
    // then (part, state), (part), (class) of the class, each before Globals.
    std::optional<QString> lookup(const QString &sub, const QString &cls, const QString &part,
                                  const QString &state, const QString &property) const;
    std::optional<QString> value(const QString &section, const QString &property) const;

private:
    QHash<QString, QHash<QString, QString>> m_sections;   // "sub::cls.part(state)" -> key -> value
};

// What to draw: class, part and state by name (as the INI spells them,
// lower-case) and the state's number in tmschema.h, which picks the frame.
struct Part
{
    QString cls;
    QString part;      // empty for the class itself
    QString state;     // empty for none
    int stateId = 0;   // 1-based frame; 0 draws the first
    QString sub;       // SetWindowTheme subclass, rarely needed
};

class Theme
{
public:
    // The style at PATH in its colour and size variant (ThemeManager's
    // ColorName and SizeName); null if it cannot be read.
    static std::shared_ptr<Theme> load(const QString &path, const QString &colour, const QString &size,
                                       QString *error = nullptr);

    QString path() const { return m_path; }
    std::optional<QString> property(const Part &p, const QString &name) const;
    std::optional<QString> sysMetric(const QString &name) const;
    // The decoded art of an ImageFile property, as stored (not keyed or premultiplied).
    QImage bitmap(const QString &imageFile, bool *partialAlpha = nullptr) const;

private:
    Theme() = default;
    QString m_path;
    PeResources m_resources;
    Ini m_ini;
    mutable QMutex m_lock;
    struct Decoded { QImage image; bool partialAlpha = false; };
    mutable QHash<QString, Decoded> m_bitmaps;
};

// DrawThemeBackground and friends over a Theme, drawn with nearest-neighbour
// sampling as uxtheme does, into premultiplied images that are cached.
class Renderer
{
public:
    explicit Renderer(std::shared_ptr<Theme> theme, int dpi = 96);
    const Theme &theme() const { return *m_theme; }
    int dpi() const { return m_dpi; }

    // Whether the style draws this part at all (an image or a BorderFill).
    bool has(const Part &p) const;
    // Draw the part's background (and its image glyph) into RECT; false if
    // the style has nothing for it, so the caller can fall back.
    bool draw(QPainter *painter, const QRect &rect, const Part &p) const;
    // The frame the part would draw for its state, at its own size.
    QImage stateImage(const Part &p, bool glyph = false) const;
    // GetThemePartSize(TS_TRUE): the frame's size, or 0x0.
    QSize trueSize(const Part &p) const;
    QMargins contentMargins(const Part &p) const;
    QRect contentRect(const Part &p, const QRect &bounds) const;
    std::optional<QColor> colour(const Part &p, const QString &name) const;
    std::optional<int> integer(const Part &p, const QString &name) const;
    std::optional<QString> property(const Part &p, const QString &name) const { return m_theme->property(p, name); }

private:
    QImage frame(const Part &p, bool glyph, QSize destination) const;
    QString chooseImage(const Part &p, bool glyph, QSize destination) const;
    QImage render(const Part &p, QSize size) const;
    std::shared_ptr<Theme> m_theme;
    int m_dpi;
    mutable QMutex m_lock;
    mutable QHash<QString, QImage> m_cache;
};

// A BITMAP resource (a DIB without its file header) as an ARGB32 image; PARTIAL
// tells whether any pixel is not opaque. Null if it cannot be read.
QImage decodeDib(const QByteArray &dib, bool *partial);

bool parseColour(const QString &value, QColor *out);
bool parseMargins(const QString &value, QMargins *out);   // "left, right, top, bottom"
bool parseSize(const QString &value, QSize *out);
bool parseBool(const QString &value);

} // namespace w4w
