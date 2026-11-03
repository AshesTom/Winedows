// SPDX-License-Identifier: LGPL-2.1-or-later
#include "notepad.h"

#include "applets.h"
#include "fontpolicy.h"
#include "theme.h"
#include "xpdialog.h"
#include "xpfiles.h"
#include "xpprogram.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSaveFile>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringDecoder>
#include <QStringEncoder>
#include <QTextBlock>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>
#ifdef XP_PRINTING
#include <QAbstractTextDocumentLayout>
#include <QPrintDialog>
#include <QPrinter>
#include <QPrinterInfo>
#endif

namespace {

// notepad.exe's commands
enum {
    IDM_NEW = 1, IDM_OPEN = 2, IDM_SAVE = 3, IDM_SAVEAS = 4, IDM_PAGESETUP = 5, IDM_PRINT = 6, IDM_EXIT = 7,
    IDM_UNDO = 16, IDM_FIND = 21, IDM_FINDNEXT = 22, IDM_REPLACE = 23, IDM_GOTO = 24, IDM_SELECTALL = 25,
    IDM_TIMEDATE = 26, IDM_STATUSBAR = 27, IDM_WORDWRAP = 32, IDM_FONT = 33, IDM_HELP = 64, IDM_ABOUT = 65,
    IDM_CUT = 768, IDM_COPY = 769, IDM_PASTE = 770, IDM_DELETE = 771,
};
// and strings
enum {
    IDS_CANNOT_FIND_FILE = 2, IDS_CHANGED = 3, IDS_UNTITLED = 4, IDS_TITLE_SUFFIX = 5, IDS_NOT_FOUND = 6,
    IDS_NOTEPAD = 9, IDS_CANNOT_CREATE = 14, IDS_HEADER = 18, IDS_FOOTER = 19, IDS_TEXT_FILES = 20,
    IDS_ALL_FILES = 21, IDS_OPEN = 22, IDS_SAVE_AS = 23, IDS_NO_ACCESS = 26, IDS_UNICODE_LOST = 27,
    IDS_GOTO_TITLE = 30, IDS_LINE_RANGE = 31, IDS_ANSI = 32, IDS_POSITION = 37,
};
// comdlg32.dll's dialogs and strings
enum {
    FINDDLGORD = 1540, REPLACEDLGORD = 1541, FORMATDLGORD31 = 1543,
    IDS_REPLACE_FILE = 257, IDS_SIZE_NUMBER = 1098, IDS_SIZE_RANGE = 1099, IDS_TRUETYPE = 1101,
    IDS_NO_FONT = 1282, IDS_REGULAR = 1284, IDS_MARGINS_INCHES = 1585, IDS_MARGINS_MM = 1586,
    IDS_AUTO_SELECT = 1589, IDS_MARGINS_OVERLAP = 1108,
};
// Controls of those dialogs
enum {
    ID_FIND_WHAT = 1152, ID_REPLACE_WITH = 1153, ID_WHOLE_WORD = 1040, ID_MATCH_CASE = 1041, ID_UP = 1056,
    ID_DOWN = 1057, ID_DIRECTION = 1072, ID_REPLACE = 1024, ID_REPLACE_ALL = 1025, ID_HELP = 1038,
};

QString settingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/notepad.conf");
}

// The current time and date as Time/Date and .LOG insert them: the locale's short time, then its short
// date with the whole year (XP's sShortDate).
QString timeDate()
{
    const QLocale locale = QLocale::system();
    QString dateFormat = locale.dateFormat(QLocale::ShortFormat);
    if (!dateFormat.contains(QLatin1String("yyyy")))
        dateFormat.replace(QLatin1String("yy"), QLatin1String("yyyy"));
    const QDateTime now = QDateTime::currentDateTime();
    return locale.toString(now.time(), QLocale::ShortFormat) + QLatin1Char(' ') + locale.toString(now.date(), dateFormat);
}

// The edit control: its left margin is two pixels wider than Qt's document margin (measured on XP's
// Notepad, comctl32 6's edit), white, outside the viewport
class NotepadEdit : public QPlainTextEdit
{
public:
    explicit NotepadEdit(QWidget *parent)
        : QPlainTextEdit(parent)
    {
        setViewportMargins(2, 0, 0, 0);
        setBackgroundRole(QPalette::Base);
        setAutoFillBackground(true);
    }
};

QPushButton *button(const xpdialog::Page &page, int id)
{
    return qobject_cast<QPushButton *>(page.controls.value(id));
}

// A CBS_SIMPLE combo box of comdlg32's Font dialog: an edit over its list, in the template's place.
struct SimpleCombo
{
    QLineEdit *edit;
    QListWidget *list;
};
SimpleCombo simpleCombo(const xpdialog::Page &page, int id, QWidget *parent)
{
    QWidget *placeholder = page.controls.value(id);
    const QRect r = placeholder ? placeholder->geometry() : QRect();
    if (placeholder)
        placeholder->hide();
    const int editHeight = ShellTheme::instance()->scaled(20);
    SimpleCombo combo{new QLineEdit(parent), new QListWidget(parent)};
    combo.edit->setGeometry(r.x(), r.y(), r.width(), editHeight);
    // (The template's height is the whole control's at 76 units)
    combo.list->setGeometry(r.x(), r.y() + editHeight - 1, r.width(), xpdialog::unitY(76) - editHeight + 1);
    combo.list->setUniformItemSizes(true);
    // A list box shows its selection in the highlight colour with or without the focus
    QPalette palette = combo.list->palette();
    for (QPalette::ColorRole role : {QPalette::Highlight, QPalette::HighlightedText})
        palette.setColor(QPalette::Inactive, role, palette.color(QPalette::Active, role));
    combo.list->setPalette(palette);
    QObject::connect(combo.list, &QListWidget::currentTextChanged, combo.edit, [combo](const QString &text) {
        if (!combo.edit->hasFocus())
            combo.edit->setText(text);
    });
    // Typing finds the first item that starts with the words, as the edit of a simple combo does
    QObject::connect(combo.edit, &QLineEdit::textEdited, combo.list, [combo](const QString &text) {
        for (int i = 0; i < combo.list->count(); ++i)
            if (combo.list->item(i)->text().startsWith(text, Qt::CaseInsensitive)) {
                combo.list->setCurrentRow(i);
                combo.list->scrollToItem(combo.list->item(i), QAbstractItemView::PositionAtTop);
                return;
            }
    });
    return combo;
}

#ifdef XP_PRINTING
// A paper size by its key (QPageSize::key), A4 if none has it
QPageSize pageSize(const QString &key)
{
    for (int id = 0; id <= QPageSize::LastPageSize; ++id)
        if (const QPageSize size{QPageSize::PageSizeId(id)}; size.key() == key)
            return size;
    for (const QPageSize &size : QPrinterInfo::defaultPrinter().supportedPageSizes())
        if (size.key() == key)
            return size;
    return QPageSize(QPageSize::A4);
}

// The Page Setup dialog's picture of the page: its shape, a shadow, and lines of text inside the margins
class PagePreview : public QWidget
{
public:
    explicit PagePreview(QWidget *parent) : QWidget(parent) {}
    void set(const QSizeF &paper, const QMarginsF &margins)
    {
        m_paper = paper;
        m_margins = margins;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (m_paper.isEmpty())
            return;
        QPainter painter(this);
        const int shadow = 4;
        const QSizeF room(width() - shadow - 1, height() - shadow - 1);
        const double scale = qMin(room.width() / m_paper.width(), room.height() / m_paper.height());
        const QSize size(qRound(m_paper.width() * scale), qRound(m_paper.height() * scale));
        const QRect page(QPoint((width() - shadow - size.width()) / 2, (height() - shadow - size.height()) / 2), size);
        const QColor shade = palette().color(QPalette::Shadow);
        painter.fillRect(page.translated(shadow, shadow), palette().color(QPalette::Dark));
        painter.fillRect(page, Qt::white);
        painter.setPen(shade);
        painter.drawRect(page.adjusted(0, 0, -1, -1));
        const QRect body = page.adjusted(qRound(m_margins.left() * scale), qRound(m_margins.top() * scale),
                                         -qRound(m_margins.right() * scale), -qRound(m_margins.bottom() * scale));
        if (body.width() < 2 || body.height() < 2)
            return;
        painter.setClipRect(body);
        painter.setPen(palette().color(QPalette::Dark));
        for (int y = body.top() + 1, line = 0; y < body.bottom(); y += 2, ++line)
            painter.drawLine(body.left(), y, body.right() - (line % 7 == 6 ? body.width() / 3 : 0), y);
    }

private:
    QSizeF m_paper;
    QMarginsF m_margins;
};

// A header or footer with Notepad's codes: &f the file, &p the page, &d the date, &t the time, &l &c &r
// where the following words go, && an "&"
void drawCodes(QPainter &painter, const QRect &rect, const QString &codes, const QString &file, int page)
{
    QString parts[3];
    int at = 1;
    for (int i = 0; i < codes.size(); ++i) {
        if (codes[i] != QLatin1Char('&') || i + 1 == codes.size()) {
            parts[at] += codes[i];
            continue;
        }
        const QChar code = codes[++i].toLower();
        const QLocale locale = QLocale::system();
        if (code == QLatin1Char('f'))
            parts[at] += file;
        else if (code == QLatin1Char('p'))
            parts[at] += QString::number(page);
        else if (code == QLatin1Char('d'))
            parts[at] += locale.toString(QDate::currentDate(), QLocale::LongFormat);
        else if (code == QLatin1Char('t'))
            parts[at] += locale.toString(QTime::currentTime(), QLocale::ShortFormat);
        else if (code == QLatin1Char('l'))
            at = 0;
        else if (code == QLatin1Char('c'))
            at = 1;
        else if (code == QLatin1Char('r'))
            at = 2;
        else
            parts[at] += codes[i];
    }
    painter.drawText(rect, Qt::AlignLeft | Qt::AlignVCenter, parts[0]);
    painter.drawText(rect, Qt::AlignHCenter | Qt::AlignVCenter, parts[1]);
    painter.drawText(rect, Qt::AlignRight | Qt::AlignVCenter, parts[2]);
}
#endif

} // namespace

// Notepad's status bar as comctl32 draws it: the Status class's background, two parts edge to edge
// (SB_SETPARTS at three quarters of the width, no size grip), each its Pane, the position's words in
// the second
class NotepadStatus : public QStatusBar
{
public:
    explicit NotepadStatus(QWidget *parent)
        : QStatusBar(parent)
    {
        setSizeGripEnabled(false);
        setFixedHeight(ShellTheme::instance()->scaled(26));
    }
    void setText(const QString &text)
    {
        m_text = text;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        ShellTheme *theme = ShellTheme::instance();
        const int split = width() * 3 / 4;
        const QRect panes[] = {QRect(0, theme->scaled(4), split, height() - theme->scaled(6)),
                               QRect(split, theme->scaled(4), width() - split, height() - theme->scaled(6))};
        if (const w4w::Renderer *renderer = theme->renderer()) {
            if (!renderer->draw(&painter, rect(), {QStringLiteral("status"), {}, {}, 0, {}}))
                painter.fillRect(rect(), palette().color(QPalette::Window));
            for (const QRect &pane : panes)
                renderer->draw(&painter, pane, {QStringLiteral("status"), QStringLiteral("pane"), {}, 0, {}});
        } else {
            // Windows Classic: each part's sunken outer edge
            painter.fillRect(rect(), palette().color(QPalette::Window));
            for (const QRect &pane : panes) {
                const QRect r = pane.adjusted(0, 0, -3, -1);
                painter.setPen(theme->system(QStringLiteral("btnshadow"), Qt::gray));
                painter.drawLine(r.topLeft(), r.topRight());
                painter.drawLine(r.topLeft(), r.bottomLeft());
                painter.setPen(theme->system(QStringLiteral("btnhighlight"), Qt::white));
                painter.drawLine(r.bottomLeft(), r.bottomRight());
                painter.drawLine(r.topRight(), r.bottomRight());
            }
        }
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(panes[1].adjusted(theme->scaled(TEXT_INSET), 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, m_text);
    }

private:
    static const int TEXT_INSET = 5;
    QString m_text;
};

Notepad::Notepad(const QString &file)
    : m_resources(xpfiles::resources(xpfiles::find(QStringLiteral("notepad.exe"))))
    , m_dialogs(xpfiles::resources(xpfiles::find(QStringLiteral("comdlg32.dll"))))
    , m_edit(new NotepadEdit(this))
{
    setAttribute(Qt::WA_DeleteOnClose);
    if (m_resources)
        setWindowIcon(QIcon(QPixmap::fromImage(xpfiles::icon(*m_resources, 2, 32))));
    m_header = text(IDS_HEADER);
    m_footer = text(IDS_FOOTER);
    readSettings();

    setCentralWidget(m_edit);
    m_edit->installEventFilter(this);
    m_edit->viewport()->installEventFilter(this);
    m_edit->viewport()->setAcceptDrops(true);
    m_edit->setTabChangesFocus(false);
    // The edit control greys a scroll bar with nothing to scroll (ESB_DISABLE_BOTH)
    for (QScrollBar *bar : {m_edit->verticalScrollBar(), m_edit->horizontalScrollBar()})
        connect(bar, &QScrollBar::rangeChanged, bar, [bar](int minimum, int maximum) { bar->setEnabled(maximum > minimum); });

    connect(m_edit->document(), &QTextDocument::modificationChanged, this, [this] { updateTitle(); });
    connect(m_edit->document(), &QTextDocument::undoCommandAdded, this, [this] { m_undone = false; });
    connect(m_edit, &QPlainTextEdit::cursorPositionChanged, this, &Notepad::updateStatus);
    connect(m_edit, &QPlainTextEdit::selectionChanged, this, &Notepad::updateEditMenu);
    connect(m_edit, &QPlainTextEdit::textChanged, this, &Notepad::updateEditMenu);
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, &Notepad::updateEditMenu);

    m_status = new NotepadStatus(this);
    setStatusBar(m_status);

    if (m_resources) {
        m_actions = xpprogram::fillMenuBar(menuBar(), *m_resources, 1);
        const QList<xpprogram::Accelerator> keys = xpprogram::accelerators(*m_resources, QStringLiteral("MAINACC"));
        for (auto it = m_actions.cbegin(); it != m_actions.cend(); ++it)
            it.value()->setShortcuts(xpprogram::shortcuts(keys, it.key()));
    }
    const auto on = [this](int id, auto slot) {
        if (QAction *action = m_actions.value(id))
            connect(action, &QAction::triggered, this, slot);
    };
    on(IDM_NEW, [this] { newFile(); });
    on(IDM_OPEN, [this] { openFile(); });
    on(IDM_SAVE, [this] { save(); });
    on(IDM_SAVEAS, [this] { saveAs(); });
    on(IDM_PAGESETUP, [this] { pageSetup(); });
    on(IDM_PRINT, [this] { print(); });
    on(IDM_EXIT, [this] { close(); });
    on(IDM_UNDO, [this] { undo(); });
    on(IDM_CUT, [this] { m_edit->cut(); });
    on(IDM_COPY, [this] { m_edit->copy(); });
    on(IDM_PASTE, [this] { m_edit->paste(); });
    on(IDM_DELETE, [this] { m_edit->textCursor().removeSelectedText(); });
    on(IDM_FIND, [this] { find(false); });
    on(IDM_FINDNEXT, [this] {
        if (m_findText.isEmpty())
            find(false);
        else
            findNext();
    });
    on(IDM_REPLACE, [this] { find(true); });
    on(IDM_GOTO, [this] { goTo(); });
    on(IDM_SELECTALL, [this] { m_edit->selectAll(); });
    on(IDM_TIMEDATE, [this] { insertTimeDate(); });
    on(IDM_WORDWRAP, [this] { setWordWrap(!m_wrap); });
    on(IDM_FONT, [this] { chooseFont(); });
    on(IDM_STATUSBAR, [this] {
        m_statusBar = !m_statusBar;
        setWordWrap(m_wrap);
    });
    on(IDM_ABOUT, [this] {
        applets::about(text(IDS_NOTEPAD), m_resources ? xpfiles::icon(*m_resources, 2, 32) : QImage());
    });
    for (int id : {IDM_WORDWRAP, IDM_STATUSBAR})
        if (QAction *action = m_actions.value(id))
            action->setCheckable(true);
    // Help Topics is notepad.chm, which nothing here can show
    if (QAction *help = m_actions.value(IDM_HELP))
        help->setEnabled(false);
#ifndef XP_PRINTING
    for (int id : {IDM_PAGESETUP, IDM_PRINT})
        if (QAction *action = m_actions.value(id))
            action->setEnabled(false);
#endif

    applyFont();
    setWordWrap(m_wrap);
    updateEditMenu();

    QSettings settings(settingsPath(), QSettings::IniFormat);
    if (!restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray())) {
        // CW_USEDEFAULT: three quarters of the screen
        const QRect area = QGuiApplication::primaryScreen()->availableGeometry();
        resize(area.width() * 3 / 4, area.height() * 3 / 4);
    }

    if (!file.isEmpty()) {
        const QString path = QFileInfo(file).absoluteFilePath();
        if (QFileInfo::exists(path)) {
            load(path);
        } else {
            // XP asks before making it
            const QString question = text(IDS_CANNOT_FIND_FILE).replace(QLatin1String("%%"), QFileInfo(path).fileName());
            if (QMessageBox::question(this, text(IDS_NOTEPAD), question, QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel) == QMessageBox::Yes) {
                if (QFile out(path); out.open(QIODevice::WriteOnly))
                    m_path = path;
                else
                    message(IDS_CANNOT_CREATE, QFileInfo(path).fileName(), QMessageBox::Warning);
            }
        }
    }
    updateTitle();
    updateStatus();
}

QString Notepad::text(int id) const
{
    return m_resources ? xpfiles::string(*m_resources, id) : QString();
}

void Notepad::message(int id, const QString &file, int icon) const
{
    QMessageBox box(QMessageBox::Icon(icon), text(IDS_NOTEPAD), text(id).replace(QLatin1String("%%"), file), QMessageBox::Ok,
                    m_findDialog ? static_cast<QWidget *>(m_findDialog.data()) : const_cast<Notepad *>(this));
    box.exec();
}

void Notepad::updateTitle()
{
    const QString name = m_path.isEmpty() ? text(IDS_UNTITLED) : QFileInfo(m_path).fileName();
    setWindowTitle(name + text(IDS_TITLE_SUFFIX));
}

void Notepad::updateStatus()
{
    // "   Ln %d, Col %d  "
    const QTextCursor cursor = m_edit->textCursor();
    QString line = text(IDS_POSITION);
    for (int value : {cursor.blockNumber() + 1, cursor.positionInBlock() + 1})
        if (const int at = line.indexOf(QLatin1String("%d")); at >= 0)
            line.replace(at, 2, QString::number(value));
    m_status->setText(line);
}

void Notepad::updateEditMenu()
{
    // As WM_INITMENUPOPUP sets them, and accelerators of greyed items do nothing
    const bool selection = m_edit->textCursor().hasSelection();
    const bool empty = m_edit->document()->isEmpty();
    const auto enable = [this](int id, bool on) {
        if (QAction *action = m_actions.value(id))
            action->setEnabled(on);
    };
    enable(IDM_UNDO, m_edit->document()->isUndoAvailable() || m_edit->document()->isRedoAvailable());
    enable(IDM_CUT, selection);
    enable(IDM_COPY, selection);
    enable(IDM_DELETE, selection);
    enable(IDM_PASTE, QApplication::clipboard()->mimeData() && QApplication::clipboard()->mimeData()->hasText());
    enable(IDM_FIND, !empty);
    enable(IDM_FINDNEXT, !empty);
    enable(IDM_REPLACE, !empty);
}

bool Notepad::confirmDiscard()
{
    if (!m_edit->document()->isModified())
        return true;
    const QString name = m_path.isEmpty() ? text(IDS_UNTITLED) : m_path;
    const auto answer = QMessageBox::warning(this, text(IDS_NOTEPAD), text(IDS_CHANGED).replace(QLatin1String("%%"), name),
                                             QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
    if (answer == QMessageBox::Yes)
        return save();
    return answer == QMessageBox::No;
}

void Notepad::newFile()
{
    if (!confirmDiscard())
        return;
    m_edit->clear();
    m_edit->document()->setModified(false);
    m_path.clear();
    m_encoding = Ansi;
    m_crlf = false;
    updateTitle();
}

void Notepad::openFile()
{
    if (!confirmDiscard())
        return;
    Encoding encoding = Ansi;
    const QString path = fileDialog(false, &encoding);
    if (!path.isEmpty())
        load(path, encoding);
}

bool Notepad::load(const QString &path, std::optional<Encoding> chosen)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        message(IDS_NO_ACCESS, path, QMessageBox::Warning);
        return false;
    }
    const QByteArray data = file.readAll();
    // A byte order mark says it; otherwise what the Open dialog chose, ANSI by default
    QString content;
    Encoding encoding = chosen.value_or(Ansi);
    if (data.startsWith("\xff\xfe"))
        encoding = Unicode;
    else if (data.startsWith("\xfe\xff"))
        encoding = UnicodeBigEndian;
    else if (data.startsWith("\xef\xbb\xbf"))
        encoding = Utf8;
    const auto decode = [&data](QStringConverter::Encoding codec, int mark) {
        QStringDecoder decoder(codec, QStringConverter::Flag::Stateless);
        return QString(decoder(data.mid(mark)));
    };
    switch (encoding) {
    case Unicode: content = decode(QStringConverter::Utf16LE, data.startsWith("\xff\xfe") ? 2 : 0); break;
    case UnicodeBigEndian: content = decode(QStringConverter::Utf16BE, data.startsWith("\xfe\xff") ? 2 : 0); break;
    case Utf8: content = decode(QStringConverter::Utf8, data.startsWith("\xef\xbb\xbf") ? 3 : 0); break;
    case Ansi: content = decode(QStringConverter::System, 0); break;
    }
    // Lines end as the file's do when it is saved again
    m_crlf = content.contains(QLatin1String("\r\n"));
    content.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    m_edit->setPlainText(content);
    m_edit->document()->setModified(false);
    m_path = path;
    m_encoding = encoding;
    // A file that starts with ".LOG" gets the time and date at its end
    if (content.startsWith(QLatin1String(".LOG"))) {
        m_edit->moveCursor(QTextCursor::End);
        m_edit->insertPlainText(QLatin1Char('\n') + timeDate() + QLatin1Char('\n'));
    }
    updateTitle();
    updateStatus();
    return true;
}

bool Notepad::save()
{
    if (m_path.isEmpty())
        return saveAs();
    return write(m_path, m_encoding);
}

bool Notepad::saveAs()
{
    Encoding encoding = m_encoding;
    const QString path = fileDialog(true, &encoding);
    if (path.isEmpty() || !write(path, encoding))
        return false;
    m_path = path;
    m_encoding = encoding;
    updateTitle();
    return true;
}

bool Notepad::write(const QString &path, Encoding encoding)
{
    QString content = m_edit->toPlainText();
    if (m_crlf)
        content.replace(QLatin1Char('\n'), QLatin1String("\r\n"));
    QByteArray data;
    switch (encoding) {
    case Unicode: data = QByteArray("\xff\xfe") + QStringEncoder(QStringConverter::Utf16LE)(content); break;
    case UnicodeBigEndian: data = QByteArray("\xfe\xff") + QStringEncoder(QStringConverter::Utf16BE)(content); break;
    case Utf8: data = QByteArray("\xef\xbb\xbf") + content.toUtf8(); break;
    case Ansi: {
        QStringEncoder encoder(QStringConverter::System);
        data = encoder(content);
        if (encoder.hasError()) {
            const QString warning = text(IDS_UNICODE_LOST).replace(QLatin1String("%%"), QFileInfo(path).fileName());
            if (QMessageBox::question(this, text(IDS_NOTEPAD), warning, QMessageBox::Ok | QMessageBox::Cancel) != QMessageBox::Ok)
                return false;
        }
        break;
    }
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(true);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        message(IDS_CANNOT_CREATE, QFileInfo(path).fileName(), QMessageBox::Warning);
        return false;
    }
    m_edit->document()->setModified(false);
    return true;
}

QString Notepad::fileDialog(bool saving, Encoding *encoding)
{
    QFileDialog dialog(this, text(saving ? IDS_SAVE_AS : IDS_OPEN));
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setOption(QFileDialog::DontConfirmOverwrite);
    dialog.setAcceptMode(saving ? QFileDialog::AcceptSave : QFileDialog::AcceptOpen);
    dialog.setFileMode(saving ? QFileDialog::AnyFile : QFileDialog::ExistingFile);
    const QString textFiles = text(IDS_TEXT_FILES);
    dialog.setNameFilters({textFiles, text(IDS_ALL_FILES).trimmed() + QStringLiteral(" (*)")});
    dialog.setDefaultSuffix(QStringLiteral("txt"));
    connect(&dialog, &QFileDialog::filterSelected, &dialog, [&dialog, textFiles](const QString &filter) {
        dialog.setDefaultSuffix(filter == textFiles ? QStringLiteral("txt") : QString());
    });
    if (!m_path.isEmpty())
        dialog.selectFile(m_path);
    else if (saving)
        dialog.selectFile(QStringLiteral("*.txt"));
    // The Encoding line under the file types, from notepad.exe's NPENCODINGDIALOG
    QWidget holder;
    const xpdialog::Page encodingPage = m_resources ? xpdialog::build(*m_resources, QStringLiteral("NPENCODINGDIALOG"), &holder) : xpdialog::Page();
    auto *label = qobject_cast<QLabel *>(encodingPage.controls.value(259));
    auto *combo = qobject_cast<QComboBox *>(encodingPage.controls.value(257));
    if (label && combo) {
        for (int i = 0; i < 4; ++i)
            combo->addItem(text(IDS_ANSI + i));
        combo->setCurrentIndex(*encoding);
        if (auto *grid = qobject_cast<QGridLayout *>(dialog.layout())) {
            const int row = grid->rowCount();
            label->setMinimumSize(0, 0);
            combo->setMinimumSize(0, 0);
            combo->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            grid->addWidget(label, row, 0);
            grid->addWidget(combo, row, 1);
        }
    }
    for (;;) {
        if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
            return QString();
        const QString path = dialog.selectedFiles().constFirst();
        if (combo)
            *encoding = Encoding(combo->currentIndex());
        // comdlg32's own question, with OFN_OVERWRITEPROMPT
        if (saving && QFileInfo::exists(path) && m_dialogs) {
            const QString question = xpfiles::string(*m_dialogs, IDS_REPLACE_FILE).replace(QLatin1String("%s"), path);
            if (QMessageBox::warning(this, dialog.windowTitle(), question, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                continue;
        }
        return path;
    }
}

void Notepad::pageSetup()
{
#ifdef XP_PRINTING
    if (!m_resources)
        return;
    QDialog dialog(this);
    const xpdialog::Page page = xpdialog::build(*m_resources, 12, &dialog);
    dialog.setWindowTitle(page.caption);
    const auto edit = [&page](int id) { return qobject_cast<QLineEdit *>(page.controls.value(id)); };
    // Inches where the locale measures so, as comdlg32 asks the locale
    const bool inches = QLocale::system().measurementSystem() == QLocale::ImperialUSSystem;
    const double unit = inches ? 25.4 : 1.0;
    if (auto *group = qobject_cast<QGroupBox *>(page.controls.value(1075)); group && m_dialogs)
        group->setTitle(xpfiles::string(*m_dialogs, inches ? IDS_MARGINS_INCHES : IDS_MARGINS_MM));
    // The paper sizes the printer has, otherwise the common ones
    QList<QPageSize> sizes = QPrinterInfo::printerInfo(m_printer).supportedPageSizes();
    if (sizes.isEmpty())
        sizes = QPrinterInfo::defaultPrinter().supportedPageSizes();
    if (sizes.isEmpty())
        for (QPageSize::PageSizeId id : {QPageSize::Letter, QPageSize::Legal, QPageSize::Executive, QPageSize::A3, QPageSize::A4, QPageSize::A5, QPageSize::B5})
            sizes.append(QPageSize(id));
    const QString paper = m_paper.isEmpty() ? QPageSize(inches ? QPageSize::Letter : QPageSize::A4).key() : m_paper;
    auto *sizeCombo = qobject_cast<QComboBox *>(page.controls.value(1137));
    if (sizeCombo) {
        for (const QPageSize &size : std::as_const(sizes)) {
            sizeCombo->addItem(size.name(), size.key());
            if (size.key() == paper)
                sizeCombo->setCurrentIndex(sizeCombo->count() - 1);
        }
    }
    if (auto *source = qobject_cast<QComboBox *>(page.controls.value(1138)); source && m_dialogs)
        source->addItem(xpfiles::string(*m_dialogs, IDS_AUTO_SELECT));
    auto *portrait = qobject_cast<QRadioButton *>(page.controls.value(1056));
    auto *landscape = qobject_cast<QRadioButton *>(page.controls.value(1057));
    if (portrait && landscape)
        (m_orientation == QPageLayout::Landscape ? landscape : portrait)->setChecked(true);
    const int margins[] = {1155, 1156, 1157, 1158};   // left, top, right, bottom
    const double values[] = {m_margins.left(), m_margins.top(), m_margins.right(), m_margins.bottom()};
    for (int i = 0; i < 4; ++i)
        if (QLineEdit *line = edit(margins[i]))
            line->setText(QLocale::system().toString(values[i] / unit, 'g', 4));
    if (QLineEdit *header = edit(30))
        header->setText(m_header);
    if (QLineEdit *footer = edit(31))
        footer->setText(m_footer);
    // The page's picture in place of the template's white rectangle and its shadows
    auto *preview = new PagePreview(&dialog);
    if (QWidget *frame = page.controls.value(1080)) {
        QRect area = frame->geometry();
        if (QWidget *shadow = page.controls.value(1081))
            area = area.united(shadow->geometry());
        if (QWidget *shadow = page.controls.value(1082))
            area = area.united(shadow->geometry());
        for (int id : {1080, 1081, 1082})
            page.controls.value(id)->hide();
        preview->setGeometry(area);
    }
    const auto chosenMargins = [&] {
        double out[4];
        for (int i = 0; i < 4; ++i)
            out[i] = (edit(margins[i]) ? QLocale::system().toDouble(edit(margins[i])->text()) : 0) * unit;
        return QMarginsF(out[0], out[1], out[2], out[3]);
    };
    const auto chosenPaper = [&] {
        QSizeF size = pageSize(paper).size(QPageSize::Millimeter);
        for (const QPageSize &candidate : std::as_const(sizes))
            if (sizeCombo && candidate.key() == sizeCombo->currentData().toString())
                size = candidate.size(QPageSize::Millimeter);
        if (landscape && landscape->isChecked())
            size.transpose();
        return size;
    };
    const auto refresh = [&] { preview->set(chosenPaper(), chosenMargins()); };
    refresh();
    if (sizeCombo)
        connect(sizeCombo, &QComboBox::currentIndexChanged, &dialog, refresh);
    if (landscape)
        connect(landscape, &QRadioButton::toggled, &dialog, refresh);
    for (int id : margins)
        if (QLineEdit *line = edit(id))
            connect(line, &QLineEdit::textChanged, &dialog, refresh);
    if (QPushButton *printer = button(page, 1026))
        connect(printer, &QPushButton::clicked, &dialog, [this, &dialog] {
            QPrinter chosen;
            if (!m_printer.isEmpty())
                chosen.setPrinterName(m_printer);
            QPrintDialog setup(&chosen, &dialog);
            if (setup.exec() == QDialog::Accepted)
                m_printer = chosen.printerName();
        });
    if (QPushButton *ok = button(page, 1)) {
        ok->setDefault(true);
        connect(ok, &QPushButton::clicked, &dialog, [&] {
            const QMarginsF chosen = chosenMargins();
            const QSizeF paperSize = chosenPaper();
            if (chosen.left() + chosen.right() >= paperSize.width() || chosen.top() + chosen.bottom() >= paperSize.height()
                || chosen.left() < 0 || chosen.right() < 0 || chosen.top() < 0 || chosen.bottom() < 0) {
                QMessageBox::warning(&dialog, dialog.windowTitle(), m_dialogs ? xpfiles::string(*m_dialogs, IDS_MARGINS_OVERLAP) : QString());
                return;
            }
            dialog.accept();
        });
    }
    if (QPushButton *cancel = button(page, 2))
        connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_margins = chosenMargins();
    if (sizeCombo)
        m_paper = sizeCombo->currentData().toString();
    m_orientation = landscape && landscape->isChecked() ? QPageLayout::Landscape : QPageLayout::Portrait;
    m_header = edit(30) ? edit(30)->text() : m_header;
    m_footer = edit(31) ? edit(31)->text() : m_footer;
    writeSettings();
#endif
}

void Notepad::print()
{
#ifdef XP_PRINTING
    QPrinter printer(QPrinter::HighResolution);
    if (!m_printer.isEmpty())
        printer.setPrinterName(m_printer);
    const QPageSize paper = m_paper.isEmpty() ? printer.pageLayout().pageSize() : pageSize(m_paper);
    printer.setFullPage(true);
    printer.setPageLayout(QPageLayout(paper, m_orientation, QMarginsF(), QPageLayout::Millimeter));
    printer.setDocName(m_path.isEmpty() ? text(IDS_UNTITLED) : QFileInfo(m_path).fileName());
    QPrintDialog dialog(&printer, this);
    dialog.setOption(QAbstractPrintDialog::PrintSelection, m_edit->textCursor().hasSelection());
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_printer = printer.printerName();
    QPainter painter(&printer);
    if (!painter.isActive())
        return;
    // The margins in the printer's pixels; the header and footer in the middle of theirs
    const double perMm = printer.resolution() / 25.4;
    const QRect paperRect = printer.pageLayout().fullRectPixels(printer.resolution());
    const QRect body = paperRect.adjusted(qRound(m_margins.left() * perMm), qRound(m_margins.top() * perMm),
                                          -qRound(m_margins.right() * perMm), -qRound(m_margins.bottom() * perMm));
    QFont font = m_edit->font();
    font.setPointSize(m_points);
    painter.setFont(font);
    QTextDocument document;
    document.documentLayout()->setPaintDevice(&printer);
    document.setDefaultFont(font);
    document.setDocumentMargin(0);
    QTextOption option = document.defaultTextOption();
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    document.setDefaultTextOption(option);
    document.setPlainText(printer.printRange() == QPrinter::Selection ? m_edit->textCursor().selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'))
                                                                      : m_edit->toPlainText());
    document.setPageSize(body.size());
    const QString name = printer.docName();
    const int lineHeight = QFontMetrics(font, &printer).height();
    const QRect header(body.left(), paperRect.top() + (body.top() - paperRect.top() - lineHeight) / 2, body.width(), lineHeight);
    const QRect footer(body.left(), body.bottom() + (paperRect.bottom() - body.bottom() - lineHeight) / 2, body.width(), lineHeight);
    for (int page = 0; page < document.pageCount(); ++page) {
        if (page > 0)
            printer.newPage();
        drawCodes(painter, header, m_header, name, page + 1);
        drawCodes(painter, footer, m_footer, name, page + 1);
        painter.save();
        painter.translate(body.left(), body.top() - page * body.height());
        document.drawContents(&painter, QRectF(0, page * body.height(), body.width(), body.height()));
        painter.restore();
    }
    writeSettings();
#endif
}

void Notepad::undo()
{
    // The edit control's one level: Undo again takes the undoing back
    if (m_undone && m_edit->document()->isRedoAvailable()) {
        m_edit->redo();
        m_undone = false;
    } else {
        m_edit->undo();
        m_undone = true;
    }
}

void Notepad::find(bool replacing)
{
    if (!m_dialogs)
        return;
    if (m_findDialog) {
        if (m_findDialog->property("replacing").toBool() == replacing) {
            m_findDialog->raise();
            m_findDialog->activateWindow();
            return;
        }
        m_findDialog->close();
    }
    auto *dialog = new QDialog(this);
    m_findDialog = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setProperty("replacing", replacing);
    const xpdialog::Page page = xpdialog::build(*m_dialogs, replacing ? REPLACEDLGORD : FINDDLGORD, dialog);
    dialog->setWindowTitle(page.caption);
    // Notepad asks for FR_HIDEWHOLEWORD and no Help
    for (int id : {ID_WHOLE_WORD, ID_HELP})
        if (QWidget *control = page.controls.value(id))
            control->hide();
    auto *what = qobject_cast<QLineEdit *>(page.controls.value(ID_FIND_WHAT));
    auto *with = qobject_cast<QLineEdit *>(page.controls.value(ID_REPLACE_WITH));
    auto *matchCase = qobject_cast<QCheckBox *>(page.controls.value(ID_MATCH_CASE));
    auto *up = qobject_cast<QRadioButton *>(page.controls.value(ID_UP));
    auto *down = qobject_cast<QRadioButton *>(page.controls.value(ID_DOWN));
    if (!what)
        return;
    const QString selected = m_edit->textCursor().selectedText();
    what->setText(!selected.isEmpty() && !selected.contains(QChar::ParagraphSeparator) ? selected : m_findText);
    what->selectAll();
    if (with)
        with->setText(m_replaceText);
    if (matchCase)
        matchCase->setChecked(m_matchCase);
    if (up && down)
        (m_down ? down : up)->setChecked(true);
    const auto take = [=, this] {
        m_findText = what->text();
        if (with)
            m_replaceText = with->text();
        if (matchCase)
            m_matchCase = matchCase->isChecked();
        if (down)
            m_down = down->isChecked();
    };
    const QList<int> needWords = {1, ID_REPLACE, ID_REPLACE_ALL};
    const auto enable = [=] {
        for (int id : needWords)
            if (QPushButton *b = button(page, id))
                b->setEnabled(!what->text().isEmpty());
    };
    enable();
    connect(what, &QLineEdit::textChanged, dialog, enable);
    if (QPushButton *next = button(page, 1)) {
        next->setDefault(true);
        connect(next, &QPushButton::clicked, this, [=, this] {
            take();
            if (replacing)
                m_down = true;
            findNext();
        });
    }
    if (QPushButton *one = button(page, ID_REPLACE))
        connect(one, &QPushButton::clicked, this, [=, this] {
            take();
            replace();
        });
    if (QPushButton *all = button(page, ID_REPLACE_ALL))
        connect(all, &QPushButton::clicked, this, [=, this] {
            take();
            replaceAll();
        });
    if (QPushButton *cancel = button(page, 2))
        connect(cancel, &QPushButton::clicked, dialog, &QDialog::close);
    dialog->show();
    what->setFocus();
}

bool Notepad::findNext()
{
    QTextDocument::FindFlags flags;
    if (m_matchCase)
        flags |= QTextDocument::FindCaseSensitively;
    QTextCursor from = m_edit->textCursor();
    if (!m_down) {
        flags |= QTextDocument::FindBackward;
        from.setPosition(from.selectionStart());
    } else {
        from.setPosition(from.selectionEnd());
    }
    // From the caret to the end (or the start), without going round, as XP's Notepad
    const QTextCursor found = m_edit->document()->find(m_findText, from, flags);
    if (found.isNull()) {
        message(IDS_NOT_FOUND, m_findText, QMessageBox::Information);
        return false;
    }
    m_edit->setTextCursor(found);
    return true;
}

void Notepad::replace()
{
    QTextCursor cursor = m_edit->textCursor();
    const Qt::CaseSensitivity sensitivity = m_matchCase ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (cursor.hasSelection() && cursor.selectedText().compare(m_findText, sensitivity) == 0) {
        cursor.insertText(m_replaceText);
        m_edit->setTextCursor(cursor);
    }
    m_down = true;
    findNext();
}

void Notepad::replaceAll()
{
    QTextDocument::FindFlags flags;
    if (m_matchCase)
        flags |= QTextDocument::FindCaseSensitively;
    QTextCursor edit(m_edit->document());
    edit.beginEditBlock();
    QTextCursor found = m_edit->document()->find(m_findText, 0, flags);
    while (!found.isNull()) {
        found.insertText(m_replaceText);
        found = m_edit->document()->find(m_findText, found, flags);
    }
    edit.endEditBlock();
}

void Notepad::goTo()
{
    if (!m_resources)
        return;
    QDialog dialog(this);
    const xpdialog::Page page = xpdialog::build(*m_resources, 14, &dialog);
    dialog.setWindowTitle(page.caption);
    auto *line = qobject_cast<QLineEdit *>(page.controls.value(258));
    if (!line)
        return;
    line->setText(QString::number(m_edit->textCursor().blockNumber() + 1));
    line->selectAll();
    if (QPushButton *ok = button(page, 1)) {
        ok->setDefault(true);
        connect(ok, &QPushButton::clicked, &dialog, [&] {
            bool number = false;
            const int wanted = line->text().toInt(&number);
            if (!number || wanted < 1 || wanted > m_edit->document()->blockCount()) {
                QMessageBox::information(&dialog, text(IDS_GOTO_TITLE), text(IDS_LINE_RANGE));
                line->selectAll();
                return;
            }
            dialog.accept();
        });
    }
    if (QPushButton *cancel = button(page, 2))
        connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QTextCursor cursor(m_edit->document()->findBlockByNumber(line->text().toInt() - 1));
    m_edit->setTextCursor(cursor);
}

void Notepad::insertTimeDate()
{
    m_edit->insertPlainText(timeDate());
}

void Notepad::setWordWrap(bool on)
{
    m_wrap = on;
    m_edit->setLineWrapMode(on ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    m_edit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    // The edit control's scroll bars stay, greyed when there is nothing to scroll; wrapped, it has no horizontal one
    m_edit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_edit->setHorizontalScrollBarPolicy(on ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAlwaysOn);
    // Word Wrap takes the status bar and Go To away
    statusBar()->setVisible(m_statusBar && !on);
    if (QAction *action = m_actions.value(IDM_WORDWRAP))
        action->setChecked(on);
    if (QAction *action = m_actions.value(IDM_STATUSBAR)) {
        action->setChecked(m_statusBar && !on);
        action->setEnabled(!on);
    }
    if (QAction *action = m_actions.value(IDM_GOTO))
        action->setEnabled(!on);
    writeSettings();
}

void Notepad::chooseFont()
{
    if (!m_dialogs)
        return;
    QDialog dialog(this);
    const xpdialog::Page page = xpdialog::build(*m_dialogs, FORMATDLGORD31, &dialog);
    dialog.setWindowTitle(page.caption);
    // Without CF_EFFECTS and CF_SHOWHELP, and no Apply
    for (int id : {1072, 1040, 1041, 1091, 1139, 1026, 1038})
        if (QWidget *control = page.controls.value(id))
            control->hide();
    const SimpleCombo family = simpleCombo(page, 1136, &dialog);
    const SimpleCombo style = simpleCombo(page, 1137, &dialog);
    const SimpleCombo size = simpleCombo(page, 1138, &dialog);
    // The font list's pictures: bitmap 38's lower row, TrueType first, on magenta
    QIcon trueType;
    const QImage pictures = xpfiles::bitmap(*m_dialogs, 38);
    if (pictures.width() >= 20 && pictures.height() >= 24) {
        QImage picture = pictures.copy(0, 12, 20, 12).convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < picture.height(); ++y)
            for (int x = 0; x < picture.width(); ++x)
                if (picture.pixel(x, y) == qRgb(255, 0, 255))
                    picture.setPixel(x, y, qRgba(0, 0, 0, 0));
        trueType = QIcon(QPixmap::fromImage(picture));
    }
    family.list->setIconSize(QSize(20, 12));
    for (const QString &name : QFontDatabase::families()) {
        if (name.startsWith(QLatin1Char('@')) || QFontDatabase::isPrivateFamily(name))
            continue;
        auto *item = new QListWidgetItem(name, family.list);
        if (QFontDatabase::isSmoothlyScalable(name))
            item->setIcon(trueType);
    }
    for (int i = 0; i < 4; ++i)
        style.list->addItem(xpfiles::string(*m_dialogs, IDS_REGULAR + i));
    auto *sample = qobject_cast<QLabel *>(page.controls.value(1092));
    auto *description = qobject_cast<QLabel *>(page.controls.value(1093));
    auto *script = qobject_cast<QComboBox *>(page.controls.value(1140));
    if (sample) {
        sample->setAlignment(Qt::AlignCenter);
        sample->setWordWrap(false);
        sample->show();
    }
    // Regular, Bold, Italic, Bold Italic as comdlg32 orders them
    const auto styleIndex = [](bool bold, bool italic) { return bold ? (italic ? 3 : 1) : (italic ? 2 : 0); };
    const auto chosenFont = [&] {
        QFont font(family.edit->text());
        const int index = style.list->currentRow();
        font.setBold(index == 1 || index == 3);
        font.setItalic(index == 2 || index == 3);
        font.setPixelSize(qMax(1, qRound(size.edit->text().toDouble() * ShellTheme::instance()->dpi() / 72.0)));
        return font;
    };
    const auto refresh = [&] {
        const QString name = family.edit->text();
        // The sizes: XP's for a TrueType font, the font's own otherwise
        const QString keep = size.edit->text();
        size.list->clear();
        QList<int> points = QFontDatabase::isSmoothlyScalable(name) ? QList<int>{8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72}
                                                                    : QFontDatabase::pointSizes(name);
        for (int point : std::as_const(points))
            size.list->addItem(QString::number(point));
        const auto matches = size.list->findItems(keep, Qt::MatchExactly);
        if (!matches.isEmpty())
            size.list->setCurrentItem(matches.first());
        size.edit->setText(keep);
        if (script) {
            const QString current = script->currentText();
            script->clear();
            for (QFontDatabase::WritingSystem system : QFontDatabase::writingSystems(name)) {
                if (system == QFontDatabase::Any || system == QFontDatabase::Symbol)
                    continue;
                // GDI's name for the Latin character set
                script->addItem(system == QFontDatabase::Latin ? QObject::tr("Western") : QFontDatabase::writingSystemName(system));
            }
            const int index = script->findText(current);
            script->setCurrentIndex(index < 0 ? 0 : index);
        }
        if (description && m_dialogs)
            description->setText(QFontDatabase::isSmoothlyScalable(name) ? xpfiles::string(*m_dialogs, IDS_TRUETYPE) : QString());
        if (sample) {
            QFont font = chosenFont();
            w4w::unsmoothedFont(font, font.pixelSize());
            sample->setFont(font);
            sample->setText(sample->property("words").toString());
        }
    };
    if (sample)
        sample->setProperty("words", sample->text());
    family.edit->setText(m_family);
    if (const auto found = family.list->findItems(m_family, Qt::MatchFixedString); !found.isEmpty()) {
        family.list->setCurrentItem(found.first());
        family.list->scrollToItem(found.first(), QAbstractItemView::PositionAtTop);
    }
    style.list->setCurrentRow(styleIndex(m_bold, m_italic));
    style.edit->setText(style.list->currentItem()->text());
    size.edit->setText(QString::number(m_points));
    refresh();
    for (QLineEdit *line : {family.edit, style.edit, size.edit})
        connect(line, &QLineEdit::textChanged, &dialog, refresh);
    for (QListWidget *list : {family.list, style.list, size.list})
        connect(list, &QListWidget::currentTextChanged, &dialog, refresh);
    // ChooseFont starts in the Font box, its words selected
    family.edit->setFocus();
    family.edit->selectAll();
    if (QPushButton *ok = button(page, 1)) {
        ok->setDefault(true);
        connect(ok, &QPushButton::clicked, &dialog, [&] {
            if (family.list->findItems(family.edit->text(), Qt::MatchFixedString).isEmpty()) {
                QMessageBox::warning(&dialog, page.caption, xpfiles::string(*m_dialogs, IDS_NO_FONT));
                family.edit->setFocus();
                family.edit->selectAll();
                return;
            }
            bool number = false;
            const int points = size.edit->text().toInt(&number);
            if (!number) {
                QMessageBox::warning(&dialog, page.caption, xpfiles::string(*m_dialogs, IDS_SIZE_NUMBER));
                return;
            }
            if (points < 1 || points > 1638) {
                QMessageBox::warning(&dialog, page.caption, xpfiles::string(*m_dialogs, IDS_SIZE_RANGE).arg(1).arg(1638).replace(QLatin1String("%d"), QString()));
                return;
            }
            dialog.accept();
        });
    }
    if (QPushButton *cancel = button(page, 2))
        connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_family = family.list->findItems(family.edit->text(), Qt::MatchFixedString).first()->text();
    m_points = size.edit->text().toInt();
    const int index = qMax(0, style.list->currentRow());
    m_bold = index == 1 || index == 3;
    m_italic = index == 2 || index == 3;
    applyFont();
    writeSettings();
}

void Notepad::applyFont()
{
    // ChooseFont's lfHeight: the points at the screen's DPI, in whole pixels
    QFont font(m_family);
    font.setPixelSize(qMax(1, qRound(m_points * ShellTheme::instance()->dpi() / 72.0)));
    font.setBold(m_bold);
    font.setItalic(m_italic);
    w4w::unsmoothedFont(font, font.pixelSize());
    m_edit->setFont(font);
    // The edit control's tab stops: every 32 dialog units, eight average characters
    m_edit->setTabStopDistance(8 * QFontMetricsF(font).averageCharWidth());
}

void Notepad::readSettings()
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    m_family = settings.value(QStringLiteral("lfFaceName"), m_family).toString();
    m_points = settings.value(QStringLiteral("iPointSize"), m_points).toInt();
    m_bold = settings.value(QStringLiteral("bold"), m_bold).toBool();
    m_italic = settings.value(QStringLiteral("italic"), m_italic).toBool();
    m_wrap = settings.value(QStringLiteral("fWrap"), m_wrap).toBool();
    m_statusBar = settings.value(QStringLiteral("StatusBar"), m_statusBar).toBool();
    m_header = settings.value(QStringLiteral("szHeader"), m_header).toString();
    m_footer = settings.value(QStringLiteral("szTrailer"), m_footer).toString();
    m_margins = QMarginsF(settings.value(QStringLiteral("iMarginLeft"), m_margins.left()).toDouble(),
                          settings.value(QStringLiteral("iMarginTop"), m_margins.top()).toDouble(),
                          settings.value(QStringLiteral("iMarginRight"), m_margins.right()).toDouble(),
                          settings.value(QStringLiteral("iMarginBottom"), m_margins.bottom()).toDouble());
    m_orientation = settings.value(QStringLiteral("landscape")).toBool() ? QPageLayout::Landscape : QPageLayout::Portrait;
    m_paper = settings.value(QStringLiteral("paper")).toString();
    m_printer = settings.value(QStringLiteral("printer")).toString();
}

void Notepad::writeSettings() const
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("lfFaceName"), m_family);
    settings.setValue(QStringLiteral("iPointSize"), m_points);
    settings.setValue(QStringLiteral("bold"), m_bold);
    settings.setValue(QStringLiteral("italic"), m_italic);
    settings.setValue(QStringLiteral("fWrap"), m_wrap);
    settings.setValue(QStringLiteral("StatusBar"), m_statusBar);
    settings.setValue(QStringLiteral("szHeader"), m_header);
    settings.setValue(QStringLiteral("szTrailer"), m_footer);
    settings.setValue(QStringLiteral("iMarginLeft"), m_margins.left());
    settings.setValue(QStringLiteral("iMarginTop"), m_margins.top());
    settings.setValue(QStringLiteral("iMarginRight"), m_margins.right());
    settings.setValue(QStringLiteral("iMarginBottom"), m_margins.bottom());
    settings.setValue(QStringLiteral("landscape"), m_orientation == QPageLayout::Landscape);
    settings.setValue(QStringLiteral("paper"), m_paper);
    settings.setValue(QStringLiteral("printer"), m_printer);
}

void Notepad::closeEvent(QCloseEvent *event)
{
    if (!confirmDiscard()) {
        event->ignore();
        return;
    }
    QSettings(settingsPath(), QSettings::IniFormat).setValue(QStringLiteral("geometry"), saveGeometry());
    writeSettings();
    event->accept();
}

bool Notepad::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_edit && event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->matches(QKeySequence::Undo)) {
        undo();
        return true;
    }
    // A file dropped on the text opens, as WM_DROPFILES does
    if (watched == m_edit->viewport() && (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove)) {
        auto *drag = static_cast<QDragMoveEvent *>(event);
        if (drag->mimeData()->hasUrls()) {
            drag->acceptProposedAction();
            return true;
        }
    }
    if (watched == m_edit->viewport() && event->type() == QEvent::Drop) {
        auto *drop = static_cast<QDropEvent *>(event);
        if (drop->mimeData()->hasUrls()) {
            const QString path = drop->mimeData()->urls().value(0).toLocalFile();
            drop->acceptProposedAction();
            if (!path.isEmpty() && confirmDiscard())
                load(path);
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}
