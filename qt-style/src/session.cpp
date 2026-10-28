// SPDX-License-Identifier: LGPL-2.1-or-later
#include "session.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QtEndian>

#include <optional>

namespace w4w {
namespace {

using Values = QHash<QString, QString>;          // lower-case value name -> raw data
using Registry = QHash<QString, Values>;         // lower-case key (single backslashes) -> values

// Wine's registry file: [Key\\Path] stamp, then "Name"=data lines, hex data
// continued over lines ending in a backslash. Only HKCU's keys are wanted.
Registry parseRegistry(const QString &path)
{
    Registry out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return out;
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    Values *current = nullptr;
    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines[i];
        if (line.startsWith(QLatin1Char('['))) {
            const int end = line.indexOf(QLatin1Char(']'));
            if (end < 0)
                continue;
            QString key = line.mid(1, end - 1).toLower();
            key.replace(QLatin1String("\\\\"), QLatin1String("\\"));
            current = &out[key];
            continue;
        }
        if (!current || !line.startsWith(QLatin1Char('"')))
            continue;
        // The name, with \" and \\ escapes.
        QString name;
        int at = 1;
        for (; at < line.size() && line[at] != QLatin1Char('"'); ++at) {
            if (line[at] == QLatin1Char('\\') && at + 1 < line.size())
                ++at;
            name += line[at];
        }
        if (at + 1 >= line.size() || line[at + 1] != QLatin1Char('='))
            continue;
        QString data = line.mid(at + 2);
        while (data.endsWith(QLatin1Char('\\')) && i + 1 < lines.size()) {
            data.chop(1);
            data += lines[++i].trimmed();
        }
        current->insert(name.toLower(), data);
    }
    return out;
}

QString stringValue(const Registry &reg, const QString &key, const QString &name)
{
    QString data = reg.value(key.toLower()).value(name.toLower());
    if (data.startsWith(QLatin1String("str(")))
        data = data.mid(data.indexOf(QLatin1Char(':')) + 1);
    if (!data.startsWith(QLatin1Char('"')))
        return QString();
    QString out;
    for (int i = 1; i < data.size() && data[i] != QLatin1Char('"'); ++i) {
        if (data[i] == QLatin1Char('\\') && i + 1 < data.size()) {
            ++i;
            if (data[i] == QLatin1Char('n'))
                out += QLatin1Char('\n');
            else if (data[i] == QLatin1Char('0'))
                out += QChar(0);
            else
                out += data[i];
            continue;
        }
        out += data[i];
    }
    return out;
}

std::optional<quint32> dwordValue(const Registry &reg, const QString &key, const QString &name)
{
    const QString data = reg.value(key.toLower()).value(name.toLower());
    if (data.startsWith(QLatin1String("dword:"))) {
        bool ok = false;
        const quint32 v = data.mid(6).toUInt(&ok, 16);
        if (ok)
            return v;
    }
    const QString text = stringValue(reg, key, name);
    bool ok = false;
    const quint32 v = text.toUInt(&ok);
    return ok ? std::optional<quint32>(v) : std::nullopt;
}

QByteArray binaryValue(const Registry &reg, const QString &key, const QString &name)
{
    QString data = reg.value(key.toLower()).value(name.toLower());
    if (!data.startsWith(QLatin1String("hex")))
        return {};
    data = data.mid(data.indexOf(QLatin1Char(':')) + 1);
    QByteArray out;
    for (const QString &byte : data.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool ok = false;
        const int v = byte.trimmed().toInt(&ok, 16);
        if (!ok)
            return {};
        out.append(char(v));
    }
    return out;
}

// A LOGFONTW (92 bytes) as a Qt font, its height taken at the prefix's DPI.
std::optional<QFont> logFont(const QByteArray &data, int dpi)
{
    if (data.size() < 92)
        return std::nullopt;
    const auto *d = data.constData();
    const qint32 height = qFromLittleEndian<qint32>(d);
    const qint32 weight = qFromLittleEndian<qint32>(d + 16);
    const bool italic = d[20] != 0;
    const QString face = QString::fromUtf16(reinterpret_cast<const char16_t *>(d + 28), 32).section(QChar(0), 0, 0);
    if (face.isEmpty() || height == 0)
        return std::nullopt;
    QFont font(face);
    // Negative: the character height in pixels; positive: the cell height.
    const double pixels = height < 0 ? -height : height * 0.85;
    font.setPointSizeF(pixels * 72.0 / dpi);
    if (weight > 0)
        font.setWeight(QFont::Weight(qBound(1, int(weight), 1000)));
    font.setItalic(italic);
    return font;
}

// linux-look.conf: "name=value" lines, # comments.
void readLinuxLook(Look *look)
{
    QFile file(linuxLookPath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    for (const QByteArray &raw : file.readAll().split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        const int equals = line.indexOf(QLatin1Char('='));
        if (line.startsWith(QLatin1Char('#')) || equals < 0)
            continue;
        const QString name = line.left(equals).trimmed(), value = line.mid(equals + 1).trimmed();
        if (name == QLatin1String("style"))
            look->accurate = value.compare(QLatin1String("accurate"), Qt::CaseInsensitive) == 0;
        else if (name == QLatin1String("icon_theme"))
            look->iconTheme = value;
        else if (name == QLatin1String("theme_build"))
            look->themeBuild = value.toInt();
    }
}

} // namespace

QString linuxLookPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/linux-look.conf");
}

QString findPrefix()
{
    for (const char *variable : {"WIN4WINE_PREFIX", "WINEPREFIX"}) {
        const QString value = qEnvironmentVariable(variable);
        if (!value.isEmpty() && QFileInfo(value + QStringLiteral("/user.reg")).isFile())
            return QDir(value).absolutePath();
    }
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/win4wine/session.json");
    QFile file(config);
    if (file.open(QIODevice::ReadOnly)) {
        const QString prefix = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("prefix")).toString();
        if (!prefix.isEmpty() && QFileInfo(prefix + QStringLiteral("/user.reg")).isFile())
            return QDir(prefix).absolutePath();
    }
    const QString wine = QDir::homePath() + QStringLiteral("/.wine");
    return QFileInfo(wine + QStringLiteral("/user.reg")).isFile() ? wine : QString();
}

QString linuxPath(const QString &prefix, const QString &windowsPath)
{
    QString path = windowsPath;
    path.replace(QLatin1String("%SystemRoot%"), QLatin1String("C:\\windows"), Qt::CaseInsensitive);
    path.replace(QLatin1String("%WinDir%"), QLatin1String("C:\\windows"), Qt::CaseInsensitive);
    path.replace(QLatin1Char('/'), QLatin1Char('\\'));
    if (path.size() < 3 || path[1] != QLatin1Char(':'))
        return QString();
    // dosdevices/c: is the drive's root (a link to ../drive_c).
    QString current = prefix + QStringLiteral("/dosdevices/") + path.left(2).toLower();
    for (const QString &component : path.mid(3).split(QLatin1Char('\\'), Qt::SkipEmptyParts)) {
        const QString exact = current + QLatin1Char('/') + component;
        if (QFileInfo::exists(exact)) {
            current = exact;
            continue;
        }
        const QStringList entries = QDir(current).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
        bool found = false;
        for (const QString &entry : entries)
            if (entry.compare(component, Qt::CaseInsensitive) == 0) {
                current += QLatin1Char('/') + entry;
                found = true;
                break;
            }
        if (!found)
            return QString();
    }
    return current;
}

Look readLook(const QString &prefix)
{
    Look look;
    readLinuxLook(&look);
    if (prefix.isEmpty())
        return look;
    const Registry reg = parseRegistry(prefix + QStringLiteral("/user.reg"));
    const QString manager = QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\ThemeManager");
    const QString dll = stringValue(reg, manager, QStringLiteral("DllName"));
    const QString active = stringValue(reg, manager, QStringLiteral("ThemeActive"));
    if (qEnvironmentVariableIsSet("WIN4WINE_QT_DEBUG"))
        fprintf(stderr, "win4wine-xp: ThemeManager DllName=%s ThemeActive=%s\n", qPrintable(dll), qPrintable(active));
    if (active != QLatin1String("0") && !dll.isEmpty()) {
        look.stylePath = linuxPath(prefix, dll);
        look.themed = !look.stylePath.isEmpty();
    }
    look.colourName = stringValue(reg, manager, QStringLiteral("ColorName"));
    look.sizeName = stringValue(reg, manager, QStringLiteral("SizeName"));
    // For trying another style (qt-style/README.md): WIN4WINE_QT_MSSTYLES=PATH[:COLOR]
    if (const QString forced = qEnvironmentVariable("WIN4WINE_QT_MSSTYLES"); !forced.isEmpty()) {
        look.stylePath = forced.section(QLatin1Char(':'), 0, 0);
        look.colourName = forced.section(QLatin1Char(':'), 1, 1);
        look.themed = QFileInfo(look.stylePath).isFile();
    }

    const QString colours = QStringLiteral("Control Panel\\Colors");
    for (auto it = reg.value(colours.toLower()).cbegin(); it != reg.value(colours.toLower()).cend(); ++it) {
        const QStringList rgb = stringValue(reg, colours, it.key()).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (rgb.size() == 3)
            look.colours.insert(it.key(), QColor(rgb[0].toInt(), rgb[1].toInt(), rgb[2].toInt()));
    }

    const QString metrics = QStringLiteral("Control Panel\\Desktop\\WindowMetrics");
    look.dpi = int(dwordValue(reg, metrics, QStringLiteral("AppliedDPI"))
                       .value_or(dwordValue(reg, QStringLiteral("Control Panel\\Desktop"), QStringLiteral("LogPixels")).value_or(96)));
    if (look.dpi < 48 || look.dpi > 480)
        look.dpi = 96;
    const auto message = logFont(binaryValue(reg, metrics, QStringLiteral("MessageFont")), look.dpi);
    const auto menu = logFont(binaryValue(reg, metrics, QStringLiteral("MenuFont")), look.dpi);
    const auto status = logFont(binaryValue(reg, metrics, QStringLiteral("StatusFont")), look.dpi);
    if (message) {
        look.haveFonts = true;
        look.messageFont = *message;
        look.menuFont = menu.value_or(*message);
        look.statusFont = status.value_or(*message);
    }
    return look;
}

Session *Session::instance()
{
    static Session *session = nullptr;
    if (!session) {
        session = new Session;
        if (QCoreApplication::instance())
            session->moveToThread(QCoreApplication::instance()->thread());
    }
    return session;
}

Session::Session()
    : m_prefix(findPrefix())
{
    m_look = readLook(m_prefix);
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(400);
    connect(&m_debounce, &QTimer::timeout, this, &Session::reload);
    // Wine replaces user.reg by renaming a new file over it: watch the
    // directory as well as the file, and the style file being replaced.
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce, qOverload<>(&QTimer::start));
    watch();
}

void Session::watch()
{
    if (m_prefix.isEmpty())
        return;
    const QStringList files = m_watcher.files(), directories = m_watcher.directories();
    if (!files.isEmpty())
        m_watcher.removePaths(files);
    if (!directories.isEmpty())
        m_watcher.removePaths(directories);
    m_watcher.addPath(m_prefix);
    m_watcher.addPath(m_prefix + QStringLiteral("/user.reg"));
    // Control Panel replaces linux-look.conf by renaming a new file over it.
    const QString conf = linuxLookPath();
    QDir().mkpath(QFileInfo(conf).absolutePath());
    m_watcher.addPath(QFileInfo(conf).absolutePath());
    if (QFileInfo::exists(conf))
        m_watcher.addPath(conf);
    if (!m_look.stylePath.isEmpty())
        m_watcher.addPath(m_look.stylePath);
}

void Session::reload()
{
    Look look = readLook(m_prefix);
    // A user.reg caught while Wine writes it has no colours yet: keep the last look.
    if (look.colours.isEmpty() && !m_look.colours.isEmpty())
        return;
    watch();
    if (look == m_look)
        return;
    m_look = look;
    Q_EMIT changed();
}

} // namespace w4w
