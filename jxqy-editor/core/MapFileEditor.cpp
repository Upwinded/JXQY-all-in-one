#include "MapFileEditor.h"
#include "Util.h"

#include <cstring>
#include <algorithm>
#include <limits>

static const char MAP_HEADSTR_V2[MAP_EDITOR_HEADSTR_LEN] = MAP_EDITOR_HEADSTR_V2;
static const char MAP_HEADSTR_V3[MAP_EDITOR_HEADSTR_LEN] = MAP_EDITOR_HEADSTR_V3;

namespace
{
bool hasMapHeader(const uint8_t* data, const char* header)
{
    return std::memcmp(data, header, MAP_EDITOR_HEADSTR_LEN) == 0;
}

int32_t readInt32(const char* data)
{
    int32_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

void writeInt32(char* data, int32_t value)
{
    std::memcpy(data, &value, sizeof(value));
}

size_t fixedStringLength(const uint8_t* data, size_t capacity)
{
    size_t length = 0;
    while (length < capacity && data[length] != 0)
        length++;
    return length;
}

std::string convertFixedStringToUtf8(const std::string& text, bool forceGbk = false)
{
    if (text.empty())
        return text;

    if (forceGbk)
    {
        std::string converted = Util::gbkToUtf8(text);
        if (!converted.empty() &&
            Util::isUtf8(reinterpret_cast<const uint8_t*>(converted.data()), converted.size()))
        {
            return converted;
        }
        return text;
    }

    const uint8_t* data = reinterpret_cast<const uint8_t*>(text.data());
    if (Util::isUtf8Bom(data, text.size()))
        return text.substr(3);
    if (Util::isUtf8(data, text.size()))
        return text;

    std::string converted = Util::gbkToUtf8(text);
    if (!converted.empty() &&
        Util::isUtf8(reinterpret_cast<const uint8_t*>(converted.data()), converted.size()))
    {
        return converted;
    }

    return std::string();
}

std::string convertFixedStringToUtf8OrOriginal(const std::string& text, bool forceGbk = false)
{
    std::string converted = convertFixedStringToUtf8(text, forceGbk);
    if (converted.empty() && !text.empty())
        return text;
    return converted;
}

std::string readFixedStringToUtf8(const uint8_t* data, size_t capacity, bool forceGbk = false)
{
    size_t length = fixedStringLength(data, capacity);
    std::string raw(reinterpret_cast<const char*>(data), length);
    return convertFixedStringToUtf8OrOriginal(raw, forceGbk);
}

size_t utf8SafePrefixLength(const std::string& text, size_t maxBytes)
{
    if (text.size() <= maxBytes)
        return text.size();

    size_t length = maxBytes;
    while (length > 0 &&
           (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80)
    {
        length--;
    }
    return length;
}

void writeFixedUtf8String(char* destination, size_t capacity, const std::string& text)
{
    if (capacity == 0)
        return;

    std::memset(destination, 0, capacity);
    std::string utf8Text = convertFixedStringToUtf8(text);
    size_t copyLength = utf8SafePrefixLength(utf8Text, capacity - 1);
    if (copyLength > 0)
        std::memcpy(destination, utf8Text.data(), copyLength);
}

size_t alignTo16(size_t value)
{
    return (value + 15) & ~static_cast<size_t>(15);
}

size_t chooseV3PathLength(const std::string& path)
{
    size_t needed = path.size() + 1;
    return alignTo16(std::max(static_cast<size_t>(MAP_EDITOR_V3_PATH_LEN), needed));
}

size_t chooseV3NameLength(const MapDataFull& data)
{
    size_t needed = MAP_EDITOR_V3_NAME_LEN;
    for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
    {
        std::string name = convertFixedStringToUtf8OrOriginal(data.mpc[i].name);
        needed = std::max(needed, name.size() + 1);
    }
    return alignTo16(needed);
}

bool legacyMapUsesGbkStrings(const uint8_t* data, size_t length, int32_t infoLen, int32_t nameLen)
{
    auto looksLikeMisdecodedGbk = [](const uint8_t* textData, size_t textLength) -> bool {
        if (textLength == 0 || !Util::isUtf8(textData, textLength))
            return false;
        for (size_t i = 0; i < textLength; i++)
        {
            uint8_t value = textData[i];
            if (value < 0x80)
                continue;
            // Legacy Chinese GBK bytes can accidentally form valid 2-byte UTF-8
            // such as U+027D/U+056F. Resource paths here are ASCII plus CJK,
            // so these 2-byte code points are a strong GBK signal.
            if (value < 0xE0)
                return true;
            if (value < 0xF0)
            {
                i += 2;
                continue;
            }
            return true;
        }
        return false;
    };

    auto hasInvalidUtf8 = [](const uint8_t* textData, size_t capacity) -> bool {
        size_t textLength = fixedStringLength(textData, capacity);
        return textLength > 0 && !Util::isUtf8(textData, textLength);
    };
    auto hasLegacyGbkSignal = [&](const uint8_t* textData, size_t capacity) -> bool {
        size_t textLength = fixedStringLength(textData, capacity);
        return hasInvalidUtf8(textData, capacity) || looksLikeMisdecodedGbk(textData, textLength);
    };

    if (length >= MAP_EDITOR_HEADSTR_LEN + MAP_EDITOR_NULL_LEN + MAP_EDITOR_PATH_LEN &&
        hasLegacyGbkSignal(data + MAP_EDITOR_HEADSTR_LEN + MAP_EDITOR_NULL_LEN, MAP_EDITOR_PATH_LEN))
    {
        return true;
    }

    if (infoLen <= 0 || nameLen <= 0 ||
        static_cast<int64_t>(infoLen) < static_cast<int64_t>(nameLen) + 16)
        return false;

    size_t mpcInfoOffset = MAP_EDITOR_HEAD_LEN;
    for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
    {
        size_t nameOffset = mpcInfoOffset + static_cast<size_t>(i) * static_cast<size_t>(infoLen);
        if (nameOffset > length ||
            static_cast<size_t>(nameLen) > length - nameOffset)
            break;
        if (hasLegacyGbkSignal(data + nameOffset, static_cast<size_t>(nameLen)))
            return true;
    }

    return false;
}

std::string normalizeRelativeResourcePath(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');

    // Original MAP files commonly store a single leading backslash even
    // though the value is relative to the assets root.  Strip all leading
    // separators at the resource-resolution boundary, but never allow parent
    // traversal or drive-qualified segments to escape that root.
    while (!path.empty() && path.front() == '/')
        path.erase(path.begin());

    std::string normalized;
    size_t position = 0;
    while (position <= path.size())
    {
        size_t separator = path.find('/', position);
        std::string segment = path.substr(
            position,
            separator == std::string::npos ? std::string::npos : separator - position);
        if (!segment.empty() && segment != ".")
        {
            if (segment == ".." || segment.find(':') != std::string::npos)
                return std::string();
            if (!normalized.empty())
                normalized += '/';
            normalized += segment;
        }
        if (separator == std::string::npos)
            break;
        position = separator + 1;
    }
    for (char& character : normalized)
    {
        if (character >= 'A' && character <= 'Z')
        {
            character = static_cast<char>(
                character + ('a' - 'A'));
        }
    }
    return normalized;
}
}

MapFileEditor::MapFileEditor()
{
}

MapFileEditor::~MapFileEditor()
{
}

bool MapFileEditor::isMapFile(const std::string& fileName)
{
    std::vector<uint8_t> buffer = Util::readFileToBuffer(fileName);
    if (buffer.size() < MAP_EDITOR_HEADSTR_LEN)
        return false;
    return isMapData(buffer.data(), buffer.size());
}

bool MapFileEditor::isMapData(const uint8_t* data, size_t length)
{
    if (!data || length < MAP_EDITOR_HEADSTR_LEN)
        return false;
    return hasMapHeader(data, MAP_HEADSTR_V2) || hasMapHeader(data, MAP_HEADSTR_V3);
}

bool MapFileEditor::loadFromFile(const std::string& fileName)
{
    std::vector<uint8_t> buffer = Util::readFileToBuffer(fileName);
    if (buffer.empty())
    {
        lastError = "Cannot read file: " + fileName;
        return false;
    }
    if (!loadFromBuffer(buffer.data(), buffer.size()))
        return false;
    mapFileName = fileName;
    return true;
}

bool MapFileEditor::loadFromBuffer(
    const uint8_t* data, size_t length, bool forceLegacyGbkStrings)
{
    // Parse into a temporary editor so a failed open never destroys a valid,
    // possibly modified map already displayed by the caller.
    MapFileEditor parsed;
    if (!parsed.parseMapData(data, length, forceLegacyGbkStrings))
    {
        lastError = parsed.lastError;
        return false;
    }
    mapData = std::move(parsed.mapData);
    mapFileName.clear();
    loaded = true;
    lastError.clear();
    return true;
}

bool MapFileEditor::parseMapData(
    const uint8_t* data, size_t length, bool forceLegacyGbkStrings)
{
    if (!data || length < MAP_EDITOR_HEAD_LEN)
    {
        lastError = "File too small to be a valid map file";
        return false;
    }

    bool isVersion2 = hasMapHeader(data, MAP_HEADSTR_V2);
    bool isVersion3 = hasMapHeader(data, MAP_HEADSTR_V3);
    if (!isVersion2 && !isVersion3)
    {
        lastError = "Invalid map file header";
        return false;
    }

    memcpy(&mapData.head, data, sizeof(MapEditorHead));

    size_t mpcInfoOffset = MAP_EDITOR_HEAD_LEN;
    int32_t mpcCount = MAP_EDITOR_MPC_COUNT;
    bool forceLegacyGbk = false;

    if (isVersion2)
    {
        if (mapData.head.nameLen <= 0 ||
            static_cast<int64_t>(mapData.head.infoLen) <
                static_cast<int64_t>(mapData.head.nameLen) + 16)
        {
            mapData.head.infoLen = MAP_EDITOR_V2_INFO_LEN;
            mapData.head.nameLen = MAP_EDITOR_V2_NAME_LEN;
        }
        forceLegacyGbk = forceLegacyGbkStrings || legacyMapUsesGbkStrings(
            data, length, mapData.head.infoLen, mapData.head.nameLen);
        mapData.mpcPath = readFixedStringToUtf8(
            reinterpret_cast<const uint8_t*>(mapData.head.path), MAP_EDITOR_PATH_LEN, forceLegacyGbk);
    }
    else
    {
        int32_t headerSize = readInt32(mapData.head.dataNil2);
        int32_t pathLen = readInt32(mapData.head.dataNil2 + 4);
        int32_t flags = readInt32(mapData.head.dataNil2 + 8);
        mpcCount = readInt32(mapData.head.dataNil2 + 12);
        if (headerSize < MAP_EDITOR_HEAD_LEN || pathLen < 0 ||
            static_cast<size_t>(pathLen) !=
                static_cast<size_t>(headerSize) - MAP_EDITOR_HEAD_LEN ||
            mpcCount != MAP_EDITOR_MPC_COUNT ||
            (flags & MAP_EDITOR_V3_FLAG_UTF8) == 0 ||
            static_cast<size_t>(headerSize) > length)
        {
            lastError = "Invalid MAP 3.0 header parameters";
            return false;
        }
        mpcInfoOffset = static_cast<size_t>(headerSize);
        if (pathLen > 0)
        {
            mapData.mpcPath = readFixedStringToUtf8(
                data + MAP_EDITOR_HEAD_LEN, static_cast<size_t>(pathLen));
        }
        else
        {
            mapData.mpcPath = readFixedStringToUtf8(
                reinterpret_cast<const uint8_t*>(mapData.head.path), MAP_EDITOR_PATH_LEN);
        }
    }

    if (mapData.head.width < 0 || mapData.head.height < 0)
    {
        lastError = "Invalid negative map dimensions";
        return false;
    }

    size_t w = static_cast<size_t>(mapData.head.width);
    size_t h = static_cast<size_t>(mapData.head.height);
    if ((w != 0 && h > (std::numeric_limits<size_t>::max)() / w) ||
        w * h > (std::numeric_limits<size_t>::max)() / 10)
    {
        lastError = "Map dimensions overflow";
        return false;
    }
    size_t tileSize = w * h * 10;

    if (mapData.head.nameLen <= 0 ||
        static_cast<int64_t>(mapData.head.infoLen) <
            static_cast<int64_t>(mapData.head.nameLen) + 16 ||
        mpcInfoOffset > length)
    {
        lastError = "Invalid map header parameters";
        return false;
    }

    size_t infoLen = static_cast<size_t>(mapData.head.infoLen);
    size_t nameLen = static_cast<size_t>(mapData.head.nameLen);
    size_t mpcInfoSize = static_cast<size_t>(mpcCount) * infoLen;
    if (mpcCount <= 0 ||
        infoLen > length ||
        mpcInfoSize / infoLen != static_cast<size_t>(mpcCount) ||
        mpcInfoSize > length - mpcInfoOffset ||
        tileSize > length - mpcInfoOffset - mpcInfoSize)
    {
        lastError = "Map file truncated at data sections";
        return false;
    }

    for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
    {
        size_t nameOffset = mpcInfoOffset + static_cast<size_t>(i) * infoLen;

        if (nameOffset > length || nameLen > length - nameOffset)
        {
            lastError = "Map file truncated at MPC info section";
            return false;
        }

        mapData.mpc[i].name = readFixedStringToUtf8(data + nameOffset, nameLen, forceLegacyGbk);

        size_t fieldOffset = nameOffset + nameLen;
        if (fieldOffset <= length && 16 <= length - fieldOffset)
        {
            memcpy(&mapData.mpc[i].index, data + fieldOffset, 4);
            memcpy(&mapData.mpc[i].dynamic, data + fieldOffset + 4, 4);
            memcpy(&mapData.mpc[i].obstacle, data + fieldOffset + 8, 4);
            memcpy(&mapData.mpc[i].nil, data + fieldOffset + 12, 4);

            size_t opaqueLength = std::min(
                mapData.mpc[i].opaqueTail.size(),
                infoLen - nameLen - 16);
            if (opaqueLength > 0)
            {
                memcpy(mapData.mpc[i].opaqueTail.data(),
                       data + fieldOffset + 16,
                       opaqueLength);
            }
        }
    }

    size_t tileDataOffset = mpcInfoOffset + mpcInfoSize;
    size_t expectedTileSize = tileSize;

    if (tileDataOffset + expectedTileSize > length)
    {
        lastError = "Map file truncated at tile data section";
        return false;
    }

    mapData.tile.resize(mapData.head.height);
    for (int y = 0; y < mapData.head.height; y++)
    {
        mapData.tile[y].resize(mapData.head.width);
        for (int x = 0; x < mapData.head.width; x++)
        {
            size_t tileOffset = tileDataOffset + (y * mapData.head.width + x) * 10;
            const uint8_t* tilePtr = data + tileOffset;

            for (int layer = 0; layer < MAP_EDITOR_TILE_LAYER; layer++)
            {
                mapData.tile[y][x].layer[layer].frame = tilePtr[layer * 2];
                mapData.tile[y][x].layer[layer].mpc = tilePtr[layer * 2 + 1];
            }
            mapData.tile[y][x].obstacle = tilePtr[6];
            mapData.tile[y][x].trap = tilePtr[7];
            mapData.tile[y][x].end[0] = tilePtr[8];
            mapData.tile[y][x].end[1] = tilePtr[9];
        }
    }

    return true;
}

bool MapFileEditor::saveToFile(const std::string& fileName) const
{
    if (!loaded)
    {
        return false;
    }

    std::vector<uint8_t> buffer = saveToBuffer();
    if (buffer.empty())
        return false;

    return Util::writeFileFromBuffer(fileName, buffer.data(), buffer.size());
}

std::vector<uint8_t> MapFileEditor::saveToBuffer() const
{
    std::vector<uint8_t> buffer;

    if (!loaded)
        return buffer;

    MapEditorHead saveHead = mapData.head;
    std::string mpcPath = convertFixedStringToUtf8OrOriginal(getMpcPath());
    size_t pathLen = chooseV3PathLength(mpcPath);
    size_t nameLen = chooseV3NameLength(mapData);
    size_t infoLen = nameLen + MAP_EDITOR_V3_INFO_EXTRA_LEN;
    if (pathLen > static_cast<size_t>(std::numeric_limits<int32_t>::max()) ||
        nameLen > static_cast<size_t>(std::numeric_limits<int32_t>::max()) ||
        infoLen > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
    {
        return buffer;
    }

    std::memcpy(saveHead.head, MAP_HEADSTR_V3, MAP_EDITOR_HEADSTR_LEN);
    // The first 16 bytes are the Ver3.0 metadata fields.  Preserve the
    // remaining opaque header bytes carried by real Ver2.0/Ver3.0 maps.
    std::memset(saveHead.dataNil2, 0, 16);
    saveHead.infoLen = static_cast<int32_t>(infoLen);
    saveHead.nameLen = static_cast<int32_t>(nameLen);
    writeFixedUtf8String(saveHead.path, MAP_EDITOR_PATH_LEN, mpcPath);
    if (saveHead.width < 0 || saveHead.height < 0)
        return buffer;
    if (static_cast<int>(mapData.tile.size()) < saveHead.height)
        return buffer;
    for (int y = 0; y < saveHead.height; y++)
    {
        if (static_cast<int>(mapData.tile[y].size()) < saveHead.width)
            return buffer;
    }

    size_t tileSize = static_cast<size_t>(saveHead.width) * static_cast<size_t>(saveHead.height) * 10;
    if (static_cast<size_t>(saveHead.width) != 0 &&
        tileSize / static_cast<size_t>(saveHead.width) / 10 != static_cast<size_t>(saveHead.height))
        return buffer; // overflow
    if (tileSize > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
        return buffer; // overflow
    saveHead.dataLen = static_cast<int32_t>(tileSize);

    size_t headerSize = MAP_EDITOR_HEAD_LEN + pathLen;
    if (headerSize > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
        return buffer;
    writeInt32(saveHead.dataNil2, static_cast<int32_t>(headerSize));
    writeInt32(saveHead.dataNil2 + 4, static_cast<int32_t>(pathLen));
    writeInt32(saveHead.dataNil2 + 8, MAP_EDITOR_V3_FLAG_UTF8);
    writeInt32(saveHead.dataNil2 + 12, MAP_EDITOR_MPC_COUNT);

    size_t mpcInfoSize = static_cast<size_t>(MAP_EDITOR_MPC_COUNT) * infoLen;
    if (mpcInfoSize / infoLen != static_cast<size_t>(MAP_EDITOR_MPC_COUNT))
        return buffer;
    size_t totalSize = headerSize + mpcInfoSize + tileSize;
    if (totalSize < tileSize || totalSize < headerSize)
        return buffer; // overflow
    buffer.resize(totalSize, 0);

    memcpy(buffer.data(), &saveHead, sizeof(MapEditorHead));
    if (!mpcPath.empty())
    {
        std::memcpy(buffer.data() + MAP_EDITOR_HEAD_LEN, mpcPath.data(), mpcPath.size());
    }

    size_t mpcInfoOffset = headerSize;
    for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
    {
        size_t nameOffset = mpcInfoOffset + static_cast<size_t>(i) * infoLen;

        memset(buffer.data() + nameOffset, 0, infoLen);

        std::string name = convertFixedStringToUtf8OrOriginal(mapData.mpc[i].name);
        if (!name.empty())
        {
            memcpy(buffer.data() + nameOffset, name.data(), name.size());
        }

        size_t fieldOffset = nameOffset + nameLen;
        memcpy(buffer.data() + fieldOffset, &mapData.mpc[i].index, 4);
        memcpy(buffer.data() + fieldOffset + 4, &mapData.mpc[i].dynamic, 4);
        memcpy(buffer.data() + fieldOffset + 8, &mapData.mpc[i].obstacle, 4);
        memcpy(buffer.data() + fieldOffset + 12, &mapData.mpc[i].nil, 4);
        memcpy(buffer.data() + fieldOffset + 16,
               mapData.mpc[i].opaqueTail.data(),
               mapData.mpc[i].opaqueTail.size());
    }

    size_t tileDataOffset = mpcInfoOffset + mpcInfoSize;
    for (int y = 0; y < saveHead.height; y++)
    {
        for (int x = 0; x < saveHead.width; x++)
        {
            size_t tileOffset = tileDataOffset + (y * saveHead.width + x) * 10;
            uint8_t* tilePtr = buffer.data() + tileOffset;

            for (int layer = 0; layer < MAP_EDITOR_TILE_LAYER; layer++)
            {
                tilePtr[layer * 2] = mapData.tile[y][x].layer[layer].frame;
                tilePtr[layer * 2 + 1] = mapData.tile[y][x].layer[layer].mpc;
            }
            tilePtr[6] = mapData.tile[y][x].obstacle;
            tilePtr[7] = mapData.tile[y][x].trap;
            tilePtr[8] = mapData.tile[y][x].end[0];
            tilePtr[9] = mapData.tile[y][x].end[1];
        }
    }

    return buffer;
}

void MapFileEditor::clear()
{
    mapData = MapDataFull();
    mapFileName.clear();
    loaded = false;
    lastError.clear();
}

bool MapFileEditor::isLoaded() const
{
    return loaded;
}

int32_t MapFileEditor::getWidth() const
{
    return mapData.head.width;
}

int32_t MapFileEditor::getHeight() const
{
    return mapData.head.height;
}

bool MapFileEditor::resizeMap(int32_t newWidth, int32_t newHeight, const MapTileData& emptyFill)
{
    if (newWidth <= 0 || newHeight <= 0)
        return false;

    int32_t oldWidth = mapData.head.width;
    int32_t oldHeight = mapData.head.height;

    if (newWidth == oldWidth && newHeight == oldHeight)
        return true;

    // 创建新的 tile 二维数组
    std::vector<std::vector<MapTileData>> newTiles;
    newTiles.resize(newHeight);
    for (int32_t y = 0; y < newHeight; y++)
    {
        newTiles[y].resize(newWidth, emptyFill);
        for (int32_t x = 0; x < newWidth; x++)
        {
            if (x < oldWidth && y < oldHeight)
            {
                newTiles[y][x] = mapData.tile[y][x];
            }
        }
    }

    mapData.tile = std::move(newTiles);
    mapData.head.width = newWidth;
    mapData.head.height = newHeight;

    return true;
}

std::string MapFileEditor::getMpcPath() const
{
    if (!mapData.mpcPath.empty())
        return mapData.mpcPath;
    return readFixedStringToUtf8(
        reinterpret_cast<const uint8_t*>(mapData.head.path), MAP_EDITOR_PATH_LEN);
}

void MapFileEditor::setMpcPath(const std::string& path)
{
    mapData.mpcPath = convertFixedStringToUtf8OrOriginal(path);
    writeFixedUtf8String(mapData.head.path, MAP_EDITOR_PATH_LEN, mapData.mpcPath);
}

const MpcInfoData& MapFileEditor::getMpcInfo(int index) const
{
    if (index >= 0 && index < MAP_EDITOR_MPC_COUNT)
        return mapData.mpc[index];

    static MpcInfoData emptyInfo;
    return emptyInfo;
}

void MapFileEditor::setMpcInfo(int index, const MpcInfoData& info)
{
    if (index >= 0 && index < MAP_EDITOR_MPC_COUNT)
    {
        mapData.mpc[index] = info;
    }
}

int MapFileEditor::findMpcIndexByName(const std::string& name) const
{
    for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
    {
        if (mapData.mpc[i].name == name)
            return i;
    }
    return -1;
}

int MapFileEditor::getUsedMpcCount() const
{
    int count = 0;
    for (int i = 0; i < MAP_EDITOR_MPC_COUNT; i++)
    {
        if (!mapData.mpc[i].name.empty())
            count++;
    }
    return count;
}

const MapTileData& MapFileEditor::getTile(int x, int y) const
{
    if (x >= 0 && x < mapData.head.width && y >= 0 && y < mapData.head.height)
        return mapData.tile[y][x];

    static MapTileData emptyTile;
    return emptyTile;
}

MapTileData& MapFileEditor::getTileRef(int x, int y)
{
    if (x >= 0 && x < mapData.head.width && y >= 0 && y < mapData.head.height)
        return mapData.tile[y][x];

    static MapTileData emptyTile = {};
    emptyTile = MapTileData();
    return emptyTile;
}

void MapFileEditor::setTile(int x, int y, const MapTileData& tile)
{
    if (x >= 0 && x < mapData.head.width && y >= 0 && y < mapData.head.height)
    {
        mapData.tile[y][x] = tile;
    }
}

const std::vector<std::vector<MapTileData>>& MapFileEditor::getTileData() const
{
    return mapData.tile;
}

void MapFileEditor::setTileDataAndSize(const std::vector<std::vector<MapTileData>>& tileData,
                                        int32_t width, int32_t height)
{
    mapData.tile = tileData;
    mapData.head.width = width;
    mapData.head.height = height;
}

uint8_t MapFileEditor::getTileObstacle(int x, int y) const
{
    if (x >= 0 && x < mapData.head.width && y >= 0 && y < mapData.head.height)
        return mapData.tile[y][x].obstacle;
    return 0;
}

void MapFileEditor::setTileObstacle(int x, int y, uint8_t obstacle)
{
    if (x >= 0 && x < mapData.head.width && y >= 0 && y < mapData.head.height)
        mapData.tile[y][x].obstacle = obstacle;
}

uint8_t MapFileEditor::getTileTrap(int x, int y) const
{
    if (x >= 0 && x < mapData.head.width && y >= 0 && y < mapData.head.height)
        return mapData.tile[y][x].trap;
    return 0;
}

void MapFileEditor::setTileTrap(int x, int y, uint8_t trapIndex)
{
    if (x >= 0 && x < mapData.head.width && y >= 0 && y < mapData.head.height)
        mapData.tile[y][x].trap = trapIndex;
}

MapTileLayerData MapFileEditor::getTileLayer(int x, int y, int layer) const
{
    if (x >= 0 && x < mapData.head.width && y >= 0 && y < mapData.head.height &&
        layer >= 0 && layer < MAP_EDITOR_TILE_LAYER)
        return mapData.tile[y][x].layer[layer];
    return MapTileLayerData();
}

void MapFileEditor::setTileLayer(int x, int y, int layer, const MapTileLayerData& layerData)
{
    if (x >= 0 && x < mapData.head.width && y >= 0 && y < mapData.head.height &&
        layer >= 0 && layer < MAP_EDITOR_TILE_LAYER)
    {
        mapData.tile[y][x].layer[layer] = layerData;
    }
}

std::string MapFileEditor::getLastError() const
{
    return lastError;
}

void MapFileEditor::setAssetsBasePath(const std::string& path)
{
    assetsBasePath = path;
}

const std::string& MapFileEditor::getAssetsBasePath() const
{
    return assetsBasePath;
}

void MapFileEditor::setMapFileName(const std::string& fileName)
{
    mapFileName = fileName;
}

const std::string& MapFileEditor::getMapFileName() const
{
    return mapFileName;
}

std::string MapFileEditor::getMpcFilePath(int mpcIndex) const
{
    if (mpcIndex < 0 || mpcIndex >= MAP_EDITOR_MPC_COUNT)
        return "";

    if (mapData.mpc[mpcIndex].name.empty())
        return "";

    std::string originalMpcPath = getMpcPath();
    std::string mpcPath = normalizeRelativeResourcePath(originalMpcPath);
    if (!originalMpcPath.empty() && mpcPath.empty())
        return "";
    if (mpcPath.empty())
    {
        mpcPath = "mpc/map/";
        std::string folderName = mapFileName;
        size_t lastSep = folderName.find_last_of("\\/");
        if (lastSep != std::string::npos)
            folderName = folderName.substr(lastSep + 1);
        size_t dotPos = folderName.find_last_of('.');
        if (dotPos != std::string::npos)
            folderName = folderName.substr(0, dotPos);
        if (!folderName.empty())
            mpcPath += folderName + "/";
    }
    if (!mpcPath.empty() && mpcPath.back() != '/')
    {
        mpcPath += "/";
    }
    mpcPath += mapData.mpc[mpcIndex].name;

    return normalizeRelativeResourcePath(mpcPath);
}
