#pragma once
#include <vector>
#include "../Types/Types.h"
#include "../File/File.h"
#include "../Engine/Engine.h"
#include "../File/PakFile.h"


#define Directly_Load false

#define frameNullLen 1

struct IMPFrame
{
	int dataLen = 0;
	int xOffset = 0;
	int yOffset = 0;
	int frameNull[frameNullLen];
	//char* data = nullptr;
	std::unique_ptr<char[]> data;
	_shared_image image = nullptr;
};

#define imageNullLen 5
#define imgHeadLen 16
#define imgHeadString u8"IMG File Ver1.0"

struct IMPImage
{
private:
	static uint32_t IMPImageCount;
public:
#ifdef DEBUG
	IMPImage() { GameLog::write("IMPImageCount Inc:%d", ++IMPImageCount); }
	~IMPImage() { GameLog::write("IMPImageCount Dec:%d", --IMPImageCount); }
#endif // DEBUG

	char head[imgHeadLen];
	int directions = 1;
	int interval = 1;
	int imageNull[imageNullLen];
	std::vector<IMPFrame> frame;
};


struct IMPList
{
	unsigned int hash;
	int offset;
};

#define impHeadString u8"IMP File Ver1.0"
#define impHeadLen 16
#define impNullLen 7

struct IMPFile
{
	char head[impHeadLen];
	int imageCount = 0;
	int impNull[impNullLen];
	std::vector<IMPList> list;
	std::vector<IMPImage> image;
};

class IMP
{
private:
	static bool loadIMPImage(_shared_imp impImage, const std::string& fileName, bool directlyLoad = Directly_Load);
	static bool loadIMPImageFromMem(_shared_imp impImage, std::unique_ptr<char[]>& data, int size, bool directlyLoad = Directly_Load);
	static bool loadIMPImageFromFile(_shared_imp impImage, const std::string& fileName, bool directlyLoad = Directly_Load);
	static bool loadIMPImageFromPak(_shared_imp impImage, const std::string& fileName, const std::string& pakName = u8"", bool directlyLoad = Directly_Load, bool firstReadPak = false);

public:

	static unsigned int getIMPImageActionTime(_shared_imp impImage);

	static void copyIMPImage(_shared_imp dst, _shared_imp src);
	
	static _shared_imp createIMPImage(const std::string& fileName, bool directlyLoad = Directly_Load);
	static _shared_imp createIMPImage(unsigned int fileID, bool directlyLoad = Directly_Load);
	static _shared_imp createIMPImageFromMem(std::unique_ptr<char[]>& data, int size, bool directlyLoad = Directly_Load);
	static _shared_imp createIMPImageFromFile(const std::string& fileName, bool directlyLoad = Directly_Load);
	static _shared_imp createIMPImageFromPak(const std::string& fileName, bool directlyLoad = Directly_Load, const std::string & pakName = u8"", bool firstReadPak = false);
	static _shared_imp createIMPImageFromPNG(std::string pngName, bool directlyLoad = Directly_Load);
	static _shared_imp createIMPImageFromImage(_shared_image img);


	static _shared_imp createIMPImageFromFrame(_shared_imp impImage, int index);

	static _shared_image loadImage(_shared_imp impImage, int index, int * xOffset = nullptr, int * yOffset = nullptr);
	static _shared_image loadImageForTime(_shared_imp impImage, UTime time, int * xOffset = nullptr, int * yOffset = nullptr, bool once = false, bool reverse = false);
	static _shared_image loadImageForDirection(_shared_imp impImage, int direction, UTime time, int * xOffset = nullptr, int * yOffset = nullptr, bool once = false, bool reverse = false);
	static _shared_image loadImageForLastFrame(_shared_imp impImage, int direction, int * xOffset = nullptr, int * yOffset = nullptr, bool reverse = false);

private:
	static bool cmpIMGHead(_shared_imp img);
	static void clearIMPImage(_shared_imp impImage);

};
