#include "SaveFileManager.h"

std::string SaveFileManager::_currentPath = SAVE_CURRENT_FOLDER;

std::string SaveFileManager::calculateFolderName(int index)
{
	if (index < 0)
	{
		return SAVE_CURRENT_FOLDER;
	}
	else
	{
		return convert::formatString(SAVE_FOLDER, index);
	}
}

std::vector<std::string> SaveFileManager::readFileList(int index)
{
	std::string listFileName = calculateFolderName(index) + SAVE_LIST_FILE;
	INIReader ini(listFileName);
	int count = ini.GetInteger(SAVE_LIST_SECTION, SAVE_LIST_COUNT_KEY, 0);
	std::vector<std::string> fileList;
	for (int i = 0; i < count; i++)
	{
		fileList.push_back(ini.Get(SAVE_LIST_SECTION, convert::formatString("%d", i), u8""));
	}
	return fileList;
}

void SaveFileManager::CopySaveFileTo(int index)
{
	std::string src = SAVE_CURRENT_FOLDER;
	std::string dst = convert::formatString(SAVE_FOLDER, index);
	auto fileList = readFileList(-1);
	File::copy(src + SAVE_LIST_FILE, dst + SAVE_LIST_FILE);
	for (size_t i = 0; i < fileList.size(); i++)
	{
		File::copy(src + fileList[i], dst + fileList[i]);
	}
}

void SaveFileManager::CopySaveFileFrom(int index)
{
	std::string dst = SAVE_CURRENT_FOLDER;
	std::string src = convert::formatString(SAVE_FOLDER, index);
	auto fileList = readFileList(index);
	File::copy(src + SAVE_LIST_FILE, dst + SAVE_LIST_FILE);
	for (size_t i = 0; i < fileList.size(); i++)
	{
		File::copy(src + fileList[i], dst + fileList[i]);
	}
}

std::string SaveFileManager::CurrentPath()
{
	return _currentPath;
}

void SaveFileManager::AppendFile(const std::string & fileName)
{
	auto fileList = readFileList(-1);

	bool found = false;
	for (size_t i = 0; i < fileList.size(); i++)
	{
		if (fileList[i] == fileName)
		{
			found = true;
			break;
		}
	}
	if (!found)
	{
		std::string iniName = SAVE_CURRENT_FOLDER;
		iniName += SAVE_LIST_FILE;
		INIReader ini(iniName);
		auto count = ini.GetInteger(SAVE_LIST_SECTION, SAVE_LIST_COUNT_KEY, 0);
		count++;
		ini.SetInteger(SAVE_LIST_SECTION, SAVE_LIST_COUNT_KEY, count);
		ini.Set(SAVE_LIST_SECTION, convert::formatString("%d", count - 1), fileName);
		ini.saveToFile(iniName);
	}
}
