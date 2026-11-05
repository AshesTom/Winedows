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
#include <QRegularExpression>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <atomic>
#include <functional>
#include <memory>

namespace fileops {
namespace {

const qint64 CHUNK = 1 << 20;
const int STRING_COPY_PLATE = 4178;

// The copy engine's name for a copy in its own folder (sub_3006d1c7): "Copy () of " (4178) before
// NAME, or NAME itself when it already starts as the template does up to its "(". Then
// PathYetAnotherMakeUniqueName (sub_30030889): first without the "()" and the space before it
// ("Copy of a.txt"), then 2, 3… in the first parentheses holding only digits ("Copy (2) of a.txt").
QString copyName(const QString &folder, const QString &name)
{
    const QString plate = xpfiles::shellString(STRING_COPY_PLATE, "Copy () of ");
    const qsizetype open = plate.indexOf(QLatin1Char('('));
    const QString pattern = open > 0 && name.startsWith(plate.left(open + 1)) ? name : plate + name;
    const QDir dir(folder);
    QString first = pattern;
    if (qsizetype empty = pattern.indexOf(QLatin1String("()")); empty >= 0) {
        qsizetype length = 2;
        if (empty > 0 && pattern.at(empty - 1) == QLatin1Char(' ')) {
            --empty;
            ++length;
        }
        first.remove(empty, length);
    }
    if (!QFileInfo::exists(dir.filePath(first)))
        return first;
    static const QRegularExpression digits(QStringLiteral("\\(([0-9]*)\\)"));
    const QRegularExpressionMatch match = digits.match(pattern);
    if (!match.hasMatch())
        return freeName(folder, name);   // " (%d)" before the extension
    for (int number = 2; number < 1000; ++number) {
        const QString candidate = pattern.left(match.capturedStart(1)) + QString::number(number) + pattern.mid(match.capturedEnd(1));
        if (!QFileInfo::exists(dir.filePath(candidate)))
            return candidate;
    }
    return freeName(folder, name);
}

// The work behind a progress dialog, off the GUI thread
class Job : public QThread
{
    Q_OBJECT
public:
    using QThread::QThread;
    std::atomic<bool> cancelled{false};
    QString error;
    // each item done, where it was and where it is now: for the undo stack
    QList<QPair<QString, QString>> results;

Q_SIGNALS:
    void fileStarted(const QString &name, const QString &from);
    void progressed(qint64 done, qint64 total);
    // a question comes: the dialog shows now if it has not yet
    void wantsDialog();
};

// The transfer's work. A blocking signal asks about files of the same name.
class Worker : public Job
{
    Q_OBJECT
public:
    enum Answer { Yes, YesToAll, No, Cancel };
    // into the folder TARGET, or each source to its own path in TARGETS (FOF_MULTIDESTFILES)
    Worker(QStringList sources, QString target, QStringList targets, Action action, QObject *parent)
        : Job(parent)
        , m_sources(std::move(sources))
        , m_target(std::move(target))
        , m_targets(std::move(targets))
        , m_action(action)
    {
    }

Q_SIGNALS:
    void askReplace(const QString &path, int *answer);

protected:
    void run() override
    {
        qint64 total = 0;
        for (const QString &source : std::as_const(m_sources))
            total += sizeOf(source);
        m_total = qMax<qint64>(1, total);
        for (qsizetype i = 0; i < m_sources.size(); ++i) {
            const QString &source = m_sources.at(i);
            if (cancelled)
                return;
            const QString target = m_targets.isEmpty() ? QDir(m_target).filePath(QFileInfo(source).fileName()) : m_targets.value(i);
            if (QFileInfo(target).absoluteFilePath() == QFileInfo(source).absoluteFilePath()) {
                if (m_action == Action::Copy) {
                    const QString copy = QDir(m_target).filePath(copyName(m_target, QFileInfo(source).fileName()));
                    copyItem(source, copy);
                    done(source, copy);
                }
                continue;
            }
            if (target.startsWith(QFileInfo(source).absoluteFilePath() + QLatin1Char('/'))) {
                error = QObject::tr("Cannot put '%1' into itself.").arg(QFileInfo(source).fileName());
                return;
            }
            if (m_action == Action::Move && !QFileInfo::exists(target) && QFile::rename(source, target)) {
                m_done += sizeOf(target);
                report();
                results.append({source, target});
                continue;
            }
            QDir().mkpath(QFileInfo(target).absolutePath());
            copyItem(source, target);
            if (m_action == Action::Move && !cancelled && error.isEmpty()) {
                QFileInfo info(source);
                if (info.isDir() && !info.isSymLink())
                    QDir(source).removeRecursively();
                else
                    QFile::remove(source);
            }
            done(source, target);
        }
    }

private:
    void done(const QString &source, const QString &target)
    {
        if (!cancelled && error.isEmpty() && QFileInfo::exists(target))
            results.append({source, target});
    }

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
            Q_EMIT wantsDialog();
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
    QStringList m_targets;
    Action m_action;
    qint64 m_total = 1, m_done = 0;
    bool m_replaceAll = false;
};

// The delete's work: to the Recycle Bin an item at a time, or for good a file at a time, each
// folder after what it holds. What is deleted from the Recycle Bin takes its .trashinfo with it.
class Deleter : public Job
{
    Q_OBJECT
public:
    Deleter(QStringList paths, bool permanent, QString trash, QObject *parent)
        : Job(parent)
        , m_paths(std::move(paths))
        , m_permanent(permanent)
        , m_trash(std::move(trash))
    {
    }

protected:
    void run() override
    {
        qint64 total = 0;
        for (const QString &path : std::as_const(m_paths))
            total += m_permanent ? countOf(path) : 1;
        m_total = qMax<qint64>(1, total);
        QStringList failed;
        for (const QString &path : std::as_const(m_paths)) {
            if (cancelled)
                break;
            const QFileInfo info(path);
            bool done;
            if (!m_permanent) {
                Q_EMIT fileStarted(info.fileName(), info.absolutePath());
                QString inTrash;
                done = QFile::moveToTrash(path, &inTrash);
                if (done)
                    results.append({path, inTrash});
                ++m_done;
                Q_EMIT progressed(m_done, m_total);
            } else {
                done = removeItem(path);
            }
            if (done && m_permanent && QFileInfo(info.absolutePath()).canonicalFilePath() == m_trash)
                QFile::remove(QFileInfo(m_trash).absolutePath() + QStringLiteral("/info/") + info.fileName() + QStringLiteral(".trashinfo"));
            if (!done && !cancelled)
                failed << info.fileName();
        }
        if (!failed.isEmpty())
            error = QObject::tr("Cannot delete %1.").arg(failed.join(QStringLiteral(", ")));
    }

private:
    static qint64 countOf(const QString &path)
    {
        const QFileInfo info(path);
        if (!info.isDir() || info.isSymLink())
            return 1;
        qint64 count = 1;
        QDirIterator it(path, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            ++count;
        }
        return count;
    }

    bool removeItem(const QString &path)
    {
        if (cancelled)
            return false;
        const QFileInfo info(path);
        bool done = true;
        if (info.isDir() && !info.isSymLink()) {
            const QFileInfoList children = QDir(path).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
            for (const QFileInfo &child : children)
                done = removeItem(child.absoluteFilePath()) && done;
            if (cancelled)
                return false;
            Q_EMIT fileStarted(info.fileName(), info.absolutePath());
            done = QDir().rmdir(path) && done;
        } else {
            Q_EMIT fileStarted(info.fileName(), info.absolutePath());
            done = QFile::remove(path);
        }
        ++m_done;
        Q_EMIT progressed(m_done, m_total);
        return done;
    }

    QStringList m_paths;
    bool m_permanent;
    QString m_trash;
    qint64 m_total = 1, m_done = 0;
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

// shell32's copy engine (its dialog thread sub_300afcf5, the dialog's procedure sub_300ae4d3)
const int DIALOG_PROGRESS = 1020, ID_CANCEL = 2, ID_ANIMATION = 106, ID_FILE = 102, ID_FROM = 103, ID_BAR = 104, ID_TIME = 105;
// the caption is 5952 plus the operation (FO_MOVE 1, FO_COPY 2, FO_DELETE 3)
const int STRING_MOVING = 5953, STRING_COPYING = 5954, STRING_DELETING = 5955, STRING_FROM_TO = 5968, STRING_FROM = 5969;
const int STRING_MINUTES = 8739, STRING_SECONDS = 8740, STRING_EMPTYING = 9025, STRING_DELETING_BIN = 9026;
// on WM_SHOWWINDOW: 159 plus the operation; a delete has 164 for good, 162 to the Recycle Bin
// (FOF_ALLOWUNDO), and 163 when its progress title is 9025 or 9026, the Recycle Bin's own
const int AVI_MOVE = 160, AVI_COPY = 161, AVI_RECYCLE = 162, AVI_FROM_BIN = 163, AVI_DELETE = 164;
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

// Dialog 1020 for JOB, made its child: CAPTION, animation AVI, and FROM for the folder of each file
// (empty: the line stays blank). UNIT is the size the estimate counts in. A SIMPLE title takes the
// file's line for good, as FOF_SIMPLEPROGRESS has it.
void runWithDialog(Job *job, QDialog *window, int avi, qint64 unit, const std::function<QString(const QString &)> &from,
                   const QString &simple, QWidget *parent)
{
    QLabel *name = nullptr, *fromLine = nullptr, *time = nullptr;
    QProgressBar *bar = nullptr;
    QPushButton *cancel = nullptr;
    const std::optional<w4w::PeResources> shell32 = xpfiles::resources(xpfiles::find(QStringLiteral("shell32.dll")));
    if (shell32) {
        const xpdialog::Page page = xpdialog::build(*shell32, DIALOG_PROGRESS, window);
        name = qobject_cast<QLabel *>(page.controls.value(ID_FILE));
        fromLine = qobject_cast<QLabel *>(page.controls.value(ID_FROM));
        time = qobject_cast<QLabel *>(page.controls.value(ID_TIME));
        bar = qobject_cast<QProgressBar *>(page.controls.value(ID_BAR));
        cancel = qobject_cast<QPushButton *>(page.controls.value(ID_CANCEL));
        if (auto *animation = dynamic_cast<xpdialog::Animation *>(page.controls.value(ID_ANIMATION)))
            animation->open(*shell32, avi);
    }
    if (!name || !fromLine || !bar || !cancel) {
        // without the user's shell32: the same lines in a window of their own
        qDeleteAll(window->findChildren<QWidget *>(Qt::FindDirectChildrenOnly));
        window->setFixedSize(370, 150);
        auto *layout = new QVBoxLayout(window);
        name = new QLabel(window);
        fromLine = new QLabel(window);
        time = new QLabel(window);
        bar = new QProgressBar(window);
        bar->setTextVisible(false);
        cancel = new QPushButton(QObject::tr("Cancel"), window);
        layout->addWidget(name);
        layout->addWidget(fromLine);
        layout->addWidget(bar);
        layout->addWidget(time);
        layout->addWidget(cancel, 0, Qt::AlignRight);
    }
    bar->setRange(0, 100);
    job->setParent(window);
    if (!simple.isEmpty())
        name->setText(simple);
    else
        QObject::connect(job, &Job::fileStarted, window, [name, fromLine, from](const QString &file, const QString &folder) {
            name->setText(file);
            fromLine->setText(from(folder));
        });
    auto estimate = std::make_shared<Estimate>();
    auto counts = std::make_shared<QPair<qint64, qint64>>(0, 1);
    QObject::connect(job, &Job::progressed, window, [bar, counts, unit](qint64 done, qint64 total) {
        *counts = {done / unit, qMax<qint64>(1, total / unit)};
        bar->setValue(int(done * 100 / qMax<qint64>(1, total)));
    });
    auto *estimates = new QTimer(window);
    QObject::connect(estimates, &QTimer::timeout, window, [time, estimate, counts] {
        if (time)
            time->setText(estimate->update(counts->first, counts->second));
    });
    auto shown = std::make_shared<QElapsedTimer>();
    auto showNow = [window, job, shown, estimates, estimate, counts, time] {
        if (shown->isValid() || job->isFinished())
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
    // a question can come before the dialog: it is the dialog's own, so the dialog comes too
    QObject::connect(job, &Job::wantsDialog, window, showNow);
    QObject::connect(cancel, &QPushButton::clicked, window, [job] { job->cancelled = true; });
    QObject::connect(job, &QThread::finished, window, [window, job, parent, shown] {
        if (!job->error.isEmpty())
            QMessageBox::warning(parent, window->windowTitle(), job->error);
        const qint64 left = shown->isValid() ? LEAST_SHOWN - shown->elapsed() : 0;
        QTimer::singleShot(qMax<qint64>(0, left), window, &QWidget::close);
    });
    showing->start(SHOW_DELAY);
    job->start();
}

QDialog *progressWindow(int caption, const char *fallback)
{
    auto *window = new QDialog(nullptr);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setWindowTitle(xpfiles::shellString(caption, fallback));
    return window;
}

// shell32's undo stack (sub_300ba1b1 pushes, sub_300ba22e reads the top, sub_3006b401 undoes): an
// entry for each move, copy, delete to the Recycle Bin and rename, the oldest dropped past ten,
// named by string 4143 plus the operation. An undo that fails or is cancelled stays on the stack.
const int UNDO_DEPTH = 10, STRING_UNDO_ITEM = 4164, STRING_UNDO = 4165, STRING_OPERATION = 4143;
enum Operation { FO_MOVE = 1, FO_COPY = 2, FO_DELETE = 3, FO_RENAME = 4 };

struct UndoEntry
{
    int operation;
    QList<QPair<QString, QString>> items;   // where each item was, and where the operation put it
};

QList<UndoEntry> &undoEntries()
{
    static QList<UndoEntry> entries;
    return entries;
}

// an undo running: nothing more can be undone until it ends
int undoing = 0;

void record(int operation, const QList<QPair<QString, QString>> &items)
{
    if (items.isEmpty())
        return;
    QList<UndoEntry> &entries = undoEntries();
    entries.append({operation, items});
    while (entries.size() > UNDO_DEPTH)
        entries.removeFirst();
    Q_EMIT UndoStack::instance()->changed();
}

// Runs the copy or move; DONE learns whether all of it was done. RECORD puts it on the undo stack.
void startTransfer(const QStringList &sources, const QString &targetFolder, const QStringList &targets, Action action, bool record,
                   QWidget *parent, const std::function<void(bool)> &done = {})
{
    const bool copy = action == Action::Copy;
    QDialog *window = progressWindow(copy ? STRING_COPYING : STRING_MOVING, copy ? "Copying..." : "Moving...");
    auto *worker = new Worker(sources, targetFolder, targets, action, window);
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
    QObject::connect(worker, &QThread::finished, window, [worker, copy, record, done] {
        if (record)
            fileops::record(copy ? FO_COPY : FO_MOVE, worker->results);
        if (done)
            done(!worker->cancelled && worker->error.isEmpty());
    });
    const auto from = [targetFolder, targets](const QString &folder) {
        const QString to = targets.isEmpty() ? targetFolder : QFileInfo(targets.first()).absolutePath();
        return xpfiles::shellString(STRING_FROM_TO, "From '%1!ls!' to '%2!ls!'")
            .replace(QLatin1String("%1!ls!"), QFileInfo(folder).fileName())
            .replace(QLatin1String("%2!ls!"), QFileInfo(to).fileName());
    };
    runWithDialog(worker, window, copy ? AVI_COPY : AVI_MOVE, BLOCK, from, QString(), parent);
}

void startRemove(const QStringList &paths, bool permanent, bool record, QWidget *parent, const std::function<void(bool)> &done = {})
{
    const QString title = paths.size() == 1 ? QObject::tr("Confirm File Delete") : QObject::tr("Confirm Multiple File Delete");
    const QString question = permanent ? QObject::tr("Are you sure you want to delete %1?").arg(quotedList(paths))
                                       : QObject::tr("Are you sure you want to send %1 to the Recycle Bin?").arg(quotedList(paths));
    // Recycle Bin Properties' "Display delete confirmation dialog"
    QSettings settings(QStringLiteral("win4wine"), QStringLiteral("fileops"));
    if (settings.value(QStringLiteral("RecycleBin/ConfirmDelete"), true).toBool()
        && QMessageBox::question(parent, title, question) != QMessageBox::Yes) {
        if (done)
            done(false);
        return;
    }
    // the Recycle Bin's folder deletes its own items with FOF_SIMPLEPROGRESS and title 9026
    const QString trash = QFileInfo(trashFiles()).canonicalFilePath();
    const bool fromBin = permanent && QFileInfo(QFileInfo(paths.first()).absolutePath()).canonicalFilePath() == trash;
    QDialog *window = progressWindow(STRING_DELETING, "Deleting...");
    auto *deleter = new Deleter(paths, permanent, trash, window);
    // only what went to the Recycle Bin (FOF_ALLOWUNDO) can come back
    QObject::connect(deleter, &QThread::finished, window, [deleter, permanent, record, done] {
        if (record && !permanent)
            fileops::record(FO_DELETE, deleter->results);
        if (done)
            done(!deleter->cancelled && deleter->error.isEmpty());
    });
    const auto from = [](const QString &folder) {
        return xpfiles::shellString(STRING_FROM, "From '%1!ls!'").replace(QLatin1String("%1!ls!"), QFileInfo(folder).fileName());
    };
    const QString simple = fromBin ? xpfiles::shellString(STRING_DELETING_BIN, "Deleting files from the Recycle Bin") : QString();
    runWithDialog(deleter, window, fromBin ? AVI_FROM_BIN : permanent ? AVI_DELETE : AVI_RECYCLE, 1, from, simple, parent);
}

} // namespace

void transfer(const QStringList &sources, const QString &targetFolder, Action action, QWidget *parent)
{
    if (!sources.isEmpty())
        startTransfer(sources, targetFolder, {}, action, true, parent);
}

void remove(const QStringList &paths, bool permanent, QWidget *parent)
{
    if (!paths.isEmpty())
        startRemove(paths, permanent, true, parent);
}

void recordRename(const QString &before, const QString &after)
{
    if (!undoing)
        record(FO_RENAME, {{before, after}});
}

UndoStack *UndoStack::instance()
{
    static UndoStack *stack = new UndoStack;
    return stack;
}

bool UndoStack::canUndo() const
{
    return !undoing && !undoEntries().isEmpty();
}

QString UndoStack::menuText() const
{
    if (!canUndo())
        return xpfiles::shellString(STRING_UNDO, "&Undo\tCtrl+Z");
    static const char *const names[] = {"Move", "Copy", "Delete", "Rename"};
    const int operation = undoEntries().last().operation;
    return xpfiles::shellString(STRING_UNDO_ITEM, "&Undo %s\tCtrl+Z")
        .replace(QLatin1String("%s"), xpfiles::shellString(STRING_OPERATION + operation, names[operation - 1]));
}

void UndoStack::undo(QWidget *parent)
{
    if (!canUndo())
        return;
    const UndoEntry entry = undoEntries().last();
    ++undoing;
    Q_EMIT changed();
    // the entry leaves the stack once the undo is done
    const auto finished = [](bool done) {
        --undoing;
        if (done && !undoEntries().isEmpty())
            undoEntries().removeLast();
        Q_EMIT UndoStack::instance()->changed();
    };
    QStringList before, after;
    for (const auto &item : entry.items) {
        before << item.first;
        after << item.second;
    }
    switch (entry.operation) {
    case FO_MOVE:
        // back where each item was (FO_MOVE with FOF_MULTIDESTFILES)
        startTransfer(after, QString(), before, Action::Move, false, parent, finished);
        break;
    case FO_COPY:
        // the copies deleted for good, after asking (FO_DELETE without FOF_ALLOWUNDO)
        startRemove(after, true, false, parent, finished);
        break;
    case FO_DELETE:
        finished(restoreFromTrash(after, parent));
        break;
    case FO_RENAME:
        finished(QFile::rename(after.first(), before.first()));
        break;
    }
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
    // FO_DELETE of everything in it, FOF_SIMPLEPROGRESS under title 9025
    QStringList paths;
    for (const QString &name : names)
        paths << files.absoluteFilePath(name);
    const QString trash = QFileInfo(trashFiles()).canonicalFilePath();
    QDialog *window = progressWindow(STRING_DELETING, "Deleting...");
    auto *deleter = new Deleter(paths, true, trash, window);
    QObject::connect(deleter, &QThread::finished, window, [deleter, trash] {
        if (deleter->cancelled)
            return;
        // .trashinfo files left without their item
        QDir info(QFileInfo(trash).absolutePath() + QStringLiteral("/info"));
        for (const QString &name : info.entryList(QDir::Files))
            if (name.endsWith(QLatin1String(".trashinfo")) && !QFileInfo::exists(QDir(trash).filePath(name.chopped(10))))
                info.remove(name);
    });
    runWithDialog(deleter, window, AVI_FROM_BIN, 1, {}, xpfiles::shellString(STRING_EMPTYING, "Emptying the Recycle Bin"), parent);
}

bool restoreFromTrash(const QStringList &paths, QWidget *parent)
{
    bool all = true;
    for (const QString &path : paths) {
        const QString name = QFileInfo(path).fileName();
        const QString infoPath = QFileInfo(trashFiles()).absolutePath() + QStringLiteral("/info/") + name + QStringLiteral(".trashinfo");
        QSettings info(infoPath, QSettings::IniFormat);
        const QByteArray encoded = info.value(QStringLiteral("Trash Info/Path")).toByteArray();
        const QString original = QUrl::fromPercentEncoding(encoded);
        if (original.isEmpty() || QFileInfo::exists(original)) {
            QMessageBox::warning(parent, QObject::tr("Restore"), QObject::tr("Cannot restore '%1': something else has its place.").arg(name));
            all = false;
            continue;
        }
        QDir().mkpath(QFileInfo(original).absolutePath());
        if (QFile::rename(path, original))
            QFile::remove(infoPath);
        else {
            QMessageBox::warning(parent, QObject::tr("Restore"), QObject::tr("Cannot restore '%1'.").arg(name));
            all = false;
        }
    }
    return all;
}

} // namespace fileops

#include "fileops.moc"
