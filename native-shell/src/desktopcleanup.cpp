// SPDX-License-Identifier: LGPL-2.1-or-later
#include "desktopcleanup.h"

#include "desktopicons.h"
#include "fileicons.h"
#include "fileops.h"
#include "programs.h"
#include "xpdialog.h"
#include "xpfiles.h"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTreeWidget>

namespace {

// fldrclnr.dll's strings: the completing page's text (1 with the count, 2 for one, 3 for none chosen, 4 for
// none found), "Never", the reminder's title and text, the folder's name, the Shortcuts page's header,
// its list's columns
const int STRING_COUNT = 1, STRING_ONE = 2, STRING_NONE_CHOSEN = 3, STRING_NONE_FOUND = 4, STRING_NEVER = 5;
const int STRING_REMINDER_TITLE = 6, STRING_REMINDER = 7, STRING_FOLDER = 8;
const int STRING_HEADER_TITLE = 9, STRING_HEADER_SUBTITLE = 10, STRING_DATE_COLUMN = 11, STRING_NAME_COLUMN = 12;
// Its pages (Welcome, Shortcuts, Completing), their controls, the watermark and header bitmaps and its icon
const int DIALOG_WELCOME = 101, DIALOG_SHORTCUTS = 102, DIALOG_COMPLETING = 103;
const int CONTROL_LIST = 1010, CONTROL_TITLE = 1011, CONTROL_TEXT = 1012, CONTROL_LIST_LABEL = 1013, CONTROL_BACK_HINT = 1014;
const int BITMAP_WATERMARK = 200, BITMAP_HEADER = 201, ICON_WIZARD = 100;
// explorer.exe's and fldrclnr's days between cleanups when none is set, and how far back explorer puts the
// first "last cleanup" (sub_1023b85+0x1549: -30 days, so the first reminder comes 30 days after the first logon)
const int DEFAULT_DAYS = 60, FIRST_DAYS_BACK = 30;
// The Shortcuts page's list (sub_693d2772): the name column 200 pixels, the date's the rest
const int NAME_COLUMN_WIDTH = 200;
// The welcome and completing pages' title: string 20 (Verdana Bold), 12 points, weight 700 (sub_693d3952)
const int TITLE_POINTS = 12;
// comctl32's Wizard97 sheet, measured on XP's comctl32 in Wine: the pages' area 193 dialog units high, the
// header's line 35 units down, interior pages 7 units in from the sides and under that line; buttons
// 55 x 14 units, Back against Next, 12 pixels between Next and Cancel, under the area's line and to the
// right edge. The header's title and subtitle (its bold font and its own) and the 49-pixel header bitmap
// at its right, centred in the band.
const int AREA_HEIGHT = 193, HEADER_LINE = 35, INTERIOR_MARGIN = 7, BUTTON_WIDTH = 55, BUTTON_HEIGHT = 14;
const int BUTTON_GAP = 12, BUTTON_MARGIN = 12;
const QPoint HEADER_TITLE(20, 10), HEADER_SUBTITLE(40, 24);

QString explorerSettingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/native-explorer.conf");
}

// Explorer\Desktop\CleanupWiz's values
QVariant cleanupValue(const QString &name, const QVariant &fallback = {})
{
    return QSettings(explorerSettingsPath(), QSettings::IniFormat).value(QStringLiteral("CleanupWiz/") + name, fallback);
}

void setCleanupValue(const QString &name, const QVariant &value)
{
    QSettings(explorerSettingsPath(), QSettings::IniFormat).setValue(QStringLiteral("CleanupWiz/") + name, value);
}

// fldrclnr's sub_693d2c7b: "Days between clean up", 60 when it is negative or missing
int daysBetween()
{
    const int days = cleanupValue(QStringLiteral("DaysBetweenCleanup"), DEFAULT_DAYS).toInt();
    return days < 0 ? DEFAULT_DAYS : days;
}

const std::optional<w4w::PeResources> &fldrclnr()
{
    static const auto resources = xpfiles::resources(xpfiles::find(QStringLiteral("fldrclnr.dll")));
    return resources;
}

QString text(int id, const char *fallback)
{
    const QString found = fldrclnr() ? xpfiles::string(*fldrclnr(), id) : QString();
    return found.isEmpty() ? QString::fromLatin1(fallback) : found;
}

QString folderPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)).filePath(text(STRING_FOLDER, "Unused Desktop Shortcuts"));
}

// SHFormatDateTime with FDTF_SHORTDATE (sub_693d2986): the locale's short date with the whole year, as
// XP's sShortDate has it; "Never" for a shortcut never run
QString dateText(const QDateTime &when)
{
    if (!when.isValid())
        return text(STRING_NEVER, "Never");
    const QLocale locale = QLocale::system();
    QString format = locale.dateFormat(QLocale::ShortFormat);
    if (!format.contains(QLatin1String("yyyy")))
        format.replace(QLatin1String("yy"), QLatin1String("yyyy"));
    return locale.toString(when.date(), format);
}

// The interior pages' white band (comctl32's Wizard97 header): its title in the dialog font's bold, its
// subtitle under it, the header bitmap at its right, and its etched line
class Header : public QWidget
{
public:
    Header(const QString &title, const QString &subtitle, QWidget *parent)
        : QWidget(parent)
        , m_title(title)
        , m_subtitle(subtitle)
    {
        if (fldrclnr())
            m_bitmap = xpfiles::bitmap(*fldrclnr(), BITMAP_HEADER);
        auto *line = new QFrame(this);
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        m_line = line;
    }

protected:
    void resizeEvent(QResizeEvent *) override
    {
        m_line->setGeometry(0, height() - 2, width(), 2);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const int band = height() - 2;
        painter.fillRect(0, 0, width(), band, Qt::white);
        int right = width();
        if (!m_bitmap.isNull()) {
            const int margin = (band - m_bitmap.height()) / 2;
            right = width() - margin - m_bitmap.width();
            painter.drawImage(right, margin, m_bitmap);
            right -= margin;
        }
        painter.setPen(Qt::black);
        QFont bold = font();
        bold.setBold(true);
        painter.setFont(bold);
        painter.drawText(QRect(HEADER_TITLE, QPoint(right, band)), Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, m_title);
        painter.setFont(font());
        painter.drawText(QRect(HEADER_SUBTITLE, QPoint(right, band)), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_subtitle);
    }

private:
    QString m_title, m_subtitle;
    QImage m_bitmap;
    QWidget *m_line = nullptr;
};

// The welcome and completing pages: white, with the watermark down their left
class Exterior : public QWidget
{
public:
    explicit Exterior(QWidget *parent)
        : QWidget(parent)
    {
        if (fldrclnr())
            m_watermark = xpfiles::bitmap(*fldrclnr(), BITMAP_WATERMARK);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::white);
        if (!m_watermark.isNull())
            painter.drawImage(0, 0, m_watermark);
    }

private:
    QImage m_watermark;
};

class Wizard : public QDialog
{
public:
    explicit Wizard(const QList<desktopcleanup::Shortcut> &shortcuts);

    // fldrclnr writes its time once the wizard is gone, however it ended
    ~Wizard() override { desktopcleanup::stamp(); }

private:
    void go(int page);
    void fillCompleting();
    void finish();
    QStringList chosen() const;

    QList<desktopcleanup::Shortcut> m_shortcuts;
    QWidget *m_pages[3] = {};
    Header *m_header = nullptr;
    QTreeWidget *m_list = nullptr;
    QListWidget *m_chosenList = nullptr;
    xpdialog::Page m_completing;
    QPushButton *m_back = nullptr, *m_next = nullptr, *m_finish = nullptr, *m_cancel = nullptr;
    int m_page = 0;
};

Wizard::Wizard(const QList<desktopcleanup::Shortcut> &shortcuts)
    : m_shortcuts(shortcuts)
{
    setAttribute(Qt::WA_DeleteOnClose);
    const int areaHeight = xpdialog::unitY(AREA_HEIGHT), lineY = xpdialog::unitY(HEADER_LINE);
    const int marginX = xpdialog::unitX(INTERIOR_MARGIN), marginY = xpdialog::unitY(INTERIOR_MARGIN);
    QFont titleFont(QStringLiteral("Verdana"));
    titleFont.setPixelSize(TITLE_POINTS * 96 / 72);
    titleFont.setBold(true);

    // Welcome
    auto *welcome = new Exterior(this);
    const xpdialog::Page welcomePage = xpdialog::build(*fldrclnr(), DIALOG_WELCOME, welcome);
    setWindowTitle(welcomePage.caption);
    setFont(welcome->font());
    const int width = welcome->width() + 1;
    welcome->setFixedSize(welcome->width(), areaHeight);
    if (QWidget *title = welcomePage.controls.value(CONTROL_TITLE))
        title->setFont(titleFont);
    m_pages[0] = welcome;

    // Shortcuts: the header and the page under it
    m_header = new Header(text(STRING_HEADER_TITLE, "Shortcuts"), text(STRING_HEADER_SUBTITLE, ""), this);
    m_header->setFont(font());
    m_header->setGeometry(0, 0, width, lineY + 2);
    auto *list = new QWidget(this);
    const xpdialog::Page listPage = xpdialog::build(*fldrclnr(), DIALOG_SHORTCUTS, list);
    list->setFixedSize(width - 2 * marginX, list->height());
    list->move(marginX, lineY + 2 + marginY);
    m_pages[1] = list;
    m_list = new QTreeWidget(list);
    if (QWidget *place = listPage.controls.value(CONTROL_LIST)) {
        m_list->setGeometry(place->geometry());
        place->hide();
        // "Shortcuts:" goes to the list that stands there
        for (QLabel *label : list->findChildren<QLabel *>())
            if (label->buddy() == place)
                label->setBuddy(m_list);
    }
    m_list->setRootIsDecorated(false);
    m_list->setIconSize(QSize(16, 16));
    m_list->setHeaderLabels({text(STRING_NAME_COLUMN, "Shortcut to Clean Up"), text(STRING_DATE_COLUMN, "Date Last Used")});
    m_list->header()->setSectionsClickable(false);
    m_list->header()->setStretchLastSection(true);
    m_list->setColumnWidth(0, NAME_COLUMN_WIDTH);
    for (const desktopcleanup::Shortcut &shortcut : std::as_const(m_shortcuts)) {
        auto *item = new QTreeWidgetItem(m_list, {shortcut.name, dateText(shortcut.lastUsed)});
        item->setIcon(0, QIcon(QPixmap::fromImage(shortcut.icon)));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, shortcut.unused ? Qt::Checked : Qt::Unchecked);
    }

    // Completing
    auto *completing = new Exterior(this);
    m_completing = xpdialog::build(*fldrclnr(), DIALOG_COMPLETING, completing);
    completing->setFixedSize(completing->width(), areaHeight);
    if (QWidget *title = m_completing.controls.value(CONTROL_TITLE))
        title->setFont(titleFont);
    // an LVS_LIST view of the names chosen, with their small icons
    m_chosenList = new QListWidget(completing);
    if (QWidget *place = m_completing.controls.value(CONTROL_LIST)) {
        m_chosenList->setGeometry(place->geometry());
        place->hide();
        m_completing.controls.insert(CONTROL_LIST, m_chosenList);
    }
    m_chosenList->setViewMode(QListView::ListMode);
    m_chosenList->setFlow(QListView::TopToBottom);
    m_chosenList->setWrapping(true);
    m_chosenList->setResizeMode(QListView::Adjust);
    m_chosenList->setIconSize(QSize(16, 16));
    m_chosenList->setSelectionMode(QAbstractItemView::NoSelection);
    m_pages[2] = completing;

    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    line->setGeometry(0, areaHeight, width, 2);

    const int buttonWidth = xpdialog::unitX(BUTTON_WIDTH), buttonHeight = xpdialog::unitY(BUTTON_HEIGHT);
    const int buttonY = areaHeight + BUTTON_MARGIN;
    m_back = new QPushButton(QObject::tr("< &Back"), this);
    m_next = new QPushButton(QObject::tr("&Next >"), this);
    m_finish = new QPushButton(QObject::tr("Finish"), this);
    m_cancel = new QPushButton(QObject::tr("Cancel"), this);
    const int cancelX = width - BUTTON_MARGIN - buttonWidth, nextX = cancelX - BUTTON_GAP - buttonWidth;
    m_cancel->setGeometry(cancelX, buttonY, buttonWidth, buttonHeight);
    m_next->setGeometry(nextX, buttonY, buttonWidth, buttonHeight);
    m_finish->setGeometry(nextX, buttonY, buttonWidth, buttonHeight);
    m_back->setGeometry(nextX - buttonWidth, buttonY, buttonWidth, buttonHeight);
    setFixedSize(width, buttonY + buttonHeight + BUTTON_MARGIN);

    connect(m_back, &QPushButton::clicked, this, [this] { go(m_page - 1); });
    connect(m_next, &QPushButton::clicked, this, [this] { go(m_page + 1); });
    connect(m_finish, &QPushButton::clicked, this, [this] {
        finish();
        close();
    });
    connect(m_cancel, &QPushButton::clicked, this, &QDialog::close);
    // PropertySheet's nStartPage: the completing page when there is nothing to list
    go(m_shortcuts.isEmpty() ? 2 : 0);
}

void Wizard::go(int page)
{
    m_page = qBound(0, page, 2);
    for (int i = 0; i < 3; ++i)
        m_pages[i]->setVisible(i == m_page);
    m_header->setVisible(m_page == 1);
    if (m_page == 2)
        fillCompleting();
    m_back->setEnabled(m_page > 0 && !m_shortcuts.isEmpty());
    m_next->setVisible(m_page < 2);
    m_finish->setVisible(m_page == 2);
    (m_page == 2 ? m_finish : m_next)->setDefault(true);
    (m_page == 2 ? m_next : m_finish)->setDefault(false);
    (m_page == 2 ? m_finish : m_next)->setFocus();
}

// sub_693d2e7c: the names chosen, and the words for how many
void Wizard::fillCompleting()
{
    m_chosenList->clear();
    int count = 0;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        const QTreeWidgetItem *item = m_list->topLevelItem(i);
        if (item->checkState(0) != Qt::Checked)
            continue;
        new QListWidgetItem(item->icon(0), item->text(0), m_chosenList);
        ++count;
    }
    QString words;
    if (m_shortcuts.isEmpty())
        words = text(STRING_NONE_FOUND, "Windows did not find any desktop shortcuts to clean up.");
    else if (count == 0)
        words = text(STRING_NONE_CHOSEN, "Windows will not move any shortcuts to the Unused Desktop Shortcuts folder on your desktop.");
    else if (count == 1)
        words = text(STRING_ONE, "Windows will move the following shortcut to the Unused Desktop Shortcuts folder on your desktop.");
    else
        words = text(STRING_COUNT, "Windows will move the following %d shortcuts to the Unused Desktop Shortcuts folder on your desktop.")
                    .replace(QLatin1String("%d"), QString::number(count));
    if (auto *label = qobject_cast<QLabel *>(m_completing.controls.value(CONTROL_TEXT)))
        label->setText(words);
    for (int id : {CONTROL_LIST_LABEL, CONTROL_LIST, CONTROL_BACK_HINT})
        if (QWidget *control = m_completing.controls.value(id))
            control->setVisible(count > 0);
}

QStringList Wizard::chosen() const
{
    QStringList paths;
    for (int i = 0; i < m_list->topLevelItemCount() && i < m_shortcuts.size(); ++i)
        if (m_list->topLevelItem(i)->checkState(0) == Qt::Checked)
            paths.append(m_shortcuts[i].path);
    return paths;
}

// sub_693d33a1: the folder made on the desktop, the shortcuts moved into it with the shell's progress;
// Internet Explorer, which is no file, leaves the desktop and is put there as a shortcut
void Wizard::finish()
{
    const QStringList paths = chosen();
    if (paths.isEmpty())
        return;
    const QString folder = folderPath();
    QDir().mkpath(folder);
    QStringList files;
    for (const QString &path : paths) {
        if (path == QLatin1String("::internet")) {
            setDesktopItemShown(DesktopItem::Internet, false);
            if (const auto browser = programs::defaultBrowser(); browser && !browser->file.isEmpty())
                QFile::copy(browser->file, QDir(folder).filePath(QFileInfo(browser->file).fileName()));
        } else {
            files.append(path);
        }
    }
    if (!files.isEmpty())
        fileops::transfer(files, folder, fileops::Action::Move, nullptr);
}

} // namespace

namespace desktopcleanup {

bool due()
{
    if (!enabled())
        return false;
    const QDateTime now = QDateTime::currentDateTime();
    qint64 last = cleanupValue(QStringLiteral("LastUsedTime"), 0).toLongLong();
    if (last <= 0) {
        last = now.addDays(-FIRST_DAYS_BACK).toMSecsSinceEpoch();
        setCleanupValue(QStringLiteral("LastUsedTime"), last);
    }
    // explorer takes a value of 0 days or less as no wait at all
    const int days = cleanupValue(QStringLiteral("DaysBetweenCleanup"), DEFAULT_DAYS).toInt();
    if (days <= 0)
        return true;
    return now > QDateTime::fromMSecsSinceEpoch(last).addDays(days);
}

bool enabled()
{
    return !cleanupValue(QStringLiteral("NoRun"), false).toBool();
}

void setEnabled(bool enabled)
{
    setCleanupValue(QStringLiteral("NoRun"), !enabled);
}

QList<Shortcut> shortcuts(bool all)
{
    const QDateTime limit = QDateTime::currentDateTime().addDays(-daysBetween());
    QList<Shortcut> found;
    const auto add = [&](Shortcut shortcut) {
        shortcut.lastUsed = programs::lastRun(shortcut.path);
        shortcut.unused = !shortcut.lastUsed.isValid() || shortcut.lastUsed < limit;
        if (all || shortcut.unused)
            found.append(shortcut);
    };
    // The desktop's own items come first, as the desktop folder enumerates them; of those only Internet
    // Explorer: My Computer, My Documents, My Network Places and the Recycle Bin are never moved
    if (desktopItemShown(DesktopItem::Internet))
        if (const auto browser = programs::defaultBrowser())
            add({QStringLiteral("::internet"), browser->name, programs::iconOf(*browser).pixmap(16, 16).toImage(), {}, false});
    // Its shortcuts: the desktop entries that start a program
    const QFileInfoList files = QDir(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation))
                                    .entryInfoList({QStringLiteral("*.desktop")}, QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &file : files)
        if (const auto program = programs::fromFile(file.absoluteFilePath()))
            add({file.absoluteFilePath(), program->name.isEmpty() ? file.completeBaseName() : program->name,
                 fileicons::image(file, 16), {}, false});
    return found;
}

void stamp()
{
    setCleanupValue(QStringLiteral("LastUsedTime"), QDateTime::currentMSecsSinceEpoch());
    setCleanupValue(QStringLiteral("DaysBetweenCleanup"), daysBetween());
}

QImage reminderIcon(int size)
{
    return fldrclnr() ? xpfiles::icon(*fldrclnr(), ICON_WIZARD, size) : QImage();
}

QString reminderTitle()
{
    return text(STRING_REMINDER_TITLE, "There are unused icons on your desktop");
}

QString reminderText()
{
    return text(STRING_REMINDER, "The desktop cleanup wizard can help you clean up your desktop. Click this balloon to start the wizard.");
}

void showWizard(const QList<Shortcut> &shortcuts)
{
    if (!fldrclnr())
        return;
    auto *wizard = new Wizard(shortcuts);
    // over a modal dialog (Display Properties' Desktop Items), it has to be modal too to be used
    if (QApplication::activeModalWidget())
        wizard->setWindowModality(Qt::ApplicationModal);
    wizard->show();
}

} // namespace desktopcleanup
