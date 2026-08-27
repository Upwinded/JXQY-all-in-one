#include "ImageFrameImport.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>

#include <limits>

namespace
{
ImageFrameImportProbe loadFrame(
    const QString& filePath,
    ImageFrameData* importedFrame)
{
    ImageFrameImportProbe probe;
    if (filePath.trimmed().isEmpty())
    {
        probe.error = ImageFrameImportError::EmptyPath;
        return probe;
    }

    const QFileInfo fileInfo(filePath);
    probe.normalizedPath = QDir::cleanPath(fileInfo.absoluteFilePath());
    if (!fileInfo.exists())
    {
        probe.error = ImageFrameImportError::FileMissing;
        return probe;
    }
    if (!fileInfo.isFile())
    {
        probe.error = ImageFrameImportError::NotRegularFile;
        return probe;
    }
    if (!fileInfo.isReadable())
    {
        probe.error = ImageFrameImportError::FileNotReadable;
        return probe;
    }
    if (fileInfo.suffix().compare(
            QStringLiteral("png"), Qt::CaseInsensitive) != 0)
    {
        probe.error = ImageFrameImportError::UnsupportedExtension;
        return probe;
    }

    probe.fileSize = fileInfo.size();
    QImageReader reader(probe.normalizedPath);
    reader.setAutoTransform(false);
    const QByteArray detectedFormat = reader.format().toLower();
    if (!detectedFormat.isEmpty() && detectedFormat != QByteArrayLiteral("png"))
    {
        probe.error = ImageFrameImportError::UnsupportedFormat;
        return probe;
    }
    if (!reader.canRead())
    {
        probe.error = ImageFrameImportError::DecodeFailed;
        return probe;
    }
    if (reader.format().toLower() != QByteArrayLiteral("png"))
    {
        probe.error = ImageFrameImportError::UnsupportedFormat;
        return probe;
    }

    QImage image = reader.read();
    if (image.isNull())
    {
        probe.error = ImageFrameImportError::DecodeFailed;
        return probe;
    }
    image = image.convertToFormat(QImage::Format_ARGB32);
    probe.imageSize = image.size();

    QByteArray encodedImage;
    QBuffer buffer(&encodedImage);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG") ||
        encodedImage.isEmpty() ||
        encodedImage.size() >
            static_cast<qsizetype>((std::numeric_limits<int32_t>::max)()))
    {
        probe.error = ImageFrameImportError::EncodeFailed;
        return probe;
    }

    if (importedFrame)
    {
        importedFrame->encodedImage = std::move(encodedImage);
        importedFrame->decodedImage = std::move(image);
    }
    return probe;
}

void setFailure(
    int index,
    ImageFrameImportError failure,
    int* failedIndex,
    ImageFrameImportError* error)
{
    if (failedIndex)
        *failedIndex = index;
    if (error)
        *error = failure;
}
}

ImageFrameImportProbe ImageFrameImport::probeFile(const QString& filePath)
{
    return loadFrame(filePath, nullptr);
}

bool ImageFrameImport::loadFile(
    const ImageFrameImportRequest& request,
    ImageFrameData* importedFrame,
    ImageFrameImportError* error)
{
    if (error)
        *error = ImageFrameImportError::None;
    if (importedFrame == nullptr)
        return false;
    *importedFrame = {};

    ImageFrameData loadedFrame;
    const ImageFrameImportProbe probe = loadFrame(
        request.filePath, &loadedFrame);
    if (!probe.isValid())
    {
        if (error)
            *error = probe.error;
        return false;
    }
    loadedFrame.xOffset = request.xOffset;
    loadedFrame.yOffset = request.yOffset;
    *importedFrame = std::move(loadedFrame);
    return true;
}

bool ImageFrameImport::prepareBatch(
    const std::vector<ImageFrameImportRequest>& requests,
    int existingFrameCount,
    std::vector<ImageFrameData>* importedFrames,
    int* failedIndex,
    ImageFrameImportError* error)
{
    if (importedFrames == nullptr)
        return false;
    importedFrames->clear();
    setFailure(-1, ImageFrameImportError::None, failedIndex, error);

    if (requests.empty())
    {
        setFailure(-1, ImageFrameImportError::EmptyQueue, failedIndex, error);
        return false;
    }
    if (existingFrameCount <= 0 ||
        existingFrameCount > MaximumImageFrameCount)
    {
        setFailure(
            -1,
            ImageFrameImportError::InvalidDocumentFrameCount,
            failedIndex,
            error);
        return false;
    }
    if (requests.size() >
        static_cast<size_t>(MaximumImageFrameCount - existingFrameCount))
    {
        setFailure(
            -1,
            ImageFrameImportError::FrameLimitExceeded,
            failedIndex,
            error);
        return false;
    }

    return prepareRequests(
        requests, importedFrames, failedIndex, error);
}

bool ImageFrameImport::prepareRequests(
    const std::vector<ImageFrameImportRequest>& requests,
    std::vector<ImageFrameData>* importedFrames,
    int* failedIndex,
    ImageFrameImportError* error)
{
    if (importedFrames == nullptr)
        return false;
    importedFrames->clear();
    setFailure(-1, ImageFrameImportError::None, failedIndex, error);
    if (requests.empty())
    {
        setFailure(-1, ImageFrameImportError::EmptyQueue, failedIndex, error);
        return false;
    }
    if (requests.size() > MaximumImageFrameCount)
    {
        setFailure(
            -1,
            ImageFrameImportError::FrameLimitExceeded,
            failedIndex,
            error);
        return false;
    }

    std::vector<ImageFrameData> preparedFrames;
    preparedFrames.reserve(requests.size());
    for (size_t index = 0; index < requests.size(); ++index)
    {
        ImageFrameData frame;
        ImageFrameImportError loadError = ImageFrameImportError::None;
        if (!loadFile(requests[index], &frame, &loadError))
        {
            setFailure(
                static_cast<int>(index), loadError, failedIndex, error);
            return false;
        }
        preparedFrames.push_back(std::move(frame));
    }

    *importedFrames = std::move(preparedFrames);
    return true;
}
