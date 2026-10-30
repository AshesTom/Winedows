// SPDX-License-Identifier: LGPL-2.1-or-later
#include "sounds.h"

#include "xpfiles.h"

#include <QProcess>
#include <QStandardPaths>

namespace sounds {

static QString player()
{
    static const QString found = [] {
        for (const char *name : {"pw-play", "paplay", "aplay"})
            if (const QString path = QStandardPaths::findExecutable(QLatin1String(name)); !path.isEmpty())
                return path;
        return QString();
    }();
    return found;
}

void playFile(const QString &path)
{
    if (!path.isEmpty() && !player().isEmpty())
        QProcess::startDetached(player(), {path});
}

void play(const QString &name)
{
    playFile(xpfiles::find(name));
}

void playAndWait(const QString &name, int mostMilliseconds)
{
    const QString path = xpfiles::find(name);
    if (path.isEmpty() || player().isEmpty())
        return;
    QProcess process;
    process.start(player(), {path});
    process.waitForFinished(mostMilliseconds);
}

} // namespace sounds
