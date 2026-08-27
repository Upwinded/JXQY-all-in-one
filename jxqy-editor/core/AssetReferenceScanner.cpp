#include "AssetReferenceScanner.h"

#include "EditorAssetPath.h"
#include "ImageResourceCandidates.h"
#include "LuaLexer.h"
#include "LuaScriptSyntaxValidator.h"
#include "Util.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
struct IniEntry
{
    QString section;
    QString key;
    QString value;
    int lineNumber = 0;
};

enum class IniShape
{
    None,
    Npc,
    Object
};

QString normalizedRelativePath(const QString& rootPath, const QString& filePath)
{
    QString relativePath = QDir(rootPath).relativeFilePath(filePath);
    relativePath.replace('\\', '/');
    return relativePath;
}

bool hasSupportedIniExtension(const QFileInfo& fileInfo)
{
    const QString suffix = fileInfo.suffix();
    return suffix.compare(QStringLiteral("ini"), Qt::CaseInsensitive) == 0 ||
        suffix.compare(QStringLiteral("npc"), Qt::CaseInsensitive) == 0 ||
        suffix.compare(QStringLiteral("obj"), Qt::CaseInsensitive) == 0;
}

bool isScriptTextPath(const QString& relativePath, const QFileInfo& fileInfo)
{
    const QString normalized = relativePath.toLower();
    const QString suffix = fileInfo.suffix();
    return normalized.startsWith(QStringLiteral("script/")) &&
        (suffix.compare(QStringLiteral("lua"), Qt::CaseInsensitive) == 0 ||
         suffix.compare(QStringLiteral("txt"), Qt::CaseInsensitive) == 0);
}

QByteArray withoutUtf8Bom(QByteArray payload)
{
    if (payload.startsWith("\xEF\xBB\xBF"))
        payload.remove(0, 3);
    return payload;
}

bool decodeUtf8(const QByteArray& payload, QString& text)
{
    const QByteArray bytes = withoutUtf8Bom(payload);
    const std::string content(bytes.constData(), static_cast<size_t>(bytes.size()));
    if (!Util::isUtf8(
            reinterpret_cast<const uint8_t*>(content.data()), content.size()))
    {
        text.clear();
        return false;
    }
    text = QString::fromUtf8(bytes);
    return true;
}

QList<IniEntry> parseIniEntries(const QString& text)
{
    QList<IniEntry> entries;
    QString currentSection;
    const QStringList lines = text.split('\n');
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
    {
        const QString trimmed = lines[lineIndex].trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(';') || trimmed.startsWith('#'))
            continue;
        if (trimmed.startsWith('[') && trimmed.endsWith(']'))
        {
            currentSection = trimmed.mid(1, trimmed.size() - 2).trimmed();
            continue;
        }
        const qsizetype separator = trimmed.indexOf('=');
        if (separator <= 0)
            continue;
        IniEntry entry;
        entry.section = currentSection;
        entry.key = trimmed.left(separator).trimmed();
        entry.value = trimmed.mid(separator + 1).trimmed();
        entry.lineNumber = lineIndex + 1;
        entries.append(entry);
    }
    return entries;
}

QStringList prefixedCandidates(const QString& reference,
                               const QStringList& folders)
{
    QString normalized;
    if (!EditorAssetPath::normalizeResourcePath(reference, normalized))
        return {};
    QStringList candidates;
    for (QString folder : folders)
    {
        folder.replace('\\', '/');
        while (folder.endsWith('/'))
            folder.chop(1);
        candidates.append(folder + '/' + normalized);
    }
    return candidates;
}

QStringList directCandidates(const QString& reference)
{
    QString normalized;
    if (!EditorAssetPath::normalizeResourcePath(reference, normalized))
        return {};
    return {normalized};
}

QStringList npcIniCandidates(const QString& reference)
{
    QString normalized;
    if (!EditorAssetPath::normalizeResourcePath(reference, normalized))
        return {};

    QStringList candidates{QStringLiteral("ini/npcres/") + normalized};
    if (!normalized.contains('/'))
        candidates.append(QStringLiteral("ini/npc/") + normalized);
    return candidates;
}

QStringList soundCandidates(const QString& reference)
{
    QString normalized;
    if (!EditorAssetPath::normalizeResourcePath(reference, normalized))
        return {};
    QString candidate = normalized;
    if (!candidate.startsWith(QStringLiteral("sound/"), Qt::CaseInsensitive))
        candidate.prepend(QStringLiteral("sound/"));

    QStringList candidates{candidate};
    const QFileInfo fileInfo(candidate);
    if (fileInfo.suffix().isEmpty())
        candidates.append(candidate + QStringLiteral(".wav"));
    else if (fileInfo.suffix().compare(QStringLiteral("xnb"), Qt::CaseInsensitive) == 0)
        candidates.append(candidate.left(candidate.size() - 3) + QStringLiteral("wav"));
    return candidates;
}

QStringList entityImageCandidates(const QString& reference, bool isNpc)
{
    const std::vector<std::string> rawCandidates =
        buildEditorEntityImageCandidates(reference.toUtf8().constData(), isNpc);
    QStringList candidates;
    for (const std::string& rawCandidate : rawCandidates)
        candidates.append(QString::fromUtf8(rawCandidate));
    return candidates;
}

void appendOccurrence(AssetReferenceScanReport& report,
                      const QString& sourceFilePath,
                      const QString& sourceRelativePath,
                      const IniEntry& entry,
                      const QString& reference,
                      const QStringList& candidates,
                      const QList<ResourceContentRoot>& orderedRoots)
{
    if (reference.trimmed().isEmpty())
        return;

    AssetReferenceOccurrence occurrence;
    occurrence.kind = AssetReferenceKind::StaticIni;
    occurrence.sourceFilePath = sourceFilePath;
    occurrence.sourceRelativePath = sourceRelativePath;
    occurrence.lineNumber = entry.lineNumber;
    occurrence.section = entry.section;
    occurrence.field = entry.key;
    occurrence.reference = reference.trimmed();
    report.staticReferences++;

    QStringList normalizedCandidates;
    for (const QString& candidate : candidates)
    {
        QString normalized;
        if (EditorAssetPath::normalizeResourcePath(candidate, normalized) &&
            !normalizedCandidates.contains(normalized, Qt::CaseInsensitive))
        {
            normalizedCandidates.append(normalized);
        }
    }
    if (normalizedCandidates.isEmpty())
    {
        occurrence.status = AssetReferenceStatus::Invalid;
        report.occurrences.append(occurrence);
        return;
    }

    for (const QString& candidate : normalizedCandidates)
    {
        for (const ResourceContentRoot& root : orderedRoots)
        {
            if (!root.available || !QFileInfo(root.rootPath).isDir())
                continue;
            QString absolutePath;
            if (EditorAssetPath::resolveLogicalResourcePath(
                    root.rootPath,
                    candidate,
                    absolutePath) &&
                QFileInfo(absolutePath).isFile())
            {
                occurrence.status = AssetReferenceStatus::Resolved;
                occurrence.resolvedFilePath = absolutePath;
                report.occurrences.append(occurrence);
                return;
            }
        }
    }

    occurrence.status = AssetReferenceStatus::Missing;
    report.missingReferences++;
    report.occurrences.append(occurrence);
}

void appendFlyIniList(AssetReferenceScanReport& report,
                      const QString& sourceFilePath,
                      const QString& sourceRelativePath,
                      const IniEntry& entry,
                      const QList<ResourceContentRoot>& orderedRoots)
{
    QString canonical = entry.value;
    canonical.replace(QChar(0xFF1B), QChar(';'));
    const QStringList items = canonical.split(';', Qt::SkipEmptyParts);
    for (QString item : items)
    {
        item = item.trimmed();
        item.replace(QChar(0xFF1A), QChar(':'));
        QString fileName = item;
        const qsizetype colon = item.lastIndexOf(':');
        if (colon >= 0)
        {
            bool distanceOk = false;
            item.mid(colon + 1).trimmed().toInt(&distanceOk);
            if (!distanceOk || item.indexOf(':') != colon)
            {
                appendOccurrence(report, sourceFilePath, sourceRelativePath,
                    entry, item, {}, orderedRoots);
                continue;
            }
            fileName = item.left(colon).trimmed();
        }
        appendOccurrence(report, sourceFilePath, sourceRelativePath, entry,
            fileName, prefixedCandidates(fileName, {QStringLiteral("ini/magic")}),
            orderedRoots);
    }
}

bool sectionMatchesNumberedPrefix(const QString& section,
                                  const QString& prefix)
{
    if (!section.startsWith(prefix, Qt::CaseInsensitive))
        return false;
    bool ok = false;
    section.mid(prefix.size()).toInt(&ok);
    return ok;
}

void scanEntityEntries(AssetReferenceScanReport& report,
                       const QList<IniEntry>& entries,
                       IniShape shape,
                       const QString& sourceFilePath,
                       const QString& sourceRelativePath,
                       const QList<ResourceContentRoot>& orderedRoots)
{
    static const QSet<QString> scriptFields = {
        QStringLiteral("scriptfile"), QStringLiteral("scriptfileright"),
        QStringLiteral("timerscriptfile")};
    for (const IniEntry& entry : entries)
    {
        if (entry.section.isEmpty() || entry.value.isEmpty())
            continue;
        const QString key = entry.key.toLower();
        QStringList candidates;
        if (shape == IniShape::Npc)
        {
            if (key == QStringLiteral("npcini"))
                candidates = npcIniCandidates(entry.value);
            else if (scriptFields.contains(key) || key == QStringLiteral("deathscript"))
                candidates = prefixedCandidates(entry.value, {QStringLiteral("script")});
            else if (key == QStringLiteral("bodyini") || key == QStringLiteral("dropini"))
                candidates = prefixedCandidates(entry.value, {QStringLiteral("ini/obj")});
            else if (key == QStringLiteral("flyini") ||
                     key == QStringLiteral("flyini2") ||
                     key == QStringLiteral("magicini"))
                candidates = prefixedCandidates(entry.value, {QStringLiteral("ini/magic")});
            else if (key == QStringLiteral("flyinis"))
            {
                appendFlyIniList(report, sourceFilePath, sourceRelativePath,
                    entry, orderedRoots);
                continue;
            }
            else
                continue;
        }
        else
        {
            if (key == QStringLiteral("objfile") || key == QStringLiteral("objfilemovie"))
                candidates = prefixedCandidates(entry.value, {QStringLiteral("ini/objres")});
            else if (scriptFields.contains(key))
                candidates = prefixedCandidates(entry.value, {QStringLiteral("script")});
            else if (key == QStringLiteral("revivenpcini"))
                candidates = prefixedCandidates(entry.value, {QStringLiteral("ini/npc")});
            else if (key == QStringLiteral("wavfile"))
                candidates = soundCandidates(entry.value);
            else
                continue;
        }
        appendOccurrence(report, sourceFilePath, sourceRelativePath, entry,
            entry.value, candidates, orderedRoots);
    }
}

void scanIniEntries(AssetReferenceScanReport& report,
                    const QList<IniEntry>& entries,
                    const QString& sourceFilePath,
                    const QString& sourceRelativePath,
                    const QList<ResourceContentRoot>& orderedRoots)
{
    const QString lowerPath = sourceRelativePath.toLower();
    const QFileInfo fileInfo(sourceRelativePath);
    if (lowerPath.startsWith(QStringLiteral("ini/npcres/")))
    {
        for (const IniEntry& entry : entries)
        {
            const QString key = entry.key.toLower();
            if (entry.section.isEmpty() || entry.value.isEmpty())
                continue;
            if (key == QStringLiteral("image") || key == QStringLiteral("shade"))
                appendOccurrence(report, sourceFilePath, sourceRelativePath, entry,
                    entry.value, entityImageCandidates(entry.value, true), orderedRoots);
            else if (key == QStringLiteral("sound"))
                appendOccurrence(report, sourceFilePath, sourceRelativePath, entry,
                    entry.value, soundCandidates(entry.value), orderedRoots);
        }
        return;
    }
    if (lowerPath.startsWith(QStringLiteral("ini/objres/")))
    {
        for (const IniEntry& entry : entries)
        {
            if (entry.section.compare(QStringLiteral("common"),
                                      Qt::CaseInsensitive) != 0 ||
                entry.value.isEmpty())
            {
                continue;
            }
            const QString key = entry.key.toLower();
            if (key == QStringLiteral("image") || key == QStringLiteral("shade") ||
                key == QStringLiteral("animation"))
            {
                appendOccurrence(report, sourceFilePath, sourceRelativePath, entry,
                    entry.value, entityImageCandidates(entry.value, false), orderedRoots);
            }
            else if (key == QStringLiteral("sound"))
            {
                appendOccurrence(report, sourceFilePath, sourceRelativePath, entry,
                    entry.value, soundCandidates(entry.value), orderedRoots);
            }
        }
        return;
    }
    if (lowerPath.startsWith(QStringLiteral("ini/ui/")))
    {
        const bool isMenu = lowerPath.endsWith(QStringLiteral(".menu.ini"));
        static const QSet<QString> imageFields = {
            QStringLiteral("image"), QStringLiteral("bitmap"),
            QStringLiteral("baseimage"), QStringLiteral("thumbimage"),
            QStringLiteral("slidebtn"), QStringLiteral("iconimage"),
            QStringLiteral("indicateimage"), QStringLiteral("backimage1"),
            QStringLiteral("backimage2")};
        for (const IniEntry& entry : entries)
        {
            if (entry.value.isEmpty())
                continue;
            const QString key = entry.key.toLower();
            if (isMenu)
            {
                const bool structureReference =
                    (entry.section.compare(QStringLiteral("menu"), Qt::CaseInsensitive) == 0 &&
                     key == QStringLiteral("window")) ||
                    ((sectionMatchesNumberedPrefix(entry.section, QStringLiteral("component")) ||
                      sectionMatchesNumberedPrefix(entry.section, QStringLiteral("submenu"))) &&
                     key == QStringLiteral("file"));
                if (structureReference)
                    appendOccurrence(report, sourceFilePath, sourceRelativePath, entry,
                        entry.value, directCandidates(entry.value), orderedRoots);
            }
            else if (entry.section.compare(QStringLiteral("init"), Qt::CaseInsensitive) == 0)
            {
                if (imageFields.contains(key))
                    appendOccurrence(report, sourceFilePath, sourceRelativePath, entry,
                        entry.value, directCandidates(entry.value), orderedRoots);
                else if (key == QStringLiteral("sound"))
                    appendOccurrence(report, sourceFilePath, sourceRelativePath, entry,
                        entry.value, soundCandidates(entry.value), orderedRoots);
            }
        }
        return;
    }

    IniShape shape = IniShape::None;
    if (fileInfo.suffix().compare(QStringLiteral("npc"), Qt::CaseInsensitive) == 0)
        shape = IniShape::Npc;
    else if (fileInfo.suffix().compare(QStringLiteral("obj"), Qt::CaseInsensitive) == 0)
        shape = IniShape::Object;
    else
    {
        bool hasNpcIdentity = false;
        bool hasObjectIdentity = false;
        for (const IniEntry& entry : entries)
        {
            hasNpcIdentity = hasNpcIdentity ||
                entry.key.compare(QStringLiteral("NPCIni"), Qt::CaseInsensitive) == 0;
            hasObjectIdentity = hasObjectIdentity ||
                entry.key.compare(QStringLiteral("ObjFile"), Qt::CaseInsensitive) == 0;
        }
        if (hasNpcIdentity != hasObjectIdentity)
            shape = hasNpcIdentity ? IniShape::Npc : IniShape::Object;
    }
    if (shape != IniShape::None)
        scanEntityEntries(report, entries, shape, sourceFilePath,
            sourceRelativePath, orderedRoots);
}

bool looksLikeResourceLiteral(const QString& value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.contains('/') || trimmed.contains('\\'))
        return true;
    static const QStringList extensions = {
        QStringLiteral(".ini"), QStringLiteral(".npc"), QStringLiteral(".obj"),
        QStringLiteral(".lua"), QStringLiteral(".txt"), QStringLiteral(".mpc"),
        QStringLiteral(".asf"), QStringLiteral(".shd"), QStringLiteral(".imp"),
        QStringLiteral(".img"), QStringLiteral(".pic"), QStringLiteral(".png"),
        QStringLiteral(".jpg"), QStringLiteral(".jpeg"), QStringLiteral(".bmp"),
        QStringLiteral(".gif"), QStringLiteral(".webp"), QStringLiteral(".tga"),
        QStringLiteral(".wav"), QStringLiteral(".mp3"), QStringLiteral(".ogg")};
    for (const QString& extension : extensions)
    {
        if (trimmed.endsWith(extension, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

void scanLuaTokens(AssetReferenceScanReport& report,
                   const QString& sourceFilePath,
                   const QString& sourceRelativePath,
                   const QString& text)
{
    const LuaLexResult lexResult =
        LuaLexer::lex(text, sourceRelativePath);
    QList<const LuaToken*> tokens;
    tokens.reserve(lexResult.tokens.size());
    for (const LuaToken& token : lexResult.tokens)
    {
        if (LuaLexer::isSignificant(token))
            tokens.append(&token);
    }

    for (int index = 0; index < tokens.size(); ++index)
    {
        const LuaToken& token = *tokens[index];
        if (token.kind != LuaTokenKind::String ||
            !looksLikeResourceLiteral(
                token.decodedText))
        {
            continue;
        }
        const bool dynamic =
            (index > 0 &&
             tokens[index - 1]->kind ==
                 LuaTokenKind::Symbol &&
             tokens[index - 1]->text ==
                 QStringLiteral("..")) ||
            (index + 1 < tokens.size() &&
             tokens[index + 1]->kind ==
                 LuaTokenKind::Symbol &&
             tokens[index + 1]->text ==
                 QStringLiteral(".."));
        AssetReferenceOccurrence occurrence;
        occurrence.kind = dynamic ? AssetReferenceKind::LuaDynamic
                                  : AssetReferenceKind::LuaLiteral;
        occurrence.status = dynamic ? AssetReferenceStatus::Dynamic
                                    : AssetReferenceStatus::Candidate;
        occurrence.sourceFilePath = sourceFilePath;
        occurrence.sourceRelativePath = sourceRelativePath;
        occurrence.lineNumber =
            token.range.start.line;
        occurrence.field = dynamic
            ? QCoreApplication::translate(
                "AssetReferenceScanner", "Lua 动态拼接")
            : QCoreApplication::translate(
                "AssetReferenceScanner", "Lua 字符串");
        occurrence.reference =
            token.decodedText;
        report.occurrences.append(occurrence);
        if (dynamic)
            report.dynamicExpressions++;
        else
            report.luaCandidates++;
    }
    for (const LuaLexWarning& warning :
         lexResult.warnings)
    {
        report.issues.append(
            {
                sourceFilePath,
                sourceRelativePath,
                warning.range.start.line,
                warning.message
            });
    }
}

void appendIssue(AssetReferenceScanReport& report,
                 const QString& sourceFilePath,
                 const QString& sourceRelativePath,
                 const QString& message)
{
    report.issues.append({sourceFilePath, sourceRelativePath, 0, message});
}
}

AssetReferenceScanReport AssetReferenceScanner::scan(
    const QString& activeContentRoot,
    const QList<ResourceContentRoot>& orderedContentRoots,
    const ProgressCallback& progressCallback,
    const CancelCallback& cancelCallback)
{
    AssetReferenceScanReport report;
    QElapsedTimer timer;
    timer.start();

    const QString rootPath = EditorAssetPath::normalizedAbsolutePath(activeContentRoot);
    if (!QFileInfo(rootPath).isDir())
    {
        appendIssue(report, rootPath, QString(), QCoreApplication::translate(
            "AssetReferenceScanner", "活动内容根不可用"));
        report.elapsedMilliseconds = timer.elapsed();
        return report;
    }

    QList<ResourceContentRoot> orderedRoots = orderedContentRoots;
    if (orderedRoots.isEmpty())
    {
        ResourceContentRoot root;
        root.rootPath = rootPath;
        root.kind = ResourceContentRoot::Kind::Local;
        root.available = true;
        orderedRoots.append(root);
    }

    QStringList files;
    QDirIterator iterator(
        rootPath,
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
        const QFileInfo fileInfo = iterator.fileInfo();
        const QString relativePath = normalizedRelativePath(rootPath, filePath);
        if (hasSupportedIniExtension(fileInfo) ||
            isScriptTextPath(relativePath, fileInfo))
        {
            files.append(filePath);
        }
    }
    files.sort(Qt::CaseInsensitive);
    report.totalFiles = files.size();

    for (int fileIndex = 0; fileIndex < files.size(); ++fileIndex)
    {
        if (cancelCallback && cancelCallback())
        {
            report.cancelled = true;
            break;
        }
        const QString filePath = files[fileIndex];
        const QString relativePath = normalizedRelativePath(rootPath, filePath);
        if (progressCallback &&
            (fileIndex == 0 || fileIndex + 1 == files.size() || fileIndex % 64 == 0))
        {
            progressCallback(fileIndex + 1, files.size(), relativePath);
        }

        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly))
        {
            appendIssue(report, filePath, relativePath, QCoreApplication::translate(
                "AssetReferenceScanner", "无法读取文件"));
            report.scannedFiles++;
            continue;
        }
        const QByteArray payload = file.readAll();
        QString text;
        if (!decodeUtf8(payload, text))
        {
            appendIssue(report, filePath, relativePath, QCoreApplication::translate(
                "AssetReferenceScanner", "文本不是有效的 UTF-8"));
            report.scannedFiles++;
            continue;
        }

        const QFileInfo fileInfo(filePath);
        if (isScriptTextPath(relativePath, fileInfo))
        {
            const QByteArray scriptBytes = withoutUtf8Bom(payload);
            const std::string script(scriptBytes.constData(),
                                     static_cast<size_t>(scriptBytes.size()));
            if (LuaScriptSyntaxValidator::shouldValidateScriptFile(
                    rootPath, filePath, script))
            {
                report.scriptFiles++;
                scanLuaTokens(report, filePath, relativePath, text);
            }
        }
        else
        {
            report.iniFiles++;
            scanIniEntries(report, parseIniEntries(text), filePath,
                relativePath, orderedRoots);
        }
        report.scannedFiles++;
    }

    report.elapsedMilliseconds = timer.elapsed();
    return report;
}
