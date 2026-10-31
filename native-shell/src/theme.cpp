// SPDX-License-Identifier: LGPL-2.1-or-later
#include "theme.h"

#include <QDirIterator>

#include "ipcfile.h"
#include "session.h"
#include "xpfiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPainter>
#include <QSaveFile>

namespace {

// Windows Classic's scheme, as XP's Display Properties has it
const QHash<QString, QColor> &classicColours()
{
    static const QHash<QString, QColor> colours = {
        {QStringLiteral("background"), QColor(58, 110, 165)}, {QStringLiteral("activecaption"), QColor(10, 36, 106)},
        {QStringLiteral("gradientactivecaption"), QColor(166, 202, 240)}, {QStringLiteral("inactivecaption"), QColor(128, 128, 128)},
        {QStringLiteral("gradientinactivecaption"), QColor(192, 192, 192)}, {QStringLiteral("captiontext"), Qt::white},
        {QStringLiteral("inactivecaptiontext"), QColor(212, 208, 200)}, {QStringLiteral("btnface"), QColor(212, 208, 200)},
        {QStringLiteral("highlight"), QColor(10, 36, 106)}, {QStringLiteral("highlighttext"), Qt::white},
        {QStringLiteral("btnshadow"), QColor(128, 128, 128)}, {QStringLiteral("dkshadow3d"), QColor(64, 64, 64)},
        {QStringLiteral("light3d"), QColor(212, 208, 200)}, {QStringLiteral("btnhighlight"), Qt::white},
        {QStringLiteral("window"), Qt::white}, {QStringLiteral("menu"), QColor(212, 208, 200)},
        {QStringLiteral("graytext"), QColor(128, 128, 128)},
    };
    return colours;
}

QColor metric(const std::shared_ptr<w4w::Theme> &theme, const char *name)
{
    QColor out = classicColours().value(QLatin1String(name));
    if (theme)
        w4w::parseColour(theme->sysMetric(QLatin1String(name)).value_or(QString()), &out);
    return out;
}

} // namespace

ShellTheme *ShellTheme::instance()
{
    static ShellTheme *theme = new ShellTheme;
    return theme;
}

ShellTheme::ShellTheme()
{
    reload();
    connect(w4w::Session::instance(), &w4w::Session::changed, this, [this] {
        reload();
        Q_EMIT changed();
    });
}

void ShellTheme::drawShadowedText(QPainter &painter, const QRect &rect, int flags, const QString &text, const QColor &colour, const QColor &shadow, QPoint offset)
{
    constexpr int padding = 5;
    QImage mask(rect.size() + QSize(2 * padding, 2 * padding), QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    {
        QPainter textPainter(&mask);
        textPainter.setFont(painter.font());
        textPainter.setPen(colour);
        textPainter.drawText(QRect(QPoint(padding, padding), rect.size()), flags, text);
    }
    // Pad the text mask before averaging columns x+1..x+4; truncate the
    // horizontal coverage before the vertical pass.
    QImage horizontal(mask.size(), QImage::Format_Grayscale8);
    horizontal.fill(0);
    for (int y = 0; y < mask.height(); ++y) {
        uchar *line = horizontal.scanLine(y);
        // Leave the final five columns unfiltered (italic overhang can
        // reach this padding when the label is right-aligned).
        for (int x = 0; x < mask.width(); ++x)
            line[x] = (mask.pixel(x, y) & 0x00ffffff) ? 255 : 0;
        for (int x = 0; x < mask.width() - 5; ++x) {
            unsigned sum = 0;
            for (int dx = 1; dx <= 4; ++dx)
                if (mask.pixel(x + dx, y) & 0x00ffffff)
                    sum += 255;
            line[x] = sum >> 2;
        }
    }
    QImage soft(mask.size(), QImage::Format_ARGB32_Premultiplied);
    soft.fill(Qt::transparent);
    {
        // Average this row with the four rows above it. Divide by four and
        // saturate to keep the continuous shadow's full coverage.
        for (int y = 5; y < mask.height(); ++y) {
            for (int x = 0; x < mask.width(); ++x) {
                unsigned alpha = 0;
                for (int dy = 0; dy < 5; ++dy)
                    alpha += horizontal.constScanLine(y - dy)[x];
                alpha = qMin(255u, alpha >> 2);
                if (alpha) {
                    const auto tint = [alpha](int channel) { return ((channel * alpha + 128) >> 8) + 1; };
                    soft.setPixel(x, y, qRgba(tint(shadow.red()), tint(shadow.green()), tint(shadow.blue()), alpha));
                }
            }
        }
    }
    painter.drawImage(rect.topLeft() + offset - QPoint(4, 8), soft);
    painter.setPen(colour);
    painter.drawText(rect, flags, text);
}

QString ShellTheme::shellStylePath(const LookChoice &look)
{
    // Without a visual style XP's shell32 takes system32's shellstyle.dll, the same file as Luna's blue
    // (its task pane stays blue under Windows Classic).
    if (look.style.isEmpty()) {
        const QString system = xpfiles::find(QStringLiteral("shellstyle.dll"));
        return system.isEmpty() ? xpfiles::find(QStringLiteral("blue_ss.dll")) : system;
    }
    // Beside a style that is not Luna: Shell\\NormalColor\\shellstyle.dll
    if (look.style != lunaPath()) {
        QDirIterator it(QFileInfo(look.style).absolutePath() + QStringLiteral("/Shell"), {QStringLiteral("shellstyle.dll")}, QDir::Files, QDirIterator::Subdirectories);
        QString any;
        while (it.hasNext()) {
            const QString path = it.next();
            if (QFileInfo(path).dir().dirName().compare(look.colour, Qt::CaseInsensitive) == 0)
                return path;
            any = path;
        }
        if (!any.isEmpty())
            return any;
    }
    return xpfiles::find(look.colour == QLatin1String("Metallic") ? QStringLiteral("metal_ss.dll")
                         : look.colour == QLatin1String("HomeStead") ? QStringLiteral("home_ss.dll") : QStringLiteral("blue_ss.dll"));
}

const QList<ClassicScheme> &ShellTheme::classicSchemes()
{
    static const QList<ClassicScheme> schemes = [] {
        QList<ClassicScheme> out;
        QFile file(xpfiles::find(QStringLiteral("schemes.conf")));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return out;
        for (const QString &raw : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
            const QString line = raw.trimmed();
            if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
                const QString title = line.mid(1, line.size() - 2);
                out.append({title.section(QStringLiteral(" | "), 0, 0), title.section(QStringLiteral(" | "), 1), {}});
            } else if (const qsizetype equals = line.indexOf(QLatin1Char('=')); equals > 0 && !out.isEmpty()) {
                const QStringList rgb = line.mid(equals + 1).split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (rgb.size() == 3)
                    out.last().colours.append({line.left(equals), QColor(rgb[0].toInt(), rgb[1].toInt(), rgb[2].toInt())});
            }
        }
        return out;
    }();
    return schemes;
}

const ClassicScheme *ShellTheme::classicScheme(const LookChoice &choice)
{
    const QList<ClassicScheme> &schemes = classicSchemes();
    const auto find = [&](const QString &name, const QString &size) -> const ClassicScheme * {
        for (const ClassicScheme &scheme : schemes)
            if (scheme.name == name && (size.isEmpty() || scheme.size == size))
                return &scheme;
        return nullptr;
    };
    if (const ClassicScheme *scheme = find(choice.colour, choice.size))
        return scheme;
    if (const ClassicScheme *scheme = find(choice.colour, QString()))
        return scheme;
    return schemes.isEmpty() ? nullptr : find(QStringLiteral("Windows Standard"), QString());
}

QString ShellTheme::lunaPath()
{
    return xpfiles::find(QStringLiteral("luna.msstyles"));
}

void ShellTheme::ensureDefaultLook()
{
    if (!w4w::findPrefix().isEmpty() || QFileInfo::exists(w4w::nativeLookPath()))
        return;
    LookChoice choice;
    choice.style = lunaPath();
    choice.wallpaper = xpfiles::find(QStringLiteral("bliss.jpg"));
    write(choice);
    // XP's icons for this session's programs too (session/icon_theme.py), unless the user chose others.
    if (!QFileInfo::exists(w4w::linuxLookPath())) {
        QDir().mkpath(QFileInfo(w4w::linuxLookPath()).absolutePath());
        QSaveFile look(w4w::linuxLookPath());
        if (look.open(QIODevice::WriteOnly)) {
            look.write("icon_theme=win4wine-xp-icons\n");
            look.commit();
        }
    }
}

void ShellTheme::write(const LookChoice &choice)
{
    const QString path = w4w::nativeLookPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    const QString text = QStringLiteral("style=%1\ncolour=%2\nsize=%3\nwallpaper=%4\nwallpaper_style=%5\ntile_wallpaper=%6\nbackground=%7 %8 %9\n")
                             .arg(choice.style, choice.colour, choice.size, choice.wallpaper)
                             .arg(choice.wallpaperStyle)
                             .arg(int(choice.tile))
                             .arg(choice.background.red())
                             .arg(choice.background.green())
                             .arg(choice.background.blue());
    file.write(text.toUtf8());
    // Windows Classic: its scheme's colours beside its name, for every program that reads this file
    if (choice.style.isEmpty())
        if (const ClassicScheme *scheme = classicScheme(choice))
            for (const auto &[name, colour] : scheme->colours)
                file.write(QStringLiteral("classic.%1=%2 %3 %4\n").arg(name).arg(colour.red()).arg(colour.green()).arg(colour.blue()).toUtf8());
    file.commit();
}

LookChoice ShellTheme::choice() const
{
    const w4w::Look &look = w4w::Session::instance()->look();
    LookChoice out;
    out.style = look.themed ? QDir::cleanPath(look.stylePath) : QString();
    if (!look.colourName.isEmpty())
        out.colour = look.colourName;
    if (!look.sizeName.isEmpty())
        out.size = look.sizeName;
    out.wallpaper = look.wallpaper.isEmpty() ? QString() : QDir::cleanPath(look.wallpaper);
    out.wallpaperStyle = look.wallpaperStyle;
    out.tile = look.tileWallpaper;
    out.background = look.colours.value(QStringLiteral("background"), system(QStringLiteral("background"), out.background));
    return out;
}

FrameStyle ShellTheme::frameStyleFor(const LookChoice &choice, int dpi)
{
    FrameStyle style;
    std::shared_ptr<w4w::Theme> theme;
    if (!choice.style.isEmpty())
        theme = w4w::Theme::load(choice.style, choice.colour, choice.size);
    if (theme)
        style.renderer = std::make_shared<w4w::Renderer>(theme, dpi);
    // Windows Classic: the chosen scheme's colours
    QHash<QString, QColor> scheme;
    if (!theme)
        if (const ClassicScheme *chosen = classicScheme(choice))
            for (const auto &[name, colour] : chosen->colours)
                scheme.insert(name, colour);
    const auto colour = [&](const char *name) { return scheme.value(QLatin1String(name), metric(theme, name)); };
    style.activeCaption = colour("activecaption");
    style.activeGradient = colour("gradientactivecaption");
    style.inactiveCaption = colour("inactivecaption");
    style.inactiveGradient = colour("gradientinactivecaption");
    style.activeText = colour("captiontext");
    style.inactiveText = colour("inactivecaptiontext");
    style.face = colour("btnface");
    style.highlight = colour("highlight");
    style.shadow = colour("btnshadow");
    style.darkShadow = colour("dkshadow3d");
    style.light = colour("btnhighlight");
    if (theme) {
        style.captionFont = xpfiles::themeFont(theme->sysMetric(QStringLiteral("captionfont")).value_or(QStringLiteral("Trebuchet MS, 10, bold")), dpi);
        // The art's rows over the caption: 30 with the normal size's 25.
        const int bar = theme->sysMetric(QStringLiteral("captionbarheight")).value_or(QStringLiteral("25")).toInt();
        style.top = (bar + 5) * dpi / 96;
    } else {
        style.captionFont = xpfiles::themeFont(QStringLiteral("Tahoma, 8, bold"), dpi);
        style.top = 23 * dpi / 96;
    }
    style.border = 4 * dpi / 96;
    return style;
}

void ShellTheme::reload()
{
    const w4w::Look &look = w4w::Session::instance()->look();
    m_dpi = look.dpi;
    LookChoice wanted;
    wanted.style = look.themed ? look.stylePath : QString();
    if (!look.colourName.isEmpty())
        wanted.colour = look.colourName;
    if (!look.sizeName.isEmpty())
        wanted.size = look.sizeName;
    m_frame = frameStyleFor(wanted, m_dpi);
    m_system.clear();
    for (const char *name : {"background", "window", "btnface", "highlight", "highlighttext", "menu", "graytext", "btnshadow", "dkshadow3d", "light3d", "btnhighlight",
                             "gradientactivecaption"}) {
        QColor colour = classicColours().value(QLatin1String(name));
        if (m_frame.renderer)
            w4w::parseColour(m_frame.renderer->theme().sysMetric(QLatin1String(name)).value_or(QString()), &colour);
        m_system.insert(QLatin1String(name), colour);
    }
    for (auto it = look.colours.cbegin(); it != look.colours.cend(); ++it)
        m_system.insert(it.key(), it.value());
    // The session's colours by their registry names, under the names this shell asks for
    const std::pair<const char *, const char *> names[] = {
        {"buttonface", "btnface"}, {"hilight", "highlight"}, {"hilighttext", "highlighttext"}, {"buttonshadow", "btnshadow"},
        {"buttondkshadow", "dkshadow3d"}, {"buttonlight", "light3d"}, {"buttonhilight", "btnhighlight"},
        {"activetitle", "activecaption"}, {"gradientactivetitle", "gradientactivecaption"}, {"inactivetitle", "inactivecaption"},
        {"gradientinactivetitle", "gradientinactivecaption"}, {"titletext", "captiontext"}, {"inactivetitletext", "inactivecaptiontext"},
        {"buttontext", "btntext"}, {"infowindow", "infobk"},
    };
    for (const auto &[registry, metric] : names)
        if (look.colours.contains(QLatin1String(registry)))
            m_system.insert(QLatin1String(metric), look.colours.value(QLatin1String(registry)));
    if (!m_frame.renderer) {
        // Windows Classic's frames in the scheme's colours
        m_frame.activeCaption = system(QStringLiteral("activecaption"), m_frame.activeCaption);
        m_frame.activeGradient = system(QStringLiteral("gradientactivecaption"), m_frame.activeGradient);
        m_frame.inactiveCaption = system(QStringLiteral("inactivecaption"), m_frame.inactiveCaption);
        m_frame.inactiveGradient = system(QStringLiteral("gradientinactivecaption"), m_frame.inactiveGradient);
        m_frame.activeText = system(QStringLiteral("captiontext"), m_frame.activeText);
        m_frame.inactiveText = system(QStringLiteral("inactivecaptiontext"), m_frame.inactiveText);
        m_frame.face = system(QStringLiteral("btnface"), m_frame.face);
        m_frame.highlight = system(QStringLiteral("highlight"), m_frame.highlight);
        m_frame.shadow = system(QStringLiteral("btnshadow"), m_frame.shadow);
        m_frame.darkShadow = system(QStringLiteral("dkshadow3d"), m_frame.darkShadow);
        m_frame.light = system(QStringLiteral("btnhighlight"), m_frame.light);
    }
}

QColor ShellTheme::system(const QString &name, const QColor &fallback) const
{
    return m_system.value(name.toLower(), fallback);
}

QFont ShellTheme::font(const QString &spec) const
{
    return xpfiles::themeFont(spec, m_dpi);
}

QFont ShellTheme::systemFont(const QString &role, bool bold) const
{
    QString spec = m_frame.renderer ? m_frame.renderer->theme().sysMetric(role).value_or(QStringLiteral("Tahoma, 8"))
                                   : QStringLiteral("Tahoma, 8");
    if (bold)
        spec += QStringLiteral(", bold");
    return font(spec);
}


QImage ShellTheme::shellIcon(int id, int size) const
{
    // shell32.dll is 8 MB: its directory stays in memory, its icons in a cache.
    static const auto shell32 = xpfiles::resources(xpfiles::find(QStringLiteral("shell32.dll")));
    static QHash<quint64, QImage> cache;
    const quint64 key = quint64(id) << 16 | quint64(size);
    auto it = cache.constFind(key);
    if (it != cache.cend())
        return *it;
    QImage image;
    if (shell32) {
        image = xpfiles::icon(*shell32, id, size);
        if (!image.isNull() && image.width() != size)
            image = image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    cache.insert(key, image);
    return image;
}

QImage ShellTheme::defaultIcon(int size) const
{
    return shellIcon(3, size);
}

void ShellTheme::raised(QPainter &painter, const QRect &rect, bool pressed) const
{
    const QColor light = system(QStringLiteral("btnhighlight"), Qt::white), face = system(QStringLiteral("light3d"), QColor(212, 208, 200));
    const QColor shadow = system(QStringLiteral("btnshadow"), Qt::gray), dark = system(QStringLiteral("dkshadow3d"), Qt::black);
    const QRect r = rect.adjusted(0, 0, -1, -1);
    painter.setPen(pressed ? dark : light);
    painter.drawLine(r.topLeft(), r.topRight());
    painter.drawLine(r.topLeft(), r.bottomLeft());
    painter.setPen(pressed ? light : dark);
    painter.drawLine(r.bottomLeft(), r.bottomRight());
    painter.drawLine(r.topRight(), r.bottomRight());
    const QRect inner = r.adjusted(1, 1, -1, -1);
    painter.setPen(pressed ? shadow : face);
    painter.drawLine(inner.topLeft(), inner.topRight());
    painter.drawLine(inner.topLeft(), inner.bottomLeft());
    painter.setPen(pressed ? face : shadow);
    painter.drawLine(inner.bottomLeft(), inner.bottomRight());
    painter.drawLine(inner.topRight(), inner.bottomRight());
}

void ShellTheme::sunken(QPainter &painter, const QRect &rect) const
{
    const QRect r = rect.adjusted(0, 0, -1, -1);
    painter.setPen(system(QStringLiteral("btnshadow"), Qt::gray));
    painter.drawLine(r.topLeft(), r.topRight());
    painter.drawLine(r.topLeft(), r.bottomLeft());
    painter.setPen(system(QStringLiteral("btnhighlight"), Qt::white));
    painter.drawLine(r.bottomLeft(), r.bottomRight());
    painter.drawLine(r.topRight(), r.bottomRight());
}
