#include "DesktopFileOpenEventRouter.h"

#include <QDir>
#include <QEvent>
#include <QFileOpenEvent>
#include <QFileInfo>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <utility>

DesktopFileOpenEventRouter::DesktopFileOpenEventRouter(QObject* parent)
    : QObject(parent)
{
}

void DesktopFileOpenEventRouter::setOpenHandler(OpenHandler handler)
{
    openHandler = std::move(handler);
    dispatchPendingFiles();
}

void DesktopFileOpenEventRouter::setReady(bool isReady)
{
    ready = isReady;
    dispatchPendingFiles();
}

void DesktopFileOpenEventRouter::discardPendingFilesMatching(
    const QStringList& filePaths)
{
    QSet<QString> normalizedPaths;
    for (const QString& filePath : filePaths)
    {
        normalizedPaths.insert(QDir::cleanPath(
            QFileInfo(filePath).absoluteFilePath()));
    }

    pendingFiles.erase(
        std::remove_if(
            pendingFiles.begin(), pendingFiles.end(),
            [&normalizedPaths](const QString& filePath)
            {
                return normalizedPaths.contains(QDir::cleanPath(
                    QFileInfo(filePath).absoluteFilePath()));
            }),
        pendingFiles.end());
}

int DesktopFileOpenEventRouter::pendingFileCount() const
{
    return pendingFiles.size();
}

bool DesktopFileOpenEventRouter::eventFilter(
    QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::FileOpen)
        return QObject::eventFilter(watched, event);

    auto* fileOpenEvent = static_cast<QFileOpenEvent*>(event);
    QString localFilePath;
    const QUrl url = fileOpenEvent->url();
    if (url.isValid() && !url.isEmpty())
    {
        if (!url.isLocalFile())
            return QObject::eventFilter(watched, event);
        localFilePath = url.toLocalFile();
    }
    else
    {
        localFilePath = fileOpenEvent->file();
        if (!QDir::isAbsolutePath(localFilePath))
            return QObject::eventFilter(watched, event);
    }

    if (localFilePath.isEmpty())
        return QObject::eventFilter(watched, event);

    pendingFiles.append(QDir::cleanPath(localFilePath));
    fileOpenEvent->accept();
    dispatchPendingFiles();
    return true;
}

void DesktopFileOpenEventRouter::dispatchPendingFiles()
{
    if (!ready || !openHandler)
        return;

    while (!pendingFiles.isEmpty())
    {
        const QString filePath = pendingFiles.takeFirst();
        openHandler(filePath);
    }
}
