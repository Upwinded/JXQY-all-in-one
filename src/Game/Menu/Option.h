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

	std::shared_ptr<Button> rtnBtn = nullptr;
	std::shared_ptr<Scrollbar> music = nullptr;
	std::shared_ptr<Scrollbar> sound = nullptr;
	std::shared_ptr<Scrollbar> speed = nullptr;
	std::shared_ptr<CheckBox> player = nullptr;
	std::shared_ptr<CheckBox> dyLoad = nullptr;
	std::shared_ptr<CheckBox> shadow = nullptr;
	std::shared_ptr<ImageContainer> playerBg = nullptr;
	std::shared_ptr<ImageContainer> dyLoadBg = nullptr;
	std::shared_ptr<ImageContainer> shadowBg = nullptr;

	std::shared_ptr<CheckBox> musicCB = nullptr;
	std::shared_ptr<CheckBox> soundCB = nullptr;
	std::shared_ptr<CheckBox> speedCB = nullptr;

private:
	void init();
	void freeResource();

	int musicPos = 0;
	int soundPos = 0;

	virtual void onEvent();

	virtual bool onHandleEvent(AEvent & e);
};

