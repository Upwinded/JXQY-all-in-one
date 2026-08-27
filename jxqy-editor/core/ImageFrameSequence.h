#pragma once

#include <QByteArray>
#include <QImage>

#include <cstdint>
#include <vector>

constexpr int MaximumImageFrameCount = 10000;

struct ImageFrameData
{
    QByteArray encodedImage;
    QImage decodedImage;
    int32_t xOffset = 0;
    int32_t yOffset = 0;
    int32_t reserved = 0;
};

enum class ImageFrameMoveDirection
{
    Earlier,
    Later
};

struct ImageFrameSequenceEdit
{
    std::vector<ImageFrameData> frames;
    std::vector<int> selectedIndices;
    int currentIndex = -1;
    bool changed = false;
};

class ImageFrameSequence
{
public:
    static bool insertFramesAt(
        const std::vector<ImageFrameData>& frames,
        int insertionIndex,
        const std::vector<ImageFrameData>& insertedFrames,
        ImageFrameSequenceEdit* edit);

    static bool copySelectedFrames(
        const std::vector<ImageFrameData>& frames,
        const std::vector<int>& selectedIndices,
        std::vector<ImageFrameData>* copiedFrames);

    static bool insertFramesAfter(
        const std::vector<ImageFrameData>& frames,
        int currentIndex,
        const std::vector<ImageFrameData>& insertedFrames,
        ImageFrameSequenceEdit* edit);

    static bool duplicateSelectedFrames(
        const std::vector<ImageFrameData>& frames,
        const std::vector<int>& selectedIndices,
        ImageFrameSequenceEdit* edit);

    static bool moveSelectedFrames(
        const std::vector<ImageFrameData>& frames,
        const std::vector<int>& selectedIndices,
        int currentIndex,
        ImageFrameMoveDirection direction,
        ImageFrameSequenceEdit* edit);
};
