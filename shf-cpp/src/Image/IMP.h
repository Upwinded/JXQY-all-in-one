#pragma once
#include <vector>
#include "../Types/Types.h"
#include "../File/File.h"
#include "../Engine/Engine.h"
#include "../File/PakFile.h"

class IMP
{
public:
	IMP();
	~IMP();

private:
	static IMP imp;
	static IMP * this_;

public:
	static IMP * getInstance();

	static unsigned int getIMPImageActionTime(IMPImage * impImage);

	static bool loadIMPImage(IMPImage * impImage, const std::string& fileName);
	static bool loadIMPImageFromMem(IMPImage * impImage, char *data, int size);
	static bool loadIMPImageFromFile(IMPImage * impImage, const std::string& fileName);
	static bool loadIMPImageFromPak(IMPImage * impImage, const std::string& fileName, const std::string & pakName = "", bool firstReadPak = false);

	static void copyIMPImage(IMPImage* dst, IMPImage* src);
	
	static IMPImage * createIMPImage(const std::string& fileName);
	static IMPImage * createIMPImage(unsigned int fileID);
	static IMPImage * createIMPImageFromMem(char *data, int size);
	static IMPImage * createIMPImageFromFile(const std::string& fileName);
	static IMPImage * createIMPImageFromPak(const std::string& fileName, const std::string & pakName = "", bool firstReadPak = false);
	static IMPImage * createIMPImageFromImage(_image img);

	static IMPImage * createIMPImageFromFrame(IMPImage * impImage, int index);

	static void clearIMPImage(IMPImage* impImage);

	static _image loadImage(IMPImage* impImage, int index, int * xOffset = NULL, int * yOffset = NULL);
	static _image loadImageForTime(IMPImage* impImage, unsigned int time, int * xOffset = NULL, int * yOffset = NULL);
	static _image loadImageForDirection(IMPImage* impImage, int direction, unsigned int time, int * xOffset = NULL, int * yOffset = NULL);
	static _image loadImageForLastFrame(IMPImage* impImage, int direction, int * xOffset = NULL, int * yOffset = NULL);

private:
	static bool cmpIMGHead(IMPImage* img);

};