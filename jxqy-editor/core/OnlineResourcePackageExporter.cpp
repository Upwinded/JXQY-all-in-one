#include "OnlineResourcePackageExporter.h"

#include "AuthoringMutationGate.h"
#include "DurableFileTransaction.h"
#include "EditorAssetPath.h"
#include "GameProfile.h"
#include "INIFileEditor.h"
#include "../../src/Resource/ResourceCatalog.h"
#include "../../src/Update/ArtifactChecksum.h"
#include "../../src/Update/OnlineUpdateCatalog.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

extern "C"
{
#include "miniz.h"
}

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <limits>
#include <vector>

namespace
{
struct SourceEntry
{
    QString absolutePath;
    QString relativePath;
    QByteArray archivePath;
    quint64 size = 0;
    QByteArray inlineBytes;
};

enum class PublishedDirectoryKind
{
    Resource,
    Common
};

struct TemporaryArtifacts
{
    QStringList paths;

    ~TemporaryArtifacts()
    {
        for (const QString& path : paths)
            QFile::remove(path);
    }
};

struct ArchiveReadbackSink
{
    mz_uint64 expectedSize = 0;
    mz_uint64 nextOffset = 0;
};

bool containsExcludedComponent(const QString& relativePath)
{
    const QStringList components =
        relativePath.split('/', Qt::SkipEmptyParts);
    for (const QString& component : components)
    {
        if (component.compare(
                QStringLiteral(".git"),
                Qt::CaseInsensitive) == 0 ||
            component.compare(
                QStringLiteral(".jxqy_editor"),
                Qt::CaseInsensitive) == 0)
        {
            return true;
        }
    }
    return false;
}

bool containsUppercaseAscii(const QByteArray& path)
{
    return std::any_of(path.cbegin(), path.cend(), [](char character)
    {
        return character >= 'A' && character <= 'Z';
    });
}

QString portablePathKey(const QString& path)
{
    QString key = path.normalized(QString::NormalizationForm_C);
    for (qsizetype index = 0; index < key.size(); ++index)
    {
        const ushort code = key.at(index).unicode();
        if (code >= 'A' && code <= 'Z')
        {
            key[index] = QChar(code + ('a' - 'A'));
        }
    }
    return key;
}

size_t archiveWriteCallback(
    void* opaque,
    mz_uint64 offset,
    const void* buffer,
    size_t bytes)
{
    auto* output = static_cast<QSaveFile*>(opaque);
    if (offset > static_cast<mz_uint64>(std::numeric_limits<qint64>::max()) ||
        bytes > static_cast<size_t>(std::numeric_limits<qint64>::max()) ||
        !output->seek(static_cast<qint64>(offset)))
    {
        return 0;
    }
    const qint64 written = output->write(
        static_cast<const char*>(buffer),
        static_cast<qint64>(bytes));
    return written < 0 ? 0 : static_cast<size_t>(written);
}

size_t sourceReadCallback(
    void* opaque,
    mz_uint64 offset,
    void* buffer,
    size_t bytes)
{
    auto* input = static_cast<QFile*>(opaque);
    if (offset > static_cast<mz_uint64>(std::numeric_limits<qint64>::max()) ||
        bytes > static_cast<size_t>(std::numeric_limits<qint64>::max()) ||
        !input->seek(static_cast<qint64>(offset)))
    {
        return 0;
    }
    const qint64 read = input->read(
        static_cast<char*>(buffer),
        static_cast<qint64>(bytes));
    return read < 0 ? 0 : static_cast<size_t>(read);
}

size_t archiveReadbackCallback(
    void* opaque,
    mz_uint64 offset,
    const void*,
    size_t bytes)
{
    auto* sink = static_cast<ArchiveReadbackSink*>(opaque);
    if (offset != sink->nextOffset ||
        static_cast<mz_uint64>(bytes) >
            sink->expectedSize - sink->nextOffset)
    {
        return 0;
    }
    sink->nextOffset += static_cast<mz_uint64>(bytes);
    return bytes;
}

bool inspectWrittenArchive(
    const QString& archivePath,
    const std::vector<SourceEntry>& expectedEntries,
    PublishedDirectoryKind kind,
    const QString& expectedGameId,
    const QString& expectedCommonVersion)
{
    QFile input(archivePath);
    if (!input.open(QIODevice::ReadOnly) || input.size() < 0)
        return false;

    mz_zip_archive archive;
    std::memset(&archive, 0, sizeof(archive));
    archive.m_pRead = sourceReadCallback;
    archive.m_pIO_opaque = &input;
    if (!mz_zip_reader_init(
            &archive,
            static_cast<mz_uint64>(input.size()),
            MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY))
    {
        return false;
    }

    bool valid = mz_zip_reader_get_num_files(&archive) ==
        static_cast<mz_uint>(expectedEntries.size());
    QMap<QByteArray, quint64> expectedByPath;
    for (const SourceEntry& entry : expectedEntries)
        expectedByPath.insert(entry.archivePath, entry.size);

    QByteArray manifestBytes;
    QByteArray commonVersionBytes;
    int manifestCount = 0;
    int commonVersionCount = 0;
    const mz_uint fileCount = mz_zip_reader_get_num_files(&archive);
    for (mz_uint index = 0; valid && index < fileCount; ++index)
    {
        const mz_uint fileNameLength =
            mz_zip_reader_get_filename(&archive, index, nullptr, 0);
        if (fileNameLength <= 1)
        {
            valid = false;
            break;
        }
        std::vector<char> fileName(fileNameLength);
        if (mz_zip_reader_get_filename(
                &archive,
                index,
                fileName.data(),
                fileNameLength) != fileNameLength)
        {
            valid = false;
            break;
        }
        const QByteArray path(
            fileName.data(), static_cast<qsizetype>(fileNameLength - 1));
        mz_zip_archive_file_stat stat;
        std::memset(&stat, 0, sizeof(stat));
        if (!mz_zip_reader_file_stat(&archive, index, &stat) ||
            stat.m_is_directory || !stat.m_is_supported ||
            !expectedByPath.contains(path) ||
            expectedByPath.value(path) !=
                static_cast<quint64>(stat.m_uncomp_size))
        {
            valid = false;
            break;
        }
        ArchiveReadbackSink sink{
            stat.m_uncomp_size,
            0
        };
        if (!mz_zip_reader_extract_to_callback(
                &archive,
                index,
                archiveReadbackCallback,
                &sink,
                0) ||
            sink.nextOffset != sink.expectedSize)
        {
            valid = false;
            break;
        }
        expectedByPath.remove(path);
        if (path == QByteArrayLiteral("game_profile.ini"))
        {
            size_t extractedSize = 0;
            void* extracted = mz_zip_reader_extract_to_heap(
                &archive, index, &extractedSize, 0);
            if (extracted == nullptr ||
                extractedSize > static_cast<size_t>(
                    std::numeric_limits<qsizetype>::max()))
            {
                mz_free(extracted);
                valid = false;
                break;
            }
            manifestBytes = QByteArray(
                static_cast<const char*>(extracted),
                static_cast<qsizetype>(extractedSize));
            mz_free(extracted);
            manifestCount++;
        }
        if (path == QByteArrayLiteral("version.ini"))
        {
            size_t extractedSize = 0;
            void* extracted = mz_zip_reader_extract_to_heap(
                &archive, index, &extractedSize, 0);
            if (extracted == nullptr ||
                extractedSize > static_cast<size_t>(
                    std::numeric_limits<qsizetype>::max()))
            {
                mz_free(extracted);
                valid = false;
                break;
            }
            commonVersionBytes = QByteArray(
                static_cast<const char*>(extracted),
                static_cast<qsizetype>(extractedSize));
            mz_free(extracted);
            commonVersionCount++;
        }
    }
    valid = valid && expectedByPath.isEmpty();
    if (kind == PublishedDirectoryKind::Resource)
        valid = valid && manifestCount == 1;
    else
        valid = valid && manifestCount == 0 &&
            commonVersionCount == 1;
    mz_zip_reader_end(&archive);
    if (!valid)
    {
        return false;
    }
    if (kind == PublishedDirectoryKind::Common)
    {
        std::string parsedVersion;
        return OnlineUpdate::parseCommonPackageVersion(
                   std::string_view(
                       commonVersionBytes.constData(),
                       static_cast<std::size_t>(commonVersionBytes.size())),
                   parsedVersion) &&
            parsedVersion ==
                expectedCommonVersion.trimmed().toUtf8().toStdString();
    }
    if (manifestBytes.isEmpty() ||
        manifestBytes.size() > std::numeric_limits<int>::max())
    {
        return false;
    }

    INIFileEditor manifest;
    return manifest.loadFromBuffer(
               manifestBytes.constData(),
               static_cast<int>(manifestBytes.size())) &&
        QString::fromStdString(manifest.get("Game", "Id", "")).compare(
            expectedGameId, Qt::CaseInsensitive) == 0;
}

bool createResourceCatalogBytes(
    const GameProfile& profile,
    const QString& archiveFileName,
    const OnlineResourcePackageExporter::Result& packageResult,
    QByteArray& bytes)
{
    bytes.clear();
    INIFileEditor catalog;
    catalog.setInteger("Catalog", "SchemaVersion", 1);
    const std::string section =
        "Resource." + profile.id.trimmed().toUtf8().toStdString();
    if (!profile.name.trimmed().isEmpty())
    {
        catalog.set(
            section,
            "Name",
            profile.name.trimmed().toUtf8().toStdString());
    }
    if (!profile.author.trimmed().isEmpty())
    {
        catalog.set(
            section,
            "Author",
            profile.author.trimmed().toUtf8().toStdString());
    }
    catalog.set(
        section,
        "Version",
        profile.releaseMetadata.displayVersion);
    catalog.set(
        section,
        "MinimumEngineVersion",
        profile.releaseMetadata.minimumEngineVersion);
    catalog.set(
        section,
        "Artifact",
        archiveFileName.toUtf8().toStdString());
    catalog.setInt64(
        section,
        "Size",
        static_cast<std::int64_t>(packageResult.archiveSize));
    catalog.set(
        section,
        "Crc32",
        packageResult.crc32Hex.toLatin1().toStdString());
    if (!profile.dependencyId.trimmed().isEmpty())
    {
        catalog.set(
            section,
            "Dependencies",
            profile.dependencyId.trimmed().toUtf8().toStdString());
    }
    if (profile.resourceOnly)
    {
        catalog.setBoolean(section, "ResourceOnly", true);
    }
    const std::string text = catalog.saveToString();

    // This file is a one-resource catalog fragment. Dependencies are expected
    // to be supplied by other fragments when the final catalog is published.
    // Add validation-only stubs so the strict full-catalog parser can still
    // validate every field without writing duplicate dependency metadata into
    // this fragment.
    INIFileEditor validationCatalog;
    if (!validationCatalog.loadFromString(text))
        return false;
    int dependencyIndex = 0;
    for (const QString& rawDependency :
         profile.dependencyId.split(',', Qt::KeepEmptyParts))
    {
        const QString dependencyId = rawDependency.trimmed();
        if (dependencyId.compare(
                profile.id.trimmed(), Qt::CaseInsensitive) == 0)
        {
            return false;
        }
        if (dependencyId.isEmpty())
        {
            continue;
        }
        const std::string dependencySection =
            "Resource." + dependencyId.toUtf8().toStdString();
        if (validationCatalog.hasSection(dependencySection))
            continue;
        validationCatalog.set(dependencySection, "Version", "0");
        validationCatalog.set(
            dependencySection, "MinimumEngineVersion", "0.0.0");
        validationCatalog.set(
            dependencySection,
            "Artifact",
            "dependency-" +
                std::to_string(++dependencyIndex) + ".zip");
        validationCatalog.set(dependencySection, "Size", "1");
        validationCatalog.set(dependencySection, "Crc32", "00000000");
    }
    const std::string validationText = validationCatalog.saveToString();
    const OnlineUpdate::CatalogParseResult parsed =
        OnlineUpdate::parseCatalog(validationText);
    if (!parsed.succeeded())
        return false;
    bytes = QByteArray(
        text.data(), static_cast<qsizetype>(text.size()));
    return true;
}

bool createCommonCatalogBytes(
    const QString& displayVersion,
    const QString& archiveFileName,
    const OnlineResourcePackageExporter::Result& packageResult,
    QByteArray& bytes)
{
    bytes.clear();
    INIFileEditor catalog;
    catalog.setInteger("Catalog", "SchemaVersion", 1);
    catalog.set(
        "Common",
        "Version",
        displayVersion.trimmed().toUtf8().toStdString());
    catalog.set(
        "Common",
        "Artifact",
        archiveFileName.toUtf8().toStdString());
    catalog.setInt64(
        "Common",
        "Size",
        static_cast<std::int64_t>(packageResult.archiveSize));
    catalog.set(
        "Common",
        "Crc32",
        packageResult.crc32Hex.toLatin1().toStdString());
    const std::string text = catalog.saveToString();
    const OnlineUpdate::CatalogParseResult parsed =
        OnlineUpdate::parseCatalog(text);
    if (!parsed.succeeded() || !parsed.catalog.commonPackage.has_value())
        return false;
    bytes = QByteArray(text.data(), static_cast<qsizetype>(text.size()));
    return true;
}

bool hasUnsafeFileType(const QFileInfo& info)
{
    if (!info.isFile() || info.isSymLink())
    {
        return true;
    }
#if defined(Q_OS_WIN)
    if (info.isJunction())
    {
        return true;
    }
#endif
    return false;
}
}

namespace
{
OnlineResourcePackageExporter::Result
exportDirectoryPackage(
    const QString& sourceRoot,
    const QString& outputFilePath,
    PublishedDirectoryKind kind,
    const QString& commonDisplayVersion)
{
    using Result = OnlineResourcePackageExporter::Result;
    using Status = OnlineResourcePackageExporter::Status;
    Result result;
    if (sourceRoot.trimmed().isEmpty() || outputFilePath.trimmed().isEmpty())
    {
        result.status = Status::InvalidInput;
        result.errorPath = sourceRoot;
        return result;
    }
    auto sourceReadLease = AuthoringMutationGate::instance().acquireLease(
        "Resource publication is reading a coherent source snapshot");
    if (!sourceReadLease)
    {
        result.status = Status::SourceBusy;
        result.errorPath = sourceRoot;
        return result;
    }

    const QFileInfo rootInfo(sourceRoot);
    if (!rootInfo.exists() || !rootInfo.isDir() || rootInfo.isSymLink()
#if defined(Q_OS_WIN)
        || rootInfo.isJunction()
#endif
    )
    {
        result.status = Status::InvalidInput;
        result.errorPath = sourceRoot;
        return result;
    }

    const QDir root(rootInfo.canonicalFilePath());
    const QString resolvedRootKey =
        EditorAssetPath::comparisonKey(root.absolutePath());
    const QString resolvedOutputKey =
        EditorAssetPath::comparisonKey(outputFilePath);
    if (EditorAssetPath::isLexicallyInside(
            resolvedRootKey, resolvedOutputKey))
    {
        result.status = Status::InvalidInput;
        result.errorPath = outputFilePath;
        return result;
    }
    const QString manifestPath =
        root.filePath(QStringLiteral("game_profile.ini"));
    const QFileInfo manifestInfo(manifestPath);
    if (kind == PublishedDirectoryKind::Resource && !manifestInfo.exists())
    {
        result.status = Status::MissingManifest;
        result.errorPath = root.filePath(QStringLiteral("game_profile.ini"));
        return result;
    }
    if (kind == PublishedDirectoryKind::Common && manifestInfo.exists())
    {
        result.status = Status::InvalidInput;
        result.errorPath = manifestPath;
        return result;
    }
    if (kind == PublishedDirectoryKind::Resource &&
        (!manifestInfo.isFile() || manifestInfo.isSymLink() ||
         manifestInfo.size() <= 0 ||
         static_cast<quint64>(manifestInfo.size()) >
             static_cast<quint64>(
                 RuntimeResource::MaximumCatalogIniBytes)))
    {
        result.status = Status::InvalidManifest;
        result.errorPath = manifestPath;
        return result;
    }
    GameProfile profile;
    if (kind == PublishedDirectoryKind::Resource &&
        (!profile.loadFromFile(manifestPath) ||
         profile.id.trimmed().isEmpty() ||
         profile.releaseMetadata.displayVersion.empty() ||
         profile.releaseMetadata.minimumEngineVersion.empty()))
    {
        result.status = Status::InvalidManifest;
        result.errorPath = manifestPath;
        return result;
    }
    if (kind == PublishedDirectoryKind::Common &&
        commonDisplayVersion.trimmed().isEmpty())
    {
        result.status = Status::InvalidInput;
        result.errorPath = commonDisplayVersion;
        return result;
    }
    std::vector<SourceEntry> entries;
    QSet<QString> portableKeys;
    QDirIterator iterator(
        root.absolutePath(),
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
            QDir::System,
        QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        const QString absolutePath = iterator.next();
        QString relativePath = root.relativeFilePath(absolutePath);
        relativePath.replace('\\', '/');
        if (containsExcludedComponent(relativePath))
        {
            continue;
        }
        if (kind == PublishedDirectoryKind::Common &&
            relativePath == QStringLiteral("version.ini"))
        {
            continue;
        }

        const QFileInfo info = iterator.fileInfo();
        if (info.isSymLink()
#if defined(Q_OS_WIN)
            || info.isJunction()
#endif
        )
        {
            result.status = Status::UnsafeSourceEntry;
            result.errorPath = absolutePath;
            return result;
        }
        if (info.isDir())
        {
            continue;
        }
        if (hasUnsafeFileType(info))
        {
            result.status = Status::UnsafeSourceEntry;
            result.errorPath = absolutePath;
            return result;
        }
        const QByteArray archivePath = relativePath.toUtf8();
        if (archivePath.isEmpty() || archivePath.contains('\0') ||
            archivePath.startsWith('/') || archivePath.contains("../") ||
            containsUppercaseAscii(archivePath))
        {
            result.status = containsUppercaseAscii(archivePath)
                ? Status::NonLowercasePath
                : Status::UnsafeSourceEntry;
            result.errorPath = relativePath;
            return result;
        }
        const QString key = portablePathKey(relativePath);
        if (portableKeys.contains(key))
        {
            result.status = Status::DuplicatePortablePath;
            result.errorPath = relativePath;
            return result;
        }
        portableKeys.insert(key);
        if (entries.size() >= static_cast<std::size_t>(
                OnlineResourcePackageExporter::MaximumFileCount))
        {
            result.status = Status::TooManyFiles;
            return result;
        }
        const qint64 signedSize = info.size();
        if (signedSize < 0 ||
            result.uncompressedSize >
                OnlineResourcePackageExporter::MaximumUncompressedBytes -
                    static_cast<quint64>(signedSize))
        {
            result.status = Status::PackageTooLarge;
            result.errorPath = relativePath;
            return result;
        }
        result.uncompressedSize += static_cast<quint64>(signedSize);
        entries.push_back({
            absolutePath,
            relativePath,
            archivePath,
            static_cast<quint64>(signedSize),
            {} });
    }

    if (kind == PublishedDirectoryKind::Common)
    {
        const QByteArray commonVersionBytes =
            QByteArrayLiteral("[Common]\nVersion=") +
            commonDisplayVersion.trimmed().toUtf8() + '\n';
        std::string parsedVersion;
        if (!OnlineUpdate::parseCommonPackageVersion(
                std::string_view(
                    commonVersionBytes.constData(),
                    static_cast<std::size_t>(commonVersionBytes.size())),
                parsedVersion) ||
            entries.size() >= static_cast<std::size_t>(
                OnlineResourcePackageExporter::MaximumFileCount) ||
            result.uncompressedSize >
                OnlineResourcePackageExporter::MaximumUncompressedBytes -
                    static_cast<quint64>(commonVersionBytes.size()))
        {
            result.status = Status::InvalidInput;
            result.errorPath = commonDisplayVersion;
            return result;
        }
        portableKeys.insert(QStringLiteral("version.ini"));
        result.uncompressedSize +=
            static_cast<quint64>(commonVersionBytes.size());
        entries.push_back({
            {},
            QStringLiteral("version.ini"),
            QByteArrayLiteral("version.ini"),
            static_cast<quint64>(commonVersionBytes.size()),
            commonVersionBytes });
    }

    std::sort(entries.begin(), entries.end(),
        [](const SourceEntry& left, const SourceEntry& right)
        {
            return left.archivePath < right.archivePath;
        });
    result.fileCount = static_cast<qsizetype>(entries.size());
    const QFileInfo outputInfo(outputFilePath);
    const QString outputRoot = outputInfo.absolutePath();
    const QString archiveFileName = outputInfo.fileName();
    if (containsUppercaseAscii(archiveFileName.toUtf8()))
    {
        result.status = Status::InvalidInput;
        result.errorPath = archiveFileName;
        return result;
    }
    const std::string artifactPath =
        (QStringLiteral("resources/") + archiveFileName).
            toUtf8().toStdString();
    if (!OnlineUpdate::isSafeArtifactPath(artifactPath))
    {
        result.status = Status::InvalidInput;
        result.errorPath = archiveFileName;
        return result;
    }
    result.catalogPath = QDir(outputRoot).filePath(
        outputInfo.completeBaseName() + QStringLiteral(".catalog.ini"));
    const QString transactionId =
        QUuid::createUuid().toString(QUuid::Id128);
    const QString stagedArchivePath = QDir(outputRoot).filePath(
        QStringLiteral(".%1.%2.package.tmp")
            .arg(archiveFileName, transactionId));
    const QString stagedCatalogPath = QDir(outputRoot).filePath(
        QStringLiteral(".%1.%2.catalog.tmp")
            .arg(outputInfo.completeBaseName(), transactionId));
    TemporaryArtifacts temporaryArtifacts{
        { stagedArchivePath, stagedCatalogPath } };
    QSaveFile output(stagedArchivePath);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly))
    {
        result.status = Status::OutputOpenFailed;
        result.errorPath = stagedArchivePath;
        return result;
    }

    mz_zip_archive archive;
    std::memset(&archive, 0, sizeof(archive));
    archive.m_pWrite = archiveWriteCallback;
    archive.m_pIO_opaque = &output;
    if (!mz_zip_writer_init_v2(&archive, 0, MZ_ZIP_FLAG_WRITE_ZIP64))
    {
        output.cancelWriting();
        result.status = Status::ArchiveWriteFailed;
        return result;
    }

    bool archiveSucceeded = true;
    MZ_TIME_T fixedTimestamp = static_cast<MZ_TIME_T>(315532800);
    for (const SourceEntry& entry : entries)
    {
        if (!entry.inlineBytes.isNull())
        {
            if (!mz_zip_writer_add_mem_ex_v2(
                    &archive,
                    entry.archivePath.constData(),
                    entry.inlineBytes.constData(),
                    static_cast<size_t>(entry.inlineBytes.size()),
                    nullptr,
                    0,
                    MZ_DEFAULT_COMPRESSION,
                    0,
                    0,
                    &fixedTimestamp,
                    nullptr,
                    0,
                    nullptr,
                    0))
            {
                result.status = Status::ArchiveWriteFailed;
                result.errorPath = entry.relativePath;
                archiveSucceeded = false;
                break;
            }
            continue;
        }
        QFile input(entry.absolutePath);
        if (!input.open(QIODevice::ReadOnly))
        {
            result.status = Status::SourceReadFailed;
            result.errorPath = entry.relativePath;
            archiveSucceeded = false;
            break;
        }
        if (!mz_zip_writer_add_read_buf_callback(
                &archive,
                entry.archivePath.constData(),
                sourceReadCallback,
                &input,
                static_cast<mz_uint64>(entry.size),
                &fixedTimestamp,
                nullptr,
                0,
                MZ_DEFAULT_COMPRESSION,
                nullptr,
                0,
                nullptr,
                0))
        {
            result.status = Status::ArchiveWriteFailed;
            result.errorPath = entry.relativePath;
            archiveSucceeded = false;
            break;
        }
    }
    if (archiveSucceeded && !mz_zip_writer_finalize_archive(&archive))
    {
        archiveSucceeded = false;
        result.status = Status::ArchiveWriteFailed;
    }
    mz_zip_writer_end(&archive);
    if (!archiveSucceeded)
    {
        output.cancelWriting();
        return result;
    }
    if (!output.commit())
    {
        result.status = Status::OutputCommitFailed;
        result.errorPath = stagedArchivePath;
        return result;
    }

    const std::filesystem::path nativeArchivePath =
#if defined(Q_OS_WIN)
        std::filesystem::path(stagedArchivePath.toStdWString());
#else
        std::filesystem::u8path(stagedArchivePath.toUtf8().constData());
#endif
    std::uint32_t checksum = 0;
    std::uint64_t archiveSize = 0;
    if (!OnlineUpdate::calculateFileCrc32(
            nativeArchivePath, checksum, archiveSize))
    {
        result.status = Status::ChecksumReadFailed;
        result.errorPath = stagedArchivePath;
        return result;
    }
    result.archiveSize = static_cast<quint64>(archiveSize);
    result.crc32Hex = QString::fromStdString(
        OnlineUpdate::crc32ToLowerHex(checksum));
    if (!inspectWrittenArchive(
            stagedArchivePath,
            entries,
            kind,
            profile.id.trimmed(),
            commonDisplayVersion.trimmed()))
    {
        result.status = Status::ArchiveReadbackFailed;
        result.errorPath = stagedArchivePath;
        return result;
    }

    QByteArray catalogBytes;
    const bool catalogCreated = kind == PublishedDirectoryKind::Resource
        ? createResourceCatalogBytes(
              profile,
              archiveFileName,
              result,
              catalogBytes)
        : createCommonCatalogBytes(
              commonDisplayVersion,
              archiveFileName,
              result,
              catalogBytes);
    if (!catalogCreated)
    {
        result.status = kind == PublishedDirectoryKind::Resource
            ? Status::InvalidManifest
            : Status::InvalidInput;
        result.errorPath = kind == PublishedDirectoryKind::Resource
            ? manifestPath
            : commonDisplayVersion;
        return result;
    }
    QSaveFile catalogOutput(stagedCatalogPath);
    catalogOutput.setDirectWriteFallback(false);
    if (!catalogOutput.open(QIODevice::WriteOnly) ||
        catalogOutput.write(catalogBytes) != catalogBytes.size() ||
        !catalogOutput.commit())
    {
        catalogOutput.cancelWriting();
        result.status = Status::CatalogWriteFailed;
        result.errorPath = stagedCatalogPath;
        return result;
    }

    sourceReadLease.release();
    DurableFileTransaction publishTransaction(outputRoot);
    QString transactionError;
    if (!publishTransaction.addPreparedWrite(
            outputFilePath,
            stagedArchivePath,
            transactionError) ||
        !publishTransaction.addPreparedWrite(
            result.catalogPath,
            stagedCatalogPath,
            transactionError) ||
        !publishTransaction.commit(transactionError))
    {
        result.status = Status::OutputCommitFailed;
        result.errorPath = outputRoot;
        return result;
    }
    result.status = Status::Success;
    return result;
}
}

OnlineResourcePackageExporter::Result
OnlineResourcePackageExporter::exportPackage(
    const QString& sourceRoot,
    const QString& outputFilePath)
{
    return exportDirectoryPackage(
        sourceRoot,
        outputFilePath,
        PublishedDirectoryKind::Resource,
        {});
}

OnlineResourcePackageExporter::Result
OnlineResourcePackageExporter::exportCommonPackage(
    const QString& sourceRoot,
    const QString& displayVersion,
    const QString& outputFilePath)
{
    return exportDirectoryPackage(
        sourceRoot,
        outputFilePath,
        PublishedDirectoryKind::Common,
        displayVersion);
}
