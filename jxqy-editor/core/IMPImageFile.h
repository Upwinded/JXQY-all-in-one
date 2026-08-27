#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <QImage>

#include "ImageFrameSequence.h"

class PicFileEditor;

static const int IMG_HEAD_LEN = 16;
static const int IMAGE_NULL_LEN = 5;
static const int FRAME_NULL_LEN = 1;

struct IMPFrameData
{
    int dataLen = 0;
    int xOffset = 0;
    int yOffset = 0;
    int frameNull[FRAME_NULL_LEN] = {0};
    std::vector<uint8_t> data;
};

struct IMPImageData
{
    char head[IMG_HEAD_LEN] = {0};
    int frameCount = 0;
    int directions = 1;
    int interval = 30;
    int imageNull[IMAGE_NULL_LEN] = {0};
    std::vector<IMPFrameData> frame;
};

class IMPImageFile
{
public:
    IMPImageFile();
    ~IMPImageFile();
    IMPImageFile(const IMPImageFile&) = delete;
    IMPImageFile& operator=(const IMPImageFile&) = delete;
    IMPImageFile(IMPImageFile&&) noexcept = default;
    IMPImageFile& operator=(IMPImageFile&&) noexcept = default;

    void clear();
    bool load(const std::string& fileName);
    bool save(const std::string& fileName);

    int getImageCount() const;
    int getInterval() const;
    void setInterval(int interval);
    int getDirection() const;
    void setDirection(int direction);

    QImage getFrameImage(int index) const;
    void drawFrame(QImage& dest, int index, int x, int y) const;
    void drawFrameWithOffset(QImage& dest, int index, int x, int y) const;

    bool getFrameData(int index, const uint8_t** data, int* len) const;
    void getFrameOffset(int index, int* xOffset, int* yOffset) const;
    bool getFrameSequence(std::vector<ImageFrameData>* frames) const;
    bool setFrameSequence(const std::vector<ImageFrameData>& frames);

    void setFrame(int index, const uint8_t* data, int len, int xOffset, int yOffset);
    void setFrameData(int index, const uint8_t* data, int len);
    void setFrameOffset(int index, int xOffset, int yOffset);

    void copyFrame(int dest, int source);
    void insertFrame(int index, const uint8_t* data, int len, int xOffset, int yOffset);
    void addFrame(const uint8_t* data, int len, int xOffset, int yOffset);
    void deleteFrame(int index);
    bool saveFrame(int index, const std::string& fileName) const;

    bool importFromPicFile(PicFileEditor& picEditor);
    bool importFromPicFile(const std::string& fileName);

private:
    IMPImageData imageData;
};
