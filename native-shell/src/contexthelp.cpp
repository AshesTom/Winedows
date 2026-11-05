// SPDX-License-Identifier: LGPL-2.1-or-later
#include "contexthelp.h"

#include "xpfiles.h"

#include <QApplication>
#include <QFile>
#include <QHash>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QWhatsThis>
#include <QWidget>
#include <QtEndian>

#include <algorithm>
#include <map>
#include <memory>

namespace contexthelp {
namespace {

const char *const HELP_ID = "win4wineHelpId", *const HELP_FILE = "win4wineHelpFile";

quint16 u16(const QByteArray &data, qint64 at)
{
    return at >= 0 && at + 2 <= data.size() ? qFromLittleEndian<quint16>(data.constData() + at) : 0;
}

quint32 u32(const QByteArray &data, qint64 at)
{
    return at >= 0 && at + 4 <= data.size() ? qFromLittleEndian<quint32>(data.constData() + at) : 0;
}

// WinHelp's compressed numbers: the low bit says whether the value takes the short or the long form.
quint32 scanWord(const QByteArray &data, qint64 &at)
{
    if (at >= data.size())
        return 0;
    if (uchar(data.at(at)) & 1) {
        at += 2;
        return u16(data, at - 2) >> 1;
    }
    return uchar(data.at(at++)) >> 1;
}

qint32 scanInt(const QByteArray &data, qint64 &at)
{
    const qint64 start = at;
    const qint32 value = qint32(scanWord(data, at));
    return value - (at - start == 2 ? 0x4000 : 0x40);
}

quint32 scanLong(const QByteArray &data, qint64 &at)
{
    if (at >= data.size())
        return 0;
    if (uchar(data.at(at)) & 1) {
        at += 4;
        return u32(data, at - 4) >> 1;
    }
    at += 2;
    return u16(data, at - 2) >> 1;
}

// LZ77 as WinHelp packs |TOPIC and |PhrImage: a control byte, then for each of its bits a literal
// byte (0) or a 12-bit distance and a 4-bit length (1).
QByteArray lz77(const QByteArray &data, qint64 from, qint64 to, qint64 limit)
{
    QByteArray out;
    out.reserve(int(limit));
    qint64 at = from;
    while (at < to && out.size() < limit) {
        const uchar control = uchar(data.at(at++));
        for (int bit = 0; bit < 8 && at < to; ++bit) {
            if (control & (1 << bit)) {
                if (at + 1 >= to)
                    return out;
                const quint16 word = u16(data, at);
                at += 2;
                const int length = (word >> 12) + 3, back = (word & 0xfff) + 1;
                for (int i = 0; i < length; ++i)
                    out.append(back <= out.size() ? out.at(out.size() - back) : '\0');
            } else {
                out.append(data.at(at++));
            }
        }
    }
    return out;
}

struct Run
{
    QByteArray text;   // Windows-1252, as the file and the raster font have it
    bool bold = false;
};
using Paragraph = QList<Run>;

// A WinHelp 4 file (the format HCW writes): its internal files, the topics that |CTXOMAP numbers,
// and their paragraphs as runs of regular and bold text.
class HelpFile
{
public:
    explicit HelpFile(const QString &path)
    {
        QFile file(path);
        if (path.isEmpty() || !file.open(QIODevice::ReadOnly))
            return;
        m_data = file.readAll();
        if (u32(m_data, 0) != 0x00035F3F)
            return;
        m_files = directory(u32(m_data, 4));
        readFonts();
        readPhrases();
        readTopics();
    }

    QList<Paragraph> popup(int id) const
    {
        const auto mapped = m_map.constFind(id);
        if (mapped == m_map.constEnd() || m_topics.empty())
            return {};
        // the topic holding that offset (the few numbers that point inside one show it whole)
        auto topic = m_topics.upper_bound(*mapped);
        if (topic == m_topics.begin())
            return {};
        --topic;
        return topic->second;
    }

private:
    QByteArray file(const QString &name) const
    {
        const auto at = m_files.constFind(name);
        if (at == m_files.constEnd())
            return {};
        // FILEHEADER: reserved space, used space, flags
        return m_data.mid(*at + 9, u32(m_data, *at + 4));
    }

    // The directory's B+ tree: its leaf pages hold each file's name and offset.
    QHash<QString, qint64> directory(qint64 at) const
    {
        QHash<QString, qint64> files;
        const qint64 tree = at + 9;
        if (u16(m_data, tree) != 0x293B)
            return files;
        const int pageSize = u16(m_data, tree + 4);
        int page = qint16(u16(m_data, tree + 26));
        const int levels = qint16(u16(m_data, tree + 32));
        const qint64 pages = tree + 38;
        for (int level = 1; level < levels; ++level)
            page = qint16(u16(m_data, pages + qint64(page) * pageSize + 4));
        for (int guard = 0; page != -1 && guard < 10000; ++guard) {
            const qint64 base = pages + qint64(page) * pageSize;
            const int count = qint16(u16(m_data, base + 2));
            qint64 entry = base + 8;
            for (int i = 0; i < count; ++i) {
                const qint64 end = m_data.indexOf('\0', entry);
                if (end < 0)
                    return files;
                files.insert(QString::fromLatin1(m_data.mid(entry, end - entry)), u32(m_data, end + 1));
                entry = end + 5;
            }
            page = qint16(u16(m_data, base + 6));
        }
        return files;
    }

    void readFonts()
    {
        const QByteArray fonts = file(QStringLiteral("|FONT"));
        const int count = u16(fonts, 2), offset = u16(fonts, 6);
        // the old descriptor: attributes (1 bold), half points, family, face, colours
        for (int i = 0; i < count; ++i)
            m_bold.append(offset + i * 11 < fonts.size() && (uchar(fonts.at(offset + i * 11)) & 1));
    }

    // Hall compression's phrases: their lengths bit-coded in |PhrIndex, their bytes in |PhrImage.
    void readPhrases()
    {
        const QByteArray index = file(QStringLiteral("|PhrIndex"));
        if (index.size() < 28)
            return;
        const int entries = int(u32(index, 4));
        const quint32 imageSize = u32(index, 12), packedSize = u32(index, 16);
        const int bits = u16(index, 24) & 0xf;
        qint64 at = 28;
        quint32 word = 0;
        int left = 0;
        const auto bit = [&] {
            if (!left) {
                word = u32(index, at);
                at += 4;
                left = 32;
            }
            const bool value = word & 1;
            word >>= 1;
            --left;
            return value;
        };
        QList<int> offsets{0};
        for (int i = 0; i < entries; ++i) {
            int length = 1;
            while (bit())
                length += 1 << bits;
            for (int k = 0; k < bits; ++k)
                if (bit())
                    length += 1 << k;
            offsets.append(offsets.last() + length);
        }
        QByteArray image = file(QStringLiteral("|PhrImage"));
        if (imageSize != packedSize)
            image = lz77(image, 0, image.size(), imageSize);
        for (int i = 0; i < entries; ++i)
            m_phrases.append(image.mid(offsets.at(i), offsets.at(i + 1) - offsets.at(i)));
    }

    QByteArray unhall(const QByteArray &data) const
    {
        QByteArray out;
        for (qint64 i = 0; i < data.size();) {
            const uchar c = uchar(data.at(i++));
            if (!(c & 1)) {
                out += m_phrases.value(c / 2);
            } else if ((c & 3) == 1) {
                out += m_phrases.value(128 + (c / 4) * 256 + (i < data.size() ? uchar(data.at(i)) : 0));
                ++i;
            } else if ((c & 7) == 3) {
                out += data.mid(i, c / 8 + 1);
                i += c / 8 + 1;
            } else if ((c & 15) == 7) {
                out += QByteArray(c / 16 + 1, ' ');
            } else {
                out += QByteArray(c / 16 + 1, '\0');
            }
        }
        return out;
    }

    // |TOPIC in 4 KB blocks, each LZ77-packed after its 12-byte header and unpacked to 16 KB less
    // it; a TOPICPOS is the block (bits 14 up) and the place in it, header counted. The records
    // chain by NextBlock; a topic header (type 2) starts a topic, display records (0x20, 0x23) hold
    // its paragraphs. A |CTXOMAP number gives a TOPICOFFSET: 0x8000 a block, plus the characters
    // (TopicLength) of the records before it in that block.
    void readTopics()
    {
        const QByteArray system = file(QStringLiteral("|SYSTEM"));
        const int flags = u16(system, 10);
        const QByteArray topic = file(QStringLiteral("|TOPIC"));
        const qint64 blockSize = flags == 8 ? 2048 : 4096, unpacked = 0x4000 - 12;
        QByteArray stream;
        for (qint64 at = 0; at < topic.size(); at += blockSize) {
            QByteArray block = flags == 4 || flags == 8 ? lz77(topic, at + 12, qMin(topic.size(), at + blockSize), unpacked)
                                                         : topic.mid(at + 12, blockSize - 12);
            block.resize(unpacked, '\0');
            stream += block;
        }
        const auto place = [&](quint32 pos) { return qint64(pos >> 14) * unpacked + qint64(pos & 0x3fff) - 12; };
        quint32 pos = u32(topic, 4);
        qint64 counter = 0;
        quint32 block = quint32(-1);
        QList<Paragraph> *current = nullptr;
        for (int guard = 0; guard < 1000000; ++guard) {
            const qint64 at = place(pos);
            if (at < 0 || at + 21 > stream.size())
                break;
            const qint32 size = qint32(u32(stream, at)), length2 = qint32(u32(stream, at + 4));
            const quint32 next = u32(stream, at + 12);
            const qint32 length1 = qint32(u32(stream, at + 16));
            const uchar kind = uchar(stream.at(at + 20));
            if (pos >> 14 != block) {
                block = pos >> 14;
                counter = qint64(block) * 0x8000;
            }
            if (kind == 2) {
                current = &m_topics[counter];
            } else if ((kind == 0x20 || kind == 0x23) && current) {
                const QByteArray data1 = stream.mid(at + 21, length1 - 21);
                QByteArray data2 = stream.mid(at + length1, size - length1);
                if (length2 > data2.size())
                    data2 = unhall(data2);
                qint64 i = 0;
                scanLong(data1, i);
                counter += scanWord(data1, i);
                if (kind == 0x20)
                    paragraphs(data1, i, data2, *current);
            }
            if (qint32(next) <= 0 || next == pos)
                break;
            pos = next;
        }
        const QByteArray map = file(QStringLiteral("|CTXOMAP"));
        for (int i = 0, count = u16(map, 0); i < count; ++i)
            m_map.insert(qint32(u32(map, 2 + 8 * i)), u32(map, 6 + 8 * i));
    }

    // A display record's paragraph info (two bytes, an ID, a bit field and what it says is there),
    // then its commands, each after the next of LinkData2's NUL-separated strings: 0x80 a font,
    // 0x81 a line break, 0x82 the end of a paragraph, 0xFF the end.
    void paragraphs(const QByteArray &data1, qint64 i, const QByteArray &data2, QList<Paragraph> &out) const
    {
        i += 4;
        const quint16 bits = u16(data1, i);
        i += 2;
        if (bits & 0x1)
            scanLong(data1, i);
        for (int field : {0x2, 0x4, 0x8, 0x10, 0x20, 0x40})
            if (bits & field)
                scanInt(data1, i);
        if (bits & 0x100)
            i += 3;
        if (bits & 0x200) {
            const int tabs = scanInt(data1, i);
            for (int t = 0; t < tabs; ++t)
                if (scanWord(data1, i) & 0x4000)
                    scanWord(data1, i);
        }
        const QList<QByteArray> pieces = data2.split('\0');
        Paragraph paragraph;
        bool bold = false;
        const auto add = [&](const QByteArray &text) {
            if (text.isEmpty())
                return;
            if (!paragraph.isEmpty() && paragraph.last().bold == bold)
                paragraph.last().text += text;
            else
                paragraph.append({text, bold});
        };
        for (int piece = 0; i < data1.size(); ++piece) {
            add(pieces.value(piece));
            const uchar command = uchar(data1.at(i++));
            if (command == 0xff)
                break;
            switch (command) {
            case 0x80:
                bold = m_bold.value(u16(data1, i));
                i += 2;
                break;
            case 0x81:
                add("\n");
                break;
            case 0x82:
                if (!paragraph.isEmpty())
                    out.append(paragraph);
                paragraph.clear();
                break;
            case 0x83:
            case 0x8b:
                add(" ");
                break;
            case 0x8c:
                add("-");
                break;
            case 0x20:
                i += 4;
                break;
            case 0x21:
                i += 2;
                break;
            case 0xe2: case 0xe3: case 0xe6: case 0xe7:
                i += 4;
                break;
            case 0xc8: case 0xcc: case 0xea: case 0xeb: case 0xee: case 0xef:
                i += 2 + u16(data1, i);
                break;
            default:
                break;
            }
        }
        if (!paragraph.isEmpty())
            out.append(paragraph);
    }

    QByteArray m_data;
    QHash<QString, qint64> m_files;
    QList<bool> m_bold;
    QList<QByteArray> m_phrases;
    std::map<quint32, QList<Paragraph>> m_topics;
    QHash<qint32, quint32> m_map;
};

const HelpFile &helpFile(const QString &name)
{
    static QHash<QString, std::shared_ptr<HelpFile>> files;
    std::shared_ptr<HelpFile> &file = files[name.toLower()];
    if (!file)
        file = std::make_shared<HelpFile>(xpfiles::find(name));
    return *file;
}

// MS Sans Serif at 8 points (sserife.fon's 13-pixel font, the help file's face): its glyphs by
// their Windows-1252 code, from the .fon's FNT 2.0 or 3.0 resource.
class RasterFont
{
public:
    RasterFont()
    {
        QFile file(xpfiles::find(QStringLiteral("sserife.fon")));
        if (!file.open(QIODevice::ReadOnly))
            return;
        const QByteArray data = file.readAll();
        const qint64 ne = u32(data, 0x3c);
        const qint64 table = ne + u16(data, ne + 0x24);
        const int shift = u16(data, table);
        for (qint64 at = table + 2; u16(data, at); ) {
            const int kind = u16(data, at), count = u16(data, at + 2);
            at += 8;
            for (int i = 0; i < count; ++i, at += 12)
                if (kind == 0x8008 && read(data, qint64(u16(data, at)) << shift))
                    return;
        }
    }

    bool isNull() const { return m_glyphs.isEmpty(); }
    int height() const { return m_height; }
    int ascent() const { return m_ascent; }
    // GDI's simulated bold: each glyph drawn again a pixel to the right, a pixel wider
    int advance(uchar code, bool bold) const { return glyph(code).width() + (bold ? 1 : 0); }

    void draw(QPainter &painter, int x, int y, uchar code, bool bold) const
    {
        const QImage image = glyph(code);
        painter.drawImage(x, y, image);
        if (bold)
            painter.drawImage(x + 1, y, image);
    }

private:
    QImage glyph(uchar code) const
    {
        return m_glyphs.value(code, m_glyphs.value(m_default));
    }

    bool read(const QByteArray &data, qint64 start)
    {
        const int version = u16(data, start);
        if ((version != 0x200 && version != 0x300) || u16(data, start + 68) != 8 || u16(data, start + 70) != 96)
            return false;
        m_height = u16(data, start + 88);
        m_ascent = u16(data, start + 74);
        const int first = uchar(data.at(start + 95)), last = uchar(data.at(start + 96));
        m_default = uchar(first + uchar(data.at(start + 97)));
        const qint64 entries = start + (version == 0x300 ? 148 : 118);
        const int entrySize = version == 0x300 ? 6 : 4;
        for (int code = first; code <= last; ++code) {
            const qint64 entry = entries + (code - first) * entrySize;
            const int width = u16(data, entry);
            const qint64 bits = start + (version == 0x300 ? u32(data, entry + 2) : u16(data, entry + 2));
            const int columns = (width + 7) / 8;
            if (bits + columns * m_height > data.size())
                continue;
            QImage image(width, m_height, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            for (int column = 0; column < columns; ++column)
                for (int row = 0; row < m_height; ++row) {
                    const uchar byte = uchar(data.at(bits + column * m_height + row));
                    for (int x = 0; x < 8 && column * 8 + x < width; ++x)
                        if (byte & (0x80 >> x))
                            image.setPixel(column * 8 + x, row, 0xff000000);
                }
            m_glyphs.insert(uchar(code), image);
        }
        return !m_glyphs.isEmpty();
    }

    QHash<uchar, QImage> m_glyphs;
    uchar m_default = '?';
    int m_height = 0, m_ascent = 0;
};

const RasterFont &popupFont()
{
    static const RasterFont font;
    return font;
}

// XP's WinHelp popup, measured against XP's winhlp32 at 1024 x 768 (help numbers 6004, 6005, 6026
// and 6027): a black line, a grey one inside it, then the popup's yellow. The text 17 pixels in from
// the yellow and its first line 6 down, its lines 15 apart (the paragraphs' exact spacing of 23 half
// points); a line ends before a word whose ink would pass 297 pixels. The yellow is 5 pixels wider
// than the widest line with its trailing space, and 5 taller than the last line. The shadow: every
// other pixel black, 6 wide, 6 down and right.
const QRgb FILL = qRgb(255, 255, 238), INNER = qRgb(192, 192, 192);
const int LEFT = 17, RIGHT = 5, TOP = 6, BOTTOM = 5, PITCH = 15, WRAP = 297, SHADOW = 6;

struct Word
{
    QList<Run> runs;   // a word can change font inside
    int width = 0;
};

class Popup : public QWidget
{
public:
    Popup(const QList<Paragraph> &paragraphs, const QPoint &at)
        : QWidget(nullptr, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
    {
        setProperty("win4wineShadow", true);   // its own shadow, not the menus'
        setAttribute(Qt::WA_DeleteOnClose);
        layOut(paragraphs);
        const QSize box(m_textWidth + LEFT + RIGHT + 4, m_lines.size() * PITCH + TOP + BOTTOM + 4);
        resize(box + QSize(SHADOW, SHADOW));
        QPoint place = at;
        if (QScreen *screen = QGuiApplication::screenAt(at)) {
            const QRect area = screen->geometry();
            place.setX(qBound(area.left(), place.x(), area.right() + 1 - width()));
            place.setY(qBound(area.top(), place.y(), area.bottom() + 1 - height()));
            m_beneath = screen->grabWindow(0, place.x(), place.y(), width(), height()).toImage();
        }
        move(place);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const QRect box(0, 0, width() - SHADOW, height() - SHADOW);
        if (!m_beneath.isNull())
            painter.drawImage(0, 0, m_beneath);
        else
            painter.fillRect(rect(), Qt::white);
        // the shadow: every other pixel black, over what was there
        QImage pattern(2, 2, QImage::Format_ARGB32_Premultiplied);
        pattern.fill(Qt::transparent);
        pattern.setPixel(1, 0, 0xff000000);
        pattern.setPixel(0, 1, 0xff000000);
        const QBrush checks(pattern);
        painter.setBrushOrigin(0, 0);
        painter.fillRect(QRect(box.right() + 1, SHADOW, SHADOW, box.height()), checks);
        painter.fillRect(QRect(SHADOW, box.bottom() + 1, box.width() - SHADOW, SHADOW), checks);
        painter.fillRect(box, Qt::black);
        painter.fillRect(box.adjusted(1, 1, -1, -1), QColor(INNER));
        painter.fillRect(box.adjusted(2, 2, -2, -2), QColor(FILL));
        const RasterFont &font = popupFont();
        int y = 2 + TOP;
        for (const QList<Word> &line : std::as_const(m_lines)) {
            int x = 2 + LEFT;
            for (const Word &word : line) {
                for (const Run &run : word.runs)
                    for (const char c : run.text) {
                        font.draw(painter, x, y, uchar(c), run.bold);
                        x += font.advance(uchar(c), run.bold);
                    }
            }
            y += PITCH;
        }
    }

    void keyPressEvent(QKeyEvent *) override { close(); }

private:
    // Words and the spaces after them, wrapped at WRAP; a line break or a new paragraph starts a line.
    void layOut(const QList<Paragraph> &paragraphs)
    {
        const RasterFont &font = popupFont();
        const auto measure = [&](const Run &run) {
            int width = 0;
            for (const char c : run.text)
                width += font.advance(uchar(c), run.bold);
            return width;
        };
        for (const Paragraph &paragraph : paragraphs) {
            QList<Word> words;
            Word word;
            bool space = false;
            for (const Run &run : paragraph)
                for (const char c : run.text) {
                    if (c == '\n') {
                        words.append(word);
                        words.append(Word());   // an empty word: the break
                        word = Word();
                        space = false;
                        continue;
                    }
                    if (c != ' ' && space) {
                        words.append(word);
                        word = Word();
                        space = false;
                    }
                    if (c == ' ')
                        space = true;
                    if (word.runs.isEmpty() || word.runs.last().bold != run.bold)
                        word.runs.append({QByteArray(), run.bold});
                    word.runs.last().text += c;
                }
            words.append(word);
            QList<Word> line;
            int lineWidth = 0;
            const auto finish = [&] {
                m_lines.append(line);
                m_textWidth = qMax(m_textWidth, lineWidth);
                line.clear();
                lineWidth = 0;
            };
            for (Word &w : words) {
                if (w.runs.isEmpty()) {
                    finish();
                    continue;
                }
                w.width = 0;
                int ink = 0;   // without the spaces after it
                for (const Run &run : w.runs)
                    w.width += measure(run);
                Run trimmed = w.runs.last();
                while (trimmed.text.endsWith(' '))
                    trimmed.text.chop(1);
                ink = w.width - measure(w.runs.last()) + measure(trimmed);
                if (!line.isEmpty() && lineWidth + ink > WRAP)
                    finish();
                line.append(w);
                lineWidth += w.width;
            }
            if (!line.isEmpty())
                finish();
        }
    }

    QList<QList<Word>> m_lines;
    int m_textWidth = 0;
    QImage m_beneath;
};

QPointer<QWidget> &shown()
{
    static QPointer<QWidget> popup;
    return popup;
}

// While Qt's "What's This?" mode is on (after the title bar's "?"), a click on a control with a
// help number shows its popup; F1 shows the focused control's.
class Watcher : public QObject
{
public:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (!object->isWidgetType())
            return false;
        auto *widget = static_cast<QWidget *>(object);
        switch (event->type()) {
        case QEvent::QueryWhatsThis:
            if (helped(widget)) {
                event->accept();
                return true;
            }
            break;
        case QEvent::WhatsThis:
            if (QWidget *control = helped(widget)) {
                // Qt's own text would end the mode: this popup does too, as XP's WinHelp ends it
                QWhatsThis::leaveWhatsThisMode();
                showFor(control, static_cast<QHelpEvent *>(event)->globalPos());
                event->accept();
                return true;
            }
            break;
        case QEvent::KeyPress:
            // any key closes the popup, wherever the keyboard is
            if (shown()) {
                shown()->close();
                return true;
            }
            if (static_cast<QKeyEvent *>(event)->key() == Qt::Key_F1 && widget == QApplication::focusWidget())
                if (QWidget *control = helped(widget)) {
                    showFor(control, control->mapToGlobal(QPoint(control->width() / 2, control->height())));
                    return true;
                }
            break;
        default:
            break;
        }
        return false;
    }

private:
    static QWidget *helped(QWidget *widget)
    {
        for (; widget; widget = widget->isWindow() ? nullptr : widget->parentWidget())
            if (widget->property(HELP_ID).isValid())
                return widget;
        return nullptr;
    }

    static void showFor(QWidget *control, const QPoint &at)
    {
        const int id = control->property(HELP_ID).toInt();
        if (id != -1)
            show(control->property(HELP_FILE).toString(), id, at);
    }
};

} // namespace

void attach(const xpdialog::Page &page, const QList<QPair<int, int>> &ids, const QString &file)
{
    static Watcher *watcher = [] {
        auto *created = new Watcher;
        qApp->installEventFilter(created);
        return created;
    }();
    Q_UNUSED(watcher);
    for (const auto &[control, help] : ids)
        if (QWidget *widget = page.controls.value(control)) {
            widget->setProperty(HELP_ID, help);
            widget->setProperty(HELP_FILE, file);
        }
}

bool show(const QString &file, int id, const QPoint &globalPos)
{
    const QList<Paragraph> paragraphs = helpFile(file).popup(id);
    if (paragraphs.isEmpty() || popupFont().isNull())
        return false;
    if (shown())
        shown()->close();
    auto *popup = new Popup(paragraphs, globalPos);
    shown() = popup;
    popup->show();
    return true;
}

} // namespace contexthelp
