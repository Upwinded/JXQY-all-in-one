#include "ScriptProjectSearch.h"

#include "DurableFileTransaction.h"
#include "EditorAssetPath.h"
#include "LuaScriptSyntaxValidator.h"
#include "Util.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>

#include <algorithm>
#include <string>
#include <utility>

namespace
{
struct MatchRange
{
    qsizetype start = 0;
    qsizetype length = 0;
};

QString translated(const char* source)
{
    return QCoreApplication::translate("ScriptProjectSearch", source);
}

QString normalizedRelativePath(const QString& rootPath,
                               const QString& filePath)
{
    QString relativePath = QDir(rootPath).relativeFilePath(filePath);
    relativePath.replace('\\', '/');
    return relativePath;
}

bool hasScriptTextExtension(const QFileInfo& fileInfo)
{
    const QString suffix = fileInfo.suffix();
    return suffix.compare(QStringLiteral("txt"), Qt::CaseInsensitive) == 0 ||
        suffix.compare(QStringLiteral("lua"), Qt::CaseInsensitive) == 0;
}

bool isWordCharacter(const QChar character)
{
    return character == QLatin1Char('_') || character.isLetterOrNumber();
}

bool hasWholeWordBoundaries(const QString& text,
                            qsizetype start,
                            qsizetype length)
{
    const bool leftBoundary = start <= 0 ||
        !isWordCharacter(text.at(start - 1));
    const qsizetype end = start + length;
    const bool rightBoundary = end >= text.size() ||
        !isWordCharacter(text.at(end));
    return leftBoundary && rightBoundary;
}

QRegularExpression buildRegularExpression(
    const ScriptProjectSearchOptions& options)
{
    QString pattern = options.query;
    if (options.wholeWords)
    {
        pattern = QStringLiteral(
            "(?<![\\p{L}\\p{N}_])(?:%1)(?![\\p{L}\\p{N}_])")
            .arg(pattern);
    }
    QRegularExpression::PatternOptions patternOptions =
        QRegularExpression::UseUnicodePropertiesOption;
    if (!options.caseSensitive)
        patternOptions |= QRegularExpression::CaseInsensitiveOption;
    return QRegularExpression(pattern, patternOptions);
}

QList<MatchRange> literalMatches(
    const QString& text, const ScriptProjectSearchOptions& options)
{
    QList<MatchRange> matches;
    const Qt::CaseSensitivity sensitivity = options.caseSensitive
        ? Qt::CaseSensitive : Qt::CaseInsensitive;
    qsizetype offset = 0;
    while (offset <= text.size() - options.query.size())
    {
        const qsizetype matchStart = text.indexOf(
            options.query, offset, sensitivity);
        if (matchStart < 0)
            break;
        if (!options.wholeWords || hasWholeWordBoundaries(
                text, matchStart, options.query.size()))
        {
            matches.append({matchStart, options.query.size()});
        }
        offset = matchStart + qMax<qsizetype>(options.query.size(), 1);
    }
    return matches;
}

QList<MatchRange> regularExpressionMatches(
    const QString& text,
    const QRegularExpression& expression,
    bool& hasZeroLengthMatch)
{
    QList<MatchRange> matches;
    hasZeroLengthMatch = false;
    QRegularExpressionMatchIterator iterator = expression.globalMatch(text);
    while (iterator.hasNext())
    {
        const QRegularExpressionMatch match = iterator.next();
        if (match.capturedLength() == 0)
        {
            hasZeroLengthMatch = true;
            matches.clear();
            return matches;
        }
        matches.append({match.capturedStart(), match.capturedLength()});
    }
    return matches;
}

QString replacedText(const QString& source,
                     const QList<MatchRange>& matches,
                     const QString& replacement)
{
    QString result = source;
    for (auto iterator = matches.crbegin(); iterator != matches.crend(); ++iterator)
        result.replace(iterator->start, iterator->length, replacement);
    return result;
}

QByteArray withoutUtf8Bom(const QByteArray& payload, bool& hadBom)
{
    hadBom = payload.startsWith("\xEF\xBB\xBF");
    return hadBom ? payload.mid(3) : payload;
}

bool decodeUtf8(const QByteArray& payload, QString& text, bool& hadBom)
{
    const QByteArray bytes = withoutUtf8Bom(payload, hadBom);
    const std::string content(
        bytes.constData(), static_cast<size_t>(bytes.size()));
    if (!Util::isUtf8(
            reinterpret_cast<const uint8_t*>(content.data()), content.size()))
    {
        text.clear();
        return false;
    }
    text = QString::fromUtf8(bytes);
    return true;
}

QByteArray encodedReplacement(const QString& text, bool hadBom)
{
    QByteArray bytes = text.toUtf8();
    if (hadBom)
        bytes.prepend("\xEF\xBB\xBF");
    return bytes;
}

QByteArray sha256(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

bool readBytes(const QString& filePath, QByteArray& bytes)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    bytes = file.readAll();
    return file.error() == QFileDevice::NoError;
}

void appendIssue(ScriptProjectSearchReport& report,
                 const QString& filePath,
                 const QString& relativePath,
                 const QString& message)
{
    report.issues.append({filePath, relativePath, message});
}
}

bool ScriptProjectSearchFileResult::hasChanges() const
{
    return beforeText != afterText;
}

ScriptProjectSearchReport ScriptProjectSearch::scan(
    const QString& activeContentRoot,
    const ScriptProjectSearchOptions& options,
    const ProgressCallback& progressCallback,
    const CancelCallback& cancelCallback)
{
    ScriptProjectSearchReport report;
    QElapsedTimer timer;
    timer.start();

    report.rootPath =
        EditorAssetPath::normalizedAbsolutePath(activeContentRoot);
    if (!QFileInfo(report.rootPath).isDir())
    {
        report.validationError = translated(QT_TRANSLATE_NOOP(
            "ScriptProjectSearch", "活动内容根不可用"));
        report.elapsedMilliseconds = timer.elapsed();
        return report;
    }
    if (options.query.isEmpty())
    {
        report.validationError = translated(QT_TRANSLATE_NOOP(
            "ScriptProjectSearch", "搜索文本不能为空"));
        report.elapsedMilliseconds = timer.elapsed();
        return report;
    }

    QRegularExpression expression;
    if (options.regularExpression)
    {
        expression = buildRegularExpression(options);
        if (!expression.isValid())
        {
            report.validationError = translated(QT_TRANSLATE_NOOP(
                "ScriptProjectSearch", "正则表达式无效：%1"))
                .arg(expression.errorString());
            report.elapsedMilliseconds = timer.elapsed();
            return report;
        }
        const QRegularExpressionMatch emptyMatch =
            expression.match(QString());
        if (emptyMatch.hasMatch() && emptyMatch.capturedLength() == 0)
        {
            report.validationError =
                translated(QT_TRANSLATE_NOOP(
                    "ScriptProjectSearch", "正则表达式不能匹配空字符串"));
            report.elapsedMilliseconds = timer.elapsed();
            return report;
        }
    }

    const QString scriptRoot = QDir(report.rootPath).filePath(
        QStringLiteral("script"));
    if (!QFileInfo(scriptRoot).isDir())
    {
        report.validationError = translated(QT_TRANSLATE_NOOP(
            "ScriptProjectSearch", "活动内容根没有 script 目录"));
        report.elapsedMilliseconds = timer.elapsed();
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
            report.elapsedMilliseconds = timer.elapsed();
            return report;
        }
        const QString filePath = iterator.next();
        if (hasScriptTextExtension(iterator.fileInfo()))
            files.append(filePath);
    }
    std::sort(files.begin(), files.end(),
        [](const QString& left, const QString& right)
        {
            const int insensitive = QString::compare(
                left, right, Qt::CaseInsensitive);
            return insensitive == 0
                ? QString::compare(left, right, Qt::CaseSensitive) < 0
                : insensitive < 0;
        });
    report.totalFiles = files.size();

    for (int fileIndex = 0; fileIndex < files.size(); ++fileIndex)
    {
        if (cancelCallback && cancelCallback())
        {
            report.cancelled = true;
            break;
        }

        const QString filePath = files[fileIndex];
        const QString relativePath = normalizedRelativePath(
            report.rootPath, filePath);
        if (progressCallback &&
            (fileIndex == 0 || fileIndex + 1 == files.size() ||
             fileIndex % 64 == 0))
        {
            progressCallback(fileIndex + 1, files.size(), relativePath);
        }

        QByteArray payload;
        if (!readBytes(filePath, payload))
        {
            appendIssue(report, filePath, relativePath,
                translated(QT_TRANSLATE_NOOP(
                    "ScriptProjectSearch", "无法读取脚本文件")));
            report.scannedFiles++;
            continue;
        }
        report.scannedFiles++;

        const std::string rawContent(
            payload.constData(), static_cast<size_t>(payload.size()));
        if (!LuaScriptSyntaxValidator::shouldValidateScriptFile(
                report.rootPath, filePath, rawContent))
        {
            report.skippedFiles++;
            continue;
        }
        report.scriptFiles++;

        QString beforeText;
        bool hadBom = false;
        if (!decodeUtf8(payload, beforeText, hadBom))
        {
            appendIssue(report, filePath, relativePath,
                translated(QT_TRANSLATE_NOOP(
                    "ScriptProjectSearch", "脚本不是有效的 UTF-8 文本")));
            continue;
        }

        QList<MatchRange> matches;
        if (options.regularExpression)
        {
            bool zeroLengthMatch = false;
            matches = regularExpressionMatches(
                beforeText, expression, zeroLengthMatch);
            if (zeroLengthMatch)
            {
                report.validationError =
                    translated(QT_TRANSLATE_NOOP(
                        "ScriptProjectSearch",
                        "正则表达式产生了零长度匹配：%1"))
                        .arg(relativePath);
                report.files.clear();
                report.matchingFiles = 0;
                report.totalMatches = 0;
                break;
            }
        }
        else
        {
            matches = literalMatches(beforeText, options);
        }

        if (matches.isEmpty())
            continue;

        ScriptProjectSearchFileResult fileResult;
        fileResult.filePath =
            EditorAssetPath::normalizedAbsolutePath(filePath);
        fileResult.relativePath = relativePath;
        fileResult.beforeText = beforeText;
        fileResult.afterText = replacedText(
            beforeText, matches, options.replacement);
        fileResult.originalSha256 = sha256(payload);
        fileResult.replacementBytes = encodedReplacement(
            fileResult.afterText, hadBom);
        fileResult.matchCount = matches.size();
        report.files.append(fileResult);
        report.matchingFiles++;
        report.totalMatches += fileResult.matchCount;
    }

    report.elapsedMilliseconds = timer.elapsed();
    return report;
}

ScriptProjectReplaceResult ScriptProjectSearch::publish(
    const QString& activeContentRoot,
    const ScriptProjectSearchReport& report,
    const QStringList& selectedFilePaths,
    const QSet<QString>& blockedFilePaths)
{
    ScriptProjectReplaceResult result;
    const QString rootPath =
        EditorAssetPath::normalizedAbsolutePath(activeContentRoot);
    if (!QFileInfo(rootPath).isDir() ||
        EditorAssetPath::comparisonKey(rootPath) !=
            EditorAssetPath::comparisonKey(report.rootPath))
    {
        result.errorMessage = translated(QT_TRANSLATE_NOOP(
            "ScriptProjectSearch", "活动内容根已变化，请重新搜索"));
        return result;
    }
    if (selectedFilePaths.isEmpty())
    {
        result.errorMessage = translated(QT_TRANSLATE_NOOP(
            "ScriptProjectSearch", "没有选择要替换的文件"));
        return result;
    }

    QHash<QString, const ScriptProjectSearchFileResult*> filesByKey;
    for (const ScriptProjectSearchFileResult& file : report.files)
    {
        filesByKey.insert(
            EditorAssetPath::comparisonKey(file.filePath), &file);
    }
    QSet<QString> blockedKeys;
    for (const QString& blockedPath : blockedFilePaths)
        blockedKeys.insert(EditorAssetPath::comparisonKey(blockedPath));

    QList<const ScriptProjectSearchFileResult*> selectedFiles;
    QSet<QString> selectedKeys;
    for (const QString& selectedPath : selectedFilePaths)
    {
        const QString key = EditorAssetPath::comparisonKey(selectedPath);
        if (selectedKeys.contains(key))
        {
            result.errorMessage = translated(QT_TRANSLATE_NOOP(
                "ScriptProjectSearch", "选择中包含重复文件：%1"))
                .arg(selectedPath);
            return result;
        }
        selectedKeys.insert(key);
        const ScriptProjectSearchFileResult* file = filesByKey.value(key);
        if (!file || !file->hasChanges() ||
            !EditorAssetPath::isInside(rootPath, file->filePath))
        {
            result.errorMessage = translated(QT_TRANSLATE_NOOP(
                "ScriptProjectSearch", "选择包含不可发布的文件：%1"))
                .arg(selectedPath);
            return result;
        }
        if (blockedKeys.contains(key))
        {
            result.errorMessage = translated(QT_TRANSLATE_NOOP(
                "ScriptProjectSearch",
                "文件已在编辑器中打开，请关闭后重新搜索：%1"))
                .arg(file->relativePath);
            return result;
        }
        selectedFiles.append(file);
    }

    QList<QByteArray> currentFileBytes;
    currentFileBytes.reserve(selectedFiles.size());
    for (const ScriptProjectSearchFileResult* file : selectedFiles)
    {
        QByteArray currentBytes;
        if (!readBytes(file->filePath, currentBytes))
        {
            result.errorMessage = translated(QT_TRANSLATE_NOOP(
                "ScriptProjectSearch", "发布前无法读取文件：%1"))
                .arg(file->relativePath);
            return result;
        }
        if (sha256(currentBytes) != file->originalSha256)
        {
            result.errorMessage = translated(QT_TRANSLATE_NOOP(
                "ScriptProjectSearch",
                "文件在搜索后已变化，请重新搜索：%1"))
                .arg(file->relativePath);
            return result;
        }
        currentFileBytes.append(std::move(currentBytes));
    }

    DurableFileTransaction transaction(rootPath);
    QString transactionMessage;
    for (int index = 0; index < selectedFiles.size(); ++index)
    {
        const ScriptProjectSearchFileResult* file = selectedFiles[index];
        if (!transaction.addBytesWriteChecked(
                file->filePath,
                file->replacementBytes,
                true,
                currentFileBytes[index],
                transactionMessage))
        {
            result.errorMessage = translated(QT_TRANSLATE_NOOP(
                "ScriptProjectSearch", "无法准备脚本替换事务：%1"))
                .arg(transactionMessage);
            return result;
        }
    }
    if (!transaction.commit(transactionMessage))
    {
        result.errorMessage = translated(QT_TRANSLATE_NOOP(
            "ScriptProjectSearch", "脚本替换事务失败：%1"))
            .arg(transactionMessage);
        return result;
    }

    result.success = true;
    result.replacedFiles = selectedFiles.size();
    for (const ScriptProjectSearchFileResult* file : selectedFiles)
    {
        result.replacements += file->matchCount;
        result.replacedFilePaths.append(file->filePath);
    }
    result.warningMessage = transactionMessage;
    return result;
}
