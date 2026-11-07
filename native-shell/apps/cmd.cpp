// SPDX-License-Identifier: LGPL-2.1-or-later
// cmd: XP's Command Prompt as a program of its own, as cmd.exe in its console is: what runs
// in it cannot take the shell down.
#include "console.h"
#include "xpapp.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char **argv)
{
    xpapp::prepare();
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("XP's Command Prompt"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("folder"), QStringLiteral("where it starts (the home by default)"), QStringLiteral("[folder]"));
    parser.process(app);
    if (!xpapp::start(app, "cmd"))
        return 1;
    Console console(parser.positionalArguments().value(0));
    console.show();
    return app.exec();
}
