// SPDX-License-Identifier: LGPL-2.1-or-later
#include "fileproperties.h"

#include "fileicons.h"
#include "programs.h"
#include "theme.h"
#include "contexthelp.h"
#include "xpdialog.h"
#include "xpfiles.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMimeDatabase>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QtEndian>

#include <array>
#include <memory>
#include <cmath>
#include <sys/stat.h>

namespace fileproperties {
namespace {

// shell32's templates and their controls
const int DIALOG_FILE = 1041, DIALOG_SEVERAL = 1042, DIALOG_VERSION = 1043, DIALOG_FOLDER = 1044;

// The aHelpIds tables in shell32.dll for its dialogs, shown with windows.hlp (WinHelpW without a
// file) on WM_HELP and WM_CONTEXTMENU: each control and its help number, -1 for none.
const QList<QPair<int, int>> HELP_FILE = {   // 0x7c9c29e8, dialog 1041 (with a flag of the page set, 0x7c9c3a28: 13075 then has 4867)
    {13095, -1}, {13096, -1}, {13097, -1}, {13057, 4779}, {13156, 4708}, {13155, 475102}, {13080, 4709},
    {13059, 4709}, {13152, 4857}, {13153, 4857}, {13089, 4711}, {13065, 4711}, {13081, 4710}, {13064, 4710},
    {13106, 4702}, {13107, 4702}, {13088, 4781}, {14113, 4781}, {13092, 4747}, {13072, 4747}, {13093, 4713},
    {13073, 4713}, {13094, 4714}, {13074, 4714}, {13091, 28548}, {13075, 4715}, {13076, 4717}, {13077, 4716},
    {13154, 4858}, {14425, 4865}, {13111, 4866}, {14423, 4866}};
const QList<QPair<int, int>> HELP_FOLDER = {   // 0x7c9e2918, dialogs 1042 and 1044
    {13095, -1}, {13096, -1}, {13057, 4779}, {14113, 4780}, {13080, 4709}, {13059, 4709}, {13089, 4711},
    {13065, 4711}, {13081, 4710}, {13064, 4710}, {13106, 4702}, {13107, 4702}, {13091, 28548}, {13075, 4715},
    {13076, 4717}, {13077, 4716}, {13154, 4858}};
const QList<QPair<int, int>> HELP_VERSION = {   // 0x7c9dd400, dialog 1043
    {13139, 4720}, {13140, 4720}, {13141, 4720}, {13136, 4700}, {13137, 4700}, {13138, 4700}};
const QList<QPair<int, int>> HELP_ATTRIBUTES = {   // 0x7c9e28a8, dialogs 1054 and 1055
    {13057, -1}, {13157, -1}, {13166, -1}, {13077, 4716}, {13158, 4859}, {13105, 4701}, {13159, 4860},
    {13154, 4706}};
const int ICON = 13057, NAME = 13156, COUNT = 14113, TYPE = 13059, OPENS_ICON = 13108, OPENS = 13153, CHANGE = 13155, LOCATION = 13065,
          SIZE = 13064, ON_DISK = 13106, CONTAINS = 14113, CREATED = 13072, MODIFIED = 13073, ACCESSED = 13074, READ_ONLY = 13075,
          HIDDEN = 13076, ARCHIVE = 13077, ADVANCED = 13154;
const int VERSION_FILE = 13139, VERSION_DESCRIPTION = 13140, VERSION_COPYRIGHT = 13141, VERSION_ITEMS = 13137, VERSION_VALUE = 13138;
// shell32's words: "%1 (%2 bytes)", "%1 Files, %2 Folders", Multiple Types, "All in ", Unknown application,
// File Folder's type, and the version items' names
const int STRING_BYTES = 4118, STRING_SMALL_BYTES = 4113, STRING_CONTAINS = 8711, STRING_MULTIPLE = 8705, STRING_ALL_IN = 8706,
          STRING_UNKNOWN_APP = 28956;
struct VersionKey
{
    const char *key;
    int name;
};
// The version page's list (a sorted list box): shell32's table, FileDescription and LegalCopyright being
// above it
const VersionKey VERSION_KEYS[] = {{"Comments", 8752}, {"CompanyName", 8753}, {"InternalName", 8755}, {"LegalTrademarks", 8756},
                                   {"OriginalFilename", 8757}, {"PrivateBuild", 8758}, {"ProductName", 8759}, {"ProductVersion", 8760},
                                   {"SpecialBuild", 8761}, {"FileVersion", 8765}};
const int STRING_LANGUAGE = 8763;

const std::optional<w4w::PeResources> &shell32()
{
    static const auto resources = xpfiles::resources(xpfiles::find(QStringLiteral("shell32.dll")));
    return resources;
}

QString text(int id, const char *fallback)
{
    const QString value = shell32() ? xpfiles::string(*shell32(), id) : QString();
    return value.isEmpty() ? QLatin1String(fallback) : value;
}

// FormatMessage's %1!ls!: the inserts in order
QString format(QString pattern, const QStringList &inserts)
{
    pattern.replace(QRegularExpression(QStringLiteral("%(\\d)!l?s!")), QStringLiteral("%\\1"));
    for (const QString &insert : inserts)
        pattern = pattern.arg(insert);
    return pattern;
}

// StrFormatByteSize's "1.00 MB": three digits, the rest dropped
QString shortSize(qint64 bytes)
{
    if (bytes < 1024)
        return format(text(STRING_SMALL_BYTES, "%s bytes").replace(QLatin1String("%s"), QLatin1String("%1")), {QLocale().toString(bytes)});
    double value = bytes;
    int unit = -1;
    while (value >= 1024 && unit < 3) {
        value /= 1024;
        ++unit;
    }
    const int decimals = value < 10 ? 2 : value < 100 ? 1 : 0;
    const double scale = std::pow(10, decimals);
    return QStringLiteral("%1 %2").arg(QLocale().toString(std::floor(value * scale) / scale, 'f', decimals), QLatin1String(std::array{"KB", "MB", "GB", "TB"}[unit]));
}

QString sizeLine(qint64 bytes)
{
    return format(text(STRING_BYTES, "%1!ls! (%2!ls! bytes)"), {shortSize(bytes), QLocale().toString(bytes)});
}

// What the file takes on the disk: its blocks, as XP counts its clusters
qint64 onDisk(const QString &path)
{
    struct stat info;
    if (::lstat(QFile::encodeName(path).constData(), &info) != 0)
        return 0;
    return qint64(info.st_blocks) * 512;
}

// GetDateFormat's long date and GetTimeFormat's time: "Saturday, October 31, 2026, 4:01:01 PM", without a zone
QString dateLine(const QDateTime &time)
{
    const QLocale locale;
    QString timeFormat = locale.timeFormat(QLocale::LongFormat);
    timeFormat.remove(QRegularExpression(QStringLiteral("\\s*t+")));
    return locale.toString(time.date(), QLocale::LongFormat) + QLatin1String(", ") + locale.toString(time.time(), timeFormat.trimmed());
}

// The application xdg-mime opens the file with, or none
std::optional<programs::Program> opensWith(const QFileInfo &file)
{
    const QString mime = QMimeDatabase().mimeTypeForFile(file).name();
    QProcess query;
    query.start(QStringLiteral("xdg-mime"), {QStringLiteral("query"), QStringLiteral("default"), mime});
    if (!query.waitForFinished(2000))
        return std::nullopt;
    const QString id = QString::fromUtf8(query.readAllStandardOutput()).trimmed();
    return id.isEmpty() ? std::nullopt : programs::find(id);
}

// The sizes and counts of folders, added up little by little
class Counter : public QObject
{
public:
    Counter(const QStringList &paths, QLabel *size, QLabel *disk, QLabel *contains)
        : QObject(size)
        , m_pending(paths)
        , m_size(size)
        , m_disk(disk)
        , m_contains(contains)
    {
        connect(&m_timer, &QTimer::timeout, this, &Counter::step);
        m_timer.start(0);
    }

private:
    void step()
    {
        for (int i = 0; i < 200 && !m_pending.isEmpty(); ++i) {
            const QFileInfo info(m_pending.takeLast());
            if (info.isDir() && !info.isSymLink()) {
                ++m_folders;
                const QFileInfoList children = QDir(info.absoluteFilePath()).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
                for (const QFileInfo &child : children)
                    m_pending.append(child.absoluteFilePath());
            } else {
                ++m_files;
                m_bytes += info.size();
                m_onDisk += onDisk(info.absoluteFilePath());
            }
        }
        if (m_size)
            m_size->setText(sizeLine(m_bytes));
        if (m_disk)
            m_disk->setText(sizeLine(m_onDisk));
        if (m_contains)
            m_contains->setText(format(text(STRING_CONTAINS, "%1!ls! Files, %2!ls! Folders"), {QLocale().toString(m_files), QLocale().toString(m_folders - m_roots)}));
        if (m_pending.isEmpty())
            m_timer.stop();
    }

public:
    int m_roots = 0;   // the folders asked about, which the count leaves out

private:
    QTimer m_timer;
    QStringList m_pending;
    QPointer<QLabel> m_size, m_disk, m_contains;
    qint64 m_bytes = 0, m_onDisk = 0;
    int m_files = 0, m_folders = 0;
};

// VS_VERSIONINFO: the fixed file version and the first string table's values
struct VersionInfo
{
    QString fileVersion;
    QHash<QString, QString> strings;
    quint16 language = 0;
};

std::optional<VersionInfo> readVersion(const QString &path)
{
    const auto pe = xpfiles::resources(path);
    if (!pe)
        return std::nullopt;
    const QByteArray data = pe->find(QStringLiteral("16"), QStringLiteral("1"));
    if (data.size() < 92)
        return std::nullopt;
    const auto u16 = [&](qsizetype at) { return at + 2 <= data.size() ? qFromLittleEndian<quint16>(data.constData() + at) : quint16(0); };
    const auto u32 = [&](qsizetype at) { return at + 4 <= data.size() ? qFromLittleEndian<quint32>(data.constData() + at) : quint32(0); };
    const auto key = [&](qsizetype at, qsizetype *end) {
        QString out;
        for (; at + 2 <= data.size() && u16(at); at += 2)
            out += QChar(u16(at));
        *end = (at + 2 + 3) & ~qsizetype(3);
        return out;
    };
    VersionInfo info;
    qsizetype at = 0;
    key(6, &at);
    if (u32(at) != 0xfeef04bd)
        return std::nullopt;
    const quint32 ms = u32(at + 8), ls = u32(at + 12);
    info.fileVersion = QStringLiteral("%1.%2.%3.%4").arg(ms >> 16).arg(ms & 0xffff).arg(ls >> 16).arg(ls & 0xffff);
    at = (at + u16(2) + 3) & ~qsizetype(3);
    const qsizetype total = u16(0);
    // Children: StringFileInfo (StringTables of Strings) and VarFileInfo (Translation)
    while (at + 6 < total) {
        const qsizetype length = u16(at);
        if (!length)
            break;
        qsizetype body = 0;
        const QString name = key(at + 6, &body);
        if (name == QLatin1String("StringFileInfo")) {
            qsizetype table = body;
            const qsizetype tableLength = u16(table);
            qsizetype entry = 0;
            const QString tableName = key(table + 6, &entry);
            info.language = quint16(tableName.left(4).toUInt(nullptr, 16));
            while (entry < table + tableLength) {
                const qsizetype entryLength = u16(entry);
                if (!entryLength)
                    break;
                qsizetype value = 0;
                const QString entryKey = key(entry + 6, &value);
                QString words;
                for (qsizetype c = value; c + 2 <= entry + entryLength && u16(c); c += 2)
                    words += QChar(u16(c));
                info.strings.insert(entryKey, words.trimmed());
                entry = (entry + entryLength + 3) & ~qsizetype(3);
            }
        }
        at = (at + length + 3) & ~qsizetype(3);
    }
    return info;
}

// VerLanguageName's words for a language ID: "English (United States)"
QString languageName(quint16 id)
{
    static const QHash<int, QLocale::Language> primary = {
        {0x09, QLocale::English}, {0x0c, QLocale::French}, {0x07, QLocale::German}, {0x10, QLocale::Italian}, {0x0a, QLocale::Spanish},
        {0x11, QLocale::Japanese}, {0x04, QLocale::Chinese}, {0x12, QLocale::Korean}, {0x13, QLocale::Dutch}, {0x16, QLocale::Portuguese},
        {0x19, QLocale::Russian}, {0x15, QLocale::Polish}, {0x1d, QLocale::Swedish}, {0x00, QLocale::AnyLanguage}};
    if ((id & 0x3ff) == 0)
        return QStringLiteral("Language Neutral");
    static const QHash<int, QLocale::Territory> territories = {
        {0x0409, QLocale::UnitedStates}, {0x0809, QLocale::UnitedKingdom}, {0x040c, QLocale::France}, {0x0407, QLocale::Germany},
        {0x0410, QLocale::Italy}, {0x0c0a, QLocale::Spain}, {0x040a, QLocale::Spain}, {0x0411, QLocale::Japan}, {0x0804, QLocale::China},
        {0x0404, QLocale::Taiwan}, {0x0412, QLocale::SouthKorea}, {0x0413, QLocale::Netherlands}, {0x0416, QLocale::Brazil},
        {0x0816, QLocale::Portugal}, {0x0419, QLocale::Russia}, {0x0415, QLocale::Poland}, {0x041d, QLocale::Sweden}};
    const auto language = primary.constFind(id & 0x3ff);
    if (language == primary.constEnd())
        return QStringLiteral("0x%1").arg(id, 4, 16, QLatin1Char('0'));
    const QString name = QLocale::languageToString(*language);
    const auto territory = territories.constFind(id);
    return territory == territories.constEnd() ? name : QStringLiteral("%1 (%2)").arg(name, QLocale::territoryToString(*territory));
}

QWidget *versionPage(const VersionInfo &info, QString *caption)
{
    auto *page = new QWidget;
    const xpdialog::Page dialog = xpdialog::build(*shell32(), DIALOG_VERSION, page);
    contexthelp::attach(dialog, HELP_VERSION);
    *caption = dialog.caption;
    const auto set = [&](int id, const QString &value) {
        if (auto *label = qobject_cast<QLabel *>(dialog.controls.value(id)))
            label->setText(value);
        else if (auto *edit = qobject_cast<QLineEdit *>(dialog.controls.value(id)))
            edit->setText(value);
    };
    set(VERSION_FILE, info.fileVersion);
    set(VERSION_DESCRIPTION, info.strings.value(QStringLiteral("FileDescription")));
    set(VERSION_COPYRIGHT, info.strings.value(QStringLiteral("LegalCopyright")));
    // The list box and the value's multiline edit, in their places
    QWidget *listPlace = dialog.controls.value(VERSION_ITEMS), *valuePlace = dialog.controls.value(VERSION_VALUE);
    if (!listPlace || !valuePlace)
        return page;
    auto *list = new QListWidget(page);
    list->setGeometry(listPlace->geometry());
    listPlace->hide();
    auto *value = new QPlainTextEdit(page);
    value->setGeometry(valuePlace->geometry());
    value->setReadOnly(true);
    valuePlace->hide();
    QList<QPair<QString, QString>> items;
    for (const VersionKey &entry : VERSION_KEYS)
        if (const QString words = info.strings.value(QLatin1String(entry.key)); !words.isEmpty())
            items.append({text(entry.name, entry.key), words});
    items.append({text(STRING_LANGUAGE, "Language"), languageName(info.language)});
    std::sort(items.begin(), items.end(), [](const auto &a, const auto &b) { return a.first.compare(b.first, Qt::CaseInsensitive) < 0; });
    for (const auto &[name, words] : std::as_const(items)) {
        auto *row = new QListWidgetItem(name, list);
        row->setData(Qt::UserRole, words);
    }
    QObject::connect(list, &QListWidget::currentItemChanged, value, [value](QListWidgetItem *row) {
        value->setPlainText(row ? row->data(Qt::UserRole).toString() : QString());
    });
    list->setCurrentRow(0);
    return page;
}

// shell32's Advanced Attributes (dialogs 1054 a file, 1055 a folder): the archive, index, compress and
// encrypt attributes have no Linux equivalent here, so they are shown greyed; the dialog is XP's.
void showAdvanced(QWidget *parent, bool folder)
{
    if (!shell32())
        return;
    QDialog dialog(parent);
    const xpdialog::Page page = xpdialog::build(*shell32(), folder ? 1055 : 1054, &dialog);
    contexthelp::attach(page, HELP_ATTRIBUTES);
    dialog.setWindowTitle(page.caption);
    for (QWidget *control : std::as_const(page.controls))
        if (qobject_cast<QCheckBox *>(control) || (qobject_cast<QPushButton *>(control) && control != page.controls.value(1) && control != page.controls.value(2)))
            control->setEnabled(false);
    if (auto *ok = qobject_cast<QPushButton *>(page.controls.value(1)))
        QObject::connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    if (auto *cancel = qobject_cast<QPushButton *>(page.controls.value(2)))
        QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    dialog.exec();
}

} // namespace

void show(const QStringList &paths, QWidget *parent)
{
    if (paths.isEmpty() || !shell32())
        return;
    const QFileInfo first(paths.first());
    const bool one = paths.size() == 1, folder = one && first.isDir();
    auto *dialog = new QDialog(parent);
    // XP's sheets have no caption icon.
    dialog->setWindowIcon(QIcon());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    auto *tabs = new xpdialog::SheetTabs(dialog);
    auto *general = new QWidget;
    const xpdialog::Page page = xpdialog::build(*shell32(), one ? (folder ? DIALOG_FOLDER : DIALOG_FILE) : DIALOG_SEVERAL, general);
    contexthelp::attach(page, one && !folder ? HELP_FILE : HELP_FOLDER);
    const auto label = [&](int id) { return qobject_cast<QLabel *>(page.controls.value(id)); };
    const auto set = [&](int id, const QString &value) {
        if (QLabel *line = label(id))
            line->setText(value);
    };

    if (QLabel *icon = label(ICON))
        icon->setPixmap(fileicons::forFile(first).pixmap(32, 32));
    auto *name = qobject_cast<QLineEdit *>(page.controls.value(NAME));
    if (name)
        name->setText(first.fileName());
    if (!one)
        set(COUNT, format(text(STRING_CONTAINS, "%1!ls! Files, %2!ls! Folders"), {QString::number(std::count_if(paths.cbegin(), paths.cend(), [](const QString &p) { return !QFileInfo(p).isDir(); })),
                                                                                  QString::number(std::count_if(paths.cbegin(), paths.cend(), [](const QString &p) { return QFileInfo(p).isDir(); }))}));

    // Type: one type for all, else Multiple Types; Location: "All in" their folder
    QString type = fileicons::typeName(first);
    for (const QString &path : paths)
        if (fileicons::typeName(QFileInfo(path)) != type)
            type = text(STRING_MULTIPLE, "Multiple Types");
    set(TYPE, type);
    bool sameFolder = true;
    for (const QString &path : paths)
        sameFolder = sameFolder && QFileInfo(path).absolutePath() == first.absolutePath();
    set(LOCATION, one ? first.absolutePath() : (sameFolder ? text(STRING_ALL_IN, "All in ") + first.absolutePath() : QString()));

    // Opens with: the default application, or Unknown application without its picture
    if (one && !folder) {
        const auto program = opensWith(first);
        set(OPENS, program ? program->name : text(STRING_UNKNOWN_APP, "Unknown application"));
        if (QLabel *icon = label(OPENS_ICON)) {
            icon->setVisible(bool(program));
            if (program)
                icon->setPixmap(programs::iconOf(*program).pixmap(16, 16));
        }
        if (QLabel *opens = label(OPENS); opens && !program)
            opens->move(page.controls.value(OPENS_ICON) ? page.controls.value(OPENS_ICON)->x() : opens->x(), opens->y());
        // Change... is XP's Open With (shell32's PickIcon/open-with dialog here): it makes the
        // chosen program the MIME type's default and updates the Opens with line.
        if (auto *change = qobject_cast<QPushButton *>(page.controls.value(CHANGE))) {
            change->setEnabled(true);
            QLabel *opens = label(OPENS), *opensIcon = label(OPENS_ICON);
            QObject::connect(change, &QPushButton::clicked, dialog, [dialog, path = first.absoluteFilePath(), opens, opensIcon] {
                const auto chosen = programs::chooseProgram(path, dialog, true);
                if (!chosen)
                    return;
                if (opens)
                    opens->setText(chosen->name);
                if (opensIcon) {
                    opensIcon->setVisible(true);
                    opensIcon->setPixmap(programs::iconOf(*chosen).pixmap(16, 16));
                }
                programs::setDefaultFor(path, *chosen);
            });
        }
    }

    if (one && !folder) {
        set(SIZE, sizeLine(first.size()));
        set(ON_DISK, sizeLine(onDisk(first.absoluteFilePath())));
    } else {
        auto *counter = new Counter(paths, label(SIZE), label(ON_DISK), one ? label(CONTAINS) : nullptr);
        counter->m_roots = int(std::count_if(paths.cbegin(), paths.cend(), [](const QString &p) { return QFileInfo(p).isDir(); }));
    }
    if (one) {
        set(CREATED, dateLine(first.birthTime().isValid() ? first.birthTime() : first.metadataChangeTime()));
        set(MODIFIED, dateLine(first.lastModified()));
        set(ACCESSED, dateLine(first.lastRead()));
    }

    // Attributes: Read-only is the lack of write permission and Hidden a name starting with a dot; a
    // Linux file system has no archive bit (XP greys what a volume cannot have) and no Advanced attributes.
    auto *readOnly = qobject_cast<QAbstractButton *>(page.controls.value(READ_ONLY));
    bool allReadOnly = true;
    for (const QString &path : paths)
        allReadOnly = allReadOnly && !QFileInfo(path).isWritable();
    if (readOnly)
        readOnly->setChecked(allReadOnly);
    if (auto *hidden = qobject_cast<QAbstractButton *>(page.controls.value(HIDDEN))) {
        hidden->setChecked(std::all_of(paths.cbegin(), paths.cend(), [](const QString &p) { return QFileInfo(p).fileName().startsWith(QLatin1Char('.')); }));
        hidden->setEnabled(false);
    }
    if (QWidget *archive = page.controls.value(ARCHIVE))
        archive->setEnabled(false);
    if (auto *advanced = qobject_cast<QPushButton *>(page.controls.value(ADVANCED)); advanced && one) {
        advanced->show();
        advanced->setEnabled(true);
        QObject::connect(advanced, &QPushButton::clicked, dialog, [dialog, folder] { showAdvanced(dialog, folder); });
    }

    tabs->addTab(general, page.caption.isEmpty() ? QObject::tr("General") : page.caption);
    if (one && !folder)
        if (const auto version = readVersion(first.absoluteFilePath())) {
            QString caption;
            QWidget *versionWidget = versionPage(*version, &caption);
            tabs->addTab(versionWidget, caption.isEmpty() ? QObject::tr("Version") : caption);
        }

    if (one && !folder) {
        // XP's Summary page is a document page; here its fields are the file's, with the document
        // properties left empty where Linux has no equivalent.
        auto *summary = new QWidget;
        auto *form = new QFormLayout(summary);
        form->setContentsMargins(6, 6, 6, 6);
        const auto row = [&](const QString &name, const QString &value) { form->addRow(name, new QLabel(value)); };
        row(QObject::tr("Title"), first.completeBaseName());
        row(QObject::tr("Subject"), QString());
        row(QObject::tr("Author"), first.owner());
        row(QObject::tr("Keywords"), QString());
        row(QObject::tr("Comments"), QString());
        row(QObject::tr("Size"), sizeLine(first.size()));
        row(QObject::tr("Created"), dateLine(first.birthTime().isValid() ? first.birthTime() : first.metadataChangeTime()));
        row(QObject::tr("Modified"), dateLine(first.lastModified()));
        row(QObject::tr("Location"), first.absolutePath());
        tabs->addTab(summary, QObject::tr("Summary"));
    }
    dialog->setWindowTitle(QObject::tr("%1 Properties").arg(one ? first.fileName() : QStringLiteral("%1, ...").arg(first.fileName())));
    auto *ok = new QPushButton(QObject::tr("OK"), dialog);
    auto *cancel = new QPushButton(QObject::tr("Cancel"), dialog);
    auto *apply = new QPushButton(QObject::tr("&Apply"), dialog);
    ok->setDefault(true);
    xpdialog::arrangeSheet(dialog, tabs, {ok, cancel, apply}, ShellTheme::instance()->scaled(58));
    apply->setEnabled(false);
    const auto changed = [apply] { apply->setEnabled(true); };
    if (name)
        QObject::connect(name, &QLineEdit::textEdited, dialog, changed);
    if (readOnly)
        QObject::connect(readOnly, &QAbstractButton::toggled, dialog, changed);
    // A rename, and Read-only as the owner's, group's and others' write permissions
    // What the sheet stands for, which Apply changes
    struct State
    {
        QStringList paths;
        bool readOnly;
    };
    auto state = std::make_shared<State>(State{paths, allReadOnly});
    const auto applyChanges = [state, name, readOnly, apply, dialog, one] {
        if (readOnly && readOnly->isChecked() != state->readOnly) {
            for (const QString &path : std::as_const(state->paths)) {
                QFile file(path);
                QFile::Permissions permissions = file.permissions();
                if (readOnly->isChecked())
                    permissions &= ~(QFile::WriteOwner | QFile::WriteUser | QFile::WriteGroup | QFile::WriteOther);
                else
                    permissions |= QFile::WriteOwner | QFile::WriteUser;
                file.setPermissions(permissions);
            }
            state->readOnly = readOnly->isChecked();
        }
        if (one && name) {
            QFileInfo current(state->paths.first());
            const QString wanted = name->text();
            if (!wanted.isEmpty() && wanted != current.fileName() && !wanted.contains(QLatin1Char('/'))) {
                const QString target = current.absoluteDir().filePath(wanted);
                if (QFile::rename(current.absoluteFilePath(), target)) {
                    state->paths[0] = target;
                    dialog->setWindowTitle(QObject::tr("%1 Properties").arg(wanted));
                }
            }
        }
        apply->setEnabled(false);
    };
    QObject::connect(apply, &QPushButton::clicked, dialog, applyChanges);
    QObject::connect(ok, &QPushButton::clicked, dialog, [applyChanges, dialog] {
        applyChanges();
        dialog->accept();
    });
    QObject::connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    dialog->show();
}

} // namespace fileproperties
