// SPDX-License-Identifier: LGPL-2.1-or-later
// calc: XP's Calculator as a program of its own, as calc.exe is.
#include "calculator.h"
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
    parser.setApplicationDescription(QStringLiteral("XP's Calculator"));
    parser.addHelpOption();
    parser.process(app);
    if (!xpapp::start(app, "calc"))
        return 1;
    if (xpfiles::find(QStringLiteral("calc.exe")).isEmpty()) {
        fprintf(stderr, "calc: calc.exe not found. Run native-shell/extract.py on the XP ISO again.\n");
        return 1;
    }
    (new Calculator)->show();
    return app.exec();
}
