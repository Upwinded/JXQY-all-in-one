#pragma once

#include "ImageFrameSequence.h"

#include <QSize>
#include <QString>

#include <cstdint>
#include <vector>

enum class ImageFrameImportError
{
    None,
    EmptyQueue,
    InvalidDocumentFrameCount,
    FrameLimitExceeded,
    EmptyPath,
    FileMissing,
    NotRegularFile,
    FileNotReadable,
    UnsupportedExtension,
    UnsupportedFormat,
    DecodeFailed,
    EncodeFailed
};

struct ImageFrameImportProbe
{
    QString normalizedPath;
    QSize imageSize;
    qint64 fileSize = 0;
    ImageFrameImportError error = ImageFrameImportError::None;

    bool isValid() const
    {
        return error == ImageFrameImportError::None;
    }
};

struct ImageFrameImportRequest
{
    QString filePath;
    int32_t xOffset = 0;
    int32_t yOffset = 0;
};

class ImageFrameImport
{
public:
    static ImageFrameImportProbe probeFile(const QString& filePath);

    static bool loadFile(
        const ImageFrameImportRequest& request,
        ImageFrameData* importedFrame,
        ImageFrameImportError* error = nullptr);

    static bool prepareRequests(
        const std::vector<ImageFrameImportRequest>& requests,
        std::vector<ImageFrameData>* importedFrames,
        int* failedIndex = nullptr,
        ImageFrameImportError* error = nullptr);

    static bool prepareBatch(
        const std::vector<ImageFrameImportRequest>& requests,
        int existingFrameCount,
        std::vector<ImageFrameData>* importedFrames,
        int* failedIndex = nullptr,
        ImageFrameImportError* error = nullptr);
};
