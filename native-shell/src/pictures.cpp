// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pictures.h"

#include "theme.h"
#ifdef XP_PRINTING
#include <QPrintDialog>
#include <QComboBox>
#include <QListWidget>
#include <QPageSize>
#include <QPrinterInfo>
#include <QProgressDialog>
#include <QPushButton>
#include <QWizard>
#include <QWizardPage>
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
#include <QInputDialog>
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
    m_buttons.value(VIEW_HELP)->setEnabled(true);
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
    // XP's annotations: a small toolbar of its own over the picture. Here pen, highlighter, eraser,
    // clear and Save; the ink is kept in a transparent image over the picture.
    m_annotationBar = new QWidget(this);
    auto *annotationRow = new QHBoxLayout(m_annotationBar);
    annotationRow->setContentsMargins(4, 2, 4, 2);
    annotationRow->setSpacing(4);
    // XP's annotation tools for a TIFF, by shimgvw.dll's tooltips 409 to 415; its Select (402) and the
    // annotations' own menu (493, 494) are not there. The colours and widths are not measured.
    const auto addAnnotation = [this, annotationRow](int string, const char *fallback, int tool, const QColor &colour, int width) {
        auto *button = new QToolButton(m_annotationBar);
        const QString tip = viewerString(string, fallback);
        button->setText(QString(tip).remove(QLatin1String("New ")).remove(QLatin1String(" Annotation")));
        button->setToolTip(tip);
        button->setAutoRaise(true);
        annotationRow->addWidget(button);
        connect(button, &QToolButton::clicked, this, [this, tool, colour, width] {
            m_inkTool = tool;
            m_inkColour = colour;
            m_inkWidth = width;
        });
    };
    addAnnotation(409, "New Freehand Annotation", InkFreehand, Qt::black, 3);
    addAnnotation(410, "New Highlight Annotation", InkHighlight, QColor(255, 255, 0, 128), 12);
    addAnnotation(411, "New Straight Line Annotation", InkLine, Qt::black, 3);
    addAnnotation(412, "New Frame Annotation", InkFrame, Qt::black, 3);
    addAnnotation(413, "New Solid Rectangle Annotation", InkSolidRectangle, Qt::black, 3);
    addAnnotation(414, "New Text Annotation", InkText, Qt::black, 3);
    addAnnotation(415, "New Attached Note Annotation", InkNote, Qt::black, 3);
    auto *undo = new QToolButton(m_annotationBar);
    undo->setText(tr("Undo"));
    undo->setAutoRaise(true);
    annotationRow->addWidget(undo);
    connect(undo, &QToolButton::clicked, this, [this] {
        if (m_inkUndo.isEmpty())
            return;
        m_ink = m_inkUndo.takeLast();
        update();
    });
    auto *clear = new QToolButton(m_annotationBar);
    clear->setText(tr("Clear"));
    clear->setAutoRaise(true);
    annotationRow->addWidget(clear);
    connect(clear, &QToolButton::clicked, this, [this] {
        if (m_ink.isNull())
            return;
        m_inkUndo.append(m_ink);
        if (m_inkUndo.size() > 20)
            m_inkUndo.removeFirst();
        m_ink.fill(Qt::transparent);
        update();
    });
    auto *save = new QToolButton(m_annotationBar);
    save->setText(tr("Save"));
    save->setAutoRaise(true);
    annotationRow->addWidget(save);
    connect(save, &QToolButton::clicked, this, &Viewer::saveAnnotations);
    m_annotationBar->adjustSize();
    m_annotationBar->hide();
    m_annotateButton = new QToolButton(m_toolbar);
    m_annotateButton->setText(QStringLiteral("✎"));
    m_annotateButton->setToolTip(tr("Annotate"));
    m_annotateButton->setAutoRaise(true);
    m_annotateButton->setCheckable(true);
    row->addWidget(m_annotateButton);
    connect(m_annotateButton, &QToolButton::toggled, this, &Viewer::toggleAnnotations);
    m_toolbar->adjustSize();
    const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
    resize(qMin(800, screen.width() * 3 / 4), qMin(600, screen.height() * 2 / 3));
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
    m_ink = QImage(m_image.size(), QImage::Format_ARGB32_Premultiplied);
    m_ink.fill(Qt::transparent);
    m_inkUndo.clear();
    // XP annotates TIFF files only: the pencil is there for them, not for a bitmap.
    const QString suffix = QFileInfo(m_paths.value(m_index)).suffix().toLower();
    const bool annotatable = suffix == QLatin1String("tif") || suffix == QLatin1String("tiff");
    if (m_annotateButton) {
        m_annotateButton->setVisible(annotatable);
        if (!annotatable && m_annotateButton->isChecked())
            m_annotateButton->setChecked(false);
    }
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
        // photowiz.dll's Photo Printing Wizard: pick pictures, a printer and a layout, then print.
        if (m_image.isNull() || m_paths.isEmpty())
            break;
        const auto photo = [](int id, const char *fallback) {
            const auto resources = xpfiles::resources(xpfiles::find(QStringLiteral("photowiz.dll")));
            // the shell's string reader numbers the string blocks from 1, so these IDs are 16 above
            // the ones it finds
            const QString text = resources ? xpfiles::string(*resources, id - 16) : QString();
            return text.isEmpty() ? QObject::tr(fallback) : text;
        };
        QWizard wizard(this);
        wizard.setWindowTitle(photo(2016, "Photo Printing Wizard"));
        wizard.setWizardStyle(QWizard::ClassicStyle);
        wizard.setOption(QWizard::NoBackButtonOnStartPage, true);
        wizard.setButtonText(QWizard::FinishButton, QObject::tr("Print"));
        auto *selection = new QWizardPage;
        selection->setTitle(photo(2017, "Picture Selection"));
        auto *selectionLayout = new QVBoxLayout(selection);
        selectionLayout->addWidget(new QLabel(photo(2018, "Pictures with check marks will be printed.  To print all pictures, click Select All."), selection));
        auto *pictures = new QListWidget(selection);
        for (const QString &path : std::as_const(m_paths)) {
            auto *item = new QListWidgetItem(QFileInfo(path).fileName(), pictures);
            item->setData(Qt::UserRole, path);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(path == m_paths.value(m_index) ? Qt::Checked : Qt::Unchecked);
        }
        selectionLayout->addWidget(pictures, 1);
        auto *all = new QPushButton(QObject::tr("Select All"), selection);
        selectionLayout->addWidget(all, 0, Qt::AlignLeft);
        QObject::connect(all, &QPushButton::clicked, pictures, [pictures] {
            bool unchecked = false;
            for (int i = 0; i < pictures->count(); ++i)
                if (pictures->item(i)->checkState() != Qt::Checked)
                    unchecked = true;
            for (int i = 0; i < pictures->count(); ++i)
                pictures->item(i)->setCheckState(unchecked ? Qt::Checked : Qt::Unchecked);
        });
        wizard.addPage(selection);

        auto *options = new QWizardPage;
        options->setTitle(photo(2019, "Printing Options"));
        auto *optionsLayout = new QVBoxLayout(options);
        optionsLayout->addWidget(new QLabel(photo(2020, "Select the printer and paper you want to use."), options));
        auto *printerBox = new QComboBox(options);
        const QList<QPrinterInfo> printers = QPrinterInfo::availablePrinters();
        for (const QPrinterInfo &info : printers)
            printerBox->addItem(info.printerName());
        if (printerBox->count() == 0 && !QPrinterInfo::defaultPrinterName().isEmpty())
            printerBox->addItem(QPrinterInfo::defaultPrinterName());
        auto *paperBox = new QComboBox(options);
        for (const QPageSize::PageSizeId id : {QPageSize::A4, QPageSize::Letter, QPageSize::Legal, QPageSize::A5, QPageSize::B5})
            paperBox->addItem(QPageSize(id).name(), int(id));
        optionsLayout->addWidget(new QLabel(QObject::tr("Printer:"), options));
        optionsLayout->addWidget(printerBox);
        optionsLayout->addWidget(new QLabel(QObject::tr("Paper:"), options));
        optionsLayout->addWidget(paperBox);
        optionsLayout->addStretch(1);
        wizard.addPage(options);

        auto *layoutPage = new QWizardPage;
        layoutPage->setTitle(photo(2021, "Layout Selection"));
        auto *layoutLayout = new QVBoxLayout(layoutPage);
        layoutLayout->addWidget(new QLabel(photo(2022, "Select a layout from the choices below."), layoutPage));
        auto *layoutBox = new QComboBox(layoutPage);
        layoutBox->addItem(photo(2039, "Full page print"), QSize(1, 1));
        layoutBox->addItem(QObject::tr("2 by 2"), QSize(2, 2));
        layoutBox->addItem(QObject::tr("3 by 3"), QSize(3, 3));
        layoutBox->addItem(QObject::tr("4 by 4"), QSize(4, 4));
        layoutBox->addItem(QObject::tr("Wallet"), QSize(4, 5));
        layoutBox->addItem(QObject::tr("Contact sheet"), QSize(6, 7));
        layoutLayout->addWidget(layoutBox);
        auto *preview = new QLabel(layoutPage);
        preview->setAlignment(Qt::AlignCenter);
        preview->setFrameShape(QFrame::Box);
        preview->setMinimumSize(220, 160);
        layoutLayout->addWidget(preview, 1);
        const auto drawPreview = [preview, layoutBox, image = m_image] {
            QPixmap page(260, 190);
            page.fill(Qt::white);
            QPainter painter(&page);
            painter.setPen(QPen(QColor(120, 120, 120), 1));
            painter.drawRect(page.rect().adjusted(6, 6, -7, -7));
            const QSize cells = layoutBox->currentData().toSize();
            const QRectF sheet = page.rect().adjusted(14, 14, -14, -14);
            const qreal gap = 3;
            const QSizeF cell((sheet.width() - gap * (cells.width() - 1)) / cells.width(),
                              (sheet.height() - gap * (cells.height() - 1)) / cells.height());
            for (int row = 0; row < cells.height(); ++row)
                for (int column = 0; column < cells.width(); ++column) {
                    const QRectF box(sheet.x() + column * (cell.width() + gap), sheet.y() + row * (cell.height() + gap),
                                     cell.width(), cell.height());
                    painter.fillRect(box, QColor(242, 242, 242));
                    painter.drawRect(box);
                    if (row == 0 && column == 0 && !image.isNull()) {
                        QSize fit = image.size();
                        fit.scale(box.size().toSize(), Qt::KeepAspectRatio);
                        const QRect target(int(box.center().x() - fit.width() / 2), int(box.center().y() - fit.height() / 2), fit.width(), fit.height());
                        painter.drawImage(target, image);
                    }
                }
            painter.end();
            preview->setPixmap(page);
        };
        QObject::connect(layoutBox, &QComboBox::currentIndexChanged, layoutPage, [drawPreview] { drawPreview(); });
        drawPreview();
        wizard.addPage(layoutPage);

        if (wizard.exec() != QDialog::Accepted)
            break;
        QStringList chosen;
        for (int i = 0; i < pictures->count(); ++i)
            if (pictures->item(i)->checkState() == Qt::Checked)
                chosen.append(pictures->item(i)->data(Qt::UserRole).toString());
        if (chosen.isEmpty())
            break;
        QPrinter printer(QPrinter::HighResolution);
        if (!printerBox->currentText().isEmpty())
            printer.setPrinterName(printerBox->currentText());
        printer.setPageSize(QPageSize(QPageSize::PageSizeId(paperBox->currentData().toInt())));
        const QSize cells = layoutBox->currentData().toSize();
        QProgressDialog progress(photo(2028, "Printing page %1 of %2..."), QObject::tr("Cancel"), 0, chosen.size(), this);
        progress.setWindowTitle(photo(2034, "Please Wait"));
        progress.setWindowModality(Qt::WindowModal);
        QPainter painter;
        if (!painter.begin(&printer)) {
            QMessageBox::warning(this, photo(2016, "Photo Printing Wizard"), photo(3017, "The wizard cannot print your pictures because an error occurred:"));
            break;
        }
        const QRect page = painter.viewport();
        const int margin = qMin(page.width(), page.height()) / 40;
        const int gap = margin / 4;
        const QSize cell((page.width() - 2 * margin - (cells.width() - 1) * gap) / cells.width(),
                         (page.height() - 2 * margin - (cells.height() - 1) * gap) / cells.height());
        int slot = 0;
        int pageNumber = 1;
        for (int i = 0; i < chosen.size(); ++i) {
            if (progress.wasCanceled())
                break;
            progress.setValue(i);
            progress.setLabelText(photo(2028, "Printing page %1 of %2...").arg(pageNumber).arg((chosen.size() + cells.width() * cells.height() - 1)
                                                                                                / (cells.width() * cells.height())));
            QApplication::processEvents();
            QImageReader reader(chosen[i]);
            reader.setAutoTransform(true);
            QImage image = reader.read();
            if (image.isNull())
                continue;
            if (slot == 0 && pageNumber > 1)
                printer.newPage();
            const int column = slot % cells.width(), row = slot / cells.width();
            QSize size = image.size();
            size.scale(cell, Qt::KeepAspectRatio);
            const QRect target(page.x() + margin + column * (cell.width() + gap) + (cell.width() - size.width()) / 2,
                               page.y() + margin + row * (cell.height() + gap) + (cell.height() - size.height()) / 2,
                               size.width(), size.height());
            painter.drawImage(target, image);
            if (++slot >= cells.width() * cells.height()) {
                slot = 0;
                ++pageNumber;
            }
        }
        painter.end();
        progress.setValue(chosen.size());
        QMessageBox::information(this, photo(2016, "Photo Printing Wizard"), photo(3028, "You have successfully printed your pictures."));
#endif
        break;
    }
    case VIEW_HELP: {
        // XP's own ImgPrev.chm (shimgvw.dll names it), through Wine's hh.exe when Wine is there, as the tour
        // runs; otherwise the shortcut summary.
        const QString chm = xpfiles::find(QStringLiteral("imgprev.chm"));
        const QString wine = QStandardPaths::findExecutable(QStringLiteral("wine"));
        if (!chm.isEmpty() && !wine.isEmpty()) {
            QProcess::startDetached(wine, {QStringLiteral("hh.exe"), QStringLiteral("Z:") + QString(chm).replace(QLatin1Char('/'), QLatin1Char('\\'))},
                                    QDir::homePath());
            break;
        }
        QMessageBox::information(this, viewerString(VIEW_HELP, "Help"),
                                 tr("Windows Picture and Fax Viewer\n\n"
                                    "Left/Right: previous/next picture\n"
                                    "Ctrl+B: Best Fit; Ctrl+A: Actual Size\n"
                                    "+/- or the wheel: zoom; Ctrl or middle button: move\n"
                                    "F11: slide show; Ctrl+K / Ctrl+L: rotate\n"
                                    "Ctrl+S: Copy To; Ctrl+P: Print; Delete: Recycle Bin\n"
                                    "Ctrl+E: open for editing"));
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
    if (!m_ink.isNull())
        painter.drawImage(imageRect(), m_ink);
    if (m_inkDrawing && m_inkTool != InkFreehand) {
        painter.save();
        painter.setClipRect(QRect(0, 0, width(), height() - m_toolbar->height() - margin - ShellTheme::instance()->scaled(PICTURE_TOOLBAR_GAP)));
        const QRect rect = imageRect();
        painter.translate(rect.topLeft());
        painter.scale(m_scale, m_scale);
        drawInkShape(painter, m_inkFrom, m_inkTo);
        painter.restore();
    }
}

void Viewer::toggleAnnotations(bool on)
{
    m_annotating = on;
    if (m_annotationBar)
        m_annotationBar->setVisible(on);
    update();
}

QPointF Viewer::inkPoint(QPoint at) const
{
    const QRect rect = imageRect();
    return QPointF((at.x() - rect.x()) / m_scale, (at.y() - rect.y()) / m_scale);
}

void Viewer::drawInkShape(QPainter &painter, const QPointF &from, const QPointF &to) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setPen(QPen(m_inkColour, m_inkWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    const QRectF box = QRectF(from, to).normalized();
    switch (m_inkTool) {
    case InkLine:
        painter.drawLine(from, to);
        break;
    case InkFrame:
        painter.drawRect(box);
        break;
    case InkSolidRectangle:
    case InkHighlight:
        painter.fillRect(box, m_inkColour);
        break;
    case InkText:
    case InkNote:
        // the box the words go in; a note is a yellow sheet
        if (m_inkTool == InkNote)
            painter.fillRect(box, QColor(255, 255, 225));
        painter.setPen(QPen(Qt::gray, 1, Qt::DotLine));
        painter.drawRect(box);
        break;
    default:
        break;
    }
    painter.restore();
}

void Viewer::saveAnnotations()
{
    if (m_image.isNull() || m_ink.isNull())
        return;
    QImage out = m_image.convertToFormat(QImage::Format_ARGB32);
    QPainter painter(&out);
    painter.drawImage(0, 0, m_ink);
    painter.end();
    const QFileInfo info(m_paths.value(m_index));
    const QString base = info.dir().filePath(info.completeBaseName() + tr(" (annotated)") + QLatin1Char('.') + info.suffix());
    QString target = base;
    for (int i = 2; QFileInfo::exists(target); ++i)
        target = info.dir().filePath(info.completeBaseName() + tr(" (annotated %1)").arg(i) + QLatin1Char('.') + info.suffix());
    QImageWriter writer(target);
    if (!writer.write(out))
        QMessageBox::warning(this, tr("Annotations"), writer.errorString());
    else
        m_ink.fill(Qt::transparent);
    update();
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
    if (m_annotating && event->button() == Qt::LeftButton) {
        m_inkUndo.append(m_ink);
        if (m_inkUndo.size() > 20)
            m_inkUndo.removeFirst();
        m_inkFrom = m_inkTo = m_inkLast = inkPoint(event->position().toPoint());
        m_inkDrawing = true;
        if (m_inkTool == InkFreehand) {
            QPainter painter(&m_ink);
            painter.setPen(QPen(m_inkColour, m_inkWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPoint(m_inkFrom);
        }
        update();
        return;
    }
    // Ctrl or the middle button moves the picture; a click zooms, Shift the other way round
    m_dragFrom = event->position().toPoint();
    m_panning = event->button() == Qt::MiddleButton || (event->modifiers() & Qt::ControlModifier);
    if (m_panning || event->button() != Qt::LeftButton || !imageRect().contains(m_dragFrom))
        return;
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    zoomAt(m_dragFrom, shift == m_clickZoomsOut);
}

void Viewer::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_annotating && m_inkDrawing && event->button() == Qt::LeftButton) {
        m_inkDrawing = false;
        const QRectF box = QRectF(m_inkFrom, m_inkTo).normalized();
        if (m_inkTool == InkText || m_inkTool == InkNote) {
            bool ok = false;
            const QString text = QInputDialog::getMultiLineText(this, viewerString(m_inkTool == InkText ? 414 : 415, ""), QString(), QString(), &ok);
            if (ok && !text.isEmpty()) {
                QPainter painter(&m_ink);
                if (m_inkTool == InkNote)
                    painter.fillRect(box, QColor(255, 255, 225));
                painter.setFont(QFont(QStringLiteral("Arial"), 12));
                painter.setPen(m_inkColour);
                painter.drawText(box.adjusted(2, 2, -2, -2), Qt::TextWordWrap, text);
            } else {
                m_ink = m_inkUndo.takeLast();
            }
        } else if (m_inkTool != InkFreehand) {
            QPainter painter(&m_ink);
            drawInkShape(painter, m_inkFrom, m_inkTo);
        }
        update();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void Viewer::mouseMoveEvent(QMouseEvent *event)
{
    if (m_annotating && m_inkDrawing && (event->buttons() & Qt::LeftButton)) {
        const QPointF at = inkPoint(event->position().toPoint());
        if (m_inkTool == InkFreehand) {
            QPainter painter(&m_ink);
            painter.setPen(QPen(m_inkColour, m_inkWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawLine(m_inkLast, at);
            m_inkLast = at;
        } else {
            m_inkTo = at;
        }
        update();
        return;
    }
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
