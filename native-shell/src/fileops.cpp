// SPDX-License-Identifier: LGPL-2.1-or-later
#include "fileops.h"

#include "sounds.h"

#include "fileicons.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
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
#include <QVBoxLayout>

#include <atomic>

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
    void progressed(int percent);
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
        Q_EMIT progressed(int(m_done * 100 / m_total));
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

void transfer(const QStringList &sources, const QString &targetFolder, Action action, QWidget *parent)
{
    if (sources.isEmpty())
        return;
    auto *window = new QWidget(nullptr, Qt::Dialog);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setWindowTitle(action == Action::Copy ? QObject::tr("Copying...") : QObject::tr("Moving..."));
    window->setFixedSize(370, 150);
    auto *layout = new QVBoxLayout(window);
    auto *name = new QLabel(window), *from = new QLabel(window);
    auto *bar = new QProgressBar(window);
    bar->setTextVisible(false);
    auto *cancel = new QPushButton(QObject::tr("Cancel"), window);
    layout->addWidget(name);
    layout->addWidget(from);
    layout->addWidget(bar);
    layout->addWidget(cancel, 0, Qt::AlignRight);
    auto *worker = new Worker(sources, targetFolder, action, window);
    QObject::connect(worker, &Worker::fileStarted, window, [name, from, targetFolder](const QString &file, const QString &folder) {
        name->setText(file);
        from->setText(QObject::tr("From '%1' to '%2'").arg(QFileInfo(folder).fileName(), QFileInfo(targetFolder).fileName()));
    });
    QObject::connect(worker, &Worker::progressed, bar, &QProgressBar::setValue);
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
    QObject::connect(worker, &QThread::finished, window, [window, worker, parent] {
        if (!worker->error.isEmpty())
            QMessageBox::warning(parent, window->windowTitle(), worker->error);
        window->close();
    });
    window->show();
    worker->start();
}

void remove(const QStringList &paths, bool permanent, QWidget *parent)
{
    if (paths.isEmpty())
        return;
    const QString title = paths.size() == 1 ? QObject::tr("Confirm File Delete") : QObject::tr("Confirm Multiple File Delete");
    const QString question = permanent ? QObject::tr("Are you sure you want to delete %1?").arg(quotedList(paths))
                                       : QObject::tr("Are you sure you want to send %1 to the Recycle Bin?").arg(quotedList(paths));
    if (QMessageBox::question(parent, title, question) != QMessageBox::Yes)
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
