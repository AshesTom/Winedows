// SPDX-License-Identifier: LGPL-2.1-or-later
#include "controlpanel.h"

#include "applets.h"
#include "displayproperties.h"
#include "fileicons.h"
#include "filebrowser.h"
#include "theme.h"
#include "xpfiles.h"

#include <QDir>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QToolTip>

namespace controlpanel {
namespace {

// shell32.dll: each category's name, its description just after, and its icon
const struct { int string, icon; const char *fallback; } CATEGORIES[] = {
    {32004, 270, "Appearance and Themes"},
    {32010, 273, "Network and Internet Connections"},
    {32006, 271, "Add or Remove Programs"},
    {32018, 277, "Sounds, Speech, and Audio Devices"},
    {32014, 275, "Performance and Maintenance"},
    {32008, 272, "Printers and Other Hardware"},
    {32002, 269, "User Accounts"},
    {32016, 276, "Date, Time, Language, and Regional Options"},
    {32000, 268, "Accessibility Options"},
};
QIcon cplIcon(const char *file, int group, int fallback)
{
    QIcon icon;
    if (const auto resources = xpfiles::resources(xpfiles::find(QLatin1String(file))))
        for (int size : {16, 32, 48}) {
            const QImage image = xpfiles::icon(*resources, group, size);
            if (!image.isNull())
                icon.addPixmap(QPixmap::fromImage(image));
        }
    return icon.isNull() ? fileicons::shell(fallback) : icon;
}

QString cplString(const char *file, int id, const char *fallback)
{
    const auto resources = xpfiles::resources(xpfiles::find(QLatin1String(file)));
    QString text = resources ? xpfiles::string(*resources, id) : QString();
    text.remove(QChar(0));
    return text.isEmpty() ? QLatin1String(fallback) : text;
}

bool installed(const char *program)
{
    return !QStandardPaths::findExecutable(QLatin1String(program)).isEmpty();
}

} // namespace

QList<Category> categories()
{
    QList<Category> out;
    for (const auto &entry : CATEGORIES)
        out.append({xpfiles::shellString(entry.string, entry.fallback), xpfiles::shellString(entry.string + 1, ""), fileicons::shell(entry.icon)});
    return out;
}

QList<Applet> applets()
{
    QList<Applet> out;
    const auto add = [&](const char *id, const QString &name, const char *description, const QIcon &icon, int category) {
        out.append({QLatin1String(id), name, QObject::tr(description), icon, category});
    };
    add("display", cplString("desk.cpl", 41, "Display"), QT_TR_NOOP("Change the appearance of your desktop, such as the background, the theme and the look of Linux programs."), cplIcon("desk.cpl", 100, 22), Appearance);
    add("folders", QObject::tr("Folder Options"), QT_TR_NOOP("Customize the display of files and folders."), fileicons::shell(fileicons::Folder), Appearance);
    add("fonts", QObject::tr("Fonts"), QT_TR_NOOP("Show and manage the fonts on your computer."), fileicons::shell(39), Appearance);
    if (installed("nmcli") || installed("nm-connection-editor"))
        add("network", cplString("ncpa.cpl", 2001, "Network Connections"), QT_TR_NOOP("Connects to other computers, networks, and the Internet."), cplIcon("ncpa.cpl", 1001, 18), Network);
    add("programs", QObject::tr("Add or Remove Programs"), QT_TR_NOOP("Shows the programs installed on your computer."), cplIcon("appwiz.cpl", 1500, 3), Programs);
    if (installed("pactl"))
        add("sound", QObject::tr("Sounds and Audio Devices"), QT_TR_NOOP("Change the sound volume and the output device."), cplIcon("mmsys.cpl", 3004, 41), Sounds);
    add("system", cplString("sysdm.cpl", 1, "System"), QT_TR_NOOP("See information about your computer system."), cplIcon("sysdm.cpl", 1, 16), Performance);
    add("mouse", cplString("main.cpl", 100, "Mouse"), QT_TR_NOOP("Customize your mouse settings, such as the button configuration and pointer speed."), cplIcon("main.cpl", 100, 3), Hardware);
    add("keyboard", cplString("main.cpl", 102, "Keyboard"), QT_TR_NOOP("Customize your keyboard settings, such as the cursor blink rate and the character repeat rate."), cplIcon("main.cpl", 104, 3), Hardware);
    if (installed("system-config-printer"))
        add("printers", xpfiles::shellString(9319, "Printers and Faxes"), QT_TR_NOOP("Shows installed printers and fax printers."), fileicons::shell(fileicons::Printer), Hardware);
    add("users", cplString("nusrmgr.cpl", 101, "User Accounts"), QT_TR_NOOP("Change your picture on the Welcome screen and the Start menu."), cplIcon("nusrmgr.cpl", 200, 269), Users);
    if (installed("timedatectl"))
        add("datetime", cplString("timedate.cpl", 300, "Date and Time"), QT_TR_NOOP("Set the date, time, and time zone for your computer."), cplIcon("timedate.cpl", 200, 276), DateTime);
    return out;
}

void open(const QString &id)
{
    if (id == QLatin1String("display"))
        DisplayProperties::showTab(DisplayProperties::Themes);
    else if (id == QLatin1String("display-desktop"))
        DisplayProperties::showTab(DisplayProperties::Desktop);
    else if (id == QLatin1String("fonts")) {
        const QString folder = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/fonts");
        QDir().mkpath(folder);
        FileBrowser::open(folder);
    } else if (id == QLatin1String("printers"))
        QProcess::startDetached(QStringLiteral("system-config-printer"), {});
    else
        applets::open(id);
}

} // namespace controlpanel

namespace {

// The shell style's resources: the Control Panel's watermark, and the computer behind a system folder's warning
const int BITMAP_PANEL_WATERMARK = 26, BITMAP_WARNING_WATERMARK = 25;
// At 96 DPI, from the two UI files, measured on XP where they leave it open: margins between
// items collapse, and those at the container's edge do not count. A category's icon is 48
// pixels, an applet's and the banner's 32.
const int PADDING = 20, DIRECTIVE_TOP = 30, DIRECTIVE_HEIGHT = 36, LIST_TOP = 12, ITEM_MARGIN = 12, ITEM_PADDING = 1, CATEGORY_ICON = 48, ICON = 32, ICON_GAP = 5, TASK_ICON = 16, BANNER = 35;
const int BANNER_ICON_X = 10, BANNER_ICON_Y = 7, BANNER_TEXT_X = 50;
// gridlayout(5, 2): the categories go down the first column first
const int CATEGORY_ROWS = 5, DIRECTIVE_INDENT = 3;
// shell32.dll's "Pick a category", "Pick a task...", "Pick a Control Panel icon", "or pick a Control Panel icon"
const int STRING_PICK_CATEGORY = 32513, STRING_PICK_TASK = 32514, STRING_PICK_ICON = 32515, STRING_OR_PICK_ICON = 32516;

// A category's tasks that do something here: shell32.dll's words, and the applet they open
const struct { int category, string; const char *applet; } TASKS[] = {
    {controlpanel::Appearance, 32040, "display"},
    {controlpanel::Appearance, 32044, "display-desktop"},
    {controlpanel::Sounds, 32098, "sound"},
    {controlpanel::Performance, 32177, "system"},
    {controlpanel::Users, 32161, "users"},
    {controlpanel::DateTime, 32120, "datetime"},
};

QColor colourIn(const QString &text, const QString &pattern, const QColor &fallback)
{
    const auto match = QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption).match(text);
    return match.hasMatch() ? QColor(match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt()) : fallback;
}

// Franklin Gothic Medium has no bold: GDI makes one by drawing it again a pixel to the right,
// and each letter takes a pixel more. (Qt's made-up bold closes the dot of an "i".)
QFont madeUpBold(ShellTheme *theme)
{
    QFont font = theme->font(QStringLiteral("Franklin Gothic Medium, 21"));
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1);
    return font;
}

void drawMadeUpBold(QPainter &painter, const QRect &rect, int flags, const QString &text, QRect *used = nullptr)
{
    painter.drawText(rect.translated(1, 0), flags, text);
    painter.drawText(rect, flags, text, used);
}

const QString RGB = QStringLiteral("\\(\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*\\)");
const QString ARGB = QStringLiteral("argb\\(\\s*0\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*\\)");

} // namespace

ControlPanelCategories::ControlPanelCategories(std::function<void(int)> openCategory, QWidget *parent)
    : QWidget(parent)
    , m_openCategory(std::move(openCategory))
{
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    loadStyle();
    connect(ShellTheme::instance(), &ShellTheme::changed, this, [this] {
        loadStyle();
        rebuild();
    });
}

void ControlPanelCategories::loadStyle()
{
    ShellTheme *theme = ShellTheme::instance();
    const LookChoice look = theme->choice();
    const QString file = ShellTheme::shellStylePath(look);
    m_watermark = m_bullet = QImage();
    const auto resources = xpfiles::resources(file);
    if (!resources) {
        // Windows Classic: the system colours
        m_background = m_separatorBottom = theme->system(QStringLiteral("window"), Qt::white);
        m_separatorTop = theme->system(QStringLiteral("btnshadow"), Qt::gray);
        m_headerLeft = theme->frame().activeCaption;
        m_headerRight = theme->frame().activeGradient;
        m_directive = theme->system(QStringLiteral("highlight"), QColor(10, 36, 106));
        m_link = Qt::black;
        return;
    }
    const QString ui = QString::fromLatin1(resources->find(QStringLiteral("UIFILE"), QStringLiteral("2")));
    m_background = colourIn(ui, QStringLiteral("resid=CategoryListSS>.*?atom\\(view\\)\\]\\s*\\{\\s*background:\\s*rgb") + RGB, QColor(99, 117, 214));
    const auto separator = QRegularExpression(QStringLiteral("atom\\(separator\\)\\]\\s*\\{\\s*background:\\s*gradient\\(") + ARGB + QStringLiteral(",\\s*") + ARGB, QRegularExpression::CaseInsensitiveOption).match(ui);
    m_separatorTop = separator.hasMatch() ? QColor(separator.captured(1).toInt(), separator.captured(2).toInt(), separator.captured(3).toInt()) : QColor(148, 187, 255);
    m_separatorBottom = separator.hasMatch() ? QColor(separator.captured(4).toInt(), separator.captured(5).toInt(), separator.captured(6).toInt()) : m_background;
    const auto header = QRegularExpression(QStringLiteral("atom\\(header\\)\\]\\s*\\{\\s*background:\\s*gradient\\(") + ARGB + QStringLiteral(",\\s*") + ARGB, QRegularExpression::CaseInsensitiveOption).match(ui);
    m_headerLeft = header.hasMatch() ? QColor(header.captured(1).toInt(), header.captured(2).toInt(), header.captured(3).toInt()) : QColor(51, 102, 204);
    m_headerRight = header.hasMatch() ? QColor(header.captured(4).toInt(), header.captured(5).toInt(), header.captured(6).toInt()) : m_background;
    m_directive = colourIn(ui, QStringLiteral("resid=DirectiveSS>.*?foreground:\\s*rgb") + RGB, QColor(214, 223, 245));
    m_link = Qt::white;
    m_watermark = xpfiles::bitmap(*resources, BITMAP_PANEL_WATERMARK);
    m_bullet = QImage::fromData(resources->find(QStringLiteral("23"), QStringLiteral("TASKBULLET.GIF")));
}

void ControlPanelCategories::setCategory(int category)
{
    m_category = category;
    rebuild();
}

void ControlPanelCategories::resizeEvent(QResizeEvent *)
{
    rebuild();
}

void ControlPanelCategories::rebuild()
{
    m_entries.clear();
    m_hot = -1;
    ShellTheme *theme = ShellTheme::instance();
    const int padding = theme->scaled(PADDING), margin = theme->scaled(ITEM_MARGIN), itemPadding = theme->scaled(ITEM_PADDING);
    const int top = m_category >= 0 ? theme->scaled(BANNER) : 0;
    const int width = qMax(100, this->width() - 1 - 2 * padding);
    int y = top + padding;
    m_directiveY = y;
    m_directive2Y = -1;
    // Two columns; ROWS fills the first column first, without it each row is filled in turn.
    const auto grid = [&](const QList<Entry> &entries, int iconSize, int rows) {
        const int columnWidth = (width - margin) / 2, height = iconSize + 2 * itemPadding, pitch = height + margin;
        for (int i = 0; i < entries.size(); ++i) {
            Entry entry = entries[i];
            const int column = rows > 0 ? i / rows : i % 2, row = rows > 0 ? i % rows : i / 2;
            entry.rect = QRect(1 + padding + column * (columnWidth + margin), y + row * pitch, columnWidth, height);
            entry.iconSize = iconSize;
            m_entries.append(entry);
        }
        const int used = rows > 0 ? qMin(rows, int(entries.size())) : (int(entries.size()) + 1) / 2;
        y += used > 0 ? used * pitch - margin : 0;
    };
    y += theme->scaled(DIRECTIVE_HEIGHT) + theme->scaled(LIST_TOP);
    if (m_category < 0) {
        QList<Entry> items;
        const auto list = controlpanel::categories();
        for (int i = 0; i < list.size(); ++i) {
            Entry entry;
            entry.text = list[i].name;
            entry.description = list[i].description;
            entry.icon = list[i].icon;
            entry.category = i;
            items.append(entry);
        }
        grid(items, theme->scaled(CATEGORY_ICON), CATEGORY_ROWS);
    } else {
        // verticalflowlayout: one task under the other
        bool tasks = false;
        const int taskHeight = theme->scaled(TASK_ICON) + 2 * itemPadding;
        for (const auto &task : TASKS) {
            if (task.category != m_category)
                continue;
            tasks = true;
            Entry entry;
            entry.rect = QRect(1 + padding, y, width, taskHeight);
            entry.text = xpfiles::shellString(task.string, "");
            entry.description = xpfiles::shellString(task.string + 1, "");
            const QString applet = QLatin1String(task.applet);
            entry.action = [applet] { controlpanel::open(applet); };
            m_entries.append(entry);
            y += taskHeight + margin;
        }
        if (tasks) {
            y += theme->scaled(DIRECTIVE_TOP) - margin;
            m_directive2Y = y;
            y += theme->scaled(DIRECTIVE_HEIGHT) + theme->scaled(LIST_TOP);
        }
        QList<Entry> items;
        for (const controlpanel::Applet &applet : controlpanel::applets()) {
            if (applet.category != m_category)
                continue;
            Entry entry;
            entry.text = applet.name;
            entry.description = applet.description;
            entry.icon = applet.icon;
            const QString id = applet.id;
            entry.action = [id] { controlpanel::open(id); };
            items.append(entry);
        }
        grid(items, theme->scaled(ICON), 0);
    }
    setMinimumHeight(y + padding);
    update();
}

void ControlPanelCategories::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    ShellTheme *theme = ShellTheme::instance();
    painter.fillRect(rect(), m_background);
    // The watermark, at the view's lower right corner
    if (!m_watermark.isNull())
        painter.drawImage(width() - m_watermark.width(), height() - m_watermark.height(), m_watermark);
    // The separator on its left, the white line along its bottom
    QLinearGradient separator(0, 0, 0, height());
    separator.setColorAt(0, m_separatorTop);
    separator.setColorAt(1, m_separatorBottom);
    painter.fillRect(QRect(0, 0, 1, height()), separator);
    painter.fillRect(QRect(0, height() - 1, width(), 1), Qt::white);

    if (m_category >= 0) {
        // The banner: its header gradient, the category's name, its icon over the edge
        const int banner = theme->scaled(BANNER);
        QLinearGradient header(0, 0, width(), 0);
        header.setColorAt(0, m_headerLeft);
        header.setColorAt(1, m_headerRight);
        painter.fillRect(QRect(1, 0, width() - 1, banner), header);
        const auto list = controlpanel::categories();
        if (m_category < list.size()) {
            list[m_category].icon.paint(&painter, QRect(theme->scaled(BANNER_ICON_X), theme->scaled(BANNER_ICON_Y), theme->scaled(ICON), theme->scaled(ICON)));
            painter.setFont(theme->font(QStringLiteral("Tahoma, 8, bold")));
            painter.setPen(Qt::white);
            painter.drawText(QRect(theme->scaled(BANNER_TEXT_X), 0, width() - theme->scaled(BANNER_TEXT_X) - 4, banner), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, list[m_category].name);
        }
    }

    // The directives: Franklin Gothic Medium, 21 points, the style's pale colour
    painter.setFont(madeUpBold(theme));
    painter.setPen(m_directive);
    const int padding = theme->scaled(PADDING), directive = theme->scaled(DIRECTIVE_HEIGHT);
    const bool tasks = m_directive2Y >= 0;
    const auto directiveAt = [&](int y, const QString &text) {
        drawMadeUpBold(painter, QRect(1 + padding + theme->scaled(DIRECTIVE_INDENT), y, width() - 1 - 2 * padding, directive), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, text);
    };
    if (m_category < 0)
        directiveAt(m_directiveY, xpfiles::shellString(STRING_PICK_CATEGORY, "Pick a category"));
    else if (tasks)
        directiveAt(m_directiveY, xpfiles::shellString(STRING_PICK_TASK, "Pick a task..."));
    else
        directiveAt(m_directiveY, xpfiles::shellString(STRING_PICK_ICON, "Pick a Control Panel icon"));
    if (tasks)
        directiveAt(m_directive2Y, xpfiles::shellString(STRING_OR_PICK_ICON, "or pick a Control Panel icon"));

    // The links: Tahoma 8 bold, underlined under the pointer
    const int gap = theme->scaled(ICON_GAP);
    for (int i = 0; i < m_entries.size(); ++i) {
        const Entry &entry = m_entries[i];
        QFont font = theme->font(QStringLiteral("Tahoma, 8, bold"));
        font.setUnderline(i == m_hot);
        painter.setFont(font);
        painter.setPen(m_link);
        int x = entry.rect.x();
        if (!entry.icon.isNull()) {
            const int side = entry.iconSize;
            entry.icon.paint(&painter, QRect(x + theme->scaled(ITEM_PADDING), entry.rect.y() + (entry.rect.height() - side) / 2, side, side));
            x += side + theme->scaled(ITEM_PADDING);
        } else if (!m_bullet.isNull()) {
            painter.drawImage(x, entry.rect.y() + (entry.rect.height() - m_bullet.height()) / 2, m_bullet);
            x += theme->scaled(TASK_ICON);
        }
        painter.drawText(QRect(x + gap, entry.rect.y(), entry.rect.right() - x - gap, entry.rect.height()), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, entry.text);
    }
}

int ControlPanelCategories::entryAt(QPoint at) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].rect.contains(at))
            return i;
    return -1;
}

void ControlPanelCategories::mouseMoveEvent(QMouseEvent *event)
{
    const int hot = entryAt(event->position().toPoint());
    if (hot != m_hot) {
        m_hot = hot;
        update();
    }
    setCursor(hot >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void ControlPanelCategories::leaveEvent(QEvent *)
{
    m_hot = -1;
    update();
}

bool ControlPanelCategories::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(event);
        const int at = entryAt(help->pos());
        if (at >= 0 && !m_entries[at].description.isEmpty())
            QToolTip::showText(help->globalPos(), m_entries[at].description, this);
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(event);
}

void ControlPanelCategories::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const int at = entryAt(event->position().toPoint());
    if (at < 0)
        return;
    // (Either may rebuild the entries.)
    const Entry entry = m_entries[at];
    if (entry.action)
        entry.action();
    else if (entry.category == controlpanel::Programs)
        controlpanel::open(QStringLiteral("programs"));   // XP goes straight to the applet.
    else if (entry.category >= 0)
        m_openCategory(entry.category);
}

namespace {
// shell32.dll: "These files are hidden.", why, and the words for a drive and for a folder
const int STRING_HIDDEN = 17240, STRING_WHY = 17241, STRING_SHOW_DRIVE = 31319, STRING_SHOW_FOLDER = 31323;
// The shell style's UIFILE 1, at 96 DPI
const int WARNING_LEFT = 10, WARNING_TITLE_TOP = 20, WARNING_MESSAGE_TOP = 15, WARNING_LINK_TOP = 20, WARNING_RIGHT = 10;
}

SystemFolderWarning::SystemFolderWarning(std::function<void()> showContents, QWidget *parent)
    : QWidget(parent)
    , m_showContents(std::move(showContents))
{
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    loadStyle();
    connect(ShellTheme::instance(), &ShellTheme::changed, this, [this] {
        loadStyle();
        update();
    });
}

void SystemFolderWarning::setDrive(bool drive)
{
    m_drive = drive;
    update();
}

void SystemFolderWarning::loadStyle()
{
    ShellTheme *theme = ShellTheme::instance();
    const LookChoice look = theme->choice();
    const QString file = ShellTheme::shellStylePath(look);
    m_watermark = QImage();
    const auto resources = xpfiles::resources(file);
    if (!resources) {
        // Windows Classic: the system colours
        m_background = m_accentBottom = theme->system(QStringLiteral("window"), Qt::white);
        m_accentTop = theme->system(QStringLiteral("btnshadow"), Qt::gray);
        m_title = theme->system(QStringLiteral("highlight"), QColor(10, 36, 106));
        return;
    }
    const QString ui = QString::fromLatin1(resources->find(QStringLiteral("UIFILE"), QStringLiteral("1")));
    m_background = colourIn(ui, QStringLiteral("atom\\(blockade\\)\\]\\s*\\{\\s*background:\\s*rgb") + RGB, QColor(99, 117, 214));
    const auto accent = QRegularExpression(QStringLiteral("atom\\(blockadeaccent\\)\\]\\s*\\{\\s*background:\\s*gradient\\(") + ARGB + QStringLiteral(",\\s*") + ARGB, QRegularExpression::CaseInsensitiveOption).match(ui);
    m_accentTop = accent.hasMatch() ? QColor(accent.captured(1).toInt(), accent.captured(2).toInt(), accent.captured(3).toInt()) : QColor(148, 187, 255);
    m_accentBottom = accent.hasMatch() ? QColor(accent.captured(4).toInt(), accent.captured(5).toInt(), accent.captured(6).toInt()) : m_background;
    m_title = colourIn(ui, QStringLiteral("atom\\(blockadetitle\\)\\].*?foreground:\\s*rgb") + RGB, QColor(214, 223, 245));
    m_watermark = xpfiles::bitmap(*resources, BITMAP_WARNING_WATERMARK);
}

QRect SystemFolderWarning::linkRect() const
{
    ShellTheme *theme = ShellTheme::instance();
    const int left = 1 + theme->scaled(WARNING_LEFT), width = qMax(50, this->width() - left - theme->scaled(WARNING_RIGHT));
    int y = theme->scaled(WARNING_TITLE_TOP);
    y += QFontMetrics(madeUpBold(theme)).boundingRect(QRect(0, 0, width, 1000), Qt::TextWordWrap, xpfiles::shellString(STRING_HIDDEN, "These files are hidden.")).height();
    y += theme->scaled(WARNING_MESSAGE_TOP);
    y += QFontMetrics(theme->font(QStringLiteral("Franklin Gothic Medium, 14"))).boundingRect(QRect(0, 0, width, 1000), Qt::TextWordWrap, xpfiles::shellString(STRING_WHY, "This folder contains files that keep your system working properly. You should not modify its contents.")).height();
    y += theme->scaled(WARNING_LINK_TOP);
    const QFontMetrics link(theme->font(QStringLiteral("Tahoma, 8, bold")));
    const QString text = m_drive ? xpfiles::shellString(STRING_SHOW_DRIVE, "Show the contents of this drive") : xpfiles::shellString(STRING_SHOW_FOLDER, "Show the contents of this folder");
    return QRect(left, y, link.horizontalAdvance(text) + theme->scaled(3) + 2, link.height() + 2);
}

void SystemFolderWarning::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    ShellTheme *theme = ShellTheme::instance();
    painter.fillRect(rect(), m_background);
    if (!m_watermark.isNull())
        painter.drawImage(width() - 1 - m_watermark.width(), height() - 1 - m_watermark.height(), m_watermark);
    QLinearGradient accent(0, 0, 0, height());
    accent.setColorAt(0, m_accentTop);
    accent.setColorAt(1, m_accentBottom);
    painter.fillRect(QRect(0, 0, 1, height()), accent);
    painter.fillRect(QRect(0, height() - 1, width(), 1), Qt::white);
    painter.fillRect(QRect(width() - 1, 0, 1, height()), Qt::white);

    const int left = 1 + theme->scaled(WARNING_LEFT), textWidth = qMax(50, width() - left - theme->scaled(WARNING_RIGHT));
    int y = theme->scaled(WARNING_TITLE_TOP);
    QRect used;
    painter.setFont(madeUpBold(theme));
    painter.setPen(m_title);
    drawMadeUpBold(painter, QRect(left, y, textWidth, 1000), Qt::TextWordWrap, xpfiles::shellString(STRING_HIDDEN, "These files are hidden."), &used);
    y += used.height() + theme->scaled(WARNING_MESSAGE_TOP);
    painter.setFont(theme->font(QStringLiteral("Franklin Gothic Medium, 14")));
    const bool classic = m_watermark.isNull();
    painter.setPen(classic ? QColor(Qt::black) : QColor(Qt::white));
    painter.drawText(QRect(left + 1, y, textWidth - 1, 1000), Qt::TextWordWrap, xpfiles::shellString(STRING_WHY, "This folder contains files that keep your system working properly. You should not modify its contents."));
    QFont link = theme->font(QStringLiteral("Tahoma, 8, bold"));
    link.setUnderline(m_hot);
    painter.setFont(link);
    painter.drawText(linkRect().adjusted(theme->scaled(3), 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                     m_drive ? xpfiles::shellString(STRING_SHOW_DRIVE, "Show the contents of this drive") : xpfiles::shellString(STRING_SHOW_FOLDER, "Show the contents of this folder"));
}

void SystemFolderWarning::mouseMoveEvent(QMouseEvent *event)
{
    const bool hot = linkRect().contains(event->position().toPoint());
    if (hot != m_hot) {
        m_hot = hot;
        setCursor(hot ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void SystemFolderWarning::leaveEvent(QEvent *)
{
    m_hot = false;
    update();
}

void SystemFolderWarning::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && linkRect().contains(event->position().toPoint()))
        m_showContents();
}
