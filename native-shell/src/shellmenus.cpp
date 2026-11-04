// SPDX-License-Identifier: LGPL-2.1-or-later
#include "shellmenus.h"

#include "filebrowser.h"
#include "fileicons.h"
#include "fileops.h"
#include "xpfiles.h"
#include "xpprogram.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QProcess>
#include <QStandardPaths>

namespace shellmenus {

namespace {

const int STRING_SHORTCUT_TO = 4152;
const int ZIP_SEND_TO = 10148;   // zipfldr.dll's "Compressed (zipped) Folder"

} // namespace

const std::optional<w4w::PeResources> &shell32()
{
    static const auto pe = xpfiles::resources(xpfiles::find(QStringLiteral("shell32.dll")));
    return pe;
}

const QList<Order> &orders()
{
    static const QList<Order> list{{8976, "Name", 0}, {8978, "Size", 1}, {8979, "Type", 2}, {8980, "Date Modified", 3}};
    return list;
}

QString shortcutName(const QString &folder, const QString &target)
{
    // "Shortcut () to %s.lnk": the name XP gives a shortcut, without its .lnk (a link here)
    QString pattern = xpfiles::shellString(STRING_SHORTCUT_TO, "Shortcut () to %s.lnk");
    pattern.remove(QLatin1String(" ()")).remove(QLatin1String(".lnk"));
    return fileops::freeName(folder, QString(pattern).replace(QLatin1String("%s"), QFileInfo(target).fileName()));
}

void makeShortcuts(const QStringList &paths, const QString &folder)
{
    for (const QString &path : paths)
        QFile::link(path, QDir(folder).filePath(shortcutName(folder, path)));
}

void compressToZip(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    const QFileInfo first(paths.first());
    const QString folder = first.absolutePath();
    const QString name = fileops::freeName(folder, first.completeBaseName() + QStringLiteral(".zip"));
    QStringList arguments{QStringLiteral("-a"), QStringLiteral("-cf"), QDir(folder).filePath(name), QStringLiteral("-C"), folder};
    for (const QString &path : paths)
        arguments.append(QFileInfo(path).fileName());
    QProcess::startDetached(QStringLiteral("bsdtar"), arguments, folder);
}

void addSendTo(QMenu *menu, const QStringList &paths, QWidget *parent)
{
    if (!shell32())
        return;
    const QHash<int, QAction *> sendTo = xpprogram::fillMenu(menu, *shell32(), MENU_SEND_TO);
    QAction *popup = sendTo.value(CMD_SEND_TO);
    if (!popup || !popup->menu())
        return;
    // What XP's SendTo folder holds. Desktop (create shortcut) and Mail Recipient are setup's names,
    // which no extracted file has.
    QMenu *targets = popup->menu();
    targets->clear();
    if (const auto zip = xpfiles::resources(xpfiles::find(QStringLiteral("zipfldr.dll")));
        zip && !QStandardPaths::findExecutable(QStringLiteral("bsdtar")).isEmpty())
        targets->addAction(QIcon(QPixmap::fromImage(xpfiles::icon(*zip, zip->names(QStringLiteral("14")).value(0), 16))),
                           xpfiles::string(*zip, ZIP_SEND_TO), parent, [paths] { compressToZip(paths); });
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    targets->addAction(fileicons::shell(fileicons::Desktop), QObject::tr("Desktop (create shortcut)"), parent,
                       [paths, desktop] { makeShortcuts(paths, desktop); });
    if (!QStandardPaths::findExecutable(QStringLiteral("xdg-email")).isEmpty())
        targets->addAction(QObject::tr("Mail Recipient"), parent, [paths] {
            QStringList arguments;
            for (const QString &path : paths)
                arguments << QStringLiteral("--attach") << path;
            QProcess::startDetached(QStringLiteral("xdg-email"), arguments);
        });
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    targets->addAction(fileicons::shell(fileicons::Documents), places::title(documents), parent, [paths, documents, parent] {
        fileops::transfer(paths, documents, fileops::Action::Copy, parent);
    });
}

} // namespace shellmenus
