#pragma once
#include "../../Component/Component.h"
class System :
	public Panel
{
public:
	System();
	virtual ~System();

	virtual void init() override;

	std::shared_ptr<ImageContainer> title = nullptr;
	std::shared_ptr<Button> returnBtn = nullptr;
	std::shared_ptr<Button> saveloadBtn = nullptr;
	std::shared_ptr<Button> optionBtn = nullptr;
	std::shared_ptr<Button> quitBtn = nullptr;

private:
	void freeResource();
	virtual void onEvent();
	virtual bool onHandleEvent(AEvent & e);
	virtual void saveScreen();

};

