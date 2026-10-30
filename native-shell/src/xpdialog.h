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
// Everything that can be used on PAGE is greyed: a page of XP's with nothing behind it here.
void greyControls(const Page &page);

// XP's tabs: on as many rows as they need, the rows evened out and the selected tab's at the
// bottom, as comctl32's SysTabControl32 with TCS_MULTILINE has them.
class SheetTabs : public QWidget
{
public:
    explicit SheetTabs(QWidget *parent = nullptr);
    void addTab(QWidget *page, const QString &text);
    void setCurrentIndex(int index);
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
    int m_current = 0, m_hot = -1;
};

// A sheet's window as comctl32's template 1006 lays it out: the tabs, and the buttons at the
// lower right, 50 x 14 dialog units each and 4 apart, 4 from the edges.
void arrangeSheet(QDialog *dialog, SheetTabs *tabs, const QList<QPushButton *> &buttons);

} // namespace xpdialog
