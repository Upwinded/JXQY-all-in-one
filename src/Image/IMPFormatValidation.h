#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace IMPFormatValidation
{
constexpr int ImageHeaderLength = 48;
constexpr int FrameHeaderLength = 16;

inline bool readInt32(const char* data, size_t size, size_t offset, int32_t& value)
{
    if (data == nullptr || offset > size || size - offset < sizeof(value))
    {
        return false;
    }
    std::memcpy(&value, data + offset, sizeof(value));
    return true;
}

inline bool validate(const char* data, int size)
{
    if (data == nullptr || size < ImageHeaderLength ||
        std::memcmp(data, "IMG File Ver1.0", 16) != 0)
    {
        return false;
    }

    int32_t frameCount = 0;
    int32_t directions = 0;
    const size_t totalSize = static_cast<size_t>(size);
    if (!readInt32(data, totalSize, 16, frameCount) ||
        !readInt32(data, totalSize, 20, directions) ||
        frameCount < 0 || directions <= 0)
    {
        return false;
    }

    size_t offset = ImageHeaderLength;
    size_t remaining = totalSize - offset;
    if (static_cast<size_t>(frameCount) > remaining / FrameHeaderLength)
    {
        return false;
    }

    for (int32_t frameIndex = 0; frameIndex < frameCount; frameIndex++)
    {
        int32_t dataLength = 0;
        if (!readInt32(data, totalSize, offset, dataLength) || dataLength < 0 ||
            remaining < FrameHeaderLength)
        {
            return false;
        }
        offset += FrameHeaderLength;
        remaining -= FrameHeaderLength;
        if (static_cast<size_t>(dataLength) > remaining)
        {
            return false;
        }
        offset += static_cast<size_t>(dataLength);
        remaining -= static_cast<size_t>(dataLength);
    }
    return true;
}
}
