// SPDX-License-Identifier: LGPL-2.1-or-later
// wmplayer: Windows Media Player as a program of its own, as XP has it apart from explorer.exe
// (Program Files\Windows Media Player\wmplayer.exe). The shell starts it; so do music and videos.
#include "mediaplayer.h"
#include "menushadow.h"
#include "xpfiles.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFont>

#include <cstdio>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    menushadow::install();
    app.setApplicationName(QStringLiteral("wmplayer"));
    app.setQuitOnLastWindowClosed(true);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Windows Media Player 9, from the skins of the user's wmploc.dll"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("what to play"), QStringLiteral("[files...]"));
    parser.process(app);

    if (!MediaPlayer::available()) {
        fprintf(stderr, "wmplayer: wmploc.dll and wmp.dll not found. Run native-shell/extract.py on the XP ISO again.\n");
        return 1;
    }
    xpfiles::loadFonts();
    // XP's message box font for the menus and dialogs: Tahoma 8
    QFont font(QStringLiteral("Tahoma"));
    font.setPixelSize(11);
    QApplication::setFont(font);
    MediaPlayer player(parser.positionalArguments());
    player.show();
    return app.exec();
}
