// SPDX-License-Identifier: LGPL-2.1-or-later
#include "cards.h"

#include "xpfiles.h"

#include <QHash>
#include <QPainter>
#include <QtEndian>

#include <optional>

namespace cards {
namespace {

enum { IDB_GHOST = 53, IDB_X = 67, IDB_O = 68 };

std::optional<w4w::PeResources> g_pe;
QHash<int, QImage> g_pictures;
QSize g_size;

// Most of cards.dll's bitmaps are OS/2's (a BITMAPCOREHEADER and three-byte colours)
QByteArray modernDib(const QByteArray &dib)
{
    if (dib.size() < 12 || qFromLittleEndian<quint32>(dib.constData()) != 12)
        return dib;
    const auto *data = reinterpret_cast<const uchar *>(dib.constData());
    const int width = qFromLittleEndian<quint16>(data + 4), height = qFromLittleEndian<quint16>(data + 6);
    const int bits = qFromLittleEndian<quint16>(data + 10);
    const int colours = bits <= 8 ? 1 << bits : 0;
    if (dib.size() < 12 + colours * 3)
        return dib;
    QByteArray out(40 + colours * 4, '\0');
    auto *header = reinterpret_cast<uchar *>(out.data());
    qToLittleEndian<quint32>(40, header);
    qToLittleEndian<qint32>(width, header + 4);
    qToLittleEndian<qint32>(height, header + 8);
    qToLittleEndian<quint16>(1, header + 12);
    qToLittleEndian<quint16>(quint16(bits), header + 14);
    for (int i = 0; i < colours; ++i) {
        for (int c = 0; c < 3; ++c)
            out[40 + i * 4 + c] = dib[12 + i * 3 + c];
    }
    return out + dib.mid(12 + colours * 3);
}

QImage bitmap(int id)
{
    if (!g_pe)
        return {};
    bool partial = false;
    return w4w::decodeDib(modernDib(g_pe->find(QStringLiteral("2"), QString::number(id))), &partial)
        .convertToFormat(QImage::Format_ARGB32);
}

// What cdtDraw keeps of the screen under a card: three pixels in each corner
QImage rounded(QImage image)
{
    if (image.isNull())
        return image;
    const int right = image.width() - 1, bottom = image.height() - 1;
    for (const QPoint &point : {QPoint(0, 0), QPoint(1, 0), QPoint(0, 1), QPoint(right, 0), QPoint(right - 1, 0), QPoint(right, 1),
                                QPoint(right, bottom), QPoint(right, bottom - 1), QPoint(right - 1, bottom),
                                QPoint(0, bottom), QPoint(1, bottom), QPoint(0, bottom - 1)})
        image.setPixel(point, 0);
    return image;
}

QImage picture(int key, const std::function<QImage()> &make)
{
    const auto found = g_pictures.constFind(key);
    if (found != g_pictures.cend())
        return *found;
    const QImage image = make();
    g_pictures.insert(key, image);
    return image;
}

}

bool load()
{
    if (g_pe)
        return true;
    g_pe = xpfiles::resources(xpfiles::find(QStringLiteral("cards.dll")));
    if (!g_pe)
        return false;
    g_size = bitmap(IDB_GHOST).size();
    return !g_size.isEmpty();
}

QSize size()
{
    return g_size;
}

QImage face(int card)
{
    return picture(card, [card] {
        const int id = rank(card) % 13 + suit(card) * 13 + 1;
        QImage image = bitmap(id);
        // The red suits' aces to tens are two-colour pictures, red and white: the edge is redrawn black
        if (!image.isNull() && ((id > 13 && id < 24) || (id > 26 && id < 37))) {
            QPainter p(&image);
            const int w = image.width(), h = image.height();
            p.fillRect(2, 0, w - 4, 1, Qt::black);
            p.fillRect(w - 1, 2, 1, h - 4, Qt::black);
            p.fillRect(2, h - 1, w - 4, 1, Qt::black);
            p.fillRect(0, 2, 1, h - 4, Qt::black);
            for (const QPoint &point : {QPoint(1, 1), QPoint(w - 2, 1), QPoint(w - 2, h - 2), QPoint(1, h - 2)})
                p.fillRect(QRect(point, QSize(1, 1)), Qt::black);
        }
        return rounded(image);
    });
}

QImage inverted(int card)
{
    return picture(0x100 + card, [card] {
        QImage image = face(card);
        image.invertPixels();
        return image;
    });
}

QImage back(int id)
{
    return picture(0x200 + id, [id] { return rounded(bitmap(qBound(int(FirstBack), id, int(LastBack)))); });
}

QImage ghost(const QColor &background)
{
    // The outline is black on white, and is ANDed onto the colour
    QImage image = bitmap(IDB_GHOST);
    for (int y = 0; y < image.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
            line[x] = (line[x] & background.rgb()) | 0xff000000;
    }
    return rounded(image);
}

QImage deckO()
{
    return picture(0x300, [] { return rounded(bitmap(IDB_O)); });
}

QImage deckX()
{
    return picture(0x301, [] { return rounded(bitmap(IDB_X)); });
}

}
