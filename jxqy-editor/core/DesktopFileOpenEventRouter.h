#pragma once

#include <QObject>
#include <QStringList>

#include <functional>

class DesktopFileOpenEventRouter final : public QObject
{
public:
    using OpenHandler = std::function<bool(const QString&)>;

    explicit DesktopFileOpenEventRouter(QObject* parent = nullptr);

    void setOpenHandler(OpenHandler handler);
    void setReady(bool ready);
    void discardPendingFilesMatching(const QStringList& filePaths);
    int pendingFileCount() const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void dispatchPendingFiles();

    OpenHandler openHandler;
    QStringList pendingFiles;
    bool ready = false;
};
