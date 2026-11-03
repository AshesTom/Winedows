// SPDX-License-Identifier: LGPL-2.1-or-later
#include "taskbarproperties.h"

#include "xpdialog.h"
#include "xpfiles.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
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

QPixmap picture(int id)
{
    return explorer() ? QPixmap::fromImage(xpfiles::bitmap(*explorer(), id)) : QPixmap();
}

} // namespace

TaskbarSettings TaskbarSettings::load()
{
    const QSettings settings(settingsPath(), QSettings::IniFormat);
    TaskbarSettings loaded;
    loaded.group = settings.value(QStringLiteral("Taskbar/TaskbarGlomming"), true).toBool();
    loaded.clock = !settings.value(QStringLiteral("Taskbar/HideClock"), false).toBool();
    return loaded;
}

void TaskbarSettings::save() const
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Taskbar/TaskbarGlomming"), group);
    settings.setValue(QStringLiteral("Taskbar/HideClock"), !clock);
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
    // What this taskbar is and cannot change: locked, on top, nothing hidden, no Quick Launch
    const QHash<int, bool> states{
        {ID_LOCK, true}, {ID_AUTO_HIDE, false}, {ID_ON_TOP, true}, {ID_GROUP, settings.group}, {ID_QUICK_LAUNCH, false},
        {ID_CLOCK, settings.clock}, {ID_HIDE_INACTIVE, false},
    };
    for (auto it = states.begin(); it != states.end(); ++it)
        if (QCheckBox *check = m_checks.value(it.key())) {
            check->setChecked(it.value());
            check->setEnabled(it.key() == ID_GROUP || it.key() == ID_CLOCK);
            connect(check, &QCheckBox::toggled, this, [this] {
                updatePreviews();
                m_apply->setEnabled(true);
            });
        }
    if (QWidget *customize = dialog.controls.value(ID_CUSTOMIZE_NOTIFICATIONS))
        customize->setEnabled(false);
    return page;
}

QWidget *TaskbarProperties::startMenuPage()
{
    auto *page = new QWidget;
    if (!explorer())
        return page;
    const xpdialog::Page dialog = xpdialog::build(*explorer(), DIALOG_START_MENU, page);
    // This shell has XP's Start menu, not the classic one, nor their Customize dialogs.
    if (auto *start = qobject_cast<QAbstractButton *>(dialog.controls.value(ID_START_MENU)))
        start->setChecked(true);
    for (int id : {ID_CLASSIC, ID_CUSTOMIZE, ID_CUSTOMIZE_CLASSIC})
        if (QWidget *control = dialog.controls.value(id))
            control->setEnabled(false);
    if (auto *preview = qobject_cast<QLabel *>(dialog.controls.value(ID_START_PREVIEW)))
        preview->setPixmap(picture(BITMAP_START_MENU));
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
    TaskbarSettings settings;
    settings.group = m_checks.value(ID_GROUP) && m_checks.value(ID_GROUP)->isChecked();
    settings.clock = m_checks.value(ID_CLOCK) && m_checks.value(ID_CLOCK)->isChecked();
    settings.save();
    m_apply->setEnabled(false);
    emit TaskbarSettingsNotifier::instance()->changed();
}
