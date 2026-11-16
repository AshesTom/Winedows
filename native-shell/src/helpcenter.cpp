// SPDX-License-Identifier: LGPL-2.1-or-later
#include "helpcenter.h"

#include "browserbands.h"
#include "fontpolicy.h"
#include "programs.h"
#include "theme.h"
#include "xpfiles.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QRegularExpression>
#include <QScreen>
#include <QToolButton>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWebEngineHistory>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>
#include <QWebEngineView>
#include <QXmlStreamReader>

using namespace bands;

namespace {

// helpctr.exe's 24-pixel pictures of its toolbar (300, hot 301), in HCpan_06.xml's order of IDs
const int STRIP_NORMAL = 300, STRIP_HOT = 301;
const char *const STRIP_IDS[] = {"BACK", "FORWARD", "HOME", "INDEX", "FAVORITES", "HISTORY", "SUPPORT", "OPTIONS"};
// The home page's colours and the search band's (measured on XP SP3): the page, its headings and bullets,
// the band and the line under it
const char PAGE_COLOUR[] = "#6375d6", HEADING_COLOUR[] = "#d6dff5", BULLET_COLOUR[] = "#ccd5f2";
const QColor BAND(0, 51, 153), BAND_LINE(196, 216, 235);
const int BAND_HEIGHT = 64, BAND_LINE_HEIGHT = 3;
// The pchdt_w3.cab files the pages use: the topics' pictures (desktop_icon_01 to 04), the link arrow, the
// search arrow and the title's question mark
const char *const BUCKET_ICONS[] = {"HCimgE40.bmp", "HCimgE41.bmp", "HCimgE42.bmp", "HCimgE43.bmp"};
const char ARROW[] = "HCimgC21.bmp", SEARCH_GO[] = "HCimgC20.bmp", LOGO[] = "HCimgD30.bmp";
const char HOME_PAGE[] = "HCsys_02.htm", TOOLBAR[] = "HCpan_06.xml", DATABASE[] = "HCdat_w3.edb";
// XP SP3's home page: its four groups of topics, by their nodes in the help database (as XP shows them)
const QStringList BUCKETS[] = {
    {"What_s_new_in_Windows_XP", "Music__video__games_and_photos", "Windows_basics", "Protecting_your_PC__security_basics"},
    {"Networking_and_the_Web", "Working_remotely", "System_administration"},
    {"Customizing_your_computer", "Accessibility"},
    {"Printing_and_faxing", "Performance_and_maintenance", "Hardware", "Fixing_a_problem", "Send_your_feedback_to_Microsoft"},
};

QString cabFile(const char *name)
{
    return xpfiles::find(QLatin1String(name));
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// A bitmap of the cab, black taken out as helpcenter:bitmap draws them on the page
QImage keyed(const char *name, int size = 0)
{
    QImage image(cabFile(name));
    if (image.isNull())
        return image;
    image = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
            if ((line[x] & 0xffffff) == 0)
                line[x] = 0;
    }
    return size ? image.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation) : image;
}

// The same as a data: URL, for the pages
QString picture(const char *name, int size = 0)
{
    const QImage image = keyed(name, size);
    if (image.isNull())
        return {};
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
}

// The home page's words: an element's inner HTML and TITLE by its ID, as HCsys_02.htm has them
struct HomeWords
{
    QHash<QString, QString> inner, title;
};

const HomeWords &homeWords()
{
    static HomeWords words = [] {
        HomeWords found;
        const QString html = QString::fromLatin1(readFile(cabFile(HOME_PAGE)));
        // Each element with an ID: its tag's TITLE, and the words up to its first closing tag of its kind
        // (those that hold other DIVs are not words)
        static const QRegularExpression opening(QStringLiteral("<(DIV|ID|SPAN|A)\\s[^>]*?\\bID=(\\w+)[^>]*>"), QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression titled(QStringLiteral("\\bTITLE=\"([^\"]*)\""), QRegularExpression::CaseInsensitiveOption);
        for (auto it = opening.globalMatch(html); it.hasNext();) {
            const auto match = it.next();
            const QString tag = match.captured(1), id = match.captured(2);
            if (const auto title = titled.match(match.captured(0)); title.hasMatch())
                found.title.insert(id, title.captured(1));
            const qsizetype end = html.indexOf(QStringLiteral("</") + tag + QLatin1Char('>'), match.capturedEnd(), Qt::CaseInsensitive);
            if (end < 0)
                continue;
            const QString inner = html.mid(match.capturedEnd(), end - match.capturedEnd()).simplified();
            if (!inner.contains(QLatin1String("<DIV"), Qt::CaseInsensitive))
                found.inner.insert(id, inner);
        }
        return found;
    }();
    return words;
}

QString homeWord(const QString &id, const char *fallback)
{
    const QString text = homeWords().inner.value(id);
    return text.isEmpty() ? QLatin1String(fallback) : text;
}

// A topic of the help database: its node, title and description. HCdat_w3.edb is an ESE database; its
// taxonomy's records hold a node's ID (after a length), its title and its description in UTF-16, one
// after the other, which is all this reads of it.
struct Topic
{
    QString node, title, description;
};

const QList<Topic> &topics()
{
    static QList<Topic> list = [] {
        QList<Topic> found;
        const QByteArray data = readFile(cabFile(DATABASE));
        QStringList runs;
        QString run;
        for (qsizetype at = 0; at + 1 < data.size(); at += 2) {
            const ushort c = uchar(data[at]) | (uchar(data[at + 1]) << 8);
            if (c >= 0x20 && c < 0x7f) {
                run.append(QChar(c));
                continue;
            }
            if (run.size() >= 2)
                runs.append(run);
            run.clear();
        }
        // A node's ID is its title's letters and digits, the rest as underscores (more or less of them)
        static const QRegularExpression notWord(QStringLiteral("[^A-Za-z0-9]"));
        QSet<QString> seen;
        for (int i = 0; i + 1 < runs.size(); ++i) {
            const QString node = runs[i].mid(1), title = runs[i + 1];
            if (node.size() < 3 || !node.contains(QLatin1Char('_')) && node != title || seen.contains(node)
                || QString(node).remove(QLatin1Char('_')) != QString(title).remove(notWord))
                continue;
            seen.insert(node);
            QString description = i + 2 < runs.size() ? runs[i + 2] : QString();
            if (description.contains(QLatin1String("://")) || description.contains(QLatin1Char('%')) || !description.contains(QLatin1Char(' ')))
                description.clear();
            found.append({node, title, description});
        }
        return found;
    }();
    return list;
}

// The UTF-16 words in the database from AT on, at either alignment (its records are not aligned)
QStringList wordsAt(const QByteArray &data, qsizetype at, qsizetype length)
{
    QStringList out;
    for (qsizetype start : {at, at + 1}) {
        QString run;
        for (qsizetype i = start; i + 1 < qMin(data.size(), start + length); i += 2) {
            const ushort c = uchar(data[i]) | (uchar(data[i + 1]) << 8);
            if ((c >= 0x20 && c < 0x7f) || c == 0x2019 || c == 0x2018) {
                run.append(QChar(c));
                continue;
            }
            if (run.size() >= 2)
                out.append(run);
            run.clear();
        }
    }
    return out;
}

Topic topic(const QString &node)
{
    static QHash<QString, Topic> cache;
    if (const auto it = cache.constFind(node); it != cache.constEnd())
        return *it;
    for (const Topic &t : topics())
        if (t.node == node)
            return cache[node] = t;
    // A node's title follows its ID in its record: the first words with the same letters and digits
    static const QByteArray data = readFile(cabFile(DATABASE));
    static const QRegularExpression notWord(QStringLiteral("[^A-Za-z0-9]"));
    const QString letters = QString(node).remove(QLatin1Char('_'));
    const QByteArray pattern(reinterpret_cast<const char *>(node.utf16()), node.size() * 2);
    for (qsizetype at = data.indexOf(pattern); at >= 0; at = data.indexOf(pattern, at + 1))
        for (QString words : wordsAt(data, at + pattern.size(), 1000))
            if (QString(words).remove(notWord) == letters) {
                // (a record's next length can follow the words without a gap)
                while (!words.isEmpty() && !words.back().isLetterOrNumber() && !QStringLiteral(".?)").contains(words.back()))
                    words.chop(1);
                return cache[node] = {node, words, QString()};
            }
    // Some records keep their title away from their ID: any words of the database with those letters
    static const QHash<QString, QString> byLetters = [] {
        QHash<QString, QString> found;
        for (QString words : wordsAt(data, 0, data.size())) {
            while (!words.isEmpty() && !words.back().isLetterOrNumber() && !QStringLiteral(".?)").contains(words.back()))
                words.chop(1);
            const QString key = QString(words).remove(notWord);
            if (key.size() >= 3 && !found.contains(key))
                found.insert(key, words);
        }
        return found;
    }();
    if (const QString title = byLetters.value(letters); !title.isEmpty())
        return cache[node] = {node, title, QString()};
    return cache[node] = {node, QString(node).replace(QLatin1Char('_'), QLatin1Char(' ')).simplified(), QString()};
}

QString escaped(const QString &text)
{
    return text.toHtmlEscaped();
}

// A heading as XP draws it: Arial Bold 22 px (matched pixel by pixel on XP SP3's home page), hinted
// whole by its TrueType instructions and greyed by GDI's Standard smoothing (fontpolicy.h). Chromium
// smooths its own outlines, so the headings are pictures of the shell's text in the page.
QString heading(QString text)
{
    text = QString(text).remove(QRegularExpression(QStringLiteral("<[^>]*>"))).simplified();
    QFont font(QStringLiteral("Arial"));
    font.setBold(true);
    font.setPixelSize(22);
    w4w::xpFontSmoothing(font, 22);
    const QFontMetrics metrics(font);
    QImage image(metrics.horizontalAdvance(text) + 2, metrics.height(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setFont(font);
        w4w::drawGdiText(&painter, image.rect(), Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, text, QColor(QLatin1String(HEADING_COLOUR)));
    }
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QStringLiteral("<img style='display: block' alt='%1' src='data:image/png;base64,%2'>").arg(escaped(text), QString::fromLatin1(png.toBase64()));
}

// The pages' style: Tahoma 8 in white on the home page's blue, as on XP; the headings are pictures (heading())
QString page(const QString &title, const QString &body)
{
    return QStringLiteral(
               "<!DOCTYPE html><html><head><meta charset='utf-8'><title>%1</title><style>"
               "body { margin: 5px; background: %2; color: white; font: 8pt Tahoma; cursor: default; }"
               "h2 { margin: 0; line-height: 0; }"
               "a { color: white; text-decoration: none; } a:hover { text-decoration: underline; }"
               "ul { margin: 0; padding-left: 15px; } li { color: %3; margin: 0 0 5px 0; } li a { font-weight: bold; }"
               ".topics td { padding-bottom: 28px; vertical-align: middle; }"
               ".link { margin: 0 0 16px 4px; } .link img { width: 16px; height: 16px; vertical-align: middle; margin-right: 6px; }"
               "p { margin: 0 0 8px 0; }"
               "</style></head><body>%4</body></html>")
        .arg(escaped(title), QLatin1String(PAGE_COLOUR), QLatin1String(BULLET_COLOUR), body);
}

QString homePage()
{
    const HomeWords &words = homeWords();
    QString left = QStringLiteral("<table class='topics' cellspacing=0 cellpadding=0>");
    for (int bucket = 0; bucket < 4; ++bucket) {
        left += QStringLiteral("<tr><td style='width: 66px; padding-left: 13px'><img src='%1' width=48 height=48></td><td style='padding-right: 12px'><ul>").arg(picture(BUCKET_ICONS[bucket]));
        for (const QString &node : BUCKETS[bucket])
            left += QStringLiteral("<li><a href='hcp://topic/%1'>%2</a></li>").arg(node, escaped(topic(node).title));
        left += QStringLiteral("</ul></td></tr>");
    }
    left += QStringLiteral("</table>");
    const QString arrow = picture(ARROW, 16);
    const auto link = [&](const char *id, const char *fallback, const QString &href) {
        const QString key = QLatin1String(id);
        return QStringLiteral("<div class='link'><a href='%1' title='%2'><img src='%3'>%4</a></div>")
            .arg(href, escaped(words.title.value(key, words.title.value(key + QStringLiteral("_a")))), arrow,
                 homeWord(key + QStringLiteral("_b"), fallback).replace(QLatin1String("<ID"), QLatin1String("<span")).replace(QLatin1String("</ID>"), QLatin1String("</span>")));
    };
    QString right = QStringLiteral("<h2 style='margin-bottom: 14px'>%1</h2>").arg(heading(homeWord(QStringLiteral("locRightTitle"), "Ask for assistance")));
    right += link("center_RA", "Invite a friend to connect to your computer with <B>Remote Assistance</B>", QStringLiteral("hcp://page/remote-assistance"));
    right += link("center_PSS", "Get <B>support</B>, or find information in <B>Windows XP newsgroups</B>", QStringLiteral("hcp://page/support"));
    right += QStringLiteral("<h2 style='margin: 26px 0 14px 0'>%1</h2>").arg(heading(homeWord(QStringLiteral("locRightTitle2"), "Pick a task")));
    right += link("center_UPDATE", "Keep your computer up-to-date with <B>Windows Update</B>", QStringLiteral("hcp://action/windowsupdate"));
    right += link("center_COMPAT", "Find <B>compatible hardware and software</B> for Windows XP", QStringLiteral("hcp://action/compatibility"));
    right += link("center_SR", "Undo changes to your computer with <B>System Restore</B>", QStringLiteral("hcp://page/system-restore"));
    right += link("center_TOOLS", "Use <B>Tools</B> to view your computer information and diagnose problems", QStringLiteral("hcp://page/tools"));
    right += QStringLiteral("<h2 style='margin: 23px 0 16px 0'>%1</h2><div>%2</div>")
                 .arg(heading(homeWord(QStringLiteral("idNews_Title"), "Did you know?")),
                      homeWord(QStringLiteral("locNotConnected"), "").replace(QLatin1String("javascript:OpenConnWizard()"), QLatin1String("hcp://page/new-connection"))
                          .replace(QLatin1String("class='sys-link-homepage sys-font-body-bold'"), QLatin1String("style='font-weight: bold'")));
    const QString body = QStringLiteral("<table width='100%' cellspacing=0 cellpadding=0><tr>"
                                        "<td width='40%' valign=top><h2 style='padding-left: 9px; margin: 3px 0 15px 0'>%1</h2>%2</td>"
                                        "<td width='60%' valign=top style='padding-top: 3px'><div style='margin-left: -6px'>%3</div></td></tr></table>")
                             .arg(heading(homeWord(QStringLiteral("locLeftTitle"), "Pick a Help topic")), left, right);
    return page(QStringLiteral("Help and Support Home Page"), body);
}

QString topicPage(const QString &node)
{
    const Topic t = topic(node);
    QString body = QStringLiteral("<h2 style='margin: 9px 0 14px 13px'>%1</h2><div style='margin-left: 13px'>").arg(heading(t.title));
    if (!t.description.isEmpty())
        body += QStringLiteral("<p>%1</p>").arg(escaped(t.description));
    body += QStringLiteral("<p>%1</p></div>").arg(QObject::tr("The topics of this subject are in Windows XP's help files, which this Help and Support Center does not show yet."));
    return page(t.title, body);
}

QString searchPage(const QString &query)
{
    QString body = QStringLiteral("<h2 style='margin: 9px 0 14px 13px'>%1</h2><div style='margin-left: 13px'>").arg(heading(QObject::tr("Search Results")));
    int found = 0;
    const QStringList words = query.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const Topic &t : topics()) {
        if (words.isEmpty() || !std::all_of(words.begin(), words.end(), [&](const QString &w) { return t.title.contains(w, Qt::CaseInsensitive); }))
            continue;
        body += QStringLiteral("<p><a href='hcp://topic/%1' style='font-weight: bold'>%2</a></p>").arg(t.node, escaped(t.title));
        if (++found == 100)
            break;
    }
    if (!found)
        body += QStringLiteral("<p>%1</p>").arg(QObject::tr("No results were found for \"%1\".").arg(escaped(query)));
    return page(QObject::tr("Search Results"), body + QStringLiteral("</div>"));
}

// A part of the Help and Support Center that is not ported yet: its title and what it does
QString placeholderPage(const QString &title, const QString &what)
{
    return page(title, QStringLiteral("<h2 style='margin: 9px 0 14px 13px'>%1</h2><div style='margin-left: 13px'><p>%2</p></div>").arg(heading(title), escaped(what)));
}

// hcp://: the pages above, made when asked for
class HcpHandler : public QWebEngineUrlSchemeHandler
{
public:
    using QWebEngineUrlSchemeHandler::QWebEngineUrlSchemeHandler;
    void requestStarted(QWebEngineUrlRequestJob *job) override
    {
        const QUrl url = job->requestUrl();
        const QString host = url.host(), path = url.path().mid(1);
        const HomeWords &words = homeWords();
        QString html;
        if (host == QLatin1String("topic"))
            html = topicPage(path);
        else if (host == QLatin1String("search"))
            html = searchPage(QUrlQuery(url).queryItemValue(QStringLiteral("query"), QUrl::FullyDecoded));
        else if (host == QLatin1String("page") && path == QLatin1String("remote-assistance"))
            html = placeholderPage(QObject::tr("Remote Assistance"), words.title.value(QStringLiteral("center_RA_a")));
        else if (host == QLatin1String("page") && path == QLatin1String("support"))
            html = placeholderPage(QObject::tr("Support"), words.title.value(QStringLiteral("center_PSS_a")));
        else if (host == QLatin1String("page") && path == QLatin1String("system-restore"))
            html = placeholderPage(QObject::tr("System Restore"), words.title.value(QStringLiteral("center_SR")));
        else if (host == QLatin1String("page") && path == QLatin1String("tools"))
            html = placeholderPage(QObject::tr("Tools"), words.title.value(QStringLiteral("center_TOOLS")));
        else if (host == QLatin1String("page"))
            html = placeholderPage(QUrlQuery(url).queryItemValue(QStringLiteral("title"), QUrl::FullyDecoded),
                                   QUrlQuery(url).queryItemValue(QStringLiteral("text"), QUrl::FullyDecoded));
        else
            html = homePage();
        auto *buffer = new QBuffer(job);
        buffer->setData(html.toUtf8());
        job->reply("text/html", buffer);
    }
};

// The search band: XP's dark blue under the toolbar, with the search box, and the title on its right
class SearchBand : public QWidget
{
public:
    explicit SearchBand(QWidget *parent)
        : QWidget(parent)
    {
        setFixedHeight(BAND_HEIGHT + BAND_LINE_HEIGHT);
        m_logo = keyed(LOGO);
        // The SKU's name, as the help package says it
        const QString package = QString::fromUtf8(readFile(cabFile("package_description.xml")));
        static const QRegularExpression sku(QStringLiteral("DISPLAYNAME=\"([^\"]+)\""));
        m_sku = sku.match(package).captured(1);
        if (m_sku.isEmpty())
            m_sku = QStringLiteral("Windows XP Professional");
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(QRect(0, 0, width(), BAND_HEIGHT), BAND);
        painter.fillRect(QRect(0, BAND_HEIGHT, width(), BAND_LINE_HEIGHT), BAND_LINE);
        painter.setPen(Qt::white);
        QFont title(QStringLiteral("Verdana"));
        title.setBold(true);
        title.setPixelSize(18);
        painter.setFont(title);
        const QString name = QObject::tr("Help and Support Center");
        const int titleWidth = painter.fontMetrics().horizontalAdvance(name);
        painter.drawText(QRect(width() - 13 - titleWidth, 8, titleWidth, 24), Qt::AlignLeft | Qt::AlignVCenter, name);
        if (!m_logo.isNull())
            painter.drawImage(width() - 13 - titleWidth - 6 - m_logo.width(), 8, m_logo);
        painter.setFont(ShellTheme::instance()->systemFont(QStringLiteral("msgboxfont")));
        painter.drawText(QRect(0, 40, width() - 13, 14), Qt::AlignRight | Qt::AlignVCenter, m_sku);
    }

private:
    QImage m_logo;
    QString m_sku;
};

} // namespace

void HelpCenter::registerSchemes()
{
    QWebEngineUrlScheme scheme("hcp");
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    scheme.setFlags(QWebEngineUrlScheme::LocalScheme | QWebEngineUrlScheme::LocalAccessAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);
}

HelpCenter::HelpCenter(const QString &topic)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Help and Support Center"));
    if (const auto helpctr = xpfiles::resources(xpfiles::find(QStringLiteral("helpctr.exe"))))
        setWindowIcon(QIcon(QPixmap::fromImage(xpfiles::icon(*helpctr, xpfiles::iconGroupAt(*helpctr, 0), 32))));
    static bool handlerInstalled = false;
    if (!handlerInstalled) {
        QWebEngineProfile::defaultProfile()->installUrlSchemeHandler("hcp", new HcpHandler(qApp));
        handlerInstalled = true;
    }
    // As XP SP3 opens it (measured: 766 x 648, frame included)
    {
        const QRect work = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->availableGeometry() : QRect(0, 0, 1024, 738);
        const FrameStyle &frame = ShellTheme::instance()->frame();
        resize(qMin(766, work.width()) - 2 * frame.border, qMin(648, work.height()) - frame.top - frame.border);
    }

    // The page follows hcp:// links itself; the frame's actions it hands to the window
    class Page : public QWebEnginePage
    {
    public:
        Page(HelpCenter *window)
            : QWebEnginePage(window), m_window(window)
        {
        }

    protected:
        bool acceptNavigationRequest(const QUrl &url, NavigationType, bool) override { return m_window->follow(url); }

    private:
        HelpCenter *m_window;
    };
    m_view = new QWebEngineView(this);
    m_view->setPage(new Page(this));
    m_view->setContextMenuPolicy(Qt::NoContextMenu);
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(buildSearchBand());
    layout->addWidget(m_view, 1);
    setCentralWidget(central);
    buildToolbar();
    connect(m_view, &QWebEngineView::urlChanged, this, [this] {
        m_backButton->setEnabled(m_view->history()->canGoBack());
        m_forwardButton->setEnabled(m_view->history()->canGoForward());
    });
    navigate(topic.isEmpty() ? QStringLiteral("hcp://services/centers/homepage") : topic);
}

void HelpCenter::navigate(const QString &address)
{
    QUrl url(address);
    if (url.scheme() != QLatin1String("hcp")) {
        programs::openInternetExplorer(address);
        return;
    }
    // XP's own addresses this does not make go to the home page
    if (url.host() != QLatin1String("topic") && url.host() != QLatin1String("search") && url.host() != QLatin1String("page"))
        url = QUrl(QStringLiteral("hcp://services/centers/homepage"));
    m_view->load(url);
}

bool HelpCenter::follow(const QUrl &url)
{
    if (url.scheme() == QLatin1String("hcp") && url.host() == QLatin1String("action")) {
        const QString what = url.path().mid(1);
        if (what == QLatin1String("windowsupdate"))
            programs::openInternetExplorer(QStringLiteral("http://windowsupdate.microsoft.com/"));
        else if (what == QLatin1String("compatibility"))
            programs::openInternetExplorer(QStringLiteral("http://www.microsoft.com/windows/catalog/"));
        return false;
    }
    if (url.scheme() == QLatin1String("hcp") || url.scheme() == QLatin1String("data"))
        return true;
    // A web address opens in Internet Explorer, as XP's help does
    programs::openInternetExplorer(url.toString());
    return false;
}

void HelpCenter::buildToolbar()
{
    // HCpan_06.xml's buttons, with helpctr.exe's pictures; a band of the visual style's rebar
    const int BUTTON_HEIGHT = 38;
    auto *toolbar = new RebarToolBar(tr("Navigation"), this, false);
    addToolBar(toolbar);
    toolbar->setMovable(false);
    toolbar->toggleViewAction()->setVisible(false);
    toolbar->setContextMenuPolicy(Qt::PreventContextMenu);
    toolbar->setIconSize(QSize(24, 24));
    auto *lead = new QWidget(toolbar);
    lead->setFixedSize(2, BUTTON_HEIGHT);
    toolbar->addWidget(lead);
    QImage normal, hot;
    if (const auto helpctr = xpfiles::resources(xpfiles::find(QStringLiteral("helpctr.exe")))) {
        normal = xpfiles::bitmap(*helpctr, STRIP_NORMAL);
        hot = xpfiles::bitmap(*helpctr, STRIP_HOT);
    }
    const auto pictureOf = [&](const QString &id) {
        QIcon icon;
        for (int i = 0; i < int(std::size(STRIP_IDS)); ++i)
            if (id == QLatin1String(STRIP_IDS[i]) && !normal.isNull()) {
                icon.addPixmap(QPixmap::fromImage(normal.copy(i * 24, 0, 24, 24)), QIcon::Normal);
                icon.addPixmap(QPixmap::fromImage(hot.copy(i * 24, 0, 24, 24)), QIcon::Active);
            }
        return icon;
    };
    QFile file(cabFile(TOOLBAR));
    if (!file.open(QIODevice::ReadOnly))
        return;
    QXmlStreamReader xml(&file);
    QString id, type, text, tip;
    bool hideText = false, enabled = true;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QLatin1String("BUTTON")) {
            id = xml.attributes().value(QLatin1String("ID")).toString();
            type = xml.attributes().value(QLatin1String("TYPE")).toString();
            hideText = xml.attributes().value(QLatin1String("HIDETEXT")) == QLatin1String("true");
            enabled = xml.attributes().value(QLatin1String("ENABLED")) != QLatin1String("false");
            text.clear();
            tip.clear();
        } else if (xml.isStartElement() && xml.name() == QLatin1String("TEXT")) {
            text = xml.readElementText();
        } else if (xml.isStartElement() && xml.name() == QLatin1String("TOOLTIP")) {
            tip = xml.readElementText();
        } else if (xml.isEndElement() && xml.name() == QLatin1String("BUTTON")) {
            if (type == QLatin1String("SEPARATOR")) {
                toolbar->addWidget(new BandSeparator(toolbar, BUTTON_HEIGHT));
                continue;
            }
            const bool history = type == QLatin1String("BACK") || type == QLatin1String("FORWARD");
            auto *button = new BandButton(toolbar, BUTTON_HEIGHT, 24, history ? BandButton::Split : BandButton::Plain);
            button->setIcon(pictureOf(id));
            // XP shows the access keys' underlines on these buttons
            button->setText(text);
            button->setToolTip(tip);
            button->setToolButtonStyle(hideText ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
            button->setEnabled(enabled);
            toolbar->addWidget(button);
            if (type == QLatin1String("BACK")) {
                m_backButton = button;
                connect(button, &QToolButton::clicked, m_view, &QWebEngineView::back);
            } else if (type == QLatin1String("FORWARD")) {
                m_forwardButton = button;
                connect(button, &QToolButton::clicked, m_view, &QWebEngineView::forward);
            } else if (id == QLatin1String("HOME")) {
                connect(button, &QToolButton::clicked, this, [this] { navigate(QString()); });
            } else {
                const QString title = QString(text).remove(QLatin1Char('&'));
                connect(button, &QToolButton::clicked, this, [this, title, tip] {
                    QUrl url(QStringLiteral("hcp://page/panel"));
                    QUrlQuery query;
                    query.addQueryItem(QStringLiteral("title"), title);
                    query.addQueryItem(QStringLiteral("text"), tip);
                    url.setQuery(query);
                    m_view->load(url);
                });
            }
            if (history) {
                auto *menu = new QMenu(button);
                const bool back = type == QLatin1String("BACK");
                connect(menu, &QMenu::aboutToShow, this, [this, menu, back] {
                    menu->clear();
                    QWebEngineHistory *history = m_view->history();
                    const QList<QWebEngineHistoryItem> items = back ? history->backItems(9) : history->forwardItems(9);
                    for (int i = 0; i < items.size(); ++i) {
                        const QWebEngineHistoryItem item = back ? items[items.size() - 1 - i] : items[i];
                        menu->addAction(item.title(), this, [history, item] { history->goToItem(item); });
                    }
                });
                button->setMenu(menu);
            }
        }
    }
}

QWidget *HelpCenter::buildSearchBand()
{
    // XP's (measured on SP3): "Search" 13 from the left, the box 81 in and 145 wide, its arrow after it,
    // "Set search options" under them
    auto *band = new SearchBand(this);
    auto *label = new QLabel(tr("Search"), band);
    QFont bold(QStringLiteral("Verdana"));
    bold.setBold(true);
    bold.setPixelSize(13);
    label->setFont(bold);
    label->setStyleSheet(QStringLiteral("color: white"));
    label->move(13, 16);
    m_search = new QLineEdit(band);
    m_search->setGeometry(81, 13, 143, 22);
    m_search->setToolTip(tr("Type or paste your search term(s)."));
    auto *go = new QToolButton(band);
    go->setAutoRaise(true);
    go->setIcon(QIcon(QPixmap::fromImage(keyed(SEARCH_GO))));
    go->setIconSize(QSize(24, 24));
    go->setGeometry(226, 12, 26, 26);
    go->setStyleSheet(QStringLiteral("QToolButton { border: none; }"));
    auto *options = new QLabel(QStringLiteral("<a href='#' style='color: white; text-decoration: none'>%1</a>").arg(tr("Set search options")), band);
    options->move(79, 41);
    connect(options, &QLabel::linkActivated, this, [this] {
        QUrl url(QStringLiteral("hcp://page/options"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("title"), tr("Set search options"));
        url.setQuery(query);
        m_view->load(url);
    });
    const auto search = [this] {
        if (m_search->text().trimmed().isEmpty())
            return;
        QUrl url(QStringLiteral("hcp://search/"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("query"), m_search->text().trimmed());
        url.setQuery(query);
        m_view->load(url);
    };
    connect(go, &QToolButton::clicked, this, search);
    connect(m_search, &QLineEdit::returnPressed, this, search);
    return band;
}
