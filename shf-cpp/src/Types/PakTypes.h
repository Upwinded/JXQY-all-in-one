#pragma once

enum FilePos
{
	noThisFile = 0,
	fileExists,
	fileInPak
};

#define pakHeadLen 8
#define pakHeadString "PACKAGE"
#define blockSize 0x10000

struct PakHead
{
	char head[pakHeadLen];
	int fileCount;
	int compressType;
};

struct PakFileInfo
{
	unsigned long fileID;
	unsigned long offset;
	unsigned long fileSize;
};