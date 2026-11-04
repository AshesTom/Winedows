// SPDX-License-Identifier: LGPL-2.1-or-later
#include "mediaplayer.h"

#include "xpfiles.h"
#include "xpdialog.h"
#include "xpprogram.h"

#include <QApplication>
#include <QAudioBuffer>
#include <QAudioBufferOutput>
#include <QAudioOutput>
#include <QDesktopServices>
#include <QDir>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QTreeWidget>
#include <QLineEdit>
#include <QDirIterator>
#include <QProcess>
#include <QToolButton>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QMenu>
#include <QMenuBar>
#include <QVBoxLayout>
#include <QMimeData>
#include <QRandomGenerator>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QVideoFrame>
#include <QVideoSink>
#include <QtMath>

#include <complex>

namespace {

// _WMPOCXEvents' arguments, as wmp.dll's type library names them.
const QHash<QString, QStringList> &playerEvents()
{
    static const QHash<QString, QStringList> events{
        {QStringLiteral("openstatechange"), {QStringLiteral("NewState")}},
        {QStringLiteral("playstatechange"), {QStringLiteral("NewState")}},
        {QStringLiteral("audiolanguagechange"), {QStringLiteral("LangID")}},
        {QStringLiteral("statuschange"), {}},
        {QStringLiteral("scriptcommand"), {QStringLiteral("scType"), QStringLiteral("Param")}},
        {QStringLiteral("newstream"), {}},
        {QStringLiteral("disconnect"), {QStringLiteral("Result")}},
        {QStringLiteral("buffering"), {QStringLiteral("Start")}},
        {QStringLiteral("error"), {}},
        {QStringLiteral("warning"), {QStringLiteral("WarningType"), QStringLiteral("Param"), QStringLiteral("Description")}},
        {QStringLiteral("endofstream"), {QStringLiteral("Result")}},
        {QStringLiteral("positionchange"), {QStringLiteral("oldPosition"), QStringLiteral("newPosition")}},
        {QStringLiteral("markerhit"), {QStringLiteral("MarkerNum")}},
        {QStringLiteral("durationunitchange"), {QStringLiteral("NewDurationUnit")}},
        {QStringLiteral("cdrommediachange"), {QStringLiteral("CdromNum")}},
        {QStringLiteral("playlistchange"), {QStringLiteral("Playlist"), QStringLiteral("change")}},
        {QStringLiteral("currentplaylistchange"), {QStringLiteral("change")}},
        {QStringLiteral("currentplaylistitemavailable"), {QStringLiteral("bstrItemName")}},
        {QStringLiteral("mediachange"), {QStringLiteral("Item")}},
        {QStringLiteral("currentmediaitemavailable"), {QStringLiteral("bstrItemName")}},
        {QStringLiteral("currentitemchange"), {QStringLiteral("pdispMedia")}},
        {QStringLiteral("mediacollectionchange"), {}},
        {QStringLiteral("modechange"), {QStringLiteral("ModeName"), QStringLiteral("NewValue")}},
        {QStringLiteral("mediaerror"), {QStringLiteral("pMediaObject")}},
        {QStringLiteral("domainchange"), {QStringLiteral("strDomain")}},
        {QStringLiteral("switchedtoplayerapplication"), {}},
        {QStringLiteral("switchedtocontrol"), {}},
        {QStringLiteral("playerdockedstatechange"), {}},
        {QStringLiteral("playerreconnect"), {}},
        {QStringLiteral("click"), {QStringLiteral("nButton"), QStringLiteral("nShiftState"), QStringLiteral("fX"), QStringLiteral("fY")}},
        {QStringLiteral("doubleclick"), {QStringLiteral("nButton"), QStringLiteral("nShiftState"), QStringLiteral("fX"), QStringLiteral("fY")}},
    };
    return events;
}

// mediacenter's settings and wmp.dll's defaults for them: the value its registry getters are given just
// before the setting's name (push default, push name, call: ShowTitles 1, ShowEffects 1, ShowCaptions 0,
// SnapToVideo 1, VizAutoSelect 0, VideoZoom 100, CaptionsHeight 100, PlaylistHeight 100, SettingsWidth 100,
// UserWMPShowSettings 0, UserWMPShowMetadata 1, the colours and the panes' sizes). ShowPlaylist,
// ShowResizeBars and QuickHide are not read from the registry there; they are chosen here.
QHash<QString, QVariant> mediacenterDefaults()
{
    return {
        {QStringLiteral("appcolorlight"), QStringLiteral("#6071BF")},
        {QStringLiteral("appcolormedium"), QStringLiteral("#394472")},
        {QStringLiteral("appcolordark"), QStringLiteral("#212741")},
        {QStringLiteral("appcolorblackbackground"), QStringLiteral("#394472")},
        {QStringLiteral("appcolorlimited"), false},
        {QStringLiteral("toolbarbuttonface"), QStringLiteral("#6679CC")},
        {QStringLiteral("toolbarbuttonshadow"), QStringLiteral("#394472")},
        {QStringLiteral("toolbarbuttonhighlight"), QStringLiteral("#7A90F2")},
        {QStringLiteral("itemplayingcolor"), QStringLiteral("#89E116")},
        {QStringLiteral("itemplayingbackgroundcolor"), QStringLiteral("#000000")},
        {QStringLiteral("itemerrorcolor"), QStringLiteral("#FAAC4C")},
        {QStringLiteral("playlistwidth"), 186},
        {QStringLiteral("playlistheight"), 100},
        {QStringLiteral("metadatawidth"), 186},
        {QStringLiteral("metadataheight"), 160},
        {QStringLiteral("settingswidth"), 100},
        {QStringLiteral("settingsheight"), 135},
        {QStringLiteral("userdisplayview"), QStringLiteral("VizView")},
        {QStringLiteral("userwmpdisplayview"), QStringLiteral("VizView")},
        {QStringLiteral("userwmpsettingsview"), QStringLiteral("EQView")},
        {QStringLiteral("userwmpmetadataview"), QStringLiteral("MediaInfoView")},
        {QStringLiteral("displayview"), QStringLiteral("VizView")},
        {QStringLiteral("settingsview"), QStringLiteral("EQView")},
        {QStringLiteral("metadataview"), QStringLiteral("MediaInfoView")},
        {QStringLiteral("userdisplaypreset"), 0},
        {QStringLiteral("userwmpdisplaypreset"), 0},
        {QStringLiteral("userwmpsettingspreset"), 0},
        {QStringLiteral("userwmpmetadatapreset"), 0},
        {QStringLiteral("usershowsettings"), false},
        {QStringLiteral("userwmpshowsettings"), false},
        {QStringLiteral("userwmpshowmetadata"), true},
        {QStringLiteral("metadataretrieval"), 3},
        {QStringLiteral("displaypreset"), 0},
        {QStringLiteral("settingspreset"), 0},
        {QStringLiteral("metadatapreset"), 0},
        {QStringLiteral("effecttype"), QStringLiteral("Bars")},
        {QStringLiteral("effectpreset"), 0},
        {QStringLiteral("videozoom"), 100},
        {QStringLiteral("videoshrinktofit"), true},
        {QStringLiteral("videostretchtofit"), false},
        {QStringLiteral("uservideostretchtofit"), false},
        {QStringLiteral("showcaptions"), false},
        {QStringLiteral("showtitles"), true},
        {QStringLiteral("showeffects"), true},
        {QStringLiteral("showfullscreenplaylist"), false},
        {QStringLiteral("contrastmode"), QString()},
        {QStringLiteral("showsettings"), false},
        {QStringLiteral("showresizebars"), true},
        {QStringLiteral("showplaylist"), true},
        {QStringLiteral("showmetadata"), true},
        {QStringLiteral("fullscreenavailable"), false},
        {QStringLiteral("fullscreenrequest"), false},
        {QStringLiteral("quickhide"), false},
        {QStringLiteral("dynamicrangecontrol"), 0},
        {QStringLiteral("slowrate"), 0.5},
        {QStringLiteral("fastrate"), 1.4},
        {QStringLiteral("buttonhueshift"), 0},
        {QStringLiteral("buttonsaturation"), 1},
        {QStringLiteral("backhueshift"), 0},
        {QStringLiteral("backsaturation"), 1},
        {QStringLiteral("vizrequest"), 0},
        {QStringLiteral("vizautoselect"), false},
        {QStringLiteral("captionsheight"), 100},
        {QStringLiteral("snaptovideo"), true},
        {QStringLiteral("pinfullscreencontrols"), false},
    };
}

// The skins' strings of the player's objects that wmp.dll keeps in its registry.
QString namedString(const QString &name)
{
    if (name.compare(QLatin1String("PLCID"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("409");
    return {};
}

const QStringList &mediaFilters()
{
    static const QStringList filters{
        QStringLiteral("*.wma"), QStringLiteral("*.wmv"), QStringLiteral("*.asf"), QStringLiteral("*.mp3"), QStringLiteral("*.wav"),
        QStringLiteral("*.mid"), QStringLiteral("*.midi"), QStringLiteral("*.avi"), QStringLiteral("*.mpg"), QStringLiteral("*.mpeg"),
        QStringLiteral("*.m1v"), QStringLiteral("*.mp2"), QStringLiteral("*.mpa"), QStringLiteral("*.aif"), QStringLiteral("*.aiff"),
        QStringLiteral("*.au"), QStringLiteral("*.snd"), QStringLiteral("*.ogg"), QStringLiteral("*.oga"), QStringLiteral("*.flac"),
        QStringLiteral("*.opus"), QStringLiteral("*.m4a"), QStringLiteral("*.aac"), QStringLiteral("*.mp4"), QStringLiteral("*.m4v"),
        QStringLiteral("*.mkv"), QStringLiteral("*.webm"), QStringLiteral("*.mov"), QStringLiteral("*.ogv"),
    };
    return filters;
}

// The main menu's commands that this player carries out (wmploc.dll's menu 102).
enum Command {
    Open = 57601, OpenUrl = 18872, CloseMedia = 57602, Exit = 57665, PlayPause = 18808, Stop = 18809, Fast = 18834, Normal = 18835,
    Slow = 18836, Previous = 18810, Next = 18811, Rewind = 18812, FastForward = 18813, Shuffle = 18842, Repeat = 18843,
    VolumeUp = 18815, VolumeDown = 18816, Mute = 18817, About = 57664, GoToFirst = 16000, SkinChooser = 16007,
    ShowTitle = 18791, ShowMetadata = 18787, ShowPlaylist = 18783, ShowResizeBars = 18790, ShowEnhancements = 18789,
    HideTaskbar = 19724, FullScreen = 18782, FullMode = 18780, SkinMode = 18781,
};

// The Skin Chooser task: wmploc.dll's dialog 203 (its toolbar, the list of skins, the preview and
// the skin's title, author and copyright), the words of its toolbar's buttons (strings 1306 and 1320).
const int DIALOG_SKIN_CHOOSER = 203, ID_TOOLBAR = 209, ID_LIST = 202, ID_PREVIEW = 203, ID_TITLE = 206, ID_AUTHOR = 207,
          ID_COPYRIGHT = 208, ID_HINT = 215;
const int STRING_APPLY_SKIN = 1306, STRING_MORE_SKINS = 1320, STRING_DELETE_SKIN = 1302, STRING_APPLY_TIP = 1304,
          STRING_MORE_TIP = 1303;

// ";&Apply Skin;;": a toolbar button's words between its semicolons
QString toolbarText(const QString &text)
{
    const QStringList parts = text.split(QLatin1Char(';'));
    return parts.size() > 1 ? parts[1] : text;
}

} // namespace

namespace {

// Runs a function with a widget's size each time it changes.
class SizeFollower : public QObject
{
public:
    SizeFollower(QWidget *widget, std::function<void(QSize)> follow) : QObject(widget), m_follow(std::move(follow))
    {
        widget->installEventFilter(this);
    }
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Resize)
            m_follow(static_cast<QWidget *>(watched)->size());
        return false;
    }

private:
    std::function<void(QSize)> m_follow;
};

} // namespace

bool MediaPlayer::available()
{
    return !xpfiles::find(QStringLiteral("wmploc.dll")).isEmpty() && !xpfiles::find(QStringLiteral("wmp.dll")).isEmpty();
}

MediaPlayer::MediaPlayer(const QStringList &files)
{
    qRegisterMetaType<wmp::ObjectRef>();
    m_loc = xpfiles::resources(xpfiles::find(QStringLiteral("wmploc.dll")));
    m_wmp = xpfiles::resources(xpfiles::find(QStringLiteral("wmp.dll")));
    if (m_wmp) {
        m_properties = wmpskin::properties(*m_wmp);
        // WMP_MEMBERS=IWMPListBoxCtrl: an interface's members, for reading wmp.dll's type library
        if (const QString wanted = qEnvironmentVariable("WMP_MEMBERS"); !wanted.isEmpty())
            for (const auto members = m_properties.members.value(wanted); const QString &member : members.keys())
                fprintf(stderr, "%s%s\n", qPrintable(member), members.value(member) ? "()" : "");
        m_predefined = wmpskin::predefinedObjects(*m_wmp);
    }
    if (m_loc)
        for (const QString &name : m_loc->names(QStringLiteral("256")))
            m_skinNames.insert(name.toLower(), name);
    m_mediacenter = mediacenterDefaults();
    m_app = {
        {QStringLiteral("titlebarvisible"), false},
        {QStringLiteral("titlebarautohide"), false},
        {QStringLiteral("titlebarcurrentlyvisible"), false},
        {QStringLiteral("currenttask"), m_task},
        {QStringLiteral("settingsvisible"), false},
        {QStringLiteral("playlistvisible"), true},
        {QStringLiteral("taskbarvisible"), true},
        {QStringLiteral("bgpluginrunning"), false},
        {QStringLiteral("isxporbetter"), true},
        {QStringLiteral("maximized"), false},
    };
    // what the user chose last time
    QSettings settings(QStringLiteral("win4wine"), QStringLiteral("mediaplayer"));
    settings.beginGroup(QStringLiteral("mediacenter"));
    for (const QString &key : settings.childKeys())
        m_mediacenter.insert(key, settings.value(key));
    settings.endGroup();

    m_player = new QMediaPlayer(this);
    m_audio = new QAudioOutput(this);
    m_audio->setVolume(float(settings.value(QStringLiteral("volume"), 50).toInt()) / 100.f);
    m_audio->setMuted(settings.value(QStringLiteral("mute"), false).toBool());
    m_player->setAudioOutput(m_audio);
    m_video = new QVideoSink(this);
    m_player->setVideoSink(m_video);
    m_buffers = new QAudioBufferOutput(this);
    m_player->setAudioBufferOutput(m_buffers);
    m_shuffle = settings.value(QStringLiteral("shuffle"), false).toBool();
    m_loop = settings.value(QStringLiteral("loop"), false).toBool();

    connect(m_video, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        m_frame = frame.toImage();
        emit videoChanged();
    });
    connect(m_buffers, &QAudioBufferOutput::audioBufferReceived, this, &MediaPlayer::updateSpectrum);
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        switch (status) {
        case QMediaPlayer::LoadingMedia:
            setOpenState(11);
            break;
        case QMediaPlayer::LoadedMedia:
        case QMediaPlayer::BufferedMedia:
            if (m_openState != 13) {
                setOpenState(12);
                setOpenState(13);
                emit fired(QStringLiteral("player"), QStringLiteral("mediachange"),
                           {QVariant::fromValue(wmp::ObjectRef{QStringLiteral("player.currentmedia")})});
                emit fired(QStringLiteral("player"), QStringLiteral("currentitemchange"),
                           {QVariant::fromValue(wmp::ObjectRef{QStringLiteral("player.currentmedia")})});
            }
            break;
        case QMediaPlayer::EndOfMedia:
            setPlayState(8);
            if (m_current + 1 < m_items.size() || m_loop || m_shuffle) {
                setPlayState(9);
                advance(1);
            } else {
                setPlayState(1);
            }
            break;
        case QMediaPlayer::InvalidMedia:
            setOpenState(6);
            emit fired(QStringLiteral("player"), QStringLiteral("error"), {});
            break;
        default:
            break;
        }
        changedAll(QStringLiteral("player"), {"status"});
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        if (state == QMediaPlayer::PlayingState)
            setPlayState(3);
        else if (state == QMediaPlayer::PausedState)
            setPlayState(2);
        else if (m_playState != 8 && m_playState != 9)
            setPlayState(1);
    });
    connect(m_player, &QMediaPlayer::positionChanged, this, [this] {
        changedAll(QStringLiteral("player.controls"), {"currentposition", "currentpositionstring"});
    });
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
        if (m_current >= 0 && m_current < m_items.size())
            m_items[m_current].duration = duration;
        changedAll(QStringLiteral("player.currentmedia"), {"duration", "durationstring"});
    });
    connect(m_player, &QMediaPlayer::metaDataChanged, this, [this] {
        if (m_current < 0 || m_current >= m_items.size())
            return;
        const QMediaMetaData data = m_player->metaData();
        Item &item = m_items[m_current];
        if (const QString title = data.stringValue(QMediaMetaData::Title); !title.isEmpty())
            item.title = title;
        if (const QString artist = data.stringValue(QMediaMetaData::ContributingArtist); !artist.isEmpty())
            item.artist = artist;
        else if (const QString artist = data.stringValue(QMediaMetaData::AlbumArtist); !artist.isEmpty())
            item.artist = artist;
        if (const QString album = data.stringValue(QMediaMetaData::AlbumTitle); !album.isEmpty())
            item.album = album;
        changedAll(QStringLiteral("player.currentmedia"), {"name"});
        emit fired(QStringLiteral("player"), QStringLiteral("mediachange"),
                   {QVariant::fromValue(wmp::ObjectRef{QStringLiteral("player.currentmedia")})});
        emit fired(QStringLiteral("player"), QStringLiteral("currentmediaitemavailable"), {QStringLiteral("Title")});
        if (!albumArt(true).isNull()) {
            emit fired(QStringLiteral("player"), QStringLiteral("currentmediaitemavailable"), {QStringLiteral("WMPImage_AlbumArtLarge")});
            emit fired(QStringLiteral("player"), QStringLiteral("currentmediaitemavailable"), {QStringLiteral("WMPImage_AlbumArtSmall")});
        }
    });
    connect(m_player, &QMediaPlayer::hasVideoChanged, this, [this](bool video) {
        if (!video)
            m_frame = QImage();
        emit videoChanged();
    });
    connect(m_audio, &QAudioOutput::volumeChanged, this, [this] { changedAll(QStringLiteral("player.settings"), {"volume"}); });
    connect(m_audio, &QAudioOutput::mutedChanged, this, [this] { changedAll(QStringLiteral("player.settings"), {"mute"}); });

    // played once the window is there: its skins follow the player's events from the start
    m_pending = files;
    connect(&m_decay, &QTimer::timeout, this, [this] {
        // a stopped or paused visualization falls back
        if (m_playState == 3)
            return;
        bool any = false;
        for (float &level : m_spectrum)
            any |= (level *= 0.8f) > 0.01f;
        if (!any)
            m_spectrum.clear();
        emit videoChanged();
    });
    m_decay.start(50);
}

MediaPlayer::~MediaPlayer()
{
    QSettings settings(QStringLiteral("win4wine"), QStringLiteral("mediaplayer"));
    settings.setValue(QStringLiteral("volume"), qRound(m_audio->volume() * 100));
    settings.setValue(QStringLiteral("mute"), m_audio->isMuted());
    settings.setValue(QStringLiteral("shuffle"), m_shuffle);
    settings.setValue(QStringLiteral("loop"), m_loop);
    if (m_window)
        settings.setValue(QStringLiteral("geometry"), QRect(m_window->mapToGlobal(QPoint()), m_window->size()));
    settings.setValue(QStringLiteral("titleBarVisible"), !m_menuFrame.isNull());
    delete m_window;
    delete m_menuFrame;
}

std::optional<wmpskin::Node> MediaPlayer::skin(const QString &name) const
{
    if (!m_loc)
        return std::nullopt;
    const QString resource = m_skinNames.value(name.toLower(), name.toUpper());
    const QByteArray data = m_loc->find(QStringLiteral("256"), resource);
    if (data.isEmpty())
        return std::nullopt;
    std::optional<wmpskin::Node> node = wmpskin::readCompiled(data, m_properties.names, [this](int id) { return text(id); });
    // WMP_TREE=name.wsz: the skin's elements and attributes, for reading wmploc.dll's skins
    if (node && qEnvironmentVariable("WMP_TREE").compare(name, Qt::CaseInsensitive) == 0) {
        const std::function<void(const wmpskin::Node &, int)> print = [&print](const wmpskin::Node &element, int depth) {
            QString line = QString(depth * 2, QLatin1Char(' ')) + QLatin1Char('<') + element.tag;
            for (const auto &[key, value] : element.attributes)
                line += QStringLiteral(" %1=\"%2\"").arg(key, value.toString().left(160));
            fprintf(stderr, "%s>\n", qPrintable(line));
            for (const wmpskin::Node &child : element.children)
                print(child, depth + 1);
        };
        print(*node, 0);
    }
    return node;
}

QString MediaPlayer::text(int id) const
{
    return m_loc ? xpfiles::string(*m_loc, id) : QString();
}

void MediaPlayer::show()
{
    if (const QString tree = qEnvironmentVariable("WMP_TREE"); !tree.isEmpty())
        skin(tree);
    std::optional<wmpskin::Node> document = skin(QStringLiteral("MainAppSkin.wsz"));
    if (!document) {
        fprintf(stderr, "explorer: MainAppSkin.wsz not found in wmploc.dll\n");
        return;
    }
    QSettings settings(QStringLiteral("win4wine"), QStringLiteral("mediaplayer"));
    // wmp.dll's full mode opens at its last size; the first time, at 640 x 480 on screens that take it
    QRect geometry = settings.value(QStringLiteral("geometry")).toRect();
    const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
    if (!geometry.isValid() || !screen.intersects(geometry)) {
        geometry = QRect(0, 0, qMin(640, screen.width()), qMin(480, screen.height()));
        geometry.moveCenter(screen.center());
    }
    m_window = new wmp::SkinView(this, *document, QString(), nullptr, geometry.size());
    m_window->setWindowFlag(Qt::FramelessWindowHint);
    m_window->setAttribute(Qt::WA_DeleteOnClose, false);
    // XP's caption: the application's name (string 102 of wmploc.dll is "Windows Media Player")
    const QString title = text(102).isEmpty() ? QStringLiteral("Windows Media Player") : text(102);
    m_window->setWindowTitle(title);
    if (m_loc) {
        const QImage icon = xpfiles::icon(*m_loc, 102, 32);
        if (!icon.isNull())
            m_window->setWindowIcon(QPixmap::fromImage(icon));
    }
    m_window->setAcceptDrops(true);
    m_window->windowRequest = [this](const QString &request) { windowRequest(request); };
    m_window->installEventFilter(this);   // files dropped on it
    m_window->setGeometry(geometry);
    m_window->setMinimumSize(300, 200);
    m_window->show();
    m_window->start();
    emit changed(QStringLiteral("app"), QStringLiteral("currenttask"));
    open(m_pending);
    m_pending.clear();
    if (settings.value(QStringLiteral("titleBarVisible")).toBool())
        set(QStringLiteral("app"), QStringLiteral("titlebarvisible"), true);
    // wmp.dll opens in the mode it was left in
    if (settings.value(QStringLiteral("mode")).toString() == QLatin1String("skin"))
        showSkin(currentSkin());
    connect(qApp, &QApplication::aboutToQuit, this, [this] {
        if (m_window)
            m_window->close();
    });
}

void MediaPlayer::open(const QStringList &files, bool play)
{
    if (files.isEmpty())
        return;
    const int first = int(m_items.size());
    for (const QString &file : files) {
        const QUrl url = QUrl::fromUserInput(file, QDir::currentPath(), QUrl::AssumeLocalFile);
        Item item;
        item.url = url;
        item.title = QFileInfo(url.isLocalFile() ? url.toLocalFile() : url.path()).completeBaseName();
        m_items.append(item);
    }
    emit fired(QStringLiteral("player"), QStringLiteral("currentplaylistchange"), {6});
    changedAll(QStringLiteral("player.currentplaylist"), {"count"});
    if (play || m_current < 0)
        playIndex(first);
}

void MediaPlayer::playIndex(int index)
{
    if (index < 0 || index >= m_items.size())
        return;
    m_current = index;
    setOpenState(8);
    setPlayState(9);
    m_player->setSource(m_items[index].url);
    m_player->play();
    changedAll(QStringLiteral("player"), {"currentmedia", "url"});
    changedAll(QStringLiteral("player.currentmedia"), {"name", "duration", "durationstring", "sourceurl"});
    changedAll(QStringLiteral("player.controls"), {"isavailable"});
}

void MediaPlayer::advance(int step)
{
    if (m_items.isEmpty())
        return;
    int next = m_current + step;
    if (m_shuffle && m_items.size() > 1)
        next = int(QRandomGenerator::global()->bounded(m_items.size()));
    if (next >= m_items.size())
        next = m_loop ? 0 : -1;
    if (next < 0)
        next = m_loop ? int(m_items.size()) - 1 : (step < 0 ? 0 : -1);
    if (next < 0) {
        m_player->stop();
        return;
    }
    playIndex(next);
}

void MediaPlayer::setPlayState(int state)
{
    if (m_playState == state)
        return;
    m_playState = state;
    changedAll(QStringLiteral("player"), {"playstate", "status"});
    changedAll(QStringLiteral("player.controls"), {"isavailable"});
    emit fired(QStringLiteral("player"), QStringLiteral("playstatechange"), {state});
}

void MediaPlayer::setOpenState(int state)
{
    if (m_openState == state)
        return;
    m_openState = state;
    changedAll(QStringLiteral("player"), {"openstate", "status"});
    changedAll(QStringLiteral("player.controls"), {"isavailable"});
    emit fired(QStringLiteral("player"), QStringLiteral("openstatechange"), {state});
}

void MediaPlayer::changedAll(const QString &object, std::initializer_list<const char *> names)
{
    for (const char *name : names)
        emit changed(object, QLatin1String(name));
}

QString MediaPlayer::timeString(double seconds)
{
    const int total = qMax(0, int(seconds));
    const int hours = total / 3600, minutes = total / 60 % 60, rest = total % 60;
    if (hours)
        return QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(rest, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(minutes, 2, 10, QLatin1Char('0')).arg(rest, 2, 10, QLatin1Char('0'));
}

// ---- the objects the skins see ----

QHash<QString, QString> MediaPlayer::globals() const
{
    return {
        {QStringLiteral("player"), QStringLiteral("player")},
        {QStringLiteral("mediacenter"), QStringLiteral("mediacenter")},
        {QStringLiteral("app"), QStringLiteral("app")},
        {QStringLiteral("helper"), QStringLiteral("helper")},
    };
}

QString MediaPlayer::eventSource(const QString &tag) const
{
    static const QHash<QString, QString> sources{
        {QStringLiteral("player"), QStringLiteral("player")},
        {QStringLiteral("settings"), QStringLiteral("player.settings")},
        {QStringLiteral("controls"), QStringLiteral("player.controls")},
        {QStringLiteral("network"), QStringLiteral("player.network")},
        {QStringLiteral("currentmedia"), QStringLiteral("player.currentmedia")},
        {QStringLiteral("currentplaylist"), QStringLiteral("player.currentplaylist")},
        {QStringLiteral("dvd"), QStringLiteral("player.dvd")},
        {QStringLiteral("mediacenter"), QStringLiteral("mediacenter")},
        {QStringLiteral("app"), QStringLiteral("app")},
        {QStringLiteral("playerapplication"), QStringLiteral("app")},
        {QStringLiteral("helper"), QStringLiteral("helper")},
    };
    return sources.value(tag.toLower());
}

std::optional<QStringList> MediaPlayer::eventArguments(const QString &object, const QString &event) const
{
    if (object == QLatin1String("player")) {
        auto it = playerEvents().constFind(event);
        if (it != playerEvents().constEnd())
            return *it;
    }
    if (event.endsWith(QLatin1String("_onchange")))
        return QStringList();
    return std::nullopt;
}

bool MediaPlayer::isMethod(const QString &object, const QString &name) const
{
    static const QHash<QString, QSet<QString>> methods{
        {QStringLiteral("player"), {QStringLiteral("launchurl"), QStringLiteral("close"), QStringLiteral("newmedia"),
                                    QStringLiteral("newplaylist"), QStringLiteral("openplayer"), QStringLiteral("playlistnames"),
                                    QStringLiteral("playlistcurrent"), QStringLiteral("playlistplay")}},
        {QStringLiteral("player.controls"), {QStringLiteral("play"), QStringLiteral("pause"), QStringLiteral("stop"), QStringLiteral("next"),
                                             QStringLiteral("previous"), QStringLiteral("fastforward"), QStringLiteral("fastreverse"),
                                             QStringLiteral("isavailable"), QStringLiteral("playitem"), QStringLiteral("step"),
                                             QStringLiteral("getaudiolanguageid"), QStringLiteral("getaudiolanguagedescription")}},
        {QStringLiteral("player.settings"), {QStringLiteral("getmode"), QStringLiteral("setmode"), QStringLiteral("isavailable"),
                                             QStringLiteral("requestmediaaccessrights")}},
        {QStringLiteral("player.currentplaylist"), {QStringLiteral("item"), QStringLiteral("getiteminfo"), QStringLiteral("setiteminfo"),
                                                    QStringLiteral("clear"), QStringLiteral("appenditem"), QStringLiteral("removeitem"),
                                                    QStringLiteral("insertitem"), QStringLiteral("moveitem"), QStringLiteral("isidentical"),
                                                    QStringLiteral("attributename")}},
        {QStringLiteral("player.dvd"), {QStringLiteral("isavailable"), QStringLiteral("topmenu"), QStringLiteral("titlemenu"),
                                        QStringLiteral("back"), QStringLiteral("resume")}},
        {QStringLiteral("player.network"), {QStringLiteral("getproxyname"), QStringLiteral("getproxysettings")}},
        {QStringLiteral("mediacenter"), {QStringLiteral("getnamedstring"), QStringLiteral("getdurationstringfromseconds")}},
        {QStringLiteral("app"), {QStringLiteral("gotoskinmode"), QStringLiteral("adjustleft"), QStringLiteral("configureplugins"),
                                 QStringLiteral("gettimestring")}},
        {QStringLiteral("helper"), {QStringLiteral("viewfriendlyname"), QStringLiteral("viewpresetcount"), QStringLiteral("viewpresetname"),
                                    QStringLiteral("effectfriendlyname"), QStringLiteral("effectpresetname"), QStringLiteral("resolvedisplayview"),
                                    QStringLiteral("isvaliddisplayview"), QStringLiteral("getskinfile"), QStringLiteral("gettimestring"),
                                    QStringLiteral("getcurrentartid"), QStringLiteral("getcurrentscriptcommand"), QStringLiteral("calclayout"),
                                    QStringLiteral("getlayoutsize"), QStringLiteral("getrootplaylist"), QStringLiteral("gethtmlviewurl"),
                                    QStringLiteral("cansendlink"), QStringLiteral("sendlink"), QStringLiteral("editobj")}},
    };
    if (object.startsWith(QLatin1String("media:")) || object == QLatin1String("player.currentmedia"))
        return name == QLatin1String("getiteminfo") || name == QLatin1String("getattributename") || name == QLatin1String("isidentical")
            || name == QLatin1String("setiteminfo") || name == QLatin1String("getmarkername") || name == QLatin1String("getmarkertime")
            || name == QLatin1String("isreadonlyitem") || name == QLatin1String("getiteminfobytype")
            || name == QLatin1String("getattributecountbytype");
    return methods.value(object).contains(name);
}

QVariant MediaPlayer::mediaProperty(int index, const QString &name)
{
    if (index < 0 || index >= m_items.size()) {
        if (name == QLatin1String("name") || name == QLatin1String("sourceurl") || name == QLatin1String("durationstring"))
            return QString();
        if (name == QLatin1String("duration") || name == QLatin1String("imagesourcewidth") || name == QLatin1String("imagesourceheight")
            || name == QLatin1String("attributecount"))
            return 0;
        return {};
    }
    const Item &item = m_items[index];
    const bool current = index == m_current;
    const double duration = (current && m_player->duration() > 0 ? m_player->duration() : item.duration) / 1000.0;
    if (name == QLatin1String("name"))
        return item.title;
    if (name == QLatin1String("sourceurl"))
        return item.url.isLocalFile() ? item.url.toLocalFile() : item.url.toString();
    if (name == QLatin1String("duration"))
        return duration;
    if (name == QLatin1String("durationstring"))
        return duration > 0 ? timeString(duration) : QString();
    if (name == QLatin1String("imagesourcewidth"))
        return current && m_player->hasVideo() ? m_frame.width() : 0;
    if (name == QLatin1String("imagesourceheight"))
        return current && m_player->hasVideo() ? m_frame.height() : 0;
    if (name == QLatin1String("attributecount"))
        return 4;
    if (name == QLatin1String("markercount"))
        return 0;
    return {};
}

QVariant MediaPlayer::mediaCall(int index, const QString &name, const QVariantList &arguments)
{
    if (name == QLatin1String("getiteminfo")) {
        if (index < 0 || index >= m_items.size())
            return QString();
        const Item &item = m_items[index];
        const QString attribute = arguments.value(0).toString().toLower();
        if (attribute == QLatin1String("title") || attribute == QLatin1String("name"))
            return item.title;
        if (attribute == QLatin1String("author") || attribute == QLatin1String("artist") || attribute == QLatin1String("wm/albumartist"))
            return item.artist;
        if (attribute == QLatin1String("album") || attribute == QLatin1String("wm/albumtitle"))
            return item.album;
        if (attribute == QLatin1String("sourceurl"))
            return mediaProperty(index, QStringLiteral("sourceurl"));
        if (attribute == QLatin1String("duration"))
            return mediaProperty(index, QStringLiteral("duration"));
        if (attribute == QLatin1String("mediatype"))
            return index == m_current && m_player->hasVideo() ? QStringLiteral("video") : QStringLiteral("audio");
        if (attribute == QLatin1String("filetype"))
            return QFileInfo(item.url.path()).suffix().toLower();
        return QString();
    }
    if (name == QLatin1String("getattributecountbytype"))
        return 0;
    if (name == QLatin1String("getiteminfobytype"))
        return QString();
    if (name == QLatin1String("getattributename"))
        return QStringList{QStringLiteral("Title"), QStringLiteral("Author"), QStringLiteral("WM/AlbumTitle"), QStringLiteral("Duration")}
            .value(arguments.value(0).toInt());
    if (name == QLatin1String("isidentical"))
        return arguments.value(0).value<wmp::ObjectRef>().key == QStringLiteral("media:%1").arg(index);
    return {};
}

bool MediaPlayer::controlAvailable(const QString &control) const
{
    const QString name = control.toLower();
    const bool media = m_current >= 0;
    const bool playing = m_playState == 3 || m_playState == 4 || m_playState == 5;
    if (name == QLatin1String("play"))
        return !m_items.isEmpty() && m_playState != 3;
    if (name == QLatin1String("pause"))
        return playing;
    if (name == QLatin1String("stop"))
        return media && (playing || m_playState == 2 || m_playState == 9);
    if (name == QLatin1String("next") || name == QLatin1String("previous"))
        return m_items.size() > 1;
    if (name == QLatin1String("fastforward") || name == QLatin1String("fastreverse") || name == QLatin1String("currentposition")
        || name == QLatin1String("seek"))
        return media && m_player->isSeekable();
    return false;
}

QVariant MediaPlayer::get(const QString &object, const QString &name)
{
    const auto ref = [](const QString &key) { return QVariant::fromValue(wmp::ObjectRef{key}); };
    if (object == QLatin1String("player")) {
        if (name == QLatin1String("openstate"))
            return m_openState;
        if (name == QLatin1String("playstate"))
            return m_playState;
        if (name == QLatin1String("status")) {
            // wmploc.dll's words for the states, as the player's status line shows them
            switch (m_playState) {
            case 3: return m_player->hasVideo() || m_player->hasAudio() ? QStringLiteral("Playing") : QString();
            case 2: return QStringLiteral("Paused");
            case 1: return QStringLiteral("Stopped");
            case 9: return QStringLiteral("Connecting...");
            default: return m_items.isEmpty() ? QStringLiteral("Ready") : QString();
            }
        }
        if (name == QLatin1String("url"))
            return m_current >= 0 ? mediaProperty(m_current, QStringLiteral("sourceurl")) : QString();
        // never null: an empty media item before anything is open, as wmp.dll's
        if (name == QLatin1String("currentmedia"))
            return ref(QStringLiteral("player.currentmedia"));
        if (name == QLatin1String("currentplaylist"))
            return ref(QStringLiteral("player.currentplaylist"));
        for (const char *child : {"controls", "settings", "network", "dvd", "closedcaption", "error", "cdromcollection",
                                  "mediacollection", "playlistcollection"})
            if (name == QLatin1String(child))
                return ref(QStringLiteral("player.") + name);
        if (name == QLatin1String("versioninfo"))
            return QStringLiteral("9.0.0.4503");
        if (name == QLatin1String("fullscreen"))
            return !m_fullScreen.isNull();
        if (name == QLatin1String("enabled") || name == QLatin1String("enablecontextmenu") || name == QLatin1String("isonline")
            || name == QLatin1String("stretchtofit") || name == QLatin1String("windowlessvideo"))
            return name != QLatin1String("isonline");
        if (name == QLatin1String("uimode"))
            return QStringLiteral("full");
        if (name == QLatin1String("isremote"))
            return false;
        return {};
    }
    if (object == QLatin1String("player.controls")) {
        if (name == QLatin1String("currentposition"))
            return m_player->position() / 1000.0;
        if (name == QLatin1String("currentpositionstring"))
            return m_current >= 0 && m_playState != 1 ? timeString(m_player->position() / 1000.0) : QString();
        if (name == QLatin1String("currentitem"))
            return m_current >= 0 ? ref(QStringLiteral("player.currentmedia")) : QVariant();
        if (name == QLatin1String("currentmarker") || name == QLatin1String("currentaudiolanguage")
            || name == QLatin1String("currentaudiolanguageindex") || name == QLatin1String("audiolanguagecount"))
            return 0;
        return {};
    }
    if (object == QLatin1String("player.settings")) {
        if (name == QLatin1String("volume"))
            return qRound(m_audio->volume() * 100);
        if (name == QLatin1String("mute"))
            return m_audio->isMuted();
        if (name == QLatin1String("rate"))
            return m_player->playbackRate();
        if (name == QLatin1String("balance"))
            return 0;
        if (name == QLatin1String("playcount"))
            return 1;
        if (name == QLatin1String("autostart") || name == QLatin1String("invokeurls") || name == QLatin1String("enableerrordialogs"))
            return name != QLatin1String("enableerrordialogs");
        if (name == QLatin1String("baseurl") || name == QLatin1String("defaultframe"))
            return QString();
        return {};
    }
    if (object == QLatin1String("player.network")) {
        if (name == QLatin1String("bufferingprogress") || name == QLatin1String("downloadprogress") || name == QLatin1String("receptionquality"))
            return 100;
        if (name == QLatin1String("sourceprotocol"))
            return m_current >= 0 && !m_items[m_current].url.isLocalFile() ? m_items[m_current].url.scheme() : QStringLiteral("File");
        if (name == QLatin1String("bitrate") || name == QLatin1String("maxbitrate")) {
            const QVariant rate = m_player->metaData().value(QMediaMetaData::AudioBitRate);
            return rate.isValid() ? rate.toInt() : 0;
        }
        if (name == QLatin1String("bufferingcount") || name == QLatin1String("bufferingtime") || name == QLatin1String("bandwidth")
            || name == QLatin1String("lostpackets") || name == QLatin1String("framerate") || name == QLatin1String("encodedframerate"))
            return 0;
        return {};
    }
    if (object == QLatin1String("player.currentmedia"))
        return mediaProperty(m_current, name);
    if (object.startsWith(QLatin1String("media:")))
        return mediaProperty(object.mid(6).toInt(), name);
    if (object == QLatin1String("player.currentplaylist")) {
        if (name == QLatin1String("count"))
            return int(m_items.size());
        if (name == QLatin1String("name"))
            return m_items.size() == 1 ? m_items.first().title : QString();
        if (name == QLatin1String("attributecount"))
            return 0;
        return {};
    }
    if (object == QLatin1String("player.dvd")) {
        if (name == QLatin1String("domain"))
            return QString();
        return {};
    }
    if (object == QLatin1String("player.error")) {
        if (name == QLatin1String("errorcount"))
            return 0;
        return {};
    }
    if (object == QLatin1String("player.cdromcollection") || object == QLatin1String("player.playlistcollection")) {
        if (name == QLatin1String("count"))
            return 0;
        return {};
    }
    if (object == QLatin1String("player.closedcaption")) {
        if (name == QLatin1String("samistyle") || name == QLatin1String("samilang") || name == QLatin1String("samifilename")
            || name == QLatin1String("captioningid"))
            return QString();
        return {};
    }
    if (object == QLatin1String("mediacenter"))
        return m_mediacenter.value(name);
    if (object == QLatin1String("app")) {
        if (name == QLatin1String("maximized"))
            return m_window && fullWindow()->isMaximized();
        return m_app.value(name);
    }
    return {};
}

bool MediaPlayer::set(const QString &object, const QString &name, const QVariant &value)
{
    if (object == QLatin1String("player")) {
        if (name == QLatin1String("url")) {
            m_items.clear();
            m_current = -1;
            open({value.toString()});
            return true;
        }
        if (name == QLatin1String("fullscreen") && m_window) {
            setFullScreen(value.toBool());
            return true;
        }
        return false;
    }
    if (object == QLatin1String("player.controls")) {
        if (name == QLatin1String("currentposition")) {
            m_player->setPosition(qint64(value.toDouble() * 1000));
            return true;
        }
        return false;
    }
    if (object == QLatin1String("player.settings")) {
        if (name == QLatin1String("volume"))
            m_audio->setVolume(float(qBound(0, qRound(value.toDouble()), 100)) / 100.f);
        else if (name == QLatin1String("mute"))
            m_audio->setMuted(value.toBool());
        else if (name == QLatin1String("rate")) {
            m_player->setPlaybackRate(value.toDouble());
            changedAll(QStringLiteral("player.settings"), {"rate"});
        } else
            return false;
        return true;
    }
    if (object == QLatin1String("mediacenter")) {
        if (m_mediacenter.value(name) == value)
            return true;
        m_mediacenter.insert(name, value);
        // the skins ask for full screen with fullScreenRequest (NowPlaying's button); wmp.dll clears it
        if (name == QLatin1String("fullscreenrequest") && value.toBool()) {
            m_mediacenter.insert(name, false);
            QTimer::singleShot(0, this, [this] { setFullScreen(true); });
        } else if (name == QLatin1String("pinfullscreencontrols") && m_fullScreen) {
            showFullScreenControls(true);
        }
        if (name == QLatin1String("backhueshift") || name == QLatin1String("backsaturation") || name == QLatin1String("buttonhueshift")
            || name == QLatin1String("buttonsaturation") || name.startsWith(QLatin1String("show")) || name.endsWith(QLatin1String("width"))
            || name.endsWith(QLatin1String("height")) || name.endsWith(QLatin1String("view")) || name.endsWith(QLatin1String("preset"))) {
            QSettings settings(QStringLiteral("win4wine"), QStringLiteral("mediaplayer"));
            settings.setValue(QStringLiteral("mediacenter/") + name, value);
        }
        emit changed(object, name);
        return true;
    }
    if (object == QLatin1String("app")) {
        if (name == QLatin1String("maximized"))
            return false;
        if (m_app.value(name) == value)
            return true;
        m_app.insert(name, value);
        emit changed(object, name);
        if (name == QLatin1String("titlebarvisible"))
            QTimer::singleShot(0, this, [this, shown = value.toBool()] { setMenuBarShown(shown); });
        if (name == QLatin1String("currenttask")) {
            m_task = value.toString();
            emit taskChanged();
        }
        return true;
    }
    return false;
}

QVariant MediaPlayer::call(const QString &object, const QString &name, const QVariantList &arguments)
{
    if (object == QLatin1String("player")) {
        if (name == QLatin1String("launchurl"))
            QDesktopServices::openUrl(QUrl(arguments.value(0).toString()));
        else if (name == QLatin1String("close")) {
            m_player->stop();
            m_player->setSource(QUrl());
            m_current = -1;
            setOpenState(0);
            setPlayState(0);
        } else if (name == QLatin1String("playlistnames")) {
            QVariantList names;
            for (int i = 0; i < m_items.size(); ++i)
                names.append(QStringList{m_items[i].title, mediaProperty(i, QStringLiteral("durationstring")).toString()});
            return names;
        } else if (name == QLatin1String("playlistcurrent")) {
            return m_current;
        } else if (name == QLatin1String("playlistplay")) {
            const int index = arguments.value(0).toInt();
            if (index >= 0 && index < m_items.size())
                playIndex(index);
        }
        return {};
    }
    if (object == QLatin1String("player.controls")) {
        if (name == QLatin1String("play")) {
            if (m_current < 0 && !m_items.isEmpty())
                playIndex(0);
            else {
                m_player->setPlaybackRate(1.0);
                m_player->play();
            }
        } else if (name == QLatin1String("pause"))
            m_player->pause();
        else if (name == QLatin1String("stop")) {
            m_player->stop();
            m_player->setPosition(0);
        } else if (name == QLatin1String("next"))
            advance(1);
        else if (name == QLatin1String("previous")) {
            if (m_player->position() > 3000)
                m_player->setPosition(0);
            else
                advance(-1);
        } else if (name == QLatin1String("fastforward")) {
            m_player->setPlaybackRate(5.0);
            setPlayState(4);
        } else if (name == QLatin1String("fastreverse")) {
            m_player->setPosition(qMax<qint64>(0, m_player->position() - 5000));
        } else if (name == QLatin1String("isavailable"))
            return controlAvailable(arguments.value(0).toString());
        else if (name == QLatin1String("playitem")) {
            const QString key = arguments.value(0).value<wmp::ObjectRef>().key;
            if (key.startsWith(QLatin1String("media:")))
                playIndex(key.mid(6).toInt());
        }
        return {};
    }
    if (object == QLatin1String("player.settings")) {
        const QString mode = arguments.value(0).toString().toLower();
        if (name == QLatin1String("getmode"))
            return mode == QLatin1String("shuffle") ? m_shuffle : mode == QLatin1String("loop") ? m_loop : false;
        if (name == QLatin1String("setmode")) {
            const bool on = arguments.value(1).toBool();
            if (mode == QLatin1String("shuffle"))
                m_shuffle = on;
            else if (mode == QLatin1String("loop"))
                m_loop = on;
            emit fired(QStringLiteral("player"), QStringLiteral("modechange"), {mode, on});
        }
        if (name == QLatin1String("isavailable"))
            return true;
        return {};
    }
    if (object == QLatin1String("player.currentplaylist")) {
        if (name == QLatin1String("item")) {
            const int i = arguments.value(0).toInt();
            return i >= 0 && i < m_items.size() ? QVariant::fromValue(wmp::ObjectRef{QStringLiteral("media:%1").arg(i)}) : QVariant();
        }
        if (name == QLatin1String("getiteminfo"))
            return QString();
        if (name == QLatin1String("clear")) {
            m_player->stop();
            m_items.clear();
            m_current = -1;
            emit fired(QStringLiteral("player"), QStringLiteral("currentplaylistchange"), {1});
        }
        return {};
    }
    if (object == QLatin1String("player.currentmedia"))
        return mediaCall(m_current, name, arguments);
    if (object.startsWith(QLatin1String("media:")))
        return mediaCall(object.mid(6).toInt(), name, arguments);
    if (object == QLatin1String("player.dvd"))
        return name == QLatin1String("isavailable") ? QVariant(false) : QVariant();
    if (object == QLatin1String("mediacenter")) {
        if (name == QLatin1String("getnamedstring"))
            return namedString(arguments.value(0).toString());
        if (name == QLatin1String("getdurationstringfromseconds"))
            return timeString(arguments.value(0).toDouble());
        return {};
    }
    if (object == QLatin1String("app")) {
        if (name == QLatin1String("gettimestring"))
            return timeString(arguments.value(0).toDouble());
        if (name == QLatin1String("gotoskinmode")) {
            QTimer::singleShot(0, this, [this] { showSkin(currentSkin()); });
            return {};
        }
        if (name == QLatin1String("adjustleft") && m_window) {
            // the taskbar's width taken from (or given to) the window's left
            const int dx = arguments.value(0).toInt();
            QRect geometry = fullWindow()->geometry();
            geometry.setLeft(geometry.left() + dx);
            fullWindow()->setGeometry(geometry);
        }
        return {};
    }
    if (object == QLatin1String("helper")) {
        if (name == QLatin1String("gettimestring"))
            return timeString(arguments.value(0).toDouble());
        if (name == QLatin1String("isvaliddisplayview"))
            return plugin(arguments.value(0).toString()).has_value();
        if (name == QLatin1String("resolvedisplayview"))
            return m_player->hasVideo() ? QStringLiteral("VideoView") : m_mediacenter.value(QStringLiteral("userdisplayview")).toString();
        if (name == QLatin1String("viewpresetcount"))
            return 1;
        if (name == QLatin1String("getcurrentartid")) {
            // wmploc.dll's art in pairs, 75 and 200 pixels: 520/521 any media, 525/526 music, 527/528 video
            // (529/530 a CD, 531/532 a DVD)
            const bool large = arguments.value(0).toBool();
            int small = 520;
            if (m_current >= 0)
                small = m_player->hasVideo() ? 527 : 525;
            return large ? small + 1 : small;
        }
        if (name == QLatin1String("calclayout")) {
            calcLayout(arguments.value(0).toInt(), arguments.value(1).toInt(), arguments.value(2).toBool(), arguments.value(3).toBool());
            return {};
        }
        if (name == QLatin1String("getlayoutsize"))
            return m_layout.value(arguments.value(0).toInt());
        if (name == QLatin1String("effectfriendlyname"))
            return QStringLiteral("Bars and Waves");
        if (name == QLatin1String("effectpresetname"))
            return QStringLiteral("Bars");
        if (name == QLatin1String("viewfriendlyname") || name == QLatin1String("viewpresetname"))
            return QString();
        return QString();
    }
    return {};
}

// Now Playing's panels, as NowPlaying.js expects helper.calcLayout to share them out ("Dimensions
// must match wmpnowplayinghelper.cpp": its constants are the script's). 1 settings, 2 sidebar,
// 3 metadata, 4 playlist, 5 display, 6 captions, 7 banner.
void MediaPlayer::calcLayout(int width, int height, bool captions, bool banner)
{
    const int separator = 2, controlbar = 20;
    const auto setting = [this](const char *name) { return m_mediacenter.value(QLatin1String(name)).toInt(); };
    const auto shown = [this](const char *name) { return m_mediacenter.value(QLatin1String(name)).toBool(); };
    m_layout.clear();
    // the sidebar (playlist, media information) while the display keeps its 200
    int sidebar = 0;
    if ((shown("showplaylist") || shown("showmetadata")) && width - separator - 100 >= 200)
        sidebar = qBound(100, setting("playlistwidth"), qMin(500, width - separator - 200));
    // the settings under the display while the media keeps its 20
    int settings = 0;
    const int media = height - controlbar;
    if (shown("showsettings") && media - separator - 21 >= 20)
        settings = qBound(21, setting("settingsheight"), qMin(500, media - separator - 20));
    int metadata = 0, playlist = 0;
    if (sidebar) {
        if (shown("showmetadata"))
            metadata = shown("showplaylist") ? qBound(26, setting("metadataheight"), qMax(26, height - separator - 35)) : height;
        if (shown("showplaylist"))
            playlist = qMax(0, height - (metadata ? metadata + separator : 0));
    }
    int display = media - (settings ? settings + separator : 0);
    const int bannerHeight = banner && display - 36 >= 19 ? 36 : 0;
    display -= bannerHeight;
    int captionsHeight = 0;
    if (captions)
        captionsHeight = qBound(0, setting("captionsheight"), qMax(0, display - 19));
    display -= captionsHeight;
    m_layout = {{1, settings}, {2, sidebar}, {3, metadata}, {4, playlist}, {5, qMax(19, display)}, {6, captionsHeight}, {7, bannerHeight}};
}

// ---- the user's files ----

QImage MediaPlayer::image(const QString &url, const QString &skinFolder)
{
    if (url.isEmpty())
        return {};
    // the media's own picture: its tags' cover, or the Folder.jpg and AlbumArtSmall.jpg WMP keeps beside it
    if (url.startsWith(QLatin1String("WMPImage_AlbumArt"), Qt::CaseInsensitive))
        return albumArt(url.endsWith(QLatin1String("Large"), Qt::CaseInsensitive));
    static QHash<QString, QImage> cache;
    const QString key = skinFolder + QLatin1Char('|') + url;
    if (auto it = cache.constFind(key); it != cache.constEnd())
        return *it;
    QImage image;
    const QString lower = url.toLower();
    if (lower.startsWith(QLatin1String("res://")) && m_loc) {
        // res://wmploc/#1970, res://wmploc.dll/RT_BITMAP/#1970, res://wmploc/IDB_OLDSTRBMP_WMLOGO
        QStringList parts = url.mid(6).split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (!parts.isEmpty())
            parts.removeFirst();
        QString type = parts.size() > 1 ? parts.first() : QStringLiteral("2");
        QString name = parts.value(parts.size() > 1 ? 1 : 0);
        if (type.compare(QLatin1String("RT_BITMAP"), Qt::CaseInsensitive) == 0 || type.compare(QLatin1String("RT_IMAGE"), Qt::CaseInsensitive) == 0)
            type = QStringLiteral("2");
        if (name.startsWith(QLatin1Char('#')))
            name = name.mid(1);
        if (type == QLatin1String("2")) {
            bool numeric = false;
            const int id = name.toInt(&numeric);
            image = numeric ? xpfiles::bitmap(*m_loc, id) : QImage();
            if (!numeric) {
                bool partial = false;
                image = w4w::decodeDib(m_loc->find(QStringLiteral("2"), name), &partial);
            }
        } else {
            image = QImage::fromData(m_loc->find(type, name));
        }
    } else if (!skinFolder.isEmpty()) {
        // a skin's own file; its .bmp files are often RLE-compressed, which Qt's reader does not take
        QFile file(QDir(skinFolder).filePath(url));
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray data = file.readAll();
            if (data.startsWith("BM") && data.size() > 14) {
                bool partial = false;
                image = w4w::decodeDib(data.mid(14), &partial);
            }
            if (image.isNull())
                image = QImage::fromData(data);
        }
    }
    cache.insert(key, image);
    return image;
}

QImage MediaPlayer::albumArt(bool large) const
{
    if (m_current < 0 || m_current >= m_items.size())
        return {};
    const QMediaMetaData data = m_player->metaData();
    for (const QMediaMetaData::Key key : {QMediaMetaData::CoverArtImage, QMediaMetaData::ThumbnailImage}) {
        const QImage picture = data.value(key).value<QImage>();
        if (!picture.isNull())
            return picture;
    }
    const QUrl url = m_items[m_current].url;
    if (!url.isLocalFile())
        return {};
    const QDir folder = QFileInfo(url.toLocalFile()).absoluteDir();
    for (const QString &name : large ? QStringList{QStringLiteral("Folder.jpg"), QStringLiteral("AlbumArtSmall.jpg")}
                                     : QStringList{QStringLiteral("AlbumArtSmall.jpg"), QStringLiteral("Folder.jpg")})
        for (const QString &entry : folder.entryList({name}, QDir::Files))   // whatever its case
            if (const QImage picture(folder.filePath(entry)); !picture.isNull())
                return picture;
    return {};
}

QString MediaPlayer::string(const QString &url)
{
    const int at = url.indexOf(QLatin1String("RT_STRING/#"), 0, Qt::CaseInsensitive);
    if (!url.startsWith(QLatin1String("res://"), Qt::CaseInsensitive) || at < 0)
        return url;
    return text(url.mid(at + 11).toInt());
}

QString MediaPlayer::script(const QString &url, const QString &skinFolder)
{
    if (url.startsWith(QLatin1String("res://"), Qt::CaseInsensitive)) {
        if (!m_loc)
            return {};
        QString name = url.section(QLatin1Char('/'), -1);
        if (name.startsWith(QLatin1Char('#')))
            name = name.mid(1);
        name = m_skinNames.value(name.toLower(), name.toUpper());
        return QString::fromLatin1(m_loc->find(QStringLiteral("256"), name));
    }
    QFile file(QDir(skinFolder).filePath(url));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = file.readAll();
    if (data.startsWith("\xff\xfe"))
        return QString::fromUtf16(reinterpret_cast<const char16_t *>(data.constData() + 2), (data.size() - 2) / 2);
    return QString::fromLatin1(data);
}

std::optional<wmpskin::Node> MediaPlayer::task(QString *folder)
{
    folder->clear();
    if (m_task.compare(QLatin1String("NowPlaying"), Qt::CaseInsensitive) == 0)
        return skin(QStringLiteral("NowPlaying.wsz"));
    static const QSet<QString> widgets{QStringLiteral("skinviewer"), QStringLiteral("medialibrary"), QStringLiteral("mediaguide"),
                                       QStringLiteral("cdaudio"), QStringLiteral("radiotuner"), QStringLiteral("portabledevice"),
                                       QStringLiteral("services")};
    if (widgets.contains(m_task.toLower()))
        return std::nullopt;   // taskWidget
    // The tasks wmp.dll draws itself (Media Guide is a web page, the Media Library and the CD tasks
    // its own windows) are not ported: their place shows the player's colours and the task's name.
    wmpskin::Node view;
    view.tag = QStringLiteral("view");
    view.attributes = {{QStringLiteral("backgroundColor"), QStringLiteral("wmpprop:mediacenter.appColorBlackBackground")}};
    wmpskin::Node caption;
    caption.tag = QStringLiteral("text");
    static const QHash<QString, int> names{
        {QStringLiteral("mediaguide"), 16001}, {QStringLiteral("cdaudio"), 16002}, {QStringLiteral("medialibrary"), 16003},
        {QStringLiteral("radiotuner"), 16004}, {QStringLiteral("portabledevice"), 16005}, {QStringLiteral("services"), 16006},
        {QStringLiteral("skinviewer"), 16007},
    };
    Q_UNUSED(names);
    caption.attributes = {
        {QStringLiteral("value"), m_task},
        {QStringLiteral("left"), 16}, {QStringLiteral("top"), 12},
        {QStringLiteral("fontSize"), 14},
        {QStringLiteral("foregroundColor"), QStringLiteral("white")},
    };
    view.children.append(caption);
    return view;
}

std::optional<wmpskin::Node> MediaPlayer::plugin(const QString &objectId)
{
    // The Now Playing plugins wmp.dll names (its table of views and their CLSIDs) and the skin each
    // loads from wmploc.dll.
    static const QHash<QString, QString> skins{
        {QStringLiteral("vizview"), QStringLiteral("Viz.wsz")},
        {QStringLiteral("videoview"), QStringLiteral("Video.wsz")},
        {QStringLiteral("borderview"), QStringLiteral("Border.wsz")},
        {QStringLiteral("metadataview"), QStringLiteral("MediaInfo.wsz")},
        {QStringLiteral("mediainfoview"), QStringLiteral("MediaInfo.wsz")},
        {QStringLiteral("albumartview"), QStringLiteral("AlbumArt.wsz")},
        {QStringLiteral("captionsview"), QStringLiteral("Captions.wsz")},
        {QStringLiteral("bannerview"), QStringLiteral("Banner.wsz")},
        {QStringLiteral("eqview"), QStringLiteral("Equalizer.wsz")},
        {QStringLiteral("srsview"), QStringLiteral("SRSWow.wsz")},
        {QStringLiteral("videosettingsview"), QStringLiteral("VSettings.wsz")},
        {QStringLiteral("advancedvideosettingsview"), QStringLiteral("VSettings.wsz")},
        {QStringLiteral("advancedaudioview"), QStringLiteral("AdvancedAudio.wsz")},
        {QStringLiteral("crossfadeview"), QStringLiteral("CrossFade.wsz")},
        {QStringLiteral("medialinkview"), QStringLiteral("MediaLink.wsz")},
        {QStringLiteral("appcolorview"), QStringLiteral("AppColorChooser.wsz")},
        {QStringLiteral("htmlview"), QStringLiteral("HTMLView.wsz")},
    };
    // the video when there is one, the visualization otherwise
    QString id = objectId.toLower();
    if (id == QLatin1String("vizview") && m_player->hasVideo())
        id = QStringLiteral("videoview");
    const QString name = skins.value(id);
    return name.isEmpty() ? std::nullopt : skin(name);
}

QString MediaPlayer::preference(const QString &name)
{
    // each skin its own; wmp.dll's theme.loadPreference gives "--" for one never saved
    QSettings settings(QStringLiteral("win4wine"), QStringLiteral("mediaplayer"));
    return settings.value(QStringLiteral("preferences/") + m_preferenceGroup + name, QStringLiteral("--")).toString();
}

void MediaPlayer::setPreference(const QString &name, const QString &value)
{
    QSettings settings(QStringLiteral("win4wine"), QStringLiteral("mediaplayer"));
    settings.setValue(QStringLiteral("preferences/") + m_preferenceGroup + name, value);
}

// ---- the window ----

bool MediaPlayer::eventFilter(QObject *watched, QEvent *event)
{
    // full screen: the mouse brings the controls back, Esc leaves it as wmp.dll does
    if (m_fullScreen && watched->isWidgetType() && (watched == m_fullScreen || m_fullScreen->isAncestorOf(static_cast<QWidget *>(watched)))) {
        if (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress)
            showFullScreenControls(true);
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            setFullScreen(false);
            return true;
        }
    }
    const bool ours = watched == m_window || (m_skinWindow && watched == m_skinWindow) || (m_fullScreen && watched == m_fullScreen)
        || (m_menuFrame && watched == m_menuFrame);
    if (m_menuFrame && watched == m_menuFrame && event->type() == QEvent::Close) {
        // the frame's close button: the player's Exit
        m_player->stop();
        QApplication::quit();
    }
    // wmploc.dll's keys: accelerator table 102 in full mode, 4002 in skin mode
    if (ours && event->type() == QEvent::KeyPress && m_loc) {
        static const QList<xpprogram::Accelerator> full = xpprogram::accelerators(*m_loc, QStringLiteral("102")),
                                                   skinned = xpprogram::accelerators(*m_loc, QStringLiteral("4002"));
        if (const int id = xpprogram::command(watched == m_skinWindow ? skinned : full, static_cast<QKeyEvent *>(event))) {
            command(id);
            return true;
        }
    }
    if (ours && (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove)) {
        auto *drag = static_cast<QDragMoveEvent *>(event);
        if (drag->mimeData()->hasUrls()) {
            drag->acceptProposedAction();
            return true;
        }
    } else if (ours && event->type() == QEvent::Drop) {
        auto *drop = static_cast<QDropEvent *>(event);
        QStringList files;
        for (const QUrl &url : drop->mimeData()->urls())
            files.append(url.isLocalFile() ? url.toLocalFile() : url.toString());
        m_items.clear();
        m_current = -1;
        open(files);
        drop->acceptProposedAction();
        return true;
    }
    return wmp::Model::eventFilter(watched, event);
}

void MediaPlayer::windowRequest(const QString &request)
{
    if (!m_window)
        return;
    if (request == QLatin1String("close")) {
        fullWindow()->close();
        m_player->stop();
        QApplication::quit();
    } else if (request == QLatin1String("minimize")) {
        fullWindow()->showMinimized();
    } else if (request == QLatin1String("maximize")) {
        fullWindow()->showMaximized();
        emit changed(QStringLiteral("app"), QStringLiteral("maximized"));
    } else if (request == QLatin1String("restore")) {
        fullWindow()->showNormal();
        emit changed(QStringLiteral("app"), QStringLiteral("maximized"));
    } else if (request.startsWith(QLatin1String("automenu:")) || request.startsWith(QLatin1String("menu:"))) {
        showMenu(request.section(QLatin1Char(':'), 1));
    } else if (request == QLatin1String("skinmode")) {
        showSkin(currentSkin());
    } else if (request == QLatin1String("returntomediacenter")) {
        setFullScreen(false);
    }
}

void MediaPlayer::setFullScreen(bool on)
{
    if (on == !m_fullScreen.isNull() || !m_window)
        return;
    if (!on) {
        m_fullScreen->deleteLater();
        m_fullScreen.clear();
        fullWindow()->show();
        fullWindow()->activateWindow();
        changedAll(QStringLiteral("player"), {"fullscreen"});
        return;
    }
    const QRect screen = (m_window->screen() ? m_window->screen() : QGuiApplication::primaryScreen())->geometry();
    auto *window = new QWidget(nullptr, Qt::FramelessWindowHint);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setWindowTitle(m_window->windowTitle());
    window->setWindowIcon(m_window->windowIcon());
    window->setAutoFillBackground(true);
    QPalette palette = window->palette();
    palette.setColor(QPalette::Window, Qt::black);
    window->setPalette(palette);
    window->setGeometry(screen);
    m_fullScreen = window;
    const auto view = [this, window](const QString &name) -> wmp::SkinView * {
        const std::optional<wmpskin::Node> document = name.endsWith(QLatin1String(".wsz")) ? skin(name) : plugin(name);
        if (!document)
            return nullptr;
        auto *skinView = new wmp::SkinView(this, *document, QString(), window);
        skinView->windowRequest = [this](const QString &request) { windowRequest(request); };
        skinView->installEventFilter(this);
        skinView->setMouseTracking(true);
        return skinView;
    };
    // the display under everything: the video, or the visualization
    if (wmp::SkinView *display = view(QStringLiteral("VizView"))) {
        display->setGeometry(QRect(QPoint(), screen.size()));
        display->show();
        display->start();
    }
    m_fullScreenTop = view(QStringLiteral("FullScreenTopLayout.wsz"));
    m_fullScreenBottom = view(QStringLiteral("FullScreenBottomLayout.wsz"));
    if (m_fullScreenTop) {
        m_fullScreenTop->setGeometry(0, 0, screen.width(), m_fullScreenTop->preferredSize().height());
        m_fullScreenTop->show();
        m_fullScreenTop->start();
    }
    if (m_fullScreenBottom) {
        const int height = m_fullScreenBottom->preferredSize().height();
        m_fullScreenBottom->setGeometry(0, screen.height() - height, screen.width(), height);
        m_fullScreenBottom->show();
        m_fullScreenBottom->start();
    }
    if (!m_fullScreenIdle) {
        m_fullScreenIdle = new QTimer(this);
        m_fullScreenIdle->setSingleShot(true);
        m_fullScreenIdle->setInterval(3000);
        connect(m_fullScreenIdle, &QTimer::timeout, this, [this] {
            if (!m_mediacenter.value(QStringLiteral("pinfullscreencontrols")).toBool())
                showFullScreenControls(false);
        });
    }
    window->installEventFilter(this);
    window->setMouseTracking(true);
    window->showFullScreen();
    fullWindow()->hide();
    showFullScreenControls(true);
    changedAll(QStringLiteral("player"), {"fullscreen"});
}

void MediaPlayer::showFullScreenControls(bool shown)
{
    if (!m_fullScreen)
        return;
    for (wmp::SkinView *bar : {m_fullScreenTop.data(), m_fullScreenBottom.data()})
        if (bar)
            bar->setVisible(shown);
    m_fullScreen->setCursor(shown ? Qt::ArrowCursor : Qt::BlankCursor);
    if (shown && m_fullScreenIdle)
        m_fullScreenIdle->start();
}

// ---- skin mode ----

QStringList MediaPlayer::skinFiles() const
{
    // XP's skins from the media, then those the user added
    QStringList files;
    QStringList folders{QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/win4wine/wmp-skins")};
    if (const QString revert = xpfiles::find(QStringLiteral("Revert.wmz")); !revert.isEmpty())
        folders.prepend(QFileInfo(revert).absolutePath());
    for (const QString &folder : std::as_const(folders))
        for (const QFileInfo &file : QDir(folder).entryInfoList({QStringLiteral("*.wmz"), QStringLiteral("*.wms")}, QDir::Files, QDir::Name | QDir::IgnoreCase))
            files.append(file.absoluteFilePath());
    return files;
}

QString MediaPlayer::currentSkin() const
{
    // the skin chosen last; wmp.dll's first is Revert
    QSettings settings(QStringLiteral("win4wine"), QStringLiteral("mediaplayer"));
    const QString chosen = settings.value(QStringLiteral("skin")).toString();
    const QStringList files = skinFiles();
    if (files.contains(chosen))
        return chosen;
    for (const QString &file : files)
        if (QFileInfo(file).completeBaseName().compare(QLatin1String("Revert"), Qt::CaseInsensitive) == 0)
            return file;
    return files.value(0);
}

std::optional<wmpskin::Node> MediaPlayer::loadSkin(const QString &file, QString *folder) const
{
    // A .wmz is a zip: its files go to a folder of their own, which the skin's pictures and scripts are read from.
    QString wms = file;
    if (file.endsWith(QLatin1String(".wmz"), Qt::CaseInsensitive)) {
        const QFileInfo info(file);
        const QString target = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/win4wine/wmp-skins/")
            + info.completeBaseName();
        const QFileInfo stamp(target + QStringLiteral("/.unpacked"));
        if (!stamp.exists() || stamp.lastModified() < info.lastModified()) {
            QDir(target).removeRecursively();
            QDir().mkpath(target);
            if (QProcess::execute(QStringLiteral("bsdtar"), {QStringLiteral("-xf"), file, QStringLiteral("-C"), target}) != 0)
                return std::nullopt;
            QFile(stamp.filePath()).open(QIODevice::WriteOnly);
        }
        const QFileInfoList found = QDir(target).entryInfoList({QStringLiteral("*.wms")}, QDir::Files);
        if (found.isEmpty())
            return std::nullopt;
        wms = found.first().absoluteFilePath();
    }
    QFile source(wms);
    if (!source.open(QIODevice::ReadOnly))
        return std::nullopt;
    std::optional<wmpskin::Node> document = wmpskin::readXml(source.readAll());
    if (!document || document->tag != QLatin1String("theme"))
        return std::nullopt;
    wmpskin::expand(*document, m_predefined);
    *folder = QFileInfo(wms).absolutePath();
    return document;
}

void MediaPlayer::showSkin(const QString &file)
{
    QString folder;
    std::optional<wmpskin::Node> document = loadSkin(file, &folder);
    if (!document)
        return;
    QSettings settings(QStringLiteral("win4wine"), QStringLiteral("mediaplayer"));
    settings.setValue(QStringLiteral("skin"), file);
    closeSkin();
    m_preferenceGroup = QFileInfo(file).completeBaseName() + QLatin1Char('/');
    m_skinWindow = new wmp::SkinView(this, *document, folder);
    // a skin without titleBar="false" has the system's frame (Classic)
    m_skinWindow->setWindowFlag(Qt::FramelessWindowHint, !m_skinWindow->hasTitleBar());
    m_skinWindow->setWindowTitle(m_window ? m_window->windowTitle() : QStringLiteral("Windows Media Player"));
    m_skinWindow->setWindowIcon(m_window ? m_window->windowIcon() : QIcon());
    m_skinWindow->setAcceptDrops(true);
    m_skinWindow->installEventFilter(this);
    m_skinWindow->windowRequest = [this, document, folder](const QString &request) {
        if (request == QLatin1String("returntomediacenter")) {
            QTimer::singleShot(0, this, &MediaPlayer::showFullMode);
        } else if (request == QLatin1String("close") && sender() == nullptr) {
            QTimer::singleShot(0, this, [this] { windowRequest(QStringLiteral("close")); });
        } else if (request == QLatin1String("minimize") && m_skinWindow) {
            m_skinWindow->showMinimized();
        } else if (request.startsWith(QLatin1String("openview:"))) {
            // theme.openView: another of the theme's views, a window of its own
            auto *view = new wmp::SkinView(this, *document, folder, nullptr, QSize(), request.section(QLatin1Char(':'), 1));
            view->setAttribute(Qt::WA_DeleteOnClose);
            view->setWindowFlag(Qt::FramelessWindowHint);
            view->windowRequest = [view](const QString &inner) {
                if (inner == QLatin1String("close"))
                    view->close();
            };
            m_skinViews.append(view);
            view->show();
            view->start();
        } else if (request.startsWith(QLatin1String("automenu:")) || request.startsWith(QLatin1String("menu:"))) {
            showMenu(request.section(QLatin1Char(':'), 1));
        }
    };
    const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
    QRect geometry(QPoint(), m_skinWindow->preferredSize());
    geometry.moveCenter(screen.center());
    if (const QPoint at = settings.value(QStringLiteral("skinPosition")).toPoint(); !at.isNull() && screen.contains(at))
        geometry.moveTopLeft(at);
    m_skinWindow->setGeometry(geometry);
    if (m_skinWindow->isResizable() && m_skinWindow->hasTitleBar())
        m_skinWindow->setMinimumSize(m_skinWindow->minimumViewSize().expandedTo(QSize(1, 1)));
    else
        m_skinWindow->setFixedSize(geometry.size());
    if (m_window)
        fullWindow()->hide();
    m_skinWindow->show();
    m_skinWindow->start();
    settings.setValue(QStringLiteral("mode"), QStringLiteral("skin"));
    settings.sync();
}

void MediaPlayer::closeSkin()
{
    for (const QPointer<wmp::SkinView> &view : std::as_const(m_skinViews))
        if (view)
            view->close();
    m_skinViews.clear();
    if (m_skinWindow) {
        QSettings settings(QStringLiteral("win4wine"), QStringLiteral("mediaplayer"));
        settings.setValue(QStringLiteral("skinPosition"), m_skinWindow->pos());
        m_skinWindow->close();
        m_skinWindow->deleteLater();
    }
}

void MediaPlayer::showFullMode()
{
    closeSkin();
    m_preferenceGroup.clear();
    QSettings(QStringLiteral("win4wine"), QStringLiteral("mediaplayer")).setValue(QStringLiteral("mode"), QStringLiteral("full"));
    if (m_window) {
        fullWindow()->show();
        fullWindow()->raise();
        fullWindow()->activateWindow();
    }
}

QWidget *MediaPlayer::fullWindow() const
{
    return m_menuFrame ? m_menuFrame.data() : static_cast<QWidget *>(m_window.data());
}

void MediaPlayer::setMenuBarShown(bool shown)
{
    if (!m_window || shown == !m_menuFrame.isNull())
        return;
    const QRect client(m_window->mapToGlobal(QPoint()), m_window->size());
    const bool visible = fullWindow()->isVisible();
    if (!shown) {
        QWidget *frame = m_menuFrame;
        m_menuFrame.clear();
        m_window->setParent(nullptr, Qt::Window | Qt::FramelessWindowHint);
        m_window->setGeometry(client);
        if (visible)
            m_window->show();
        frame->deleteLater();
        return;
    }
    auto *frame = new QWidget(nullptr, Qt::Window);
    frame->setWindowTitle(m_window->windowTitle());
    frame->setWindowIcon(m_window->windowIcon());
    frame->setAcceptDrops(true);
    frame->installEventFilter(this);
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *bar = new QMenuBar(frame);
    bar->setNativeMenuBar(false);   // in the window, as XP has it, never a desktop's global menu
    if (m_loc) {
        const QHash<int, QAction *> actions = xpprogram::fillMenuBar(bar, *m_loc, 102);
        connectMenu(actions);
        // the checks follow the player when a menu opens
        for (QMenu *menu : bar->findChildren<QMenu *>())
            connect(menu, &QMenu::aboutToShow, this, [this, actions] {
                for (const int id : {int(Shuffle), int(Repeat), int(Mute)})
                    if (QAction *action = actions.value(id))
                        action->setChecked(id == Shuffle ? m_shuffle : id == Repeat ? m_loop : m_audio->isMuted());
            });
    }
    layout->setMenuBar(bar);
    m_window->setParent(frame);
    layout->addWidget(m_window);
    m_menuFrame = frame;
    m_window->show();
    // the frame round the client the skin had
    frame->resize(client.width(), client.height() + bar->sizeHint().height());
    frame->move(client.topLeft() - QPoint(0, bar->sizeHint().height()));
    if (visible)
        frame->show();
}

void MediaPlayer::connectMenu(const QHash<int, QAction *> &actions)
{
    for (auto it = actions.begin(); it != actions.end(); ++it) {
        QAction *action = it.value();
        const int id = it.key();
        static const QSet<int> handled{Open, OpenUrl, CloseMedia, Exit, PlayPause, Stop, Fast, Normal, Slow, Previous, Next, Rewind,
                                       FastForward, Shuffle, Repeat, VolumeUp, VolumeDown, Mute, FullScreen, HideTaskbar,
                                       FullMode, SkinMode};
        action->setEnabled(handled.contains(id) || (id >= GoToFirst && id <= SkinChooser));
        if (id == Shuffle || id == Repeat || id == Mute) {
            action->setCheckable(true);
            action->setChecked(id == Shuffle ? m_shuffle : id == Repeat ? m_loop : m_audio->isMuted());
        }
        connect(action, &QAction::triggered, this, [this, id] { command(id); });
    }
}

// The Skin Chooser: dialog 203 of wmploc.dll, stretched over the task's room as wmp.dll does it
QWidget *MediaPlayer::taskWidget(QWidget *parent)
{
    if (m_task.compare(QLatin1String("MediaLibrary"), Qt::CaseInsensitive) == 0) {
        auto *library = new QWidget(parent);
        auto *layout = new QVBoxLayout(library);
        layout->setContentsMargins(8, 8, 8, 8);
        auto *search = new QLineEdit(library);
        search->setPlaceholderText(QObject::tr("Search"));
        auto *list = new QTreeWidget(library);
        list->setRootIsDecorated(false);
        list->setHeaderLabels({QObject::tr("Title"), QObject::tr("Artist"), QObject::tr("Album"), QObject::tr("Length")});
        layout->addWidget(search);
        layout->addWidget(list, 1);
        QStringList patterns;
        for (const QString &filter : mediaFilters())
            patterns.append(filter.mid(2)); // "*.mp3" -> "mp3"
        QDirIterator it(QStandardPaths::writableLocation(QStandardPaths::MusicLocation), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            if (!patterns.contains(QFileInfo(path).suffix().toLower(), Qt::CaseInsensitive))
                continue;
            const QFileInfo info(path);
            auto *item = new QTreeWidgetItem(list, {info.completeBaseName(), QString(), info.dir().dirName(), QString()});
            item->setData(0, Qt::UserRole, path);
        }
        list->setColumnWidth(0, 260);
        QObject::connect(search, &QLineEdit::textChanged, list, [list](const QString &text) {
            for (int i = 0; i < list->topLevelItemCount(); ++i)
                list->topLevelItem(i)->setHidden(!text.isEmpty()
                    && !list->topLevelItem(i)->text(0).contains(text, Qt::CaseInsensitive)
                    && !list->topLevelItem(i)->text(2).contains(text, Qt::CaseInsensitive));
        });
        QObject::connect(list, &QTreeWidget::itemActivated, library, [this, list](QTreeWidgetItem *item) {
            if (item)
                open({item->data(0, Qt::UserRole).toString()});
        });
        QObject::connect(list, &QTreeWidget::itemDoubleClicked, library, [this, list](QTreeWidgetItem *item) {
            if (item)
                open({item->data(0, Qt::UserRole).toString()});
        });
        return library;
    }
    if (m_task.compare(QLatin1String("SkinViewer"), Qt::CaseInsensitive) != 0 || !m_loc) {
        // The online tasks are gone and there is no CD or portable device here: their page says so
        // instead of the bare task name.
        if (m_task.compare(QLatin1String("MediaLibrary"), Qt::CaseInsensitive) != 0) {
            static const QHash<QString, QString> messages{
                {QStringLiteral("mediaguide"), QObject::tr("Media Guide is not available: the online service has closed.")},
                {QStringLiteral("radiotuner"), QObject::tr("Radio Tuner is not available: the online service has closed.")},
                {QStringLiteral("cdaudio"), QObject::tr("Insert an audio CD in a drive.")},
                {QStringLiteral("portabledevice"), QObject::tr("No portable device is connected.")},
                {QStringLiteral("services"), QObject::tr("No premium services are available.")},
            };
            const QString message = messages.value(m_task.toLower());
            if (!message.isEmpty()) {
                auto *page = new QWidget(parent);
                page->setAutoFillBackground(true);
                QPalette palette = page->palette();
                palette.setColor(QPalette::Window, QColor(57, 68, 114));
                page->setPalette(palette);
                auto *layout = new QVBoxLayout(page);
                auto *label = new QLabel(message, page);
                label->setWordWrap(true);
                label->setAlignment(Qt::AlignCenter);
                label->setStyleSheet(QStringLiteral("color: white; font-size: 14px;"));
                layout->addWidget(label);
                return page;
            }
            return nullptr;
        }
    }
    auto *chooser = new QWidget(parent);
    // opaque over the taskcenter's "Opening..." (the style's window colour may have no alpha)
    QPalette palette = chooser->palette();
    QColor face = palette.color(QPalette::Window);
    face.setAlpha(255);
    palette.setColor(QPalette::Window, face);
    chooser->setPalette(palette);
    chooser->setAutoFillBackground(true);
    auto *page = new QWidget(chooser);
    const xpdialog::Page dialog = xpdialog::build(*m_loc, DIALOG_SKIN_CHOOSER, page);
    QWidget *listPlace = dialog.controls.value(ID_LIST);
    QWidget *toolbarPlace = dialog.controls.value(ID_TOOLBAR);
    auto *preview = qobject_cast<QLabel *>(dialog.controls.value(ID_PREVIEW));
    auto *title = qobject_cast<QLabel *>(dialog.controls.value(ID_TITLE));
    auto *author = qobject_cast<QLabel *>(dialog.controls.value(ID_AUTHOR));
    auto *copyright = qobject_cast<QLabel *>(dialog.controls.value(ID_COPYRIGHT));
    if (QWidget *hint = dialog.controls.value(ID_HINT))
        hint->hide();
    auto *list = new QListWidget(page);
    list->setGeometry(listPlace ? listPlace->geometry() : QRect());
    delete listPlace;
    // the toolbar's buttons, as text buttons (TBSTYLE_LIST)
    auto *toolbar = new QWidget(page);
    toolbar->setGeometry(toolbarPlace ? toolbarPlace->geometry().adjusted(0, 0, 0, 6) : QRect());
    delete toolbarPlace;
    auto *row = new QHBoxLayout(toolbar);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    auto *apply = new QToolButton(toolbar);
    apply->setText(toolbarText(text(STRING_APPLY_SKIN)));
    apply->setToolTip(text(STRING_APPLY_TIP));
    auto *more = new QToolButton(toolbar);
    more->setText(toolbarText(text(STRING_MORE_SKINS)));
    more->setToolTip(text(STRING_MORE_TIP));
    more->setEnabled(false);   // Microsoft's skin gallery is gone
    auto *remove = new QToolButton(toolbar);
    remove->setText(QStringLiteral("r"));
    remove->setFont(QFont(QStringLiteral("Marlett")));
    remove->setToolTip(text(STRING_DELETE_SKIN));
    for (QToolButton *button : {apply, more, remove}) {
        button->setAutoRaise(true);
        row->addWidget(button);
    }
    row->addStretch(1);
    const QStringList files = skinFiles();
    const QString current = currentSkin();
    for (const QString &file : files) {
        auto *item = new QListWidgetItem(QFileInfo(file).completeBaseName(), list);
        item->setData(Qt::UserRole, file);
        if (file == current)
            list->setCurrentItem(item);
    }
    const QString titleWords = title ? title->text() : QString(), authorWords = author ? author->text() : QString(),
                  copyrightWords = copyright ? copyright->text() : QString();
    const auto fill = [](const QString &words, const QString &value) { return QString(words).replace(QLatin1String("%1!.1023ls!"), value); };
    const auto show = [=, this] {
        QListWidgetItem *item = list->currentItem();
        if (!item)
            return;
        const QString file = item->data(Qt::UserRole).toString();
        remove->setEnabled(!file.startsWith(QFileInfo(xpfiles::find(QStringLiteral("Revert.wmz"))).absolutePath()));
        QString folder;
        std::optional<wmpskin::Node> document = loadSkin(file, &folder);
        if (title)
            title->setText(fill(titleWords, item->text()));
        if (author)
            author->setText(fill(authorWords, document ? document->attribute(QStringLiteral("author")).toString() : QString()));
        if (copyright)
            copyright->setText(fill(copyrightWords, document ? document->attribute(QStringLiteral("copyright")).toString() : QString()));
        if (!preview)
            return;
        preview->setVisible(true);
        preview->setAlignment(Qt::AlignCenter);
        preview->setPixmap(document ? skinPicture(*document, folder, preview->size()) : QPixmap());
    };
    QObject::connect(list, &QListWidget::currentItemChanged, chooser, show);
    const auto applyChosen = [this, list] {
        if (QListWidgetItem *item = list->currentItem())
            showSkin(item->data(Qt::UserRole).toString());
    };
    QObject::connect(apply, &QToolButton::clicked, chooser, applyChosen);
    QObject::connect(list, &QListWidget::itemActivated, chooser, applyChosen);
    QObject::connect(remove, &QToolButton::clicked, chooser, [list] {
        QListWidgetItem *item = list->currentItem();
        if (item && QFile::remove(item->data(Qt::UserRole).toString()))
            delete item;
    });
    // its controls follow the task's size: the list down to the bottom, the preview over the rest
    const QRect listRect = list->geometry(), previewRect = preview ? preview->geometry() : QRect();
    const QSize designed = page->size();
    page->setMinimumSize(0, 0);
    page->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    auto *resizer = new SizeFollower(chooser, [=](QSize size) {
        page->setGeometry(QRect(QPoint(), size));
        const int dw = size.width() - designed.width(), dh = size.height() - designed.height();
        list->setGeometry(listRect.adjusted(0, 0, 0, dh));
        if (preview)
            preview->setGeometry(previewRect.adjusted(0, 0, dw, dh));
        for (QLabel *label : {title, author, copyright})
            if (label)
                label->move(label->x(), label->property("designedY").toInt() + dh);
    });
    for (QLabel *label : {title, author, copyright})
        if (label)
            label->setProperty("designedY", label->y());
    Q_UNUSED(resizer);
    QTimer::singleShot(0, chooser, show);
    return chooser;
}

QPixmap MediaPlayer::skinPicture(const wmpskin::Node &document, const QString &folder, QSize room)
{
    // the skin drawn as it would open, in its own shape, fitted into ROOM
    wmp::SkinView view(this, document, folder);
    view.resize(view.preferredSize());
    QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    view.render(&image, QPoint(), view.shape(), QWidget::DrawChildren);
    if (image.width() > room.width() || image.height() > room.height())
        image = image.scaled(room, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return QPixmap::fromImage(image);
}

void MediaPlayer::showMenu(const QString &name)
{
    if (!m_loc)
        return;
    // automenu.show('play'): the main menu's popup of that name (menu 102 of wmploc.dll)
    QMenuBar bar;
    const QHash<int, QAction *> actions = xpprogram::fillMenuBar(&bar, *m_loc, 102);
    QMenu *chosen = nullptr;
    for (QAction *action : bar.actions())
        if (action->menu() && action->text().remove(QLatin1Char('&')).compare(name.isEmpty() ? QStringLiteral("Play") : name, Qt::CaseInsensitive) == 0)
            chosen = action->menu();
    if (!chosen)
        return;
    connectMenu(actions);
    chosen->exec(QCursor::pos());
}

void MediaPlayer::command(int id)
{
    switch (id) {
    case Open:
        openDialog();
        break;
    case CloseMedia:
        call(QStringLiteral("player"), QStringLiteral("close"), {});
        break;
    case Exit:
        windowRequest(QStringLiteral("close"));
        break;
    case PlayPause:
        call(QStringLiteral("player.controls"), m_playState == 3 ? QStringLiteral("pause") : QStringLiteral("play"), {});
        break;
    case Stop:
        call(QStringLiteral("player.controls"), QStringLiteral("stop"), {});
        break;
    case Fast:
    case Normal:
    case Slow:
        set(QStringLiteral("player.settings"), QStringLiteral("rate"), id == Fast ? 1.4 : id == Slow ? 0.5 : 1.0);
        break;
    case Previous:
        call(QStringLiteral("player.controls"), QStringLiteral("previous"), {});
        break;
    case Next:
        call(QStringLiteral("player.controls"), QStringLiteral("next"), {});
        break;
    case Rewind:
        call(QStringLiteral("player.controls"), QStringLiteral("fastreverse"), {});
        break;
    case FastForward:
        call(QStringLiteral("player.controls"), QStringLiteral("fastforward"), {});
        break;
    case Shuffle:
        call(QStringLiteral("player.settings"), QStringLiteral("setmode"), {QStringLiteral("shuffle"), !m_shuffle});
        break;
    case Repeat:
        call(QStringLiteral("player.settings"), QStringLiteral("setmode"), {QStringLiteral("loop"), !m_loop});
        break;
    case VolumeUp:
    case VolumeDown:
        set(QStringLiteral("player.settings"), QStringLiteral("volume"), qRound(m_audio->volume() * 100) + (id == VolumeUp ? 10 : -10));
        break;
    case Mute:
        m_audio->setMuted(!m_audio->isMuted());
        break;
    case FullScreen:
        set(QStringLiteral("player"), QStringLiteral("fullscreen"), m_fullScreen.isNull());
        break;
    case FullMode:
        showFullMode();
        break;
    case SkinMode:
        showSkin(currentSkin());
        break;
    case HideTaskbar:
        set(QStringLiteral("app"), QStringLiteral("taskbarvisible"), !m_app.value(QStringLiteral("taskbarvisible")).toBool());
        break;
    default:
        if (id >= GoToFirst && id <= SkinChooser) {
            static const char *const tasks[] = {"NowPlaying", "MediaGuide", "CDAudio", "MediaLibrary", "RadioTuner", "PortableDevice",
                                                "Services", "SkinViewer"};
            set(QStringLiteral("app"), QStringLiteral("currenttask"), QLatin1String(tasks[id - GoToFirst]));
        }
        break;
    }
}

void MediaPlayer::openDialog()
{
    const QString filter = QStringLiteral("Media files (%1);;All Files (*)").arg(mediaFilters().join(QLatin1Char(' ')));
    const QStringList files = QFileDialog::getOpenFileNames(m_window, text(18872).isEmpty() ? QStringLiteral("Open") : QStringLiteral("Open"),
                                                            QStandardPaths::writableLocation(QStandardPaths::MusicLocation), filter);
    if (files.isEmpty())
        return;
    m_items.clear();
    m_current = -1;
    open(files);
}

void MediaPlayer::updateSpectrum(const QAudioBuffer &buffer)
{
    // 32 bands of the left channel, as a visualization's level each frame
    const QAudioFormat format = buffer.format();
    const int channels = qMax(1, format.channelCount());
    const int frames = int(buffer.frameCount());
    if (frames < 64)
        return;
    const int size = 512;
    std::vector<std::complex<float>> samples(size);
    for (int i = 0; i < size; ++i) {
        const int frame = i * frames / size;
        float value = 0;
        if (format.sampleFormat() == QAudioFormat::Float)
            value = buffer.constData<float>()[frame * channels];
        else if (format.sampleFormat() == QAudioFormat::Int16)
            value = buffer.constData<qint16>()[frame * channels] / 32768.f;
        else if (format.sampleFormat() == QAudioFormat::Int32)
            value = float(buffer.constData<qint32>()[frame * channels] / 2147483648.0);
        else if (format.sampleFormat() == QAudioFormat::UInt8)
            value = (buffer.constData<quint8>()[frame * channels] - 128) / 128.f;
        const float window = 0.5f - 0.5f * std::cos(2 * float(M_PI) * i / (size - 1));
        samples[size_t(i)] = value * window;
    }
    // an FFT in place
    for (int i = 1, j = 0; i < size; ++i) {
        int bit = size >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(samples[size_t(i)], samples[size_t(j)]);
    }
    for (int length = 2; length <= size; length <<= 1) {
        const float angle = -2 * float(M_PI) / length;
        const std::complex<float> step(std::cos(angle), std::sin(angle));
        for (int i = 0; i < size; i += length) {
            std::complex<float> w(1);
            for (int k = 0; k < length / 2; ++k) {
                const std::complex<float> u = samples[size_t(i + k)], v = samples[size_t(i + k + length / 2)] * w;
                samples[size_t(i + k)] = u + v;
                samples[size_t(i + k + length / 2)] = u - v;
                w *= step;
            }
        }
    }
    const int bands = 32;
    QList<float> levels(bands);
    for (int band = 0; band < bands; ++band) {
        // logarithmic bands over the first half
        const int from = int(std::pow(size / 2.0, double(band) / bands)), to = qMax(from + 1, int(std::pow(size / 2.0, double(band + 1) / bands)));
        float sum = 0;
        for (int i = from; i < to && i < size / 2; ++i)
            sum = qMax(sum, std::abs(samples[size_t(i)]));
        const float db = 20 * std::log10(sum / 16 + 1e-6f);
        const float level = qBound(0.f, (db + 60) / 60, 1.f);
        levels[band] = qMax(level, m_spectrum.value(band) * 0.85f);
    }
    m_spectrum = levels;
    emit videoChanged();
}
