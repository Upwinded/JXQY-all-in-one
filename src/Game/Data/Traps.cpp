#include "Traps.h"



Traps::Traps()
{
}


Traps::~Traps()
{
	freeResource();
}

void Traps::load()
{
	freeResource();
	std::string fileName = TRAPS_INI;
	fileName = SAVE_CURRENT_FOLDER + fileName;
	ini = new INIReader(fileName);
}

void Traps::save()
{
	if (ini == nullptr)
	{
		return;
	}
	std::string fileName = TRAPS_INI;
	fileName = SAVE_CURRENT_FOLDER + fileName;
	ini->saveToFile(fileName);
}

void Traps::freeResource()
{
	if (ini != nullptr)
	{
		delete ini;
		ini = nullptr;
	}
}

std::string Traps::get(const std::string & mapName, int index)
{
	if (ini == nullptr)
	{
		return "";
	}
	std::string name = convert::formatString("%d", index);
	return ini->Get(mapName, name, "");
}

void Traps::set(const std::string & mapName, int index, const std::string & value)
{
	if (ini == nullptr)
	{
		std::string fileName = TRAPS_INI;
		fileName = SAVE_CURRENT_FOLDER + fileName;
		ini = new INIReader(fileName);
	}
	std::string name = convert::formatString("%d", index);
	ini->Set(mapName, name, value);
}
