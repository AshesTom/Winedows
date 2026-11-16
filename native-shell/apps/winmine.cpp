// SPDX-License-Identifier: LGPL-2.1-or-later
// winmine: XP's Minesweeper as a program of its own, as winmine.exe is.
#include "minesweeper.h"
#include "xpapp.h"
#include "xpfiles.h"

#include <QApplication>
#include <QCommandLineParser>

#include <cstdio>

int main(int argc, char **argv)
{
    xpapp::prepare();
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("XP's Minesweeper"));
    parser.addHelpOption();
    parser.process(app);
    if (!xpapp::start(app, "winmine"))
        return 1;
    if (xpfiles::find(QStringLiteral("winmine.exe")).isEmpty()) {
        fprintf(stderr, "winmine: winmine.exe not found. Run native-shell/extract.py on the XP ISO again.\n");
        return 1;
    }
    (new Minesweeper)->show();
    return app.exec();
}
