// SPDX-License-Identifier: LGPL-2.1-or-later
#include "xpapp.h"
#include "editmenu.h"
#include "menushadow.h"
#include "programs.h"
#include "theme.h"
#include "xpfiles.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>

#include <cstdio>

namespace xpapp {

namespace {
// The session's fontconfig rules for Linux programs, while this program reads XP's own
QByteArray linuxFonts;
bool swappedFonts = false;
}

void prepare(bool webPages)
{
    // XP's programs smooth each face as XP would (fontpolicy.h), which fontconfig must then let them
    // do; what they start goes back to the session's rules for Linux programs (start()).
    if (const QByteArray xpFonts = qgetenv("WIN4WINE_XP_FONTCONFIG"); !xpFonts.isEmpty() && !webPages) {
        linuxFonts = qgetenv("FONTCONFIG_FILE");
        qputenv("FONTCONFIG_FILE", xpFonts);
        swappedFonts = true;
    }
    // Only X11 windows can be EWMH's desktop and dock here. Tahoma is hinted
    // as Windows hints it, with FreeType's classic TrueType interpreter.
    qputenv("QT_QPA_PLATFORM", "xcb");
    qputenv("FREETYPE_PROPERTIES", "truetype:interpreter-version=35");
    // This is a desktop of its own: the desktop's platform theme is not for it
    // (Plasma's event filter crashed on the Start menu's cascades).
    qputenv("QT_QPA_PLATFORMTHEME", "generic");
    // (For the web pages of a folder window, set before the application exists.)
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeMenuBar);
}

bool start(QApplication &app, const char *name)
{
    menushadow::install();
    editmenu::install();
    app.setApplicationName(QLatin1String(name));

    xpfiles::loadFonts();
    // fontconfig has read its rules by now
    if (swappedFonts)
        qputenv("FONTCONFIG_FILE", linuxFonts);
    if (ShellTheme::lunaPath().isEmpty()) {
        fprintf(stderr, "%s: luna.msstyles not found. Run native-shell/extract.py on the XP ISO first.\n", name);
        return false;
    }
    ShellTheme::ensureDefaultLook();
    ShellTheme *theme = ShellTheme::instance();
    const auto applyFont = [theme] {
        QApplication::setFont(theme->systemFont(QStringLiteral("msgboxfont")));
    };
    applyFont();
    QObject::connect(theme, &ShellTheme::changed, &app, applyFont);
    programs::useIconTheme();
    QObject::connect(theme, &ShellTheme::changed, &app, [] { programs::useIconTheme(); });
    return true;
}

QString sibling(const QString &name)
{
    return QFileInfo(QCoreApplication::applicationFilePath()).dir().filePath(name);
}

} // namespace xpapp
