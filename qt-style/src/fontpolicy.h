// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QFont>
#include <QStringList>

namespace w4w {

// XP with font smoothing disabled: lfntobj.cxx only requests FO_GRAY16 when
// FE_AA_ON or an explicit smoothing quality is selected. NONANTIALIASED_QUALITY
// excludes smoothing. Keep that monochrome path at every size, including titles.
inline void unsmoothedFont(QFont &font, int)
{
    font.setHintingPreference(QFont::PreferFullHinting);
    font.setKerning(false);
    font.setStyleStrategy(QFont::NoAntialias);
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
    unsmoothedFont(font, pixels);
    return font;
}

} // namespace w4w
