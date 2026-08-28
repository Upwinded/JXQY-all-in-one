#include "TestSupport.h"

#include "../core/MapFileEditor.h"
#include "../core/PicFileEditor.h"
#include "../core/Util.h"

#include <QFile>

#include <algorithm>
#include <cstring>
#include <iostream>

namespace TestSupport
{
bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAILED: " << message << '\n';
    return condition;
}

bool check(bool condition, const QString& message)
{
    if (!condition)
        std::cerr << "FAILED: " << message.toUtf8().constData() << '\n';
    return condition;
}

bool writeUtf8TextFile(const QString& filePath, const QString& content)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    const QByteArray data = content.toUtf8();
    bool ok = file.write(data) == data.size();
    file.close();
    return ok;
}

bool writeRawFile(const QString& filePath, const QByteArray& content)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    bool ok = file.write(content) == content.size();
    file.close();
    return ok;
}

QByteArray readRawFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QString readUtf8TextFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    const QString text = QString::fromUtf8(file.readAll());
    file.close();
    return text;
}

MapTileData makeMarkerTile(uint8_t marker)
{
    MapTileData tile;
    tile.layer[0].mpc = marker;
    tile.layer[0].frame = marker + 10;
    tile.layer[1].mpc = marker + 20;
    tile.layer[1].frame = marker + 30;
    tile.obstacle = marker + 40;
    tile.trap = marker + 50;
    return tile;
}

std::vector<uint8_t> makeSinglePixelMpc(const QColor& color)
{
    MPCFileHead head = {};
    std::memcpy(head.head, "MPC File Ver2.0", 16);
    head.picCount = 1;
    head.paletteLen = 256;

    std::vector<uint8_t> buffer;
    appendValue(buffer, head);

    std::vector<ColorARGB> palette(256);
    palette[1] = {
        static_cast<uint8_t>(color.blue()),
        static_cast<uint8_t>(color.green()),
        static_cast<uint8_t>(color.red()),
        static_cast<uint8_t>(color.alpha())
    };
    const uint8_t* paletteBytes = reinterpret_cast<const uint8_t*>(palette.data());
    buffer.insert(buffer.end(), paletteBytes, paletteBytes + palette.size() * sizeof(ColorARGB));

    int32_t frameOffset = 0;
    appendValue(buffer, frameOffset);

    MPCPicHead picHead = {};
    picHead.dataLen = static_cast<int32_t>(sizeof(MPCPicHead) + 2);
    picHead.width = 1;
    picHead.height = 1;
    appendValue(buffer, picHead);
    buffer.push_back(1);
    buffer.push_back(1);
    return buffer;
}

std::vector<uint8_t> makeEmptyMapBuffer(int width, int height)
{
    MapEditorHead head = {};
    std::memcpy(head.head, MAP_EDITOR_HEADSTR_V2, MAP_EDITOR_HEADSTR_LEN);
    head.width = width;
    head.height = height;
    head.infoLen = 0x40;
    head.nameLen = 0x20;
    head.dataLen = width * height * 10;

    std::vector<uint8_t> buffer(
        MAP_EDITOR_HEAD_LEN + MAP_EDITOR_MPC_COUNT * 0x40 + width * height * 10, 0);
    std::memcpy(buffer.data(), &head, sizeof(head));

    size_t tileOffset = MAP_EDITOR_HEAD_LEN + MAP_EDITOR_MPC_COUNT * 0x40;
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            size_t offset = tileOffset + (y * width + x) * 10;
            buffer[offset + 8] = 0x00;
            buffer[offset + 9] = 0x1F;
        }
    }
    return buffer;
}

std::vector<uint8_t> buildMpcFileFromImages(const std::vector<QImage>& frames,
    int directions, int interval, int yMove)
{
    MPCPalette palette;
    palette.length = 256;
    palette.colors.resize(256);
    palette.colors[0] = {0, 0, 0, 0};
    palette.colors[1] = {0, 0, 255, 255};

    MPCFileHead head = {};
    std::memcpy(head.head, "MPC File Ver2.0", 16);
    head.picCount = static_cast<int32_t>(frames.size());
    head.directions = directions;
    head.interval = interval;
    head.yMove = yMove;
    head.paletteLen = 256;
    head.maxWidth = 0;
    head.maxHeight = 0;
    for (const QImage& frame : frames)
    {
        head.maxWidth = std::max(head.maxWidth, frame.width());
        head.maxHeight = std::max(head.maxHeight, frame.height());
    }

    std::vector<uint8_t> buffer;
    appendValue(buffer, head);
    const uint8_t* paletteBytes = reinterpret_cast<const uint8_t*>(palette.colors.data());
    buffer.insert(buffer.end(), paletteBytes,
        paletteBytes + palette.colors.size() * sizeof(ColorARGB));

    const size_t tableOffset = buffer.size();
    const size_t tableBytes = frames.size() * sizeof(int32_t);
    const size_t dataStart = tableOffset + tableBytes;
    buffer.insert(buffer.end(), tableBytes, 0);

    for (size_t i = 0; i < frames.size(); i++)
    {
        int32_t relativeOffset = static_cast<int32_t>(buffer.size() - dataStart);
        std::memcpy(buffer.data() + tableOffset + i * sizeof(int32_t),
            &relativeOffset, sizeof(int32_t));

        MPCPicHead picHead = {};
        picHead.width = frames[i].width();
        picHead.height = frames[i].height();
        std::vector<uint8_t> frameData = PicFileEditor::encodeMPCFrame(frames[i], palette);
        picHead.dataLen = static_cast<int32_t>(sizeof(MPCPicHead) + frameData.size());
        appendValue(buffer, picHead);
        buffer.insert(buffer.end(), frameData.begin(), frameData.end());
    }
    return buffer;
}

bool writeMpcFileFromImages(const QString& path, const std::vector<QImage>& frames, int yMove)
{
    std::vector<uint8_t> mpc = buildMpcFileFromImages(frames, 2, 30, yMove);
    return Util::writeFileFromBuffer(path.toUtf8().toStdString(), mpc.data(), mpc.size());
}
}
