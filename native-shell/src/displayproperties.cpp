// SPDX-License-Identifier: LGPL-2.1-or-later
#include "displayproperties.h"

#include "fileicons.h"
#include "frames.h"
#include "x11.h"
#include "session.h"
#include "sessiondialog.h"
#include "xpdialog.h"
#include "xpfiles.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QSaveFile>
#include <QSettings>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPointer>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>

#include <memory>

#include <xcb/xcb.h>

namespace {

// themeui.dll's dialogs, their controls and strings
const int DIALOG_THEMES = 1000, DIALOG_APPEARANCE = 1001;
const int THEME_COMBO = 1101, THEME_SAVE = 1107, THEME_DELETE = 1108, THEME_PREVIEW = 1103;
const int APPEARANCE_PREVIEW = 1110, APPEARANCE_STYLE = 1111, APPEARANCE_COLOUR = 1114, APPEARANCE_SIZE = 1116, APPEARANCE_ADVANCED = 1117, APPEARANCE_EFFECTS = 1118;
const int STRING_PLEASE_WAIT = 2050, PLEASE_WAIT_MS = 700;
const int STRING_BROWSE = 2010, STRING_NOT_A_THEME = 3006, STRING_DELETE_ERROR = 3010;
const int STRING_WINDOWS_CLASSIC = 2016, STRING_WINDOWS_XP = 2017, STRING_CLASSIC_STYLE = 2003, STRING_NORMAL = 2019, STRING_NONE = 2022, STRING_CURRENT = 2004;
const int BITMAP_MONITOR = 126, BITMAP_COLOURS = 125, BITMAP_ENERGY = 122;
const int DIALOG_SCREEN_SAVER = 1015, DIALOG_SETTINGS = 1007;
const int SAVER_WAIT_LABEL = 1316, SAVER_POWER_LOGO = 1305;
const int SAVER_MONITOR = 1302, SAVER_COMBO = 1300, SAVER_SETTINGS = 1303, SAVER_PREVIEW = 1304, SAVER_WAIT = 1306, SAVER_UPDOWN = 1307, SAVER_PASSWORD = 1320, SAVER_POWER = 1314;
const int SETTINGS_MONITOR = 1812, SETTINGS_ARRANGE = 1819, SETTINGS_LAYOUT = 1801, SETTINGS_DISPLAYS = 1800, SETTINGS_NAME = 1811, SETTINGS_SLIDER = 1808, SETTINGS_TEXT = 1814, SETTINGS_QUALITY = 1807, SETTINGS_COLOURS = 1813, SETTINGS_TROUBLESHOOT = 1821, SETTINGS_ADVANCED = 1802;
// The monitor bitmap's screen
const QRect MONITOR_SCREEN(16, 17, 152, 112);

const std::optional<w4w::PeResources> &themeui()
{
    static const auto resources = xpfiles::resources(xpfiles::find(QStringLiteral("themeui.dll")));
    return resources;
}

QString text(int id, const char *fallback)
{
    const QString found = themeui() ? xpfiles::string(*themeui(), id) : QString();
    return found.isEmpty() ? QLatin1String(fallback) : found;
}

// What a style's resources offer: its name, its colours and its sizes (ID, name shown)
struct StyleInfo
{
    QString name;
    QList<QPair<QString, QString>> colours, sizes;
};

StyleInfo styleInfo(const QString &path)
{
    StyleInfo info;
    info.name = QFileInfo(path).baseName();
    const auto resources = xpfiles::resources(path);
    if (!resources)
        return info;
    const auto list = [&](const char *type) {
        const QByteArray bytes = resources->find(QLatin1String(type), QStringLiteral("1"));
        return QString::fromUtf16(reinterpret_cast<const char16_t *>(bytes.constData()), bytes.size() / 2).split(QChar(0), Qt::SkipEmptyParts);
    };
    const QByteArray ini = resources->find(QStringLiteral("TEXTFILE"), QStringLiteral("THEMES_INI"));
    QString themes = QString::fromUtf16(reinterpret_cast<const char16_t *>(ini.constData()), ini.size() / 2);
    themes.remove(QLatin1Char('\r'));
    const auto displayName = [&](const QString &section, const QString &fallback) {
        const QRegularExpression pattern(QStringLiteral("^\\[%1\\][^\\[]*?^DisplayName\\s*=\\s*([^;\\n]*)").arg(QRegularExpression::escape(section)),
                                         QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
        const auto match = pattern.match(themes);
        return match.hasMatch() ? match.captured(1).trimmed() : fallback;
    };
    info.name = displayName(QStringLiteral("documentation"), info.name);
    for (const QString &colour : list("COLORNAMES"))
        info.colours.append({colour, displayName(QStringLiteral("ColorScheme.") + colour, colour)});
    for (const QString &size : list("SIZENAMES"))
        info.sizes.append({size, displayName(QStringLiteral("Size.") + size, size)});
    return info;
}

QString ownThemes()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/win4wine/themes");
}

bool isImported(const QString &style)
{
    return style.startsWith(ownThemes() + QLatin1Char('/'));
}

// The theme's folder under ours: what Delete removes
QString importedFolder(const QString &style)
{
    return ownThemes() + QLatin1Char('/') + style.mid(ownThemes().size() + 1).section(QLatin1Char('/'), 0, 0);
}

// The visual style a .theme file comes with: beside it or one folder down, by its name
// first. (Its Path often names XP's own Luna, wherever the file was made.)
QString styleOfTheme(const QString &file)
{
    const QFileInfo theme(file);
    const QDir folder = theme.dir();
    QStringList found;
    for (const QFileInfo &entry : folder.entryInfoList({QStringLiteral("*.msstyles")}, QDir::Files))
        found << entry.absoluteFilePath();
    for (const QFileInfo &sub : folder.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        for (const QFileInfo &entry : QDir(sub.absoluteFilePath()).entryInfoList({QStringLiteral("*.msstyles")}, QDir::Files))
            found << entry.absoluteFilePath();
    for (const QString &path : std::as_const(found))
        if (QFileInfo(path).completeBaseName().compare(theme.completeBaseName(), Qt::CaseInsensitive) == 0)
            return path;
    return found.value(0);
}

// A style's name in the lists. Many a theme keeps Luna's name inside: its folder tells them apart.
QString styleName(const QString &style)
{
    return isImported(style) ? importedFolder(style).section(QLatin1Char('/'), -1) : styleInfo(style).name;
}

bool copyFolder(const QString &from, const QString &to)
{
    if (!QDir().mkpath(to))
        return false;
    bool ok = true;
    for (const QFileInfo &entry : QDir(from).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
        const QString target = to + QLatin1Char('/') + entry.fileName();
        if (entry.isSymLink())
            continue;
        ok = (entry.isDir() ? copyFolder(entry.absoluteFilePath(), target) : QFile::copy(entry.absoluteFilePath(), target)) && ok;
    }
    return ok;
}

// Luna, then any other .msstyles the user has put in their themes folder or an extraction has
QStringList stylePaths()
{
    QStringList out;
    const QString luna = ShellTheme::lunaPath();
    if (!luna.isEmpty())
        out << luna;
    QDirIterator it(ownThemes(), {QStringLiteral("*.msstyles")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        out << it.next();
    return out;
}

QStringList wallpaperPaths()
{
    QStringList out;
    const QString bliss = xpfiles::find(QStringLiteral("bliss.jpg"));
    if (bliss.isEmpty())
        return out;
    const QFileInfoList files = QFileInfo(bliss).absoluteDir().entryInfoList({QStringLiteral("*.jpg"), QStringLiteral("*.bmp"), QStringLiteral("*.png")}, QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &file : files)
        out << file.absoluteFilePath();
    return out;
}

QString wallpaperName(const QString &path)
{
    QString name = QFileInfo(path).completeBaseName();
    if (!name.isEmpty())
        name[0] = name[0].toUpper();
    return name;
}

QString settingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/native-explorer.conf");
}

void setScreenSaver(int minutes)
{
    // The X server's own: it blanks the screen. 0 turns it off.
    xcb_set_screen_saver(x11::connection(), qint16(minutes * 60), 0, XCB_BLANKING_PREFERRED, XCB_EXPOSURES_ALLOWED);
    xcb_flush(x11::connection());
}

// What xrandr offers for the screen, smallest first; nothing without it.
QList<QSize> screenModes()
{
    QList<QSize> modes;
    QProcess process;
    process.start(QStringLiteral("xrandr"), {QStringLiteral("--query")});
    if (!process.waitForFinished(3000))
        return modes;
    static const QRegularExpression line(QStringLiteral("^\\s+(\\d+)x(\\d+)\\s"));
    for (const QString &text : QString::fromLocal8Bit(process.readAllStandardOutput()).split(QLatin1Char(0x0a))) {
        const auto match = line.match(text);
        const QSize size(match.captured(1).toInt(), match.captured(2).toInt());
        if (match.hasMatch() && !modes.contains(size))
            modes.append(size);
    }
    std::sort(modes.begin(), modes.end(), [](const QSize &a, const QSize &b) { return a.width() != b.width() ? a.width() < b.width() : a.height() < b.height(); });
    return modes;
}

QImage loadWallpaper(const QString &path)
{
    QImage image(path);
    return image.isNull() ? image : image.convertToFormat(QImage::Format_RGB32);
}

} // namespace

ThemePreview::ThemePreview(QWidget *parent)
    : QWidget(parent)
{
}

void ThemePreview::setChoice(const LookChoice &choice)
{
    m_choice = choice;
    m_style = ShellTheme::frameStyleFor(choice, ShellTheme::instance()->dpi());
    if (choice.wallpaper != m_wallpaperPath) {
        m_wallpaperPath = choice.wallpaper;
        m_wallpaper = loadWallpaper(m_wallpaperPath);
    }
    update();
}

void ThemePreview::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), m_choice.background);
    if (!m_wallpaper.isNull()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(rect(), m_wallpaper);
    }
    const QColor face = m_style.face;
    w4w::Renderer *renderer = m_style.renderer.get();
    const auto window = [&](const QRect &r, const QString &title, bool active, const QColor &client) {
        FrameLook look;
        look.title = title;
        look.active = active;
        look.icon = QImage();
        painter.save();
        painter.setClipRegion(frameShape(r.size(), m_style, false).translated(r.topLeft()));
        drawFrame(painter, r, m_style, look);
        const QRect inside = r.adjusted(m_style.border, m_style.top, -m_style.border, -m_style.border);
        painter.fillRect(inside, client);
        painter.restore();
        return inside;
    };
    const auto windowText = [&](const QRect &inside, const QString &words) {
        painter.setPen(Qt::black);
        painter.setFont(ShellTheme::instance()->font(QStringLiteral("Tahoma, 8")));
        painter.drawText(inside.adjusted(2, 1, 0, 0), Qt::AlignLeft | Qt::AlignTop, words);
    };
    // A window's scroll bar, by the style's parts
    const auto scrollBar = [&](const QRect &inside) {
        const int side = 17;
        const QRect bar(inside.right() + 1 - side, inside.y(), side, inside.height());
        if (!renderer) {
            painter.fillRect(bar, face);
            return;
        }
        using w4w::Part;
        renderer->draw(&painter, bar, Part{QStringLiteral("scrollbar"), QStringLiteral("lowertrackvert"), {}, 1, {}});
        renderer->draw(&painter, QRect(bar.x(), bar.y(), side, side), Part{QStringLiteral("scrollbar"), QStringLiteral("arrowbtn"), {}, 1, {}});
        renderer->draw(&painter, QRect(bar.x(), bar.bottom() + 1 - side, side, side), Part{QStringLiteral("scrollbar"), QStringLiteral("arrowbtn"), {}, 5, {}});
        const QRect thumb(bar.x(), bar.y() + side, side, side);
        renderer->draw(&painter, thumb, Part{QStringLiteral("scrollbar"), QStringLiteral("thumbbtnvert"), {}, 1, {}});
        renderer->draw(&painter, thumb, Part{QStringLiteral("scrollbar"), QStringLiteral("grippervert"), {}, 1, {}});
    };
    const int w = width(), h = height();
    if (m_mode == ThemeSample) {
        const QRect active(w * 32 / 348, h * 34 / 219, w * 215 / 348, h * 125 / 219);
        const QRect inside = window(active, tr("Active Window"), true, Qt::white);
        windowText(inside, tr("Window Text"));
        scrollBar(inside);
        painter.drawImage(w - 48, h - 44, ShellTheme::instance()->shellIcon(fileicons::RecycleEmpty, 32));
    } else {
        window(QRect(w / 12, h / 12, w * 7 / 12, h / 2), tr("Inactive Window"), false, face);
        const QRect inside = window(QRect(w / 6, h / 4, w * 7 / 10, h * 11 / 20), tr("Active Window"), true, Qt::white);
        windowText(inside, tr("Window Text"));
        scrollBar(inside);
        const QRect box = window(QRect(w / 5, h * 9 / 20, w * 3 / 5, h * 9 / 20), tr("Message Box"), true, face);
        windowText(box, tr("Message Text"));
        const QRect ok(box.center().x() - 37, box.bottom() - 26, 75, 23);
        if (renderer) {
            renderer->draw(&painter, ok, w4w::Part{QStringLiteral("button"), QStringLiteral("pushbutton"), {}, 1, {}});
        } else {
            painter.fillRect(ok, face);
            painter.setPen(Qt::black);
            painter.drawRect(ok.adjusted(0, 0, -1, -1));
        }
        painter.setPen(Qt::black);
        painter.drawText(ok, Qt::AlignCenter, tr("OK"));
    }
    painter.setPen(palette().color(QPalette::Mid));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

MonitorPreview::MonitorPreview(QWidget *parent)
    : QWidget(parent)
{
    if (themeui()) {
        m_monitor = xpfiles::bitmap(*themeui(), BITMAP_MONITOR).convertToFormat(QImage::Format_ARGB32);
        // Its magenta is see-through.
        for (int y = 0; y < m_monitor.height(); ++y) {
            auto *row = reinterpret_cast<QRgb *>(m_monitor.scanLine(y));
            for (int x = 0; x < m_monitor.width(); ++x)
                if ((row[x] & 0xffffff) == 0xff00ff)
                    row[x] = 0;
        }
    }
    setFixedSize(m_monitor.isNull() ? QSize(185, 163) : m_monitor.size());
}

void MonitorPreview::setChoice(const LookChoice &choice)
{
    m_choice = choice;
    if (choice.wallpaper != m_wallpaperPath) {
        m_wallpaperPath = choice.wallpaper;
        m_wallpaper = loadWallpaper(m_wallpaperPath);
    }
    update();
}

void MonitorPreview::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.drawImage(0, 0, m_monitor);
    painter.setClipRect(MONITOR_SCREEN);
    painter.fillRect(MONITOR_SCREEN, m_choice.background);
    if (m_wallpaper.isNull())
        return;
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    // The picture at the scale of a 1024-wide screen on this small one
    const qreal scale = qreal(MONITOR_SCREEN.width()) / 1024;
    const QSize shown = (QSizeF(m_wallpaper.size()) * scale).toSize().expandedTo(QSize(1, 1));
    if (m_choice.tile) {
        painter.drawTiledPixmap(MONITOR_SCREEN, QPixmap::fromImage(m_wallpaper.scaled(shown, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)));
    } else if (m_choice.wallpaperStyle == 2) {
        painter.drawImage(MONITOR_SCREEN, m_wallpaper);
    } else {
        QRect target(QPoint(0, 0), shown);
        target.moveCenter(MONITOR_SCREEN.center());
        painter.drawImage(target, m_wallpaper);
    }
}

void DisplayProperties::showTab(Tab tab)
{
    static QPointer<DisplayProperties> dialog;
    if (!dialog)
        dialog = new DisplayProperties;
    dialog->m_tabs->setCurrentIndex(tab);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

DisplayProperties::DisplayProperties()
    : m_choice(ShellTheme::instance()->choice())
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Display Properties"));
    m_tabs = new xpdialog::SheetTabs(this);
    // In XP's order, and what Linux adds after them
    m_tabs->addTab(themesPage(), tr("Themes"));
    m_tabs->addTab(desktopPage(), tr("Desktop"));
    m_tabs->addTab(screenSaverPage(), tr("Screen Saver"));
    m_tabs->addTab(appearancePage(), tr("Appearance"));
    m_tabs->addTab(settingsPage(), tr("Settings"));
    m_tabs->addTab(linuxPage(), tr("Linux"));
    auto *ok = new QPushButton(tr("OK"), this);
    auto *cancel = new QPushButton(tr("Cancel"), this);
    m_apply = new QPushButton(tr("&Apply"), this);
    ok->setDefault(true);
    xpdialog::arrangeSheet(this, m_tabs, {ok, cancel, m_apply});
    m_apply->setEnabled(false);
    connect(m_apply, &QPushButton::clicked, this, &DisplayProperties::apply);
    connect(ok, &QPushButton::clicked, this, [this] {
        apply();
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    fillAppearance();
    selectWallpaper();
    changed();
    m_apply->setEnabled(false);
}

QWidget *DisplayProperties::themesPage()
{
    auto *page = new QWidget;
    if (themeui()) {
        const xpdialog::Page dialog = xpdialog::build(*themeui(), DIALOG_THEMES, page);
        m_theme = qobject_cast<QComboBox *>(dialog.controls.value(THEME_COMBO));
        if (QWidget *save = dialog.controls.value(THEME_SAVE))
            save->setEnabled(false);
        m_deleteTheme = qobject_cast<QPushButton *>(dialog.controls.value(THEME_DELETE));
        if (QWidget *place = dialog.controls.value(THEME_PREVIEW)) {
            m_themePreview = new ThemePreview(page);
            m_themePreview->setGeometry(place->geometry());
            place->hide();
        }
    }
    if (!m_theme)
        m_theme = new QComboBox(page);
    if (!m_themePreview)
        m_themePreview = new ThemePreview(page);
    if (!m_deleteTheme)
        m_deleteTheme = new QPushButton(page);
    connect(m_deleteTheme, &QPushButton::clicked, this, &DisplayProperties::deleteTheme);
    fillThemes();
    connect(m_theme, &QComboBox::activated, this, [this](int index) {
        const QString kind = m_theme->itemData(index).toString();
        if (kind == QLatin1String("browse")) {
            importTheme();
            return;
        }
        if (kind == QLatin1String("xp")) {
            m_choice.style = ShellTheme::lunaPath();
            m_choice.colour = QStringLiteral("NormalColor");
            m_choice.size = QStringLiteral("NormalSize");
            m_choice.wallpaper = xpfiles::find(QStringLiteral("bliss.jpg"));
            m_choice.wallpaperStyle = 2;
            m_choice.tile = false;
            m_choice.background = QColor(0, 78, 152);
        } else if (kind == QLatin1String("classic")) {
            // XP's Windows Classic theme has no picture.
            m_choice.style.clear();
            m_choice.colour = QStringLiteral("NormalColor");
            m_choice.size = QStringLiteral("NormalSize");
            m_choice.wallpaper.clear();
            m_choice.background = QColor(58, 110, 165);
        } else if (kind.endsWith(QLatin1String(".msstyles"))) {
            m_choice.style = kind;
            const StyleInfo info = styleInfo(kind);
            if (!info.colours.isEmpty())
                m_choice.colour = info.colours.first().first;
            if (!info.sizes.isEmpty())
                m_choice.size = info.sizes.first().first;
        } else {
            return;
        }
        fillAppearance();
        selectWallpaper();
        changed();
    });
    return page;
}

// XP's list: its themes, those the user imported, the current one if it is none of them, Browse
void DisplayProperties::fillThemes()
{
    m_theme->clear();
    m_theme->addItem(text(STRING_WINDOWS_XP, "Windows XP"), QStringLiteral("xp"));
    m_theme->addItem(text(STRING_WINDOWS_CLASSIC, "Windows Classic"), QStringLiteral("classic"));
    for (const QString &path : stylePaths())
        if (isImported(path))
            m_theme->addItem(styleName(path), path);
    m_theme->addItem(text(STRING_CURRENT, "Current"), QStringLiteral("current"));
    m_theme->addItem(text(STRING_BROWSE, "Browse..."), QStringLiteral("browse"));
}

QWidget *DisplayProperties::desktopPage()
{
    auto *page = new QWidget;
    // XP's places, measured on its Desktop tab: themeui.dll keeps this page in code
    page->setFixedSize(378, 354);
    m_monitor = new MonitorPreview(page);
    m_monitor->move((378 - m_monitor->width()) / 2, 11);
    auto *label = new QLabel(tr("&Background:"), page);
    label->setGeometry(14, 190, 200, 16);
    m_wallpapers = new QListWidget(page);
    m_wallpapers->setGeometry(14, 208, 273, 113);
    label->setBuddy(m_wallpapers);
    auto *browse = new QPushButton(tr("&Browse..."), page);
    browse->setGeometry(297, 209, 75, 23);
    auto *position = new QLabel(tr("&Position:"), page);
    position->setGeometry(297, 244, 75, 16);
    m_position = new QComboBox(page);
    m_position->setGeometry(297, 260, 75, 21);
    position->setBuddy(m_position);
    m_position->addItems({tr("Center"), tr("Tile"), tr("Stretch")});
    auto *colour = new QLabel(tr("&Color:"), page);
    colour->setGeometry(297, 291, 75, 16);
    m_backgroundButton = new QPushButton(page);
    m_backgroundButton->setGeometry(297, 307, 75, 21);
    colour->setBuddy(m_backgroundButton);
    auto *customize = new QPushButton(tr("Customize &Desktop..."), page);
    customize->setGeometry(14, 330, 135, 23);
    customize->setEnabled(false);

    auto *none = new QListWidgetItem(QIcon(QPixmap::fromImage(ShellTheme::instance()->shellIcon(200, 16))), text(STRING_NONE, "(None)"));
    m_wallpapers->addItem(none);
    const QStringList paths = wallpaperPaths();
    for (const QString &path : paths) {
        auto *item = new QListWidgetItem(fileicons::forFile(QFileInfo(path)), wallpaperName(path));
        item->setData(Qt::UserRole, path);
        m_wallpapers->addItem(item);
    }
    connect(m_wallpapers, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (m_filling || !item)
            return;
        m_choice.wallpaper = item->data(Qt::UserRole).toString();
        changed();
    });
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Browse"), QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
                                                          tr("Pictures (*.jpg *.jpeg *.png *.bmp *.gif)"));
        if (path.isEmpty())
            return;
        m_choice.wallpaper = path;
        selectWallpaper();
        changed();
    });
    connect(m_position, &QComboBox::activated, this, [this](int index) {
        m_choice.tile = index == 1;
        m_choice.wallpaperStyle = index == 2 ? 2 : 0;
        changed();
    });
    connect(m_backgroundButton, &QPushButton::clicked, this, [this] {
        const QColor colour = QColorDialog::getColor(m_choice.background, this);
        if (colour.isValid()) {
            m_choice.background = colour;
            selectWallpaper();
            changed();
        }
    });
    return page;
}

QWidget *DisplayProperties::appearancePage()
{
    auto *page = new QWidget;
    if (themeui()) {
        const xpdialog::Page dialog = xpdialog::build(*themeui(), DIALOG_APPEARANCE, page);
        m_style = qobject_cast<QComboBox *>(dialog.controls.value(APPEARANCE_STYLE));
        m_colour = qobject_cast<QComboBox *>(dialog.controls.value(APPEARANCE_COLOUR));
        m_size = qobject_cast<QComboBox *>(dialog.controls.value(APPEARANCE_SIZE));
        for (int id : {APPEARANCE_ADVANCED, APPEARANCE_EFFECTS})
            if (QWidget *button = dialog.controls.value(id))
                button->setEnabled(false);
        if (QWidget *place = dialog.controls.value(APPEARANCE_PREVIEW)) {
            m_appearancePreview = new ThemePreview(page);
            m_appearancePreview->setMode(ThemePreview::AppearanceSample);
            m_appearancePreview->setGeometry(place->geometry());
            place->hide();
        }
    }
    if (!m_style)
        m_style = new QComboBox(page);
    if (!m_colour)
        m_colour = new QComboBox(page);
    if (!m_size)
        m_size = new QComboBox(page);
    if (!m_appearancePreview)
        m_appearancePreview = new ThemePreview(page);
    connect(m_style, &QComboBox::activated, this, [this] {
        m_choice.style = m_style->currentData().toString();
        const StyleInfo info = styleInfo(m_choice.style);
        if (!info.colours.isEmpty())
            m_choice.colour = info.colours.first().first;
        if (!info.sizes.isEmpty())
            m_choice.size = info.sizes.first().first;
        fillAppearance();
        changed();
    });
    connect(m_colour, &QComboBox::activated, this, [this] {
        m_choice.colour = m_colour->currentData().toString();
        changed();
    });
    connect(m_size, &QComboBox::activated, this, [this] {
        m_choice.size = m_size->currentData().toString();
        changed();
    });
    return page;
}

QWidget *DisplayProperties::screenSaverPage()
{
    auto *page = new QWidget;
    page->setFixedSize(378, 354);
    const QSettings settings(settingsPath(), QSettings::IniFormat);
    const int minutes = settings.value(QStringLiteral("screenSaverMinutes"), 0).toInt();
    if (themeui()) {
        const xpdialog::Page dialog = xpdialog::build(*themeui(), DIALOG_SCREEN_SAVER, page);
        m_saver = qobject_cast<QComboBox *>(dialog.controls.value(SAVER_COMBO));
        for (int id : {SAVER_SETTINGS, SAVER_PREVIEW, SAVER_PASSWORD, SAVER_POWER})
            if (QWidget *control = dialog.controls.value(id))
                control->setEnabled(false);
        if (QWidget *place = dialog.controls.value(SAVER_MONITOR)) {
            m_saverMonitor = new MonitorPreview(page);
            m_saverMonitor->move(place->x() + (place->width() - m_saverMonitor->width()) / 2, place->y());
            place->hide();
        }
        // The edit and its arrows are one control here.
        QWidget *edit = dialog.controls.value(SAVER_WAIT), *arrows = dialog.controls.value(SAVER_UPDOWN);
        if (edit && arrows) {
            m_saverWait = new QSpinBox(page);
            // (The arrows sit inside the edit, whatever the resource's places say.)
            m_saverWait->setGeometry(edit->geometry());
            edit->hide();
            arrows->hide();
            if (auto *label = qobject_cast<QLabel *>(dialog.controls.value(SAVER_WAIT_LABEL)))
                label->setBuddy(m_saverWait);
        }
        if (auto *logo = qobject_cast<QLabel *>(dialog.controls.value(SAVER_POWER_LOGO))) {
            logo->setPixmap(QPixmap::fromImage(xpfiles::bitmap(*themeui(), BITMAP_ENERGY)));
            logo->adjustSize();
        }
    }
    if (!m_saver)
        m_saver = new QComboBox(page);
    if (!m_saverWait)
        m_saverWait = new QSpinBox(page);
    if (!m_saverMonitor)
        m_saverMonitor = new MonitorPreview(page);
    m_saver->addItem(text(STRING_NONE, "(None)"));
    m_saver->addItem(tr("Blank"));
    m_saver->setCurrentIndex(minutes > 0 ? 1 : 0);
    m_saverWait->setRange(1, 9999);
    m_saverWait->setValue(minutes > 0 ? minutes : 10);
    m_saverWait->setEnabled(minutes > 0);
    connect(m_saver, &QComboBox::activated, this, [this](int index) {
        m_saverWait->setEnabled(index > 0);
        m_apply->setEnabled(true);
    });
    connect(m_saverWait, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] {
        if (m_apply)
            m_apply->setEnabled(true);
    });
    return page;
}

void DisplayProperties::applyScreenSaver()
{
    const int minutes = m_saver->currentIndex() > 0 ? m_saverWait->value() : 0;
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("screenSaverMinutes"), minutes);
    setScreenSaver(minutes);
}

void DisplayProperties::restoreScreenSaver()
{
    const QSettings settings(settingsPath(), QSettings::IniFormat);
    if (settings.contains(QStringLiteral("screenSaverMinutes")))
        setScreenSaver(settings.value(QStringLiteral("screenSaverMinutes")).toInt());
}

QWidget *DisplayProperties::settingsPage()
{
    auto *page = new QWidget;
    page->setFixedSize(378, 354);
    QComboBox *quality = nullptr;
    QLabel *name = nullptr;
    if (themeui()) {
        const xpdialog::Page dialog = xpdialog::build(*themeui(), DIALOG_SETTINGS, page);
        m_resolution = qobject_cast<QSlider *>(dialog.controls.value(SETTINGS_SLIDER));
        m_resolutionText = qobject_cast<QLabel *>(dialog.controls.value(SETTINGS_TEXT));
        quality = qobject_cast<QComboBox *>(dialog.controls.value(SETTINGS_QUALITY));
        if (auto *strip = qobject_cast<QLabel *>(dialog.controls.value(SETTINGS_COLOURS))) {
            const QImage colours = xpfiles::bitmap(*themeui(), BITMAP_COLOURS);
            if (!colours.isNull()) {
                strip->setPixmap(QPixmap::fromImage(colours.scaled(strip->size(), Qt::IgnoreAspectRatio, Qt::FastTransformation)));
                strip->setFrameShape(QFrame::Panel);
                strip->setFrameShadow(QFrame::Sunken);
            }
        }
        name = qobject_cast<QLabel *>(dialog.controls.value(SETTINGS_NAME));
        // One monitor: XP leaves out the arrangement and the list of them.
        for (int id : {SETTINGS_ARRANGE, SETTINGS_LAYOUT, SETTINGS_DISPLAYS})
            if (QWidget *control = dialog.controls.value(id))
                control->hide();
        for (int id : {SETTINGS_TROUBLESHOOT, SETTINGS_ADVANCED})
            if (QWidget *control = dialog.controls.value(id))
                control->setEnabled(false);
        if (QWidget *place = dialog.controls.value(SETTINGS_MONITOR)) {
            m_settingsMonitor = new MonitorPreview(page);
            m_settingsMonitor->move(place->x() + (place->width() - m_settingsMonitor->width()) / 2, place->y());
            place->hide();
        }
    }
    if (!m_resolution)
        m_resolution = new QSlider(Qt::Horizontal, page);
    if (!m_resolutionText)
        m_resolutionText = new QLabel(page);
    if (!m_settingsMonitor)
        m_settingsMonitor = new MonitorPreview(page);
    if (quality)
        quality->addItem(tr("Highest (32 bit)"));
    const QScreen *screen = QGuiApplication::primaryScreen();
    if (name) {
        const QString monitor = (screen->manufacturer() + QLatin1Char(0x20) + screen->model()).trimmed();
        name->setText(monitor.isEmpty() ? tr("Default Monitor") : monitor);
    }
    m_mode = screen->size();
    m_modes = screenModes();
    if (!m_modes.contains(m_mode))
        m_modes.append(m_mode);
    std::sort(m_modes.begin(), m_modes.end(), [](const QSize &a, const QSize &b) { return a.width() != b.width() ? a.width() < b.width() : a.height() < b.height(); });
    m_resolution->setRange(0, int(m_modes.size()) - 1);
    m_resolution->setPageStep(1);
    m_resolution->setValue(int(m_modes.indexOf(m_mode)));
    // Without xrandr there is only the size it has.
    m_resolution->setEnabled(m_modes.size() > 1);
    const auto show = [this](int index) {
        const QSize mode = m_modes.value(index);
        m_resolutionText->setText(tr("%1 by %2 pixels").arg(mode.width()).arg(mode.height()));
    };
    show(m_resolution->value());
    connect(m_resolution, &QSlider::valueChanged, this, [this, show](int index) {
        show(index);
        m_apply->setEnabled(true);
    });
    return page;
}

// As XP: the new size, and back to the old one unless it is kept within 15 seconds.
void DisplayProperties::applyResolution()
{
    const QSize wanted = m_modes.value(m_resolution->value());
    if (!wanted.isValid() || wanted == m_mode)
        return;
    const QSize before = m_mode;
    const auto setSize = [](const QSize &size) {
        QProcess::execute(QStringLiteral("xrandr"), {QStringLiteral("-s"), QStringLiteral("%1x%2").arg(size.width()).arg(size.height())});
    };
    setSize(wanted);
    QMessageBox box(QMessageBox::Question, text(2023, "Display Settings"), tr("Your desktop has been reconfigured. Do you want to keep these settings?"), QMessageBox::Yes | QMessageBox::No, this);
    QTimer::singleShot(15000, &box, [&box] { box.done(QMessageBox::No); });
    if (box.exec() == QMessageBox::Yes) {
        m_mode = wanted;
    } else {
        setSize(before);
        m_resolution->setValue(int(m_modes.indexOf(before)));
    }
}

void DisplayProperties::fillAppearance()
{
    m_filling = true;
    m_style->clear();
    const QStringList paths = stylePaths();
    for (const QString &path : paths)
        m_style->addItem(styleName(path), path);
    m_style->addItem(text(STRING_CLASSIC_STYLE, "Windows Classic style"), QString());
    m_style->setCurrentIndex(qMax(0, m_style->findData(m_choice.style)));
    m_colour->clear();
    m_size->clear();
    if (m_choice.style.isEmpty()) {
        m_colour->addItem(text(STRING_WINDOWS_CLASSIC, "Windows Classic"), QStringLiteral("NormalColor"));
        m_size->addItem(text(STRING_NORMAL, "Normal"), QStringLiteral("NormalSize"));
    } else {
        const StyleInfo info = styleInfo(m_choice.style);
        for (const auto &[id, name] : info.colours)
            m_colour->addItem(name, id);
        for (const auto &[id, name] : info.sizes)
            m_size->addItem(name, id);
    }
    m_colour->setCurrentIndex(qMax(0, m_colour->findData(m_choice.colour)));
    m_size->setCurrentIndex(qMax(0, m_size->findData(m_choice.size)));
    m_filling = false;
}

void DisplayProperties::selectWallpaper()
{
    m_filling = true;
    QListWidgetItem *chosen = m_wallpapers->item(0);
    for (int i = 0; i < m_wallpapers->count(); ++i)
        if (m_wallpapers->item(i)->data(Qt::UserRole).toString() == m_choice.wallpaper)
            chosen = m_wallpapers->item(i);
    if (!m_choice.wallpaper.isEmpty() && chosen == m_wallpapers->item(0)) {
        // A picture from elsewhere joins the list, as Browse does on XP.
        chosen = new QListWidgetItem(wallpaperName(m_choice.wallpaper));
        chosen->setData(Qt::UserRole, m_choice.wallpaper);
        m_wallpapers->addItem(chosen);
    }
    m_wallpapers->setCurrentItem(chosen);
    m_position->setCurrentIndex(m_choice.tile ? 1 : m_choice.wallpaperStyle == 2 ? 2 : 0);
    // XP's colour button: the colour and an arrow
    QPixmap swatch(52, 13);
    swatch.fill(Qt::transparent);
    {
        QPainter painter(&swatch);
        painter.fillRect(QRect(0, 0, 42, 13), m_choice.background);
        painter.setPen(Qt::black);
        painter.drawRect(0, 0, 41, 12);
        painter.setBrush(Qt::black);
        painter.drawPolygon(QPolygon({QPoint(45, 5), QPoint(51, 5), QPoint(48, 8)}));
    }
    m_backgroundButton->setIcon(swatch);
    m_backgroundButton->setIconSize(swatch.size());
    m_filling = false;
}

// A .theme with its style, or a .msstyles: the style and its Shell folder are copied to the
// user's themes. Only their resources are ever read; nothing of them runs.
void DisplayProperties::importTheme()
{
    const auto restore = [this] { changed(); };
    const QString file = QFileDialog::getOpenFileName(this, tr("Open Theme"), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
                                                      tr("Theme Files (*.theme *.msstyles)"));
    if (file.isEmpty()) {
        restore();
        return;
    }
    const QString style = file.endsWith(QLatin1String(".msstyles"), Qt::CaseInsensitive) ? file : styleOfTheme(file);
    const StyleInfo info = style.isEmpty() ? StyleInfo() : styleInfo(style);
    if (info.colours.isEmpty() || !w4w::Theme::load(style, info.colours.first().first, info.sizes.value(0).first)) {
        QMessageBox::warning(this, text(3005, "Error Loading Theme"), text(STRING_NOT_A_THEME, "The file \"%s\" is not a valid theme.").replace(QLatin1String("%s"), QFileInfo(file).fileName()));
        restore();
        return;
    }
    const QFileInfo source(style);
    const QString folder = ownThemes() + QLatin1Char('/') + source.completeBaseName();
    // Importing it again replaces it.
    if (QFileInfo::exists(folder))
        QDir(folder).removeRecursively();
    const QString target = folder + QLatin1Char('/') + source.fileName();
    bool ok = QDir().mkpath(folder) && QFile::copy(style, target);
    if (ok && QFileInfo(source.absolutePath() + QStringLiteral("/Shell")).isDir())
        ok = copyFolder(source.absolutePath() + QStringLiteral("/Shell"), folder + QStringLiteral("/Shell"));
    if (ok && file.endsWith(QLatin1String(".theme"), Qt::CaseInsensitive))
        ok = QFile::copy(file, folder + QLatin1Char('/') + QFileInfo(file).fileName());
    if (!ok) {
        QDir(folder).removeRecursively();
        QMessageBox::warning(this, text(3005, "Error Loading Theme"), tr("The theme could not be copied to %1.").arg(ownThemes()));
        restore();
        return;
    }
    m_choice.style = target;
    m_choice.colour = info.colours.first().first;
    m_choice.size = info.sizes.value(0).first;
    fillThemes();
    fillAppearance();
    selectWallpaper();
    changed();
}

void DisplayProperties::deleteTheme()
{
    const QString style = m_theme->currentData().toString();
    if (!isImported(style))
        return;
    if (QMessageBox::question(this, text(STRING_DELETE_ERROR - 1, "Delete Theme"), tr("Are you sure you want to delete the theme '%1'?").arg(m_theme->currentText())) != QMessageBox::Yes)
        return;
    const QString folder = importedFolder(style);
    // Windows XP takes over, as on XP when the current theme goes.
    const bool applied = ShellTheme::instance()->choice().style == style;
    m_choice.style = ShellTheme::lunaPath();
    m_choice.colour = QStringLiteral("NormalColor");
    m_choice.size = QStringLiteral("NormalSize");
    if (applied)
        ShellTheme::write(m_choice);
    if (!QDir(folder).removeRecursively())
        QMessageBox::warning(this, text(STRING_DELETE_ERROR, "Error Deleting Theme"), tr("%1 could not be removed.").arg(folder));
    fillThemes();
    fillAppearance();
    selectWallpaper();
    changed();
}

void DisplayProperties::changed()
{
    if (m_filling)
        return;
    m_themePreview->setChoice(m_choice);
    m_appearancePreview->setChoice(m_choice);
    m_monitor->setChoice(m_choice);
    m_saverMonitor->setChoice(m_choice);
    m_settingsMonitor->setChoice(m_choice);
    const int imported = m_theme->findData(m_choice.style);
    m_theme->setCurrentIndex(m_choice.style.isEmpty() ? 0 + 1
                             : m_choice.style == ShellTheme::lunaPath() && m_choice.colour == QLatin1String("NormalColor") ? 0
                             : imported >= 0 ? imported : m_theme->findData(QStringLiteral("current")));
    m_deleteTheme->setEnabled(isImported(m_choice.style));
    m_apply->setEnabled(true);
}

// The icon themes, as Control Panel's Icons item lists them (session/linuxlook.c): the
// folders with an index.theme of icons, not only cursors. (folder name, name shown)
static QList<QPair<QString, QString>> iconThemes()
{
    QMap<QString, QString> found;
    QStringList bases = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("icons"), QStandardPaths::LocateDirectory);
    bases << QDir::homePath() + QStringLiteral("/.icons");
    for (const QString &base : std::as_const(bases))
        for (const QString &name : QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString index = base + QLatin1Char('/') + name + QStringLiteral("/index.theme");
            if (!QFileInfo::exists(index) || found.contains(name))
                continue;
            QSettings settings(index, QSettings::IniFormat);
            settings.beginGroup(QStringLiteral("Icon Theme"));
            if (settings.value(QStringLiteral("Directories")).toString().isEmpty() && settings.value(QStringLiteral("Directories")).toStringList().isEmpty())
                continue;
            if (settings.value(QStringLiteral("Hidden")).toBool())
                continue;
            found.insert(name, settings.value(QStringLiteral("Name"), name).toString());
        }
    QList<QPair<QString, QString>> out;
    // XP's own first
    if (found.contains(QStringLiteral("win4wine-xp-icons")))
        out.append({QStringLiteral("win4wine-xp-icons"), found.take(QStringLiteral("win4wine-xp-icons"))});
    for (auto it = found.cbegin(); it != found.cend(); ++it)
        out.append({it.key(), it.value()});
    return out;
}

QWidget *DisplayProperties::linuxPage()
{
    auto *page = new QWidget;
    page->setFixedSize(378, 354);
    const w4w::Look &look = w4w::Session::instance()->look();

    auto *programs = new QGroupBox(tr("Qt and GTK programs"), page);
    programs->setGeometry(11, 11, 356, 136);
    auto *intro = new QLabel(tr("Linux programs follow the theme you choose here: their controls are drawn with its visual style and colors."), programs);
    intro->setWordWrap(true);
    intro->setGeometry(12, 20, 332, 42);
    auto *styleLabel = new QLabel(tr("&Style:"), programs);
    styleLabel->setGeometry(12, 68, 80, 16);
    m_linuxStyle = new QComboBox(programs);
    m_linuxStyle->setGeometry(92, 66, 252, 21);
    styleLabel->setBuddy(m_linuxStyle);
    m_linuxStyle->addItem(tr("Standard (keeps the programs' own layout)"));
    m_linuxStyle->addItem(tr("Accurate (XP's metrics, no pictures on buttons)"));
    m_linuxStyle->setCurrentIndex(look.accurate ? 1 : 0);
    auto *rebuild = new QPushButton(tr("&Rebuild GTK Theme"), programs);
    rebuild->setGeometry(92, 96, 150, 23);

    auto *icons = new QGroupBox(tr("Icons"), page);
    icons->setGeometry(11, 157, 356, 120);
    auto *iconLabel = new QLabel(tr("&Icon theme:"), icons);
    iconLabel->setGeometry(12, 26, 80, 16);
    m_iconTheme = new QComboBox(icons);
    m_iconTheme->setGeometry(92, 24, 252, 21);
    iconLabel->setBuddy(m_iconTheme);
    m_iconTheme->addItem(tr("The desktop's own"), QString());
    for (const auto &[id, name] : iconThemes())
        m_iconTheme->addItem(name, id);
    m_iconTheme->setCurrentIndex(qMax(0, m_iconTheme->findData(look.iconTheme)));
    m_iconSamples = new QWidget(icons);
    m_iconSamples->setGeometry(12, 56, 332, 52);
    auto *row = new QHBoxLayout(m_iconSamples);
    row->setContentsMargins(0, 0, 0, 0);
    showIconSamples();

    connect(m_linuxStyle, &QComboBox::activated, this, [this] { m_apply->setEnabled(true); });
    connect(m_iconTheme, &QComboBox::activated, this, [this] {
        showIconSamples();
        m_apply->setEnabled(true);
    });
    connect(rebuild, &QPushButton::clicked, this, [this] {
        m_rebuild = true;
        applyLinuxLook();
    });
    return page;
}

void DisplayProperties::showIconSamples()
{
    auto *row = static_cast<QHBoxLayout *>(m_iconSamples->layout());
    while (QLayoutItem *item = row->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    // The chosen theme's icons, the shell's own theme put back afterwards
    const QString current = QIcon::themeName();
    const QString wanted = m_iconTheme->currentData().toString();
    if (!wanted.isEmpty())
        QIcon::setThemeName(wanted);
    for (const char *name : {"folder", "user-home", "computer", "text-x-generic", "user-trash", "network-workgroup", "printer"}) {
        auto *label = new QLabel(m_iconSamples);
        label->setPixmap(QIcon::fromTheme(QLatin1String(name)).pixmap(32, 32));
        label->setAlignment(Qt::AlignCenter);
        row->addWidget(label);
    }
    QIcon::setThemeName(current);
}

void DisplayProperties::applyLinuxLook()
{
    // linux-look.conf (session/linuxlook.c has its other editor): other lines stay.
    const QString path = w4w::linuxLookPath();
    QStringList kept;
    int build = 0;
    QFile old(path);
    if (old.open(QIODevice::ReadOnly)) {
        for (const QByteArray &raw : old.readAll().split('\n')) {
            const QString line = QString::fromUtf8(raw);
            if (line.startsWith(QLatin1String("theme_build=")))
                build = line.mid(12).toInt();
            else if (!line.startsWith(QLatin1String("style=")) && !line.startsWith(QLatin1String("icon_theme=")) && !line.trimmed().isEmpty())
                kept << line;
        }
        old.close();
    }
    kept << QStringLiteral("style=%1").arg(m_linuxStyle->currentIndex() == 1 ? QStringLiteral("accurate") : QStringLiteral("standard"));
    if (!m_iconTheme->currentData().toString().isEmpty())
        kept << QStringLiteral("icon_theme=%1").arg(m_iconTheme->currentData().toString());
    if (m_rebuild)
        ++build;
    m_rebuild = false;
    if (build)
        kept << QStringLiteral("theme_build=%1").arg(build);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write((kept.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
        file.commit();
    }
}

void DisplayProperties::apply()
{
    applyScreenSaver();
    applyResolution();
    // A new visual style takes a moment: XP says so meanwhile.
    const LookChoice before = ShellTheme::instance()->choice();
    const bool newStyle = before.style != m_choice.style || before.colour != m_choice.colour || before.size != m_choice.size;
    std::unique_ptr<QWidget> wait;
    if (newStyle) {
        wait.reset(sessiondialog::showPleaseWait(text(STRING_PLEASE_WAIT, "Please Wait")));
        // Painted before the new style takes the time it takes
        QElapsedTimer shown;
        shown.start();
        while (shown.elapsed() < PLEASE_WAIT_MS)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    ShellTheme::write(m_choice);
    applyLinuxLook();
    m_apply->setEnabled(false);
}
