// SPDX-License-Identifier: LGPL-2.1-or-later
#include "shellmenus.h"

#include "filebrowser.h"
#include "fileicons.h"
#include "fileops.h"
#include "programs.h"
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
#include <QStorageInfo>

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
        fileops::createShortcut(path, QDir(folder).filePath(shortcutName(folder, path) + QStringLiteral(".lnk")));
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

QString sendToFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/win4wine/SendTo");
}

// A desktop entry's command with %f, %F, %u, %U and %% filled, as a SendTo target runs.
QStringList sendToCommand(const programs::Program &program, const QStringList &paths)
{
    QStringList command = program.command;
    bool inserted = false;
    for (QString &argument : command) {
        if (argument == QLatin1String("%f") || argument == QLatin1String("%u")) {
            argument = paths.value(0);
            inserted = true;
        } else if (argument == QLatin1String("%F") || argument == QLatin1String("%U")) {
            argument = paths.join(QLatin1Char(' '));
            inserted = true;
        } else if (argument == QLatin1String("%%")) {
            argument = QStringLiteral("%");
        }
    }
    if (!inserted)
        command += paths;
    return command;
}

void addSendToFolder(QMenu *targets, const QStringList &paths, QWidget *parent)
{
    const QString folder = sendToFolder();
    QDir().mkpath(folder);
    const QFileInfoList entries = QDir(folder).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries) {
        const QString path = entry.filePath();
        if (entry.suffix() == QLatin1String("desktop")) {
            const auto program = programs::fromFile(path);
            if (!program)
                continue;
            QIcon icon = programs::iconOf(*program);
            if (icon.isNull())
                icon = fileicons::shell(fileicons::Document);
            targets->addAction(icon, program->name, parent, [program, paths] {
                const QStringList command = sendToCommand(*program, paths);
                if (!command.isEmpty())
                    QProcess::startDetached(command.value(0), command.mid(1));
            });
        } else if (entry.isSymLink() || entry.isDir()) {
            const QFileInfo target = entry.isSymLink() ? QFileInfo(entry.symLinkTarget()) : entry;
            if (!target.isDir())
                continue;
            targets->addAction(fileicons::shell(fileicons::Folder), entry.completeBaseName(), parent,
                               [paths, target, parent] { fileops::transfer(paths, target.absoluteFilePath(), fileops::Action::Copy, parent); });
        }
    }
}

void addFileVerbs(QMenu *menu, const QStringList &paths, QWidget *parent)
{
    if (paths.size() != 1)
        return;
    const QFileInfo file(paths.first());
    if (!file.isFile())
        return;
    static const QSet<QString> textTypes{QStringLiteral("txt"), QStringLiteral("log"), QStringLiteral("ini"), QStringLiteral("inf"),
                                         QStringLiteral("bat"), QStringLiteral("cmd")};
    static const QSet<QString> mediaTypes{QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("wma"), QStringLiteral("avi"),
                                          QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("wmv"), QStringLiteral("asf"),
                                          QStringLiteral("mid"), QStringLiteral("midi"), QStringLiteral("rmi"), QStringLiteral("m3u"),
                                          QStringLiteral("au"), QStringLiteral("aif"), QStringLiteral("aiff"), QStringLiteral("snd")};
    const QString suffix = file.suffix().toLower();
    if (textTypes.contains(suffix)) {
        // txtfile's and batfile's edit/print verbs (hivecls.inf): Notepad and its /p, which is lpr here.
        menu->addAction(QObject::tr("&Edit"), parent, [path = paths.first()] { programs::openNotepad(path); });
        QAction *print = menu->addAction(QObject::tr("&Print"), parent, [path = paths.first()] { QProcess::startDetached(QStringLiteral("lpr"), {path}); });
        print->setEnabled(!QStandardPaths::findExecutable(QStringLiteral("lpr")).isEmpty());
    } else if (mediaTypes.contains(suffix)) {
        // AVIFile's Play verb.
        menu->addAction(QObject::tr("&Play"), parent, [path = paths.first()] { programs::openMediaPlayer({path}); });
    }
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
    // XP's SendTo folder, then the removable drives the shell adds, then the built-in targets.
    addSendToFolder(targets, paths, parent);
    for (const QStorageInfo &volume : QStorageInfo::mountedVolumes()) {
        if (!volume.isValid() || !volume.isReady() || volume.isReadOnly())
            continue;
        const QString root = volume.rootPath();
        const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
        if (root == QLatin1String("/") || root == home || home.startsWith(root + QLatin1Char('/')))
            continue;
        targets->addAction(fileicons::shell(fileicons::Drive), QObject::tr("Removable Disk (%1)").arg(volume.displayName()), parent,
                           [paths, root, parent] { fileops::transfer(paths, root, fileops::Action::Copy, parent); });
    }
    if (!targets->actions().isEmpty())
        targets->addSeparator();
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
