#pragma once
#include "../../Component/Component.h"
#include "../Config/Config.h"

class Option :
	public Panel
{
public:
	Option();
	virtual ~Option();

	Config * config = nullptr;

	Button * rtnBtn = nullptr;
	Scrollbar * music = nullptr;
	Scrollbar * sound = nullptr;
	Scrollbar * speed = nullptr;
	CheckBox * player = nullptr;
	CheckBox * dyLoad = nullptr;
	CheckBox * shadow = nullptr;
	ImageContainer * playerBg = nullptr;
	ImageContainer * dyLoadBg = nullptr;
	ImageContainer * shadowBg = nullptr;

	CheckBox * musicCB = nullptr;
	CheckBox * soundCB = nullptr;
	CheckBox * speedCB = nullptr;

private:
	void init();
	void freeResource();

	int musicPos = 0;
	int soundPos = 0;

	virtual void onEvent();

	virtual bool onHandleEvent(AEvent * e);
};

