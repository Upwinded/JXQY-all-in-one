#pragma once

#include "ImageFrameSequence.h"

#include <vector>

enum class ImageFrameTransformOperation
{
    RotateLeft90,
    RotateRight90,
    FlipHorizontal,
    FlipVertical
};

enum class ImageFrameTransformError
{
    None,
    InvalidDocument,
    EmptySelection,
    DuplicateIndex,
    IndexOutOfRange,
    CurrentIndexInvalid,
    InvalidOperation,
    InvalidFrame,
    OffsetOutOfRange,
    ImageAllocationFailed,
    EncodeFailed
};

struct ImageFrameTransformFailure
{
    ImageFrameTransformError error = ImageFrameTransformError::None;
    int frameIndex = -1;
};

class ImageFrameTransform
{
public:
    static bool transformSelectedFrames(
        const std::vector<ImageFrameData>& frames,
        const std::vector<int>& selectedIndices,
        int currentIndex,
        ImageFrameTransformOperation operation,
        ImageFrameSequenceEdit* edit,
        ImageFrameTransformFailure* failure = nullptr);
};
