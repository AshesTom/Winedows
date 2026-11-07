// SPDX-License-Identifier: LGPL-2.1-or-later
// iexplore: XP's Internet Explorer as a program of its own, as iexplore.exe is beside explorer.exe.
#include "internetexplorer.h"
#include "xpapp.h"
#include "xpfiles.h"

#include <QApplication>
#include <QCommandLineParser>

#include <cstdio>

int main(int argc, char **argv)
{
    xpapp::prepare();
    InternetExplorer::registerSchemes();
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("XP's Internet Explorer"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("address"), QStringLiteral("the page to open (the home page without one)"), QStringLiteral("[address]"));
    parser.process(app);
    if (!xpapp::start(app, "iexplore"))
        return 1;
    if (xpfiles::find(QStringLiteral("browselc.dll")).isEmpty()) {
        fprintf(stderr, "iexplore: browselc.dll not found. Run native-shell/extract.py on the XP ISO again.\n");
        return 1;
    }
    InternetExplorer::open(parser.positionalArguments().value(0));
    return app.exec();
}
