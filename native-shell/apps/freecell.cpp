// SPDX-License-Identifier: LGPL-2.1-or-later
// freecell: XP's FreeCell as a program of its own, as freecell.exe is.
#include "freecell.h"
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
    parser.setApplicationDescription(QStringLiteral("XP's FreeCell"));
    parser.addHelpOption();
    parser.process(app);
    if (!xpapp::start(app, "freecell"))
        return 1;
    if (xpfiles::find(QStringLiteral("freecell.exe")).isEmpty()) {
        fprintf(stderr, "freecell: freecell.exe not found. Run native-shell/extract.py on the XP ISO again.\n");
        return 1;
    }
    (new FreeCell)->show();
    return app.exec();
}
