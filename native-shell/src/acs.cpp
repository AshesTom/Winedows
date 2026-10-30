// SPDX-License-Identifier: LGPL-2.1-or-later
#include "acs.h"

#include "xpfiles.h"

#include <QFile>
#include <QMouseEvent>
#include <QPainter>
#include <QRandomGenerator>
#include <QtEndian>

namespace {

const quint32 SIGNATURE = 0xabcdabc3;
const quint32 FLAG_VOICE = 0x20, FLAG_BALLOON = 0x200;
// A character is never still for long: how long it rests between two animations, and how many
// frames one may loop by its branches before it takes the way out its frames give.
const int IDLE_MIN_MS = 500, IDLE_MAX_MS = 2500, MOST_FRAMES = 600;

class Reader
{
public:
    Reader(const QByteArray &data, qint64 at) : m_data(data), at(at) {}
    bool ok() const { return m_ok; }
    template<typename T> T read()
    {
        if (at < 0 || at + qint64(sizeof(T)) > m_data.size()) {
            m_ok = false;
            return T();
        }
        const T value = qFromLittleEndian<T>(m_data.constData() + at);
        at += sizeof(T);
        return value;
    }
    QString string()
    {
        const quint32 length = read<quint32>();
        if (!length)
            return QString();
        if (at + 2 * (qint64(length) + 1) > m_data.size()) {
            m_ok = false;
            return QString();
        }
        const QString text = QString::fromUtf16(reinterpret_cast<const char16_t *>(m_data.constData() + at), length);
        at += 2 * (qint64(length) + 1);
        return text;
    }

private:
    const QByteArray &m_data;
    bool m_ok = true;

public:
    qint64 at;
};

// The character's images: literals and repeats of what came before, in a stream
// of bits read from the low end. Length is checked by the caller.
QByteArray decompress(const char *source, qint64 size, int expected)
{
    QByteArray out;
    out.reserve(expected);
    qint64 bit = 8;
    const qint64 bits = size * 8;
    const auto read = [&](int count) -> quint32 {
        quint32 value = 0;
        for (int i = 0; i < count; ++i, ++bit)
            if (bit < bits && (source[bit / 8] >> (bit % 8)) & 1)
                value |= 1u << i;
        return value;
    };
    while (bit < bits && out.size() <= expected) {
        if (!read(1)) {
            out.append(char(read(8)));
            continue;
        }
        int length = 2;
        quint32 offset;
        if (read(1)) {
            if (read(1)) {
                if (read(1)) {
                    offset = read(20);
                    if (offset == 0xfffff)
                        break;
                    offset += 4673;
                    length = 3;
                } else {
                    offset = read(12) + 577;
                }
            } else {
                offset = read(9) + 65;
            }
        } else {
            offset = read(6) + 1;
        }
        int ones = 0;
        while (ones < 16 && read(1))
            ++ones;
        if (ones)
            length += (1 << ones) - 1 + int(read(ones));
        if (offset > quint32(out.size()) || out.size() + length > expected)
            return QByteArray();
        for (int i = 0; i < length; ++i)
            out.append(out.at(out.size() - offset));
    }
    return out;
}

} // namespace

std::shared_ptr<AcsCharacter> AcsCharacter::load(const QString &path)
{
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly))
        return nullptr;
    auto character = std::make_shared<AcsCharacter>();
    character->m_data = file.readAll();
    const QByteArray &data = character->m_data;
    Reader header(data, 0);
    if (header.read<quint32>() != SIGNATURE)
        return nullptr;
    const quint32 infoAt = header.read<quint32>();
    header.read<quint32>();
    const quint32 animationsAt = header.read<quint32>();
    header.read<quint32>();
    const quint32 imagesAt = header.read<quint32>();
    if (!header.ok())
        return nullptr;

    // The character: its size, its transparent colour and its palette
    Reader info(data, infoAt);
    info.at += 4 + 8 + 16;
    const int width = info.read<quint16>(), height = info.read<quint16>();
    character->m_size = QSize(width, height);
    character->m_transparent = info.read<quint8>();
    const quint32 flags = info.read<quint32>();
    info.at += 4;
    if (flags & FLAG_VOICE) {
        info.at += 16 + 16 + 4 + 2;
        if (info.read<quint8>()) {
            info.at += 2;
            info.string();
            info.at += 6;
            info.string();
        }
    }
    if (flags & FLAG_BALLOON) {
        info.at += 2 + 12;
        info.string();
        info.at += 10;
    }
    const quint32 colours = info.read<quint32>();
    if (!info.ok() || colours > 256 || !width || !height)
        return nullptr;
    for (quint32 i = 0; i < colours; ++i) {
        const quint32 bgra = info.read<quint32>();
        character->m_palette.append(qRgb((bgra >> 16) & 255, (bgra >> 8) & 255, bgra & 255));
    }

    Reader images(data, imagesAt);
    const quint32 imageCount = images.read<quint32>();
    for (quint32 i = 0; i < imageCount && images.ok(); ++i) {
        character->m_images.append(images.read<quint32>());
        images.at += 8;
    }

    Reader list(data, animationsAt);
    const quint32 animationCount = list.read<quint32>();
    for (quint32 i = 0; i < animationCount && list.ok(); ++i) {
        const QString name = list.string().toLower();
        const quint32 at = list.read<quint32>();
        list.read<quint32>();
        Reader animation(data, at);
        animation.string();
        animation.read<quint8>();
        animation.string();
        const int frameCount = animation.read<quint16>();
        QList<Frame> frames;
        for (int f = 0; f < frameCount && animation.ok(); ++f) {
            Frame frame;
            const int layers = animation.read<quint16>();
            for (int l = 0; l < layers && animation.ok(); ++l) {
                const int index = int(animation.read<quint32>());
                const int x = animation.read<qint16>(), y = animation.read<qint16>();
                frame.layers.append({index, x, y});
            }
            animation.read<quint16>();    // its sound
            frame.duration = animation.read<quint16>() * 10;
            frame.exit = animation.read<qint16>();
            const int branches = animation.read<quint8>();
            for (int b = 0; b < branches && animation.ok(); ++b) {
                const int to = animation.read<quint16>(), probability = animation.read<quint16>();
                frame.branches.append({to, probability});
            }
            const int overlays = animation.read<quint8>();
            for (int o = 0; o < overlays && animation.ok(); ++o) {
                animation.at += 2 + 2 + 1;
                const bool region = animation.read<quint8>();
                animation.at += 8;
                if (region)
                    animation.at += animation.read<quint32>();
            }
            frames.append(frame);
        }
        if (animation.ok())
            character->m_animations.insert(name, frames);
    }
    return character->m_animations.isEmpty() ? nullptr : character;
}

QImage AcsCharacter::image(int index) const
{
    if (const auto found = m_cache.constFind(index); found != m_cache.constEnd())
        return *found;
    QImage out;
    if (index >= 0 && index < m_images.size()) {
        Reader header(m_data, m_images[index]);
        header.read<quint8>();
        const int width = header.read<quint16>(), height = header.read<quint16>();
        const bool compressed = header.read<quint8>();
        const qint64 size = header.read<quint32>();
        const int stride = (width + 3) & ~3, expected = stride * height;
        if (header.ok() && header.at + size <= m_data.size() && width && height) {
            const QByteArray pixels = compressed ? decompress(m_data.constData() + header.at, size, expected)
                                                 : m_data.mid(header.at, expected);
            if (pixels.size() == expected) {
                out = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
                // From the bottom row up
                for (int y = 0; y < height; ++y) {
                    const auto *from = reinterpret_cast<const uchar *>(pixels.constData()) + (height - 1 - y) * stride;
                    auto *to = reinterpret_cast<QRgb *>(out.scanLine(y));
                    for (int x = 0; x < width; ++x)
                        to[x] = from[x] == m_transparent || from[x] >= m_palette.size() ? 0 : m_palette[from[x]];
                }
            }
        }
    }
    m_cache.insert(index, out);
    return out;
}

QImage AcsCharacter::compose(const Frame &frame) const
{
    QImage out(m_size, QImage::Format_ARGB32_Premultiplied);
    out.fill(0);
    QPainter painter(&out);
    // The first layer is on top.
    for (int i = int(frame.layers.size()) - 1; i >= 0; --i)
        painter.drawImage(frame.layers[i].x, frame.layers[i].y, image(frame.layers[i].image));
    return out;
}

AgentWidget::AgentWidget(QWidget *parent)
    : QWidget(parent)
    , m_character(AcsCharacter::load(xpfiles::find(QStringLiteral("rover.acs"))))
{
    setAttribute(Qt::WA_TranslucentBackground);
    if (m_character)
        setFixedSize(m_character->size());
    m_frameTimer.setSingleShot(true);
    m_idleTimer.setSingleShot(true);
    connect(&m_frameTimer, &QTimer::timeout, this, &AgentWidget::step);
    connect(&m_idleTimer, &QTimer::timeout, this, &AgentWidget::idle);
}

void AgentWidget::play(const QString &name)
{
    if (!m_character || !m_character->hasAnimation(name))
        return;
    m_frames = m_character->animation(name);
    m_at = -1;
    m_played = 0;
    m_leaving = false;
    m_idleTimer.stop();
    step();
}

void AgentWidget::step()
{
    if (m_frames.isEmpty())
        return;
    int next = m_at + 1;
    if (++m_played > MOST_FRAMES)
        m_leaving = true;
    if (m_leaving) {
        // Out by the frames meant for it, or at once if they lead nowhere.
        if (m_at >= 0 && m_at < m_frames.size() && m_frames[m_at].exit >= 0)
            next = m_frames[m_at].exit;
        if (m_played > 2 * MOST_FRAMES)
            next = int(m_frames.size());
    } else if (m_at >= 0 && m_at < m_frames.size() && !m_frames[m_at].branches.isEmpty()) {
        int roll = QRandomGenerator::global()->bounded(100);
        for (const AcsCharacter::Branch &branch : m_frames[m_at].branches) {
            if (roll < branch.probability) {
                next = branch.frame;
                break;
            }
            roll -= branch.probability;
        }
    }
    if (next >= m_frames.size()) {
        m_frames.clear();
        if (m_character->hasAnimation(QStringLiteral("RestPose"))) {
            const auto rest = m_character->animation(QStringLiteral("RestPose"));
            if (!rest.isEmpty())
                m_shown = m_character->compose(rest.first());
        }
        update();
        m_idleTimer.start(QRandomGenerator::global()->bounded(IDLE_MIN_MS, IDLE_MAX_MS));
        return;
    }
    m_at = next;
    const AcsCharacter::Frame &frame = m_frames[m_at];
    if (!frame.layers.isEmpty()) {
        m_shown = m_character->compose(frame);
        update();
    }
    m_frameTimer.start(qMax(frame.duration, 10));
}

void AgentWidget::idle()
{
    play(QStringLiteral("Idle"));
}

void AgentWidget::showEvent(QShowEvent *)
{
    if (m_character && m_shown.isNull())
        play(QStringLiteral("Show"));
    else if (m_character && !m_frameTimer.isActive())
        m_idleTimer.start(IDLE_MIN_MS);
}

void AgentWidget::hideEvent(QHideEvent *)
{
    m_frameTimer.stop();
    m_idleTimer.stop();
}

void AgentWidget::paintEvent(QPaintEvent *)
{
    if (!m_shown.isNull())
        QPainter(this).drawImage(0, 0, m_shown);
}

void AgentWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
        emit clicked();
}
