#include "LuaScriptSyntaxValidator.h"
#include "Util.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
}

namespace
{
QString normalizePath(QString path)
{
    path.replace("\\", "/");
    while (path.startsWith("./"))
        path = path.mid(2);
    while (path.contains("//"))
        path.replace("//", "/");
    return path;
}

bool isTextScriptExtension(const QString& extension)
{
    return extension.compare("txt", Qt::CaseInsensitive) == 0 ||
        extension.compare("lua", Qt::CaseInsensitive) == 0;
}

bool isGeneratedDataText(const QString& fileName)
{
    return fileName.compare("talkindex.txt", Qt::CaseInsensitive) == 0;
}

bool isLegacyMapTalkText(const QString& relativePath)
{
    QString normalized = normalizePath(relativePath).toLower();
    return normalized.startsWith("script/map/") &&
        normalized.endsWith("/talk.txt");
}

bool isDocumentationText(const QString& fileName)
{
    return fileName.compare(QString::fromUtf8("help编写脚本文件.txt"), Qt::CaseInsensitive) == 0 ||
        fileName.compare(QString::fromUtf8("script错误汇总.txt"), Qt::CaseInsensitive) == 0;
}

std::string removeUtf8Bom(std::string content)
{
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF)
    {
        content.erase(0, 3);
    }
    return content;
}

int extractLuaErrorLineNumber(const QString& message)
{
    static const QRegularExpression linePattern(QStringLiteral(":(\\d+):"));
    QRegularExpressionMatchIterator iterator = linePattern.globalMatch(message);
    int lineNumber = 0;
    while (iterator.hasNext())
        lineNumber = iterator.next().captured(1).toInt();
    return lineNumber;
}
}

QString LuaScriptSyntaxIssue::toString() const
{
    if (lineNumber > 0)
        return QString::fromUtf8("%1:%2: %3").arg(filePath).arg(lineNumber).arg(message);
    return QString::fromUtf8("%1: %2").arg(filePath, message);
}

bool LuaScriptSyntaxReport::hasErrors() const
{
    return failedFiles > 0 || !issues.isEmpty();
}

LuaScriptSyntaxReport LuaScriptSyntaxValidator::validateAssetsScripts(
    const QString& assetsDir,
    const ProgressCallback& progressCallback,
    const CancelCallback& cancelCallback)
{
    LuaScriptSyntaxReport report;
    QString cleanAssetsDir = QDir::cleanPath(QFileInfo(assetsDir).absoluteFilePath());
    QString scriptRoot = QDir(cleanAssetsDir).filePath("script");
    if (!QDir(scriptRoot).exists())
    {
        report.scriptRootMissing = true;
        return report;
    }

    QStringList files;
    QDirIterator iterator(
        scriptRoot,
        QDir::Files,
        QDirIterator::Subdirectories |
            QDirIterator::FollowSymlinks);
    while (iterator.hasNext())
    {
        if (cancelCallback && cancelCallback())
        {
            report.cancelled = true;
            return report;
        }
        files.append(iterator.next());
    }
    files.sort(Qt::CaseSensitive);

    report.totalFiles = files.size();
    for (int i = 0; i < files.size(); i++)
    {
        if (cancelCallback && cancelCallback())
        {
            report.cancelled = true;
            break;
        }

        const QString filePath = files[i];
        if (progressCallback)
            progressCallback(i + 1, files.size(), filePath);

        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly))
        {
            LuaScriptSyntaxIssue issue;
            issue.filePath = filePath;
            issue.message = QString::fromUtf8("无法读取脚本文件");
            issue.ioFailure = true;
            report.issues.append(issue);
            report.failedFiles++;
            continue;
        }

        QByteArray payload = file.readAll();
        std::string content(payload.constData(), static_cast<size_t>(payload.size()));
        if (!shouldValidateScriptFile(cleanAssetsDir, filePath, content))
        {
            report.skippedFiles++;
            continue;
        }

        report.checkedFiles++;
        LuaScriptSyntaxIssue issue = validateScriptContent(filePath, content);
        if (!issue.message.isEmpty())
        {
            report.issues.append(issue);
            report.failedFiles++;
        }
    }

    return report;
}

LuaScriptSyntaxIssue LuaScriptSyntaxValidator::validateScriptContent(
    const QString& filePath,
    const std::string& content)
{
    LuaScriptSyntaxIssue issue;
    issue.filePath = filePath;

    std::string script = removeUtf8Bom(content);
    if (!Util::isUtf8(
            reinterpret_cast<const uint8_t*>(script.data()), script.size()))
    {
        issue.message = QString::fromUtf8("脚本不是有效的 UTF-8 文本");
        return issue;
    }
    lua_State* luaState = luaL_newstate();
    if (luaState == nullptr)
    {
        issue.message = QString::fromUtf8("无法创建 Lua 状态");
        return issue;
    }

    QByteArray chunkName = filePath.toUtf8();
    int status = luaL_loadbufferx(
        luaState,
        script.data(),
        script.size(),
        chunkName.constData(),
        "t");
    if (status != LUA_OK)
    {
        const char* luaMessage = lua_tostring(luaState, -1);
        issue.message = QString::fromUtf8(luaMessage != nullptr ? luaMessage : "Lua 语法错误");
        issue.lineNumber = extractLuaErrorLineNumber(issue.message);
    }

    lua_close(luaState);
    return issue;
}

bool LuaScriptSyntaxValidator::shouldValidateScriptFile(
    const QString& assetsDir,
    const QString& filePath,
    const std::string& content)
{
    QFileInfo fileInfo(filePath);
    if (!isTextScriptExtension(fileInfo.suffix()) ||
        isGeneratedDataText(fileInfo.fileName()) ||
        isDocumentationText(fileInfo.fileName()))
    {
        return false;
    }

    QString relativePath = normalizePath(QDir(assetsDir).relativeFilePath(filePath));
    if (!relativePath.toLower().startsWith("script/"))
    {
        return false;
    }
    if (isLegacyMapTalkText(relativePath))
    {
        return false;
    }

    return !isIniLikeText(removeUtf8Bom(content));
}

bool LuaScriptSyntaxValidator::isIniLikeText(const std::string& content)
{
    size_t lineStart = 0;
    while (lineStart <= content.size())
    {
        size_t lineEnd = content.find('\n', lineStart);
        if (lineEnd == std::string::npos)
            lineEnd = content.size();

        std::string line = content.substr(lineStart, lineEnd - lineStart);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        size_t first = 0;
        while (first < line.size() && (line[first] == ' ' || line[first] == '\t'))
            first++;

        std::string trimmed = line.substr(first);
        if (!trimmed.empty() && trimmed.front() != ';' && trimmed.front() != '#')
            return trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']';

        if (lineEnd == content.size())
            break;
        lineStart = lineEnd + 1;
    }

    return false;
}
