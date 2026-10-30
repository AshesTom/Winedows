// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's Common Tasks pane, the blue column beside a folder: collapsible groups
// drawn from the shell style's resources (blue_ss.dll and its colour variants).
#pragma once

#include <QColor>
#include <QIcon>
#include <QImage>
#include <QList>
#include <QWidget>
#include <functional>

struct PaneTask
{
    QString text;
    QIcon icon;
    std::function<void()> action;    // null: plain text, as the Details group's
    bool bold = false;
};

struct PaneGroup
{
    QString title;
    bool primary = false;            // the blue header of a folder's main tasks
    QList<PaneTask> tasks;
    QIcon icon = {};                 // 32 pixels, beside the title and above the header (mainsection)
};

class TaskPane : public QWidget
{
    Q_OBJECT
public:
    explicit TaskPane(QWidget *parent = nullptr);
    void setGroups(const QList<PaneGroup> &groups);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Row { QRect rect; int group = -1, task = -1; QString text; };
    void loadStyle();
    void layoutGroups();
    QPair<int, int> hit(QPoint at) const;

    QList<PaneGroup> m_groups;
    QHash<QString, bool> m_collapsed;
    QList<QRect> m_headers, m_bodies;
    QList<Row> m_rows;
    QPair<int, int> m_hot{-1, -1};

    // From the shell style's resources
    QImage m_primaryHeader, m_header;
    QImage m_arrows[8];     // up, up hot, down, down hot; those of the primary group from 4
    QColor m_top, m_bottom, m_primaryBody, m_body, m_link, m_hotLink;
    QString m_styleKey;
};
