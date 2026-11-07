// SPDX-License-Identifier: LGPL-2.1-or-later
// notepad: XP's Notepad as a program of its own, as notepad.exe is beside explorer.exe.
#include "notepad.h"
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
    parser.setApplicationDescription(QStringLiteral("XP's Notepad"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("the file to open"), QStringLiteral("[file]"));
    parser.process(app);
    if (!xpapp::start(app, "notepad"))
        return 1;
    if (xpfiles::find(QStringLiteral("notepad.exe")).isEmpty()) {
        fprintf(stderr, "notepad: notepad.exe not found. Run native-shell/extract.py on the XP ISO again.\n");
        return 1;
    }
    (new Notepad(parser.positionalArguments().value(0)))->show();
    return app.exec();
}
