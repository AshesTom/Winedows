// SPDX-License-Identifier: LGPL-2.1-or-later
// spider: XP's Spider as a program of its own, as spider.exe is.
#include "spider.h"
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
    parser.setApplicationDescription(QStringLiteral("XP's Spider Solitaire"));
    parser.addHelpOption();
    parser.process(app);
    if (!xpapp::start(app, "spider"))
        return 1;
    if (xpfiles::find(QStringLiteral("spider.exe")).isEmpty()) {
        fprintf(stderr, "spider: spider.exe not found. Run native-shell/extract.py on the XP ISO again.\n");
        return 1;
    }
    (new Spider)->show();
    return app.exec();
}
