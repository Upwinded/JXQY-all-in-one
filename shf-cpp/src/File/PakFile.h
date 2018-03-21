#pragma once

#include "../Types/Types.h"
#include "../Engine/Engine.h"
#include "INIReader.h"

class PakFile
{
private:
	static Engine* engine;
	static PakFile pakFile;
	static PakFile * this_;

	static bool cmpPakHead(PakHead * pakHead);

	static unsigned int hashFileName(const std::string & fileName);

	static int findFileInPak(unsigned int fileID, const std::string & pakName);

	static int findFileInPak(const std::string & fileName, const std::string & pakName);
	static FilePos findFile(const std::string & fileName, int * index, const std::string & pakName = "", bool firstReadPak = false);

	static int unpak(char * inBuffer, int inLen, char * outBuffer, int outLen, int compressType);

	static bool readPak(const std::string & pakName, int index, char ** s, int * len);
	static bool readPak(const std::string & fileName, const std::string & pakName, char ** s, int * len);
public:
	PakFile();
	~PakFile();

	static std::vector<std::string> pakList;

	static int readFile(unsigned int fileID, char ** s);
	static int readFile(const std::string & fileName, char ** s);
	static int readFile(const std::string & fileName, char ** s, int * len, const std::string & pakName = "", bool firstReadPak = false);
};

