#include "PicFileEditor.h"
#include "AuthoringMutationGate.h"
#include "IMPImageFile.h"

#include <fstream>
#include <cstring>
#include <climits>
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <QBuffer>
#include <QPainter>
#include <QSaveFile>

namespace
{
constexpr int LegacyMpcFileHeadSize = 124;
constexpr int LegacyAsfFileHeadSize = 60;
constexpr uint64_t MaxDecodedPixelCount = 64ULL * 1024ULL * 1024ULL;

bool hasValidImageDimensions(int32_t width, int32_t height)
{
    return width > 0 &&
        height > 0 &&
        static_cast<uint64_t>(width) * static_cast<uint64_t>(height) <= MaxDecodedPixelCount;
}

void advancePixelPosition(int& x, int& y, int width, int count)
{
    if (width <= 0 || count <= 0)
    {
        return;
    }
    int linearPosition = y * width + x + count;
    y = linearPosition / width;
    x = linearPosition % width;
}

bool readInt32At(const uint8_t* data, int bufferLen, size_t offset, int32_t& value)
{
    if (offset > static_cast<size_t>(bufferLen) ||
        static_cast<size_t>(bufferLen) - offset < sizeof(value))
    {
        return false;
    }

    std::memcpy(&value, data + offset, sizeof(value));
    return true;
}

std::string normalizePathForMpcRule(std::string fileName)
{
    std::replace(fileName.begin(), fileName.end(), '\\', '/');
    std::transform(fileName.begin(), fileName.end(), fileName.begin(), [](unsigned char value)
    {
        if (value >= 'A' && value <= 'Z')
        {
            return static_cast<char>(value - 'A' + 'a');
        }
        return static_cast<char>(value);
    });
    return fileName;
}

bool containsPathSegment(const std::string& path, const std::string& segment)
{
    return path.rfind(segment, 0) == 0 ||
        path.find("/" + segment) != std::string::npos;
}

bool shouldUseMpcPaletteAlphaForPath(const std::string& fileName)
{
    const std::string path = normalizePathForMpcRule(fileName);
    return containsPathSegment(path, "mpc/effect/") ||
        containsPathSegment(path, "mpc/magic/") ||
        path.rfind("mpc/ui/column/column2.mpc", 0) == 0 ||
        path.find("/mpc/ui/column/column2.mpc") != std::string::npos;
}

bool shouldPreserveAsfPixelAlphaForPath(const std::string& fileName)
{
    // ASF stores per-run alpha and UI resources rely on intermediate values for
    // soft panels and gauges. MPC-only binary transparency rules do not apply.
    (void)fileName;
    return true;
}
}

PicFileEditor::PicFileEditor()
{
}

PicFileEditor::~PicFileEditor()
{
    clear();
}

void PicFileEditor::clear()
{
    useMpcPaletteAlpha = false;
    preserveAsfPixelAlpha = true;
    clearPicFileData(&picFileData);
}

void PicFileEditor::clearPicFileData(PicFileData* picFile)
{
    if (picFile == nullptr)
        return;

    picFile->picType = PicType::None;
    picFile->mpcFileHead.picCount = 0;
    picFile->asfFileHead.picCount = 0;
    picFile->palette.length = 0;
    picFile->palette.colors.clear();
    picFile->pics.clear();
}

bool PicFileEditor::loadFromFile(const std::string& fileName)
{
    std::vector<uint8_t> buffer = Util::readFileToBuffer(fileName);
    if (buffer.size() < 16 ||
        buffer.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        return false;

    if (!loadFromBuffer(buffer.data(), static_cast<int>(buffer.size())))
    {
        return false;
    }

    picFileData.fileName = fileName;
    useMpcPaletteAlpha = shouldUseMpcPaletteAlphaForPath(fileName);
    preserveAsfPixelAlpha = shouldPreserveAsfPixelAlphaForPath(fileName);
    return true;
}

bool PicFileEditor::loadFromBuffer(const uint8_t* data, int bufferLen)
{
    if (data == nullptr || bufferLen < 16)
        return false;

    PicType picType = Util::detectPicType(data);
    if (picType == PicType::None)
        return false;

    clear();
    picFileData.picType = picType;

    bool loaded = false;
    switch (picType)
    {
    case PicType::Mpc:
        loaded = loadMPC(data, bufferLen);
        break;
    case PicType::Shd:
        loaded = loadSHD(data, bufferLen);
        break;
    case PicType::Asf100:
    case PicType::Asf101:
        loaded = loadASF(data, bufferLen);
        break;
    case PicType::Pic:
        loaded = loadPIC(data, bufferLen);
        break;
    case PicType::Imp:
        loaded = loadIMP(data, bufferLen);
        break;
    case PicType::Img:
        loaded = loadIMG(data, bufferLen);
        break;
    default:
        break;
    }

    if (!loaded)
        clear();
    return loaded;
}

bool PicFileEditor::loadMPC(const uint8_t* data, int bufferLen)
{
    if (bufferLen < static_cast<int>(sizeof(MPCFileHead)))
        return false;

    std::memcpy(&picFileData.mpcFileHead, data, sizeof(MPCFileHead));
    picFileData.palette.length = picFileData.mpcFileHead.paletteLen;
    if (picFileData.palette.length < 0 || picFileData.palette.length > 256)
        return false;

    if (picFileData.mpcFileHead.picCount <= 0 || picFileData.mpcFileHead.picCount > 10000)
        return false;

    size_t paletteBytes =
        static_cast<size_t>(picFileData.palette.length) * sizeof(ColorARGB);
    size_t offsetTableBytes =
        static_cast<size_t>(picFileData.mpcFileHead.picCount) * sizeof(int32_t);
    size_t fileHeadSize = sizeof(MPCFileHead);
    bool absoluteOffsets = false;

    auto selectLayout = [&](size_t candidateHeadSize) {
        size_t tableOffset = candidateHeadSize + paletteBytes;
        if (tableOffset > static_cast<size_t>(bufferLen) ||
            static_cast<size_t>(bufferLen) - tableOffset < offsetTableBytes)
        {
            return false;
        }

        size_t dataStart = tableOffset + offsetTableBytes;
        int32_t firstOffset = 0;
        if (!readInt32At(data, bufferLen, tableOffset, firstOffset))
            return false;

        if (firstOffset == 0)
        {
            fileHeadSize = candidateHeadSize;
            absoluteOffsets = false;
            return true;
        }
        if (firstOffset >= 0 && static_cast<size_t>(firstOffset) == dataStart)
        {
            fileHeadSize = candidateHeadSize;
            absoluteOffsets = true;
            return true;
        }
        return false;
    };

    if (!selectLayout(sizeof(MPCFileHead)) &&
        !selectLayout(LegacyMpcFileHeadSize))
    {
        return false;
    }

    if (fileHeadSize == LegacyMpcFileHeadSize)
        picFileData.mpcFileHead.mpcNull2[7] = 0;

    const uint8_t* palettePtr = data + fileHeadSize;
    if (paletteBytes > 0)
    {
        picFileData.palette.colors.resize(picFileData.palette.length);
        std::memcpy(picFileData.palette.colors.data(), palettePtr, paletteBytes);
    }

    size_t tableOffset = fileHeadSize + paletteBytes;
    size_t dataStart = tableOffset + offsetTableBytes;
    std::vector<int32_t> frameOffsets(picFileData.mpcFileHead.picCount);
    for (int i = 0; i < picFileData.mpcFileHead.picCount; i++)
    {
        if (!readInt32At(
                data,
                bufferLen,
                tableOffset + static_cast<size_t>(i) * sizeof(int32_t),
                frameOffsets[i]) ||
            frameOffsets[i] < 0)
        {
            return false;
        }
    }

    picFileData.pics.resize(picFileData.mpcFileHead.picCount);
    for (int i = 0; i < picFileData.mpcFileHead.picCount; i++)
    {
        size_t frameOffset = absoluteOffsets
            ? static_cast<size_t>(frameOffsets[i])
            : dataStart + static_cast<size_t>(frameOffsets[i]);
        if (frameOffset > static_cast<size_t>(bufferLen) ||
            static_cast<size_t>(bufferLen) - frameOffset < sizeof(MPCPicHead))
        {
            return false;
        }

        std::memcpy(
            &picFileData.pics[i].picHead,
            data + frameOffset,
            sizeof(MPCPicHead));

        int32_t rawLen = picFileData.pics[i].picHead.dataLen;
        if (rawLen < static_cast<int32_t>(sizeof(MPCPicHead)))
            return false;
        if (static_cast<size_t>(rawLen) > static_cast<size_t>(bufferLen) - frameOffset)
            return false;

        int32_t dataLen = rawLen - static_cast<int32_t>(sizeof(MPCPicHead));
        if (dataLen > 0 &&
            !hasValidImageDimensions(
                picFileData.pics[i].picHead.width,
                picFileData.pics[i].picHead.height))
        {
            return false;
        }
        if (dataLen > 0)
        {
            picFileData.pics[i].data.resize(dataLen);
            std::memcpy(
                picFileData.pics[i].data.data(),
                data + frameOffset + sizeof(MPCPicHead),
                dataLen);
        }
    }

    return true;
}

bool PicFileEditor::loadSHD(const uint8_t* data, int bufferLen)
{
    if (bufferLen < static_cast<int>(sizeof(MPCFileHead)))
        return false;

    std::memcpy(&picFileData.mpcFileHead, data, sizeof(MPCFileHead));
    if (picFileData.mpcFileHead.picCount <= 0 || picFileData.mpcFileHead.picCount > 10000)
        return false;

    size_t offsetTableBytes =
        static_cast<size_t>(picFileData.mpcFileHead.picCount) * sizeof(int32_t);
    size_t fileHeadSize = sizeof(MPCFileHead);
    bool absoluteOffsets = false;

    auto selectLayout = [&](size_t candidateHeadSize) {
        if (candidateHeadSize > static_cast<size_t>(bufferLen) ||
            static_cast<size_t>(bufferLen) - candidateHeadSize < offsetTableBytes)
        {
            return false;
        }

        size_t dataStart = candidateHeadSize + offsetTableBytes;
        int32_t firstOffset = 0;
        if (!readInt32At(data, bufferLen, candidateHeadSize, firstOffset))
            return false;
        if (firstOffset == 0)
        {
            fileHeadSize = candidateHeadSize;
            absoluteOffsets = false;
            return true;
        }
        if (firstOffset >= 0 && static_cast<size_t>(firstOffset) == dataStart)
        {
            fileHeadSize = candidateHeadSize;
            absoluteOffsets = true;
            return true;
        }
        return false;
    };

    if (!selectLayout(sizeof(MPCFileHead)) &&
        !selectLayout(LegacyMpcFileHeadSize))
    {
        return false;
    }

    if (fileHeadSize == LegacyMpcFileHeadSize)
        picFileData.mpcFileHead.mpcNull2[7] = 0;

    size_t dataStart = fileHeadSize + offsetTableBytes;
    std::vector<int32_t> frameOffsets(picFileData.mpcFileHead.picCount);
    for (int i = 0; i < picFileData.mpcFileHead.picCount; i++)
    {
        if (!readInt32At(
                data,
                bufferLen,
                fileHeadSize + static_cast<size_t>(i) * sizeof(int32_t),
                frameOffsets[i]) ||
            frameOffsets[i] < 0)
        {
            return false;
        }
    }

    picFileData.pics.resize(picFileData.mpcFileHead.picCount);
    for (int i = 0; i < picFileData.mpcFileHead.picCount; i++)
    {
        size_t frameOffset = absoluteOffsets
            ? static_cast<size_t>(frameOffsets[i])
            : dataStart + static_cast<size_t>(frameOffsets[i]);
        if (frameOffset > static_cast<size_t>(bufferLen) ||
            static_cast<size_t>(bufferLen) - frameOffset < sizeof(MPCPicHead))
        {
            return false;
        }

        std::memcpy(
            &picFileData.pics[i].picHead,
            data + frameOffset,
            sizeof(MPCPicHead));

        int32_t rawLen = picFileData.pics[i].picHead.dataLen;
        if (rawLen < static_cast<int32_t>(sizeof(MPCPicHead)))
            return false;
        if (static_cast<size_t>(rawLen) > static_cast<size_t>(bufferLen) - frameOffset)
            return false;

        int32_t dataLen = rawLen - static_cast<int32_t>(sizeof(MPCPicHead));
        if (dataLen > 0 &&
            !hasValidImageDimensions(
                picFileData.pics[i].picHead.width,
                picFileData.pics[i].picHead.height))
        {
            return false;
        }
        if (dataLen > 0)
        {
            picFileData.pics[i].data.resize(dataLen);
            std::memcpy(
                picFileData.pics[i].data.data(),
                data + frameOffset + sizeof(MPCPicHead),
                dataLen);
        }
    }

    return true;
}

bool PicFileEditor::loadASF(const uint8_t* data, int bufferLen)
{
    if (bufferLen < static_cast<int>(sizeof(ASFFileHead)))
        return false;

    std::memcpy(&picFileData.asfFileHead, data, sizeof(ASFFileHead));
    picFileData.palette.length = picFileData.asfFileHead.paletteLen;
    if (picFileData.palette.length < 0 || picFileData.palette.length > 256)
        return false;

    if (picFileData.asfFileHead.picCount <= 0 || picFileData.asfFileHead.picCount > 10000)
        return false;
    if (!hasValidImageDimensions(
            picFileData.asfFileHead.width,
            picFileData.asfFileHead.height))
    {
        return false;
    }

    picFileData.mpcFileHead.picCount = picFileData.asfFileHead.picCount;
    picFileData.pics.resize(picFileData.asfFileHead.picCount);

    size_t paletteBytes =
        static_cast<size_t>(picFileData.palette.length) * sizeof(ColorARGB);
    size_t tableBytes =
        static_cast<size_t>(picFileData.asfFileHead.picCount) * 2 * sizeof(int32_t);
    size_t fileHeadSize = sizeof(ASFFileHead);

    auto selectLayout = [&](size_t candidateHeadSize) {
        size_t tableOffset = candidateHeadSize + paletteBytes;
        if (tableOffset > static_cast<size_t>(bufferLen) ||
            static_cast<size_t>(bufferLen) - tableOffset < tableBytes)
        {
            return false;
        }

        size_t dataStart = tableOffset + tableBytes;
        int32_t firstOffset = 0;
        return readInt32At(data, bufferLen, tableOffset, firstOffset) &&
            firstOffset >= 0 &&
            static_cast<size_t>(firstOffset) == dataStart;
    };

    if (selectLayout(sizeof(ASFFileHead)))
    {
        fileHeadSize = sizeof(ASFFileHead);
    }
    else if (selectLayout(LegacyAsfFileHeadSize))
    {
        fileHeadSize = LegacyAsfFileHeadSize;
        picFileData.asfFileHead.asfNull2[3] = 0;
    }
    else
    {
        return false;
    }

    if (paletteBytes > 0)
    {
        picFileData.palette.colors.resize(picFileData.palette.length);
        std::memcpy(
            picFileData.palette.colors.data(),
            data + fileHeadSize,
            paletteBytes);
    }

    size_t tableOffset = fileHeadSize + paletteBytes;
    size_t dataStart = tableOffset + tableBytes;
    std::vector<int32_t> frameOffsets(picFileData.asfFileHead.picCount);
    for (int i = 0; i < picFileData.asfFileHead.picCount; i++)
    {
        size_t entryOffset = tableOffset + static_cast<size_t>(i) * 8;
        if (!readInt32At(data, bufferLen, entryOffset, frameOffsets[i]) ||
            !readInt32At(
                data,
                bufferLen,
                entryOffset + sizeof(int32_t),
                picFileData.pics[i].picHead.dataLen))
        {
            return false;
        }
        if (frameOffsets[i] < 0 ||
            static_cast<size_t>(frameOffsets[i]) < dataStart ||
            picFileData.pics[i].picHead.dataLen < 0)
        {
            return false;
        }
        picFileData.pics[i].picHead.width = picFileData.asfFileHead.width;
        picFileData.pics[i].picHead.height = picFileData.asfFileHead.height;
    }

    for (int i = 0; i < picFileData.asfFileHead.picCount; i++)
    {
        int dataLen = picFileData.pics[i].picHead.dataLen;
        size_t frameOffset = static_cast<size_t>(frameOffsets[i]);
        if (frameOffset > static_cast<size_t>(bufferLen) ||
            static_cast<size_t>(dataLen) > static_cast<size_t>(bufferLen) - frameOffset)
        {
            return false;
        }
        if (dataLen > 0)
        {
            picFileData.pics[i].data.resize(dataLen);
            std::memcpy(
                picFileData.pics[i].data.data(),
                data + frameOffset,
                dataLen);
        }
    }

    return true;
}

bool PicFileEditor::loadPIC(const uint8_t* data, int bufferLen)
{
    const uint8_t* bufferEnd = data + bufferLen;
    const uint8_t* readPtr = data + 16;

    if (readPtr + 4 > bufferEnd)
        return false;

    int32_t picCount;
    std::memcpy(&picCount, readPtr, 4);
    readPtr += 4;

    if (picCount <= 0 || picCount > 10000)
        return false;

    picFileData.mpcFileHead.picCount = picCount;
    picFileData.pics.resize(picCount);

    if (readPtr + picCount * 4 > bufferEnd)
        return false;

    for (int i = 0; i < picCount; i++)
    {
        std::memcpy(&picFileData.pics[i].picHead.dataLen, readPtr, 4);
        readPtr += 4;
    }

    for (int i = 0; i < picCount; i++)
    {
        int dataLen = picFileData.pics[i].picHead.dataLen;
        if (dataLen < 0)
            return false;
        if (dataLen > 16)
        {
            if (static_cast<size_t>(dataLen) >
                static_cast<size_t>(bufferEnd - readPtr))
                return false;
            picFileData.pics[i].data.resize(dataLen);
            std::memcpy(picFileData.pics[i].data.data(), readPtr, dataLen);
            readPtr += dataLen;
        }
    }

    return true;
}

bool PicFileEditor::loadIMP(const uint8_t* data, int bufferLen)
{
    const uint8_t* bufferEnd = data + bufferLen;
    const uint8_t* readPtr = data;

    if (readPtr + 16 + 4 + 4 + 4 + 5 * 4 > bufferEnd)
        return false;

    readPtr += 16;

    int32_t frameCount = 0;
    std::memcpy(&frameCount, readPtr, 4);
    readPtr += 4;

    int32_t directions = 1;
    std::memcpy(&directions, readPtr, 4);
    readPtr += 4;

    int32_t interval = 30;
    std::memcpy(&interval, readPtr, 4);
    readPtr += 4;

    readPtr += 5 * 4;

    if (frameCount <= 0 || frameCount > 10000)
        return false;

    picFileData.mpcFileHead.picCount = frameCount;
    picFileData.mpcFileHead.directions = directions;
    picFileData.mpcFileHead.interval = interval;
    picFileData.pics.resize(frameCount);

    for (int i = 0; i < frameCount; i++)
    {
        if (readPtr + 4 * 4 > bufferEnd)
            return false;

        int32_t dataLen = 0;
        std::memcpy(&dataLen, readPtr, 4);
        readPtr += 4;

        int32_t xOffset = 0;
        std::memcpy(&xOffset, readPtr, 4);
        readPtr += 4;

        int32_t yOffset = 0;
        std::memcpy(&yOffset, readPtr, 4);
        readPtr += 4;

        readPtr += 1 * 4;

        picFileData.pics[i].picHead.dataLen = dataLen;
        picFileData.pics[i].xOffset = xOffset;
        picFileData.pics[i].yOffset = yOffset;

        if (dataLen < 0)
            return false;
        if (dataLen > 0)
        {
            if (static_cast<size_t>(dataLen) >
                static_cast<size_t>(bufferEnd - readPtr))
                return false;
            picFileData.pics[i].data.resize(dataLen);
            std::memcpy(picFileData.pics[i].data.data(), readPtr, dataLen);
            readPtr += dataLen;
        }
    }

    return true;
}

bool PicFileEditor::loadIMG(const uint8_t* data, int bufferLen)
{
    const int maxImgSize = 64 * 1024 * 1024; // 64MB upper limit
    if (bufferLen <= 0 || bufferLen > maxImgSize)
        return false;

    picFileData.mpcFileHead.picCount = 1;
    picFileData.pics.resize(1);

    picFileData.pics[0].data.resize(bufferLen);
    std::memcpy(picFileData.pics[0].data.data(), data, bufferLen);
    picFileData.pics[0].picHead.dataLen = bufferLen;

    return true;
}

QImage PicFileEditor::decodeMPCFrameFromData(const MPCPic& pic, const MPCPalette& palette) const
{
    int width = pic.picHead.width;
    int height = pic.picHead.height;
    if (width <= 0 || height <= 0)
        return QImage();

    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    int x = 0;
    int y = 0;
    int state = 0;

    for (int i = 0; i < static_cast<int>(pic.data.size()) && y < height; i++)
    {
        uint8_t byteValue = pic.data[i];

        if (state == 0)
        {
            if (byteValue <= 128)
            {
                state = byteValue;
            }
            else
            {
                advancePixelPosition(x, y, width, byteValue - 128);
            }
        }
        else
        {
            if (x >= 0 && y >= 0 && x < width && y < height)
            {
                if (static_cast<size_t>(byteValue) < palette.colors.size())
                {
                    const ColorARGB& color = palette.colors[byteValue];
                    const uint8_t alpha = useMpcPaletteAlpha ? color.alpha : 0xFF;
                    image.setPixel(x, y, qRgba(color.red, color.green, color.blue, alpha));
                }
            }
            advancePixelPosition(x, y, width, 1);
            state--;
        }
    }

    return image;
}

QImage PicFileEditor::decodeSHDFrameFromData(const MPCPic& pic) const
{
    int width = pic.picHead.width;
    int height = pic.picHead.height;
    if (width <= 0 || height <= 0)
        return QImage();

    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    int x = 0;
    int y = 0;

    for (int i = 0; i < static_cast<int>(pic.data.size()) && y < height; i++)
    {
        uint8_t byteValue = pic.data[i];

        if (byteValue <= 128)
        {
            for (int state = 0; state < byteValue; state++)
            {
                if (x >= 0 && y >= 0 && x < width && y < height)
                {
                    image.setPixel(x, y, qRgba(0, 0, 0, 128));
                }
                advancePixelPosition(x, y, width, 1);
            }
        }
        else
        {
            advancePixelPosition(x, y, width, byteValue - 128);
        }
    }

    return image;
}

QImage PicFileEditor::decodeASFFrameFromData(const MPCPic& pic, const ASFFileHead& asfHead, const MPCPalette& palette) const
{
    int width = asfHead.width;
    int height = asfHead.height;
    if (width <= 0 || height <= 0)
        return QImage();

    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    int x = 0;
    int y = 0;
    int i = 0;

    while (i < static_cast<int>(pic.data.size()) - 1)
    {
        uint8_t count = pic.data[i];
        uint8_t alpha = pic.data[i + 1];
        i += 2;

        if (alpha == 0)
        {
            advancePixelPosition(x, y, width, count);
        }
        else
        {
            for (int j = 0; j < count; j++)
            {
                if (i >= static_cast<int>(pic.data.size()))
                {
                    break;
                }

                uint8_t paletteIndex = pic.data[i];
                i++;

                if (x >= 0 && y >= 0 && x < width && y < height)
                {
                    if (static_cast<size_t>(paletteIndex) < palette.colors.size())
                    {
                        const ColorARGB& color = palette.colors[paletteIndex];
                        const uint8_t pixelAlpha = preserveAsfPixelAlpha ? alpha : 0xFF;
                        image.setPixel(x, y, qRgba(color.red, color.green, color.blue, pixelAlpha));
                    }
                }
                advancePixelPosition(x, y, width, 1);
            }
        }
    }

    return image;
}

QImage PicFileEditor::decodeMPCFrame(int frameIndex) const
{
    if (frameIndex < 0 || frameIndex >= static_cast<int>(picFileData.pics.size()))
        return QImage();

    return decodeMPCFrameFromData(picFileData.pics[frameIndex], picFileData.palette);
}

QImage PicFileEditor::decodeSHDFrame(int frameIndex) const
{
    if (frameIndex < 0 || frameIndex >= static_cast<int>(picFileData.pics.size()))
        return QImage();

    return decodeSHDFrameFromData(picFileData.pics[frameIndex]);
}

QImage PicFileEditor::decodeASFFrame(int frameIndex) const
{
    if (frameIndex < 0 || frameIndex >= static_cast<int>(picFileData.pics.size()))
        return QImage();

    return decodeASFFrameFromData(picFileData.pics[frameIndex], picFileData.asfFileHead, picFileData.palette);
}

QImage PicFileEditor::getFrameImage(int frameIndex) const
{
    if (frameIndex < 0 || frameIndex >= static_cast<int>(picFileData.pics.size()))
        return QImage();

    // After transparent-edge cropping (or any rewrite that stored PNG bytes +
    // explicit offsets), frames must be read as PNG regardless of the original
    // picType. This keeps public reads, repeated crops and saveAsIMP consistent.
    if (picFileData.framesNormalizedToPng)
    {
        const auto& pic = picFileData.pics[frameIndex];
        if (!pic.data.empty())
        {
            QImage image;
            if (image.loadFromData(pic.data.data(), static_cast<int>(pic.data.size())))
                return image.convertToFormat(QImage::Format_ARGB32);
        }
        return QImage();
    }

    switch (picFileData.picType)
    {
    case PicType::Mpc:
        return decodeMPCFrame(frameIndex);
    case PicType::Shd:
        return decodeSHDFrame(frameIndex);
    case PicType::Asf100:
    case PicType::Asf101:
        return decodeASFFrame(frameIndex);
    case PicType::Imp:
    case PicType::Pic:
    case PicType::Img:
    {
        const auto& pic = picFileData.pics[frameIndex];
        if (!pic.data.empty())
        {
            QImage image;
            if (image.loadFromData(pic.data.data(), static_cast<int>(pic.data.size())))
            {
                return image.convertToFormat(QImage::Format_ARGB32);
            }
        }
        return QImage();
    }
    default:
        return QImage();
    }
}

bool PicFileEditor::saveAsIMP(const std::string& fileName) const
{
    return saveAsIMP(&picFileData, fileName);
}

bool PicFileEditor::saveAsIMP(const PicFileData* picFile, const std::string& fileName) const
{
    auto mutationLease =
        AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(QString::fromUtf8(
                fileName.data(), static_cast<qsizetype>(fileName.size())));
    if (!mutationLease)
        return false;

    if (picFile == nullptr || picFile->pics.empty() || picFile->pics.size() > 10000)
        return false;

    QSaveFile file(QString::fromUtf8(fileName.data(), static_cast<qsizetype>(fileName.size())));
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    auto write = [&file](const void* data, qint64 size) {
        return size == 0 || file.write(static_cast<const char*>(data), size) == size;
    };

    static const char imgHead[] = "IMG File Ver1.0";
    if (!write(imgHead, 16))
    {
        file.cancelWriting();
        return false;
    }

    int32_t frameCount = static_cast<int32_t>(picFile->pics.size());
    int32_t directions = 1;
    int32_t interval = 30;
    int32_t imageNull[5] = {0};

    if (picFile->picType == PicType::Mpc || picFile->picType == PicType::Shd)
    {
        directions = picFile->mpcFileHead.directions;
        interval = picFile->mpcFileHead.interval;
    }
    else if (picFile->picType == PicType::Asf100 || picFile->picType == PicType::Asf101)
    {
        directions = picFile->asfFileHead.directions;
        interval = picFile->asfFileHead.interval;
    }
    else if (picFile->picType == PicType::Imp || picFile->picType == PicType::Img || picFile->picType == PicType::Pic)
    {
        if (picFile->mpcFileHead.directions > 0)
            directions = picFile->mpcFileHead.directions;
        if (picFile->mpcFileHead.interval > 0)
            interval = picFile->mpcFileHead.interval;
    }

    if (!write(&frameCount, sizeof(frameCount)) ||
        !write(&directions, sizeof(directions)) ||
        !write(&interval, sizeof(interval)) ||
        !write(imageNull, sizeof(imageNull)))
    {
        file.cancelWriting();
        return false;
    }

    for (int i = 0; i < frameCount; i++)
    {
        QImage frameImage;
        int32_t xOffset = 0;
        int32_t yOffset = 0;
        int32_t frameNull = 0;

        if (picFile->framesNormalizedToPng)
        {
            // Frames were rewritten (e.g. by transparent-edge cropping) as PNG
            // bytes with explicit per-frame offsets. Decode PNG directly and use
            // the stored offsets regardless of the original picType.
            if (!picFile->pics[i].data.empty())
            {
                frameImage.loadFromData(picFile->pics[i].data.data(), static_cast<int>(picFile->pics[i].data.size()));
                frameImage = frameImage.convertToFormat(QImage::Format_ARGB32);
            }
            xOffset = picFile->pics[i].xOffset;
            yOffset = picFile->pics[i].yOffset;
        }
        else
        {
            switch (picFile->picType)
            {
            case PicType::Mpc:
                frameImage = decodeMPCFrameFromData(picFile->pics[i], picFile->palette);
                break;
            case PicType::Shd:
                frameImage = decodeSHDFrameFromData(picFile->pics[i]);
                break;
            case PicType::Asf100:
            case PicType::Asf101:
                frameImage = decodeASFFrameFromData(picFile->pics[i], picFile->asfFileHead, picFile->palette);
                break;
            case PicType::Pic:
            case PicType::Imp:
            case PicType::Img:
            {
                if (!picFile->pics[i].data.empty())
                {
                    frameImage.loadFromData(picFile->pics[i].data.data(), static_cast<int>(picFile->pics[i].data.size()));
                    frameImage = frameImage.convertToFormat(QImage::Format_ARGB32);
                }
                break;
            }
            default:
                break;
            }

            if (picFile->picType == PicType::Mpc || picFile->picType == PicType::Shd)
            {
                xOffset = picFile->pics[i].picHead.width / 2;
                yOffset = picFile->pics[i].picHead.height - picFile->mpcFileHead.yMove;
            }
            else if (picFile->picType == PicType::Asf100 || picFile->picType == PicType::Asf101)
            {
                xOffset = picFile->asfFileHead.xMove;
                yOffset = picFile->asfFileHead.yMove + 16;
            }
            else if (picFile->picType == PicType::Imp || picFile->picType == PicType::Img || picFile->picType == PicType::Pic)
            {
                xOffset = picFile->pics[i].xOffset;
                yOffset = picFile->pics[i].yOffset;
            }
        }

        if (frameImage.isNull() && !picFile->pics[i].data.empty())
        {
            file.cancelWriting();
            return false;
        }

        QByteArray byteArray;
        QBuffer buffer(&byteArray);
        buffer.open(QIODevice::WriteOnly);
        if (!frameImage.isNull())
        {
            if (!frameImage.save(&buffer, "PNG"))
            {
                file.cancelWriting();
                return false;
            }
        }

        int32_t dataLen = static_cast<int32_t>(byteArray.size());
        if (!write(&dataLen, sizeof(dataLen)) ||
            !write(&xOffset, sizeof(xOffset)) ||
            !write(&yOffset, sizeof(yOffset)) ||
            !write(&frameNull, sizeof(frameNull)) ||
            !write(byteArray.constData(), dataLen))
        {
            file.cancelWriting();
            return false;
        }
    }

    return file.commit();
}

TransparentCropResult PicFileEditor::cropTransparentEdges(const QImage& image, int xOffset, int yOffset)
{
    TransparentCropResult result;
    result.xOffset = xOffset;
    result.yOffset = yOffset;

    // Null or empty input is treated as a fully transparent frame: it collapses
    // to a 1x1 transparent frame with the original offset preserved.
    if (image.isNull() || image.width() <= 0 || image.height() <= 0)
    {
        result.image = QImage(1, 1, QImage::Format_ARGB32);
        result.image.fill(Qt::transparent);
        result.wasFullyTransparent = true;
        return result;
    }

    int left = image.width();
    int top = image.height();
    int right = -1;
    int bottom = -1;

    for (int y = 0; y < image.height(); y++)
    {
        for (int x = 0; x < image.width(); x++)
        {
            if (qAlpha(image.pixel(x, y)) != 0)
            {
                if (x < left) left = x;
                if (x > right) right = x;
                if (y < top) top = y;
                if (y > bottom) bottom = y;
            }
        }
    }

    if (right < left || bottom < top)
    {
        // No opaque pixel: keep a stable 1x1 transparent frame, offset unchanged.
        result.image = QImage(1, 1, QImage::Format_ARGB32);
        result.image.fill(Qt::transparent);
        result.wasFullyTransparent = true;
        return result;
    }

    int cropLeft = left;
    int cropTop = top;
    int cropWidth = right - left + 1;
    int cropHeight = bottom - top + 1;

    result.image = image.copy(cropLeft, cropTop, cropWidth, cropHeight)
                       .convertToFormat(QImage::Format_ARGB32);
    // Strict offset adjustment. Negative offsets are allowed and must not be
    // padded, zeroed or clamped: the world position of every opaque pixel is
    // preserved because (px - cropLeft) - (xOffset - cropLeft) == px - xOffset.
    result.xOffset = xOffset - cropLeft;
    result.yOffset = yOffset - cropTop;
    result.wasFullyTransparent = false;
    return result;
}

bool PicFileEditor::calculateFrameOffsetChanges(
    const std::vector<ImageFrameOffset>& currentOffsets,
    ImageFrameOffsetBatchMode mode,
    bool updateX,
    bool updateY,
    int32_t xValue,
    int32_t yValue,
    std::vector<ImageFrameOffsetChange>* changes)
{
    if (changes == nullptr)
        return false;
    changes->clear();
    if (!updateX && !updateY)
        return false;

    std::unordered_set<int> frameIndices;
    std::vector<ImageFrameOffsetChange> calculatedChanges;
    calculatedChanges.reserve(currentOffsets.size());
    const int64_t minimum = (std::numeric_limits<int32_t>::min)();
    const int64_t maximum = (std::numeric_limits<int32_t>::max)();

    for (const ImageFrameOffset& current : currentOffsets)
    {
        if (current.frameIndex < 0 ||
            !frameIndices.insert(current.frameIndex).second)
        {
            return false;
        }

        int64_t newX = current.xOffset;
        int64_t newY = current.yOffset;
        switch (mode)
        {
        case ImageFrameOffsetBatchMode::Set:
            if (updateX)
                newX = xValue;
            if (updateY)
                newY = yValue;
            break;
        case ImageFrameOffsetBatchMode::Add:
            if (updateX)
                newX += xValue;
            if (updateY)
                newY += yValue;
            break;
        default:
            return false;
        }

        if (newX < minimum || newX > maximum ||
            newY < minimum || newY > maximum)
        {
            return false;
        }
        if (newX == current.xOffset && newY == current.yOffset)
            continue;

        calculatedChanges.push_back({
            current.frameIndex,
            current.xOffset,
            current.yOffset,
            static_cast<int32_t>(newX),
            static_cast<int32_t>(newY)});
    }

    *changes = std::move(calculatedChanges);
    return true;
}

void PicFileEditor::cropTransparentEdgesAllFrames()
{
    if (picFileData.pics.empty())
        return;

    // directions/interval stay sourced from the original headers in saveAsIMP;
    // only per-frame pixels and offsets are rewritten here.
    for (size_t i = 0; i < picFileData.pics.size(); i++)
    {
        QImage frameImage = getFrameImage(static_cast<int>(i));

        int32_t xOffset = 0;
        int32_t yOffset = 0;
        getFrameOffset(static_cast<int>(i), &xOffset, &yOffset);

        TransparentCropResult cropped = cropTransparentEdges(frameImage, xOffset, yOffset);

        QByteArray pngBytes;
        if (!cropped.image.isNull())
        {
            QBuffer buffer(&pngBytes);
            buffer.open(QIODevice::WriteOnly);
            cropped.image.save(&buffer, "PNG");
        }

        MPCPic& pic = picFileData.pics[i];
        pic.xOffset = cropped.xOffset;
        pic.yOffset = cropped.yOffset;
        pic.picHead.width = cropped.image.width();
        pic.picHead.height = cropped.image.height();
        pic.picHead.dataLen = static_cast<int32_t>(pngBytes.size());
        pic.data.assign(pngBytes.constData(), pngBytes.constData() + pngBytes.size());
        pic.decodedImage = cropped.image;
    }

    picFileData.framesNormalizedToPng = true;
}

bool PicFileEditor::convertFileFormat(const std::string& inputFileName, const std::string& outputFileName, bool toIMP)
{
    if (toIMP)
    {
        if (!loadFromFile(inputFileName))
            return false;

        return saveAsIMP(outputFileName);
    }
    else
    {
        // Reverse conversion from IMP/IMG to MPC/SHD/ASF is not supported
        return false;
    }
}

bool PicFileEditor::convertFileFormat(const std::string& inputFileName, bool toIMP)
{
    std::string outputFileName = inputFileName;

    if (toIMP)
    {
        size_t dotPos = outputFileName.rfind('.');
        if (dotPos != std::string::npos)
            outputFileName = outputFileName.substr(0, dotPos);
        outputFileName += ".img";
    }

    return convertFileFormat(inputFileName, outputFileName, toIMP);
}

int PicFileEditor::getFrameCount() const
{
    return static_cast<int>(picFileData.pics.size());
}

int PicFileEditor::getDirection() const
{
    if (picFileData.picType == PicType::Mpc || picFileData.picType == PicType::Shd)
        return picFileData.mpcFileHead.directions;
    if (picFileData.picType == PicType::Asf100 || picFileData.picType == PicType::Asf101)
        return picFileData.asfFileHead.directions;
    return 1;
}

int PicFileEditor::getInterval() const
{
    if (picFileData.picType == PicType::Mpc || picFileData.picType == PicType::Shd)
        return picFileData.mpcFileHead.interval;
    if (picFileData.picType == PicType::Asf100 || picFileData.picType == PicType::Asf101)
        return picFileData.asfFileHead.interval;
    return 30;
}

PicFileData* PicFileEditor::getPicFileData()
{
    return &picFileData;
}

int PicFileEditor::findClosestPaletteIndex(const QColor& color, const MPCPalette& palette)
{
    int bestIndex = 0;
    int bestDistance = INT_MAX;

    for (int i = 0; i < static_cast<int>(palette.colors.size()); i++)
    {
        const ColorARGB& palColor = palette.colors[i];
        int dr = static_cast<int>(palColor.red) - color.red();
        int dg = static_cast<int>(palColor.green) - color.green();
        int db = static_cast<int>(palColor.blue) - color.blue();
        int distance = dr * dr + dg * dg + db * db;

        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestIndex = i;
            if (distance == 0)
                break;
        }
    }

    return bestIndex;
}

std::vector<uint8_t> PicFileEditor::encodeMPCFrame(const QImage& image, const MPCPalette& palette)
{
    std::vector<uint8_t> result;

    if (image.isNull() || palette.colors.empty())
        return result;

    int width = image.width();
    int height = image.height();

    for (int y = 0; y < height; y++)
    {
        int x = 0;
        while (x < width)
        {
            QRgb pixel = image.pixel(x, y);
            bool isTransparent = (qAlpha(pixel) == 0);

            if (isTransparent)
            {
                int skipCount = 0;
                while (x + skipCount < width && qAlpha(image.pixel(x + skipCount, y)) == 0)
                {
                    skipCount++;
                }

                while (skipCount > 0)
                {
                    int chunk = std::min(skipCount, 127);
                    result.push_back(static_cast<uint8_t>(chunk + 128));
                    skipCount -= chunk;
                    x += chunk;
                }
            }
            else
            {
                int runCount = 0;
                int startX = x;
                while (x + runCount < width && qAlpha(image.pixel(x + runCount, y)) != 0)
                {
                    runCount++;
                }

                int encoded = 0;
                while (encoded < runCount)
                {
                    int chunk = std::min(runCount - encoded, 128);
                    result.push_back(static_cast<uint8_t>(chunk));

                    for (int k = 0; k < chunk; k++)
                    {
                        QColor color = QColor::fromRgba(image.pixel(startX + encoded + k, y));
                        int paletteIndex = findClosestPaletteIndex(color, palette);
                        result.push_back(static_cast<uint8_t>(paletteIndex));
                    }

                    encoded += chunk;
                    x += chunk;
                }
            }
        }
    }

    return result;
}

std::vector<uint8_t> PicFileEditor::encodeSHDFrame(const QImage& image)
{
    std::vector<uint8_t> result;

    if (image.isNull())
        return result;

    int width = image.width();
    int height = image.height();

    for (int y = 0; y < height; y++)
    {
        int x = 0;
        while (x < width)
        {
            QRgb pixel = image.pixel(x, y);
            bool isShadow = (qAlpha(pixel) >= 64);

            if (!isShadow)
            {
                int skipCount = 0;
                while (x + skipCount < width && qAlpha(image.pixel(x + skipCount, y)) < 64)
                {
                    skipCount++;
                }

                while (skipCount > 0)
                {
                    int chunk = std::min(skipCount, 127);
                    result.push_back(static_cast<uint8_t>(chunk + 128));
                    skipCount -= chunk;
                    x += chunk;
                }
            }
            else
            {
                int runCount = 0;
                while (x + runCount < width && qAlpha(image.pixel(x + runCount, y)) >= 64)
                {
                    runCount++;
                }

                while (runCount > 0)
                {
                    int chunk = std::min(runCount, 128);
                    result.push_back(static_cast<uint8_t>(chunk));
                    runCount -= chunk;
                    x += chunk;
                }
            }
        }
    }

    return result;
}

std::vector<uint8_t> PicFileEditor::encodeASFFrame(const QImage& image, const MPCPalette& palette)
{
    std::vector<uint8_t> result;

    if (image.isNull() || palette.colors.empty())
        return result;

    int width = image.width();
    int height = image.height();

    for (int y = 0; y < height; y++)
    {
        int x = 0;
        while (x < width)
        {
            QRgb pixel = image.pixel(x, y);
            int alpha = qAlpha(pixel);

            if (alpha == 0)
            {
                int skipCount = 0;
                while (x + skipCount < width && qAlpha(image.pixel(x + skipCount, y)) == 0)
                {
                    skipCount++;
                }

                while (skipCount > 0)
                {
                    int chunk = std::min(skipCount, 255);
                    result.push_back(static_cast<uint8_t>(chunk));
                    result.push_back(0);
                    skipCount -= chunk;
                    x += chunk;
                }
            }
            else
            {
                uint8_t commonAlpha = static_cast<uint8_t>(alpha);
                int runCount = 0;
                int startX = x;
                while (x + runCount < width)
                {
                    QRgb nextPixel = image.pixel(x + runCount, y);
                    if (qAlpha(nextPixel) == 0)
                        break;
                    if (std::abs(qAlpha(nextPixel) - alpha) > 8)
                        break;
                    runCount++;
                }

                while (runCount > 0)
                {
                    int chunk = std::min(runCount, 255);
                    result.push_back(static_cast<uint8_t>(chunk));
                    result.push_back(commonAlpha);

                    for (int k = 0; k < chunk; k++)
                    {
                        QColor color = QColor::fromRgba(image.pixel(startX + k, y));
                        int paletteIndex = findClosestPaletteIndex(color, palette);
                        result.push_back(static_cast<uint8_t>(paletteIndex));
                    }

                    startX += chunk;
                    runCount -= chunk;
                    x += chunk;
                }
            }
        }
    }

    return result;
}

bool PicFileEditor::savePIC(const PicFileData* picFile, const std::string& fileName) const
{
    auto mutationLease =
        AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(QString::fromUtf8(
                fileName.data(), static_cast<qsizetype>(fileName.size())));
    if (!mutationLease)
        return false;

    if (picFile == nullptr || picFile->pics.empty())
        return false;

    FILE* file = Util::openFileForWriteUtf8(fileName);
    if (!file)
        return false;

    const char picHeader[16] = "PIC File Ver1.0";
    if (std::fwrite(picHeader, 1, 16, file) != 16)
    {
        std::fclose(file);
        return false;
    }

    int32_t picCount = static_cast<int32_t>(picFile->pics.size());
    if (std::fwrite(&picCount, 4, 1, file) != 1)
    {
        std::fclose(file);
        return false;
    }

    std::vector<int32_t> dataLenArray(picCount);
    for (int i = 0; i < picCount; i++)
    {
        int32_t rawSize = static_cast<int32_t>(picFile->pics[i].data.size());
        dataLenArray[i] = (rawSize > 16) ? rawSize : 0;
    }
    if (std::fwrite(dataLenArray.data(), 4, picCount, file) != static_cast<size_t>(picCount))
    {
        std::fclose(file);
        return false;
    }

    for (int i = 0; i < picCount; i++)
    {
        int32_t dataLen = dataLenArray[i];
        if (dataLen > 0 && !picFile->pics[i].data.empty())
        {
            if (std::fwrite(picFile->pics[i].data.data(), 1, dataLen, file) != static_cast<size_t>(dataLen))
            {
                std::fclose(file);
                return false;
            }
        }
    }

    if (std::fclose(file) != 0)
        return false;

    return true;
}

bool PicFileEditor::saveMPC(const PicFileData* picFile, const std::string& fileName) const
{
    auto mutationLease =
        AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(QString::fromUtf8(
                fileName.data(), static_cast<qsizetype>(fileName.size())));
    if (!mutationLease)
        return false;

    if (picFile == nullptr || picFile->pics.empty() || picFile->pics.size() > 10000)
        return false;

    int32_t paletteLength = static_cast<int32_t>(
        std::min<size_t>(picFile->palette.colors.size(), 256));
    int64_t frameBytes = 0;
    int32_t maxWidth = 0;
    int32_t maxHeight = 0;
    for (const auto& pic : picFile->pics)
    {
        if (pic.data.size() >
            static_cast<size_t>(std::numeric_limits<int32_t>::max() - sizeof(MPCPicHead)))
        {
            return false;
        }
        frameBytes += sizeof(MPCPicHead) + pic.data.size();
        if (frameBytes > std::numeric_limits<int32_t>::max())
            return false;
        maxWidth = std::max(maxWidth, pic.picHead.width);
        maxHeight = std::max(maxHeight, pic.picHead.height);
    }

    FILE* file = Util::openFileForWriteUtf8(fileName);
    if (!file)
        return false;

    MPCFileHead localHead = picFile->mpcFileHead;
    localHead.picCount = static_cast<int32_t>(picFile->pics.size());
    localHead.paletteLen = paletteLength;
    localHead.dataLen = static_cast<int32_t>(frameBytes);
    localHead.maxWidth = maxWidth;
    localHead.maxHeight = maxHeight;
    std::memcpy(localHead.head, "MPC File Ver2.0", 16);

    if (fwrite(&localHead, 1, sizeof(MPCFileHead), file) != sizeof(MPCFileHead))
    {
        fclose(file);
        return false;
    }

    if (paletteLength > 0)
    {
        size_t paletteSize = static_cast<size_t>(paletteLength) * sizeof(ColorARGB);
        if (fwrite(picFile->palette.colors.data(), 1, paletteSize, file) != paletteSize)
        {
            fclose(file);
            return false;
        }
    }

    std::vector<int32_t> offsets(picFile->pics.size());
    int32_t currentOffset = 0;

    for (size_t i = 0; i < picFile->pics.size(); i++)
    {
        offsets[i] = currentOffset;
        currentOffset += static_cast<int32_t>(
            sizeof(MPCPicHead) + picFile->pics[i].data.size());
    }

    for (size_t i = 0; i < picFile->pics.size(); i++)
    {
        if (fwrite(&offsets[i], 1, 4, file) != 4)
        {
            fclose(file);
            return false;
        }
    }

    for (const auto& pic : picFile->pics)
    {
        MPCPicHead localPicHead = pic.picHead;
        localPicHead.dataLen = static_cast<int32_t>(
            sizeof(MPCPicHead) + pic.data.size());
        if (fwrite(&localPicHead, 1, sizeof(MPCPicHead), file) != sizeof(MPCPicHead))
        {
            fclose(file);
            return false;
        }
        if (!pic.data.empty())
        {
            if (fwrite(pic.data.data(), 1, pic.data.size(), file) != pic.data.size())
            {
                fclose(file);
                return false;
            }
        }
    }

    if (fclose(file) != 0)
        return false;

    return true;
}

bool PicFileEditor::saveSHD(const PicFileData* picFile, const std::string& fileName) const
{
    auto mutationLease =
        AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(QString::fromUtf8(
                fileName.data(), static_cast<qsizetype>(fileName.size())));
    if (!mutationLease)
        return false;

    if (picFile == nullptr || picFile->pics.empty() || picFile->pics.size() > 10000)
        return false;

    int64_t frameBytes = 0;
    int32_t maxWidth = 0;
    int32_t maxHeight = 0;
    for (const auto& pic : picFile->pics)
    {
        if (pic.data.size() >
            static_cast<size_t>(std::numeric_limits<int32_t>::max() - sizeof(MPCPicHead)))
        {
            return false;
        }
        frameBytes += sizeof(MPCPicHead) + pic.data.size();
        if (frameBytes > std::numeric_limits<int32_t>::max())
            return false;
        maxWidth = std::max(maxWidth, pic.picHead.width);
        maxHeight = std::max(maxHeight, pic.picHead.height);
    }

    FILE* file = Util::openFileForWriteUtf8(fileName);
    if (!file)
        return false;

    MPCFileHead localHead = picFile->mpcFileHead;
    localHead.picCount = static_cast<int32_t>(picFile->pics.size());
    localHead.paletteLen = 0;
    localHead.dataLen = static_cast<int32_t>(frameBytes);
    localHead.maxWidth = maxWidth;
    localHead.maxHeight = maxHeight;
    std::memcpy(localHead.head, "SHD File Ver2.0", 16);

    if (fwrite(&localHead, 1, sizeof(MPCFileHead), file) != sizeof(MPCFileHead))
    {
        fclose(file);
        return false;
    }

    std::vector<int32_t> offsets(picFile->pics.size());
    int32_t currentOffset = 0;

    for (size_t i = 0; i < picFile->pics.size(); i++)
    {
        offsets[i] = currentOffset;
        currentOffset += static_cast<int32_t>(
            sizeof(MPCPicHead) + picFile->pics[i].data.size());
    }

    for (size_t i = 0; i < picFile->pics.size(); i++)
    {
        if (fwrite(&offsets[i], 1, 4, file) != 4)
        {
            fclose(file);
            return false;
        }
    }

    for (const auto& pic : picFile->pics)
    {
        MPCPicHead localPicHead = pic.picHead;
        localPicHead.dataLen = static_cast<int32_t>(
            sizeof(MPCPicHead) + pic.data.size());
        if (fwrite(&localPicHead, 1, sizeof(MPCPicHead), file) != sizeof(MPCPicHead))
        {
            fclose(file);
            return false;
        }
        if (!pic.data.empty())
        {
            if (fwrite(pic.data.data(), 1, pic.data.size(), file) != pic.data.size())
            {
                fclose(file);
                return false;
            }
        }
    }

    if (fclose(file) != 0)
        return false;

    return true;
}

bool PicFileEditor::saveASF(const PicFileData* picFile, const std::string& fileName) const
{
    auto mutationLease =
        AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(QString::fromUtf8(
                fileName.data(), static_cast<qsizetype>(fileName.size())));
    if (!mutationLease)
        return false;

    if (picFile == nullptr || picFile->pics.empty() || picFile->pics.size() > 10000)
        return false;
    if (!hasValidImageDimensions(
            picFile->asfFileHead.width,
            picFile->asfFileHead.height))
    {
        return false;
    }

    int32_t paletteLength = static_cast<int32_t>(
        std::min<size_t>(picFile->palette.colors.size(), 256));
    int64_t frameBytes = 0;
    for (const auto& pic : picFile->pics)
    {
        frameBytes += pic.data.size();
        if (frameBytes > std::numeric_limits<int32_t>::max())
            return false;
    }

    int64_t dataStartOffset64 =
        sizeof(ASFFileHead) +
        static_cast<int64_t>(paletteLength) * sizeof(ColorARGB) +
        static_cast<int64_t>(picFile->pics.size()) * 8;
    if (dataStartOffset64 + frameBytes > std::numeric_limits<int32_t>::max())
        return false;

    FILE* file = Util::openFileForWriteUtf8(fileName);
    if (!file)
        return false;

    ASFFileHead localHead = picFile->asfFileHead;
    localHead.picCount = static_cast<int32_t>(picFile->pics.size());
    localHead.paletteLen = paletteLength;

    if (fwrite(&localHead, 1, sizeof(ASFFileHead), file) != sizeof(ASFFileHead))
    {
        fclose(file);
        return false;
    }

    if (paletteLength > 0)
    {
        size_t paletteSize = static_cast<size_t>(paletteLength) * sizeof(ColorARGB);
        if (fwrite(picFile->palette.colors.data(), 1, paletteSize, file) != paletteSize)
        {
            fclose(file);
            return false;
        }
    }

    std::vector<int32_t> frameOffsets(picFile->pics.size());
    std::vector<int32_t> frameDataLens(picFile->pics.size());

    int32_t dataStartOffset = static_cast<int32_t>(dataStartOffset64);

    int32_t currentDataOffset = dataStartOffset;
    for (size_t i = 0; i < picFile->pics.size(); i++)
    {
        frameOffsets[i] = currentDataOffset;
        frameDataLens[i] = static_cast<int32_t>(picFile->pics[i].data.size());
        currentDataOffset += frameDataLens[i];
    }

    for (size_t i = 0; i < picFile->pics.size(); i++)
    {
        if (fwrite(&frameOffsets[i], 1, 4, file) != 4)
        {
            fclose(file);
            return false;
        }
        if (fwrite(&frameDataLens[i], 1, 4, file) != 4)
        {
            fclose(file);
            return false;
        }
    }

    for (const auto& pic : picFile->pics)
    {
        if (!pic.data.empty())
        {
            if (fwrite(pic.data.data(), 1, pic.data.size(), file) != pic.data.size())
            {
                fclose(file);
                return false;
            }
        }
    }

    if (fclose(file) != 0)
        return false;

    return true;
}

void PicFileEditor::setFrameImage(int frameIndex, const QImage& image)
{
    if (frameIndex < 0 || frameIndex >= static_cast<int>(picFileData.pics.size()))
        return;

    if (image.isNull())
        return;

    picFileData.pics[frameIndex].decodedImage = image;

    if (picFileData.framesNormalizedToPng)
    {
        QByteArray byteArray;
        QBuffer buffer(&byteArray);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        picFileData.pics[frameIndex].picHead.width = image.width();
        picFileData.pics[frameIndex].picHead.height = image.height();
        picFileData.pics[frameIndex].data.assign(
            reinterpret_cast<const uint8_t*>(byteArray.constData()),
            reinterpret_cast<const uint8_t*>(byteArray.constData()) +
                byteArray.size());
        picFileData.pics[frameIndex].picHead.dataLen =
            static_cast<int32_t>(byteArray.size());
        return;
    }

    switch (picFileData.picType)
    {
    case PicType::Mpc:
    {
        picFileData.pics[frameIndex].picHead.width = image.width();
        picFileData.pics[frameIndex].picHead.height = image.height();
        std::vector<uint8_t> encoded = encodeMPCFrame(image, picFileData.palette);
        picFileData.pics[frameIndex].data = encoded;
        picFileData.pics[frameIndex].picHead.dataLen = sizeof(MPCPicHead) + static_cast<int32_t>(encoded.size());
        break;
    }
    case PicType::Shd:
    {
        picFileData.pics[frameIndex].picHead.width = image.width();
        picFileData.pics[frameIndex].picHead.height = image.height();
        std::vector<uint8_t> encoded = encodeSHDFrame(image);
        picFileData.pics[frameIndex].data = encoded;
        picFileData.pics[frameIndex].picHead.dataLen = sizeof(MPCPicHead) + static_cast<int32_t>(encoded.size());
        break;
    }
    case PicType::Asf100:
    case PicType::Asf101:
    {
        QImage frameImage = image.convertToFormat(QImage::Format_ARGB32);
        if (picFileData.pics.size() <= 1 ||
            picFileData.asfFileHead.width <= 0 ||
            picFileData.asfFileHead.height <= 0)
        {
            picFileData.asfFileHead.width = frameImage.width();
            picFileData.asfFileHead.height = frameImage.height();
        }
        else if (frameImage.width() != picFileData.asfFileHead.width ||
                 frameImage.height() != picFileData.asfFileHead.height)
        {
            QImage normalized(
                picFileData.asfFileHead.width,
                picFileData.asfFileHead.height,
                QImage::Format_ARGB32);
            normalized.fill(Qt::transparent);
            QPainter painter(&normalized);
            painter.drawImage(0, 0, frameImage);
            painter.end();
            frameImage = normalized;
        }

        picFileData.pics[frameIndex].decodedImage = frameImage;
        picFileData.pics[frameIndex].picHead.width = picFileData.asfFileHead.width;
        picFileData.pics[frameIndex].picHead.height = picFileData.asfFileHead.height;
        std::vector<uint8_t> encoded = encodeASFFrame(frameImage, picFileData.palette);
        picFileData.pics[frameIndex].data = encoded;
        picFileData.pics[frameIndex].picHead.dataLen = static_cast<int32_t>(encoded.size());
        break;
    }
    case PicType::Pic:
    case PicType::Imp:
    case PicType::Img:
    {
        QByteArray byteArray;
        QBuffer buffer(&byteArray);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        picFileData.pics[frameIndex].data.resize(byteArray.size());
        std::memcpy(picFileData.pics[frameIndex].data.data(), byteArray.constData(), byteArray.size());
        picFileData.pics[frameIndex].picHead.dataLen = static_cast<int32_t>(byteArray.size());
        break;
    }
    default:
        break;
    }
}

void PicFileEditor::insertFrame(int frameIndex, const QImage& image)
{
    if (frameIndex < 0)
        frameIndex = 0;
    if (frameIndex > static_cast<int>(picFileData.pics.size()))
        frameIndex = static_cast<int>(picFileData.pics.size());

    MPCPic newPic;
    newPic.picHead.dataLen = sizeof(MPCPicHead);
    newPic.picHead.width = image.width();
    newPic.picHead.height = image.height();
    newPic.decodedImage = image;

    picFileData.pics.insert(picFileData.pics.begin() + frameIndex, newPic);

    setFrameImage(frameIndex, image);

    picFileData.mpcFileHead.picCount = static_cast<int32_t>(picFileData.pics.size());
    picFileData.asfFileHead.picCount = static_cast<int32_t>(picFileData.pics.size());
}

void PicFileEditor::addFrame(const QImage& image)
{
    insertFrame(static_cast<int>(picFileData.pics.size()), image);
}

void PicFileEditor::removeFrame(int frameIndex)
{
    if (frameIndex < 0 || frameIndex >= static_cast<int>(picFileData.pics.size()))
        return;

    picFileData.pics.erase(picFileData.pics.begin() + frameIndex);
    picFileData.mpcFileHead.picCount = static_cast<int32_t>(picFileData.pics.size());
    picFileData.asfFileHead.picCount = static_cast<int32_t>(picFileData.pics.size());
}

bool PicFileEditor::getFrameSequence(
    std::vector<ImageFrameData>* frames) const
{
    if (frames == nullptr)
        return false;
    frames->clear();
    if (picFileData.pics.empty() ||
        picFileData.pics.size() > MaximumImageFrameCount)
    {
        return false;
    }

    std::vector<ImageFrameData> capturedFrames;
    capturedFrames.reserve(picFileData.pics.size());
    for (int index = 0;
         index < static_cast<int>(picFileData.pics.size());
         index++)
    {
        const QImage frameImage = getFrameImage(index);
        if (frameImage.isNull())
            return false;

        ImageFrameData frame;
        QBuffer buffer(&frame.encodedImage);
        if (!buffer.open(QIODevice::WriteOnly) ||
            !frameImage.save(&buffer, "PNG"))
        {
            return false;
        }
        frame.decodedImage =
            frameImage.convertToFormat(QImage::Format_ARGB32);
        getFrameOffset(index, &frame.xOffset, &frame.yOffset);
        frame.reserved = picFileData.pics[index].picHead.picNull[0];
        capturedFrames.push_back(std::move(frame));
    }
    *frames = std::move(capturedFrames);
    return true;
}

bool PicFileEditor::setFrameSequence(
    const std::vector<ImageFrameData>& frames)
{
    if (frames.empty() || frames.size() > MaximumImageFrameCount)
        return false;

    std::vector<MPCPic> replacementFrames;
    replacementFrames.reserve(frames.size());
    for (const ImageFrameData& source : frames)
    {
        if (source.encodedImage.size() >
                static_cast<qsizetype>(
                    (std::numeric_limits<int32_t>::max)()) ||
            source.encodedImage.isEmpty() || source.decodedImage.isNull())
        {
            return false;
        }

        MPCPic frame;
        frame.picHead.dataLen =
            static_cast<int32_t>(source.encodedImage.size());
        frame.picHead.width = source.decodedImage.width();
        frame.picHead.height = source.decodedImage.height();
        frame.picHead.picNull[0] = source.reserved;
        frame.data.assign(
            reinterpret_cast<const uint8_t*>(source.encodedImage.constData()),
            reinterpret_cast<const uint8_t*>(source.encodedImage.constData()) +
                source.encodedImage.size());
        frame.decodedImage = source.decodedImage;
        frame.xOffset = source.xOffset;
        frame.yOffset = source.yOffset;
        replacementFrames.push_back(std::move(frame));
    }

    picFileData.pics = std::move(replacementFrames);
    picFileData.mpcFileHead.picCount =
        static_cast<int32_t>(picFileData.pics.size());
    picFileData.asfFileHead.picCount =
        static_cast<int32_t>(picFileData.pics.size());
    picFileData.framesNormalizedToPng = true;
    return true;
}

bool PicFileEditor::ensureFrameOffsetsEditable()
{
    if (picFileData.pics.empty())
        return false;
    if (picFileData.framesNormalizedToPng ||
        picFileData.picType == PicType::Imp ||
        picFileData.picType == PicType::Img)
    {
        return true;
    }

    // Legacy formats derive offsets from shared headers and cannot represent
    // arbitrary per-frame edits. Decode every frame first into a temporary
    // vector so a single decode/PNG failure leaves the loaded document intact.
    std::vector<MPCPic> normalizedFrames = picFileData.pics;
    for (int i = 0; i < static_cast<int>(picFileData.pics.size()); i++)
    {
        const QImage frameImage = getFrameImage(i);
        if (frameImage.isNull() && !picFileData.pics[i].data.empty())
            return false;

        int32_t xOffset = 0;
        int32_t yOffset = 0;
        getFrameOffset(i, &xOffset, &yOffset);

        QByteArray pngBytes;
        if (!frameImage.isNull())
        {
            QBuffer buffer(&pngBytes);
            if (!buffer.open(QIODevice::WriteOnly) ||
                !frameImage.save(&buffer, "PNG"))
            {
                return false;
            }
        }

        MPCPic& normalizedFrame = normalizedFrames[i];
        normalizedFrame.xOffset = xOffset;
        normalizedFrame.yOffset = yOffset;
        normalizedFrame.picHead.width = frameImage.width();
        normalizedFrame.picHead.height = frameImage.height();
        normalizedFrame.picHead.dataLen =
            static_cast<int32_t>(pngBytes.size());
        normalizedFrame.data.assign(
            reinterpret_cast<const uint8_t*>(pngBytes.constData()),
            reinterpret_cast<const uint8_t*>(pngBytes.constData()) +
                pngBytes.size());
        normalizedFrame.decodedImage = frameImage;
    }

    picFileData.pics = std::move(normalizedFrames);
    picFileData.framesNormalizedToPng = true;
    return true;
}

bool PicFileEditor::setFrameOffset(
    int frameIndex, int32_t xOffset, int32_t yOffset)
{
    if (frameIndex < 0 || frameIndex >= static_cast<int>(picFileData.pics.size()))
        return false;

    if (!ensureFrameOffsetsEditable())
        return false;

    picFileData.pics[frameIndex].xOffset = xOffset;
    picFileData.pics[frameIndex].yOffset = yOffset;
    return true;
}

void PicFileEditor::getFrameOffset(int frameIndex, int32_t* xOffset, int32_t* yOffset) const
{
    if (frameIndex < 0 || frameIndex >= static_cast<int>(picFileData.pics.size()))
        return;

    // After transparent-edge cropping the per-frame stored offsets are authoritative.
    if (picFileData.framesNormalizedToPng)
    {
        if (xOffset != nullptr)
            *xOffset = picFileData.pics[frameIndex].xOffset;
        if (yOffset != nullptr)
            *yOffset = picFileData.pics[frameIndex].yOffset;
        return;
    }

    if (xOffset != nullptr)
    {
        if (picFileData.picType == PicType::Mpc || picFileData.picType == PicType::Shd)
            *xOffset = picFileData.pics[frameIndex].picHead.width / 2;
        else if (picFileData.picType == PicType::Asf100 || picFileData.picType == PicType::Asf101)
            *xOffset = picFileData.asfFileHead.xMove;
        else if (picFileData.picType == PicType::Imp || picFileData.picType == PicType::Img)
            *xOffset = picFileData.pics[frameIndex].xOffset;
        else
            *xOffset = 0;
    }

    if (yOffset != nullptr)
    {
        if (picFileData.picType == PicType::Mpc || picFileData.picType == PicType::Shd)
            *yOffset = picFileData.pics[frameIndex].picHead.height - picFileData.mpcFileHead.yMove;
        else if (picFileData.picType == PicType::Asf100 || picFileData.picType == PicType::Asf101)
            *yOffset = picFileData.asfFileHead.yMove + 16;
        else if (picFileData.picType == PicType::Imp || picFileData.picType == PicType::Img)
            *yOffset = picFileData.pics[frameIndex].yOffset;
        else
            *yOffset = 0;
    }
}

void PicFileEditor::setDirection(int direction)
{
    if (picFileData.picType == PicType::Mpc || picFileData.picType == PicType::Shd)
        picFileData.mpcFileHead.directions = direction;
    else if (picFileData.picType == PicType::Asf100 || picFileData.picType == PicType::Asf101)
        picFileData.asfFileHead.directions = direction;
}

void PicFileEditor::setInterval(int interval)
{
    if (picFileData.picType == PicType::Mpc || picFileData.picType == PicType::Shd)
        picFileData.mpcFileHead.interval = interval;
    else if (picFileData.picType == PicType::Asf100 || picFileData.picType == PicType::Asf101)
        picFileData.asfFileHead.interval = interval;
}
