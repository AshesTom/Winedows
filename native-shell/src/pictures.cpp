// SPDX-License-Identifier: LGPL-2.1-or-later
#include "pictures.h"

#include "theme.h"
#include "xpfiles.h"

#include <QApplication>
#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
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

} // namespace pictures
