#include "JxAssetMigrator.h"
#include "AuthoringMutationGate.h"
#include "GameProfile.h"
#include "../../src/Resource/ResourceCatalog.h"
#include "INIFileEditor.h"
#include "LegacyTextDecoder.h"
#include "ScriptConverter.h"
#include "LuaScriptSyntaxValidator.h"
#include "MapConverter.h"
#include "PicFileEditor.h"
#include "Util.h"

#include <QDir>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QImage>
#include <QHash>
#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <sstream>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

QString assetResourceTypeId(AssetResourceType type)
{
    switch (type)
    {
    case AssetResourceType::All: return QStringLiteral("all");
    case AssetResourceType::Scripts: return QStringLiteral("scripts");
    case AssetResourceType::Maps: return QStringLiteral("maps");
    case AssetResourceType::Images: return QStringLiteral("images");
    case AssetResourceType::Audio: return QStringLiteral("audio");
    case AssetResourceType::Other: return QStringLiteral("other");
    }
    return QString();
}

bool parseAssetResourceType(const QString& id, AssetResourceType& type)
{
    const QString normalized = id.trimmed().toLower();
    const QList<AssetResourceType> cliTypes = {
        AssetResourceType::All,
        AssetResourceType::Scripts,
        AssetResourceType::Maps,
        AssetResourceType::Images,
        AssetResourceType::Audio
    };
    for (AssetResourceType candidate : cliTypes)
    {
        if (normalized == assetResourceTypeId(candidate))
        {
            type = candidate;
            return true;
        }
    }
    return false;
}

QList<AssetResourceType> assetResourceDomainTypes()
{
    return {
        AssetResourceType::Scripts,
        AssetResourceType::Maps,
        AssetResourceType::Images,
        AssetResourceType::Audio,
        AssetResourceType::Other
    };
}

QString assetMigrationFileActionId(
    AssetMigrationFileAction action)
{
    switch (action)
    {
    case AssetMigrationFileAction::Copy:
        return QStringLiteral("copy");
    case AssetMigrationFileAction::Convert:
        return QStringLiteral("convert");
    case AssetMigrationFileAction::Skip:
        return QStringLiteral("skip");
    case AssetMigrationFileAction::Fail:
        return QStringLiteral("fail");
    }
    return QStringLiteral("fail");
}

namespace
{
QString canonicalMigrationPathKey(
    QString path)
{
    path.replace(
        QLatin1Char('\\'),
        QLatin1Char('/'));
    return QDir::cleanPath(path).
        normalized(
            QString::NormalizationForm_C).
        toCaseFolded();
}

bool isRuntimeEntryJpeg(QString relativePath)
{
    relativePath.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (relativePath.startsWith(QLatin1Char('/')))
        relativePath.remove(0, 1);
    relativePath = QDir::cleanPath(relativePath).toLower();
    if (!relativePath.endsWith(QStringLiteral(".jpg")) &&
        !relativePath.endsWith(QStringLiteral(".jpeg")))
    {
        return false;
    }
    return relativePath == QStringLiteral("cover.jpg") ||
        relativePath == QStringLiteral("cover.jpeg") ||
        relativePath.startsWith(QStringLiteral("asf/ui/title/")) ||
        relativePath.startsWith(QStringLiteral("mpc/ui/title/"));
}

QString replaceJpegExtensionWithPng(QString path)
{
    static const QRegularExpression jpegExtension(
        QStringLiteral("\\.jpe?g$"),
        QRegularExpression::CaseInsensitiveOption);
    path.replace(jpegExtension, QStringLiteral(".png"));
    return path;
}
}

QStringList assetMigrationOutputPathCollisionSources(
    const QList<QPair<QString, QString>>&
        sourceOutputPaths)
{
    QMap<QString, QStringList> ownersByOutput;
    for (const auto& sourceOutput :
         sourceOutputPaths)
    {
        const QString outputKey =
            canonicalMigrationPathKey(
                sourceOutput.second);
        ownersByOutput[outputKey].append(
            sourceOutput.first);
    }

    QSet<QString> collisions;
    for (auto owners =
             ownersByOutput.cbegin();
         owners != ownersByOutput.cend();
         ++owners)
    {
        if (owners->size() < 2)
            continue;
        for (const QString& sourcePath :
             *owners)
        {
            collisions.insert(sourcePath);
        }
    }
    QStringList result =
        collisions.values();
    result.sort(Qt::CaseSensitive);
    return result;
}

namespace
{
const char* kMigrationMarkerFileName = ".jxqy_asset_migration_marker";

#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
JxAssetMigrator::FileSystemFaultInjector& fileSystemFaultInjector()
{
    thread_local JxAssetMigrator::FileSystemFaultInjector injector;
    return injector;
}
#endif

bool shouldFailFileSystemOperation(
    JxAssetMigrator::FileSystemOperation operation,
    const QString& sourcePath,
    const QString& targetPath = QString())
{
#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
    const auto& injector = fileSystemFaultInjector();
    return injector && injector(operation, sourcePath, targetPath);
#else
    (void)operation;
    (void)sourcePath;
    (void)targetPath;
    return false;
#endif
}

Qt::CaseSensitivity fileSystemPathCaseSensitivity()
{
#if defined(Q_OS_WIN)
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

QString pathWithCanonicalExistingAncestor(const QString& path)
{
    QString currentPath = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    QStringList missingParts;
    QFileInfo currentInfo(currentPath);
    while (!currentInfo.exists())
    {
        const QString fileName = currentInfo.fileName();
        const QString parentPath = QDir::cleanPath(currentInfo.absolutePath());
        if (fileName.isEmpty() || parentPath == currentPath)
            break;
        missingParts.prepend(fileName);
        currentPath = parentPath;
        currentInfo.setFile(currentPath);
    }

    QString resolvedPath = currentInfo.exists()
        ? currentInfo.canonicalFilePath()
        : currentPath;
    if (resolvedPath.isEmpty())
        resolvedPath = currentPath;
    for (const QString& part : missingParts)
        resolvedPath = QDir(resolvedPath).filePath(part);
    return QDir::cleanPath(resolvedPath);
}

QString lowerPathKey(QString path)
{
    return canonicalMigrationPathKey(
        std::move(path));
}

bool normalizeResourceTypes(const QList<AssetResourceType>& input,
    QList<AssetResourceType>& normalized)
{
    normalized.clear();
    if (input.isEmpty())
        return false;

    const bool selectsAll = input.contains(AssetResourceType::All);
    for (AssetResourceType type : input)
    {
        if (type == AssetResourceType::Other)
            return false;
        if (type != AssetResourceType::All &&
            type != AssetResourceType::Scripts &&
            type != AssetResourceType::Maps &&
            type != AssetResourceType::Images &&
            type != AssetResourceType::Audio)
        {
            return false;
        }
        if (selectsAll && type != AssetResourceType::All)
            return false;
    }

    if (selectsAll)
    {
        normalized.append(AssetResourceType::All);
        return true;
    }

    for (AssetResourceType type : assetResourceDomainTypes())
    {
        if (type != AssetResourceType::Other && input.contains(type))
            normalized.append(type);
    }
    return !normalized.isEmpty();
}

bool isCompleteProjectMigration(const QList<AssetResourceType>& resourceTypes)
{
    return resourceTypes.size() == 1 &&
        resourceTypes.first() == AssetResourceType::All;
}

AssetResourceType resourceTypeForRelativePath(const QString& relativePath)
{
    const QString path = lowerPathKey(relativePath);
    const QString root = path.section('/', 0, 0);
    if (root == QStringLiteral("script"))
        return AssetResourceType::Scripts;
    if (root == QStringLiteral("map"))
        return AssetResourceType::Maps;
    if (root == QStringLiteral("asf") || root == QStringLiteral("mpc") ||
        root == QStringLiteral("img"))
    {
        return AssetResourceType::Images;
    }
    if (root == QStringLiteral("music") || root == QStringLiteral("sound"))
        return AssetResourceType::Audio;
    return AssetResourceType::Other;
}

QStringList resourceTypeIds(const QList<AssetResourceType>& resourceTypes)
{
    QStringList ids;
    for (AssetResourceType type : resourceTypes)
        ids.append(assetResourceTypeId(type));
    return ids;
}

void initializeResourceDomainReport(AssetMigrationReport& report,
    const QList<AssetResourceType>& resourceTypes)
{
    report.completeProject = isCompleteProjectMigration(resourceTypes);
    report.selectedResourceTypes = resourceTypeIds(resourceTypes);
    for (AssetResourceType domain : assetResourceDomainTypes())
    {
        AssetResourceDomainReport domainReport;
        domainReport.selected = report.completeProject ||
            resourceTypes.contains(domain);
        report.resourceDomains.insert(assetResourceTypeId(domain), domainReport);
    }
}

AssetResourceDomainReport& domainReportFor(AssetMigrationReport& report,
    AssetResourceType domain)
{
    return report.resourceDomains[assetResourceTypeId(domain)];
}

void appendFileOutcome(
    AssetMigrationReport& report,
    const QString& sourcePath,
    const QString& outputPath,
    AssetResourceType domain,
    AssetMigrationFileAction action,
    const QString& reason,
    const QString& message = QString(),
    bool sourceScan = true,
    const QString& entryType = QStringLiteral("file"),
    const QString& outputSha256 = QString())
{
    AssetMigrationFileOutcome outcome;
    outcome.sourcePath = sourcePath;
    outcome.outputPath = outputPath;
    outcome.outputSha256 = outputSha256;
    outcome.domain = assetResourceTypeId(domain);
    outcome.entryType = entryType;
    outcome.action = action;
    outcome.reason = reason;
    outcome.message = message;
    outcome.sourceScan = sourceScan;
    report.fileOutcomes.append(std::move(outcome));
}

bool isFileSystemLink(const QFileInfo& information)
{
    if (information.isSymLink() ||
        information.isSymbolicLink())
    {
        return true;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
    if (information.isJunction())
        return true;
#endif
#ifdef Q_OS_WIN
    const std::wstring nativePath =
        QDir::toNativeSeparators(
            information.absoluteFilePath()).
            toStdWString();
    const DWORD attributes =
        GetFileAttributesW(
            nativePath.c_str());
    if (attributes !=
            INVALID_FILE_ATTRIBUTES &&
        (attributes &
         FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    {
        return true;
    }
#endif
    return false;
}

QString migrationEntryType(
    const QFileInfo& information)
{
    if (isFileSystemLink(information))
    {
        return information.isDir()
            ? QStringLiteral("directory-link")
            : QStringLiteral("file-link");
    }
    return information.isDir()
        ? QStringLiteral("directory")
        : QStringLiteral("file");
}

AssetMigrationFileOutcome* sourceOutcomeForOutput(
    AssetMigrationReport& report,
    const QString& outputPath)
{
    const QString outputKey = lowerPathKey(outputPath);
    for (auto outcome = report.fileOutcomes.rbegin();
         outcome != report.fileOutcomes.rend();
         ++outcome)
    {
        if (outcome->sourceScan &&
            lowerPathKey(outcome->outputPath) == outputKey)
        {
            return &*outcome;
        }
    }
    return nullptr;
}

void addDomainWrittenFiles(AssetMigrationReport& report,
    AssetResourceType domain, int previousWrittenFiles)
{
    const int writtenFiles = report.writtenFiles - previousWrittenFiles;
    if (writtenFiles > 0)
        domainReportFor(report, domain).writtenFiles += writtenFiles;
}

QStringList publishedPathsForResourceTypes(
    const QList<AssetResourceType>& resourceTypes)
{
    QStringList paths;
    for (AssetResourceType type : resourceTypes)
    {
        switch (type)
        {
        case AssetResourceType::Scripts:
            paths << QStringLiteral("script")
                  << QStringLiteral("talkindex.txt");
            break;
        case AssetResourceType::Maps:
            paths << QStringLiteral("map");
            break;
        case AssetResourceType::Images:
            paths << QStringLiteral("asf") << QStringLiteral("img") << QStringLiteral("mpc");
            break;
        case AssetResourceType::Audio:
            paths << QStringLiteral("music") << QStringLiteral("sound");
            break;
        case AssetResourceType::All:
        case AssetResourceType::Other:
            break;
        }
    }
    return paths;
}

bool isTextExtension(const QString& extension)
{
    return extension == "txt" || extension == "ini" ||
        extension == "npc" || extension == "obj";
}

bool isRuntimeTextPath(const QString& relativePath)
{
    const QString path = lowerPathKey(relativePath);
    const QString root = path.section('/', 0, 0);
    if (root == QStringLiteral("ini") ||
        root == QStringLiteral("script"))
    {
        return true;
    }
    return !path.contains(QLatin1Char('/')) &&
        (path == QStringLiteral("game_profile.ini") ||
         path == QStringLiteral("partneridx.ini") ||
         path == QStringLiteral("talkindex.txt"));
}

bool isRuntimeMapPath(const QString& relativePath)
{
    return lowerPathKey(relativePath).startsWith(
        QStringLiteral("map/"));
}

bool isRawImageExtension(const QString& extension)
{
    return extension == "mpc" || extension == "shd" || extension == "asf" ||
        extension == "pic" || extension == "img";
}

bool isConvertibleLegacyImageExtension(const QString& extension)
{
    return extension == "mpc" || extension == "shd" ||
        extension == "asf" || extension == "pic";
}

bool isTopLevelImgPath(const QString& relativePath)
{
    const QString normalized = lowerPathKey(relativePath);
    return normalized == QStringLiteral("img") ||
        normalized.startsWith(QStringLiteral("img/"));
}

bool isConvertibleScriptText(const QString& relativePath, const QString& extension)
{
    if (extension != "txt")
        return false;

    QString normalized = lowerPathKey(relativePath);
    return normalized.startsWith("script/");
}

bool isOrphanScriptText(const QString& relativePath)
{
    QString normalized = lowerPathKey(relativePath);
    return normalized.startsWith(QString::fromUtf8("script/未找到/")) ||
        normalized.startsWith(QString::fromUtf8("script/未找到的/"));
}

bool isLegacyScriptDocumentation(const QString& relativePath)
{
    const QString fileName = QFileInfo(relativePath).fileName();
    return fileName.compare(QString::fromUtf8("Help编写脚本文件.txt"),
               Qt::CaseInsensitive) == 0 ||
        fileName.compare(QString::fromUtf8("script错误汇总.txt"),
            Qt::CaseInsensitive) == 0;
}

QString joinRelativePath(const QStringList& parts)
{
    QString result;
    for (const QString& part : parts)
    {
        if (part.isEmpty())
            continue;
        if (!result.isEmpty())
            result += "/";
        result += part;
    }
    return result;
}

QString lowercaseAsciiPath(QString path)
{
    for (int index = 0; index < path.size(); ++index)
    {
        const ushort character = path.at(index).unicode();
        if (character >= 'A' && character <= 'Z')
        {
            path[index] = QChar(
                static_cast<ushort>(character + ('a' - 'A')));
        }
    }
    return path;
}

QString appendPath(const QString& base, const QString& relative)
{
    QDir dir(base);
    return QDir::cleanPath(dir.filePath(relative));
}

bool ensureParentDirectory(const QString& filePath)
{
    return QDir().mkpath(QFileInfo(filePath).absolutePath());
}

QString normalizedRelativePath(QString path)
{
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (path.startsWith(QStringLiteral("./")))
        path.remove(0, 2);
    return path;
}

bool isMigrationGeneratedArtifact(const QString& relativePath)
{
    const QString path = lowerPathKey(relativePath);
    return path == QStringLiteral("migration_report.txt") ||
        path == QStringLiteral("migration_report.json") ||
        path == QString::fromLatin1(kMigrationMarkerFileName);
}

// This is the first published JSON migration-marker format. Only increment it
// after an incompatible format change has actually shipped.
constexpr int MigrationMarkerSchemaVersion = 1;
constexpr qint64 MaximumMigrationMarkerBytes =
    256LL * 1024LL * 1024LL;

struct ManagedOutputDigest
{
    QString relativePath;
    QString sha256;
};

using ManagedOutputDigestMap =
    QMap<QString, ManagedOutputDigest>;

bool isLowercaseSha256(const QString& value)
{
    if (value.size() != 64)
        return false;
    for (const QChar character : value)
    {
        if (!character.isDigit() &&
            (character < QLatin1Char('a') ||
             character > QLatin1Char('f')))
        {
            return false;
        }
    }
    return true;
}

bool isSafeManagedOutputRelativePath(
    const QString& relativePath)
{
    const QString normalized =
        normalizedRelativePath(relativePath);
    if (normalized.isEmpty() ||
        QDir::isAbsolutePath(normalized) ||
        normalized.startsWith(QLatin1Char('/')) ||
        normalized.contains(QChar::Null))
    {
        return false;
    }
    const QStringList parts =
        normalized.split(
            QLatin1Char('/'),
            Qt::KeepEmptyParts);
    for (const QString& part : parts)
    {
        if (part.isEmpty() ||
            part == QStringLiteral(".") ||
            part == QStringLiteral("..") ||
            part.contains(QLatin1Char(':')))
        {
            return false;
        }
    }
    return !isMigrationGeneratedArtifact(normalized);
}

bool calculateFileSha256(
    const QString& filePath,
    QString& sha256)
{
    sha256.clear();
    const QFileInfo information(filePath);
    if (!information.exists() ||
        !information.isFile() ||
        isFileSystemLink(information))
    {
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QCryptographicHash hash(
        QCryptographicHash::Sha256);
    constexpr qint64 ChunkBytes = 1024 * 1024;
    while (!file.atEnd())
    {
        const QByteArray chunk = file.read(ChunkBytes);
        if (chunk.isEmpty() && !file.atEnd())
            return false;
        hash.addData(chunk);
    }
    if (file.error() != QFileDevice::NoError)
        return false;
    sha256 = QString::fromLatin1(
        hash.result().toHex());
    return isLowercaseSha256(sha256);
}

struct MigrationFallbackRoots
{
    QStringList content;
    QStringList ui;
};

QString fileSystemPathToQString(
    const std::filesystem::path& path)
{
    if (path.empty())
        return QString();
    const std::string utf8 = path.u8string();
    return QDir::cleanPath(
        QString::fromUtf8(
            utf8.data(),
            static_cast<int>(utf8.size())));
}

bool resolveMigrationFallbackRoots(
    const QString& collectionRoot,
    const QString& candidateRoot,
    MigrationFallbackRoots& roots,
    QString& errorMessage)
{
    roots = MigrationFallbackRoots();
    errorMessage.clear();

    RuntimeResource::ResourceCatalogRequest request;
    request.primaryCollectionRoot =
        std::filesystem::u8path(
            collectionRoot.toUtf8().constData());
    RuntimeResource::SupplementalResourceRoot candidate;
    candidate.root = std::filesystem::u8path(
        candidateRoot.toUtf8().constData());
    candidate.stableEntryKey =
        "migration.candidate";
    candidate.sourceTag = "editor-migration";
    candidate.replacesPrimaryGameId = true;
    request.supplementalRoots.push_back(
        std::move(candidate));

    const RuntimeResource::ExactSelectionResult selection =
        RuntimeResource::resolveResourceCatalogEntrySelection(
            request,
            "migration.candidate");
    if (!selection.succeeded())
    {
        errorMessage = QString::fromUtf8(
            selection.message.data(),
            static_cast<int>(selection.message.size()));
        if (errorMessage.isEmpty())
        {
            errorMessage = QString::fromUtf8(
                "无法按运行时规则解析转换资源的基底链");
        }
        return false;
    }

    const QString candidateKey =
        lowerPathKey(
            pathWithCanonicalExistingAncestor(
                candidateRoot));
    for (const RuntimeResource::ContentRoot& root :
         selection.selection.orderedContentRoots)
    {
        if (root.kind ==
                RuntimeResource::ContentRootKind::Active ||
            root.kind ==
                RuntimeResource::ContentRootKind::Common)
        {
            continue;
        }
        const QString path =
            fileSystemPathToQString(root.root);
        if (!path.isEmpty() &&
            lowerPathKey(
                pathWithCanonicalExistingAncestor(path)) !=
                candidateKey &&
            !roots.content.contains(
                path,
                fileSystemPathCaseSensitivity()))
        {
            roots.content.append(path);
        }
    }
    for (const std::filesystem::path& root :
         selection.selection.orderedUiFallbackRoots)
    {
        const QString path =
            fileSystemPathToQString(root);
        if (!path.isEmpty() &&
            lowerPathKey(
                pathWithCanonicalExistingAncestor(path)) !=
                candidateKey &&
            !roots.ui.contains(
                path,
                fileSystemPathCaseSensitivity()))
        {
            roots.ui.append(path);
        }
    }
    const QString commonRoot =
        fileSystemPathToQString(
            selection.selection.commonResourceRoot);
    if (!commonRoot.isEmpty() &&
        QDir(commonRoot).exists() &&
        !roots.ui.contains(
            commonRoot,
            fileSystemPathCaseSensitivity()))
    {
        roots.ui.append(commonRoot);
    }
    return true;
}

bool isUiMigrationResourcePath(
    const QString& relativePath)
{
    const QString path = lowerPathKey(relativePath);
    static const QStringList uiRoots = {
        QStringLiteral("ini/ui"),
        QStringLiteral("asf/ui"),
        QStringLiteral("mpc/ui"),
        QStringLiteral("bmp/ui"),
        QStringLiteral("image/ui"),
        QStringLiteral("sound/ui")
    };
    for (const QString& root : uiRoots)
    {
        if (path == root ||
            path.startsWith(root + QLatin1Char('/')))
        {
            return true;
        }
    }
    return false;
}

bool isDependencyDeduplicationCandidate(
    const QString& relativePath)
{
    const QString root =
        lowerPathKey(relativePath).
            section(QLatin1Char('/'), 0, 0);
    static const QSet<QString> resourceRoots = {
        QStringLiteral("asf"),
        QStringLiteral("bmp"),
        QStringLiteral("font"),
        QStringLiteral("image"),
        QStringLiteral("img"),
        QStringLiteral("mpc"),
        QStringLiteral("music"),
        QStringLiteral("sound"),
        QStringLiteral("video")
    };
    return resourceRoots.contains(root);
}

AssetMigrationFileOutcome* outcomeForOutput(
    AssetMigrationReport& report,
    const QString& outputPath)
{
    const QString outputKey =
        lowerPathKey(outputPath);
    for (auto outcome = report.fileOutcomes.rbegin();
         outcome != report.fileOutcomes.rend();
         ++outcome)
    {
        if (lowerPathKey(outcome->outputPath) ==
                outputKey &&
            (outcome->action ==
                 AssetMigrationFileAction::Copy ||
             outcome->action ==
                 AssetMigrationFileAction::Convert))
        {
            return &*outcome;
        }
    }
    return nullptr;
}

bool removeExactDependencyDuplicates(
    const QString& candidateRoot,
    const MigrationFallbackRoots& fallbackRoots,
    QSet<QString>& omittedPathKeys,
    AssetMigrationReport& report,
    const JxAssetMigrator::LogCallback& logCallback,
    QString& errorMessage)
{
    omittedPathKeys.clear();
    errorMessage.clear();
    const QDir candidateDirectory(candidateRoot);
    QDirIterator iterator(
        candidateRoot,
        QDir::Files |
            QDir::Hidden |
            QDir::System,
        QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        const QString candidatePath =
            iterator.next();
        const QFileInfo candidateInformation =
            iterator.fileInfo();
        const QString relativePath =
            normalizedRelativePath(
                candidateDirectory.relativeFilePath(
                    candidatePath));
        if (!isDependencyDeduplicationCandidate(
                relativePath) ||
            isMigrationGeneratedArtifact(relativePath) ||
            isFileSystemLink(candidateInformation))
        {
            continue;
        }

        const QStringList& roots =
            isUiMigrationResourcePath(relativePath)
            ? fallbackRoots.ui
            : fallbackRoots.content;
        for (const QString& root : roots)
        {
            const QString fallbackPath =
                appendPath(root, relativePath);
            const QFileInfo fallbackInformation(
                fallbackPath);
            if (!fallbackInformation.exists() &&
                !isFileSystemLink(fallbackInformation))
            {
                continue;
            }
            if (!fallbackInformation.isFile() ||
                isFileSystemLink(fallbackInformation))
            {
                // This is the first runtime fallback that owns the path.
                // Do not skip over an unsafe or non-file entry and compare a
                // deeper dependency instead.
                break;
            }
            if (fallbackInformation.size() !=
                candidateInformation.size())
            {
                break;
            }

            QString candidateSha256;
            QString fallbackSha256;
            if (!calculateFileSha256(
                    candidatePath,
                    candidateSha256) ||
                !calculateFileSha256(
                    fallbackPath,
                    fallbackSha256))
            {
                errorMessage = QString::fromUtf8(
                    "无法读取待查重资源或基底资源: %1")
                    .arg(relativePath);
                return false;
            }
            if (candidateSha256 != fallbackSha256)
                break;

            const qint64 duplicateBytes =
                candidateInformation.size();
            if (!QFile::remove(candidatePath))
            {
                errorMessage = QString::fromUtf8(
                    "无法从转换候选中省略基底重复资源: %1")
                    .arg(relativePath);
                return false;
            }
            omittedPathKeys.insert(
                lowerPathKey(relativePath));
            report.dependencyDuplicateFiles++;
            report.dependencyDuplicateBytes +=
                static_cast<quint64>(
                    std::max<qint64>(0, duplicateBytes));
            if (report.writtenFiles > 0)
                report.writtenFiles--;
            AssetResourceDomainReport& domain =
                domainReportFor(
                    report,
                    resourceTypeForRelativePath(
                        relativePath));
            if (domain.writtenFiles > 0)
                domain.writtenFiles--;

            AssetMigrationFileOutcome* outcome =
                outcomeForOutput(
                    report,
                    relativePath);
            const QString message =
                QString::fromUtf8(
                    "与运行时首先命中的基底资源完全相同，"
                    "转换输出省略该文件；基底=%1")
                    .arg(root);
            if (outcome)
            {
                outcome->action =
                    AssetMigrationFileAction::Skip;
                outcome->reason =
                    QStringLiteral(
                        "identical-to-dependency");
                outcome->message = message;
                outcome->outputSha256.clear();
            }
            else
            {
                appendFileOutcome(
                    report,
                    QStringLiteral(
                        "<generated:dependency-deduplication>"),
                    relativePath,
                    resourceTypeForRelativePath(
                        relativePath),
                    AssetMigrationFileAction::Skip,
                    QStringLiteral(
                        "identical-to-dependency"),
                    message,
                    false);
            }
            break;
        }
    }

    if (report.dependencyDuplicateFiles > 0)
    {
        const QString message = QString::fromUtf8(
            "基底资源查重: 省略 %1 个完全相同的媒体资源，共 %2 字节。")
            .arg(report.dependencyDuplicateFiles)
            .arg(report.dependencyDuplicateBytes);
        report.logLines.append(message);
        if (logCallback)
            logCallback(message);
    }
    return true;
}

ManagedOutputDigestMap readMigrationMarkerProvenance(
    const QString& outputRoot)
{
    ManagedOutputDigestMap result;
    const QString markerPath =
        appendPath(
            outputRoot,
            QString::fromLatin1(
                kMigrationMarkerFileName));
    const QFileInfo markerInformation(markerPath);
    if (!markerInformation.exists() ||
        !markerInformation.isFile() ||
        isFileSystemLink(markerInformation) ||
        markerInformation.size() < 0 ||
        markerInformation.size() >
            MaximumMigrationMarkerBytes)
    {
        return result;
    }

    QFile marker(markerPath);
    if (!marker.open(QIODevice::ReadOnly))
        return result;
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(
            marker.readAll(),
            &parseError);
    if (parseError.error !=
            QJsonParseError::NoError ||
        !document.isObject())
    {
        return {};
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("format")).
                toString() !=
            QStringLiteral(
                "jxqy-editor-asset-migration-output") ||
        root.value(QStringLiteral("schemaVersion")).
                toInt(-1) !=
            MigrationMarkerSchemaVersion ||
        root.value(QStringLiteral("hashAlgorithm")).
                toString() !=
            QStringLiteral("sha256") ||
        !root.value(
                 QStringLiteral(
                     "managedOutputSha256")).
                 isObject())
    {
        return {};
    }

    const QJsonObject managedOutputs =
        root.value(
                QStringLiteral(
                    "managedOutputSha256")).
            toObject();
    for (auto entry = managedOutputs.begin();
         entry != managedOutputs.end();
         ++entry)
    {
        const QString relativePath =
            normalizedRelativePath(entry.key());
        const QString sha256 =
            entry.value().toString();
        const QString pathKey =
            lowerPathKey(relativePath);
        if (!isSafeManagedOutputRelativePath(
                relativePath) ||
            !isLowercaseSha256(sha256) ||
            result.contains(pathKey))
        {
            return {};
        }
        result.insert(
            pathKey,
            {relativePath, sha256});
    }
    return result;
}

bool collectManagedOutputDigests(
    const QString& stagingRoot,
    ManagedOutputDigestMap& digests)
{
    digests.clear();
    const QDir stagingDirectory(stagingRoot);
    QDirIterator iterator(
        stagingRoot,
        QDir::Files |
            QDir::Hidden |
            QDir::System |
            QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        const QString filePath = iterator.next();
        const QFileInfo information = iterator.fileInfo();
        const QString relativePath =
            normalizedRelativePath(
                stagingDirectory.relativeFilePath(
                    filePath));
        if (isMigrationGeneratedArtifact(relativePath))
            continue;
        if (!isSafeManagedOutputRelativePath(
                relativePath) ||
            isFileSystemLink(information))
        {
            digests.clear();
            return false;
        }
        QString sha256;
        if (!calculateFileSha256(
                filePath,
                sha256))
        {
            digests.clear();
            return false;
        }
        const QString pathKey =
            lowerPathKey(relativePath);
        if (digests.contains(pathKey))
        {
            digests.clear();
            return false;
        }
        digests.insert(
            pathKey,
            {relativePath, sha256});
    }
    return true;
}

ManagedOutputDigestMap mergeManagedOutputDigests(
    const ManagedOutputDigestMap& previous,
    const ManagedOutputDigestMap& current,
    const QSet<QString>& explicitlyOmittedPathKeys)
{
    ManagedOutputDigestMap combined = previous;
    for (const QString& pathKey :
         explicitlyOmittedPathKeys)
    {
        combined.remove(pathKey);
    }
    for (auto digest = current.cbegin();
         digest != current.cend();
         ++digest)
    {
        combined.insert(
            digest.key(),
            digest.value());
    }
    return combined;
}

void applyManagedOutputDigestsToReport(
    AssetMigrationReport& report,
    const ManagedOutputDigestMap& current,
    const ManagedOutputDigestMap& combined)
{
    report.managedOutputSha256.clear();
    for (const ManagedOutputDigest& digest :
         combined)
    {
        report.managedOutputSha256.insert(
            digest.relativePath,
            digest.sha256);
    }
    for (AssetMigrationFileOutcome& outcome :
         report.fileOutcomes)
    {
        const auto digest = current.constFind(
            lowerPathKey(outcome.outputPath));
        if (digest != current.cend())
            outcome.outputSha256 =
                digest->sha256;
    }
}

bool pathIsInPublishedScope(
    const QString& relativePath,
    const QStringList& publishedRoots)
{
    if (publishedRoots.isEmpty())
        return true;

    const QString path = lowerPathKey(relativePath);
    for (QString root : publishedRoots)
    {
        root = lowerPathKey(normalizedRelativePath(root));
        while (root.endsWith(QLatin1Char('/')))
            root.chop(1);
        if (!root.isEmpty() &&
            (path == root ||
             path.startsWith(root + QLatin1Char('/'))))
        {
            return true;
        }
    }
    return false;
}

struct ExistingOutputSnapshotEntry
{
    QString relativePath;
    QString entryType;
    QString sha256;
};

struct ExistingOutputSnapshot
{
    bool rootExists = false;
    QMap<QString, ExistingOutputSnapshotEntry> entries;
};

bool outputEntryExists(const QFileInfo& information)
{
    return information.exists() ||
        isFileSystemLink(information);
}

bool collectExistingOutputSnapshot(
    const QString& rootPath,
    const QStringList& publishedRoots,
    ExistingOutputSnapshot& snapshot,
    QString& errorRelativePath,
    QString& errorMessage)
{
    snapshot = ExistingOutputSnapshot();
    errorRelativePath.clear();
    errorMessage.clear();

    const QFileInfo rootInformation(rootPath);
    if (!outputEntryExists(rootInformation))
        return true;
    snapshot.rootExists = true;
    if (isFileSystemLink(rootInformation) ||
        !rootInformation.isDir())
    {
        errorRelativePath = QStringLiteral(".");
        errorMessage = QString::fromUtf8(
            "旧输出根不是可安全复核的普通目录");
        return false;
    }

    const QDir rootDirectory(rootPath);
    QDirIterator iterator(
        rootPath,
        QDir::AllEntries |
            QDir::NoDotAndDotDot |
            QDir::Hidden |
            QDir::System,
        QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        const QString entryPath = iterator.next();
        const QFileInfo information = iterator.fileInfo();
        const QString relativePath =
            normalizedRelativePath(
                rootDirectory.relativeFilePath(
                    entryPath));
        const bool fileSystemLink =
            isFileSystemLink(information);
        if (!pathIsInPublishedScope(
                relativePath,
                publishedRoots) &&
            !isMigrationGeneratedArtifact(
                relativePath) &&
            !fileSystemLink)
        {
            continue;
        }

        const QString pathKey =
            lowerPathKey(relativePath);
        if (!isSafeManagedOutputRelativePath(
                relativePath) &&
            !isMigrationGeneratedArtifact(
                relativePath))
        {
            errorRelativePath = relativePath;
            errorMessage = QString::fromUtf8(
                "旧输出包含不安全的相对路径");
            return false;
        }
        if (snapshot.entries.contains(pathKey))
        {
            errorRelativePath = relativePath;
            errorMessage = QString::fromUtf8(
                "旧输出包含仅大小写不同的冲突路径");
            return false;
        }

        ExistingOutputSnapshotEntry entry;
        entry.relativePath = relativePath;
        entry.entryType =
            migrationEntryType(information);
        if (fileSystemLink)
        {
            snapshot.entries.insert(
                pathKey,
                std::move(entry));
            continue;
        }
        if (information.isFile())
        {
            if (!calculateFileSha256(
                    entryPath,
                    entry.sha256))
            {
                errorRelativePath = relativePath;
                errorMessage = QString::fromUtf8(
                    "无法读取旧输出文件的 SHA-256");
                return false;
            }
        }
        else if (!information.isDir())
        {
            errorRelativePath = relativePath;
            errorMessage = QString::fromUtf8(
                "旧输出包含不支持的目录项类型");
            return false;
        }
        snapshot.entries.insert(
            pathKey,
            std::move(entry));
    }
    return true;
}

bool compareExistingOutputSnapshots(
    const ExistingOutputSnapshot& expected,
    const ExistingOutputSnapshot& actual,
    bool compareRootExistence,
    QString& changedRelativePath)
{
    changedRelativePath.clear();
    if (compareRootExistence &&
        expected.rootExists != actual.rootExists)
    {
        changedRelativePath = QStringLiteral(".");
        return false;
    }

    for (auto expectedEntry =
             expected.entries.cbegin();
         expectedEntry != expected.entries.cend();
         ++expectedEntry)
    {
        const auto actualEntry =
            actual.entries.constFind(
                expectedEntry.key());
        if (actualEntry ==
                actual.entries.cend() ||
            actualEntry->relativePath !=
                expectedEntry->relativePath ||
            actualEntry->entryType !=
                expectedEntry->entryType ||
            actualEntry->sha256 !=
                expectedEntry->sha256)
        {
            changedRelativePath =
                expectedEntry->relativePath;
            return false;
        }
    }
    for (auto actualEntry =
             actual.entries.cbegin();
         actualEntry != actual.entries.cend();
         ++actualEntry)
    {
        if (!expected.entries.contains(
                actualEntry.key()))
        {
            changedRelativePath =
                actualEntry->relativePath;
            return false;
        }
    }
    return true;
}

bool existingEntryStillMatchesSnapshot(
    const QString& existingRoot,
    const ExistingOutputSnapshotEntry& expected)
{
    const QString existingPath =
        appendPath(
            existingRoot,
            expected.relativePath);
    const QFileInfo information(existingPath);
    if (!outputEntryExists(information) ||
        isFileSystemLink(information) ||
        migrationEntryType(information) !=
            expected.entryType)
    {
        return false;
    }
    if (information.isDir())
        return true;

    QString sha256;
    return calculateFileSha256(
               existingPath,
               sha256) &&
        sha256 == expected.sha256;
}

bool mergeExistingOutputFiles(
    const QString& existingRoot,
    const QString& stagingRoot,
    const ExistingOutputSnapshot&
        existingSnapshot,
    const ManagedOutputDigestMap&
        previousManagedOutputs,
    const ManagedOutputDigestMap&
        currentManagedOutputs,
    const QSet<QString>&
        explicitlyOmittedPathKeys,
    const QStringList&
        managedPublishedRoots,
    AssetMigrationReport& report,
    const JxAssetMigrator::LogCallback& logCallback)
{
    if (!existingSnapshot.rootExists)
        return true;

    bool merged = true;
    for (auto previous =
             previousManagedOutputs.cbegin();
         previous != previousManagedOutputs.cend();
         ++previous)
    {
        if (!pathIsInPublishedScope(
                previous->relativePath,
                managedPublishedRoots))
        {
            continue;
        }

        const auto existing =
            existingSnapshot.entries.constFind(
                previous.key());
        if (explicitlyOmittedPathKeys.contains(
                previous.key()))
        {
            if (existing ==
                    existingSnapshot.entries.cend())
            {
                continue;
            }
            if (existing->entryType !=
                    QStringLiteral("file") ||
                existing->sha256 !=
                    previous->sha256)
            {
                const AssetResourceType domain =
                    resourceTypeForRelativePath(
                        previous->relativePath);
                appendFileOutcome(
                    report,
                    previous->relativePath,
                    previous->relativePath,
                    domain,
                    AssetMigrationFileAction::Fail,
                    QStringLiteral(
                        "existing-managed-output-modified"),
                    QString::fromUtf8(
                        "本代确认该文件与基底完全相同并准备省略，"
                        "但旧输出与上一代 SHA-256 清单不一致；"
                        "视为玩家内容冲突并取消发布"),
                    false,
                    existing->entryType,
                    existing->sha256);
                domainReportFor(
                    report,
                    domain).failedFiles++;
                report.errorCount++;
                merged = false;
            }
            continue;
        }
        if (existing ==
                existingSnapshot.entries.cend())
        {
            const auto current =
                currentManagedOutputs.constFind(
                    previous.key());
            const QString outputPath =
                current ==
                        currentManagedOutputs.cend()
                ? previous->relativePath
                : current->relativePath;
            const AssetResourceType domain =
                resourceTypeForRelativePath(
                    outputPath);
            appendFileOutcome(
                report,
                previous->relativePath,
                outputPath,
                domain,
                AssetMigrationFileAction::Fail,
                QStringLiteral(
                    "existing-managed-output-deleted"),
                QString::fromUtf8(
                    "上一代 SHA-256 清单记录了该受管文件，"
                    "但旧输出已缺失；视为玩家删除冲突并取消发布"),
                false,
                QStringLiteral("file"));
            domainReportFor(
                report,
                domain).failedFiles++;
            report.errorCount++;
            merged = false;
            continue;
        }

        if (!currentManagedOutputs.contains(
                previous.key()) &&
            (existing->entryType !=
                 QStringLiteral("file") ||
             existing->sha256 !=
                 previous->sha256))
        {
            const AssetResourceType domain =
                resourceTypeForRelativePath(
                    previous->relativePath);
            appendFileOutcome(
                report,
                previous->relativePath,
                previous->relativePath,
                domain,
                AssetMigrationFileAction::Fail,
                QStringLiteral(
                    "existing-managed-output-modified"),
                QString::fromUtf8(
                    "本代不再生成该受管文件，"
                    "但旧输出与上一代 SHA-256 清单不一致；"
                    "视为玩家内容冲突并取消发布"),
                false,
                existing->entryType,
                existing->sha256);
            domainReportFor(
                report,
                domain).failedFiles++;
            report.errorCount++;
            merged = false;
        }
    }

    QHash<QString, QString> stagedPathsByKey;
    QDir stagingDirectory(stagingRoot);
    QDirIterator stagedIterator(
        stagingRoot,
        QDir::AllEntries |
            QDir::NoDotAndDotDot |
            QDir::Hidden |
            QDir::System,
        QDirIterator::Subdirectories);
    while (stagedIterator.hasNext())
    {
        const QString path = stagedIterator.next();
        const QString relativePath = normalizedRelativePath(
            stagingDirectory.relativeFilePath(path));
        stagedPathsByKey.insert(
            lowerPathKey(relativePath),
            relativePath);
    }

    int preservedCount = 0;
    for (const ExistingOutputSnapshotEntry&
             existingEntry :
         existingSnapshot.entries)
    {
        const QString relativePath =
            existingEntry.relativePath;
        if (isMigrationGeneratedArtifact(relativePath))
            continue;
        const QString existingPath =
            appendPath(existingRoot, relativePath);
        const QFileInfo existingInformation(
            existingPath);
        const AssetResourceType domain =
            resourceTypeForRelativePath(relativePath);
        const QString entryType =
            existingEntry.entryType;
        if (entryType.endsWith(
                QStringLiteral("-link")))
        {
            appendFileOutcome(
                report,
                relativePath,
                relativePath,
                domain,
                AssetMigrationFileAction::Fail,
                QStringLiteral(
                    "existing-link-not-supported"),
                QString::fromUtf8(
                    "旧输出包含文件系统链接；为避免越界跟随，取消发布"),
                false,
                entryType);
            domainReportFor(
                report,
                domain).failedFiles++;
            report.errorCount++;
            merged = false;
            continue;
        }
        if (!existingEntryStillMatchesSnapshot(
                existingRoot,
                existingEntry))
        {
            appendFileOutcome(
                report,
                relativePath,
                relativePath,
                domain,
                AssetMigrationFileAction::Fail,
                QStringLiteral(
                    "existing-output-changed-during-merge"),
                QString::fromUtf8(
                    "旧输出在合并期间发生变化，取消发布"),
                false,
                entryType);
            domainReportFor(report, domain).failedFiles++;
            report.errorCount++;
            merged = false;
            continue;
        }

        const QString stagedPath =
            appendPath(stagingRoot, relativePath);
        const QString pathKey =
            lowerPathKey(relativePath);
        const auto previousDigest =
            previousManagedOutputs.constFind(
                pathKey);
        if (explicitlyOmittedPathKeys.contains(
                pathKey) &&
            previousDigest !=
                previousManagedOutputs.cend() &&
            existingEntry.entryType ==
                QStringLiteral("file") &&
            existingEntry.sha256 ==
                previousDigest->sha256)
        {
            continue;
        }
        const QString caseCollision =
            stagedPathsByKey.value(pathKey);
        if (!caseCollision.isEmpty() &&
            caseCollision.compare(
                relativePath,
                Qt::CaseSensitive) != 0)
        {
            appendFileOutcome(
                report,
                relativePath,
                caseCollision,
                domain,
                AssetMigrationFileAction::Fail,
                QStringLiteral(
                    "existing-entry-case-collision"),
                QString::fromUtf8(
                    "旧输出目录项与本次暂存路径仅大小写不同"),
                false,
                entryType);
            domainReportFor(report, domain).failedFiles++;
            report.errorCount++;
            merged = false;
            continue;
        }

        const QFileInfo stagedInformation(stagedPath);
        if (stagedInformation.exists())
        {
            if (existingInformation.isDir() &&
                stagedInformation.isDir() &&
                !isFileSystemLink(
                    stagedInformation))
            {
                appendFileOutcome(
                    report,
                    relativePath,
                    relativePath,
                    domain,
                    AssetMigrationFileAction::Copy,
                    QStringLiteral(
                        "preserve-existing-directory-already-staged"),
                    QString::fromUtf8(
                        "旧输出目录已由本次暂存目录覆盖保留"),
                    false,
                    QStringLiteral("directory"));
                preservedCount++;
                continue;
            }
            if (existingInformation.isFile() &&
                stagedInformation.isFile() &&
                !isFileSystemLink(
                    stagedInformation))
            {
                const auto stagedDigest =
                    currentManagedOutputs.constFind(
                        pathKey);
                if (stagedDigest ==
                        currentManagedOutputs.cend())
                {
                    appendFileOutcome(
                        report,
                        relativePath,
                        relativePath,
                        domain,
                        AssetMigrationFileAction::Fail,
                        QStringLiteral(
                            "existing-managed-output-hash-failed"),
                        QString::fromUtf8(
                            "无法核对旧输出与新候选的 SHA-256，"
                            "为避免覆盖玩家内容而取消发布"),
                        false,
                        entryType);
                    domainReportFor(
                        report,
                        domain).failedFiles++;
                    report.errorCount++;
                    merged = false;
                    continue;
                }

                if (existingEntry.sha256 ==
                    stagedDigest->sha256)
                {
                    continue;
                }

                const auto previousDigest =
                    previousManagedOutputs.constFind(
                        pathKey);
                if (previousDigest !=
                        previousManagedOutputs.cend() &&
                    existingEntry.sha256 ==
                        previousDigest->sha256)
                {
                    // The previous managed bytes are intact. The new
                    // generation may replace them transactionally.
                    continue;
                }

                const bool provenanceMissing =
                    previousDigest ==
                    previousManagedOutputs.cend();
                const QString reason =
                    provenanceMissing
                    ? QStringLiteral(
                          "existing-managed-output-provenance-missing")
                    : QStringLiteral(
                          "existing-managed-output-modified");
                const QString message =
                    provenanceMissing
                    ? QString::fromUtf8(
                          "旧输出与新候选同路径且字节不同，"
                          "上一代未记录可信 SHA-256；"
                          "视为玩家内容冲突并取消发布")
                    : QString::fromUtf8(
                          "旧输出 SHA-256 与上一代记录不一致；"
                          "视为玩家修改冲突并取消发布");
                appendFileOutcome(
                    report,
                    relativePath,
                    relativePath,
                    domain,
                    AssetMigrationFileAction::Fail,
                    reason,
                    message,
                    false,
                    entryType,
                    existingEntry.sha256);
                domainReportFor(
                    report,
                    domain).failedFiles++;
                report.errorCount++;
                merged = false;
                continue;
            }
            appendFileOutcome(
                report,
                relativePath,
                relativePath,
                domain,
                AssetMigrationFileAction::Fail,
                existingInformation.isDir()
                    ? QStringLiteral(
                          "existing-directory-conflicts-with-staged-entry")
                    : QStringLiteral(
                          "existing-file-conflicts-with-staged-entry"),
                existingInformation.isDir()
                    ? QString::fromUtf8(
                          "旧输出目录与本次暂存目录项冲突")
                    : QString::fromUtf8(
                          "旧输出文件与本次暂存目录项冲突"),
                false,
                entryType);
            domainReportFor(report, domain).failedFiles++;
            report.errorCount++;
            merged = false;
            continue;
        }

        if (existingEntry.entryType ==
            QStringLiteral("directory"))
        {
            if (!QDir().mkpath(stagedPath))
            {
                appendFileOutcome(
                    report,
                    relativePath,
                    relativePath,
                    domain,
                    AssetMigrationFileAction::Fail,
                    QStringLiteral(
                        "preserve-existing-directory-failed"),
                    QString::fromUtf8(
                        "无法在本次暂存目录中保留旧输出目录"),
                    false,
                    QStringLiteral("directory"));
                domainReportFor(report, domain).failedFiles++;
                report.errorCount++;
                merged = false;
                continue;
            }

            appendFileOutcome(
                report,
                relativePath,
                relativePath,
                domain,
                AssetMigrationFileAction::Copy,
                QStringLiteral(
                    "preserve-existing-player-directory"),
                QString::fromUtf8(
                    "本次迁移未拥有该目录，保留旧输出目录项"),
                false,
                QStringLiteral("directory"));
            stagedPathsByKey.insert(
                pathKey,
                relativePath);
            preservedCount++;
            continue;
        }

        if (existingEntry.entryType !=
                QStringLiteral("file") ||
            !ensureParentDirectory(stagedPath) ||
            !QFile::copy(existingPath, stagedPath))
        {
            appendFileOutcome(
                report,
                relativePath,
                relativePath,
                domain,
                AssetMigrationFileAction::Fail,
                QStringLiteral(
                    "preserve-existing-output-failed"),
                QString::fromUtf8(
                    "无法把旧输出中的玩家文件复制到本次暂存目录"),
                false,
                entryType);
            domainReportFor(report, domain).failedFiles++;
            report.errorCount++;
            merged = false;
            continue;
        }

        QString stagedSha256;
        QString existingSha256AfterCopy;
        if (!calculateFileSha256(
                stagedPath,
                stagedSha256) ||
            !calculateFileSha256(
                existingPath,
                existingSha256AfterCopy) ||
            stagedSha256 !=
                existingEntry.sha256 ||
            existingSha256AfterCopy !=
                existingEntry.sha256)
        {
            appendFileOutcome(
                report,
                relativePath,
                relativePath,
                domain,
                AssetMigrationFileAction::Fail,
                QStringLiteral(
                    "preserve-existing-output-hash-failed"),
                QString::fromUtf8(
                    "旧玩家文件复制前后或暂存 SHA-256 不一致，取消发布"),
                false,
                entryType);
            domainReportFor(report, domain).failedFiles++;
            report.errorCount++;
            merged = false;
            continue;
        }

        appendFileOutcome(
            report,
            relativePath,
            relativePath,
            domain,
            AssetMigrationFileAction::Copy,
            QStringLiteral(
                "preserve-existing-player-file"),
            QString::fromUtf8(
                "本次迁移未拥有该路径，按原字节保留旧输出"),
            false,
            entryType,
            stagedSha256);
        stagedPathsByKey.insert(pathKey, relativePath);
        report.writtenFiles++;
        domainReportFor(report, domain).writtenFiles++;
        preservedCount++;
    }

    if (preservedCount > 0)
    {
        const QString message = QString::fromUtf8(
                "从旧输出保留 %1 个本次迁移未拥有的玩家目录项。")
                .arg(preservedCount);
        report.logLines.append(message);
        if (logCallback)
            logCallback(message);
    }
    if (!merged)
    {
        const QString message = QString::fromUtf8(
            "错误: 旧输出合并存在路径冲突或复制失败，取消发布。");
        report.logLines.append(message);
        if (logCallback)
            logCallback(message);
    }
    return merged;
}

bool quarantineUnavailableScript(
    const QString& stagingRoot,
    const QString& existingOutputRoot,
    const QMap<QString, bool>&
        scannedSourcePathIsDirectory,
    const ManagedOutputDigestMap&
        previousManagedOutputs,
    const QString& scriptPath,
    QString& sourceRelativePath,
    QString& quarantineRelativePath)
{
    sourceRelativePath = normalizedRelativePath(
        QDir(stagingRoot).relativeFilePath(scriptPath));
    if (sourceRelativePath.isEmpty() ||
        sourceRelativePath == QStringLiteral("..") ||
        sourceRelativePath.startsWith(
            QStringLiteral("../")))
    {
        return false;
    }

    QMap<QString, bool> occupiedPathIsDirectory =
        scannedSourcePathIsDirectory;
    QSet<QString>
        sourceOrStagingOccupiedPathKeys;
    for (auto occupied =
             scannedSourcePathIsDirectory.cbegin();
         occupied !=
             scannedSourcePathIsDirectory.cend();
         ++occupied)
    {
        sourceOrStagingOccupiedPathKeys.insert(
            occupied.key());
    }
    QSet<QString>
        reusableExistingManagedPathKeys;
    const auto collectOccupiedPaths =
        [&](const QString& rootPath,
            bool existingOutput)
    {
        const QDir rootDirectory(rootPath);
        if (!rootDirectory.exists())
            return;
        QDirIterator iterator(
            rootPath,
            QDir::AllEntries |
                QDir::NoDotAndDotDot |
                QDir::Hidden |
                QDir::System,
            QDirIterator::Subdirectories);
        while (iterator.hasNext())
        {
            const QString path =
                iterator.next();
            const QFileInfo information =
                iterator.fileInfo();
            const QString relativePath =
                normalizedRelativePath(
                    rootDirectory.
                        relativeFilePath(path));
            const QString pathKey =
                lowerPathKey(relativePath);
            const bool ordinaryDirectory =
                information.isDir() &&
                !isFileSystemLink(
                    information);
            if (!existingOutput)
            {
                sourceOrStagingOccupiedPathKeys.
                    insert(pathKey);
            }
            else if (information.isFile() &&
                     !isFileSystemLink(
                         information))
            {
                const auto previous =
                    previousManagedOutputs.
                        constFind(pathKey);
                QString actualSha256;
                if (previous !=
                        previousManagedOutputs.
                            cend() &&
                    calculateFileSha256(
                        path,
                        actualSha256) &&
                    actualSha256 ==
                        previous->sha256)
                {
                    reusableExistingManagedPathKeys.
                        insert(pathKey);
                }
            }
            const auto occupied =
                occupiedPathIsDirectory.
                    constFind(pathKey);
            occupiedPathIsDirectory.insert(
                pathKey,
                occupied ==
                        occupiedPathIsDirectory.
                            cend()
                    ? ordinaryDirectory
                    : occupied.value() &&
                          ordinaryDirectory);
        }
    };
    collectOccupiedPaths(
        stagingRoot,
        false);
    collectOccupiedPaths(
        existingOutputRoot,
        true);

    const auto candidateIsAvailable =
        [&](const QString& relativePath)
    {
        const QString candidateKey =
            lowerPathKey(relativePath);
        if (!isSafeManagedOutputRelativePath(
                relativePath))
        {
            return false;
        }
        if (occupiedPathIsDirectory.contains(
                candidateKey) &&
            (sourceOrStagingOccupiedPathKeys.
                 contains(candidateKey) ||
             !reusableExistingManagedPathKeys.
                  contains(candidateKey)))
        {
            return false;
        }
        const QStringList parts =
            normalizedRelativePath(
                relativePath).
                split(
                    QLatin1Char('/'),
                    Qt::SkipEmptyParts);
        QString ancestor;
        for (int index = 0;
             index + 1 < parts.size();
             ++index)
        {
            if (!ancestor.isEmpty())
                ancestor += QLatin1Char('/');
            ancestor += parts[index];
            const auto occupied =
                occupiedPathIsDirectory.
                    constFind(
                        lowerPathKey(
                            ancestor));
            if (occupied !=
                    occupiedPathIsDirectory.
                        cend() &&
                !occupied.value())
            {
                return false;
            }
        }
        return true;
    };

    const QString sourceHash =
        QString::fromLatin1(
            QCryptographicHash::hash(
                sourceRelativePath.
                    normalized(
                        QString::
                            NormalizationForm_C).
                    toUtf8(),
                QCryptographicHash::Sha256).
                toHex().
                left(12));
    const QString preferredRelativePath =
        QStringLiteral(
            ".jxqy_migration_unavailable/") +
        sourceRelativePath +
        QStringLiteral(".invalid");
    QString candidateRelativePath =
        preferredRelativePath;
    if (!candidateIsAvailable(
            candidateRelativePath))
    {
        candidateRelativePath =
            preferredRelativePath +
            QLatin1Char('.') +
            sourceHash;
    }
    if (!candidateIsAvailable(
            candidateRelativePath))
    {
        const QString alternateRootBase =
            QStringLiteral(
                ".jxqy_migration_unavailable-") +
            sourceHash;
        quint64 suffix = 1;
        do
        {
            const QString alternateRoot =
                suffix == 1
                ? alternateRootBase
                : alternateRootBase +
                      QLatin1Char('-') +
                      QString::number(
                          suffix);
            candidateRelativePath =
                alternateRoot +
                QLatin1Char('/') +
                sourceRelativePath +
                QStringLiteral(".invalid");
            ++suffix;
        }
        while (!candidateIsAvailable(
            candidateRelativePath));
    }

    const QString candidatePath =
        appendPath(
            stagingRoot,
            candidateRelativePath);
    if (!ensureParentDirectory(candidatePath) ||
        !QFile::rename(
            scriptPath,
            candidatePath))
    {
        return false;
    }
    quarantineRelativePath =
        candidateRelativePath;
    return true;
}

bool writeMigrationMarkerFile(
    const QString& outputDir,
    const ManagedOutputDigestMap&
        managedOutputs =
            ManagedOutputDigestMap())
{
    QString markerPath = appendPath(outputDir, kMigrationMarkerFileName);
    ensureParentDirectory(markerPath);

    QSaveFile file(markerPath);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    QJsonObject hashes;
    for (const ManagedOutputDigest& digest :
         managedOutputs)
    {
        hashes.insert(
            digest.relativePath,
            digest.sha256);
    }

    QJsonObject root;
    root.insert(
        QStringLiteral("format"),
        QStringLiteral(
            "jxqy-editor-asset-migration-output"));
    root.insert(
        QStringLiteral("schemaVersion"),
        MigrationMarkerSchemaVersion);
    root.insert(
        QStringLiteral("hashAlgorithm"),
        QStringLiteral("sha256"));
    root.insert(
        QStringLiteral("managedOutputSha256"),
        hashes);
    const QByteArray payload =
        QJsonDocument(root).toJson(
            QJsonDocument::Indented);
    if (file.write(payload) != payload.size())
    {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

bool removePathIfExists(const QString& path)
{
    QFileInfo fileInfo(path);
    if (!outputEntryExists(fileInfo))
        return true;
    if (isFileSystemLink(fileInfo))
        return false;

    if (fileInfo.isDir())
    {
        QDir dir(path);
        return dir.removeRecursively();
    }

    return QFile::remove(path);
}

bool renamePath(const QString& sourcePath, const QString& targetPath,
                JxAssetMigrator::FileSystemOperation operation)
{
    if (shouldFailFileSystemOperation(operation, sourcePath, targetPath))
        return false;

    QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || QFileInfo::exists(targetPath) ||
        !ensureParentDirectory(targetPath))
    {
        return false;
    }

    if (sourceInfo.isDir())
        return QDir().rename(sourcePath, targetPath);
    return QFile::rename(sourcePath, targetPath);
}

bool removeEmptyDirectoryIfExists(
    const QString& path, JxAssetMigrator::FileSystemOperation operation)
{
    QDir directory(path);
    if (!directory.exists())
        return true;
    if (shouldFailFileSystemOperation(operation, path))
        return false;
    if (!directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty())
        return false;
    return QDir().rmdir(path);
}

QString uniqueSiblingPath(const QString& path, const QString& label)
{
    return path + "." + label + "-" +
        QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool publishStagedRoot(
    const QString& stagingRoot,
    const QString& outputRoot,
    const ExistingOutputSnapshot&
        expectedSnapshot,
    QString& retainedBackupPath,
    QString& errorMessage,
    QString& changedRelativePath)
{
    const QFileInfo outputInformation(outputRoot);
    const bool hadPreviousOutput =
        outputEntryExists(outputInformation);
    if (hadPreviousOutput !=
            expectedSnapshot.rootExists ||
        (hadPreviousOutput &&
         (isFileSystemLink(outputInformation) ||
          !outputInformation.isDir())))
    {
        changedRelativePath = QStringLiteral(".");
        errorMessage = QString::fromUtf8(
            "旧输出根在发布前发生变化，取消发布");
        return false;
    }

    QString backupPath;
    ExistingOutputSnapshot
        validatedBackupSnapshot;
    if (hadPreviousOutput)
    {
        backupPath = uniqueSiblingPath(outputRoot, "migration-backup");
        if (!renamePath(outputRoot, backupPath,
                        JxAssetMigrator::FileSystemOperation::BackupRoot))
        {
            errorMessage = QString::fromUtf8("无法备份上一份迁移输出: %1").arg(outputRoot);
            return false;
        }

        ExistingOutputSnapshot detachedSnapshot;
        QString snapshotErrorPath;
        QString snapshotError;
        bool snapshotMatches =
            collectExistingOutputSnapshot(
                backupPath,
                QStringList(),
                detachedSnapshot,
                snapshotErrorPath,
                snapshotError);
        if (snapshotMatches)
        {
            snapshotMatches =
                compareExistingOutputSnapshots(
                    expectedSnapshot,
                    detachedSnapshot,
                    true,
                    changedRelativePath);
        }
        else
        {
            changedRelativePath =
                snapshotErrorPath;
        }
        if (!snapshotMatches)
        {
            if (!renamePath(
                    backupPath,
                    outputRoot,
                    JxAssetMigrator::
                        FileSystemOperation::
                            RestoreRoot))
            {
                retainedBackupPath =
                    backupPath;
                errorMessage =
                    QString::fromUtf8(
                        "旧输出在发布前发生变化，且恢复失败；"
                        "完整备份仍位于 %1")
                        .arg(backupPath);
            }
            else
            {
                errorMessage =
                    QString::fromUtf8(
                        "旧输出在发布前发生变化，"
                        "已恢复旧输出并取消发布");
            }
            if (!snapshotError.isEmpty())
                errorMessage +=
                    QStringLiteral(": ") +
                    snapshotError;
            return false;
        }
        validatedBackupSnapshot =
            detachedSnapshot;
    }

    if (!renamePath(stagingRoot, outputRoot,
                    JxAssetMigrator::FileSystemOperation::PublishRoot))
    {
        if (hadPreviousOutput && !renamePath(
                backupPath, outputRoot,
                JxAssetMigrator::FileSystemOperation::RestoreRoot))
        {
            errorMessage = QString::fromUtf8(
                "无法发布新迁移输出，且上一份输出恢复失败；备份仍位于 %1")
                .arg(backupPath);
            retainedBackupPath = backupPath;
        }
        else
        {
            errorMessage = QString::fromUtf8("无法发布新迁移输出，上一份输出已恢复: %1")
                .arg(outputRoot);
        }
        return false;
    }

    if (hadPreviousOutput)
    {
        const bool cleanupBlocked =
            shouldFailFileSystemOperation(
                JxAssetMigrator::
                    FileSystemOperation::
                        RemoveBackup,
                backupPath);
        ExistingOutputSnapshot
            cleanupSnapshot;
        QString cleanupSnapshotPath;
        QString cleanupSnapshotError;
        const bool cleanupSnapshotRead =
            collectExistingOutputSnapshot(
                backupPath,
                QStringList(),
                cleanupSnapshot,
                cleanupSnapshotPath,
                cleanupSnapshotError);
        QString changedBackupPath;
        const bool backupUnchanged =
            cleanupSnapshotRead &&
            compareExistingOutputSnapshots(
                validatedBackupSnapshot,
                cleanupSnapshot,
                true,
                changedBackupPath);
        const bool backupRootIsLink =
            isFileSystemLink(
                QFileInfo(backupPath));
        if (backupRootIsLink)
        {
            retainedBackupPath = backupPath;
            errorMessage = QString::fromUtf8(
                "发布后旧输出备份根成为文件系统链接或 reparse point，"
                "拒绝递归删除并保留该路径");
        }
        else if (!backupUnchanged)
        {
            retainedBackupPath = backupPath;
            errorMessage = QString::fromUtf8(
                "发布后旧输出备份发生变化，"
                "为保留新写入内容未删除备份: %1")
                .arg(
                    changedBackupPath.isEmpty()
                        ? cleanupSnapshotPath
                        : changedBackupPath);
        }
        else if (cleanupBlocked ||
                 !removePathIfExists(
                     backupPath))
        {
            retainedBackupPath = backupPath;
            errorMessage = QString::fromUtf8(
                "旧输出备份清理失败");
        }
    }
    return true;
}

struct PublishedEntry
{
    QString relativePath;
    bool previousMoved = false;
    bool stagedMoved = false;
};

bool publishStagedEntries(
    const QString& stagingRoot,
    const QString& outputRoot,
    const QStringList& relativePaths,
    const ExistingOutputSnapshot&
        expectedSnapshot,
    QString& retainedBackupPath,
    QString& errorMessage,
    QString& changedRelativePath)
{
    const QFileInfo outputInformation(outputRoot);
    const bool hadOutputRoot =
        outputEntryExists(outputInformation);
    if (hadOutputRoot !=
            expectedSnapshot.rootExists ||
        (hadOutputRoot &&
         (isFileSystemLink(outputInformation) ||
          !outputInformation.isDir())))
    {
        changedRelativePath = QStringLiteral(".");
        errorMessage = QString::fromUtf8(
            "旧输出根在局部发布前发生变化，取消发布");
        return false;
    }
    if (!QDir().mkpath(outputRoot))
    {
        errorMessage = QString::fromUtf8("无法创建迁移输出目录: %1").arg(outputRoot);
        return false;
    }

    const QString backupRoot = uniqueSiblingPath(outputRoot, "migration-backup");
    QList<PublishedEntry> publishedEntries;
    bool publishOk = true;
    ExistingOutputSnapshot
        validatedBackupSnapshot;

    // Phase one detaches every selected old entry before any staging entry is
    // made visible. This keeps a later validation or publish failure capable
    // of restoring the complete previous selected generation.
    for (const QString& relativePath : relativePaths)
    {
        PublishedEntry entry;
        entry.relativePath = relativePath;
        const QString finalPath = appendPath(outputRoot, relativePath);
        const QString backupPath = appendPath(backupRoot, relativePath);

        const QFileInfo finalInformation(finalPath);
        if (outputEntryExists(finalInformation))
        {
            if (!renamePath(finalPath, backupPath,
                            JxAssetMigrator::FileSystemOperation::BackupEntry))
            {
                errorMessage = QString::fromUtf8("无法备份现有迁移子路径: %1").arg(finalPath);
                publishOk = false;
                publishedEntries.append(entry);
                break;
            }
            entry.previousMoved = true;
        }
        publishedEntries.append(entry);
    }

    if (publishOk)
    {
        ExistingOutputSnapshot detachedSnapshot;
        QString snapshotErrorPath;
        QString snapshotError;
        bool snapshotMatches =
            collectExistingOutputSnapshot(
                backupRoot,
                relativePaths,
                detachedSnapshot,
                snapshotErrorPath,
                snapshotError);
        if (snapshotMatches)
        {
            snapshotMatches =
                compareExistingOutputSnapshots(
                    expectedSnapshot,
                    detachedSnapshot,
                    false,
                    changedRelativePath);
        }
        else
        {
            changedRelativePath =
                snapshotErrorPath;
        }
        if (!snapshotMatches)
        {
            publishOk = false;
            errorMessage = QString::fromUtf8(
                "旧输出在局部发布前发生变化，取消发布");
            if (!snapshotError.isEmpty())
                errorMessage +=
                    QStringLiteral(": ") +
                    snapshotError;
        }
        else
        {
            // backupRoot only contains entries detached from relativePaths.
            // Reuse the snapshot that was already compared with the merge
            // baseline so a write between two scans cannot become the cleanup
            // baseline and then be deleted silently.
            validatedBackupSnapshot =
                detachedSnapshot;
        }
    }

    // Phase two starts only after the complete detached backup matches the
    // merge snapshot.
    if (publishOk)
    {
        for (PublishedEntry& entry :
             publishedEntries)
        {
            const QString finalPath =
                appendPath(
                    outputRoot,
                    entry.relativePath);
            const QString stagedPath =
                appendPath(
                    stagingRoot,
                    entry.relativePath);
            if (!QFileInfo::exists(stagedPath))
                continue;
            if (!renamePath(stagedPath, finalPath,
                            JxAssetMigrator::FileSystemOperation::PublishEntry))
            {
                errorMessage = QString::fromUtf8("无法发布迁移子路径: %1").arg(finalPath);
                publishOk = false;
                break;
            }
            entry.stagedMoved = true;
        }
    }

    if (!publishOk)
    {
        bool rollbackOk = true;
        for (auto entry = publishedEntries.crbegin(); entry != publishedEntries.crend(); ++entry)
        {
            const QString finalPath = appendPath(outputRoot, entry->relativePath);
            const QString backupPath = appendPath(backupRoot, entry->relativePath);
            if (entry->stagedMoved && !renamePath(
                    finalPath, appendPath(stagingRoot, entry->relativePath),
                    JxAssetMigrator::FileSystemOperation::RollbackPublishedEntry))
            {
                rollbackOk = false;
            }
            if (entry->previousMoved && !renamePath(
                    backupPath, finalPath,
                    JxAssetMigrator::FileSystemOperation::RestoreEntry))
                rollbackOk = false;
        }
        if (rollbackOk && !hadOutputRoot &&
            !removeEmptyDirectoryIfExists(
                outputRoot,
                JxAssetMigrator::FileSystemOperation::RemoveCreatedOutputRoot))
        {
            rollbackOk = false;
            errorMessage += QString::fromUtf8("；回滚未能移除本次新建的输出目录: %1")
                .arg(outputRoot);
        }
        if (!rollbackOk)
        {
            retainedBackupPath = backupRoot;
            errorMessage += QString::fromUtf8("；回滚未完全成功，备份保留在 %1").arg(backupRoot);
        }
        else
        {
            if (shouldFailFileSystemOperation(
                    JxAssetMigrator::FileSystemOperation::RemoveBackup, backupRoot) ||
                !removePathIfExists(backupRoot))
            {
                retainedBackupPath = backupRoot;
                errorMessage += QString::fromUtf8("；回滚备份目录清理失败: %1")
                    .arg(backupRoot);
            }
        }
        return false;
    }

    removePathIfExists(stagingRoot);
    if (outputEntryExists(
            QFileInfo(backupRoot)))
    {
        const bool cleanupBlocked =
            shouldFailFileSystemOperation(
                JxAssetMigrator::
                    FileSystemOperation::
                        RemoveBackup,
                backupRoot);
        ExistingOutputSnapshot
            cleanupSnapshot;
        QString cleanupSnapshotPath;
        QString cleanupSnapshotError;
        const bool cleanupSnapshotRead =
            collectExistingOutputSnapshot(
                backupRoot,
                QStringList(),
                cleanupSnapshot,
                cleanupSnapshotPath,
                cleanupSnapshotError);
        QString changedBackupPath;
        const bool backupUnchanged =
            cleanupSnapshotRead &&
            compareExistingOutputSnapshots(
                validatedBackupSnapshot,
                cleanupSnapshot,
                true,
                changedBackupPath);
        const bool backupRootIsLink =
            isFileSystemLink(
                QFileInfo(backupRoot));
        if (backupRootIsLink)
        {
            retainedBackupPath =
                backupRoot;
            errorMessage = QString::fromUtf8(
                "发布后旧输出备份根成为文件系统链接或 reparse point，"
                "拒绝递归删除并保留该路径");
        }
        else if (!backupUnchanged)
        {
            retainedBackupPath =
                backupRoot;
            errorMessage = QString::fromUtf8(
                "发布后旧输出备份发生变化，"
                "为保留新写入内容未删除备份: %1")
                .arg(
                    changedBackupPath.isEmpty()
                        ? cleanupSnapshotPath
                        : changedBackupPath);
        }
        else if (cleanupBlocked ||
                 !removePathIfExists(
                     backupRoot))
        {
            retainedBackupPath =
                backupRoot;
            errorMessage = QString::fromUtf8(
                "旧输出备份清理失败");
        }
    }
    return true;
}

QString normalizeIncludePrefix(QString prefix)
{
    prefix.replace("\\", "/");
    prefix = prefix.trimmed();
    while (prefix.startsWith('/'))
        prefix.remove(0, 1);
    while (prefix.endsWith('/'))
        prefix.chop(1);
    return prefix;
}

bool isSafeRelativeSubdirectory(const QString& path)
{
    const QString normalized = normalizeIncludePrefix(path);
    if (normalized.isEmpty() || QDir::isAbsolutePath(path))
        return false;

    const QStringList parts = normalized.split('/', Qt::KeepEmptyParts);
    for (const QString& part : parts)
    {
        if (part.isEmpty() || part == "." || part == ".." || part.contains(':') ||
            part.contains(QChar::Null))
        {
            return false;
        }
    }
    return true;
}

bool containsIniLineBreak(const QString& value)
{
    return value.contains('\r') || value.contains('\n') || value.contains(QChar::Null);
}

bool hasUnsafeProfileText(const AssetMigrationOptions& options)
{
    const QStringList values = {
        options.modId,
        options.modName,
        options.dependencyId,
        options.uiBaseId,
        options.uiProfile,
        options.saveNamespace
    };
    for (const QString& value : values)
    {
        if (containsIniLineBreak(value))
            return true;
    }
    for (auto feature = options.features.cbegin(); feature != options.features.cend(); ++feature)
    {
        if (containsIniLineBreak(feature.key()))
            return true;
    }
    return false;
}

QString matchedCasePreservingPrefix(const QString& relativePath, const QString& includePrefix)
{
    QString normalizedPath = relativePath;
    normalizedPath.replace("\\", "/");
    const QStringList pathParts = normalizedPath.split('/', Qt::SkipEmptyParts);
    const QStringList prefixParts = normalizeIncludePrefix(includePrefix).split('/', Qt::SkipEmptyParts);
    if (prefixParts.isEmpty() || prefixParts.size() > pathParts.size())
        return QString();

    for (qsizetype i = 0; i < prefixParts.size(); ++i)
    {
        if (pathParts[i].compare(prefixParts[i], fileSystemPathCaseSensitivity()) != 0)
            return QString();
    }
    return pathParts.mid(0, prefixParts.size()).join('/');
}

bool relativePathMatchesPrefix(const QString& relativePath, const QString& includePrefix)
{
    QString prefix = normalizeIncludePrefix(includePrefix);
    if (prefix.isEmpty())
        return true;

    QString path = relativePath;
    path.replace("\\", "/");
    const Qt::CaseSensitivity caseSensitivity = fileSystemPathCaseSensitivity();
    return path.compare(prefix, caseSensitivity) == 0 ||
        path.startsWith(prefix + "/", caseSensitivity);
}

enum class UiWindowDefaultProfile
{
    Jxqy2,
    Yycs,
    Xjxqy
};

QString firstDependencyId(const QString& dependencyIds)
{
    const QStringList parts = dependencyIds.split(',', Qt::SkipEmptyParts);
    for (const QString& part : parts)
    {
        const QString dependencyId = part.trimmed();
        if (!dependencyId.isEmpty())
            return dependencyId;
    }
    return QString();
}

QStringList dependencyIds(const QString& declaredIds)
{
    QStringList result;
    for (const QString& part :
         declaredIds.split(',', Qt::SkipEmptyParts))
    {
        const QString dependencyId = part.trimmed();
        if (!dependencyId.isEmpty() &&
            !result.contains(dependencyId, Qt::CaseInsensitive))
        {
            result.append(dependencyId);
        }
    }
    return result;
}

bool findDependencyPack(
    const QString& outputDir,
    const AssetMigrationOptions& options,
    const QStringList& requestedIds,
    ResourcePackInfo& result)
{
    const QString normalizedOutputRoot =
        QDir::cleanPath(QFileInfo(outputDir).absoluteFilePath());
    QString candidateCollectionRoot =
        QFileInfo(normalizedOutputRoot).absolutePath();
    for (int depth = 0;
         depth < 16 && !candidateCollectionRoot.isEmpty();
         ++depth)
    {
        const QDir candidateDirectory(candidateCollectionRoot);
        if (QFileInfo::exists(candidateDirectory.filePath(
                QStringLiteral("resources.ini"))) ||
            ResourcePackScanner::hasManifest(candidateCollectionRoot))
        {
            const QList<ResourcePackInfo> packs =
                ResourcePackScanner::scanPacks(candidateCollectionRoot);
            for (const QString& dependencyId : requestedIds)
            {
                for (const ResourcePackInfo& pack : packs)
                {
                    if (pack.profile.id.compare(
                            dependencyId,
                            Qt::CaseInsensitive) == 0)
                    {
                        result = pack;
                        return true;
                    }
                }
            }
        }

        const QString parentRoot =
            QFileInfo(candidateCollectionRoot).absolutePath();
        if (parentRoot == candidateCollectionRoot)
        {
            break;
        }
        candidateCollectionRoot = parentRoot;
    }

    return false;
}

bool findDependencyProfile(
    const QString& outputDir,
    const AssetMigrationOptions& options,
    GameProfile& profile)
{
    ResourcePackInfo dependencyPack;
    if (!findDependencyPack(
            outputDir,
            options,
            dependencyIds(options.dependencyId),
            dependencyPack))
    {
        return false;
    }
    profile = dependencyPack.profile;
    return true;
}

QString findUiBaseRoot(
    const QString& outputDir,
    const AssetMigrationOptions& options,
    GameProfile* selectedProfile)
{
    if (selectedProfile)
        *selectedProfile = GameProfile();
    QStringList requestedIds;
    const QString uiBaseId = options.uiBaseId.trimmed();
    if (!uiBaseId.isEmpty())
        requestedIds.append(uiBaseId);

    const QString uiProfile = options.uiProfile.trimmed();
    const QStringList declaredDependencyIds =
        dependencyIds(options.dependencyId);
    if (uiBaseId.isEmpty())
    {
        for (const QString& dependencyId : declaredDependencyIds)
        {
            if (!uiProfile.isEmpty() &&
                dependencyId.compare(uiProfile, Qt::CaseInsensitive) == 0 &&
                !requestedIds.contains(dependencyId, Qt::CaseInsensitive))
            {
                requestedIds.append(dependencyId);
            }
        }
        for (const QString& dependencyId : declaredDependencyIds)
        {
            if (!requestedIds.contains(dependencyId, Qt::CaseInsensitive))
                requestedIds.append(dependencyId);
        }
    }

    for (const QString& requestedId : requestedIds)
    {
        ResourcePackInfo uiBasePack;
        if (!findDependencyPack(
                outputDir,
                options,
                QStringList{requestedId},
                uiBasePack))
        {
            continue;
        }
        if (uiBasePack.profile.id.compare(
                requestedId, Qt::CaseInsensitive) != 0)
        {
            continue;
        }
        if (uiBaseId.isEmpty() && !uiProfile.isEmpty() &&
            uiBasePack.profile.id.compare(
                uiProfile, Qt::CaseInsensitive) != 0 &&
            uiBasePack.profile.uiProfile.compare(
                uiProfile, Qt::CaseInsensitive) != 0)
        {
            continue;
        }

        const QString normalizedOutputRoot =
            QDir::cleanPath(QFileInfo(outputDir).absoluteFilePath());
        const QString normalizedBaseRoot =
            QDir::cleanPath(QFileInfo(uiBasePack.rootPath).absoluteFilePath());
        if (normalizedBaseRoot.compare(
                normalizedOutputRoot,
                fileSystemPathCaseSensitivity()) != 0)
        {
            if (selectedProfile)
                *selectedProfile = uiBasePack.profile;
            return normalizedBaseRoot;
        }
    }
    return QString();
}

void setFeatureDefault(
    QMap<QString, bool>& features,
    const QString& name,
    bool value)
{
    for (auto feature = features.cbegin(); feature != features.cend(); ++feature)
    {
        if (feature.key().compare(name, Qt::CaseInsensitive) == 0)
        {
            return;
        }
    }
    features.insert(name, value);
}

void applyExplicitProfileValues(
    const GameProfile& profile,
    bool titleMusicIsExplicit,
    AssetMigrationOptions& options)
{
    if (!options.defeatedNpcExperienceModeDefined &&
        profile.defeatedNpcExperienceModeDefined)
    {
        options.defeatedNpcExperienceMode =
            profile.defeatedNpcExperienceMode;
        options.defeatedNpcExperienceModeDefined = true;
    }
    if (!options.experienceMultiplierDefined &&
        profile.experienceMultiplierDefined)
    {
        options.experienceMultiplier = profile.experienceMultiplier;
        options.experienceMultiplierDefined = true;
    }
    if (!options.levelUpThresholdModeDefined &&
        profile.levelUpThresholdModeDefined)
    {
        options.levelUpThresholdMode = profile.levelUpThresholdMode;
        options.levelUpThresholdModeDefined = true;
    }
    if (!options.partnerFollowRadiusDefined &&
        profile.partnerFollowRadiusDefined)
    {
        options.partnerFollowRadius = profile.partnerFollowRadius;
        options.partnerFollowRadiusDefined = true;
    }
    if (!options.partnerFollowRunRadiusDefined &&
        profile.partnerFollowRunRadiusDefined)
    {
        options.partnerFollowRunRadius = profile.partnerFollowRunRadius;
        options.partnerFollowRunRadiusDefined = true;
    }
    if (!options.minimumMagicDamageDefined &&
        profile.minimumMagicDamageDefined)
    {
        options.minimumMagicDamage = profile.minimumMagicDamage;
        options.minimumMagicDamageDefined = true;
    }
    if (!options.magicEffectCalculationModeDefined &&
        profile.magicEffectCalculationModeDefined)
    {
        options.magicEffectCalculationMode =
            profile.magicEffectCalculationMode;
        options.magicEffectCalculationModeDefined = true;
    }
    if (!options.npcActionProfileDefined &&
        profile.npcActionProfileDefined)
    {
        options.npcActionProfile = profile.npcActionProfile;
        options.npcActionProfileDefined = true;
    }
    if (!options.npcRuntimeProfileDefined &&
        profile.npcRuntimeProfileDefined)
    {
        options.npcRuntimeProfile = profile.npcRuntimeProfile;
        options.npcRuntimeProfileDefined = true;
    }
    if (!options.specialActionModeDefined &&
        profile.specialActionModeDefined)
    {
        options.specialActionMode = profile.specialActionMode;
        options.specialActionModeDefined = true;
    }
    if (!options.addLifeModeDefined && profile.addLifeModeDefined)
    {
        options.addLifeMode = profile.addLifeMode;
        options.addLifeModeDefined = true;
    }
    if (!options.titleMusicDefined && titleMusicIsExplicit)
    {
        options.titleMusic = profile.titleMusic;
        options.titleMusicDefined = true;
    }
    if (options.features.isEmpty())
    {
        options.features = profile.features;
    }
}

void applyProfileDefaults(
    const GameProfile& profile,
    AssetMigrationOptions& options)
{
    const int fallbackType = profile.typeDefined
        ? profile.type
        : options.modType;
    const bool trilogy = fallbackType == 1 || fallbackType == 2;

    if (!options.defeatedNpcExperienceModeDefined)
    {
        options.defeatedNpcExperienceMode =
            profile.defeatedNpcExperienceModeDefined
            ? profile.defeatedNpcExperienceMode
            : (fallbackType == 0
                ? DefeatedNpcExperienceMode::StoredExperience
                : DefeatedNpcExperienceMode::LevelProductWithBonus);
        options.defeatedNpcExperienceModeDefined = true;
    }
    if (!options.experienceMultiplierDefined)
    {
        options.experienceMultiplier =
            profile.experienceMultiplierDefined
            ? profile.experienceMultiplier
            : (fallbackType >= 0 && fallbackType <= 2 ? 3.0 : 1.0);
        options.experienceMultiplierDefined = true;
    }
    if (!options.levelUpThresholdModeDefined)
    {
        options.levelUpThresholdMode = profile.levelUpThresholdModeDefined
            ? profile.levelUpThresholdMode
            : (trilogy
                ? LevelUpThresholdMode::GreaterThan
                : LevelUpThresholdMode::GreaterThanOrEqual);
        options.levelUpThresholdModeDefined = true;
    }
    if (!options.partnerFollowRadiusDefined)
    {
        options.partnerFollowRadius = profile.partnerFollowRadiusDefined
            ? profile.partnerFollowRadius
            : (trilogy ? 2 : 1);
        options.partnerFollowRadiusDefined = true;
    }
    if (!options.partnerFollowRunRadiusDefined)
    {
        options.partnerFollowRunRadius = profile.partnerFollowRunRadiusDefined
            ? profile.partnerFollowRunRadius
            : 5;
        options.partnerFollowRunRadiusDefined = true;
    }
    if (!options.minimumMagicDamageDefined &&
        profile.minimumMagicDamageDefined)
    {
        options.minimumMagicDamage = profile.minimumMagicDamage;
        options.minimumMagicDamageDefined = true;
    }
    if (!options.magicEffectCalculationModeDefined)
    {
        options.magicEffectCalculationMode =
            profile.magicEffectCalculationModeDefined
                ? profile.magicEffectCalculationMode
                : (fallbackType == 2
                    ? MagicEffectCalculationMode::AddToAttack
                    : MagicEffectCalculationMode::ReplaceAttack);
        options.magicEffectCalculationModeDefined = true;
    }
    if (!options.npcActionProfileDefined)
    {
        options.npcActionProfile = profile.npcActionProfileDefined
            ? profile.npcActionProfile
            : (fallbackType == 1
                ? ScriptNpcActionProfile::Yycs
                : (fallbackType == 2
                    ? ScriptNpcActionProfile::Xjxqy
                    : ScriptNpcActionProfile::Legacy));
        options.npcActionProfileDefined = true;
    }
    if (!options.npcRuntimeProfileDefined)
    {
        options.npcRuntimeProfile = profile.npcRuntimeProfileDefined
            ? profile.npcRuntimeProfile
            : (trilogy
                ? ScriptNpcRuntimeProfile::Trilogy
                : ScriptNpcRuntimeProfile::Legacy);
        options.npcRuntimeProfileDefined = true;
    }
    if (!options.specialActionModeDefined)
    {
        options.specialActionMode = profile.specialActionModeDefined
            ? profile.specialActionMode
            : (trilogy
                ? ScriptSpecialActionMode::Overlay
                : ScriptSpecialActionMode::Replace);
        options.specialActionModeDefined = true;
    }
    if (!options.addLifeModeDefined)
    {
        options.addLifeMode = profile.addLifeModeDefined
            ? profile.addLifeMode
            : (trilogy
                ? ScriptAddLifeMode::DirectClamp
                : ScriptAddLifeMode::PlayerRules);
        options.addLifeModeDefined = true;
    }
    if (!options.titleMusicDefined)
    {
        options.titleMusic = profile.titleMusic;
        options.titleMusicDefined = true;
    }

    for (auto feature = profile.features.cbegin();
         feature != profile.features.cend(); ++feature)
    {
        setFeatureDefault(options.features, feature.key(), feature.value());
    }
    setFeatureDefault(
        options.features,
        QStringLiteral("MagicTriggerAtAnimationEnd"),
        trilogy);
    setFeatureDefault(
        options.features,
        QStringLiteral("LumAsBrightness"),
        !trilogy);
    setFeatureDefault(
        options.features,
        QStringLiteral("AmbientLumOverlay"),
        !trilogy);
    setFeatureDefault(
        options.features,
        QStringLiteral("RainSceneTint"),
        trilogy);
}

void applyTypeFallbackDefaults(AssetMigrationOptions& options)
{
    int fallbackType = options.modType;
    if (fallbackType < 0)
    {
        const QString dependencyId =
            firstDependencyId(options.dependencyId).toUpper();
        if (dependencyId == QStringLiteral("JXQY2"))
        {
            fallbackType = 0;
        }
        else if (dependencyId == QStringLiteral("YYCS"))
        {
            fallbackType = 1;
        }
        else if (dependencyId == QStringLiteral("XJXQY"))
        {
            fallbackType = 2;
        }
    }
    GameProfile fallback;
    fallback.type = fallbackType;
    fallback.typeDefined = fallbackType >= 0;
    if (fallbackType == 0)
    {
        fallback.titleMusic = QStringLiteral("ks64.mp3");
    }
    else if (fallbackType == 1)
    {
        fallback.titleMusic = QStringLiteral("mc000.mp3");
    }
    else if (fallbackType == 2)
    {
        fallback.titleMusic = QString::fromUtf8("情缘之伴奏.mp3");
    }
    applyProfileDefaults(fallback, options);
}

QString defaultTitleMusicForOptions(const AssetMigrationOptions& options)
{
    return options.titleMusicDefined
        ? options.titleMusic
        : QString();
}

UiWindowDefaultProfile uiWindowDefaultProfileForOptions(const AssetMigrationOptions& options)
{
    QString uiProfile = options.uiProfile.trimmed().toLower();
    if (uiProfile == "jxqy2")
        return UiWindowDefaultProfile::Jxqy2;
    if (uiProfile == "xjxqy")
        return UiWindowDefaultProfile::Xjxqy;
    if (uiProfile == "yycs")
        return UiWindowDefaultProfile::Yycs;

    QString dependencyId = firstDependencyId(options.dependencyId).toUpper();
    if (options.modType == 0)
        return UiWindowDefaultProfile::Jxqy2;
    if (options.modType == 1)
        return UiWindowDefaultProfile::Yycs;
    if (options.modType == 2)
        return UiWindowDefaultProfile::Xjxqy;
    if (dependencyId == "JXQY2")
        return UiWindowDefaultProfile::Jxqy2;
    if (dependencyId == "XJXQY")
        return UiWindowDefaultProfile::Xjxqy;
    return UiWindowDefaultProfile::Yycs;
}

QString uiWindowDefaultProfileName(UiWindowDefaultProfile profile)
{
    switch (profile)
    {
    case UiWindowDefaultProfile::Jxqy2:
        return "jxqy2";
    case UiWindowDefaultProfile::Xjxqy:
        return "xjxqy";
    case UiWindowDefaultProfile::Yycs:
    default:
        return "yycs";
    }
}

UiWindowDefaultProfile detectUiWindowDefaultProfile(
    const QString& sourceRoot,
    const AssetMigrationOptions& options,
    bool* authoritativeProfile = nullptr)
{
    if (authoritativeProfile != nullptr)
        *authoritativeProfile = false;
    if (!options.uiProfile.trimmed().isEmpty())
    {
        if (authoritativeProfile != nullptr)
            *authoritativeProfile = true;
        return uiWindowDefaultProfileForOptions(options);
    }

    const QSet<QString> relevantPaths = {
        "asf/ui/dialog/panel.asf",
        "asf/ui/dialog/window-dialog.asf",
        "asf/ui/top/window.asf",
        "ini/ui/top/window.ini",
        "mpc/ui/dialog/panel.mpc"
    };
    QSet<QString> sourcePaths;
    QDir source(sourceRoot);
    QDirIterator iterator(sourceRoot, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext() && sourcePaths.size() < relevantPaths.size())
    {
        const QString relativePath = lowerPathKey(source.relativeFilePath(iterator.next()));
        if (relevantPaths.contains(relativePath))
            sourcePaths.insert(relativePath);
    }

    if (sourcePaths.contains("asf/ui/dialog/window-dialog.asf"))
    {
        if (authoritativeProfile != nullptr)
            *authoritativeProfile = true;
        return UiWindowDefaultProfile::Xjxqy;
    }
    if (sourcePaths.contains("mpc/ui/dialog/panel.mpc"))
    {
        if (authoritativeProfile != nullptr)
            *authoritativeProfile = true;
        return UiWindowDefaultProfile::Jxqy2;
    }
    if (sourcePaths.contains("asf/ui/dialog/panel.asf") &&
        (sourcePaths.contains("asf/ui/top/window.asf") ||
            sourcePaths.contains("ini/ui/top/window.ini")))
    {
        if (authoritativeProfile != nullptr)
            *authoritativeProfile = true;
        return UiWindowDefaultProfile::Yycs;
    }
    return uiWindowDefaultProfileForOptions(options);
}

void appendYycsUiWindowDefaultLines(const QString& menuName, QStringList& defaultLines)
{
    if (menuName == "dialog" || menuName == "choose")
        defaultLines << "Align=alBottomCenter" << "AlignX=-45" << "AlignY=-100";
    else if (menuName == "top")
        defaultLines << "Align=alTopCenter" << "Scale=1.5" << "Stretch=true";
    else if (menuName == "goods" || menuName == "memo" || menuName == "magic")
        defaultLines << "Align=alRight" << "AlignX=30";
    else if (menuName == "bottom")
        defaultLines << "Align=alBottomCenter" << "AlignX=94";
    else if (menuName == "option" || menuName == "littlemap" || menuName == "saveload")
        defaultLines << "Align=alCenter" << "Stretch=false";
    else if (menuName == "column")
        defaultLines << "Align=alBottomCenter" << "AlignX=-211";
    else if (menuName == "message")
        defaultLines << "Align=alBottomCenter" << "AlignX=-10" << "AlignY=-71";
    else if (menuName == "system")
        defaultLines << "Align=alCenter";
    else if (menuName == "title")
        defaultLines << "Align=alClient" << "Stretch=true"
            << "KeepAspect=true" << "FadeMirroredBars=true";
    else if (menuName == "yesno")
        defaultLines << "Align=alCenter" << "AlignX=0" << "AlignY=0";
    else if (menuName == "timer")
        defaultLines << "Align=alRTCorner" << "AlignX=30";
    else if (menuName == "tooltip")
        defaultLines << "Align=alTopCenter" << "AlignY=27";
}

void appendXjxqyUiWindowDefaultLines(const QString& menuName, QStringList& defaultLines)
{
    if (menuName == "dialog" || menuName == "choose")
        defaultLines << "Align=alBottomCenter" << "AlignY=-110";
    else if (menuName == "top")
        defaultLines << "Align=alBottomCenter" << "AlignX=-274" << "AlignY=-13";
    else if (menuName == "goods" || menuName == "memo" || menuName == "magic")
        defaultLines << "Align=alRight" << "AlignX=30";
    else if (menuName == "bottom")
        defaultLines << "Align=alBottomCenter" << "AlignX=-38";
    else if (menuName == "option" || menuName == "littlemap" ||
        menuName == "saveload" || menuName == "littlegame")
    {
        defaultLines << "Align=alCenter" << "Stretch=false";
    }
    else if (menuName == "column")
        defaultLines << "Align=alBottomCenter";
    else if (menuName == "message")
        defaultLines << "Align=alBottomCenter" << "AlignY=-100";
    else if (menuName == "system" || menuName == "xiulian")
        defaultLines << "Align=alCenter";
    else if (menuName == "title")
        defaultLines << "Align=alClient" << "Stretch=true"
            << "KeepAspect=true" << "FadeMirroredBars=true";
    else if (menuName == "yesno")
        defaultLines << "Align=alTopCenter";
    else if (menuName == "timer")
        defaultLines << "Align=alRTCorner" << "AlignX=30";
}

void appendJxqy2UiWindowDefaultLines(const QString& menuName, QStringList& defaultLines)
{
    if (menuName == "buysell" || menuName == "equip" ||
        menuName == "state" || menuName == "xiulian")
    {
        defaultLines << "Align=alLTCorner" << "AlignX=-30";
    }
    else if (menuName == "dialog" || menuName == "choose")
        defaultLines << "Align=alBottomCenter" << "AlignX=0" << "AlignY=-96";
    else if (menuName == "timer")
        defaultLines << "Align=alTopCenter" << "AlignX=0" << "AlignY=0";
    else if (menuName == "tooltip")
        defaultLines << "Align=alBottomCenter" << "AlignX=-1" << "AlignY=-80";
    else if (menuName == "yesno")
        defaultLines << "Align=alCenter" << "AlignX=0" << "AlignY=0";
    else if (menuName == "mapthumbnail")
        defaultLines << "Align=alNone" << "Stretch=true";
    else if (menuName == "title")
        defaultLines << "Align=alClient" << "Stretch=true"
            << "KeepAspect=true" << "FadeMirroredBars=true";
}

bool findUiWindowDefaultLines(const QString& relativePath, UiWindowDefaultProfile profile, QStringList& defaultLines)
{
    QString path = lowerPathKey(relativePath);
    while (path.startsWith("./"))
        path = path.mid(2);

    if (profile == UiWindowDefaultProfile::Xjxqy &&
        path == "ini/ui/title/window1.ini")
    {
        appendXjxqyUiWindowDefaultLines("title", defaultLines);
        return !defaultLines.isEmpty();
    }

    const QString prefix = "ini/ui/";
    const QString suffix = "/window.ini";
    if (!path.startsWith(prefix) || !path.endsWith(suffix))
        return false;

    QString menuName = path.mid(prefix.size(), path.size() - prefix.size() - suffix.size());
    if (menuName.contains('/'))
        return false;

    switch (profile)
    {
    case UiWindowDefaultProfile::Jxqy2:
        appendJxqy2UiWindowDefaultLines(menuName, defaultLines);
        break;
    case UiWindowDefaultProfile::Xjxqy:
        appendXjxqyUiWindowDefaultLines(menuName, defaultLines);
        break;
    case UiWindowDefaultProfile::Yycs:
    default:
        appendYycsUiWindowDefaultLines(menuName, defaultLines);
        break;
    }

    return !defaultLines.isEmpty();
}

bool findUiTitleButtonDefaultLines(const QString& relativePath, QStringList& defaultLines)
{
    QString path = lowerPathKey(relativePath);
    while (path.startsWith("./"))
        path = path.mid(2);

    const QString prefix = "ini/ui/title/";
    if (!path.startsWith(prefix) || !path.endsWith(".ini"))
        return false;

    const QString fileName = path.mid(prefix.size(), path.size() - prefix.size() - 4);
    static const QSet<QString> titleButtonFiles = {
        "initbtn",
        "loadbtn",
        "teambtn",
        "exitbtn",
        "initbtn1",
        "loadbtn1",
        "teambtn1",
        "exitbtn1"
    };
    if (!titleButtonFiles.contains(fileName))
        return false;

    defaultLines << "Stretch=true";
    return true;
}

bool findYycsTopButtonDefaultLines(const QString& relativePath,
    UiWindowDefaultProfile profile, QStringList& defaultLines)
{
    if (profile != UiWindowDefaultProfile::Yycs)
        return false;

    QString path = lowerPathKey(relativePath);
    while (path.startsWith("./"))
        path = path.mid(2);

    if (!path.startsWith("ini/ui/top/btn") || !path.endsWith(".ini"))
        return false;

    defaultLines << "Stretch=true";
    return true;
}

bool findUiDefaultLines(const QString& relativePath, UiWindowDefaultProfile profile, QStringList& defaultLines)
{
    if (findUiWindowDefaultLines(relativePath, profile, defaultLines))
        return true;
    if (findUiTitleButtonDefaultLines(relativePath, defaultLines))
        return true;
    return findYycsTopButtonDefaultLines(relativePath, profile, defaultLines);
}

bool isUiDefaultKey(const QString& key)
{
    return key == "align" || key == "alignx" || key == "aligny" ||
        key == "stretch" || key == "scale" || key == "keepaspect";
}

QString iniKeyName(const QString& line)
{
    QString normalized = line;
    if (normalized.endsWith('\r'))
        normalized.chop(1);

    QString trimmed = normalized.trimmed();
    if (trimmed.startsWith(';') || trimmed.startsWith('#'))
        return QString();

    int separator = trimmed.indexOf('=');
    if (separator <= 0)
        return QString();

    return trimmed.left(separator).trimmed().toLower();
}

bool isUiPresentationKey(const QString& key)
{
    static const QSet<QString> keys = {
        QStringLiteral("left"),
        QStringLiteral("top"),
        QStringLiteral("width"),
        QStringLiteral("height"),
        QStringLiteral("font"),
        QStringLiteral("color"),
        QStringLiteral("normalcolor"),
        QStringLiteral("hovercolor"),
        QStringLiteral("presscolor"),
        QStringLiteral("align"),
        QStringLiteral("alignx"),
        QStringLiteral("aligny"),
        QStringLiteral("scale"),
        QStringLiteral("stretch"),
        QStringLiteral("keepaspect"),
        QStringLiteral("fademirroredbars"),
        QStringLiteral("scalechildren"),
        QStringLiteral("centerchildren"),
        QStringLiteral("charactersperline"),
        QStringLiteral("lineheight"),
        QStringLiteral("linecount")
    };
    return keys.contains(key);
}

QString iniSectionName(QString line)
{
    if (!line.isEmpty() && line.front() == QChar::ByteOrderMark)
        line.remove(0, 1);
    const QString trimmed = line.trimmed();
    if (!trimmed.startsWith('[') || !trimmed.endsWith(']'))
        return QString();
    return trimmed.mid(1, trimmed.size() - 2).trimmed().toLower();
}

QString alignUiPresentationText(
    const QString& localText,
    const QString& baseText)
{
    struct SectionPresentation
    {
        QStringList orderedKeys;
        QHash<QString, QString> lines;
    };

    QHash<QString, SectionPresentation> baseSections;
    QString baseSection;
    for (QString line : baseText.split('\n'))
    {
        const QString section = iniSectionName(line);
        if (!section.isEmpty())
        {
            baseSection = section;
            continue;
        }
        const QString key = iniKeyName(line);
        if (baseSection.isEmpty() || !isUiPresentationKey(key))
            continue;
        if (line.endsWith('\r'))
            line.chop(1);
        SectionPresentation& presentation = baseSections[baseSection];
        if (!presentation.lines.contains(key))
            presentation.orderedKeys.append(key);
        presentation.lines.insert(key, line);
    }
    if (baseSections.isEmpty())
        return localText;

    const bool usesCrLf = localText.contains(QStringLiteral("\r\n"));
    const QString lineEnding = usesCrLf
        ? QStringLiteral("\r\n")
        : QStringLiteral("\n");
    const bool hadFinalNewline = localText.endsWith('\n');
    QStringList localLines = localText.split('\n');
    if (hadFinalNewline && !localLines.isEmpty())
        localLines.removeLast();
    for (QString& line : localLines)
    {
        if (line.endsWith('\r'))
            line.chop(1);
    }

    QStringList output;
    QString localSection;
    QSet<QString> writtenPresentationKeys;
    auto appendMissingBaseLines = [&]()
    {
        const auto section = baseSections.constFind(localSection);
        if (section == baseSections.cend())
            return;
        for (const QString& key : section->orderedKeys)
        {
            if (!writtenPresentationKeys.contains(key))
                output.append(section->lines.value(key));
        }
    };

    for (const QString& line : localLines)
    {
        const QString sectionName = iniSectionName(line);
        if (!sectionName.isEmpty())
        {
            appendMissingBaseLines();
            localSection = sectionName;
            writtenPresentationKeys.clear();
            output.append(line);
            continue;
        }

        const auto section = baseSections.constFind(localSection);
        const QString key = iniKeyName(line);
        if (section != baseSections.cend() && isUiPresentationKey(key))
        {
            if (section->lines.contains(key) &&
                !writtenPresentationKeys.contains(key))
            {
                output.append(section->lines.value(key));
                writtenPresentationKeys.insert(key);
            }
            continue;
        }
        output.append(line);
    }
    appendMissingBaseLines();

    QString result = output.join(lineEnding);
    if (hadFinalNewline)
        result.append(lineEnding);
    return result;
}

QString iniInitValue(const QString& content, const QString& wantedKey)
{
    const QString normalizedWantedKey = wantedKey.trimmed().toLower();
    const QStringList lines = content.split('\n');
    bool inInitSection = false;
    for (const QString& line : lines)
    {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith('[') && trimmed.endsWith(']'))
        {
            inInitSection = trimmed.compare("[Init]", Qt::CaseInsensitive) == 0;
            continue;
        }
        if (!inInitSection || iniKeyName(line) != normalizedWantedKey)
            continue;

        const int separator = line.indexOf('=');
        if (separator >= 0)
            return line.mid(separator + 1).trimmed();
    }
    return QString();
}

QString setIniInitValue(QString content, const QString& key, const QString& value)
{
    const bool hadFinalNewline = content.endsWith('\n');
    QStringList lines = content.split('\n');
    if (hadFinalNewline && !lines.isEmpty())
        lines.removeLast();

    int initStart = -1;
    int initEnd = lines.size();
    for (int i = 0; i < lines.size(); i++)
    {
        const QString trimmed = lines[i].trimmed();
        if (!trimmed.startsWith('[') || !trimmed.endsWith(']'))
            continue;
        if (trimmed.compare("[Init]", Qt::CaseInsensitive) == 0)
        {
            initStart = i;
            for (int j = i + 1; j < lines.size(); j++)
            {
                const QString nextTrimmed = lines[j].trimmed();
                if (nextTrimmed.startsWith('[') && nextTrimmed.endsWith(']'))
                {
                    initEnd = j;
                    break;
                }
            }
            break;
        }
    }
    if (initStart < 0)
        return content;

    const QString normalizedKey = key.trimmed().toLower();
    for (int i = initStart + 1; i < initEnd; i++)
    {
        if (iniKeyName(lines[i]) == normalizedKey)
        {
            lines[i] = key + "=" + value;
            QString result = lines.join('\n');
            if (hadFinalNewline)
                result.append('\n');
            return result;
        }
    }

    lines.insert(initEnd, key + "=" + value);
    QString result = lines.join('\n');
    if (hadFinalNewline)
        result.append('\n');
    return result;
}

bool readImageDimensions(const QString& imagePath, int& width, int& height)
{
    QFile imageFile(imagePath);
    if (!imageFile.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = imageFile.readAll();
    imageFile.close();
    if (data.isEmpty())
        return false;

    PicFileEditor editor;
    if (!editor.loadFromBuffer(
            reinterpret_cast<const uint8_t*>(data.constData()), data.size()))
    {
        return false;
    }

    const PicFileData* picFile = editor.getPicFileData();
    if (picFile == nullptr)
        return false;

    switch (picFile->picType)
    {
    case PicType::Mpc:
    case PicType::Shd:
        width = picFile->mpcFileHead.maxWidth;
        height = picFile->mpcFileHead.maxHeight;
        break;
    case PicType::Asf100:
    case PicType::Asf101:
        width = picFile->asfFileHead.width;
        height = picFile->asfFileHead.height;
        break;
    default:
    {
        const QImage frame = editor.getFrameImage(0);
        width = frame.width();
        height = frame.height();
        break;
    }
    }
    return width > 0 && height > 0;
}

QString resolveUiImagePath(const QString& outputDir, QString logicalPath)
{
    logicalPath.replace('\\', '/');
    while (logicalPath.startsWith('/'))
        logicalPath.remove(0, 1);
    if (logicalPath.isEmpty())
        return QString();

    const QString directPath = appendPath(outputDir, logicalPath);
    if (QFileInfo::exists(directPath))
        return directPath;

    if (!logicalPath.contains('/'))
    {
        const QStringList dialogPrefixes = {
            "asf/ui/dialog/",
            "mpc/ui/dialog/",
            "asf/",
            "mpc/"
        };
        for (const QString& prefix : dialogPrefixes)
        {
            const QString candidate = appendPath(outputDir, prefix + logicalPath);
            if (QFileInfo::exists(candidate))
                return candidate;
        }
    }
    return QString();
}

bool isIgnoredLegacyNonRuntimeFile(const QString& filePath, const QString& relativePath)
{
    QFileInfo fileInfo(filePath);
    if (fileInfo.fileName().compare(QStringLiteral("vssver.scc"), Qt::CaseInsensitive) == 0)
        return true;

    QString path = lowerPathKey(relativePath);
    while (path.startsWith("./"))
        path = path.mid(2);

    QString extension = fileInfo.suffix().toLower();
    if (extension == "exe" || extension == "dll" || extension == "pdb" || extension == "xnb")
        return true;
    if (extension == "zip" || extension == "rar" || extension == "7z")
        return true;
    if (path == "save" || path.startsWith("save/"))
        return true;
    if (path == "content" || path.startsWith("content/"))
        return true;
    if (fileInfo.fileName().startsWith(QStringLiteral("migration_report."), Qt::CaseInsensitive) ||
        fileInfo.fileName().startsWith(QStringLiteral("conversion_report."), Qt::CaseInsensitive) ||
        fileInfo.fileName().compare(QStringLiteral(".jxqy_asset_migration_marker"), Qt::CaseInsensitive) == 0)
    {
        return true;
    }

    // Unknown custom roots and root-level files are player content. Preserve
    // them byte-for-byte instead of treating the runtime-root allowlist as a
    // deletion policy.
    return false;
}

bool mapLegacyNewGameSaveTemplatePath(const QString& relativePath, QString& mappedPath)
{
    mappedPath.clear();
    QString normalized = relativePath;
    normalized.replace("\\", "/");
    while (normalized.startsWith("./"))
        normalized = normalized.mid(2);

    QString path = lowerPathKey(normalized);
    const QString prefix = "save/rpg0/";
    if (!path.startsWith(prefix))
        return false;

    QString tail = normalized.mid(prefix.size());
    if (tail.isEmpty())
        return false;
    if (tail.compare(QStringLiteral("game.ini"), Qt::CaseInsensitive) == 0)
        tail = "game.ini";

    mappedPath = "ini/save/" + tail;
    return true;
}

bool isIdentifierStart(QChar ch)
{
    ushort value = ch.unicode();
    return ch == '_' ||
        (value >= 'A' && value <= 'Z') ||
        (value >= 'a' && value <= 'z');
}

bool isIdentifierChar(QChar ch)
{
    ushort value = ch.unicode();
    return isIdentifierStart(ch) || (value >= '0' && value <= '9');
}

bool convertLegacyJxTextToUtf8(const std::string& content, const QString& sourceEncoding, std::string& utf8Content)
{
    LegacyTextEncoding encoding = LegacyTextEncoding::Auto;
    return LegacyTextDecoder::parseEncoding(
               sourceEncoding.toStdString(), encoding) &&
        LegacyTextDecoder::decodeToUtf8(
            content, encoding, utf8Content);
}

int findLineComment(const QString& line)
{
    bool inString = false;
    bool escaped = false;
    for (int i = 0; i + 1 < line.size(); i++)
    {
        QChar ch = line[i];
        if (escaped)
        {
            escaped = false;
            continue;
        }
        if (ch == '\\' && inString)
        {
            escaped = true;
            continue;
        }
        if (ch == '"')
        {
            inString = !inString;
            continue;
        }
        if (!inString && ch == '/' && line[i + 1] == '/')
            return i;
        if (!inString && ch == '-' && line[i + 1] == '-')
            return i;
    }
    return -1;
}

bool looksLikeResourceReference(const QString& value)
{
    static const QRegularExpression resourceReferencePattern(
        QStringLiteral(
            R"(^[^\s\"<>|?*]+\.(?:ini|txt|npc|obj|map|tmx|asf|mpc|mpi|img|imp|pic|shd|wav|mp3|wma|ogg|mid|xnb|avi|wmv|mp4|mkv|bmp|png|jpg|jpeg|gif|fnt|ttf|dat)(?:[?#].*)?$)"),
        QRegularExpression::CaseInsensitiveOption);
    return resourceReferencePattern.match(value.trimmed()).hasMatch();
}

bool looksLikeResourceReferenceList(const QString& value)
{
    const QStringList entries = value.split(',', Qt::SkipEmptyParts);
    if (entries.isEmpty())
        return false;
    for (const QString& entry : entries)
    {
        if (!looksLikeResourceReference(entry))
            return false;
    }
    return true;
}

bool isIniDisplayTextKey(const QString& key)
{
    if (key == QStringLiteral("filename") ||
        key.endsWith(QStringLiteral("filename")))
    {
        return false;
    }
    return key == QStringLiteral("name") ||
        key == QStringLiteral("intro") ||
        key == QStringLiteral("description") ||
        key == QStringLiteral("text") ||
        key == QStringLiteral("format") ||
        key == QStringLiteral("caption") ||
        key == QStringLiteral("tip") ||
        key == QStringLiteral("message") ||
        key == QStringLiteral("author") ||
        key == QStringLiteral("title");
}

QString lowercaseIniResourceReferences(const QString& text)
{
    QStringList lines = text.split('\n');
    for (QString& line : lines)
    {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(';') ||
            trimmed.startsWith('#') || trimmed.startsWith('['))
        {
            continue;
        }

        const int separator = line.indexOf('=');
        if (separator <= 0)
            continue;
        const QString key = line.left(separator).trimmed().toLower();
        const QString value = line.mid(separator + 1);
        if (!isIniDisplayTextKey(key) &&
            looksLikeResourceReferenceList(value))
        {
            line = line.left(separator + 1) +
                lowercaseAsciiPath(value);
        }
    }
    return lines.join('\n');
}

const QSet<QString>& allStringArgumentsAreResourceReferences()
{
    static const QSet<QString> functions = {
        QStringLiteral("addgoods"),
        QStringLiteral("addmagic"),
        QStringLiteral("addmagicexp"),
        QStringLiteral("addrandgoods"),
        QStringLiteral("addtalent"),
        QStringLiteral("buygoods"),
        QStringLiteral("buygoodsonly"),
        QStringLiteral("cleargoods"),
        QStringLiteral("delgoods"),
        QStringLiteral("delmagic"),
        QStringLiteral("equipgoods"),
        QStringLiteral("loadmap"),
        QStringLiteral("loadnpc"),
        QStringLiteral("loadobj"),
        QStringLiteral("movemagic"),
        QStringLiteral("playmovie"),
        QStringLiteral("playmusic"),
        QStringLiteral("playrandommusic"),
        QStringLiteral("playsound"),
        QStringLiteral("runscript"),
        QStringLiteral("savenpc"),
        QStringLiteral("saveobj"),
        QStringLiteral("sellgoods"),
        QStringLiteral("setlevelfile"),
        QStringLiteral("settimescript")
    };
    return functions;
}

QString enclosingScriptFunctionName(
    const QString& line,
    qsizetype stringStart)
{
    QStringList callStack;
    bool inString = false;
    bool escaped = false;
    for (qsizetype index = 0; index < stringStart; ++index)
    {
        const QChar character = line.at(index);
        if (inString)
        {
            if (escaped)
            {
                escaped = false;
            }
            else if (character == QLatin1Char('\\'))
            {
                escaped = true;
            }
            else if (character == QLatin1Char('"'))
            {
                inString = false;
            }
            continue;
        }
        if (character == QLatin1Char('"'))
        {
            inString = true;
            continue;
        }
        if (character == QLatin1Char(')'))
        {
            if (!callStack.isEmpty())
                callStack.removeLast();
            continue;
        }
        if (character != QLatin1Char('('))
            continue;

        qsizetype nameEnd = index;
        qsizetype nameStart = nameEnd;
        while (nameStart > 0 && line.at(nameStart - 1).isSpace())
            --nameStart;
        nameEnd = nameStart;
        while (nameStart > 0)
        {
            const QChar nameCharacter = line.at(nameStart - 1);
            if (!nameCharacter.isLetterOrNumber() &&
                nameCharacter != QLatin1Char('_'))
            {
                break;
            }
            --nameStart;
        }
        callStack.append(line.mid(
            nameStart, nameEnd - nameStart).toLower());
    }
    return callStack.isEmpty() ? QString() : callStack.constLast();
}

QString lowercaseScriptResourceReferences(const QString& text)
{
    static const QRegularExpression stringLiteralPattern(
        QStringLiteral(R"(\"((?:\\.|[^\"\\])*)\")"));
    QStringList lines = text.split('\n');
    for (QString& line : lines)
    {
        const int commentPosition = findLineComment(line);
        const int codeLength = commentPosition >= 0
            ? commentPosition
            : line.size();
        const QString code = line.left(codeLength);
        struct Replacement
        {
            qsizetype start = 0;
            qsizetype length = 0;
            QString text;
        };
        QList<Replacement> replacements;
        QRegularExpressionMatchIterator matches =
            stringLiteralPattern.globalMatch(code);
        while (matches.hasNext())
        {
            const QRegularExpressionMatch match = matches.next();
            const QString value = match.captured(1);
            const QString functionName = enclosingScriptFunctionName(
                code, match.capturedStart(0));
            if (looksLikeResourceReference(value) ||
                allStringArgumentsAreResourceReferences().contains(
                    functionName))
            {
                const QString normalized = lowercaseAsciiPath(value);
                if (normalized != value)
                {
                    replacements.append({
                        match.capturedStart(1),
                        match.capturedLength(1),
                        normalized
                    });
                }
            }
        }
        for (auto replacement = replacements.crbegin();
             replacement != replacements.crend(); ++replacement)
        {
            line.replace(
                replacement->start,
                replacement->length,
                replacement->text);
        }
    }
    return lines.join('\n');
}

std::string lowercaseTextResourceReferences(
    const std::string& content,
    const QString& extension,
    bool executableScript)
{
    const QString text = QString::fromUtf8(
        content.data(), static_cast<int>(content.size()));
    if (executableScript)
    {
        return lowercaseScriptResourceReferences(text).
            toUtf8().toStdString();
    }
    if (extension == QStringLiteral("ini") ||
        extension == QStringLiteral("npc") ||
        extension == QStringLiteral("obj"))
    {
        return lowercaseIniResourceReferences(text).
            toUtf8().toStdString();
    }
    return content;
}

const QSet<QString>& runtimeScriptApis()
{
    static const QSet<QString> knownApis = []() {
        static const char* names[] = {
            "printf", "assign", "getvar", "add",
            "talk", "say", "fadein", "fadeout", "setfadelum", "setmainlum",
            "playmusic", "playrandommusic", "stopmusic", "playsound", "runscript",
            "movescreen", "sleep", "playmovie", "stopmovie", "loadmap", "loadgame",
            "setmappos", "setmaptrap", "savemaptrap", "setmaptime", "changeasfcolor",
            "changemapcolor", "loadobj", "saveobj", "addobj", "delobj", "setobjpos",
            "setobjofs", "setobjkind", "setobjscript", "clearbody", "openbox", "closebox",
            "loadnpc", "savenpc", "addnpc", "delnpc", "setnpcres", "setnpcscript",
            "setnpcdeathscript", "npcgoto", "npcgotoex", "npcgotodir", "follownpc",
            "followplayer", "enablenpcai", "disablenpcai", "npcattack", "setnpcpos",
            "setnpcdir", "setnpckind", "setnpclevel", "setnpcaction",
            "setnpcrelation", "setnpcactiontype", "setnpcactionfile",
            "npcspecialaction", "npcspecialactionex", "changelife", "changemana",
            "changethew", "getnpcstate", "addkindvalue", "setmapnpcattr",
            "setnpctalkcontent", "talkselftip", "setallnpcisenemy",
            "loadplayer", "saveplayer", "setplayerpos",
            "setplayerdir", "setplayerscn", "setplayerlum", "setlevelfile",
            "setmagiclevel", "getplayermagiclevel", "getleechcraftdifference", "movemagic", "setplayerlevel", "setplayerstate",
            "enablerun", "disablerun", "enablejump", "disablejump", "enablefight",
            "disablefight", "playergoto", "playergotoex", "playerrunto",
            "playerjumpto", "playergotodir", "setwalkisrun", "addlife", "addlifemax", "addthew",
            "addthewmax", "addmana", "addmanamax", "addattack", "adddefend",
            "addevade", "addexp", "addmoney", "equipgoods", "addrandmoney",
            "addgoods", "addrandgoods", "addmagic", "addtalent", "addonemagic", "delgoods",
            "delmagic", "addmagicexp", "fulllife", "fullthew", "fullmana",
            "updatestate", "savegoods", "loadgoods", "cleargoods", "getgoodsnum",
            "getmoneynum", "setmoneynum", "gamble", "showstealwin", "showgivegoodswin", "showmessage", "showsystemmsg", "memo", "addtomemo", "clearmemo",
            "buygoods", "buygoodsonly", "sellgoods", "returntotitle", "enableinput", "disableinput",
            "hideinterface", "hidebottomwnd", "showbottomwnd", "hidemousecursor",
            "showmousecursor", "showsnow", "showrandomsnow", "showrain", "beginrain",
            "endrain", "checkyear", "getrandnum", "getplayerlevel", "getnpccount",
            "delcurobj", "showinterface", "drawbackground", "cleareffect", "savegame",
            "clearallsave", "enablesave", "disablesave",
            "limitmana", "shownpc", "openwatereffect", "closewatereffect", "watch",
            "settrap", "setnpcdestination", "setnpcmagicfile", "setnpcmagiclevel", "setnpcclickscript",
            "setpartnerlevel", "playeraddemotion", "playeraddjustice", "getpartneridx", "movescreenex", "displaymessage",
            "disablemapscroll", "enablemapscroll", "openobj", "freemap",
            "opentimelimit", "closetimelimit", "hidetimerwnd", "settimescript",
            "choose", "chooseex", "chooseplus", "select", "playerchange", "mergenpc",
            "if", "goto", "return"
        };
        QSet<QString> result;
        for (const char* name : names)
            result.insert(QString::fromLatin1(name));
        return result;
    }();
    return knownApis;
}

QStringList scanCallsOutsideStrings(const QString& text)
{
    QStringList result;
    QStringList lines = text.split('\n');
    for (QString line : lines)
    {
        int commentPos = findLineComment(line);
        if (commentPos >= 0)
            line = line.left(commentPos);

        bool inString = false;
        bool escaped = false;
        for (int i = 0; i < line.size(); i++)
        {
            QChar ch = line[i];
            if (escaped)
            {
                escaped = false;
                continue;
            }
            if (ch == '\\' && inString)
            {
                escaped = true;
                continue;
            }
            if (ch == '"')
            {
                inString = !inString;
                continue;
            }
            if (inString || !isIdentifierStart(ch))
                continue;

            int start = i;
            i++;
            while (i < line.size() && isIdentifierChar(line[i]))
                i++;

            int after = i;
            while (after < line.size() && line[after].isSpace())
                after++;
            if (after < line.size() && line[after] == '(')
                result.append(line.mid(start, i - start).toLower());
            i--;
        }
    }
    return result;
}
}

int JxAssetMigrator::resolveMinimumMagicDamageDefault(
    const QString& outputDir,
    const AssetMigrationOptions& options)
{
    if (options.minimumMagicDamageDefined &&
        options.minimumMagicDamage >= 0)
    {
        return options.minimumMagicDamage;
    }

    GameProfile dependencyProfile;
    if (findDependencyProfile(outputDir, options, dependencyProfile) &&
        dependencyProfile.minimumMagicDamageDefined)
    {
        return dependencyProfile.minimumMagicDamage;
    }

    return 10;
}

MagicEffectCalculationMode
JxAssetMigrator::resolveMagicEffectCalculationModeDefault(
    const QString& outputDir,
    const AssetMigrationOptions& options)
{
    if (options.magicEffectCalculationModeDefined)
    {
        return options.magicEffectCalculationMode;
    }

    GameProfile dependencyProfile;
    if (findDependencyProfile(outputDir, options, dependencyProfile) &&
        dependencyProfile.magicEffectCalculationModeDefined)
    {
        return dependencyProfile.magicEffectCalculationMode;
    }

    return options.modType == 2
        ? MagicEffectCalculationMode::AddToAttack
        : MagicEffectCalculationMode::ReplaceAttack;
}

MigrationResult JxAssetMigrator::migrate(const QString& sourceDir,
    const QString& outputDir,
    const AssetMigrationOptions& options,
    AssetMigrationReport& report,
    const LogCallback& logCallback,
    const ProgressCallback& progressCallback,
    const CancelCallback& cancelCallback)
{
    report = AssetMigrationReport();
    if (sourceDir.trimmed().isEmpty() || outputDir.trimmed().isEmpty())
    {
        report.errorCount++;
        appendReportLog(report, logCallback,
            QString::fromUtf8("错误: 源目录和输出目录都不能为空。"));
        return MigrationResult::Failed;
    }

    // Convert to absolute canonical paths so relative sourceDir/outputDir work correctly.
    // Without this, QDir::cleanPath("月影传说assets") stays relative,
    // and QDir(sourceRoot).relativeFilePath() embeds the source dir name into output paths.
    QString sourceRoot = QFileInfo(sourceDir).absoluteFilePath();
    QString finalOutputRoot = QFileInfo(outputDir).absoluteFilePath();
    // Normalize separators and resolve .. components
    sourceRoot = QDir::cleanPath(sourceRoot);
    finalOutputRoot = QDir::cleanPath(finalOutputRoot);
    if (AuthoringMutationGate::wouldReplaceResourceCollection(sourceRoot))
    {
        report.errorCount++;
        appendReportLog(
            report,
            logCallback,
            QString::fromUtf8(
                "错误: 源目录不能是资源集合根或其上级目录；"
                "请选择一个具体资源包。"));
        return MigrationResult::Failed;
    }
    if (AuthoringMutationGate::wouldReplaceResourceCollection(
            finalOutputRoot))
    {
        report.errorCount++;
        appendReportLog(
            report,
            logCallback,
            QString::fromUtf8(
                "错误: 输出目录不能覆盖资源集合根或其上级目录。"));
        return MigrationResult::Failed;
    }
    auto mutationLease =
        AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(finalOutputRoot);
    if (!mutationLease)
    {
        report.errorCount++;
        appendReportLog(
            report,
            logCallback,
            QString::fromUtf8(
                "错误: 输出资源正在更新或进行其他写入。"));
        return MigrationResult::Failed;
    }
    if (!mutationLease.addResourcePath(sourceRoot))
    {
        report.errorCount++;
        appendReportLog(
            report,
            logCallback,
            QString::fromUtf8(
                "错误: 源资源正在更新或进行其他写入。"));
        return MigrationResult::Failed;
    }

    QFileInfo sourceInfo(sourceRoot);
    if (isFileSystemLink(sourceInfo))
    {
        report.errorCount++;
        appendFileOutcome(
            report,
            QStringLiteral("."),
            QString(),
            AssetResourceType::Other,
            AssetMigrationFileAction::Fail,
            QStringLiteral(
                "source-root-link-not-supported"),
            QString::fromUtf8(
                "源目录根是文件系统链接；迁移不会跟随该链接"),
            true,
            migrationEntryType(sourceInfo));
        appendReportLog(
            report,
            logCallback,
            QString::fromUtf8(
                "错误: 源目录根不能是 symlink/junction: %1")
                .arg(sourceRoot));
        return MigrationResult::Failed;
    }
    if (!sourceInfo.exists() || !sourceInfo.isDir())
    {
        report.errorCount++;
        appendReportLog(report, logCallback,
            QString::fromUtf8("错误: 源目录不存在或不是目录: %1").arg(sourceRoot));
        return MigrationResult::Failed;
    }
    const QString canonicalSourceRoot = sourceInfo.canonicalFilePath();
    if (!canonicalSourceRoot.isEmpty())
        sourceRoot = QDir::cleanPath(canonicalSourceRoot);

    QList<AssetResourceType> resourceTypes;
    if (!normalizeResourceTypes(options.resourceTypes, resourceTypes))
    {
        report.errorCount++;
        appendReportLog(report, logCallback,
            QString::fromUtf8("错误: 资源域必须是 all 或一个以上互不冲突的 scripts/maps/images/audio。"));
        return MigrationResult::Failed;
    }
    initializeResourceDomainReport(report, resourceTypes);

    AssetMigrationOptions effectiveOptions = options;
    effectiveOptions.resourceTypes = resourceTypes;
    if (effectiveOptions.modId.trimmed().isEmpty())
        effectiveOptions.modId = QFileInfo(finalOutputRoot).fileName();

    GameProfile sourceProfile;
    const QString sourceManifestPath =
        ResourcePackScanner::manifestPath(sourceRoot);
    if (sourceProfile.loadFromFile(sourceManifestPath))
    {
        INIFileEditor sourceManifest;
        const bool titleMusicIsExplicit =
            sourceManifest.loadFromFile(
                sourceManifestPath.toStdString()) &&
            sourceManifest.hasKey("Title", "Music");
        applyExplicitProfileValues(
            sourceProfile,
            titleMusicIsExplicit,
            effectiveOptions);
        if (effectiveOptions.uiProfile.trimmed().isEmpty())
        {
            effectiveOptions.uiProfile = sourceProfile.uiProfile;
        }
    }
    ResourcePackInfo dependencyPack;
    const bool dependencyProfileFound = findDependencyPack(
        finalOutputRoot,
        effectiveOptions,
        dependencyIds(effectiveOptions.dependencyId),
        dependencyPack);
    GameProfile dependencyProfile;
    if (dependencyProfileFound)
    {
        if (!mutationLease.addResourcePath(dependencyPack.rootPath))
        {
            report.errorCount++;
            appendReportLog(
                report,
                logCallback,
                QString::fromUtf8(
                    "错误: 依赖资源正在更新，无法取得一致的迁移基底。"));
            return MigrationResult::Failed;
        }
        const QString dependencyManifestPath =
            ResourcePackScanner::manifestPath(dependencyPack.rootPath);
        if (!dependencyProfile.loadFromFile(dependencyManifestPath) ||
            dependencyProfile.id.compare(
                dependencyPack.profile.id, Qt::CaseSensitive) != 0)
        {
            report.errorCount++;
            appendReportLog(
                report,
                logCallback,
                QString::fromUtf8(
                    "错误: 依赖资源在迁移开始前发生变化，请重新执行转换。"));
            return MigrationResult::Failed;
        }
        applyProfileDefaults(dependencyProfile, effectiveOptions);
    }
    else
    {
        applyTypeFallbackDefaults(effectiveOptions);
    }
    if (!effectiveOptions.minimumMagicDamageDefined)
    {
        effectiveOptions.minimumMagicDamage = 10;
        effectiveOptions.minimumMagicDamageDefined = true;
    }
    if (!effectiveOptions.magicEffectCalculationModeDefined)
    {
        effectiveOptions.magicEffectCalculationMode =
            resolveMagicEffectCalculationModeDefault(
                finalOutputRoot, effectiveOptions);
        effectiveOptions.magicEffectCalculationModeDefined = true;
    }

    const bool completeProject = isCompleteProjectMigration(resourceTypes);
    const bool imageSubsetMigration = resourceTypes.size() == 1 &&
        resourceTypes.first() == AssetResourceType::Images &&
        !options.includePrefix.isEmpty();
    const bool writesFullProfile = options.writeModProfile && completeProject;
    if ((!options.includePrefix.isEmpty() && !imageSubsetMigration) ||
        (imageSubsetMigration && !isSafeRelativeSubdirectory(options.includePrefix)) ||
        options.modType < -1 || options.modType > 3 ||
        (writesFullProfile && hasUnsafeProfileText(effectiveOptions)) ||
        (options.sourceEncoding.compare("gbk", Qt::CaseInsensitive) != 0 &&
         options.sourceEncoding.compare("utf8", Qt::CaseInsensitive) != 0) ||
        (writesFullProfile && options.modType < 0 &&
         firstDependencyId(options.dependencyId).isEmpty()))
    {
        report.errorCount++;
        appendReportLog(report, logCallback,
            QString::fromUtf8("错误: 迁移选项组合无效；继承 Type 需要内容依赖，"
                "图片子集需要安全的相对 includePrefix，配置字段不能包含换行或 NUL。"));
        return MigrationResult::Failed;
    }
    QString uiBaseRoot;
    if (completeProject)
    {
        bool authoritativeUiProfile = false;
        const UiWindowDefaultProfile detectedUiProfile =
            detectUiWindowDefaultProfile(
                sourceRoot,
                effectiveOptions,
                &authoritativeUiProfile);
        if (!authoritativeUiProfile &&
            effectiveOptions.uiProfile.trimmed().isEmpty() &&
            dependencyProfileFound &&
            !dependencyProfile.uiProfile.trimmed().isEmpty())
        {
            effectiveOptions.uiProfile =
                dependencyProfile.uiProfile.trimmed();
        }
        else
        {
            effectiveOptions.uiProfile =
                uiWindowDefaultProfileName(detectedUiProfile);
        }
        if (effectiveOptions.uiBaseId.trimmed().isEmpty() &&
            authoritativeUiProfile &&
            !effectiveOptions.dependencyId.trimmed().isEmpty() &&
            effectiveOptions.uiProfile.trimmed().compare(
                firstDependencyId(effectiveOptions.dependencyId), Qt::CaseInsensitive) != 0)
        {
            // 例如内容依赖 JXQY2、但源 UI 检测为 YYCS 的 Mod：
            // 仅在两个域明确不同时写入独立 UI 基底，避免基础资源包自依赖。
            effectiveOptions.uiBaseId = effectiveOptions.uiProfile.trimmed().toUpper();
        }
        GameProfile selectedUiBaseProfile;
        uiBaseRoot = findUiBaseRoot(
            finalOutputRoot,
            effectiveOptions,
            &selectedUiBaseProfile);
        if (!uiBaseRoot.isEmpty())
        {
            if (!mutationLease.addResourcePath(uiBaseRoot))
            {
                report.errorCount++;
                appendReportLog(
                    report,
                    logCallback,
                    QString::fromUtf8(
                        "错误: UI 基底资源正在更新，无法取得一致快照。"));
                return MigrationResult::Failed;
            }
            GameProfile lockedUiBaseProfile;
            if (!lockedUiBaseProfile.loadFromFile(
                    ResourcePackScanner::manifestPath(uiBaseRoot)) ||
                lockedUiBaseProfile.id.compare(
                    selectedUiBaseProfile.id, Qt::CaseSensitive) != 0)
            {
                report.errorCount++;
                appendReportLog(
                    report,
                    logCallback,
                    QString::fromUtf8(
                        "错误: UI 基底资源在迁移开始前发生变化，请重新执行转换。"));
                return MigrationResult::Failed;
            }
        }
    }
    for (const LegacyImageCategoryDefinition& item :
         LegacyImageMigrationPolicy::definitions())
    {
        report.legacyImageModes.insert(item.id,
            LegacyImageMigrationPolicy::modeId(
                effectiveOptions.legacyImages.mode(item.category)));
    }
    report.cropTransparentRequested =
        effectiveOptions.legacyImages.cropTransparent();
    report.cropTransparentEffective =
        effectiveOptions.legacyImages.effectiveCropTransparent();

    // Reject source=output same directory — would corrupt source and skip file scanning
    const Qt::CaseSensitivity pathCaseSensitivity = fileSystemPathCaseSensitivity();
    const QString comparisonOutputRoot =
        pathWithCanonicalExistingAncestor(finalOutputRoot);
    if (sourceRoot.compare(comparisonOutputRoot, pathCaseSensitivity) == 0)
    {
        report.errorCount++;
        appendReportLog(report, logCallback,
            QString::fromUtf8("错误: 源目录和输出目录不能相同: %1").arg(sourceRoot));
        return MigrationResult::Failed;
    }
    QString sourcePrefix = QDir::fromNativeSeparators(sourceRoot);
    if (!sourcePrefix.endsWith('/'))
        sourcePrefix += '/';
    QString normalizedOutputRoot = QDir::fromNativeSeparators(comparisonOutputRoot);
    if (normalizedOutputRoot.startsWith(sourcePrefix, pathCaseSensitivity))
    {
        report.errorCount++;
        appendReportLog(report, logCallback,
            QString::fromUtf8("错误: 输出目录不能位于源目录内部: %1").arg(finalOutputRoot));
        return MigrationResult::Failed;
    }
    QString outputPrefix = normalizedOutputRoot;
    if (!outputPrefix.endsWith('/'))
        outputPrefix += '/';
    QString normalizedSourceRoot = QDir::fromNativeSeparators(sourceRoot);
    if (normalizedSourceRoot.startsWith(outputPrefix, pathCaseSensitivity))
    {
        report.errorCount++;
        appendReportLog(report, logCallback,
            QString::fromUtf8("错误: 源目录不能位于输出目录内部: %1").arg(sourceRoot));
        return MigrationResult::Failed;
    }

    QFileInfo outputInfo(finalOutputRoot);
    if (isFileSystemLink(outputInfo))
    {
        report.errorCount++;
        appendFileOutcome(
            report,
            QStringLiteral("."),
            QStringLiteral("."),
            AssetResourceType::Other,
            AssetMigrationFileAction::Fail,
            QStringLiteral(
                "existing-output-root-link-not-supported"),
            QString::fromUtf8(
                "旧输出根是文件系统链接；迁移不会跟随该链接"),
            false,
            migrationEntryType(outputInfo));
        appendReportLog(
            report,
            logCallback,
            QString::fromUtf8(
                "错误: 输出目录根不能是 symlink/junction: %1")
                .arg(finalOutputRoot));
        return MigrationResult::Failed;
    }
    if (outputInfo.exists() && !outputInfo.isDir())
    {
        report.errorCount++;
        appendReportLog(report, logCallback,
            QString::fromUtf8("错误: 输出路径必须是普通目录: %1").arg(finalOutputRoot));
        return MigrationResult::Failed;
    }

    QDir outputDirectory(finalOutputRoot);
    if (QDir(comparisonOutputRoot).isRoot())
    {
        report.errorCount++;
        appendReportLog(report, logCallback,
            QString::fromUtf8("错误: 输出目录不能是磁盘根目录: %1").arg(finalOutputRoot));
        return MigrationResult::Failed;
    }
    if (outputDirectory.exists())
    {
        QFileInfoList existingEntries = outputDirectory.entryInfoList(
            QDir::AllEntries | QDir::NoDotAndDotDot);
        if (!existingEntries.isEmpty())
        {
            if (completeProject)
            {
                bool isPreviousMigration =
                    QFileInfo(outputDirectory.filePath("migration_report.json")).isFile() ||
                    QFileInfo(outputDirectory.filePath("migration_report.txt")).isFile() ||
                    QFileInfo(outputDirectory.filePath(kMigrationMarkerFileName)).isFile();
                if (!isPreviousMigration)
                {
                    report.errorCount++;
                    appendReportLog(report, logCallback,
                        QString::fromUtf8(
                            "错误: 输出目录非空且不是旧迁移目录，请选择空目录: %1")
                            .arg(finalOutputRoot));
                    return MigrationResult::Failed;
                }
            }
        }
    }
    const ManagedOutputDigestMap
        previousManagedOutputs =
            readMigrationMarkerProvenance(
                finalOutputRoot);

    const QString outputParent = QFileInfo(finalOutputRoot).absolutePath();
    if (!QDir().mkpath(outputParent))
    {
        report.errorCount++;
        appendReportLog(report, logCallback,
            QString::fromUtf8("错误: 无法创建迁移输出父目录: %1").arg(outputParent));
        return MigrationResult::Failed;
    }

    QString stagingTemplate = QDir(outputParent).filePath(
        "." + QFileInfo(finalOutputRoot).fileName() + ".migration-XXXXXX");
    auto stagingDirectory = std::make_unique<QTemporaryDir>(stagingTemplate);
    if (!stagingDirectory->isValid())
    {
        report.errorCount++;
        appendReportLog(report, logCallback,
            QString::fromUtf8("错误: 无法创建迁移暂存目录: %1").arg(stagingTemplate));
        return MigrationResult::Failed;
    }
    const QString outputRoot = stagingDirectory->path();

    if (completeProject && !writeMigrationMarkerFile(outputRoot))
    {
        report.errorCount++;
        appendReportLog(report, logCallback,
            QString::fromUtf8("错误: 无法写入迁移输出标记: %1")
                .arg(appendPath(outputRoot, kMigrationMarkerFileName)));
        return MigrationResult::Failed;
    }

    appendReportLog(report, logCallback, QString::fromUtf8("资源格式转换源目录: %1").arg(sourceRoot));
    appendReportLog(report, logCallback, QString::fromUtf8("资源格式转换输出目录: %1").arg(finalOutputRoot));
    appendReportLog(report, logCallback,
        QString::fromUtf8("资源域: %1").arg(report.selectedResourceTypes.join(QStringLiteral(", "))));
    QStringList imageModes;
    for (const LegacyImageCategoryDefinition& item :
         LegacyImageMigrationPolicy::definitions())
    {
        imageModes.append(QStringLiteral("%1=%2")
            .arg(item.id, report.legacyImageModes.value(item.id)));
    }
    appendReportLog(report, logCallback,
        QString::fromUtf8("旧图片策略: %1；透明裁剪请求值=%2；透明裁剪有效值=%3")
            .arg(imageModes.join(QStringLiteral(", ")),
                report.cropTransparentRequested
                    ? QStringLiteral("true") : QStringLiteral("false"),
                report.cropTransparentEffective
                    ? QStringLiteral("true") : QStringLiteral("false")));

    auto preserveFailedStaging = [&](bool wasCancelled) {
        report.cancelled = report.cancelled || wasCancelled;
        appendReportLog(report, logCallback,
            QString::fromUtf8("未发布失败/取消的迁移结果；诊断暂存目录保留在: %1")
                .arg(outputRoot));
        writeReportFile(outputRoot, report);
        writeReportJsonFile(outputRoot, report, MigrationResult::Failed);
        stagingDirectory->setAutoRemove(false);
        if (!report.reportFilePath.isEmpty())
        {
            appendReportLog(report, logCallback,
                QString::fromUtf8("迁移报告: %1").arg(report.reportFilePath));
        }
        return MigrationResult::Failed;
    };

    QStringList allDirectories;
    QStringList allFiles;
    QDirIterator iterator(
        sourceRoot,
        QDir::AllEntries |
            QDir::NoDotAndDotDot |
            QDir::Hidden |
            QDir::System,
        QDirIterator::Subdirectories);
    QDir sourceDirObj(sourceRoot);
    while (iterator.hasNext())
    {
        if (cancelCallback && cancelCallback())
        {
            report.cancelled = true;
            break;
        }
        const QString path = iterator.next();
        const QFileInfo information(path);
        if (information.isDir())
            allDirectories.append(path);
        else
            allFiles.append(path);
    }
    allDirectories.sort(Qt::CaseSensitive);
    allFiles.sort(Qt::CaseSensitive);

    QMap<QString, bool>
        scannedSourcePathIsDirectory;
    const auto reserveScannedSourcePath =
        [&](const QString& sourcePath,
            bool ordinaryDirectory)
    {
        const QString sourceRelativePath =
            normalizePath(
                sourceDirObj.relativeFilePath(
                    sourcePath));
        const QString outputPathKey =
            lowerPathKey(
                mapOutputRelativePath(
                    sourceRelativePath));
        const auto existing =
            scannedSourcePathIsDirectory.
                constFind(outputPathKey);
        scannedSourcePathIsDirectory.insert(
            outputPathKey,
            existing ==
                    scannedSourcePathIsDirectory.
                        cend()
                ? ordinaryDirectory
                : existing.value() &&
                      ordinaryDirectory);
    };
    for (const QString& directoryPath :
         allDirectories)
    {
        const QFileInfo information(
            directoryPath);
        reserveScannedSourcePath(
            directoryPath,
            information.isDir() &&
                !isFileSystemLink(
                    information));
    }
    for (const QString& filePath :
         allFiles)
    {
        reserveScannedSourcePath(
            filePath,
            false);
    }

    QSet<QString> legacyNewGameSaveTemplatePaths;
    for (const QString& filePath : allFiles)
    {
        const QString relativePath =
            normalizePath(
                sourceDirObj.relativeFilePath(
                    filePath));
        QString mappedSaveTemplatePath;
        if (mapLegacyNewGameSaveTemplatePath(
                relativePath,
                mappedSaveTemplatePath))
        {
            legacyNewGameSaveTemplatePaths.insert(
                lowerPathKey(
                    mappedSaveTemplatePath));
        }
    }

    struct SourceOutputCandidate
    {
        QString sourcePath;
        QString outputPath;
    };
    QList<SourceOutputCandidate>
        sourceOutputCandidates;
    bool sourceProvidesModProfile = false;
    auto registerSourceOutput =
        [&](const QString& sourceRelativePath,
            const QString& outputRelativePath)
    {
        SourceOutputCandidate candidate;
        candidate.sourcePath =
            sourceRelativePath;
        candidate.outputPath =
            outputRelativePath;
        sourceOutputCandidates.append(
            std::move(candidate));
    };

    if (!report.cancelled)
    {
        for (const QString& directoryPath :
             allDirectories)
        {
            const QFileInfo information(
                directoryPath);
            const QString relativePath =
                normalizePath(
                    sourceDirObj.relativeFilePath(
                        directoryPath));
            const AssetResourceType resourceType =
                resourceTypeForRelativePath(
                    relativePath);
            if (isFileSystemLink(information) ||
                isIgnoredLegacyNonRuntimeFile(
                    directoryPath,
                    relativePath) ||
                (!completeProject &&
                 !resourceTypes.contains(
                     resourceType)) ||
                (!options.includePrefix.isEmpty() &&
                 !relativePathMatchesPrefix(
                     relativePath,
                     options.includePrefix)))
            {
                continue;
            }
            registerSourceOutput(
                relativePath,
                mapOutputRelativePath(
                    relativePath));
        }

        for (const QString& filePath :
             allFiles)
        {
            const QFileInfo information(filePath);
            const QString sourceRelativePath =
                normalizePath(
                    sourceDirObj.relativeFilePath(
                        filePath));
            QString relativePath =
                sourceRelativePath;
            QString mappedSaveTemplatePath;
            const bool isLegacySaveTemplate =
                mapLegacyNewGameSaveTemplatePath(
                    relativePath,
                    mappedSaveTemplatePath);
            if (!isLegacySaveTemplate &&
                legacyNewGameSaveTemplatePaths.
                    contains(
                        lowerPathKey(
                            relativePath)))
            {
                continue;
            }
            if (isLegacySaveTemplate)
                relativePath =
                    mappedSaveTemplatePath;
            const AssetResourceType resourceType =
                resourceTypeForRelativePath(
                    relativePath);
            if (isFileSystemLink(information) ||
                isIgnoredLegacyNonRuntimeFile(
                    filePath,
                    relativePath) ||
                (!completeProject &&
                 !resourceTypes.contains(
                     resourceType)) ||
                (!options.includePrefix.isEmpty() &&
                 !relativePathMatchesPrefix(
                     relativePath,
                     options.includePrefix)))
            {
                continue;
            }
            const std::optional<
                LegacyImageCategory>
                legacyImageCategory =
                    LegacyImageMigrationPolicy::
                        classifyRelativePath(
                            relativePath);
            if (legacyImageCategory.has_value() &&
                !LegacyImageMigrationPolicy::
                     definition(
                         *legacyImageCategory).
                     entersOutput)
            {
                continue;
            }
            const QString outputRelativePath =
                mapOutputRelativePath(
                    relativePath);
            if (lowerPathKey(
                    outputRelativePath) ==
                QStringLiteral(
                    "game_profile.ini"))
            {
                sourceProvidesModProfile = true;
            }
            registerSourceOutput(
                sourceRelativePath,
                outputRelativePath);
        }
    }

    QList<QPair<QString, QString>>
        sourceOutputPaths;
    for (const SourceOutputCandidate& candidate :
         sourceOutputCandidates)
    {
        sourceOutputPaths.append(
            qMakePair(
                candidate.sourcePath,
                candidate.outputPath));
    }
    QMap<QString, QString>
        generatedProducerOutputs;
    const bool scriptsSelectedForPreflight =
        completeProject ||
        resourceTypes.contains(
            AssetResourceType::Scripts);
    const QFileInfo talkIndexSource(
        appendPath(
            sourceRoot,
            QStringLiteral(
                "script/common/Talkidx.dat")));
    const QFileInfo talkDataSource(
        appendPath(
            sourceRoot,
            QStringLiteral(
                "script/common/Talk.dat")));
    if (scriptsSelectedForPreflight &&
        talkIndexSource.isFile() &&
        !isFileSystemLink(talkIndexSource) &&
        talkDataSource.isFile() &&
        !isFileSystemLink(talkDataSource))
    {
        const QString commonProducer =
            QStringLiteral(
                "<generated:talk-dat-index:common>");
        const QString rootProducer =
            QStringLiteral(
                "<generated:talk-dat-index:root>");
        const QString commonOutput =
            QStringLiteral(
                "script/common/talkindex.txt");
        const QString rootOutput =
            QStringLiteral("talkindex.txt");
        sourceOutputPaths.append(
            qMakePair(
                commonProducer,
                commonOutput));
        sourceOutputPaths.append(
            qMakePair(
                rootProducer,
                rootOutput));
        generatedProducerOutputs.insert(
            commonProducer,
            commonOutput);
        generatedProducerOutputs.insert(
            rootProducer,
            rootOutput);
    }
    const QStringList collisionSourceList =
        assetMigrationOutputPathCollisionSources(
            sourceOutputPaths);
    const QSet<QString> collidingSourcePaths(
        collisionSourceList.cbegin(),
        collisionSourceList.cend());

    if (!collidingSourcePaths.isEmpty())
    {
        for (const QString& directoryPath :
             allDirectories)
        {
            const QString sourceRelativePath =
                normalizePath(
                    sourceDirObj.relativeFilePath(
                        directoryPath));
            const bool collision =
                collidingSourcePaths.contains(
                    sourceRelativePath);
            QString outputRelativePath =
                mapOutputRelativePath(
                    sourceRelativePath);
            QString message;
            if (collision)
            {
                message = QString::fromUtf8(
                    "多个源目录项映射到同一大小写不敏感输出路径");
            }
            appendFileOutcome(
                report,
                sourceRelativePath,
                outputRelativePath,
                resourceTypeForRelativePath(
                    sourceRelativePath),
                collision
                    ? AssetMigrationFileAction::Fail
                    : AssetMigrationFileAction::Skip,
                collision
                    ? QStringLiteral(
                          "source-output-path-collision")
                    : QStringLiteral(
                          "migration-aborted-by-output-path-collision"),
                message,
                true,
                migrationEntryType(
                    QFileInfo(directoryPath)));
            if (collision)
            {
                domainReportFor(
                    report,
                    resourceTypeForRelativePath(
                        sourceRelativePath)).
                    failedFiles++;
                report.errorCount++;
            }
        }
        for (const QString& filePath :
             allFiles)
        {
            const QString sourceRelativePath =
                normalizePath(
                    sourceDirObj.relativeFilePath(
                        filePath));
            const bool collision =
                collidingSourcePaths.contains(
                    sourceRelativePath);
            QString relativePath =
                sourceRelativePath;
            QString mappedSaveTemplatePath;
            if (mapLegacyNewGameSaveTemplatePath(
                    relativePath,
                    mappedSaveTemplatePath))
            {
                relativePath =
                    mappedSaveTemplatePath;
            }
            const QString outputRelativePath =
                mapOutputRelativePath(
                    relativePath);
            QString message;
            if (collision)
            {
                message = QString::fromUtf8(
                    "多个源文件或目录项映射到同一大小写不敏感输出路径");
            }
            appendFileOutcome(
                report,
                sourceRelativePath,
                outputRelativePath,
                resourceTypeForRelativePath(
                    relativePath),
                collision
                    ? AssetMigrationFileAction::Fail
                    : AssetMigrationFileAction::Skip,
                collision
                    ? QStringLiteral(
                          "source-output-path-collision")
                    : QStringLiteral(
                          "migration-aborted-by-output-path-collision"),
                message,
                true,
                migrationEntryType(
                    QFileInfo(filePath)));
            if (collision)
            {
                domainReportFor(
                    report,
                    resourceTypeForRelativePath(
                        relativePath)).
                    failedFiles++;
                report.errorCount++;
            }
        }
        for (auto generated =
                 generatedProducerOutputs.cbegin();
             generated !=
                 generatedProducerOutputs.cend();
             ++generated)
        {
            if (!collidingSourcePaths.contains(
                    generated.key()))
            {
                continue;
            }
            appendFileOutcome(
                report,
                QStringLiteral(
                    "script/common/Talk.dat + "
                    "script/common/Talkidx.dat"),
                generated.value(),
                AssetResourceType::Scripts,
                AssetMigrationFileAction::Fail,
                QStringLiteral(
                    "generated-output-path-collision"),
                QString::fromUtf8(
                    "Talk.dat/Talkidx.dat 复合转换输出"
                    "与显式源 talkindex 文件冲突"),
                false);
            domainReportFor(
                report,
                AssetResourceType::Scripts).
                failedFiles++;
            report.errorCount++;
        }
        appendReportLog(
            report,
            logCallback,
            QString::fromUtf8(
                "错误: 多个源目录项映射到同一输出路径；"
                "未写入任何源资源，取消发布。"));
        return preserveFailedStaging(false);
    }

    QSet<QString> directoryMatchedDomains;
    QString publishedImagePrefix;
    for (int directoryIndex = 0;
         directoryIndex < allDirectories.size();
         ++directoryIndex)
    {
        const QString directoryPath =
            allDirectories[directoryIndex];
        const QFileInfo directoryInformation(
            directoryPath);
        const QString sourceRelativePath =
            normalizePath(
                sourceDirObj.relativeFilePath(
                    directoryPath));
        const AssetResourceType resourceType =
            resourceTypeForRelativePath(
                sourceRelativePath);
        const QString outputRelativePath =
            mapOutputRelativePath(
                sourceRelativePath);
        const QString entryType =
            migrationEntryType(
                directoryInformation);

        if (report.cancelled ||
            (cancelCallback && cancelCallback()))
        {
            report.cancelled = true;
            for (int remaining = directoryIndex;
                 remaining < allDirectories.size();
                 ++remaining)
            {
                const QString remainingPath =
                    allDirectories[remaining];
                const QFileInfo remainingInformation(
                    remainingPath);
                const QString remainingRelativePath =
                    normalizePath(
                        sourceDirObj.relativeFilePath(
                            remainingPath));
                appendFileOutcome(
                    report,
                    remainingRelativePath,
                    mapOutputRelativePath(
                        remainingRelativePath),
                    resourceTypeForRelativePath(
                        remainingRelativePath),
                    AssetMigrationFileAction::Skip,
                    QStringLiteral(
                        "migration-cancelled-before-processing"),
                    QString::fromUtf8(
                        "迁移取消，目录未处理"),
                    true,
                    migrationEntryType(
                        remainingInformation));
            }
            break;
        }

        if (isFileSystemLink(
                directoryInformation))
        {
            domainReportFor(
                report,
                resourceType).failedFiles++;
            report.errorCount++;
            appendFileOutcome(
                report,
                sourceRelativePath,
                outputRelativePath,
                resourceType,
                AssetMigrationFileAction::Fail,
                QStringLiteral(
                    "source-directory-link-not-supported"),
                QString::fromUtf8(
                    "迁移不会跟随源目录中的目录 symlink/junction"),
                true,
                entryType);
            appendReportLog(
                report,
                logCallback,
                QString::fromUtf8(
                    "错误: 源目录项是目录链接，未跟随 %1")
                    .arg(sourceRelativePath));
            continue;
        }

        if (isIgnoredLegacyNonRuntimeFile(
                directoryPath,
                sourceRelativePath))
        {
            appendFileOutcome(
                report,
                sourceRelativePath,
                QString(),
                resourceType,
                AssetMigrationFileAction::Skip,
                QStringLiteral(
                    "known-non-runtime-directory"),
                QString::fromUtf8(
                    "已识别的存档、旧生成物或非运行时目录不进入发布"),
                true,
                QStringLiteral("directory"));
            continue;
        }
        if (!completeProject &&
            !resourceTypes.contains(resourceType))
        {
            appendFileOutcome(
                report,
                sourceRelativePath,
                outputRelativePath,
                resourceType,
                AssetMigrationFileAction::Skip,
                QStringLiteral(
                    "resource-domain-not-selected"),
                QString::fromUtf8(
                    "目录不属于本次选择的资源域"),
                true,
                QStringLiteral("directory"));
            continue;
        }
        if (!options.includePrefix.isEmpty() &&
            !relativePathMatchesPrefix(
                sourceRelativePath,
                options.includePrefix))
        {
            appendFileOutcome(
                report,
                sourceRelativePath,
                outputRelativePath,
                resourceType,
                AssetMigrationFileAction::Skip,
                QStringLiteral(
                    "include-prefix-not-selected"),
                QString::fromUtf8(
                    "目录不在本次图片子集前缀内"),
                true,
                QStringLiteral("directory"));
            continue;
        }

        if (imageSubsetMigration &&
            publishedImagePrefix.isEmpty())
        {
            publishedImagePrefix =
                normalizePath(
                    mapOutputRelativePath(
                        matchedCasePreservingPrefix(
                            sourceRelativePath,
                            options.includePrefix)));
        }
        directoryMatchedDomains.insert(
            assetResourceTypeId(
                resourceType));
        const QString outputPath =
            appendPath(
                outputRoot,
                outputRelativePath);
        if (!QDir().mkpath(outputPath))
        {
            domainReportFor(
                report,
                resourceType).failedFiles++;
            report.errorCount++;
            appendFileOutcome(
                report,
                sourceRelativePath,
                outputRelativePath,
                resourceType,
                AssetMigrationFileAction::Fail,
                QStringLiteral(
                    "source-directory-preserve-failed"),
                QString::fromUtf8(
                    "无法在暂存输出中创建源目录"),
                true,
                QStringLiteral("directory"));
            appendReportLog(
                report,
                logCallback,
                QString::fromUtf8(
                    "错误: 无法迁移源目录项 %1")
                    .arg(sourceRelativePath));
            continue;
        }

        appendFileOutcome(
            report,
            sourceRelativePath,
            outputRelativePath,
            resourceType,
            AssetMigrationFileAction::Copy,
            QStringLiteral(
                "preserve-source-directory"),
            QString::fromUtf8(
                "保留源目录项；空目录不会在迁移中丢失"),
            true,
            QStringLiteral("directory"));
    }

    for (int i = 0; i < allFiles.size(); i++)
    {
        if (report.cancelled ||
            (cancelCallback && cancelCallback()))
        {
            report.cancelled = true;
            appendReportLog(report, logCallback, QString::fromUtf8("用户取消资源格式转换。"));
            for (int remaining = i;
                 remaining < allFiles.size();
                 ++remaining)
            {
                const QString remainingSourcePath =
                    normalizePath(
                        sourceDirObj.relativeFilePath(
                            allFiles[remaining]));
                QString remainingOutputPath =
                    remainingSourcePath;
                QString mappedPath;
                if (mapLegacyNewGameSaveTemplatePath(
                        remainingSourcePath,
                        mappedPath))
                {
                    remainingOutputPath = mappedPath;
                }
                remainingOutputPath =
                    mapOutputRelativePath(
                        remainingOutputPath);
                appendFileOutcome(
                    report,
                    remainingSourcePath,
                    remainingOutputPath,
                    resourceTypeForRelativePath(
                        remainingOutputPath),
                    AssetMigrationFileAction::Skip,
                    QStringLiteral(
                        "migration-cancelled-before-processing"),
                    QString::fromUtf8(
                        "迁移取消，文件未处理"));
            }
            break;
        }

        QString filePath = allFiles[i];
        const QString sourceRelativePath =
            normalizePath(
                sourceDirObj.relativeFilePath(filePath));
        QString relativePath = sourceRelativePath;
        if (progressCallback)
        {
            progressCallback(
                i + 1,
                allFiles.size(),
                sourceRelativePath);
        }

        QString mappedSaveTemplatePath;
        const bool isLegacyNewGameSaveTemplate =
            mapLegacyNewGameSaveTemplatePath(relativePath, mappedSaveTemplatePath);
        if (!isLegacyNewGameSaveTemplate &&
            legacyNewGameSaveTemplatePaths.contains(lowerPathKey(relativePath)))
        {
            appendFileOutcome(
                report,
                sourceRelativePath,
                mapOutputRelativePath(relativePath),
                resourceTypeForRelativePath(relativePath),
                AssetMigrationFileAction::Skip,
                QStringLiteral(
                    "superseded-by-legacy-save-template"),
                QString::fromUtf8(
                    "同一路径由 save/rpg0 旧存档模板迁移结果提供"));
            continue;
        }
        if (isLegacyNewGameSaveTemplate)
            relativePath = mappedSaveTemplatePath;
        const QFileInfo sourceFileInformation(
            filePath);
        if (isFileSystemLink(
                sourceFileInformation))
        {
            const AssetResourceType linkedResourceType =
                resourceTypeForRelativePath(
                    relativePath);
            const QString linkedOutputRelativePath =
                mapOutputRelativePath(
                    relativePath);
            report.processedFiles++;
            AssetResourceDomainReport& resourceDomainReport =
                domainReportFor(
                    report,
                    linkedResourceType);
            resourceDomainReport.processedFiles++;
            resourceDomainReport.failedFiles++;
            report.errorCount++;
            appendFileOutcome(
                report,
                sourceRelativePath,
                linkedOutputRelativePath,
                linkedResourceType,
                AssetMigrationFileAction::Fail,
                QStringLiteral(
                    "source-file-link-not-supported"),
                QString::fromUtf8(
                    "迁移不会跟随源目录中的文件 symlink"),
                true,
                migrationEntryType(
                    sourceFileInformation));
            appendReportLog(
                report,
                logCallback,
                QString::fromUtf8(
                    "错误: 源文件是链接，未复制 %1")
                    .arg(sourceRelativePath));
            continue;
        }
        if (isIgnoredLegacyNonRuntimeFile(filePath, relativePath))
        {
            appendFileOutcome(
                report,
                sourceRelativePath,
                QString(),
                resourceTypeForRelativePath(relativePath),
                AssetMigrationFileAction::Skip,
                QStringLiteral(
                    "known-non-runtime-file"),
                QString::fromUtf8(
                    "已识别的二进制、归档、存档或旧迁移报告不进入发布"));
            continue;
        }

        const AssetResourceType resourceType =
            resourceTypeForRelativePath(relativePath);
        if (!completeProject && !resourceTypes.contains(resourceType))
        {
            appendFileOutcome(
                report,
                sourceRelativePath,
                mapOutputRelativePath(relativePath),
                resourceType,
                AssetMigrationFileAction::Skip,
                QStringLiteral(
                    "resource-domain-not-selected"),
                QString::fromUtf8(
                    "文件不属于本次选择的资源域"));
            continue;
        }
        QString outputRelativePath = mapOutputRelativePath(relativePath);
        QString outputPath = appendPath(outputRoot, outputRelativePath);
        QString extension = QFileInfo(filePath).suffix().toLower();
        const std::optional<LegacyImageCategory> legacyImageCategory =
            LegacyImageMigrationPolicy::classifyRelativePath(relativePath);
        const bool topLevelImg = isTopLevelImgPath(relativePath);
        const bool normalizeRuntimeJpeg = isRuntimeEntryJpeg(relativePath);

        if (!options.includePrefix.isEmpty() &&
            !relativePathMatchesPrefix(relativePath, options.includePrefix))
        {
            appendFileOutcome(
                report,
                sourceRelativePath,
                outputRelativePath,
                resourceType,
                AssetMigrationFileAction::Skip,
                QStringLiteral(
                    "include-prefix-not-selected"),
                QString::fromUtf8(
                    "文件不在本次图片子集前缀内"));
            continue;
        }

        if (imageSubsetMigration && publishedImagePrefix.isEmpty())
        {
            publishedImagePrefix = normalizePath(mapOutputRelativePath(
                matchedCasePreservingPrefix(relativePath, options.includePrefix)));
        }

        report.processedFiles++;
        AssetResourceDomainReport& resourceDomainReport =
            domainReportFor(report, resourceType);
        resourceDomainReport.processedFiles++;
        const int previousWrittenFiles = report.writtenFiles;
        bool ok = false;
        AssetMigrationFileAction successfulAction =
            AssetMigrationFileAction::Copy;
        QString successReason =
            QStringLiteral("raw-byte-copy");
        if (normalizeRuntimeJpeg)
        {
            successfulAction = AssetMigrationFileAction::Convert;
            successReason = QStringLiteral("runtime-entry-jpeg-normalized");
            ok = processRuntimeJpegFile(
                filePath, outputPath, relativePath, report);
        }
        else if (legacyImageCategory.has_value())
        {
            const LegacyImageCategoryDefinition& definition =
                LegacyImageMigrationPolicy::definition(
                    *legacyImageCategory);
            const LegacyImageMode mode =
                effectiveOptions.legacyImages.mode(
                    *legacyImageCategory);
            if (!definition.entersOutput)
            {
                successfulAction =
                    AssetMigrationFileAction::Skip;
                successReason =
                    QStringLiteral("legacy-image-policy-excluded");
            }
            else if (mode == LegacyImageMode::Convert &&
                     isConvertibleLegacyImageExtension(
                         extension))
            {
                successfulAction =
                    AssetMigrationFileAction::Convert;
                successReason =
                    QStringLiteral("legacy-image-converted");
            }
            else
            {
                successReason =
                    QStringLiteral("legacy-image-preserved");
            }
            ok = processImageFile(filePath, outputPath, relativePath,
                *legacyImageCategory, effectiveOptions, report);
        }
        else if (topLevelImg)
        {
            ok = processRawCopyFile(filePath, outputPath, report);
            if (ok)
                report.preservedImages++;
        }
        else if (isTextExtension(extension) &&
                 isRuntimeTextPath(relativePath))
        {
            successfulAction =
                AssetMigrationFileAction::Convert;
            successReason =
                QStringLiteral("text-normalized-to-utf8");
            ok = processTextFile(filePath, outputPath, relativePath, effectiveOptions, report);
        }
        else if (extension == "map" &&
                 isRuntimeMapPath(relativePath))
        {
            successfulAction =
                AssetMigrationFileAction::Convert;
            successReason =
                QStringLiteral("map-converted");
            ok = processMapFile(filePath, outputPath, relativePath, effectiveOptions, report);
        }
        else if (isRawImageExtension(extension))
        {
            ok = processRawCopyFile(filePath, outputPath, report);
            if (ok)
                report.preservedImages++;
        }
        else
        {
            ok = processRawCopyFile(filePath, outputPath, report);
        }
        addDomainWrittenFiles(report, resourceType, previousWrittenFiles);

        if (!ok)
        {
            resourceDomainReport.failedFiles++;
            if (legacyImageCategory.has_value() || topLevelImg ||
                isRawImageExtension(extension))
            {
                report.failedImages++;
            }
            report.errorCount++;
            appendReportLog(report, logCallback, QString::fromUtf8("错误: 转换失败 %1").arg(relativePath));
            appendFileOutcome(
                report,
                sourceRelativePath,
                outputRelativePath,
                resourceType,
                AssetMigrationFileAction::Fail,
                QStringLiteral("processing-failed"),
                QString::fromUtf8(
                    "文件读取、转换或写入失败"));
        }
        else if (report.writtenFiles ==
                 previousWrittenFiles)
        {
            appendFileOutcome(
                report,
                sourceRelativePath,
                QString(),
                resourceType,
                AssetMigrationFileAction::Skip,
                successfulAction ==
                        AssetMigrationFileAction::Skip
                    ? successReason
                    : QStringLiteral(
                          "processor-produced-no-runtime-output"),
                QString::fromUtf8(
                    "文件已检查，但未产生运行时输出"));
        }
        else
        {
            appendFileOutcome(
                report,
                sourceRelativePath,
                outputRelativePath,
                resourceType,
                successfulAction,
                successReason);
        }
    }

    // If cancelled, skip all post-processing steps that modify the output directory.
    // Only write the report so the user can see what was completed before cancellation.
    bool cancelled = report.cancelled ||
        (cancelCallback && cancelCallback());
    if (cancelled)
    {
        appendReportLog(report, logCallback, QString::fromUtf8("迁移已取消，跳过后续转换/验证步骤。"));
        return preserveFailedStaging(true);
    }

    for (AssetResourceType resourceType : resourceTypes)
    {
        if (resourceType == AssetResourceType::All ||
            domainReportFor(report, resourceType).processedFiles > 0 ||
            directoryMatchedDomains.contains(
                assetResourceTypeId(
                    resourceType)))
        {
            continue;
        }

        report.errorCount++;
        if (imageSubsetMigration)
        {
            appendReportLog(report, logCallback,
                QString::fromUtf8("错误: 图片子集未匹配任何运行时资源: %1")
                    .arg(options.includePrefix));
        }
        else
        {
            appendReportLog(report, logCallback,
                QString::fromUtf8("错误: 所选资源域未匹配任何运行时资源: %1")
                    .arg(assetResourceTypeId(resourceType)));
        }
    }

    const bool scriptsSelected = completeProject ||
        resourceTypes.contains(AssetResourceType::Scripts);
    if (scriptsSelected)
    {
        const int previousWrittenFiles = report.writtenFiles;
        convertTalkDatToTalkIndex(outputRoot, outputRoot, effectiveOptions,
            report, logCallback);
        ensureMoneyDropScripts(outputRoot, report);
        addDomainWrittenFiles(report, AssetResourceType::Scripts,
            previousWrittenFiles);
    }

    if (completeProject)
    {
        const int previousWrittenFiles = report.writtenFiles;
        ensureChooseMenuFiles(
            outputRoot,
            uiBaseRoot,
            effectiveOptions,
            report);
        alignUiPresentationWithBase(
            outputRoot,
            uiBaseRoot,
            report);
        addDomainWrittenFiles(report, AssetResourceType::Other,
            previousWrittenFiles);
    }

    if (options.convertScript && scriptsSelected)
    {
        appendReportLog(report, logCallback, QString::fromUtf8("开始检查转换后脚本语法"));
        LuaScriptSyntaxReport syntaxReport = LuaScriptSyntaxValidator::validateAssetsScripts(
            outputRoot,
            LuaScriptSyntaxValidator::ProgressCallback(),
            cancelCallback);
        report.scriptSyntaxTotalFiles = syntaxReport.totalFiles;
        report.scriptSyntaxCheckedFiles = syntaxReport.checkedFiles;
        report.scriptSyntaxSkippedFiles = syntaxReport.skippedFiles;
        appendReportLog(report, logCallback,
            QString::fromUtf8("脚本语法检查完成: 检查 %1 个，跳过 %2 个，错误 %3 个")
                .arg(syntaxReport.checkedFiles)
                .arg(syntaxReport.skippedFiles)
                .arg(syntaxReport.failedFiles));

        if (syntaxReport.scriptRootMissing)
        {
            report.warningCount++;
            appendReportLog(report, logCallback, QString::fromUtf8("警告: 输出目录缺少 script 子目录，跳过脚本语法检查"));
        }
        if (syntaxReport.cancelled)
        {
            appendReportLog(report, logCallback, QString::fromUtf8("脚本语法检查已取消。"));
            return preserveFailedStaging(true);
        }

        for (const LuaScriptSyntaxIssue& issue : syntaxReport.issues)
        {
            QString item = issue.toString();
            report.scriptSyntaxErrors.append(item);
            QString sourceRelativePath;
            QString quarantineRelativePath;
            const QString outputRelativePath =
                normalizePath(
                    QDir(outputRoot).relativeFilePath(
                        issue.filePath));
            AssetMigrationFileOutcome* outcome =
                sourceOutcomeForOutput(
                    report,
                    outputRelativePath);

            if (issue.ioFailure)
            {
                report.errorCount++;
                domainReportFor(
                    report,
                    AssetResourceType::Scripts).
                        failedFiles++;
                if (outcome)
                {
                    outcome->action =
                        AssetMigrationFileAction::Fail;
                    outcome->reason =
                        QStringLiteral(
                            "script-validation-io-failed");
                    outcome->message = item;
                }
                appendReportLog(
                    report,
                    logCallback,
                    QString::fromUtf8(
                        "错误: 脚本验证读取失败 %1")
                        .arg(item));
                continue;
            }

            if (!quarantineUnavailableScript(
                    outputRoot,
                    finalOutputRoot,
                    scannedSourcePathIsDirectory,
                    previousManagedOutputs,
                    issue.filePath,
                    sourceRelativePath,
                    quarantineRelativePath))
            {
                report.errorCount++;
                domainReportFor(
                    report,
                    AssetResourceType::Scripts).
                        failedFiles++;
                if (outcome)
                {
                    outcome->action =
                        AssetMigrationFileAction::Fail;
                    outcome->reason =
                        QStringLiteral(
                            "script-quarantine-failed");
                    outcome->message = item;
                }
                appendReportLog(
                    report,
                    logCallback,
                    QString::fromUtf8(
                        "错误: 无法隔离语法错误脚本 %1")
                        .arg(item));
                continue;
            }

            report.warningCount++;
            report.unavailableScripts.append(
                sourceRelativePath);
            if (outcome)
            {
                outcome->action =
                    AssetMigrationFileAction::Skip;
                outcome->outputPath =
                    quarantineRelativePath;
                outcome->reason =
                    QStringLiteral(
                        "lua-syntax-error-quarantined");
                outcome->message = item;
            }
            appendReportLog(
                report,
                logCallback,
                QString::fromUtf8(
                    "警告: Lua 语法错误，仅隔离对应脚本 %1；"
                    "其他有效资源仍可发布。")
                    .arg(item));
        }
    }

    if (effectiveOptions.writeModProfile && completeProject)
    {
        const int previousWrittenFiles = report.writtenFiles;
        const QString stagedProfilePath =
            appendPath(
                outputRoot,
                QStringLiteral(
                    "game_profile.ini"));
        const bool profileWritten =
            (!sourceProvidesModProfile ||
             QFileInfo(stagedProfilePath).
                 isFile()) &&
            writeModProfileFile(
                outputRoot,
                effectiveOptions,
                report);
        if (!profileWritten)
        {
            if (sourceProvidesModProfile)
            {
                AssetMigrationFileOutcome*
                    outcome =
                        sourceOutcomeForOutput(
                            report,
                            QStringLiteral(
                                "game_profile.ini"));
                const bool wasCountedAsWritten =
                    outcome &&
                    (outcome->action ==
                         AssetMigrationFileAction::
                             Copy ||
                     outcome->action ==
                         AssetMigrationFileAction::
                             Convert);
                const bool wasAlreadyFailed =
                    outcome &&
                    outcome->action ==
                        AssetMigrationFileAction::
                            Fail;
                if (wasCountedAsWritten)
                {
                    if (report.writtenFiles > 0)
                        report.writtenFiles--;
                    AssetResourceDomainReport&
                        otherDomain =
                            domainReportFor(
                                report,
                                AssetResourceType::
                                    Other);
                    if (otherDomain.writtenFiles >
                        0)
                    {
                        otherDomain.
                            writtenFiles--;
                    }
                }
                if (outcome)
                {
                    outcome->action =
                        AssetMigrationFileAction::
                            Fail;
                    outcome->reason =
                        QStringLiteral(
                            "source-mod-profile-merge-failed");
                    outcome->message =
                        QString::fromUtf8(
                            "无法在保留源清单未知内容的同时"
                            "合并迁移选项");
                }
                if (!wasAlreadyFailed)
                {
                    report.errorCount++;
                    domainReportFor(
                        report,
                        AssetResourceType::Other).
                        failedFiles++;
                }
            }
            else
            {
                report.errorCount++;
                domainReportFor(
                    report,
                    AssetResourceType::Other).
                        failedFiles++;
            }
            appendReportLog(
                report,
                logCallback,
                QString::fromUtf8(
                    "错误: 无法合并或写入 game_profile.ini。"));
        }
        else if (sourceProvidesModProfile)
        {
            // The source copy was already counted during the scan. Profile
            // composition replaces that staged file but does not represent a
            // second published file.
            report.writtenFiles =
                previousWrittenFiles;
            AssetMigrationFileOutcome* outcome =
                sourceOutcomeForOutput(
                    report,
                    QStringLiteral(
                        "game_profile.ini"));
            if (outcome &&
                outcome->action !=
                    AssetMigrationFileAction::Fail)
            {
                outcome->reason =
                    QStringLiteral(
                        "source-mod-profile-merged");
                outcome->message =
                    QString::fromUtf8(
                        "源目录已提供 game_profile.ini；"
                        "保留未知内容并用迁移选项更新规范字段");
            }
            appendReportLog(
                report,
                logCallback,
                QString::fromUtf8(
                    "源目录已提供 game_profile.ini，"
                    "已保留未知内容并合并迁移选项中的规范字段。"));
        }
        addDomainWrittenFiles(report, AssetResourceType::Other,
            previousWrittenFiles);
    }

    QSet<QString> dependencyDuplicateOmissions;
    const QString stagedProfilePath =
        appendPath(
            outputRoot,
            QStringLiteral("game_profile.ini"));
    if (report.errorCount == 0 &&
        QFileInfo(stagedProfilePath).isFile())
    {
        MigrationFallbackRoots fallbackRoots;
        QString fallbackError;
        if (!resolveMigrationFallbackRoots(
                outputParent,
                outputRoot,
                fallbackRoots,
                fallbackError))
        {
            report.errorCount++;
            appendReportLog(
                report,
                logCallback,
                QString::fromUtf8(
                    "错误: 无法解析基底资源查重顺序: %1")
                    .arg(fallbackError));
        }
        else
        {
            QStringList rootsToLock =
                fallbackRoots.content;
            for (const QString& root : fallbackRoots.ui)
            {
                if (!rootsToLock.contains(
                        root,
                        fileSystemPathCaseSensitivity()))
                {
                    rootsToLock.append(root);
                }
            }
            bool locked = true;
            for (const QString& root : rootsToLock)
            {
                if (!mutationLease.addResourcePath(root))
                {
                    locked = false;
                    break;
                }
            }
            if (!locked)
            {
                report.errorCount++;
                appendReportLog(
                    report,
                    logCallback,
                    QString::fromUtf8(
                        "错误: 基底资源正在更新，无法取得一致的查重快照。"));
            }
            else
            {
                MigrationFallbackRoots lockedFallbackRoots;
                QString lockedFallbackError;
                if (!resolveMigrationFallbackRoots(
                        outputParent,
                        outputRoot,
                        lockedFallbackRoots,
                        lockedFallbackError) ||
                    lockedFallbackRoots.content !=
                        fallbackRoots.content ||
                    lockedFallbackRoots.ui !=
                        fallbackRoots.ui)
                {
                    report.errorCount++;
                    appendReportLog(
                        report,
                        logCallback,
                        QString::fromUtf8(
                            "错误: 基底依赖链在查重前发生变化，请重新执行转换。"));
                }
                else
                {
                    QString deduplicationError;
                    if (!removeExactDependencyDuplicates(
                            outputRoot,
                            lockedFallbackRoots,
                            dependencyDuplicateOmissions,
                            report,
                            logCallback,
                            deduplicationError))
                    {
                        report.errorCount++;
                        appendReportLog(
                            report,
                            logCallback,
                            QString::fromUtf8(
                                "错误: 基底资源查重失败: %1")
                                .arg(deduplicationError));
                    }
                }
            }
        }
    }

    QStringList publishedPaths;
    if (!completeProject)
    {
        publishedPaths =
            publishedPathsForResourceTypes(resourceTypes);
        if (imageSubsetMigration)
        {
            QString imageOutputPrefix =
                publishedImagePrefix;
            while (imageOutputPrefix.startsWith('/'))
                imageOutputPrefix.remove(0, 1);
            while (imageOutputPrefix.endsWith('/'))
                imageOutputPrefix.chop(1);
            publishedPaths = {imageOutputPrefix};
        }
        if (resourceTypes.contains(
                AssetResourceType::Scripts))
        {
            QSet<QString> quarantineRoots;
            const auto addQuarantineRoot =
                [&](const QString& relativePath)
            {
                const QString root =
                    normalizedRelativePath(
                        relativePath).
                        section(
                            QLatin1Char('/'),
                            0,
                            0);
                if (root ==
                        QStringLiteral(
                            ".jxqy_migration_unavailable") ||
                    root.startsWith(
                        QStringLiteral(
                            ".jxqy_migration_unavailable-")))
                {
                    quarantineRoots.insert(root);
                }
            };
            for (const AssetMigrationFileOutcome&
                     outcome :
                 report.fileOutcomes)
            {
                if (outcome.reason ==
                    QStringLiteral(
                        "lua-syntax-error-quarantined"))
                {
                    addQuarantineRoot(
                        outcome.outputPath);
                }
            }
            for (const ManagedOutputDigest& digest :
                 previousManagedOutputs)
            {
                addQuarantineRoot(
                    digest.relativePath);
            }
            QStringList orderedQuarantineRoots =
                quarantineRoots.values();
            orderedQuarantineRoots.sort(
                Qt::CaseSensitive);
            publishedPaths.append(
                orderedQuarantineRoots);
        }
        QStringList uniquePublishedPaths;
        QSet<QString> publishedPathKeys;
        for (const QString& publishedPath :
             publishedPaths)
        {
            const QString pathKey =
                lowerPathKey(
                    publishedPath);
            if (publishedPathKeys.contains(
                    pathKey))
            {
                continue;
            }
            publishedPathKeys.insert(pathKey);
            uniquePublishedPaths.append(
                publishedPath);
        }
        publishedPaths =
            uniquePublishedPaths;
    }
    QStringList transactionPaths = publishedPaths;
    if (!completeProject)
    {
        transactionPaths
            << QStringLiteral("migration_report.txt")
            << QStringLiteral("migration_report.json")
            << QString::fromLatin1(
                   kMigrationMarkerFileName);
    }

    ManagedOutputDigestMap currentManagedOutputs;
    ManagedOutputDigestMap combinedManagedOutputs =
        previousManagedOutputs;
    ExistingOutputSnapshot existingSnapshot;
    QString snapshotErrorPath;
    QString snapshotError;
    if (report.errorCount == 0)
    {
        if (!collectExistingOutputSnapshot(
                finalOutputRoot,
                completeProject
                    ? QStringList()
                    : transactionPaths,
                existingSnapshot,
                snapshotErrorPath,
                snapshotError))
        {
            report.errorCount++;
            appendFileOutcome(
                report,
                snapshotErrorPath.isEmpty()
                    ? QStringLiteral(".")
                    : snapshotErrorPath,
                snapshotErrorPath,
                snapshotErrorPath.isEmpty()
                    ? AssetResourceType::Other
                    : resourceTypeForRelativePath(
                          snapshotErrorPath),
                AssetMigrationFileAction::Fail,
                QStringLiteral(
                    "existing-output-snapshot-failed"),
                snapshotError,
                false);
            appendReportLog(
                report,
                logCallback,
                QString::fromUtf8(
                    "错误: 无法建立旧输出发布快照，取消发布: %1")
                    .arg(snapshotError));
        }
        else if (!collectManagedOutputDigests(
                outputRoot,
                currentManagedOutputs))
        {
            report.errorCount++;
            appendReportLog(
                report,
                logCallback,
                QString::fromUtf8(
                    "错误: 无法为本次受管输出生成完整 SHA-256 清单，取消发布。"));
        }
        else
        {
            combinedManagedOutputs =
                mergeManagedOutputDigests(
                    previousManagedOutputs,
                    currentManagedOutputs,
                    dependencyDuplicateOmissions);
            applyManagedOutputDigestsToReport(
                report,
                currentManagedOutputs,
                combinedManagedOutputs);
        }
    }

    // Build a complete publish candidate before the root swap. Files from an
    // earlier output generation that this staging generation does not own are
    // copied forward byte-for-byte. Any path-shape/case conflict blocks the
    // transaction, leaving the prior output untouched.
    if (report.errorCount == 0)
    {
        mergeExistingOutputFiles(
            finalOutputRoot,
            outputRoot,
            existingSnapshot,
            previousManagedOutputs,
            currentManagedOutputs,
            dependencyDuplicateOmissions,
            completeProject
                ? QStringList()
                : publishedPaths,
            report,
            logCallback);
    }
    if (report.errorCount == 0 &&
        !writeMigrationMarkerFile(
            outputRoot,
            combinedManagedOutputs))
    {
        report.errorCount++;
        appendReportLog(
            report,
            logCallback,
            QString::fromUtf8(
                "错误: 无法写入带 SHA-256 provenance 的迁移输出标记，取消发布。"));
    }

    // Determine final status before writing report
    MigrationResult result = MigrationResult::Success;
    if (report.errorCount > 0)
    {
        appendReportLog(report, logCallback,
            QString::fromUtf8("迁移失败: %1 个错误").arg(report.errorCount));
        result = MigrationResult::Failed;
    }
    else if (report.warningCount > 0)
    {
        appendReportLog(report, logCallback,
            QString::fromUtf8("迁移完成但存在警告: %1 个").arg(report.warningCount));
        result = MigrationResult::Partial;
    }
    else
    {
        appendReportLog(report, logCallback, QString::fromUtf8("迁移流程完成，未记录处理错误"));
    }

    if (result == MigrationResult::Failed)
        return preserveFailedStaging(false);

    // Reports are part of the staged artifact. A report write failure prevents
    // publication just like any other output failure.
    bool textReportWritten = writeReportFile(outputRoot, report);
    bool jsonReportWritten = writeReportJsonFile(outputRoot, report, result);
    if (!textReportWritten || !jsonReportWritten)
    {
        report.errorCount++;
        appendReportLog(report, logCallback,
            QString::fromUtf8("错误: 无法完整写入迁移报告，取消发布新输出。"));
        return preserveFailedStaging(false);
    }

    QString retainedBackupPath;
    QString publishError;
    QString changedRelativePath;
    bool published = false;
    if (completeProject)
    {
        published = publishStagedRoot(
            outputRoot,
            finalOutputRoot,
            existingSnapshot,
            retainedBackupPath,
            publishError,
            changedRelativePath);
    }
    else
    {
        published = publishStagedEntries(
            outputRoot,
            finalOutputRoot,
            transactionPaths,
            existingSnapshot,
            retainedBackupPath,
            publishError,
            changedRelativePath);
    }

    if (!published)
    {
        report.errorCount++;
        if (!changedRelativePath.isEmpty())
        {
            appendFileOutcome(
                report,
                changedRelativePath,
                changedRelativePath,
                changedRelativePath ==
                        QStringLiteral(".")
                    ? AssetResourceType::Other
                    : resourceTypeForRelativePath(
                          changedRelativePath),
                AssetMigrationFileAction::Fail,
                QStringLiteral(
                    "existing-output-changed-before-publish"),
                QString::fromUtf8(
                    "旧输出在合并完成后、发布前发生变化"),
                false);
        }
        appendReportLog(report, logCallback,
            QString::fromUtf8("错误: %1").arg(publishError));
        return preserveFailedStaging(false);
    }

    stagingDirectory->setAutoRemove(false);
    report.reportFilePath = appendPath(finalOutputRoot, "migration_report.txt");
    report.reportJsonFilePath = appendPath(finalOutputRoot, "migration_report.json");

    if (!retainedBackupPath.isEmpty())
    {
        report.warningCount++;
        result = MigrationResult::Partial;
        const QString retainedBackupWarning =
            publishError.isEmpty()
            ? QString::fromUtf8(
                  "新输出已发布，但旧输出备份清理失败")
            : publishError;
        report.publishWarning =
            retainedBackupWarning;
        report.retainedBackupPath =
            retainedBackupPath;
        appendReportLog(
            report,
            logCallback,
            QString::fromUtf8(
                "警告: %1；备份保留在: %2")
                .arg(
                    retainedBackupWarning,
                    retainedBackupPath));
        const bool partialTextReportWritten =
            writeReportFile(
                finalOutputRoot,
                report);
        const bool partialJsonReportWritten =
            writeReportJsonFile(
                finalOutputRoot,
                report,
                result);
        if (!partialTextReportWritten ||
            !partialJsonReportWritten)
        {
            report.errorCount++;
            result =
                MigrationResult::Failed;
            appendReportLog(
                report,
                logCallback,
                QString::fromUtf8(
                    "错误: 新输出已经发布且旧输出备份仍保留，"
                    "但最终 Partial 报告重写失败；"
                    "不能把磁盘中的旧报告状态视为可信。"));

            // Retry once with the terminal Failed status. A persistent write
            // failure clears the corresponding report path, so callers never
            // receive a non-empty path to a stale Success/Partial report.
            const bool failedTextReportWritten =
                writeReportFile(
                    finalOutputRoot,
                    report);
            const bool failedJsonReportWritten =
                writeReportJsonFile(
                    finalOutputRoot,
                    report,
                    result);
            if (!failedTextReportWritten)
                report.reportFilePath.clear();
            if (!failedJsonReportWritten)
                report.reportJsonFilePath.clear();
        }
    }

    appendReportLog(report, logCallback,
        QString::fromUtf8("迁移报告: %1").arg(report.reportFilePath));
    return result;
}

QString JxAssetMigrator::normalizePath(QString path) const
{
    path.replace("\\", "/");
    while (path.startsWith("./"))
        path = path.mid(2);
    while (path.contains("//"))
        path.replace("//", "/");
    return path;
}

QString JxAssetMigrator::mapOutputRelativePath(const QString& relativePath) const
{
    QString normalized = normalizePath(relativePath);
    QStringList parts = normalized.split("/", Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return normalized;

    // Preserve original directory structure: do not remap asf/ to mpc/.
    // The engine auto-detects image format from file headers, so both
    // asf/ and mpc/ paths work at runtime regardless of directory name.
    static const QSet<QString> canonicalRuntimeRootDirectories = {
        "asf",
        "font",
        "img",
        "ini",
        "map",
        "mpc",
        "music",
        "script",
        "sound",
        "video"
    };
    QString rootDirectory = parts[0].toLower();
    if (canonicalRuntimeRootDirectories.contains(rootDirectory))
        parts[0] = rootDirectory;

    if (parts.size() == 1 &&
        parts[0].compare(
            QStringLiteral(
                "game_profile.ini"),
            Qt::CaseInsensitive) == 0)
    {
        parts[0] =
            QStringLiteral(
                "game_profile.ini");
    }

    if (parts.size() >= 3 &&
        parts[0].compare("ini", Qt::CaseInsensitive) == 0 &&
        parts[1].compare("ui", Qt::CaseInsensitive) == 0)
    {
        for (int i = 2; i < parts.size(); i++)
            parts[i] = parts[i].toLower();
    }

    if (parts.size() >= 3 &&
        parts[0].compare("script", Qt::CaseInsensitive) == 0 &&
        parts[1].compare("common", Qt::CaseInsensitive) == 0 &&
        parts.last().compare("newgame.txt", Qt::CaseInsensitive) == 0)
    {
        parts.last() = "newgame.txt";
    }

    QString outputPath = lowercaseAsciiPath(joinRelativePath(parts));
    if (isRuntimeEntryJpeg(outputPath))
        outputPath = replaceJpegExtensionWithPng(outputPath);
    return outputPath;
}

bool JxAssetMigrator::copyFileReplacing(const QString& sourcePath, const QString& outputPath, AssetMigrationReport& report)
{
    if (!ensureParentDirectory(outputPath))
        return false;

    // Atomic replace via QSaveFile: data is written to a temp file and only
    // swapped onto the target on a successful commit(). If the write or commit
    // fails, the existing target file is left intact — the previous approach
    // removed the target before renaming the temp file, which lost the old
    // file when the rename failed.
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly))
        return false;

    QSaveFile output(outputPath);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly))
        return false;

    char buffer[65536];
    qint64 bytesRead;
    bool writeOk = true;
    while ((bytesRead = source.read(buffer, sizeof(buffer))) > 0)
    {
        if (output.write(buffer, bytesRead) != bytesRead)
        {
            writeOk = false;
            break;
        }
    }
    if (bytesRead < 0)
        writeOk = false;

    source.close();

    if (!writeOk)
    {
        output.cancelWriting();
        return false;
    }
    if (!output.commit())
        return false;

    report.writtenFiles++;
    return true;
}

bool JxAssetMigrator::writeTextFileUtf8(const QString& outputPath, const std::string& content, bool withBom, AssetMigrationReport& report)
{
    if (!ensureParentDirectory(outputPath))
        return false;
    if (!content.empty() &&
        !Util::isUtf8(reinterpret_cast<const uint8_t*>(content.data()), content.size()))
    {
        return false;
    }

    QByteArray outData;
    if (withBom)
        outData.append("\xEF\xBB\xBF", 3);
    outData.append(content.data(), static_cast<int>(content.size()));

    // Use QSaveFile for reliable transactional write on all platforms.
    // On Windows, QSaveFile uses ReplaceFile API which atomically replaces
    // the target file, avoiding the issue where _wrename fails if target exists.
    QSaveFile file(outputPath);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    if (file.write(outData) != outData.size())
    {
        file.cancelWriting();
        return false;
    }

    if (!file.commit())
        return false;

    report.writtenFiles++;
    return true;
}

bool JxAssetMigrator::processTextFile(const QString& sourcePath, const QString& outputPath, const QString& relativePath,
    const AssetMigrationOptions& options, AssetMigrationReport& report)
{
    std::vector<uint8_t> buffer = Util::readFileToBuffer(sourcePath.toUtf8().toStdString());
    QString extension = QFileInfo(sourcePath).suffix().toLower();
    const bool isScriptText = isConvertibleScriptText(relativePath, extension);
    const bool isExecutableScriptText = isScriptText &&
        !isLegacyScriptDocumentation(relativePath);
    if (buffer.empty())
    {
        QFileInfo sourceInfo(sourcePath);
        if (sourceInfo.exists() && sourceInfo.size() == 0)
            return writeTextFileUtf8(outputPath, std::string(), !isExecutableScriptText, report);
        return false;
    }

    std::string content(buffer.begin(), buffer.end());
    std::string utf8Content;
    if (!convertLegacyJxTextToUtf8(content, options.sourceEncoding, utf8Content))
    {
        report.warningCount++;
        report.logLines.append(QString::fromUtf8("警告: 文本编码转换失败，跳过输出 %1").arg(relativePath));
        return false;
    }
    content = utf8Content;

    content = rewriteLegacyJxReferences(content, relativePath);

    if (lowerPathKey(relativePath) == "ini/map/mapname.ini")
        content = rewriteMapNameIniToIdentity(content);

    if (lowerPathKey(relativePath).startsWith("ini/objres/") && extension == "ini")
        content = normalizeObjectResourceIni(content, relativePath);

    if (extension == "ini")
        content = applyUiDefaults(content, relativePath, options);

    if (isExecutableScriptText && options.convertScript)
    {
        ScriptConverter converter;
        content = converter.convertScript(content);
        if (!isOrphanScriptText(relativePath))
        {
        for (const ScriptConversionDiagnostic& diagnostic : converter.getDiagnostics())
        {
            QString item = QString::fromUtf8("%1:%2 [%3] %4 | %5")
                .arg(sourcePath)
                .arg(diagnostic.lineNumber)
                .arg(QString::fromUtf8(diagnostic.category.c_str()))
                .arg(QString::fromUtf8(diagnostic.message.c_str()))
                .arg(QString::fromUtf8(diagnostic.line.c_str()));
            report.unhandledScriptStatements.append(item);
            report.warningCount++;
            report.logLines.append(QString::fromUtf8("警告: 脚本转换未处理语句 %1").arg(item));
        }
        }
        if (!isOrphanScriptText(relativePath))
            scanUnsupportedScriptApis(content, sourcePath, report);
    }

    if (isExecutableScriptText && options.replaceWavWithMp3)
        content = replacePlayMusicWavWithMp3(content);

    content = lowercaseTextResourceReferences(
        content, extension, isExecutableScriptText);

    const bool writeBom =
        !isExecutableScriptText &&
        lowerPathKey(relativePath) !=
            QStringLiteral(
                "game_profile.ini");
    return writeTextFileUtf8(
        outputPath,
        content,
        writeBom,
        report);
}

bool JxAssetMigrator::processMapFile(const QString& sourcePath, const QString& outputPath, const QString& relativePath,
    const AssetMigrationOptions& options, AssetMigrationReport& report)
{
    if (!ensureParentDirectory(outputPath))
        return false;

    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly))
        return false;
    QByteArray header = source.peek(MAP_EDITOR_HEADSTR_LEN);
    source.close();
    if (!header.startsWith("MAP File"))
    {
        report.warningCount++;
        report.logLines.append(QStringLiteral("Warning: skipped non-MAP placeholder file %1").arg(relativePath));
        return true;
    }

    MapConverter converter;
    bool ok = converter.migrateFile(
        sourcePath.toUtf8().toStdString(),
        outputPath.toUtf8().toStdString(),
        options.sourceEncoding.compare(
            QStringLiteral("gbk"), Qt::CaseInsensitive) == 0);
    if (ok)
    {
        report.writtenFiles++;
        report.convertedMaps++;
    }
    else if (!converter.getLastMessage().empty())
        report.logLines.append(QString::fromStdString(converter.getLastMessage()));
    return ok;
}

bool JxAssetMigrator::processRawCopyFile(const QString& sourcePath, const QString& outputPath, AssetMigrationReport& report)
{
    return copyFileReplacing(sourcePath, outputPath, report);
}

bool JxAssetMigrator::processRuntimeJpegFile(const QString& sourcePath,
    const QString& outputPath, const QString& relativePath,
    AssetMigrationReport& report)
{
    QImage image(sourcePath);
    if (image.isNull() || !ensureParentDirectory(outputPath))
    {
        appendReportLog(report, LogCallback(),
            QString::fromUtf8("运行时入口 JPEG 转换失败: %1")
                .arg(relativePath));
        return false;
    }

    QSaveFile output(outputPath);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) ||
        !image.convertToFormat(QImage::Format_RGBA8888).save(&output, "PNG") ||
        !output.commit())
    {
        output.cancelWriting();
        appendReportLog(report, LogCallback(),
            QString::fromUtf8("运行时入口 JPEG 写入 PNG 失败: %1")
                .arg(relativePath));
        return false;
    }

    report.writtenFiles++;
    report.convertedWithoutCrop++;
    return true;
}

bool JxAssetMigrator::processImageFile(const QString& sourcePath,
    const QString& outputPath, const QString& relativePath,
    LegacyImageCategory category, const AssetMigrationOptions& options,
    AssetMigrationReport& report)
{
    const LegacyImageCategoryDefinition& categoryDefinition =
        LegacyImageMigrationPolicy::definition(category);
    if (!categoryDefinition.entersOutput)
    {
        report.skippedUnknownImages++;
        appendReportLog(report, LogCallback(),
            QString::fromUtf8("跳过 unknown 旧图片目录文件: %1")
                .arg(relativePath));
        return true;
    }

    QString extension = QFileInfo(sourcePath).suffix().toLower();
    const LegacyImageMode mode = options.legacyImages.mode(category);
    if (mode != LegacyImageMode::Convert ||
        !isConvertibleLegacyImageExtension(extension))
    {
        const bool copied = copyFileReplacing(sourcePath, outputPath, report);
        if (copied)
        {
            if (category == LegacyImageCategory::Map)
                report.preservedMapImages++;
            else
                report.preservedImages++;
        }
        return copied;
    }

    if (!ensureParentDirectory(outputPath))
        return false;

    // Save legacy pictures as IMP/IMG content while keeping the original file
    // extension so existing runtime resource references continue to resolve.
    PicFileEditor editor;
    if (!editor.loadFromFile(sourcePath.toUtf8().toStdString()))
    {
        appendReportLog(report, LogCallback(), QString::fromUtf8("图片加载失败: %1").arg(relativePath));
        return false;
    }

    const bool cropped = options.legacyImages.shouldCrop(category);
    if (cropped)
        editor.cropTransparentEdgesAllFrames();

    if (editor.saveAsIMP(outputPath.toUtf8().toStdString()))
    {
        report.writtenFiles++;
        if (cropped)
            report.convertedAndCropped++;
        else
            report.convertedWithoutCrop++;
        return true;
    }

    appendReportLog(report, LogCallback(), QString::fromUtf8("图片转换IMP失败: %1").arg(relativePath));
    return false;
}

std::string JxAssetMigrator::rewriteLegacyJxReferences(const std::string& content, const QString& relativePath) const
{
    QString text = QString::fromUtf8(content.data(), static_cast<int>(content.size()));
    text.replace("content\\partneridx.ini", "partneridx.ini", Qt::CaseInsensitive);
    text.replace("content/partneridx.ini", "partneridx.ini", Qt::CaseInsensitive);

    const QString normalizedPath = lowerPathKey(relativePath);
    if (normalizedPath.endsWith(
            QString::fromUtf8(
                "script/map/map_029_码头/结局2紫轩死亡.txt")))
    {
        // The symmetric branch in this script is MoveScreen(1,80,1).
        text.replace(
            QStringLiteral("MoveScreen(1.80,1)"),
            QStringLiteral("MoveScreen(1,80,1)"),
            Qt::CaseInsensitive);
    }

    const bool isTitleConfiguration =
        normalizedPath.startsWith(QStringLiteral("ini/ui/title/"));
    const bool isGameProfile =
        normalizedPath == QStringLiteral("game_profile.ini");
    if (isTitleConfiguration || isGameProfile)
    {
        const QString keys = isTitleConfiguration
            ? QStringLiteral("Bitmap|Image")
            : QStringLiteral("Cover");
        const QRegularExpression jpegReference(
            QStringLiteral(
                "^(\\s*(?:%1)\\s*=\\s*[^\\r\\n]*?)\\.jpe?g(\\s*)$")
                .arg(keys),
            QRegularExpression::CaseInsensitiveOption |
                QRegularExpression::MultilineOption);
        text.replace(jpegReference, QStringLiteral("\\1.png\\2"));
    }

    return text.toUtf8().toStdString();
}

std::string JxAssetMigrator::rewriteMapNameIniToIdentity(const std::string& content) const
{
    QString text = QString::fromUtf8(content.data(), static_cast<int>(content.size()));
    QStringList lines = text.split('\n');
    bool inInitSection = false;

    for (QString& line : lines)
    {
        QString trimmed = line.trimmed();
        if (trimmed.startsWith('[') && trimmed.endsWith(']'))
        {
            inInitSection = trimmed.mid(1, trimmed.size() - 2).compare("Init", Qt::CaseInsensitive) == 0;
            continue;
        }

        if (!inInitSection || trimmed.startsWith(';') || trimmed.startsWith('#'))
            continue;

        int equalsPos = line.indexOf('=');
        if (equalsPos <= 0)
            continue;

        QString prefix = line.left(equalsPos + 1);
        QString key = line.left(equalsPos).trimmed();
        if (!key.isEmpty())
            line = prefix + key;
    }

    return lines.join('\n').toUtf8().toStdString();
}

std::string JxAssetMigrator::normalizeObjectResourceIni(const std::string& content, const QString& relativePath) const
{
    QString text = QString::fromUtf8(content.data(), static_cast<int>(content.size()));
    QStringList lines = text.split('\n');

    if (lowerPathKey(relativePath).endsWith("/obj-sound.ini"))
    {
        for (QString& line : lines)
        {
            QString trimmed = line.trimmed();
            if (trimmed.startsWith("Image=", Qt::CaseInsensitive))
            {
                QString prefix = line.left(line.indexOf('=') + 1);
                line = prefix;
            }
        }
        text = lines.join('\n');
        lines = text.split('\n');
    }

    bool hasCommonSection = false;
    for (const QString& line : lines)
    {
        QString trimmed = line.trimmed();
        if (trimmed.compare("[Common]", Qt::CaseInsensitive) == 0)
        {
            hasCommonSection = true;
            break;
        }
    }
    if (hasCommonSection)
        return text.toUtf8().toStdString();

    QStringList commonLines;
    bool inSection = false;
    bool chosenSection = false;
    QStringList currentSectionLines;
    bool currentSectionHasResource = false;
    int chosenSectionStartLine = -1;  // first content line index of chosen section
    int chosenSectionEndLine = -1;    // one past last content line index
    int sectionContentStartLine = -1;

    auto flushSection = [&](int sectionContentStart, int currentLineIndex) {
        if (!chosenSection && currentSectionHasResource)
        {
            // Only the resource keys (Image/Shade/Sound/Animation) move into [Common];
            // comments, blank lines and other config stay in the original
            // section so runtime semantics are not duplicated.
            for (const QString& l : currentSectionLines)
            {
                QString t = l.trimmed();
                if (t.startsWith("Image=", Qt::CaseInsensitive) ||
                    t.startsWith("Shade=", Qt::CaseInsensitive) ||
                    t.startsWith("Sound=", Qt::CaseInsensitive) ||
                    t.startsWith("Animation=", Qt::CaseInsensitive))
                {
                    commonLines.append(l);
                }
            }
            chosenSection = true;
            chosenSectionStartLine = sectionContentStart;
            chosenSectionEndLine = currentLineIndex;
        }
        currentSectionLines.clear();
        currentSectionHasResource = false;
    };

    for (int i = 0; i < lines.size(); i++)
    {
        QString line = lines[i];
        QString trimmed = line.trimmed();
        if (trimmed.startsWith('[') && trimmed.endsWith(']'))
        {
            if (inSection)
                flushSection(sectionContentStartLine, i);
            inSection = true;
            sectionContentStartLine = i + 1;
            continue;
        }

        if (!inSection || chosenSection)
            continue;

        currentSectionLines.append(line);
        if (trimmed.startsWith("Image=", Qt::CaseInsensitive) ||
            trimmed.startsWith("Shade=", Qt::CaseInsensitive) ||
            trimmed.startsWith("Sound=", Qt::CaseInsensitive) ||
            trimmed.startsWith("Animation=", Qt::CaseInsensitive))
        {
            currentSectionHasResource = true;
        }
    }
    if (inSection)
        flushSection(sectionContentStartLine, lines.size());

    if (commonLines.isEmpty())
        return text.toUtf8().toStdString();

    QStringList output;
    output.append("[Common]");
    output.append(commonLines);
    output.append("");
    for (int i = 0; i < lines.size(); i++)
    {
        // Skip resource key lines from the chosen section (they're now in [Common])
        if (i >= chosenSectionStartLine && i < chosenSectionEndLine)
        {
            QString trimmed = lines[i].trimmed();
            if (trimmed.startsWith("Image=", Qt::CaseInsensitive) ||
                trimmed.startsWith("Shade=", Qt::CaseInsensitive) ||
                trimmed.startsWith("Sound=", Qt::CaseInsensitive) ||
                trimmed.startsWith("Animation=", Qt::CaseInsensitive))
            {
                continue;
            }
        }
        output.append(lines[i]);
    }
    return output.join('\n').toUtf8().toStdString();
}

std::string JxAssetMigrator::applyUiDefaults(const std::string& content, const QString& relativePath,
    const AssetMigrationOptions& options) const
{
    const UiWindowDefaultProfile profile =
        uiWindowDefaultProfileForOptions(options);
    if (profile == UiWindowDefaultProfile::Yycs)
    {
        const QString path = lowerPathKey(relativePath);
        QString text = QString::fromUtf8(
            content.data(), static_cast<int>(content.size()));
        if (path == QStringLiteral("ini/ui/dialog/label.ini"))
        {
            text = setIniInitValue(text, "Left", "65");
            text = setIniInitValue(text, "Top", "30");
            text = setIniInitValue(text, "Width", "310");
            text = setIniInitValue(text, "Height", "70");
            text = setIniInitValue(text, "Font", "18");
            text = setIniInitValue(text, "CharactersPerLine", "17");
            text = setIniInitValue(text, "LineHeight", "22");
            text = setIniInitValue(text, "LineCount", "3");
            return text.toUtf8().toStdString();
        }
        if (path == QStringLiteral("ini/ui/choose/label.ini"))
        {
            text = setIniInitValue(text, "Left", "65");
            text = setIniInitValue(text, "Top", "30");
            text = setIniInitValue(text, "Width", "310");
            text = setIniInitValue(text, "Height", "22");
            text = setIniInitValue(text, "Font", "18");
            return text.toUtf8().toStdString();
        }
        if (path == QStringLiteral("ini/ui/choose/btna.ini") ||
            path == QStringLiteral("ini/ui/choose/btnb.ini"))
        {
            text = setIniInitValue(text, "Left", "65");
            text = setIniInitValue(
                text,
                "Top",
                path.endsWith(QStringLiteral("btna.ini")) ? "52" : "74");
            text = setIniInitValue(text, "Width", "310");
            text = setIniInitValue(text, "Height", "22");
            text = setIniInitValue(text, "Font", "18");
            return text.toUtf8().toStdString();
        }
        if (path == QStringLiteral("ini/ui/message/window.ini"))
        {
            text = setIniInitValue(text, "Align", "alBottomCenter");
            text = setIniInitValue(text, "AlignX", "-10");
            text = setIniInitValue(text, "AlignY", "-71");
            return text.toUtf8().toStdString();
        }
        if (path == QStringLiteral("ini/ui/message/label.ini"))
        {
            text = setIniInitValue(text, "Left", "46");
            text = setIniInitValue(text, "Top", "32");
            text = setIniInitValue(text, "Width", "148");
            text = setIniInitValue(text, "Height", "50");
            text = setIniInitValue(text, "Color", "155,34,22,204");
            return text.toUtf8().toStdString();
        }
    }

    QStringList defaultLines;
    if (!findUiDefaultLines(relativePath, profile, defaultLines))
        return content;

    QString text = QString::fromUtf8(content.data(), static_cast<int>(content.size()));
    bool hadFinalNewline = text.endsWith('\n');
    QStringList lines = text.split('\n');
    if (hadFinalNewline && !lines.isEmpty())
        lines.removeLast();

    int initStart = -1;
    int initEnd = lines.size();
    for (int i = 0; i < lines.size(); i++)
    {
        QString trimmed = lines[i].trimmed();
        if (!trimmed.startsWith('[') || !trimmed.endsWith(']'))
            continue;

        if (trimmed.compare("[Init]", Qt::CaseInsensitive) == 0)
        {
            initStart = i;
            initEnd = lines.size();
            for (int j = i + 1; j < lines.size(); j++)
            {
                QString nextTrimmed = lines[j].trimmed();
                if (nextTrimmed.startsWith('[') && nextTrimmed.endsWith(']'))
                {
                    initEnd = j;
                    break;
                }
            }
            break;
        }
    }

    if (initStart < 0)
        return content;

    const bool usesAspectFitTitle = std::any_of(
        defaultLines.cbegin(),
        defaultLines.cend(),
        [](const QString& line)
        {
            return iniKeyName(line) == "keepaspect";
        });
    if (usesAspectFitTitle)
    {
        for (int i = initStart + 1; i < initEnd;)
        {
            const QString key = iniKeyName(lines[i]);
            if (key == "scalechildren" || key == "centerchildren")
            {
                lines.removeAt(i);
                initEnd--;
                continue;
            }
            i++;
        }
    }

    QSet<QString> existingKeys;
    int lastWindowKeyIndex = -1;
    int imageKeyIndex = -1;

    for (int i = initStart + 1; i < initEnd; i++)
    {
        QString key = iniKeyName(lines[i]);
        if (isUiDefaultKey(key))
        {
            existingKeys.insert(key);
            lastWindowKeyIndex = i;
        }
        else if (key == "image")
        {
            imageKeyIndex = i;
        }
    }

    QStringList additions;
    for (const QString& line : defaultLines)
    {
        QString key = iniKeyName(line);
        if (!existingKeys.contains(key))
            additions.append(line);
    }

    if (additions.isEmpty())
        return content;

    int insertIndex = initEnd;
    if (lastWindowKeyIndex >= 0)
        insertIndex = lastWindowKeyIndex + 1;
    else if (imageKeyIndex >= 0)
        insertIndex = imageKeyIndex + 1;

    for (int i = additions.size() - 1; i >= 0; i--)
        lines.insert(insertIndex, additions[i]);

    QString result = lines.join('\n');
    if (hadFinalNewline)
        result.append('\n');
    return result.toUtf8().toStdString();
}

std::string JxAssetMigrator::replacePlayMusicWavWithMp3(const std::string& content) const
{
    std::istringstream stream(content);
    std::string result;
    std::string line;

    while (std::getline(stream, line))
    {
        std::string lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (lower.find("playmusic") != std::string::npos)
        {
            size_t pos = 0;
            while ((pos = lower.find(".wav", pos)) != std::string::npos)
            {
                line.replace(pos, 4, ".mp3");
                lower.replace(pos, 4, ".mp3");
                pos += 4;
            }
        }
        result += line + "\n";
    }

    return result;
}

void JxAssetMigrator::ensureMoneyDropScripts(
    const QString& outputDir,
    AssetMigrationReport& report)
{
    if (!QFileInfo(appendPath(
             outputDir,
             QString::fromUtf8("ini/obj/可捡钱.ini"))).isFile())
    {
        return;
    }

    const std::array<std::pair<int, int>, 7> moneyRanges = {{
        {10, 40},
        {50, 80},
        {90, 120},
        {130, 160},
        {170, 200},
        {210, 240},
        {250, 280}
    }};

    for (std::size_t index = 0; index < moneyRanges.size(); ++index)
    {
        const QString relativePath = QString::fromUtf8(
            "script/common/%1级钱.txt").arg(index + 1);
        const QString path = appendPath(outputDir, relativePath);
        const QFileInfo outputInfo(path);
        if (outputInfo.isFile())
            continue;

        if (outputInfo.exists())
        {
            report.errorCount++;
            domainReportFor(report, AssetResourceType::Scripts).
                failedFiles++;
            appendFileOutcome(
                report,
                QStringLiteral("<generated:money-drop-script>"),
                relativePath,
                AssetResourceType::Scripts,
                AssetMigrationFileAction::Fail,
                QStringLiteral("generated-money-drop-script-path-conflict"),
                QString::fromUtf8(
                    "钱袋脚本目标路径已被非文件对象占用"),
                false);
            report.logLines.append(QString::fromUtf8(
                "错误: 钱袋脚本目标路径不可用 %1")
                .arg(relativePath));
            continue;
        }

        const auto [minimumMoney, maximumMoney] = moneyRanges[index];
        const QString content = QString::fromUtf8(
            "  playsound(\"物-银子.wav\");\n"
            "  addrandmoney(%1,%2);\n"
            "  delcurobj();\n")
            .arg(minimumMoney)
            .arg(maximumMoney);
        if (writeTextFileUtf8(
                path, content.toUtf8().toStdString(), false, report))
        {
            appendFileOutcome(
                report,
                QStringLiteral("<generated:money-drop-script>"),
                relativePath,
                AssetResourceType::Scripts,
                AssetMigrationFileAction::Convert,
                QStringLiteral("generated-money-drop-script"),
                QString::fromUtf8(
                    "补齐当前资源掉落钱袋所需的等级脚本"),
                false);
            continue;
        }

        report.errorCount++;
        domainReportFor(report, AssetResourceType::Scripts).
            failedFiles++;
        appendFileOutcome(
            report,
            QStringLiteral("<generated:money-drop-script>"),
            relativePath,
            AssetResourceType::Scripts,
            AssetMigrationFileAction::Fail,
            QStringLiteral("generated-money-drop-script-write-failed"),
            QString::fromUtf8(
                "无法写入当前资源掉落钱袋的等级脚本"),
            false);
        report.logLines.append(QString::fromUtf8(
            "错误: 无法生成钱袋脚本 %1").arg(relativePath));
    }
}

bool JxAssetMigrator::alignUiPresentationWithBase(
    const QString& outputDir,
    const QString& uiBaseRoot,
    AssetMigrationReport& report)
{
    if (uiBaseRoot.isEmpty())
        return true;

    const QString outputUiRoot = appendPath(outputDir, "ini/ui");
    const QString baseUiRoot = appendPath(uiBaseRoot, "ini/ui");
    if (!QFileInfo(outputUiRoot).isDir() || !QFileInfo(baseUiRoot).isDir())
        return true;

    bool succeeded = true;
    int alignedFileCount = 0;
    const QDir outputRoot(outputDir);
    const QDir outputUiDirectory(outputUiRoot);
    QDirIterator iterator(
        outputUiRoot,
        QStringList{QStringLiteral("*.ini")},
        QDir::Files,
        QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        const QString localPath = iterator.next();
        const QString uiRelativePath =
            normalizePath(outputUiDirectory.relativeFilePath(localPath));
        const QString basePath = appendPath(baseUiRoot, uiRelativePath);
        if (!QFileInfo(basePath).isFile())
            continue;

        QFile localFile(localPath);
        QFile baseFile(basePath);
        if (!localFile.open(QIODevice::ReadOnly) ||
            !baseFile.open(QIODevice::ReadOnly))
        {
            succeeded = false;
        }
        else
        {
            const QByteArray localBytes = localFile.readAll();
            const QByteArray baseBytes = baseFile.readAll();
            localFile.close();
            baseFile.close();

            const QString localText = QString::fromUtf8(localBytes);
            const QString alignedText = alignUiPresentationText(
                localText,
                QString::fromUtf8(baseBytes));
            if (alignedText == localText)
                continue;

            QSaveFile output(localPath);
            output.setDirectWriteFallback(false);
            if (!output.open(QIODevice::WriteOnly))
            {
                succeeded = false;
            }
            else
            {
                const QByteArray alignedBytes = alignedText.toUtf8();
                if (output.write(alignedBytes) != alignedBytes.size() ||
                    !output.commit())
                {
                    output.cancelWriting();
                    succeeded = false;
                }
                else
                {
                    alignedFileCount++;
                    continue;
                }
            }
        }

        const QString outputRelativePath =
            normalizePath(outputRoot.relativeFilePath(localPath));
        report.errorCount++;
        domainReportFor(report, AssetResourceType::Other).failedFiles++;
        appendFileOutcome(
            report,
            QStringLiteral("<inherited:ui-base-presentation>"),
            outputRelativePath,
            AssetResourceType::Other,
            AssetMigrationFileAction::Fail,
            QStringLiteral("ui-base-presentation-alignment-failed"),
            QString::fromUtf8("无法按 UI 基底对齐本地界面布局"),
            false);
        report.logLines.append(
            QString::fromUtf8("错误: 无法按 UI 基底对齐界面布局 %1")
                .arg(outputRelativePath));
    }

    if (alignedFileCount > 0)
    {
        report.logLines.append(
            QString::fromUtf8("按 UI 基底对齐本地界面布局: %1 个文件；基底=%2")
                .arg(alignedFileCount)
                .arg(uiBaseRoot));
    }
    return succeeded;
}

void JxAssetMigrator::ensureChooseMenuFiles(
    const QString& outputDir,
    const QString& uiBaseRoot,
    const AssetMigrationOptions& options,
    AssetMigrationReport& report)
{
    const QString dialogWindowPath = appendPath(outputDir, "ini/ui/dialog/window.ini");
    if (!QFileInfo::exists(dialogWindowPath))
        return;

    QFile dialogWindowFile(dialogWindowPath);
    if (!dialogWindowFile.open(QIODevice::ReadOnly))
    {
        report.errorCount++;
        report.logLines.append(QString::fromUtf8("错误: 无法读取选择菜单基准文件 %1")
            .arg(dialogWindowPath));
        return;
    }
    const QByteArray dialogWindowContent = dialogWindowFile.readAll();
    dialogWindowFile.close();
    if (dialogWindowContent.isEmpty())
        return;

    QString chooseWindowContent = QString::fromUtf8(dialogWindowContent);
    const QString imageValue = iniInitValue(chooseWindowContent, "Image");
    const QString imagePath = resolveUiImagePath(outputDir, imageValue);
    int nativeImageWidth = 0;
    int nativeImageHeight = 0;
    if (!imagePath.isEmpty() && readImageDimensions(imagePath, nativeImageWidth, nativeImageHeight))
    {
        bool widthOk = false;
        const int configuredWidth = iniInitValue(chooseWindowContent, "Width").toInt(&widthOk);
        bool heightOk = false;
        const int configuredHeight = iniInitValue(chooseWindowContent, "Height").toInt(&heightOk);
        bool alignXOk = false;
        int alignX = iniInitValue(chooseWindowContent, "AlignX").toInt(&alignXOk);
        if (!alignXOk)
            alignX = 0;
        bool alignYOk = false;
        int alignY = iniInitValue(chooseWindowContent, "AlignY").toInt(&alignYOk);
        if (!alignYOk)
            alignY = 0;

        chooseWindowContent = setIniInitValue(
            chooseWindowContent, "Width", QString::number(nativeImageWidth));
        chooseWindowContent = setIniInitValue(
            chooseWindowContent, "Height", QString::number(nativeImageHeight));
        const QString align = iniInitValue(chooseWindowContent, "Align").toLower();
        if (widthOk && configuredWidth > 0 && align.contains("center"))
        {
            alignX += (nativeImageWidth - configuredWidth) / 2;
            chooseWindowContent = setIniInitValue(
                chooseWindowContent, "AlignX", QString::number(alignX));
        }
        if (heightOk && configuredHeight > 0 && nativeImageHeight > configuredHeight &&
            align.contains("bottom"))
        {
            alignY += nativeImageHeight - configuredHeight;
            chooseWindowContent = setIniInitValue(
                chooseWindowContent, "AlignY", QString::number(alignY));
        }
    }

    const UiWindowDefaultProfile profile = uiWindowDefaultProfileForOptions(options);
    if (profile == UiWindowDefaultProfile::Yycs)
    {
        chooseWindowContent = setIniInitValue(chooseWindowContent, "Left", "0");
        chooseWindowContent = setIniInitValue(chooseWindowContent, "Top", "0");
    }
    QString label;
    QString buttonA;
    QString buttonB;
    switch (profile)
    {
    case UiWindowDefaultProfile::Jxqy2:
        label =
            "[Init]\n"
            "Left=36\n"
            "Top=18\n"
            "Width=384\n"
            "Height=28\n"
            "Font=17\n"
            "Color=40,32,24\n";
        buttonA =
            "[Init]\n"
            "Left=36\n"
            "Top=52\n"
            "Width=384\n"
            "Height=24\n"
            "Font=17\n"
            "Color=30,65,145,230\n"
            "NormalColor=30,65,145,230\n"
            "HoverColor=170,45,30,240\n"
            "PressColor=170,45,30,240\n";
        buttonB = buttonA;
        buttonB.replace("Top=52", "Top=82");
        break;
    case UiWindowDefaultProfile::Xjxqy:
        label =
            "[Init]\n"
            "Left=50\n"
            "Top=14\n"
            "Width=456\n"
            "Height=18\n"
            "Font=16\n"
            "Color=255,255,255\n";
        buttonA =
            "[Init]\n"
            "Left=50\n"
            "Top=32\n"
            "Width=456\n"
            "Height=18\n"
            "Font=16\n"
            "NormalColor=80,160,255,230\n"
            "HoverColor=220,40,40,230\n"
            "PressColor=220,40,40,230\n";
        buttonB = buttonA;
        buttonB.replace("Top=32", "Top=50");
        break;
    case UiWindowDefaultProfile::Yycs:
    default:
    {
        bool panelWidthOk = false;
        const int panelWidth = iniInitValue(
            chooseWindowContent, "Width").toInt(&panelWidthOk);
        bool panelHeightOk = false;
        const int panelHeight = iniInitValue(
            chooseWindowContent, "Height").toInt(&panelHeightOk);
        const bool compactDialogPanel =
            panelWidthOk && panelHeightOk &&
            panelWidth >= 160 && panelHeight >= 72 &&
            (panelWidth < 400 || panelHeight < 110);
        if (compactDialogPanel)
        {
            const int horizontalInset = std::clamp(
                panelWidth / 14, 16, 45);
            const int contentWidth = std::max(
                1, panelWidth - horizontalInset * 2);
            const int labelTop = std::max(6, panelHeight / 10);
            const int labelHeight = std::clamp(
                panelHeight / 4, 18, 28);
            const int optionHeight = std::clamp(
                (panelHeight - labelTop - labelHeight - 7) / 2,
                20,
                24);
            const int optionATop = labelTop + labelHeight + 1;
            const int optionBTop = optionATop + optionHeight;
            label = QString(
                "[Init]\n"
                "Left=%1\n"
                "Top=%2\n"
                "Width=%3\n"
                "Height=%4\n"
                "Font=17\n"
                "Color=20,20,20\n")
                .arg(horizontalInset)
                .arg(labelTop)
                .arg(contentWidth)
                .arg(labelHeight);
            auto makeCompactButton = [&](int top)
            {
                return QString(
                    "[Init]\n"
                    "Left=%1\n"
                    "Top=%2\n"
                    "Width=%3\n"
                    "Height=%4\n"
                    "Font=17\n"
                    "Color=0,0,180\n"
                    "NormalColor=0,0,180\n"
                    "HoverColor=180,0,0\n"
                    "PressColor=180,0,0\n")
                    .arg(horizontalInset)
                    .arg(top)
                    .arg(contentWidth)
                    .arg(optionHeight);
            };
            buttonA = makeCompactButton(optionATop);
            buttonB = makeCompactButton(optionBTop);
        }
        else
        {
            label =
                "[Init]\n"
                "Left=65\n"
                "Top=30\n"
                "Width=310\n"
                "Height=22\n"
                "Font=18\n"
                "Color=20,20,20\n";
            buttonA =
                "[Init]\n"
                "Left=65\n"
                "Top=52\n"
                "Width=310\n"
                "Height=22\n"
                "Font=18\n"
                "Color=0,0,180\n"
                "NormalColor=0,0,180\n"
                "HoverColor=180,0,0\n"
                "PressColor=180,0,0\n";
            buttonB = buttonA;
            buttonB.replace("Top=52", "Top=74");
        }
        break;
    }
    }

    const QString menu =
        "[menu]\n"
        "name=ChooseMenu\n"
        "visible=false\n"
        "window=ini\\ui\\choose\\window.ini\n\n"
        "[component1]\n"
        "type=Label\n"
        "name=messageLabel\n"
        "file=ini\\ui\\choose\\label.ini\n\n"
        "[component2]\n"
        "type=ChooseTextButton\n"
        "name=selectA\n"
        "file=ini\\ui\\choose\\btnA.ini\n\n"
        "[component3]\n"
        "type=ChooseTextButton\n"
        "name=selectB\n"
        "file=ini\\ui\\choose\\btnB.ini\n";

    bool wroteAny = false;
    auto writeIfMissing = [&](const QString& relativePath, const QByteArray& content)
    {
        const QString path = appendPath(outputDir, relativePath);
        if (QFileInfo::exists(path))
            return;
        const QString inheritedPath = uiBaseRoot.isEmpty()
            ? QString()
            : appendPath(uiBaseRoot, relativePath);
        const bool inheritsBaseFile =
            !inheritedPath.isEmpty() && QFileInfo(inheritedPath).isFile();
        const bool written = inheritsBaseFile
            ? copyFileReplacing(inheritedPath, path, report)
            : writeTextFileUtf8(path, content.toStdString(), false, report);
        if (written)
        {
            wroteAny = true;
            appendFileOutcome(
                report,
                inheritsBaseFile
                    ? QStringLiteral("<inherited:ui-base>")
                    : QStringLiteral("<generated:choose-menu>"),
                relativePath,
                AssetResourceType::Other,
                AssetMigrationFileAction::Convert,
                inheritsBaseFile
                    ? QStringLiteral("inherited-ui-base-file")
                    : QStringLiteral("generated-choose-menu"),
                inheritsBaseFile
                    ? QString::fromUtf8(
                          "源资源未提供选择菜单，继承 UI 基底的现有配置")
                    : QString::fromUtf8(
                          "根据对话窗口基准生成缺失的选择菜单配置"),
                false);
        }
        else
        {
            report.errorCount++;
            domainReportFor(
                report,
                AssetResourceType::Other).
                    failedFiles++;
            appendFileOutcome(
                report,
                inheritsBaseFile
                    ? QStringLiteral("<inherited:ui-base>")
                    : QStringLiteral("<generated:choose-menu>"),
                relativePath,
                AssetResourceType::Other,
                AssetMigrationFileAction::Fail,
                inheritsBaseFile
                    ? QStringLiteral("inherited-ui-base-file-copy-failed")
                    : QStringLiteral("generated-choose-menu-write-failed"),
                inheritsBaseFile
                    ? QString::fromUtf8("无法复制 UI 基底的选择菜单配置")
                    : QString::fromUtf8("无法写入缺失的选择菜单配置"),
                false);
            report.logLines.append(
                (inheritsBaseFile
                     ? QString::fromUtf8("错误: 无法继承 UI 基底文件 %1")
                     : QString::fromUtf8("错误: 无法生成选择菜单文件 %1"))
                    .arg(relativePath));
        }
    };

    writeIfMissing("ini/ui/choose/choose.menu.ini", menu.toUtf8());
    writeIfMissing("ini/ui/choose/window.ini", chooseWindowContent.toUtf8());
    writeIfMissing("ini/ui/choose/label.ini", label.toUtf8());
    writeIfMissing("ini/ui/choose/btnA.ini", buttonA.toUtf8());
    writeIfMissing("ini/ui/choose/btnB.ini", buttonB.toUtf8());
    if (wroteAny)
    {
        QString message = uiBaseRoot.isEmpty()
            ? QString::fromUtf8(
                  "生成同源选择菜单配置: UI profile=%1").arg(options.uiProfile)
            : QString::fromUtf8(
                  "补齐选择菜单配置并优先继承 UI 基底: %1")
                  .arg(uiBaseRoot);
        if (nativeImageWidth > 0 && nativeImageHeight > 0)
        {
            message += QString::fromUtf8(", 图片尺寸=%1x%2")
                .arg(nativeImageWidth)
                .arg(nativeImageHeight);
        }
        report.logLines.append(message);
    }
}

bool JxAssetMigrator::writeModProfileFile(const QString& outputDir, const AssetMigrationOptions& options,
    AssetMigrationReport& report)
{
    QString id = options.modId.trimmed();
    if (id.isEmpty())
        id = QFileInfo(outputDir).fileName();
    if (id.isEmpty())
        id = "MOD";

    QString name = options.modName.trimmed();
    if (name.isEmpty())
        name = id;

    QString saveNamespace = options.saveNamespace.trimmed();
    if (saveNamespace.isEmpty())
        saveNamespace = id;

    const QString profilePath =
        appendPath(
            outputDir,
            QStringLiteral(
                "game_profile.ini"));
    const bool sourceProfileExists =
        QFileInfo(profilePath).isFile();
    GameProfile profile;
    if (sourceProfileExists)
    {
        if (!profile.loadFromFile(
                profilePath))
        {
            return false;
        }
    }
    else
    {
        profile.startupVideos.clear();
        profile.titleMenu =
            QStringLiteral(
                "ini\\ui\\title\\title.menu.ini");
        profile.titleNewYearMenu.clear();
        profile.titleMusic = defaultTitleMusicForOptions(options);
        profile.titleTeamVideo.clear();
        profile.teamInfoFile.clear();
        profile.newGameScript =
            QStringLiteral("newgame.txt");
    }

    if (options.titleMusicDefined)
    {
        profile.titleMusic = options.titleMusic;
    }
    profile.defeatedNpcExperienceMode =
        options.defeatedNpcExperienceMode;
    profile.defeatedNpcExperienceModeDefined =
        options.defeatedNpcExperienceModeDefined;
    profile.experienceMultiplier = options.experienceMultiplier;
    profile.experienceMultiplierDefined =
        options.experienceMultiplierDefined;
    profile.levelUpThresholdMode = options.levelUpThresholdMode;
    profile.levelUpThresholdModeDefined =
        options.levelUpThresholdModeDefined;
    profile.partnerFollowRadius = options.partnerFollowRadius;
    profile.partnerFollowRadiusDefined =
        options.partnerFollowRadiusDefined;
    profile.partnerFollowRunRadius = options.partnerFollowRunRadius;
    profile.partnerFollowRunRadiusDefined =
        options.partnerFollowRunRadiusDefined;
    profile.minimumMagicDamage = options.minimumMagicDamage;
    profile.minimumMagicDamageDefined = true;
    profile.magicEffectCalculationMode =
        options.magicEffectCalculationMode;
    profile.magicEffectCalculationModeDefined = true;
    profile.npcActionProfile = options.npcActionProfile;
    profile.npcActionProfileDefined = options.npcActionProfileDefined;
    profile.npcRuntimeProfile = options.npcRuntimeProfile;
    profile.npcRuntimeProfileDefined = options.npcRuntimeProfileDefined;
    profile.specialActionMode = options.specialActionMode;
    profile.specialActionModeDefined = options.specialActionModeDefined;
    profile.addLifeMode = options.addLifeMode;
    profile.addLifeModeDefined = options.addLifeModeDefined;

    profile.id = id;
    profile.name = name;
    profile.typeDefined =
        options.modType >= 0;
    profile.type =
        options.modType >= 0
        ? options.modType
        : 0;
    profile.dependencyId =
        options.dependencyId.trimmed();
    profile.textEncodingConverted = true;
    profile.uiBaseId =
        options.uiBaseId.trimmed();
    profile.uiProfile =
        options.uiProfile.trimmed().
            toUpper();
    profile.preferLocalUi =
        options.preferLocalUi;
    profile.features =
        options.features;
    profile.saveNamespace =
        saveNamespace;

    if (shouldFailFileSystemOperation(
            FileSystemOperation::
                PrepareModProfileOutput,
            profilePath))
    {
        return false;
    }

    QByteArray profileBytes;
    if (!profile.prepareSaveBytes(
            profilePath,
            profileBytes))
    {
        if (!sourceProfileExists)
        {
            appendFileOutcome(
                report,
                QStringLiteral("<generated:game-profile>"),
                QStringLiteral("game_profile.ini"),
                AssetResourceType::Other,
                AssetMigrationFileAction::Fail,
                QStringLiteral(
                    "generated-game-profile-prepare-failed"),
                QString::fromUtf8(
                    "无法准备默认资源包清单内容"),
                false);
        }
        return false;
    }
    const bool written = writeTextFileUtf8(
        profilePath,
        std::string(
            profileBytes.constData(),
            static_cast<std::size_t>(
                profileBytes.size())),
        false,
        report);
    if (written && !sourceProfileExists)
    {
        appendFileOutcome(
            report,
            QStringLiteral("<generated:game-profile>"),
            QStringLiteral("game_profile.ini"),
            AssetResourceType::Other,
            AssetMigrationFileAction::Convert,
            QStringLiteral("generated-game-profile"),
            QString::fromUtf8(
                "源目录未提供资源包清单，按迁移选项生成默认清单"),
            false);
    }
    else if (!written &&
             !sourceProfileExists)
    {
        appendFileOutcome(
            report,
            QStringLiteral("<generated:game-profile>"),
            QStringLiteral("game_profile.ini"),
            AssetResourceType::Other,
            AssetMigrationFileAction::Fail,
            QStringLiteral(
                "generated-game-profile-write-failed"),
            QString::fromUtf8(
                "无法写入默认资源包清单"),
            false);
    }
    return written;
}

void JxAssetMigrator::scanUnsupportedScriptApis(const std::string& convertedContent, const QString& sourcePath, AssetMigrationReport& report) const
{
    const QSet<QString>& knownApis = runtimeScriptApis();
    QString text = QString::fromUtf8(convertedContent.data(), static_cast<int>(convertedContent.size()));
    QStringList calls = scanCallsOutsideStrings(text);

    QSet<QString> existing;
    for (const QString& item : report.unsupportedScriptApis)
        existing.insert(item);
    for (const QString& call : calls)
    {
        if (knownApis.contains(call))
            continue;
        QString item = QString("%1: %2").arg(call, sourcePath);
        if (!existing.contains(item))
        {
            report.unsupportedScriptApis.append(item);
            existing.insert(item);
            report.warningCount++;
            report.logLines.append(QString::fromUtf8("警告: 脚本 API 未处理 %1").arg(item));
        }
    }
}

bool JxAssetMigrator::writeReportFile(const QString& outputDir, AssetMigrationReport& report) const
{
    QString reportPath = appendPath(outputDir, "migration_report.txt");
    report.reportFilePath.clear();
    if (!ensureParentDirectory(reportPath))
        return false;

    QSaveFile file(reportPath);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << "JX legacy assets migration report\n";
    stream << "Selected resource types: "
        << report.selectedResourceTypes.join(QStringLiteral(", ")) << "\n";
    stream << "Complete project: " << (report.completeProject ? "yes" : "no") << "\n";
    stream << "Resource domains:\n";
    for (AssetResourceType domain : assetResourceDomainTypes())
    {
        const QString id = assetResourceTypeId(domain);
        const AssetResourceDomainReport domainReport =
            report.resourceDomains.value(id);
        stream << "  " << id
            << ": selected=" << (domainReport.selected ? "yes" : "no")
            << ", processed=" << domainReport.processedFiles
            << ", written=" << domainReport.writtenFiles
            << ", failed=" << domainReport.failedFiles << "\n";
    }
    stream << "Legacy image modes:\n";
    for (const LegacyImageCategoryDefinition& item :
         LegacyImageMigrationPolicy::definitions())
    {
        stream << "  " << item.id << "="
            << report.legacyImageModes.value(item.id) << "\n";
    }
    stream << "Crop transparent: requested="
        << (report.cropTransparentRequested ? "true" : "false")
        << ", effective="
        << (report.cropTransparentEffective ? "true" : "false") << "\n";
    stream << "Processed files: " << report.processedFiles << "\n";
    stream << "Written files: " << report.writtenFiles << "\n";
    stream << "Dependency duplicate files: "
        << report.dependencyDuplicateFiles << "\n";
    stream << "Dependency duplicate bytes: "
        << report.dependencyDuplicateBytes << "\n";
    stream << "Warnings: " << report.warningCount << "\n";
    stream << "Errors: " << report.errorCount << "\n";
    stream << "convertedAndCropped: " << report.convertedAndCropped << "\n";
    stream << "convertedWithoutCrop: " << report.convertedWithoutCrop << "\n";
    stream << "preservedImages: " << report.preservedImages << "\n";
    stream << "convertedMaps: " << report.convertedMaps << "\n";
    stream << "preservedMapImages: " << report.preservedMapImages << "\n";
    stream << "skippedUnknownImages: " << report.skippedUnknownImages << "\n";
    stream << "failedImages: " << report.failedImages << "\n";
    stream << "Unsupported script APIs: " << report.unsupportedScriptApis.size() << "\n";
    stream << "Unhandled script statements: " << report.unhandledScriptStatements.size() << "\n";
    stream << "Script syntax files: total " << report.scriptSyntaxTotalFiles
        << ", checked " << report.scriptSyntaxCheckedFiles
        << ", skipped " << report.scriptSyntaxSkippedFiles << "\n";
    stream << "Script syntax errors: " << report.scriptSyntaxErrors.size() << "\n\n";
    stream << "Unavailable scripts: "
        << report.unavailableScripts.size() << "\n";
    stream << "Managed output SHA-256 entries: "
        << report.managedOutputSha256.size() << "\n";
    stream << "File outcomes: "
        << report.fileOutcomes.size() << "\n\n";

    if (!report.unsupportedScriptApis.isEmpty())
    {
        stream << "Unsupported script APIs:\n";
        for (const QString& item : report.unsupportedScriptApis)
            stream << "  " << item << "\n";
        stream << "\n";
    }

    if (!report.unhandledScriptStatements.isEmpty())
    {
        stream << "Unhandled script statements:\n";
        for (const QString& item : report.unhandledScriptStatements)
            stream << "  " << item << "\n";
        stream << "\n";
    }

    if (!report.scriptSyntaxErrors.isEmpty())
    {
        stream << "Script syntax errors:\n";
        for (const QString& item : report.scriptSyntaxErrors)
            stream << "  " << item << "\n";
        stream << "\n";
    }

    if (!report.unavailableScripts.isEmpty())
    {
        stream << "Unavailable scripts:\n";
        for (const QString& item :
             report.unavailableScripts)
        {
            stream << "  " << item << "\n";
        }
        stream << "\n";
    }

    stream << "File outcomes:\n";
    for (const AssetMigrationFileOutcome& outcome :
         report.fileOutcomes)
    {
        QString message = outcome.message;
        message.replace(
            QRegularExpression(
                QStringLiteral("[\\r\\n\\t]+")),
            QStringLiteral(" "));
        stream << "  ["
            << assetMigrationFileActionId(
                   outcome.action)
            << "] type=" << outcome.entryType
            << " source=" << outcome.sourcePath
            << " output=" << outcome.outputPath
            << " domain=" << outcome.domain
            << " reason=" << outcome.reason
            << " sourceScan="
            << (outcome.sourceScan ? "yes" : "no");
        if (!outcome.outputSha256.isEmpty())
            stream << " sha256=" << outcome.outputSha256;
        if (!message.isEmpty())
            stream << " message=" << message;
        stream << "\n";
    }
    stream << "\n";

    stream << "Log:\n";
    for (const QString& line : report.logLines)
        stream << "  " << line << "\n";

    stream.flush();
    bool ok = stream.status() != QTextStream::WriteFailed;
    if (!ok)
    {
        file.cancelWriting();
        return false;
    }
    if (shouldFailFileSystemOperation(
            FileSystemOperation::CommitTextReport, reportPath) ||
        !file.commit())
    {
        file.cancelWriting();
        return false;
    }

    report.reportFilePath = reportPath;
    return true;
}

bool JxAssetMigrator::writeReportJsonFile(const QString& outputDir, AssetMigrationReport& report, MigrationResult status)
{
    QString reportPath = appendPath(outputDir, "migration_report.json");
    report.reportJsonFilePath.clear();
    if (!ensureParentDirectory(reportPath))
        return false;

    QJsonObject root;

    // Status
    QString statusString;
    switch (status)
    {
    case MigrationResult::Success: statusString = "Success"; break;
    case MigrationResult::Partial: statusString = "Partial"; break;
    case MigrationResult::Failed: statusString = "Failed"; break;
    }
    root["status"] = statusString;
    root["cancelled"] = report.cancelled;
    root["completeProject"] = report.completeProject;

    QJsonArray selectedResourceTypes;
    for (const QString& type : report.selectedResourceTypes)
        selectedResourceTypes.append(type);
    root["selectedResourceTypes"] = selectedResourceTypes;

    QJsonObject resourceDomains;
    for (AssetResourceType domain : assetResourceDomainTypes())
    {
        const QString id = assetResourceTypeId(domain);
        const AssetResourceDomainReport domainReport =
            report.resourceDomains.value(id);
        QJsonObject domainObject;
        domainObject["selected"] = domainReport.selected;
        domainObject["processed"] = domainReport.processedFiles;
        domainObject["written"] = domainReport.writtenFiles;
        domainObject["failed"] = domainReport.failedFiles;
        resourceDomains[id] = domainObject;
    }
    root["resourceDomains"] = resourceDomains;

    QJsonObject legacyImageModes;
    for (const LegacyImageCategoryDefinition& item :
         LegacyImageMigrationPolicy::definitions())
    {
        legacyImageModes[item.id] =
            report.legacyImageModes.value(item.id);
    }
    QJsonObject legacyImages;
    legacyImages["modes"] = legacyImageModes;
    legacyImages["cropTransparent"] = report.cropTransparentRequested;
    legacyImages["effectiveCropTransparent"] =
        report.cropTransparentEffective;
    root["legacyImages"] = legacyImages;

    // Counts
    QJsonObject counts;
    counts["processed"] = report.processedFiles;
    counts["written"] = report.writtenFiles;
    counts["dependencyDuplicateFiles"] =
        report.dependencyDuplicateFiles;
    counts["dependencyDuplicateBytes"] =
        static_cast<qint64>(
            report.dependencyDuplicateBytes);
    counts["warnings"] = report.warningCount;
    counts["errors"] = report.errorCount;
    counts["convertedAndCropped"] = report.convertedAndCropped;
    counts["convertedWithoutCrop"] = report.convertedWithoutCrop;
    counts["preservedImages"] = report.preservedImages;
    counts["convertedMaps"] = report.convertedMaps;
    counts["preservedMapImages"] = report.preservedMapImages;
    counts["skippedUnknownImages"] = report.skippedUnknownImages;
    counts["failedImages"] = report.failedImages;
    counts["unsupportedScriptApis"] = report.unsupportedScriptApis.size();
    counts["unhandledScriptStatements"] = report.unhandledScriptStatements.size();
    counts["scriptSyntaxTotalFiles"] = report.scriptSyntaxTotalFiles;
    counts["scriptSyntaxCheckedFiles"] = report.scriptSyntaxCheckedFiles;
    counts["scriptSyntaxSkippedFiles"] = report.scriptSyntaxSkippedFiles;
    counts["scriptSyntaxErrors"] = report.scriptSyntaxErrors.size();
    counts["unavailableScripts"] =
        report.unavailableScripts.size();
    counts["managedOutputSha256"] =
        report.managedOutputSha256.size();
    counts["fileOutcomes"] =
        report.fileOutcomes.size();
    root["counts"] = counts;

    // Unsupported script APIs
    QJsonArray apisArray;
    for (const QString& api : report.unsupportedScriptApis)
        apisArray.append(api);
    if (!apisArray.isEmpty())
        root["unsupportedScriptApis"] = apisArray;

    QJsonArray unhandledArray;
    for (const QString& item : report.unhandledScriptStatements)
        unhandledArray.append(item);
    if (!unhandledArray.isEmpty())
        root["unhandledScriptStatements"] = unhandledArray;

    QJsonArray syntaxErrorsArray;
    for (const QString& item : report.scriptSyntaxErrors)
        syntaxErrorsArray.append(item);
    root["scriptSyntaxErrors"] = syntaxErrorsArray;

    QJsonArray unavailableScripts;
    for (const QString& item :
         report.unavailableScripts)
    {
        unavailableScripts.append(item);
    }
    root["unavailableScripts"] =
        unavailableScripts;

    QJsonObject managedOutputSha256;
    for (auto digest =
             report.managedOutputSha256.cbegin();
         digest !=
             report.managedOutputSha256.cend();
         ++digest)
    {
        managedOutputSha256.insert(
            digest.key(),
            digest.value());
    }
    root["managedOutputSha256"] =
        managedOutputSha256;
    root["publishWarning"] =
        report.publishWarning;
    root["retainedBackupPath"] =
        report.retainedBackupPath;
    QJsonArray logLines;
    for (const QString& line :
         report.logLines)
    {
        logLines.append(line);
    }
    root["logLines"] =
        logLines;

    QJsonArray fileOutcomes;
    for (const AssetMigrationFileOutcome& outcome :
         report.fileOutcomes)
    {
        QJsonObject fileOutcome;
        fileOutcome["source"] =
            outcome.sourcePath;
        fileOutcome["output"] =
            outcome.outputPath;
        fileOutcome["outputSha256"] =
            outcome.outputSha256;
        fileOutcome["domain"] =
            outcome.domain;
        fileOutcome["entryType"] =
            outcome.entryType;
        fileOutcome["action"] =
            assetMigrationFileActionId(
                outcome.action);
        fileOutcome["reason"] =
            outcome.reason;
        fileOutcome["message"] =
            outcome.message;
        fileOutcome["sourceScan"] =
            outcome.sourceScan;
        fileOutcomes.append(fileOutcome);
    }
    root["files"] = fileOutcomes;

    QSaveFile file(reportPath);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QJsonDocument doc(root);
    QByteArray payload = doc.toJson();
    // Detect short writes and flush/close errors (e.g. disk full), not just
    // outright write failures, so callers don't report a JSON file that was
    // never fully written.
    bool ok = (file.write(payload) == payload.size());
    if (!ok)
    {
        file.cancelWriting();
        return false;
    }
    if (shouldFailFileSystemOperation(
            FileSystemOperation::CommitJsonReport, reportPath) ||
        !file.commit())
    {
        file.cancelWriting();
        return false;
    }

    report.reportJsonFilePath = reportPath;
    return true;
}

void JxAssetMigrator::appendReportLog(AssetMigrationReport& report, const LogCallback& logCallback, const QString& message)
{
    report.logLines.append(message);
    if (logCallback)
        logCallback(message);
}

#if defined(JXQY_EDITOR_ENABLE_TEST_HOOKS)
void JxAssetMigrator::setFileSystemFaultInjectorForTests(
    FileSystemFaultInjector injector)
{
    fileSystemFaultInjector() = std::move(injector);
}
#endif

void JxAssetMigrator::convertTalkDatToTalkIndex(const QString& inputDir, const QString& outputDir,
    const AssetMigrationOptions& options, AssetMigrationReport& report, const LogCallback& logCallback)
{
    QString talkIdxPath = appendPath(inputDir, "script/common/Talkidx.dat");
    QString talkDatPath = appendPath(inputDir, "script/common/Talk.dat");

    const QFileInfo talkIndexInformation(
        talkIdxPath);
    const QFileInfo talkDataInformation(
        talkDatPath);
    if (!talkIndexInformation.isFile() ||
        isFileSystemLink(
            talkIndexInformation) ||
        !talkDataInformation.isFile() ||
        isFileSystemLink(
            talkDataInformation))
    {
        return;
    }

    appendReportLog(report, logCallback, QString::fromUtf8("发现 Talk.dat/Talkidx.dat，开始转换为 talkindex.txt"));

    // Read Talkidx.dat binary index
    std::vector<uint8_t> idxBuffer = Util::readFileToBuffer(talkIdxPath.toUtf8().toStdString());
    if (idxBuffer.empty() || idxBuffer.size() % 12 != 0)
    {
        report.warningCount++;
        appendReportLog(report, logCallback, QString::fromUtf8("警告: Talkidx.dat 格式异常，跳过对话转换"));
        return;
    }

    // Read Talk.dat text data
    std::vector<uint8_t> datBuffer = Util::readFileToBuffer(talkDatPath.toUtf8().toStdString());
    if (datBuffer.empty())
    {
        report.warningCount++;
        appendReportLog(report, logCallback, QString::fromUtf8("警告: Talk.dat 读取失败，跳过对话转换"));
        return;
    }

    size_t entryCount = idxBuffer.size() / 12;
    struct TalkEntry
    {
        uint32_t talkIndex;
        uint32_t portraitIndex;
        uint32_t offset;
    };

    std::vector<TalkEntry> entries(entryCount);
    for (size_t i = 0; i < entryCount; i++)
    {
        const uint8_t* p = idxBuffer.data() + i * 12;
        entries[i].talkIndex = static_cast<uint32_t>(p[0]) |
            (static_cast<uint32_t>(p[1]) << 8) |
            (static_cast<uint32_t>(p[2]) << 16) |
            (static_cast<uint32_t>(p[3]) << 24);
        entries[i].portraitIndex = static_cast<uint32_t>(p[4]) |
            (static_cast<uint32_t>(p[5]) << 8) |
            (static_cast<uint32_t>(p[6]) << 16) |
            (static_cast<uint32_t>(p[7]) << 24);
        entries[i].offset = static_cast<uint32_t>(p[8]) |
            (static_cast<uint32_t>(p[9]) << 8) |
            (static_cast<uint32_t>(p[10]) << 16) |
            (static_cast<uint32_t>(p[11]) << 24);
    }

    // Build talkindex.txt content
    std::string output;
    output.reserve(datBuffer.size() * 2);
    int convertedEntries = 0;
    for (size_t i = 0; i < entryCount; i++)
    {
        uint32_t startOffset = entries[i].offset;
        uint32_t endOffset = (i + 1 < entryCount) ? entries[i + 1].offset : static_cast<uint32_t>(datBuffer.size());

        if (startOffset >= datBuffer.size() || endOffset > datBuffer.size() || startOffset > endOffset)
        {
            report.warningCount++;
            continue;
        }

        std::string rawText(datBuffer.data() + startOffset, datBuffer.data() + endOffset);
        size_t nullPos = rawText.find('\0');
        if (nullPos != std::string::npos)
            rawText.resize(nullPos);

        std::string utf8Text;
        if (!convertLegacyJxTextToUtf8(rawText, options.sourceEncoding, utf8Text))
        {
            report.warningCount++;
            appendReportLog(report, logCallback,
                QString::fromUtf8("警告: Talk.dat 条目 %1 编码转换失败，已跳过")
                    .arg(entries[i].talkIndex));
            continue;
        }

        output += "[" + std::to_string(entries[i].talkIndex) + "," +
            std::to_string(entries[i].portraitIndex) + "]" + utf8Text + "\n";
        convertedEntries++;
    }

    QString outputPath = appendPath(outputDir, "script/common/talkindex.txt");
    const bool wroteCommonTalkIndex = writeTextFileUtf8(outputPath, output, false, report);
    const QString rootOutputPath =
        appendPath(
            outputDir,
            QStringLiteral(
                "talkindex.txt"));
    const bool wroteRootTalkIndex =
        writeTextFileUtf8(
            rootOutputPath,
            output,
            false,
            report);
    const auto appendGeneratedOutcome =
        [&](const QString& relativePath,
            bool written)
    {
        appendFileOutcome(
            report,
            QStringLiteral(
                "script/common/Talk.dat + "
                "script/common/Talkidx.dat"),
            relativePath,
            AssetResourceType::Scripts,
            written
                ? AssetMigrationFileAction::
                      Convert
                : AssetMigrationFileAction::
                      Fail,
            written
                ? QStringLiteral(
                      "generated-talk-index")
                : QStringLiteral(
                      "generated-talk-index-write-failed"),
            written
                ? QString::fromUtf8(
                      "由 staging 中受控的 Talk.dat/"
                      "Talkidx.dat 生成")
                : QString::fromUtf8(
                      "无法写入 Talk 派生索引"),
            false);
        if (!written)
        {
            domainReportFor(
                report,
                AssetResourceType::Scripts).
                failedFiles++;
        }
    };
    appendGeneratedOutcome(
        QStringLiteral(
            "script/common/talkindex.txt"),
        wroteCommonTalkIndex);
    appendGeneratedOutcome(
        QStringLiteral("talkindex.txt"),
        wroteRootTalkIndex);
    if (wroteCommonTalkIndex && wroteRootTalkIndex)
    {
        appendReportLog(report, logCallback,
            QString::fromUtf8("已转换 Talk.dat/Talkidx.dat -> talkindex.txt (%1/%2 条对话)")
                .arg(convertedEntries)
                .arg(entryCount));
    }
    else
    {
        report.errorCount++;
        appendReportLog(report, logCallback, QString::fromUtf8("错误: talkindex.txt 写入失败"));
    }
}
