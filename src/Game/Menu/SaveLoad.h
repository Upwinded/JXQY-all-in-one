#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"

class SaveLoad :
	public Panel
{
public:
	SaveLoad(bool s, bool l);
	virtual ~SaveLoad();

private:
	bool save = true;
	bool load = true;

public:
	int index = -1;

	ListBox * listBox = nullptr;

	ImageContainer * snap = nullptr;
	Button * loadBtn = nullptr;
	Button * saveBtn = nullptr;
	Button * exitBtn = nullptr;

	void init();

private:
	virtual void onEvent();
	void freeResource();
	virtual bool onHandleEvent(AEvent * e);

};

