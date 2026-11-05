// SPDX-License-Identifier: LGPL-2.1-or-later
#include "fileops.h"

#include "sounds.h"

#include "fileicons.h"
#include "xpdialog.h"
#include "xpfiles.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QtEndian>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <atomic>
#include <memory>

namespace fileops {
namespace {

const qint64 CHUNK = 1 << 20;

// The transfer's work, off the GUI thread. A blocking signal asks about files of the same name.
class Worker : public QThread
{
    Q_OBJECT
public:
    enum Answer { Yes, YesToAll, No, Cancel };
    Worker(QStringList sources, QString target, Action action, QObject *parent)
        : QThread(parent)
        , m_sources(std::move(sources))
        , m_target(std::move(target))
        , m_action(action)
    {
    }
    std::atomic<bool> cancelled{false};
    QString error;

Q_SIGNALS:
    void fileStarted(const QString &name, const QString &from);
    void progressed(qint64 done, qint64 total);
    void askReplace(const QString &path, int *answer);

protected:
    void run() override
    {
        qint64 total = 0;
        for (const QString &source : std::as_const(m_sources))
            total += sizeOf(source);
        m_total = qMax<qint64>(1, total);
        for (const QString &source : std::as_const(m_sources)) {
            if (cancelled)
                return;
            const QString target = QDir(m_target).filePath(QFileInfo(source).fileName());
            if (QFileInfo(target).absoluteFilePath() == QFileInfo(source).absoluteFilePath()) {
                if (m_action == Action::Copy)
                    copyItem(source, QDir(m_target).filePath(freeName(m_target, QFileInfo(source).fileName())));
                continue;
            }
            if (target.startsWith(QFileInfo(source).absoluteFilePath() + QLatin1Char('/'))) {
                error = QObject::tr("Cannot put '%1' into itself.").arg(QFileInfo(source).fileName());
                return;
            }
            if (m_action == Action::Move && !QFileInfo::exists(target) && QFile::rename(source, target)) {
                m_done += sizeOf(target);
                report();
                continue;
            }
            copyItem(source, target);
            if (m_action == Action::Move && !cancelled && error.isEmpty()) {
                QFileInfo info(source);
                if (info.isDir() && !info.isSymLink())
                    QDir(source).removeRecursively();
                else
                    QFile::remove(source);
            }
        }
    }

private:
    static qint64 sizeOf(const QString &path)
    {
        QFileInfo info(path);
        if (!info.isDir() || info.isSymLink())
            return info.size();
        qint64 size = 0;
        QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            size += it.fileInfo().size();
        }
        return size;
    }

    void report()
    {
        Q_EMIT progressed(m_done, m_total);
    }

    void copyItem(const QString &source, const QString &target)
    {
        if (cancelled || !error.isEmpty())
            return;
        QFileInfo info(source);
        if (info.isSymLink()) {
            QFile::remove(target);
            QFile::link(info.symLinkTarget(), target);
            return;
        }
        if (info.isDir()) {
            QDir().mkpath(target);
            const QFileInfoList children = QDir(source).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
            for (const QFileInfo &child : children)
                copyItem(child.absoluteFilePath(), QDir(target).filePath(child.fileName()));
            return;
        }
        if (QFileInfo::exists(target) && !m_replaceAll) {
            int answer = Cancel;
            Q_EMIT askReplace(target, &answer);
            if (answer == Cancel) {
                cancelled = true;
                return;
            }
            if (answer == No)
                return;
            m_replaceAll = answer == YesToAll;
        }
        Q_EMIT fileStarted(info.fileName(), info.absolutePath());
        QFile in(source), out(target + QStringLiteral(".xp-part"));
        if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly)) {
            error = QObject::tr("Cannot copy %1: %2").arg(info.fileName(), in.isOpen() ? out.errorString() : in.errorString());
            return;
        }
        QByteArray buffer;
        while (!in.atEnd()) {
            if (cancelled) {
                out.remove();
                return;
            }
            buffer = in.read(CHUNK);
            if (out.write(buffer) != buffer.size()) {
                error = QObject::tr("Cannot copy %1: %2").arg(info.fileName(), out.errorString());
                out.remove();
                return;
            }
            m_done += buffer.size();
            report();
        }
        out.close();
        out.setPermissions(in.permissions());
        QFile::remove(target);
        if (!QFile::rename(out.fileName(), target))
            error = QObject::tr("Cannot copy %1.").arg(info.fileName());
    }

    QStringList m_sources;
    QString m_target;
    Action m_action;
    qint64 m_total = 1, m_done = 0;
    bool m_replaceAll = false;
};

QString quotedList(const QStringList &paths)
{
    if (paths.size() == 1)
        return QStringLiteral("'%1'").arg(QFileInfo(paths.first()).fileName());
    return QObject::tr("these %1 items").arg(paths.size());
}

} // namespace

QString shortcutTarget(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    const QByteArray data = file.readAll();
    if (data.size() < 76 || qFromLittleEndian<quint32>(data.constData()) != 0x4C)
        return QString();
    const auto u16 = [&](qsizetype at) { return at + 2 <= data.size() ? qFromLittleEndian<quint16>(data.constData() + at) : quint16(0); };
    const auto u32 = [&](qsizetype at) { return at + 4 <= data.size() ? qFromLittleEndian<quint32>(data.constData() + at) : quint32(0); };
    const quint32 flags = u32(20);
    qsizetype at = 76;
    if (flags & 0x1)   // LinkTargetIDList
        at += 2 + qsizetype(u16(at));
    QString target;
    if ((flags & 0x2) && at + 28 <= data.size()) {   // LinkInfo
        const qsizetype start = at, size = qsizetype(u32(at));
        const quint32 infoFlags = u32(at + 8);
        const qsizetype baseOffset = qsizetype(u32(at + 16)), suffixOffset = qsizetype(u32(at + 24));
        if ((infoFlags & 1) && baseOffset > 0) {
            const auto ansi = [&](qsizetype offset) {
                if (offset <= 0 || start + offset >= data.size())
                    return QString();
                qsizetype end = data.indexOf('\0', start + offset);
                if (end < 0)
                    end = data.size();
                return QString::fromLocal8Bit(data.mid(start + offset, end - start - offset));
            };
            target = ansi(baseOffset);
            if (suffixOffset > 0)
                target += ansi(suffixOffset);
        }
        at = start + (size > 0 ? size : 0);
    }
    // StringData: NAME_STRING (0x4), RELATIVE_PATH (0x8)
    const auto stringData = [&] {
        if (at + 2 > data.size())
            return QString();
        const qsizetype count = qsizetype(u16(at));
        at += 2;
        if (count == 0 || at + count * 2 > data.size())
            return QString();
        const QString value = QString::fromUtf16(reinterpret_cast<const char16_t *>(data.constData() + at), count);
        at += count * 2;
        return value;
    };
    if (flags & 0x4)
        stringData();
    if ((flags & 0x8) && target.isEmpty())
        target = stringData();
    // A link made on Windows names a drive ("C:\Program Files\…"): the Wine prefix's dosdevices maps
    // the letter, and without a prefix the target cannot be found
    target.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (target.size() >= 2 && target.at(1) == QLatin1Char(':') && target.at(0).isLetter()) {
        QString prefix = qEnvironmentVariable("WINEPREFIX");
        if (prefix.isEmpty())
            prefix = QDir::homePath() + QStringLiteral("/.wine");
        const QString drive = prefix + QStringLiteral("/dosdevices/") + target.left(2).toLower();
        if (!QFileInfo::exists(drive))
            return QString();
        return QFileInfo(drive).canonicalFilePath() + target.mid(2);
    }
    if (!target.isEmpty() && QDir::isRelativePath(target))
        target = QFileInfo(path).absoluteDir().filePath(target);
    return target;
}

bool createShortcut(const QString &target, const QString &linkPath)
{
    const QFileInfo info(target);
    if (target.isEmpty())
        return false;
    QByteArray base = QFile::encodeName(info.absoluteFilePath());
    if (base.isEmpty())
        return false;
    QByteArray link(76, '\0');
    const auto put32 = [&link](qsizetype at, quint32 value) { qToLittleEndian(value, link.data() + at); };
    put32(0, 0x4C);   // HeaderSize
    // LinkCLSID {00021401-0000-0000-C000-000000000046}
    const QByteArray clsid = QByteArray::fromHex("0114020000000000c000000000000046");
    link.replace(4, 16, clsid);
    put32(20, 0x82);  // HasLinkInfo | IsUnicode
    put32(24, info.isDir() ? 0x10 : 0x20);   // FILE_ATTRIBUTE_DIRECTORY / ARCHIVE
    put32(56, 0);     // IconIndex
    put32(60, 1);     // ShowCommand: SW_SHOWNORMAL
    put32(52, info.isDir() ? 0 : quint32(info.size()));
    // LinkInfo: VolumeID (a fixed drive, empty label), the local base path and an empty suffix
    QByteArray linkInfo(28, '\0');
    const quint32 volumeSize = 17, baseOffset = 28 + volumeSize, suffixOffset = baseOffset + base.size() + 1;
    const auto put = [&linkInfo](qsizetype at, quint32 value) { qToLittleEndian(value, linkInfo.data() + at); };
    put(0, suffixOffset + 1);   // size
    put(4, 0x1C);               // HeaderSize
    put(8, 1);                  // VolumeIDAndLocalBasePath
    put(12, 0x1C);              // VolumeIDOffset
    put(16, baseOffset);        // LocalBasePathOffset
    put(20, 0);                 // CommonNetworkRelativeLinkOffset
    put(24, suffixOffset);      // CommonPathSuffixOffset
    QByteArray volume(17, '\0');
    qToLittleEndian(volumeSize, volume.data());
    qToLittleEndian(quint32(3), volume.data() + 4);   // DRIVE_FIXED
    qToLittleEndian(quint32(0x10), volume.data() + 12);   // VolumeLabelOffset
    linkInfo += volume;
    linkInfo += base;
    linkInfo.append('\0');
    linkInfo.append('\0');
    link += linkInfo;
    QFile file(linkPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(link) == link.size();
}

QString freeName(const QString &folder, const QString &base)
{
    const QFileInfo parts(base);
    const QString stem = parts.isDir() ? base : parts.completeBaseName();
    const QString suffix = parts.suffix().isEmpty() ? QString() : QLatin1Char('.') + parts.suffix();
    QString name = base;
    for (int i = 2; QFileInfo::exists(QDir(folder).filePath(name)); ++i)
        name = QStringLiteral("%1 (%2)%3").arg(stem).arg(i).arg(suffix);
    return name;
}

namespace {

// shell32's copy engine (its dialog thread sub_300afcf5, the dialog's procedure sub_300ae487)
const int DIALOG_PROGRESS = 1020, ID_CANCEL = 2, ID_ANIMATION = 106, ID_FILE = 102, ID_FROM = 103, ID_BAR = 104, ID_TIME = 105;
const int STRING_MOVING = 5953, STRING_COPYING = 5954, STRING_FROM_TO = 5968, STRING_MINUTES = 8739, STRING_SECONDS = 8740;
const int AVI_MOVE = 160, AVI_COPY = 161;
// the dialog comes after a second, if the work is not done by then, and stays at least a second;
// the time left is worked out every 2 seconds
const int SHOW_DELAY = 1000, LEAST_SHOWN = 1000, ESTIMATE_INTERVAL = 2000;
// the work in blocks of 32 KB, as sub_300a9b1e counts the bytes
const qint64 BLOCK = 0x8000;

// sub_300ab727 and sub_300aa71e: the blocks done since the last look, per second, evened with the
// rate before ((new + 2 old) / 3); then the time left, shown from 5 seconds, in minutes past one
// (rounded up) or in seconds rounded up to 5, and nothing past 4 hours.
class Estimate
{
public:
    QString update(qint64 done, qint64 total)
    {
        const qint64 now = m_clock.isValid() ? m_clock.elapsed() : 0;
        if (!m_clock.isValid())
            m_clock.start();
        QString text;
        const qint64 moved = done - m_lastDone, elapsed = now - m_lastTime;
        if (moved && done > 0 && elapsed) {
            const qint64 tenths = qMax<qint64>(1, elapsed / 100);
            qint64 rate = qMax<qint64>(1, moved * 10 / tenths);
            if (elapsed < 1000)
                rate = m_rate;
            if (m_rate)
                rate = (rate + 2 * m_rate) / 3;
            if (rate) {
                m_rate = rate;
                const qint64 seconds = (total - done) / rate + 1;
                if (seconds >= 5)
                    text = timeLeft(seconds);
            }
        }
        m_lastTime = now;
        m_lastDone = done;
        return text;
    }
    bool started() const { return m_clock.isValid(); }

private:
    static QString timeLeft(qint64 seconds)
    {
        if (seconds > 14400)
            return QString();
        if (seconds > 60)
            return xpfiles::shellString(STRING_MINUTES, "%d Minutes Remaining").replace(QLatin1String("%d"), QString::number(seconds / 60 + 1));
        return xpfiles::shellString(STRING_SECONDS, "%d Seconds Remaining").replace(QLatin1String("%d"), QString::number((seconds + 4) / 5 * 5));
    }

    QElapsedTimer m_clock;
    qint64 m_lastTime = 0, m_lastDone = 0, m_rate = 0;
};

} // namespace

void transfer(const QStringList &sources, const QString &targetFolder, Action action, QWidget *parent)
{
    if (sources.isEmpty())
        return;
    auto *window = new QDialog(nullptr);
    window->setAttribute(Qt::WA_DeleteOnClose);
    const bool copy = action == Action::Copy;
    window->setWindowTitle(xpfiles::shellString(copy ? STRING_COPYING : STRING_MOVING, copy ? "Copying..." : "Moving..."));
    QLabel *name = nullptr, *from = nullptr, *time = nullptr;
    QProgressBar *bar = nullptr;
    QPushButton *cancel = nullptr;
    const std::optional<w4w::PeResources> shell32 = xpfiles::resources(xpfiles::find(QStringLiteral("shell32.dll")));
    if (shell32) {
        const xpdialog::Page page = xpdialog::build(*shell32, DIALOG_PROGRESS, window);
        name = qobject_cast<QLabel *>(page.controls.value(ID_FILE));
        from = qobject_cast<QLabel *>(page.controls.value(ID_FROM));
        time = qobject_cast<QLabel *>(page.controls.value(ID_TIME));
        bar = qobject_cast<QProgressBar *>(page.controls.value(ID_BAR));
        cancel = qobject_cast<QPushButton *>(page.controls.value(ID_CANCEL));
        // on WM_SHOWWINDOW: the move's animation (160) or the copy's (161)
        if (auto *animation = dynamic_cast<xpdialog::Animation *>(page.controls.value(ID_ANIMATION)))
            animation->open(*shell32, copy ? AVI_COPY : AVI_MOVE);
    }
    if (!name || !from || !bar || !cancel) {
        // without the user's shell32: the same lines in a window of their own
        qDeleteAll(window->findChildren<QWidget *>(Qt::FindDirectChildrenOnly));
        window->setFixedSize(370, 150);
        auto *layout = new QVBoxLayout(window);
        name = new QLabel(window);
        from = new QLabel(window);
        time = new QLabel(window);
        bar = new QProgressBar(window);
        bar->setTextVisible(false);
        cancel = new QPushButton(QObject::tr("Cancel"), window);
        layout->addWidget(name);
        layout->addWidget(from);
        layout->addWidget(bar);
        layout->addWidget(time);
        layout->addWidget(cancel, 0, Qt::AlignRight);
    }
    bar->setRange(0, 100);
    auto *worker = new Worker(sources, targetFolder, action, window);
    QObject::connect(worker, &Worker::fileStarted, window, [name, from, targetFolder](const QString &file, const QString &folder) {
        name->setText(file);
        from->setText(xpfiles::shellString(STRING_FROM_TO, "From '%1!ls!' to '%2!ls!'")
                          .replace(QLatin1String("%1!ls!"), QFileInfo(folder).fileName())
                          .replace(QLatin1String("%2!ls!"), QFileInfo(targetFolder).fileName()));
    });
    auto estimate = std::make_shared<Estimate>();
    auto counts = std::make_shared<QPair<qint64, qint64>>(0, 1);
    QObject::connect(worker, &Worker::progressed, window, [bar, counts](qint64 done, qint64 total) {
        *counts = {done / BLOCK, qMax<qint64>(1, total / BLOCK)};
        bar->setValue(int(done * 100 / qMax<qint64>(1, total)));
    });
    auto *estimates = new QTimer(window);
    QObject::connect(estimates, &QTimer::timeout, window, [time, estimate, counts] {
        if (time)
            time->setText(estimate->update(counts->first, counts->second));
    });
    auto shown = std::make_shared<QElapsedTimer>();
    auto showNow = [window, worker, shown, estimates, estimate, counts, time] {
        if (shown->isValid() || worker->isFinished())
            return;
        shown->start();
        window->show();
        if (time)
            time->setText(estimate->update(counts->first, counts->second));
        estimates->start(ESTIMATE_INTERVAL);
    };
    auto *showing = new QTimer(window);
    showing->setSingleShot(true);
    QObject::connect(showing, &QTimer::timeout, window, showNow);
    // a replace question can come before the dialog: it is the dialog's own, so the dialog comes too
    QObject::connect(worker, &Worker::askReplace, window, showNow);
    QObject::connect(worker, &Worker::askReplace, window, [window](const QString &path, int *answer) {
        QMessageBox box(QMessageBox::Question, QObject::tr("Confirm File Replace"),
                        QObject::tr("This folder already contains a file named '%1'.\n\nWould you like to replace the existing file?").arg(QFileInfo(path).fileName()),
                        QMessageBox::NoButton, window);
        QPushButton *yes = box.addButton(QMessageBox::Yes);
        QPushButton *all = box.addButton(QMessageBox::YesToAll);
        QPushButton *no = box.addButton(QMessageBox::No);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        *answer = box.clickedButton() == yes ? Worker::Yes : box.clickedButton() == all ? Worker::YesToAll : box.clickedButton() == no ? Worker::No : Worker::Cancel;
    }, Qt::BlockingQueuedConnection);
    QObject::connect(cancel, &QPushButton::clicked, window, [worker] { worker->cancelled = true; });
    QObject::connect(worker, &QThread::finished, window, [window, worker, parent, shown] {
        if (!worker->error.isEmpty())
            QMessageBox::warning(parent, window->windowTitle(), worker->error);
        const qint64 left = shown->isValid() ? LEAST_SHOWN - shown->elapsed() : 0;
        QTimer::singleShot(qMax<qint64>(0, left), window, &QWidget::close);
    });
    showing->start(SHOW_DELAY);
    worker->start();
}

void remove(const QStringList &paths, bool permanent, QWidget *parent)
{
    if (paths.isEmpty())
        return;
    const QString title = paths.size() == 1 ? QObject::tr("Confirm File Delete") : QObject::tr("Confirm Multiple File Delete");
    const QString question = permanent ? QObject::tr("Are you sure you want to delete %1?").arg(quotedList(paths))
                                       : QObject::tr("Are you sure you want to send %1 to the Recycle Bin?").arg(quotedList(paths));
    // Recycle Bin Properties' "Display delete confirmation dialog"
    QSettings settings(QStringLiteral("win4wine"), QStringLiteral("fileops"));
    if (settings.value(QStringLiteral("RecycleBin/ConfirmDelete"), true).toBool()
        && QMessageBox::question(parent, title, question) != QMessageBox::Yes)
        return;
    QStringList failed;
    for (const QString &path : paths) {
        bool done;
        if (!permanent)
            done = QFile::moveToTrash(path);
        else if (QFileInfo(path).isDir() && !QFileInfo(path).isSymLink())
            done = QDir(path).removeRecursively();
        else
            done = QFile::remove(path);
        if (!done)
            failed << QFileInfo(path).fileName();
    }
    if (!failed.isEmpty())
        QMessageBox::warning(parent, title, QObject::tr("Cannot delete %1.").arg(failed.join(QStringLiteral(", "))));
}

void copyToClipboard(const QStringList &paths, bool cut)
{
    auto *data = new QMimeData;
    QList<QUrl> urls;
    QByteArray gnome = cut ? "cut" : "copy";
    for (const QString &path : paths) {
        urls << QUrl::fromLocalFile(path);
        gnome += '\n' + QUrl::fromLocalFile(path).toEncoded();
    }
    data->setUrls(urls);
    // What Dolphin and Nautilus understand as a cut
    data->setData(QStringLiteral("application/x-kde-cutselection"), cut ? "1" : "0");
    data->setData(QStringLiteral("x-special/gnome-copied-files"), gnome);
    QApplication::clipboard()->setMimeData(data);
}

bool clipboardHasFiles()
{
    const QMimeData *data = QApplication::clipboard()->mimeData();
    return data && data->hasUrls();
}

void pasteInto(const QString &folder, QWidget *parent)
{
    const QMimeData *data = QApplication::clipboard()->mimeData();
    if (!data || !data->hasUrls())
        return;
    QStringList sources;
    for (const QUrl &url : data->urls())
        if (url.isLocalFile())
            sources << url.toLocalFile();
    const bool cut = data->data(QStringLiteral("application/x-kde-cutselection")) == "1"
        || data->data(QStringLiteral("x-special/gnome-copied-files")).startsWith("cut");
    transfer(sources, folder, cut ? Action::Move : Action::Copy, parent);
    if (cut)
        QApplication::clipboard()->clear();
}

QString trashFiles()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash/files");
}

void emptyTrash(QWidget *parent)
{
    sounds::play(QStringLiteral("Windows XP Recycle.wav"));
    const QDir files(trashFiles());
    const QStringList names = files.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
    if (names.isEmpty())
        return;
    if (QMessageBox::question(parent, QObject::tr("Confirm Multiple File Delete"),
                              names.size() == 1 ? QObject::tr("Are you sure you want to delete '%1'?").arg(names.first())
                                                 : QObject::tr("Are you sure you want to delete these %1 items?").arg(names.size())) != QMessageBox::Yes)
        return;
    QDir(trashFiles()).removeRecursively();
    QDir().mkpath(trashFiles());
    QDir info(QFileInfo(trashFiles()).absolutePath() + QStringLiteral("/info"));
    for (const QString &name : info.entryList(QDir::Files))
        info.remove(name);
}

void restoreFromTrash(const QStringList &paths, QWidget *parent)
{
    for (const QString &path : paths) {
        const QString name = QFileInfo(path).fileName();
        const QString infoPath = QFileInfo(trashFiles()).absolutePath() + QStringLiteral("/info/") + name + QStringLiteral(".trashinfo");
        QSettings info(infoPath, QSettings::IniFormat);
        const QByteArray encoded = info.value(QStringLiteral("Trash Info/Path")).toByteArray();
        const QString original = QUrl::fromPercentEncoding(encoded);
        if (original.isEmpty() || QFileInfo::exists(original)) {
            QMessageBox::warning(parent, QObject::tr("Restore"), QObject::tr("Cannot restore '%1': something else has its place.").arg(name));
            continue;
        }
        QDir().mkpath(QFileInfo(original).absolutePath());
        if (QFile::rename(path, original))
            QFile::remove(infoPath);
        else
            QMessageBox::warning(parent, QObject::tr("Restore"), QObject::tr("Cannot restore '%1'.").arg(name));
    }
}

} // namespace fileops

#include "fileops.moc"
