#include "TimerMenu.h"
#include "../GameManager/GameManager.h"

TimerMenu::TimerMenu()
{
	visible = false;
	coverMouse = false;
	needEvents = false;
	init();
}

TimerMenu::~TimerMenu()
{
	freeResource();
}

void TimerMenu::init()
{
	freeResource();
	loadMenuDefinition("ini\\ui\\timer\\timer.menu.ini");

	timeLabel = getComponentByName<Label>("timeLabel");
	if (timeLabel != nullptr)
	{
		timeLabel->coverMouse = false;
	}
	setChildRectReferToParent();
}

void TimerMenu::startTimer(int seconds)
{
	visible = true;
	updateTime(seconds);
}

void TimerMenu::stopTimer()
{
	visible = false;
}

void TimerMenu::hideTimer()
{
	name = "TimerMenu";
	visible = false;
}

void TimerMenu::updateTime(int seconds)
{
	if (timeLabel != nullptr)
	{
		int min = seconds / 60;
		int sec = seconds % 60;
		char buf[32];
		snprintf(buf, sizeof(buf), "%02d分%02d秒", min, sec);
		timeLabel->setStr(buf);
	}
}

void TimerMenu::onUpdate()
{
	if (gm->timerStarted && !gm->timerHidden)
	{
		visible = true;
		updateTime(gm->timerSeconds);
	}
	else
	{
		visible = false;
	}
}

void TimerMenu::freeResource()
{
	timeLabel = nullptr;
	ConfigDrivenPanel::freeResource();
}
