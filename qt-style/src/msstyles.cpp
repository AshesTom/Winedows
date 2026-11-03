// SPDX-License-Identifier: LGPL-2.1-or-later
#include "msstyles.h"

#include <cmath>

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

// An RLE4 picture (comdlg32's font pictures) as the plain 8-bit one it stands for: each pair of
// nibbles a pixel each
static QByteArray expandRle4(const QByteArray &dib, quint32 headerSize, int width, int height, quint32 colours)
{
    colours = colours ? colours : 16;
    const qint64 start = headerSize + 4 * qint64(colours);
    const int stride = (width + 3) & ~3;
    QByteArray out = dib.left(start);
    qToLittleEndian<quint16>(8, out.data() + 14);
    qToLittleEndian<quint32>(0, out.data() + 16);
    qToLittleEndian<quint32>(0, out.data() + 20);
    qToLittleEndian<quint32>(colours, out.data() + 32);
    const qint64 pixelsAt = out.size();
    out.append(QByteArray(qint64(stride) * height, '\0'));
    auto *pixels = reinterpret_cast<uchar *>(out.data() + pixelsAt);
    const auto *in = reinterpret_cast<const uchar *>(dib.constData());
    const qint64 size = dib.size();
    const auto put = [&](int x, int y, int value) {
        if (x < width && y < height)
            pixels[y * stride + x] = uchar(value);
    };
    int x = 0, y = 0;
    for (qint64 i = start; i + 1 < size && y < height;) {
        const int count = in[i], value = in[i + 1];
        i += 2;
        if (count) {
            for (int n = 0; n < count; ++n, ++x)
                put(x, y, n % 2 ? value & 15 : value >> 4);
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
            // VALUE nibbles, padded to a word
            const int bytes = (value + 1) / 2;
            for (int n = 0; n < value && i + n / 2 < size; ++n, ++x)
                put(x, y, n % 2 ? in[i + n / 2] & 15 : in[i + n / 2] >> 4);
            i += bytes + (bytes & 1);
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
    if (r.u32(16) == 2 && r.u16(14) == 4 && qint32(r.u32(4)) > 0 && qint32(r.u32(8)) > 0 && qint32(r.u32(4)) <= 16384 && qint32(r.u32(8)) <= 16384 && r.u32(32) <= 16)
        return decodeDib(expandRle4(dib, headerSize, qint32(r.u32(4)), qint32(r.u32(8)), r.u32(32)), partial);
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

enum class Sizing { TrueSize, Stretch, Tile };

// uxtheme's default is Stretch.
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

// GDI's nearest-neighbour step, in win32k's nine-grid (EngNineGrid) and in StretchBlt's
// COLORONCOLOR: 32.32 fixed point rounded up, the first sample half a step in. OUT gets
// COUNT source positions from FROM on, over SOURCECOUNT pixels.
void stretchMap(int *out, int count, int from, int sourceCount)
{
    if (count <= 0 || sourceCount <= 0)
        return;
    if (count == sourceCount) {
        for (int i = 0; i < count; ++i)
            out[i] = from + i;
        return;
    }
    const quint64 step = ((quint64(sourceCount) << 32) - 1) / quint64(count) + 1;
    quint64 at = ((quint64(sourceCount) << 32) / quint64(count)) >> 1;
    for (int i = 0; i < count; ++i, at += step)
        out[i] = from + int(at >> 32);
}

// One axis of win32k's nine-grid: each destination pixel's source pixel, and where the
// middle is. The margins keep their size while the destination and the source both have
// a middle between them; otherwise the destination is shared between the margins in their
// proportion and there is no middle. Margins stretch, the middle stretches or tiles from its
// own start.
struct Axis
{
    QList<int> map;
    int middleFrom = 0, middleTo = 0;
};

Axis gridAxis(int destination, int source, int nearMargin, int farMargin, bool tile)
{
    Axis axis;
    axis.map.resize(qMax(0, destination));
    int near = nearMargin, far = farMargin;
    const bool middle = destination - near - far > 0 && source - near - far > 0;
    if (!middle) {
        near = nearMargin + farMargin ? int(qint64(nearMargin) * destination / (nearMargin + farMargin)) : 0;
        far = destination - near;
    }
    stretchMap(axis.map.data(), near, 0, nearMargin);
    axis.middleFrom = axis.middleTo = near;
    if (middle) {
        const int count = destination - near - far, sourceCount = source - nearMargin - farMargin;
        if (tile) {
            for (int i = 0; i < count; ++i)
                axis.map[near + i] = nearMargin + i % sourceCount;
        } else {
            stretchMap(axis.map.data() + near, count, nearMargin, sourceCount);
        }
        axis.middleTo = near + count;
    }
    stretchMap(axis.map.data() + destination - far, far, source - farMargin, farMargin);
    return axis;
}

// SOURCE (premultiplied) as a nine-grid of SIZE, as EngNineGrid builds it before blending it.
QImage nineGrid(const QImage &source, QSize size, QMargins m, bool tile, bool borderOnly)
{
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    if (size.isEmpty() || source.isNull())
        return out;
    // (Margins wider than the art together may overlap: win32k shares the destination out.)
    m.setLeft(qBound(0, m.left(), source.width()));
    m.setRight(qBound(0, m.right(), source.width()));
    m.setTop(qBound(0, m.top(), source.height()));
    m.setBottom(qBound(0, m.bottom(), source.height()));
    const Axis xs = gridAxis(size.width(), source.width(), m.left(), m.right(), tile);
    const Axis ys = gridAxis(size.height(), source.height(), m.top(), m.bottom(), tile);
    for (int y = 0; y < size.height(); ++y) {
        const auto *from = reinterpret_cast<const QRgb *>(source.constScanLine(ys.map[y]));
        auto *to = reinterpret_cast<QRgb *>(out.scanLine(y));
        const bool middleRow = y >= ys.middleFrom && y < ys.middleTo;
        for (int x = 0; x < size.width(); ++x) {
            if (borderOnly && middleRow && x >= xs.middleFrom && x < xs.middleTo)
                continue;
            to[x] = from[xs.map[x]];
        }
    }
    return out;
}

// SOURCE at SIZE, sampled as StretchBlt does in COLORONCOLOR
QImage stretched(const QImage &source, QSize size)
{
    return nineGrid(source, size, QMargins(), false, false);
}

// uxtheme's premultiplication of 32-bpp art at load: each channel times alpha over 255,
// rounded down
QImage premultiplied(QImage image)
{
    image = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const int a = qAlpha(row[x]);
            row[x] = qRgba(qRed(row[x]) * a / 255, qGreen(row[x]) * a / 255, qBlue(row[x]) * a / 255, a);
        }
    }
    image.reinterpretAsFormat(QImage::Format_ARGB32_Premultiplied);
    return image;
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
        // uxtheme's load: a 32-bpp picture has an alpha channel only if a pixel is neither
        // opaque nor clear; without one its alpha byte means nothing.
        d.partialAlpha = false;
        for (int y = 0; y < d.image.height() && !d.partialAlpha; ++y) {
            const auto *row = reinterpret_cast<const QRgb *>(d.image.constScanLine(y));
            for (int x = 0; x < d.image.width(); ++x)
                if (qAlpha(row[x]) != 0 && qAlpha(row[x]) != 255) {
                    d.partialAlpha = true;
                    break;
                }
        }
        if (!d.partialAlpha)
            for (int y = 0; y < d.image.height(); ++y) {
                auto *row = reinterpret_cast<QRgb *>(d.image.scanLine(y));
                for (int x = 0; x < d.image.width(); ++x)
                    row[x] |= 0xff000000u;
            }
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

namespace {

// Windows' MulDiv: A times B over C, rounded half away from zero; -1 for a zero C
int mulDiv(int a, int b, int c)
{
    if (c == 0)
        return -1;
    const qint64 product = qint64(a) * b;
    const qint64 half = qAbs(qint64(c)) / 2;
    const qint64 result = ((product < 0) != (c < 0) ? product - (c < 0 ? -half : half) : product + (c < 0 ? -half : half)) / c;
    return int(result);
}

bool isImageGlyph(const Renderer &r, const Part &p)
{
    return r.property(p, QStringLiteral("glyphtype")).value_or(QString()).trimmed()
               .compare(QLatin1String("imageglyph"), Qt::CaseInsensitive) == 0;
}

} // namespace

// The DIBs of a part as uxtheme's CImageFile keeps them: its ImageFile (GlyphImageFile for
// an image glyph), then, when an ImageSelectType is set, ImageFile1..7 up to the first
// missing; those belong to the glyph if it is an image glyph, else to the background, and
// the first of them replaces the default. The default comes first in the list.
QList<Renderer::Image> Renderer::images(const Part &p, bool glyph) const
{
    QList<Image> out;
    const bool imageGlyph = isImageGlyph(*this, p);
    if (glyph && !imageGlyph)
        return out;
    const auto add = [&](const QString &file, int index) {
        Image image;
        image.file = file;
        if (index > 0) {
            bool ok = false;
            const int dpi = property(p, QStringLiteral("mindpi%1").arg(index)).value_or(QString()).trimmed().toInt(&ok);
            if (ok)
                image.minDpi = qMax(1, dpi);
        }
        // MinSize (the background's) or MinSize1..7; else the frame's own size
        const QString minSize = index > 0 ? QStringLiteral("minsize%1").arg(index) : glyph ? QString() : QStringLiteral("minsize");
        if (minSize.isEmpty() || !parseSize(property(p, minSize).value_or(QString()), &image.minSize))
            image.minSize = frame(p, file, glyph).size();
        out.append(image);
    };
    add(property(p, glyph ? QStringLiteral("glyphimagefile") : QStringLiteral("imagefile")).value_or(QString()), 0);
    const QString select = property(p, QStringLiteral("imageselecttype")).value_or(QString()).trimmed().toLower();
    if (!select.isEmpty() && select != QLatin1String("none") && glyph == imageGlyph) {
        for (int i = 1; i <= 7; ++i) {
            const auto file = property(p, QStringLiteral("imagefile%1").arg(i));
            if (!file)
                break;
            add(*file, i);
        }
        if (out.size() > 1)
            out[0] = out[1];
    }
    if (out.size() == 1 && out.first().file.isEmpty())
        out.clear();
    return out;
}

// CImageFile's choice for a destination (its SelectCorrectImageFile): by size the last whose
// MinSize fits, by DPI the last whose MinDpi the screen reaches, else the default. A stretched
// or tiled background keeps its default when the glyph is an image glyph.
std::optional<Renderer::Image> Renderer::choose(const Part &p, bool glyph, QSize destination) const
{
    QList<Image> list = images(p, glyph);
    if (list.isEmpty())
        return std::nullopt;
    const Image fallback = list.takeFirst();
    if (!glyph && sizingOf(property(p, QStringLiteral("sizingtype"))) != Sizing::TrueSize && isImageGlyph(*this, p))
        return fallback;
    const QString select = property(p, QStringLiteral("imageselecttype")).value_or(QString()).trimmed().toLower();
    if (select == QLatin1String("dpi")) {
        for (int i = int(list.size()) - 1; i >= 0; --i)
            if (m_dpi >= list[i].minDpi)
                return list[i];
    } else if ((select == QLatin1String("size") || parseBool(property(p, QStringLiteral("sourcegrow")).value_or(QString())))
               && !destination.isEmpty()) {
        for (int i = int(list.size()) - 1; i >= 0; --i)
            if (destination.width() >= list[i].minSize.width() && destination.height() >= list[i].minSize.height())
                return list[i];
    }
    return fallback;
}

// FILE's frame for the part's state: ImageCount frames side by side unless ImageLayout is
// vertical (uxtheme's default is horizontal), each width (or height) over ImageCount; the
// colour key taken out of art without an alpha channel; premultiplied as uxtheme does at load.
QImage Renderer::frame(const Part &p, const QString &file, bool glyph) const
{
    bool partial = false;
    const QImage image = m_theme->bitmap(file, &partial);
    if (image.isNull())
        return {};
    const int count = qMax(1, integer(p, QStringLiteral("imagecount")).value_or(1));
    const bool vertical = property(p, QStringLiteral("imagelayout")).value_or(QString()).trimmed()
                              .compare(QLatin1String("vertical"), Qt::CaseInsensitive) == 0;
    const int w = vertical ? image.width() : image.width() / count;
    const int h = vertical ? image.height() / count : image.height();
    if (w <= 0 || h <= 0)
        return {};
    const int index = (p.stateId >= 1 && p.stateId <= count) ? p.stateId - 1 : 0;
    QImage crop = image.copy(vertical ? QRect(0, index * h, w, h) : QRect(index * w, 0, w, h));
    if (!partial && parseBool(property(p, glyph ? QStringLiteral("glyphtransparent") : QStringLiteral("transparent")).value_or(QString()))) {
        QColor key(255, 0, 255);
        parseColour(property(p, glyph ? QStringLiteral("glyphtransparentcolor") : QStringLiteral("transparentcolor")).value_or(QString()), &key);
        const QRgb keyRgb = key.rgb() & 0x00ffffffu;
        for (int y = 0; y < crop.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(crop.scanLine(y));
            for (int x = 0; x < crop.width(); ++x)
                if ((row[x] & 0x00ffffffu) == keyRgb)
                    row[x] = 0;
        }
    }
    return premultiplied(crop);
}

QImage Renderer::stateImage(const Part &p, bool glyph) const
{
    const auto image = choose(p, glyph, QSize(1 << 16, 1 << 16));
    return image ? frame(p, image->file, glyph) : QImage();
}

QSize Renderer::trueSize(const Part &p) const
{
    if (property(p, QStringLiteral("bgtype")).value_or(QString()).trimmed().compare(QLatin1String("borderfill"), Qt::CaseInsensitive) == 0) {
        const int border = integer(p, QStringLiteral("bordersize")).value_or(1);
        return QSize(2 * border + 1, 2 * border + 1);
    }
    return stateImage(p, false).size();
}

bool Renderer::has(const Part &p) const
{
    const QString bg = property(p, QStringLiteral("bgtype")).value_or(QStringLiteral("imagefile")).trimmed().toLower();
    if (bg == QLatin1String("borderfill"))
        return true;
    return bg != QLatin1String("none") && (!images(p, false).isEmpty() || !images(p, true).isEmpty());
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

// CImageFile::DrawBackground for one of the part's DIBs in ROOM: the size it is drawn at (a
// true-size image's own, or stretched as TrueSizeScalingType, TrueSizeStretchMark,
// IntegralSizing and UniformSizing say), its place in the room by HAlign and VAlign, the
// fill around it (BgFill), then the nine-grid that GdiDrawStream hands to win32k, after
// SourceShrink has scaled the art and its margins down.
void Renderer::drawImage(QPainter &painter, const Part &p, bool glyph, const QRect &room) const
{
    if (room.isEmpty())
        return;
    const auto image = choose(p, glyph, room.size());
    if (!image)
        return;
    const QImage source = frame(p, image->file, glyph);
    if (source.isNull())
        return;
    const auto flag = [&](const char *name) { return parseBool(property(p, QLatin1String(name)).value_or(QString())); };
    // An image glyph is always drawn at its true size.
    const Sizing sizing = glyph ? Sizing::TrueSize : sizingOf(property(p, QStringLiteral("sizingtype")));
    QSize drawn = room.size();
    bool stretch = false;
    if (sizing == Sizing::TrueSize) {
        drawn = source.size();
        const QString scaling = property(p, QStringLiteral("truesizescalingtype")).value_or(QString()).trimmed().toLower();
        if (scaling == QLatin1String("size") || scaling == QLatin1String("dpi")) {
            if (source.width() > room.width() || source.height() > room.height()) {
                // Larger than its room: stretched into it
                stretch = true;
                drawn = room.size();
            } else {
                const bool byDpi = scaling == QLatin1String("dpi");
                int w = byDpi ? mulDiv(source.width(), m_dpi, image->minDpi) : mulDiv(source.width(), room.width(), image->minSize.width());
                int h = byDpi ? mulDiv(source.height(), m_dpi, image->minDpi) : mulDiv(source.height(), room.height(), image->minSize.height());
                const int mark = integer(p, QStringLiteral("truesizestretchmark")).value_or(0);
                if (w > 0 && h > 0) {
                    w = qMin(w, room.width());
                    h = qMin(h, room.height());
                    if ((w - source.width()) * 100 / source.width() >= mark && (h - source.height()) * 100 / source.height() >= mark) {
                        stretch = true;
                        drawn = QSize(w, h);
                    }
                }
            }
        }
        if (stretch && flag("integralsizing"))
            drawn = QSize(int(double(drawn.width()) / source.width()) * source.width(),
                          int(double(drawn.height()) / source.height()) * source.height());
        if (flag("uniformsizing")) {
            const double rx = double(drawn.width()) / source.width(), ry = double(drawn.height()) / source.height();
            if (rx < ry)
                drawn.setHeight(int(source.height() * rx));
            else if (ry < rx)
                drawn.setWidth(int(source.width() * ry));
        }
        if (drawn.isEmpty())
            return;
    }
    // Smaller than the room: placed by HAlign and VAlign (centred unless they say otherwise)
    QRect to = room;
    bool smaller = false;
    if (drawn.width() < room.width()) {
        smaller = true;
        const QString h = property(p, QStringLiteral("halign")).value_or(QString()).trimmed().toLower();
        const int x = h == QLatin1String("left") ? room.x()
                      : h == QLatin1String("right") ? room.x() + room.width() - drawn.width()
                                                     : room.x() + (room.width() - drawn.width()) / 2;
        to.setLeft(x);
        to.setWidth(drawn.width());
    }
    if (drawn.height() < room.height()) {
        smaller = true;
        const QString v = property(p, QStringLiteral("valign")).value_or(QString()).trimmed().toLower();
        const int y = v == QLatin1String("top") ? room.y()
                      : v == QLatin1String("bottom") ? room.y() + room.height() - drawn.height()
                                                      : room.y() + (room.height() - drawn.height()) / 2;
        to.setTop(y);
        to.setHeight(drawn.height());
    }
    const bool borderOnly = !glyph && flag("borderonly");
    if (smaller && !borderOnly && flag("bgfill")) {
        QColor fill(255, 255, 255);
        parseColour(property(p, QStringLiteral("fillcolor")).value_or(QString()), &fill);
        painter.fillRect(room, fill);
    }
    if (sizing == Sizing::TrueSize && !stretch) {
        // The art at its size from the corner of its place, cut to it
        painter.drawImage(to.topLeft(), source, QRect(QPoint(0, 0), to.size().boundedTo(source.size())));
        return;
    }
    QMargins margins;
    QImage art = source;
    if (sizing != Sizing::TrueSize) {
        parseMargins(property(p, QStringLiteral("sizingmargins")).value_or(QString()), &margins);
        // SourceShrink: below its MinSize the art and its margins shrink by the room over NormalSize.
        if (flag("sourceshrink") && (drawn.width() < image->minSize.width() || drawn.height() < image->minSize.height())) {
            QSize normal(60, 30);
            parseSize(property(p, QStringLiteral("normalsize")).value_or(QString()), &normal);
            float sx = 1, sy = 1;
            if (drawn.width() < image->minSize.width() && normal.width() > 0)
                sx = float(drawn.width()) / normal.width();
            if (drawn.height() < image->minSize.height() && normal.height() > 0)
                sy = float(drawn.height()) / normal.height();
            float scale = qMin(sx, sy);
            if (scale > 1)
                scale = float(int(scale));
            if (scale != 1) {
                const auto scaled = [scale](int n) { return int(std::floor(n * scale + 0.5f)); };
                margins = QMargins(scaled(margins.left()), scaled(margins.top()), scaled(margins.right()), scaled(margins.bottom()));
                const QSize size(int(source.width() * scale), int(source.height() * scale));
                if (size.isEmpty())
                    return;
                art = stretched(source, size);
            }
        }
    }
    painter.drawImage(to.topLeft(), nineGrid(art, to.size(), margins, sizing == Sizing::Tile, borderOnly));
}

QImage Renderer::render(const Part &p, QSize size) const
{
    QImage canvas(size, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    const QRect dest = canvas.rect();
    const QString bg = property(p, QStringLiteral("bgtype")).value_or(QStringLiteral("imagefile")).trimmed().toLower();
    if (bg == QLatin1String("borderfill")) {
        QColor border(0, 0, 0), fill(255, 255, 255);
        parseColour(property(p, QStringLiteral("bordercolor")).value_or(QString()), &border);
        parseColour(property(p, QStringLiteral("fillcolor")).value_or(QString()), &fill);
        const int b = qMax(0, integer(p, QStringLiteral("bordersize")).value_or(1));
        if (!parseBool(property(p, QStringLiteral("borderonly")).value_or(QString())))
            painter.fillRect(dest, fill);
        if (b > 0 && dest.width() > 2 * b && dest.height() > 2 * b) {
            painter.fillRect(QRect(0, 0, dest.width(), b), border);
            painter.fillRect(QRect(0, dest.height() - b, dest.width(), b), border);
            painter.fillRect(QRect(0, b, b, dest.height() - 2 * b), border);
            painter.fillRect(QRect(dest.width() - b, b, b, dest.height() - 2 * b), border);
        }
    } else if (bg != QLatin1String("none") && !parseBool(property(p, QStringLiteral("glyphonly")).value_or(QString()))) {
        drawImage(painter, p, false, dest);
    }
    // An image glyph in the content rect (a combo box's arrow), at its true size
    if (bg != QLatin1String("borderfill"))
        drawImage(painter, p, true, contentRect(p, dest));
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
        // (MinSize only chooses images and starts SourceShrink: uxtheme draws below it too.)
        image = render(p, rect.size());
        QMutexLocker locker(&m_lock);
        if (m_cache.size() > 2000)
            m_cache.clear();
        m_cache.insert(key, image);
    }
    painter->drawImage(rect.topLeft(), image);
    return true;
}

} // namespace w4w
