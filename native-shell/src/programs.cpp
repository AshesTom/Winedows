// SPDX-License-Identifier: LGPL-2.1-or-later
#include "programs.h"

#include "filebrowser.h"
#include "fileops.h"
#include "session.h"
#include "contexthelp.h"
#include "fileicons.h"
#include "xpapp.h"
#include "xpdialog.h"
#include "xpfiles.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QXmlStreamReader>
#include <QDir>
#include <QCheckBox>
#include <QDialog>
#include <QFileDialog>
#include <QTreeWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QMimeDatabase>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <cmath>

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
    program.file = path;
    const QString locale = QLocale().name();
    program.name = values.value(QStringLiteral("Name[%1]").arg(locale),
                                values.value(QStringLiteral("Name[%1]").arg(locale.section(QLatin1Char('_'), 0, 0)),
                                             values.value(QStringLiteral("Name"))));
    program.comment = values.value(QStringLiteral("Comment[%1]").arg(locale),
                                   values.value(QStringLiteral("Comment[%1]").arg(locale.section(QLatin1Char('_'), 0, 0)),
                                                values.value(QStringLiteral("Comment"))));
    program.icon = values.value(QStringLiteral("Icon"));
    program.categories = values.value(QStringLiteral("Categories")).split(QLatin1Char(';'), Qt::SkipEmptyParts);
    program.mimeTypes = values.value(QStringLiteral("MimeType")).split(QLatin1Char(';'), Qt::SkipEmptyParts);
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

// When each shortcut was last run, by its path (a key of its own: QSettings would take the path's
// slashes for groups)
QString runPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/native-runs.conf");
}

QString runKey(const QString &path)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(path));
}

// A launch count with the time it was last used: XP's UserAssist keeps both, and its Start menu
// ranks a program lower as that time recedes.
struct Use
{
    int count = 0;
    qint64 last = 0;   // ms since the epoch, 0 when only an old count was stored
};

Use readUse(const QSettings &uses, const QString &id)
{
    const QString text = uses.value(id).toString();
    const qsizetype separator = text.indexOf(QLatin1Char(';'));
    if (separator < 0)
        return {text.toInt(), 0};
    return {text.left(separator).toInt(), text.mid(separator + 1).toLongLong()};
}

double useScore(Use use, qint64 now)
{
    if (use.last <= 0 || now <= use.last)
        return use.count;
    // XP's shell32 halves a use's weight as it ages; this port uses a two-week half-life.
    return use.count * std::pow(0.5, double(now - use.last) / (14.0 * 24 * 60 * 60 * 1000));
}

void noteUse(const QString &id)
{
    QSettings uses(usePath(), QSettings::IniFormat);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const Use previous = readUse(uses, id);
    const double decayed = previous.last > 0 ? useScore(previous, now) : previous.count;
    const int count = qMax(1, qRound(decayed)) + 1;
    uses.setValue(id, QStringLiteral("%1;%2").arg(count).arg(now));
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

std::optional<Program> forWindowClass(const QString &wmClass)
{
    if (wmClass.isEmpty())
        return std::nullopt;
    // Its StartupWMClass first, then an entry or a command of the class's name, as docks match them.
    const QHash<QString, QString> files = entryFiles();
    std::optional<Program> byName;
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        const QHash<QString, QString> values = readEntryValues(it.value());
        if (values.value(QStringLiteral("Type")) != QLatin1String("Application"))
            continue;
        if (values.value(QStringLiteral("StartupWMClass")).compare(wmClass, Qt::CaseInsensitive) == 0)
            return toProgram(it.value(), values);
        if (byName)
            continue;
        const Program program = toProgram(it.value(), values);
        const QString base = QFileInfo(it.key()).completeBaseName();
        if (base.compare(wmClass, Qt::CaseInsensitive) == 0 || base.section(QLatin1Char('.'), -1).compare(wmClass, Qt::CaseInsensitive) == 0
            || (!program.command.isEmpty() && QFileInfo(program.command.first()).fileName().compare(wmClass, Qt::CaseInsensitive) == 0))
            byName = program;
    }
    return byName;
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
    noteUse(program.id);
    noteRun(program.file);
}

void noteRun(const QString &path)
{
    if (path.isEmpty())
        return;
    QSettings runs(runPath(), QSettings::IniFormat);
    runs.setValue(runKey(path), QDateTime::currentMSecsSinceEpoch());
}

QDateTime lastRun(const QString &path)
{
    const QSettings runs(runPath(), QSettings::IniFormat);
    const qint64 at = runs.value(runKey(path), 0).toLongLong();
    return at > 0 ? QDateTime::fromMSecsSinceEpoch(at) : QDateTime();
}

void launchWith(const Program &program, const QStringList &files)
{
    if (program.command.isEmpty())
        return;
    QStringList command = program.command;
    bool inserted = false;
    for (QString &argument : command) {
        if (argument == QLatin1String("%f") || argument == QLatin1String("%u")) {
            argument = files.value(0);
            inserted = true;
        } else if (argument == QLatin1String("%F") || argument == QLatin1String("%U")) {
            argument = files.join(QLatin1Char(' '));
            inserted = true;
        } else if (argument == QLatin1String("%%")) {
            argument = QStringLiteral("%");
        }
    }
    if (!inserted)
        command += files;
    QProcess::startDetached(command.first(), command.mid(1), QDir::homePath());
    noteUse(program.id);
}

QList<Program> allPrograms()
{
    QList<Program> all;
    QSet<QString> seen;
    for (const QList<Program> &group : folders())
        for (const Program &program : group)
            if (!seen.contains(program.id)) {
                seen.insert(program.id);
                all.append(program);
            }
    std::sort(all.begin(), all.end(), [](const Program &a, const Program &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return all;
}

namespace {

// shell32's Open With: dialog 1063 for a file of a known type, 1070 (with a description for the new
// type) for one of none
const int DIALOG_OPEN_WITH = 1063, DIALOG_OPEN_WITH_NEW = 1070, STRING_RECOMMENDED = 29952, STRING_OTHER = 29953;
enum { ID_FILE_ICON = 12297, ID_FILE = 13582, ID_PROGRAMS = 13829, ID_ALWAYS = 13577, ID_BROWSE = 13581, ID_DESCRIPTION = 13574 };
// its aHelpIds table in shell32 (0x7c9de730), with windows.hlp
const QList<QPair<int, int>> HELP_OPEN_WITH = {
    {12297, 4789}, {12291, 4789}, {13582, -1}, {13576, 4788}, {13574, 4788}, {13829, 4789}, {13577, 4794}, {13575, 4790},
    {13581, 4790}, {13585, 3001}};

// Qt's own list, without the user's shell32
std::optional<Program> chooseInList(const QString &path, QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Open With - %1").arg(QFileInfo(path).fileName()));
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(QObject::tr("Choose the program you want to use to open this file:"), &dialog));
    auto *list = new QListWidget(&dialog);
    const QList<Program> all = allPrograms();
    for (const Program &program : all) {
        auto *item = new QListWidgetItem(iconOf(program), program.name, list);
        item->setToolTip(program.comment);
    }
    layout->addWidget(list, 1);
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    auto *ok = new QPushButton(QObject::tr("OK"), &dialog);
    auto *cancel = new QPushButton(QObject::tr("Cancel"), &dialog);
    ok->setDefault(true);
    row->addWidget(ok);
    row->addWidget(cancel);
    layout->addLayout(row);
    std::optional<Program> chosen;
    QObject::connect(ok, &QPushButton::clicked, &dialog, [&] {
        const int index = list->currentRow();
        if (index >= 0 && index < all.size())
            chosen = all[index];
        dialog.accept();
    });
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog, [&] {
        const int index = list->currentRow();
        if (index >= 0 && index < all.size())
            chosen = all[index];
        dialog.accept();
    });
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    if (list->count())
        list->setCurrentRow(0);
    dialog.exec();
    return chosen;
}

bool fits(const Program &program, const QStringList &types)
{
    return std::any_of(types.cbegin(), types.cend(), [&](const QString &type) { return program.mimeTypes.contains(type); });
}

QStringList typesOf(const QString &path)
{
    const QMimeType type = QMimeDatabase().mimeTypeForFile(path);
    QStringList types = QStringList{type.name()} + type.allAncestors();
    // every file's last ancestor would recommend the programs that open any file for each
    if (!type.isDefault())
        types.removeAll(QStringLiteral("application/octet-stream"));
    return types;
}

} // namespace

QList<Program> recommendedFor(const QString &path)
{
    const QStringList types = typesOf(path);
    QList<Program> out;
    for (const Program &program : allPrograms())
        if (fits(program, types))
            out.append(program);
    return out;
}

std::optional<Program> chooseProgram(const QString &path, QWidget *parent, bool alwaysFixed)
{
    const auto shell32 = xpfiles::resources(xpfiles::find(QStringLiteral("shell32.dll")));
    if (!shell32)
        return chooseInList(path, parent);
    const QMimeType type = QMimeDatabase().mimeTypeForFile(path);
    const bool unknown = type.isDefault();
    QDialog dialog(parent);
    dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, true);
    const xpdialog::Page page = xpdialog::build(*shell32, unknown ? DIALOG_OPEN_WITH_NEW : DIALOG_OPEN_WITH, &dialog);
    if (page.controls.isEmpty())
        return chooseInList(path, parent);
    dialog.setWindowTitle(page.caption);
    contexthelp::attach(page, HELP_OPEN_WITH);
    if (auto *icon = qobject_cast<QLabel *>(page.controls.value(ID_FILE_ICON)))
        icon->setPixmap(fileicons::forFile(QFileInfo(path)).pixmap(32, 32));
    if (auto *file = qobject_cast<QLabel *>(page.controls.value(ID_FILE)))
        file->setText(QFileInfo(path).fileName());
    // The programs: a tree view of two parents, Recommended Programs (those whose MimeType holds the
    // file's type or one it comes from) and Other Programs, without lines or buttons (the template's
    // TVS_SHOWSELALWAYS only), each program with its small icon
    QWidget *place = page.controls.value(ID_PROGRAMS);
    // (without TVS_HASLINES or TVS_HASBUTTONS: no branches drawn)
    struct Tree : QTreeWidget
    {
        using QTreeWidget::QTreeWidget;
        void drawBranches(QPainter *, const QRect &, const QModelIndex &) const override {}
    };
    auto *tree = new Tree(&dialog);
    if (place) {
        tree->setGeometry(place->geometry());
        place->hide();
    }
    tree->setHeaderHidden(true);
    tree->setRootIsDecorated(false);
    tree->setIconSize(QSize(16, 16));
    tree->setIndentation(19);
    const QStringList types = typesOf(path);
    QList<Program> programs = allPrograms();
    const auto parent_ = [&](int id, const char *fallback) {
        auto *item = new QTreeWidgetItem(tree, {xpfiles::shellString(id, fallback)});
        item->setFlags(Qt::ItemIsEnabled);
        item->setExpanded(true);
        return item;
    };
    QTreeWidgetItem *recommended = parent_(STRING_RECOMMENDED, "Recommended Programs:");
    QTreeWidgetItem *other = parent_(STRING_OTHER, "Other Programs:");
    QList<QTreeWidgetItem *> firsts;
    for (qsizetype i = 0; i < programs.size(); ++i) {
        const Program &program = programs.at(i);
        auto *item = new QTreeWidgetItem(fits(program, types) ? recommended : other, {program.name});
        item->setIcon(0, iconOf(program));
        item->setData(0, Qt::UserRole, int(i));
        item->setToolTip(0, program.comment);
    }
    // XP leaves out an empty parent
    if (!recommended->childCount()) {
        delete recommended;
        recommended = nullptr;
    }
    if (!other->childCount()) {
        delete other;
        other = nullptr;
    }
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        tree->topLevelItem(i)->setExpanded(true);
    if (tree->topLevelItemCount() && tree->topLevelItem(0)->childCount())
        tree->setCurrentItem(tree->topLevelItem(0)->child(0));
    tree->show();
    // Always use: on for a type of none, and fixed on from a file's Change...
    auto *always = qobject_cast<QCheckBox *>(page.controls.value(ID_ALWAYS));
    if (always) {
        always->setChecked(unknown || alwaysFixed);
        always->setEnabled(!alwaysFixed);
    }
    std::optional<Program> chosen;
    const auto accept = [&] {
        QTreeWidgetItem *item = tree->currentItem();
        const QVariant index = item ? item->data(0, Qt::UserRole) : QVariant();
        if (!index.isValid())
            return;
        chosen = programs.value(index.toInt());
        dialog.accept();
    };
    if (auto *ok = qobject_cast<QPushButton *>(page.controls.value(1))) {
        ok->setDefault(true);
        QObject::connect(ok, &QPushButton::clicked, &dialog, accept);
    }
    if (auto *cancel = qobject_cast<QPushButton *>(page.controls.value(2)))
        QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    QObject::connect(tree, &QTreeWidget::itemDoubleClicked, &dialog, accept);
    // Browse...: a program file, then in Other Programs and chosen
    if (auto *browse = qobject_cast<QPushButton *>(page.controls.value(ID_BROWSE)))
        QObject::connect(browse, &QPushButton::clicked, &dialog, [&] {
            const QString file = QFileDialog::getOpenFileName(&dialog, browse->text().remove(QLatin1Char('&')).remove(QStringLiteral("...")),
                                                              QStringLiteral("/usr/bin"));
            if (file.isEmpty())
                return;
            Program program;
            program.name = QFileInfo(file).fileName();
            program.command = {file};
            programs.append(program);
            if (!other)
                other = parent_(STRING_OTHER, "Other Programs:");
            auto *item = new QTreeWidgetItem(other, {program.name});
            item->setData(0, Qt::UserRole, int(programs.size() - 1));
            other->setExpanded(true);
            tree->setCurrentItem(item);
        });
    dialog.exec();
    if (chosen && always && always->isChecked() && !alwaysFixed && !chosen->id.isEmpty()) {
        if (!unknown) {
            setDefaultFor(path, *chosen);
        } else if (const QString suffix = QFileInfo(path).suffix().toLower(); !suffix.isEmpty()) {
            // XP makes a type of the extension, named by the description (its "XYZQ File" without
            // one): a shared-mime-info type of the user's for it, then the program its default
            auto *description = qobject_cast<QLineEdit *>(page.controls.value(ID_DESCRIPTION));
            const QString name = description && !description->text().trimmed().isEmpty()
                ? description->text().trimmed() : QObject::tr("%1 File").arg(suffix.toUpper());
            const QString type = QStringLiteral("application/x-win4wine-") + suffix;
            const QString mime = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/mime");
            QDir().mkpath(mime + QStringLiteral("/packages"));
            QFile file(mime + QStringLiteral("/packages/win4wine-") + suffix + QStringLiteral(".xml"));
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                file.write(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                          "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
                                          "  <mime-type type=\"%1\">\n    <comment>%2</comment>\n    <glob pattern=\"*.%3\"/>\n"
                                          "  </mime-type>\n</mime-info>\n")
                               .arg(type, name.toHtmlEscaped(), suffix.toHtmlEscaped()).toUtf8());
                file.close();
                QProcess::execute(QStringLiteral("update-mime-database"), {mime});
                QProcess::startDetached(QStringLiteral("xdg-mime"), {QStringLiteral("default"), chosen->id, type});
            }
        }
    }
    return chosen;
}

void setDefaultFor(const QString &path, const Program &program)
{
    const QString mime = QMimeDatabase().mimeTypeForFile(path).name();
    if (!mime.isEmpty() && !program.id.isEmpty())
        QProcess::startDetached(QStringLiteral("xdg-mime"), {QStringLiteral("default"), program.id, mime});
}

void removeUse(const QString &id)
{
    QSettings uses(usePath(), QSettings::IniFormat);
    uses.remove(id);
}

void clearUses()
{
    QSettings uses(usePath(), QSettings::IniFormat);
    uses.clear();
}

QList<Program> mostUsed(int count)
{
    QSettings uses(usePath(), QSettings::IniFormat);
    QStringList ids = uses.allKeys();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    std::sort(ids.begin(), ids.end(), [&](const QString &a, const QString &b) {
        const Use left = readUse(uses, a), right = readUse(uses, b);
        const double leftScore = useScore(left, now), rightScore = useScore(right, now);
        return leftScore != rightScore ? leftScore > rightScore : left.last > right.last;
    });
    QList<Program> out;
    const QHash<QString, QString> files = entryFiles();
    const auto add = [&](const std::optional<Program> &program) {
        if (!program || out.size() >= count)
            return;
        for (const Program &already : std::as_const(out))
            if (already.id == program->id)
                return;
        out.append(*program);
    };
    for (const QString &id : std::as_const(ids))
        add(readEntry(files.value(id)));
    if (out.size() >= count)
        return out;

    // Programs by the name they run as
    QHash<QString, Program> byCommand;
    for (const QString &file : files) {
        const auto program = readEntry(file);
        if (program && !program->command.isEmpty())
            byCommand.insert(QFileInfo(program->command.first()).fileName(), *program);
    }
    // What the desktop saw used lately, most recent first: its list of recent files says with what
    QList<QPair<QString, QString>> recent;
    QFile list(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/recently-used.xbel"));
    if (list.open(QIODevice::ReadOnly)) {
        QXmlStreamReader xml(&list);
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.qualifiedName() == QLatin1String("bookmark:application")) {
                const QString exec = xml.attributes().value(QLatin1String("exec")).toString().remove(QLatin1Char(0x27));
                recent.append({xml.attributes().value(QLatin1String("modified")).toString(), QFileInfo(exec.section(QLatin1Char(0x20), 0, 0)).fileName()});
            }
        }
    }
    std::sort(recent.begin(), recent.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    for (const auto &entry : std::as_const(recent))
        if (const auto found = byCommand.constFind(entry.second); found != byCommand.constEnd())
            add(*found);
    // A new account: the everyday programs that are installed, as XP starts with its own
    for (const char *name : {"konsole", "gnome-terminal", "xfce4-terminal", "lxterminal", "kate", "gedit", "gnome-text-editor", "mousepad", "dolphin", "nautilus", "thunar", "kcalc", "gnome-calculator", "galculator", "gwenview", "vlc", "xterm"})
        if (const auto found = byCommand.constFind(QLatin1String(name)); found != byCommand.constEnd())
            add(*found);
    // Still room: the rest, by name, so that the column is full as XP's is
    QList<Program> rest = byCommand.values();
    std::sort(rest.begin(), rest.end(), [](const Program &a, const Program &b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
    for (const Program &program : std::as_const(rest))
        add(program);
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

void openCommandPrompt(const QString &folder)
{
    // A program of its own: what runs in it cannot take the shell down.
    QProcess::startDetached(xpapp::sibling(QStringLiteral("cmd")), {folder}, folder);
}

void openNotepad(const QString &file)
{
    QStringList arguments;
    if (!file.isEmpty())
        arguments << QStringLiteral("--") << file;
    QProcess::startDetached(xpapp::sibling(QStringLiteral("notepad")), arguments, QDir::homePath());
}

void openCalculator()
{
    QProcess::startDetached(xpapp::sibling(QStringLiteral("calc")), {}, QDir::homePath());
}

void openMediaPlayer(const QStringList &files)
{
    // wmplayer, beside the shell
    const QString program = xpapp::sibling(QStringLiteral("wmplayer"));
    QStringList arguments;
    if (!files.isEmpty())
        arguments << QStringLiteral("--") << files;
    QProcess::startDetached(program, arguments, QDir::homePath());
}

void open(const QString &target)
{
    // A Windows shortcut opens what it points at.
    QString resolved = target;
    if (QFileInfo(target).suffix().compare(QLatin1String("lnk"), Qt::CaseInsensitive) == 0) {
        const QString shortcut = fileops::shortcutTarget(target);
        if (!shortcut.isEmpty())
            resolved = shortcut;
    }
    // Folders and XP's own places have their windows here; the rest, their programs.
    if (target == QLatin1String("file:///"))
        FileBrowser::open(places::Computer);
    else if (target == QLatin1String("trash:///"))
        FileBrowser::open(places::RecycleBin);
    else if (QFileInfo(resolved).isDir())
        FileBrowser::open(resolved);
    else
        QProcess::startDetached(QStringLiteral("xdg-open"), {resolved}, QDir::homePath());
}

} // namespace programs
