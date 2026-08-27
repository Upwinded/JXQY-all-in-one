#pragma once

#include <QList>
#include <QString>

#include <functional>
#include <string>

struct LuaScriptSyntaxIssue
{
    QString filePath;
    int lineNumber = 0;
    QString message;
    bool ioFailure = false;

    QString toString() const;
};

struct LuaScriptSyntaxReport
{
    int totalFiles = 0;
    int checkedFiles = 0;
    int skippedFiles = 0;
    int failedFiles = 0;
    bool scriptRootMissing = false;
    bool cancelled = false;
    QList<LuaScriptSyntaxIssue> issues;

    bool hasErrors() const;
};

class LuaScriptSyntaxValidator
{
public:
    using ProgressCallback = std::function<void(int current, int total, const QString& currentFile)>;
    using CancelCallback = std::function<bool()>;

    static LuaScriptSyntaxReport validateAssetsScripts(
        const QString& assetsDir,
        const ProgressCallback& progressCallback = ProgressCallback(),
        const CancelCallback& cancelCallback = CancelCallback());

    static LuaScriptSyntaxIssue validateScriptContent(
        const QString& filePath,
        const std::string& content);

    static bool shouldValidateScriptFile(
        const QString& assetsDir,
        const QString& filePath,
        const std::string& content);

private:
    static bool isIniLikeText(const std::string& content);
};
