// SPDX-License-Identifier: LGPL-2.1-or-later
#include "startmenu.h"

#include "applets.h"

#include "xpdialog.h"
#include <QTimer>
#include <QSettings>
#include <QPushButton>
#include <QPointer>
#include <QFileDialog>
#include <QComboBox>

#include "filebrowser.h"

#include "programs.h"
#include "theme.h"
#include "xpfiles.h"

#include <algorithm>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>
#include <QXmlStreamReader>

#include <pwd.h>
#include <unistd.h>

using w4w::Part;

namespace {

// explorer.exe's and shell32.dll's strings, for the language of the user's XP
const int EXPLORER_ALL_PROGRAMS = 8226, EXPLORER_LOG_OFF = 730, EXPLORER_TURN_OFF = 731;
// shell32.dll's Run dialog, and how many commands its list keeps
const int ACTIVATE_DELAY_MS = 100;
const int DIALOG_RUN = 1003, RUN_COMMAND = 12298, RUN_SEPARATE_MEMORY = 12306, RUN_BROWSE = 12288, STRING_BROWSE_TITLE = 9015, RUN_HISTORY = 26;
const int EXPLORER_HELP = 7021, EXPLORER_SEARCH = 7020, EXPLORER_RUN = 7023, EXPLORER_RUN_TITLE = 722;
const int SHELL_DOCUMENTS = 9227, SHELL_RECENT = 12691, SHELL_PICTURES = 9076, SHELL_MUSIC = 9105, SHELL_COMPUTER = 9216;
const int SHELL_CONTROL_PANEL = 4161, SHELL_PRINTERS = 9319;
// shell32.dll's icons
const int ICON_DOCUMENTS = 235, ICON_RECENT = 21, ICON_PICTURES = 236, ICON_MUSIC = 237, ICON_COMPUTER = 16;
const int ICON_CONTROL_PANEL = 22, ICON_PRINTERS = 138, ICON_HELP = 24, ICON_SEARCH = 23, ICON_RUN = 25;
const int ICON_LOG_OFF = 45, ICON_TURN_OFF = 28, ICON_PROGRAMS = 20, ICON_DOCUMENT = 1;

// The Start panel at 96 DPI, from the style's DefaultPaneSize and XP's menu
const int PANEL_WIDTH = 380, COLUMN = 190, USER_HEIGHT = 64, LOGOFF_HEIGHT = 40, MORE_HEIGHT = 30, MORE_GAP = 6, BODY_MIN = 336;
const int PLACE_HEIGHT = 30, PLACE_ICON = 24, LEFT_BIG = 39, LEFT_SMALL = 40;
// XP's left column: six recent programs, and a line above All Programs
const int RECENT_PROGRAMS = 6, LEFT_TOP = 5, LEFT_LINE = 7;
const int CASCADE_ROW = 23, CASCADE_ICON = 16;

Part panelPart(const char *name)
{
    return Part{QStringLiteral("startpanel"), QLatin1String(name), {}, 1, {}};
}

const std::optional<w4w::PeResources> &explorer()
{
    static const auto resources = xpfiles::resources(xpfiles::find(QStringLiteral("explorer.exe")));
    return resources;
}

QString explorerString(int id, const char *fallback)
{
    const QString text = explorer() ? xpfiles::string(*explorer(), id) : QString();
    return text.isEmpty() ? QLatin1String(fallback) : text;
}

QString shellString(int id, const char *fallback)
{
    static const auto resources = xpfiles::resources(xpfiles::find(QStringLiteral("shell32.dll")));
    const QString text = resources ? xpfiles::string(*resources, id) : QString();
    return text.isEmpty() ? QLatin1String(fallback) : text;
}

QString userName()
{
    const passwd *entry = getpwuid(getuid());
    if (!entry)
        return qEnvironmentVariable("USER");
    // The GECOS field's first part is the full name, when there is one.
    const QString full = QString::fromLocal8Bit(entry->pw_gecos).section(QLatin1Char(','), 0, 0).trimmed();
    return full.isEmpty() ? QString::fromLocal8Bit(entry->pw_name) : full;
}

QImage userPicture(int size)
{
    const QString name = QString::fromLocal8Bit(getpwuid(getuid()) ? getpwuid(getuid())->pw_name : "");
    QImage picture;
    for (const QString &path : {QDir::homePath() + QStringLiteral("/.face"), QStringLiteral("/var/lib/AccountsService/icons/") + name,
                                xpfiles::find(QStringLiteral("beach.bmp"))}) {
        picture = QImage(path);
        if (!picture.isNull())
            break;
    }
    return picture.isNull() ? picture : picture.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

// Runs a command line, or opens what it names, as XP's Run dialog does.
void runCommand(const QString &line)
{
    QStringList words = QProcess::splitCommand(line);
    if (words.isEmpty())
        return;
    const QString program = words.takeFirst();
    // XP's own, which is here without a program of its own
    if (program.compare(QLatin1String("winver"), Qt::CaseInsensitive) == 0) {
        applets::open(QStringLiteral("winver"));
        return;
    }
    if (!QStandardPaths::findExecutable(program).isEmpty() || QFileInfo(program).isExecutable())
        QProcess::startDetached(program, words, QDir::homePath());
    else
        programs::open(line);
}

} // namespace

// XP's own dialog from shell32.dll, with what was run before in its list
void showRunDialog()
{
    static QPointer<QDialog> open;
    if (open) {
        open->raise();
        open->activateWindow();
        return;
    }
    const auto shell = xpfiles::resources(xpfiles::find(QStringLiteral("shell32.dll")));
    if (!shell)
        return;
    auto *dialog = new QDialog;
    open = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowFlag(Qt::WindowContextHelpButtonHint, true);
    const xpdialog::Page page = xpdialog::build(*shell, DIALOG_RUN, dialog);
    dialog->setWindowTitle(page.caption);
    // It is there for 16-bit programs, and XP only shows it for them.
    if (QWidget *memory = page.controls.value(RUN_SEPARATE_MEMORY))
        memory->hide();
    auto *combo = qobject_cast<QComboBox *>(page.controls.value(RUN_COMMAND));
    auto *ok = qobject_cast<QPushButton *>(page.controls.value(1));
    auto *cancel = qobject_cast<QPushButton *>(page.controls.value(2));
    auto *browse = qobject_cast<QPushButton *>(page.controls.value(RUN_BROWSE));
    if (!combo || !ok || !cancel || !browse)
        return;
    QSettings settings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/native-explorer.conf"), QSettings::IniFormat);
    const QStringList history = settings.value(QStringLiteral("runHistory")).toStringList();
    combo->addItems(history);
    combo->setCurrentText(history.value(0));
    combo->lineEdit()->selectAll();
    ok->setDefault(true);
    ok->setEnabled(!combo->currentText().trimmed().isEmpty());
    QObject::connect(combo, &QComboBox::currentTextChanged, ok, [ok](const QString &text) { ok->setEnabled(!text.trimmed().isEmpty()); });
    // Enter in the field, even with the text as it was: and only once, should the dialog see it too.
    QObject::connect(combo->lineEdit(), &QLineEdit::returnPressed, ok, &QPushButton::click);
    QObject::connect(ok, &QPushButton::clicked, dialog, [dialog, combo] {
        if (!dialog->isVisible() || combo->currentText().trimmed().isEmpty())
            return;
        dialog->hide();
        const QString line = combo->currentText().trimmed();
        QSettings write(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/native-explorer.conf"), QSettings::IniFormat);
        QStringList runs = write.value(QStringLiteral("runHistory")).toStringList();
        runs.removeAll(line);
        runs.prepend(line);
        write.setValue(QStringLiteral("runHistory"), runs.mid(0, RUN_HISTORY));
        runCommand(line);
        dialog->accept();
    });
    QObject::connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    QObject::connect(browse, &QPushButton::clicked, dialog, [dialog, combo] {
        // shell32's "Programs#*.exe;...#All Files#*.*#": Linux's programs have no such names.
        const QString file = QFileDialog::getOpenFileName(dialog, xpfiles::shellString(STRING_BROWSE_TITLE, "Browse"), QDir::homePath());
        if (!file.isEmpty())
            combo->setCurrentText(file);
    });
    xpdialog::placeLowerLeft(dialog);
    dialog->show();
    combo->setFocus();
    // The Windows key's grab may just be ending: ask for the keyboard once it has.
    QTimer::singleShot(ACTIVATE_DELAY_MS, dialog, [dialog, combo] {
        dialog->activateWindow();
        combo->setFocus();
    });
}

namespace {

QString xdgDir(QStandardPaths::StandardLocation location)
{
    return QStandardPaths::writableLocation(location);
}

} // namespace

Cascade::Cascade(const QList<MenuEntry> &entries, QPoint corner, bool upward)
    : QWidget(nullptr, Qt::ToolTip | Qt::X11BypassWindowManagerHint | Qt::FramelessWindowHint)
    , m_entries(entries)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFocusPolicy(Qt::NoFocus);
    ShellTheme *theme = ShellTheme::instance();
    QFontMetrics metrics(theme->font(QStringLiteral("Tahoma, 8")));
    int textWidth = 80;
    for (const MenuEntry &entry : entries)
        textWidth = qMax(textWidth, metrics.horizontalAdvance(entry.text));
    const int row = theme->scaled(CASCADE_ROW);
    int width = theme->scaled(10 + CASCADE_ICON + 8 + 22) + textWidth;
    const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
    width = qMin(width, screen.width() / 2);
    const int height = qMin(int(entries.size()) * row + theme->scaled(6), screen.height());
    for (int i = 0; i < entries.size(); ++i)
        m_rects.append(QRect(theme->scaled(3), theme->scaled(3) + i * row, width - theme->scaled(6), row));
    QPoint at = upward ? corner - QPoint(0, height) : corner;
    at.setY(qBound(screen.top(), at.y(), screen.bottom() + 1 - height));
    at.setX(qMin(at.x(), screen.right() + 1 - width));
    setGeometry(QRect(at, QSize(width, height)));
}

int Cascade::entryAt(QPoint global) const
{
    const QPoint at = mapFromGlobal(global);
    if (!rect().contains(at))
        return -2;
    for (int i = 0; i < m_rects.size(); ++i)
        if (m_rects[i].contains(at))
            return i;
    return -1;
}

void Cascade::setHot(int index)
{
    if (index != m_hot) {
        m_hot = index;
        update();
    }
}

QRect Cascade::entryGlobalRect(int index) const
{
    return m_rects[index].translated(mapToGlobal(QPoint(0, 0)));
}

void Cascade::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    ShellTheme *theme = ShellTheme::instance();
    w4w::Renderer *renderer = theme->renderer();
    const Part toolbar{QStringLiteral("toolbar"), {}, {}, 0, QStringLiteral("startmenu")};
    if (renderer) {
        renderer->draw(&painter, rect(), toolbar);
    } else {
        painter.fillRect(rect(), theme->system(QStringLiteral("menu"), Qt::lightGray));
        theme->raised(painter, rect());
    }
    painter.setFont(theme->font(QStringLiteral("Tahoma, 8")));
    const int icon = theme->scaled(CASCADE_ICON);
    for (int i = 0; i < m_entries.size(); ++i) {
        const QRect r = m_rects[i];
        const bool hot = i == m_hot;
        if (hot) {
            if (renderer)
                renderer->draw(&painter, r, Part{QStringLiteral("toolbar"), QStringLiteral("button"), {}, 2, QStringLiteral("startmenu")});
            else
                painter.fillRect(r, theme->system(QStringLiteral("highlight"), Qt::darkBlue));
        }
        const QPixmap pixmap = m_entries[i].icon.pixmap(icon, icon);
        painter.drawPixmap(r.x() + theme->scaled(7), r.y() + (r.height() - icon) / 2, pixmap);
        const QColor text = hot ? Qt::white : (renderer ? renderer->colour(toolbar, QStringLiteral("textcolor")).value_or(Qt::black) : QColor(Qt::black));
        painter.setPen(text);
        const int textX = r.x() + theme->scaled(7 + CASCADE_ICON + 7);
        const int textEnd = r.right() - theme->scaled(14);
        painter.drawText(QRect(textX, r.y(), textEnd - textX, r.height()), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                         painter.fontMetrics().elidedText(m_entries[i].text, Qt::ElideRight, textEnd - textX));
        if (m_entries[i].children) {
            // XP's menu arrow
            const int mid = r.center().y(), x = r.right() - theme->scaled(8);
            QPolygon arrow;
            arrow << QPoint(x, mid - 3) << QPoint(x + 3, mid) << QPoint(x, mid + 3);
            painter.save();
            painter.setPen(text);
            painter.setBrush(text);
            painter.drawPolygon(arrow);
            painter.restore();
        }
    }
}

StartPanel::StartPanel(QPoint bottomLeft, std::function<void()> logOff, std::function<void()> turnOff, std::function<void()> controlPanel)
    : QWidget(nullptr, Qt::Popup)
    , m_logOff(std::move(logOff))
    , m_turnOff(std::move(turnOff))
    , m_controlPanel(std::move(controlPanel))
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    m_openTimer.setSingleShot(true);
    m_openTimer.setInterval(300);
    connect(&m_openTimer, &QTimer::timeout, this, [this] { openChildren(0); });
    build(bottomLeft);
}

StartPanel::~StartPanel()
{
    closeCascades(0);
}

void StartPanel::addLeft(int &y, const QString &text, const QString &subtitle, const QIcon &icon, std::function<void()> action, bool big)
{
    ShellTheme *theme = ShellTheme::instance();
    Item item;
    const int height = theme->scaled(big ? LEFT_BIG : LEFT_SMALL);
    const int size = theme->scaled(32);
    item.rect = QRect(m_left.x() + theme->scaled(3), y, m_left.width() - theme->scaled(6), height);
    item.text = text;
    item.subtitle = subtitle;
    item.bold = big;
    item.icon = icon.pixmap(size, size).toImage();
    item.action = std::move(action);
    m_items.append(item);
    y += height;
}

void StartPanel::addPlace(int &y, int iconId, const QString &text, bool bold, std::function<void()> action, bool arrow)
{
    ShellTheme *theme = ShellTheme::instance();
    Item item;
    const int height = theme->scaled(PLACE_HEIGHT);
    item.rect = QRect(m_right.x() + theme->scaled(5), y, m_right.width() - theme->scaled(10), height);
    item.text = QString(text).remove(QLatin1Char('&'));
    item.bold = bold;
    item.icon = theme->shellIcon(iconId, theme->scaled(PLACE_ICON));
    item.enabled = bool(action) || arrow;
    item.action = std::move(action);
    item.arrow = arrow;
    m_items.append(item);
    y += height;
}

// "All Programs" and its arrow, together in the middle of the column
QRect StartPanel::moreArrow() const
{
    ShellTheme *theme = ShellTheme::instance();
    const int side = theme->scaled(19), gap = theme->scaled(MORE_GAP);
    const int text = QFontMetrics(theme->font(QStringLiteral("Tahoma, 8, bold"))).horizontalAdvance(m_moreText);
    return QRect(m_more.x() + (m_more.width() - text - gap - side) / 2 + text + gap, m_more.y() + (m_more.height() - side) / 2, side, side);
}

void StartPanel::build(QPoint bottomLeft)
{
    ShellTheme *theme = ShellTheme::instance();
    const int width = theme->scaled(PANEL_WIDTH), column = theme->scaled(COLUMN);
    const int userHeight = theme->scaled(USER_HEIGHT), logoffHeight = theme->scaled(LOGOFF_HEIGHT);
    const int moreHeight = theme->scaled(MORE_HEIGHT);
    m_userName = userName();
    m_picture = userPicture(theme->scaled(48));
    m_moreText = explorerString(EXPLORER_ALL_PROGRAMS, "All Programs").remove(QLatin1Char('&'));
    m_logOffText = explorerString(EXPLORER_LOG_OFF, "Log Off").remove(QLatin1Char('&')).remove(QLatin1String("..."));
    m_turnOffText = explorerString(EXPLORER_TURN_OFF, "Turn Off Computer").remove(QLatin1Char('&')).remove(QLatin1String("..."));
    m_logOffIcon = theme->shellIcon(ICON_LOG_OFF, theme->scaled(24));
    m_turnOffIcon = theme->shellIcon(ICON_TURN_OFF, theme->scaled(24));

    // The places first: they decide the height
    m_right = QRect(column, userHeight, width - column, 0);
    int y = userHeight + theme->scaled(9);
    const auto opens = [](const QString &path) { return [path] { programs::open(path); }; };
    addPlace(y, ICON_DOCUMENTS, shellString(SHELL_DOCUMENTS, "My Documents"), true, opens(xdgDir(QStandardPaths::DocumentsLocation)));
    addPlace(y, ICON_RECENT, shellString(SHELL_RECENT, "My Recent Documents"), true, {}, true);
    m_items.last().children = [this] { return recentDocuments(); };
    addPlace(y, ICON_PICTURES, shellString(SHELL_PICTURES, "My Pictures"), true, opens(xdgDir(QStandardPaths::PicturesLocation)));
    addPlace(y, ICON_MUSIC, shellString(SHELL_MUSIC, "My Music"), true, opens(xdgDir(QStandardPaths::MusicLocation)));
    addPlace(y, ICON_COMPUTER, shellString(SHELL_COMPUTER, "My Computer"), true, opens(QStringLiteral("file:///")));
    Item separator;
    separator.separator = true;
    separator.rect = QRect(m_right.x() + theme->scaled(10), y + theme->scaled(3), m_right.width() - theme->scaled(20), theme->scaled(2));
    m_items.append(separator);
    y += theme->scaled(8);
    addPlace(y, ICON_CONTROL_PANEL, shellString(SHELL_CONTROL_PANEL, "Control Panel"), false, m_controlPanel);
    const bool printers = !QStandardPaths::findExecutable(QStringLiteral("system-config-printer")).isEmpty();
    addPlace(y, ICON_PRINTERS, shellString(SHELL_PRINTERS, "Printers and Faxes"), false,
             printers ? std::function<void()>([] { QProcess::startDetached(QStringLiteral("system-config-printer"), {}); }) : std::function<void()>());
    separator.rect.moveTop(y + theme->scaled(3));
    m_items.append(separator);
    y += theme->scaled(8);
    addPlace(y, ICON_HELP, explorerString(EXPLORER_HELP, "Help and Support"), false, {});
    addPlace(y, ICON_SEARCH, explorerString(EXPLORER_SEARCH, "Search"), false, [] { FileBrowser::open(places::Search); });
    addPlace(y, ICON_RUN, explorerString(EXPLORER_RUN, "Run..."), false, [] { showRunDialog(); });
    const auto browser = programs::defaultBrowser(), mail = programs::defaultMail();
    const int pinned = (browser ? 1 : 0) + (mail ? 1 : 0);
    const int leftHeight = theme->scaled(LEFT_TOP + pinned * LEFT_BIG + (pinned ? LEFT_LINE : 0) + RECENT_PROGRAMS * LEFT_SMALL + LEFT_LINE) + moreHeight;
    const int bodyHeight = std::max({theme->scaled(BODY_MIN), y - userHeight + theme->scaled(5), leftHeight});
    m_right.setHeight(bodyHeight);

    m_user = QRect(0, 0, width, userHeight);
    m_logoff = QRect(0, userHeight + bodyHeight, width, logoffHeight);
    m_left = QRect(0, userHeight, column, bodyHeight - moreHeight);
    m_more = QRect(0, userHeight + bodyHeight - moreHeight, column, moreHeight);

    // The left column: the default browser and mail, then the most used programs
    y = m_left.y() + theme->scaled(LEFT_TOP);
    const auto launcher = [](const programs::Program &program) { return [program] { programs::launch(program); }; };
    if (browser)
        addLeft(y, tr("Internet"), browser->name, programs::iconOf(*browser), launcher(*browser), true);
    if (mail)
        addLeft(y, tr("E-mail"), mail->name, programs::iconOf(*mail), launcher(*mail), true);
    if (browser || mail) {
        Item line;
        line.separator = true;
        line.rect = QRect(m_left.x() + theme->scaled(10), y + theme->scaled(2), m_left.width() - theme->scaled(20), theme->scaled(2));
        m_items.append(line);
        y += theme->scaled(LEFT_LINE);
    }
    // (Asking for two more: the browser and the mail may be among them.)
    int added = 0;
    const QList<programs::Program> used = programs::mostUsed(RECENT_PROGRAMS + 2);
    for (const programs::Program &program : used) {
        if ((browser && program.id == browser->id) || (mail && program.id == mail->id))
            continue;
        if (added++ >= RECENT_PROGRAMS)
            break;
        addLeft(y, program.name, {}, programs::iconOf(program), launcher(program), false);
    }
    Item moreLine;
    moreLine.separator = true;
    moreLine.rect = QRect(m_left.x() + theme->scaled(10), m_more.y() - theme->scaled(4), m_left.width() - theme->scaled(20), theme->scaled(2));
    m_items.append(moreLine);

    // The bottom's buttons, right-aligned as XP's
    QFontMetrics metrics(theme->font(QStringLiteral("Tahoma, 8")));
    const int icon = theme->scaled(24), gap = theme->scaled(4);
    const int turnOffWidth = icon + gap + metrics.horizontalAdvance(m_turnOffText) + theme->scaled(4);
    const int logOffWidth = icon + gap + metrics.horizontalAdvance(m_logOffText) + theme->scaled(4);
    const int buttonY = m_logoff.y() + (logoffHeight - icon) / 2;
    m_turnOffButton = QRect(width - theme->scaled(10) - turnOffWidth, buttonY, turnOffWidth, icon);
    m_logOffButton = QRect(m_turnOffButton.x() - theme->scaled(10) - logOffWidth, buttonY, logOffWidth, icon);

    const int height = userHeight + bodyHeight + logoffHeight;
    setGeometry(QRect(bottomLeft - QPoint(0, height), QSize(width, height)));
    // The user pane's art has the panel's rounded top corners: they are its window's shape.
    if (w4w::Renderer *renderer = theme->renderer()) {
        QImage art(width, userHeight, QImage::Format_ARGB32_Premultiplied);
        art.fill(Qt::transparent);
        {
            QPainter painter(&art);
            renderer->draw(&painter, art.rect(), panelPart("userpane"));
        }
        QRegion shape(rect());
        for (int row = 0; row < userHeight; ++row) {
            const auto *pixels = reinterpret_cast<const QRgb *>(art.constScanLine(row));
            int left = 0, right = 0;
            while (left < 16 && qAlpha(pixels[left]) == 0)
                ++left;
            while (right < 16 && qAlpha(pixels[width - 1 - right]) == 0)
                ++right;
            if (!left && !right)
                break;
            shape -= QRect(0, row, left, 1);
            shape -= QRect(width - right, row, right, 1);
        }
        setMask(shape);
    }
}

void StartPanel::paintPane(QPainter &painter, const char *part, const QRect &rect, const QColor &fallback)
{
    w4w::Renderer *renderer = ShellTheme::instance()->renderer();
    if (renderer)
        renderer->draw(&painter, rect, panelPart(part));
    else
        painter.fillRect(rect, fallback);
}

void StartPanel::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    ShellTheme *theme = ShellTheme::instance();
    w4w::Renderer *renderer = theme->renderer();
    const QColor highlight = theme->system(QStringLiteral("highlight"), Qt::darkBlue);
    const QColor face = theme->system(QStringLiteral("btnface"), Qt::lightGray);
    painter.fillRect(rect(), face);
    paintPane(painter, "userpane", m_user, theme->frame().activeCaption);
    paintPane(painter, "proglist", m_left, theme->system(QStringLiteral("window"), Qt::white));
    paintPane(painter, "moreprograms", m_more, theme->system(QStringLiteral("window"), Qt::white));
    paintPane(painter, "placeslist", m_right, face);
    paintPane(painter, "logoff", m_logoff, theme->frame().activeCaption);

    // The user's picture and name
    const QRect tile(theme->scaled(5), theme->scaled(5), theme->scaled(54), theme->scaled(54));
    if (renderer) {
        renderer->draw(&painter, tile, panelPart("userpicture"));
    }
    if (!m_picture.isNull())
        painter.drawImage(tile.x() + (tile.width() - m_picture.width()) / 2, tile.y() + (tile.height() - m_picture.height()) / 2, m_picture);
    QFont nameFont = theme->font(renderer ? renderer->property(panelPart("userpane"), QStringLiteral("font")).value_or(QStringLiteral("Franklin Gothic Medium, 14"))
                                          : QStringLiteral("Tahoma, 14, bold"));
    painter.setFont(nameFont);
    const QRect nameRect(tile.right() + theme->scaled(8), m_user.y(), m_user.width() - tile.right() - theme->scaled(12), m_user.height());
    if (renderer) {
        const QColor shadow = renderer->colour(panelPart("userpane"), QStringLiteral("textshadowcolor")).value_or(Qt::black);
        ShellTheme::drawShadowedText(painter, nameRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, m_userName, Qt::white, shadow, QPoint(1, 1));
    } else {
        painter.setPen(Qt::white);
        painter.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, m_userName);
    }

    // The items
    const QFont normal = theme->font(QStringLiteral("Tahoma, 8")), bold = theme->font(QStringLiteral("Tahoma, 8, bold"));
    const QColor leftText = renderer ? renderer->colour(panelPart("proglist"), QStringLiteral("textcolor")).value_or(Qt::black) : QColor(Qt::black);
    const QColor rightText = renderer ? renderer->colour(panelPart("placeslist"), QStringLiteral("textcolor")).value_or(Qt::black) : QColor(Qt::black);
    const QColor leftHot = renderer ? renderer->colour(panelPart("proglist"), QStringLiteral("hottracking")).value_or(highlight) : highlight;
    const QColor rightHot = renderer ? renderer->colour(panelPart("placeslist"), QStringLiteral("hottracking")).value_or(highlight) : highlight;
    for (int i = 0; i < m_items.size(); ++i) {
        const Item &item = m_items[i];
        const bool onLeft = item.rect.x() < m_right.x();
        if (item.separator) {
            if (renderer)
                renderer->draw(&painter, item.rect, panelPart(onLeft ? "proglistseparator" : "placeslistseparator"));
            else
                theme->sunken(painter, item.rect);
            continue;
        }
        const bool hot = i == m_hot && item.enabled;
        if (hot)
            painter.fillRect(item.rect, onLeft ? leftHot : rightHot);
        const int iconX = item.rect.x() + theme->scaled(3);
        if (!item.icon.isNull())
            painter.drawImage(iconX, item.rect.y() + (item.rect.height() - item.icon.height()) / 2, item.icon);
        const int textX = iconX + item.icon.width() + theme->scaled(4);
        const int textEnd = item.rect.right() - (item.arrow ? theme->scaled(14) : theme->scaled(2));
        QColor colour = hot ? QColor(Qt::white) : onLeft ? leftText : rightText;
        if (!item.enabled)
            colour = theme->system(QStringLiteral("graytext"), Qt::gray);
        painter.setPen(colour);
        painter.setFont(item.bold ? bold : normal);
        if (item.subtitle.isEmpty()) {
            painter.drawText(QRect(textX, item.rect.y(), textEnd - textX, item.rect.height()), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                             painter.fontMetrics().elidedText(item.text, Qt::ElideRight, textEnd - textX));
        } else {
            const int line = theme->scaled(14), top = item.rect.y() + (item.rect.height() - 2 * line) / 2;
            painter.drawText(QRect(textX, top, textEnd - textX, line), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, item.text);
            painter.setFont(normal);
            painter.setPen(hot ? QColor(Qt::white) : QColor(128, 128, 128));
            painter.drawText(QRect(textX, top + line, textEnd - textX, line), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                             painter.fontMetrics().elidedText(item.subtitle, Qt::ElideRight, textEnd - textX));
        }
        if (item.arrow) {
            const int mid = item.rect.center().y(), x = item.rect.right() - theme->scaled(9);
            QPolygon arrow;
            arrow << QPoint(x, mid - 3) << QPoint(x + 3, mid) << QPoint(x, mid + 3);
            painter.save();
            painter.setPen(colour);
            painter.setBrush(colour);
            painter.drawPolygon(arrow);
            painter.restore();
        }
    }

    // All Programs
    const bool moreHot = m_hotPart == MoreHot;
    const QRect arrow = moreArrow();
    painter.setFont(bold);
    const QRect moreText(m_more.x(), m_more.y(), arrow.x() - m_more.x() - theme->scaled(MORE_GAP), m_more.height());
    if (moreHot) {
        const QRect band(m_more.x() + theme->scaled(8), m_more.y() + theme->scaled(3), m_more.width() - theme->scaled(10), m_more.height() - theme->scaled(6));
        painter.fillRect(band, leftHot);
    }
    painter.setPen(moreHot ? QColor(Qt::white) : leftText);
    painter.drawText(moreText, Qt::AlignRight | Qt::AlignVCenter | Qt::TextSingleLine, m_moreText);
    if (renderer) {
        renderer->draw(&painter, arrow, panelPart("moreprogramsarrow"));
    } else {
        QPolygon triangle;
        triangle << arrow.topLeft() + QPoint(4, 3) << QPoint(arrow.right() - 3, arrow.center().y()) << arrow.bottomLeft() + QPoint(4, -3);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 128, 0));
        painter.drawPolygon(triangle);
    }

    // Log Off and Turn Off Computer
    painter.setFont(normal);
    const auto logoffButton = [&](const QRect &rect, const QImage &icon, const QString &text, bool hot) {
        painter.drawImage(rect.topLeft(), icon);
        if (hot) {
            // XP lightens the hot button's icon.
            QImage light = icon.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            QPainter tint(&light);
            tint.setCompositionMode(QPainter::CompositionMode_SourceAtop);
            tint.fillRect(light.rect(), QColor(255, 255, 255, 70));
            tint.end();
            painter.drawImage(rect.topLeft(), light);
        }
        painter.setPen(Qt::white);
        painter.drawText(QRect(rect.x() + icon.width() + theme->scaled(4), rect.y(), rect.width() - icon.width() - theme->scaled(4), rect.height()),
                         Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, text);
    };
    logoffButton(m_logOffButton, m_logOffIcon, m_logOffText, m_hotPart == LogOffHot);
    logoffButton(m_turnOffButton, m_turnOffIcon, m_turnOffText, m_hotPart == TurnOffHot);
}

QList<MenuEntry> StartPanel::allPrograms() const
{
    ShellTheme *theme = ShellTheme::instance();
    QList<MenuEntry> out;
    const QIcon folderIcon(QPixmap::fromImage(theme->shellIcon(ICON_PROGRAMS, theme->scaled(CASCADE_ICON))));
    const auto folders = programs::folders();
    for (auto it = folders.cbegin(); it != folders.cend(); ++it) {
        MenuEntry folder;
        folder.text = it.key();
        folder.icon = folderIcon;
        const QList<programs::Program> items = it.value();
        folder.children = [items] {
            QList<MenuEntry> entries;
            for (const programs::Program &program : items) {
                MenuEntry entry;
                entry.text = program.name;
                entry.icon = programs::iconOf(program);
                entry.action = [program] { programs::launch(program); };
                entries.append(entry);
            }
            return entries;
        };
        out.append(folder);
    }
    return out;
}

QList<MenuEntry> StartPanel::recentDocuments() const
{
    // The freedesktop list of recent files: href and modified, of each bookmark
    QList<QPair<QString, QString>> found;
    QFile file(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/recently-used.xbel"));
    if (file.open(QIODevice::ReadOnly)) {
        QXmlStreamReader reader(&file);
        while (!reader.atEnd()) {
            if (reader.readNext() == QXmlStreamReader::StartElement && reader.name() == QLatin1String("bookmark"))
                found.append({reader.attributes().value(QLatin1String("modified")).toString(),
                              reader.attributes().value(QLatin1String("href")).toString()});
        }
    }
    std::sort(found.begin(), found.end(), std::greater<>());
    ShellTheme *theme = ShellTheme::instance();
    const QIcon documentIcon(QPixmap::fromImage(theme->shellIcon(ICON_DOCUMENT, theme->scaled(CASCADE_ICON))));
    QList<MenuEntry> out;
    for (const auto &[modified, href] : std::as_const(found)) {
        if (out.size() >= 15)
            break;
        const QString path = QUrl(href).toLocalFile();
        if (path.isEmpty() || !QFileInfo::exists(path))
            continue;
        MenuEntry entry;
        entry.text = QFileInfo(path).fileName();
        entry.icon = documentIcon;
        entry.action = [path] { programs::open(path); };
        out.append(entry);
    }
    if (out.isEmpty()) {
        MenuEntry empty;
        empty.text = tr("(Empty)");
        empty.icon = documentIcon;
        empty.action = [] {};
        out.append(empty);
    }
    return out;
}

void StartPanel::closeCascades(int from)
{
    while (m_cascades.size() > from)
        delete m_cascades.takeLast();
}

void StartPanel::openChildren(int level)
{
    if (level == 0) {
        QList<MenuEntry> entries;
        QPoint corner;
        bool upward = false;
        if (m_openWanted == -2) {
            entries = allPrograms();
            corner = mapToGlobal(QPoint(moreArrow().right() + 1, m_more.bottom() + 1));
            upward = true;
        } else if (m_openWanted >= 0 && m_openWanted < m_items.size() && m_items[m_openWanted].children) {
            entries = m_items[m_openWanted].children();
            corner = mapToGlobal(QPoint(m_items[m_openWanted].rect.right(), m_items[m_openWanted].rect.y()));
        }
        closeCascades(0);
        if (entries.isEmpty())
            return;
        auto *cascade = new Cascade(entries, corner, upward);
        m_cascades.append(cascade);
        cascade->show();
        cascade->raise();
        return;
    }
    Cascade *parent = m_cascades.value(level - 1);
    if (!parent || parent->hot() < 0 || !parent->entry(parent->hot()).children) {
        closeCascades(level);
        return;
    }
    const QRect row = parent->entryGlobalRect(parent->hot());
    const QList<MenuEntry> entries = parent->entry(parent->hot()).children();
    closeCascades(level);
    auto *cascade = new Cascade(entries, QPoint(row.right() + 3, row.y() - 3), false);
    m_cascades.append(cascade);
    cascade->show();
    cascade->raise();
}

void StartPanel::track(QPoint global)
{
    // The topmost cascade under the pointer has it.
    for (int level = int(m_cascades.size()) - 1; level >= 0; --level) {
        Cascade *cascade = m_cascades[level];
        const int entry = cascade->entryAt(global);
        if (entry == -2)
            continue;
        if (entry != cascade->hot()) {
            cascade->setHot(entry);
            closeCascades(level + 1);
            if (entry >= 0 && cascade->entry(entry).children)
                openChildren(level + 1);
        }
        return;
    }
    const QPoint at = mapFromGlobal(global);
    int hot = -1;
    auto part = Nothing;
    if (rect().contains(at)) {
        for (int i = 0; i < m_items.size(); ++i)
            if (!m_items[i].separator && m_items[i].rect.contains(at))
                hot = i;
        if (m_more.contains(at))
            part = MoreHot;
        else if (m_logOffButton.contains(at))
            part = LogOffHot;
        else if (m_turnOffButton.contains(at))
            part = TurnOffHot;
    }
    if (hot != m_hot || part != m_hotPart) {
        m_hot = hot;
        m_hotPart = part;
        update();
        // Moving onto something else in the panel closes the open cascades, after a moment.
        int wanted = -1;
        if (part == MoreHot)
            wanted = -2;
        else if (hot >= 0 && m_items[hot].children)
            wanted = hot;
        if (wanted != -1 || rect().contains(at)) {
            m_openWanted = wanted;
            if (wanted == -1)
                closeCascades(0);
            m_openTimer.start();
        }
    }
}

void StartPanel::mouseMoveEvent(QMouseEvent *event)
{
    track(event->globalPosition().toPoint());
}

void StartPanel::run(const std::function<void()> &action)
{
    close();
    if (action)
        action();
}

void StartPanel::mousePressEvent(QMouseEvent *event)
{
    const QPoint global = event->globalPosition().toPoint();
    for (Cascade *cascade : std::as_const(m_cascades))
        if (cascade->entryAt(global) != -2)
            return;
    if (!rect().contains(event->position().toPoint()))
        close();
}

void StartPanel::mouseReleaseEvent(QMouseEvent *event)
{
    const QPoint global = event->globalPosition().toPoint();
    if (event->button() != Qt::LeftButton)
        return;
    for (int level = int(m_cascades.size()) - 1; level >= 0; --level) {
        const int entry = m_cascades[level]->entryAt(global);
        if (entry == -2)
            continue;
        if (entry >= 0 && m_cascades[level]->entry(entry).action) {
            const auto action = m_cascades[level]->entry(entry).action;
            run(action);
        }
        return;
    }
    const QPoint at = event->position().toPoint();
    if (m_more.contains(at)) {
        m_openWanted = -2;
        m_openTimer.stop();
        openChildren(0);
    } else if (m_logOffButton.contains(at)) {
        run(m_logOff);
    } else if (m_turnOffButton.contains(at)) {
        run(m_turnOff);
    } else {
        for (const Item &item : std::as_const(m_items))
            if (!item.separator && item.rect.contains(at) && item.enabled) {
                if (item.children) {
                    m_openTimer.stop();
                    m_openWanted = m_hot;
                    openChildren(0);
                } else {
                    run(item.action);
                }
                return;
            }
    }
}

void StartPanel::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
        close();
    else
        QWidget::keyPressEvent(event);
}

void StartPanel::hideEvent(QHideEvent *)
{
    closeCascades(0);
    Q_EMIT closed();
}
