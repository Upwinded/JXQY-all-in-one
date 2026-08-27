#pragma once

#include <QByteArray>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

struct ScriptProjectSearchOptions
{
    QString query;
    QString replacement;
    bool caseSensitive = false;
    bool wholeWords = false;
    bool regularExpression = false;
};

struct ScriptProjectSearchIssue
{
    QString filePath;
    QString relativePath;
    QString message;
};

struct ScriptProjectSearchFileResult
{
    QString filePath;
    QString relativePath;
    QString beforeText;
    QString afterText;
    QByteArray originalSha256;
    QByteArray replacementBytes;
    int matchCount = 0;

    bool hasChanges() const;
};

struct ScriptProjectSearchReport
{
    QString rootPath;
    int totalFiles = 0;
    int scannedFiles = 0;
    int scriptFiles = 0;
    int skippedFiles = 0;
    int matchingFiles = 0;
    int totalMatches = 0;
    qint64 elapsedMilliseconds = 0;
    bool cancelled = false;
    QString validationError;
    QList<ScriptProjectSearchFileResult> files;
    QList<ScriptProjectSearchIssue> issues;
};

struct ScriptProjectReplaceResult
{
    bool success = false;
    int replacedFiles = 0;
    int replacements = 0;
    QStringList replacedFilePaths;
    QString errorMessage;
    QString warningMessage;
};

class ScriptProjectSearch
{
public:
    using ProgressCallback =
        std::function<void(int current, int total, const QString& currentFile)>;
    using CancelCallback = std::function<bool()>;

    static ScriptProjectSearchReport scan(
        const QString& activeContentRoot,
        const ScriptProjectSearchOptions& options,
        const ProgressCallback& progressCallback = ProgressCallback(),
        const CancelCallback& cancelCallback = CancelCallback());

    static ScriptProjectReplaceResult publish(
        const QString& activeContentRoot,
        const ScriptProjectSearchReport& report,
        const QStringList& selectedFilePaths,
        const QSet<QString>& blockedFilePaths = QSet<QString>());
};
