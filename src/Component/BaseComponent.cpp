#include "BaseComponent.h"

std::map<std::string, _shared_imp> BaseComponent::res;

BaseComponent::BaseComponent() 
{ 
	result = erNone; 
	priority = epComponent; 
}

BaseComponent::~BaseComponent() 
{ 
	freeResource(); 
}

void BaseComponent::initFromIniFileName(const std::string& fileName)
{
	std::unique_ptr<char[]> s;
	int len = 0;
	len = PakFile::readFile(fileName, s);
	if (s == nullptr || len == 0)
	{
		GameLog::write("no ini file: %s\n", fileName.c_str());
		return;
	}
	INIReader ini(s);
	initFromIni(ini);
}

void BaseComponent::initFromIniWithName(INIReader& ini, const std::string& fileName) 
{ 
	initFromIni(ini); 
}

_shared_imp BaseComponent::loadRes(const std::string& impName)
{
	if (impName.empty())
	{
		return nullptr;
	}
	auto iter = res.find(impName);
	if (iter != res.end())
	{
		return iter->second;
	}

	auto imp = IMP::createIMPImage(impName);
	res[impName] = imp;
	return imp;
}

void BaseComponent::tryCleanRes()
{
	auto iter = res.begin();
	while (iter != res.end())
	{
		if (iter->second.use_count() <= 1)
		{
			iter = res.erase(iter);
		}
		else
		{
			iter++;
		}
	}
}
