// SPDX-License-Identifier: LGPL-2.1-or-later
// Windows Media Player's skin engine, rebuilt: a skin's elements (view, subview, button, buttongroup,
// text, slider, video, effects, playlist, menu...) laid out, drawn and driven as wmp.dll does it,
// their scripts (the skin's own JScript, RT_TEXT of wmploc.dll or a .wmz) run by Qt's JavaScript
// engine against the player's objects. A view is a widget; a taskcenter holds another skin's view.
//
// What wmp.dll does that this follows:
//  - jscript: values are worked out once, when the skin loads; wmpprop: values follow what they read,
//    and a slider's is written back when the user moves it; wmpenabled:/wmpdisabled: follow
//    player.controls.isAvailable.
//  - When an element's size changes by d, its children with horizontalAlignment (verticalAlignment)
//    "stretch" grow by d, "right" ("bottom") move by d, "center" by d / 2.
//  - An element without a size takes its image's (a subview its backgroundImage's, a buttongroup
//    its mappingImage's).
//  - Handlers run with their element's properties in scope (onclick="app.titleBarVisible=down"),
//    events of the player's objects with the arguments _WMPOCXEvents names (NewState, Item...).
#pragma once

#include "wmpskin.h"

#include <QHash>
#include <QImage>
#include <QJSEngine>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QVariant>
#include <QWidget>

#include <functional>
#include <memory>

namespace wmp {

// A reference to one of the player's objects (player.controls, a media item...) as a value.
struct ObjectRef
{
    QString key;
};

// The player's objects as the skins' scripts see them: player, mediacenter, app and what they hold.
// Names come lower-case (COM is not case-sensitive); keys are the model's own ("player.controls").
class Model : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    // The global names a skin's scripts have, and the object each is.
    virtual QHash<QString, QString> globals() const = 0;
    // The object whose events a skin's <player>, <settings>, <mediacenter>... handles; empty if none.
    virtual QString eventSource(const QString &tag) const = 0;
    virtual const wmpskin::Properties &properties() const = 0;
    // A property: its value, an ObjectRef, or an invalid QVariant if the object has none of that name.
    virtual QVariant get(const QString &object, const QString &name) = 0;
    virtual bool set(const QString &object, const QString &name, const QVariant &value) = 0;
    virtual bool isMethod(const QString &object, const QString &name) const = 0;
    virtual QVariant call(const QString &object, const QString &name, const QVariantList &arguments) = 0;
    // The arguments' names of an object's event (openstatechange: NewState); empty if it is not one.
    virtual std::optional<QStringList> eventArguments(const QString &object, const QString &event) const = 0;
    // An image of the skin's: res://wmploc/#1970, res://wmploc/IDB_..., or a file of the skin.
    virtual QImage image(const QString &url, const QString &skinFolder) = 0;
    // A string by its res:// address (res://wmploc/RT_STRING/#1270), or the text itself.
    virtual QString string(const QString &url) = 0;
    // A script file by its res:// address or a file of the skin.
    virtual QString script(const QString &url, const QString &skinFolder) = 0;
    // A skin document for a taskcenter: the task now shown, or none.
    virtual std::optional<wmpskin::Node> task(QString *folder) = 0;
    // What a taskcenter shows for a task wmp.dll draws itself (the Skin Chooser), when task() has no skin.
    virtual QWidget *taskWidget(QWidget *parent) { Q_UNUSED(parent); return nullptr; }
    // The skin a plugin element shows for its objectID (VizView, EQView...), or none.
    virtual std::optional<wmpskin::Node> plugin(const QString &objectId) = 0;
    // What a video element shows and an effects element draws.
    virtual QImage videoFrame() const = 0;
    virtual QList<float> spectrum() const = 0;
    virtual QString preference(const QString &name) = 0;
    virtual void setPreference(const QString &name, const QString &value) = 0;
signals:
    // A property changed: bindings follow, <name>_onchange handlers run.
    void changed(const QString &object, const QString &name);
    // An event of an object's: its handlers run with ARGUMENTS.
    void fired(const QString &object, const QString &event, const QVariantList &arguments);
    void taskChanged();
    void videoChanged();
};

class SkinView;

class Bridge : public QObject
{
    Q_OBJECT
public:
    explicit Bridge(SkinView *view) : m_view(view) {}
    Q_INVOKABLE QJSValue get(const QString &key, const QString &name);
    Q_INVOKABLE bool set(const QString &key, const QString &name, const QJSValue &value);
    Q_INVOKABLE bool has(const QString &key, const QString &name);
    Q_INVOKABLE QJSValue call(const QString &key, const QString &name, const QJSValue &arguments);

private:
    SkinView *m_view;
};

struct Element;

// The drawn view of a skin document: the main window's, or a task's inside a taskcenter.
class SkinView : public QWidget
{
    Q_OBJECT
public:
    // FOLDER is where the skin's own files are (empty for wmploc.dll's); SIZE, when valid, the size
    // its view has from the start (the window's last, or its taskcenter's).
    // VIEW_ID picks one of the theme's views (its first otherwise).
    SkinView(Model *model, const wmpskin::Node &document, const QString &folder, QWidget *parent = nullptr, QSize size = QSize(),
             const QString &viewId = QString());
    ~SkinView() override;

    // The root view's size before anything resized it, and its title.
    QSize preferredSize() const;
    QString title() const;
    bool isResizable() const;
    // titleBar (true unless the view says otherwise): the window has the system's frame
    bool hasTitleBar() const;
    QSize minimumViewSize() const;
    // The region the window shows: what is drawn, without the clipped parts.
    QRegion shape();
    // Runs the view's onload; after the window has its size.
    void start();
    void close();
    // Handlers that run at the view's request: closing, minimizing...
    std::function<void(const QString &)> windowRequest;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool event(QEvent *event) override;

private:
    friend class Bridge;
    struct Binding;

    Element *build(const wmpskin::Node &node, Element *parent);
    void loadScripts(Element *element);
    void applyAttributes(Element *element);
    // CODE's value, with the members of the object SCOPE (a key) in scope when there is one
    QJSValue evaluate(const QString &code, const QString &scope = QString(), const QStringList &names = {},
                      const QVariantList &arguments = {});
    void runHandler(const QString &key, const QString &name, const QVariantList &arguments = {});
    QString keyOf(const Element *element) const;
    Element *elementOf(const QString &key) const;
    QJSValue proxy(const QString &key);
    QJSValue toScript(const QVariant &value);
    QVariant fromScript(const QJSValue &value) const;

    // An element's property, with the defaults wmp.dll gives
    QVariant property(const Element *element, const QString &name) const;
    void setProperty(Element *element, const QString &name, const QVariant &value, bool fromBinding = false);
    QJSValue elementMethod(Element *element, const QString &name, const QVariantList &arguments);
    QSize intrinsicSize(const Element *element) const;
    void assign(Element *element, const QString &name, const QVariant &value);
    void bind(Element *element, const QString &name, const QString &expression, bool enabledFlag, bool invert);
    void reevaluate(const QString &object, const QString &name);
    void modelChanged(const QString &object, const QString &name);
    void modelFired(const QString &object, const QString &event, const QVariantList &arguments);

    QRect rectOf(const Element *element) const;
    bool visible(const Element *element) const;
    bool enabled(const Element *element) const;
    QImage imageOf(const Element *element, const QString &name) const;
    void paintElement(QPainter &painter, Element *element, const QPoint &origin, qreal opacity);
    void paintSlider(QPainter &painter, Element *element, const QRect &rect);
    void paintText(QPainter &painter, Element *element, const QRect &rect);
    QFont textFont(const Element *element, bool hover) const;
    void paintButtonGroup(QPainter &painter, Element *element, const QRect &rect);
    void paintEffects(QPainter &painter, Element *element, const QRect &rect);
    void paintPlaylist(QPainter &painter, Element *element, const QRect &rect);
    // the playlist's row under POINT, or -1; its lines are Tahoma 8 as it paints them
    int playlistRow(Element *element, const QPoint &point) const;
    Element *hit(Element *element, const QPoint &point, const QPoint &origin, int *button) const;
    bool interactive(const Element *element) const;
    void setHover(Element *element, int button);
    void fireMouse(Element *element, const QString &name, QMouseEvent *mouse);
    void dragSlider(Element *slider, const QPoint &point);
    void updateTask(Element *element);
    void host(Element *element, std::optional<wmpskin::Node> document, const QString &folder);
    void layoutHosted();
    // editbox and listbox: Qt's line edit and list in the element's place; popup: a list box
    // shown as a menu (show()). Their items and text stay in the element's values.
    void createControl(Element *element);
    bool isPopup(const Element *element) const;
    void syncControl(Element *element);
    QVariant controlMethod(Element *element, const QString &name, const QVariantList &arguments, bool *handled);
    void scheduleShape();

    Model *m_model;
    QString m_folder;
    QJSEngine m_js;
    Bridge m_bridge{this};
    QJSValue m_makeObject, m_makeMethod;
    QHash<QString, QJSValue> m_proxies;
    std::vector<std::unique_ptr<Element>> m_elements;
    Element *m_theme = nullptr;
    Element *m_view = nullptr;
    QHash<QString, Element *> m_ids;           // lower-case id
    QList<Element *> m_handlerNodes;           // <player openstatechange=...>
    QList<std::shared_ptr<Binding>> m_bindings;
    QHash<QString, QList<std::weak_ptr<Binding>>> m_watchers;   // "key\nname"
    QSet<QString> *m_tracking = nullptr;
    QHash<QString, QVariant> m_event;          // the event object of the handler running
    Element *m_hover = nullptr;
    int m_hoverButton = -1;
    Element *m_pressed = nullptr;
    int m_playlistSelected = -1, m_playlistTop = 0;
    int m_pressedButton = -1;
    QTimer m_timer;                            // view.timerInterval
    QTimer m_animation;                        // moveTo, alphaBlendTo, scrolling text
    QTimer m_shapeTimer;
    QRegion m_shape;
    int m_depth = 0;
    bool m_loaded = false;
    bool m_shapePass = false;
    bool m_started = false;
    bool m_closing = false;
    QSize m_lastSize;
};

} // namespace wmp

Q_DECLARE_METATYPE(wmp::ObjectRef)
