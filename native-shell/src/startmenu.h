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
#include <QPointer>
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
    bool enabled = true;                              // greyed, never selected: (Empty)
};

class Cascade : public QWidget
{
    Q_OBJECT
public:
    // FLAT: the classic Start menu's cascades, XP's flat menus (20-pixel rows, a 1-pixel border, words after an
    // icon column and the entries' access keys) rather than the Start panel's All Programs.
    Cascade(const QList<MenuEntry> &entries, QPoint corner, bool upward, bool flat = false);
    int entryAt(QPoint global) const;
    void setHot(int index);
    int hot() const { return m_hot; }
    // The row of the cascade before it that this one opened from
    void setOwnerRow(int row) { m_ownerRow = row; }
    int ownerRow() const { return m_ownerRow; }
    const MenuEntry &entry(int index) const { return m_entries[index]; }
    QRect entryGlobalRect(int index) const;
    // The next entry that can be selected after FROM, STEP rows on (1 or -1), round the end; -1 if none.
    int nextSelectable(int from, int step) const;
    // The entries a typed letter means: those whose words start with it, in order.
    QList<int> entriesFor(QChar letter) const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QList<MenuEntry> m_entries;
    QList<QRect> m_rects;
    int m_hot = -1, m_ownerRow = -1;
    bool m_flat = false;
};

// XP's Run dialog
void showRunDialog();

class StartPanel : public QWidget
{
    Q_OBJECT
public:
    StartPanel(QPoint bottomLeft, std::function<void()> logOff, std::function<void()> turnOff, std::function<void()> controlPanel);
    ~StartPanel() override;
    // Whether the access keys are underlined: opened with the keys, or once a key is pressed
    void setKeyboardCues(bool on);

Q_SIGNALS:
    void closed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool event(QEvent *event) override;

private:
    // What the keyboard moves between, as explorer.exe's DV2ControlHost routes the arrows: the panes,
    // in their order, and in each its items. ITEM is an index of m_items, or one of the parts below.
    enum Pane { LeftPane, MorePane, PlacesPane, BottomPane };
    enum { MoreItem = -2, LogOffItem = -3, TurnOffItem = -4 };
    struct Target
    {
        Pane pane;
        int item;
        QRect rect;
        QString text;     // with its & before the access key, if any
    };
    QList<Target> targets() const;
    int currentTarget(const QList<Target> &all) const;
    void select(const Target &target);
    QString tipOf(const Target &target) const;
    // What XP fills around Log Off's or Turn Off's button selected with the keys
    static QRect keySelection(const QRect &button);
    // The infotip under a target's words, as XP shows it for a selection made with the keys
    void showTip(const Target &target);
    // XP's infotip: the pale gradient and 6-pixel margin around the words (Qt's tooltip has 2).
    void showInfoTip(const QPoint &at, const QString &text);
    void hideInfoTip();
    QPointer<QWidget> m_infoTip;
    void activate(const Target &target);
    void openByKeyboard(int wanted);
    bool cascadeKey(QKeyEvent *event);
    static QChar accessKey(const QString &text);

    struct Item
    {
        QRect rect;
        QString text, subtitle;
        QImage icon;
        QString tip;      // its infotip
        QString programId; // a most-used program's desktop-entry id, for Remove from This List
        QString path;      // a Start Menu folder entry, for dragging it to another place
        bool bold = false, separator = false, enabled = true, arrow = false;
        std::function<void()> action;
        std::function<QList<MenuEntry>()> children;
    };

    void build(QPoint bottomLeft);
    QRect moreArrow() const;
    QRect moreBand() const;
    void paintClassic(QPainter &painter);
    // The classic Start menu: explorer.exe's menu 204 in a flat menu with the edition's banner (TaskbarSettings::classicStartMenu)
    void buildClassicMenu(QPoint bottomLeft);
    void paintClassicMenu(QPainter &painter);
    QList<MenuEntry> classicDocuments() const;
    bool m_classicMenu = false;
    QImage m_banner;
    static void etch(QPainter &painter, const QRect &rect);
    void addLeft(int &y, const QString &text, const QString &subtitle, const QIcon &icon, std::function<void()> action, bool big);
    void addPlace(int &y, int iconId, const QString &text, bool bold, std::function<void()> action, bool arrow = false);
    void paintPane(QPainter &painter, const char *part, const QRect &rect, const QColor &fallback);
    void track(QPoint global);
    void switchCascade();
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
    int m_dragFrom = -1, m_dragTo = -1;
    bool m_dragMoved = false;
    enum { Nothing, MoreHot, LogOffHot, TurnOffHot } m_hotPart = Nothing;
    QList<Cascade *> m_cascades;
    QTimer m_openTimer;
    QTimer m_switchTimer;
    int m_switchLevel = -1;
    int m_openWanted = -1;     // the panel's item waiting to open its cascade (-2: All Programs)
    bool m_keysInCascade = false;
    bool m_cues = false;
    bool m_byKeys = false;     // the selection was made with the keys   // the keyboard works in the last cascade, not the panel
    std::function<void()> m_logOff, m_turnOff, m_controlPanel;
};
