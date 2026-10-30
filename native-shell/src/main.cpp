// SPDX-License-Identifier: LGPL-2.1-or-later
// The XP shell as a Linux program (docs/native-shell.md): the desktop and its
// icons, the taskbar, the Start menu, Display Properties, the windows' frames. A window manager manages it as a desktop and a
// dock: bridge-wm, or any other that follows EWMH.
#include "applets.h"
#include "controlpanel.h"
#include "desktop.h"
#include "displayproperties.h"
#include "sounds.h"

#include <QMessageBox>
#include "filebrowser.h"
#include "frames.h"
#include "programs.h"
#include "taskbar.h"
#include "theme.h"
#include "tray.h"
#include "windowlist.h"
#include "xpfiles.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QKeyEvent>
#include <QScreen>

#include <memory>

namespace {

// The Windows key, when the shell's own windows have the keyboard (else bridge-wm tells: Tray).
class WindowsKey : public QObject
{
public:
    explicit WindowsKey(Taskbar *taskbar)
        : m_taskbar(taskbar)
    {
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::KeyPress) {
            const int key = static_cast<QKeyEvent *>(event)->key();
            m_alone = key == Qt::Key_Meta || key == Qt::Key_Super_L || key == Qt::Key_Super_R;
            if (key == Qt::Key_Escape && static_cast<QKeyEvent *>(event)->modifiers() & Qt::ControlModifier)
                m_taskbar->showStartMenu();
        } else if (event->type() == QEvent::KeyRelease && m_alone) {
            const int key = static_cast<QKeyEvent *>(event)->key();
            if (key == Qt::Key_Meta || key == Qt::Key_Super_L || key == Qt::Key_Super_R) {
                m_alone = false;
                m_taskbar->showStartMenu();
            }
        }
        return QObject::eventFilter(object, event);
    }

private:
    Taskbar *m_taskbar;
    bool m_alone = false;
};

} // namespace

// XP's sound for a message box, by its icon
class MessageSounds : public QObject
{
protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Show)
            if (auto *box = qobject_cast<QMessageBox *>(watched)) {
                if (box->icon() == QMessageBox::Critical)
                    sounds::play(QStringLiteral("Windows XP Critical Stop.wav"));
                else if (box->icon() == QMessageBox::Warning)
                    sounds::play(QStringLiteral("Windows XP Exclamation.wav"));
                else if (box->icon() == QMessageBox::Information)
                    sounds::play(QStringLiteral("Windows XP Default.wav"));
            }
        return false;
    }
};

int main(int argc, char **argv)
{
    // Only X11 windows can be EWMH's desktop and dock here. Tahoma is hinted
    // as Windows hints it, with FreeType's classic TrueType interpreter.
    qputenv("QT_QPA_PLATFORM", "xcb");
    qputenv("FREETYPE_PROPERTIES", "truetype:interpreter-version=35");
    // This is a desktop of its own: the desktop's platform theme is not for it
    // (Plasma's event filter crashed on the Start menu's cascades).
    qputenv("QT_QPA_PLATFORMTHEME", "generic");
    // (For the web pages of a folder window, set before the application exists.)
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("xp-native-shell"));
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("The Windows XP shell's desktop and taskbar, natively"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("ipc"), QStringLiteral("bridge-wm's IPC directory (tray icons, Windows key)"), QStringLiteral("dir")});
    parser.addOption({QStringLiteral("frames"), QStringLiteral("draw the Linux windows' frames (without Wine's proxy.exe; needs --ipc)")});
    parser.addOption({QStringLiteral("explore"), QStringLiteral("only a folder window, for other programs that open folders"), QStringLiteral("folder")});
    parser.addOption({QStringLiteral("applet"), QStringLiteral("only a Control Panel applet: display, folders, fonts, network, programs, sound, system, mouse, keyboard, printers, users, datetime, winver"), QStringLiteral("id")});
    parser.process(app);

    xpfiles::loadFonts();
    if (ShellTheme::lunaPath().isEmpty()) {
        fprintf(stderr, "xp-native-shell: luna.msstyles not found. Run native-shell/extract.py on the XP ISO first.\n");
        return 1;
    }
    ShellTheme::ensureDefaultLook();
    ShellTheme *theme = ShellTheme::instance();
    QApplication::setFont(theme->font(QStringLiteral("Tahoma, 8")));
    programs::useIconTheme();
    QObject::connect(theme, &ShellTheme::changed, &app, [] { programs::useIconTheme(); });
    if (parser.isSet(QStringLiteral("applet"))) {
        app.setQuitOnLastWindowClosed(true);
        controlpanel::open(parser.value(QStringLiteral("applet")));
        return app.exec();
    }
    if (parser.isSet(QStringLiteral("explore"))) {
        app.setQuitOnLastWindowClosed(true);
        FileBrowser::open(parser.value(QStringLiteral("explore")));
        return app.exec();
    }
    applets::applyInputSettings();
    DisplayProperties::restoreScreenSaver();
    sounds::play(QStringLiteral("Windows XP Logon Sound.wav"));

    Desktop desktop;
    const int iconSize = theme->scaled(16);
    WindowList windows(iconSize);
    const QString ipc = parser.value(QStringLiteral("ipc"));
    Tray tray(ipc, iconSize);
    Taskbar taskbar(&windows, &tray);
    std::unique_ptr<FrameManager> frames;
    if (parser.isSet(QStringLiteral("frames")) && !ipc.isEmpty())
        frames = std::make_unique<FrameManager>(ipc);

    WindowsKey windowsKey(&taskbar);
    app.installEventFilter(&windowsKey);
    MessageSounds messageSounds;
    app.installEventFilter(&messageSounds);

    const auto replace = [&] {
        desktop.placeOnScreens();
        taskbar.placeOnScreen();
    };
    const auto follow = [&](QScreen *screen) {
        QObject::connect(screen, &QScreen::geometryChanged, &app, replace);
    };
    for (QScreen *screen : QGuiApplication::screens())
        follow(screen);
    QObject::connect(&app, &QGuiApplication::screenAdded, &app, [&](QScreen *screen) {
        follow(screen);
        replace();
    });
    QObject::connect(&app, &QGuiApplication::screenRemoved, &app, replace);
    QObject::connect(&app, &QGuiApplication::primaryScreenChanged, &app, replace);
    QObject::connect(theme, &ShellTheme::changed, &desktop, &Desktop::reload);
    // The icons' places depend on the work area, which the WM sets once the taskbar is there.
    QObject::connect(&windows, &WindowList::workAreaChanged, &desktop, &Desktop::placeOnScreens);

    desktop.show();
    taskbar.show();
    return app.exec();
}
