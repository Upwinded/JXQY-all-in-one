#pragma once
#include "../../Component/Component.h"
#include "../Menu/Menu.h"
#include "../../Weather/Weather.h"

class Title :
	public Panel
{
public:
	Title();
	virtual ~Title();

	std::shared_ptr<Button> initBtn = nullptr;
	std::shared_ptr<Button> exitBtn = nullptr;
	std::shared_ptr<Button> loadBtn = nullptr;
	std::shared_ptr<Button> teamBtn = nullptr;
	std::shared_ptr<Weather> weather = nullptr;

	void playTitleBGM();
	void init();
	void freeResource();
	void setTitleAlign();
private:
	virtual void onEvent();
	virtual bool onInitial();
	virtual void onExit();
	virtual void onRun();
	virtual bool onHandleEvent(AEvent & e);
};

