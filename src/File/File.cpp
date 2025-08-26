#include <iostream>
#include <fstream>
#include "File.h"

#define APP_NAME u8"剑侠情缘2"
#define ORG_NAME u8"com.upwinded.jxqy_all_in_one"
#include "log.h"

bool File::fileExist(const std::string& fileName)
{
#if defined(__APPLE__) && ((TARGET_OS_MAC == 1) && (TARGET_OS_IOS == 0))
    std::string newFileName = SDL_GetBasePath() + std::string(AssetsPath) + fileName;
#else
    std::string newFileName = AssetsPath + fileName;
#endif
    convert::replaceAllString(newFileName, u8"\\", u8"/");
    if (newFileName.length() <= 0)
    {
        return false;
    }
    auto fp = SDL_IOFromFile(newFileName.c_str(), u8"rb");

#if defined( __ANDROID__)
    if (fp == nullptr)
    {
        std::string path = SDL_GetAndroidInternalStoragePath();
        if (path.length() > 0 && *(path.end() - 1) != '/') { path += u8"/"; }
//        convert::replaceAllString(newFileName, u8"/", u8"_");
        newFileName = path + newFileName;
//        auto tempPath = convert::extractFilePath(newFileName);
//        SDL_CreateDirectory(tempPath.c_str());
        fp = SDL_IOFromFile(newFileName.c_str(), u8"rb");
    }
#elif defined(__APPLE__)
    if (fp == nullptr)
    {
        std::string path = SDL_GetPrefPath(ORG_NAME, APP_NAME);
        //std::string path = u8"~/Library/Containers/${BundleId}";
        if (path.length() > 0 && *(path.end() - 1) != '/') { path += u8"/"; }
//        convert::replaceAllString(newFileName, u8"/", u8"_");
        newFileName = path + newFileName;
//        auto tempPath = convert::extractFilePath(newFileName);
//        SDL_CreateDirectory(tempPath.c_str());
        fp = SDL_IOFromFile(newFileName.c_str(), u8"rb");
    }
#endif

    if (fp == nullptr) { return false;}

    SDL_SeekIO(fp, 0, SDL_IO_SEEK_END);
    auto len = SDL_TellIO(fp);
    SDL_CloseIO(fp);
    if (len > 0) {return true;}
    return false;
//
//    std::fstream file;
//    bool ret = false;
//    file.open(fileName.c_str(), std::ios::in);
//    if (file)
//    {
//        ret = true;
//        file.close();
//    }
//    return ret;
}

bool File::readFile(const std::string& fileName, std::unique_ptr<char[]>& s, int& len)
{
#if defined(__APPLE__) && ((TARGET_OS_MAC == 1) && (TARGET_OS_IOS == 0))
    std::string newFileName = SDL_GetBasePath() + std::string(AssetsPath) + fileName;
#else
    std::string newFileName = AssetsPath + fileName;
#endif

    convert::replaceAllString(newFileName, u8"\\", u8"/");
    {
        auto tempPath = convert::extractFilePath(newFileName);
        SDL_CreateDirectory(tempPath.c_str());
    }
    auto fp = SDL_IOFromFile(newFileName.c_str(), u8"rb");
#if defined( __ANDROID__)
    if (!fp)
    {
        std::string path = SDL_GetAndroidInternalStoragePath();
        if (path.length() > 0 && *(path.end() - 1) != '/') { path += u8"/"; }
//        convert::replaceAllString(newFileName, u8"/", u8"_");
        newFileName = path + newFileName;
        
        fp = SDL_IOFromFile(newFileName.c_str(), u8"rb");
    }
#elif defined(__APPLE__)
    if (fp == nullptr)
    {
        std::string path = SDL_GetPrefPath(ORG_NAME, APP_NAME);
        //std::string path = u8"~/Library/Containers/${BundleId}";
        if (path.length() > 0 && *(path.end() - 1) != '/') { path += u8"/"; }
        //convert::replaceAllString(newFileName, u8"/", u8"_");
        newFileName = path + newFileName;
        fp = SDL_IOFromFile(newFileName.c_str(), u8"rb");
    }
#endif
    if (!fp)
    {
        GameLog::write("Can not open file(rb) %s\n", newFileName.c_str());
        return false;
    }
    SDL_SeekIO(fp, 0, SDL_IO_SEEK_END);
    int length = (int)SDL_TellIO(fp);
    len = length;
    SDL_SeekIO(fp, 0, SDL_IO_SEEK_SET);
    s = std::make_unique<char[]>(length + 1);
	memset(s.get(), 0, length + 1);
    //for (int i = 0; i <= length; (*s)[i++] = '\0');
    SDL_ReadIO(fp, s.get(), length);
    SDL_CloseIO(fp);
    return true;
}

//void File::readFile(const std::string& fileName, void * s, int len)
//{
//
//    std::string  newFileName = AssetsPath + fileName;
//
//    convert::replaceAllString(newFileName, u8"\\", u8"/");
//    auto fp = SDL_IOFromFile(newFileName.c_str(), u8"rb");
//#ifdef __ANDROID__
//    if (!fp)
//    {
//        std::string path = SDL_AndroidGetInternalStoragePath();
//        if (*path.end() != '/') {path += u8"/";}
//        convert::replaceAllString(newFileName, u8"/", u8"_");
//        newFileName = path + newFileName;
//        fp = SDL_IOFromFile(newFileName.c_str(), u8"rb");
//    }
//#endif
//    if (!fp)
//    {
//        GameLog::write(stderr, u8"Can not open file %s\n", newFileName.c_str());
//        return;
//    }
//    SDL_SeekIO(fp, 0, 0);
//    SDL_ReadIO(fp, s, len, 1);
//    SDL_CloseIO(fp);
//}

void File::writeFile(const std::string& fileName, const void* s, int len)
{
    if (s == nullptr)
    {
        return;
    }
#if defined(__APPLE__) && ((TARGET_OS_MAC == 1) && (TARGET_OS_IOS == 0))
    std::string newFileName = SDL_GetBasePath() + std::string(AssetsPath) + fileName;
#else
    std::string newFileName = AssetsPath + fileName;
#endif

    convert::replaceAllString(newFileName, u8"\\", u8"/");
    
    {
        auto tempPath = convert::extractFilePath(newFileName);
        SDL_CreateDirectory(tempPath.c_str());
    }
    auto fp = SDL_IOFromFile(newFileName.c_str(), u8"wb");
#ifdef __ANDROID__
    if (!fp)
    {
        std::string path = SDL_GetAndroidInternalStoragePath();
        if (path.length() > 0 && *(path.end() - 1) != '/') { path += u8"/"; }
//        convert::replaceAllString(newFileName, u8"/", u8"_");
        newFileName = path + newFileName;
        auto tempPath = convert::extractFilePath(newFileName);
        SDL_CreateDirectory(tempPath.c_str());
        fp = SDL_IOFromFile(newFileName.c_str(), u8"wb");
    }
#elif defined( __APPLE__)
    if (fp == nullptr)
    {
        std::string path = SDL_GetPrefPath(ORG_NAME, APP_NAME);
        //std::string path = u8"~/Library/Containers/${BundleId}";
        if (path.length() > 0 && *(path.end() - 1) != '/') { path += u8"/"; }
//        convert::replaceAllString(newFileName, u8"/", u8"_");
        newFileName = path + newFileName;
        auto tempPath = convert::extractFilePath(newFileName);
        SDL_CreateDirectory(tempPath.c_str());
        fp = SDL_IOFromFile(newFileName.c_str(), u8"wb");

    }
#else
    if (!fp)
    {
        auto tempPath = convert::extractFilePath(newFileName);
        SDL_CreateDirectory(tempPath.c_str());
        fp = SDL_IOFromFile(newFileName.c_str(), u8"wb+");
    }
#endif
    if (!fp)
    {
        GameLog::write("Can not open file(wb) %s\n", fileName.c_str());
        return;
    }
    SDL_SeekIO(fp, 0, SDL_IO_SEEK_SET);
    SDL_WriteIO(fp, s, len);
    SDL_CloseIO(fp);
}

void File::writeFile(const std::string& fileName, const std::unique_ptr<char[]>& s, int len)
{
    writeFile(fileName, s.get(), len);
}

void File::appendFile(const std::string& fileName, const void* s, int len)
{
    if (s == nullptr)
    {
        return;
    }
#if defined(__APPLE__) && ((TARGET_OS_MAC == 1) && (TARGET_OS_IOS == 0))
    std::string newFileName = SDL_GetBasePath() + std::string(AssetsPath) + fileName;
#else
    std::string newFileName = AssetsPath + fileName;
#endif

    convert::replaceAllString(newFileName, u8"\\", u8"/");
    auto fp = SDL_IOFromFile(newFileName.c_str(), u8"wb+");
#ifdef __ANDROID__
    if (!fp)
    {
        std::string path = SDL_GetAndroidInternalStoragePath();
        if (path.length() > 0 && *(path.end() - 1) != '/') { path += u8"/"; }
//        convert::replaceAllString(newFileName, u8"/", u8"_");
        newFileName = path + newFileName;
        auto tempPath = convert::extractFilePath(newFileName);
        SDL_CreateDirectory(tempPath.c_str());
        fp = SDL_IOFromFile(newFileName.c_str(), u8"wb+");
    }
#elif defined( __APPLE__)
    if (fp == nullptr)
    {
        std::string path = SDL_GetPrefPath(ORG_NAME, APP_NAME);
        //std::string path = u8"~/Library/Containers/${BundleId}";
        if (path.length() > 0 && *(path.end() - 1) != '/') { path += u8"/"; }
//        convert::replaceAllString(newFileName, u8"/", u8"_");
        newFileName = path + newFileName;
        auto tempPath = convert::extractFilePath(newFileName);
        SDL_CreateDirectory(tempPath.c_str());
        fp = SDL_IOFromFile(newFileName.c_str(), u8"wb+");

    }
#else
    if (!fp)
    {
        auto tempPath = convert::extractFilePath(newFileName);
        SDL_CreateDirectory(tempPath.c_str());
        fp = SDL_IOFromFile(newFileName.c_str(), u8"wb+");
    }
#endif
    if (!fp)
    {
        fprintf(stderr, u8"Can not open file(wb+) %s\n", fileName.c_str());
        return;
    }
    SDL_SeekIO(fp, 0, SDL_IO_SEEK_END);
    SDL_WriteIO(fp, s, len);
    SDL_CloseIO(fp);
}

void File::appendFile(const std::string& fileName, const std::unique_ptr<char[]>& s, int len)
{
    appendFile(fileName, s.get(), len);
}

void File::copy(const std::string& src, const std::string& dst)
{
    std::unique_ptr<char[]> s;
    int len = 0;
    if (readFile(src, s, len))
    {
        if (s != nullptr && len > 0)
        {
            writeFile(dst, s, len);
        }
    }
}

std::string File::getAssetsName(const std::string& fileName)
{
#if defined(__APPLE__) && ((TARGET_OS_MAC == 1) && (TARGET_OS_IOS == 0))
    std::string newFileName = SDL_GetBasePath() + std::string(AssetsPath) + fileName;
#else
    std::string newFileName = AssetsPath + fileName;
#endif
    convert::replaceAllString(newFileName, u8"\\", u8"/");
    return newFileName;
}

//std::unique_ptr<char[]> File::getIdxContent(std::string fileName_idx, std::string fileName_grp, std::vector<int>* offset, std::vector<int>* length)
//{
//    std::unique_ptr<char[]> Ridx;
//    int len = 0;
//    if (File::readFile(fileName_idx, Ridx, len))
//    {
//        offset->resize(len / 4 + 1);
//        length->resize(len / 4);
//        offset->at(0) = 0;
//        for (int i = 0; i < len / 4; i++)
//        {
//            (*offset)[i + 1] = ((int*)Ridx.get())[i];
//            (*length)[i] = (*offset)[i + 1] - (*offset)[i];
//        }
//        int total_length = offset->back();
//
//        auto Rgrp = std::make_unique<char[]>(total_length);
//        File::readFile(fileName_grp, Rgrp, total_length);
//        return Rgrp;
//    }
//}
