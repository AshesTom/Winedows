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
#include "notifications.h"
#include "programs.h"
#include "securitycenter.h"
#include "taskbar.h"
#include "theme.h"
#include "tray.h"
#include "windowlist.h"
#include "xpapp.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QKeyEvent>
#include <QScreen>

#include <climits>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

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

// XP's other programs are programs of their own beside it: explorer --notepad and the rest,
// as earlier versions had them, run those.
static void forward(int argc, char **argv)
{
    static const struct {
        const char *option, *program;
    } FORWARDED[] = {{"--notepad", "notepad"}, {"--calc", "calc"}, {"--console", "cmd"}, {"--picture", "shimgvw"}};
    if (argc < 2)
        return;
    for (const auto &each : FORWARDED) {
        if (strcmp(argv[1], each.option) != 0)
            continue;
        // beside this program, wherever it was started from
        char self[PATH_MAX];
        const ssize_t length = readlink("/proc/self/exe", self, sizeof self - 1);
        std::string program = length > 0 ? std::string(self, size_t(length)) : std::string(argv[0]);
        program = program.substr(0, program.rfind('/') + 1) + each.program;
        std::vector<char *> arguments{const_cast<char *>(each.program)};
        for (int i = 2; i < argc; ++i)
            arguments.push_back(argv[i]);
        arguments.push_back(nullptr);
        execv(program.c_str(), arguments.data());
        perror(program.c_str());
        exit(1);
    }
}

int main(int argc, char **argv)
{
    forward(argc, argv);
    xpapp::prepare();
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("The Windows XP shell's desktop and taskbar, natively"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("ipc"), QStringLiteral("bridge-wm's IPC directory (tray icons, Windows key)"), QStringLiteral("dir")});
    parser.addOption({QStringLiteral("frames"), QStringLiteral("draw the Linux windows' frames (without Wine's proxy.exe; needs --ipc)")});
    parser.addOption({QStringLiteral("explore"), QStringLiteral("only a folder window, for other programs that open folders"), QStringLiteral("folder")});
    parser.addOption({QStringLiteral("applet"), QStringLiteral("only a Control Panel applet: display, folders, fonts, taskbar, network, programs, sound, system, mouse, keyboard, printers, users, datetime, security, winver"), QStringLiteral("id")});
    parser.process(app);

    if (!xpapp::start(app, "explorer"))
        return 1;
    ShellTheme *theme = ShellTheme::instance();
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
    NotificationServer notifications(&tray);
    securitycenter::Alerts securityAlerts(&tray);
    std::unique_ptr<FrameManager> frames;
    if (parser.isSet(QStringLiteral("frames")) && !ipc.isEmpty())
        frames = std::make_unique<FrameManager>(ipc);
    if (frames)
        frames->setButtonRect([&taskbar](quint32 window) { return taskbar.buttonRect(window); });

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
