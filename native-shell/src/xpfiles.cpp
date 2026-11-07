// SPDX-License-Identifier: LGPL-2.1-or-later
#include "xpfiles.h"
#include "fontpolicy.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QFontDatabase>
#include <QHash>
#include <QtEndian>

namespace xpfiles {

static QStringList roots()
{
    QStringList out;
    if (const QString assets = qEnvironmentVariable("WIN4WINE_ASSETS"); !assets.isEmpty())
        out << assets;
    // .local/native-shell/bin/explorer
    const QString local = QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../.."));
    out << local + QStringLiteral("/native-assets") << local + QStringLiteral("/assets");
    return out;
}

QString find(const QString &name)
{
    static QHash<QString, QString> files;
    if (files.isEmpty())
        for (const QString &root : roots()) {
            QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                const QString key = it.fileName().toLower();
                if (!files.contains(key))
                    files.insert(key, it.filePath());
            }
        }
    return files.value(name.toLower());
}

void loadFonts()
{
    for (const char *name : {"tahoma.ttf", "tahomabd.ttf", "framd.ttf", "framdit.ttf", "trebuc.ttf", "trebucbd.ttf", "marlett.ttf",
                             "arial.ttf", "arialbd.ttf", "ariali.ttf", "arialbi.ttf", "micross.ttf", "msgothic.ttc", "lucon.ttf", "verdanab.ttf"})
        if (const QString path = find(QLatin1String(name)); !path.isEmpty())
            QFontDatabase::addApplicationFont(path);
}

std::optional<w4w::PeResources> resources(const QString &path)
{
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return w4w::PeResources::parse(file.readAll());
}

QString string(const w4w::PeResources &pe, int id)
{
    const QByteArray block = pe.find(QStringLiteral("6"), QString::number(id / 16 + 1));
    qsizetype at = 0;
    for (int i = 0; at + 2 <= block.size(); ++i) {
        const int length = qFromLittleEndian<quint16>(block.constData() + at);
        at += 2;
        if (at + 2 * qsizetype(length) > block.size())
            break;
        if (i == id % 16)
            return QString::fromUtf16(reinterpret_cast<const char16_t *>(block.constData() + at), length);
        at += 2 * qsizetype(length);
    }
    return QString();
}

QString fileDescription(const w4w::PeResources &pe)
{
    // VS_VERSIONINFO's String: its key, then its value, each in UTF-16 and DWORD aligned
    const QByteArray info = pe.find(QStringLiteral("16"), QStringLiteral("1"));
    static const QByteArray key = QByteArray(reinterpret_cast<const char *>(u"FileDescription"), 2 * 16);
    qsizetype at = info.indexOf(key);
    if (at < 0)
        return QString();
    at = (at + key.size() + 3) & ~qsizetype(3);
    QString out;
    for (; at + 2 <= info.size(); at += 2) {
        const char16_t c = qFromLittleEndian<quint16>(info.constData() + at);
        if (!c)
            break;
        out += QChar(c);
    }
    return out.trimmed();
}

QString shellString(int id, const char *fallback)
{
    static const auto resources = xpfiles::resources(find(QStringLiteral("shell32.dll")));
    const QString text = resources ? string(*resources, id) : QString();
    return text.isEmpty() ? QLatin1String(fallback) : text;
}

QString explorerString(int id, const char *fallback)
{
    static const auto resources = xpfiles::resources(find(QStringLiteral("explorer.exe")));
    const QString text = resources ? string(*resources, id) : QString();
    return text.isEmpty() ? QLatin1String(fallback) : text;
}

QImage bitmap(const w4w::PeResources &pe, int id)
{
    bool partial = false;
    return w4w::decodeDib(pe.find(QStringLiteral("2"), QString::number(id)), &partial);
}

QImage icon(const w4w::PeResources &pe, int group, int size, int maxBits)
{
    return icon(pe, QString::number(group), size, maxBits);
}

QImage icon(const w4w::PeResources &pe, const QString &group, int size, int maxBits)
{
    // GRPICONDIR: 6 bytes, then 14 for each image; a RT_ICON is what an .ico
    // file holds, only its directory says otherwise: its offset for the ID.
    const QByteArray directory = pe.find(QStringLiteral("14"), group);
    if (directory.size() < 6)
        return {};
    const int count = qFromLittleEndian<quint16>(directory.constData() + 4);
    int best = -1, bestScore = -1;
    for (int i = 0; i < count && 6 + 14 * (i + 1) <= directory.size(); ++i) {
        const char *entry = directory.constData() + 6 + 14 * i;
        const int width = uchar(entry[0]) ? uchar(entry[0]) : 256;
        const int bits = qFromLittleEndian<quint16>(entry + 6);
        if (bits > maxBits)
            continue;
        const int score = (width == size ? 1000 : 500 - qAbs(width - size)) * 64 + bits;
        if (score > bestScore) {
            bestScore = score;
            best = i;
        }
    }
    if (best < 0)
        return {};
    const char *entry = directory.constData() + 6 + 14 * best;
    const QByteArray image = pe.find(QStringLiteral("3"), QString::number(qFromLittleEndian<quint16>(entry + 12)));
    if (image.isEmpty())
        return {};
    QByteArray file;
    file.append(directory.constData(), 4);
    const char one[2] = {1, 0};
    file.append(one, 2);
    file.append(entry, 12);
    char offset[4];
    qToLittleEndian<quint32>(22, offset);
    file.append(offset, 4);
    file.append(image);
    return QImage::fromData(file, "ICO");
}

QFont themeFont(const QString &spec, int dpi)
{
    return w4w::themeFont(spec, dpi);
}

} // namespace xpfiles
