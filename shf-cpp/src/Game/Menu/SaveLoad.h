#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"

class SaveLoad :
	public Panel
{
public:
	SaveLoad(bool s, bool l);
	~SaveLoad();

private:
	bool save = true;
	bool load = true;

public:
	int index = -1;

	ListBox * listBox = NULL;

	ImageContainer * snap = NULL;
	Button * loadBtn = NULL;
	Button * saveBtn = NULL;
	Button * exitBtn = NULL;

	void init();

private:
	virtual void onEvent();
	virtual void freeResource();
	virtual bool onHandleEvent(AEvent * e);

};

