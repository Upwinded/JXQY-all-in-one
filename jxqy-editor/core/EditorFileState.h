#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <functional>

class DurableFileTransaction;

// The file read by a document and its ordinary project save destination.
class EditorFileState
{
public:
    using OverwriteConfirmation = std::function<bool(const QStringList&)>;
    static EditorFileState read(const QString& sourcePath, const QString& targetPath);
    static EditorFileState newFile(const QString& targetPath);
    EditorFileState forSave(const QString& targetPath) const;
    bool isReadable() const;
    bool prepareWrite(DurableFileTransaction& transaction,
                      const QString& targetPath, const QString& temporaryPath,
                      EditorFileState& savedState, QString& errorMessage,
                      const OverwriteConfirmation& confirmOverwrite = {}) const;

private:
    struct Snapshot
    {
        QString path;
        QByteArray sha256;
        bool exists = false;
        bool readable = false;
    };
    static Snapshot readSnapshot(const QString& path);
    Snapshot source;
    Snapshot target;
};
