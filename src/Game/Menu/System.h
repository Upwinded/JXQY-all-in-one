#pragma once
#include "../../Component/Component.h"
class System :
	public Panel
{
public:
	System();
	virtual ~System();

	ImageContainer * title = nullptr;
	Button * returnBtn = nullptr;
	Button * saveloadBtn = nullptr;
	Button * optionBtn = nullptr;
	Button * quitBtn = nullptr;

	void init();
private:
	void freeResource();
	virtual void onEvent();
	virtual bool onHandleEvent(AEvent * e);
	virtual void saveScreen();

};

