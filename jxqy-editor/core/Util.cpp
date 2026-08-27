#include "Util.h"
#include "AuthoringMutationGate.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#include <algorithm>
#include <cerrno>
#include <fstream>
#include <cstdio>
#include <limits>
#include <locale>
#include <codecvt>

#include <QSaveFile>
#include <QString>

#ifndef _WIN32
#include <iconv.h>
#endif

namespace
{
#ifndef _WIN32
std::string convertEncoding(const std::string& input, const char* targetEncoding, const char* sourceEncoding)
{
    if (input.empty())
        return std::string();

    iconv_t converter = iconv_open(targetEncoding, sourceEncoding);
    if (converter == reinterpret_cast<iconv_t>(-1))
        return std::string();

    size_t inputBytesLeft = input.size();
    const char* inputCursor = input.data();
    std::string output(std::max<size_t>(input.size() * 2, 64), '\0');
    size_t outputOffset = 0;

    while (inputBytesLeft > 0)
    {
        if (outputOffset == output.size())
            output.resize(output.size() * 2);

        char* outputCursor = output.data() + outputOffset;
        size_t outputBytesLeft = output.size() - outputOffset;
        char* mutableInputCursor = const_cast<char*>(inputCursor);
        size_t result = iconv(
            converter,
            &mutableInputCursor,
            &inputBytesLeft,
            &outputCursor,
            &outputBytesLeft);
        inputCursor = mutableInputCursor;
        outputOffset = output.size() - outputBytesLeft;

        if (result != static_cast<size_t>(-1))
            continue;
        if (errno != E2BIG)
        {
            iconv_close(converter);
            return std::string();
        }
        output.resize(output.size() * 2);
    }

    iconv_close(converter);
    output.resize(outputOffset);
    return output;
}
#endif
}

namespace Util
{
    static const std::string PNG_HEAD = "\x89PNG";

    static const std::string MPC_HEAD = "MPC File Ver2.0";
    static const std::string PIC_HEAD = "PIC File Ver1.0";
    static const std::string SHD_HEAD = "SHD File Ver2.0";
    static const std::string ASF100_HEAD = "ASF 1.00";
    static const std::string ASF101_HEAD = "ASF 1.01";
    static const std::string IMG_HEAD = "IMG File Ver1.0";

    bool compareFileHead(const uint8_t* data, const std::string& headStr)
    {
        if (data == nullptr)
            return false;
        for (size_t i = 0; i < headStr.size(); i++)
        {
            if (data[i] != static_cast<uint8_t>(headStr[i]))
            {
                return false;
            }
        }
        return true;
    }

    ImageType detectImageType(const uint8_t* data)
    {
        if (data == nullptr)
            return ImageType::None;
        if (compareFileHead(data, PNG_HEAD))
            return ImageType::Png;
        return ImageType::None;
    }

    ImageType detectImageType(const std::string& fileName)
    {
        std::vector<uint8_t> buffer = readFileToBuffer(fileName);
        if (buffer.size() < 16)
            return ImageType::None;
        return detectImageType(buffer.data());
    }

    PicType detectPicType(const uint8_t* data)
    {
        if (data == nullptr)
            return PicType::None;
        if (compareFileHead(data, MPC_HEAD))
            return PicType::Mpc;
        if (compareFileHead(data, PIC_HEAD))
            return PicType::Pic;
        if (compareFileHead(data, SHD_HEAD))
            return PicType::Shd;
        if (compareFileHead(data, ASF100_HEAD))
            return PicType::Asf100;
        if (compareFileHead(data, ASF101_HEAD))
            return PicType::Asf101;
        if (compareFileHead(data, IMG_HEAD))
            return PicType::Imp;
        if (compareFileHead(data, PNG_HEAD))
            return PicType::Img;
        return PicType::None;
    }

    PicType detectPicType(const std::string& fileName)
    {
        std::vector<uint8_t> buffer = readFileToBuffer(fileName);
        if (buffer.size() < 16)
            return PicType::None;
        return detectPicType(buffer.data());
    }

    PicType detectPicTypeByExtension(const std::string& fileName)
    {
        std::string ext = toLower(extractFileExtension(fileName));
        if (ext == ".mpc")
            return PicType::Mpc;
        if (ext == ".shd")
            return PicType::Shd;
        if (ext == ".pic")
            return PicType::Pic;
        if (ext == ".asf")
            return PicType::Asf100;
        if (ext == ".imp" || ext == ".img")
            return PicType::Img;
        return PicType::None;
    }

    std::vector<std::string> getAllFiles(const std::string& path, const std::string& extension)
    {
        std::vector<std::string> result;
#ifdef _WIN32
        std::wstring widePath = utf8ToWide(path + "*.*");
        WIN32_FIND_DATAW findData;
        HANDLE handle = FindFirstFileW(widePath.c_str(), &findData);
        if (handle == INVALID_HANDLE_VALUE)
            return result;

        std::string extUpper = toUpper(extension);
        do
        {
            std::string name = wideToUtf8(findData.cFileName);
            if (name == "." || name == "..")
                continue;

            std::string fullPath = path + name;
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                auto subFiles = getAllFiles(fullPath + "\\", extension);
                result.insert(result.end(), subFiles.begin(), subFiles.end());
            }
            else
            {
                if (extUpper == ".*" || toUpper(extractFileExtension(name)) == extUpper)
                {
                    result.push_back(fullPath);
                }
            }
        } while (FindNextFileW(handle, &findData));
        FindClose(handle);
#else
        DIR* dir = opendir(path.c_str());
        if (!dir)
            return result;

        std::string extUpper = toUpper(extension);
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr)
        {
            std::string name = entry->d_name;
            if (name == "." || name == "..")
                continue;

            std::string fullPath = path + name;
            struct stat statBuf;
            if (stat(fullPath.c_str(), &statBuf) == 0)
            {
                if (S_ISDIR(statBuf.st_mode))
                {
                    auto subFiles = getAllFiles(fullPath + "/", extension);
                    result.insert(result.end(), subFiles.begin(), subFiles.end());
                }
                else
                {
                    if (extUpper == ".*" || toUpper(extractFileExtension(name)) == extUpper)
                    {
                        result.push_back(fullPath);
                    }
                }
            }
        }
        closedir(dir);
#endif
        return result;
    }

    std::wstring utf8ToWide(const std::string& utf8Str)
    {
        if (utf8Str.empty())
            return L"";
#ifdef _WIN32
        if (utf8Str.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
            return L"";
        const int sourceLength = static_cast<int>(utf8Str.size());
        int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            utf8Str.data(), sourceLength, nullptr, 0);
        if (len <= 0)
            return L"";
        std::wstring result(static_cast<size_t>(len), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                utf8Str.data(), sourceLength, result.data(), len) != len)
        {
            return L"";
        }
        return result;
#else
        try
        {
            std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
            return converter.from_bytes(utf8Str);
        }
        catch (const std::exception&)
        {
            return std::wstring(utf8Str.begin(), utf8Str.end());
        }
#endif
    }

    std::string wideToUtf8(const std::wstring& wideStr)
    {
        if (wideStr.empty())
            return "";
#ifdef _WIN32
        if (wideStr.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
            return "";
        const int sourceLength = static_cast<int>(wideStr.size());
        int len = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            wideStr.data(), sourceLength, nullptr, 0, nullptr, nullptr);
        if (len <= 0)
            return "";
        std::string result(static_cast<size_t>(len), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                wideStr.data(), sourceLength, result.data(), len, nullptr, nullptr) != len)
        {
            return "";
        }
        return result;
#else
        try
        {
            std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
            return converter.to_bytes(wideStr);
        }
        catch (const std::exception&)
        {
            return std::string(wideStr.begin(), wideStr.end());
        }
#endif
    }

    std::vector<uint8_t> readFileToBuffer(const std::string& utf8Path)
    {
        std::vector<uint8_t> result;
#ifdef _WIN32
        std::wstring widePath = utf8ToWide(utf8Path);
        if (widePath.empty())
            return result;

        // Use _wfopen for Unicode path support
        FILE* fp = _wfopen(widePath.c_str(), L"rb");
        if (!fp)
            return result;

        _fseeki64(fp, 0, SEEK_END);
        __int64 fileSize = _ftelli64(fp);
        _fseeki64(fp, 0, SEEK_SET);

        if (fileSize <= 0)
        {
            fclose(fp);
            return result;
        }

        // Validate file size is allocatable
        if (static_cast<unsigned __int64>(fileSize) > static_cast<unsigned __int64>((std::numeric_limits<size_t>::max)()))
        {
            fclose(fp);
            return result;
        }

        result.resize(static_cast<size_t>(fileSize));
        size_t readBytes = fread(result.data(), 1, static_cast<size_t>(fileSize), fp);
        fclose(fp);

        if (readBytes != static_cast<size_t>(fileSize))
        {
            result.clear();
        }
#else
        std::ifstream file(utf8Path, std::ios::binary);
        if (!file.is_open())
            return result;

        file.seekg(0, std::ios::end);
        auto fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        if (fileSize <= 0)
            return result;

        result.resize(static_cast<size_t>(fileSize));
        file.read(reinterpret_cast<char*>(result.data()), fileSize);
        file.close();
#endif
        return result;
    }

    bool fileExistsUtf8(const std::string& utf8Path)
    {
#ifdef _WIN32
        std::wstring widePath = utf8ToWide(utf8Path);
        if (widePath.empty())
            return false;
        DWORD attrib = GetFileAttributesW(widePath.c_str());
        return (attrib != INVALID_FILE_ATTRIBUTES);
#else
        struct stat st;
        return (stat(utf8Path.c_str(), &st) == 0);
#endif
    }

    bool removeFileUtf8(const std::string& utf8Path)
    {
        auto mutationLease =
            AuthoringMutationGate::instance().
                acquireMutationLeaseForPath(QString::fromUtf8(
                    utf8Path.data(),
                    static_cast<qsizetype>(utf8Path.size())));
        if (!mutationLease)
            return false;
#ifdef _WIN32
        std::wstring widePath = utf8ToWide(utf8Path);
        if (widePath.empty())
            return false;
        return (_wremove(widePath.c_str()) == 0);
#else
        return (std::remove(utf8Path.c_str()) == 0);
#endif
    }

    bool renameFileUtf8(const std::string& oldPath, const std::string& newPath)
    {
        auto mutationLease =
            AuthoringMutationGate::instance().
                acquireMutationLeaseForPath(QString::fromUtf8(
                    oldPath.data(),
                    static_cast<qsizetype>(oldPath.size())));
        if (!mutationLease)
            return false;
        if (!mutationLease.addResourcePath(QString::fromUtf8(
                newPath.data(),
                static_cast<qsizetype>(newPath.size()))))
        {
            return false;
        }
#ifdef _WIN32
        std::wstring wideOld = utf8ToWide(oldPath);
        std::wstring wideNew = utf8ToWide(newPath);
        if (wideOld.empty() || wideNew.empty())
            return false;
        return (_wrename(wideOld.c_str(), wideNew.c_str()) == 0);
#else
        return (std::rename(oldPath.c_str(), newPath.c_str()) == 0);
#endif
    }

    bool writeFileFromBuffer(const std::string& utf8Path, const void* data, size_t size)
    {
        auto mutationLease =
            AuthoringMutationGate::instance().
                acquireMutationLeaseForPath(QString::fromUtf8(
                    utf8Path.data(),
                    static_cast<qsizetype>(utf8Path.size())));
        if (!mutationLease)
            return false;
        if ((data == nullptr && size > 0) ||
            size > static_cast<size_t>((std::numeric_limits<qint64>::max)()))
        {
            return false;
        }

        QSaveFile file(QString::fromUtf8(utf8Path.data(), static_cast<qsizetype>(utf8Path.size())));
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly))
            return false;

        if (size > 0 && file.write(static_cast<const char*>(data), static_cast<qint64>(size)) !=
                static_cast<qint64>(size))
        {
            file.cancelWriting();
            return false;
        }
        return file.commit();
    }

    FILE* openFileForWriteUtf8(const std::string& utf8Path)
    {
#ifdef _WIN32
        std::wstring widePath = utf8ToWide(utf8Path);
        if (widePath.empty())
            return nullptr;
        return _wfopen(widePath.c_str(), L"wb");
#else
        return fopen(utf8Path.c_str(), "wb");
#endif
    }

    std::string wideToAnsi(const std::wstring& wideStr)
    {
        if (wideStr.empty())
            return "";
#ifdef _WIN32
        if (wideStr.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
            return "";
        const int sourceLength = static_cast<int>(wideStr.size());
        BOOL usedDefaultCharacter = FALSE;
        int len = WideCharToMultiByte(936, WC_NO_BEST_FIT_CHARS,
            wideStr.data(), sourceLength, nullptr, 0, nullptr, &usedDefaultCharacter);
        if (len <= 0 || usedDefaultCharacter)
            return "";
        std::string result(static_cast<size_t>(len), '\0');
        usedDefaultCharacter = FALSE;
        if (WideCharToMultiByte(936, WC_NO_BEST_FIT_CHARS,
                wideStr.data(), sourceLength, result.data(), len, nullptr,
                &usedDefaultCharacter) != len || usedDefaultCharacter)
        {
            return "";
        }
        return result;
#else
        return utf8ToGbk(wideToUtf8(wideStr));
#endif
    }

    std::wstring ansiToWide(const std::string& ansiStr)
    {
        if (ansiStr.empty())
            return L"";
#ifdef _WIN32
        if (ansiStr.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
            return L"";
        const int sourceLength = static_cast<int>(ansiStr.size());
        int len = MultiByteToWideChar(936, 0, ansiStr.data(), sourceLength, nullptr, 0);
        if (len <= 0)
            return L"";
        std::wstring result(static_cast<size_t>(len), L'\0');
        if (MultiByteToWideChar(936, 0, ansiStr.data(), sourceLength, result.data(), len) != len)
            return L"";
        return result;
#else
        return utf8ToWide(gbkToUtf8(ansiStr));
#endif
    }

    std::string gbkToUtf8(const std::string& gbkStr)
    {
        if (gbkStr.empty())
            return "";
#ifdef _WIN32
        return wideToUtf8(ansiToWide(gbkStr));
#else
        return convertEncoding(gbkStr, "UTF-8", "GBK");
#endif
    }

    std::string utf8ToGbk(const std::string& utf8Str)
    {
        if (utf8Str.empty())
            return "";
#ifdef _WIN32
        return wideToAnsi(utf8ToWide(utf8Str));
#else
        return convertEncoding(utf8Str, "GBK", "UTF-8");
#endif
    }

    bool isUtf8Bom(const uint8_t* data, size_t length)
    {
        if (length < 3)
            return false;
        return data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF;
    }

    bool isUtf8(const uint8_t* data, size_t length)
    {
        if (isUtf8Bom(data, length))
            return true;

        size_t i = 0;
        while (i < length)
        {
            uint8_t byte = data[i];
            if (byte <= 0x7F)
            {
                i++;
            }
            else if (byte >= 0xC2 && byte <= 0xDF)
            {
                if (i + 1 >= length || (data[i + 1] & 0xC0) != 0x80)
                    return false;
                i += 2;
            }
            else if (byte >= 0xE0 && byte <= 0xEF)
            {
                if (i + 2 >= length || (data[i + 1] & 0xC0) != 0x80 || (data[i + 2] & 0xC0) != 0x80)
                    return false;
                if ((byte == 0xE0 && data[i + 1] < 0xA0) ||
                    (byte == 0xED && data[i + 1] > 0x9F))
                {
                    return false;
                }
                i += 3;
            }
            else if (byte >= 0xF0 && byte <= 0xF4)
            {
                if (i + 3 >= length || (data[i + 1] & 0xC0) != 0x80 || (data[i + 2] & 0xC0) != 0x80 || (data[i + 3] & 0xC0) != 0x80)
                    return false;
                if ((byte == 0xF0 && data[i + 1] < 0x90) ||
                    (byte == 0xF4 && data[i + 1] > 0x8F))
                {
                    return false;
                }
                i += 4;
            }
            else
            {
                return false;
            }
        }
        return true;
    }

    bool isLikelyGbkTextMisreadAsUtf8(const std::string& utf8Text)
    {
        if (utf8Text.empty() ||
            !isUtf8(reinterpret_cast<const uint8_t*>(utf8Text.data()), utf8Text.size()))
        {
            return false;
        }

        const QString decoded = QString::fromUtf8(
            utf8Text.data(), static_cast<qsizetype>(utf8Text.size()));
        bool hasCjk = false;
        bool hasSuspiciousLegacyCodePoint = false;
        for (QChar character : decoded)
        {
            const ushort value = character.unicode();
            hasCjk = hasCjk ||
                (value >= 0x3400 && value <= 0x4DBF) ||
                (value >= 0x4E00 && value <= 0x9FFF) ||
                (value >= 0x3000 && value <= 0x303F) ||
                (value >= 0xFF00 && value <= 0xFFEF);
            hasSuspiciousLegacyCodePoint = hasSuspiciousLegacyCodePoint ||
                (value >= 0x0100 && value <= 0x024F) ||
                (value >= 0x0250 && value <= 0x03FF) ||
                (value >= 0x0400 && value <= 0x052F);
        }
        return !hasCjk && hasSuspiciousLegacyCodePoint;
    }

    std::string toLower(const std::string& str)
    {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(),
            [](unsigned char c) { return std::tolower(c); });
        return result;
    }

    std::string toUpper(const std::string& str)
    {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(),
            [](unsigned char c) { return std::toupper(c); });
        return result;
    }

    std::string extractFileName(const std::string& path)
    {
        size_t pos = path.find_last_of("\\/");
        if (pos == std::string::npos)
            return path;
        return path.substr(pos + 1);
    }

    std::string extractFilePath(const std::string& path)
    {
        size_t pos = path.find_last_of("\\/");
        if (pos == std::string::npos)
            return "";
        return path.substr(0, pos + 1);
    }

    std::string extractFileExtension(const std::string& path)
    {
        std::string fileName = extractFileName(path);
        size_t pos = fileName.find_last_of('.');
        if (pos == std::string::npos)
            return "";
        return fileName.substr(pos);
    }

    std::string extractRelativePath(const std::string& basePath, const std::string& filePath)
    {
        if (filePath.size() < basePath.size())
            return filePath;
        if (filePath.substr(0, basePath.size()) == basePath)
            return filePath.substr(basePath.size());
        return filePath;
    }

    uint32_t argb32(uint8_t alpha, uint8_t red, uint8_t green, uint8_t blue)
    {
        return (alpha << 24) | (red << 16) | (green << 8) | blue;
    }

    uint32_t rgb32(uint8_t red, uint8_t green, uint8_t blue)
    {
        return argb32(255, red, green, blue);
    }
}
