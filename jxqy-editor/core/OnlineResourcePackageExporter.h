#pragma once

#include <QString>

class OnlineResourcePackageExporter
{
public:
    enum class Status
    {
        Success,
        InvalidInput,
        SourceBusy,
        MissingManifest,
        InvalidManifest,
        UnsafeSourceEntry,
        NonLowercasePath,
        DuplicatePortablePath,
        TooManyFiles,
        PackageTooLarge,
        SourceReadFailed,
        OutputOpenFailed,
        ArchiveWriteFailed,
        ArchiveReadbackFailed,
        CatalogWriteFailed,
        OutputCommitFailed,
        ChecksumReadFailed
    };

    struct Result
    {
        Status status = Status::InvalidInput;
        QString errorPath;
        QString catalogPath;
        QString crc32Hex;
        quint64 archiveSize = 0;
        quint64 uncompressedSize = 0;
        qsizetype fileCount = 0;

        bool succeeded() const
        {
            return status == Status::Success;
        }
    };

    static constexpr qsizetype MaximumFileCount = 500000;
    static constexpr quint64 MaximumUncompressedBytes =
        256ULL * 1024ULL * 1024ULL * 1024ULL;

    // Publishes one deterministic full package and a valid one-resource
    // catalog beside it. The archive contains the source root's files directly,
    // so game_profile.ini remains at archive root. Source files are never
    // modified by publication.
    static Result exportPackage(
        const QString& sourceRoot,
        const QString& outputFilePath);

    // Publishes the shared common directory as a deterministic ZIP and writes
    // a matching [Common] catalog fragment beside it. Common packages contain
    // shared game content and do not contain game_profile.ini or engine files.
    static Result exportCommonPackage(
        const QString& sourceRoot,
        const QString& displayVersion,
        const QString& outputFilePath);
};
