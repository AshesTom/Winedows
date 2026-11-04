// SPDX-License-Identifier: LGPL-2.1-or-later
#include "wmpengine.h"

#include <QBitmap>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QToolTip>
#include <QWindow>
#include <QtMath>

#include <algorithm>
#include <cstdio>

namespace wmp {

struct Animation
{
    QString name;
    double from = 0, to = 0;
    qint64 start = 0, duration = 0;
};

struct Element
{
    int index = 0;
    QString tag;
    Element *parent = nullptr;
    QList<Element *> children;
    QHash<QString, QVariant> values;     // lower-case names
    QStringList order;                   // the attributes in the file's order, which values are worked out in
    QHash<QString, QString> handlers;    // lower-case event -> code
    QHash<QString, bool> members;        // what its interfaces have, lower-case -> method
    QString source;                      // a handler node's object (<player ...>)
    QList<Animation> animations;
    QList<std::pair<int, QString>> menuItems;  // a menu's; id 0 a separator
    QSet<int> disabledItems, checkedItems;
    QPointer<SkinView> hosted;           // a taskcenter's task, a plugin's view
    QPointer<QWidget> widget;            // a task the player draws itself
    double scroll = 0;                   // scrolling text
    qint64 scrolled = 0;
    bool handlerNode = false;
};

struct SkinView::Binding
{
    Element *element = nullptr;
    QString name;
    QString expression;
    int mode = 0;        // 0 the value, 1 wmpenabled:, 2 wmpdisabled:
    QSet<QString> watched;
};

namespace {

const QSet<QString> &elementTags()
{
    static const QSet<QString> tags{
        QStringLiteral("theme"), QStringLiteral("view"), QStringLiteral("subview"), QStringLiteral("button"),
        QStringLiteral("buttongroup"), QStringLiteral("buttonelement"), QStringLiteral("text"), QStringLiteral("slider"),
        QStringLiteral("progressbar"), QStringLiteral("customslider"), QStringLiteral("video"), QStringLiteral("effects"),
        QStringLiteral("playlist"), QStringLiteral("plugin"), QStringLiteral("menu"), QStringLiteral("automenu"),
        QStringLiteral("taskcenter"), QStringLiteral("equalizersettings"), QStringLiteral("videosettings"),
        QStringLiteral("librarytree"), QStringLiteral("editbox"), QStringLiteral("listbox"), QStringLiteral("popup"),
    };
    return tags;
}

bool debugging()
{
    static const bool on = qEnvironmentVariableIsSet("WMP_DEBUG");
    return on;
}

int toInt(const QVariant &value, int fallback = 0)
{
    if (!value.isValid() || value.isNull())
        return fallback;
    bool ok = false;
    const double number = value.toDouble(&ok);
    return ok ? int(std::lround(number)) : fallback;
}

double toDouble(const QVariant &value, double fallback = 0)
{
    bool ok = false;
    const double number = value.toDouble(&ok);
    return ok ? number : fallback;
}

bool toBool(const QVariant &value, bool fallback)
{
    if (!value.isValid() || value.isNull())
        return fallback;
    if (value.typeId() == QMetaType::QString) {
        const QString text = value.toString().trimmed().toLower();
        if (text == QLatin1String("true") || text == QLatin1String("-1") || text == QLatin1String("1"))
            return true;
        if (text == QLatin1String("false") || text == QLatin1String("0") || text.isEmpty())
            return false;
        return fallback;
    }
    return value.toBool();
}

// A skin's colour: #RRGGBB, a name, or none.
QColor toColor(const QVariant &value)
{
    const QString text = value.toString().trimmed();
    if (text.isEmpty() || text.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0)
        return {};
    QColor color(text);
    if (!color.isValid() && text.size() == 6)
        color = QColor(QLatin1Char('#') + text);
    return color;
}

// What uxtheme and wmp.dll do to a skin's picture: the transparency colour taken out, then the hue
// turned by SHIFT degrees and the saturation multiplied by SATURATION.
QImage prepared(const QImage &source, const QColor &key, double shift, double saturation)
{
    if (source.isNull())
        return source;
    QImage image = source.convertToFormat(QImage::Format_ARGB32);
    const bool tint = shift != 0 || saturation != 1;
    const QRgb keyed = key.isValid() ? (key.rgb() & 0xffffff) : 0x1000000;
    for (int y = 0; y < image.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if ((line[x] & 0xffffff) == keyed) {
                line[x] = 0;
                continue;
            }
            if (!tint)
                continue;
            QColor color = QColor::fromRgba(line[x]);
            float h, s, l, a;
            color.getHslF(&h, &s, &l, &a);
            if (h < 0)
                h = 0;
            h = std::fmod(h + float(shift) / 360.f + 1.f, 1.f);
            s = std::clamp(s * float(saturation), 0.f, 1.f);
            line[x] = QColor::fromHslF(h, s, l, a).rgba();
        }
    }
    return image;
}

// The parts of a buttongroup's mapping image in one colour.
QRegion mappingRegion(const QImage &mapping, const QColor &color)
{
    if (mapping.isNull() || !color.isValid())
        return {};
    // index 1 black: what QBitmap takes as set (without a colour table the conversion guesses)
    QImage mask(mapping.size(), QImage::Format_MonoLSB);
    mask.setColorTable({qRgb(255, 255, 255), qRgb(0, 0, 0)});
    mask.fill(0);
    const QRgb wanted = color.rgb() & 0xffffff;
    const QImage source = mapping.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < source.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        for (int x = 0; x < source.width(); ++x)
            if ((line[x] & 0xffffff) == wanted)
                mask.setPixel(x, y, 1);
    }
    return QRegion(QBitmap::fromImage(mask));
}

void drawImage(QPainter &painter, const QRect &rect, const QImage &image, bool tiled, bool stretched)
{
    if (image.isNull() || rect.isEmpty())
        return;
    if (tiled) {
        for (int y = rect.top(); y < rect.bottom() + 1; y += image.height())
            for (int x = rect.left(); x < rect.right() + 1; x += image.width())
                painter.drawImage(QPoint(x, y), image, QRect(0, 0, qMin(image.width(), rect.right() + 1 - x),
                                                              qMin(image.height(), rect.bottom() + 1 - y)));
    } else if (stretched) {
        painter.drawImage(rect, image);
    } else {
        painter.drawImage(rect.topLeft(), image, QRect(QPoint(), rect.size()));
    }
}

} // namespace

// ---- the scripts' side ----

QJSValue Bridge::get(const QString &key, const QString &name)
{
    const QString lower = name.toLower();
    if (m_view->m_tracking)
        m_view->m_tracking->insert(key + QLatin1Char('\n') + lower);
    if (key == QLatin1String("event"))
        return m_view->toScript(m_view->m_event.value(lower));
    if (Element *element = m_view->elementOf(key)) {
        if (element->members.value(lower) || lower == QLatin1String("moveto") || lower == QLatin1String("alphablendto"))
            return m_view->m_makeMethod.call({key, lower});
        QVariant value = m_view->property(element, lower);
        // A .wms gives every attribute as text; wmp.dll's properties have types, so scripts see
        // left="715" as a number (left + 21) and visible="false" as false.
        if (value.typeId() == QMetaType::QString && lower != QLatin1String("value") && lower != QLatin1String("id")) {
            const QString text = value.toString().trimmed();
            bool number = false;
            const double parsed = text.toDouble(&number);
            if (number && !text.isEmpty())
                value = parsed;
            else if (text.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0)
                value = true;
            else if (text.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0)
                value = false;
        }
        return m_view->toScript(value);
    }
    if (m_view->m_model->isMethod(key, lower))
        return m_view->m_makeMethod.call({key, lower});
    return m_view->toScript(m_view->m_model->get(key, lower));
}

bool Bridge::set(const QString &key, const QString &name, const QJSValue &value)
{
    const QString lower = name.toLower();
    if (Element *element = m_view->elementOf(key)) {
        m_view->assign(element, lower, m_view->fromScript(value));
        return true;
    }
    return m_view->m_model->set(key, lower, m_view->fromScript(value));
}

bool Bridge::has(const QString &key, const QString &name)
{
    const QString lower = name.toLower();
    if (key == QLatin1String("event"))
        return m_view->m_event.contains(lower);
    if (Element *element = m_view->elementOf(key))
        return element->members.contains(lower) || element->values.contains(lower);
    return m_view->m_model->isMethod(key, lower) || m_view->m_model->get(key, lower).isValid();
}

QJSValue Bridge::call(const QString &key, const QString &name, const QJSValue &arguments)
{
    QVariantList list;
    const int length = arguments.property(QStringLiteral("length")).toInt();
    for (int i = 0; i < length; ++i)
        list.append(m_view->fromScript(arguments.property(quint32(i))));
    if (Element *element = m_view->elementOf(key))
        return m_view->elementMethod(element, name, list);
    return m_view->toScript(m_view->m_model->call(key, name, list));
}

// ---- the view ----

SkinView::SkinView(Model *model, const wmpskin::Node &document, const QString &folder, QWidget *parent, QSize initial,
                   const QString &viewId)
    : QWidget(parent), m_model(model), m_folder(folder)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
    m_js.installExtensions(QJSEngine::ConsoleExtension);
    QJSEngine::setObjectOwnership(&m_bridge, QJSEngine::CppOwnership);
    m_js.globalObject().setProperty(QStringLiteral("__host"), m_js.newQObject(&m_bridge));
    const QJSValue maker = m_js.evaluate(QStringLiteral(R"((function(host) {
        var objects = {}, methods = {};
        return [function(key) {
            if (objects[key]) return objects[key];
            return objects[key] = new Proxy({}, {
                get: function(t, name) {
                    if (typeof name !== 'string') return undefined;
                    if (name === '__key') return key;
                    if (name === 'toString' || name === 'valueOf') return function() { return '[object]'; };
                    return host.get(key, name);
                },
                set: function(t, name, value) { host.set(key, name, value); return true; },
                has: function(t, name) { return typeof name === 'string' && host.has(key, name); }
            });
        }, function(key, name) {
            var id = key + '\n' + name;
            if (methods[id]) return methods[id];
            return methods[id] = function() { return host.call(key, name, Array.prototype.slice.call(arguments)); };
        }];
    }))"));
    const QJSValue pair = maker.call({m_js.globalObject().property(QStringLiteral("__host"))});
    m_js.evaluate(QStringLiteral("function __put(object, name, args, value) { var f = object['set' + name]; if (typeof f == 'function') f.apply(null, args.concat([value])); }"));
    m_makeObject = pair.property(0);
    m_makeMethod = pair.property(1);

    Element *root = build(document, nullptr);
    m_theme = root && root->tag == QLatin1String("theme") ? root : nullptr;
    if (m_theme) {
        // its first view, or the one asked for (theme.openView)
        for (Element *child : std::as_const(m_theme->children))
            if (child->tag == QLatin1String("view")
                && (viewId.isEmpty() || child->values.value(QStringLiteral("id")).toString().compare(viewId, Qt::CaseInsensitive) == 0)) {
                m_view = child;
                break;
            }
    } else if (root && root->tag == QLatin1String("view")) {
        m_view = root;
    }

    QJSValue global = m_js.globalObject();
    const QHash<QString, QString> globals = m_model->globals();
    for (auto it = globals.begin(); it != globals.end(); ++it)
        global.setProperty(it.key(), proxy(it.value()));
    // wmp.dll finds the player's objects whatever their case (FullScreenTopLayout.wsz writes mediaCenter)
    global.setProperty(QStringLiteral("mediaCenter"), global.property(QStringLiteral("mediacenter")));
    if (m_view)
        global.setProperty(QStringLiteral("view"), proxy(keyOf(m_view)));
    if (m_theme)
        global.setProperty(QStringLiteral("theme"), proxy(keyOf(m_theme)));
    global.setProperty(QStringLiteral("event"), proxy(QStringLiteral("event")));
    for (auto it = m_ids.begin(); it != m_ids.end(); ++it) {
        const QString id = it.value()->values.value(QStringLiteral("id")).toString();
        global.setProperty(id, proxy(keyOf(it.value())));
    }

    connect(m_model, &Model::changed, this, &SkinView::modelChanged);
    connect(m_model, &Model::fired, this, &SkinView::modelFired);
    connect(m_model, &Model::videoChanged, this, [this] { update(); });
    connect(m_model, &Model::taskChanged, this, [this] {
        for (const auto &element : m_elements)
            if (element->tag == QLatin1String("taskcenter"))
                updateTask(element.get());
    });

    // The scripts, then the values that use them.
    if (m_theme)
        loadScripts(m_theme);
    if (m_view)
        loadScripts(m_view);
    for (const auto &element : m_elements)
        if (element.get() != m_theme && element.get() != m_view && element->values.contains(QStringLiteral("scriptfile")))
            loadScripts(element.get());
    // a view sized by its scripts (height="jscript:GetViewHeight();") has that size from the start
    if (m_view && !initial.isValid())
        for (const QString name : {QStringLiteral("width"), QStringLiteral("height")})
            if (const QString text = m_view->values.value(name).toString(); text.startsWith(QLatin1String("jscript:"), Qt::CaseInsensitive))
                m_view->values.insert(name, fromScript(evaluate(text.mid(8), keyOf(m_view))));
    const QSize size = initial.isValid() ? initial : preferredSize();
    if (m_view) {
        m_view->values.insert(QStringLiteral("width"), size.width());
        m_view->values.insert(QStringLiteral("height"), size.height());
    }
    for (const auto &element : m_elements)
        applyAttributes(element.get());
    m_loaded = true;
    m_lastSize = size;
    resize(size);

    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (m_view)
            runHandler(keyOf(m_view), QStringLiteral("ontimer"));
    });
    m_animation.setInterval(30);
    connect(&m_animation, &QTimer::timeout, this, [this] {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        bool busy = false;
        for (const auto &owned : m_elements) {
            Element *element = owned.get();
            for (int i = 0; i < element->animations.size();) {
                const Animation animation = element->animations[i];
                const double t = animation.duration > 0 ? qMin(1.0, double(now - animation.start) / animation.duration) : 1.0;
                setProperty(element, animation.name, int(std::lround(animation.from + (animation.to - animation.from) * t)));
                if (t >= 1.0) {
                    element->animations.removeAt(i);
                    if (animation.name == QLatin1String("alphablend") || animation.name == QLatin1String("left"))
                        runHandler(keyOf(element), animation.name == QLatin1String("left") ? QStringLiteral("onendmove")
                                                                                            : QStringLiteral("onendalphablend"));
                } else {
                    ++i;
                    busy = true;
                }
            }
            if (element->tag == QLatin1String("text") && toBool(property(element, QStringLiteral("scrolling")), false) && visible(element)) {
                busy = true;
                const int delay = qMax(10, toInt(property(element, QStringLiteral("scrollingdelay")), 50));
                if (now - element->scrolled >= delay) {
                    element->scrolled = now;
                    element->scroll += toInt(property(element, QStringLiteral("scrollingamount")), 1);
                    update(rectOf(element));
                }
            }
        }
        if (!busy)
            m_animation.stop();
    });
    m_shapeTimer.setSingleShot(true);
    m_shapeTimer.setInterval(40);
    connect(&m_shapeTimer, &QTimer::timeout, this, [this] {
        m_shape = QRegion();
        if (isWindow())
            setMask(shape());
    });
}

SkinView::~SkinView()
{
    m_closing = true;
    disconnect(m_model, nullptr, this, nullptr);
}

QSize SkinView::preferredSize() const
{
    if (!m_view)
        return {};
    QSize size(toInt(m_view->values.value(QStringLiteral("width")), 0), toInt(m_view->values.value(QStringLiteral("height")), 0));
    if (size.width() <= 0 || size.height() <= 0) {
        const QSize image = intrinsicSize(m_view);
        size = QSize(size.width() > 0 ? size.width() : image.width(), size.height() > 0 ? size.height() : image.height());
    }
    return size;
}

QString SkinView::title() const
{
    return m_view ? m_view->values.value(QStringLiteral("title")).toString() : QString();
}

bool SkinView::isResizable() const
{
    return m_view && toBool(m_view->values.value(QStringLiteral("resizable")), true);
}

bool SkinView::hasTitleBar() const
{
    return m_view && toBool(m_view->values.value(QStringLiteral("titlebar")), true);
}

QSize SkinView::minimumViewSize() const
{
    if (!m_view)
        return {};
    return QSize(toInt(m_view->values.value(QStringLiteral("minwidth"))), toInt(m_view->values.value(QStringLiteral("minheight"))));
}

Element *SkinView::build(const wmpskin::Node &node, Element *parent)
{
    auto owned = std::make_unique<Element>();
    Element *element = owned.get();
    element->index = int(m_elements.size());
    element->tag = node.tag;
    element->parent = parent;
    m_elements.push_back(std::move(owned));
    const bool isElement = elementTags().contains(node.tag);
    element->handlerNode = !isElement;
    if (!isElement) {
        element->source = node.tag;
        m_handlerNodes.append(element);
    } else {
        for (const QString &interface : wmpskin::interfaces(node.tag)) {
            const QHash<QString, bool> own = m_model->properties().members.value(interface);
            for (auto it = own.begin(); it != own.end(); ++it)
                element->members.insert(it.key(), it.value());
        }
    }
    for (const auto &[name, value] : node.attributes) {
        const QString lower = name.toLower();
        const bool event = (lower.startsWith(QLatin1String("on")) && !element->members.contains(lower))
            || lower.endsWith(QLatin1String("_onchange"));
        if (event || (element->handlerNode && lower != QLatin1String("classid")))
            element->handlers.insert(lower, value.toString());
        else {
            element->values.insert(lower, value);
            element->order.append(lower);
        }
    }
    if (const QString id = element->values.value(QStringLiteral("id")).toString(); !id.isEmpty() && isElement)
        m_ids.insert(id.toLower(), element);
    if (parent)
        parent->children.append(element);
    for (const wmpskin::Node &child : node.children)
        build(child, element);
    return element;
}

void SkinView::loadScripts(Element *element)
{
    const QString files = element->values.value(QStringLiteral("scriptfile")).toString();
    for (const QString &file : files.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QString text = m_model->script(file.trimmed(), m_folder);
        if (text.isEmpty())
            continue;
        // JScript lets a script assign to a COM property with arguments: eq.gainLevels(i) = v
        static const QRegularExpression indexed(QStringLiteral(R"(([\w.]+)\.(\w+)\(([^()=;]*)\)\s*=(?!=)\s*([^;\n]+);)"));
        QString source = text;
        source.replace(indexed, QStringLiteral("__put(\\1, '\\2', [\\3], \\4);"));
        const QJSValue result = m_js.evaluate(source, file);
        if (result.isError() && debugging())
            fprintf(stderr, "wmp: %s: %s (line %d)\n", qPrintable(file), qPrintable(result.toString()),
                    result.property(QStringLiteral("lineNumber")).toInt());
    }
}

// Attribute values once the scripts are there: jscript: worked out, wmpprop: bound, strings found.
void SkinView::applyAttributes(Element *element)
{
    if (element->handlerNode) {
        // <player enableErrorDialogs="false">: properties of the object
        const QString source = m_ids.contains(element->source) ? keyOf(m_ids.value(element->source))
                                                                : m_model->eventSource(element->source);
        element->source = source;
        for (auto it = element->handlers.begin(); it != element->handlers.end();) {
            if (!it.key().endsWith(QLatin1String("_onchange")) && !m_model->eventArguments(source, it.key())
                && !m_model->properties().members.isEmpty() && m_model->get(source, it.key()).isValid()) {
                m_model->set(source, it.key(), it.value());
                it = element->handlers.erase(it);
            } else {
                ++it;
            }
        }
        return;
    }
    // in the file's order: height="view.height - top" takes the top worked out before it
    for (const QString &name : std::as_const(element->order)) {
        const QVariant original = element->values.value(name);
        if (original.typeId() != QMetaType::QString)
            continue;
        const QString text = original.toString();
        if (text.startsWith(QLatin1String("jscript:"), Qt::CaseInsensitive)) {
            const QVariant value = fromScript(evaluate(text.mid(8), keyOf(element)));
            element->values.insert(name, value);
        } else if (text.startsWith(QLatin1String("wmpprop:"), Qt::CaseInsensitive)) {
            element->values.remove(name);
            bind(element, name, text.mid(8), false, false);
        } else if (text.startsWith(QLatin1String("wmpenabled:"), Qt::CaseInsensitive)
                   || text.startsWith(QLatin1String("wmpdisabled:"), Qt::CaseInsensitive)) {
            const bool disabled = text.startsWith(QLatin1String("wmpdisabled:"), Qt::CaseInsensitive);
            element->values.remove(name);
            bind(element, name, text.mid(disabled ? 12 : 11), true, disabled);
        } else if (text.contains(QLatin1String("RT_STRING"), Qt::CaseInsensitive) && text.startsWith(QLatin1String("res://"), Qt::CaseInsensitive)) {
            element->values.insert(name, m_model->string(text));
        }
    }
}

// A script's assignment: a value, or a binding as an attribute would have it
// (seek.visible = "wmpenabled:player.controls.seek"), which replaces the one before.
void SkinView::assign(Element *element, const QString &name, const QVariant &value)
{
    m_bindings.erase(std::remove_if(m_bindings.begin(), m_bindings.end(),
                                    [&](const std::shared_ptr<Binding> &b) { return b->element == element && b->name == name; }),
                     m_bindings.end());
    const QString text = value.typeId() == QMetaType::QString ? value.toString() : QString();
    if (text.startsWith(QLatin1String("wmpprop:"), Qt::CaseInsensitive))
        bind(element, name, text.mid(8), false, false);
    else if (text.startsWith(QLatin1String("wmpenabled:"), Qt::CaseInsensitive))
        bind(element, name, text.mid(11), true, false);
    else if (text.startsWith(QLatin1String("wmpdisabled:"), Qt::CaseInsensitive))
        bind(element, name, text.mid(12), true, true);
    else if (text.startsWith(QLatin1String("jscript:"), Qt::CaseInsensitive))
        setProperty(element, name, fromScript(evaluate(text.mid(8), keyOf(element))));
    else
        setProperty(element, name, value);
}

void SkinView::bind(Element *element, const QString &name, const QString &expression, bool enabledFlag, bool invert)
{
    auto binding = std::make_shared<Binding>();
    binding->element = element;
    binding->name = name;
    binding->mode = enabledFlag ? (invert ? 2 : 1) : 0;
    binding->expression = expression.trimmed();
    if (enabledFlag) {
        // wmpenabled:player.controls.Play is player.controls.isAvailable('Play')
        const int dot = binding->expression.lastIndexOf(QLatin1Char('.'));
        binding->expression = binding->expression.left(dot) + QStringLiteral(".isAvailable('") + binding->expression.mid(dot + 1)
            + QStringLiteral("')");
    }
    m_bindings.append(binding);
    QSet<QString> read;
    QSet<QString> *outer = m_tracking;
    m_tracking = &read;
    QVariant value = fromScript(evaluate(binding->expression, keyOf(element)));
    m_tracking = outer;
    for (const QString &dependency : std::as_const(read))
        if (!binding->watched.contains(dependency)) {
            binding->watched.insert(dependency);
            m_watchers[dependency].append(binding);
        }
    if (binding->mode == 2)
        value = !toBool(value, false);
    setProperty(element, name, value, true);
}

void SkinView::reevaluate(const QString &object, const QString &name)
{
    const auto watchers = m_watchers.value(object + QLatin1Char('\n') + name);
    for (const std::weak_ptr<Binding> &weak : watchers) {
        const std::shared_ptr<Binding> binding = weak.lock();
        if (!binding || m_depth > 16)
            continue;
        QSet<QString> read;
        QSet<QString> *outer = m_tracking;
        m_tracking = &read;
        ++m_depth;
        QVariant value = fromScript(evaluate(binding->expression, keyOf(binding->element)));
        --m_depth;
        m_tracking = outer;
        for (const QString &dependency : std::as_const(read))
            if (!binding->watched.contains(dependency)) {
                binding->watched.insert(dependency);
                m_watchers[dependency].append(binding);
            }
        if (binding->mode == 2)
            value = !toBool(value, false);
        setProperty(binding->element, binding->name, value, true);
    }
}

QJSValue SkinView::evaluate(const QString &code, const QString &scope, const QStringList &names, const QVariantList &arguments)
{
    QJSValue result;
    if (scope.isEmpty()) {
        result = m_js.evaluate(code);
    } else {
        // values and handlers see their element's members, then the globals; eval gives an
        // if's value as wmp.dll's script engine does (width="if(a){1}else{2}")
        QJSValue global = m_js.globalObject();
        global.setProperty(QStringLiteral("__scope"), proxy(scope));
        global.setProperty(QStringLiteral("__code"), code);
        const QString wrapper = QStringLiteral("(function(%1) { with (__scope) { return eval(__code); } })").arg(names.join(QLatin1Char(',')));
        const QJSValue function = m_js.evaluate(wrapper);
        if (function.isError()) {
            result = function;
        } else {
            QJSValueList list;
            for (const QVariant &argument : arguments)
                list.append(toScript(argument));
            result = function.call(list);
        }
    }
    if (result.isError() && debugging())
        fprintf(stderr, "wmp: %s: %s\n", qPrintable(code.left(120)), qPrintable(result.toString()));
    return result.isError() ? QJSValue() : result;
}

void SkinView::runHandler(const QString &key, const QString &name, const QVariantList &arguments)
{
    if (m_closing)
        return;
    QList<std::pair<Element *, QString>> found;
    if (Element *element = elementOf(key); element && element->handlers.contains(name))
        found.append({element, element->handlers.value(name)});
    for (Element *node : std::as_const(m_handlerNodes))
        if (node->source == key && node->handlers.contains(name))
            found.append({node, node->handlers.value(name)});
    if (found.isEmpty())
        return;
    if (qEnvironmentVariableIntValue("WMP_DEBUG") > 2)
        for (const auto &[element, code] : found)
            fprintf(stderr, "wmp: %s.%s: %s\n", qPrintable(element->values.value(QStringLiteral("id")).toString()), qPrintable(name),
                    qPrintable(code.left(80)));
    const QStringList names = m_model->eventArguments(key, name).value_or(QStringList());
    // <player openstatechange="..."> runs with the player in scope
    for (const auto &[element, code] : found)
        evaluate(code, element->handlerNode ? element->source : keyOf(element), names, arguments);
}

QString SkinView::keyOf(const Element *element) const
{
    return QLatin1Char('e') + QString::number(element->index);
}

Element *SkinView::elementOf(const QString &key) const
{
    if (key.size() < 2 || key[0] != QLatin1Char('e') || !key[1].isDigit())
        return nullptr;
    const int index = key.mid(1).toInt();
    return index >= 0 && index < int(m_elements.size()) ? m_elements[size_t(index)].get() : nullptr;
}

QJSValue SkinView::proxy(const QString &key)
{
    auto it = m_proxies.constFind(key);
    if (it != m_proxies.constEnd())
        return *it;
    const QJSValue value = m_makeObject.call({key});
    m_proxies.insert(key, value);
    return value;
}

QJSValue SkinView::toScript(const QVariant &value)
{
    if (!value.isValid())
        return QJSValue(QJSValue::UndefinedValue);
    if (value.metaType() == QMetaType::fromType<ObjectRef>())
        return proxy(value.value<ObjectRef>().key);
    if (value.typeId() == QMetaType::QVariantList) {
        const QVariantList list = value.toList();
        QJSValue array = m_js.newArray(quint32(list.size()));
        for (int i = 0; i < list.size(); ++i)
            array.setProperty(quint32(i), toScript(list[i]));
        return array;
    }
    return m_js.toScriptValue(value);
}

QVariant SkinView::fromScript(const QJSValue &value) const
{
    if (value.isUndefined() || value.isNull())
        return {};
    if (value.isObject() && value.hasProperty(QStringLiteral("__key")))
        return QVariant::fromValue(ObjectRef{value.property(QStringLiteral("__key")).toString()});
    if (value.isBool())
        return value.toBool();
    if (value.isNumber())
        return value.toNumber();
    if (value.isString())
        return value.toString();
    return value.toVariant();
}

// ---- elements' properties ----

QSize SkinView::intrinsicSize(const Element *element) const
{
    const QString &tag = element->tag;
    QImage image;
    if (tag == QLatin1String("subview") || tag == QLatin1String("view"))
        image = imageOf(element, QStringLiteral("backgroundimage"));
    else if (tag == QLatin1String("button"))
        image = imageOf(element, QStringLiteral("image"));
    else if (tag == QLatin1String("buttongroup")) {
        image = imageOf(element, QStringLiteral("mappingimage"));
        if (image.isNull())
            image = imageOf(element, QStringLiteral("image"));
    } else if (tag == QLatin1String("slider") || tag == QLatin1String("progressbar")) {
        image = imageOf(element, QStringLiteral("backgroundimage"));
        if (image.isNull())
            image = imageOf(element, QStringLiteral("foregroundimage"));
    } else if (tag == QLatin1String("customslider")) {
        image = imageOf(element, QStringLiteral("image"));
    } else if (tag == QLatin1String("text")) {
        // its lines (wmploc.dll's strings break some: "Now\nPlaying"), wrapped when it says so
        const QFontMetrics metrics(textFont(element, false));
        const QString value = property(element, QStringLiteral("value")).toString();
        const int width = toInt(element->values.value(QStringLiteral("width")), 0);
        const bool wrap = width > 0 && toBool(property(element, QStringLiteral("wordwrap")), false);
        const QRect bounds = metrics.boundingRect(QRect(0, 0, wrap ? width : 100000, 100000), wrap ? Qt::TextWordWrap : 0, value);
        return QSize(bounds.width(), qMax(metrics.height(), bounds.height()));
    }
    if (tag == QLatin1String("button") && toBool(property(element, QStringLiteral("tiled")), false))
        return {};
    return image.size();
}

QVariant SkinView::property(const Element *element, const QString &name) const
{
    auto it = element->values.constFind(name);
    if (it != element->values.constEnd())
        return *it;
    const QString &tag = element->tag;
    if (name == QLatin1String("width") || name == QLatin1String("height")) {
        const QSize size = intrinsicSize(element);
        return name == QLatin1String("width") ? size.width() : size.height();
    }
    if (name == QLatin1String("left") || name == QLatin1String("top") || name == QLatin1String("zindex"))
        return 0;
    if (name == QLatin1String("visible") || name == QLatin1String("enabled") || name == QLatin1String("tabstop"))
        return true;
    if (name == QLatin1String("alphablend"))
        return 255;
    if (name == QLatin1String("elementtype"))
        return tag;
    if (name == QLatin1String("horizontalalignment"))
        return QStringLiteral("left");
    if (name == QLatin1String("verticalalignment"))
        return QStringLiteral("top");
    if (name == QLatin1String("down") || name == QLatin1String("sticky"))
        return false;
    if (tag == QLatin1String("slider") || tag == QLatin1String("progressbar") || tag == QLatin1String("customslider")) {
        if (name == QLatin1String("min") || name == QLatin1String("value"))
            return 0;
        if (name == QLatin1String("max"))
            return 100;
        if (name == QLatin1String("direction"))
            return QStringLiteral("horizontal");
        if (name == QLatin1String("slide"))
            return tag != QLatin1String("progressbar");
    }
    if (tag == QLatin1String("text")) {
        if (name == QLatin1String("value") || name == QLatin1String("tooltip"))
            return QString();
        if (name == QLatin1String("fontface"))
            return QStringLiteral("Tahoma");
        if (name == QLatin1String("fontsize"))
            return 8;
        if (name == QLatin1String("foregroundcolor"))
            return QStringLiteral("black");
        if (name == QLatin1String("textwidth")) {
                return QFontMetrics(textFont(element, false)).horizontalAdvance(property(element, QStringLiteral("value")).toString());
        }
    }
    if (tag == QLatin1String("buttongroup") && name == QLatin1String("buttoncount")) {
        int count = 0;
        for (const Element *child : element->children)
            count += child->tag == QLatin1String("buttonelement");
        return count;
    }
    if (tag == QLatin1String("theme") && name == QLatin1String("currentviewid"))
        return m_view ? m_view->values.value(QStringLiteral("id")) : QVariant();
    if (tag == QLatin1String("effects")) {
        if (name == QLatin1String("currenteffecttitle"))
            return QStringLiteral("Bars and Waves");
        if (name == QLatin1String("currentpresettitle"))
            return QStringLiteral("Bars");
        if (name == QLatin1String("currentpreset") || name == QLatin1String("currenteffect"))
            return 0;
        if (name == QLatin1String("effectcount") || name == QLatin1String("currenteffectpresetcount"))
            return 1;
    }
    if (element->members.contains(name))
        return QString();
    return {};
}

void SkinView::setProperty(Element *element, const QString &name, const QVariant &value, bool fromBinding)
{
    Q_UNUSED(fromBinding);
    const QVariant old = property(element, name);
    const bool geometry = name == QLatin1String("width") || name == QLatin1String("height");
    // a text's textWidth follows its words and font
    const bool measured = element->tag == QLatin1String("text")
        && (name == QLatin1String("value") || name.startsWith(QLatin1String("font")));
    const QVariant oldTextWidth = measured ? property(element, QStringLiteral("textwidth")) : QVariant();
    const int oldSize = geometry ? toInt(old) : 0;
    if (element->values.value(name) == value && element->values.contains(name))
        return;
    element->values.insert(name, value);
    const QVariant now = property(element, name);
    // wmp.dll aligns children when a size changes after the skin has loaded, not while its values are set
    if (geometry && toInt(now) != oldSize && m_loaded) {
        const int delta = toInt(now) - oldSize;
        const bool horizontal = name == QLatin1String("width");
        for (Element *child : std::as_const(element->children)) {
            if (child->handlerNode)
                continue;
            const QString alignment = property(child, horizontal ? QStringLiteral("horizontalalignment") : QStringLiteral("verticalalignment"))
                                          .toString().toLower();
            const QString position = horizontal ? QStringLiteral("left") : QStringLiteral("top");
            if (alignment == QLatin1String("stretch"))
                setProperty(child, name, toInt(property(child, name)) + delta);
            else if (alignment == QLatin1String("right") || alignment == QLatin1String("bottom"))
                setProperty(child, position, toInt(property(child, position)) + delta);
            else if (alignment == QLatin1String("center"))
                setProperty(child, position, toInt(property(child, position)) + delta / 2);
        }
    }
    if (old == now && !geometry)
        return;
    const QString key = keyOf(element);
    if (measured && property(element, QStringLiteral("textwidth")) != oldTextWidth) {
        reevaluate(key, QStringLiteral("textwidth"));
        runHandler(key, QStringLiteral("textwidth_onchange"));
    }
    reevaluate(key, name);
    if (m_started || name != QLatin1String("value"))
        runHandler(key, name + QLatin1String("_onchange"));
    if (element == m_view && geometry && !m_closing && size() != QSize(toInt(property(m_view, QStringLiteral("width"))), toInt(property(m_view, QStringLiteral("height"))))) {
        const QSize wanted(toInt(property(m_view, QStringLiteral("width"))), toInt(property(m_view, QStringLiteral("height"))));
        // a script sizing its view (Iconic's view.width = bigWidth) sizes the window, fixed or not
        if (isWindow() && m_started) {
            if (minimumSize() == maximumSize())
                setFixedSize(wanted);
            else
                resize(wanted);
        }
    }
    if (qEnvironmentVariableIntValue("WMP_DEBUG") > 2)
        fprintf(stderr, "wmp: set %s.%s = %s\n", qPrintable(element->values.value(QStringLiteral("id")).toString()), qPrintable(name),
                qPrintable(now.toString()));
    if (element->tag == QLatin1String("plugin") && name == QLatin1String("objectid") && m_loaded)
        host(element, m_model->plugin(now.toString()), QString());
    if (name == QLatin1String("timerinterval") && element == m_view)
        m_timer.setInterval(qMax(10, toInt(now)));
    if (element->tag == QLatin1String("text") && name == QLatin1String("scrolling") && toBool(now, false))
        m_animation.start();
    if (geometry || name == QLatin1String("left") || name == QLatin1String("top") || name == QLatin1String("visible"))
        layoutHosted();
    if (element->widget && (element->tag == QLatin1String("editbox") || element->tag == QLatin1String("listbox")))
        syncControl(element);
    scheduleShape();
    update();
}

// ---- edit boxes and list boxes ----

namespace {
QStringList itemsOf(const Element *element)
{
    return element->values.value(QStringLiteral("__items")).toStringList();
}
} // namespace

bool SkinView::isPopup(const Element *element) const
{
    // <popup> is wmp.dll's list box with its popup property set: shown as a menu, not in place
    return element->tag == QLatin1String("popup") || toBool(property(element, QStringLiteral("popup")), false);
}

void SkinView::createControl(Element *element)
{
    if (element->widget || isPopup(element))
        return;
    QWidget *widget = nullptr;
    if (element->tag == QLatin1String("listbox")) {
        auto *list = new QListWidget(this);
        connect(list, &QListWidget::currentRowChanged, this, [this, element](int row) {
            if (row != toInt(property(element, QStringLiteral("selecteditem")), -1))
                setProperty(element, QStringLiteral("selecteditem"), row);
        });
        widget = list;
    } else {
        // editStyle multiline (or word wrap): several lines
        const bool lines = toBool(property(element, QStringLiteral("wordwrap")), false)
            || property(element, QStringLiteral("editstyle")).toString().contains(QLatin1String("multiline"), Qt::CaseInsensitive);
        if (lines) {
            auto *edit = new QPlainTextEdit(this);
            connect(edit, &QPlainTextEdit::textChanged, this, [this, element, edit] {
                if (edit->toPlainText() != property(element, QStringLiteral("value")).toString())
                    setProperty(element, QStringLiteral("value"), edit->toPlainText());
            });
            widget = edit;
        } else {
            auto *edit = new QLineEdit(this);
            connect(edit, &QLineEdit::textEdited, this, [this, element](const QString &text) {
                setProperty(element, QStringLiteral("value"), text);
            });
            connect(edit, &QLineEdit::returnPressed, this, [this, element] { runHandler(keyOf(element), QStringLiteral("onenter")); });
            widget = edit;
        }
    }
    element->widget = widget;
    syncControl(element);
    widget->show();
}

void SkinView::syncControl(Element *element)
{
    QWidget *widget = element->widget;
    if (!widget)
        return;
    // the skin's colours, font and border
    QPalette palette = widget->palette();
    const QColor background = toColor(property(element, QStringLiteral("backgroundcolor")));
    const QColor foreground = toColor(property(element, QStringLiteral("foregroundcolor")));
    if (background.isValid())
        palette.setColor(QPalette::Base, background);
    if (foreground.isValid())
        palette.setColor(QPalette::Text, foreground);
    widget->setPalette(palette);
    QFont font = widget->font();
    if (const QString face = property(element, QStringLiteral("fontface")).toString(); !face.isEmpty())
        font.setFamily(face);
    if (const int size = toInt(property(element, QStringLiteral("fontsize")), 0); size > 0)
        font.setPointSize(size);
    const QString style = property(element, QStringLiteral("fontstyle")).toString().toLower();
    font.setBold(style.contains(QLatin1String("bold")));
    font.setItalic(style.contains(QLatin1String("italic")));
    widget->setFont(font);
    const bool border = toBool(property(element, QStringLiteral("border")), true);
    const bool readOnly = toBool(property(element, QStringLiteral("readonly")), false);
    if (element->tag == QLatin1String("editbox"))
        element->values.insert(QStringLiteral("linecount"), int(property(element, QStringLiteral("value")).toString().count(QLatin1Char('\n'))) + 1);
    if (auto *edit = qobject_cast<QLineEdit *>(widget)) {
        edit->setFrame(border);
        edit->setReadOnly(readOnly);
        if (const int limit = toInt(property(element, QStringLiteral("textlimit")), 0); limit > 0)
            edit->setMaxLength(limit);
        const QString justification = property(element, QStringLiteral("justification")).toString().toLower();
        edit->setAlignment(justification == QLatin1String("right") ? Qt::AlignRight : justification == QLatin1String("center") ? Qt::AlignHCenter : Qt::AlignLeft);
        if (const QString value = property(element, QStringLiteral("value")).toString(); edit->text() != value)
            edit->setText(value);
    } else if (auto *edit = qobject_cast<QPlainTextEdit *>(widget)) {
        edit->setFrameShape(border ? QFrame::StyledPanel : QFrame::NoFrame);
        edit->setReadOnly(readOnly);
        if (const QString value = property(element, QStringLiteral("value")).toString(); edit->toPlainText() != value)
            edit->setPlainText(value);
    } else if (auto *list = qobject_cast<QListWidget *>(widget)) {
        list->setFrameShape(border ? QFrame::StyledPanel : QFrame::NoFrame);
        list->setSelectionMode(toBool(property(element, QStringLiteral("multiselect")), false) ? QAbstractItemView::ExtendedSelection
                                                                                               : QAbstractItemView::SingleSelection);
        list->setSortingEnabled(toBool(property(element, QStringLiteral("sorted")), false));
        QStringList shown;
        for (int i = 0; i < list->count(); ++i)
            shown.append(list->item(i)->text());
        if (shown != itemsOf(element)) {
            const QSignalBlocker block(list);
            list->clear();
            list->addItems(itemsOf(element));
        }
        const int selected = toInt(property(element, QStringLiteral("selecteditem")), -1);
        if (list->currentRow() != selected) {
            const QSignalBlocker block(list);
            list->setCurrentRow(selected);
        }
    }
}

QVariant SkinView::controlMethod(Element *element, const QString &name, const QVariantList &arguments, bool *handled)
{
    *handled = true;
    const auto integer = [&](int i) { return toInt(arguments.value(i), 0); };
    QStringList items = itemsOf(element);
    const auto store = [&] {
        element->values.insert(QStringLiteral("__items"), items);
        setProperty(element, QStringLiteral("itemcount"), int(items.size()));
        if (element->widget)
            syncControl(element);
    };
    // IWMPListBoxCtrl, the list box's and the popup's
    if (name == QLatin1String("appenditem")) {
        items.append(arguments.value(0).toString());
        store();
        return int(items.size()) - 1;
    }
    if (name == QLatin1String("insertitem")) {
        items.insert(qBound(0, integer(0), int(items.size())), arguments.value(1).toString());
        store();
        return {};
    }
    if (name == QLatin1String("replaceitem")) {
        if (integer(0) >= 0 && integer(0) < items.size())
            items[integer(0)] = arguments.value(1).toString();
        store();
        return {};
    }
    if (name == QLatin1String("deleteitem")) {
        if (integer(0) >= 0 && integer(0) < items.size())
            items.removeAt(integer(0));
        store();
        return {};
    }
    if (name == QLatin1String("deleteall")) {
        items.clear();
        store();
        setProperty(element, QStringLiteral("selecteditem"), -1);
        return {};
    }
    if (name == QLatin1String("getitem"))
        return items.value(integer(0));
    if (name == QLatin1String("finditem")) {
        for (int i = qMax(0, integer(0)); i < items.size(); ++i)
            if (items[i].compare(arguments.value(1).toString(), Qt::CaseInsensitive) == 0)
                return i;
        return -1;
    }
    if (name == QLatin1String("getnextselecteditem")) {
        const int start = integer(0);
        if (auto *list = qobject_cast<QListWidget *>(element->widget.data())) {
            for (int i = start + 1; i < list->count(); ++i)
                if (list->item(i)->isSelected())
                    return i;
            return -1;
        }
        const int selected = toInt(property(element, QStringLiteral("selecteditem")), -1);
        return selected > start ? selected : -1;
    }
    if (name == QLatin1String("setselectedstate")) {
        if (auto *list = qobject_cast<QListWidget *>(element->widget.data()); list && integer(0) >= 0 && integer(0) < list->count())
            list->item(integer(0))->setSelected(toBool(arguments.value(1), true));
        else if (toBool(arguments.value(1), true))
            setProperty(element, QStringLiteral("selecteditem"), integer(0));
        return {};
    }
    if (name == QLatin1String("show") && isPopup(element)) {
        // the popup list: a menu of the items at the pointer, the selected one checked
        QMenu menu;
        const int selected = toInt(property(element, QStringLiteral("selecteditem")), -1);
        for (int i = 0; i < items.size(); ++i) {
            QAction *action = menu.addAction(items[i]);
            action->setCheckable(true);
            action->setChecked(i == selected);
            action->setData(i);
        }
        QAction *chosen = menu.exec(QCursor::pos());
        if (debugging())
            fprintf(stderr, "wmp: popup %d items, chose %s\n", int(items.size()), chosen ? qPrintable(chosen->text()) : "nothing");
        if (chosen)
            setProperty(element, QStringLiteral("selecteditem"), chosen->data().toInt());
        runHandler(keyOf(element), QStringLiteral("ondismiss"));
        return {};
    }
    if (name == QLatin1String("dismiss"))
        return {};
    // IWMPEditCtrl
    auto *line = qobject_cast<QLineEdit *>(element->widget.data());
    auto *text = qobject_cast<QPlainTextEdit *>(element->widget.data());
    const QString value = property(element, QStringLiteral("value")).toString();
    const QStringList lines = value.split(QLatin1Char('\n'));
    if (name == QLatin1String("getline"))
        return lines.value(integer(0));
    if (name == QLatin1String("getlineindex")) {
        int index = 0;
        for (int i = 0; i < integer(0) && i < lines.size(); ++i)
            index += int(lines[i].size()) + 1;
        return index;
    }
    if (name == QLatin1String("getlinefromchar"))
        return int(value.left(integer(0)).count(QLatin1Char('\n')));
    if (name == QLatin1String("getselectionstart") || name == QLatin1String("getselectionend")) {
        int start = 0, end = 0;
        if (line) {
            start = line->hasSelectedText() ? line->selectionStart() : line->cursorPosition();
            end = line->hasSelectedText() ? line->selectionEnd() : start;
        } else if (text) {
            start = text->textCursor().selectionStart();
            end = text->textCursor().selectionEnd();
        }
        return name == QLatin1String("getselectionstart") ? start : end;
    }
    if (name == QLatin1String("setselection")) {
        if (line)
            line->setSelection(integer(0), integer(1) - integer(0));
        else if (text) {
            QTextCursor cursor = text->textCursor();
            cursor.setPosition(integer(0));
            cursor.setPosition(integer(1), QTextCursor::KeepAnchor);
            text->setTextCursor(cursor);
        }
        return {};
    }
    if (name == QLatin1String("replaceselection")) {
        if (line)
            line->insert(arguments.value(0).toString());
        else if (text)
            text->insertPlainText(arguments.value(0).toString());
        setProperty(element, QStringLiteral("value"), line ? line->text() : text ? text->toPlainText() : value + arguments.value(0).toString());
        return {};
    }
    *handled = false;
    return {};
}

QJSValue SkinView::elementMethod(Element *element, const QString &name, const QVariantList &arguments)
{
    const QString lower = name.toLower();
    if (element->tag == QLatin1String("editbox") || element->tag == QLatin1String("listbox") || element->tag == QLatin1String("popup")) {
        bool handled = false;
        const QVariant result = controlMethod(element, lower, arguments, &handled);
        if (handled)
            return result.isValid() ? m_js.toScriptValue(result) : QJSValue();
    }
    const auto argument = [&](int i) { return arguments.value(i); };
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (lower == QLatin1String("moveto")) {
        const int duration = toInt(argument(2));
        for (const auto &[property, value] : {std::pair{QStringLiteral("left"), argument(0)}, std::pair{QStringLiteral("top"), argument(1)}})
            if (duration <= 0)
                setProperty(element, property, toInt(value));
            else
                element->animations.append({property, double(toInt(this->property(element, property))), double(toInt(value)), now, duration});
        if (duration > 0)
            m_animation.start();
        else
            runHandler(keyOf(element), QStringLiteral("onendmove"));
        return {};
    }
    if (lower == QLatin1String("alphablendto")) {
        const int duration = toInt(argument(1));
        if (duration <= 0) {
            setProperty(element, QStringLiteral("alphablend"), toInt(argument(0)));
            runHandler(keyOf(element), QStringLiteral("onendalphablend"));
        } else {
            element->animations.append({QStringLiteral("alphablend"), double(toInt(property(element, QStringLiteral("alphablend")))),
                                        double(toInt(argument(0))), now, duration});
            m_animation.start();
        }
        return {};
    }
    if (element->tag == QLatin1String("theme")) {
        if (lower == QLatin1String("loadstring"))
            return m_model->string(argument(0).toString());
        if (lower == QLatin1String("loadpreference"))
            return m_model->preference(argument(0).toString());
        if (lower == QLatin1String("savepreference")) {
            m_model->setPreference(argument(0).toString(), argument(1).toString());
            return {};
        }
        if (lower == QLatin1String("logstring")) {
            if (debugging())
                fprintf(stderr, "wmp: %s\n", qPrintable(argument(0).toString()));
            return {};
        }
        if (lower == QLatin1String("closeview") && windowRequest) {
            windowRequest(QStringLiteral("close"));
            return {};
        }
        if (lower == QLatin1String("openview") || lower == QLatin1String("openviewrelative") || lower == QLatin1String("opendialog")) {
            if (windowRequest)
                windowRequest(lower + QLatin1Char(':') + argument(0).toString());
            return {};
        }
        return {};
    }
    if (element == m_view) {
        if (windowRequest && (lower == QLatin1String("close") || lower == QLatin1String("minimize") || lower == QLatin1String("maximize")
                              || lower == QLatin1String("restore") || lower == QLatin1String("returntomediacenter")))
            windowRequest(lower);
        else if (lower == QLatin1String("size") && windowRequest)
            windowRequest(QStringLiteral("size:") + argument(0).toString());
        return {};
    }
    if (element->tag == QLatin1String("buttongroup")) {
        QList<Element *> buttons;
        for (Element *child : std::as_const(element->children))
            if (child->tag == QLatin1String("buttonelement"))
                buttons.append(child);
        if (lower == QLatin1String("getbutton")) {
            const int i = toInt(argument(0)) - 1;
            return i >= 0 && i < buttons.size() ? proxy(keyOf(buttons[i])) : QJSValue();
        }
        if (lower == QLatin1String("click")) {
            const int i = toInt(argument(0)) - 1;
            if (i >= 0 && i < buttons.size())
                runHandler(keyOf(buttons[i]), QStringLiteral("onclick"));
        }
        return {};
    }
    if (element->tag == QLatin1String("buttonelement") && lower == QLatin1String("click")) {
        runHandler(keyOf(element), QStringLiteral("onclick"));
        return {};
    }
    if (element->tag == QLatin1String("menu")) {
        if (lower == QLatin1String("deleteallitems")) {
            element->menuItems.clear();
            element->disabledItems.clear();
            element->checkedItems.clear();
        } else if (lower == QLatin1String("appenditem")) {
            element->menuItems.append({toInt(argument(0)), m_model->string(argument(1).toString())});
        } else if (lower == QLatin1String("appendseparator")) {
            element->menuItems.append({0, QString()});
        } else if (lower == QLatin1String("enableitem")) {
            if (toBool(argument(1), true))
                element->disabledItems.remove(toInt(argument(0)));
            else
                element->disabledItems.insert(toInt(argument(0)));
        } else if (lower == QLatin1String("checkitem") || lower == QLatin1String("checkradioitem")) {
            if (toBool(argument(1), true))
                element->checkedItems.insert(toInt(argument(0)));
            else
                element->checkedItems.remove(toInt(argument(0)));
        } else if (lower == QLatin1String("show") || lower == QLatin1String("showex")) {
            QMenu menu;
            for (const auto &[id, text] : std::as_const(element->menuItems)) {
                if (!id) {
                    menu.addSeparator();
                    continue;
                }
                QAction *action = menu.addAction(text);
                action->setData(id);
                action->setEnabled(!element->disabledItems.contains(id));
                action->setCheckable(element->checkedItems.contains(id));
                action->setChecked(element->checkedItems.contains(id));
            }
            QAction *chosen = menu.exec(QCursor::pos());
            return chosen ? chosen->data().toInt() : -1;
        }
        return {};
    }
    if (element->tag == QLatin1String("plugin")) {
        // a plugin's own properties (PLUGIN_PROP_SKINFILE, PLUGIN_PROP_CURRENTPRESET...)
        if (lower == QLatin1String("setproperty"))
            element->values.insert(QStringLiteral("plugin:") + argument(0).toString().toLower(), argument(1));
        else if (lower == QLatin1String("getproperty"))
            return toScript(element->values.value(QStringLiteral("plugin:") + argument(0).toString().toLower(), QString()));
        return {};
    }
    if (element->tag == QLatin1String("automenu") && lower == QLatin1String("show")) {
        if (windowRequest)
            windowRequest(QStringLiteral("automenu:") + property(element, QStringLiteral("menuname")).toString());
        return {};
    }
    return m_model->isMethod(QStringLiteral("element.") + element->tag, lower)
        ? toScript(m_model->call(QStringLiteral("element.") + element->tag, lower, arguments))
        : QJSValue();
}

// ---- the model's changes ----

void SkinView::modelChanged(const QString &object, const QString &name)
{
    reevaluate(object, name);
    runHandler(object, name + QLatin1String("_onchange"));
    if (object == QLatin1String("player.controls") && name == QLatin1String("isavailable"))
        update();
}

void SkinView::modelFired(const QString &object, const QString &event, const QVariantList &arguments)
{
    runHandler(object, event, arguments);
}

void SkinView::start()
{
    if (m_started || !m_view)
        return;
    m_started = true;
    if (qEnvironmentVariableIntValue("WMP_DEBUG") > 1)
        for (const auto &element : m_elements)
            if (!element->handlerNode) {
                int depth = 0;
                for (const Element *e = element->parent; e; e = e->parent)
                    ++depth;
                const QRect r = rectOf(element.get());
                fprintf(stderr, "%*s%s %s %d,%d %dx%d%s\n", depth * 2, "", qPrintable(element->tag),
                        qPrintable(element->values.value(QStringLiteral("id")).toString()), r.x(), r.y(), r.width(), r.height(),
                        toBool(property(element.get(), QStringLiteral("visible")), true) ? "" : " hidden");
            }
    const int interval = toInt(property(m_view, QStringLiteral("timerinterval")));
    if (interval > 0 && m_view->handlers.contains(QStringLiteral("ontimer"))) {
        m_timer.setInterval(interval);
        m_timer.start();
    }
    runHandler(keyOf(m_view), QStringLiteral("onload"));
    // WMP_DUMP=id: an element's properties a few seconds on, for debugging skins
    if (const QString wanted = qEnvironmentVariable("WMP_DUMP"); !wanted.isEmpty() && m_ids.contains(wanted.toLower()))
        QTimer::singleShot(5000, this, [this, wanted] {
            const Element *element = m_ids.value(wanted.toLower());
            const QRect r = rectOf(element);
            fprintf(stderr, "wmp: %s %d,%d %dx%d visible %d\n", qPrintable(wanted), r.x(), r.y(), r.width(), r.height(), visible(element));
            for (auto it = element->values.begin(); it != element->values.end(); ++it)
                fprintf(stderr, "wmp:   %s = %s\n", qPrintable(it.key()), qPrintable(it.value().toString()));
        });
    for (const auto &element : m_elements)
        if (element->tag == QLatin1String("taskcenter"))
            updateTask(element.get());
        else if (element->tag == QLatin1String("plugin") && !element->hosted && element->values.contains(QStringLiteral("objectid")))
            host(element.get(), m_model->plugin(property(element.get(), QStringLiteral("objectid")).toString()), QString());
        else if (element->tag == QLatin1String("text") && toBool(property(element.get(), QStringLiteral("scrolling")), false))
            m_animation.start();
        else if (element->tag == QLatin1String("editbox") || element->tag == QLatin1String("listbox"))
            createControl(element.get());
    layoutHosted();
    scheduleShape();
}

void SkinView::close()
{
    if (m_view && m_started)
        runHandler(keyOf(m_view), QStringLiteral("onclose"));
    for (const auto &element : m_elements)
        if (element->hosted)
            element->hosted->close();
}

// ---- layout ----

QRect SkinView::rectOf(const Element *element) const
{
    if (element == m_view || element->tag == QLatin1String("theme"))
        return rect();
    QRect parent = element->parent ? rectOf(element->parent) : rect();
    if (element->tag == QLatin1String("buttonelement") || element->handlerNode)
        return parent;
    return QRect(parent.x() + toInt(property(element, QStringLiteral("left"))), parent.y() + toInt(property(element, QStringLiteral("top"))),
                 toInt(property(element, QStringLiteral("width"))), toInt(property(element, QStringLiteral("height"))));
}

bool SkinView::visible(const Element *element) const
{
    for (const Element *e = element; e && e != m_theme; e = e->parent)
        if (!toBool(property(e, QStringLiteral("visible")), true))
            return false;
    return true;
}

bool SkinView::enabled(const Element *element) const
{
    for (const Element *e = element; e && e != m_theme; e = e->parent)
        if (!toBool(property(e, QStringLiteral("enabled")), true))
            return false;
    return true;
}

void SkinView::resizeEvent(QResizeEvent *)
{
    if (m_view && size() != m_lastSize) {
        m_lastSize = size();
        setProperty(m_view, QStringLiteral("width"), width());
        setProperty(m_view, QStringLiteral("height"), height());
        if (m_started)
            runHandler(keyOf(m_view), QStringLiteral("onresize"));
    }
    layoutHosted();
    scheduleShape();
}

void SkinView::updateTask(Element *element)
{
    QString folder;
    host(element, m_model->task(&folder), folder);
}

void SkinView::host(Element *element, std::optional<wmpskin::Node> document, const QString &folder)
{
    if (element->hosted) {
        element->hosted->close();
        element->hosted->deleteLater();
        element->hosted = nullptr;
    }
    delete element->widget;
    if (!document) {
        // a task wmp.dll draws itself
        if (element->tag == QLatin1String("taskcenter") && (element->widget = m_model->taskWidget(this))) {
            element->widget->show();
            layoutHosted();
        }
        return;
    }
    element->hosted = new SkinView(m_model, *document, folder, this, rectOf(element).size());
    element->hosted->windowRequest = windowRequest;
    element->hosted->show();
    layoutHosted();
    element->hosted->start();
}

void SkinView::layoutHosted()
{
    for (const auto &element : m_elements)
        for (QWidget *widget : {static_cast<QWidget *>(element->hosted.data()), element->widget.data()})
            if (widget) {
                widget->setGeometry(rectOf(element.get()));
                widget->setVisible(visible(element.get()));
            }
}

void SkinView::scheduleShape()
{
    if (isWindow())
        m_shapeTimer.start();
}

QRegion SkinView::shape()
{
    if (!m_shape.isEmpty())
        return m_shape;
    QImage image(size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        // the window's region is its views' backgrounds, as wmp.dll makes it: what buttons draw
        // outside them is not shown
        QPainter painter(&image);
        m_shapePass = true;
        if (m_view)
            paintElement(painter, m_view, QPoint(), 1.0);
        m_shapePass = false;
    }
    // Child task views are rectangles.
    QRegion region(QBitmap::fromImage(image.createAlphaMask()));
    for (const auto &element : m_elements)
        for (QWidget *widget : {static_cast<QWidget *>(element->hosted.data()), element->widget.data()})
            if (widget && widget->isVisible())
                region += widget->geometry();
    m_shape = region.isEmpty() ? QRegion(rect()) : region;
    return m_shape;
}

// ---- drawing ----

QImage SkinView::imageOf(const Element *element, const QString &name) const
{
    const QString url = property(element, name).toString();
    if (url.isEmpty())
        return {};
    QString hueName = QStringLiteral("hueshift"), saturationName = QStringLiteral("saturation");
    if (name == QLatin1String("backgroundimage") && (element->tag == QLatin1String("subview") || element->tag == QLatin1String("view"))) {
        hueName = QStringLiteral("backgroundimagehueshift");
        saturationName = QStringLiteral("backgroundimagesaturation");
    }
    const double hue = toDouble(property(element, hueName), 0), saturation = toDouble(property(element, saturationName), 1);
    // A buttongroup's mapping image keeps its colours.
    const bool mapping = name == QLatin1String("mappingimage") || name == QLatin1String("clippingimage");
    QColor key = mapping ? QColor() : toColor(property(element, QStringLiteral("transparencycolor")));
    // a buttongroup's pictures take its buttons' transparency colour when it has none
    if (!mapping && !key.isValid() && element->tag == QLatin1String("buttongroup"))
        for (const Element *child : element->children)
            if (child->tag == QLatin1String("buttonelement") && (key = toColor(property(child, QStringLiteral("transparencycolor")))).isValid())
                break;
    // "auto", which buttons and buttongroups start with
    const QString keyText = property(element, QStringLiteral("transparencycolor")).toString().trimmed().toLower();
    const bool automatic = keyText == QLatin1String("auto")
        || (keyText.isEmpty() && !key.isValid() && (element->tag == QLatin1String("button") || element->tag == QLatin1String("buttongroup")));
    static QHash<QString, QImage> cache;
    const QString id = url + QLatin1Char('|') + m_folder + QLatin1Char('|') + QString::number(mapping ? 0 : hue) + QLatin1Char('|')
        + QString::number(mapping ? 1 : saturation) + QLatin1Char('|') + key.name() + (automatic ? QStringLiteral("|auto") : QString());
    // the media's own pictures change with it: never kept
    const bool media = url.startsWith(QLatin1String("WMPImage_"), Qt::CaseInsensitive);
    auto it = media ? cache.constEnd() : cache.constFind(id);
    if (it != cache.constEnd())
        return *it;
    const QImage source = m_model->image(url, m_folder);
    // "auto" is the picture's top-left pixel, whatever its colour (0x4b60df5f reads it with the DIB's
    // GetPixel, 0x4b61b905)
    if (automatic && !mapping && !source.isNull())
        key = QColor::fromRgb(source.pixel(0, 0));
    const QImage result = mapping ? source : prepared(source, key, hue, saturation);
    if (!media)
        cache.insert(id, result);
    return result;
}

void SkinView::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    if (m_view)
        paintElement(painter, m_view, QPoint(), 1.0);
}

void SkinView::paintElement(QPainter &painter, Element *element, const QPoint &, qreal opacity)
{
    if (element->handlerNode || !toBool(property(element, QStringLiteral("visible")), true))
        return;
    const QString &tag = element->tag;
    const QRect rect = rectOf(element);
    opacity *= qBound(0, toInt(property(element, QStringLiteral("alphablend")), 255), 255) / 255.0;
    if (opacity <= 0)
        return;
    painter.save();
    painter.setOpacity(opacity);
    if (tag == QLatin1String("subview") || tag == QLatin1String("view")) {
        painter.setClipRect(rect, Qt::IntersectClip);
        // the parts in the clipping colour of its clipping image (or its background) are not there
        const QColor clipping = toColor(property(element, QStringLiteral("clippingcolor")));
        if (clipping.isValid()) {
            QImage clip = imageOf(element, QStringLiteral("clippingimage"));
            if (clip.isNull())
                clip = m_model->image(property(element, QStringLiteral("backgroundimage")).toString(), m_folder);
            if (!clip.isNull()) {
                QRegion hole = mappingRegion(clip, clipping).translated(rect.topLeft());
                painter.setClipRegion(QRegion(rect) - hole, Qt::IntersectClip);
            }
        }
        if (const QColor background = toColor(property(element, QStringLiteral("backgroundcolor"))); background.isValid())
            painter.fillRect(rect, background);
        const QImage image = imageOf(element, QStringLiteral("backgroundimage"));
        drawImage(painter, rect, image, toBool(property(element, QStringLiteral("backgroundtiled")), false),
                  toBool(property(element, QStringLiteral("resizebackgroundimage")), false));
    } else if (m_shapePass) {
    } else if (tag == QLatin1String("button")) {
        const bool on = toBool(property(element, QStringLiteral("down")), false);
        const bool hover = m_hover == element, pressed = m_pressed == element && hover;
        QStringList order;
        if (!enabled(element))
            order = {QStringLiteral("disabledimage"), QStringLiteral("image")};
        else if (pressed || (on && hover))
            order = {QStringLiteral("hoverdownimage"), QStringLiteral("downimage"), QStringLiteral("image")};
        else if (on)
            order = {QStringLiteral("downimage"), QStringLiteral("image")};
        else if (hover)
            order = {QStringLiteral("hoverimage"), QStringLiteral("image")};
        else
            order = {QStringLiteral("image")};
        if (pressed)
            order.removeFirst(), order.prepend(QStringLiteral("downimage"));
        QImage image;
        for (const QString &name : std::as_const(order))
            if (!(image = imageOf(element, name)).isNull())
                break;
        drawImage(painter, rect, image, toBool(property(element, QStringLiteral("tiled")), false), true);
    } else if (tag == QLatin1String("buttongroup")) {
        paintButtonGroup(painter, element, rect);
    } else if (tag == QLatin1String("text")) {
        paintText(painter, element, rect);
    } else if (tag == QLatin1String("slider") || tag == QLatin1String("progressbar") || tag == QLatin1String("customslider")) {
        paintSlider(painter, element, rect);
    } else if (tag == QLatin1String("video")) {
        const QColor background = toColor(property(element, QStringLiteral("backgroundcolor")));
        painter.fillRect(rect, background.isValid() ? background : QColor(Qt::black));
        const QImage frame = m_model->videoFrame();
        if (!frame.isNull()) {
            QSize size = frame.size();
            if (toBool(property(element, QStringLiteral("stretchtofit")), false) || size.width() > rect.width() || size.height() > rect.height())
                size.scale(rect.size(), toBool(property(element, QStringLiteral("maintainaspectratio")), true) ? Qt::KeepAspectRatio
                                                                                                              : Qt::IgnoreAspectRatio);
            const QRect target(rect.x() + (rect.width() - size.width()) / 2, rect.y() + (rect.height() - size.height()) / 2, size.width(),
                               size.height());
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            painter.drawImage(target, frame);
        }
    } else if (tag == QLatin1String("effects")) {
        paintEffects(painter, element, rect);
    } else if (tag == QLatin1String("playlist")) {
        paintPlaylist(painter, element, rect);
    }
    if (tag == QLatin1String("subview") || tag == QLatin1String("view") || tag == QLatin1String("theme")) {
        QList<Element *> children = element->children;
        std::stable_sort(children.begin(), children.end(), [this](const Element *a, const Element *b) {
            return toInt(property(a, QStringLiteral("zindex"))) < toInt(property(b, QStringLiteral("zindex")));
        });
        for (Element *child : std::as_const(children))
            paintElement(painter, child, QPoint(), 1.0);
    }
    painter.restore();
}

void SkinView::paintButtonGroup(QPainter &painter, Element *element, const QRect &rect)
{
    const QImage mapping = imageOf(element, QStringLiteral("mappingimage"));
    const bool groupEnabled = enabled(element);
    QImage base = groupEnabled ? QImage() : imageOf(element, QStringLiteral("disabledimage"));
    if (base.isNull())
        base = imageOf(element, QStringLiteral("image"));
    if (toBool(property(element, QStringLiteral("showbackground")), true) || !base.isNull())
        drawImage(painter, rect, base, false, false);
    const QImage disabled = imageOf(element, QStringLiteral("disabledimage"));
    int index = 0;
    for (Element *button : std::as_const(element->children)) {
        if (button->tag != QLatin1String("buttonelement"))
            continue;
        const int i = index++;
        const QRegion region = mappingRegion(mapping, toColor(property(button, QStringLiteral("mappingcolor"))));
        if (region.isEmpty())
            continue;
        const bool on = toBool(property(button, QStringLiteral("down")), false);
        const bool hover = m_hover == element && m_hoverButton == i;
        const bool pressed = m_pressed == element && m_pressedButton == i && hover;
        const bool buttonEnabled = groupEnabled && toBool(property(button, QStringLiteral("enabled")), true);
        QImage image;
        if (!buttonEnabled)
            image = disabled;
        else if (pressed)
            image = imageOf(element, QStringLiteral("downimage"));
        else if (on && hover)
            image = imageOf(element, QStringLiteral("hoverdownimage"));
        else if (on)
            image = imageOf(element, QStringLiteral("downimage"));
        else if (hover)
            image = imageOf(element, QStringLiteral("hoverimage"));
        if (on && hover && image.isNull())
            image = imageOf(element, QStringLiteral("downimage"));
        if (image.isNull())
            continue;
        painter.save();
        painter.setClipRegion(region.translated(rect.topLeft()), Qt::IntersectClip);
        drawImage(painter, rect, image, false, false);
        painter.restore();
    }
}

QFont SkinView::textFont(const Element *element, bool hover) const
{
    const bool on = enabled(element);
    QString style = property(element, on ? (hover ? QStringLiteral("hoverfontstyle") : QStringLiteral("fontstyle")) : QStringLiteral("disabledfontstyle"))
                        .toString().toLower();
    if (style.isEmpty())
        style = property(element, QStringLiteral("fontstyle")).toString().toLower();
    QFont font(property(element, QStringLiteral("fontface")).toString());
    // a skin's point size at XP's 96 dots an inch
    font.setPixelSize(qMax(1, int(std::lround(toDouble(property(element, QStringLiteral("fontsize")), 8) * 96 / 72))));
    font.setBold(style.contains(QLatin1String("bold")));
    font.setItalic(style.contains(QLatin1String("italic")));
    font.setUnderline(style.contains(QLatin1String("underline")));
    font.setStrikeOut(style.contains(QLatin1String("strikeout")));
    font.setHintingPreference(QFont::PreferFullHinting);
    if (!toBool(property(element, QStringLiteral("fontsmoothing")), true))
        font.setStyleStrategy(QFont::NoAntialias);
    return font;
}

void SkinView::paintText(QPainter &painter, Element *element, const QRect &rect)
{
    const bool hover = m_hover == element;
    const bool on = enabled(element);
    QColor background = toColor(property(element, on ? (hover ? QStringLiteral("hoverbackgroundcolor") : QStringLiteral("backgroundcolor"))
                                                      : QStringLiteral("disabledbackgroundcolor")));
    if (!background.isValid() && (hover || !on))
        background = toColor(property(element, QStringLiteral("backgroundcolor")));
    if (background.isValid())
        painter.fillRect(rect, background);
    QColor color = toColor(property(element, on ? (hover ? QStringLiteral("hoverforegroundcolor") : QStringLiteral("foregroundcolor"))
                                                : QStringLiteral("disabledforegroundcolor")));
    if (!color.isValid())
        color = toColor(property(element, QStringLiteral("foregroundcolor")));
    const QFont font = textFont(element, hover);
    painter.setFont(font);
    painter.setPen(color.isValid() ? color : QColor(Qt::black));
    const QString text = property(element, QStringLiteral("value")).toString();
    const QString justification = property(element, QStringLiteral("justification")).toString().toLower();
    int flags = Qt::AlignTop;
    flags |= justification == QLatin1String("center") ? Qt::AlignHCenter : justification == QLatin1String("right") ? Qt::AlignRight : Qt::AlignLeft;
    if (toBool(property(element, QStringLiteral("wordwrap")), false))
        flags |= Qt::TextWordWrap;
    painter.setClipRect(rect, Qt::IntersectClip);
    if (toBool(property(element, QStringLiteral("scrolling")), false)) {
        // the marquee: the text goes by, then comes back from the other side
        const int width = QFontMetrics(font).horizontalAdvance(text);
        const int span = width + rect.width();
        const int offset = span > 0 ? int(element->scroll) % span : 0;
        const bool down = property(element, QStringLiteral("scrollingdirection")).toString().toLower() == QLatin1String("right");
        const int x = down ? rect.x() - width + offset : rect.right() + 1 - offset;
        painter.drawText(QRect(x, rect.y(), width + 1, rect.height()), Qt::AlignLeft | Qt::AlignTop, text);
        return;
    }
    // XP's DrawText ends what does not fit with an ellipsis only when asked; skins clip.
    painter.drawText(rect, flags, text);
}

void SkinView::paintSlider(QPainter &painter, Element *element, const QRect &rect)
{
    const double min = toDouble(property(element, QStringLiteral("min"))), max = toDouble(property(element, QStringLiteral("max")), 100);
    const double value = toDouble(property(element, QStringLiteral("value")));
    const double fraction = max > min ? qBound(0.0, (value - min) / (max - min), 1.0) : 0;
    if (element->tag == QLatin1String("customslider")) {
        // image is a strip of the slider at each of its values, as wide as the element each (Pyrite's
        // volume: 399 pixels, 21 of 19); the value picks the one shown
        QImage strip = imageOf(element, m_pressed == element ? QStringLiteral("downimage")
                                        : m_hover == element ? QStringLiteral("hoverimage") : QStringLiteral("image"));
        if (strip.isNull())
            strip = imageOf(element, QStringLiteral("image"));
        if (!enabled(element) && !imageOf(element, QStringLiteral("disabledimage")).isNull())
            strip = imageOf(element, QStringLiteral("disabledimage"));
        const int frames = rect.width() > 0 ? qMax(1, strip.width() / rect.width()) : 1;
        const int frame = qBound(0, int(std::lround(fraction * (frames - 1))), frames - 1);
        painter.drawImage(rect.topLeft(), strip, QRect(frame * rect.width(), 0, rect.width(), rect.height()));
        return;
    }
    const bool vertical = property(element, QStringLiteral("direction")).toString().toLower() == QLatin1String("vertical");
    const bool tiled = toBool(property(element, QStringLiteral("tiled")), false);
    const bool on = enabled(element);
    const int border = toInt(property(element, QStringLiteral("bordersize")));
    QImage background = imageOf(element, !on ? QStringLiteral("disabledimage")
                                             : m_hover == element ? QStringLiteral("backgroundhoverimage") : QStringLiteral("backgroundimage"));
    if (background.isNull())
        background = imageOf(element, QStringLiteral("backgroundimage"));
    if (background.isNull()) {
        if (const QColor color = toColor(property(element, QStringLiteral("backgroundcolor"))); color.isValid())
            painter.fillRect(rect, color);
    } else {
        drawImage(painter, rect, background, tiled, true);
    }
    QImage thumb = imageOf(element, !on ? QStringLiteral("thumbdisabledimage")
                                        : m_pressed == element ? QStringLiteral("thumbdownimage")
                                        : m_hover == element ? QStringLiteral("thumbhoverimage") : QStringLiteral("thumbimage"));
    if (thumb.isNull())
        thumb = imageOf(element, QStringLiteral("thumbimage"));
    const int track = (vertical ? rect.height() - thumb.height() : rect.width() - thumb.width()) - 2 * border;
    const int position = border + int(std::lround(fraction * qMax(0, track)));
    QImage foreground = imageOf(element, m_hover == element ? QStringLiteral("foregroundhoverimage") : QStringLiteral("foregroundimage"));
    if (foreground.isNull())
        foreground = imageOf(element, QStringLiteral("foregroundimage"));
    const int filled = position + (vertical ? thumb.height() : thumb.width()) / 2;
    if (!foreground.isNull() || toColor(property(element, QStringLiteral("foregroundcolor"))).isValid()) {
        painter.save();
        const QRect part = vertical ? QRect(rect.x(), rect.bottom() + 1 - filled, rect.width(), filled) : QRect(rect.x(), rect.y(), filled, rect.height());
        painter.setClipRect(part, Qt::IntersectClip);
        if (!foreground.isNull())
            drawImage(painter, rect, foreground, tiled, true);
        else
            painter.fillRect(part, toColor(property(element, QStringLiteral("foregroundcolor"))));
        painter.restore();
    }
    if (!thumb.isNull()) {
        const QPoint at = vertical ? QPoint(rect.x() + (rect.width() - thumb.width()) / 2, rect.bottom() + 1 - position - thumb.height())
                                   : QPoint(rect.x() + position, rect.y() + (rect.height() - thumb.height()) / 2);
        painter.drawImage(at, thumb);
    }
}

void SkinView::paintEffects(QPainter &painter, Element *element, const QRect &rect)
{
    painter.fillRect(rect, Qt::black);
    const QList<float> bands = m_model->spectrum();
    if (bands.isEmpty())
        return;
    Q_UNUSED(element);
    // Bars and Waves' Bars: green bars on black, with peaks
    const int count = bands.size();
    const qreal width = qreal(rect.width()) / count;
    for (int i = 0; i < count; ++i) {
        const int height = int(bands[i] * rect.height());
        const QRectF bar(rect.x() + i * width + 1, rect.bottom() + 1 - height, qMax<qreal>(1, width - 2), height);
        QLinearGradient gradient(bar.bottomLeft(), QPointF(bar.x(), rect.top()));
        gradient.setColorAt(0, QColor(0, 120, 0));
        gradient.setColorAt(1, QColor(160, 255, 60));
        painter.fillRect(bar, gradient);
    }
}

void SkinView::paintPlaylist(QPainter &painter, Element *element, const QRect &rect)
{
    const QColor background = toColor(property(element, QStringLiteral("backgroundcolor")));
    painter.fillRect(rect, background.isValid() ? background : QColor(Qt::black));
    const QVariantList items = m_model->call(QStringLiteral("player"), QStringLiteral("playlistnames"), {}).toList();
    const int current = m_model->call(QStringLiteral("player"), QStringLiteral("playlistcurrent"), {}).toInt();
    QFont font(QStringLiteral("Tahoma"));
    font.setPointSizeF(8);
    painter.setFont(font);
    const QFontMetrics metrics(font);
    const int line = metrics.height() + 2;
    QColor foreground = toColor(property(element, QStringLiteral("foregroundcolor")));
    if (!foreground.isValid())
        foreground = Qt::white;
    QColor playing = toColor(property(element, QStringLiteral("itemplayingcolor")));
    if (!playing.isValid())
        playing = QColor(QStringLiteral("#8EEB6E"));
    QColor selected = toColor(property(element, QStringLiteral("itemselectedcolor")));
    if (!selected.isValid())
        selected = QColor(QStringLiteral("#3F5A86"));
    painter.setClipRect(rect, Qt::IntersectClip);
    m_playlistTop = qBound(0, m_playlistTop, qMax(0, int(items.size()) - 1));
    for (int i = m_playlistTop; i < items.size(); ++i) {
        const QRect row(rect.x() + 4, rect.y() + 2 + (i - m_playlistTop) * line, rect.width() - 8, line);
        if (row.top() > rect.bottom())
            break;
        if (i == m_playlistSelected)
            painter.fillRect(row.adjusted(-2, 0, 2, 0), selected);
        const QStringList columns = items[i].toStringList();
        painter.setPen(i == current ? playing : foreground);
        painter.drawText(row, Qt::AlignLeft | Qt::AlignVCenter, metrics.elidedText(columns.value(0), Qt::ElideRight, row.width() - 40));
        painter.drawText(row, Qt::AlignRight | Qt::AlignVCenter, columns.value(1));
    }
}

int SkinView::playlistRow(Element *element, const QPoint &point) const
{
    const QRect rect = rectOf(element);
    QFont font(QStringLiteral("Tahoma"));
    font.setPointSizeF(8);
    const int line = QFontMetrics(font).height() + 2;
    if (!rect.contains(point) || point.y() < rect.y() + 2)
        return -1;
    const int row = m_playlistTop + (point.y() - rect.y() - 2) / line;
    const int count = int(m_model->call(QStringLiteral("player"), QStringLiteral("playlistnames"), {}).toList().size());
    return row < count ? row : -1;
}

// ---- the mouse ----

bool SkinView::interactive(const Element *element) const
{
    const QString &tag = element->tag;
    if (tag == QLatin1String("button") || tag == QLatin1String("buttongroup") || tag == QLatin1String("slider")
        || tag == QLatin1String("customslider") || tag == QLatin1String("playlist"))
        return true;
    if (tag == QLatin1String("progressbar"))
        return false;
    return element->handlers.contains(QStringLiteral("onclick")) || element->handlers.contains(QStringLiteral("onmousedown"))
        || element->handlers.contains(QStringLiteral("ondblclick")) || element->handlers.contains(QStringLiteral("onmouseup"));
}

Element *SkinView::hit(Element *element, const QPoint &point, const QPoint &origin, int *button) const
{
    Q_UNUSED(origin);
    if (element->handlerNode || !toBool(property(element, QStringLiteral("visible")), true) || element->hosted)
        return nullptr;
    const QString &tag = element->tag;
    const QRect rect = rectOf(element);
    const bool container = tag == QLatin1String("subview") || tag == QLatin1String("view") || tag == QLatin1String("theme");
    if (!rect.contains(point) && tag != QLatin1String("theme"))
        return nullptr;
    if (container) {
        const QColor clipping = toColor(property(element, QStringLiteral("clippingcolor")));
        if (clipping.isValid()) {
            QImage clip = imageOf(element, QStringLiteral("clippingimage"));
            if (clip.isNull())
                clip = m_model->image(property(element, QStringLiteral("backgroundimage")).toString(), m_folder);
            if (!clip.isNull() && mappingRegion(clip, clipping).contains(point - rect.topLeft()))
                return nullptr;
        }
        QList<Element *> children = element->children;
        std::stable_sort(children.begin(), children.end(), [this](const Element *a, const Element *b) {
            return toInt(property(a, QStringLiteral("zindex"))) < toInt(property(b, QStringLiteral("zindex")));
        });
        for (auto it = children.rbegin(); it != children.rend(); ++it)
            if (Element *found = hit(*it, point, origin, button))
                return found;
        if (toBool(property(element, QStringLiteral("passthrough")), false))
            return nullptr;
        // its background, where it has one
        const QImage image = imageOf(element, QStringLiteral("backgroundimage"));
        if (!image.isNull() && !toBool(property(element, QStringLiteral("backgroundtiled")), false)
            && !toBool(property(element, QStringLiteral("resizebackgroundimage")), false)) {
            const QPoint at = point - rect.topLeft();
            if (QRect(QPoint(), image.size()).contains(at) && qAlpha(image.pixel(at)) == 0)
                return nullptr;
        }
        return element;
    }
    if (toBool(property(element, QStringLiteral("passthrough")), false))
        return nullptr;
    if (tag == QLatin1String("buttongroup")) {
        const QImage mapping = imageOf(element, QStringLiteral("mappingimage"));
        const QPoint at = point - rect.topLeft();
        if (mapping.isNull() || !QRect(QPoint(), mapping.size()).contains(at))
            return nullptr;
        const QRgb pixel = mapping.pixel(at) & 0xffffff;
        int index = 0;
        for (const Element *child : element->children) {
            if (child->tag != QLatin1String("buttonelement"))
                continue;
            const QColor color = toColor(property(child, QStringLiteral("mappingcolor")));
            if (color.isValid() && (color.rgb() & 0xffffff) == pixel) {
                *button = index;
                return element;
            }
            ++index;
        }
        return nullptr;
    }
    if (tag == QLatin1String("button")) {
        const QImage image = imageOf(element, QStringLiteral("image"));
        if (!image.isNull() && !toBool(property(element, QStringLiteral("tiled")), false)) {
            const QPoint at = QPoint((point.x() - rect.x()) * image.width() / qMax(1, rect.width()),
                                     (point.y() - rect.y()) * image.height() / qMax(1, rect.height()));
            if (QRect(QPoint(), image.size()).contains(at) && qAlpha(image.pixel(at)) == 0)
                return nullptr;
        }
    }
    return element;
}

void SkinView::setHover(Element *element, int button)
{
    if (element == m_hover && button == m_hoverButton)
        return;
    Element *old = m_hover;
    const int oldButton = m_hoverButton;
    m_hover = element;
    m_hoverButton = button;
    if (old && old != element)
        runHandler(keyOf(old), QStringLiteral("onmouseout"));
    if (old && old->tag == QLatin1String("buttongroup") && oldButton >= 0) {
        int index = 0;
        for (Element *child : std::as_const(old->children))
            if (child->tag == QLatin1String("buttonelement") && index++ == oldButton)
                runHandler(keyOf(child), QStringLiteral("onmouseout"));
    }
    if (element && element != old)
        runHandler(keyOf(element), QStringLiteral("onmouseover"));
    if (element && element->tag == QLatin1String("buttongroup") && button >= 0) {
        int index = 0;
        for (Element *child : std::as_const(element->children))
            if (child->tag == QLatin1String("buttonelement") && index++ == button)
                runHandler(keyOf(child), QStringLiteral("onmouseover"));
    }
    QToolTip::hideText();
    update();
}

void SkinView::fireMouse(Element *element, const QString &name, QMouseEvent *mouse)
{
    const QRect rect = rectOf(element);
    const QPoint at = mouse->position().toPoint();
    m_event = {
        {QStringLiteral("button"), mouse->button() == Qt::RightButton ? 2 : mouse->button() == Qt::MiddleButton ? 4 : 1},
        {QStringLiteral("clientx"), at.x()}, {QStringLiteral("clienty"), at.y()},
        {QStringLiteral("offsetx"), at.x() - rect.x()}, {QStringLiteral("offsety"), at.y() - rect.y()},
        {QStringLiteral("x"), at.x()}, {QStringLiteral("y"), at.y()},
        {QStringLiteral("screenx"), mouse->globalPosition().toPoint().x()}, {QStringLiteral("screeny"), mouse->globalPosition().toPoint().y()},
        {QStringLiteral("shiftkey"), bool(mouse->modifiers() & Qt::ShiftModifier)},
        {QStringLiteral("ctrlkey"), bool(mouse->modifiers() & Qt::ControlModifier)},
        {QStringLiteral("altkey"), bool(mouse->modifiers() & Qt::AltModifier)},
        {QStringLiteral("srcelement"), QVariant::fromValue(ObjectRef{keyOf(element)})},
    };
    runHandler(keyOf(element), name);
}

void SkinView::mousePressEvent(QMouseEvent *event)
{
    if (!m_view)
        return;
    int button = -1;
    Element *target = hit(m_view, event->position().toPoint(), QPoint(), &button);
    if (qEnvironmentVariableIntValue("WMP_DEBUG") > 2)
        fprintf(stderr, "wmp: press %d,%d on %s %s button %d\n", event->position().toPoint().x(), event->position().toPoint().y(),
                target ? qPrintable(target->tag) : "-", target ? qPrintable(target->values.value(QStringLiteral("id")).toString()) : "", button);
    m_pressed = nullptr;
    m_pressedButton = -1;
    if (target && interactive(target) && enabled(target)) {
        m_pressed = target;
        m_pressedButton = button;
        fireMouse(target, QStringLiteral("onmousedown"), event);
        if (target->tag == QLatin1String("slider") || target->tag == QLatin1String("customslider"))
            dragSlider(target, event->position().toPoint());
        else if (target->tag == QLatin1String("playlist"))
            m_playlistSelected = playlistRow(target, event->position().toPoint());
        update();
        return;
    }
    if (target && target != m_view)
        fireMouse(target, QStringLiteral("onmousedown"), event);
    fireMouse(m_view, QStringLiteral("onmousedown"), event);
    // The background moves the window, its edges size it.
    if (event->button() == Qt::LeftButton && window()->windowHandle()) {
        const QPoint at = window()->mapFromGlobal(event->globalPosition().toPoint());
        const QRect frame = window()->rect();
        Qt::Edges edges;
        if (isResizable() && isWindow()) {
            if (at.x() < 4) edges |= Qt::LeftEdge;
            if (at.x() >= frame.width() - 4) edges |= Qt::RightEdge;
            if (at.y() < 4) edges |= Qt::TopEdge;
            if (at.y() >= frame.height() - 4) edges |= Qt::BottomEdge;
        }
        if (edges)
            window()->windowHandle()->startSystemResize(edges);
        else if (target)
            window()->windowHandle()->startSystemMove();
    }
}

void SkinView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_pressed && (m_pressed->tag == QLatin1String("slider") || m_pressed->tag == QLatin1String("customslider"))) {
        dragSlider(m_pressed, event->position().toPoint());
        return;
    }
    int button = -1;
    Element *target = m_view ? hit(m_view, event->position().toPoint(), QPoint(), &button) : nullptr;
    if (target && !interactive(target) && !(target->tag == QLatin1String("text") && (target->values.contains(QStringLiteral("hoverforegroundcolor"))
                                                                                    || target->values.contains(QStringLiteral("tooltip")))))
        target = nullptr;
    setHover(target, button);
    if (target)
        fireMouse(target, QStringLiteral("onmousemove"), event);
}

void SkinView::mouseReleaseEvent(QMouseEvent *event)
{
    Element *pressed = m_pressed;
    const int pressedButton = m_pressedButton;
    m_pressed = nullptr;
    m_pressedButton = -1;
    if (!pressed) {
        update();
        return;
    }
    int button = -1;
    Element *target = hit(m_view, event->position().toPoint(), QPoint(), &button);
    fireMouse(pressed, QStringLiteral("onmouseup"), event);
    // a slider's ondragend: the skins' seek bars move the player only then (Revert, Splat)
    if (pressed->tag == QLatin1String("slider") || pressed->tag == QLatin1String("customslider"))
        runHandler(keyOf(pressed), QStringLiteral("ondragend"));
    if (target == pressed && button == pressedButton && event->button() == Qt::LeftButton && enabled(pressed)) {
        if (pressed->tag == QLatin1String("buttongroup")) {
            int index = 0;
            for (Element *child : std::as_const(pressed->children)) {
                if (child->tag != QLatin1String("buttonelement") || index++ != button)
                    continue;
                if (!toBool(property(child, QStringLiteral("enabled")), true))
                    break;
                if (toBool(property(child, QStringLiteral("sticky")), false)) {
                    // a radio group has one down at a time
                    if (toBool(property(pressed, QStringLiteral("radio")), false)) {
                        for (Element *other : std::as_const(pressed->children))
                            if (other != child && other->tag == QLatin1String("buttonelement"))
                                setProperty(other, QStringLiteral("down"), false);
                        setProperty(child, QStringLiteral("down"), true);
                    } else {
                        setProperty(child, QStringLiteral("down"), !toBool(property(child, QStringLiteral("down")), false));
                    }
                }
                fireMouse(child, QStringLiteral("onclick"), event);
                break;
            }
            fireMouse(pressed, QStringLiteral("onclick"), event);
        } else {
            if (pressed->tag == QLatin1String("button") && toBool(property(pressed, QStringLiteral("sticky")), false))
                setProperty(pressed, QStringLiteral("down"), !toBool(property(pressed, QStringLiteral("down")), false));
            fireMouse(pressed, QStringLiteral("onclick"), event);
        }
    }
    update();
}

void SkinView::mouseDoubleClickEvent(QMouseEvent *event)
{
    int button = -1;
    Element *target = m_view ? hit(m_view, event->position().toPoint(), QPoint(), &button) : nullptr;
    if (target && target->tag == QLatin1String("playlist") && enabled(target)) {
        // a double-click plays the item, as the playlist control does
        if (const int row = playlistRow(target, event->position().toPoint()); row >= 0) {
            m_playlistSelected = row;
            m_model->call(QStringLiteral("player"), QStringLiteral("playlistplay"), {row});
            update();
        }
        return;
    }
    if (target && target != m_view && target->handlers.contains(QStringLiteral("ondblclick")))
        fireMouse(target, QStringLiteral("ondblclick"), event);
    else if (m_view && (!target || !interactive(target)))
        fireMouse(m_view, QStringLiteral("ondblclick"), event);
    else
        mousePressEvent(event);
}

void SkinView::wheelEvent(QWheelEvent *event)
{
    int button = -1;
    Element *target = m_view ? hit(m_view, event->position().toPoint(), QPoint(), &button) : nullptr;
    if (target && target->tag == QLatin1String("playlist")) {
        m_playlistTop = qMax(0, m_playlistTop + (event->angleDelta().y() > 0 ? -3 : 3));
        update();
        return;
    }
    if (target && (target->tag == QLatin1String("slider")) && enabled(target)) {
        const double min = toDouble(property(target, QStringLiteral("min"))), max = toDouble(property(target, QStringLiteral("max")), 100);
        const double step = (max - min) / 20 * (event->angleDelta().y() > 0 ? 1 : -1);
        QMouseEvent fake(QEvent::MouseMove, event->position(), event->globalPosition(), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        const double value = qBound(min, toDouble(property(target, QStringLiteral("value"))) + step, max);
        setProperty(target, QStringLiteral("value"), value);
        for (const auto &binding : std::as_const(m_bindings))
            if (binding->element == target && binding->name == QLatin1String("value") && !binding->mode) {
                m_js.globalObject().setProperty(QStringLiteral("__value"), value);
                evaluate(binding->expression + QStringLiteral(" = __value"), keyOf(target));
            }
        runHandler(keyOf(target), QStringLiteral("onchange"));
    }
}

void SkinView::dragSlider(Element *slider, const QPoint &point)
{
    const QRect rect = rectOf(slider);
    const double min = toDouble(property(slider, QStringLiteral("min"))), max = toDouble(property(slider, QStringLiteral("max")), 100);
    if (slider->tag == QLatin1String("customslider")) {
        // positionImage: a grey map of the element, black the least value and white the most; its
        // other colours are not the slider's
        const QImage map = m_model->image(property(slider, QStringLiteral("positionimage")).toString(), m_folder);
        const QPoint at = point - rect.topLeft();
        if (map.isNull() || !QRect(QPoint(), map.size()).contains(at))
            return;
        const QRgb pixel = map.pixel(at);
        if (qRed(pixel) != qGreen(pixel) || qGreen(pixel) != qBlue(pixel))
            return;
        const double value = min + qRed(pixel) / 255.0 * (max - min);
        setProperty(slider, QStringLiteral("value"), value);
        for (const auto &binding : std::as_const(m_bindings))
            if (binding->element == slider && binding->name == QLatin1String("value") && !binding->mode) {
                m_js.globalObject().setProperty(QStringLiteral("__value"), value);
                evaluate(binding->expression + QStringLiteral(" = __value"), keyOf(slider));
            }
        runHandler(keyOf(slider), QStringLiteral("onchange"));
        return;
    }
    const bool vertical = property(slider, QStringLiteral("direction")).toString().toLower() == QLatin1String("vertical");
    const QImage thumb = imageOf(slider, QStringLiteral("thumbimage"));
    double fraction;
    if (vertical) {
        const int track = qMax(1, rect.height() - thumb.height());
        fraction = 1.0 - double(point.y() - rect.y() - thumb.height() / 2) / track;
    } else {
        const int track = qMax(1, rect.width() - thumb.width());
        fraction = double(point.x() - rect.x() - thumb.width() / 2) / track;
    }
    const double value = min + qBound(0.0, fraction, 1.0) * (max - min);
    setProperty(slider, QStringLiteral("value"), value);
    // a wmpprop: value is the player's: the slider moves it
    for (const auto &binding : std::as_const(m_bindings))
        if (binding->element == slider && binding->name == QLatin1String("value") && !binding->mode) {
            m_js.globalObject().setProperty(QStringLiteral("__value"), value);
            evaluate(binding->expression + QStringLiteral(" = __value"), keyOf(slider));
        }
    runHandler(keyOf(slider), QStringLiteral("onchange"));
}

void SkinView::leaveEvent(QEvent *)
{
    if (!m_pressed)
        setHover(nullptr, -1);
}

bool SkinView::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip && m_hover) {
        auto *help = static_cast<QHelpEvent *>(event);
        QString tip;
        if (m_hover->tag == QLatin1String("buttongroup") && m_hoverButton >= 0) {
            int index = 0;
            for (Element *child : std::as_const(m_hover->children))
                if (child->tag == QLatin1String("buttonelement") && index++ == m_hoverButton)
                    tip = property(child, toBool(property(child, QStringLiteral("down")), false) && child->values.contains(QStringLiteral("downtooltip"))
                                              ? QStringLiteral("downtooltip") : QStringLiteral("uptooltip")).toString();
        } else if (m_hover->tag == QLatin1String("button")) {
            tip = property(m_hover, toBool(property(m_hover, QStringLiteral("down")), false) && m_hover->values.contains(QStringLiteral("downtooltip"))
                                        ? QStringLiteral("downtooltip") : QStringLiteral("uptooltip")).toString();
        } else {
            tip = property(m_hover, QStringLiteral("tooltip")).toString();
        }
        if (tip.startsWith(QLatin1String("res://"), Qt::CaseInsensitive))
            tip = m_model->string(tip);
        if (!tip.isEmpty())
            QToolTip::showText(help->globalPos(), tip, this);
        return true;
    }
    return QWidget::event(event);
}

} // namespace wmp
