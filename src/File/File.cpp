#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif 

#include "File.h"
#include <iostream>
#include <fstream>

#define APP_NAME "jxqy"
#define ORG_NAME "com.upwinded.jxqy"

File::File()
{

}

File::~File()
{
}

bool File::fileExist(const std::string& fileName)
{
    std::string  newFileName = AssetsPath + fileName;

    convert::replaceAllString(newFileName, "\\", "/");
    if (newFileName.length() <= 0)
    {
        return false;
    }
    auto fp = SDL_RWFromFile(newFileName.c_str(), "rb");

#if defined( __ANDROID__)
    if (fp == nullptr)
    {
        std::string path = SDL_AndroidGetInternalStoragePath();
        if (path.length() > 0 && *(path.end() - 1) != '/') { path += "/"; }
        convert::replaceAllString(newFileName, "/", "_");
        newFileName = path + newFileName;
        fp = SDL_RWFromFile(newFileName.c_str(), "rb");
    }
#elif defined( __APPLE__)
    if (fp == nullptr)
    {
        std::string path = SDL_GetPrefPath(ORG_NAME, APP_NAME);
        //std::string path = "~/Library/Containers/${BundleId}";
        if (path.length() > 0 && *(path.end() - 1) != '/') { path += "/"; }
        convert::replaceAllString(newFileName, "/", "_");
        newFileName = path + newFileName;
        fp = SDL_RWFromFile(newFileName.c_str(), "rb");
    }
#endif

    if (fp == nullptr) { return false;}

    SDL_RWseek(fp, 0, 2);
    auto len = SDL_RWtell(fp);
    SDL_RWclose(fp);
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
    std::string  newFileName = AssetsPath + fileName;

    convert::replaceAllString(newFileName, "\\", "/");
    auto fp = SDL_RWFromFile(newFileName.c_str(), "rb");
#if defined( __ANDROID__)
    if (!fp)
    {
        std::string path = SDL_AndroidGetInternalStoragePath();
        if (path.length() > 0 && *(path.end() - 1) != '/') { path += "/"; }
        convert::replaceAllString(newFileName, "/", "_");
        newFileName = path + newFileName;
        fp = SDL_RWFromFile(newFileName.c_str(), "rb");
    }
#elif defined( __APPLE__)
    if (fp == nullptr)
    {
        std::string path = SDL_GetPrefPath(ORG_NAME, APP_NAME);
        //std::string path = "~/Library/Containers/${BundleId}";
        if (path.length() > 0 && *(path.end() - 1) != '/') { path += "/"; }
        convert::replaceAllString(newFileName, "/", "_");
        newFileName = path + newFileName;
        fp = SDL_RWFromFile(newFileName.c_str(), "rb");
    }
#endif
    if (!fp)
    {
        fprintf(stderr, "Can not open file(rb) %s\n", newFileName.c_str());
        return false;
    }
    SDL_RWseek(fp, 0, SEEK_END);
    int length = SDL_RWtell(fp);
    len = length;
    SDL_RWseek(fp, 0, SEEK_SET);
    s = std::make_unique<char[]>(length + 1);
	memset(s.get(), 0, length + 1);
    //for (int i = 0; i <= length; (*s)[i++] = '\0');
    SDL_RWread(fp, s.get(), length, 1);
    SDL_RWclose(fp);
    return true;
}

//void File::readFile(const std::string& fileName, void * s, int len)
//{
//
//    std::string  newFileName = AssetsPath + fileName;
//
//    convert::replaceAllString(newFileName, "\\", "/");
//    auto fp = SDL_RWFromFile(newFileName.c_str(), "rb");
//#ifdef __ANDROID__
//    if (!fp)
//    {
//        std::string path = SDL_AndroidGetInternalStoragePath();
//        if (*path.end() != '/') {path += "/";}
//        convert::replaceAllString(newFileName, "/", "_");
//        newFileName = path + newFileName;
//        fp = SDL_RWFromFile(newFileName.c_str(), "rb");
//    }
//#endif
//    if (!fp)
//    {
//        fprintf(stderr, "Can not open file %s\n", newFileName.c_str());
//        return;
//    }
//    SDL_RWseek(fp, 0, 0);
//    SDL_RWread(fp, s, len, 1);
//    SDL_RWclose(fp);
//}

void File::writeFile(const std::string& fileName, const void* s, int len)
{
    if (s == nullptr)
    {
        return;
    }
    std::string  newFileName = AssetsPath + fileName;

    convert::replaceAllString(newFileName, "\\", "/");
    auto fp = SDL_RWFromFile(newFileName.c_str(), "wb");
#ifdef __ANDROID__
    if (!fp)
    {
        std::string path = SDL_AndroidGetInternalStoragePath();
        if (path.length() > 0 && *(path.end() - 1) != '/') { path += "/"; }
        convert::replaceAllString(newFileName, "/", "_");
        newFileName = path + newFileName;
        fp = SDL_RWFromFile(newFileName.c_str(), "wb");
    }
#elif defined( __APPLE__)
    if (fp == nullptr)
    {
        std::string path = SDL_GetPrefPath(ORG_NAME, APP_NAME);
        //std::string path = "~/Library/Containers/${BundleId}";
        if (path.length() > 0 && *(path.end() - 1) != '/') { path += "/"; }
        convert::replaceAllString(newFileName, "/", "_");
        newFileName = path + newFileName;
        fp = SDL_RWFromFile(newFileName.c_str(), "wb");

    }
#endif
    if (!fp)
    {
        fprintf(stderr, "Can not open file(wb) %s\n", fileName.c_str());
        return;
    }
    SDL_RWseek(fp, 0, 0);
    SDL_RWwrite(fp, s, len, 1);
    SDL_RWclose(fp);
}

void File::writeFile(const std::string& fileName, const std::unique_ptr<char[]>& s, int len)
{
    writeFile(fileName, s.get(), len);
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
    std::string newFileName = AssetsPath + fileName;
    convert::replaceAllString(newFileName, "\\", "/");
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
