#include "SaveFileManager.h"
#include "../../File/File.h"
#include "../../File/log.h"
#include "../../libconvert/libconvert.h"
#include <algorithm>
#include <array>

namespace
{
const std::vector<std::string> LegacySaveListFiles = { SAVE_LIST_FILE };
constexpr const char* SaveBuildGeneration =
	"save\\game_build";
constexpr const char* LoadCandidateGeneration =
	"save\\load_candidate";

bool isSaveGenerationDirectory(
	const std::string& directoryName,
	std::string& normalizedDirectory)
{
	return SaveGeneration::NormalizeGenerationDirectory(
		directoryName,
		normalizedDirectory);
}

bool isInternalScratchGeneration(
	const std::string& normalizedDirectory)
{
	return normalizedDirectory == "save\\game_build" ||
		normalizedDirectory == "save\\load_candidate";
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

bool isSecondarySaveDestination(
	const std::string& directoryName,
	std::string& normalizedDirectory)
{
	if (!SaveGeneration::NormalizeGenerationDirectory(
			directoryName,
			normalizedDirectory))
	{
		return false;
	}
	if (normalizedDirectory == "save\\rpg_auto")
	{
		return true;
	}
	constexpr const char* SlotPrefix = "save\\rpg";
	if (normalizedDirectory.rfind(SlotPrefix, 0) != 0)
	{
		return false;
	}
	const std::string indexText =
		normalizedDirectory.substr(
			std::char_traits<char>::length(SlotPrefix));
	if (indexText.size() != 1 ||
		indexText.front() < '1' ||
		indexText.front() > '7')
	{
		return false;
	}
	return true;
}

bool copySaveDirectory(
	const std::string& src,
	const std::string& dst,
	bool recoverSource = true)
{
	if (recoverSource && !File::recoverDirectoryCopy(src))
	{
		GameLog::write("SaveFileManager: source recovery failed %s\n", src.c_str());
		return false;
	}
	if (!File::fileExist(src + GLOBAL_INI))
	{
		GameLog::write("SaveFileManager: source save missing %s\n", (src + GLOBAL_INI).c_str());
		return false;
	}
	bool ok = File::copyDirectoryFiles(src, dst, LegacySaveListFiles);
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

SaveFileManager::ScratchGenerationScope::
	ScratchGenerationScope(
		const std::string& generationDirectory)
{
	if (SaveGeneration::NormalizeGenerationDirectory(
			generationDirectory,
			directory) &&
		isInternalScratchGeneration(directory))
	{
		active = true;
	}
}

SaveFileManager::ScratchGenerationScope::
	~ScratchGenerationScope()
{
	if (!active)
	{
		return;
	}
	const bool recovered =
		File::recoverDirectoryCopy(directory);
	const bool cleared =
		recovered &&
		File::clearDirectoryFiles(directory);
	if (!cleared)
	{
		GameLog::write(
			"SaveFileManager: scratch generation cleanup failed %s\n",
			directory.c_str());
	}
}

bool SaveFileManager::CopySaveGenerationWithinLimits(
	const std::string& sourceDirectory,
	const std::string& destinationDirectory,
	const SaveGenerationLimits& limits,
	const std::vector<std::string>& excludedFileNames,
	const std::function<bool()>& cancellationRequested)
{
	std::string normalizedDestination;
	if (!SaveGeneration::NormalizeGenerationDirectory(
			destinationDirectory,
			normalizedDestination) ||
		!isInternalScratchGeneration(
			normalizedDestination))
	{
		GameLog::write(
			"SaveFileManager: bounded clone destination is not internal scratch %s\n",
			destinationDirectory.c_str());
		return false;
	}
	std::string normalizedSource;
	if (SaveGeneration::NormalizeGenerationDirectory(
			sourceDirectory,
			normalizedSource))
	{
		if (normalizedSource == normalizedDestination ||
			!File::recoverDirectoryCopy(
				normalizedSource))
		{
			return false;
		}
	}
	File::DirectoryCopyLimits copyLimits;
	copyLimits.maximumFileCount =
		limits.maximumFileCount;
	copyLimits.maximumTotalBytes =
		limits.maximumTotalBytes;
	copyLimits.maximumSingleFileBytes =
		limits.maximumSingleFileBytes;
	copyLimits.cancellationRequested =
		cancellationRequested;
	return File::copyDirectoryFiles(
		sourceDirectory,
		destinationDirectory,
		excludedFileNames,
		{},
		copyLimits);
}

SaveGenerationResult
SaveFileManager::PublishPreparedLoadCandidateToCurrent(
	const SaveGenerationLimits& limits,
	const std::function<bool()>& cancellationRequested)
{
	OperationScope operation;
	SaveGenerationResult result;
	result.sourceDirectory = LoadCandidateGeneration;
	result.destinationDirectory = SAVE_CURRENT_FOLDER;
	if (limits.maximumFileCount == 0 ||
		limits.maximumTotalBytes == 0 ||
		limits.maximumSingleFileBytes <= 0)
	{
		result.error = SaveGenerationError::InvalidLimits;
		return result;
	}
	const auto cancellationIsRequested =
		[&cancellationRequested]() noexcept
		{
			if (!cancellationRequested)
			{
				return false;
			}
			try
			{
				return cancellationRequested();
			}
			catch (...)
			{
				return true;
			}
		};
	if (cancellationIsRequested())
	{
		result.error = SaveGenerationError::Cancelled;
		return result;
	}
	if (!File::recoverDirectoryCopy(
			LoadCandidateGeneration))
	{
		result.error = SaveGenerationError::SourceRecoveryFailed;
		result.errorPath = LoadCandidateGeneration;
		return result;
	}
	const std::string gameIniPath =
		withTrailingSeparator(LoadCandidateGeneration) +
		GLOBAL_INI;
	if (!File::fileExist(gameIniPath))
	{
		result.error = SaveGenerationError::GameIniMissing;
		result.errorPath = gameIniPath;
		return result;
	}
	const std::string legacyListPath =
		withTrailingSeparator(LoadCandidateGeneration) +
		SAVE_LIST_FILE;
	if (File::fileExist(legacyListPath))
	{
		// The prepare stage already excludes this legacy index. Refuse an
		// unprepared scratch directory before moving it so a failed publication
		// cannot consume or partially rewrite the caller's candidate.
		result.error = SaveGenerationError::PublicationFailed;
		result.errorPath = legacyListPath;
		return result;
	}
	if (cancellationIsRequested())
	{
		result.error = SaveGenerationError::Cancelled;
		return result;
	}
	if (!File::recoverDirectoryCopy(SAVE_CURRENT_FOLDER))
	{
		result.error = SaveGenerationError::DestinationRecoveryFailed;
		result.errorPath = SAVE_CURRENT_FOLDER;
		return result;
	}
	File::DirectoryCopyLimits copyLimits;
	copyLimits.maximumFileCount = limits.maximumFileCount;
	copyLimits.maximumTotalBytes = limits.maximumTotalBytes;
	copyLimits.maximumSingleFileBytes =
		limits.maximumSingleFileBytes;
	copyLimits.cancellationRequested = cancellationRequested;
	if (!File::promotePreparedScratchDirectory(
			LoadCandidateGeneration,
			SAVE_CURRENT_FOLDER,
			{},
			copyLimits))
	{
		result.error = cancellationIsRequested()
			? SaveGenerationError::Cancelled
			: SaveGenerationError::PublicationFailed;
		result.errorPath = SAVE_CURRENT_FOLDER;
	}
	return result;
}

SaveGenerationResult
SaveFileManager::PublishPreparedSaveGeneration(
	const std::string& draftDirectory,
	const std::string& destinationDirectory,
	const SaveGenerationLimits& limits,
	const std::vector<std::string>& excludedFileNames,
	const std::function<bool()>& cancellationRequested)
{
	OperationScope operation;
	SaveGenerationResult result;
	result.sourceDirectory = draftDirectory;
	result.destinationDirectory = destinationDirectory;

	std::string normalizedDraft;
	std::string normalizedDestination;
	if (!SaveGeneration::NormalizeGenerationDirectory(
			draftDirectory,
			normalizedDraft) ||
		normalizedDraft != SaveBuildGeneration)
	{
		result.error = SaveGenerationError::UnsafeSourceDirectory;
		return result;
	}
	if (!SaveGeneration::NormalizeGenerationDirectory(
			destinationDirectory,
			normalizedDestination))
	{
		result.error = SaveGenerationError::UnsafeDestinationDirectory;
		return result;
	}
	if (normalizedDestination != "save\\game")
	{
		std::string normalizedSecondary;
		if (!isSecondarySaveDestination(
				normalizedDestination,
				normalizedSecondary))
		{
			result.error = SaveGenerationError::UnsafeDestinationDirectory;
			return result;
		}
		normalizedDestination = std::move(normalizedSecondary);
	}
	if (limits.maximumFileCount == 0 ||
		limits.maximumTotalBytes == 0 ||
		limits.maximumSingleFileBytes <= 0)
	{
		result.error = SaveGenerationError::InvalidLimits;
		return result;
	}
	const auto cancellationIsRequested =
		[&cancellationRequested]() noexcept
		{
			if (!cancellationRequested)
			{
				return false;
			}
			try
			{
				return cancellationRequested();
			}
			catch (...)
			{
				return true;
			}
		};
	if (cancellationIsRequested())
	{
		result.error = SaveGenerationError::Cancelled;
		return result;
	}
	if (!File::recoverDirectoryCopy(normalizedDraft))
	{
		result.error = SaveGenerationError::SourceRecoveryFailed;
		result.errorPath = normalizedDraft;
		return result;
	}
	const std::string gameIniPath =
		withTrailingSeparator(normalizedDraft) + GLOBAL_INI;
	if (!File::fileExist(gameIniPath))
	{
		result.error = SaveGenerationError::GameIniMissing;
		result.errorPath = gameIniPath;
		return result;
	}
	if (!File::recoverDirectoryCopy(normalizedDestination))
	{
		result.error = SaveGenerationError::DestinationRecoveryFailed;
		result.errorPath = normalizedDestination;
		return result;
	}

	File::DirectoryCopyLimits copyLimits;
	copyLimits.maximumFileCount = limits.maximumFileCount;
	copyLimits.maximumTotalBytes = limits.maximumTotalBytes;
	copyLimits.maximumSingleFileBytes =
		limits.maximumSingleFileBytes;
	copyLimits.cancellationRequested = cancellationRequested;
	if (!File::copyDirectoryFiles(
			normalizedDraft,
			normalizedDestination,
			excludedFileNames,
			{},
			copyLimits))
	{
		result.error = cancellationIsRequested()
			? SaveGenerationError::Cancelled
			: SaveGenerationError::PublicationFailed;
		result.errorPath = normalizedDestination;
	}
	return result;
}

bool SaveFileManager::RecoverInterruptedSaveOperations()
{
	OperationScope operation;
	const std::array<std::string, 11> directories =
	{
		SAVE_CURRENT_FOLDER,
		SaveBuildGeneration,
		LoadCandidateGeneration,
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
	if (recovered)
	{
		recovered =
			File::clearDirectoryFiles(SaveBuildGeneration) &&
			File::clearDirectoryFiles(LoadCandidateGeneration);
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

bool SaveFileManager::CopySaveFileTo(int index)
{
	if (index < 1 || index > 7)
	{
		return false;
	}
	std::string src = SAVE_CURRENT_FOLDER;
	std::string dst = convert::formatString(SAVE_FOLDER, index);
	return copySaveDirectory(src, dst);
}

bool SaveFileManager::CopySaveFileFrom(int index)
{
	std::string dst = SAVE_CURRENT_FOLDER;
	GameLog::write("SaveFileManager: load save index %d\n", index);
	if (index == 0)
	{
		return copySaveDirectory(
			INI_SAVE_FOLDER, dst, false);
	}
	if (index < 1 || index > 7)
	{
		return false;
	}
	return copySaveDirectory(
		convert::formatString(SAVE_FOLDER, index),
		dst);
}

bool SaveFileManager::CopySaveFileToAuto()
{
	std::string src = SAVE_CURRENT_FOLDER;
	std::string dst = SAVE_AUTO_FOLDER;
	return copySaveDirectory(src, dst);
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
	return File::recoverDirectoryCopy(folderName) && File::fileExist(folderName + GLOBAL_INI);
}

bool SaveFileManager::ClearAllSaveData()
{
	bool ok = true;
	for (int index = 1; index <= 7; index++)
	{
		const std::string folderName = convert::formatString(SAVE_FOLDER, index);
		ok = File::recoverDirectoryCopy(folderName) && ok;
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
