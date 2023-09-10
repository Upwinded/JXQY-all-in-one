#pragma once
#include "../Element/Element.h"

class BaseComponent :
	public Element
{
public:
	BaseComponent() { result = erNone; priority = epComponent;  }
	virtual ~BaseComponent() { freeResource(); }

	virtual void freeResource() {}

	virtual void initFromIni(const std::string& fileName) {}

};

