// SPDX-License-Identifier: LGPL-2.1-or-later
#include "xpshortcuts.h"

#include "applets.h"
#include "filebrowser.h"
#include "programs.h"
#include "securitycenter.h"
#include "xpfiles.h"

#include <QDir>
#include <QHash>
#include <QStandardPaths>

namespace xpshortcuts {

namespace {

// What a shortcut does here: the ported program, or nothing yet (a dummy)
enum Action {
    None, Notepad, Calculator, CommandPrompt, MediaPlayer, Browser, WindowsExplorer, NetworkConnections, SecurityCenter,
    Help, ProgramAccess, Sound,
};

struct Shortcut
{
    const char *group;      // "" for the Start Menu's own, "Programs" and under it
    const char *nameFile;   // the file of its DisplayResource, and its string; the INF's words without one
    int nameId;
    const char *name;
    const char *iconFile;   // the file of its icon: an index (>= 0) or a resource ID (< 0), as an INF's IconIndex
    int icon;
    Action action;
    const char *target;     // the address or topic it opens
};

// From the user's XP's setup files (syssetup.inf, shell.inf, accessor.inf, wordpad.inf, pinball.inf, wab50.inf,
// msinfo32.inf, dfrg.inf, netrass.inf, ie.inf, tsoc.inf, msmsgs.inf, moviemk.inf), and as XP SP3 lists them
const Shortcut SHORTCUTS[] = {
    // The Start Menu's own, above the line
    {"", "xpsp1res.dll", 10077, "Set Program Access and Defaults", "moricons.dll", -114, ProgramAccess, nullptr},
    {"", "shell32.dll", 22075, "Windows Catalog", "moricons.dll", 107, Browser, "http://go.microsoft.com/fwlink/?LinkId=374"},
    {"", "shell32.dll", 22068, "Windows Update", "wupdmgr.exe", 0, Browser, "http://windowsupdate.microsoft.com/"},
    // Programs
    {"Programs", "shell32.dll", 22034, "Internet Explorer", "iexplore.exe", 0, Browser, nullptr},
    {"Programs", nullptr, 0, "MSN", "msnms.ico", 0, Browser, "http://www.msn.com/"},
    {"Programs", "shell32.dll", 22053, "Outlook Express", "msimn.exe", 0, None, nullptr},
    {"Programs", "rcbdyctl.dll", 152, "Remote Assistance", "rcimlby.exe", 0, Help, "hcp://CN=Microsoft Corporation,L=Redmond,S=Washington,C=US/Remote Assistance/Escalation/Common/rcscreen1.htm"},
    {"Programs", nullptr, 0, "Windows Media Player", "wmplayer.exe", 0, MediaPlayer, nullptr},
    {"Programs", nullptr, 0, "Windows Messenger", "msmsgs.exe", 0, None, nullptr},
    {"Programs", nullptr, 0, "Windows Movie Maker", "moviemk.exe", 0, None, nullptr},
    // Accessories
    {"Accessories", "shell32.dll", 22017, "Address Book", "wab.exe", 0, None, nullptr},
    {"Accessories", "shell32.dll", 22019, "Calculator", "calc.exe", 0, Calculator, nullptr},
    {"Accessories", "shell32.dll", 22022, "Command Prompt", "cmd.exe", 0, CommandPrompt, nullptr},
    {"Accessories", "shell32.dll", 22051, "Notepad", "notepad.exe", 0, Notepad, nullptr},
    {"Accessories", "shell32.dll", 22054, "Paint", "mspaint.exe", 0, None, nullptr},
    {"Accessories", "compatui.dll", 115, "Program Compatibility Wizard", "compatui.dll", 0, Help, "hcp://system/compatctr/compatmode.htm"},
    {"Accessories", "mstsc.exe", 4000, "Remote Desktop Connection", "mstsc.exe", 0, None, nullptr},
    {"Accessories", "shell32.dll", 22062, "Synchronize", "mobsync.exe", 0, None, nullptr},
    {"Accessories", "tourstart.exe", 1, "Tour Windows XP", "tourstart.exe", 0, None, nullptr},
    {"Accessories", "shell32.dll", 22067, "Windows Explorer", "explorer.exe", 1, WindowsExplorer, nullptr},
    {"Accessories", "shell32.dll", 22069, "WordPad", "wordpad.exe", 0, None, nullptr},
    {"Accessories/Accessibility", "shell32.dll", 22016, "Accessibility Wizard", "accwiz.exe", 0, None, nullptr},
    {"Accessories/Accessibility", "shell32.dll", 22041, "Magnifier", "magnify.exe", 0, None, nullptr},
    {"Accessories/Accessibility", "shell32.dll", 22048, "Narrator", "narrator.exe", 0, None, nullptr},
    {"Accessories/Accessibility", "shell32.dll", 22052, "On-Screen Keyboard", "osk.exe", 0, None, nullptr},
    {"Accessories/Accessibility", "shell32.dll", 22065, "Utility Manager", "utilman.exe", 0, None, nullptr},
    {"Accessories/Communications", "shell32.dll", 22031, "HyperTerminal", "hypertrm.exe", 0, None, nullptr},
    {"Accessories/Communications", "netshell.dll", 1200, "Network Connections", "netshell.dll", 0, NetworkConnections, nullptr},
    {"Accessories/Communications", "hnetwiz.dll", 3085, "Network Setup Wizard", "hnetwiz.dll", 0, None, nullptr},
    {"Accessories/Communications", "netshell.dll", 1010, "New Connection Wizard", "netshell.dll", 3, None, nullptr},
    {"Accessories/Communications", "xpsp2res.dll", 16201, "Wireless Network Setup Wizard", "xpsp2res.dll", -2026, None, nullptr},
    {"Accessories/Entertainment", "shell32.dll", 22061, "Sound Recorder", "sndrec32.exe", 0, None, nullptr},
    {"Accessories/Entertainment", "shell32.dll", 22066, "Volume Control", "sndvol32.exe", 0, Sound, nullptr},
    {"Accessories/Entertainment", nullptr, 0, "Windows Media Player", "wmplayer.exe", 0, MediaPlayer, nullptr},
    {"Accessories/System Tools", "shell32.dll", 22018, "Backup", "ntbackup.exe", 0, None, nullptr},
    {"Accessories/System Tools", "shell32.dll", 22021, "Character Map", "charmap.exe", 0, None, nullptr},
    {"Accessories/System Tools", "shell32.dll", 22026, "Disk Cleanup", "cleanmgr.exe", 0, None, nullptr},
    {"Accessories/System Tools", "shell32.dll", 22027, "Disk Defragmenter", "dfrgres.dll", 0, None, nullptr},
    {"Accessories/System Tools", "migwiz.exe", 202, "Files and Settings Transfer Wizard", "migwiz.exe", 0, None, nullptr},
    {"Accessories/System Tools", "shell32.dll", 22058, "Scheduled Tasks", "mstask.dll", 0, None, nullptr},
    {"Accessories/System Tools", "xpsp2res.dll", 6103, "Security Center", "xpsp2res.dll", -6105, SecurityCenter, nullptr},
    {"Accessories/System Tools", "shell32.dll", 22063, "System Information", "msinfo32.exe", 0, None, nullptr},
    {"Accessories/System Tools", nullptr, 0, "System Restore", "rstrui.exe", 0, None, nullptr},
    // Games
    {"Games", "shell32.dll", 22030, "FreeCell", "freecell.exe", 0, None, nullptr},
    {"Games", nullptr, 0, "Hearts", "mshearts.exe", 0, None, nullptr},
    {"Games", nullptr, 0, "Internet Backgammon", "bckgzm.exe", 0, None, nullptr},
    {"Games", nullptr, 0, "Internet Checkers", "chkrzm.exe", 0, None, nullptr},
    {"Games", nullptr, 0, "Internet Hearts", "hrtzzm.exe", 0, None, nullptr},
    {"Games", nullptr, 0, "Internet Reversi", "rvsezm.exe", 0, None, nullptr},
    {"Games", nullptr, 0, "Internet Spades", "shvlzm.exe", 0, None, nullptr},
    {"Games", "shell32.dll", 22045, "Minesweeper", "winmine.exe", 0, None, nullptr},
    {"Games", "shell32.dll", 22057, "Pinball", "pinball.exe", 0, None, nullptr},
    {"Games", "shell32.dll", 22060, "Solitaire", "sol.exe", 0, None, nullptr},
    {"Games", nullptr, 0, "Spider Solitaire", "spider.exe", 0, None, nullptr},
};

// The groups under Programs and their words in shell32.dll (syssetup.inf's [StartMenuGroups])
const struct { const char *group; int nameId; } GROUPS[] = {
    {"Accessories", 21761}, {"Games", 21773}, {"Startup", 21787}, {"Accessories/Accessibility", 21760},
    {"Accessories/Communications", 21768}, {"Accessories/Entertainment", 21772}, {"Accessories/System Tools", 21788},
};

const std::optional<w4w::PeResources> &peOf(const char *name)
{
    static QHash<QString, std::optional<w4w::PeResources>> cache;
    const QString key = QLatin1String(name);
    if (!cache.contains(key))
        cache.insert(key, xpfiles::resources(xpfiles::find(key)));
    return cache[key];
}

QIcon iconOf(const Shortcut &shortcut, int size)
{
    const QString file = QLatin1String(shortcut.iconFile);
    if (file.endsWith(QLatin1String(".ico"))) {
        const QString path = xpfiles::find(file);
        return path.isEmpty() ? QIcon() : QIcon(path);
    }
    const auto &pe = peOf(shortcut.iconFile);
    if (!pe)
        return {};
    const QString group = shortcut.icon < 0 ? QString::number(-shortcut.icon) : xpfiles::iconGroupAt(*pe, shortcut.icon);
    return QIcon(QPixmap::fromImage(xpfiles::icon(*pe, group, size)));
}

QString nameOf(const Shortcut &shortcut)
{
    if (shortcut.nameFile)
        if (const auto &pe = peOf(shortcut.nameFile))
            if (const QString text = xpfiles::string(*pe, shortcut.nameId).remove(QChar(0)); !text.isEmpty())
                return text;
    // Windows Media Player by its file's description, as the existing Start menu entry
    if (QLatin1String(shortcut.iconFile) == QLatin1String("wmplayer.exe"))
        if (const auto &pe = peOf("wmplayer.exe"))
            if (const QString text = xpfiles::fileDescription(*pe); !text.isEmpty())
                return text;
    return QLatin1String(shortcut.name);
}

std::function<void()> actionOf(const Shortcut &shortcut)
{
    const QString target = shortcut.target ? QLatin1String(shortcut.target) : QString();
    switch (shortcut.action) {
    case Notepad: return [] { programs::openNotepad(); };
    case Calculator: return [] { programs::openCalculator(); };
    case CommandPrompt: return [] { programs::openCommandPrompt(QDir::homePath()); };
    case MediaPlayer: return [] { programs::openMediaPlayer(); };
    case Browser: return [target] { programs::openInternetExplorer(target); };
    case WindowsExplorer: return [] { FileBrowser::explore(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)); };
    case NetworkConnections: return [] { applets::open(QStringLiteral("network")); };
    case SecurityCenter: return [] { securitycenter::showWindow(); };
    case Help: return [target] { programs::openHelpCenter(target); };
    case ProgramAccess: return [] { applets::open(QStringLiteral("programs")); };
    case Sound: return [] { applets::open(QStringLiteral("sound")); };
    case None: break;
    }
    // A program XP has and this session does not port yet: a shortcut that does nothing
    return [] {};
}

} // namespace

QList<MenuEntry> entries(const QString &group, int iconSize)
{
    QList<MenuEntry> out;
    const QString wanted = group == QLatin1String("Programs") ? QStringLiteral("Programs") : group;
    for (const Shortcut &shortcut : SHORTCUTS) {
        if (QLatin1String(shortcut.group) != wanted)
            continue;
        MenuEntry entry;
        entry.text = nameOf(shortcut);
        entry.icon = iconOf(shortcut, iconSize);
        entry.action = actionOf(shortcut);
        out.append(entry);
    }
    return out;
}

QStringList groups(const QString &group)
{
    QStringList out;
    for (const auto &entry : GROUPS) {
        const QString name = QLatin1String(entry.group);
        if (group.isEmpty() ? !name.contains(QLatin1Char('/')) : name.startsWith(group + QLatin1Char('/')) && name.count(QLatin1Char('/')) == group.count(QLatin1Char('/')) + 1)
            out.append(name);
    }
    return out;
}

QString groupName(const QString &group)
{
    for (const auto &entry : GROUPS)
        if (QLatin1String(entry.group) == group)
            if (const auto &shell32 = peOf("shell32.dll"))
                if (const QString text = xpfiles::string(*shell32, entry.nameId); !text.isEmpty())
                    return text;
    return group.section(QLatin1Char('/'), -1);
}

} // namespace xpshortcuts
