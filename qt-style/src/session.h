// SPDX-License-Identifier: LGPL-2.1-or-later
// The look the XP shell of this session has now, read from its Wine
// prefix's registry (user.reg): the visual style ThemeManager names, the
// system colours of Control Panel\Colors and the fonts of WindowMetrics.
// Wine writes user.reg back a little after a change (Display Properties);
// the file is watched, so Qt applications follow. Control Panel's GTK/QT
// Themes and Icons items (session/linuxlook.c) write the Linux side's own
// choices to ~/.config/win4wine/linux-look.conf, which is watched too.
#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QFont>
#include <QHash>
#include <QObject>
#include <QTimer>

namespace w4w {

struct Look
{
    bool themed = false;          // a visual style is active (not Windows Classic)
    QString stylePath;            // the .msstyles, as a Linux path
    QString colourName, sizeName; // ThemeManager's ColorName and SizeName
    QHash<QString, QColor> colours;   // Control Panel\Colors, lower-case names
    QFont messageFont, menuFont, statusFont;
    bool haveFonts = false;
    int dpi = 96;
    // linux-look.conf: the Accurate style (XP's own metrics, no pictures on
    // buttons or in menus) rather than the Standard one; the icon theme the
    // user chose (empty: the desktop's); the GTK theme's build, which a
    // Rebuild in Control Panel raises to have it made again.
    bool accurate = false;
    QString iconTheme;
    int themeBuild = 0;
    bool operator==(const Look &o) const
    {
        return themed == o.themed && stylePath == o.stylePath && colourName == o.colourName
            && sizeName == o.sizeName && colours == o.colours && messageFont == o.messageFont
            && menuFont == o.menuFont && statusFont == o.statusFont && dpi == o.dpi
            && accurate == o.accurate && iconTheme == o.iconTheme && themeBuild == o.themeBuild;
    }
};

class Session : public QObject
{
    Q_OBJECT
public:
    static Session *instance();
    QString prefix() const { return m_prefix; }
    const Look &look() const { return m_look; }

Q_SIGNALS:
    void changed();

private:
    Session();
    void reload();
    void watch();
    QString m_prefix;
    Look m_look;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
};

// The prefix of the session: WIN4WINE_PREFIX, WINEPREFIX, the session's
// configuration (~/.config/win4wine/session.json), or ~/.wine.
QString findPrefix();
// A Windows path ("C:\windows\...") as the Linux file it is in PREFIX, the
// case of each component found as the file system has it.
QString linuxPath(const QString &prefix, const QString &windowsPath);
Look readLook(const QString &prefix);
// ~/.config/win4wine/linux-look.conf
QString linuxLookPath();

} // namespace w4w
