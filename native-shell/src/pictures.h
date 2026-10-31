// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's pictures in folders: thumbnails made in the background, the Details
// pane's preview, the slide show and the rotation of Windows Picture and Fax
// Viewer, with the buttons of the user's shimgvw.dll.
#pragma once

#include <QHash>
#include <QIcon>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QStringList>
#include <QWidget>

class QTimer;

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

} // namespace pictures
