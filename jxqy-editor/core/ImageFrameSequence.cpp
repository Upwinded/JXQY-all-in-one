#include "ImageFrameSequence.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <unordered_set>

namespace
{
bool isValidFrame(const ImageFrameData& frame)
{
    if (frame.encodedImage.size() >
        static_cast<qsizetype>((std::numeric_limits<int32_t>::max)()))
    {
        return false;
    }
    return !frame.encodedImage.isEmpty() && !frame.decodedImage.isNull();
}

bool validateFrames(const std::vector<ImageFrameData>& frames)
{
    if (frames.empty() || frames.size() > MaximumImageFrameCount)
        return false;
    return std::all_of(frames.begin(), frames.end(), isValidFrame);
}

bool normalizeSelection(
    int frameCount,
    const std::vector<int>& selectedIndices,
    std::vector<int>* normalizedSelection)
{
    if (normalizedSelection == nullptr)
        return false;
    normalizedSelection->clear();
    if (frameCount <= 0 || selectedIndices.empty())
        return false;

    std::unordered_set<int> uniqueIndices;
    uniqueIndices.reserve(selectedIndices.size());
    for (int index : selectedIndices)
    {
        if (index < 0 || index >= frameCount ||
            !uniqueIndices.insert(index).second)
        {
            return false;
        }
        normalizedSelection->push_back(index);
    }
    std::sort(normalizedSelection->begin(), normalizedSelection->end());
    return true;
}

bool validateInsertedFrames(
    const std::vector<ImageFrameData>& frames,
    const std::vector<ImageFrameData>& insertedFrames)
{
    if (!validateFrames(frames) || insertedFrames.empty())
        return false;
    if (insertedFrames.size() >
        static_cast<size_t>(MaximumImageFrameCount) - frames.size())
    {
        return false;
    }
    return std::all_of(
        insertedFrames.begin(), insertedFrames.end(), isValidFrame);
}
}

bool ImageFrameSequence::copySelectedFrames(
    const std::vector<ImageFrameData>& frames,
    const std::vector<int>& selectedIndices,
    std::vector<ImageFrameData>* copiedFrames)
{
    if (copiedFrames == nullptr)
        return false;
    copiedFrames->clear();
    if (!validateFrames(frames))
        return false;

    std::vector<int> normalizedSelection;
    if (!normalizeSelection(
            static_cast<int>(frames.size()),
            selectedIndices,
            &normalizedSelection))
    {
        return false;
    }

    std::vector<ImageFrameData> result;
    result.reserve(normalizedSelection.size());
    for (int index : normalizedSelection)
        result.push_back(frames[static_cast<size_t>(index)]);
    *copiedFrames = std::move(result);
    return true;
}

bool ImageFrameSequence::insertFramesAt(
    const std::vector<ImageFrameData>& frames,
    int insertionIndex,
    const std::vector<ImageFrameData>& insertedFrames,
    ImageFrameSequenceEdit* edit)
{
    if (edit == nullptr)
        return false;
    *edit = {};
    if (!validateInsertedFrames(frames, insertedFrames) ||
        insertionIndex < 0 ||
        insertionIndex > static_cast<int>(frames.size()))
    {
        return false;
    }

    const size_t insertionOffset = static_cast<size_t>(insertionIndex);
    edit->frames.reserve(frames.size() + insertedFrames.size());
    edit->frames.insert(
        edit->frames.end(), frames.begin(), frames.begin() + insertionOffset);
    edit->frames.insert(
        edit->frames.end(), insertedFrames.begin(), insertedFrames.end());
    edit->frames.insert(
        edit->frames.end(), frames.begin() + insertionOffset, frames.end());
    edit->selectedIndices.reserve(insertedFrames.size());
    for (size_t offset = 0; offset < insertedFrames.size(); offset++)
    {
        edit->selectedIndices.push_back(
            static_cast<int>(insertionOffset + offset));
    }
    edit->currentIndex = insertionIndex;
    edit->changed = true;
    return true;
}

bool ImageFrameSequence::insertFramesAfter(
    const std::vector<ImageFrameData>& frames,
    int currentIndex,
    const std::vector<ImageFrameData>& insertedFrames,
    ImageFrameSequenceEdit* edit)
{
    if (edit == nullptr)
        return false;
    *edit = {};
    if (currentIndex < 0 || currentIndex >= static_cast<int>(frames.size()))
        return false;
    return insertFramesAt(
        frames, currentIndex + 1, insertedFrames, edit);
}

bool ImageFrameSequence::duplicateSelectedFrames(
    const std::vector<ImageFrameData>& frames,
    const std::vector<int>& selectedIndices,
    ImageFrameSequenceEdit* edit)
{
    if (edit == nullptr)
        return false;
    *edit = {};

    std::vector<int> normalizedSelection;
    std::vector<ImageFrameData> copiedFrames;
    if (!validateFrames(frames) ||
        !normalizeSelection(
            static_cast<int>(frames.size()),
            selectedIndices,
            &normalizedSelection) ||
        !copySelectedFrames(frames, normalizedSelection, &copiedFrames))
    {
        return false;
    }

    return insertFramesAfter(
        frames, normalizedSelection.back(), copiedFrames, edit);
}

bool ImageFrameSequence::moveSelectedFrames(
    const std::vector<ImageFrameData>& frames,
    const std::vector<int>& selectedIndices,
    int currentIndex,
    ImageFrameMoveDirection direction,
    ImageFrameSequenceEdit* edit)
{
    if (edit == nullptr)
        return false;
    *edit = {};
    if (!validateFrames(frames))
        return false;

    std::vector<int> normalizedSelection;
    if (!normalizeSelection(
            static_cast<int>(frames.size()),
            selectedIndices,
            &normalizedSelection) ||
        currentIndex < 0 || currentIndex >= static_cast<int>(frames.size()) ||
        !std::binary_search(
            normalizedSelection.begin(), normalizedSelection.end(), currentIndex))
    {
        return false;
    }

    edit->frames = frames;
    std::vector<bool> selected(frames.size(), false);
    for (int index : normalizedSelection)
        selected[static_cast<size_t>(index)] = true;
    std::vector<int> originalIndices(frames.size());
    std::iota(originalIndices.begin(), originalIndices.end(), 0);

    if (direction == ImageFrameMoveDirection::Earlier)
    {
        for (size_t index = 1; index < edit->frames.size(); index++)
        {
            if (selected[index] && !selected[index - 1])
            {
                std::swap(edit->frames[index], edit->frames[index - 1]);
                std::swap(originalIndices[index], originalIndices[index - 1]);
                selected[index] = false;
                selected[index - 1] = true;
                edit->changed = true;
            }
        }
    }
    else if (direction == ImageFrameMoveDirection::Later)
    {
        for (size_t index = edit->frames.size() - 1; index > 0; index--)
        {
            const size_t earlierIndex = index - 1;
            if (selected[earlierIndex] && !selected[index])
            {
                std::swap(edit->frames[earlierIndex], edit->frames[index]);
                std::swap(originalIndices[earlierIndex], originalIndices[index]);
                selected[earlierIndex] = false;
                selected[index] = true;
                edit->changed = true;
            }
        }
    }
    else
    {
        return false;
    }

    for (size_t index = 0; index < selected.size(); index++)
    {
        if (selected[index])
            edit->selectedIndices.push_back(static_cast<int>(index));
        if (originalIndices[index] == currentIndex)
            edit->currentIndex = static_cast<int>(index);
    }
    return edit->currentIndex >= 0;
}
