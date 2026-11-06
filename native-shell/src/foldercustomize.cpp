// SPDX-License-Identifier: LGPL-2.1-or-later
#include "foldercustomize.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSettings>
#include <QStandardPaths>

#include <fcntl.h>
#include <sys/stat.h>

namespace foldercustomize {

const Template TEMPLATES[7] = {
    {"Documents", 29984}, {"Pictures", 29985}, {"PhotoAlbum", 29986}, {"Music", 29987},
    {"MusicArtist", 29988}, {"MusicAlbum", 29989}, {"Videos", 29990},
};

QString bagsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/win4wine/native-explorer-bags.conf");
}

QString bagKey(const QString &folder, const char *value)
{
    return QString::fromLatin1(folder.toUtf8().toBase64(QByteArray::Base64UrlEncoding)) + QLatin1Char('/') + QLatin1String(value);
}

namespace {

// The folder's own value, else the inherit bag's of the folder or of the nearest parent that has one
QString merged(const QString &folder, const char *own, const char *inherited)
{
    const QSettings bags(bagsPath(), QSettings::IniFormat);
    const QString value = bags.value(bagKey(folder, own)).toString();
    if (!value.isEmpty())
        return value;
    for (QString path = QDir::cleanPath(folder); !path.isEmpty();) {
        const QString found = bags.value(bagKey(path, inherited)).toString();
        if (!found.isEmpty())
            return found;
        const QString parent = QFileInfo(path).path();
        if (parent == path)
            break;
        path = parent;
    }
    return {};
}

// Folders' icons, read once (folder icons are asked for at every listing)
QHash<QString, Icon> &icons()
{
    static QHash<QString, Icon> map = [] {
        QHash<QString, Icon> loaded;
        QSettings bags(bagsPath(), QSettings::IniFormat);
        for (const QString &group : bags.childGroups()) {
            const QString value = bags.value(group + QStringLiteral("/icon")).toString();
            const qsizetype bar = value.lastIndexOf(QLatin1Char('|'));
            if (bar <= 0)
                continue;
            const QString folder = QString::fromUtf8(QByteArray::fromBase64(group.toLatin1(), QByteArray::Base64UrlEncoding));
            loaded.insert(folder, Icon{value.left(bar), value.mid(bar + 1)});
        }
        return loaded;
    }();
    return map;
}

void touch(const QString &folder)
{
    const timespec times[2] = {{0, UTIME_OMIT}, {0, UTIME_NOW}};
    utimensat(AT_FDCWD, QFile::encodeName(folder).constData(), times, 0);
}

} // namespace

bool customizable(const QString &folder)
{
    const QFileInfo info(folder);
    if (folder.isEmpty() || !info.isDir() || info.isRoot())
        return false;
    const QString path = info.absoluteFilePath();
    if (path == QLatin1String("/usr") || path == QLatin1String("/opt"))
        return false;
    for (const auto location : {QStandardPaths::DocumentsLocation, QStandardPaths::PicturesLocation, QStandardPaths::MusicLocation,
                                QStandardPaths::MoviesLocation})
        if (const QString special = QStandardPaths::writableLocation(location); special == path && special != QDir::homePath())
            return false;
    return true;
}

QString folderType(const QString &folder)
{
    return merged(folder, "folderType", "inherit.folderType");
}

bool typeInherited(const QString &folder)
{
    const QString inherited = QSettings(bagsPath(), QSettings::IniFormat).value(bagKey(folder, "inherit.folderType")).toString();
    return !inherited.isEmpty() && inherited.compare(folderType(folder), Qt::CaseInsensitive) == 0;
}

QString logo(const QString &folder)
{
    return merged(folder, "logo", "inherit.logo");
}

QString picture(const QString &folder, bool withLogo)
{
    const QDir dir(folder);
    if (const QString chosen = withLogo ? logo(folder) : QString(); !chosen.isEmpty()) {
        const QString path = dir.absoluteFilePath(chosen);
        if (QFileInfo::exists(path))
            return path;
    }
    // folder.jpg, then folder.gif (the table at 0x7ca396c4), in any case as on XP's file systems
    for (const QString name : {QStringLiteral("folder.jpg"), QStringLiteral("folder.gif")}) {
        const QStringList found = dir.entryList({name}, QDir::Files);
        if (!found.isEmpty())
            return dir.absoluteFilePath(found.first());
    }
    return {};
}

void apply(const QString &folder, const QString &type, const QString &logo, bool subfolders)
{
    QSettings bags(bagsPath(), QSettings::IniFormat);
    const char *typeKey = subfolders ? "inherit.folderType" : "folderType";
    const char *logoKey = subfolders ? "inherit.logo" : "logo";
    if (subfolders)
        for (const char *value : {"folderType", "logo", "view"})
            bags.remove(bagKey(folder, value));
    if (bags.value(bagKey(folder, typeKey)).toString().compare(type, Qt::CaseInsensitive) != 0) {
        bags.remove(bagKey(folder, "view"));
        bags.setValue(bagKey(folder, typeKey), type);
    }
    if (bags.value(bagKey(folder, logoKey)).toString().compare(logo, Qt::CaseInsensitive) != 0) {
        if (logo.isEmpty())
            bags.remove(bagKey(folder, logoKey));
        else
            bags.setValue(bagKey(folder, logoKey), logo);
        touch(folder);
    }
    bags.sync();
}

std::optional<Icon> icon(const QString &folder)
{
    const auto found = icons().constFind(QDir::cleanPath(folder));
    if (found == icons().cend())
        return std::nullopt;
    return *found;
}

void setIcon(const QString &folder, const std::optional<Icon> &icon)
{
    const QString path = QDir::cleanPath(folder);
    QSettings bags(bagsPath(), QSettings::IniFormat);
    if (icon) {
        bags.setValue(bagKey(path, "icon"), icon->file + QLatin1Char('|') + icon->group);
        icons().insert(path, *icon);
    } else {
        bags.remove(bagKey(path, "icon"));
        icons().remove(path);
    }
    bags.sync();
}

} // namespace foldercustomize
