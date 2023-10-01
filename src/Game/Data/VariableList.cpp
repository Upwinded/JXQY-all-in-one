#include "VariableList.h"

VariableList::VariableList()
{
}


VariableList::~VariableList()
{
	freeResource();
}

void VariableList::load()
{
	freeResource();
	std::string fileName = SAVE_CURRENT_FOLDER;
	fileName += VARIABLE_INI;
	ini = std::make_shared<INIReader>(fileName);
}

void VariableList::save()
{
	std::string fileName = SAVE_CURRENT_FOLDER;
	fileName += VARIABLE_INI;
	ini->saveToFile(fileName);
}

std::string VariableList::get(const std::string & name)
{
	if (ini == nullptr)
	{
		return "";
	}
	return ini->Get(VARIABLE_SECTION, name, "");
}

int VariableList::getInteger(const std::string & name)
{
	if (ini == nullptr)
	{
		return 0;
	}

	int ret = ini->GetInteger(VARIABLE_SECTION, name, 0);
	return ret;
}

double VariableList::getReal(const std::string & name)
{
	if (ini == nullptr)
	{
		return 0.0;
	}
	return ini->GetReal(VARIABLE_SECTION, name, 0.0);
}

bool VariableList::getBoolean(const std::string & name)
{
	if (ini == nullptr)
	{
		return false;
	}
	return ini->GetBoolean(VARIABLE_SECTION, name, false);
}

void VariableList::set(const std::string & name, std::string & value)
{
	if (ini == nullptr)
	{
		return;
	}
	return ini->Set(VARIABLE_SECTION, name,  value);
}

void VariableList::setInteger(const std::string & name, int value)
{
	if (ini == nullptr)
	{
		return;
	}
	return ini->SetInteger(VARIABLE_SECTION, name, value);
}

void VariableList::setReal(const std::string & name, double value)
{
	if (ini == nullptr)
	{
		return;
	}
	return ini->SetReal(VARIABLE_SECTION, name, value);
}

void VariableList::setBoolean(const std::string & name, bool value)
{
	if (ini == nullptr)
	{
		return;
	}
	return ini->SetBoolean(VARIABLE_SECTION, name, value);
}

void VariableList::freeResource()
{
	if (ini != nullptr)
	{
		ini = nullptr;
	}
}
