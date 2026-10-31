// SPDX-License-Identifier: LGPL-2.1-or-later
#include "fileicons.h"

#include "programs.h"
#include "theme.h"
#include "xpfiles.h"

#include <algorithm>

#include <QHash>
#include <QFileInfo>
#include <QLocale>
#include <QMimeDatabase>
#include <QPixmap>
#include <QStandardPaths>

namespace fileicons {

QIcon shell(int id)
{
    static QHash<int, QIcon> cache;
    auto it = cache.constFind(id);
    if (it != cache.cend())
        return *it;
    QIcon icon;
    for (int size : {16, 24, 32, 48}) {
        const QImage image = ShellTheme::instance()->shellIcon(id, size);
        if (!image.isNull())
            icon.addPixmap(QPixmap::fromImage(image));
    }
    cache.insert(id, icon);
    return icon;
}

static int placeIcon(const QString &path)
{
    static const QHash<QString, int> places = {
        {QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), Documents},
        {QStandardPaths::writableLocation(QStandardPaths::PicturesLocation), Pictures},
        {QStandardPaths::writableLocation(QStandardPaths::MusicLocation), Music},
    };
    return places.value(path, Folder);
}

// XP's DefaultIcon of a file type, as Windows reads "file,N": N from 0 is the Nth icon group (named ones
// first, then the numbers in order), -N the group numbered N
static QIcon defaultIcon(const QString &fileName, int reference)
{
    static QHash<QString, QIcon> cache;
    const QString key = fileName + QLatin1Char(',') + QString::number(reference);
    auto it = cache.constFind(key);
    if (it != cache.cend())
        return *it;
    QIcon icon;
    if (const auto pe = xpfiles::resources(xpfiles::find(fileName))) {
        QString group = QString::number(-reference);
        if (reference >= 0) {
            QStringList named, numbered;
            for (const QString &name : pe->names(QStringLiteral("14"))) {
                bool number = false;
                name.toInt(&number);
                (number ? numbered : named).append(name);
            }
            named.sort(Qt::CaseInsensitive);
            std::sort(numbered.begin(), numbered.end(), [](const QString &a, const QString &b) { return a.toInt() < b.toInt(); });
            const QStringList all = named + numbered;
            group = all.value(reference);
        }
        for (int size : {16, 24, 32, 48}) {
            const QImage image = xpfiles::icon(*pe, group, ShellTheme::instance()->scaled(size));
            if (!image.isNull())
                icon.addPixmap(QPixmap::fromImage(image));
        }
    }
    cache.insert(key, icon);
    return icon;
}

// The DefaultIcon of each file type's class in XP's registry (its setup's INFs), by extension
struct TypeIcon
{
    const char *file;
    int reference;
};
static const QHash<QString, TypeIcon> &typeIcons()
{
    static const QHash<QString, TypeIcon> icons = [] {
        QHash<QString, TypeIcon> map;
        const auto add = [&](std::initializer_list<const char *> extensions, TypeIcon icon) {
            for (const char *extension : extensions)
                map.insert(QLatin1String(extension), icon);
        };
        add({"jpg", "jpeg", "jpe", "jfif"}, {"shimgvw.dll", 3});        // jpegfile
        add({"png", "gif", "emf", "wmf"}, {"shimgvw.dll", 2});          // pngfile, giffile, emffile, wmffile
        add({"bmp", "dib"}, {"shimgvw.dll", 1});                        // Paint.Picture
        add({"tif", "tiff"}, {"shimgvw.dll", 4});                       // TIFImage.Document
        add({"txt", "log"}, {"shell32.dll", -152});                     // txtfile
        add({"ini", "inf"}, {"shell32.dll", -151});                     // inifile, inffile
        add({"bat", "cmd"}, {"shell32.dll", -153});                     // batfile, cmdfile
        add({"dll", "sys"}, {"shell32.dll", -154});                     // dllfile
        add({"fon"}, {"shell32.dll", -155});                            // fonfile
        add({"hlp"}, {"shell32.dll", 23});                              // hlpfile
        add({"zip"}, {"zipfldr.dll", 0});                               // CompressedFolder
        add({"msi"}, {"msiexec.exe", 0});                               // Msi.Package
        // mp3file, soundrec, WMAFile, avifile, mpegfile, WMVFile, midfile: Windows Media Player's
        add({"mp3", "wav", "wma", "avi", "mpg", "mpeg", "wmv", "asf", "mid", "midi", "rmi", "m3u", "au", "aif", "aiff", "snd"},
            {"wmplayer.exe", -120});
        return map;
    }();
    return icons;
}

QIcon forFile(const QFileInfo &file)
{
    if (file.isDir()) {
        if (file.isRoot())
            return shell(Drive);
        return shell(placeIcon(file.absoluteFilePath()));
    }
    if (file.suffix() == QLatin1String("desktop")) {
        if (const auto program = programs::fromFile(file.absoluteFilePath())) {
            const QIcon icon = programs::iconOf(*program);
            if (!icon.isNull())
                return icon;
        }
    }
    if (const auto type = typeIcons().constFind(file.suffix().toLower()); type != typeIcons().cend())
        if (const QIcon icon = defaultIcon(QLatin1String(type->file), type->reference); !icon.isNull())
            return icon;
    static QMimeDatabase mimes;
    const QMimeType mime = mimes.mimeTypeForFile(file, QMimeDatabase::MatchExtension);
    static QHash<QString, QIcon> cache;
    auto it = cache.constFind(mime.name());
    if (it != cache.cend())
        return *it;
    QIcon icon = QIcon::fromTheme(mime.iconName());
    if (icon.isNull())
        icon = QIcon::fromTheme(mime.genericIconName());
    if (icon.isNull())
        icon = shell(Document);
    cache.insert(mime.name(), icon);
    return icon;
}

QImage image(const QFileInfo &file, int size)
{
    return forFile(file).pixmap(size, size).toImage();
}

QString typeName(const QFileInfo &file)
{
    if (file.isDir())
        return QObject::tr("File Folder");
    static QMimeDatabase mimes;
    const QMimeType mime = mimes.mimeTypeForFile(file, QMimeDatabase::MatchExtension);
    return mime.comment().isEmpty() ? QObject::tr("File") : mime.comment();
}

QString sizeText(qint64 bytes)
{
    // XP's Size column is always in KB, rounded up.
    return QObject::tr("%1 KB").arg(QLocale().toString((bytes + 1023) / 1024));
}

} // namespace fileicons
