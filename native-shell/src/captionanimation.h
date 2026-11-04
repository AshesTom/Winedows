// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's animation of minimizing, maximizing and restoring a window (user32's DrawAnimatedRects with
// IDANI_CAPTION): a caption strip that flies between two rectangles. XP has none for closing.
#pragma once

#include <QImage>
#include <QRect>
#include <QString>

namespace captionanimation {

// Flies a caption strip with ICON and TITLE from FROM to TO (screen rectangles of whole frames or of a
// taskbar button). Returns at once; the strip goes away by itself. Nothing when XP's "Animate windows
// when minimizing and maximizing" would be off (WIN4WINE_NO_ANIMATION).
void run(const QRect &from, const QRect &to, const QString &title, const QImage &icon);
// None for MS milliseconds: explorer.exe turns SPI_SETANIMATION off while it minimizes all the windows
// (Win+M, Show Desktop) and while it puts them back.
void pause(int ms);

} // namespace captionanimation
