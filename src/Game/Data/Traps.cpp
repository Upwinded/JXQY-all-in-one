#include "Traps.h"
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

void Traps::load()
{
	freeResource();
	loadDefinitions();
	loadTriggeredIndices();
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

void Traps::loadTriggeredIndices()
{
	triggeredIndices.clear();
	std::string fileName = TRAP_TRIGGERED_INDICES_INI;
	fileName = SaveFileManager::CurrentPath() + fileName;
	std::unique_ptr<char[]> fileData;
	int fileLength = 0;
	if (!File::readFile(
			fileName,
			fileData,
			fileLength,
			MaximumTriggeredIndicesFileBytes) ||
		fileData == nullptr || fileLength <= 0)
	{
		return;
	}
	auto terminatedData = std::make_unique<char[]>(
		static_cast<std::size_t>(fileLength) + 1);
	std::memcpy(
		terminatedData.get(),
		fileData.get(),
		static_cast<std::size_t>(fileLength));
	terminatedData[static_cast<std::size_t>(fileLength)] = '\0';
	INIReader triggeredReader(terminatedData);
	if (triggeredReader.ParseError() != 0)
	{
		return;
	}

	for (const std::string& key :
		triggeredReader.GetSectionKeys("init"))
	{
		const std::string value = triggeredReader.Get("init", key, "");
		char* end = nullptr;
		const long index = std::strtol(value.c_str(), &end, 10);
		if (end != value.c_str() && *end == '\0' &&
			index >= MinimumScriptIndex &&
			index <= MaximumScriptIndex)
		{
			triggeredIndices.insert(static_cast<int>(index));
		}
	}
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
