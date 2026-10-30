// SPDX-License-Identifier: LGPL-2.1-or-later
// A player for the animated characters of Microsoft Agent: it reads a .acs file
// of the user's own XP and shows its frames. Nothing of a character is in this
// repository; without the file there is simply no character.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QList>
#include <QTimer>
#include <QWidget>
#include <memory>
#include <optional>

class AcsCharacter
{
public:
    struct Layer { int image; int x, y; };
    struct Branch { int frame, probability; };
    struct Frame { QList<Layer> layers; int duration; int exit; QList<Branch> branches; };

    static std::shared_ptr<AcsCharacter> load(const QString &path);
    QSize size() const { return m_size; }
    bool hasAnimation(const QString &name) const { return m_animations.contains(name.toLower()); }
    QList<Frame> animation(const QString &name) const { return m_animations.value(name.toLower()); }
    QImage compose(const Frame &frame) const;

private:
    QImage image(int index) const;

    QByteArray m_data;
    QSize m_size;
    int m_transparent = 0;
    QList<QRgb> m_palette;
    QList<qint64> m_images;
    QHash<QString, QList<Frame>> m_animations;
    mutable QHash<int, QImage> m_cache;
};

// The character standing in a window, between its animations at rest.
class AgentWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AgentWidget(QWidget *parent = nullptr);
    bool loaded() const { return bool(m_character); }
    // Plays an animation it has, then goes back to rest.
    void play(const QString &name);

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void step();
    void idle();

    std::shared_ptr<AcsCharacter> m_character;
    QList<AcsCharacter::Frame> m_frames;
    int m_at = 0, m_played = 0;
    bool m_leaving = false;
    QImage m_shown;
    QTimer m_frameTimer, m_idleTimer;
};
