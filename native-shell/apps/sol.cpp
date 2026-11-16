// SPDX-License-Identifier: LGPL-2.1-or-later
// sol: XP's Solitaire as a program of its own, as sol.exe is.
#include "solitaire.h"
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
    parser.setApplicationDescription(QStringLiteral("XP's Solitaire"));
    parser.addHelpOption();
    parser.process(app);
    if (!xpapp::start(app, "sol"))
        return 1;
    if (xpfiles::find(QStringLiteral("sol.exe")).isEmpty()) {
        fprintf(stderr, "sol: sol.exe not found. Run native-shell/extract.py on the XP ISO again.\n");
        return 1;
    }
    (new Solitaire)->show();
    return app.exec();
}
