// SPDX-License-Identifier: LGPL-2.1-or-later
// XP's Start menu: the two columns of the Start panel, from the visual style's
// StartPanel parts (or the system colours under Windows Classic), and the
// cascades of All Programs and My Recent Documents.
//
// The panel is the only Qt popup: while it is open every mouse event reaches
// it, whatever is under the pointer. Cascades are windows it drives, of the tooltip
// type: Qt closes open popups when any other kind of window is shown.
#pragma once

#include <QIcon>
#include <QImage>
#include <QList>
#include <QTimer>
#include <QWidget>
#include <functional>

struct MenuEntry
{
    QString text;
    QIcon icon;
    std::function<void()> action;                     // null for a folder
    std::function<QList<MenuEntry>()> children;       // a folder's entries, when it opens
    bool separator = false;
};

class Cascade : public QWidget
{
    Q_OBJECT
public:
    Cascade(const QList<MenuEntry> &entries, QPoint corner, bool upward);
    int entryAt(QPoint global) const;
    void setHot(int index);
    int hot() const { return m_hot; }
    const MenuEntry &entry(int index) const { return m_entries[index]; }
    QRect entryGlobalRect(int index) const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QList<MenuEntry> m_entries;
    QList<QRect> m_rects;
    int m_hot = -1;
};

// XP's Run dialog
void showRunDialog();

class StartPanel : public QWidget
{
    Q_OBJECT
public:
    StartPanel(QPoint bottomLeft, std::function<void()> logOff, std::function<void()> turnOff, std::function<void()> controlPanel);
    ~StartPanel() override;

Q_SIGNALS:
    void closed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    struct Item
    {
        QRect rect;
        QString text, subtitle;
        QImage icon;
        bool bold = false, separator = false, enabled = true, arrow = false;
        std::function<void()> action;
        std::function<QList<MenuEntry>()> children;
    };

    void build(QPoint bottomLeft);
    QRect moreArrow() const;
    void addLeft(int &y, const QString &text, const QString &subtitle, const QIcon &icon, std::function<void()> action, bool big);
    void addPlace(int &y, int iconId, const QString &text, bool bold, std::function<void()> action, bool arrow = false);
    void paintPane(QPainter &painter, const char *part, const QRect &rect, const QColor &fallback);
    void track(QPoint global);
    void openChildren(int level);
    void closeCascades(int from);
    void run(const std::function<void()> &action);
    QList<MenuEntry> allPrograms() const;
    QList<MenuEntry> recentDocuments() const;

    QList<Item> m_items;
    QRect m_user, m_left, m_more, m_right, m_logoff, m_logOffButton, m_turnOffButton;
    QImage m_picture, m_logOffIcon, m_turnOffIcon;
    QString m_userName, m_logOffText, m_turnOffText, m_moreText;
    int m_hot = -1;
    enum { Nothing, MoreHot, LogOffHot, TurnOffHot } m_hotPart = Nothing;
    QList<Cascade *> m_cascades;
    QTimer m_openTimer;
    int m_openWanted = -1;     // the panel's item waiting to open its cascade (-2: All Programs)
    std::function<void()> m_logOff, m_turnOff, m_controlPanel;
};
