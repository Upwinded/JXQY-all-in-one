#pragma once
#if (defined(__APPLE__))
#include <TargetConditionals.h>
#endif

enum class FilePos
{
	noThisFile = 0,
	fileExists = 1,
	fileInPak = 2
};

#define pakHeadLen 8
#define pakHeadString u8"PACKAGE"
#define blockSize 0x10000
#define DefaultPackageFile u8"data.dat"
#if (defined(__ANDROID__))
#define AssetsPath u8""
#elif (defined(__APPLE__))
#define AssetsPath u8"assets/"
#else
#define AssetsPath u8"../../assets/"
#endif

struct PakHead
{
	char head[pakHeadLen];
	int fileCount;
	int compressType;
};

struct PakFileInfo
{
	unsigned int fileID;
	unsigned int offset;
	unsigned int fileSize;
};
