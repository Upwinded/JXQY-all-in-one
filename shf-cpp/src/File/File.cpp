#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif 

#include "File.h"
#include <iostream>
#include <fstream>

File::File()
{

}

File::~File()
{
}

bool File::fileExist(const std::string& fileName)
{
    if (fileName.length() <= 0)
    {
        return false;
    }

    std::fstream file;
    bool ret = false;
    file.open(fileName.c_str(), std::ios::in);
    if (file)
    {
        ret = true;
        file.close();
    }
    return ret;
}

bool File::readFile(const std::string& fileName, char** s, int* len)
{
    FILE* fp = fopen(fileName.c_str(), "rb");
    if (!fp)
    {
        fprintf(stderr, "Can not open file %s\n", fileName.c_str());
        return false;
    }
    fseek(fp, 0, SEEK_END);
    int length = ftell(fp);
    *len = length;
    fseek(fp, 0, 0);
    *s = new char[length + 1];
	memset(*s, 0, length + 1);
    //for (int i = 0; i <= length; (*s)[i++] = '\0');
    fread(*s, length, 1, fp);
    fclose(fp);
    return true;
}

void File::readFile(const std::string& fileName, void* s, int len)
{
    FILE* fp = fopen(fileName.c_str(), "rb");
    if (!fp)
    {
        fprintf(stderr, "Can not open file %s\n", fileName.c_str());
        return;
    }
    fseek(fp, 0, 0);
    fread(s, len, 1, fp);
    fclose(fp);
}

void File::writeFile(const std::string& fileName, void* s, int len)
{
    FILE* fp = fopen(fileName.c_str(), "wb");
    if (!fp)
    {
        fprintf(stderr, "Can not open file %s\n", fileName.c_str());
        return;
    }
    fseek(fp, 0, 0);
    fwrite(s, len, 1, fp);
    fclose(fp);
}

char* File::getIdxContent(std::string fileName_idx, std::string fileName_grp, std::vector<int>* offset, std::vector<int>* length)
{
    int* Ridx;
    int len = 0;
    File::readFile(fileName_idx, (char**)&Ridx, &len);

    offset->resize(len / 4 + 1);
    length->resize(len / 4);
    offset->at(0) = 0;
    for (int i = 0; i < len / 4; i++)
    {
        (*offset)[i + 1] = Ridx[i];
        (*length)[i] = (*offset)[i + 1] - (*offset)[i];
    }
    int total_length = offset->back();
    delete[] Ridx;

    auto Rgrp = new char[total_length];
    File::readFile(fileName_grp, Rgrp, total_length);
    return Rgrp;
}
