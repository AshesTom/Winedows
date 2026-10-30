// SPDX-License-Identifier: LGPL-2.1-or-later
// The user's own XP files, and what the shell takes from them: the visual
// style, strings, bitmaps, icons and fonts. Only resources are read; no code
// of those files runs.
#pragma once

#include "msstyles.h"

#include <QFont>
#include <QImage>
#include <QString>

namespace xpfiles {

// A file of XP's by name, wherever an extraction has it: WIN4WINE_ASSETS,
// then the checkout's .local/native-assets (native-shell/extract.py) and
// .local/assets (extract/extract.py). Empty if none has it.
QString find(const QString &name);
// The fonts XP's style names, for this program.
void loadFonts();

// A string of shell32.dll or explorer.exe, for the language of the user's XP.
QString shellString(int id, const char *fallback);
QString explorerString(int id, const char *fallback);

// RT_STRING and RT_BITMAP of a PE file's resources.
QString string(const w4w::PeResources &pe, int id);
QImage bitmap(const w4w::PeResources &pe, int id);
// The image of icon group GROUP closest to SIZE at the most colours; null if there is none.
QImage icon(const w4w::PeResources &pe, int group, int size);
// The same for a group by its name, as a number or as some files name them (IDI_APPICON).
QImage icon(const w4w::PeResources &pe, const QString &group, int size);
std::optional<w4w::PeResources> resources(const QString &path);

// A "Tahoma, 8, Bold" of a style, its size in points at DPI, hinted as Windows
// hints it, and smoothed only at the sizes where the font's gasp table asks
// for it, as XP's Standard smoothing does (Tahoma above 16 pixels, Trebuchet
// MS above 11).
QFont themeFont(const QString &spec, int dpi);

} // namespace xpfiles
