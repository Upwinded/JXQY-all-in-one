#pragma once

#include "GameProfile.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

enum class AssetReferenceKind
{
    StaticIni,
    LuaLiteral,
    LuaDynamic
};

enum class AssetReferenceStatus
{
    Resolved,
    Missing,
    Candidate,
    Dynamic,
    Invalid
};

struct AssetReferenceOccurrence
{
    AssetReferenceKind kind = AssetReferenceKind::StaticIni;
    AssetReferenceStatus status = AssetReferenceStatus::Missing;
    QString sourceFilePath;
    QString sourceRelativePath;
    int lineNumber = 0;
    QString section;
    QString field;
    QString reference;
    QString resolvedFilePath;
};

struct AssetReferenceScanIssue
{
    QString sourceFilePath;
    QString sourceRelativePath;
    int lineNumber = 0;
    QString message;
};

struct AssetReferenceScanReport
{
    int totalFiles = 0;
    int scannedFiles = 0;
    int iniFiles = 0;
    int scriptFiles = 0;
    int staticReferences = 0;
    int luaCandidates = 0;
    int dynamicExpressions = 0;
    int missingReferences = 0;
    qint64 elapsedMilliseconds = 0;
    bool cancelled = false;
    QList<AssetReferenceOccurrence> occurrences;
    QList<AssetReferenceScanIssue> issues;
};

class AssetReferenceScanner
{
public:
    using ProgressCallback =
        std::function<void(int current, int total, const QString& currentFile)>;
    using CancelCallback = std::function<bool()>;

    static AssetReferenceScanReport scan(
        const QString& activeContentRoot,
        const QList<ResourceContentRoot>& orderedContentRoots,
        const ProgressCallback& progressCallback = ProgressCallback(),
        const CancelCallback& cancelCallback = CancelCallback());
};
