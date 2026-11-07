// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QFont>
#include <QImage>
#include <QPainter>

#include <array>
#include <cmath>
#include <QHash>
#include <QRawFont>
#include <QStringList>
#include <QtEndian>

namespace w4w {

// XP's Display Properties > Appearance > Effects: no smoothing, Standard or ClearType. The session
// passes it as WIN4WINE_FONT_SMOOTHING (none, standard, cleartype), from native-look.conf's
// font_smoothing (session.sh); without it, Standard, XP's default.
enum class Smoothing { None, Standard, ClearType };

inline Smoothing fontSmoothing()
{
    static const Smoothing smoothing = [] {
        const QByteArray value = qgetenv("WIN4WINE_FONT_SMOOTHING").trimmed().toLower();
        return value == "cleartype" ? Smoothing::ClearType : value == "none" ? Smoothing::None : Smoothing::Standard;
    }();
    return smoothing;
}

// Whether a face's gasp table asks for grey smoothing (GASP_DOGRAY) at PIXELS per em: what XP's
// Standard smoothing follows (measured in XP SP3: Tahoma stays whole-pixel up to 16 px, Arial up
// to 17, Trebuchet MS Bold is smoothed from 12, as in the title bars; Lucida Console, without a
// table, is whole-pixel at 13). The session's fontconfig rules say the same for Linux programs
// (tools/fontsmoothing.py); XP's own programs get rules that leave it to this.
inline bool gaspSmooths(const QFont &font, int pixels)
{
    static QHash<QString, QList<QPair<int, int>>> tables;
    const QString key = font.family() + QLatin1Char('/') + QString::number(font.weight()) + QLatin1Char('/') + QString::number(font.italic());
    auto it = tables.find(key);
    if (it == tables.end()) {
        QFont probe = font;
        probe.setPixelSize(16);
        const QByteArray gasp = QRawFont::fromFont(probe).fontTable("gasp");
        QList<QPair<int, int>> ranges;
        if (gasp.size() >= 4) {
            const auto *d = reinterpret_cast<const uchar *>(gasp.constData());
            const int count = qFromBigEndian<quint16>(d + 2);
            for (int i = 0; i < count && 4 + 4 * i + 4 <= gasp.size(); ++i)
                ranges.append({qFromBigEndian<quint16>(d + 4 + 4 * i), qFromBigEndian<quint16>(d + 6 + 4 * i)});
        }
        it = tables.insert(key, ranges);
    }
    if (it->isEmpty())
        return pixels > 16;
    for (const auto &[maxPpem, flags] : std::as_const(*it))
        if (pixels <= maxPpem)
            return flags & 2;
    return false;
}

// A font drawn as XP draws it under the session's smoothing: whole pixels hinted by the font,
// grey where Standard smoothing and the face's gasp table agree, RGB subpixels for ClearType.
// ANTIALIASED, a font created with ANTIALIASED_QUALITY (wscui.cpl's, sub_4fad77f3): grey whatever
// the smoothing and the gasp table, but whole pixels small (XP SP3: Tahoma 8's 11 pixels whole,
// Trebuchet MS 11's 15 grey; where between is not measured).
inline void xpFontSmoothing(QFont &font, int pixels, bool antialiased = false)
{
    font.setHintingPreference(QFont::PreferFullHinting);
    font.setKerning(false);
    if (antialiased && fontSmoothing() != Smoothing::ClearType) {
        font.setStyleStrategy(pixels > 11 ? QFont::StyleStrategy(QFont::PreferAntialias | QFont::NoSubpixelAntialias) : QFont::NoAntialias);
        return;
    }
    switch (fontSmoothing()) {
    case Smoothing::ClearType:
        font.setStyleStrategy(QFont::PreferAntialias);
        break;
    case Smoothing::Standard:
        font.setStyleStrategy(gaspSmooths(font, pixels) ? QFont::StyleStrategy(QFont::PreferAntialias | QFont::NoSubpixelAntialias)
                                                        : QFont::NoAntialias);
        break;
    case Smoothing::None:
        font.setStyleStrategy(QFont::NoAntialias);
        break;
    }
}

// Text in PAINTER's font as GDI draws it under Standard smoothing: GDI blends its grey coverage with
// a gamma of 2.2 and a little more weight, and drops the faintest edges (measured on XP SP3's title
// bars against Qt's coverage of the same hinted glyphs: 0 under 40/255, then 255 * min(1, 1.15 a)^(1/2.2)).
// Whole-pixel and ClearType text is drawn as it is.
inline void drawGdiText(QPainter *painter, const QRect &rect, int flags, const QString &text, const QColor &colour)
{
    const QFont font = painter->font();
    if (fontSmoothing() != Smoothing::Standard || (font.styleStrategy() & QFont::NoAntialias) || rect.isEmpty()) {
        painter->setPen(colour);
        painter->drawText(rect, flags, text);
        return;
    }
    static const std::array<uchar, 256> contrast = [] {
        std::array<uchar, 256> table{};
        for (int a = 0; a < 256; ++a)
            table[a] = a < 40 ? 0 : uchar(qRound(255 * std::pow(qMin(1.0, 1.15 * a / 255), 1 / 2.2)));
        return table;
    }();
    const qreal ratio = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    QImage mask((QSizeF(rect.size()) * ratio).toSize(), QImage::Format_ARGB32_Premultiplied);
    mask.setDevicePixelRatio(ratio);
    mask.fill(Qt::transparent);
    {
        QPainter coverage(&mask);
        coverage.setFont(font);
        coverage.setPen(Qt::white);
        coverage.drawText(QRect(QPoint(0, 0), rect.size()), flags, text);
    }
    const QRgb ink = colour.rgb();
    for (int y = 0; y < mask.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(mask.scanLine(y));
        for (int x = 0; x < mask.width(); ++x) {
            const int a = contrast[qAlpha(line[x])];
            line[x] = qRgba(qRed(ink) * a / 255, qGreen(ink) * a / 255, qBlue(ink) * a / 255, a);
        }
    }
    painter->drawImage(rect.topLeft(), mask);
}

inline QFont themeFont(const QString &spec, int dpi)
{
    const QStringList parts = spec.split(QLatin1Char(','));
    QString family = parts.value(0).trimmed();
    // XP's font mapper accepts this short name used by msgina and some themes.
    if (family.compare(QLatin1String("Franklin Gothic"), Qt::CaseInsensitive) == 0)
        family = QStringLiteral("Franklin Gothic Medium");
    QFont font(family);
    const int pixels = qMax(1, qRound(parts.value(1).trimmed().toDouble() * dpi / 72.0));
    font.setPixelSize(pixels);
    for (int i = 2; i < parts.size(); ++i) {
        const QString style = parts[i].trimmed().toLower();
        if (style == QLatin1String("bold"))
            font.setBold(true);
        else if (style == QLatin1String("italic"))
            font.setItalic(true);
    }
    xpFontSmoothing(font, pixels);
    return font;
}

} // namespace w4w
