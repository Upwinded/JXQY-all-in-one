#pragma once

#include <vector>
#include "CommonTypes.h"

#define frameNullLen 1

struct IMPFrame
{
	int dataLen = 0;
	int xOffset = 0;
	int yOffset = 0;
	int frameNull[frameNullLen];
	char* data = NULL;
	void* image = NULL;
};

#define imageNullLen 5
#define imgHeadLen 16
#define imgHeadString "IMG File Ver1.0"

struct IMPImage
{
	char head[imgHeadLen];
	int frameCount = 0;
	int directions = 1;
	int interval = 1;
	int imageNull[imageNullLen];
	std::vector<IMPFrame> frame = {};
};

struct IMPList
{
	unsigned int hash;
	int offset;
};

#define impHeadString "IMP File Ver1.0"
#define impHeadLen 16
#define impNullLen 7

struct IMPFile
{
	char head[impHeadLen];
	int imageCount = 0;
	int impNull[impNullLen];
	std::vector<IMPList> list = {};
	std::vector<IMPImage> image = {};
};