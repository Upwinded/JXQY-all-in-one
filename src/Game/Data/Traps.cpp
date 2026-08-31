#include "Traps.h"
#include "SaveIniPersistence.h"
#include "../../File/log.h"
#include "../../libconvert/libconvert.h"
#include "../GameManager/SaveFileManager.h"

#include <cstdlib>
#include <cstring>

Traps::Traps()
{
}

Traps::Traps(const std::shared_ptr<INIReader>& iniReader)
	: ini(iniReader)
{
	removeInvalidZeroKeys();
}


Traps::~Traps()
{
	freeResource();
}

bool Traps::load(std::string* failureReason)
{
	if (failureReason != nullptr)
	{
		failureReason->clear();
	}

	const std::string definitionsFileName =
		SaveFileManager::CurrentPath() + TRAPS_INI;
	std::shared_ptr<INIReader> loadedDefinitions;
	const SaveIniPersistence::ReadStatus definitionsStatus =
		SaveIniPersistence::read(
			definitionsFileName,
			loadedDefinitions);
	if (definitionsStatus == SaveIniPersistence::ReadStatus::Unreadable ||
		definitionsStatus == SaveIniPersistence::ReadStatus::Malformed)
	{
		if (failureReason != nullptr)
		{
			*failureReason = u8"陷阱定义文件 traps.ini 无法读取或格式错误";
		}
		return false;
	}
	if (loadedDefinitions == nullptr)
	{
		loadedDefinitions = std::make_shared<INIReader>();
	}

	std::set<int> loadedTriggeredIndices;
	if (!loadTriggeredIndices(
		loadedTriggeredIndices,
		failureReason))
	{
		return false;
	}

	ini = std::move(loadedDefinitions);
	triggeredIndices = std::move(loadedTriggeredIndices);
	removeInvalidZeroKeys();
	return true;
}

bool Traps::loadInitialTemplate(std::string* failureReason)
{
	if (failureReason != nullptr)
	{
		failureReason->clear();
	}

	std::shared_ptr<INIReader> loadedDefinitions;
	const SaveIniPersistence::ReadStatus definitionsStatus =
		SaveIniPersistence::read(
			std::string(INI_SAVE_FOLDER) + TRAPS_INI,
			loadedDefinitions);
	if (definitionsStatus != SaveIniPersistence::ReadStatus::Loaded ||
		loadedDefinitions == nullptr)
	{
		if (failureReason != nullptr)
		{
			*failureReason = u8"初始机关模板 ini/save/traps.ini 不存在、为空或格式错误";
		}
		return false;
	}

	ini = std::move(loadedDefinitions);
	triggeredIndices.clear();
	removeInvalidZeroKeys();
	return true;
}

void Traps::loadDefinitions()
{
	std::string fileName = TRAPS_INI;
	fileName = SaveFileManager::CurrentPath() + fileName;
	ini = std::make_shared<INIReader>(fileName);
	removeInvalidZeroKeys();
}

bool Traps::save()
{
	return saveDefinitions() && saveTriggeredIndices();
}

void Traps::resetToEmpty()
{
	ini = std::make_shared<INIReader>();
	triggeredIndices.clear();
}

bool Traps::saveDefinitions()
{
	if (ini == nullptr)
	{
		return false;
	}
	removeInvalidZeroKeys();
	std::string fileName = TRAPS_INI;
	fileName = SaveFileManager::CurrentPath() + fileName;
	return ini->saveToFile(fileName);
}

void Traps::freeResource()
{
	if (ini != nullptr)
	{
		ini = nullptr;
	}
	triggeredIndices.clear();
}

std::string Traps::get(const std::string & mapName, int index)
{
	if (!isValidIndex(index))
	{
		GameLog::write("Traps::get ignored invalid trap index %d for map %s", index, mapName.c_str());
		return "";
	}
	if (ini == nullptr)
	{
		return "";
	}
	std::string name = convert::formatString("%d", index);
	return ini->Get(mapName, name, "");
}

void Traps::set(const std::string & mapName, int index, const std::string & value)
{
	if (!isValidIndex(index))
	{
		GameLog::write("Traps::set ignored invalid trap index %d for map %s", index, mapName.c_str());
		return;
	}
	if (ini == nullptr)
	{
		std::string fileName = TRAPS_INI;
		fileName = SaveFileManager::CurrentPath() + fileName;
		ini = std::make_shared<INIReader>(fileName);
	}
	// A valid edit to a map section also removes any legacy key 0 so a later
	// save cannot preserve the meaningless entry.
	ini->Remove(mapName, "0");
	std::string name = convert::formatString("%d", index);
	if (value.empty())
	{
		ini->Remove(mapName, name);
	}
	else
	{
		ini->Set(mapName, name, value);
	}
}

bool Traps::hasTriggered(int index) const
{
	return isValidIndex(index) &&
		triggeredIndices.find(index) != triggeredIndices.end();
}

void Traps::markTriggered(int index)
{
	if (isValidIndex(index))
	{
		triggeredIndices.insert(index);
	}
}

void Traps::reactivate(int index)
{
	if (isValidIndex(index))
	{
		triggeredIndices.erase(index);
	}
}

void Traps::beginMapVisit()
{
	if (ini == nullptr)
	{
		loadDefinitions();
	}
	triggeredIndices.clear();
}

bool Traps::isValidIndex(int index)
{
	return index >= MinimumScriptIndex && index <= MaximumScriptIndex;
}

void Traps::removeInvalidZeroKeys()
{
	if (ini == nullptr)
	{
		return;
	}

	for (const std::string& sectionName : ini->GetSectionNames())
	{
		ini->Remove(sectionName, "0");
	}
}

bool Traps::loadTriggeredIndices(
	std::set<int>& loadedIndices,
	std::string* failureReason)
{
	loadedIndices.clear();
	std::string fileName = TRAP_TRIGGERED_INDICES_INI;
	fileName = SaveFileManager::CurrentPath() + fileName;
	std::shared_ptr<INIReader> triggeredReader;
	const SaveIniPersistence::ReadStatus status =
		SaveIniPersistence::read(
			fileName,
			triggeredReader,
			MaximumTriggeredIndicesFileBytes);
	if (status == SaveIniPersistence::ReadStatus::Missing ||
		status == SaveIniPersistence::ReadStatus::Empty)
	{
		return true;
	}
	if (status == SaveIniPersistence::ReadStatus::Loaded &&
		triggeredReader != nullptr &&
		triggeredReader->GetSectionNames().empty())
	{
		// The canonical writer emits an empty [init] section when no trap has
		// fired. INIReader stores sections only after seeing a key, so that
		// valid file is represented as an empty parsed document.
		return true;
	}
	if (status != SaveIniPersistence::ReadStatus::Loaded ||
		triggeredReader == nullptr ||
		!triggeredReader->HasSection("init"))
	{
		if (failureReason != nullptr)
		{
			*failureReason =
				u8"陷阱触发状态文件 trapindexignore.ini 无法读取或格式错误";
		}
		return false;
	}

	for (const std::string& key :
		triggeredReader->GetSectionKeys("init"))
	{
		const std::string value = triggeredReader->Get("init", key, "");
		char* end = nullptr;
		const long index = std::strtol(value.c_str(), &end, 10);
		if (end == value.c_str() || *end != '\0' ||
			index < MinimumScriptIndex ||
			index > MaximumScriptIndex)
		{
			GameLog::write(
				"Traps: ignored invalid persisted triggered index %s\n",
				value.c_str());
			continue;
		}
		loadedIndices.insert(static_cast<int>(index));
	}
	return true;
}

bool Traps::saveTriggeredIndices() const
{
	std::string content = "[init]\r\n";
	int entryIndex = 0;
	for (int trapIndex : triggeredIndices)
	{
		content += std::to_string(entryIndex++) + "=" +
			std::to_string(trapIndex) + "\r\n";
	}
	std::string fileName = TRAP_TRIGGERED_INDICES_INI;
	fileName = SaveFileManager::CurrentPath() + fileName;
	return File::writeFileChecked(
		fileName,
		content.data(),
		static_cast<int>(content.size()));
}
