// SPDX-License-Identifier: LGPL-2.1-or-later
#include "displayproperties.h"

#include "frames.h"
#include "session.h"
#include "xpdialog.h"
#include "xpfiles.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
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
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

// themeui.dll's dialogs, their controls and strings
const int DIALOG_THEMES = 1000, DIALOG_APPEARANCE = 1001;
const int THEME_COMBO = 1101, THEME_SAVE = 1107, THEME_DELETE = 1108, THEME_PREVIEW = 1103;
const int APPEARANCE_PREVIEW = 1110, APPEARANCE_STYLE = 1111, APPEARANCE_COLOUR = 1114, APPEARANCE_SIZE = 1116, APPEARANCE_ADVANCED = 1117, APPEARANCE_EFFECTS = 1118;
const int STRING_WINDOWS_CLASSIC = 2016, STRING_WINDOWS_XP = 2017, STRING_CLASSIC_STYLE = 2003, STRING_NORMAL = 2019, STRING_NONE = 2022, STRING_CURRENT = 2004;
const int BITMAP_MONITOR = 126;
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

// Luna, then any other .msstyles the user has put in their themes folder or an extraction has
QStringList stylePaths()
{
    QStringList out;
    const QString luna = ShellTheme::lunaPath();
    if (!luna.isEmpty())
        out << luna;
    const QString own = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/win4wine/themes");
    QDirIterator it(own, {QStringLiteral("*.msstyles")}, QDir::Files, QDirIterator::Subdirectories);
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
    const auto window = [&](const QRect &r, const QString &title, bool active) {
        FrameLook look;
        look.title = title;
        look.active = active;
        look.icon = QImage();
        painter.save();
        painter.setClipRegion(frameShape(r.size(), m_style, false).translated(r.topLeft()));
        drawFrame(painter, r, m_style, look);
        painter.fillRect(r.adjusted(m_style.border, m_style.top, -m_style.border, -m_style.border), face);
        painter.restore();
    };
    const int w = width();
    window(QRect(w / 12, height() / 10, w * 7 / 12, height() / 2), tr("Inactive Window"), false);
    const QRect active(w / 5, height() / 4, w * 7 / 10, height() / 2);
    window(active, tr("Active Window"), true);
    painter.setPen(Qt::black);
    painter.setFont(ShellTheme::instance()->font(QStringLiteral("Tahoma, 8")));
    painter.drawText(active.adjusted(m_style.border + 6, m_style.top + 6, -m_style.border, -m_style.border), Qt::AlignLeft | Qt::AlignTop, tr("Window Text"));
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
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(7, 7, 7, 7);
    m_tabs = new QTabWidget(this);
    m_tabs->addTab(themesPage(), QString());
    m_tabs->setTabText(0, tr("Themes"));
    m_tabs->addTab(desktopPage(), tr("Desktop"));
    m_tabs->addTab(appearancePage(), QString());
    m_tabs->setTabText(2, tr("Appearance"));
    m_tabs->addTab(linuxPage(), tr("Linux Programs"));
    layout->addWidget(m_tabs);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    layout->addWidget(buttons);
    m_apply = buttons->button(QDialogButtonBox::Apply);
    m_apply->setEnabled(false);
    connect(m_apply, &QPushButton::clicked, this, &DisplayProperties::apply);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
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
        if (QWidget *remove = dialog.controls.value(THEME_DELETE))
            remove->setEnabled(false);
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
    m_theme->addItem(text(STRING_WINDOWS_XP, "Windows XP"));
    m_theme->addItem(text(STRING_WINDOWS_CLASSIC, "Windows Classic"));
    m_theme->addItem(text(STRING_CURRENT, "Current"));
    m_theme->setCurrentIndex(m_choice.style == ShellTheme::lunaPath() ? 0 : m_choice.style.isEmpty() ? 1 : 2);
    connect(m_theme, &QComboBox::activated, this, [this](int index) {
        if (index == 0) {
            m_choice.style = ShellTheme::lunaPath();
            m_choice.colour = QStringLiteral("NormalColor");
            m_choice.size = QStringLiteral("NormalSize");
            m_choice.wallpaper = xpfiles::find(QStringLiteral("bliss.jpg"));
            m_choice.wallpaperStyle = 2;
            m_choice.tile = false;
            m_choice.background = QColor(0, 78, 152);
        } else if (index == 1) {
            // XP's Windows Classic theme has no picture.
            m_choice.style.clear();
            m_choice.colour = QStringLiteral("NormalColor");
            m_choice.size = QStringLiteral("NormalSize");
            m_choice.wallpaper.clear();
            m_choice.background = QColor(58, 110, 165);
        } else {
            return;
        }
        fillAppearance();
        selectWallpaper();
        changed();
    });
    return page;
}

QWidget *DisplayProperties::desktopPage()
{
    auto *page = new QWidget;
    page->setFixedSize(378, 354);
    m_monitor = new MonitorPreview(page);
    m_monitor->move((378 - m_monitor->width()) / 2, 10);
    auto *label = new QLabel(tr("&Background:"), page);
    label->setGeometry(11, 185, 200, 16);
    m_wallpapers = new QListWidget(page);
    m_wallpapers->setGeometry(11, 203, 240, 120);
    label->setBuddy(m_wallpapers);
    auto *browse = new QPushButton(tr("&Browse..."), page);
    browse->setGeometry(262, 203, 105, 23);
    auto *position = new QLabel(tr("&Position:"), page);
    position->setGeometry(262, 232, 105, 16);
    m_position = new QComboBox(page);
    m_position->setGeometry(262, 250, 105, 21);
    position->setBuddy(m_position);
    m_position->addItems({tr("Center"), tr("Tile"), tr("Stretch")});
    auto *colour = new QLabel(tr("&Color:"), page);
    colour->setGeometry(262, 277, 105, 16);
    m_backgroundButton = new QPushButton(page);
    m_backgroundButton->setGeometry(262, 295, 60, 23);
    colour->setBuddy(m_backgroundButton);

    auto *none = new QListWidgetItem(text(STRING_NONE, "(None)"));
    m_wallpapers->addItem(none);
    const QImage picture = ShellTheme::instance()->shellIcon(1, 16);
    const QStringList paths = wallpaperPaths();
    for (const QString &path : paths) {
        auto *item = new QListWidgetItem(QIcon(QPixmap::fromImage(picture)), wallpaperName(path));
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

void DisplayProperties::fillAppearance()
{
    m_filling = true;
    m_style->clear();
    const QStringList paths = stylePaths();
    for (const QString &path : paths)
        m_style->addItem(styleInfo(path).name, path);
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
    QPixmap swatch(44, 14);
    swatch.fill(m_choice.background);
    m_backgroundButton->setIcon(swatch);
    m_backgroundButton->setIconSize(swatch.size());
    m_filling = false;
}

void DisplayProperties::changed()
{
    if (m_filling)
        return;
    m_themePreview->setChoice(m_choice);
    m_appearancePreview->setChoice(m_choice);
    m_monitor->setChoice(m_choice);
    m_theme->setCurrentIndex(m_choice.style.isEmpty() ? 1 : m_choice.style == ShellTheme::lunaPath() && m_choice.colour == QLatin1String("NormalColor") ? 0 : 2);
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
    ShellTheme::write(m_choice);
    applyLinuxLook();
    m_apply->setEnabled(false);
}
