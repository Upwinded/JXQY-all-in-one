#include "ImageFrameTransform.h"

#include <QBuffer>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <new>
#include <unordered_set>
#include <utility>

namespace
{
struct PreparedTransform
{
    int frameIndex = -1;
    int width = 0;
    int height = 0;
    int32_t xOffset = 0;
    int32_t yOffset = 0;
};

void setFailure(
    ImageFrameTransformFailure* failure,
    ImageFrameTransformError error,
    int frameIndex = -1)
{
    if (failure == nullptr)
        return;
    failure->error = error;
    failure->frameIndex = frameIndex;
}

bool isValidOperation(ImageFrameTransformOperation operation)
{
    switch (operation)
    {
    case ImageFrameTransformOperation::RotateLeft90:
    case ImageFrameTransformOperation::RotateRight90:
    case ImageFrameTransformOperation::FlipHorizontal:
    case ImageFrameTransformOperation::FlipVertical:
        return true;
    }
    return false;
}

bool isValidFrame(const ImageFrameData& frame)
{
    return !frame.encodedImage.isEmpty() &&
           frame.encodedImage.size() <=
               static_cast<qsizetype>((std::numeric_limits<int32_t>::max)()) &&
           !frame.decodedImage.isNull() &&
           frame.decodedImage.width() > 0 &&
           frame.decodedImage.height() > 0;
}

bool checkedInt32(int64_t value, int32_t* result)
{
    if (result == nullptr ||
        value < (std::numeric_limits<int32_t>::min)() ||
        value > (std::numeric_limits<int32_t>::max)())
    {
        return false;
    }
    *result = static_cast<int32_t>(value);
    return true;
}

bool prepareTransform(
    const ImageFrameData& frame,
    int frameIndex,
    ImageFrameTransformOperation operation,
    PreparedTransform* prepared)
{
    if (prepared == nullptr)
        return false;

    const int64_t width = frame.decodedImage.width();
    const int64_t height = frame.decodedImage.height();
    int64_t xOffset = frame.xOffset;
    int64_t yOffset = frame.yOffset;

    switch (operation)
    {
    case ImageFrameTransformOperation::RotateLeft90:
        prepared->width = static_cast<int>(height);
        prepared->height = static_cast<int>(width);
        xOffset = frame.yOffset;
        yOffset = width - static_cast<int64_t>(frame.xOffset);
        break;
    case ImageFrameTransformOperation::RotateRight90:
        prepared->width = static_cast<int>(height);
        prepared->height = static_cast<int>(width);
        xOffset = height - static_cast<int64_t>(frame.yOffset);
        yOffset = frame.xOffset;
        break;
    case ImageFrameTransformOperation::FlipHorizontal:
        prepared->width = static_cast<int>(width);
        prepared->height = static_cast<int>(height);
        xOffset = width - static_cast<int64_t>(frame.xOffset);
        yOffset = frame.yOffset;
        break;
    case ImageFrameTransformOperation::FlipVertical:
        prepared->width = static_cast<int>(width);
        prepared->height = static_cast<int>(height);
        xOffset = frame.xOffset;
        yOffset = height - static_cast<int64_t>(frame.yOffset);
        break;
    }

    prepared->frameIndex = frameIndex;
    return checkedInt32(xOffset, &prepared->xOffset) &&
           checkedInt32(yOffset, &prepared->yOffset);
}

bool transformPixels(
    const QImage& sourceImage,
    const PreparedTransform& prepared,
    ImageFrameTransformOperation operation,
    QImage* transformedImage)
{
    if (transformedImage == nullptr)
        return false;
    *transformedImage = {};

    const QImage source =
        sourceImage.convertToFormat(QImage::Format_ARGB32);
    if (source.isNull())
        return false;

    QImage transformed(
        prepared.width, prepared.height, QImage::Format_ARGB32);
    if (transformed.isNull())
        return false;

    const int sourceWidth = source.width();
    const int sourceHeight = source.height();
    for (int y = 0; y < sourceHeight; y++)
    {
        const QRgb* sourceLine =
            reinterpret_cast<const QRgb*>(source.constScanLine(y));
        for (int x = 0; x < sourceWidth; x++)
        {
            int targetX = x;
            int targetY = y;
            switch (operation)
            {
            case ImageFrameTransformOperation::RotateLeft90:
                targetX = y;
                targetY = sourceWidth - 1 - x;
                break;
            case ImageFrameTransformOperation::RotateRight90:
                targetX = sourceHeight - 1 - y;
                targetY = x;
                break;
            case ImageFrameTransformOperation::FlipHorizontal:
                targetX = sourceWidth - 1 - x;
                break;
            case ImageFrameTransformOperation::FlipVertical:
                targetY = sourceHeight - 1 - y;
                break;
            }
            QRgb* targetLine =
                reinterpret_cast<QRgb*>(transformed.scanLine(targetY));
            targetLine[targetX] = sourceLine[x];
        }
    }

    *transformedImage = std::move(transformed);
    return true;
}

bool encodePng(const QImage& image, QByteArray* encodedImage)
{
    if (encodedImage == nullptr)
        return false;
    encodedImage->clear();
    QBuffer buffer(encodedImage);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
    {
        encodedImage->clear();
        return false;
    }
    return !encodedImage->isEmpty();
}
}

bool ImageFrameTransform::transformSelectedFrames(
    const std::vector<ImageFrameData>& frames,
    const std::vector<int>& selectedIndices,
    int currentIndex,
    ImageFrameTransformOperation operation,
    ImageFrameSequenceEdit* edit,
    ImageFrameTransformFailure* failure)
{
    if (edit == nullptr)
    {
        setFailure(failure, ImageFrameTransformError::InvalidDocument);
        return false;
    }
    *edit = {};
    if (failure != nullptr)
        *failure = {};

    if (frames.empty() || frames.size() > MaximumImageFrameCount)
    {
        setFailure(failure, ImageFrameTransformError::InvalidDocument);
        return false;
    }
    for (size_t index = 0; index < frames.size(); index++)
    {
        if (!isValidFrame(frames[index]))
        {
            setFailure(
                failure,
                ImageFrameTransformError::InvalidFrame,
                static_cast<int>(index));
            return false;
        }
    }
    if (selectedIndices.empty())
    {
        setFailure(failure, ImageFrameTransformError::EmptySelection);
        return false;
    }
    if (!isValidOperation(operation))
    {
        setFailure(failure, ImageFrameTransformError::InvalidOperation);
        return false;
    }

    std::vector<int> normalizedSelection;
    normalizedSelection.reserve(selectedIndices.size());
    std::unordered_set<int> uniqueIndices;
    uniqueIndices.reserve(selectedIndices.size());
    for (int index : selectedIndices)
    {
        if (index < 0 || index >= static_cast<int>(frames.size()))
        {
            setFailure(
                failure,
                ImageFrameTransformError::IndexOutOfRange,
                index);
            return false;
        }
        if (!uniqueIndices.insert(index).second)
        {
            setFailure(
                failure,
                ImageFrameTransformError::DuplicateIndex,
                index);
            return false;
        }
        normalizedSelection.push_back(index);
    }
    std::sort(normalizedSelection.begin(), normalizedSelection.end());
    if (currentIndex < 0 ||
        currentIndex >= static_cast<int>(frames.size()) ||
        !std::binary_search(
            normalizedSelection.begin(), normalizedSelection.end(), currentIndex))
    {
        setFailure(
            failure,
            ImageFrameTransformError::CurrentIndexInvalid,
            currentIndex);
        return false;
    }

    std::vector<PreparedTransform> preparedTransforms;
    try
    {
        preparedTransforms.reserve(normalizedSelection.size());
        for (int index : normalizedSelection)
        {
            PreparedTransform prepared;
            if (!prepareTransform(
                    frames[static_cast<size_t>(index)],
                    index,
                    operation,
                    &prepared))
            {
                setFailure(
                    failure,
                    ImageFrameTransformError::OffsetOutOfRange,
                    index);
                return false;
            }
            preparedTransforms.push_back(prepared);
        }
    }
    catch (const std::bad_alloc&)
    {
        setFailure(failure, ImageFrameTransformError::ImageAllocationFailed);
        return false;
    }

    ImageFrameSequenceEdit result;
    try
    {
        result.frames = frames;
        result.selectedIndices = normalizedSelection;
        result.currentIndex = currentIndex;
        for (const PreparedTransform& prepared : preparedTransforms)
        {
            const ImageFrameData& sourceFrame =
                frames[static_cast<size_t>(prepared.frameIndex)];
            QImage transformedImage;
            if (!transformPixels(
                    sourceFrame.decodedImage,
                    prepared,
                    operation,
                    &transformedImage))
            {
                setFailure(
                    failure,
                    ImageFrameTransformError::ImageAllocationFailed,
                    prepared.frameIndex);
                return false;
            }

            const QImage comparableSource =
                sourceFrame.decodedImage.convertToFormat(
                    QImage::Format_ARGB32);
            const bool frameChanged =
                prepared.xOffset != sourceFrame.xOffset ||
                prepared.yOffset != sourceFrame.yOffset ||
                transformedImage.size() != comparableSource.size() ||
                transformedImage != comparableSource;
            if (!frameChanged)
                continue;

            QByteArray encodedImage;
            if (!encodePng(transformedImage, &encodedImage))
            {
                setFailure(
                    failure,
                    ImageFrameTransformError::EncodeFailed,
                    prepared.frameIndex);
                return false;
            }

            ImageFrameData& targetFrame =
                result.frames[static_cast<size_t>(prepared.frameIndex)];
            targetFrame.encodedImage = std::move(encodedImage);
            targetFrame.decodedImage = std::move(transformedImage);
            targetFrame.xOffset = prepared.xOffset;
            targetFrame.yOffset = prepared.yOffset;
            result.changed = true;
        }
    }
    catch (const std::bad_alloc&)
    {
        setFailure(failure, ImageFrameTransformError::ImageAllocationFailed);
        return false;
    }

    *edit = std::move(result);
    return true;
}
