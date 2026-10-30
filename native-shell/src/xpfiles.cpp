// SPDX-License-Identifier: LGPL-2.1-or-later
#include "xpfiles.h"

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
    // .local/native-shell/bin/xp-native-shell
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
    for (const char *name : {"tahoma.ttf", "tahomabd.ttf", "framd.ttf", "framdit.ttf", "trebuc.ttf", "trebucbd.ttf", "marlett.ttf"})
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

QImage icon(const w4w::PeResources &pe, int group, int size)
{
    // GRPICONDIR: 6 bytes, then 14 for each image; a RT_ICON is what an .ico
    // file holds, only its directory says otherwise: its offset for the ID.
    const QByteArray directory = pe.find(QStringLiteral("14"), QString::number(group));
    if (directory.size() < 6)
        return {};
    const int count = qFromLittleEndian<quint16>(directory.constData() + 4);
    int best = -1, bestScore = -1;
    for (int i = 0; i < count && 6 + 14 * (i + 1) <= directory.size(); ++i) {
        const char *entry = directory.constData() + 6 + 14 * i;
        const int width = uchar(entry[0]) ? uchar(entry[0]) : 256;
        const int bits = qFromLittleEndian<quint16>(entry + 6);
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
    const QStringList parts = spec.split(QLatin1Char(','));
    QFont font(parts.value(0).trimmed());
    const int pixels = qMax(1, qRound(parts.value(1).trimmed().toDouble() * dpi / 72.0));
    font.setPixelSize(pixels);
    for (int i = 2; i < parts.size(); ++i) {
        const QString style = parts[i].trimmed().toLower();
        if (style == QLatin1String("bold"))
            font.setBold(true);
        else if (style == QLatin1String("italic"))
            font.setItalic(true);
    }
    // Hinted as Windows hints it at any size; XP's Standard smoothing only smooths above 16 pixels.
    font.setHintingPreference(QFont::PreferFullHinting);
    font.setStyleStrategy(pixels <= 16 ? QFont::NoAntialias : QFont::StyleStrategy(QFont::PreferAntialias | QFont::NoSubpixelAntialias));
    return font;
}

} // namespace xpfiles
