// SPDX-License-Identifier: LGPL-2.1-or-later
#include "startmenu.h"

#include "applets.h"

#include "xpdialog.h"
#include <functional>
#include <QTimer>
#include <QSettings>
#include <QPushButton>
#include <QPointer>
#include <QFileDialog>
#include <QComboBox>

#include "filebrowser.h"
#include "fileicons.h"

#include "programs.h"
#include "taskbarproperties.h"
#include "xpprogram.h"
#include "theme.h"
#include "xpfiles.h"

#include <algorithm>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QApplication>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QProcess>
#include <QScreen>
#include <QStandardPaths>
#include <QToolTip>
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
// How long XP's menus wait before they change (SPI_GETMENUSHOWDELAY's default)
const int CASCADE_DELAY_MS = 400;
const int DIALOG_RUN = 1003, RUN_COMMAND = 12298, RUN_SEPARATE_MEMORY = 12306, RUN_BROWSE = 12288, STRING_BROWSE_TITLE = 9015, RUN_HISTORY = 26;
const int EXPLORER_HELP = 7021, EXPLORER_SEARCH = 7020, EXPLORER_RUN = 7023, EXPLORER_RUN_TITLE = 722, EXPLORER_INTERNET = 7024, EXPLORER_MAIL = 7025;
// The panel's own names, with their access keys (My Recent &Documents, &Control Panel); the other places'
// are shell32's, which have none.
const int EXPLORER_RECENT = 8236, EXPLORER_CONTROL_PANEL = 8234;
// explorer.exe's infotips: the places', the pinned programs', All Programs', Log Off's and Turn Off's
const int TIP_DOCUMENTS = 300, TIP_RECENT = 301, TIP_MUSIC = 302, TIP_PICTURES = 303, TIP_COMPUTER = 304, TIP_CONNECT = 306,
          TIP_CONTROL_PANEL = 307, TIP_SEARCH = 7000, TIP_HELP = 7001, TIP_RUN = 7003, TIP_INTERNET = 7004, TIP_MAIL = 7005,
          TIP_LOG_OFF = 7015, TIP_TURN_OFF = 7016, TIP_ALL_PROGRAMS = 8227;
const int SHELL_DOCUMENTS = 9227, SHELL_RECENT = 12691, SHELL_PICTURES = 9076, SHELL_MUSIC = 9105, SHELL_COMPUTER = 9216;
const int SHELL_CONTROL_PANEL = 4161, SHELL_PRINTERS = 9319;
// explorer.exe's Connect To. Set Program Access and Defaults is a shortcut of All Users' Start Menu,
// whose English file name explorer.exe holds; its picture is moricons.dll's icon 114.
const int EXPLORER_CONNECT = 8233, ICON_CONNECT = 175, MORICONS_SPAD = 114;
// shell32.dll's icons
const int ICON_DOCUMENTS = 235, ICON_RECENT = 21, ICON_PICTURES = 236, ICON_MUSIC = 237, ICON_COMPUTER = 16;
const int ICON_CONTROL_PANEL = 22, ICON_PRINTERS = 138, ICON_HELP = 24, ICON_SEARCH = 23, ICON_RUN = 25;
const int ICON_LOG_OFF = 45, ICON_TURN_OFF = 28, ICON_PROGRAMS = 20, ICON_DOCUMENT = 1;

// The Start panel at 96 DPI, from the style's DefaultPaneSize and XP's own panel, measured: both columns
// start 7 under the user pane, a line between places is 5 into its 8 pixels, the body ends 7 under the
// last place, and All Programs has 36 pixels under its line.
const int COLUMN = 190, USER_HEIGHT = 64, LOGOFF_HEIGHT = 40, MORE_HEIGHT = 36, MORE_GAP = 11, BODY_MIN = 336;
const int PLACE_HEIGHT = 30, PLACE_ICON = 24, LEFT_BIG = 40, LEFT_SMALL = 40, COLUMN_TOP = 7, PLACE_LINE = 5, BODY_END = 7;
// XP's left column: six recent programs, and a line above All Programs
const int RECENT_PROGRAMS = 6, LEFT_TOP = COLUMN_TOP, LEFT_LINE = 7;
// Under Windows Classic, measured on XP's: a raised edge of 2 around it all; a header of 40 in the caption's
// gradient 3 in, with the name in bold italic and no picture; both columns in the face colour, an etched line
// between them at 193; the places 2 under the header, 4 further in than Luna's, their words 3 after the
// icon; the body ending 2 under the last place; All Programs 32 tall under its line, in plain words with a
// small black arrow; and the bottom bar 37 tall under an etched line, its icons the 16-colour ones, 7 from
// the edge.
const int CLASSIC_EDGE = 2, CLASSIC_HEADER = 40, CLASSIC_TOP = 2, CLASSIC_BODY_END = 2, CLASSIC_MORE = 32,
          CLASSIC_LOGOFF = 37, CLASSIC_DIVIDER = 193, CLASSIC_NAME_X = 12, CLASSIC_ICON_Y = 7, CLASSIC_RIGHT_MARGIN = 7,
          CLASSIC_MORE_TEXT = 40;
const int CASCADE_ROW = 23, CASCADE_ICON = 16;
// The classic Start menu, measured on XP's own (explorer.exe's menu 204 in a menu band): a 1-pixel border; the
// edition's banner 21 wide down the left, at the bottom, the rest above it in the bitmap's top-left colour;
// rows 32 tall, the words 42 after the banner; the Start Menu folder's items (32-pixel icons 2 after the
// banner), a line
// (9 rows, the line on the fifth), menu 204's items, a line (7 rows, on the fourth) and Log Off and Turn Off,
// a row under them; menu 204's items have 24-pixel icons, 6 after the banner and 4 down. The menu is its
// widest words and 91 wide. Its cascades: rows 20 tall, the icon 3 in and the
// words 27 in, as wide as their widest words and 50; a separator 8 rows, its line on the fourth, 2 from each
// side. Their arrows are 4 x 7, 9 from the right; a cascade covers the right border of the menu it opens from,
// its top border a row above the row's.
const int CLASSIC_MENU_BANNER = 21, CLASSIC_MENU_ROW = 32, CLASSIC_MENU_ICON_X = 2, CLASSIC_MENU_SMALL_ICON = 24,
          CLASSIC_MENU_SMALL_ICON_X = 6, CLASSIC_MENU_TEXT_X = 42, CLASSIC_MENU_EXTRA = 91, CLASSIC_MENU_FIRST_LINE = 9, CLASSIC_MENU_SECOND_LINE = 7;
const int FLAT_ROW = 20, FLAT_ICON_X = 3, FLAT_TEXT_X = 27, FLAT_EXTRA = 50, FLAT_SEPARATOR = 8, FLAT_ARROW_RIGHT = 9;
// explorer.exe's banners (sub_1015839, by IsOS): 162 Whistler Server... 166 Home Edition, 167 Professional
const int BITMAP_BANNER_PROFESSIONAL = 167;
// Menu 204's commands
enum { MENU_START = 204, CMD_PROGRAMS = 504, CMD_DOCUMENTS = 501, CMD_SETTINGS = 508, CMD_SEARCH = 520, CMD_HELP = 503,
       CMD_RUN = 401, CMD_LOG_OFF = 402, CMD_FAVORITES = 507, CMD_CONTROL_PANEL = 505, CMD_NETWORK = 557, CMD_PRINTERS = 510, CMD_TASKBAR = 413 };
const int NCPA_ICON = 1001, SHELL_FIND_FILES = 23232, ICON_SETTINGS = 22, ICON_SMALL_CONTROL_PANEL = 137, ICON_NETWORK = 18, ICON_TASKBAR = 40;

// XP's menu arrow, 4 x 7, its point at the right
void menuArrow(QPainter &painter, int x, int top, const QColor &colour)
{
    for (int k = 0; k < 4; ++k)
        painter.fillRect(x + k, top + k, 1, 7 - 2 * k, colour);
}

// Classic's Log Off and Turn Off pictures, as XP shows them: the 16-colour images reduced to their main
// colour at the VGA palette's half strength (olive, maroon), white and the greys, without the dithering
QImage classicBarIcon(const QImage &source)
{
    QImage image = source.convertToFormat(QImage::Format_ARGB32);
    QHash<QRgb, int> counts;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = QColor::fromRgba(image.pixel(x, y));
            if (c.alpha() > 128 && !(c.red() == c.green() && c.green() == c.blue()))
                ++counts[c.rgb()];
        }
    QRgb main = 0;
    int most = 0;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        if (it.value() > most) {
            most = it.value();
            main = it.key();
        }
    const QColor dark(qRed(main) > 128 ? 128 : qRed(main), qGreen(main) > 128 ? 128 : qGreen(main), qBlue(main) > 128 ? 128 : qBlue(main));
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = QColor::fromRgba(image.pixel(x, y));
            if (c.alpha() > 128 && !(c.red() == c.green() && c.green() == c.blue()))
                image.setPixel(x, y, dark.rgb());
        }
    return image;
}

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

Cascade::Cascade(const QList<MenuEntry> &entries, QPoint corner, bool upward, bool flat)
    : QWidget(nullptr, Qt::ToolTip | Qt::X11BypassWindowManagerHint | Qt::FramelessWindowHint)
    , m_entries(entries)
    , m_flat(flat)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setFocusPolicy(Qt::NoFocus);
    ShellTheme *theme = ShellTheme::instance();
    QFontMetrics metrics(theme->systemFont(QStringLiteral("menufont")));
    if (flat) {
        int textWidth = 0, height = 2;
        for (const MenuEntry &entry : entries) {
            textWidth = qMax(textWidth, metrics.horizontalAdvance(QString(entry.text).remove(QLatin1Char('&'))));
            const int row = theme->scaled(entry.separator ? FLAT_SEPARATOR : FLAT_ROW);
            m_rects.append(QRect(1, height - 1, 0, row));
            height += row;
        }
        const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
        const int width = qMin(textWidth + theme->scaled(FLAT_EXTRA), screen.width() / 2);
        for (QRect &r : m_rects)
            r.setWidth(width - 2);
        height = qMin(height, screen.height());
        QPoint at = upward ? corner - QPoint(0, height) : corner;
        at.setY(qBound(screen.top(), at.y(), screen.bottom() + 1 - height));
        at.setX(qMin(at.x(), screen.right() + 1 - width));
        setGeometry(QRect(at, QSize(width, height)));
        return;
    }
    int textWidth = 80;
    for (const MenuEntry &entry : entries)
        textWidth = qMax(textWidth, metrics.horizontalAdvance(entry.text));
    const int row = qMax(theme->scaled(CASCADE_ROW), metrics.height() + theme->scaled(4));
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

int Cascade::nextSelectable(int from, int step) const
{
    const int count = int(m_entries.size());
    int at = from;
    for (int i = 0; i < count; ++i) {
        at = at < 0 ? (step > 0 ? 0 : count - 1) : (at + step + count) % count;
        if (!m_entries[at].separator && m_entries[at].enabled)
            return at;
    }
    return -1;
}

QList<int> Cascade::entriesFor(QChar letter) const
{
    QList<int> out;
    for (int i = 0; i < m_entries.size(); ++i)
        if (!m_entries[i].separator && m_entries[i].enabled && !m_entries[i].text.isEmpty()) {
            // its access key (&Programs), or else its first letter
            const QString &text = m_entries[i].text;
            const qsizetype amp = text.indexOf(QLatin1Char('&'));
            const QChar key = amp >= 0 && amp + 1 < text.size() ? text.at(amp + 1) : text.at(0);
            if (key.toLower() == letter.toLower())
                out.append(i);
        }
    return out;
}

void Cascade::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    ShellTheme *theme = ShellTheme::instance();
    w4w::Renderer *renderer = theme->renderer();
    if (m_flat) {
        // COLOR_MENU, a COLOR_BTNSHADOW border (Windows Classic: the raised edge), COLOR_MENUHILIGHT under the hot row
        painter.fillRect(rect(), theme->system(QStringLiteral("menu"), Qt::white));
        if (renderer) {
            painter.setPen(theme->system(QStringLiteral("btnshadow"), Qt::gray));
            painter.drawRect(rect().adjusted(0, 0, -1, -1));
        } else {
            theme->raised(painter, rect());
        }
        painter.setFont(theme->systemFont(QStringLiteral("menufont")));
        const QColor hilight = theme->system(renderer ? QStringLiteral("menuhilight") : QStringLiteral("highlight"), QColor(49, 106, 197));
        const int icon = theme->scaled(CASCADE_ICON);
        for (int i = 0; i < m_entries.size(); ++i) {
            const QRect r = m_rects[i];
            const MenuEntry &entry = m_entries[i];
            if (entry.separator) {
                painter.fillRect(QRect(r.x() + 2, r.y() + theme->scaled(3), r.width() - 4, 1), theme->system(QStringLiteral("btnshadow"), Qt::gray));
                continue;
            }
            const bool hot = i == m_hot && entry.enabled;
            if (hot)
                painter.fillRect(r, hilight);
            painter.drawPixmap(r.x() + theme->scaled(FLAT_ICON_X) - 1, r.y() + (r.height() - icon) / 2, entry.icon.pixmap(icon, icon));
            const QColor text = !entry.enabled ? theme->system(QStringLiteral("graytext"), Qt::gray)
                                : hot          ? theme->system(QStringLiteral("highlighttext"), Qt::white)
                                               : theme->system(QStringLiteral("menutext"), Qt::black);
            painter.setPen(text);
            const int textX = r.x() + theme->scaled(FLAT_TEXT_X) - 1;
            painter.drawText(QRect(textX, r.y() - 1, r.right() - textX, r.height()), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine | Qt::TextHideMnemonic, entry.text);
            if (entry.children)
                menuArrow(painter, width() - theme->scaled(FLAT_ARROW_RIGHT) - 1, r.y() + (r.height() - 7) / 2, text);
        }
        return;
    }
    const Part toolbar{QStringLiteral("toolbar"), {}, {}, 0, QStringLiteral("startmenu")};
    if (renderer) {
        renderer->draw(&painter, rect(), toolbar);
    } else {
        painter.fillRect(rect(), theme->system(QStringLiteral("menu"), Qt::lightGray));
        theme->raised(painter, rect());
    }
    painter.setFont(theme->systemFont(QStringLiteral("menufont")));
    const int icon = theme->scaled(CASCADE_ICON);
    for (int i = 0; i < m_entries.size(); ++i) {
        const QRect r = m_rects[i];
        const bool hot = i == m_hot && m_entries[i].enabled;
        if (hot) {
            if (renderer)
                renderer->draw(&painter, r, Part{QStringLiteral("toolbar"), QStringLiteral("button"), {}, 2, QStringLiteral("startmenu")});
            else
                painter.fillRect(r, theme->system(QStringLiteral("highlight"), Qt::darkBlue));
        }
        const QPixmap pixmap = m_entries[i].icon.pixmap(icon, icon);
        painter.drawPixmap(r.x() + theme->scaled(7), r.y() + (r.height() - icon) / 2, pixmap);
        QColor text = hot ? Qt::white : (renderer ? renderer->colour(toolbar, QStringLiteral("textcolor")).value_or(Qt::black) : QColor(Qt::black));
        if (!m_entries[i].enabled)
            text = theme->system(QStringLiteral("graytext"), Qt::gray);
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

// Connect To's cascade: the network's connections (nmcli's), then Show all connections
static QList<MenuEntry> connections()
{
    QList<MenuEntry> entries;
    QProcess nmcli;
    nmcli.start(QStringLiteral("nmcli"), {QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("NAME,TYPE"), QStringLiteral("connection"), QStringLiteral("show")});
    nmcli.waitForFinished(2000);
    const QIcon icon(QPixmap::fromImage(ShellTheme::instance()->shellIcon(ICON_CONNECT, ShellTheme::instance()->scaled(16))));
    for (const QString &line : QString::fromUtf8(nmcli.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        // NAME:TYPE, a colon in the name written "\:"
        const qsizetype colon = line.lastIndexOf(QLatin1Char(':'));
        const QString name = line.left(colon).replace(QLatin1String("\\:"), QLatin1String(":")), type = line.mid(colon + 1);
        if (type == QLatin1String("loopback") || type == QLatin1String("bridge"))
            continue;
        entries.append({name, icon, [name] { QProcess::startDetached(QStringLiteral("nmcli"), {QStringLiteral("connection"), QStringLiteral("up"), name}); }, {}, false});
    }
    if (!entries.isEmpty())
        entries.append({{}, {}, {}, {}, true});
    entries.append({QObject::tr("Show all connections"), icon, [] { applets::open(QStringLiteral("network")); }, {}, false});
    return entries;
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
    m_openTimer.setInterval(CASCADE_DELAY_MS);
    connect(&m_openTimer, &QTimer::timeout, this, [this] { openChildren(0); });
    m_switchTimer.setSingleShot(true);
    m_switchTimer.setInterval(CASCADE_DELAY_MS);
    connect(&m_switchTimer, &QTimer::timeout, this, &StartPanel::switchCascade);
    build(bottomLeft);
}

StartPanel::~StartPanel()
{
    if (m_infoTip)
        m_infoTip->deleteLater();
    closeCascades(0);
}

void StartPanel::showInfoTip(const QPoint &at, const QString &text)
{
    ShellTheme *theme = ShellTheme::instance();
    auto *tip = qobject_cast<QLabel *>(m_infoTip.data());
    if (!tip) {
        tip = new QLabel(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::BypassWindowManagerHint
                                     | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
        tip->setAttribute(Qt::WA_ShowWithoutActivating);
        tip->setAttribute(Qt::WA_TransparentForMouseEvents);
        tip->setFocusPolicy(Qt::NoFocus);
        tip->setWordWrap(true);
        tip->setMaximumWidth(theme->scaled(300));
        tip->setStyleSheet(QStringLiteral("QLabel { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                                          "stop:0 #ffffff, stop:1 #ffffe1); border: 1px solid #808080; padding: 6px; }"));
        m_infoTip = tip;
    }
    tip->setText(text);
    tip->adjustSize();
    QRect room = tip->rect().translated(at);
    const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
    QPoint place = at;
    if (room.right() > screen.right())
        place.setX(screen.right() - tip->width() + 1);
    if (room.bottom() > screen.bottom())
        place.setY(at.y() - tip->height() - theme->scaled(18));
    tip->move(place);
    tip->show();
    tip->raise();
}

void StartPanel::hideInfoTip()
{
    if (m_infoTip)
        m_infoTip->hide();
}

void StartPanel::addLeft(int &y, const QString &text, const QString &subtitle, const QIcon &icon, std::function<void()> action, bool big)
{
    ShellTheme *theme = ShellTheme::instance();
    Item item;
    const int height = theme->scaled(big ? LEFT_BIG : LEFT_SMALL);
    const int size = theme->scaled(TaskbarSettings::load().panelSmallIcons ? 24 : 32);
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
    item.text = text;
    item.bold = bold;
    item.icon = theme->shellIcon(iconId, theme->scaled(PLACE_ICON));
    item.enabled = bool(action) || arrow;
    item.action = std::move(action);
    item.arrow = arrow;
    m_items.append(item);
    y += height;
}

// "All Programs" and its arrow (the style's picture at its own size, as XP draws it), together in the
// middle of the column, under its line
QRect StartPanel::moreArrow() const
{
    ShellTheme *theme = ShellTheme::instance();
    if (!theme->renderer()) {
        // Windows Classic: the words 40 into the column, in the menu's plain font, a small arrow 13 after them
        const int words = QFontMetrics(theme->systemFont(QStringLiteral("menufont"))).horizontalAdvance(QString(m_moreText).remove(QLatin1Char('&')));
        const QRect band = moreBand();
        return QRect(band.x() + theme->scaled(CLASSIC_MORE_TEXT) + words + theme->scaled(13), band.center().y() - 3, 4, 7);
    }
    QSize size(theme->scaled(19), theme->scaled(19));
    if (w4w::Renderer *renderer = theme->renderer())
        if (const QSize art = renderer->trueSize(panelPart("moreprogramsarrow")); !art.isEmpty())
            size = art;
    const int gap = theme->scaled(MORE_GAP);
    const int text = QFontMetrics(theme->systemFont(QStringLiteral("menufont"), true)).horizontalAdvance(QString(m_moreText).remove(QLatin1Char('&')));
    const QRect band = moreBand();
    return QRect(band.x() + (band.width() - text - gap - size.width() + 1) / 2 + text + gap, band.y() + (band.height() - size.height() + 1) / 2,
                 size.width(), size.height());
}

// The part of All Programs' rectangle under its line
QRect StartPanel::moreBand() const
{
    // (Luna's line is a pixel tall at the top of it, Classic's etched one two)
    return m_more.adjusted(0, ShellTheme::instance()->scaled(ShellTheme::instance()->renderer() ? 1 : 2), 0, 0);
}

namespace {
QString startMenuFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/win4wine/Start Menu");
}

QList<MenuEntry> favoritesEntries()
{
    QList<MenuEntry> out;
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/win4wine/Favorites");
    QDir().mkpath(folder);
    const QFileInfoList entries = QDir(folder).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries) {
        const QString path = entry.filePath();
        if (entry.suffix() == QLatin1String("desktop")) {
            if (const auto program = programs::fromFile(path))
                out.append({program->name, programs::iconOf(*program), [program] { programs::launch(*program); }, {}});
        } else if (entry.isDir()) {
            out.append({entry.fileName(), fileicons::forFile(entry), [path] { FileBrowser::open(path); }, {}});
        } else {
            out.append({entry.fileName(), fileicons::forFile(entry), [path] { programs::open(path); }, {}});
        }
    }
    if (out.isEmpty())
        out.append({QObject::tr("(Empty)"), QIcon(), {}, {}, false, false});
    return out;
}
} // namespace

QList<MenuEntry> StartPanel::classicDocuments() const
{
    // My Documents and My Pictures, a line, then the recent documents (or (Empty)), as XP's
    ShellTheme *theme = ShellTheme::instance();
    const auto icon = [theme](int id) { return QIcon(QPixmap::fromImage(theme->shellIcon(id, theme->scaled(CASCADE_ICON)))); };
    const auto opens = [](QStandardPaths::StandardLocation location) {
        return [location] { programs::open(xdgDir(location)); };
    };
    QList<MenuEntry> out{{shellString(SHELL_DOCUMENTS, "My Documents"), icon(ICON_DOCUMENTS), opens(QStandardPaths::DocumentsLocation), {}},
                         {shellString(SHELL_PICTURES, "My Pictures"), icon(ICON_PICTURES), opens(QStandardPaths::PicturesLocation), {}}};
    MenuEntry line;
    line.separator = true;
    out.append(line);
    out.append(recentDocuments());
    return out;
}

void StartPanel::buildClassicMenu(QPoint bottomLeft)
{
    ShellTheme *theme = ShellTheme::instance();
    m_classicMenu = true;
    m_userName = userName();
    // menu 204's words; its command IDs say which entry is which
    QHash<int, QString> words;
    if (explorer()) {
        QMenu menu;
        const QHash<int, QAction *> actions = xpprogram::fillMenu(&menu, *explorer(), MENU_START);
        for (auto it = actions.cbegin(); it != actions.cend(); ++it)
            words.insert(it.key(), it.value()->text());
    }
    const auto word = [words](int id, const char *fallback) { return words.value(id, QLatin1String(fallback)); };
    if (explorer())
        m_banner = xpfiles::bitmap(*explorer(), BITMAP_BANNER_PROFESSIONAL);

    const TaskbarSettings classicSettings = TaskbarSettings::load();
    const int classicIcon = classicSettings.classicSmallIcons ? 16 : CLASSIC_MENU_SMALL_ICON;
    const auto big = [theme, classicIcon](int id) { return QIcon(QPixmap::fromImage(theme->shellIcon(id, theme->scaled(classicIcon)))); };
    const auto small = [theme](int id) { return QIcon(QPixmap::fromImage(theme->shellIcon(id, theme->scaled(CASCADE_ICON)))); };
    struct Row
    {
        QString text;
        QIcon icon;
        std::function<void()> action;
        std::function<QList<MenuEntry>()> children;
    };
    // The Start Menu folder's own items above the line: XP's setup puts Set Program Access and Defaults there
    QList<Row> top;
    QIcon spad;
    if (const auto moricons = xpfiles::resources(xpfiles::find(QStringLiteral("moricons.dll"))))
        spad = QIcon(QPixmap::fromImage(xpfiles::icon(*moricons, MORICONS_SPAD, theme->scaled(32))));
    const QString startFolder = startMenuFolder();
    QDir().mkpath(startFolder);
    const QFileInfoList startEntries = QDir(startFolder).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : startEntries) {
        const QString path = entry.filePath();
        if (entry.suffix() == QLatin1String("desktop")) {
            if (const auto program = programs::fromFile(path))
                top.append({program->name, programs::iconOf(*program), [program] { programs::launch(*program); }, {}});
        } else if (entry.isDir()) {
            top.append({entry.fileName(), fileicons::forFile(entry), [path] { FileBrowser::open(path); }, {}});
        } else {
            top.append({entry.fileName(), fileicons::forFile(entry), [path] { programs::open(path); }, {}});
        }
    }
    top.append({tr("Set Program Access and Defaults"), spad, [] { applets::open(QStringLiteral("programs")); }, {}});
    QList<Row> middle;
    middle.append({word(CMD_PROGRAMS, "&Programs"), big(ICON_PROGRAMS), {}, [this] { return allPrograms(); }});
    if (classicSettings.classicFavorites)
        middle.append({word(CMD_FAVORITES, "F&avorites"), big(ICON_RECENT), {}, [] { return favoritesEntries(); }});
    middle.append({word(CMD_DOCUMENTS, "&Documents"), big(ICON_RECENT), {}, [this] { return classicDocuments(); }});
    const auto controlPanel = m_controlPanel;
    middle.append({word(CMD_SETTINGS, "&Settings"), big(ICON_SETTINGS), {}, [=] {
        QList<MenuEntry> settings{{word(CMD_CONTROL_PANEL, "&Control Panel"), small(ICON_SMALL_CONTROL_PANEL), controlPanel, {}}};
        MenuEntry line;
        line.separator = true;
        settings.append(line);
        // ncpa.cpl's icon, as the Control Panel shows it
        QIcon network = small(ICON_NETWORK);
        if (const auto ncpa = xpfiles::resources(xpfiles::find(QStringLiteral("ncpa.cpl"))))
            if (const QImage image = xpfiles::icon(*ncpa, NCPA_ICON, theme->scaled(CASCADE_ICON)); !image.isNull())
                network = QIcon(QPixmap::fromImage(image));
        settings.append({word(CMD_NETWORK, "&Network Connections"), network, [] { applets::open(QStringLiteral("network")); }, {}});
        const QString printers = QStandardPaths::findExecutable(QStringLiteral("system-config-printer"));
        MenuEntry printersEntry{word(CMD_PRINTERS, "&Printers and Faxes"), small(ICON_PRINTERS), [] {
            QProcess::startDetached(QStringLiteral("system-config-printer"), {}); }, {}};
        printersEntry.enabled = !printers.isEmpty();
        settings.append(printersEntry);
        settings.append({word(CMD_TASKBAR, "&Taskbar and Start Menu"), small(ICON_TASKBAR), [] { applets::open(QStringLiteral("taskbar")); }, {}});
        return settings;
    }});
    middle.append({word(CMD_SEARCH, "Sear&ch"), big(ICON_SEARCH), {}, [=] {
        // the find extensions: shell32's own, For Files or Folders
        return QList<MenuEntry>{{shellString(SHELL_FIND_FILES, "For &Files or Folders..."), small(ICON_SEARCH), [] { FileBrowser::open(places::Search); }, {}}};
    }});
    middle.append({word(CMD_HELP, "&Help and Support"), big(ICON_HELP), {}, {}});
    if (classicSettings.classicRun)
        middle.append({word(CMD_RUN, "&Run..."), big(ICON_RUN), [] { showRunDialog(); }, {}});
    // Log Off %s... with the user's name; Shut Down... becomes Turn Off Computer... (string 731)
    QList<Row> bottom;
    if (classicSettings.classicLogOff)
        bottom.append({word(CMD_LOG_OFF, "&Log Off %s...").replace(QLatin1String("%s"), m_userName), big(ICON_LOG_OFF), m_logOff, {}});
    bottom.append({explorerString(EXPLORER_TURN_OFF, "T&urn Off Computer..."), big(ICON_TURN_OFF), m_turnOff, {}});

    const QFontMetrics metrics(theme->systemFont(QStringLiteral("menufont")));
    int textWidth = 0;
    for (const QList<Row> *rows : {&top, &middle, &bottom})
        for (const Row &row : *rows)
            textWidth = qMax(textWidth, metrics.horizontalAdvance(QString(row.text).remove(QLatin1Char('&'))));
    const int width = textWidth + theme->scaled(CLASSIC_MENU_EXTRA);
    const int left = 1 + theme->scaled(CLASSIC_MENU_BANNER);
    // the rows end one before the right border
    const int rowWidth = width - left - 2;
    int y = 1;
    const auto add = [&](const QList<Row> &rows, int iconSize) {
        for (const Row &row : rows) {
            Item item;
            item.rect = QRect(left, y, rowWidth, theme->scaled(CLASSIC_MENU_ROW));
            item.text = row.text;
            item.icon = row.icon.pixmap(theme->scaled(iconSize), theme->scaled(iconSize)).toImage();
            item.action = row.action;
            item.children = row.children;
            item.arrow = bool(row.children);
            item.enabled = true;
            m_items.append(item);
            y += item.rect.height();
        }
    };
    const auto line = [&](int rows, int at) {
        Item separator;
        separator.separator = true;
        separator.rect = QRect(left + 2, y + at, rowWidth - 3, 1);
        m_items.append(separator);
        y += rows;
    };
    add(top, 32);
    line(theme->scaled(CLASSIC_MENU_FIRST_LINE), theme->scaled(4));
    add(middle, CLASSIC_MENU_SMALL_ICON);
    line(theme->scaled(CLASSIC_MENU_SECOND_LINE), theme->scaled(3));
    add(bottom, CLASSIC_MENU_SMALL_ICON);
    const int height = y + 2;
    m_left = QRect(0, 0, width, height);
    setGeometry(QRect(bottomLeft - QPoint(0, height), QSize(width, height)));
}

void StartPanel::paintClassicMenu(QPainter &painter)
{
    ShellTheme *theme = ShellTheme::instance();
    w4w::Renderer *renderer = theme->renderer();
    painter.fillRect(rect(), theme->system(QStringLiteral("menu"), Qt::white));
    if (renderer) {
        painter.setPen(theme->system(QStringLiteral("btnshadow"), Qt::gray));
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
    } else {
        theme->raised(painter, rect());
    }
    // The banner at the bottom, its top-left colour above it
    const QRect band(1, 1, theme->scaled(CLASSIC_MENU_BANNER), height() - 2);
    if (!m_banner.isNull()) {
        painter.fillRect(band, m_banner.pixelColor(0, 0));
        painter.drawImage(band.x(), band.bottom() + 1 - m_banner.height(), m_banner);
    } else {
        painter.fillRect(band, theme->system(QStringLiteral("activecaption"), Qt::darkBlue));
    }
    painter.setFont(theme->systemFont(QStringLiteral("menufont")));
    const QColor hilight = theme->system(renderer ? QStringLiteral("menuhilight") : QStringLiteral("highlight"), QColor(49, 106, 197));
    const int mnemonics = m_cues ? Qt::TextShowMnemonic : Qt::TextHideMnemonic;
    for (int i = 0; i < m_items.size(); ++i) {
        const Item &item = m_items[i];
        if (item.separator) {
            painter.fillRect(item.rect, theme->system(QStringLiteral("btnshadow"), Qt::gray));
            continue;
        }
        const QRect r = item.rect;
        const bool hot = i == m_hot && item.enabled;
        if (hot)
            painter.fillRect(r, hilight);
        const bool large = item.icon.height() > theme->scaled(CLASSIC_MENU_SMALL_ICON);
        painter.drawImage(r.x() + theme->scaled(large ? CLASSIC_MENU_ICON_X : CLASSIC_MENU_SMALL_ICON_X), r.y() + (r.height() - item.icon.height()) / 2, item.icon);
        const QColor text = !item.enabled ? theme->system(QStringLiteral("graytext"), Qt::gray)
                            : hot         ? theme->system(QStringLiteral("highlighttext"), Qt::white)
                                          : theme->system(QStringLiteral("menutext"), Qt::black);
        painter.setPen(text);
        const int textX = r.x() + theme->scaled(CLASSIC_MENU_TEXT_X);
        // (a row higher than Qt centres them)
        painter.drawText(QRect(textX, r.y() - 1, r.right() - textX, r.height()), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine | mnemonics, item.text);
        if (item.arrow)
            menuArrow(painter, width() - theme->scaled(FLAT_ARROW_RIGHT) - 1, r.y() + (r.height() - 7) / 2, text);
    }
}

void StartPanel::build(QPoint bottomLeft)
{
    if (TaskbarSettings::load().classicStartMenu) {
        buildClassicMenu(bottomLeft);
        return;
    }
    ShellTheme *theme = ShellTheme::instance();
    const TaskbarSettings panelSettings = TaskbarSettings::load();
    const auto shown = [&panelSettings](const char *key) { return !panelSettings.panelHidden.contains(QLatin1String(key)); };
    // Keep XP's normal columns; grow them for large fonts and translated labels.
    const QFontMetrics placeMetrics(theme->systemFont(QStringLiteral("menufont"), true));
    int column = theme->scaled(COLUMN);
    for (const QString &label : {shellString(SHELL_DOCUMENTS, "My Documents"), explorerString(EXPLORER_RECENT, "My Recent &Documents"),
                                shellString(SHELL_PICTURES, "My Pictures"), shellString(SHELL_MUSIC, "My Music"),
                                shellString(SHELL_COMPUTER, "My Computer"), explorerString(EXPLORER_CONTROL_PANEL, "&Control Panel"),
                                shellString(SHELL_PRINTERS, "Printers and Faxes"), explorerString(EXPLORER_HELP, "Help and Support")})
        column = qMax(column, placeMetrics.horizontalAdvance(QString(label).remove(QLatin1Char('&'))) + theme->scaled(PLACE_ICON + 32));
    const int width = 2 * column;
    const bool classic = !theme->renderer();
    const int userHeight = theme->scaled(classic ? CLASSIC_EDGE + 1 + CLASSIC_HEADER : USER_HEIGHT);
    const int logoffHeight = theme->scaled(classic ? CLASSIC_LOGOFF : LOGOFF_HEIGHT);
    const int moreHeight = theme->scaled(classic ? CLASSIC_MORE : MORE_HEIGHT);
    m_userName = userName();
    m_picture = userPicture(theme->scaled(48));
    // XP underlines its letter (All &Programs) even when opened with the mouse
    m_moreText = explorerString(EXPLORER_ALL_PROGRAMS, "All &Programs");
    m_logOffText = explorerString(EXPLORER_LOG_OFF, "&Log Off").remove(QLatin1String("..."));
    m_turnOffText = explorerString(EXPLORER_TURN_OFF, "T&urn Off Computer").remove(QLatin1String("..."));
    m_logOffIcon = theme->shellIcon(ICON_LOG_OFF, theme->scaled(24));
    m_turnOffIcon = theme->shellIcon(ICON_TURN_OFF, theme->scaled(24));
    if (classic)
        if (const auto shell32 = xpfiles::resources(xpfiles::find(QStringLiteral("shell32.dll")))) {
            const QImage logOff = xpfiles::icon(*shell32, ICON_LOG_OFF, theme->scaled(24), 4), turnOff = xpfiles::icon(*shell32, ICON_TURN_OFF, theme->scaled(24), 4);
            if (!logOff.isNull() && !turnOff.isNull()) {
                m_logOffIcon = classicBarIcon(logOff);
                m_turnOffIcon = classicBarIcon(turnOff);
            }
        }

    // The places first: they decide the height
    m_right = classic ? QRect(theme->scaled(CLASSIC_DIVIDER + 1), userHeight, width - theme->scaled(CLASSIC_DIVIDER + 1 + CLASSIC_EDGE), 0)
                      : QRect(column, userHeight, width - column, 0);
    int y = userHeight + theme->scaled(classic ? CLASSIC_TOP : COLUMN_TOP);
    const auto opens = [](const QString &path) { return [path] { programs::open(path); }; };
    const auto tip = [this](int id) { m_items.last().tip = explorerString(id, ""); };
    if (shown("Documents")) {
        addPlace(y, ICON_DOCUMENTS, shellString(SHELL_DOCUMENTS, "My Documents"), true, opens(xdgDir(QStandardPaths::DocumentsLocation)));
        tip(TIP_DOCUMENTS);
    }
    if (shown("Recent") && panelSettings.panelRecentDocs) {
        addPlace(y, ICON_RECENT, explorerString(EXPLORER_RECENT, "My Recent &Documents"), true, {}, true);
        m_items.last().children = [this] { return recentDocuments(); };
        tip(TIP_RECENT);
    }
    if (shown("Pictures")) {
        addPlace(y, ICON_PICTURES, shellString(SHELL_PICTURES, "My Pictures"), true, opens(xdgDir(QStandardPaths::PicturesLocation)));
        tip(TIP_PICTURES);
    }
    if (shown("Music")) {
        addPlace(y, ICON_MUSIC, shellString(SHELL_MUSIC, "My Music"), true, opens(xdgDir(QStandardPaths::MusicLocation)));
        tip(TIP_MUSIC);
    }
    if (shown("Favorites") && panelSettings.panelFavorites) {
        addPlace(y, ICON_RECENT, tr("F&avorites"), true, {}, true);
        m_items.last().children = [] { return favoritesEntries(); };
    }
    if (shown("Computer")) {
        addPlace(y, ICON_COMPUTER, shellString(SHELL_COMPUTER, "My Computer"), true, opens(QStringLiteral("file:///")));
        tip(TIP_COMPUTER);
    }
    Item separator;
    separator.separator = true;
    separator.rect = classic ? QRect(m_right.x() + theme->scaled(5), y + theme->scaled(PLACE_LINE), m_right.width() - theme->scaled(6), theme->scaled(2))
                             : QRect(m_right.x() + theme->scaled(10), y + theme->scaled(PLACE_LINE), m_right.width() - theme->scaled(20), theme->scaled(2));
    m_items.append(separator);
    y += theme->scaled(8);
    if (shown("ControlPanel")) {
        addPlace(y, ICON_CONTROL_PANEL, explorerString(EXPLORER_CONTROL_PANEL, "&Control Panel"), false, m_controlPanel);
        tip(TIP_CONTROL_PANEL);
    }
    // XP's next two: the programs' defaults (Add or Remove Programs here), and the network's connections
    addPlace(y, 0, tr("Set Program Access and Defaults"), false, [] { applets::open(QStringLiteral("programs")); });
    const auto moricons = xpfiles::resources(xpfiles::find(QStringLiteral("moricons.dll")));
    m_items.last().icon = moricons ? xpfiles::icon(*moricons, MORICONS_SPAD, theme->scaled(PLACE_ICON)) : QImage();
    if (shown("Network") && !QStandardPaths::findExecutable(QStringLiteral("nmcli")).isEmpty()) {
        addPlace(y, ICON_CONNECT, explorerString(EXPLORER_CONNECT, "Connect To"), false, {}, true);
        m_items.last().children = [] { return connections(); };
        tip(TIP_CONNECT);
    }
    const bool printers = !QStandardPaths::findExecutable(QStringLiteral("system-config-printer")).isEmpty();
    if (shown("Printers"))
        addPlace(y, ICON_PRINTERS, shellString(SHELL_PRINTERS, "Printers and Faxes"), false,
                 printers ? std::function<void()>([] { QProcess::startDetached(QStringLiteral("system-config-printer"), {}); }) : std::function<void()>());
    separator.rect.moveTop(y + theme->scaled(PLACE_LINE));
    m_items.append(separator);
    y += theme->scaled(8);
    if (shown("Help")) {
        addPlace(y, ICON_HELP, explorerString(EXPLORER_HELP, "&Help and Support"), false, {});
        tip(TIP_HELP);
    }
    if (shown("Search")) {
        addPlace(y, ICON_SEARCH, explorerString(EXPLORER_SEARCH, "&Search"), false, [] { FileBrowser::open(places::Search); });
        tip(TIP_SEARCH);
    }
    if (shown("Run")) {
        addPlace(y, ICON_RUN, explorerString(EXPLORER_RUN, "&Run..."), false, [] { showRunDialog(); });
        tip(TIP_RUN);
    }
    const auto browser = programs::defaultBrowser(), mail = programs::defaultMail();
    const int pinned = (browser ? 1 : 0) + (mail ? 1 : 0);
    const int recentPrograms = qBound(0, panelSettings.panelMinMFU, 30);
    const int leftHeight = theme->scaled((classic ? CLASSIC_TOP : LEFT_TOP) + pinned * LEFT_BIG + (pinned ? LEFT_LINE : 0) + recentPrograms * LEFT_SMALL + LEFT_LINE) + moreHeight;
    const int bodyHeight = std::max({theme->scaled(BODY_MIN), y - userHeight + theme->scaled(classic ? CLASSIC_BODY_END : BODY_END), leftHeight});
    m_right.setHeight(bodyHeight);

    m_user = QRect(0, 0, width, userHeight);
    m_logoff = QRect(0, userHeight + bodyHeight, width, logoffHeight);
    const int leftX = classic ? theme->scaled(CLASSIC_EDGE + 3) : 0;
    m_left = QRect(leftX, userHeight, (classic ? theme->scaled(CLASSIC_DIVIDER) : column) - leftX, bodyHeight - moreHeight);
    m_more = QRect(leftX, userHeight + bodyHeight - moreHeight, m_left.width(), moreHeight);

    // The left column: the default browser and mail, then the most used programs
    y = m_left.y() + theme->scaled(classic ? CLASSIC_TOP : LEFT_TOP);
    const auto launcher = [](const programs::Program &program) { return [program] { programs::launch(program); }; };
    if (browser) {
        addLeft(y, explorerString(EXPLORER_INTERNET, "Internet"), browser->name, programs::iconOf(*browser), launcher(*browser), true);
        tip(TIP_INTERNET);
    }
    if (mail) {
        addLeft(y, explorerString(EXPLORER_MAIL, "E-mail"), mail->name, programs::iconOf(*mail), launcher(*mail), true);
        tip(TIP_MAIL);
    }
    if (browser || mail) {
        Item line;
        line.separator = true;
        line.rect = classic ? QRect(m_left.x() + theme->scaled(4), y + theme->scaled(2), theme->scaled(180), theme->scaled(2))
                            : QRect(m_left.x() + theme->scaled(10), y + theme->scaled(2), m_left.width() - theme->scaled(20), theme->scaled(2));
        m_items.append(line);
        y += theme->scaled(LEFT_LINE);
    }
    // (Asking for two more: the browser and the mail may be among them.)
    int added = 0;
    const QList<programs::Program> used = programs::mostUsed(recentPrograms + 2);
    for (const programs::Program &program : used) {
        if ((browser && program.id == browser->id) || (mail && program.id == mail->id))
            continue;
        if (added++ >= recentPrograms)
            break;
        addLeft(y, program.name, {}, programs::iconOf(program), launcher(program), false);
        m_items.last().tip = program.comment;
        m_items.last().programId = program.id;
    }
    Item moreLine;
    moreLine.separator = true;
    moreLine.rect = classic ? QRect(m_left.x() + theme->scaled(4), m_more.y(), theme->scaled(180), theme->scaled(2))
                            : QRect(m_left.x() + theme->scaled(12), m_more.y(), m_left.width() - theme->scaled(20), theme->scaled(2));
    m_items.append(moreLine);

    // The bottom's buttons, right-aligned as XP's: each its icon, 4 pixels and its words, 10 from the edge
    // and 11 apart
    QFontMetrics metrics(theme->systemFont(QStringLiteral("menufont")));
    const int icon = theme->scaled(24), gap = theme->scaled(4);
    const int turnOffWidth = icon + gap + metrics.horizontalAdvance(QString(m_turnOffText).remove(QLatin1Char('&')));
    const int logOffWidth = icon + gap + metrics.horizontalAdvance(QString(m_logOffText).remove(QLatin1Char('&')));
    // (A pixel above the middle on XP's.)
    const int buttonY = m_logoff.y() + (logoffHeight - icon) / 2 - theme->scaled(1);
    m_turnOffButton = QRect(width - theme->scaled(classic ? CLASSIC_RIGHT_MARGIN : 10) - turnOffWidth,
                            classic ? m_logoff.y() + theme->scaled(CLASSIC_ICON_Y) : buttonY, turnOffWidth, icon);
    m_logOffButton = QRect(m_turnOffButton.x() - theme->scaled(11) - logOffWidth, m_turnOffButton.y(), logOffWidth, icon);

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

// An etched line of Windows Classic: shadow, then highlight (one under or beside the other)
void StartPanel::etch(QPainter &painter, const QRect &rect)
{
    ShellTheme *theme = ShellTheme::instance();
    const bool across = rect.width() >= rect.height();
    const QColor shadow = theme->system(QStringLiteral("btnshadow"), Qt::gray), light = theme->system(QStringLiteral("btnhighlight"), Qt::white);
    painter.fillRect(across ? QRect(rect.x(), rect.y(), rect.width(), 1) : QRect(rect.x(), rect.y(), 1, rect.height()), shadow);
    painter.fillRect(across ? QRect(rect.x(), rect.y() + 1, rect.width(), 1) : QRect(rect.x() + 1, rect.y(), 1, rect.height()), light);
}

// The panel under Windows Classic: a window's raised edge, the caption's gradient behind the name,
// the line between the columns and the one over the bottom bar
void StartPanel::paintClassic(QPainter &painter)
{
    ShellTheme *theme = ShellTheme::instance();
    const QRect r = rect();
    const auto line = [&](QPoint from, QPoint to, const char *colour, const QColor &fallback) {
        painter.setPen(theme->system(QLatin1String(colour), fallback));
        painter.drawLine(from, to);
    };
    line(r.topLeft(), r.topRight(), "light3d", QColor(212, 208, 200));
    line(r.topLeft(), r.bottomLeft(), "light3d", QColor(212, 208, 200));
    line(r.topLeft() + QPoint(1, 1), r.topRight() + QPoint(-1, 1), "btnhighlight", Qt::white);
    line(r.topLeft() + QPoint(1, 1), r.bottomLeft() + QPoint(1, -1), "btnhighlight", Qt::white);
    line(r.bottomLeft(), r.bottomRight(), "dkshadow3d", Qt::black);
    line(r.topRight(), r.bottomRight(), "dkshadow3d", Qt::black);
    line(r.bottomLeft() + QPoint(1, -1), r.bottomRight() + QPoint(-1, -1), "btnshadow", Qt::gray);
    line(r.topRight() + QPoint(-1, 1), r.bottomRight() + QPoint(-1, -1), "btnshadow", Qt::gray);
    const int inset = theme->scaled(CLASSIC_EDGE + 1);
    const QRect header(inset, inset, width() - 2 * inset, theme->scaled(CLASSIC_HEADER));
    QLinearGradient gradient(header.topLeft(), header.topRight());
    gradient.setColorAt(0, theme->frame().activeCaption);
    gradient.setColorAt(1, theme->frame().activeGradient);
    painter.fillRect(header, gradient);
    QFont name = theme->font(QStringLiteral("Tahoma, 14, bold"));
    name.setItalic(true);
    painter.setFont(name);
    painter.setPen(theme->frame().activeText);
    painter.drawText(header.adjusted(theme->scaled(CLASSIC_NAME_X) - inset, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, m_userName);
    etch(painter, QRect(theme->scaled(CLASSIC_DIVIDER), m_user.bottom() + 1, 2, m_logoff.y() - m_user.bottom() - 1));
    etch(painter, QRect(inset, m_logoff.y(), width() - 2 * inset, 2));
}

void StartPanel::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    if (m_classicMenu) {
        paintClassicMenu(painter);
        return;
    }
    ShellTheme *theme = ShellTheme::instance();
    w4w::Renderer *renderer = theme->renderer();
    const QColor highlight = theme->system(QStringLiteral("highlight"), Qt::darkBlue);
    const QColor face = theme->system(QStringLiteral("btnface"), Qt::lightGray);
    painter.fillRect(rect(), face);
    if (!renderer) {
        paintClassic(painter);
    } else {
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
    }

    // The items. XP underlines the access keys when the keys opened the panel, or once one is pressed
    // (All Programs' always).
    const int mnemonics = m_cues ? Qt::TextShowMnemonic : Qt::TextHideMnemonic;
    const QFont normal = theme->systemFont(QStringLiteral("menufont")), bold = theme->systemFont(QStringLiteral("menufont"), true);
    const QColor leftText = renderer ? renderer->colour(panelPart("proglist"), QStringLiteral("textcolor")).value_or(Qt::black) : QColor(Qt::black);
    const QColor rightText = renderer ? renderer->colour(panelPart("placeslist"), QStringLiteral("textcolor")).value_or(Qt::black) : QColor(Qt::black);
    // The selection, the pointer's or the keys', is COLOR_HIGHLIGHT on XP: not the style's HotTracking.
    const QColor leftHot = highlight, rightHot = highlight;
    for (int i = 0; i < m_items.size(); ++i) {
        const Item &item = m_items[i];
        const bool onLeft = item.rect.x() < m_right.x();
        if (item.separator) {
            if (renderer)
                renderer->draw(&painter, item.rect, panelPart(onLeft ? "proglistseparator" : "placeslistseparator"));
            else
                etch(painter, item.rect);
            continue;
        }
        const bool hot = i == m_hot && item.enabled;
        // Measured on XP's: the selection 2 inside its row, 8 in from the programs' pane and 6 from its
        // right, 6 in from the places' and 8 from their right; a program's icon 2 inside it.
        QRect selection = item.rect;
        if (renderer)
            selection = onLeft ? QRect(m_left.x() + theme->scaled(8), item.rect.y() + theme->scaled(2), m_left.width() - theme->scaled(14), item.rect.height() - theme->scaled(4))
                               : QRect(m_right.x() + theme->scaled(6), item.rect.y() + theme->scaled(2), m_right.width() - theme->scaled(14), item.rect.height() - theme->scaled(4));
        if (hot)
            painter.fillRect(selection, onLeft ? leftHot : rightHot);
        const int iconX = renderer && onLeft ? selection.x() + theme->scaled(2) : item.rect.x() + theme->scaled(3);
        if (!item.icon.isNull())
            painter.drawImage(iconX, item.rect.y() + (item.rect.height() - item.icon.height()) / 2, item.icon);
        const int textX = iconX + item.icon.width() + theme->scaled(!renderer && !onLeft ? 3 : 4);
        const int textEnd = item.rect.right() - (item.arrow ? theme->scaled(14) : theme->scaled(2));
        QColor colour = hot ? QColor(Qt::white) : onLeft ? leftText : rightText;
        if (!item.enabled)
            colour = theme->system(QStringLiteral("graytext"), Qt::gray);
        painter.setPen(colour);
        painter.setFont(item.bold ? bold : normal);
        if (item.subtitle.isEmpty() && !onLeft) {
            // A place's words are a pixel above the middle, as on XP; too long for the column, they take two lines.
            const QRect words(textX, item.rect.y() - theme->scaled(1), textEnd - textX, item.rect.height());
            if (painter.fontMetrics().horizontalAdvance(QString(item.text).remove(QLatin1Char('&'))) > words.width())
                painter.drawText(words.translated(0, theme->scaled(1)), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap | mnemonics, item.text);
            else
                painter.drawText(words, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine | mnemonics, item.text);
        } else if (item.subtitle.isEmpty()) {
            painter.drawText(QRect(textX, item.rect.y(), textEnd - textX, item.rect.height()), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                             painter.fontMetrics().elidedText(item.text, Qt::ElideRight, textEnd - textX));
        } else {
            const int line = qMax(theme->scaled(14), QFontMetrics(normal).height()), top = item.rect.y() + (item.rect.height() - 2 * line) / 2;
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
    painter.setFont(renderer ? bold : normal);
    const QRect band = moreBand();
    // (XP's words a pixel above the middle of their band)
    const QRect moreText = renderer ? QRect(band.x(), band.y() - theme->scaled(1), arrow.x() - band.x() - theme->scaled(MORE_GAP), band.height())
                                    : QRect(band.x() + theme->scaled(CLASSIC_MORE_TEXT), band.y(), band.width(), band.height());
    if (moreHot) {
        // XP's: 8 in from each side, 6 from the top and the bottom
        const QRect band = renderer ? m_more.adjusted(theme->scaled(8), theme->scaled(6), -theme->scaled(8), -theme->scaled(6))
                                    : QRect(m_more.x() + theme->scaled(8), m_more.y() + theme->scaled(3), m_more.width() - theme->scaled(10), m_more.height() - theme->scaled(6));
        painter.fillRect(band, leftHot);
    }
    painter.setPen(moreHot ? QColor(Qt::white) : leftText);
    painter.drawText(moreText, (renderer ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter | Qt::TextSingleLine | Qt::TextShowMnemonic, m_moreText);
    if (renderer) {
        renderer->draw(&painter, arrow, panelPart("moreprogramsarrow"));
    } else {
        // The menus' own arrow
        for (int column = 0; column < 4; ++column)
            painter.fillRect(QRect(arrow.x() + column, arrow.y() + column, 1, 7 - 2 * column), moreHot ? QColor(Qt::white) : leftText);
    }

    // Log Off and Turn Off Computer
    painter.setFont(normal);
    const auto logoffButton = [&](const QRect &rect, const QImage &icon, const QString &text, bool hot) {
        // Selected with the keys, XP fills it with COLOR_HIGHLIGHT, 3 around the picture and 8 after the
        // words; under the pointer it only lightens the picture.
        if (hot && m_byKeys)
            painter.fillRect(keySelection(rect), highlight);
        painter.drawImage(rect.topLeft(), icon);
        if (hot && !m_byKeys) {
            // XP lightens the hot button's icon.
            QImage light = icon.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            QPainter tint(&light);
            tint.setCompositionMode(QPainter::CompositionMode_SourceAtop);
            tint.fillRect(light.rect(), QColor(255, 255, 255, 70));
            tint.end();
            painter.drawImage(rect.topLeft(), light);
        }
        // White on Luna's blue bar; the menu's text on Classic's
        painter.setPen(renderer ? QColor(Qt::white) : theme->system(QStringLiteral("menutext"), Qt::black));
        // (A pixel above the middle of the picture on XP's)
        const int raise = renderer ? theme->scaled(1) : 0;
        painter.drawText(QRect(rect.x() + icon.width() + theme->scaled(4), rect.y() - raise, rect.width() - icon.width() - theme->scaled(4), rect.height()),
                         Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine | mnemonics, text);
    };
    logoffButton(m_logOffButton, m_logOffIcon, m_logOffText, m_hotPart == LogOffHot);
    logoffButton(m_turnOffButton, m_turnOffIcon, m_turnOffText, m_hotPart == TurnOffHot);
}

QList<MenuEntry> StartPanel::allPrograms() const
{
    ShellTheme *theme = ShellTheme::instance();
    QList<MenuEntry> out;
    const QIcon folderIcon(QPixmap::fromImage(theme->shellIcon(ICON_PROGRAMS, theme->scaled(CASCADE_ICON))));
    auto folders = programs::folders();
    // XP's own in Accessories, which it makes if Linux's programs did not
    folders[QStringLiteral("Accessories")];
    for (auto it = folders.cbegin(); it != folders.cend(); ++it) {
        MenuEntry folder;
        folder.text = it.key();
        folder.icon = folderIcon;
        const QList<programs::Program> items = it.value();
        const bool accessories = it.key() == QLatin1String("Accessories");
        folder.children = [items, accessories] {
            QList<MenuEntry> entries;
            for (const programs::Program &program : items) {
                MenuEntry entry;
                entry.text = program.name;
                entry.icon = programs::iconOf(program);
                entry.action = [program] { programs::launch(program); };
                entries.append(entry);
            }
            if (accessories) {
                MenuEntry prompt;
                prompt.text = QObject::tr("Command Prompt");
                if (const auto cmd = xpfiles::resources(xpfiles::find(QStringLiteral("cmd.exe"))))
                    // Its one group, by the name cmd.exe gives it
                    prompt.icon = QIcon(QPixmap::fromImage(xpfiles::icon(*cmd, cmd->names(QStringLiteral("14")).value(0), ShellTheme::instance()->scaled(CASCADE_ICON))));
                prompt.action = [] { programs::openCommandPrompt(QDir::homePath()); };
                entries.append(prompt);
                // Notepad and Calculator, by the names and icons of their files
                const auto accessory = [&entries](const char *file, const QString &group, int nameId, std::function<void()> action) {
                    const auto pe = xpfiles::resources(xpfiles::find(QLatin1String(file)));
                    if (!pe)
                        return;
                    MenuEntry entry;
                    entry.text = xpfiles::string(*pe, nameId);
                    entry.icon = QIcon(QPixmap::fromImage(xpfiles::icon(*pe, group, ShellTheme::instance()->scaled(CASCADE_ICON))));
                    entry.action = std::move(action);
                    entries.append(entry);
                };
                accessory("notepad.exe", QStringLiteral("2"), 9, [] { programs::openNotepad(); });
                accessory("calc.exe", QStringLiteral("SC"), 83, [] { programs::openCalculator(); });
                std::sort(entries.begin(), entries.end(), [](const MenuEntry &a, const MenuEntry &b) { return a.text.compare(b.text, Qt::CaseInsensitive) < 0; });
            }
            return entries;
        };
        out.append(folder);
    }
#ifdef XP_MEDIA_PLAYER
    // Windows Media Player, among XP's programs after the folders, by wmplayer.exe's name and icon
    if (const auto pe = xpfiles::resources(xpfiles::find(QStringLiteral("wmplayer.exe"))); pe && !xpfiles::find(QStringLiteral("wmp.dll")).isEmpty()) {
        MenuEntry player;
        player.text = xpfiles::fileDescription(*pe);
        if (player.text.isEmpty())
            player.text = QStringLiteral("Windows Media Player");
        player.icon = QIcon(QPixmap::fromImage(xpfiles::icon(*pe, pe->names(QStringLiteral("14")).value(0), theme->scaled(CASCADE_ICON))));
        player.action = [] { programs::openMediaPlayer(); };
        out.append(player);
    }
#endif
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
    // Customize Start Menu's Clear List hides what was there before it was pressed.
    const qint64 cleared = QSettings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/native-explorer.conf"), QSettings::IniFormat)
                               .value(QStringLiteral("StartMenu/RecentCleared"), 0).toLongLong();
    ShellTheme *theme = ShellTheme::instance();
    const QIcon documentIcon(QPixmap::fromImage(theme->shellIcon(ICON_DOCUMENT, theme->scaled(CASCADE_ICON))));
    QList<MenuEntry> out;
    for (const auto &[modified, href] : std::as_const(found)) {
        if (out.size() >= 15)
            break;
        const QString path = QUrl(href).toLocalFile();
        if (path.isEmpty() || !QFileInfo::exists(path))
            continue;
        if (QDateTime::fromString(modified, Qt::ISODate).toMSecsSinceEpoch() <= cleared)
            continue;
        MenuEntry entry;
        entry.text = QFileInfo(path).fileName();
        entry.icon = documentIcon;
        entry.action = [path] { programs::open(path); };
        out.append(entry);
    }
    if (out.isEmpty()) {
        // explorer.exe's word for an empty cascade, greyed
        MenuEntry empty;
        empty.text = tr("(Empty)");
        empty.enabled = false;
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
            // the classic menu's cascade covers its right border, its top a row above the row's
            corner = m_classicMenu ? mapToGlobal(QPoint(m_items[m_openWanted].rect.right() + 1, m_items[m_openWanted].rect.y() - 1))
                                   : mapToGlobal(QPoint(m_items[m_openWanted].rect.right(), m_items[m_openWanted].rect.y()));
        }
        closeCascades(0);
        if (entries.isEmpty())
            return;
        auto *cascade = new Cascade(entries, corner, upward, m_classicMenu);
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
    auto *cascade = new Cascade(entries, m_classicMenu ? QPoint(row.right() + 1, row.y() - 1) : QPoint(row.right() + 3, row.y() - 3), false, m_classicMenu);
    cascade->setOwnerRow(parent->hot());
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
        // Crossing the panel to get here asked for nothing; the keys now work in this cascade's family.
        m_openTimer.stop();
        m_keysInCascade = true;
        // On its way into this one, the rows that led here stay lit, and what they had asked for
        // meanwhile is forgotten.
        if (m_switchLevel < level)
            m_switchTimer.stop();
        for (int up = level; up > 0; --up)
            m_cascades[up - 1]->setHot(m_cascades[up]->ownerRow());
        // Its border, which the pointer crosses going out: no row, and nothing changes.
        if (entry == -1)
            return;
        if (entry != cascade->hot()) {
            cascade->setHot(entry);
            // Another row lights up at once; the cascades change a moment later, as XP's menus,
            // so that the pointer can cross rows on its way to what is open.
            m_switchLevel = level;
            m_switchTimer.start();
        } else if (m_switchLevel == level) {
            const Cascade *open = m_cascades.value(level + 1);
            if (open && open->ownerRow() == entry)
                m_switchTimer.stop();
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
        m_keysInCascade = false;
        m_byKeys = false;
        update();
        // Moving onto something else in the panel closes the open cascades, after a moment.
        int wanted = -1;
        if (part == MoreHot)
            wanted = -2;
        else if (hot >= 0 && m_items[hot].children)
            wanted = hot;
        if (wanted != -1 || rect().contains(at)) {
            m_openWanted = wanted;
            m_openTimer.start();
        }
    }
}

void StartPanel::switchCascade()
{
    Cascade *cascade = m_cascades.value(m_switchLevel);
    if (!cascade)
        return;
    const int row = cascade->hot();
    const Cascade *open = m_cascades.value(m_switchLevel + 1);
    if (open && open->ownerRow() == row)
        return;
    closeCascades(m_switchLevel + 1);
    if (row >= 0 && cascade->entry(row).children)
        openChildren(m_switchLevel + 1);
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
    if (event->button() == Qt::RightButton) {
        const QPoint at = event->position().toPoint();
        for (const Item &item : std::as_const(m_items))
            if (!item.separator && !item.programId.isEmpty() && item.rect.contains(at)) {
                QMenu menu;
                QAction *remove = menu.addAction(explorerString(8225, "Remove from This List"));
                if (menu.exec(global) == remove) {
                    programs::removeUse(item.programId);
                    close();
                }
                return;
            }
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    for (int level = int(m_cascades.size()) - 1; level >= 0; --level) {
        const int entry = m_cascades[level]->entryAt(global);
        if (entry == -2)
            continue;
        if (entry >= 0 && m_cascades[level]->entry(entry).action && m_cascades[level]->entry(entry).enabled) {
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

QChar StartPanel::accessKey(const QString &text)
{
    // The letter after a single &; without one, the first letter
    for (qsizetype i = 0; i + 1 < text.size(); ++i)
        if (text[i] == QLatin1Char('&')) {
            if (text[i + 1] != QLatin1Char('&'))
                return text[i + 1].toLower();
            ++i;
        }
    return text.isEmpty() ? QChar() : text[0].toLower();
}

void StartPanel::setKeyboardCues(bool on)
{
    m_cues = on;
    update();
}

QRect StartPanel::keySelection(const QRect &button)
{
    ShellTheme *theme = ShellTheme::instance();
    return button.adjusted(-theme->scaled(3), -theme->scaled(3), theme->scaled(8), theme->scaled(3));
}

QString StartPanel::tipOf(const Target &target) const
{
    // (All Programs has none on XP.)
    if (target.item == MoreItem)
        return QString();
    if (target.item == LogOffItem)
        return xpfiles::explorerString(TIP_LOG_OFF, "");
    if (target.item == TurnOffItem)
        return xpfiles::explorerString(TIP_TURN_OFF, "");
    return m_items[target.item].tip;
}

void StartPanel::showTip(const Target &target)
{
    // Measured on XP's: the tip's border at the item's words, its top just under the selection.
    ShellTheme *theme = ShellTheme::instance();
    const QString text = tipOf(target);
    if (text.isEmpty()) {
        hideInfoTip();
        return;
    }
    int x = target.rect.x(), bottom = target.rect.bottom();
    if (target.pane == BottomPane) {
        // Log Off's and Turn Off's, 7 back from the selection's right
        const QRect selection = keySelection(target.rect);
        x = selection.right() - theme->scaled(7);
        bottom = selection.bottom();
    } else if (target.item >= 0) {
        const bool onLeft = target.pane == LeftPane;
        const int icon = m_items[target.item].icon.width();
        x = onLeft ? m_left.x() + theme->scaled(10) + icon + theme->scaled(4) : target.rect.x() + theme->scaled(3) + icon + theme->scaled(4);
        bottom -= theme->scaled(2);
    }
    showInfoTip(mapToGlobal(QPoint(x, bottom + 1)), text);

}

bool StartPanel::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        // The pointer's: at the pointer, as Qt places them
        const auto *help = static_cast<QHelpEvent *>(event);
        const QList<Target> all = targets();
        for (const Target &target : all)
            if (target.rect.contains(help->pos())) {
                const QString text = tipOf(target);
                if (!text.isEmpty())
                    showInfoTip(help->globalPos(), text);
                return true;
            }
        hideInfoTip();
        return true;
    }
    return QWidget::event(event);
}

QList<StartPanel::Target> StartPanel::targets() const
{
    QList<Target> left, places;
    for (int i = 0; i < m_items.size(); ++i) {
        const Item &item = m_items[i];
        if (item.separator || !item.enabled)
            continue;
        // the classic menu is one column, Log Off and Turn Off in it
        const bool onLeft = m_classicMenu || item.rect.x() < m_right.x();
        (onLeft ? left : places).append({onLeft ? LeftPane : PlacesPane, i, item.rect, item.text});
    }
    QList<Target> out = left;
    if (m_classicMenu)
        return out;
    out.append({MorePane, MoreItem, m_more, m_moreText});
    out.append(places);
    out.append({BottomPane, LogOffItem, m_logOffButton, m_logOffText});
    out.append({BottomPane, TurnOffItem, m_turnOffButton, m_turnOffText});
    return out;
}

int StartPanel::currentTarget(const QList<Target> &all) const
{
    for (int i = 0; i < all.size(); ++i) {
        const int item = all[i].item;
        if ((item >= 0 && item == m_hot) || (item == MoreItem && m_hotPart == MoreHot) || (item == LogOffItem && m_hotPart == LogOffHot)
            || (item == TurnOffItem && m_hotPart == TurnOffHot))
            return i;
    }
    return -1;
}

void StartPanel::select(const Target &target)
{
    // A selection made with the keys opens nothing, and closes what the pointer had opened.
    m_openTimer.stop();
    closeCascades(0);
    m_keysInCascade = false;
    m_hot = target.item >= 0 ? target.item : -1;
    m_hotPart = target.item == MoreItem ? MoreHot : target.item == LogOffItem ? LogOffHot : target.item == TurnOffItem ? TurnOffHot : Nothing;
    m_byKeys = true;
    update();
    showTip(target);
}

void StartPanel::openByKeyboard(int wanted)
{
    // The cascade opens with its first entry selected, and the keys go on in it.
    m_openTimer.stop();
    m_openWanted = wanted;
    openChildren(0);
    if (Cascade *cascade = m_cascades.value(0)) {
        cascade->setHot(cascade->nextSelectable(-1, 1));
        m_keysInCascade = true;
    }
}

void StartPanel::activate(const Target &target)
{
    select(target);
    if (target.item == MoreItem)
        openByKeyboard(-2);
    else if (target.item == LogOffItem)
        run(m_logOff);
    else if (target.item == TurnOffItem)
        run(m_turnOff);
    else if (m_items[target.item].children)
        openByKeyboard(target.item);
    else
        run(m_items[target.item].action);
}

bool StartPanel::cascadeKey(QKeyEvent *event)
{
    const int level = int(m_cascades.size()) - 1;
    Cascade *cascade = m_cascades.value(level);
    if (!cascade || !m_keysInCascade)
        return false;
    const int hot = cascade->hot();
    const bool openable = hot >= 0 && cascade->entry(hot).enabled && cascade->entry(hot).children;
    switch (event->key()) {
    case Qt::Key_Down:
    case Qt::Key_Up:
        m_switchTimer.stop();
        closeCascades(level + 1);
        cascade->setHot(cascade->nextSelectable(hot, event->key() == Qt::Key_Down ? 1 : -1));
        return true;
    case Qt::Key_Right:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (openable) {
            openChildren(level + 1);
            if (Cascade *child = m_cascades.value(level + 1))
                child->setHot(child->nextSelectable(-1, 1));
        } else if (event->key() != Qt::Key_Right && hot >= 0 && cascade->entry(hot).enabled && cascade->entry(hot).action) {
            run(cascade->entry(hot).action);
        }
        return true;
    case Qt::Key_Left:
    case Qt::Key_Escape:
        // Back to the row or the panel's item that opened it
        closeCascades(level);
        m_keysInCascade = level > 0;
        return true;
    default:
        break;
    }
    const QString typed = event->text();
    if (typed.size() == 1 && typed[0].isPrint()) {
        // A menu's letters: one entry runs or opens, several take turns
        const QList<int> matches = cascade->entriesFor(typed[0]);
        if (matches.size() == 1) {
            cascade->setHot(matches.first());
            QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            return cascadeKey(&enter);
        }
        if (!matches.isEmpty()) {
            const auto next = std::upper_bound(matches.cbegin(), matches.cend(), hot);
            cascade->setHot(next == matches.cend() ? matches.first() : *next);
        }
        return true;
    }
    return true;
}

void StartPanel::keyPressEvent(QKeyEvent *event)
{
    // As XP's Start panel does, seen on XP's own: with nothing selected, Down takes the first item and Up
    // the last. Up and Down go through a pane, then on to the next pane round them all (programs, All
    // Programs, places, Log Off and Turn Off), at its first item going down and its last going up. Left
    // and Right go to the next pane that way whose rectangle has the item's height in it, at its nearest
    // item (DV2ControlHost's routing); Right opens a cascade. A letter: the item whose access key it is,
    // or whose first letter it is when it has none, runs or opens when it is the only one, else the next
    // one is selected. Escape and Left close the last cascade.
    // A key shows the access keys from then on.
    if (!m_cues) {
        m_cues = true;
        update();
    }
    if (cascadeKey(event))
        return;
    const QList<Target> all = targets();
    const int current = currentTarget(all);
    const auto paneRect = [&](Pane pane) { return pane == LeftPane ? m_left : pane == MorePane ? m_more : pane == PlacesPane ? m_right : m_logoff; };
    const int key = event->key();
    if (key == Qt::Key_Escape) {
        close();
        return;
    }
    if (key == Qt::Key_Down || key == Qt::Key_Up) {
        if (all.isEmpty())
            return;
        if (current < 0) {
            select(key == Qt::Key_Down ? all.first() : all.last());
            return;
        }
        // The targets are in the panes' order, so the next pane is entered at its first item going
        // down and at its last going up.
        const int step = key == Qt::Key_Down ? 1 : -1;
        select(all[(current + step + int(all.size())) % int(all.size())]);
        return;
    }
    if (key == Qt::Key_Left || key == Qt::Key_Right) {
        if (current < 0) {
            select(all.first());
            return;
        }
        const Target &here = all[current];
        if (key == Qt::Key_Right && (here.item == MoreItem || (here.item >= 0 && m_items[here.item].children))) {
            activate(here);
            return;
        }
        if (here.pane == BottomPane) {
            for (const Target &t : all)
                if (t.item == (key == Qt::Key_Left ? LogOffItem : TurnOffItem))
                    select(t);
            return;
        }
        const int y = here.rect.center().y();
        const int step = key == Qt::Key_Right ? 1 : -1;
        for (int k = 1; k < 4; ++k) {
            const Pane pane = Pane((here.pane + step * k + 4) % 4);
            const QRect area = paneRect(pane);
            if (y < area.top() || y > area.bottom())
                continue;
            int best = -1;
            for (int i = 0; i < all.size(); ++i)
                if (all[i].pane == pane && (best < 0 || qAbs(all[i].rect.center().y() - y) < qAbs(all[best].rect.center().y() - y)))
                    best = i;
            if (best >= 0) {
                select(all[best]);
                return;
            }
        }
        return;
    }
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (current >= 0)
            activate(all[current]);
        return;
    }
    const QString typed = event->text();
    if (typed.size() == 1 && typed[0].isPrint()) {
        QList<int> matches;
        for (int i = 0; i < all.size(); ++i)
            if (accessKey(all[i].text) == typed[0].toLower())
                matches.append(i);
        if (matches.size() == 1) {
            activate(all[matches.first()]);
        } else if (!matches.isEmpty()) {
            const auto next = std::upper_bound(matches.cbegin(), matches.cend(), current);
            select(all[next == matches.cend() ? matches.first() : *next]);
        }
        return;
    }
    QWidget::keyPressEvent(event);
}

void StartPanel::hideEvent(QHideEvent *)
{
    hideInfoTip();
    closeCascades(0);
    Q_EMIT closed();
}
