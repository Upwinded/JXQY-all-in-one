#pragma once

#include "StoryGraphProjectResolver.h"

#include <QList>
#include <QString>

#include <memory>

// Immutable bridge between the editor's logical runtime resource selection and
// the filesystem-free StoryGraphProjectResolver callback contract. It retains
// only root paths and portable identities; every formal-resource access opens
// the root's current path so external root replacement remains visible.
class StoryGraphResourceContext
{
public:
    StoryGraphResourceContext();

    // maximumSingleFileBytes is preserved exactly and must leave room for one
    // representable exceeded-size marker: [0, max(qsizetype) - 1].
    static StoryGraphResourceContext resolve(
        const QString& assetsCollectionRoot,
        const QString& activeResourcePackId,
        qsizetype maximumSingleFileBytes,
        QString* diagnosticCode = nullptr,
        QString* message = nullptr,
        const QString& activeResourcePackEntryKey = QString());

    bool isValid() const;
    void clear();

    QString assetsCollectionRoot() const;
    QString activeContentRoot() const;
    QString canonicalActiveResourcePackId() const;
    QString selectionFingerprint() const;
    QList<StoryGraphContentRoot> orderedContentRoots() const;

    // Too-large files return a synthetic Found size of
    // maximumSingleFileBytes + 1 without retaining file payload bytes. This
    // adapter result is intended for StoryGraphProjectResolver, which turns it
    // into BudgetExceeded before hashing or caching.
    StoryGraphReadResult read(
        const StoryGraphContentRoot& root,
        const QString& strictVirtualPath) const;
    // Verifies the same current root path and regular-file shape as read(), but
    // never copies payload bytes. Intended for GUI-thread snapshot admission
    // before immutable editor bytes move to the worker.
    StoryGraphReadResult probeRegularFile(
        const StoryGraphContentRoot& root,
        const QString& strictVirtualPath) const;
    StoryGraphMapFolderResolution resolveMapFolder(
        const QString& strictMapTarget) const;

private:
    struct Data;
    explicit StoryGraphResourceContext(
        std::shared_ptr<const Data> data);

    std::shared_ptr<const Data> data;
};
