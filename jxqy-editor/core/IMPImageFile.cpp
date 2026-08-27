#include "IMPImageFile.h"
#include "AuthoringMutationGate.h"
#include "PicFileEditor.h"
#include "Util.h"

#include <fstream>
#include <cstring>
#include <cstdio>
#include <limits>
#include <QBuffer>
#include <QPainter>
#include <QSaveFile>

static const std::string IMG_HEAD_STRING = "IMG File Ver1.0";

IMPImageFile::IMPImageFile()
{
}

IMPImageFile::~IMPImageFile()
{
    clear();
}

void IMPImageFile::clear()
{
    imageData.frame.clear();
    imageData.frameCount = 0;
    imageData.directions = 0;
    imageData.interval = 0;
}

bool IMPImageFile::load(const std::string& fileName)
{
    clear();

    std::vector<uint8_t> buffer = Util::readFileToBuffer(fileName);
    if (buffer.size() < 48)
        return false;

    const uint8_t* ptr = buffer.data();
    const uint8_t* endPtr = buffer.data() + buffer.size();

    std::memcpy(imageData.head, ptr, IMG_HEAD_LEN);
    ptr += IMG_HEAD_LEN;

    for (int i = 0; i < IMG_HEAD_LEN; i++)
    {
        if (imageData.head[i] != IMG_HEAD_STRING[i])
            return false;
    }

    if (ptr + 4 * 3 + IMAGE_NULL_LEN * 4 > endPtr)
        return false;

    std::memcpy(&imageData.frameCount, ptr, 4); ptr += 4;
    std::memcpy(&imageData.directions, ptr, 4); ptr += 4;
    std::memcpy(&imageData.interval, ptr, 4); ptr += 4;
    std::memcpy(imageData.imageNull, ptr, IMAGE_NULL_LEN * 4); ptr += IMAGE_NULL_LEN * 4;

    if (imageData.frameCount <= 0 || imageData.frameCount > 10000)
        return false;

    imageData.frame.resize(imageData.frameCount);

    for (int i = 0; i < imageData.frameCount; i++)
    {
        if (ptr + 4 * 3 + FRAME_NULL_LEN * 4 > endPtr)
            return false;

        std::memcpy(&imageData.frame[i].dataLen, ptr, 4); ptr += 4;
        std::memcpy(&imageData.frame[i].xOffset, ptr, 4); ptr += 4;
        std::memcpy(&imageData.frame[i].yOffset, ptr, 4); ptr += 4;
        std::memcpy(imageData.frame[i].frameNull, ptr, FRAME_NULL_LEN * 4); ptr += FRAME_NULL_LEN * 4;

        if (imageData.frame[i].dataLen < 0)
            return false;
        if (imageData.frame[i].dataLen > 0)
        {
            const size_t remaining = static_cast<size_t>(endPtr - ptr);
            if (static_cast<size_t>(imageData.frame[i].dataLen) > remaining)
                return false;
            imageData.frame[i].data.resize(imageData.frame[i].dataLen);
            std::memcpy(imageData.frame[i].data.data(), ptr, imageData.frame[i].dataLen);
            ptr += imageData.frame[i].dataLen;
        }
    }

    return true;
}

bool IMPImageFile::save(const std::string& fileName)
{
    auto mutationLease =
        AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(QString::fromUtf8(
                fileName.data(), static_cast<qsizetype>(fileName.size())));
    if (!mutationLease)
        return false;

    if (imageData.frame.empty() || imageData.frame.size() > 10000)
        return false;

    for (const IMPFrameData& frame : imageData.frame)
    {
        if (frame.data.size() > static_cast<size_t>((std::numeric_limits<int32_t>::max)()))
            return false;
    }

    QSaveFile file(QString::fromUtf8(fileName.data(), static_cast<qsizetype>(fileName.size())));
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    auto write = [&file](const void* data, qint64 size) {
        return size == 0 || file.write(static_cast<const char*>(data), size) == size;
    };

    std::memcpy(imageData.head, IMG_HEAD_STRING.c_str(), IMG_HEAD_LEN);
    const int32_t frameCount = static_cast<int32_t>(imageData.frame.size());

    if (!write(imageData.head, IMG_HEAD_LEN) ||
        !write(&frameCount, sizeof(frameCount)) ||
        !write(&imageData.directions, sizeof(imageData.directions)) ||
        !write(&imageData.interval, sizeof(imageData.interval)) ||
        !write(imageData.imageNull, IMAGE_NULL_LEN * 4))
    {
        file.cancelWriting();
        return false;
    }

    for (const IMPFrameData& frame : imageData.frame)
    {
        int32_t dataLen = static_cast<int32_t>(frame.data.size());
        if (!write(&dataLen, sizeof(dataLen)) ||
            !write(&frame.xOffset, sizeof(frame.xOffset)) ||
            !write(&frame.yOffset, sizeof(frame.yOffset)) ||
            !write(frame.frameNull, FRAME_NULL_LEN * 4) ||
            !write(frame.data.data(), dataLen))
        {
            file.cancelWriting();
            return false;
        }
    }

    if (!file.commit())
        return false;

    imageData.frameCount = frameCount;
    return true;
}

int IMPImageFile::getImageCount() const
{
    return imageData.frameCount;
}

int IMPImageFile::getInterval() const
{
    return imageData.interval;
}

void IMPImageFile::setInterval(int interval)
{
    imageData.interval = interval;
}

int IMPImageFile::getDirection() const
{
    return imageData.directions;
}

void IMPImageFile::setDirection(int direction)
{
    imageData.directions = direction;
}

QImage IMPImageFile::getFrameImage(int index) const
{
    if (index < 0 || index >= imageData.frameCount)
        return QImage();

    if (imageData.frame[index].dataLen <= 0 || imageData.frame[index].data.empty())
        return QImage();

    QImage image;
    if (image.loadFromData(imageData.frame[index].data.data(), imageData.frame[index].dataLen))
    {
        return image.convertToFormat(QImage::Format_ARGB32);
    }

    return QImage();
}

void IMPImageFile::drawFrame(QImage& dest, int index, int x, int y) const
{
    QImage frameImage = getFrameImage(index);
    if (frameImage.isNull())
        return;

    QPainter painter(&dest);
    painter.drawImage(x, y, frameImage);
}

void IMPImageFile::drawFrameWithOffset(QImage& dest, int index, int x, int y) const
{
    if (index < 0 || index >= imageData.frameCount)
        return;

    x -= imageData.frame[index].xOffset;
    y -= imageData.frame[index].yOffset;
    drawFrame(dest, index, x, y);
}

bool IMPImageFile::getFrameData(int index, const uint8_t** data, int* len) const
{
    if (data == nullptr || len == nullptr)
        return false;

    if (index < 0 || index >= imageData.frameCount)
        return false;

    *len = imageData.frame[index].dataLen;
    if (*len > 0 && !imageData.frame[index].data.empty())
        *data = imageData.frame[index].data.data();
    else
        *data = nullptr;

    return true;
}

void IMPImageFile::getFrameOffset(int index, int* xOffset, int* yOffset) const
{
    if (index < 0 || index >= imageData.frameCount)
        return;

    if (xOffset)
        *xOffset = imageData.frame[index].xOffset;
    if (yOffset)
        *yOffset = imageData.frame[index].yOffset;
}

bool IMPImageFile::getFrameSequence(
    std::vector<ImageFrameData>* frames) const
{
    if (frames == nullptr)
        return false;
    frames->clear();
    if (imageData.frame.empty() ||
        imageData.frame.size() > MaximumImageFrameCount ||
        imageData.frameCount != static_cast<int>(imageData.frame.size()))
    {
        return false;
    }

    std::vector<ImageFrameData> capturedFrames;
    capturedFrames.reserve(imageData.frame.size());
    for (const IMPFrameData& source : imageData.frame)
    {
        if (source.dataLen <= 0 ||
            source.dataLen != static_cast<int>(source.data.size()))
        {
            return false;
        }

        ImageFrameData frame;
        frame.encodedImage = QByteArray(
            reinterpret_cast<const char*>(source.data.data()),
            static_cast<qsizetype>(source.data.size()));
        frame.decodedImage.loadFromData(frame.encodedImage);
        if (frame.decodedImage.isNull())
            return false;
        frame.decodedImage =
            frame.decodedImage.convertToFormat(QImage::Format_ARGB32);
        frame.xOffset = source.xOffset;
        frame.yOffset = source.yOffset;
        frame.reserved = source.frameNull[0];
        capturedFrames.push_back(std::move(frame));
    }
    *frames = std::move(capturedFrames);
    return true;
}

bool IMPImageFile::setFrameSequence(
    const std::vector<ImageFrameData>& frames)
{
    if (frames.empty() || frames.size() > MaximumImageFrameCount)
        return false;

    std::vector<IMPFrameData> replacementFrames;
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

        IMPFrameData frame;
        frame.dataLen = static_cast<int>(source.encodedImage.size());
        frame.xOffset = source.xOffset;
        frame.yOffset = source.yOffset;
        frame.frameNull[0] = source.reserved;
        frame.data.assign(
            reinterpret_cast<const uint8_t*>(source.encodedImage.constData()),
            reinterpret_cast<const uint8_t*>(source.encodedImage.constData()) +
                source.encodedImage.size());
        replacementFrames.push_back(std::move(frame));
    }

    imageData.frame = std::move(replacementFrames);
    imageData.frameCount = static_cast<int>(imageData.frame.size());
    return true;
}

void IMPImageFile::setFrame(int index, const uint8_t* data, int len, int xOffset, int yOffset)
{
    setFrameData(index, data, len);
    setFrameOffset(index, xOffset, yOffset);
}

void IMPImageFile::setFrameData(int index, const uint8_t* data, int len)
{
    if (index < 0 || index >= imageData.frameCount)
        return;

    if (len < 0 || (len > 0 && data == nullptr))
        len = 0;

    imageData.frame[index].dataLen = len;
    imageData.frame[index].data.resize(len);
    if (len > 0 && data != nullptr)
    {
        std::memcpy(imageData.frame[index].data.data(), data, len);
    }
}

void IMPImageFile::setFrameOffset(int index, int xOffset, int yOffset)
{
    if (index < 0 || index >= imageData.frameCount)
        return;

    imageData.frame[index].xOffset = xOffset;
    imageData.frame[index].yOffset = yOffset;
}

void IMPImageFile::copyFrame(int dest, int source)
{
    if (dest == source)
        return;
    if (dest < 0 || dest >= imageData.frameCount)
        return;
    if (source < 0 || source >= imageData.frameCount)
        return;

    if (imageData.frame[source].dataLen > 0 && !imageData.frame[source].data.empty())
    {
        setFrame(dest, imageData.frame[source].data.data(), imageData.frame[source].dataLen,
                 imageData.frame[source].xOffset, imageData.frame[source].yOffset);
    }
    else
    {
        setFrame(dest, nullptr, 0, imageData.frame[source].xOffset, imageData.frame[source].yOffset);
    }
}

void IMPImageFile::insertFrame(int index, const uint8_t* data, int len, int xOffset, int yOffset)
{
    if (index < 0)
        index = 0;
    if (index > imageData.frameCount)
        index = imageData.frameCount;
    if (len < 0 || (len > 0 && data == nullptr))
        len = 0;

    IMPFrameData newFrame;
    newFrame.dataLen = len;
    newFrame.xOffset = xOffset;
    newFrame.yOffset = yOffset;
    if (len > 0 && data != nullptr)
    {
        newFrame.data.resize(len);
        std::memcpy(newFrame.data.data(), data, len);
    }

    imageData.frame.insert(imageData.frame.begin() + index, std::move(newFrame));
    imageData.frameCount++;
}

void IMPImageFile::addFrame(const uint8_t* data, int len, int xOffset, int yOffset)
{
    insertFrame(imageData.frameCount, data, len, xOffset, yOffset);
}

void IMPImageFile::deleteFrame(int index)
{
    if (index < 0 || index >= imageData.frameCount)
        return;

    imageData.frame.erase(imageData.frame.begin() + index);
    imageData.frameCount--;
}

bool IMPImageFile::saveFrame(int index, const std::string& fileName) const
{
    if (index < 0 || index >= imageData.frameCount)
        return false;

    int len = imageData.frame[index].dataLen;
    if (len <= 0 || imageData.frame[index].data.empty())
        return false;

    return Util::writeFileFromBuffer(fileName, imageData.frame[index].data.data(), len);
}

bool IMPImageFile::importFromPicFile(PicFileEditor& picEditor)
{
    PicFileData* picFile = picEditor.getPicFileData();
    if (picFile == nullptr || picFile->pics.empty())
        return false;

    clear();

    std::memcpy(imageData.head, IMG_HEAD_STRING.c_str(), IMG_HEAD_LEN);
    imageData.frameCount = static_cast<int>(picFile->pics.size());

    if (picFile->picType == PicType::Mpc || picFile->picType == PicType::Shd)
    {
        imageData.directions = picFile->mpcFileHead.directions;
        imageData.interval = picFile->mpcFileHead.interval;
    }
    else if (picFile->picType == PicType::Asf100 || picFile->picType == PicType::Asf101)
    {
        imageData.directions = picFile->asfFileHead.directions;
        imageData.interval = picFile->asfFileHead.interval;
    }

    imageData.frame.resize(imageData.frameCount);

    for (int i = 0; i < imageData.frameCount; i++)
    {
        QImage frameImage = picEditor.getFrameImage(i);

        int32_t xOffset = 0;
        int32_t yOffset = 0;
        picEditor.getFrameOffset(i, &xOffset, &yOffset);

        imageData.frame[i].xOffset = xOffset;
        imageData.frame[i].yOffset = yOffset;

        if (!frameImage.isNull())
        {
            QByteArray byteArray;
            QBuffer buffer(&byteArray);
            if (!buffer.open(QIODevice::WriteOnly) || !frameImage.save(&buffer, "PNG"))
            {
                clear();
                return false;
            }

            imageData.frame[i].dataLen = static_cast<int>(byteArray.size());
            imageData.frame[i].data.resize(byteArray.size());
            std::memcpy(imageData.frame[i].data.data(), byteArray.constData(), byteArray.size());
        }
        else
        {
            imageData.frame[i].dataLen = 0;
        }
    }

    return true;
}

bool IMPImageFile::importFromPicFile(const std::string& fileName)
{
    PicFileEditor picEditor;
    if (!picEditor.loadFromFile(fileName))
        return false;

    return importFromPicFile(picEditor);
}
