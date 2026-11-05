// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's pictures in folders: thumbnails made in the background, the Details
// pane's preview, the slide show and the rotation of Windows Picture and Fax
// Viewer, with the buttons of the user's shimgvw.dll.
#pragma once

#include <QHash>
#include <QImageReader>
#include <QTimer>
#include <QIcon>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QStringList>
#include <QWidget>

#include <memory>

class QCloseEvent;
class QPainter;
class QToolButton;

namespace pictures {

// A file Qt can show as a picture (by its name and type, without reading it)
bool isPicture(const QString &path);
// The picture's size from its header; invalid if it is none.
QSize dimensions(const QString &path);
// The picture scaled, as it should stand (EXIF), to fit BOX; read now.
QImage load(const QString &path, QSize box);
// XP's "Picture" folders: My Pictures, or one that holds mostly pictures
bool isPictureFolder(const QString &folder);
// The pictures of a folder in name order
QStringList picturesIn(const QString &folder);

// The buttons of shimgvw.dll's strips: the viewer's toolbar and the slide show's
enum ViewerButton { ZoomIn = 2, ZoomOut = 3, BestFit = 5, ActualSize = 6, SlideShowButton = 8, PreviousImage = 9, NextImage = 10,
                    RotateCounterclockwise = 11, RotateClockwise = 12 };
enum SlideButton { Play = 0, Pause = 1, Previous = 2, Next = 3, Close = 4 };
QIcon viewerIcon(int index);
QIcon slideIcon(int index);
// A string of shimgvw.dll
QString viewerString(int id, const char *fallback);

// Turns the file a quarter and saves it, as the viewer does: a JPEG without loss through
// jpegtran when its size allows, after the viewer's question otherwise. Its messages over PARENT.
bool rotate(const QString &path, bool clockwise, QWidget *parent);

// Thumbnails made in a thread, kept by path and date; ready() when one is made.
class Thumbnails : public QObject
{
    Q_OBJECT
public:
    static Thumbnails *instance();
    // The thumbnail of a picture or a folder (its first four pictures) within SIDE;
    // null while it is being made, or if it has none.
    QImage get(const QString &path, int side);
    // Forget a file's, which has changed.
    void forget(const QString &path);

Q_SIGNALS:
    void ready(const QString &path);

private:
    Thumbnails() = default;
    QString key(const QString &path, int side) const;
    QHash<QString, QImage> m_done;
    QSet<QString> m_pending;
};

// Windows Picture and Fax Viewer's slide show: the pictures on black over the
// whole screen, the next every few seconds, its toolbar in the top right corner
// while the mouse moves.
class SlideShow : public QWidget
{
    Q_OBJECT
public:
    static void start(const QStringList &paths, int first = 0);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    SlideShow(const QStringList &paths, int first);
    void show(int index);
    void setPlaying(bool playing);
    void showToolbar();

    QStringList m_paths;
    int m_index = 0;
    QImage m_image;
    QTimer *m_next, *m_hide;
    QWidget *m_toolbar;
    QList<QWidget *> m_buttons;
    bool m_playing = true;
};

// Windows Picture and Fax Viewer: a picture in a window of its own, the folder's others a key away,
// with shimgvw.dll's toolbar (its TBBUTTON table: the buttons it shows for a picture, its strip 105, its
// commands, which are its tooltips' strings), its keys (accelerator table 50) and its zoom window's ways:
// Zoom In ×1.2 up to 16 times the picture, Zoom Out ×0.833 back to Best Fit, the wheel zooming, a click
// zooming about its point (Shift the other way), Ctrl or the middle button moving the picture.
class Viewer : public QWidget
{
public:
    static void open(const QString &path);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    explicit Viewer(const QString &path);
    void show(int index);
    void command(int id);
    void setScale(double scale);
    void nextFrame();
    void showPage(int page);
    void updatePage();
    void zoomIn();
    void zoomOut();
    void zoomAt(QPoint at, bool in);
    double fitScale() const;
    QRect imageRect() const;
    void updateTitle();
    void toggleAnnotations(bool on);
    void saveAnnotations();
    QPointF inkPoint(QPoint at) const;
    // the TIFF annotation kinds: Freehand follows the mouse, the others are dragged out from corner to corner
    enum InkTool { InkFreehand, InkHighlight, InkLine, InkFrame, InkSolidRectangle, InkText, InkNote };
    void drawInkShape(QPainter &painter, const QPointF &from, const QPointF &to) const;

    QStringList m_paths;
    int m_index = 0;
    QImage m_image;
    std::unique_ptr<QImageReader> m_reader;   // the picture's frames or pages
    int m_page = 0, m_pages = 1;
    bool m_animated = false;
    QTimer m_frames;
    double m_scale = 1;
    bool m_bestFit = true;
    QPointF m_offset;      // the picture's middle from the view's, while it is bigger than the view
    QPoint m_dragFrom;
    bool m_panning = false;
    bool m_clickZoomsOut = false;   // after Zoom Out, a click zooms out (the zoom window's mode 2)
    QWidget *m_toolbar = nullptr;
    QWidget *m_annotationBar = nullptr;
    QToolButton *m_annotateButton = nullptr;
    QHash<int, QWidget *> m_buttons;
    bool m_annotating = false;
    QImage m_ink;
    QPointF m_inkFrom, m_inkTo, m_inkLast;
    int m_inkWidth = 3;
    int m_inkTool = InkFreehand;
    bool m_inkDrawing = false;
    QColor m_inkColour = Qt::black;
    QList<QImage> m_inkUndo;
};

} // namespace pictures
