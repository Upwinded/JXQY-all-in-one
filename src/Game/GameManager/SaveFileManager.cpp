#include "SaveFileManager.h"
#include "../../File/File.h"
#include "../../File/log.h"
#include "../../libconvert/libconvert.h"
#include <algorithm>
#include <array>

namespace
{
const std::vector<std::string> LegacySaveListFiles = { SAVE_LIST_FILE };
bool isSaveGenerationDirectory(
	const std::string& directoryName,
	std::string& normalizedDirectory)
{
	return SaveGeneration::NormalizeGenerationDirectory(
		directoryName,
		normalizedDirectory);
}

std::string withTrailingSeparator(
	std::string directoryName)
{
	if (!directoryName.empty() &&
		directoryName.back() != '\\' &&
		directoryName.back() != '/')
	{
		directoryName.push_back('\\');
	}
	return directoryName;
}

bool copySaveDirectory(
	const std::string& src,
	const std::string& dst)
{
	if (!File::fileExist(src + GLOBAL_INI))
	{
		GameLog::write("SaveFileManager: source save missing %s\n", (src + GLOBAL_INI).c_str());
		return false;
	}
	bool ok = File::overwriteDirectoryFiles(src, dst, LegacySaveListFiles);
	GameLog::write("SaveFileManager: copy %s -> %s %s\n", src.c_str(), dst.c_str(), ok ? "ok" : "failed");
	return ok;
}

std::string lowercaseAscii(std::string value)
{
	for (char& character : value)
	{
		if (character >= 'A' && character <= 'Z')
		{
			character = static_cast<char>(
				character + ('a' - 'A'));
		}
	}
	return value;
}

bool isIndexedCoreSaveFileName(
	const std::string& lowerFileName,
	const char* prefix)
{
	const std::string prefixText(prefix);
	if (lowerFileName.rfind(prefixText, 0) != 0 ||
		lowerFileName.size() <= prefixText.size())
	{
		return false;
	}
	const std::string suffix =
		lowerFileName.substr(prefixText.size());
	if (suffix == ".ini")
	{
		return true;
	}
	if (suffix.size() <= 4 ||
		suffix.substr(suffix.size() - 4) != ".ini")
	{
		return false;
	}
	return std::all_of(
		suffix.begin(),
		suffix.end() - 4,
		[](char character)
		{
			return character >= '0' && character <= '9';
		});
}

bool isCoreSaveFileName(const std::string& lowerFileName)
{
	static const std::array<const char*, 8> FixedCoreSaveFileNames = {
		GLOBAL_INI,
		SAVE_LIST_FILE,
		MEMO_INI,
		VARIABLE_INI,
		TRAPS_INI,
		TRAP_TRIGGERED_INDICES_INI,
		EFFECT_INI,
		PARTNER_IDX_INI
	};
	if (std::any_of(
			FixedCoreSaveFileNames.begin(),
			FixedCoreSaveFileNames.end(),
			[&lowerFileName](const char* fileName)
			{
				return lowerFileName == lowercaseAscii(fileName);
			}))
	{
		return true;
	}
	return isIndexedCoreSaveFileName(
			lowerFileName, PLAYER_INI_NAME) ||
		isIndexedCoreSaveFileName(
			lowerFileName, PARTNER_INI_NAME) ||
		isIndexedCoreSaveFileName(
			lowerFileName, MAGIC_INI_NAME) ||
		isIndexedCoreSaveFileName(
			lowerFileName, GOODS_INI_NAME);
}
}

bool SaveFileManager::IsSafeEntityListFileName(
	const std::string& fileName)
{
	if (fileName.empty() ||
		fileName.find('/') != std::string::npos ||
		fileName.find('\\') != std::string::npos ||
		!File::isSafeResourcePath(fileName))
	{
		return false;
	}
	return !isCoreSaveFileName(lowercaseAscii(fileName));
}

bool SaveFileManager::AreEntityListFileNamesDistinct(
	const std::string& npcFileName,
	const std::string& objectFileName)
{
	return npcFileName.empty() || objectFileName.empty() ||
		lowercaseAscii(npcFileName) !=
			lowercaseAscii(objectFileName);
}

SaveFileManager::CurrentPathScope::CurrentPathScope(
	const std::string& generationDirectory)
	: pathLock(SaveFileManager::_currentPathMutex)
{
	std::string normalizedDirectory;
	if (!isSaveGenerationDirectory(
			generationDirectory,
			normalizedDirectory))
	{
		pathLock.unlock();
		return;
	}
	previousPath = SaveFileManager::_currentPath;
	SaveFileManager::_currentPath =
		withTrailingSeparator(normalizedDirectory);
	active = true;
}

SaveFileManager::CurrentPathScope::~CurrentPathScope()
{
	if (active)
	{
		SaveFileManager::_currentPath = previousPath;
	}
}

bool SaveFileManager::RecoverInterruptedSaveOperations()
{
	OperationScope operation;
	const std::array<std::string, 9> directories =
	{
		SAVE_CURRENT_FOLDER,
		SAVE_AUTO_FOLDER,
		convert::formatString(SAVE_FOLDER, 1),
		convert::formatString(SAVE_FOLDER, 2),
		convert::formatString(SAVE_FOLDER, 3),
		convert::formatString(SAVE_FOLDER, 4),
		convert::formatString(SAVE_FOLDER, 5),
		convert::formatString(SAVE_FOLDER, 6),
		convert::formatString(SAVE_FOLDER, 7)
	};
	bool recovered = true;
	for (const std::string& directory : directories)
	{
		if (!File::recoverDirectoryCopy(directory))
		{
			GameLog::write(
				"SaveFileManager: save directory recovery failed %s\n",
				directory.c_str());
			recovered = false;
		}
	}
	return recovered;
}

bool SaveFileManager::ReadNpcObjFile(const std::string& fileName,
                                      std::unique_ptr<char[]>& data,
                                      int& len,
                                      std::string* loadedPath,
                                      int maximumBytes)
{
	data = nullptr;
	len = 0;
	if (fileName.empty())
	{
		return false;
	}

	//优先从当前显式 generation 读取；默认仍为 save\game\。
	const std::string savePath = CurrentPath() + fileName;
	if (File::readFile(savePath, data, len, maximumBytes) && data != nullptr)
	{
		if (loadedPath) *loadedPath = savePath;
		return true;
	}

	//回退到 ini\save\ 读取原始模板
	const std::string templatePath = std::string(INI_SAVE_FOLDER) + fileName;
	if (File::readFile(templatePath, data, len, maximumBytes) && data != nullptr)
	{
		if (loadedPath) *loadedPath = templatePath;
		return true;
	}

	len = 0;
	return false;
}

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

bool SaveFileManager::CopySaveFileTo(int index, const std::function<bool()>& cancellationRequested)
{
	OperationScope operation;
	if (index < 1 || index > 7)
	{
		return false;
	}
	std::string src = SAVE_CURRENT_FOLDER;
	std::string dst = convert::formatString(SAVE_FOLDER, index);
	if (!File::fileExist(src + GLOBAL_INI)) return false;
	const bool saved = File::overwriteDirectoryFiles(src, dst, LegacySaveListFiles, cancellationRequested);
	GameLog::write("SaveFileManager: direct save %s %s\n", dst.c_str(), saved ? "ok" : "failed");
	return saved;
}

bool SaveFileManager::CopySaveFileFrom(int index)
{
	std::string dst = SAVE_CURRENT_FOLDER;
	GameLog::write("SaveFileManager: load save index %d\n", index);
	if (index == 0)
	{
		return copySaveDirectory(
			INI_SAVE_FOLDER, dst);
	}
	if (index < 1 || index > 7)
	{
		return false;
	}
	return copySaveDirectory(
		convert::formatString(SAVE_FOLDER, index),
		dst);
}

bool SaveFileManager::CopySaveFileToAuto(const std::function<bool()>& cancellationRequested)
{
	OperationScope operation;
	std::string src = SAVE_CURRENT_FOLDER;
	std::string dst = SAVE_AUTO_FOLDER;
	if (!File::fileExist(src + GLOBAL_INI)) return false;
	const bool saved = File::overwriteDirectoryFiles(src, dst, LegacySaveListFiles, cancellationRequested);
	GameLog::write("SaveFileManager: direct save %s %s\n", dst.c_str(), saved ? "ok" : "failed");
	return saved;
}

bool SaveFileManager::CopySaveFileFromAuto()
{
	std::string dst = SAVE_CURRENT_FOLDER;
	std::string src = SAVE_AUTO_FOLDER;
	return copySaveDirectory(src, dst);
}

bool SaveFileManager::HasSaveFile(int index)
{
	if (index == 0)
	{
		return File::fileExist(
			std::string(INI_SAVE_FOLDER) + GLOBAL_INI);
	}
	if (index < 1 || index > 7)
	{
		return false;
	}
	const std::string folderName = calculateFolderName(index);
	return File::fileExist(folderName + GLOBAL_INI);
}

bool SaveFileManager::ClearAllSaveData()
{
	bool ok = true;
	for (int index = 1; index <= 7; index++)
	{
		const std::string folderName = convert::formatString(SAVE_FOLDER, index);
		ok = File::clearDirectoryFiles(folderName) && ok;
		ok = File::removeFile(std::string(SHOT_FOLDER) + convert::formatString(SHOT_PNG, index)) && ok;
		ok = File::removeFile(std::string(SHOT_FOLDER) + convert::formatString(LEGACY_SHOT_BMP, index)) && ok;
	}
	return ok;
}

void SaveFileManager::AppendFile(const std::string & fileName)
{
	(void)fileName;
}
