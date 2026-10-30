// SPDX-License-Identifier: LGPL-2.1-or-later
#include "msstyles.h"

#include <QColor>
#include <QFile>
#include <QPainter>
#include <QRegularExpression>
#include <QtEndian>

namespace w4w {
namespace {

// Bounds-checked little-endian reads of a byte array.
struct Reader
{
    const QByteArray &data;
    bool ok = true;
    quint32 u32(qint64 at)
    {
        if (at < 0 || at + 4 > data.size()) {
            ok = false;
            return 0;
        }
        return qFromLittleEndian<quint32>(data.constData() + at);
    }
    quint16 u16(qint64 at)
    {
        if (at < 0 || at + 2 > data.size()) {
            ok = false;
            return 0;
        }
        return qFromLittleEndian<quint16>(data.constData() + at);
    }
};

QString decodeUtf16(const QByteArray &bytes)
{
    QString text = QString::fromUtf16(reinterpret_cast<const char16_t *>(bytes.constData()), bytes.size() / 2);
    if (text.startsWith(QChar(0xfeff)))
        text.remove(0, 1);
    return text;
}

// A REG_MULTI_SZ-like list of UTF-16 strings (COLORNAMES, SIZENAMES, FILERESNAMES).
QStringList multiString(const QByteArray &bytes)
{
    QStringList out;
    for (const QString &s : decodeUtf16(bytes).split(QChar(0), Qt::SkipEmptyParts))
        out.append(s);
    return out;
}

QString sectionKey(const QString &sub, const QString &cls, const QString &part, const QString &state)
{
    QString key;
    if (!sub.isEmpty())
        key += sub + QLatin1String("::");
    key += cls;
    if (!part.isEmpty())
        key += QLatin1Char('.') + part;
    if (!state.isEmpty())
        key += QLatin1Char('(') + state + QLatin1Char(')');
    return key;
}

QList<int> numbers(const QString &value)
{
    static const QRegularExpression separators(QStringLiteral("[,\\s]+"));
    QList<int> out;
    for (const QString &part : value.split(separators, Qt::SkipEmptyParts)) {
        bool ok = false;
        const int n = part.toInt(&ok);
        if (!ok)
            return {};
        out.append(n);
    }
    return out;
}

} // namespace

// 32bpp art keeps its alpha; PARTIAL is when Wine's uxtheme (the session's
// shell) blends it by its alpha instead of keying out its transparent colour.
// An RLE8 picture as the plain one it stands for
static QByteArray expandRle8(const QByteArray &dib, quint32 headerSize, int width, int height, quint32 colours)
{
    const qint64 start = headerSize + 4 * qint64(colours ? colours : 256);
    const int stride = (width + 3) & ~3;
    QByteArray out = dib.left(start);
    // Compression 0, and the size of the image left for the reader to work out
    qToLittleEndian<quint32>(0, out.data() + 16);
    qToLittleEndian<quint32>(0, out.data() + 20);
    const qint64 pixelsAt = out.size();
    out.append(QByteArray(qint64(stride) * height, '\0'));
    auto *pixels = reinterpret_cast<uchar *>(out.data() + pixelsAt);
    const auto *in = reinterpret_cast<const uchar *>(dib.constData());
    const qint64 size = dib.size();
    int x = 0, y = 0;
    for (qint64 i = start; i + 1 < size && y < height;) {
        const int count = in[i], value = in[i + 1];
        i += 2;
        if (count) {
            for (int n = 0; n < count && x < width; ++n)
                pixels[y * stride + x++] = uchar(value);
        } else if (value == 0) {
            x = 0;
            ++y;
        } else if (value == 1) {
            break;
        } else if (value == 2) {
            if (i + 1 >= size)
                break;
            x += in[i];
            y += in[i + 1];
            i += 2;
        } else {
            for (int n = 0; n < value && i + n < size; ++n)
                if (x < width && y < height)
                    pixels[y * stride + x++] = in[i + n];
            i += value + (value & 1);
        }
    }
    return out;
}

QImage decodeDib(const QByteArray &dib, bool *partial)
{
    Reader r{dib};
    *partial = false;
    const quint32 headerSize = r.u32(0);
    if (!r.ok || headerSize < 40 || headerSize > quint32(dib.size()))
        return {};
    if (r.u32(16) == 1 && r.u16(14) == 8 && qint32(r.u32(4)) > 0 && qint32(r.u32(8)) > 0 && qint32(r.u32(4)) <= 16384 && qint32(r.u32(8)) <= 16384 && r.u32(32) <= 256)
        return decodeDib(expandRle8(dib, headerSize, qint32(r.u32(4)), qint32(r.u32(8)), r.u32(32)), partial);
    const qint32 width = qint32(r.u32(4));
    const qint32 rawHeight = qint32(r.u32(8));
    const int bits = r.u16(14);
    const quint32 compression = r.u32(16);
    quint32 colours = r.u32(32);
    if (!r.ok || width <= 0 || rawHeight == 0 || width > 16384 || qAbs(rawHeight) > 16384)
        return {};
    const bool bottomUp = rawHeight > 0;
    const int height = qAbs(rawHeight);
    // BI_RGB (0) and BI_BITFIELDS (3) only; XP's styles use nothing else.
    if (compression != 0 && compression != 3)
        return {};
    quint32 masks[3] = {0x00ff0000u, 0x0000ff00u, 0x000000ffu};
    qint64 offset = headerSize;
    if (bits == 16 && compression == 0) {
        masks[0] = 0x7c00;
        masks[1] = 0x03e0;
        masks[2] = 0x001f;
    }
    if (compression == 3) {
        for (int i = 0; i < 3; ++i)
            masks[i] = r.u32(headerSize == 40 ? offset + 4 * i : 40 + 4 * i);
        if (headerSize == 40)
            offset += 12;
    }
    QList<QRgb> palette;
    if (bits <= 8) {
        if (colours == 0 || colours > (1u << bits))
            colours = 1u << bits;
        for (quint32 i = 0; i < colours; ++i)
            palette.append(0xff000000u | (r.u32(offset + 4 * i) & 0x00ffffffu));
        offset += 4 * qint64(colours);
    }
    if (bits != 1 && bits != 4 && bits != 8 && bits != 16 && bits != 24 && bits != 32)
        return {};
    const qint64 stride = ((qint64(width) * bits + 31) / 32) * 4;
    if (!r.ok || offset + stride * height > dib.size())
        return {};
    auto channel = [](quint32 value, quint32 mask) {
        if (!mask)
            return 0;
        while (!(mask & 1)) {
            mask >>= 1;
            value >>= 1;
        }
        const quint32 max = mask;
        return int(((value & mask) * 255 + max / 2) / max);
    };

    QImage image(width, height, QImage::Format_ARGB32);
    bool anyPartial = false;
    const auto *base = reinterpret_cast<const uchar *>(dib.constData()) + offset;
    for (int y = 0; y < height; ++y) {
        const uchar *row = base + stride * (bottomUp ? height - 1 - y : y);
        auto *out = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            QRgb c = 0xff000000u;
            switch (bits) {
            case 1:
                c = palette.value((row[x / 8] >> (7 - x % 8)) & 1, c);
                break;
            case 4:
                c = palette.value((row[x / 2] >> (x % 2 ? 0 : 4)) & 15, c);
                break;
            case 8:
                c = palette.value(row[x], c);
                break;
            case 16: {
                const quint32 v = row[2 * x] | (row[2 * x + 1] << 8);
                c = qRgb(channel(v, masks[0]), channel(v, masks[1]), channel(v, masks[2]));
                break;
            }
            case 24:
                c = qRgb(row[3 * x + 2], row[3 * x + 1], row[3 * x]);
                break;
            case 32: {
                const uchar a = row[4 * x + 3];
                c = qRgba(row[4 * x + 2], row[4 * x + 1], row[4 * x], a);
                anyPartial |= a != 255;
                break;
            }
            }
            out[x] = c;
        }
    }
    *partial = anyPartial;
    return image;
}

namespace {

enum class Sizing { Stretch, Tile, TrueSize };

Sizing sizingOf(const std::optional<QString> &value)
{
    if (value) {
        const QString v = value->trimmed();
        if (v.compare(QLatin1String("tile"), Qt::CaseInsensitive) == 0)
            return Sizing::Tile;
        if (v.compare(QLatin1String("truesize"), Qt::CaseInsensitive) == 0)
            return Sizing::TrueSize;
    }
    return Sizing::Stretch;
}

// Copy FROM of SRC into TO of the canvas, stretched (nearest neighbour, as
// uxtheme samples) or tiled.
void blit(QPainter &p, const QRect &to, const QImage &src, const QRect &from, Sizing sizing)
{
    if (to.isEmpty() || from.isEmpty())
        return;
    if (sizing == Sizing::Tile && to.size() != from.size()) {
        const QImage piece = src.copy(from);
        p.drawTiledPixmap(to, QPixmap::fromImage(piece));
        return;
    }
    p.drawImage(to, src, from);
}

// One axis of a nine-grid: margins shrink in proportion when they do not fit.
QPair<int, int> split(int destination, int nearMargin, int farMargin)
{
    if (nearMargin + farMargin <= destination || nearMargin + farMargin == 0)
        return {nearMargin, farMargin};
    const int near = int(qint64(destination) * nearMargin / (nearMargin + farMargin));
    return {near, destination - near};
}

} // namespace

bool parseColour(const QString &value, QColor *out)
{
    const QList<int> n = numbers(value);
    if (n.size() < 3)
        return false;
    *out = QColor(qBound(0, n[0], 255), qBound(0, n[1], 255), qBound(0, n[2], 255));
    return true;
}

bool parseMargins(const QString &value, QMargins *out)
{
    const QList<int> n = numbers(value);
    if (n.size() < 4)
        return false;
    // MARGINS order: left, right, top, bottom.
    *out = QMargins(n[0], n[2], n[1], n[3]);
    return true;
}

bool parseSize(const QString &value, QSize *out)
{
    const QList<int> n = numbers(value);
    if (n.size() < 2)
        return false;
    *out = QSize(n[0], n[1]);
    return true;
}

bool parseBool(const QString &value)
{
    const QString v = value.trimmed();
    return v.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0 || v == QLatin1String("1");
}

std::optional<PeResources> PeResources::parse(const QByteArray &file)
{
    Reader r{file};
    if (file.size() < 0x40 || file.left(2) != "MZ")
        return std::nullopt;
    const qint64 pe = r.u32(0x3c);
    if (!r.ok || file.mid(pe, 4) != QByteArray("PE\0\0", 4))
        return std::nullopt;
    const int sections = r.u16(pe + 6);
    const int optionalSize = r.u16(pe + 20);
    const int magic = r.u16(pe + 24);
    const qint64 directories = pe + 24 + (magic == 0x20b ? 112 : 96);
    const quint32 resourceRva = r.u32(directories + 2 * 8);
    if (!r.ok || resourceRva == 0)
        return std::nullopt;
    struct Section { quint32 address, size, raw; };
    QList<Section> table;
    for (int i = 0; i < sections; ++i) {
        const qint64 at = pe + 24 + optionalSize + 40 * qint64(i);
        table.append({r.u32(at + 12), qMax(r.u32(at + 8), r.u32(at + 16)), r.u32(at + 20)});
    }
    if (!r.ok)
        return std::nullopt;
    auto offset = [&](quint32 rva) -> qint64 {
        for (const Section &s : table)
            if (rva >= s.address && rva < s.address + s.size)
                return qint64(rva) - s.address + s.raw;
        return -1;
    };
    const qint64 base = offset(resourceRva);
    if (base < 0)
        return std::nullopt;
    auto nameOf = [&](quint32 entry) -> QString {
        if (!(entry & 0x80000000u))
            return QString::number(entry);
        const qint64 at = base + (entry & 0x7fffffffu);
        const int length = r.u16(at);
        if (!r.ok || at + 2 + 2 * qint64(length) > file.size())
            return QString();
        return decodeUtf16(file.mid(at + 2, 2 * length)).toUpper();
    };
    auto entries = [&](qint64 directory) {
        QList<QPair<quint32, quint32>> out;
        const int count = r.u16(directory + 12) + r.u16(directory + 14);
        for (int i = 0; i < count && i < 65536 && r.ok; ++i)
            out.append({r.u32(directory + 16 + 8 * i), r.u32(directory + 20 + 8 * i)});
        return out;
    };
    PeResources out;
    for (const auto &[typeEntry, typeTarget] : entries(base)) {
        if (!(typeTarget & 0x80000000u))
            continue;
        const QString type = nameOf(typeEntry);
        for (const auto &[nameEntry, nameTarget] : entries(base + (typeTarget & 0x7fffffffu))) {
            qint64 leaf = base + (nameTarget & 0x7fffffffu);
            if (nameTarget & 0x80000000u) {
                // The first language.
                const auto languages = entries(leaf);
                if (languages.isEmpty() || (languages.first().second & 0x80000000u))
                    continue;
                leaf = base + languages.first().second;
            }
            const quint32 rva = r.u32(leaf), size = r.u32(leaf + 4);
            const qint64 at = offset(rva);
            if (!r.ok || at < 0 || at + qint64(size) > file.size())
                continue;
            out.m_types[type].insert(nameOf(nameEntry), file.mid(at, size));
        }
    }
    if (!r.ok && out.m_types.isEmpty())
        return std::nullopt;
    return out;
}

QByteArray PeResources::find(const QString &type, const QString &name) const
{
    return m_types.value(type.toUpper()).value(name.toUpper());
}

QStringList PeResources::names(const QString &type) const
{
    return m_types.value(type.toUpper()).keys();
}

Ini Ini::parse(const QString &text)
{
    Ini ini;
    QHash<QString, QString> *current = nullptr;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        const int comment = line.indexOf(QLatin1Char(';'));
        if (comment >= 0)
            line.truncate(comment);
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            QString header = line.mid(1, line.size() - 2).toLower();
            header.remove(QLatin1Char(' '));
            current = &ini.m_sections[header];
            continue;
        }
        const int equals = line.indexOf(QLatin1Char('='));
        if (!current || equals <= 0)
            continue;
        current->insert(line.left(equals).trimmed().toLower(), line.mid(equals + 1).trimmed());
    }
    return ini;
}

std::optional<QString> Ini::value(const QString &section, const QString &property) const
{
    const auto s = m_sections.constFind(section.toLower());
    if (s == m_sections.cend())
        return std::nullopt;
    const auto v = s->constFind(property.toLower());
    if (v == s->cend())
        return std::nullopt;
    return *v;
}

std::optional<QString> Ini::lookup(const QString &sub, const QString &cls, const QString &part,
                                   const QString &state, const QString &property) const
{
    const QString prop = property.toLower();
    const QString c = cls.toLower(), s = sub.toLower();
    QString p = part.toLower(), st = state.toLower();
    auto find = [&](const QString &key) -> std::optional<QString> {
        return value(key, prop);
    };
    if (!s.isEmpty()) {
        for (const QString &key : {sectionKey(s, c, p, st), sectionKey(s, c, p, {}), sectionKey(s, c, {}, {})})
            if (auto v = find(key))
                return v;
    }
    // (part, state), (part), (class): in the class, then in Globals.
    // [Class(State)] only applies to the class's own part 0.
    const QList<QPair<QString, QString>> steps = p.isEmpty()
        ? QList<QPair<QString, QString>>{{{}, st}, {{}, {}}}
        : QList<QPair<QString, QString>>{{p, st}, {p, {}}, {{}, {}}};
    for (const auto &[stepPart, stepState] : steps) {
        for (const QString &owner : {c, QStringLiteral("globals")})
            if (auto v = find(sectionKey({}, owner, stepPart, stepState)))
                return v;
    }
    return std::nullopt;
}

std::shared_ptr<Theme> Theme::load(const QString &path, const QString &colour, const QString &size, QString *error)
{
    auto fail = [&](const QString &why) -> std::shared_ptr<Theme> {
        if (error)
            *error = why;
        return nullptr;
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("cannot open %1").arg(path));
    // XP's own styles are a few MB; anything far larger is not one.
    if (file.size() > 64 * 1024 * 1024)
        return fail(QStringLiteral("%1 is too large").arg(path));
    auto resources = PeResources::parse(file.readAll());
    if (!resources)
        return fail(QStringLiteral("%1 is not a PE file with resources").arg(path));

    // The variant's INI: FILERESNAMES lists one per (colour, size), colour-major.
    const QStringList colours = multiString(resources->find(QStringLiteral("COLORNAMES"), QStringLiteral("1")));
    const QStringList sizes = multiString(resources->find(QStringLiteral("SIZENAMES"), QStringLiteral("1")));
    const QStringList files = multiString(resources->find(QStringLiteral("FILERESNAMES"), QStringLiteral("1")));
    QString iniName;
    int c = -1, s = -1;
    for (int i = 0; i < colours.size(); ++i)
        if (colours[i].compare(colour, Qt::CaseInsensitive) == 0)
            c = i;
    for (int i = 0; i < sizes.size(); ++i)
        if (sizes[i].compare(size, Qt::CaseInsensitive) == 0)
            s = i;
    if (!sizes.isEmpty() && c >= 0) {
        const int index = c * int(sizes.size()) + qMax(0, s);
        if (index < files.size())
            iniName = files[index];
    }
    if (iniName.isEmpty() && !files.isEmpty())
        iniName = files.first();
    if (iniName.isEmpty()) {
        for (const QString &name : resources->names(QStringLiteral("TEXTFILE")))
            if (name != QLatin1String("THEMES_INI")) {
                iniName = name;
                break;
            }
    }
    const QByteArray ini = resources->find(QStringLiteral("TEXTFILE"), iniName);
    if (qEnvironmentVariableIsSet("WIN4WINE_QT_DEBUG"))
        qInfo("win4wine-xp style: %s, %s/%s: %s", qPrintable(path), qPrintable(colour), qPrintable(size), qPrintable(iniName));
    if (ini.isEmpty())
        return fail(QStringLiteral("%1 has no layout INI %2").arg(path, iniName));

    std::shared_ptr<Theme> theme(new Theme);
    theme->m_path = path;
    theme->m_resources = std::move(*resources);
    QString text = decodeUtf16(ini);
    text.remove(QLatin1Char('\r'));
    theme->m_ini = Ini::parse(text);
    return theme;
}

std::optional<QString> Theme::property(const Part &p, const QString &name) const
{
    return m_ini.lookup(p.sub, p.cls, p.part, p.state, name);
}

std::optional<QString> Theme::sysMetric(const QString &name) const
{
    return m_ini.value(QStringLiteral("sysmetrics"), name);
}

QImage Theme::bitmap(const QString &imageFile, bool *partialAlpha) const
{
    QString name = imageFile.trimmed().toUpper();
    for (QChar &ch : name)
        if (!ch.isLetterOrNumber())
            ch = QLatin1Char('_');
    QMutexLocker locker(&m_lock);
    auto it = m_bitmaps.constFind(name);
    if (it == m_bitmaps.cend()) {
        Decoded d;
        QByteArray data = m_resources.find(QStringLiteral("2"), name);
        if (data.isEmpty())
            data = m_resources.find(QStringLiteral("IMAGE"), name);
        if (!data.isEmpty())
            d.image = decodeDib(data, &d.partialAlpha);
        it = m_bitmaps.insert(name, d);
    }
    if (partialAlpha)
        *partialAlpha = it->partialAlpha;
    return it->image;
}

Renderer::Renderer(std::shared_ptr<Theme> theme, int dpi)
    : m_theme(std::move(theme))
    , m_dpi(dpi)
{
}

QString Renderer::chooseImage(const Part &p, bool glyph, QSize destination) const
{
    // A plain ImageFile (GlyphImageFile) wins. ImageFile1..7, by DPI or by
    // size, are the background without an ImageFile, and the glyph with one
    // (a scroll arrow's ImageFile is its button, ImageFile1.. its arrows).
    if (auto file = property(p, glyph ? QStringLiteral("glyphimagefile") : QStringLiteral("imagefile")))
        return *file;
    if (glyph != property(p, QStringLiteral("imagefile")).has_value())
        return QString();
    const QString select = property(p, QStringLiteral("imageselecttype")).value_or(QString()).trimmed().toLower();
    // One frame of an image (its ImageCount frames, laid out vertically unless said otherwise).
    auto sizeOf = [&](const QString &file) {
        const QImage image = m_theme->bitmap(file);
        const int count = qMax(1, integer(p, QStringLiteral("imagecount")).value_or(1));
        const bool horizontal = property(p, QStringLiteral("imagelayout")).value_or(QString()).trimmed()
                                    .compare(QLatin1String("horizontal"), Qt::CaseInsensitive) == 0;
        return horizontal ? QSize(image.width() / count, image.height()) : QSize(image.width(), image.height() / count);
    };
    if (select == QLatin1String("dpi")) {
        for (int i = 7; i >= 1; --i) {
            const int minimum = property(p, QStringLiteral("mindpi%1").arg(i)).value_or(QStringLiteral("0")).toInt();
            if (minimum != 0 && m_dpi >= minimum)
                if (auto file = property(p, QStringLiteral("imagefile%1").arg(i)))
                    return *file;
        }
    } else if (select == QLatin1String("size")) {
        for (int i = 7; i >= 1; --i) {
            const auto file = property(p, QStringLiteral("imagefile%1").arg(i));
            if (!file)
                continue;
            QSize need;
            if (!parseSize(property(p, QStringLiteral("minsize%1").arg(i)).value_or(QString()), &need))
                need = sizeOf(*file);
            if (need.width() <= destination.width() && need.height() <= destination.height())
                return *file;
        }
    } else {
        return QString();
    }
    return property(p, QStringLiteral("imagefile1")).value_or(QString());
}

// The part's frame for its state, premultiplied, colour key applied.
QImage Renderer::frame(const Part &p, bool glyph, QSize destination) const
{
    QString file;
    if (destination.isEmpty()) {
        // (for chooseImage's size selection: the first numbered image, without recursing)
        file = property(p, QStringLiteral("imagefile1")).value_or(QString());
    } else {
        file = chooseImage(p, glyph, destination);
    }
    if (file.isEmpty())
        return {};
    bool partial = false;
    const QImage image = m_theme->bitmap(file, &partial);
    if (image.isNull())
        return {};
    const int count = qMax(1, property(p, glyph ? QStringLiteral("glyphimagecount") : QStringLiteral("imagecount"))
                                  .value_or(property(p, QStringLiteral("imagecount")).value_or(QStringLiteral("1")))
                                  .toInt());
    const auto layout = property(p, QStringLiteral("imagelayout"));
    // Frames run vertically unless ImageLayout says otherwise, or unless the
    // count only divides the width.
    const bool horizontal = layout ? layout->trimmed().compare(QLatin1String("horizontal"), Qt::CaseInsensitive) == 0
                                   : (count > 1 && image.height() % count != 0 && image.width() % count == 0);
    int frames = count;
    // A glyph strip may hold fewer frames than the part has states.
    if (glyph && !horizontal && image.height() % frames != 0)
        frames = 1;
    const int w = horizontal ? image.width() / frames : image.width();
    const int h = horizontal ? image.height() : image.height() / frames;
    if (w <= 0 || h <= 0)
        return {};
    const int index = (p.stateId >= 1 && p.stateId <= frames) ? p.stateId - 1 : 0;
    QImage crop = image.copy(horizontal ? QRect(index * w, 0, w, h) : QRect(0, index * h, w, h));
    const bool keyed = parseBool(property(p, glyph ? QStringLiteral("glyphtransparent") : QStringLiteral("transparent"))
                                     .value_or(QString()));
    QColor key(255, 0, 255);
    parseColour(property(p, glyph ? QStringLiteral("glyphtransparentcolor") : QStringLiteral("transparentcolor"))
                    .value_or(QString()),
                &key);
    const QRgb keyRgb = key.rgb() & 0x00ffffffu;
    for (int y = 0; y < crop.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(crop.scanLine(y));
        for (int x = 0; x < crop.width(); ++x)
            if (!partial && keyed && (row[x] & 0x00ffffffu) == keyRgb)
                row[x] = 0;
    }
    return crop.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QImage Renderer::stateImage(const Part &p, bool glyph) const
{
    return frame(p, glyph, QSize(1 << 16, 1 << 16));
}

QSize Renderer::trueSize(const Part &p) const
{
    if (property(p, QStringLiteral("bgtype")).value_or(QString()).trimmed().compare(QLatin1String("borderfill"), Qt::CaseInsensitive) == 0) {
        const int border = integer(p, QStringLiteral("bordersize")).value_or(1);
        return QSize(2 * border + 1, 2 * border + 1);
    }
    return frame(p, false, QSize(1 << 16, 1 << 16)).size();
}

bool Renderer::has(const Part &p) const
{
    const QString bg = property(p, QStringLiteral("bgtype")).value_or(QStringLiteral("imagefile")).trimmed().toLower();
    if (bg == QLatin1String("borderfill"))
        return true;
    return bg != QLatin1String("none") && !chooseImage(p, false, QSize(1 << 16, 1 << 16)).isEmpty();
}

QMargins Renderer::contentMargins(const Part &p) const
{
    QMargins m;
    if (parseMargins(property(p, QStringLiteral("contentmargins")).value_or(QString()), &m))
        return m;
    if (property(p, QStringLiteral("bgtype")).value_or(QString()).trimmed().compare(QLatin1String("borderfill"), Qt::CaseInsensitive) == 0) {
        const int b = integer(p, QStringLiteral("bordersize")).value_or(1);
        return QMargins(b, b, b, b);
    }
    parseMargins(property(p, QStringLiteral("sizingmargins")).value_or(QString()), &m);
    return m;
}

QRect Renderer::contentRect(const Part &p, const QRect &bounds) const
{
    return bounds.marginsRemoved(contentMargins(p));
}

std::optional<QColor> Renderer::colour(const Part &p, const QString &name) const
{
    QColor c;
    if (auto v = property(p, name); v && parseColour(*v, &c))
        return c;
    return std::nullopt;
}

std::optional<int> Renderer::integer(const Part &p, const QString &name) const
{
    if (auto v = property(p, name)) {
        bool ok = false;
        const int n = v->trimmed().toInt(&ok);
        if (ok)
            return n;
    }
    return std::nullopt;
}

QImage Renderer::render(const Part &p, QSize size) const
{
    QImage canvas(size, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QSize minimum;
    if (parseSize(property(p, QStringLiteral("minsize")).value_or(QString()), &minimum)
        && (size.width() < minimum.width() || size.height() < minimum.height()))
        return {};
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    const QRect dest = canvas.rect();
    const QString bg = property(p, QStringLiteral("bgtype")).value_or(QStringLiteral("imagefile")).trimmed().toLower();
    const bool borderOnly = parseBool(property(p, QStringLiteral("borderonly")).value_or(QString()));
    auto alignedIn = [&](QSize drawn, const QRect &room) {
        const QString h = property(p, QStringLiteral("halign")).value_or(QString()).trimmed().toLower();
        const QString v = property(p, QStringLiteral("valign")).value_or(QString()).trimmed().toLower();
        const int spareX = room.width() - drawn.width(), spareY = room.height() - drawn.height();
        const int x = h == QLatin1String("left") ? 0 : h == QLatin1String("right") ? spareX : spareX / 2;
        const int y = v == QLatin1String("top") ? 0 : v == QLatin1String("bottom") ? spareY : spareY / 2;
        return QRect(room.topLeft() + QPoint(qMax(0, x), qMax(0, y)), drawn);
    };
    // TrueSize art keeps its size; parts that scale (UniformSizing) shrink
    // into a smaller room, keeping their proportions.
    auto trueSizeDrawn = [&](QSize source, QSize room) {
        if (room.width() >= source.width() && room.height() >= source.height())
            return source;
        if (!parseBool(property(p, QStringLiteral("uniformsizing")).value_or(QString())))
            return source;
        return source.scaled(room, Qt::KeepAspectRatio);
    };

    if (bg == QLatin1String("borderfill")) {
        QColor border(0, 0, 0), fill(255, 255, 255);
        parseColour(property(p, QStringLiteral("bordercolor")).value_or(QString()), &border);
        parseColour(property(p, QStringLiteral("fillcolor")).value_or(QString()), &fill);
        const int b = qMax(0, integer(p, QStringLiteral("bordersize")).value_or(1));
        if (!borderOnly)
            painter.fillRect(dest, fill);
        if (b > 0 && dest.width() > 2 * b && dest.height() > 2 * b) {
            painter.fillRect(QRect(0, 0, dest.width(), b), border);
            painter.fillRect(QRect(0, dest.height() - b, dest.width(), b), border);
            painter.fillRect(QRect(0, b, b, dest.height() - 2 * b), border);
            painter.fillRect(QRect(dest.width() - b, b, b, dest.height() - 2 * b), border);
        }
    } else if (bg != QLatin1String("none")) {
        const QImage img = frame(p, false, size);
        if (!img.isNull()) {
            const Sizing sizing = sizingOf(property(p, QStringLiteral("sizingtype")));
            if (sizing == Sizing::TrueSize) {
                const QSize drawn = trueSizeDrawn(img.size(), size);
                blit(painter, alignedIn(drawn, dest), img, img.rect(), Sizing::Stretch);
            } else {
                QMargins m;
                parseMargins(property(p, QStringLiteral("sizingmargins")).value_or(QString()), &m);
                m.setLeft(qBound(0, m.left(), img.width()));
                m.setRight(qBound(0, m.right(), img.width() - m.left()));
                m.setTop(qBound(0, m.top(), img.height()));
                m.setBottom(qBound(0, m.bottom(), img.height() - m.top()));
                const auto hx = split(dest.width(), m.left(), m.right());
                const auto vy = split(dest.height(), m.top(), m.bottom());
                const int sx[] = {0, m.left(), img.width() - m.right(), img.width()};
                const int sy[] = {0, m.top(), img.height() - m.bottom(), img.height()};
                const int dx[] = {0, hx.first, dest.width() - hx.second, dest.width()};
                const int dy[] = {0, vy.first, dest.height() - vy.second, dest.height()};
                for (int row = 0; row < 3; ++row)
                    for (int col = 0; col < 3; ++col) {
                        if (borderOnly && row == 1 && col == 1)
                            continue;
                        const QRect from(sx[col], sy[row], sx[col + 1] - sx[col], sy[row + 1] - sy[row]);
                        const QRect to(dx[col], dy[row], dx[col + 1] - dx[col], dy[row + 1] - dy[row]);
                        // Only the stretchable middle tiles.
                        blit(painter, to, img, from, (row == 1 || col == 1) && sizing == Sizing::Tile ? Sizing::Tile : Sizing::Stretch);
                    }
            }
        }
    }
    // An image glyph in the content rect (a combo box's arrow).
    if (property(p, QStringLiteral("glyphtype")).value_or(QString()).trimmed().compare(QLatin1String("imageglyph"), Qt::CaseInsensitive) == 0) {
        const QRect content = contentRect(p, dest);
        const QImage glyph = frame(p, true, content.size());
        if (!glyph.isNull()) {
            const QSize drawn = trueSizeDrawn(glyph.size(), content.size());
            blit(painter, alignedIn(drawn, content), glyph, glyph.rect(), Sizing::Stretch);
        }
    }
    return canvas;
}

bool Renderer::draw(QPainter *painter, const QRect &rect, const Part &p) const
{
    if (rect.isEmpty() || !has(p))
        return false;
    const QString key = QStringLiteral("%1|%2|%3|%4|%5|%6x%7")
                            .arg(p.sub, p.cls, p.part, p.state)
                            .arg(p.stateId)
                            .arg(rect.width())
                            .arg(rect.height());
    QImage image;
    {
        QMutexLocker locker(&m_lock);
        image = m_cache.value(key);
    }
    if (image.isNull()) {
        image = render(p, rect.size());
        if (image.isNull())
            return true;   // below MinSize: nothing, as uxtheme draws nothing
        QMutexLocker locker(&m_lock);
        if (m_cache.size() > 2000)
            m_cache.clear();
        m_cache.insert(key, image);
    }
    painter->drawImage(rect.topLeft(), image);
    return true;
}

} // namespace w4w
