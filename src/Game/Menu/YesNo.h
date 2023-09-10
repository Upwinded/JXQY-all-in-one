#pragma once
#include "../../Component/Component.h"
class YesNo :
	public Panel
{
public:
	YesNo(const std::string & s = "");
	virtual ~YesNo();

	Button * yes = nullptr;
	Button * no = nullptr;
	Label * label = nullptr;

	void init(const std::string & s = "");

private:

	void freeResource();
	virtual void onEvent();
	virtual bool onHandleEvent(AEvent * e);
};

