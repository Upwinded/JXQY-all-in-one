#pragma once
#include <vector>
#include "../../File/INIReader.h"
#include "../../Engine/Engine.h"
#include "../GameTypes.h"
#include "../../libconvert/libconvert.h"

class SaveFileManager
{
public:
	SaveFileManager() {}
	virtual ~SaveFileManager() {}

private:
	static std::string calculateFolderName(int index);
	static std::vector<std::string> readFileList(int index);
	static std::string _currentPath;
public:
	//index=-1,为当前运行文件夹（save/game)
	//index=0,为新游戏数据文件夹（save/rpg0)
	//index>0,为存档文件夹（save/rpg1--rpg7)
	static void CopySaveFileTo(int index);
	static void CopySaveFileFrom(int index);
	static std::string CurrentPath();
	static void AppendFile(const std::string & fileName);

};

