// SPDX-License-Identifier: LGPL-2.1-or-later
// Windows Security Center (wscui.cpl) as XP SP2 lays it out: the blue banner, the
// Resources pane, the three essentials in their coloured boxes, "Manage security
// settings for:" and the privacy bar. Its pictures are wscui.cpl's own PNG resources, its
// words xpsp2res.dll's; its measures and colours come from XP's window captured in the
// Winedows session.
#include "securitycenter.h"

#include "controlpanel.h"
#include "theme.h"
#include "xpfiles.h"

#include <QDesktopServices>
#include <QDialog>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QProxyStyle>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QUrl>

namespace securitycenter {
namespace {

// xpsp2res.dll's words
const int STRING_FIREWALL = 17020, STRING_UPDATES = 17021, STRING_VIRUS = 17022, STRING_ON = 17028, STRING_OFF = 17029,
          STRING_RESOURCES = 17037, STRING_UPDATES_ON = 17047, STRING_UPDATES_TURNED_OFF = 17048, STRING_VIRUS_HELPS = 17053,
          STRING_NO_FIREWALL_FOUND = 17059, STRING_FIREWALL_ON = 17061, STRING_FIREWALL_MONITORED = 17044,
          STRING_VIRUS_UNWATCHED = 17073, STRING_INTRO = 17079, STRING_MANAGE = 17081, STRING_ESSENTIALS = 17024,
          STRING_RECOMMENDATIONS = 17110, STRING_NOT_MONITORED = 17111, STRING_HELP_PROTECT = 17125, STRING_WHATS_NEW = 17128,
          STRING_TITLE = 17153, STRING_INTERNET_OPTIONS = 17154, STRING_PRIVACY = 17095, STRING_WINDOWS_FIREWALL = 12122,
          STRING_SECURITY_CENTER = 6103;
// The Resources pane's links, in XP's order
const int RESOURCES[] = {17026, 17027, 17033, 17096, 17036};
// wscui.cpl's PNG resources
const int PNG_HELP = 100, PNG_EXPAND = 101, PNG_COLLAPSE = 102, PNG_FIREWALL = 103, PNG_UPDATES = 104, PNG_VIRUS = 105,
          PNG_DOT_OFF = 106, PNG_DOT_UNKNOWN = 107, PNG_DOT_ON = 108, PNG_BANNER = 109;
// The "Manage security settings for:" icons: inetcpl.cpl's, wuaucpl.cpl's and xpsp2res.dll's Windows Firewall
const int ICON_INTERNET_OPTIONS = 4487, ICON_UPDATES = 1, ICON_WINDOWS_FIREWALL = 10605;

// Fonts, as xpsp2res.dll names them (17137 and on)
const char BODY_FONT[] = "Tahoma, 8", HEADING_FONT[] = "Trebuchet MS, 13.5", TITLE_FONT[] = "Trebuchet MS, 19",
           SUBTITLE_FONT[] = "Trebuchet MS, 11", PANE_FONT[] = "Trebuchet MS, 10.5, bold", MANAGE_FONT[] = "Tahoma, 10, bold",
           BAR_FONT[] = "Tahoma, 8";

// Measured at 96 DPI on XP's window (client coordinates). Its first size:
const int WIDTH = 742, HEIGHT = 566;
// The banner and the band under it, the left pane, the privacy bar
const int BANNER = 63, BAND = 4, PANE = 230, BAR = 22;
const QColor BANNER_TOP(1, 52, 205), BANNER_BOTTOM(50, 101, 254), BAND_EDGE(116, 161, 255), BAND_MIDDLE(210, 224, 255);
const QColor PANE_COLOUR(239, 243, 254), BAR_COLOUR(3, 66, 198);
const QColor FOCUS(14, 3, 38), TEXT(30, 57, 98), LINK(33, 24, 213), BULLET(23, 57, 121), PANE_TITLE(99, 99, 102), BOX_BORDER(121, 158, 218);
// The Resources box: its place in the pane, its header's rows, its links
const QRect RESOURCES_BOX(16, 91, 204, 240);
const int RESOURCES_HEADER = 22, RESOURCES_LINK_X = 38, RESOURCES_BULLET_X = 28, RESOURCES_FIRST = 145, RESOURCES_WIDTH = 168;
// The box's corners: where each row starts, from the corner's edge row in
const int BOX_CORNER[] = {9, 6, 4, 3, 2, 1, 1, 1};
// The essentials' boxes: the top rows start 3, 1, 1 in, the bottom ones 4, 1, 1
const int SECTION_TOP[] = {3, 1, 1}, SECTION_BOTTOM[] = {4, 1, 1};
// In the page (the scrolled part, right of the pane): the margin, the heading's baseline, the text's
const int PAGE_X = 10, PAGE_WIDTH = 475, HEADING_BASELINE = 34, INTRO_BASELINE = 50, LINE = 13, SECTIONS_TOP = 111, SECTION_GAP = 9;
// In a box: its header, its icon's centre, title, status, separator and chevron; its text and button
const int HEADER = 38, ICON_CENTRE_X = 21, TITLE_X = 40, TITLE_BASELINE = 23, STATUS_RIGHT = 435, DOT_GAP = 27, SEPARATOR_X = 443,
          CHEVRON_X = 451, BODY_X = 12, BODY_BASELINE = 54, BODY_WIDTH = 450, BUTTON_X = 347, BUTTON_GAP = 9, BUTTON_WIDTH = 117,
          BUTTON_HEIGHT = 21, BUTTON_BOTTOM = 6;
// "Manage security settings for:": its baseline under the last box, its icons' row, their links
const int MANAGE_BASELINE = 27, MANAGE_ICONS = 20, MANAGE_ICON_X = 0, MANAGE_STEP = 175, MANAGE_TEXT_X = 41, MANAGE_TEXT_BASELINE = 21, PAGE_BOTTOM = 12;

struct Look
{
    QColor topLeft, topRight, bottomLeft, bottomRight, border, body, title, word;
    int dot;
};
const Look ON_LOOK{{196, 214, 255}, {217, 228, 255}, {215, 227, 255}, {237, 241, 255}, {189, 202, 234}, {239, 243, 255}, {48, 72, 110}, {4, 124, 5}, PNG_DOT_ON};
const Look OFF_LOOK{{204, 54, 2}, {229, 150, 117}, {210, 77, 29}, {235, 171, 143}, {231, 197, 151}, {255, 245, 231}, Qt::white, {186, 75, 20}, PNG_DOT_OFF};
const Look UNKNOWN_LOOK{{241, 199, 21}, {247, 221, 123}, {242, 205, 48}, {249, 228, 151}, {231, 197, 151}, {255, 245, 227}, {112, 92, 51}, {112, 92, 51}, PNG_DOT_UNKNOWN};

ShellTheme *theme()
{
    return ShellTheme::instance();
}

int scaled(int pixels)
{
    return theme()->scaled(pixels);
}

QFont xpFont(const char *spec)
{
    return theme()->font(QLatin1String(spec));
}

QImage png(int id)
{
    static const auto wscui = xpfiles::resources(xpfiles::find(QStringLiteral("wscui.cpl")));
    static QHash<int, QImage> cache;
    if (!cache.contains(id))
        cache.insert(id, wscui ? QImage::fromData(wscui->find(QStringLiteral("PNG"), QString::number(id))) : QImage());
    return cache.value(id);
}

QImage fileIcon(const char *file, int group, int size)
{
    const auto resources = xpfiles::resources(xpfiles::find(QLatin1String(file)));
    return resources ? xpfiles::icon(*resources, group, size) : QImage();
}

bool installed(const char *program)
{
    return !QStandardPaths::findExecutable(QLatin1String(program)).isEmpty();
}

// The first of the programs the system has, started; false if it has none
bool startFirst(std::initializer_list<QStringList> commands)
{
    for (const QStringList &command : commands)
        if (installed(command.first().toUtf8().constData()))
            return QProcess::startDetached(command.first(), command.mid(1));
    return false;
}

void openFirewallSettings()
{
    startFirst({{QStringLiteral("firewall-config")}, {QStringLiteral("gufw")}});
}

// The distribution's updates, where Windows Update would be
void openUpdates()
{
    startFirst({{QStringLiteral("pamac-manager"), QStringLiteral("--updates")}, {QStringLiteral("plasma-discover"), QStringLiteral("--mode"), QStringLiteral("update")},
                {QStringLiteral("gnome-software"), QStringLiteral("--mode=updates")}, {QStringLiteral("update-manager")}});
}

bool hasUpdates()
{
    for (const char *program : {"pamac-manager", "plasma-discover", "gnome-software", "update-manager"})
        if (installed(program))
            return true;
    return false;
}

// A string's text and its <A> links: (text, link) pieces; "\r\n" kept as line breaks
struct Piece { QString text; QString href; bool link = false; };
QList<Piece> pieces(const QString &source)
{
    QList<Piece> out;
    static const QRegularExpression anchor(QStringLiteral("<A([^>]*)>(.*?)</A>"), QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression href(QStringLiteral("HREF=\"([^\"]*)\""), QRegularExpression::CaseInsensitiveOption);
    QString text = source;
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    int at = 0;
    auto matches = anchor.globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        if (match.capturedStart() > at)
            out.append({text.mid(at, match.capturedStart() - at), {}, false});
        out.append({match.captured(2), href.match(match.captured(1)).captured(1), true});
        at = match.capturedEnd();
    }
    if (at < text.size())
        out.append({text.mid(at), {}, false});
    return out;
}

// A run of words on one line, its link if it is one
struct Run { QPoint baseline; QString text; int link = -1; int width = 0; };

// Words wrapped as DrawText's DT_WORDBREAK does: at spaces, the trailing one not counted.
// LINKS gets each link's target; their runs point to it.
QList<Run> wrap(const QList<Piece> &text, const QFont &font, int x, int baseline, int width, int lineHeight, QList<QString> *links, int *lastBaseline)
{
    const QFontMetrics metrics(font);
    QList<Run> runs;
    int lineX = 0;
    int y = baseline;
    for (const Piece &piece : text) {
        int link = -1;
        if (piece.link && links) {
            links->append(piece.href);
            link = int(links->size()) - 1;
        }
        const QStringList paragraphs = piece.text.split(QLatin1Char('\n'));
        for (int p = 0; p < paragraphs.size(); ++p) {
            if (p > 0) {
                y += lineHeight;
                lineX = 0;
            }
            static const QRegularExpression words(QStringLiteral("\\S+\\s*"));
            auto matches = words.globalMatch(paragraphs[p]);
            // Leading spaces of a piece (after a link) count as they are.
            if (paragraphs[p].startsWith(QLatin1Char(' ')) && lineX > 0)
                lineX += metrics.horizontalAdvance(QLatin1Char(' '));
            while (matches.hasNext()) {
                const QString word = matches.next().captured();
                const int ink = metrics.horizontalAdvance(word.trimmed());
                if (lineX > 0 && lineX + ink > width) {
                    y += lineHeight;
                    lineX = 0;
                }
                // Each word its own run: a link's are underlined one by one, with their spaces.
                Run run{QPoint(x + lineX, y), word, link, metrics.horizontalAdvance(word)};
                if (!runs.isEmpty() && runs.last().link == link && runs.last().baseline.y() == y) {
                    runs.last().text += word;
                    runs.last().width += run.width;
                } else {
                    runs.append(run);
                }
                lineX += run.width;
            }
        }
    }
    if (lastBaseline)
        *lastBaseline = y;
    return runs;
}

void drawRuns(QPainter &painter, const QList<Run> &runs, const QFont &font, const QColor &text, const QColor &link, bool underlineLinks, int hot = -1)
{
    const QFontMetrics metrics(font);
    for (const Run &run : runs) {
        const bool underline = run.link >= 0 && (underlineLinks || run.link == hot);
        painter.setFont(font);
        painter.setPen(run.link >= 0 ? link : text);
        // Links underline their words, not the space after the last.
        QString words = run.text;
        while (run.link >= 0 && words.endsWith(QLatin1Char(' ')))
            words.chop(1);
        painter.drawText(run.baseline, words);
        if (underline)
            painter.fillRect(QRect(run.baseline.x(), run.baseline.y() + metrics.underlinePos(), metrics.horizontalAdvance(words), qMax(1, metrics.lineWidth())), link);
    }
}

int runAt(const QList<Run> &runs, const QFont &font, QPoint at)
{
    const QFontMetrics metrics(font);
    for (const Run &run : runs)
        if (run.link >= 0 && QRect(run.baseline.x(), run.baseline.y() - metrics.ascent(), run.width, metrics.height()).contains(at))
            return run.link;
    return -1;
}

// A region of rows, each INSET from both sides by the corner tables at its top and bottom
QRegion rows(const QRect &rect, const int *top, int topRows, const int *bottom, int bottomRows)
{
    QRegion region;
    for (int y = 0; y < rect.height(); ++y) {
        int in = 0;
        if (y < topRows)
            in = scaled(top[y]);
        else if (rect.height() - 1 - y < bottomRows)
            in = scaled(bottom[rect.height() - 1 - y]);
        region += QRect(rect.left() + in, rect.top() + y, rect.width() - 2 * in, 1);
    }
    return region;
}

QRegion inner(const QRegion &region)
{
    return region & region.translated(1, 0) & region.translated(-1, 0) & region.translated(0, 1) & region.translated(0, -1);
}

// A gradient between four corners, interpolated across RECT
void fillBilinear(QPainter &painter, const QRegion &clip, const QRect &rect, const QColor &tl, const QColor &tr, const QColor &bl, const QColor &br)
{
    QImage image(rect.size(), QImage::Format_RGB32);
    const double w = qMax(1, rect.width() - 1), h = qMax(1, rect.height() - 1);
    for (int y = 0; y < rect.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        const double fy = y / h;
        for (int x = 0; x < rect.width(); ++x) {
            const double fx = x / w;
            const auto mix = [&](int a, int b, int c, int d) {
                return qRound(a * (1 - fx) * (1 - fy) + b * fx * (1 - fy) + c * (1 - fx) * fy + d * fx * fy);
            };
            line[x] = qRgb(mix(tl.red(), tr.red(), bl.red(), br.red()), mix(tl.green(), tr.green(), bl.green(), br.green()),
                           mix(tl.blue(), tr.blue(), bl.blue(), br.blue()));
        }
    }
    painter.save();
    painter.setClipRegion(clip);
    painter.drawImage(rect.topLeft(), image);
    painter.restore();
}

// A region filled and framed as FrameRgn frames it
void frame(QPainter &painter, const QRegion &region, const QColor &border)
{
    for (const QRect &r : region - inner(region))
        painter.fillRect(r, border);
}

// The application's style, with the keyboard's cues always shown: the Security Center's
// buttons underline their mnemonics on XP whether or not Alt was pressed.
class CueStyle : public QProxyStyle
{
public:
    int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget, QStyleHintReturn *data) const override
    {
        return hint == SH_UnderlineShortcut ? 1 : QProxyStyle::styleHint(hint, option, widget, data);
    }
};

struct Essential
{
    QString name;
    State state = NotMonitored;
    QString word;
    QList<Piece> text;
    int picture = 0;
    std::function<void()> recommendations;
    bool expanded = false;
    // Laid out
    QRect box;
    QList<Run> runs;
    QPushButton *button = nullptr;
};

class Page : public QWidget
{
public:
    explicit Page(QWidget *parent)
        : QWidget(parent)
    {
        setMouseTracking(true);
        setAutoFillBackground(true);
        QPalette palette = this->palette();
        palette.setColor(QPalette::Window, Qt::white);
        setPalette(palette);
    }

    void setStatus(const Status &status)
    {
        const auto essential = [](const QString &name, State state, const QString &word, const QString &text, int picture, std::function<void()> action) {
            Essential e;
            e.name = name;
            e.state = state;
            e.word = word;
            e.text = pieces(text);
            e.picture = picture;
            e.recommendations = std::move(action);
            // XP opens what is not ON.
            e.expanded = state != On;
            return e;
        };
        const QString on = string(STRING_ON, "ON"), off = string(STRING_OFF, "OFF"), unwatched = string(STRING_NOT_MONITORED, "NOT MONITORED");
        for (Essential &e : m_essentials)
            delete e.button;
        m_essentials.clear();
        QString firewall = status.firewall == On ? string(STRING_FIREWALL_ON, "%1 is currently ON.").replace(QLatin1String("%1"), status.firewallName)
            : status.firewall == Off ? string(STRING_NO_FIREWALL_FOUND, "Windows detects that your computer is not currently protected by a firewall.")
                                     : string(STRING_FIREWALL_MONITORED, "");
        const bool firewallTool = installed("firewall-config") || installed("gufw");
        m_essentials.append(essential(string(STRING_FIREWALL, "Firewall"), status.firewall, status.firewall == On ? on : status.firewall == Off ? off : unwatched,
                                      firewall, PNG_FIREWALL, status.firewall != On && firewallTool ? std::function<void()>(openFirewallSettings) : nullptr));
        QString updates = status.updates == On ? string(STRING_UPDATES_ON, "Automatic Updates is set to the recommended setting.")
            : status.updates == Off ? string(STRING_UPDATES_TURNED_OFF, "Automatic Updates is turned off.")
                                    : QObject::tr("This distribution's updates are not monitored here: keep it up to date with its package manager.");
        // XP's "Click Turn on Automatic Updates" names a button this system's updates do not have.
        updates.remove(QRegularExpression(QStringLiteral(" Click Turn on Automatic Updates[^.]*\\.")));
        m_essentials.append(essential(string(STRING_UPDATES, "Automatic Updates"), status.updates, status.updates == On ? on : status.updates == Off ? off : unwatched,
                                      updates, PNG_UPDATES, status.updates != On && hasUpdates() ? std::function<void()>(openUpdates) : nullptr));
        const QString virus = string(STRING_VIRUS_HELPS, "Antivirus software helps protect your computer against viruses and other security threats.")
            + QStringLiteral("\r\n\r\n") + string(STRING_VIRUS_UNWATCHED, "");
        m_essentials.append(essential(string(STRING_VIRUS, "Virus Protection"), status.virus, status.virus == On ? on : status.virus == Off ? off : unwatched,
                                      virus, PNG_VIRUS, nullptr));
        relayout();
    }

    void relayout()
    {
        const QFont body = xpFont(BODY_FONT);
        m_links.clear();
        int last = 0;
        m_intro = wrap(pieces(string(STRING_INTRO, "")), body, scaled(PAGE_X), scaled(INTRO_BASELINE), scaled(PAGE_WIDTH), scaled(LINE), &m_links, &last);
        m_whatsNew = wrap(pieces(string(STRING_WHATS_NEW, "")), body, scaled(PAGE_X), last + scaled(LINE), scaled(PAGE_WIDTH), scaled(LINE), &m_links, nullptr);
        int y = scaled(SECTIONS_TOP);
        for (Essential &e : m_essentials) {
            int height = scaled(HEADER);
            e.runs.clear();
            if (e.expanded) {
                int lastLine = 0;
                e.runs = wrap(e.text, body, scaled(PAGE_X + BODY_X), y + scaled(BODY_BASELINE), scaled(BODY_WIDTH), scaled(LINE), &m_links, &lastLine);
                // The button's row is kept whether there is a button or not, as on XP.
                const int buttonTop = lastLine + scaled(BUTTON_GAP);
                height = buttonTop + scaled(BUTTON_HEIGHT + BUTTON_BOTTOM) - y + 1;
                // XP has it for whatever is not ON, with or without a program behind it here.
                if (e.state != On) {
                    if (!e.button) {
                        e.button = new QPushButton(string(STRING_RECOMMENDATIONS, "&Recommendations..."), this);
                        static CueStyle *cues = new CueStyle;
                        e.button->setStyle(cues);
                        if (e.recommendations)
                            QObject::connect(e.button, &QPushButton::clicked, this, e.recommendations);
                    }
                    // The style draws a push button's face a pixel in from its sides and two from its top.
                    e.button->setGeometry(QRect(scaled(PAGE_X + BUTTON_X), buttonTop, scaled(BUTTON_WIDTH), scaled(BUTTON_HEIGHT)).adjusted(-1, -2, 1, 0));
                    e.button->show();
                }
            } else if (e.button) {
                e.button->hide();
            }
            e.box = QRect(scaled(PAGE_X), y, scaled(PAGE_WIDTH), height);
            y += height + scaled(SECTION_GAP) - 1;
        }
        m_manageBaseline = y - scaled(SECTION_GAP) + 1 + scaled(MANAGE_BASELINE) - 1;
        // Internet Options has nothing behind it here; the others where the system has their programs
        m_manage.clear();
        const int iconsTop = m_manageBaseline + scaled(MANAGE_ICONS);
        int x = scaled(PAGE_X);
        const auto item = [&](const QString &text, const QImage &icon, std::function<void()> action) {
            m_manage.append({QRect(x + scaled(MANAGE_ICON_X), iconsTop, scaled(32), scaled(32)), icon, text, std::move(action)});
            x += scaled(MANAGE_STEP);
        };
        item(string(STRING_INTERNET_OPTIONS, "Internet Options"), fileIcon("inetcpl.cpl", ICON_INTERNET_OPTIONS, scaled(32)), nullptr);
        item(string(STRING_UPDATES, "Automatic Updates"), fileIcon("wuaucpl.cpl", ICON_UPDATES, scaled(32)), hasUpdates() ? std::function<void()>(openUpdates) : nullptr);
        if (installed("firewall-config") || installed("gufw"))
            item(string(STRING_WINDOWS_FIREWALL, "Windows Firewall"), fileIcon("xpsp2res.dll", ICON_WINDOWS_FIREWALL, scaled(32)), openFirewallSettings);
        setMinimumHeight(iconsTop + scaled(32) + scaled(PAGE_BOTTOM));
        resize(width(), minimumHeight());
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const QFont body = xpFont(BODY_FONT), heading = xpFont(HEADING_FONT);
        painter.setFont(heading);
        painter.setPen(TEXT);
        painter.drawText(QPoint(scaled(PAGE_X), scaled(HEADING_BASELINE)), string(STRING_ESSENTIALS, "Security essentials"));
        drawRuns(painter, m_intro, body, TEXT, LINK, true);
        drawRuns(painter, m_whatsNew, body, TEXT, LINK, true);
        for (const Essential &e : m_essentials)
            paintEssential(painter, e);
        painter.setFont(heading);
        painter.setPen(TEXT);
        painter.drawText(QPoint(scaled(PAGE_X), m_manageBaseline), string(STRING_MANAGE, "Manage security settings for:"));
        const QFont manage = xpFont(MANAGE_FONT);
        for (int i = 0; i < m_manage.size(); ++i) {
            const auto &m = m_manage[i];
            painter.drawImage(m.icon, m.image);
            QFont f = manage;
            f.setUnderline(m_hotManage == i && m.action);
            painter.setFont(f);
            painter.setPen(LINK);
            painter.drawText(QPoint(m.icon.left() + scaled(MANAGE_TEXT_X), m.icon.top() + scaled(MANAGE_TEXT_BASELINE)), m.text);
        }
    }

    void paintEssential(QPainter &painter, const Essential &e) const
    {
        const Look &look = e.state == On ? ON_LOOK : e.state == Off ? OFF_LOOK : UNKNOWN_LOOK;
        const QRegion box = rows(e.box, SECTION_TOP, 3, SECTION_BOTTOM, 3);
        for (const QRect &r : box)
            painter.fillRect(r, look.body);
        frame(painter, box, look.border);
        const QRect headerRect(e.box.left(), e.box.top(), e.box.width(), scaled(HEADER));
        const QRegion header = rows(headerRect, SECTION_TOP, 3, SECTION_BOTTOM, 3);
        fillBilinear(painter, inner(header), headerRect.adjusted(1, 1, -1, -1), look.topLeft, look.topRight, look.bottomLeft, look.bottomRight);
        frame(painter, header, look.border);

        const QImage picture = png(e.picture);
        painter.drawImage(QPoint(headerRect.left() + scaled(ICON_CENTRE_X) - picture.width() / 2, headerRect.top() + (headerRect.height() - picture.height()) / 2), picture);
        painter.setFont(xpFont(HEADING_FONT));
        painter.setPen(look.title);
        painter.drawText(QPoint(headerRect.left() + scaled(TITLE_X), headerRect.top() + scaled(TITLE_BASELINE)), e.name);
        const QFontMetrics metrics(painter.font());
        const int wordLeft = headerRect.left() + scaled(STATUS_RIGHT) - metrics.horizontalAdvance(e.word);
        painter.setPen(look.word);
        painter.drawText(QPoint(wordLeft, headerRect.top() + scaled(TITLE_BASELINE)), e.word);
        const QImage dot = png(look.dot);
        painter.drawImage(QPoint(wordLeft - scaled(DOT_GAP), headerRect.top() + (headerRect.height() - dot.height()) / 2 - 1), dot);
        painter.fillRect(QRect(headerRect.left() + scaled(SEPARATOR_X), headerRect.top() + scaled(14), 1, scaled(16)), look.border);
        const QImage chevron = png(e.expanded ? PNG_COLLAPSE : PNG_EXPAND);
        painter.drawImage(QPoint(headerRect.left() + scaled(CHEVRON_X), headerRect.top() + (headerRect.height() - chevron.height()) / 2), chevron);
        if (e.expanded)
            drawRuns(painter, e.runs, xpFont(BODY_FONT), Qt::black, LINK, true);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const QPoint at = event->position().toPoint();
        int hot = -1;
        for (int i = 0; i < m_manage.size(); ++i)
            if (m_manage[i].action && (m_manage[i].icon.contains(at) || textRect(i).contains(at)))
                hot = i;
        const bool header = headerAt(at) >= 0;
        setCursor(hot >= 0 || header || linkAt(at) >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        if (hot != m_hotManage) {
            m_hotManage = hot;
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton)
            return;
        const QPoint at = event->position().toPoint();
        if (const int i = headerAt(at); i >= 0) {
            m_essentials[i].expanded = !m_essentials[i].expanded;
            relayout();
            return;
        }
        if (m_hotManage >= 0 && m_manage[m_hotManage].action) {
            m_manage[m_hotManage].action();
            return;
        }
        const int link = linkAt(at);
        if (link >= 0 && m_links[link].startsWith(QLatin1String("http")))
            QDesktopServices::openUrl(QUrl(m_links[link]));
    }

private:
    struct Manage { QRect icon; QImage image; QString text; std::function<void()> action; };

    QRect textRect(int i) const
    {
        const QFontMetrics metrics(xpFont(MANAGE_FONT));
        const QRect &icon = m_manage[i].icon;
        return QRect(icon.left() + scaled(MANAGE_TEXT_X), icon.top() + scaled(MANAGE_TEXT_BASELINE) - metrics.ascent(), metrics.horizontalAdvance(m_manage[i].text), metrics.height());
    }

    int headerAt(QPoint at) const
    {
        for (int i = 0; i < m_essentials.size(); ++i)
            if (QRect(m_essentials[i].box.topLeft(), QSize(m_essentials[i].box.width(), scaled(HEADER))).contains(at))
                return i;
        return -1;
    }

    int linkAt(QPoint at) const
    {
        const QFont body = xpFont(BODY_FONT);
        int link = runAt(m_intro, body, at);
        if (link < 0)
            link = runAt(m_whatsNew, body, at);
        for (const Essential &e : m_essentials)
            if (link < 0 && e.expanded)
                link = runAt(e.runs, body, at);
        return link;
    }

    QList<Essential> m_essentials;
    QList<Run> m_intro, m_whatsNew;
    QList<QString> m_links;
    QList<Manage> m_manage;
    int m_manageBaseline = 0, m_hotManage = -1;
};

class Window : public QDialog
{
public:
    Window()
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowFlags(Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
        setWindowTitle(string(STRING_TITLE, "Windows Security Center"));
        setWindowIcon(QIcon(QPixmap::fromImage(fileIcon("wscui.cpl", 200, scaled(16)))));
        setMouseTracking(true);
        m_scroll = new QScrollArea(this);
        m_scroll->setFrameShape(QFrame::NoFrame);
        m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scroll->setWidgetResizable(true);
        m_page = new Page(m_scroll);
        m_scroll->setWidget(m_page);
        resize(scaled(WIDTH), scaled(HEIGHT));
        check(this, [this](const Status &status) { m_page->setStatus(status); });
    }

protected:
    void resizeEvent(QResizeEvent *) override
    {
        m_scroll->setGeometry(scaled(PANE), scaled(BANNER + BAND), width() - scaled(PANE), height() - scaled(BANNER + BAND + BAR));
        layoutResources();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const int w = width();
        // The banner: top to bottom
        for (int y = 0; y < scaled(BANNER); ++y) {
            const double f = double(y) / (scaled(BANNER) - 1);
            painter.fillRect(0, y, w, 1, QColor::fromRgbF(BANNER_TOP.redF() + (BANNER_BOTTOM.redF() - BANNER_TOP.redF()) * f, BANNER_TOP.greenF() + (BANNER_BOTTOM.greenF() - BANNER_TOP.greenF()) * f,
                                                         BANNER_TOP.blueF() + (BANNER_BOTTOM.blueF() - BANNER_TOP.blueF()) * f));
        }
        // The band under it: lightest in the middle
        const double middle = (w - 1) / 2.0;
        for (int x = 0; x < w; ++x) {
            const double f = 1 - qAbs(x - middle) / middle;
            const auto mix = [f](int a, int b) { return qRound(a + (b - a) * f); };
            painter.fillRect(x, scaled(BANNER), 1, scaled(BAND), QColor(mix(BAND_EDGE.red(), BAND_MIDDLE.red()), mix(BAND_EDGE.green(), BAND_MIDDLE.green()), mix(BAND_EDGE.blue(), BAND_MIDDLE.blue())));
        }
        const QImage shield = png(PNG_BANNER);
        painter.drawImage(w - scaled(224), scaled(5), shield);
        painter.setPen(Qt::white);
        painter.setFont(xpFont(TITLE_FONT));
        painter.drawText(QRect(0, 0, w - scaled(14), scaled(37)), Qt::AlignRight | Qt::AlignBottom, string(STRING_SECURITY_CENTER, "Security Center"));
        painter.setFont(xpFont(SUBTITLE_FONT));
        painter.drawText(QRect(0, 0, w - scaled(12), scaled(57)), Qt::AlignRight | Qt::AlignBottom, string(STRING_HELP_PROTECT, "Help protect your PC"));

        // The left pane and its Resources box
        const int top = scaled(BANNER + BAND), bottom = height() - scaled(BAR);
        painter.fillRect(0, top, scaled(PANE), bottom - top, PANE_COLOUR);
        paintResources(painter);

        // The privacy bar
        painter.fillRect(0, bottom, w, scaled(BAR), BAR_COLOUR);
        drawRuns(painter, m_privacy, xpFont(BAR_FONT), Qt::white, Qt::white, true);
    }

    void paintResources(QPainter &painter)
    {
        const QRect box = scaledRect(RESOURCES_BOX).adjusted(0, 0, 0, m_resourcesOpen ? 0 : -(scaledRect(RESOURCES_BOX).height() - scaled(RESOURCES_HEADER)));
        const QRegion region = rows(box, BOX_CORNER, 8, BOX_CORNER, m_resourcesOpen ? 8 : 0);
        for (const QRect &r : region)
            painter.fillRect(r, Qt::white);
        const QRect header(box.left(), box.top(), box.width(), scaled(RESOURCES_HEADER));
        const QRegion headerRegion = rows(header, BOX_CORNER, 8, BOX_CORNER, 0);
        fillBilinear(painter, inner(headerRegion), header.adjusted(1, 1, -1, -1), QColor(202, 217, 254), QColor(222, 231, 254), QColor(214, 225, 254), QColor(233, 239, 254));
        frame(painter, region, BOX_BORDER);
        painter.fillRect(QRect(box.left(), header.bottom(), box.width(), 1) & region.boundingRect(), BOX_BORDER);
        painter.drawImage(box.left() - scaled(6), box.top() - scaled(4), png(PNG_HELP));
        painter.setFont(xpFont(PANE_FONT));
        painter.setPen(PANE_TITLE);
        painter.drawText(QRect(box.left() + scaled(36), box.top() + 1, box.width(), header.height()), Qt::AlignLeft | Qt::AlignVCenter, string(STRING_RESOURCES, "Resources"));
        const QImage chevron = png(m_resourcesOpen ? PNG_COLLAPSE : PNG_EXPAND);
        painter.drawImage(box.right() - scaled(25), box.top() + (header.height() - chevron.height()) / 2, chevron);
        // The keyboard's focus, on the header when the window opens: DrawFocusRect's dots
        if (m_resourcesFocused && isActiveWindow()) {
            const QRect focus = header.adjusted(2, 2, -2, -2);
            for (int x = focus.left(); x <= focus.right(); ++x)
                for (int y : {focus.top(), focus.bottom()})
                    if ((x + y) % 2 == 0)
                        painter.fillRect(x, y, 1, 1, FOCUS);
            for (int y = focus.top() + 1; y < focus.bottom(); ++y)
                for (int x : {focus.left(), focus.right()})
                    if ((x + y) % 2 == 0)
                        painter.fillRect(x, y, 1, 1, FOCUS);
        }
        if (!m_resourcesOpen)
            return;
        const QFont body = xpFont(BODY_FONT);
        for (int i = 0; i < m_bullets.size(); ++i)
            painter.fillRect(QRect(scaled(RESOURCES_BULLET_X), m_bullets[i] - scaled(6), scaled(3), scaled(3)), BULLET);
        drawRuns(painter, m_resources, body, LINK, LINK, false, m_hotResource);
    }

    void changeEvent(QEvent *event) override
    {
        if (event->type() == QEvent::ActivationChange)
            update();
        QDialog::changeEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const int hot = runAt(m_resources, xpFont(BODY_FONT), event->position().toPoint());
        setCursor(hot >= 0 || resourcesHeader().contains(event->position().toPoint()) ? Qt::PointingHandCursor : Qt::ArrowCursor);
        if (hot != m_hotResource) {
            m_hotResource = hot;
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const QPoint at = event->position().toPoint();
        if (resourcesHeader().contains(at)) {
            m_resourcesOpen = !m_resourcesOpen;
            m_resourcesFocused = true;
            update();
            return;
        }
        m_resourcesFocused = false;
        update();
        int link = runAt(m_resources, xpFont(BODY_FONT), at);
        if (link >= 0) {
            const QString target = m_links.value(link);
            if (target == QLatin1String("updates"))
                openUpdates();
            else if (target.startsWith(QLatin1String("http")))
                QDesktopServices::openUrl(QUrl(target));
            return;
        }
        link = runAt(m_privacy, xpFont(BAR_FONT), at);
        if (link >= 0)
            QDesktopServices::openUrl(QUrl(m_links.value(link)));
    }

private:
    QRect scaledRect(const QRect &r) const
    {
        return QRect(scaled(r.x()), scaled(r.y()), scaled(r.width()), scaled(r.height()));
    }

    QRect resourcesHeader() const
    {
        const QRect box = scaledRect(RESOURCES_BOX);
        return QRect(box.left(), box.top(), box.width(), scaled(RESOURCES_HEADER));
    }

    void layoutResources()
    {
        const QFont body = xpFont(BODY_FONT);
        m_links.clear();
        m_resources.clear();
        m_bullets.clear();
        int y = scaled(RESOURCES_FIRST);
        for (int id : RESOURCES) {
            // Each a link of its own; the updates' one opens the system's updates
            QList<Piece> text = pieces(string(id, ""));
            QString label, href;
            for (const Piece &p : text)
                if (p.link) {
                    label = p.text;
                    href = p.href;
                }
            if (label.isEmpty())
                label = text.value(0).text;
            if (id == 17027)
                href = QStringLiteral("updates");
            int last = 0;
            m_bullets.append(y);
            m_resources += wrap({{label.trimmed(), href, true}}, body, scaled(RESOURCES_LINK_X), y, scaled(RESOURCES_WIDTH), scaled(LINE), &m_links, &last);
            y = last + 2 * scaled(LINE);
        }
        int last = 0;
        m_privacy = wrap(pieces(string(STRING_PRIVACY, "")), xpFont(BAR_FONT), scaled(10), height() - scaled(BAR) + scaled(15), width(), scaled(LINE), &m_links, &last);
    }

    QScrollArea *m_scroll;
    Page *m_page;
    QList<Run> m_resources, m_privacy;
    QList<int> m_bullets;
    QList<QString> m_links;
    bool m_resourcesOpen = true, m_resourcesFocused = true;
    int m_hotResource = -1;
};

} // namespace

void showWindow()
{
    (new Window)->show();
}

} // namespace securitycenter
