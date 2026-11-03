// SPDX-License-Identifier: LGPL-2.1-or-later
// Windows Media Player 9 as a Linux program. Its full mode is what wmp.dll makes of the skins in the
// user's wmploc.dll (MainAppSkin.wsz, with Now Playing, its visualizations and its playlist as skins
// of their own); wmpengine.cpp draws and drives them, this file is the player they speak to:
// Qt Multimedia plays the files, the player's objects (player, controls, settings, currentMedia,
// mediacenter, app...) are wmp.dll's as its type library and its scripts know them, with the
// defaults wmp.dll gives its settings. Its menus are wmploc.dll's (menu 102).
#pragma once

#include "msstyles.h"
#include "wmpengine.h"

#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <optional>

class QAudioBufferOutput;
class QAudioOutput;
class QMediaPlayer;
class QMenu;
class QVideoSink;

class MediaPlayer : public wmp::Model
{
    Q_OBJECT
public:
    explicit MediaPlayer(const QStringList &files = {});
    ~MediaPlayer() override;
    // Whether the user's XP files have what the player needs.
    static bool available();
    void show();
    void open(const QStringList &files, bool play = true);

    QHash<QString, QString> globals() const override;
    QString eventSource(const QString &tag) const override;
    const wmpskin::Properties &properties() const override { return m_properties; }
    QVariant get(const QString &object, const QString &name) override;
    bool set(const QString &object, const QString &name, const QVariant &value) override;
    bool isMethod(const QString &object, const QString &name) const override;
    QVariant call(const QString &object, const QString &name, const QVariantList &arguments) override;
    std::optional<QStringList> eventArguments(const QString &object, const QString &event) const override;
    QImage image(const QString &url, const QString &skinFolder) override;
    QString string(const QString &url) override;
    QString script(const QString &url, const QString &skinFolder) override;
    std::optional<wmpskin::Node> task(QString *folder) override;
    std::optional<wmpskin::Node> plugin(const QString &objectId) override;
    QImage videoFrame() const override { return m_frame; }
    QList<float> spectrum() const override { return m_spectrum; }
    QString preference(const QString &name) override;
    void setPreference(const QString &name, const QString &value) override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Item
    {
        QUrl url;
        QString title, artist, album;
        qint64 duration = 0;   // ms, once known
    };

    std::optional<wmpskin::Node> skin(const QString &name) const;
    QString text(int id) const;
    QVariant mediaProperty(int index, const QString &name);
    QVariant mediaCall(int index, const QString &name, const QVariantList &arguments);
    bool controlAvailable(const QString &name) const;
    void setPlayState(int state);
    void setOpenState(int state);
    void playIndex(int index);
    void advance(int step);
    void command(int id);
    void showMenu(const QString &name);
    void windowRequest(const QString &request);
    void openDialog();
    void updateSpectrum(const class QAudioBuffer &buffer);
    void calcLayout(int width, int height, bool captions, bool banner);
    void changedAll(const QString &object, std::initializer_list<const char *> names);
    static QString timeString(double seconds);

    std::optional<w4w::PeResources> m_loc, m_wmp;
    wmpskin::Properties m_properties;
    QPointer<wmp::SkinView> m_window;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audio = nullptr;
    QVideoSink *m_video = nullptr;
    QAudioBufferOutput *m_buffers = nullptr;
    QImage m_frame;
    QList<float> m_spectrum;
    QList<Item> m_items;
    int m_current = -1;
    int m_playState = 0;   // WMPPlayState
    int m_openState = 0;   // WMPOpenState
    bool m_shuffle = false, m_loop = false;
    QHash<QString, QVariant> m_mediacenter;   // lower-case
    QHash<QString, QVariant> m_app;
    QString m_task = QStringLiteral("NowPlaying");
    QHash<int, int> m_layout;   // helper.getLayoutSize
    QStringList m_pending;
    QTimer m_decay;
    QHash<QString, QString> m_skinNames;   // lower-case resource name -> as wmploc.dll has it
};
