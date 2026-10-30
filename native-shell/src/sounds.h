// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's sounds, from the user's own media, through whichever player the system has.
#pragma once

#include <QString>

namespace sounds {

// A sound of %windir%\Media by its name, "Windows XP Error.wav"; nothing if it is not there.
void play(const QString &name);
// Any sound file.
void playFile(const QString &path);
// Plays and waits: what is said before the session ends.
void playAndWait(const QString &name, int mostMilliseconds);

} // namespace sounds
