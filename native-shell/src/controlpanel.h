// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's Control Panel: its categories, its applets' names and icons from the user's
// .cpl files. Each applet here works on Linux: applets that have nothing to
// change on this system are left out rather than shown doing nothing.
#pragma once

#include <QColor>
#include <QIcon>
#include <QImage>
#include <QList>
#include <QWidget>
#include <functional>

namespace controlpanel {

enum CategoryId { Appearance, Network, Programs, Sounds, Performance, Hardware, Users, DateTime };

struct Applet
{
    QString id;
    QString name, description;
    QIcon icon;
    int category = -1;   // in categories()
};

struct Category
{
    QString name, description;
    QIcon icon;
};

QList<Category> categories();
QList<Applet> applets();
// Opens the applet by its id.
void open(const QString &id);

} // namespace controlpanel

// XP's category view of the Control Panel, as shell32.dll's UIFILE 4 lays it out and
// the shell style's UIFILE 2 draws it: "Pick a category", then a category's
// "Pick a task..." and "or pick a Control Panel icon", over its colour and watermark.
class ControlPanelCategories : public QWidget
{
    Q_OBJECT
public:
    ControlPanelCategories(std::function<void(int)> openCategory, QWidget *parent);
    // -1 for the list of categories
    void setCategory(int category);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent *event) override;

private:
    struct Entry { QRect rect; QString text, description; QIcon icon; int category = -1; std::function<void()> action; };
    void loadStyle();
    void rebuild();
    int entryAt(QPoint at) const;

    std::function<void(int)> m_openCategory;
    QList<Entry> m_entries;
    int m_category = -1, m_hot = -1;
    int m_directiveY = 0, m_directive2Y = -1;

    // From the shell style's resources
    QColor m_background, m_separatorTop, m_separatorBottom, m_headerLeft, m_headerRight, m_directive, m_link;
    QImage m_listWatermark, m_categoryWatermark, m_bullet;
};
