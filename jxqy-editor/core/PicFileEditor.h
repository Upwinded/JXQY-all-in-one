#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <QImage>
#include "ImageFrameSequence.h"
#include "Util.h"

static_assert(sizeof(int32_t) == 4, "int32_t must be 4 bytes");

struct ColorARGB
{
    uint8_t blue = 0;
    uint8_t green = 0;
    uint8_t red = 0;
    uint8_t alpha = 0;
};

static_assert(sizeof(ColorARGB) == 4, "ColorARGB must be 4 bytes");

struct MPCPicHead
{
    int32_t dataLen = 0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t picNull[2] = {0};
};

static_assert(sizeof(MPCPicHead) == 20, "MPCPicHead must be 20 bytes matching Pascal TMPCpichead");

struct MPCPic
{
    MPCPicHead picHead;
    std::vector<uint8_t> data;
    QImage decodedImage;
    int32_t xOffset = 0;
    int32_t yOffset = 0;
};

struct MPCPalette
{
    int32_t length = 0;
    std::vector<ColorARGB> colors;
};

#pragma pack(push, 1)
struct MPCFileHead
{
    char head[16] = {0};
    int32_t mpcNull1[12] = {0};
    int32_t dataLen = 0;
    int32_t maxWidth = 0;
    int32_t maxHeight = 0;
    int32_t picCount = 0;
    int32_t directions = 0;
    int32_t paletteLen = 0;
    int32_t interval = 0;
    int32_t yMove = 0;
    int32_t mpcNull2[8] = {0};
};
#pragma pack(pop)

static_assert(sizeof(MPCFileHead) == 128, "MPCFileHead must be 128 bytes");

#pragma pack(push, 1)
struct ASFFileHead
{
    char head[16] = {0};
    int32_t width = 0;
    int32_t height = 0;
    int32_t picCount = 0;
    int32_t directions = 0;
    int32_t paletteLen = 0;
    int32_t interval = 0;
    int32_t xMove = 0;
    int32_t yMove = 0;
    int32_t asfNull2[4] = {0};
};
#pragma pack(pop)

static_assert(sizeof(ASFFileHead) == 64, "ASFFileHead must be 64 bytes");

struct PicFileData
{
    std::string fileName;
    uint32_t fileID = 0;
    PicType picType = PicType::None;
    MPCFileHead mpcFileHead;
    ASFFileHead asfFileHead;
    MPCPalette palette;
    std::vector<MPCPic> pics;
    // When true, every pics[i].data holds PNG bytes and pics[i].xOffset/yOffset
    // are explicit per-frame offsets (e.g. after transparent-edge cropping).
    // saveAsIMP then decodes frames as PNG and uses the stored offsets regardless
    // of picType, while directions/interval still come from the original headers.
    bool framesNormalizedToPng = false;
};

// Result of cropping transparent outer edges from a single frame.
// The offset is adjusted strictly as newOffset = oldOffset - cropLeft/Top so the
// world position of every non-transparent pixel is preserved. Fully transparent
// frames collapse to a 1x1 transparent image with the original offset kept.
struct TransparentCropResult
{
    QImage image;
    int xOffset = 0;
    int yOffset = 0;
    bool wasFullyTransparent = false;
};

enum class ImageFrameOffsetBatchMode
{
    Set,
    Add
};

struct ImageFrameOffset
{
    int frameIndex = -1;
    int32_t xOffset = 0;
    int32_t yOffset = 0;
};

struct ImageFrameOffsetChange
{
    int frameIndex = -1;
    int32_t oldXOffset = 0;
    int32_t oldYOffset = 0;
    int32_t newXOffset = 0;
    int32_t newYOffset = 0;
};

class PicFileEditor
{
public:
    PicFileEditor();
    ~PicFileEditor();
    PicFileEditor(const PicFileEditor&) = delete;
    PicFileEditor& operator=(const PicFileEditor&) = delete;
    PicFileEditor(PicFileEditor&&) noexcept = default;
    PicFileEditor& operator=(PicFileEditor&&) noexcept = default;

    bool loadFromFile(const std::string& fileName);
    bool loadFromBuffer(const uint8_t* data, int bufferLen);

    void clear();
    void clearPicFileData(PicFileData* picFile);

    QImage decodeMPCFrame(int frameIndex) const;
    QImage decodeSHDFrame(int frameIndex) const;
    QImage decodeASFFrame(int frameIndex) const;
    QImage getFrameImage(int frameIndex) const;

    bool saveAsIMP(const std::string& fileName) const;
    bool saveAsIMP(const PicFileData* picFile, const std::string& fileName) const;

    bool convertFileFormat(const std::string& inputFileName, const std::string& outputFileName, bool toIMP);
    bool convertFileFormat(const std::string& inputFileName, bool toIMP);

    int getFrameCount() const;
    int getDirection() const;
    int getInterval() const;

    void setFrameImage(int frameIndex, const QImage& image);
    void insertFrame(int frameIndex, const QImage& image);
    void addFrame(const QImage& image);
    void removeFrame(int frameIndex);
    bool getFrameSequence(std::vector<ImageFrameData>* frames) const;
    bool setFrameSequence(const std::vector<ImageFrameData>& frames);
    bool ensureFrameOffsetsEditable();
    bool setFrameOffset(int frameIndex, int32_t xOffset, int32_t yOffset);
    void getFrameOffset(int frameIndex, int32_t* xOffset, int32_t* yOffset) const;

    void setDirection(int direction);
    void setInterval(int interval);

    PicFileData* getPicFileData();

    static std::vector<uint8_t> encodeMPCFrame(const QImage& image, const MPCPalette& palette);
    static std::vector<uint8_t> encodeSHDFrame(const QImage& image);
    static std::vector<uint8_t> encodeASFFrame(const QImage& image, const MPCPalette& palette);

    static int findClosestPaletteIndex(const QColor& color, const MPCPalette& palette);

    // Shared transparent-edge crop primitive used by the GUI manual crop, the CLI
    // and the legacy resource migration. Crops alpha == 0 outer edges and adjusts
    // the offset as newOffset = oldOffset - cropLeft/Top (negative offsets allowed,
    // no transparent padding, no zeroing). Fully transparent frames become a 1x1
    // transparent frame with the original offset preserved.
    static TransparentCropResult cropTransparentEdges(const QImage& image, int xOffset, int yOffset);

    // Builds one atomic batch of per-frame offset changes. Invalid or duplicate
    // indices, a request with no enabled axis, and int32 addition overflow are
    // rejected without returning a partial change list. Unchanged frames are
    // omitted so callers do not create dirty/no-op undo commands.
    static bool calculateFrameOffsetChanges(
        const std::vector<ImageFrameOffset>& currentOffsets,
        ImageFrameOffsetBatchMode mode,
        bool updateX,
        bool updateY,
        int32_t xValue,
        int32_t yValue,
        std::vector<ImageFrameOffsetChange>* changes);

    // Applies cropTransparentEdges() to every frame in this file, storing the
    // cropped PNG and adjusted offset back into pics[] and marking the data as
    // normalized so saveAsIMP emits the cropped frames with explicit offsets.
    void cropTransparentEdgesAllFrames();

private:
    bool useMpcPaletteAlpha = false;
    bool preserveAsfPixelAlpha = true;

    bool loadMPC(const uint8_t* data, int bufferLen);
    bool loadSHD(const uint8_t* data, int bufferLen);
    bool loadASF(const uint8_t* data, int bufferLen);
    bool loadPIC(const uint8_t* data, int bufferLen);
    bool loadIMP(const uint8_t* data, int bufferLen);
    bool loadIMG(const uint8_t* data, int bufferLen);

    QImage decodeMPCFrameFromData(const MPCPic& pic, const MPCPalette& palette) const;
    QImage decodeSHDFrameFromData(const MPCPic& pic) const;
    QImage decodeASFFrameFromData(const MPCPic& pic, const ASFFileHead& asfHead, const MPCPalette& palette) const;

    bool saveMPC(const PicFileData* picFile, const std::string& fileName) const;
    bool saveSHD(const PicFileData* picFile, const std::string& fileName) const;
    bool saveASF(const PicFileData* picFile, const std::string& fileName) const;
    bool savePIC(const PicFileData* picFile, const std::string& fileName) const;

    PicFileData picFileData;
};
