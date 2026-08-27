#pragma once

#include "MapFileEditor.h"
#include <string>
#include <cstdint>
#include <vector>

// Use unified map format constants from MapFileEditor.h
#define MAP_CONVERT_HEADSTR_LEN MAP_EDITOR_HEADSTR_LEN
#define MAP_CONVERT_NULL_LEN MAP_EDITOR_NULL_LEN
#define MAP_CONVERT_PATH_LEN MAP_EDITOR_PATH_LEN
#define MAP_CONVERT_NULL2_LEN MAP_EDITOR_NULL2_LEN
#define MAP_CONVERT_HEAD_LEN MAP_EDITOR_HEAD_LEN
#define MAP_CONVERT_MPC_COUNT MAP_EDITOR_MPC_COUNT

class MapConverter
{
public:
    MapConverter();
    ~MapConverter();

    bool convertFile(const std::string& inputFileName, const std::string& outputFileName, bool toUtf8 = true,
        bool clearMapPath = false, bool forceSourceEncoding = false);
    bool convertFileInPlace(const std::string& fileName, bool toUtf8 = true);

    // Production migration contract: convert Ver2.0 to Ver3.0 while keeping
    // embedded resource paths, or validate and byte-copy an existing Ver3.0.
    // Unknown MAP versions and malformed structures fail explicitly.
    bool migrateFile(const std::string& inputFileName, const std::string& outputFileName,
        bool forceSourceEncoding = false);

    static bool isMapFile(const std::string& fileName);
    static bool isMapData(const uint8_t* data, size_t length);

    std::string getLastMessage() const;
    static std::string checkConversionWarnings(const std::string& fileName, bool toUtf8);

private:
    bool convertMapData(std::vector<uint8_t>& data, bool toUtf8, bool clearMapPath, bool forceSourceEncoding);
    std::string convertNullTerminatedString(const std::string& str, bool toUtf8, bool forceSourceEncoding);

    std::string lastMessage;
};
