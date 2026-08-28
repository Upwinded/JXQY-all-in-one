#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace MapV3ContractFixture
{
inline constexpr int32_t HeaderStringLength = 16;
inline constexpr int32_t BaseHeaderLength = 192;
inline constexpr int32_t PathLength = 256;
inline constexpr int32_t HeaderLength = BaseHeaderLength + PathLength;
inline constexpr int32_t MpcCount = 255;
inline constexpr int32_t NameLength = 128;
inline constexpr int32_t InfoLength = 160;
inline constexpr int32_t TileLength = 10;
inline constexpr char Header[HeaderStringLength] = "MAP File Ver3.0";
inline constexpr char MpcPath[] = "mpc/map/shared-scene/";
inline constexpr char MpcName[] = "shared-tile.mpc";
inline constexpr int32_t MpcIndex = 17;
inline constexpr int32_t MpcDynamic = 1;
inline constexpr int32_t MpcObstacle = 2;
inline constexpr int32_t MpcNil = 33;
inline constexpr uint8_t LayerFrames[3] = {11, 12, 13};
inline constexpr uint8_t LayerMpcs[3] = {21, 22, 23};
inline constexpr uint8_t TileObstacle = 0x80;
inline constexpr uint8_t TileTrap = 7;
inline constexpr uint8_t TileEnd[2] = {0xAA, 0x55};

inline void writeInt32(std::vector<uint8_t>& buffer, size_t offset, int32_t value)
{
    std::memcpy(buffer.data() + offset, &value, sizeof(value));
}

inline std::vector<uint8_t> build()
{
    constexpr size_t mpcBytes = static_cast<size_t>(MpcCount) * InfoLength;
    std::vector<uint8_t> buffer(
        static_cast<size_t>(HeaderLength) + mpcBytes + TileLength, 0);

    std::memcpy(buffer.data(), Header, HeaderStringLength);
    buffer[HeaderStringLength + 3] = 0x4A;
    std::memcpy(buffer.data() + 32, MpcPath, sizeof(MpcPath) - 1);
    writeInt32(buffer, 64, TileLength);
    writeInt32(buffer, 68, 1);
    writeInt32(buffer, 72, 1);
    writeInt32(buffer, 76, InfoLength);
    writeInt32(buffer, 80, NameLength);
    writeInt32(buffer, 84, HeaderLength);
    writeInt32(buffer, 88, PathLength);
    writeInt32(buffer, 92, 0x01);
    writeInt32(buffer, 96, MpcCount);
    buffer[84 + 20] = 0x6A;

    std::memcpy(buffer.data() + BaseHeaderLength, MpcPath, sizeof(MpcPath) - 1);
    const size_t firstMpcOffset = HeaderLength;
    std::memcpy(buffer.data() + firstMpcOffset, MpcName, sizeof(MpcName) - 1);
    writeInt32(buffer, firstMpcOffset + NameLength, MpcIndex);
    writeInt32(buffer, firstMpcOffset + NameLength + 4, MpcDynamic);
    writeInt32(buffer, firstMpcOffset + NameLength + 8, MpcObstacle);
    writeInt32(buffer, firstMpcOffset + NameLength + 12, MpcNil);
    buffer[firstMpcOffset + NameLength + 16 + 3] = 0x5B;

    const size_t tileOffset = static_cast<size_t>(HeaderLength) + mpcBytes;
    for (int layer = 0; layer < 3; ++layer)
    {
        buffer[tileOffset + layer * 2] = LayerFrames[layer];
        buffer[tileOffset + layer * 2 + 1] = LayerMpcs[layer];
    }
    buffer[tileOffset + 6] = TileObstacle;
    buffer[tileOffset + 7] = TileTrap;
    buffer[tileOffset + 8] = TileEnd[0];
    buffer[tileOffset + 9] = TileEnd[1];
    return buffer;
}
}
