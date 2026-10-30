// SPDX-License-Identifier: LGPL-2.1-or-later
#include "fileicons.h"

#include "programs.h"
#include "theme.h"
#include "xpfiles.h"

#include <QHash>
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
