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

	Button * initBtn = nullptr;
	Button * exitBtn = nullptr;
	Button * loadBtn = nullptr;
	Button * teamBtn = nullptr;
	Weather * weather = nullptr;

	void playTitleBGM();
	void init();
	void freeResource();
	void setTitleAlign();
private:
	virtual void onEvent();
	virtual bool onInitial();
	virtual void onExit();
	virtual void onRun();
	virtual bool onHandleEvent(AEvent * e);
};

