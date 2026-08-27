#include "MapConverter.h"
#include "Util.h"

#include <fstream>
#include <cstring>
#include <cstdio>

namespace
{
std::string lowercaseAsciiResourceName(std::string value)
{
    for (char& character : value)
    {
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character + ('a' - 'A'));
    }
    return value;
}

void normalizeMapResourceReferences(MapFileEditor& editor)
{
    const std::string normalizedPath = lowercaseAsciiResourceName(
        editor.getMpcPath());
    if (normalizedPath != editor.getMpcPath())
        editor.setMpcPath(normalizedPath);

    for (int index = 0; index < MAP_EDITOR_MPC_COUNT; ++index)
    {
        MpcInfoData info = editor.getMpcInfo(index);
        const std::string normalizedName = lowercaseAsciiResourceName(
            info.name);
        if (normalizedName == info.name)
            continue;
        info.name = normalizedName;
        editor.setMpcInfo(index, info);
    }
}

void lowercaseAsciiFixedString(uint8_t* data, size_t capacity)
{
    for (size_t index = 0; index < capacity && data[index] != 0; ++index)
    {
        if (data[index] >= 'A' && data[index] <= 'Z')
        {
            data[index] = static_cast<uint8_t>(
                data[index] + ('a' - 'A'));
        }
    }
}

void normalizeVersion3MapResourceReferences(
    std::vector<uint8_t>& data)
{
    MapEditorHead head = {};
    std::memcpy(&head, data.data(), sizeof(head));

    int32_t headerSize = 0;
    int32_t pathLength = 0;
    std::memcpy(&headerSize, head.dataNil2, sizeof(headerSize));
    std::memcpy(&pathLength, head.dataNil2 + 4, sizeof(pathLength));

    lowercaseAsciiFixedString(
        data.data() + MAP_EDITOR_HEADSTR_LEN + MAP_EDITOR_NULL_LEN,
        MAP_EDITOR_PATH_LEN);
    if (pathLength > 0)
    {
        lowercaseAsciiFixedString(
            data.data() + MAP_EDITOR_HEAD_LEN,
            static_cast<size_t>(pathLength));
    }

    const size_t infoLength = static_cast<size_t>(head.infoLen);
    const size_t nameLength = static_cast<size_t>(head.nameLen);
    const size_t mpcInfoOffset = static_cast<size_t>(headerSize);
    for (int index = 0; index < MAP_EDITOR_MPC_COUNT; ++index)
    {
        lowercaseAsciiFixedString(
            data.data() + mpcInfoOffset +
                static_cast<size_t>(index) * infoLength,
            nameLength);
    }
}
}

MapConverter::MapConverter()
{
}

MapConverter::~MapConverter()
{
}

bool MapConverter::isMapFile(const std::string& fileName)
{
    return MapFileEditor::isMapFile(fileName);
}

bool MapConverter::isMapData(const uint8_t* data, size_t length)
{
    return MapFileEditor::isMapData(data, length);
}

std::string MapConverter::checkConversionWarnings(const std::string& fileName, bool toUtf8)
{
    if (!toUtf8)
        return "GBK map output is disabled; map fixed strings must be UTF-8\n";

    std::vector<uint8_t> buffer = Util::readFileToBuffer(fileName);
    if (!buffer.empty() && !MapFileEditor::isMapData(buffer.data(), buffer.size()))
        return "Not a valid map file: " + fileName + "\n";
    return "";
}

std::string MapConverter::getLastMessage() const
{
    return lastMessage;
}

std::string MapConverter::convertNullTerminatedString(const std::string& str, bool toUtf8, bool forceSourceEncoding)
{
    (void)toUtf8;
    if (str.empty())
    {
        return str;
    }

    if (forceSourceEncoding)
    {
        return Util::gbkToUtf8(str);
    }

    if (Util::isUtf8(reinterpret_cast<const uint8_t*>(str.data()), str.size()))
    {
        return str;
    }
    return Util::gbkToUtf8(str);
}

bool MapConverter::convertMapData(std::vector<uint8_t>& data, bool toUtf8, bool clearMapPath, bool forceSourceEncoding)
{
    if (!toUtf8)
    {
        lastMessage = "GBK map output is disabled; map fixed strings must be UTF-8";
        return false;
    }

    MapFileEditor editor;
    if (!editor.loadFromBuffer(data.data(), data.size(), forceSourceEncoding))
    {
        lastMessage = editor.getLastError();
        return false;
    }
    if (clearMapPath)
        editor.setMpcPath("");
    normalizeMapResourceReferences(editor);

    std::vector<uint8_t> convertedData = editor.saveToBuffer();
    if (convertedData.empty())
    {
        lastMessage = "Failed to write MAP File Ver3.0 data";
        return false;
    }
    data.swap(convertedData);
    return true;
}

bool MapConverter::convertFile(const std::string& inputFileName, const std::string& outputFileName, bool toUtf8,
    bool clearMapPath, bool forceSourceEncoding)
{
    std::vector<uint8_t> fileData = Util::readFileToBuffer(inputFileName);
    if (fileData.empty())
    {
        lastMessage = "Cannot open file: " + inputFileName;
        return false;
    }

    if (!isMapData(fileData.data(), fileData.size()))
    {
        lastMessage = "Not a valid map file: " + inputFileName;
        return false;
    }

    if (!convertMapData(fileData, toUtf8, clearMapPath, forceSourceEncoding))
    {
        return false;
    }

    if (!Util::writeFileFromBuffer(outputFileName, fileData.data(), fileData.size()))
    {
        lastMessage = "Cannot write file: " + outputFileName;
        return false;
    }

    lastMessage = "Map file encoding converted: " + inputFileName;
    return true;
}

bool MapConverter::migrateFile(const std::string& inputFileName, const std::string& outputFileName,
    bool forceSourceEncoding)
{
    std::vector<uint8_t> fileData = Util::readFileToBuffer(inputFileName);
    if (fileData.empty())
    {
        lastMessage = "Cannot open file: " + inputFileName;
        return false;
    }

    const bool isVersion2 = fileData.size() >= MAP_EDITOR_HEADSTR_LEN &&
        std::memcmp(fileData.data(), MAP_EDITOR_HEADSTR_V2, MAP_EDITOR_HEADSTR_LEN) == 0;
    const bool isVersion3 = fileData.size() >= MAP_EDITOR_HEADSTR_LEN &&
        std::memcmp(fileData.data(), MAP_EDITOR_HEADSTR_V3, MAP_EDITOR_HEADSTR_LEN) == 0;
    if (!isVersion2 && !isVersion3)
    {
        if (fileData.size() >= 8 && std::memcmp(fileData.data(), "MAP File", 8) == 0)
            lastMessage = "Unsupported MAP version header: " + inputFileName;
        else
            lastMessage = "Not a valid map file: " + inputFileName;
        return false;
    }

    if (isVersion3)
    {
        MapFileEditor editor;
        if (!editor.loadFromBuffer(fileData.data(), fileData.size()))
        {
            lastMessage = "Invalid MAP File Ver3.0 structure: " + editor.getLastError();
            return false;
        }
        normalizeVersion3MapResourceReferences(fileData);
    }
    else if (!convertMapData(fileData, true, false, forceSourceEncoding))
    {
        lastMessage = "Invalid MAP File Ver2.0 structure: " + lastMessage;
        return false;
    }

    if (!Util::writeFileFromBuffer(outputFileName, fileData.data(), fileData.size()))
    {
        lastMessage = "Cannot write file: " + outputFileName;
        return false;
    }

    lastMessage = isVersion3
        ? "MAP File Ver3.0 validated and resource names normalized: " + inputFileName
        : "MAP File Ver2.0 migrated to Ver3.0: " + inputFileName;
    return true;
}

bool MapConverter::convertFileInPlace(const std::string& fileName, bool toUtf8)
{
    std::vector<uint8_t> fileData = Util::readFileToBuffer(fileName);
    if (fileData.empty())
    {
        lastMessage = "Cannot open file: " + fileName;
        return false;
    }
    if (!convertMapData(fileData, toUtf8, false, false))
        return false;
    if (!Util::writeFileFromBuffer(fileName, fileData.data(), fileData.size()))
    {
        lastMessage = "Cannot atomically replace file: " + fileName;
        return false;
    }
    lastMessage = "Map file encoding converted: " + fileName;
    return true;
}
