// SPDX-License-Identifier: LGPL-2.1-or-later
#include "programs.h"

#include "filebrowser.h"
#include "session.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace {

using programs::Program;

// Freedesktop categories to XP's folders, the first that matches: as session/linux_apps.py does.
const struct { const char *category, *folder; } FOLDERS[] = {
    {"Game", "Games"},
    {"Network", "Internet"},
    {"AudioVideo", "Multimedia"},
    {"Audio", "Multimedia"},
    {"Video", "Multimedia"},
    {"Graphics", "Graphics"},
    {"Office", "Office"},
    {"Development", "Programming"},
    {"Settings", "Settings"},
    {"System", "System Tools"},
    {"Utility", "Accessories"},
};

QHash<QString, QString> readEntryValues(const QString &path)
{
    QHash<QString, QString> values;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return values;
    bool inEntry = false;
    for (const QByteArray &raw : file.readAll().split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inEntry = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        const int equals = line.indexOf(QLatin1Char('='));
        if (inEntry && equals > 0)
            values.insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
    }
    return values;
}

Program toProgram(const QString &path, const QHash<QString, QString> &values)
{
    Program program;
    program.id = QFileInfo(path).fileName();
    const QString locale = QLocale().name();
    program.name = values.value(QStringLiteral("Name[%1]").arg(locale),
                                values.value(QStringLiteral("Name[%1]").arg(locale.section(QLatin1Char('_'), 0, 0)),
                                             values.value(QStringLiteral("Name"))));
    program.icon = values.value(QStringLiteral("Icon"));
    program.categories = values.value(QStringLiteral("Categories")).split(QLatin1Char(';'), Qt::SkipEmptyParts);
    // Without its field codes: nothing is being opened with it.
    QString exec = values.value(QStringLiteral("Exec"));
    exec.remove(QRegularExpression(QStringLiteral("\\s%[A-Za-z]")));
    program.command = QProcess::splitCommand(exec);
    return program;
}

// What the Start menu lists: no hidden entries, none needing a terminal.
std::optional<Program> readEntry(const QString &path)
{
    const QHash<QString, QString> values = readEntryValues(path);
    if (values.value(QStringLiteral("Type")) != QLatin1String("Application")
        || values.value(QStringLiteral("NoDisplay")) == QLatin1String("true")
        || values.value(QStringLiteral("Hidden")) == QLatin1String("true")
        || values.value(QStringLiteral("Terminal")) == QLatin1String("true"))
        return std::nullopt;
    const QString tryExec = values.value(QStringLiteral("TryExec"));
    if (!tryExec.isEmpty() && QStandardPaths::findExecutable(tryExec).isEmpty() && !QFile::exists(tryExec))
        return std::nullopt;
    Program program = toProgram(path, values);
    if (program.name.isEmpty() || program.command.isEmpty())
        return std::nullopt;
    return program;
}

// file name -> path; the first directory that has a file name wins, as XDG says.
QHash<QString, QString> entryFiles()
{
    QHash<QString, QString> files;
    for (const QString &base : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
        const QStringList names = QDir(base).entryList({QStringLiteral("*.desktop")}, QDir::Files);
        for (const QString &name : names)
            if (!files.contains(name))
                files.insert(name, base + QLatin1Char('/') + name);
    }
    return files;
}

QString folderOf(const Program &program)
{
    for (const auto &entry : FOLDERS)
        if (program.categories.contains(QLatin1String(entry.category)))
            return QLatin1String(entry.folder);
    return QStringLiteral("Accessories");
}

QString usePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/native-programs.conf");
}

QString commandOutput(const QString &program, const QStringList &arguments)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(2000))
        return QString();
    return QString::fromUtf8(process.readAllStandardOutput()).trimmed();
}

} // namespace

namespace programs {

QMap<QString, QList<Program>> folders()
{
    QMap<QString, QList<Program>> out;
    const QHash<QString, QString> files = entryFiles();
    for (const QString &path : files)
        if (const auto program = readEntry(path))
            out[folderOf(*program)].append(*program);
    for (auto it = out.begin(); it != out.end(); ++it)
        std::sort(it->begin(), it->end(), [](const Program &a, const Program &b) {
            return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
        });
    return out;
}

std::optional<Program> find(const QString &id)
{
    const QString path = entryFiles().value(id);
    return path.isEmpty() ? std::nullopt : readEntry(path);
}

std::optional<Program> fromFile(const QString &path)
{
    const QHash<QString, QString> values = readEntryValues(path);
    if (values.value(QStringLiteral("Type")) != QLatin1String("Application"))
        return std::nullopt;
    Program program = toProgram(path, values);
    if (program.command.isEmpty())
        return std::nullopt;
    return program;
}

QIcon iconOf(const Program &program)
{
    if (program.icon.startsWith(QLatin1Char('/')))
        return QIcon(program.icon);
    return QIcon::fromTheme(program.icon);
}

void launch(const Program &program)
{
    QProcess::startDetached(program.command.first(), program.command.mid(1), QDir::homePath());
    QSettings uses(usePath(), QSettings::IniFormat);
    uses.setValue(program.id, uses.value(program.id, 0).toInt() + 1);
}

QList<Program> mostUsed(int count)
{
    QSettings uses(usePath(), QSettings::IniFormat);
    QStringList ids = uses.allKeys();
    std::sort(ids.begin(), ids.end(), [&](const QString &a, const QString &b) {
        return uses.value(a).toInt() > uses.value(b).toInt();
    });
    QList<Program> out;
    const QHash<QString, QString> files = entryFiles();
    for (const QString &id : std::as_const(ids)) {
        if (out.size() >= count)
            break;
        if (const auto program = readEntry(files.value(id)))
            out.append(*program);
    }
    return out;
}

std::optional<Program> defaultBrowser()
{
    // Asked once: these scripts take a moment, and the menu must open at once.
    static const QString id = commandOutput(QStringLiteral("xdg-settings"), {QStringLiteral("get"), QStringLiteral("default-web-browser")});
    return find(id);
}

std::optional<Program> defaultMail()
{
    static const QString id = commandOutput(QStringLiteral("xdg-mime"), {QStringLiteral("query"), QStringLiteral("default"), QStringLiteral("x-scheme-handler/mailto")});
    return find(id);
}

void useIconTheme()
{
    QStringList wanted;
    const QString chosen = w4w::Session::instance()->look().iconTheme;
    if (!chosen.isEmpty())
        wanted << chosen;
    const QSettings kde(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kdeglobals"), QSettings::IniFormat);
    const QString plasma = kde.value(QStringLiteral("Icons/Theme")).toString();
    if (!plasma.isEmpty())
        wanted << plasma;
    wanted << QStringLiteral("breeze") << QStringLiteral("Adwaita") << QStringLiteral("hicolor");
    const QStringList bases = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("icons"), QStandardPaths::LocateDirectory);
    for (const QString &name : std::as_const(wanted))
        for (const QString &base : bases)
            if (QFileInfo::exists(base + QLatin1Char('/') + name + QStringLiteral("/index.theme"))) {
                QIcon::setThemeName(name);
                return;
            }
}

void open(const QString &target)
{
    // Folders and XP's own places have their windows here; the rest, their programs.
    if (target == QLatin1String("file:///"))
        FileBrowser::open(places::Computer);
    else if (target == QLatin1String("trash:///"))
        FileBrowser::open(places::RecycleBin);
    else if (QFileInfo(target).isDir())
        FileBrowser::open(target);
    else
        QProcess::startDetached(QStringLiteral("xdg-open"), {target}, QDir::homePath());
}

} // namespace programs
