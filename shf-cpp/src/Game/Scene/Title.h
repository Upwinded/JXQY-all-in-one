#pragma once
#include "../../Component/Component.h"
#include "../Menu/Menu.h"
class Title :
	public Panel
{
public:
	Title();
	~Title();

	Button * initBtn = NULL;
	Button * exitBtn = NULL;
	Button * loadBtn = NULL;
	Button * teamBtn = NULL;
	void playTitleBGM();
	void init();
	virtual void freeResource();
private:
	virtual void onEvent();
	virtual bool onInitial();
	virtual void onExit();
	virtual void onRun();
	virtual bool onHandleEvent(AEvent * e);
};

