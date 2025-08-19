#pragma once
#include "../Element/Element.h"

class BaseComponent :
	public Element
{
public:
	BaseComponent();
	virtual ~BaseComponent();

	virtual void freeResource() {}

	void initFromIniFileName(const std::string& fileName);

	virtual void initFromIni(INIReader& ini) {}
	virtual void initFromIniWithName(INIReader& ini, const std::string& fileName);

	_shared_imp loadRes(const std::string& impName);

	void tryCleanRes();
private:
	static std::map<std::string, _shared_imp> res;

};
