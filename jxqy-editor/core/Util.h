#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

enum class ImageType
{
    None,
    Png
};

enum class PicType
{
    None,
    Mpc,
    Shd,
    Pic,
    Asf100,
    Asf101,
    Rle8,
    Imp,
    Img
};

namespace Util
{
    ImageType detectImageType(const uint8_t* data);
    ImageType detectImageType(const std::string& fileName);

    PicType detectPicType(const uint8_t* data);
    PicType detectPicType(const std::string& fileName);
    PicType detectPicTypeByExtension(const std::string& fileName);

    bool compareFileHead(const uint8_t* data, const std::string& headStr);

    // 读取整个文件内容（支持UTF-8路径，兼容中文路径）
    std::vector<uint8_t> readFileToBuffer(const std::string& utf8Path);
    // 写入整个文件内容（支持UTF-8路径，兼容中文路径）
    bool writeFileFromBuffer(const std::string& utf8Path, const void* data, size_t size);
    // 以二进制写入模式打开文件（支持UTF-8路径），调用者负责fclose
    FILE* openFileForWriteUtf8(const std::string& utf8Path);
    // 判断文件是否存在（支持UTF-8路径）
    bool fileExistsUtf8(const std::string& utf8Path);
    bool removeFileUtf8(const std::string& utf8Path);
    bool renameFileUtf8(const std::string& oldPath, const std::string& newPath);

    std::vector<std::string> getAllFiles(const std::string& path, const std::string& extension = ".*");

    std::string wideToAnsi(const std::wstring& wideStr);
    std::wstring ansiToWide(const std::string& ansiStr);
    std::wstring utf8ToWide(const std::string& utf8Str);
    std::string wideToUtf8(const std::wstring& wideStr);

    std::string gbkToUtf8(const std::string& gbkStr);
    std::string utf8ToGbk(const std::string& utf8Str);
    bool isUtf8Bom(const uint8_t* data, size_t length);
    bool isUtf8(const uint8_t* data, size_t length);
    bool isLikelyGbkTextMisreadAsUtf8(const std::string& utf8Text);

    std::string toLower(const std::string& str);
    std::string toUpper(const std::string& str);

    std::string extractFileName(const std::string& path);
    std::string extractFilePath(const std::string& path);
    std::string extractFileExtension(const std::string& path);
    std::string extractRelativePath(const std::string& basePath, const std::string& filePath);

    uint32_t argb32(uint8_t alpha, uint8_t red, uint8_t green, uint8_t blue);
    uint32_t rgb32(uint8_t red, uint8_t green, uint8_t blue);
}
