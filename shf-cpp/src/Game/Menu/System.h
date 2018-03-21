#pragma once
#include "../../Component/Component.h"
class System :
	public Panel
{
public:
	System();
	~System();

	ImageContainer * title = NULL;
	Button * returnBtn = NULL;
	Button * saveloadBtn = NULL;
	Button * optionBtn = NULL;
	Button * quitBtn = NULL;

	void init();
private:
	virtual void freeResource();
	virtual void onEvent();
	virtual bool onHandleEvent(AEvent * e);
	virtual void saveScreen();

};

