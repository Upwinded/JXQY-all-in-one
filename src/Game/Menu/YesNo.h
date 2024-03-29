#pragma once
#include "../../Component/Component.h"
class YesNo :
	public Panel
{
public:
	YesNo(const std::string & s = "");
	virtual ~YesNo();

	std::shared_ptr<Button> yes = nullptr;
	std::shared_ptr<Button> no = nullptr;
	std::shared_ptr<Label> label = nullptr;

	void init(const std::string & s = "");

private:

	void freeResource();
	virtual void onEvent();
	virtual bool onHandleEvent(AEvent & e);
};

