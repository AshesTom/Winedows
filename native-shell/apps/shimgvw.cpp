// SPDX-License-Identifier: LGPL-2.1-or-later
// shimgvw: Windows Picture and Fax Viewer for pictures opened from other programs. XP runs it
// in explorer's process (rundll32 shimgvw.dll,ImageView_Fullscreen); the shell's own Preview still does.
#include "pictures.h"
#include "xpapp.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>

int main(int argc, char **argv)
{
    xpapp::prepare();
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Windows Picture and Fax Viewer"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("the picture"), QStringLiteral("file"));
    parser.process(app);
    if (parser.positionalArguments().isEmpty())
        parser.showHelp(2);
    if (!xpapp::start(app, "shimgvw"))
        return 1;
    app.setQuitOnLastWindowClosed(true);
    pictures::Viewer::open(QFileInfo(parser.positionalArguments().first()).absoluteFilePath());
    return app.exec();
}
