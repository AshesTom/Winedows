// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pictures.h"

#include "theme.h"
#ifdef XP_PRINTING
#include <QPrintDialog>
#include <QPrinter>
#endif
#include "xpfiles.h"
#include "xpprogram.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QLabel>
#include <QMouseEvent>
#include <QToolButton>
#include <QUrl>
#include <QWheelEvent>

#include <QApplication>
#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QCloseEvent>
#include <QImageReader>
#include <QSettings>
#include <QImageWriter>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QSaveFile>
#include <QScreen>
#include <QStandardPaths>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>

namespace pictures {

namespace {

// shimgvw.dll's strips at 32 bits with their alpha: the viewer's toolbar, the slide show's
const int VIEWER_STRIP = 105, SLIDE_STRIP = 109;
// The slide show's pace, and how long its toolbar and pointer stay after the mouse stops
const int SLIDE_SECONDS = 5, TOOLBAR_SECONDS = 3;
// Pictures read for a folder's thumbnail, and how many of its files are looked at to call it a picture folder
const int FOLDER_PICTURES = 4, FOLDER_SAMPLE = 200;

const std::optional<w4w::PeResources> &viewer()
{
    static const auto resources = xpfiles::resources(xpfiles::find(QStringLiteral("shimgvw.dll")));
    return resources;
}

QIcon stripIcon(int strip, int index)
{
    if (!viewer())
        return {};
    const QImage image = xpfiles::bitmap(*viewer(), strip);
    const int side = image.height();
    if (side <= 0 || (index + 1) * side > image.width())
        return {};
    return QIcon(QPixmap::fromImage(image.copy(index * side, 0, side, side)));
}

const QSet<QString> &suffixes()
{
    static const QSet<QString> known = [] {
        QSet<QString> set;
        for (const QByteArray &format : QImageReader::supportedImageFormats())
            set.insert(QString::fromLatin1(format).toLower());
        // Names XP's Images filter (shell32's string 9007) gives JPEG and TIFF too
        if (set.contains(QStringLiteral("jpeg")))
            set += {QStringLiteral("jpe"), QStringLiteral("jfif")};
        // Not pictures to a user: Qt's icons, cursors and X bitmaps of programs
        for (const char *other : {"cur", "xbm", "xpm", "pbm", "pgm", "ppm", "svgz", "icns"})
            set.remove(QLatin1String(other));
        return set;
    }();
    return known;
}

// Up to four pictures in a square of SIDE, two by two, for a folder
QImage folderImage(const QString &folder, int side)
{
    const QStringList found = picturesIn(folder).mid(0, FOLDER_PICTURES);
    if (found.isEmpty())
        return {};
    QImage image(side, side, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    const int gap = 2, cell = (side - gap) / 2;
    for (int i = 0; i < found.size(); ++i) {
        const QImage mini = load(found[i], QSize(cell, cell));
        const QRect place((i % 2) * (cell + gap), (i / 2) * (cell + gap), cell, cell);
        painter.drawImage(place.x() + (cell - mini.width()) / 2, place.y() + (cell - mini.height()) / 2, mini);
    }
    return image;
}

} // namespace

bool isPicture(const QString &path)
{
    const QFileInfo info(path);
    return !info.isDir() && suffixes().contains(info.suffix().toLower());
}

QSize dimensions(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QSize size = reader.size();
    if (reader.transformation() & QImageIOHandler::TransformationRotate90)
        size.transpose();
    return size;
}

QImage load(const QString &path, QSize box)
{
    // Not huge files: a thumbnail is made from the whole picture.
    if (!isPicture(path) || QFileInfo(path).size() > (96 << 20))
        return {};
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize stored = reader.size();
    if (!stored.isValid())
        return reader.read().scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const bool turned = reader.transformation() & QImageIOHandler::TransformationRotate90;
    const QSize shown = turned ? stored.transposed() : stored;
    const QSize target = shown.scaled(box, Qt::KeepAspectRatio).boundedTo(shown).expandedTo(QSize(1, 1));
    reader.setScaledSize(turned ? target.transposed() : target);
    return reader.read();
}

QStringList picturesIn(const QString &folder)
{
    QStringList found;
    const QFileInfoList entries = QDir(folder).entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &entry : entries)
        if (suffixes().contains(entry.suffix().toLower()))
            found.append(entry.absoluteFilePath());
    return found;
}

bool isPictureFolder(const QString &folder)
{
    if (folder.isEmpty() || folder.startsWith(QLatin1String("::")))
        return false;
    const QString mine = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (!mine.isEmpty() && mine != QDir::homePath() && (folder == mine || folder.startsWith(mine + QLatin1Char('/'))))
        return true;
    // XP customizes a folder by what it holds: pictures for the most part
    int files = 0, found = 0;
    const QFileInfoList entries = QDir(folder).entryInfoList(QDir::Files);
    for (const QFileInfo &entry : entries) {
        if (++files > FOLDER_SAMPLE)
            break;
        found += suffixes().contains(entry.suffix().toLower());
    }
    return found > 0 && found * 2 >= qMin(files, FOLDER_SAMPLE);
}

QIcon viewerIcon(int index)
{
    return stripIcon(VIEWER_STRIP, index);
}

QIcon slideIcon(int index)
{
    return stripIcon(SLIDE_STRIP, index);
}

QString viewerString(int id, const char *fallback)
{
    const QString text = viewer() ? xpfiles::string(*viewer(), id) : QString();
    return text.isEmpty() ? QString::fromUtf8(fallback) : text;
}

bool rotate(const QString &path, bool clockwise, QWidget *parent)
{
    const QString title = viewerString(1000, "Rotation");
    const QFileInfo info(path);
    if (!info.isWritable() || !QFileInfo(info.absolutePath()).isWritable()) {
        QMessageBox::warning(parent, title, viewerString(1005, "Because this image is read-only, the rotated version cannot be saved using the same file name."));
        return false;
    }
    QImageReader reader(path);
    if (reader.supportsAnimation() && reader.imageCount() > 1) {
        QMessageBox::warning(parent, title, viewerString(1007, "%1 is an animated image.  Animated images cannot be rotated.").replace(QLatin1String("%1"), info.fileName()));
        return false;
    }
    const QByteArray format = reader.format();
    // A JPEG turns without loss when its size is whole blocks.
    if (format == "jpeg") {
        const QString jpegtran = QStandardPaths::findExecutable(QStringLiteral("jpegtran"));
        if (!jpegtran.isEmpty()) {
            const QString temporary = path + QStringLiteral(".rotating");
            const int code = QProcess::execute(jpegtran, {QStringLiteral("-copy"), QStringLiteral("all"), QStringLiteral("-perfect"), QStringLiteral("-rotate"),
                                                          clockwise ? QStringLiteral("90") : QStringLiteral("270"), QStringLiteral("-outfile"), temporary, path});
            if (code == 0 && QFile::remove(path) && QFile::rename(temporary, path))
                return true;
            QFile::remove(temporary);
        }
        if (QMessageBox::question(parent, title, viewerString(1003, "Because of the dimensions of this picture, rotating it might permanently reduce its quality.\n\nDo you want to proceed?"))
            != QMessageBox::Yes)
            return false;
    }
    // The picture as it stands; its EXIF turn, if any, is in the pixels now.
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    QSaveFile file(path);
    if (image.isNull() || !file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(parent, title, viewerString(1004, "You cannot rotate this image."));
        return false;
    }
    QImageWriter writer(&file, format);
    writer.setQuality(95);
    if (!writer.write(image.transformed(QTransform().rotate(clockwise ? 90 : -90))) || !file.commit()) {
        QMessageBox::warning(parent, title, viewerString(1004, "You cannot rotate this image."));
        return false;
    }
    return true;
}

// Thumbnails

Thumbnails *Thumbnails::instance()
{
    static Thumbnails *thumbnails = new Thumbnails;
    return thumbnails;
}

QString Thumbnails::key(const QString &path, int side) const
{
    return QString::number(side) + QLatin1Char(':') + QString::number(QFileInfo(path).lastModified().toMSecsSinceEpoch()) + QLatin1Char(':') + path;
}

QImage Thumbnails::get(const QString &path, int side)
{
    const QString key = this->key(path, side);
    if (const auto found = m_done.constFind(key); found != m_done.constEnd())
        return *found;
    const bool folder = QFileInfo(path).isDir();
    if (m_pending.contains(key) || (!folder && !isPicture(path)))
        return {};
    m_pending.insert(key);
    static QThreadPool *pool = [] {
        auto *made = new QThreadPool(instance());
        made->setMaxThreadCount(qBound(1, QThread::idealThreadCount() / 2, 4));
        return made;
    }();
    QPointer<Thumbnails> self(this);
    pool->start([self, path, side, key, folder] {
        const QImage image = folder ? folderImage(path, side) : load(path, QSize(side, side));
        QMetaObject::invokeMethod(Thumbnails::instance(), [self, path, key, image] {
            if (!self)
                return;
            self->m_pending.remove(key);
            if (self->m_done.size() > 2000)
                self->m_done.clear();
            self->m_done.insert(key, image);
            emit self->ready(path);
        }, Qt::QueuedConnection);
    });
    return {};
}

void Thumbnails::forget(const QString &path)
{
    for (auto it = m_done.begin(); it != m_done.end();)
        it = it.key().endsWith(QLatin1Char(':') + path) ? m_done.erase(it) : std::next(it);
}

// The slide show

void SlideShow::start(const QStringList &paths, int first)
{
    if (paths.isEmpty())
        return;
    auto *show = new SlideShow(paths, qBound(0, first, int(paths.size()) - 1));
    if (QScreen *screen = QGuiApplication::screenAt(QCursor::pos()))
        show->setGeometry(screen->geometry());
    show->showFullScreen();
    show->activateWindow();
}

SlideShow::SlideShow(const QStringList &paths, int first)
    : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint)
    , m_paths(paths)
    , m_next(new QTimer(this))
    , m_hide(new QTimer(this))
    , m_toolbar(new QWidget(this))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setWindowTitle(viewerString(200, "Windows Picture and Fax Viewer"));
    setMouseTracking(true);
    setCursor(Qt::BlankCursor);
    m_next->setInterval(SLIDE_SECONDS * 1000);
    connect(m_next, &QTimer::timeout, this, [this] { show(m_index + 1); });
    m_hide->setSingleShot(true);
    m_hide->setInterval(TOOLBAR_SECONDS * 1000);
    connect(m_hide, &QTimer::timeout, this, [this] {
        if (!m_toolbar->underMouse()) {
            m_toolbar->hide();
            setCursor(Qt::BlankCursor);
        }
    });

    // The toolbar: Start, Pause, Previous, Next, Close, as the viewer's resources say them
    m_toolbar->setAutoFillBackground(true);
    m_toolbar->setMouseTracking(true);
    auto *layout = new QHBoxLayout(m_toolbar);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(0);
    const struct { int icon, tip; const char *fallback; } buttons[] = {
        {Play, 600, "Start Slide Show"}, {Pause, 601, "Pause Slide Show"}, {Previous, 602, "Previous Picture"}, {Next, 603, "Next Picture"}, {Close, 604, "Close the window"}};
    for (const auto &button : buttons) {
        auto *tool = new QToolButton(m_toolbar);
        tool->setAutoRaise(true);
        tool->setIcon(slideIcon(button.icon));
        tool->setIconSize(QSize(16, 16));
        tool->setToolTip(viewerString(button.tip, button.fallback));
        tool->setCheckable(button.icon == Play || button.icon == Pause);
        tool->setCursor(Qt::ArrowCursor);
        layout->addWidget(tool);
        m_buttons.append(tool);
        connect(tool, &QToolButton::clicked, this, [this, which = button.icon] {
            switch (which) {
            case Play: setPlaying(true); break;
            case Pause: setPlaying(false); break;
            case Previous: show(m_index - 1); break;
            case Next: show(m_index + 1); break;
            default: close();
            }
        });
    }
    m_toolbar->adjustSize();
    m_toolbar->hide();
    show(first);
    setPlaying(true);
}

void SlideShow::setPlaying(bool playing)
{
    m_playing = playing;
    static_cast<QToolButton *>(m_buttons[Play])->setChecked(playing);
    static_cast<QToolButton *>(m_buttons[Pause])->setChecked(!playing);
    if (playing)
        m_next->start();
    else
        m_next->stop();
}

void SlideShow::show(int index)
{
    m_index = (index % m_paths.size() + m_paths.size()) % m_paths.size();
    // Pictures larger than the screen are shrunk to it; smaller ones stay as they are.
    const QSize screen = size().isEmpty() ? QSize(1920, 1080) : size();
    m_image = load(m_paths[m_index], screen * devicePixelRatioF());
    if (m_playing)
        m_next->start();
    update();
}

void SlideShow::showToolbar()
{
    m_toolbar->show();
    m_toolbar->raise();
    unsetCursor();
    m_hide->start();
}

void SlideShow::resizeEvent(QResizeEvent *)
{
    m_toolbar->move(width() - m_toolbar->width(), 0);
    show(m_index);
}

void SlideShow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (m_image.isNull())
        return;
    const QSizeF shown = QSizeF(m_image.size()) / devicePixelRatioF();
    const QRectF place(QPointF((width() - shown.width()) / 2, (height() - shown.height()) / 2), shown);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(place, m_image);
}

void SlideShow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        close();
        break;
    case Qt::Key_Right:
    case Qt::Key_Down:
    case Qt::Key_PageDown:
    case Qt::Key_Space:
    case Qt::Key_Return:
        show(m_index + 1);
        break;
    case Qt::Key_Left:
    case Qt::Key_Up:
    case Qt::Key_PageUp:
    case Qt::Key_Backspace:
        show(m_index - 1);
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

void SlideShow::mouseMoveEvent(QMouseEvent *)
{
    showToolbar();
}

void SlideShow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        show(m_index + 1);
    else if (event->button() == Qt::RightButton)
        show(m_index - 1);
}

namespace {

// shimgvw.dll's toolbar for a picture, from its TBBUTTON table (at 0x1700 of the file): each button's
// picture in strip 105 and its command, which is its tooltip's string. 0 a separator. The fax
// annotations and the pages, hidden for a picture, are left out.
const struct { int picture, command; const char *tip; } VIEWER_BUTTONS[] = {
    {0, 417, "Previous Image (Left Arrow)"}, {1, 416, "Next Image (Right Arrow)"}, {0, 0, nullptr},
    {5, 403, "Best Fit (Ctrl+B)"}, {6, 404, "Actual Size (Ctrl+A)"}, {8, 408, "Start Slide Show (F11)"}, {0, 0, nullptr},
    {2, 400, "Zoom In (+)"}, {3, 401, "Zoom Out (-)"}, {0, 0, nullptr},
    {12, 500, "Rotate Clockwise (Ctrl+K)"}, {11, 501, "Rotate Counterclockwise (Ctrl+L)"}, {0, 0, nullptr},
    // a multi-page picture's (TIFF, faxes): Previous Page, the current page, Next Page
    {9, 406, "Previous Page (Page Up)"}, {-2, 421, "Current Page"}, {10, 407, "Next Page (Page Down)"},
    {25, 424, "Delete (Delete)"}, {20, 418, "Print (Ctrl+P)"}, {22, 420, "Copy To (Ctrl+S)"},
    {26, 425, "Closes this program and opens the image for editing (Ctrl+E)"}, {0, 0, nullptr},
    {24, 423, "Help (F1)"},
};
enum {
    VIEW_ZOOM_IN = 400, VIEW_ZOOM_OUT = 401, VIEW_BEST_FIT = 403, VIEW_ACTUAL = 404, VIEW_SLIDES = 408, VIEW_NEXT = 416,
    VIEW_PREVIOUS = 417, VIEW_PRINT = 418, VIEW_COPY_TO = 420, VIEW_HELP = 423, VIEW_DELETE = 424, VIEW_EDIT = 425,
    VIEW_ROTATE_CW = 500, VIEW_ROTATE_CCW = 501, VIEW_PREVIOUS_PAGE = 406, VIEW_NEXT_PAGE = 407, VIEW_PAGE = 421,
    // the arrows (as table 50 calls them): the pictures, or the picture's parts when it is bigger
    VIEW_LEFT = 554, VIEW_RIGHT = 555, VIEW_UP = 556, VIEW_DOWN = 557,
};
// Zoom In multiplies the shown size by 1.2 (0x5cb03fa8), up to 16 times the picture; Zoom Out by 0.833
// (0x5cb03fb0), and once the picture fits both the window and its own size, back to Best Fit.
const double ZOOM_IN = 1.2, ZOOM_OUT = 0.833, ZOOM_MOST = 16;
const int ACCELERATORS = 50, STRING_TITLE = 200, STRING_COPY_TO = 222;

} // namespace

namespace {
// Measured on XP's viewer (shimgvw.dll in Wine): the 22-pixel toolbar 8 above the client's bottom, the
// picture's frame 8 above the toolbar.
const int PICTURE_TOOLBAR_GAP = 8, TOOLBAR_BOTTOM_MARGIN = 8;
QString viewerSettingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/native-explorer.conf");
}
} // namespace

void Viewer::open(const QString &path)
{
    auto *viewer = new Viewer(path);
    viewer->QWidget::show();
    viewer->activateWindow();
}

Viewer::Viewer(const QString &path)
    : QWidget(nullptr, Qt::Window)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAutoFillBackground(true);
    const QFileInfo info(path);
    m_paths = picturesIn(info.absolutePath());
    m_index = int(m_paths.indexOf(info.absoluteFilePath()));
    if (m_index < 0) {
        m_paths = {info.absoluteFilePath()};
        m_index = 0;
    }
    if (viewer())
        if (const QImage icon = xpfiles::icon(*viewer(), viewer()->names(QStringLiteral("14")).value(0), 16); !icon.isNull())
            setWindowIcon(QPixmap::fromImage(icon));

    // the toolbar, under the picture in the middle
    m_toolbar = new QWidget(this);
    auto *row = new QHBoxLayout(m_toolbar);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    for (const auto &button : VIEWER_BUTTONS) {
        if (!button.command) {
            auto *gap = new QWidget(m_toolbar);
            gap->setFixedWidth(8);
            row->addWidget(gap);
            continue;
        }
        if (button.picture < 0) {
            // the page number: a control of its own in the toolbar (style 0xd0)
            auto *page = new QLabel(m_toolbar);
            page->setAlignment(Qt::AlignCenter);
            page->setMinimumWidth(48);
            page->setToolTip(viewerString(button.command, button.tip));
            row->addWidget(page);
            m_buttons.insert(button.command, page);
            continue;
        }
        auto *tool = new QToolButton(m_toolbar);
        tool->setAutoRaise(true);
        tool->setIcon(stripIcon(VIEWER_STRIP, button.picture));
        tool->setIconSize(QSize(16, 16));
        tool->setFixedSize(24, 22);
        tool->setToolTip(viewerString(button.command, button.tip));
        tool->setFocusPolicy(Qt::NoFocus);
        connect(tool, &QToolButton::clicked, this, [this, id = button.command] { command(id); });
        row->addWidget(tool);
        m_buttons.insert(button.command, tool);
    }
    // what this viewer cannot do: XP's help (ImgPrev.chm); printing without Qt's print support
    m_buttons.value(VIEW_HELP)->setEnabled(false);
#ifndef XP_PRINTING
    m_buttons.value(VIEW_PRINT)->setEnabled(false);
#endif
    // the window's class and its zoom window paint COLOR_WINDOW (0x5cb06861, 0x5cb202eb)
    QPalette palette = this->palette();
    palette.setColor(QPalette::Window, ShellTheme::instance()->system(QStringLiteral("window"), Qt::white));
    palette.setColor(QPalette::Base, palette.color(QPalette::Window));
    setPalette(palette);
    setBackgroundRole(QPalette::Window);
    connect(&m_frames, &QTimer::timeout, this, &Viewer::nextFrame);
    m_toolbar->adjustSize();
    const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
    resize(qMin(800, screen.width() * 2 / 3), qMin(600, screen.height() * 2 / 3));
    // As XP's viewer keeps them under Explorer\\ShellImageView (seen in Wine's registry after it closed): its
    // Bounds (a RECT) and Maximize, read before the window is shown.
    const QSettings settings(viewerSettingsPath(), QSettings::IniFormat);
    const QRect bounds = settings.value(QStringLiteral("Viewer/Bounds")).toRect();
    if (bounds.isValid() && screen.intersects(bounds))
        setGeometry(bounds);
    if (settings.value(QStringLiteral("Viewer/Maximize"), true).toBool())
        setWindowState(Qt::WindowMaximized);
    show(m_index);
}

void Viewer::updateTitle()
{
    // "<name> - Windows Picture and Fax Viewer"
    const QString name = m_paths.isEmpty() ? QString() : QFileInfo(m_paths[m_index]).fileName();
    const QString program = viewerString(STRING_TITLE, "Windows Picture and Fax Viewer");
    setWindowTitle(name.isEmpty() ? program : name + QStringLiteral(" - ") + program);
}

void Viewer::show(int index)
{
    if (m_paths.isEmpty())
        return;
    m_index = (index % int(m_paths.size()) + int(m_paths.size())) % int(m_paths.size());
    m_reader = std::make_unique<QImageReader>(m_paths[m_index]);
    m_reader->setAutoTransform(true);
    m_page = 0;
    m_pages = qMax(1, m_reader->imageCount());
    m_animated = m_reader->supportsAnimation() && m_pages > 1;
    m_image = m_reader->read();
    // an animated picture plays (GIF); one of pages keeps the first and gets the page buttons
    m_frames.stop();
    if (m_animated)
        m_frames.start(qMax(20, m_reader->nextImageDelay()));
    const bool paged = !m_animated && m_pages > 1;
    for (int id : {VIEW_PREVIOUS_PAGE, VIEW_PAGE, VIEW_NEXT_PAGE})
        m_buttons.value(id)->setVisible(paged);
    updatePage();
    m_toolbar->adjustSize();
    resizeEvent(nullptr);
    m_bestFit = true;
    m_offset = {};
    m_scale = fitScale();
    const bool several = m_paths.size() > 1;
    m_buttons.value(VIEW_PREVIOUS)->setEnabled(several);
    m_buttons.value(VIEW_NEXT)->setEnabled(several);
    updateTitle();
    update();
}

double Viewer::fitScale() const
{
    // Best Fit: the picture smaller to fit, never bigger than it is
    if (m_image.isNull())
        return 1;
    const QSize room = (size() - QSize(16, m_toolbar->height() + 16)).expandedTo(QSize(1, 1));
    return qMin(1.0, qMin(double(room.width()) / m_image.width(), double(room.height()) / m_image.height()));
}

void Viewer::setScale(double scale)
{
    m_bestFit = false;
    m_scale = qBound(1.0 / 64, scale, ZOOM_MOST);
    update();
}

QRect Viewer::imageRect() const
{
    const QSize shown = (QSizeF(m_image.size()) * m_scale).toSize();
    const QRect area(0, 0, width(), height() - m_toolbar->height() - 4);
    QPoint middle = area.center() + m_offset.toPoint();
    QRect rect(QPoint(), shown);
    rect.moveCenter(middle);
    return rect;
}

void Viewer::command(int id)
{
    if (m_paths.isEmpty())
        return;
    const QString path = m_paths[m_index];
    switch (id) {
    case VIEW_PREVIOUS:
        show(m_index - 1);
        break;
    case VIEW_NEXT:
        show(m_index + 1);
        break;
    case VIEW_BEST_FIT:
        m_bestFit = true;
        m_offset = {};
        m_scale = fitScale();
        update();
        break;
    case VIEW_ACTUAL:
        setScale(1);
        break;
    case VIEW_ZOOM_IN:
        m_clickZoomsOut = false;
        zoomIn();
        break;
    case VIEW_ZOOM_OUT:
        m_clickZoomsOut = true;
        zoomOut();
        break;
    case VIEW_SLIDES:
        SlideShow::start(m_paths, m_index);
        break;
    case VIEW_ROTATE_CW:
    case VIEW_ROTATE_CCW:
        if (rotate(path, id == VIEW_ROTATE_CW, this))
            show(m_index);
        break;
    case VIEW_DELETE:
        if (QMessageBox::question(this, tr("Confirm File Delete"), tr("Are you sure you want to send '%1' to the Recycle Bin?").arg(QFileInfo(path).fileName()))
            == QMessageBox::Yes && QFile::moveToTrash(path)) {
            m_paths.removeAt(m_index);
            if (m_paths.isEmpty())
                close();
            else
                show(m_index);
        }
        break;
    case VIEW_PREVIOUS_PAGE:
        showPage(m_page - 1);
        break;
    case VIEW_NEXT_PAGE:
        showPage(m_page + 1);
        break;
    case VIEW_PRINT: {
#ifdef XP_PRINTING
        // the picture on one page, as big as fits it, in the middle
        QPrinter printer;
        QPrintDialog dialog(&printer, this);
        if (dialog.exec() != QDialog::Accepted || m_image.isNull())
            break;
        QPainter painter(&printer);
        const QRect page = painter.viewport();
        QSize size = m_image.size();
        size.scale(page.size(), Qt::KeepAspectRatio);
        painter.drawImage(QRect(page.x() + (page.width() - size.width()) / 2, page.y() + (page.height() - size.height()) / 2,
                                size.width(), size.height()), m_image);
#endif
        break;
    }
    case VIEW_COPY_TO: {
        const QString target = QFileDialog::getSaveFileName(this, viewerString(STRING_COPY_TO, "Copy To"), path);
        if (!target.isEmpty() && target != path)
            QFile::copy(path, target);
        break;
    }
    case VIEW_EDIT:
        // closes and opens the picture for editing, in the desktop's program for it
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        close();
        break;
    case VIEW_LEFT:
    case VIEW_RIGHT:
    case VIEW_UP:
    case VIEW_DOWN: {
        const QRect rect = imageRect();
        const bool bigger = rect.width() > width() || rect.height() > height() - m_toolbar->height();
        if (!bigger) {
            show(m_index + (id == VIEW_RIGHT || id == VIEW_DOWN ? 1 : -1));
            break;
        }
        const int step = 32;
        m_offset += id == VIEW_LEFT ? QPointF(step, 0) : id == VIEW_RIGHT ? QPointF(-step, 0) : id == VIEW_UP ? QPointF(0, step) : QPointF(0, -step);
        update();
        break;
    }
    default:
        break;
    }
}

void Viewer::nextFrame()
{
    QImage frame = m_reader ? m_reader->read() : QImage();
    if (frame.isNull() && m_reader) {
        // round again
        m_reader = std::make_unique<QImageReader>(m_paths[m_index]);
        frame = m_reader->read();
    }
    if (frame.isNull())
        return;
    m_image = frame;
    m_frames.start(qMax(20, m_reader->nextImageDelay()));
    update();
}

void Viewer::showPage(int page)
{
    if (!m_reader || m_animated || page < 0 || page >= m_pages)
        return;
    m_reader = std::make_unique<QImageReader>(m_paths[m_index]);
    m_reader->setAutoTransform(true);
    if (!m_reader->jumpToImage(page))
        return;
    m_page = page;
    m_image = m_reader->read();
    updatePage();
    command(VIEW_BEST_FIT);
}

void Viewer::updatePage()
{
    if (auto *label = qobject_cast<QLabel *>(m_buttons.value(VIEW_PAGE)))
        label->setText(QStringLiteral("%1 / %2").arg(m_page + 1).arg(m_pages));
    if (QWidget *previous = m_buttons.value(VIEW_PREVIOUS_PAGE))
        previous->setEnabled(m_page > 0);
    if (QWidget *next = m_buttons.value(VIEW_NEXT_PAGE))
        next->setEnabled(m_page + 1 < m_pages);
}

void Viewer::zoomIn()
{
    setScale(qMin(m_scale * ZOOM_IN, ZOOM_MOST));
}

void Viewer::zoomOut()
{
    const QSize shown = (QSizeF(m_image.size()) * m_scale).toSize();
    const QSize room(width(), height() - m_toolbar->height() - 4);
    if (shown.width() <= qMin(room.width(), m_image.width()) && shown.height() <= qMin(room.height(), m_image.height())) {
        command(VIEW_BEST_FIT);
        return;
    }
    setScale(m_scale * ZOOM_OUT);
}

void Viewer::zoomAt(QPoint at, bool in)
{
    // the zoom window's click (0x5cb21e16): the picture's point under the mouse comes to the middle
    const QRect rect = imageRect();
    if (rect.width() > 0 && rect.height() > 0) {
        const QPointF picture((at.x() - rect.x()) / m_scale, (at.y() - rect.y()) / m_scale);
        in ? zoomIn() : zoomOut();
        m_offset = (QPointF(m_image.width() / 2.0, m_image.height() / 2.0) - picture) * m_scale;
        if (m_bestFit)
            m_offset = {};
        update();
    }
}

void Viewer::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    if (m_image.isNull())
        return;
    painter.setRenderHint(QPainter::SmoothPixmapTransform, m_scale < 1);
    // the picture keeps above the toolbar's band
    const int margin = ShellTheme::instance()->scaled(TOOLBAR_BOTTOM_MARGIN);
    painter.setClipRect(QRect(0, 0, width(), height() - m_toolbar->height() - margin - ShellTheme::instance()->scaled(PICTURE_TOOLBAR_GAP)));
    painter.drawImage(imageRect(), m_image);
}

void Viewer::closeEvent(QCloseEvent *event)
{
    // On closing, as XP's viewer writes Bounds and Maximize: the normal rectangle and whether it was maximized.
    QSettings settings(viewerSettingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Viewer/Bounds"), isMaximized() ? normalGeometry() : geometry());
    settings.setValue(QStringLiteral("Viewer/Maximize"), isMaximized());
    QWidget::closeEvent(event);
}

void Viewer::resizeEvent(QResizeEvent *)
{
    m_toolbar->move((width() - m_toolbar->width()) / 2, height() - m_toolbar->height() - ShellTheme::instance()->scaled(TOOLBAR_BOTTOM_MARGIN));
    if (m_bestFit)
        m_scale = fitScale();
}

void Viewer::keyPressEvent(QKeyEvent *event)
{
    // table 50 for a picture, 51 for one of pages (Page Up and Page Down turn its pages)
    static const QList<xpprogram::Accelerator> single = viewer() ? xpprogram::accelerators(*viewer(), QString::number(ACCELERATORS))
                                                                 : QList<xpprogram::Accelerator>(),
                                               paged = viewer() ? xpprogram::accelerators(*viewer(), QString::number(ACCELERATORS + 1))
                                                                : QList<xpprogram::Accelerator>();
    if (const int id = xpprogram::command(!m_animated && m_pages > 1 ? paged : single, event)) {
        command(id);
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

void Viewer::mousePressEvent(QMouseEvent *event)
{
    // Ctrl or the middle button moves the picture; a click zooms, Shift the other way round
    m_dragFrom = event->position().toPoint();
    m_panning = event->button() == Qt::MiddleButton || (event->modifiers() & Qt::ControlModifier);
    if (m_panning || event->button() != Qt::LeftButton || !imageRect().contains(m_dragFrom))
        return;
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    zoomAt(m_dragFrom, shift == m_clickZoomsOut);
}

void Viewer::mouseMoveEvent(QMouseEvent *event)
{
    // a picture bigger than the window follows the mouse
    if (!m_panning || !(event->buttons() & (Qt::LeftButton | Qt::MiddleButton)))
        return;
    const QPoint at = event->position().toPoint();
    m_offset += at - m_dragFrom;
    m_dragFrom = at;
    update();
}

void Viewer::wheelEvent(QWheelEvent *event)
{
    // the zoom window's wheel (0x5cb21d34): up zooms in, down out
    if (event->angleDelta().y() > 0)
        zoomIn();
    else if (event->angleDelta().y() < 0)
        zoomOut();
}

} // namespace pictures
