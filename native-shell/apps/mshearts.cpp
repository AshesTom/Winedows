// SPDX-License-Identifier: LGPL-2.1-or-later
// mshearts: XP's Hearts as a program of its own, as mshearts.exe is.
#include "hearts.h"
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
    parser.setApplicationDescription(QStringLiteral("XP's Hearts"));
    parser.addHelpOption();
    parser.process(app);
    if (!xpapp::start(app, "mshearts"))
        return 1;
    if (xpfiles::find(QStringLiteral("mshearts.exe")).isEmpty()) {
        fprintf(stderr, "mshearts: mshearts.exe not found. Run native-shell/extract.py on the XP ISO again.\n");
        return 1;
    }
    (new Hearts)->show();
    return app.exec();
}
