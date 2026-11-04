// SPDX-License-Identifier: LGPL-2.1-or-later
#include "taskbarproperties.h"
#include "theme.h"

#include "xpdialog.h"
#include "xpfiles.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QComboBox>
#include <QTreeWidget>
#include <QStandardPaths>

namespace {

// explorer.exe's words and pictures
const int STRING_SHEET = 810, STRING_TASKBAR = 518, STRING_START_MENU = 510;
const int DIALOG_TASKBAR = 6, DIALOG_START_MENU = 205;
// The Taskbar page's controls
enum {
    ID_ON_TOP = 1101, ID_AUTO_HIDE = 1102, ID_CLOCK = 1103, ID_GROUP = 1104, ID_LOCK = 1105, ID_QUICK_LAUNCH = 1107,
    ID_TASKBAR_PREVIEW = 1111, ID_NOTIFICATION_PREVIEW = 1112, ID_HIDE_INACTIVE = 1000, ID_CUSTOMIZE_NOTIFICATIONS = 1007,
};
// The Start Menu page's
enum { ID_CUSTOMIZE_CLASSIC = 1130, ID_CUSTOMIZE = 1131, ID_START_MENU = 1132, ID_CLASSIC = 1133, ID_START_PREVIEW = 1134 };
// The pictures, as explorer.exe's page picks them (its function at 0x1038be8): an auto-hidden taskbar
// is 145; otherwise 146, plus 1 unlocked, 2 not grouping and 4 without Quick Launch (its table of
// {control, step} at 0x1038c6c). The notification area is 180, plus 1 without the clock and 2 not
// hiding inactive icons. The Start menu is 170, the classic one 171.
const int BITMAP_AUTO_HIDDEN = 145, BITMAP_TASKBAR = 146, BITMAP_NOTIFICATION = 180, BITMAP_START_MENU = 170;

QString settingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/native-explorer.conf");
}

std::optional<w4w::PeResources> &explorer()
{
    static std::optional<w4w::PeResources> pe = xpfiles::resources(xpfiles::find(QStringLiteral("explorer.exe")));
    return pe;
}

QString text(int id, const char *fallback)
{
    const QString found = explorer() ? xpfiles::string(*explorer(), id) : QString();
    return found.isEmpty() ? QString::fromLatin1(fallback) : found;
}

// LoadImage with LR_LOADMAP3DCOLORS, as explorer.exe loads them (sub_1037f98): in a bitmap's colour table the greys
// 128, 192 and 223 become the 3D shadow, face and light colours, so that Windows Classic's schemes tint them
QPixmap picture(int id)
{
    if (!explorer())
        return QPixmap();
    QImage image = xpfiles::bitmap(*explorer(), id);
    QList<QRgb> table = image.colorTable();
    if (!table.isEmpty()) {
        ShellTheme *theme = ShellTheme::instance();
        for (QRgb &entry : table) {
            const QRgb grey = entry & 0xffffff;
            if (grey == 0x808080)
                entry = theme->system(QStringLiteral("btnshadow"), QColor(128, 128, 128)).rgb();
            else if (grey == 0xc0c0c0)
                entry = theme->system(QStringLiteral("btnface"), QColor(192, 192, 192)).rgb();
            else if (grey == 0xdfdfdf)
                entry = theme->system(QStringLiteral("light3d"), QColor(223, 223, 223)).rgb();
        }
        image.setColorTable(table);
    }
    return QPixmap::fromImage(image);
}

} // namespace

TaskbarSettings TaskbarSettings::load()
{
    const QSettings settings(settingsPath(), QSettings::IniFormat);
    TaskbarSettings loaded;
    loaded.group = settings.value(QStringLiteral("Taskbar/TaskbarGlomming"), true).toBool();
    loaded.groupSize = settings.value(QStringLiteral("Taskbar/TaskbarGroupSize"), 0).toInt();
    loaded.clock = !settings.value(QStringLiteral("Taskbar/HideClock"), false).toBool();
    loaded.autoHide = settings.value(QStringLiteral("Taskbar/AutoHide"), false).toBool();
    loaded.quickLaunch = settings.value(QStringLiteral("Taskbar/QuickLaunch"), false).toBool();
    loaded.desktopBand = settings.value(QStringLiteral("Taskbar/DesktopBand"), false).toBool();
    loaded.customToolbar = settings.value(QStringLiteral("Taskbar/CustomToolbar")).toString();
    loaded.locked = !settings.value(QStringLiteral("Taskbar/TaskbarSizeMove"), false).toBool();
    loaded.onTop = settings.value(QStringLiteral("Taskbar/AlwaysOnTop"), true).toBool();
    loaded.quickLaunchText = settings.value(QStringLiteral("Taskbar/QuickLaunchText"), false).toBool();
    loaded.quickLaunchTitle = settings.value(QStringLiteral("Taskbar/QuickLaunchTitle"), false).toBool();
    loaded.quickLaunchLargeIcons = settings.value(QStringLiteral("Taskbar/QuickLaunchLargeIcons"), false).toBool();
    loaded.hideInactive = settings.value(QStringLiteral("TrayNotify/EnableAutoTray"), true).toBool();
    loaded.classicStartMenu = settings.value(QStringLiteral("StartMenu/Classic"), false).toBool();
    loaded.classicFavorites = settings.value(QStringLiteral("StartMenu/ClassicFavorites"), false).toBool();
    loaded.classicLogOff = settings.value(QStringLiteral("StartMenu/ClassicLogOff"), true).toBool();
    loaded.classicRun = settings.value(QStringLiteral("StartMenu/ClassicRun"), true).toBool();
    loaded.classicSmallIcons = settings.value(QStringLiteral("StartMenu/ClassicSmallIcons"), false).toBool();
    loaded.panelHidden = settings.value(QStringLiteral("StartMenu/PanelHidden")).toStringList();
    loaded.panelFavorites = settings.value(QStringLiteral("StartMenu/PanelFavorites"), false).toBool();
    loaded.panelSmallIcons = settings.value(QStringLiteral("StartMenu/PanelSmallIcons"), false).toBool();
    loaded.panelRecentDocs = settings.value(QStringLiteral("StartMenu/PanelRecentDocs"), true).toBool();
    loaded.panelHoverOpen = settings.value(QStringLiteral("StartMenu/PanelHoverOpen"), false).toBool();
    loaded.panelMinMFU = settings.value(QStringLiteral("StartMenu/PanelMinMFU"), 6).toInt();
    loaded.startButtonBalloonTip = settings.value(QStringLiteral("StartMenu/StartButtonBalloonTip"), 0).toInt();
    return loaded;
}

void TaskbarSettings::save() const
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Taskbar/TaskbarGlomming"), group);
    settings.setValue(QStringLiteral("Taskbar/TaskbarGroupSize"), groupSize);
    settings.setValue(QStringLiteral("Taskbar/HideClock"), !clock);
    settings.setValue(QStringLiteral("Taskbar/AutoHide"), autoHide);
    settings.setValue(QStringLiteral("Taskbar/QuickLaunch"), quickLaunch);
    settings.setValue(QStringLiteral("Taskbar/DesktopBand"), desktopBand);
    if (customToolbar.isEmpty())
        settings.remove(QStringLiteral("Taskbar/CustomToolbar"));
    else
        settings.setValue(QStringLiteral("Taskbar/CustomToolbar"), customToolbar);
    settings.setValue(QStringLiteral("Taskbar/TaskbarSizeMove"), !locked);
    settings.setValue(QStringLiteral("Taskbar/AlwaysOnTop"), onTop);
    settings.setValue(QStringLiteral("Taskbar/QuickLaunchText"), quickLaunchText);
    settings.setValue(QStringLiteral("Taskbar/QuickLaunchTitle"), quickLaunchTitle);
    settings.setValue(QStringLiteral("Taskbar/QuickLaunchLargeIcons"), quickLaunchLargeIcons);
    settings.setValue(QStringLiteral("TrayNotify/EnableAutoTray"), hideInactive);
    settings.setValue(QStringLiteral("StartMenu/Classic"), classicStartMenu);
    settings.setValue(QStringLiteral("StartMenu/ClassicFavorites"), classicFavorites);
    settings.setValue(QStringLiteral("StartMenu/ClassicLogOff"), classicLogOff);
    settings.setValue(QStringLiteral("StartMenu/ClassicRun"), classicRun);
    settings.setValue(QStringLiteral("StartMenu/ClassicSmallIcons"), classicSmallIcons);
    if (panelHidden.isEmpty())
        settings.remove(QStringLiteral("StartMenu/PanelHidden"));
    else
        settings.setValue(QStringLiteral("StartMenu/PanelHidden"), panelHidden);
    settings.setValue(QStringLiteral("StartMenu/PanelFavorites"), panelFavorites);
    settings.setValue(QStringLiteral("StartMenu/PanelSmallIcons"), panelSmallIcons);
    settings.setValue(QStringLiteral("StartMenu/PanelRecentDocs"), panelRecentDocs);
    settings.setValue(QStringLiteral("StartMenu/PanelHoverOpen"), panelHoverOpen);
    settings.setValue(QStringLiteral("StartMenu/PanelMinMFU"), panelMinMFU);
    settings.setValue(QStringLiteral("StartMenu/StartButtonBalloonTip"), startButtonBalloonTip);
}

TaskbarSettingsNotifier *TaskbarSettingsNotifier::instance()
{
    static TaskbarSettingsNotifier notifier;
    return &notifier;
}

void TaskbarProperties::showTab(Tab tab)
{
    static QPointer<TaskbarProperties> dialog;
    if (!dialog)
        dialog = new TaskbarProperties;
    dialog->m_tabs->setCurrentIndex(tab);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

TaskbarProperties::TaskbarProperties()
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(text(STRING_SHEET, "Taskbar and Start Menu Properties"));
    m_tabs = new xpdialog::SheetTabs(this);
    m_tabs->addTab(taskbarPage(), text(STRING_TASKBAR, "Taskbar"));
    m_tabs->addTab(startMenuPage(), text(STRING_START_MENU, "Start Menu"));
    auto *ok = new QPushButton(tr("OK"), this);
    auto *cancel = new QPushButton(tr("Cancel"), this);
    m_apply = new QPushButton(tr("&Apply"), this);
    ok->setDefault(true);
    xpdialog::arrangeSheet(this, m_tabs, {ok, cancel, m_apply});
    m_apply->setEnabled(false);
    connect(m_apply, &QPushButton::clicked, this, &TaskbarProperties::apply);
    connect(ok, &QPushButton::clicked, this, [this] {
        apply();
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    updatePreviews();
}

QWidget *TaskbarProperties::taskbarPage()
{
    auto *page = new QWidget;
    if (!explorer())
        return page;
    const xpdialog::Page dialog = xpdialog::build(*explorer(), DIALOG_TASKBAR, page);
    for (auto it = dialog.controls.begin(); it != dialog.controls.end(); ++it)
        if (auto *check = qobject_cast<QCheckBox *>(it.value()))
            m_checks.insert(it.key(), check);
    m_taskbarPreview = qobject_cast<QLabel *>(dialog.controls.value(ID_TASKBAR_PREVIEW));
    m_notificationPreview = qobject_cast<QLabel *>(dialog.controls.value(ID_NOTIFICATION_PREVIEW));
    const TaskbarSettings settings = TaskbarSettings::load();
    const QHash<int, bool> states{
        {ID_LOCK, settings.locked}, {ID_AUTO_HIDE, settings.autoHide}, {ID_ON_TOP, settings.onTop}, {ID_GROUP, settings.group}, {ID_QUICK_LAUNCH, settings.quickLaunch},
        {ID_CLOCK, settings.clock}, {ID_HIDE_INACTIVE, settings.hideInactive},
    };
    for (auto it = states.begin(); it != states.end(); ++it)
        if (QCheckBox *check = m_checks.value(it.key())) {
            check->setChecked(it.value());
            check->setEnabled(it.key() == ID_GROUP || it.key() == ID_CLOCK || it.key() == ID_AUTO_HIDE
                              || it.key() == ID_QUICK_LAUNCH || it.key() == ID_LOCK
                              || it.key() == ID_ON_TOP || it.key() == ID_HIDE_INACTIVE);
            connect(check, &QCheckBox::toggled, this, [this] {
                updatePreviews();
                m_apply->setEnabled(true);
            });
        }
    if (auto *customize = qobject_cast<QAbstractButton *>(dialog.controls.value(ID_CUSTOMIZE_NOTIFICATIONS))) {
        // Customize goes with Hide inactive icons, as on XP
        const auto follow = [this, customize] {
            customize->setEnabled(m_checks.value(ID_HIDE_INACTIVE) && m_checks.value(ID_HIDE_INACTIVE)->isChecked());
        };
        follow();
        if (QCheckBox *hide = m_checks.value(ID_HIDE_INACTIVE))
            connect(hide, &QCheckBox::toggled, this, follow);
        connect(customize, &QAbstractButton::clicked, this, [this] { traynotify::customize(this); });
    }
    return page;
}

// explorer.exe's Customize Classic Start Menu (dialog 1135): here its advanced tree's four options
// change the classic menu; Add/Remove/Advanced/Sort/Clear are greyed.
static void showClassicCustomize(QWidget *parent)
{
    if (!explorer())
        return;
    QDialog dialog(parent);
    const xpdialog::Page page = xpdialog::build(*explorer(), 9, &dialog);
    dialog.setWindowTitle(page.caption);
    QWidget *place = page.controls.value(1123);
    auto *tree = new QTreeWidget(&dialog);
    if (place) {
        tree->setGeometry(place->geometry());
        place->hide();
    }
    tree->setHeaderHidden(true);
    tree->setRootIsDecorated(false);
    const TaskbarSettings settings = TaskbarSettings::load();
    enum Field { Favorites, LogOff, Run, SmallIcons };
    const QList<QPair<QString, QPair<Field, bool>>> options = {
        {QObject::tr("Display Favorites"), {Favorites, settings.classicFavorites}},
        {QObject::tr("Display Log Off"), {LogOff, settings.classicLogOff}},
        {QObject::tr("Display Run"), {Run, settings.classicRun}},
        {QObject::tr("Show Small Icons in Start menu"), {SmallIcons, settings.classicSmallIcons}},
    };
    for (const auto &[text, option] : options) {
        auto *item = new QTreeWidgetItem(tree, {text});
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, option.second ? Qt::Checked : Qt::Unchecked);
        item->setData(0, Qt::UserRole, int(option.first));
    }
    // Add/Remove/Advanced/Sort/Clear change the Start Menu folder; their files are not read here.
    for (int id : {1126, 1127, 1128, 1124, 1125})
        if (QWidget *control = page.controls.value(id))
            control->setEnabled(false);
    const auto save = [&] {
        TaskbarSettings changed = TaskbarSettings::load();
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            const QTreeWidgetItem *item = tree->topLevelItem(i);
            const bool on = item->checkState(0) == Qt::Checked;
            switch (Field(item->data(0, Qt::UserRole).toInt())) {
            case Favorites: changed.classicFavorites = on; break;
            case LogOff: changed.classicLogOff = on; break;
            case Run: changed.classicRun = on; break;
            case SmallIcons: changed.classicSmallIcons = on; break;
            }
        }
        changed.save();
        emit TaskbarSettingsNotifier::instance()->changed();
    };
    if (auto *ok = qobject_cast<QPushButton *>(page.controls.value(1)))
        QObject::connect(ok, &QPushButton::clicked, &dialog, [&] { save(); dialog.accept(); });
    if (auto *cancel = qobject_cast<QPushButton *>(page.controls.value(2)))
        QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    dialog.exec();
}

// explorer.exe's Customize Start Menu (property sheet pages 1135 General and 1036 Advanced): the
// Start panel's places, its program count, its icon size and its recent documents. The pages' Add/
// Remove/Advanced/Sort/Clear remain greyed.
static void showStartPanelCustomize(QWidget *parent)
{
    if (!explorer())
        return;
    QDialog dialog(parent);
    dialog.setWindowTitle(xpfiles::string(*explorer(), 1311).isEmpty() ? QObject::tr("Customize Start Menu") : xpfiles::string(*explorer(), 1311));
    auto *tabs = new xpdialog::SheetTabs(&dialog);
    auto *general = new QWidget;
    auto *advanced = new QWidget;
    const xpdialog::Page generalPage = xpdialog::build(*explorer(), 1135, general);
    const xpdialog::Page advancedPage = xpdialog::build(*explorer(), 1036, advanced);
    tabs->addTab(general, generalPage.caption.isEmpty() ? QObject::tr("General") : generalPage.caption);
    tabs->addTab(advanced, advancedPage.caption.isEmpty() ? QObject::tr("Advanced") : advancedPage.caption);
    const TaskbarSettings settings = TaskbarSettings::load();
    auto *large = qobject_cast<QAbstractButton *>(generalPage.controls.value(1302));
    auto *small = qobject_cast<QAbstractButton *>(generalPage.controls.value(1303));
    if (large && small) {
        large->setAutoExclusive(true);
        small->setAutoExclusive(true);
        (settings.panelSmallIcons ? small : large)->setChecked(true);
    }
    if (auto *count = qobject_cast<QLineEdit *>(generalPage.controls.value(1304)))
        count->setText(QString::number(settings.panelMinMFU));
    for (int id : {1310})
        if (QWidget *button = generalPage.controls.value(id))
            button->setEnabled(false);
    if (QWidget *button = advancedPage.controls.value(1309))
        button->setEnabled(false);
    if (auto *recent = qobject_cast<QAbstractButton *>(advancedPage.controls.value(1308)))
        recent->setChecked(settings.panelRecentDocs);
    if (auto *hover = qobject_cast<QAbstractButton *>(advancedPage.controls.value(1306)))
        hover->setChecked(settings.panelHoverOpen);
    QWidget *place = advancedPage.controls.value(1123);
    auto *tree = new QTreeWidget(advanced);
    if (place) {
        tree->setGeometry(place->geometry());
        place->hide();
    }
    tree->setHeaderHidden(true);
    tree->setRootIsDecorated(false);
    struct Option
    {
        const char *key;
        const char *label;
        bool favorites;
    };
    const QList<Option> options = {
        {"Documents", "My Documents", false}, {"Recent", "My Recent Documents", false}, {"Pictures", "My Pictures", false},
        {"Music", "My Music", false}, {"Computer", "My Computer", false}, {"ControlPanel", "Control Panel", false},
        {"Network", "Network Connections", false}, {"Printers", "Printers and Faxes", false}, {"Help", "Help and Support", false},
        {"Search", "Search", false}, {"Run", "Run...", false}, {"Favorites", "Favorites", true},
    };
    for (const Option &option : options) {
        auto *item = new QTreeWidgetItem(tree, {QObject::tr(option.label)});
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        const bool shown = option.favorites ? settings.panelFavorites : !settings.panelHidden.contains(QLatin1String(option.key));
        item->setCheckState(0, shown ? Qt::Checked : Qt::Unchecked);
        item->setData(0, Qt::UserRole, QString::fromLatin1(option.key));
        item->setData(1, Qt::UserRole, option.favorites);
    }
    auto *ok = new QPushButton(QObject::tr("OK"), &dialog);
    auto *cancel = new QPushButton(QObject::tr("Cancel"), &dialog);
    ok->setDefault(true);
    xpdialog::arrangeSheet(&dialog, tabs, {ok, cancel});
    QObject::connect(ok, &QPushButton::clicked, &dialog, [&] {
        TaskbarSettings changed = TaskbarSettings::load();
        changed.panelSmallIcons = small && small->isChecked();
        if (auto *count = qobject_cast<QLineEdit *>(generalPage.controls.value(1304)))
            changed.panelMinMFU = qBound(0, count->text().toInt(), 30);
        if (auto *recent = qobject_cast<QAbstractButton *>(advancedPage.controls.value(1308)))
            changed.panelRecentDocs = recent->isChecked();
        if (auto *hover = qobject_cast<QAbstractButton *>(advancedPage.controls.value(1306)))
            changed.panelHoverOpen = hover->isChecked();
        changed.panelHidden.clear();
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            const QTreeWidgetItem *item = tree->topLevelItem(i);
            const bool on = item->checkState(0) == Qt::Checked;
            if (item->data(1, Qt::UserRole).toBool())
                changed.panelFavorites = on;
            else if (!on)
                changed.panelHidden.append(item->data(0, Qt::UserRole).toString());
        }
        changed.save();
        emit TaskbarSettingsNotifier::instance()->changed();
        dialog.accept();
    });
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    dialog.exec();
}

QWidget *TaskbarProperties::startMenuPage()
{
    auto *page = new QWidget;
    if (!explorer())
        return page;
    const xpdialog::Page dialog = xpdialog::build(*explorer(), DIALOG_START_MENU, page);
    // The Start menu or the classic one; the preview is 170 or 171 (sub_1038a95). Their Customize dialogs
    // are greyed.
    m_startRadio = qobject_cast<QAbstractButton *>(dialog.controls.value(ID_START_MENU));
    m_classicRadio = qobject_cast<QAbstractButton *>(dialog.controls.value(ID_CLASSIC));
    auto *preview = qobject_cast<QLabel *>(dialog.controls.value(ID_START_PREVIEW));
    const bool classic = TaskbarSettings::load().classicStartMenu;
    if (m_startRadio && m_classicRadio) {
        m_startRadio->setAutoExclusive(true);
        m_classicRadio->setAutoExclusive(true);
        (classic ? m_classicRadio : m_startRadio)->setChecked(true);
    }
    for (int id : {ID_CUSTOMIZE, ID_CUSTOMIZE_CLASSIC})
        if (QWidget *control = dialog.controls.value(id))
            control->setEnabled(true);
    if (auto *customizeClassic = qobject_cast<QAbstractButton *>(dialog.controls.value(ID_CUSTOMIZE_CLASSIC)))
        connect(customizeClassic, &QAbstractButton::clicked, this, [this] { showClassicCustomize(this); });
    if (auto *customizeStart = qobject_cast<QAbstractButton *>(dialog.controls.value(ID_CUSTOMIZE)))
        connect(customizeStart, &QAbstractButton::clicked, this, [this] { showStartPanelCustomize(this); });
    const auto follow = [this, preview] {
        if (preview)
            preview->setPixmap(picture(BITMAP_START_MENU + (m_classicRadio && m_classicRadio->isChecked() ? 1 : 0)));
    };
    follow();
    for (QAbstractButton *radio : {m_startRadio, m_classicRadio})
        if (radio)
            connect(radio, &QAbstractButton::toggled, this, [this, follow] {
                follow();
                m_apply->setEnabled(true);
            });
    return page;
}

void TaskbarProperties::updatePreviews()
{
    const auto unchecked = [this](int id) { return m_checks.contains(id) && !m_checks.value(id)->isChecked(); };
    if (m_taskbarPreview) {
        const bool hidden = m_checks.contains(ID_AUTO_HIDE) && m_checks.value(ID_AUTO_HIDE)->isChecked();
        const int id = hidden ? BITMAP_AUTO_HIDDEN
                              : BITMAP_TASKBAR + unchecked(ID_LOCK) + 2 * unchecked(ID_GROUP) + 4 * unchecked(ID_QUICK_LAUNCH);
        m_taskbarPreview->setPixmap(picture(id));
    }
    if (m_notificationPreview)
        m_notificationPreview->setPixmap(picture(BITMAP_NOTIFICATION + unchecked(ID_CLOCK) + 2 * unchecked(ID_HIDE_INACTIVE)));
}

void TaskbarProperties::apply()
{
    TaskbarSettings settings = TaskbarSettings::load();   // and what the sheet does not show
    settings.group = m_checks.value(ID_GROUP) && m_checks.value(ID_GROUP)->isChecked();
    settings.clock = m_checks.value(ID_CLOCK) && m_checks.value(ID_CLOCK)->isChecked();
    settings.autoHide = m_checks.value(ID_AUTO_HIDE) && m_checks.value(ID_AUTO_HIDE)->isChecked();
    settings.quickLaunch = m_checks.value(ID_QUICK_LAUNCH) && m_checks.value(ID_QUICK_LAUNCH)->isChecked();
    settings.locked = m_checks.value(ID_LOCK) && m_checks.value(ID_LOCK)->isChecked();
    settings.onTop = m_checks.value(ID_ON_TOP) && m_checks.value(ID_ON_TOP)->isChecked();
    settings.hideInactive = m_checks.value(ID_HIDE_INACTIVE) && m_checks.value(ID_HIDE_INACTIVE)->isChecked();
    if (m_classicRadio)
        settings.classicStartMenu = m_classicRadio->isChecked();
    settings.save();
    m_apply->setEnabled(false);
    emit TaskbarSettingsNotifier::instance()->changed();
}

namespace traynotify {

namespace {
const int DIALOG_CUSTOMIZE = 7;
enum { ID_ITEMS = 1005, ID_BEHAVIOR = 1006, ID_RESTORE = 1402, ID_OK = 1, ID_CANCEL = 2 };
enum { STRING_NAME = 1000, STRING_BEHAVIOR = 1001, STRING_HIDE_WHEN_INACTIVE = 1002, STRING_CURRENT = 1403, STRING_PAST = 1404 };

QVariantMap behaviors()
{
    return QSettings(settingsPath(), QSettings::IniFormat).value(QStringLiteral("TrayNotify/Behaviors")).toMap();
}

std::function<QImage(const QString &)> &iconFinder()
{
    static std::function<QImage(const QString &)> lookup;
    return lookup;
}
} // namespace

Behavior behavior(const QString &name)
{
    return Behavior(behaviors().value(name, int(HideWhenInactive)).toInt());
}

void setIconLookup(std::function<QImage(const QString &)> lookup)
{
    iconFinder() = std::move(lookup);
}

void setCurrent(const QStringList &names)
{
    // Past Items: what was here before and is not now, as explorer.exe's PastIconsStream keeps them
    QSettings settings(settingsPath(), QSettings::IniFormat);
    const QStringList current = settings.value(QStringLiteral("TrayNotify/Current")).toStringList();
    if (current == names)
        return;
    QStringList past = settings.value(QStringLiteral("TrayNotify/Past")).toStringList();
    for (const QString &name : current)
        if (!names.contains(name) && !past.contains(name))
            past.prepend(name);
    for (const QString &name : names)
        past.removeAll(name);
    settings.setValue(QStringLiteral("TrayNotify/Current"), names);
    settings.setValue(QStringLiteral("TrayNotify/Past"), past);
}

void customize(QWidget *parent)
{
    if (!explorer())
        return;
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    const xpdialog::Page page = xpdialog::build(*explorer(), DIALOG_CUSTOMIZE, dialog);
    dialog->setWindowTitle(page.caption);
    // The list view: Name and Behavior, the items under Current Items and Past Items; a click on
    // an item's behaviour gives it the choice of three, as XP's in-place combo box (control 1006)
    QWidget *placeholder = page.controls.value(ID_ITEMS);
    if (!placeholder)
        return;
    auto *list = new QTreeWidget(dialog);
    list->setGeometry(placeholder->geometry());
    placeholder->hide();
    list->setRootIsDecorated(false);
    list->setHeaderLabels({text(STRING_NAME, "Name"), text(STRING_BEHAVIOR, "Behavior")});
    list->setColumnWidth(0, list->width() * 6 / 10);
    if (QWidget *combo = page.controls.value(ID_BEHAVIOR))
        combo->hide();
    const QStringList choices{text(STRING_HIDE_WHEN_INACTIVE, "Hide when inactive"), text(STRING_HIDE_WHEN_INACTIVE + 1, "Always hide"),
                              text(STRING_HIDE_WHEN_INACTIVE + 2, "Always show")};
    const QVariantMap chosen = behaviors();
    const QSettings settings(settingsPath(), QSettings::IniFormat);
    const auto fill = [&](int heading, const QStringList &names) {
        if (names.isEmpty())
            return;
        auto *group = new QTreeWidgetItem(list, {text(heading, heading == STRING_CURRENT ? "Current Items" : "Past Items")});
        QFont bold = group->font(0);
        bold.setBold(true);
        group->setFont(0, bold);
        group->setFlags(Qt::ItemIsEnabled);
        for (const QString &name : names) {
            auto *item = new QTreeWidgetItem(list, {name, choices.value(chosen.value(name, 0).toInt())});
            item->setData(0, Qt::UserRole, name);
            if (iconFinder()) {
                const QImage icon = iconFinder()(name);
                if (!icon.isNull())
                    item->setIcon(0, QIcon(QPixmap::fromImage(icon)));
            }
        }
    };
    fill(STRING_CURRENT, settings.value(QStringLiteral("TrayNotify/Current")).toStringList());
    fill(STRING_PAST, settings.value(QStringLiteral("TrayNotify/Past")).toStringList());
    QObject::connect(list, &QTreeWidget::itemClicked, dialog, [list, choices](QTreeWidgetItem *item, int) {
        const QString name = item->data(0, Qt::UserRole).toString();
        if (name.isEmpty() || list->itemWidget(item, 1))
            return;
        auto *combo = new QComboBox(list);
        combo->addItems(choices);
        combo->setCurrentIndex(choices.indexOf(item->text(1)));
        list->setItemWidget(item, 1, combo);
        QObject::connect(combo, &QComboBox::activated, list, [list, item, choices](int index) {
            item->setText(1, choices.value(index));
            list->removeItemWidget(item, 1);
        });
    });
    if (auto *restore = qobject_cast<QAbstractButton *>(page.controls.value(ID_RESTORE)))
        QObject::connect(restore, &QAbstractButton::clicked, list, [list, choices] {
            for (int i = 0; i < list->topLevelItemCount(); ++i)
                if (!list->topLevelItem(i)->data(0, Qt::UserRole).toString().isEmpty())
                    list->topLevelItem(i)->setText(1, choices.value(0));
        });
    if (auto *ok = qobject_cast<QPushButton *>(page.controls.value(ID_OK))) {
        ok->setDefault(true);
        QObject::connect(ok, &QPushButton::clicked, dialog, [dialog, list, choices] {
            QVariantMap values;
            for (int i = 0; i < list->topLevelItemCount(); ++i) {
                const QTreeWidgetItem *item = list->topLevelItem(i);
                const QString name = item->data(0, Qt::UserRole).toString();
                if (!name.isEmpty() && choices.indexOf(item->text(1)) > 0)
                    values.insert(name, choices.indexOf(item->text(1)));
            }
            QSettings(settingsPath(), QSettings::IniFormat).setValue(QStringLiteral("TrayNotify/Behaviors"), values);
            emit TaskbarSettingsNotifier::instance()->changed();
            dialog->accept();
        });
    }
    if (auto *cancel = qobject_cast<QPushButton *>(page.controls.value(ID_CANCEL)))
        QObject::connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    dialog->setModal(true);
    dialog->show();
}

} // namespace traynotify
