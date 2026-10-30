// SPDX-License-Identifier: LGPL-2.1-or-later
// The IPC directory's files (docs/architecture.md): each appears whole by its rename.
#pragma once

#include <QDir>
#include <QFile>
#include <QImage>
#include <QtEndian>

namespace ipcfile {

inline QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

inline void write(const QDir &directory, const QString &name, const QByteArray &data)
{
    const QString temporary = directory.filePath(name + QStringLiteral(".tmp"));
    QFile file(temporary);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(data);
    file.close();
    QFile::remove(directory.filePath(name));
    QFile::rename(temporary, directory.filePath(name));
}

// bridge-wm's W4I1: its magic, width and height, then straight ARGB.
inline QImage readIcon(const QString &path)
{
    const QByteArray data = read(path);
    if (data.size() < 12 || !data.startsWith("W4I1"))
        return {};
    const int width = qFromLittleEndian<qint32>(data.constData() + 4);
    const int height = qFromLittleEndian<qint32>(data.constData() + 8);
    if (width <= 0 || height <= 0 || width > 1024 || height > 1024 || data.size() < 12 + 4 * qsizetype(width) * height)
        return {};
    return QImage(reinterpret_cast<const uchar *>(data.constData()) + 12, width, height, QImage::Format_ARGB32).copy();
}

} // namespace ipcfile
