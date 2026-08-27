#pragma once

#include "ImageFrameImport.h"
#include "ImageFrameSequence.h"

#include <QByteArray>
#include <QString>

#include <vector>

enum class ImageFrameBatchScope
{
    Selected,
    All
};

enum class ImageFrameBatchError
{
    None,
    InvalidDocument,
    EmptySelection,
    DuplicateIndex,
    IndexOutOfRange,
    EncodeFailed,
    InvalidOutputDirectory,
    ManifestReadFailed,
    ManifestParseFailed,
    UnsupportedFormat,
    UnsupportedVersion,
    InvalidManifest,
    UnsafeFrameFileName,
    ReplacementConfirmationRequired,
    TargetCollision,
    TransactionRecoveryFailed,
    TransactionFailed,
    DocumentChanged,
    FrameFileInvalid
};

struct ImageFrameBatchDocument
{
    QString documentName;
    int directionCount = 0;
    int intervalMilliseconds = 0;
    std::vector<ImageFrameData> frames;
};

struct ImageFrameBatchFailure
{
    ImageFrameBatchError error = ImageFrameBatchError::None;
    int frameIndex = -1;
    ImageFrameImportError importError = ImageFrameImportError::None;
    QString detail;
};

struct ImageFrameBatchExportFile
{
    int index = -1;
    QString fileName;
    QByteArray pngBytes;
};

struct ImageFrameBatchPreparedExport
{
    QByteArray manifestBytes;
    QString documentFingerprintSha256;
    std::vector<ImageFrameBatchExportFile> files;
};

class ImageFrameBatchExchange
{
public:
    static QString manifestFileName();
    static QString frameFileName(int index);

    static QString documentFingerprintSha256(
        const ImageFrameBatchDocument& document);

    static bool prepareExport(
        const ImageFrameBatchDocument& document,
        ImageFrameBatchScope scope,
        const std::vector<int>& selectedIndices,
        ImageFrameBatchPreparedExport* preparedExport,
        ImageFrameBatchFailure* failure = nullptr);

    static bool publishExport(
        const QString& outputDirectory,
        const ImageFrameBatchPreparedExport& preparedExport,
        bool replaceExistingBatch,
        ImageFrameBatchFailure* failure = nullptr);

    static bool prepareReimport(
        const QString& manifestPath,
        const ImageFrameBatchDocument& currentDocument,
        ImageFrameSequenceEdit* edit,
        ImageFrameBatchFailure* failure = nullptr);
};
