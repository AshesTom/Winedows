// SPDX-License-Identifier: LGPL-2.1-or-later
// helpctr: XP's Help and Support Center as a program of its own, as helpctr.exe is.
#include "helpcenter.h"
#include "xpapp.h"
#include "xpfiles.h"

#include <QApplication>
#include <QCommandLineParser>

#include <cstdio>

int main(int argc, char **argv)
{
    xpapp::prepare(true);
    HelpCenter::registerSchemes();
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("XP's Help and Support Center"));
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
    parser.addHelpOption();
    // helpctr.exe's own: -url hcp://...
    const QCommandLineOption url(QStringLiteral("url"), QStringLiteral("the hcp:// page to open"), QStringLiteral("address"));
    parser.addOption(url);
    parser.process(app);
    if (!xpapp::start(app, "helpctr"))
        return 1;
    if (xpfiles::find(QStringLiteral("HCsys_02.htm")).isEmpty()) {
        fprintf(stderr, "helpctr: the help's pages (pchdt_w3.cab) not found. Run native-shell/extract.py on the XP ISO again.\n");
        return 1;
    }
    (new HelpCenter(parser.value(url)))->show();
    return app.exec();
}
