#pragma once

#include <vector>
#define map2HeadString = "MAP File Ver2.0";

struct MapV2Head
{
	char headString[16];
	char headNull[16];
	char mapPath[32];
	int dataSize;
	int w;
	int h;
	int unknown1 = 0x40;
	int unknown2 = 0x20;
	char headNull2[108];
};

struct MapV2File
{
	char fileName[32];
	int index;
	int dynamic;
	int obstacle;
	int fileNull[5];
};

enum MapV2Obstacle
{
	M2OJump = 0x40,
	M2OWalk = 0x60,
	M2OWalkObstacle = 0x80,
	M2OJumpObstacle = 0xA0
};

struct MapV2Tile
{
	unsigned char data[3][2];
	unsigned char obstacle;
	unsigned char trap;
	unsigned char tileNull[2] = {0x00, 0x1F};
};

struct MapV2
{
	MapV2Head head;
	MapV2File file[255];
	std::vector<std::vector<MapV2Tile>> tile;
};

#define MapV3HeadString = "Map File Ver3.0";

struct MapV3Head
{
	char headString[16];
	char headNull[16];
	int w;
	int h;
	int layerCount;
	int style;
	int imageCount;
};