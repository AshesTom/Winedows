// SPDX-License-Identifier: LGPL-2.1-or-later
// An XP dialog resource as Qt widgets: the controls, their texts and their
// places come from the user's XP file, in the language of that XP.
#pragma once

#include "msstyles.h"

#include <QHash>
#include <QList>
#include <QStringList>
#include <QWidget>

class QDialog;
class QPushButton;
class QStackedWidget;

namespace xpdialog {

struct Page
{
    QString caption;
    QHash<int, QWidget *> controls;   // by the control's ID
};

// Fills PARENT with dialog ID of PE (DLGTEMPLATE or DLGTEMPLATEEX) and sizes it.
Page build(const w4w::PeResources &pe, int id, QWidget *parent);
// The same for a dialog by its name (NPENCODINGDIALOG).
Page build(const w4w::PeResources &pe, const QString &name, QWidget *parent);
// Dialog units in pixels, for MS Shell Dlg at 96 DPI
int unitX(int value);
int unitY(int value);
// Everything that can be used on PAGE is greyed: a page of XP's with nothing behind it here.
void greyControls(const Page &page);

// comctl32's SysAnimate32: an AVI resource (8-bit BI_RGB or BI_RLE8, the only ones its reader takes)
// played on a timer of the file's frame time. ACS_CENTER keeps the control's size and centres the
// frame; without it the control takes the frame's size. ACS_AUTOPLAY plays on open, without end.
class Animation : public QWidget
{
public:
    explicit Animation(quint32 style, QWidget *parent = nullptr);
    // the resource of type "AVI" named ID in PE
    bool open(const w4w::PeResources &pe, int id);
    // Animate_Play: FROM to TO (-1 the last frame), REPEAT times (-1 without end)
    void play(int from = 0, int to = -1, int repeat = -1);
    void stop();
    int frameCount() const { return int(m_frames.size()); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void timerEvent(QTimerEvent *event) override;

private:
    bool decodeTo(int frame);
    QRect frameRect() const;

    quint32 m_style;
    QList<QByteArray> m_frames;
    QList<QRgb> m_palette;
    QSize m_size;
    bool m_rle = false;
    QByteArray m_pixels;            // the frame decoded last, its rows from the top, an index each
    int m_decoded = -1;
    int m_shown = 0, m_current = 0, m_from = 0, m_to = 0, m_repeat = 0, m_rate = 0, m_timer = 0;
};

// XP's tabs: on as many rows as they need, the rows evened out and the selected tab's at the
// bottom, as comctl32's SysTabControl32 with TCS_MULTILINE has them.
class SheetTabs : public QWidget
{
public:
    explicit SheetTabs(QWidget *parent = nullptr);
    void addTab(QWidget *page, const QString &text);
    void setCurrentIndex(int index);
    void setExtraHeight(int extra);
    int currentIndex() const { return m_current; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void arrange();
    QList<QList<int>> rowsFor(int width) const;
    int widthOf(int tab) const;

    QStackedWidget *m_pages;
    QStringList m_texts;
    QList<QRect> m_rects;
    QRect m_pane;
    QSize m_pageSize;
    int m_current = 0, m_hot = -1, m_extraHeight = 0;
};

// A sheet's window as comctl32's template 1006 lays it out: the tabs, and the buttons at the
// lower right, 50 x 14 dialog units each and 4 apart, 4 from the edges.
void arrangeSheet(QDialog *dialog, SheetTabs *tabs, const QList<QPushButton *> &buttons, int extraHeight = 0);

// Run's place: the lower left corner of the work area, above the Start button, frame included.
void placeLowerLeft(QWidget *window);
// winver's: the upper left corner of the work area, frame included.
void placeUpperLeft(QWidget *window);

} // namespace xpdialog
