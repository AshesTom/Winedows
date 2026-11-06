// SPDX-License-Identifier: LGPL-2.1-or-later
#include "folderoptions.h"

#include "fileicons.h"
#include "filebrowser.h"
#include "programs.h"
#include "theme.h"
#include "xpdialog.h"
#include "xpfiles.h"

#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QTreeWidget>

#include <memory>
#include <numeric>

namespace {

// shell32.dll's pages and the sheet's caption
const int DIALOG_GENERAL = 29959, DIALOG_VIEW = 29960, DIALOG_FILE_TYPES = 1099, STRING_CAPTION = 22985;
// General: the radio pairs (Tasks, Browse folders, Click items, underlining), their pictures
// (0x3019c172: icon 182 + the chosen radio's offset from 30098, in 30109 + half of it) and
// Restore Defaults
const int ID_TASKS = 30098, ID_CLASSIC = 30099, ID_SAME_WINDOW = 30100, ID_OWN_WINDOW = 30101, ID_SINGLE = 30102,
          ID_DOUBLE = 30103, ID_UNDERLINE_BROWSER = 30104, ID_UNDERLINE_POINT = 30105, ID_GENERAL_DEFAULTS = 30107,
          ID_FIRST_PICTURE = 30109, ICON_FIRST_PICTURE = 182;
// View: the tree, its Restore Defaults, Reset All Folders and Apply to All Folders (0x301b99d0)
const int ID_TREE = 30120, ID_TREE_DEFAULTS = 30121, ID_RESET_ALL = 30122, ID_APPLY_ALL = 30123;
const int STRING_FOLDER_VIEWS = 30275, STRING_APPLY_ALL = 30276, STRING_RESET_ALL = 30277;
// File Types: the list, its searching animation, New, Delete, the details' icon, program, Change...,
// words, Advanced and group box
const int ID_TYPES = 1000, ID_TYPES_ANIMATION = 1028, ID_NEW = 1001, ID_DELETE = 1002, ID_TYPE_ICON = 1007,
          ID_OPENS_WITH = 1018, ID_CHANGE = 1019, ID_TYPE_WORDS = 1020, ID_ADVANCED = 1021, ID_DETAILS = 1022;
const int AVI_SEARCH = 150, FILL_STEP = 40;
// the Extensions column as wide as XP's in Wine
const int EXTENSIONS_WIDTH = 64;
const int STRING_EXTENSIONS = 10098, STRING_FILE_TYPES = 10080, STRING_DETAILS_FOR = 10099, STRING_TYPE_WORDS = 10097,
          STRING_NONE = 10095;

// The tree's rows as XP's in Wine draws them: 16 tall, each level 19 further in, the 16-pixel image
// 4 into its level, a check box or radio 2 into that place and a row down, the words 21 after it
const int SHEET_OFFSET_X = 16, SHEET_OFFSET_Y = 14;
const int TOP_MARGIN = 2, ROW = 16, INDENT = 19, IMAGE_X = 4, GLYPH_X = 2, GLYPH_Y = 1, GLYPH = 13, TEXT_X = 21;

QString settingsFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/native-explorer.conf");
}

const std::optional<w4w::PeResources> &shell32()
{
    static const auto resources = xpfiles::resources(xpfiles::find(QStringLiteral("shell32.dll")));
    return resources;
}

QString shellText(int id, const char *fallback)
{
    return xpfiles::shellString(id, fallback);
}

// An Advanced setting's value, by the last part of its RegPath and its ValueName; Hidden is the
// shell's own showHidden (Hidden 1 shows, 2 hides).
QString valueKey(const QString &regPath, const QString &valueName)
{
    return regPath.section(QLatin1Char('\\'), -1) + QLatin1Char('/') + valueName;
}

int readValue(const QString &key, int fallback)
{
    QSettings settings(settingsFile(), QSettings::IniFormat);
    if (key == QLatin1String("Advanced/Hidden")) {
        const QVariant shown = settings.value(QStringLiteral("showHidden"));
        return shown.isValid() ? (shown.toBool() ? 1 : 2) : fallback;
    }
    return settings.value(key, fallback).toInt();
}

void writeValue(const QString &key, int value)
{
    QSettings settings(settingsFile(), QSettings::IniFormat);
    if (key == QLatin1String("Advanced/Hidden"))
        settings.setValue(QStringLiteral("showHidden"), value == 1);
    else
        settings.setValue(key, value);
}

// One key of Explorer\Advanced\Folder, as shell32's REGINST sets it
struct Option
{
    QString key;                 // under Advanced\Folder; empty for the folder itself
    QString text, type, regPath, valueName, warning;
    int checkedValue = 1, uncheckedValue = 0, defaultValue = 0, bitmap = -1;
};

// "@shell32.dll,-30503": a string of shell32's
QString indirect(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral("^@shell32\\.dll,-(\\d+)$"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = pattern.match(value);
    return match.hasMatch() ? shellText(match.captured(1).toInt(), "") : value;
}

// shell32's REGINST resource (an INF): its Advanced\Folder keys from the sections setup adds on an
// NT Professional system (RegDll's RegAdvancedOptions, RegDllNT's RegShellNT, RegDllNTPro's
// RegShellNTPro)
QList<Option> advancedOptions()
{
    QList<Option> options;
    if (!shell32())
        return options;
    const QString inf = QString::fromLatin1(shell32()->find(QStringLiteral("REGINST"), QStringLiteral("REGINST")));
    QHash<QString, QStringList> sections;
    QString section;
    for (const QString &raw : inf.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            section = line.mid(1, line.indexOf(QLatin1Char(']')) - 1);
            continue;
        }
        if (!line.isEmpty() && !line.startsWith(QLatin1Char(';')))
            sections[section].append(line);
    }
    QHash<QString, QString> strings;
    static const QRegularExpression assignment(QStringLiteral("^(\\w+)\\s*=\\s*\"?([^\";]*)\"?"));
    for (const QString &line : sections.value(QStringLiteral("Strings")))
        if (const QRegularExpressionMatch match = assignment.match(line); match.hasMatch())
            strings.insert(match.captured(1), match.captured(2).trimmed());
    const auto expand = [&strings](QString value) {
        static const QRegularExpression variable(QStringLiteral("%(\\w+)%"));
        for (QRegularExpressionMatch match = variable.match(value); match.hasMatch(); match = variable.match(value))
            value.replace(match.capturedStart(), match.capturedLength(), strings.value(match.captured(1), QStringLiteral("shell32.dll")));
        return value;
    };
    const QString folder = expand(QStringLiteral("%ADV_FOLDER%"));
    QMap<QString, Option> byKey;
    static const QRegularExpression field(QStringLiteral(",(?=(?:[^\"]*\"[^\"]*\")*[^\"]*$)"));
    for (const char *name : {"RegAdvancedOptions", "RegShellNT", "RegShellNTPro"}) {
        for (const QString &line : sections.value(QLatin1String(name))) {
            QStringList fields = line.split(field);
            if (fields.size() < 5)
                continue;
            for (QString &f : fields)
                f = f.trimmed().remove(QLatin1Char('"'));
            const QString key = expand(fields[1]);
            if (key != folder && !key.startsWith(folder + QLatin1Char('\\')))
                continue;
            Option &option = byKey[key.mid(folder.size() + 1)];
            option.key = key.mid(folder.size() + 1);
            const QString value = expand(fields[4]);
            const QString &what = fields[2];
            const bool number = expand(fields[3]).startsWith(QLatin1String("0x0001"));
            const int integer = number ? int(value.toUInt(nullptr, 16)) : 0;
            if (what == QLatin1String("Text"))
                option.text = indirect(value);
            else if (what == QLatin1String("Type"))
                option.type = value.toLower();
            else if (what == QLatin1String("RegPath"))
                option.regPath = value;
            else if (what == QLatin1String("ValueName"))
                option.valueName = value;
            else if (what == QLatin1String("CheckedValue"))
                option.checkedValue = integer;
            else if (what == QLatin1String("UncheckedValue"))
                option.uncheckedValue = integer;
            else if (what == QLatin1String("DefaultValue"))
                option.defaultValue = integer;
            else if (what == QLatin1String("Bitmap"))
                option.bitmap = value.section(QLatin1Char(','), 1).toInt();
            else if (what == QLatin1String("WarningIfNotDefault"))
                option.warning = indirect(value);
        }
    }
    // keys without a Type (a setting's Policy subkeys) are not shown
    for (const Option &option : std::as_const(byKey))
        if (!option.type.isEmpty())
            options.append(option);
    return options;
}

// "shell32.dll,4": the fifth of shell32's icon groups, as ExtractIcon counts them
QIcon shellIconAt(int index)
{
    if (!shell32() || index < 0)
        return {};
    QList<int> ids;
    for (const QString &name : shell32()->names(QStringLiteral("14"))) {
        bool ok = false;
        const int id = name.toInt(&ok);
        if (ok)
            ids.append(id);
    }
    std::sort(ids.begin(), ids.end());
    return index < ids.size() ? fileicons::shell(ids[index]) : QIcon();
}

enum { OptionRole = Qt::UserRole, ValueRole };

// browseui's RegTreeOptions tree: groups with their picture, check boxes and radio buttons drawn
// as the visual style draws them, each level sorted by its words (TVM_SORTCHILDREN, sub_3004c6bd)
class AdvancedTree : public QTreeWidget
{
public:
    explicit AdvancedTree(QWidget *parent)
        : QTreeWidget(parent)
    {
        setHeaderHidden(true);
        setRootIsDecorated(false);
        setItemsExpandable(false);
        setIndentation(ShellTheme::instance()->scaled(INDENT));
        setUniformRowHeights(true);
        // the margin above the rows is the tree's own white
        setBackgroundRole(QPalette::Base);
        setAutoFillBackground(true);
        setItemDelegate(new Delegate(this));
        setSelectionMode(QAbstractItemView::SingleSelection);
        m_options = advancedOptions();
        QHash<QString, QTreeWidgetItem *> items;
        // parents before their children: the shorter keys first
        QList<int> order(m_options.size());
        std::iota(order.begin(), order.end(), 0);
        const auto depth = [this](int i) { return m_options[i].key.isEmpty() ? -1 : int(m_options[i].key.count(QLatin1Char('\\'))); };
        std::stable_sort(order.begin(), order.end(), [depth](int a, int b) { return depth(a) < depth(b); });
        for (int i : std::as_const(order)) {
            const Option &option = m_options[i];
            const QString parentKey = option.key.contains(QLatin1Char('\\')) ? option.key.section(QLatin1Char('\\'), 0, -2) : QString();
            QTreeWidgetItem *parent = option.key.isEmpty() ? nullptr : items.value(parentKey);
            if (!option.key.isEmpty() && !parent)
                continue;
            auto *item = parent ? new QTreeWidgetItem(parent, {option.text}) : new QTreeWidgetItem(this, {option.text});
            item->setData(0, OptionRole, i);
            items.insert(option.key, item);
        }
        sortItems(0, Qt::AscendingOrder);
        expandAll();
        load(false);
    }

    // Each setting's value as it is kept, or its default
    void load(bool defaults)
    {
        forEach([this, defaults](QTreeWidgetItem *item, const Option &option) {
            if (option.type == QLatin1String("group"))
                return;
            const QString key = valueKey(option.regPath, option.valueName);
            item->setData(0, ValueRole, defaults ? option.defaultValue : readValue(key, option.defaultValue));
        });
        viewport()->update();
    }
    void save() const
    {
        const_cast<AdvancedTree *>(this)->forEach([](QTreeWidgetItem *item, const Option &option) {
            if (option.type == QLatin1String("checkbox") || (option.type == QLatin1String("radio") && item->data(0, ValueRole).toInt() == option.checkedValue))
                writeValue(valueKey(option.regPath, option.valueName), item->data(0, ValueRole).toInt());
        });
    }
    std::function<void()> changed;

    bool isOn(const QTreeWidgetItem *item) const
    {
        const Option &option = m_options[item->data(0, OptionRole).toInt()];
        return item->data(0, ValueRole).toInt() == option.checkedValue;
    }
    const Option &optionOf(const QTreeWidgetItem *item) const { return m_options[item->data(0, OptionRole).toInt()]; }

protected:
    // the first row 2 lower than Qt puts it (XP in Wine: its first image 4 under the border); the
    // tree view sets its margins for the header it would have each time
    void updateGeometries() override
    {
        QTreeWidget::updateGeometries();
        setViewportMargins(0, ShellTheme::instance()->scaled(TOP_MARGIN), 0, 0);
    }
    // TVS_HASLINES and TVS_HASBUTTONS are off: nothing beside the items
    void drawBranches(QPainter *, const QRect &, const QModelIndex &) const override {}
    void mousePressEvent(QMouseEvent *event) override
    {
        QTreeWidget::mousePressEvent(event);
        if (event->button() == Qt::LeftButton)
            if (QTreeWidgetItem *item = itemAt(event->position().toPoint()))
                toggle(item);
    }
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Space && currentItem()) {
            toggle(currentItem());
            return;
        }
        QTreeWidget::keyPressEvent(event);
    }

private:
    class Delegate : public QStyledItemDelegate
    {
    public:
        explicit Delegate(AdvancedTree *tree) : QStyledItemDelegate(tree), m_tree(tree) {}
        QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
        {
            return QSize(option.rect.width(), ShellTheme::instance()->scaled(ROW));
        }
        void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
        {
            ShellTheme *theme = ShellTheme::instance();
            const QTreeWidgetItem *item = m_tree->itemFromIndex(index);
            const Option &option_ = m_tree->optionOf(item);
            const QRect slot(option.rect.x() + theme->scaled(IMAGE_X), option.rect.y(), theme->scaled(16), theme->scaled(ROW));
            if (option_.type == QLatin1String("group")) {
                shellIconAt(option_.bitmap).paint(painter, slot);
            } else {
                QStyleOptionButton button;
                button.initFrom(m_tree);
                button.rect = QRect(slot.x() + theme->scaled(GLYPH_X), slot.y() + theme->scaled(GLYPH_Y), theme->scaled(GLYPH), theme->scaled(GLYPH));
                button.state = QStyle::State_Enabled | (m_tree->isOn(item) ? QStyle::State_On : QStyle::State_Off);
                const bool radio = option_.type == QLatin1String("radio");
                m_tree->style()->drawPrimitive(radio ? QStyle::PE_IndicatorRadioButton : QStyle::PE_IndicatorCheckBox, &button, painter, m_tree);
            }
            // the words, highlighted when chosen, as a tree view's label
            const QString text = item->text(0);
            const int textX = slot.x() + theme->scaled(TEXT_X);
            const QRect label(textX - 2, option.rect.y(), option.fontMetrics.horizontalAdvance(text) + 4, option.rect.height());
            const bool selected = option.state & QStyle::State_Selected;
            const bool focused = m_tree->hasFocus();
            if (selected && focused)
                painter->fillRect(label, option.palette.color(QPalette::Highlight));
            painter->setPen(option.palette.color(selected && focused ? QPalette::HighlightedText : QPalette::Text));
            // (a row higher than Qt centres them, as XP's tree view draws its labels)
            painter->drawText(QRect(textX, option.rect.y() - 1, option.rect.right() - textX, option.rect.height()), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, text);
            if ((option.state & QStyle::State_HasFocus) && focused) {
                QStyleOptionFocusRect focus;
                focus.QStyleOption::operator=(option);
                focus.rect = label;
                m_tree->style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, painter, m_tree);
            }
        }

    private:
        AdvancedTree *m_tree;
    };

    void forEach(const std::function<void(QTreeWidgetItem *, const Option &)> &visit)
    {
        std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *item) {
            visit(item, optionOf(item));
            for (int i = 0; i < item->childCount(); ++i)
                walk(item->child(i));
        };
        for (int i = 0; i < topLevelItemCount(); ++i)
            walk(topLevelItem(i));
    }

    void toggle(QTreeWidgetItem *item)
    {
        const Option &option = optionOf(item);
        const int before = item->data(0, ValueRole).toInt();
        if (option.type == QLatin1String("checkbox")) {
            item->setData(0, ValueRole, before == option.checkedValue ? option.uncheckedValue : option.checkedValue);
        } else if (option.type == QLatin1String("radio")) {
            if (before == option.checkedValue)
                return;
            // the radios of a group share their value
            if (QTreeWidgetItem *parent = item->parent())
                for (int i = 0; i < parent->childCount(); ++i)
                    if (optionOf(parent->child(i)).valueName == option.valueName)
                        parent->child(i)->setData(0, ValueRole, option.checkedValue);
        } else {
            return;
        }
        // WarningIfNotDefault (Hide protected operating system files): asked when leaving the default
        if (!option.warning.isEmpty() && item->data(0, ValueRole).toInt() != option.defaultValue
            && QMessageBox::warning(this, window()->windowTitle(), option.warning, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            item->setData(0, ValueRole, before);
        viewport()->update();
        if (changed)
            changed();
    }

    QList<Option> m_options;
};

// A pair of the General page's radios, exclusive within the pair only
QButtonGroup *pair(const xpdialog::Page &page, int first, int second, bool firstOn, QObject *owner)
{
    auto *group = new QButtonGroup(owner);
    for (int id : {first, second})
        if (auto *button = qobject_cast<QAbstractButton *>(page.controls.value(id)))
            group->addButton(button, id);
    if (QAbstractButton *button = group->button(firstOn ? first : second))
        button->setChecked(true);
    return group;
}

// The extension's program, as xdg-mime knows it
std::optional<programs::Program> defaultProgram(const QString &mimeType)
{
    QProcess process;
    process.start(QStringLiteral("xdg-mime"), {QStringLiteral("query"), QStringLiteral("default"), mimeType});
    if (!process.waitForFinished(3000))
        return std::nullopt;
    const QString id = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    return id.isEmpty() ? std::nullopt : programs::find(id);
}

} // namespace

namespace folderoptions {

bool hideKnownExtensions()
{
    return readValue(QStringLiteral("Advanced/HideFileExt"), 1) == 1;
}

bool rememberFolderViews()
{
    return readValue(QStringLiteral("Advanced/ClassicViewState"), 0) == 0;
}

bool fullPathInTitle()
{
    return readValue(QStringLiteral("CabinetState/FullPath"), 0) == 1;
}

bool fullPathInAddress()
{
    return readValue(QStringLiteral("CabinetState/FullPathAddress"), 1) == 1;
}

bool showSystemFolders()
{
    return readValue(QStringLiteral("Advanced/WebViewBarricade"), 0) == 1;
}

bool separateWindows()
{
    return QSettings(settingsFile(), QSettings::IniFormat).value(QStringLiteral("separateWindows"), false).toBool();
}

QString shownName(const QFileInfo &info)
{
    const QString name = info.fileName();
    if (!hideKnownExtensions() || info.isDir() || info.suffix().isEmpty() || info.completeBaseName().isEmpty())
        return name;
    // known: a type is registered for the extension (HKCR\.ext on XP)
    static const QMimeDatabase mimes;
    const QList<QMimeType> types = mimes.mimeTypesForFileName(name);
    if (types.isEmpty())
        return name;
    return info.completeBaseName();
}

QString renamedName(const QFileInfo &info, const QString &name)
{
    if (shownName(info) == info.fileName())
        return name;
    return name + QLatin1Char('.') + info.suffix();
}

void show(QWidget *parent, std::function<void()> applyToAll)
{
    static QPointer<QDialog> open;
    if (open) {
        open->show();
        open->raise();
        open->activateWindow();
        return;
    }
    if (!shell32())
        return;
    // Without a parent, as the other sheets: XP's has no icon in its title bar, and a Qt dialog
    // would take its window's
    auto *dialog = new QDialog;
    open = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(shellText(STRING_CAPTION, "Folder Options"));
    auto *tabs = new xpdialog::SheetTabs(dialog);
    auto *ok = new QPushButton(QObject::tr("OK"), dialog);
    auto *cancel = new QPushButton(QObject::tr("Cancel"), dialog);
    auto *apply = new QPushButton(QObject::tr("&Apply"), dialog);
    ok->setDefault(true);
    apply->setEnabled(false);
    const auto changed = [apply] { apply->setEnabled(true); };
    QSettings settings(settingsFile(), QSettings::IniFormat);

    // General
    auto *general = new QWidget;
    const xpdialog::Page generalPage = xpdialog::build(*shell32(), DIALOG_GENERAL, general);
    QButtonGroup *tasks = pair(generalPage, ID_TASKS, ID_CLASSIC, !settings.value(QStringLiteral("classicFolders"), false).toBool(), dialog);
    QButtonGroup *browse = pair(generalPage, ID_SAME_WINDOW, ID_OWN_WINDOW, !separateWindows(), dialog);
    QButtonGroup *click = pair(generalPage, ID_SINGLE, ID_DOUBLE, settings.value(QStringLiteral("singleClick"), false).toBool(), dialog);
    QButtonGroup *underline = pair(generalPage, ID_UNDERLINE_BROWSER, ID_UNDERLINE_POINT, !settings.value(QStringLiteral("underlineOnPoint"), false).toBool(), dialog);
    const auto pictures = [generalPage, tasks, browse, click, underline] {
        int picture = 0;
        for (QButtonGroup *group : {tasks, browse, click}) {
            const int chosen = group->checkedId();
            if (auto *label = qobject_cast<QLabel *>(generalPage.controls.value(ID_FIRST_PICTURE + picture)); label && chosen > 0)
                label->setPixmap(fileicons::shell(ICON_FIRST_PICTURE + chosen - ID_TASKS).pixmap(32, 32));
            ++picture;
        }
        // the underlining goes with single-click
        for (QAbstractButton *button : underline->buttons())
            button->setEnabled(click->checkedId() == ID_SINGLE);
    };
    pictures();
    for (QButtonGroup *group : {tasks, browse, click, underline})
        QObject::connect(group, &QButtonGroup::idClicked, dialog, [pictures, changed] {
            pictures();
            changed();
        });
    if (auto *defaults = qobject_cast<QAbstractButton *>(generalPage.controls.value(ID_GENERAL_DEFAULTS)))
        QObject::connect(defaults, &QAbstractButton::clicked, dialog, [=] {
            for (const auto &[group, id] : {std::pair{tasks, ID_TASKS}, {browse, ID_SAME_WINDOW}, {click, ID_DOUBLE}, {underline, ID_UNDERLINE_BROWSER}})
                if (QAbstractButton *button = group->button(id))
                    button->setChecked(true);
            pictures();
            changed();
        });
    tabs->addTab(general, generalPage.caption);

    // View
    auto *view = new QWidget;
    const xpdialog::Page viewPage = xpdialog::build(*shell32(), DIALOG_VIEW, view);
    auto *tree = new AdvancedTree(view);
    if (QWidget *place = viewPage.controls.value(ID_TREE)) {
        tree->setGeometry(place->geometry());
        place->hide();
    }
    tree->changed = changed;
    if (auto *defaults = qobject_cast<QAbstractButton *>(viewPage.controls.value(ID_TREE_DEFAULTS)))
        QObject::connect(defaults, &QAbstractButton::clicked, dialog, [tree, changed] {
            tree->load(true);
            changed();
        });
    if (auto *all = qobject_cast<QAbstractButton *>(viewPage.controls.value(ID_APPLY_ALL))) {
        all->setEnabled(bool(applyToAll));
        QObject::connect(all, &QAbstractButton::clicked, dialog, [dialog, applyToAll] {
            if (QMessageBox::information(dialog, shellText(STRING_FOLDER_VIEWS, "Folder views"), shellText(STRING_APPLY_ALL, ""),
                                         QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes && applyToAll)
                applyToAll();
        });
    }
    if (auto *reset = qobject_cast<QAbstractButton *>(viewPage.controls.value(ID_RESET_ALL)))
        QObject::connect(reset, &QAbstractButton::clicked, dialog, [dialog] {
            if (QMessageBox::information(dialog, shellText(STRING_FOLDER_VIEWS, "Folder views"), shellText(STRING_RESET_ALL, ""),
                                         QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes)
                FileBrowser::resetFolderViews();
        });
    tabs->addTab(view, viewPage.caption);

    // File Types: the extensions shared-mime-info knows, their type and program
    auto *types = new QWidget;
    const xpdialog::Page typesPage = xpdialog::build(*shell32(), DIALOG_FILE_TYPES, types);
    auto *list = new QTreeWidget(types);
    if (QWidget *place = typesPage.controls.value(ID_TYPES)) {
        list->setGeometry(place->geometry());
        place->hide();
    }
    for (int id : {ID_NEW, ID_DELETE, ID_ADVANCED})
        if (QWidget *button = typesPage.controls.value(id))
            button->setEnabled(false);
    list->setRootIsDecorated(false);
    list->setHeaderLabels({shellText(STRING_EXTENSIONS, "Extensions"), shellText(STRING_FILE_TYPES, "File Types")});
    list->setColumnWidth(0, ShellTheme::instance()->scaled(EXTENSIONS_WIDTH));
    list->setUniformRowHeights(true);
    auto *details = qobject_cast<QGroupBox *>(typesPage.controls.value(ID_DETAILS));
    auto *words = qobject_cast<QLabel *>(typesPage.controls.value(ID_TYPE_WORDS));
    auto *program = typesPage.controls.value(ID_OPENS_WITH);
    auto *programIcon = qobject_cast<QLabel *>(typesPage.controls.value(ID_TYPE_ICON));
    const auto describe = [=] {
        const QTreeWidgetItem *item = list->currentItem();
        if (!item)
            return;
        const QString extension = item->text(0), type = item->text(1);
        if (details)
            details->setTitle(shellText(STRING_DETAILS_FOR, "Details for '%1!ls!' extension").replace(QLatin1String("%1!ls!"), extension));
        if (words)
            words->setText(shellText(STRING_TYPE_WORDS, "").replace(QLatin1String("%1!ls!"), extension).replace(QLatin1String("%2!ls!"), type).replace(QLatin1String("%3!ls!"), type));
        const auto chosen = defaultProgram(item->data(0, Qt::UserRole).toString());
        const QString name = chosen ? chosen->name : shellText(STRING_NONE, "(None)");
        if (auto *line = qobject_cast<QLineEdit *>(program))
            line->setText(name);
        else if (auto *label = qobject_cast<QLabel *>(program))
            label->setText(name);
        if (programIcon)
            programIcon->setPixmap(chosen ? programs::iconOf(*chosen).pixmap(16, 16) : QPixmap());
    };
    QObject::connect(list, &QTreeWidget::currentItemChanged, dialog, describe);
    if (auto *change = qobject_cast<QAbstractButton *>(typesPage.controls.value(ID_CHANGE)))
        QObject::connect(change, &QAbstractButton::clicked, dialog, [=] {
            const QTreeWidgetItem *item = list->currentItem();
            if (!item)
                return;
            const QString sample = QStringLiteral("x.") + item->text(0).toLower();
            if (const auto chosen = programs::chooseProgram(sample, dialog, true)) {
                programs::setDefaultFor(sample, *chosen);
                describe();
            }
        });
    // The list fills after the page is up, a few rows at a time, over shell32's searching animation
    // (AVI 150 in control 1028, played without end: 0x3017a7ef), as XP's does
    {
        static const QMimeDatabase mimes;
        QMap<QString, QMimeType> byExtension;
        for (const QMimeType &type : mimes.allMimeTypes())
            for (const QString &suffix : type.suffixes())
                if (!suffix.contains(QLatin1Char('.')) && !byExtension.contains(suffix.toUpper()))
                    byExtension.insert(suffix.toUpper(), type);
        auto *animation = dynamic_cast<xpdialog::Animation *>(typesPage.controls.value(ID_TYPES_ANIMATION));
        if (animation && animation->open(*shell32(), AVI_SEARCH)) {
            animation->raise();
            animation->show();
            animation->play();
        }
        list->hide();
        auto *filler = new QTimer(dialog);
        auto next = std::make_shared<QMap<QString, QMimeType>::const_iterator>(byExtension.cbegin());
        auto entries = std::make_shared<QMap<QString, QMimeType>>(std::move(byExtension));
        *next = entries->cbegin();
        QObject::connect(filler, &QTimer::timeout, dialog, [=] {
            for (int i = 0; i < FILL_STEP && *next != entries->cend(); ++i, ++*next) {
                auto *item = new QTreeWidgetItem(list, {(*next).key(), (*next).value().comment()});
                item->setIcon(0, fileicons::forFile(QFileInfo(QStringLiteral("x.") + (*next).key().toLower())));
                item->setData(0, Qt::UserRole, (*next).value().name());
            }
            if (*next != entries->cend())
                return;
            filler->stop();
            if (animation) {
                animation->stop();
                animation->hide();
            }
            list->show();
            if (list->topLevelItemCount())
                list->setCurrentItem(list->topLevelItem(0));
        });
        filler->start(0);
    }
    tabs->addTab(types, typesPage.caption);

    const auto save = [=] {
        QSettings write(settingsFile(), QSettings::IniFormat);
        write.setValue(QStringLiteral("classicFolders"), tasks->checkedId() == ID_CLASSIC);
        write.setValue(QStringLiteral("separateWindows"), browse->checkedId() == ID_OWN_WINDOW);
        write.setValue(QStringLiteral("singleClick"), click->checkedId() == ID_SINGLE);
        write.setValue(QStringLiteral("underlineOnPoint"), underline->checkedId() == ID_UNDERLINE_POINT);
        write.sync();
        tree->save();
        FileBrowser::settingsChanged();
        apply->setEnabled(false);
    };
    xpdialog::arrangeSheet(dialog, tabs, {ok, cancel, apply});
    QObject::connect(apply, &QPushButton::clicked, dialog, save);
    QObject::connect(ok, &QPushButton::clicked, dialog, [dialog, save] {
        save();
        dialog->accept();
    });
    QObject::connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    // From a folder window, XP's opens 16 right of and 14 below the window's frame (in Wine)
    if (parent) {
        QObject::connect(parent, &QObject::destroyed, dialog, &QDialog::close);
        dialog->move(parent->window()->frameGeometry().topLeft() + QPoint(SHEET_OFFSET_X, SHEET_OFFSET_Y));
    }
    dialog->show();
}

} // namespace folderoptions
