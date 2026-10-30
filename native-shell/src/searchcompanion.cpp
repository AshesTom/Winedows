// SPDX-License-Identifier: LGPL-2.1-or-later
#include "searchcompanion.h"

#include "acs.h"
#include "theme.h"
#include "xpfiles.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QXmlStreamReader>

namespace {

// srchui.dll: the buttons' words
const int STRING_BACK = 4960, STRING_SEARCH = 4964, STRING_STOP = 4965, STRING_BROWSE = 5010;
// The steps' links and buttons, by the ids of XP's files
enum {
    LINK_PREFERENCES = 5000, LINK_HIGHLIGHT = 5002, LINK_NEW_SEARCH = 5006, LINK_SAMPLE = 5013, LINK_FINISHED = 5014,
    LINK_SEARCH_COMPUTER = 5300, LINK_CHARACTER = 5304, LINK_CHANGE_FILES = 5306, LINK_CHANGE_INTERNET = 5308,
    LINK_MORE_ENGINES = 5400, LINK_MEDIA = 5500, LINK_DOCUMENTS = 5501, LINK_ALL_FILES = 5502, LINK_PRINTERS = 5503,
    LINK_INTERNET = 5504, LINK_COMPUTERS = 5505, LINK_HELP = 5703,
    LINK_REFINE_FIRST = 5752, LINK_INCLUDE_HIDDEN = 5758, LINK_WITHOUT_HIDDEN = 5759, LINK_PICTURES_ONLINE = 5768, LINK_DIGITAL_ONLINE = 5771,
    EDIT_QUESTION = 5001, EDIT_NAME = 5603, EDIT_CONTENT = 5604, COMBO_LOOK_IN = 5606, RADIO_DATES = 5636, COMBO_WHICH_DATE = 5644,
    DATE_FROM = 5637, DATE_TO = 5638, RADIO_SIZE = 5639, COMBO_SIZE = 5640, EDIT_SIZE = 5641,
    LIST_ENGINE = 5309,
};
const char ENGINE_BING[] = "https://www.bing.com/search?q=";
// Those of XP's list that still answer
const QStringList LIVE_ENGINES{QStringLiteral("Google"), QStringLiteral("Yahoo")};
// What the steps have that does nothing on Linux, and is left out
const QList<int> HIDDEN_LINKS{LINK_PRINTERS, 5301, 5625, 5626, 5312, 5313, 5314, 5315, 5772};
// No step file: the least that works
const char FALLBACK[] = "<SimpleDialog><Title><Text>Search by any or all of the criteria below.</Text></Title>"
    "<Label TopMargin=\"8\" BottomMargin=\"2\"><Text>All or part of the file name:</Text></Label>"
    "<Edit Id=\"5603\" Height=\"20\"><SearchProp Name=\"Named\"/></Edit></SimpleDialog>";

QString settingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/native-explorer.conf");
}

QIcon stepIcon(int group)
{
    static QHash<int, QIcon> icons;
    if (const auto found = icons.constFind(group); found != icons.constEnd())
        return *found;
    QIcon icon;
    static const auto resources = xpfiles::resources(xpfiles::find(QStringLiteral("srchui.dll")));
    if (resources) {
        const QImage image = xpfiles::icon(*resources, group, 16);
        if (!image.isNull())
            icon.addPixmap(QPixmap::fromImage(image));
    }
    icons.insert(group, icon);
    return icon;
}

QString srchString(int id, const char *fallback)
{
    static const auto resources = xpfiles::resources(xpfiles::find(QStringLiteral("srchui.dll")));
    QString text = resources ? xpfiles::string(*resources, id) : QString();
    text.remove(QChar(0));
    return text.isEmpty() ? QLatin1String(fallback) : text;
}

// A link's words with their "&", as rich text
QString linkHtml(const QString &text, bool hot)
{
    QString out;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] == QLatin1Char('&') && i + 1 < text.size()) {
            ++i;
            out += QStringLiteral("<u>") + QString(text[i]).toHtmlEscaped() + QStringLiteral("</u>");
        } else {
            out += QString(text[i]).toHtmlEscaped();
        }
    }
    return hot ? QStringLiteral("<span style=\"text-decoration:underline\">%1</span>").arg(out) : out;
}

// A row: its icon and its words, a link if it does something
class LinkRow : public QWidget
{
public:
    LinkRow(const QIcon &icon, const QString &text, std::function<void()> action, QWidget *parent)
        : QWidget(parent), m_text(text), m_action(std::move(action))
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(5);
        auto *iconLabel = new QLabel(this);
        iconLabel->setFixedSize(16, 16);
        if (!icon.isNull())
            iconLabel->setPixmap(icon.pixmap(16, 16));
        layout->addWidget(iconLabel, 0, Qt::AlignTop);
        m_label = new QLabel(linkHtml(text, false), this);
        m_label->setWordWrap(true);
        m_label->setTextFormat(Qt::RichText);
        m_label->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(m_label, 1);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void enterEvent(QEnterEvent *) override { m_label->setText(linkHtml(m_text, true)); }
    void leaveEvent(QEvent *) override { m_label->setText(linkHtml(m_text, false)); }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) && m_action) {
            const auto action = m_action;
            action();
        }
    }

private:
    QString m_text;
    std::function<void()> m_action;
    QLabel *m_label;
};

// The rounded panel the step sits in
class Balloon : public QWidget
{
public:
    Balloon(QColor *colour, QWidget *parent) : QWidget(parent), m_colour(colour) {}

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(*m_colour);
        painter.drawRoundedRect(rect(), 10, 10);
    }

private:
    QColor *m_colour;
};

} // namespace

SearchCompanion::SearchCompanion(QWidget *parent)
    : QWidget(parent)
{
    m_top = QColor(123, 162, 231);
    m_bottom = QColor(99, 117, 214);
    m_body = QColor(214, 223, 247);
    const QSettings settings(settingsPath(), QSettings::IniFormat);
    m_character = settings.value(QStringLiteral("searchCharacter"), true).toBool();

    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_scroll->setAutoFillBackground(false);
    m_balloon = new Balloon(&m_body, m_scroll);
    m_content = new QVBoxLayout(m_balloon);
    m_content->setContentsMargins(15, 14, 13, 13);
    m_content->setSpacing(0);
    m_scroll->setWidget(m_balloon);

    m_agent = new AgentWidget(this);
    connect(m_agent, &AgentWidget::clicked, this, [this] { m_agent->play(QStringLiteral("ClickedOn")); });
    m_agent->setVisible(m_character && m_agent->loaded());
    showStep(QStringLiteral("lclsrch"), false);
}

QRect SearchCompanion::balloonRect() const
{
    const int reserved = m_agent->isVisibleTo(this) ? m_agent->height() + 4 : 8;
    return QRect(5, 8, width() - 13, qMax(50, height() - 8 - reserved));
}

void SearchCompanion::placeCharacter()
{
    QRect room = balloonRect();
    // The panel is as tall as its step, up to what there is.
    const int wanted = m_balloon->sizeHint().height();
    if (wanted > 0 && wanted < room.height())
        room.setHeight(wanted);
    m_scroll->setGeometry(room);
    if (m_agent->isVisibleTo(this))
        m_agent->move((width() - m_agent->width()) / 2 - 20, height() - m_agent->height());
    update();
}

void SearchCompanion::resizeEvent(QResizeEvent *)
{
    placeCharacter();
}

void SearchCompanion::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    QLinearGradient gradient(0, 0, 0, height());
    gradient.setColorAt(0, m_top);
    gradient.setColorAt(1, m_bottom);
    painter.fillRect(rect(), gradient);
    if (m_agent->isVisibleTo(this)) {
        // The panel's point, down to the character
        const QRect panel = m_scroll->geometry();
        QPainterPath tail;
        tail.moveTo(panel.left() + 12, panel.bottom() - 2);
        tail.lineTo(panel.left() + 32, panel.bottom() - 2);
        tail.lineTo(panel.left() + 32, panel.bottom() + 20);
        tail.closeSubpath();
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillPath(tail, m_body);
    }
}

std::optional<SearchCompanion::Node> SearchCompanion::loadStep(const QString &name) const
{
    QByteArray data;
    QFile file(xpfiles::find(name + QStringLiteral(".xml")));
    if (file.open(QIODevice::ReadOnly))
        data = file.readAll();
    else if (name == QLatin1String("lclsrch") || name == QLatin1String("lcladv"))
        data = FALLBACK;
    else
        return std::nullopt;

    QXmlStreamReader xml(data);
    QList<Node> stack;
    Node root;
    QString text;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            Node node;
            node.tag = xml.name().toString();
            for (const QXmlStreamAttribute &attribute : xml.attributes())
                node.attributes.insert(attribute.name().toString(), attribute.value().toString());
            stack.append(node);
            text.clear();
        } else if (xml.isCharacters()) {
            text += xml.text();
        } else if (xml.isEndElement()) {
            Node node = stack.takeLast();
            const QString value = text.trimmed();
            text.clear();
            if (stack.isEmpty()) {
                root = node;
                break;
            }
            Node &parent = stack.last();
            // <Text> holds the words, itself or by <Balloon> and <Bar>: the one for this mode
            if (node.tag == QLatin1String("Text")) {
                if (parent.text.isEmpty())
                    parent.text = value.isEmpty() ? node.text : value;
            } else if ((node.tag == QLatin1String("Balloon") || node.tag == QLatin1String("Bar")) && parent.tag == QLatin1String("Text")) {
                if (node.tag == QLatin1String(m_character ? "Balloon" : "Bar"))
                    parent.text = value;
            } else if (node.tag == QLatin1String("SearchProp")) {
                parent.searchProps.append({node.attributes.value(QStringLiteral("Name")), value});
            } else if (node.tag == QLatin1String("Property")) {
                parent.properties.insert(node.attributes.value(QStringLiteral("Name")), value);
            } else if (node.tag == QLatin1String("Name") && parent.tag == QLatin1String("Item")) {
                parent.text = value;
            } else if (node.tag == QLatin1String("Value") && parent.tag == QLatin1String("Item")) {
                parent.attributes.insert(QStringLiteral("Value"), value);
            } else if (node.tag == QLatin1String("Item")) {
                parent.items.append({node.text, node.attributes.value(QStringLiteral("Value"))});
            } else {
                parent.children.append(node);
            }
        }
    }
    if (root.tag.isEmpty())
        return std::nullopt;
    return root;
}

void SearchCompanion::restart()
{
    m_history.clear();
    showStep(QStringLiteral("lclsrch"), false);
}

void SearchCompanion::showStep(const QString &name, bool remember)
{
    const auto root = loadStep(name);
    if (!root)
        return;
    if (remember && !m_step.isEmpty() && m_step != name)
        m_history.append(m_step);
    m_step = name;
    m_controls.clear();
    m_expanders.clear();
    while (QLayoutItem *item = m_content->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    m_margin = 0;
    for (const Node &child : root->children)
        addNode(child, m_content);

    // The buttons at the foot of the step
    const bool form = name == QLatin1String("lclmm") || name == QLatin1String("lcldocs") || name == QLatin1String("lcladv") || name == QLatin1String("lclcomp");
    const bool web = name == QLatin1String("inetsrch") || name == QLatin1String("finish");
    const bool searching = name == QLatin1String("searching");
    auto *row = new QWidget(m_balloon);
    auto *buttons = new QHBoxLayout(row);
    buttons->setContentsMargins(0, 8, 0, 0);
    buttons->setSpacing(6);
    buttons->addStretch(1);
    if (!m_history.isEmpty() && !searching && name != QLatin1String("lclsrch") && name != QLatin1String("lclrfine")) {
        auto *back = new QPushButton(srchString(STRING_BACK, "&Back"), row);
        connect(back, &QPushButton::clicked, this, &SearchCompanion::goBack);
        buttons->addWidget(back);
    }
    if (form || web) {
        auto *search = new QPushButton(srchString(STRING_SEARCH, "Sea&rch"), row);
        search->setDefault(true);
        connect(search, &QPushButton::clicked, this, &SearchCompanion::submit);
        buttons->addWidget(search);
    }
    if (buttons->count() > 1)
        m_content->addWidget(row);
    else
        delete row;
    m_content->addStretch(1);

    const QString animation = root->attributes.value(QStringLiteral("Animation"));
    if (!animation.isEmpty())
        m_agent->play(animation);
    m_scroll->verticalScrollBar()->setValue(0);
    QMetaObject::invokeMethod(this, &SearchCompanion::placeCharacter, Qt::QueuedConnection);
}

void SearchCompanion::addNode(const Node &node, QVBoxLayout *layout)
{
    if (node.tag == QLatin1String("Link") && (HIDDEN_LINKS.contains(node.id()) || (node.properties.contains(QStringLiteral("RefineCondition")) && !condition(node.properties.value(QStringLiteral("RefineCondition"))))))
        return;
    // A margin can be negative: it takes from the one before.
    m_margin += node.attributes.value(QStringLiteral("TopMargin")).toInt();
    if (node.tag != QLatin1String("Balloon") && node.tag != QLatin1String("Bar")) {
        if (m_margin > 0)
            layout->addSpacing(m_margin);
        m_margin = 0;
    }
    if (node.tag == QLatin1String("Balloon") || node.tag == QLatin1String("Bar")) {
        if (node.tag == QLatin1String(m_character ? "Balloon" : "Bar"))
            for (const Node &child : node.children)
                addNode(child, layout);
    } else if (node.tag == QLatin1String("Title") || node.tag == QLatin1String("Label")) {
        auto *label = new QLabel(m_balloon);
        label->setWordWrap(true);
        label->setTextFormat(Qt::RichText);
        QString text = node.text;
        if (node.tag == QLatin1String("Title")) {
            // Only the step's first title says what was found.
            if (!m_titleOverride.isEmpty()) {
                text = m_titleOverride;
                m_titleOverride.clear();
            }
            QFont font = label->font();
            font.setBold(true);
            label->setFont(font);
        }
        label->setText(linkHtml(text, false));
        label->setVisible(!node.attributes.contains(QStringLiteral("Hidden")));
        layout->addWidget(label);
        m_controls.append({node, label});
    } else if (node.tag == QLatin1String("Link")) {
        addLink(node, layout);
    } else if (node.tag == QLatin1String("Expando")) {
        // Its group opens under it.
        const int id = node.id();
        auto *row = new LinkRow(QIcon(), node.text, [this, id] {
            if (QWidget *group = m_expanders.value(id)) {
                group->setVisible(!group->isVisible());
                QMetaObject::invokeMethod(this, &SearchCompanion::placeCharacter, Qt::QueuedConnection);
            }
        }, m_balloon);
        layout->addWidget(row);
    } else if (node.tag == QLatin1String("ExpandoGroup")) {
        auto *group = new QWidget(m_balloon);
        auto *groupLayout = new QVBoxLayout(group);
        groupLayout->setContentsMargins(0, 0, 0, 0);
        groupLayout->setSpacing(0);
        for (const Node &child : node.children)
            addNode(child, groupLayout);
        group->hide();
        layout->addWidget(group);
        m_expanders.insert(node.id(), group);
    } else if (node.tag == QLatin1String("CtrlRow")) {
        auto *row = new QWidget(m_balloon);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);
        for (const Node &child : node.children) {
            const int width = child.attributes.value(QStringLiteral("Width")).toInt();
            if (child.tag == QLatin1String("Label") && !width)
                continue;    // XP's label for screen readers
            if (child.tag == QLatin1String("Label")) {
                auto *label = new QLabel(child.text, row);
                label->setFixedWidth(width);
                rowLayout->addWidget(label);
            } else if (QWidget *control = controlFor(child)) {
                control->setParent(row);
                rowLayout->addWidget(control);
            }
        }
        rowLayout->addStretch(1);
        layout->addWidget(row);
    } else if (QWidget *control = controlFor(node)) {
        control->setParent(m_balloon);
        layout->addWidget(control);
    }
    m_margin += node.attributes.value(QStringLiteral("BottomMargin")).toInt();
}

void SearchCompanion::addLink(const Node &node, QVBoxLayout *layout)
{
    const int id = node.id();
    const int icon = node.attributes.value(QStringLiteral("IconId")).toInt();
    layout->addWidget(new LinkRow(stepIcon(icon), node.text, [this, id] { activate(id); }, m_balloon));
}

QWidget *SearchCompanion::controlFor(const Node &node)
{
    const QString tag = node.tag, type = node.attributes.value(QStringLiteral("Class"));
    const int id = node.id();
    QWidget *widget = nullptr;
    if (tag == QLatin1String("Radio")) {
        auto *radio = new QRadioButton(node.text, m_balloon);
        radio->setChecked(node.attributes.value(QStringLiteral("Checked")) == QLatin1String("1"));
        widget = radio;
    } else if (tag == QLatin1String("Checkbox")) {
        auto *check = new QCheckBox(node.text, m_balloon);
        check->setChecked(node.attributes.value(QStringLiteral("Checked")) == QLatin1String("1"));
        widget = check;
    } else if (tag == QLatin1String("SpellEdit")) {
        auto *edit = new QTextEdit(m_balloon);
        edit->setAcceptRichText(false);
        edit->setTabChangesFocus(true);
        edit->setFixedHeight(node.attributes.value(QStringLiteral("Height")).toInt());
        edit->setPlainText(m_webText);
        widget = edit;
    } else if (tag == QLatin1String("Edit") || type == QLatin1String("Edit")) {
        auto *edit = new QLineEdit(m_balloon);
        connect(edit, &QLineEdit::returnPressed, this, &SearchCompanion::submit);
        if (id == EDIT_SIZE)
            edit->setFixedWidth(node.attributes.value(QStringLiteral("Width")).toInt());
        edit->setVisible(!node.attributes.contains(QStringLiteral("Hidden")));
        widget = edit;
    } else if (type == QLatin1String("ComboBoxEx32") && id == COMBO_LOOK_IN) {
        auto *combo = new QComboBox(m_balloon);
        const auto add = [combo](const QString &path) {
            if (!path.isEmpty() && combo->findData(path) < 0)
                combo->addItem(QDir::toNativeSeparators(path), path);
        };
        add(m_currentFolder);
        add(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
        add(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
        add(QDir::homePath());
        add(QStringLiteral("/"));
        combo->addItem(srchString(STRING_BROWSE, "Browse..."));
        connect(combo, QOverload<int>::of(&QComboBox::activated), this, [this, combo](int index) {
            if (!combo->itemData(index).isNull())
                return;
            const QString folder = QFileDialog::getExistingDirectory(this, srchString(5011, "Choose directory to search"), QDir::homePath());
            if (!folder.isEmpty()) {
                combo->insertItem(0, folder, folder);
                combo->setCurrentIndex(0);
            } else {
                combo->setCurrentIndex(0);
            }
        });
        widget = combo;
    } else if (type == QLatin1String("ComboBoxEx32")) {
        auto *combo = new QComboBox(m_balloon);
        combo->setEditable(true);
        combo->addItem(node.text);
        widget = combo;
    } else if (type == QLatin1String("ComboBox")) {
        auto *combo = new QComboBox(m_balloon);
        for (const auto &item : node.items)
            combo->addItem(item.first, item.second);
        widget = combo;
    } else if (type == QLatin1String("SysDateTimePick32")) {
        auto *date = new QDateEdit(QDate::currentDate(), m_balloon);
        date->setCalendarPopup(true);
        widget = date;
    } else if (tag == QLatin1String("ListBox")) {
        auto *list = new QListWidget(m_balloon);
        const QSettings settings(settingsPath(), QSettings::IniFormat);
        const QString current = engine();
        const auto add = [&](const QString &name, const QString &address) {
            auto *item = new QListWidgetItem(name, list);
            item->setData(Qt::UserRole, address);
            if (address == current)
                list->setCurrentItem(item);
        };
        add(QStringLiteral("Bing"), QLatin1String(ENGINE_BING));
        for (const auto &item : node.items)
            if (LIVE_ENGINES.contains(item.first))
                add(item.first, QString(item.second).replace(QStringLiteral("http://"), QStringLiteral("https://")));
        connect(list, &QListWidget::currentItemChanged, this, [](QListWidgetItem *item) {
            if (item) {
                QSettings write(settingsPath(), QSettings::IniFormat);
                write.setValue(QStringLiteral("searchEngine"), item->data(Qt::UserRole));
            }
        });
        list->setFixedHeight(4 * list->fontMetrics().height() + 8);
        widget = list;
    } else if (tag == QLatin1String("Button")) {
        auto *button = new QPushButton(node.text, m_balloon);
        widget = button;
    }
    // A long path stays inside the panel.
    if (auto *combo = qobject_cast<QComboBox *>(widget)) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(8);
    }
    if (widget)
        m_controls.append({node, widget});
    return widget;
}

bool SearchCompanion::condition(const QString &text) const
{
    const auto flag = [&](const QString &name) {
        if (name == QLatin1String("ManyResults")) return m_found > 100;
        if (name == QLatin1String("FewResults")) return m_found <= 100;
        if (name == QLatin1String("FileNameSearch")) return !m_query.name.isEmpty();
        if (name == QLatin1String("KeywordsSearch")) return !m_query.text.isEmpty();
        if (name == QLatin1String("LookInSearch")) return !m_query.folder.isEmpty();
        if (name == QLatin1String("HiddenSearch")) return m_query.hidden;
        if (name == QLatin1String("SystemSearch")) return m_query.system;
        if (name == QLatin1String("SizeSearch")) return m_query.minSize >= 0 || m_query.maxSize >= 0;
        if (name == QLatin1String("DateSearch")) return m_query.after.isValid() || m_query.before.isValid();
        if (name == QLatin1String("PictureSearch")) return m_query.step == QLatin1String("lclmm") && m_query.extensions.contains(QStringLiteral("jpg"));
        if (name == QLatin1String("SoundSearch")) return m_query.step == QLatin1String("lclmm") && m_query.extensions.contains(QStringLiteral("mp3"));
        if (name == QLatin1String("MovieSearch")) return m_query.step == QLatin1String("lclmm") && m_query.extensions.contains(QStringLiteral("avi"));
        if (name == QLatin1String("MediaSearch")) return m_query.step == QLatin1String("lclmm");
        if (name == QLatin1String("InetActive")) return true;
        return false;
    };
    // OR of ANDs, as the steps write them
    const QStringList alternatives = text.split(QStringLiteral(" OR "));
    for (const QString &alternative : alternatives) {
        bool all = true;
        for (QString term : alternative.split(QStringLiteral(" AND "))) {
            term = term.trimmed();
            const bool negated = term.startsWith(QLatin1String("NOT "));
            if (flag(negated ? term.mid(4).trimmed() : term) == negated)
                all = false;
        }
        if (all)
            return true;
    }
    return false;
}

QString SearchCompanion::engine() const
{
    const QSettings settings(settingsPath(), QSettings::IniFormat);
    return settings.value(QStringLiteral("searchEngine"), QLatin1String(ENGINE_BING)).toString();
}

void SearchCompanion::goBack()
{
    if (m_history.isEmpty())
        return;
    m_titleOverride.clear();
    showStep(m_history.takeLast(), false);
}

void SearchCompanion::activate(int id)
{
    m_titleOverride.clear();
    if (id >= LINK_PICTURES_ONLINE && id <= LINK_DIGITAL_ONLINE) {
        submitWeb(m_query.name + QLatin1Char(' ') + m_query.text);
        return;
    }
    if (id >= LINK_REFINE_FIRST && id < LINK_PICTURES_ONLINE && id != LINK_INCLUDE_HIDDEN && id != LINK_WITHOUT_HIDDEN) {
        showStep(m_query.step.isEmpty() ? QStringLiteral("lcladv") : m_query.step);
        return;
    }
    switch (id) {
    case LINK_MEDIA: showStep(QStringLiteral("lclmm")); break;
    case LINK_DOCUMENTS: showStep(QStringLiteral("lcldocs")); break;
    case LINK_ALL_FILES: showStep(QStringLiteral("lcladv")); break;
    case LINK_COMPUTERS: showStep(QStringLiteral("lclcomp")); break;
    case LINK_INTERNET: showStep(QStringLiteral("inetsrch")); break;
    case LINK_PREFERENCES: showStep(QStringLiteral("inetopts")); break;
    case LINK_CHANGE_FILES: showStep(QStringLiteral("lclmode")); break;
    case LINK_CHANGE_INTERNET: showStep(QStringLiteral("inetpref")); break;
    case LINK_SEARCH_COMPUTER:
    case LINK_NEW_SEARCH:
    case LINK_FINISHED:
        m_webText.clear();
        restart();
        break;
    case LINK_CHARACTER:
        m_character = !m_character;
        characterChanged();
        break;
    case LINK_SAMPLE:
        submitWeb(tr("Buy a book online"));
        break;
    case LINK_HIGHLIGHT:
        emit highlightWeb(m_webText);
        break;
    case LINK_MORE_ENGINES:
        emit searchWeb(m_webText, QStringLiteral("https://www.google.com/search?q="));
        break;
    case LINK_INCLUDE_HIDDEN:
    case LINK_WITHOUT_HIDDEN:
        m_query.hidden = m_query.system = id == LINK_INCLUDE_HIDDEN;
        m_agent->play(QStringLiteral("Searching"));
        showStep(QStringLiteral("searching"), false);
        emit searchFiles(m_query);
        break;
    case LINK_HELP:
        for (const char *program : {"khelpcenter", "yelp"})
            if (const QString found = QStandardPaths::findExecutable(QLatin1String(program)); !found.isEmpty()) {
                QProcess::startDetached(found, {});
                break;
            }
        break;
    default:
        break;
    }
}

void SearchCompanion::characterChanged()
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("searchCharacter"), m_character);
    m_agent->setVisible(m_character && m_agent->loaded());
    showStep(m_step, false);
    placeCharacter();
}

SearchQuery SearchCompanion::buildQuery() const
{
    SearchQuery query;
    query.step = m_step;
    query.folder = m_currentFolder;
    bool namedByContent = m_step == QLatin1String("lclmm");
    bool sizeChosen = false, datesChosen = false;
    const auto apply = [&](const QList<QPair<QString, QString>> &props) {
        for (const auto &prop : props) {
            const bool on = prop.second.compare(QLatin1String("True"), Qt::CaseInsensitive) == 0 || prop.second == QLatin1String("1");
            if (prop.first == QLatin1String("FileType")) {
                for (QString extension : prop.second.split(QLatin1Char(';'), Qt::SkipEmptyParts))
                    query.extensions.append(extension.remove(QLatin1Char('.')).toLower());
                query.byType = true;
            } else if (prop.first == QLatin1String("SearchHidden")) {
                query.hidden = on;
            } else if (prop.first == QLatin1String("SearchSystemDirs")) {
                query.system = on;
            } else if (prop.first == QLatin1String("IncludeSubFolders")) {
                query.subfolders = on;
            } else if (prop.first == QLatin1String("CaseSensitive")) {
                query.caseSensitive = on;
            } else if (prop.first == QLatin1String("DateNDays")) {
                query.after = QDateTime::currentDateTime().addDays(-prop.second.toInt());
            } else if (prop.first == QLatin1String("DateNMonths")) {
                query.after = QDateTime::currentDateTime().addMonths(-prop.second.toInt());
            } else if (prop.first == QLatin1String("WhichDate")) {
                query.whichDate = prop.second.toInt();
            } else if (prop.first == QLatin1String("SizeLE")) {
                query.maxSize = prop.second.toLongLong();
            } else if (prop.first == QLatin1String("SizeGE")) {
                query.minSize = prop.second.toLongLong();
            }
        }
    };
    // Off, these search the way a step's plain form does.
    bool advanced = false;
    for (const Control &control : m_controls)
        if (control.node.id() == 5631)
            advanced = true;
    if (advanced) {
        query.hidden = false;
        query.system = false;
        query.subfolders = false;
    }
    for (const Control &control : m_controls) {
        const int id = control.node.id();
        if (auto *radio = qobject_cast<QRadioButton *>(control.widget)) {
            if (radio->isChecked()) {
                apply(control.node.searchProps);
                sizeChosen = sizeChosen || id == RADIO_SIZE;
                datesChosen = datesChosen || id == RADIO_DATES;
            }
        } else if (auto *check = qobject_cast<QCheckBox *>(control.widget)) {
            if (check->isChecked())
                apply(control.node.searchProps);
        } else if (qobject_cast<QLabel *>(control.widget)) {
            apply(control.node.searchProps);
        } else if (auto *edit = qobject_cast<QLineEdit *>(control.widget)) {
            if (id == EDIT_NAME || (id == EDIT_CONTENT && namedByContent)) {
                if (!edit->text().trimmed().isEmpty())
                    query.name = edit->text().trimmed();
            } else if (id == EDIT_CONTENT) {
                query.text = edit->text();
            } else if (control.node.attributes.value(QStringLiteral("Id")).toInt() == 5647) {
                query.name = edit->text().trimmed();
            }
        } else if (auto *combo = qobject_cast<QComboBox *>(control.widget)) {
            if (id == COMBO_LOOK_IN && !combo->currentData().isNull())
                query.folder = combo->currentData().toString();
            else if (id == COMBO_WHICH_DATE)
                query.whichDate = combo->currentData().toInt();
            else if (id == 5617 && combo->currentText() != QLatin1String("*.*")) {
                for (QString extension : combo->currentText().split(QLatin1Char(';'), Qt::SkipEmptyParts))
                    query.extensions.append(extension.remove(QStringLiteral("*.")).toLower());
            }
        }
    }
    for (const Control &control : m_controls) {
        const int id = control.node.id();
        if (auto *date = qobject_cast<QDateEdit *>(control.widget); date && datesChosen) {
            if (id == DATE_FROM)
                query.after = QDateTime(date->date(), QTime(0, 0));
            else if (id == DATE_TO)
                query.before = QDateTime(date->date(), QTime(23, 59, 59));
        }
        if (auto *edit = qobject_cast<QLineEdit *>(control.widget); edit && sizeChosen && id == EDIT_SIZE) {
            bool atLeast = true;
            for (const Control &other : m_controls)
                if (other.node.id() == COMBO_SIZE)
                    if (auto *combo = qobject_cast<QComboBox *>(other.widget))
                        atLeast = combo->currentIndex() == 0;
            (atLeast ? query.minSize : query.maxSize) = edit->text().toLongLong() * 1024;
        }
    }
    return query;
}

void SearchCompanion::submitWeb(const QString &text)
{
    m_webText = text.trimmed();
    if (m_webText.isEmpty())
        return;
    m_titleOverride = tr("Results for %1 are displayed. Click an option below to see other results.").arg(m_webText);
    if (const auto root = loadStep(QStringLiteral("finish")))
        for (const Node &child : root->children)
            if (child.tag == QLatin1String("Title") && child.properties.contains(QStringLiteral("TitleTemplate")))
                m_titleOverride = child.properties.value(QStringLiteral("TitleTemplate")).arg(m_webText);
    showStep(QStringLiteral("finish"));
    m_agent->play(QStringLiteral("Searching"));
    emit searchWeb(m_webText, engine());
}

void SearchCompanion::submit()
{
    if (m_step == QLatin1String("inetsrch") || m_step == QLatin1String("finish")) {
        for (const Control &control : m_controls)
            if (control.node.id() == EDIT_QUESTION)
                if (auto *edit = qobject_cast<QTextEdit *>(control.widget)) {
                    submitWeb(edit->toPlainText());
                    return;
                }
        return;
    }
    if (m_step != QLatin1String("lclmm") && m_step != QLatin1String("lcldocs") && m_step != QLatin1String("lcladv") && m_step != QLatin1String("lclcomp"))
        return;
    m_query = buildQuery();
    m_history.append(m_step);
    m_step.clear();
    // While it runs: the character at work, and a way to stop
    m_titleOverride = tr("Search Companion is searching...");
    while (QLayoutItem *item = m_content->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    m_controls.clear();
    auto *label = new QLabel(m_titleOverride, m_balloon);
    label->setWordWrap(true);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    m_content->addWidget(label);
    auto *row = new QWidget(m_balloon);
    auto *buttons = new QHBoxLayout(row);
    buttons->setContentsMargins(0, 8, 0, 0);
    buttons->addStretch(1);
    auto *stop = new QPushButton(srchString(STRING_STOP, "&Stop"), row);
    connect(stop, &QPushButton::clicked, this, &SearchCompanion::stopSearch);
    buttons->addWidget(stop);
    m_content->addWidget(row);
    m_content->addStretch(1);
    m_titleOverride.clear();
    m_agent->play(m_query.byType && m_query.step == QLatin1String("lclmm") ? QStringLiteral("ImageSearching") : QStringLiteral("Searching"));
    placeCharacter();
    emit searchFiles(m_query);
}

void SearchCompanion::searchFinished(int count)
{
    m_found = count;
    m_agent->play(count ? QStringLiteral("CharacterSucceeds") : QStringLiteral("Embarrassed"));
    QString title = count == 1 ? tr("There was one file found.  Is it what you wanted?")
        : count ? tr("There were %1 files found. Did you find what you wanted?").arg(count)
                : tr("There were no files found.  Do you want to quit searching?");
    if (const auto root = loadStep(QStringLiteral("lclrfine")))
        for (const Node &child : root->children)
            if (child.tag == QLatin1String("Title") && child.properties.contains(QStringLiteral("TitleTemplate"))) {
                title = count == 1 ? child.properties.value(QStringLiteral("OneTitleTemplate"))
                    : count ? child.properties.value(QStringLiteral("TitleTemplate")).arg(count)
                            : child.properties.value(QStringLiteral("EmptyTitleTemplate"));
                break;
            }
    m_titleOverride = title;
    m_step.clear();
    showStep(QStringLiteral("lclrfine"), false);
    m_titleOverride.clear();
}
