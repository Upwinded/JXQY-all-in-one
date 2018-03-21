#pragma once
#include "../../Component/Component.h"
#include "../Config/Config.h"

class Option :
	public Panel
{
public:
	Option();
	~Option();

	Config * config = NULL;

	Button * rtnBtn = NULL;
	Scrollbar * music = NULL;
	Scrollbar * sound = NULL;
	Scrollbar * speed = NULL;
	CheckBox * player = NULL;
	CheckBox * dyLoad = NULL;
	CheckBox * shadow = NULL;
	ImageContainer * playerBg = NULL;
	ImageContainer * dyLoadBg = NULL;
	ImageContainer * shadowBg = NULL;

	CheckBox * musicCB = NULL;
	CheckBox * soundCB = NULL;
	CheckBox * speedCB = NULL;

private:
	void init();
	virtual void freeResource();

	int musicPos = 0;
	int soundPos = 0;

	virtual void onEvent();

	virtual bool onHandleEvent(AEvent * e);
};

