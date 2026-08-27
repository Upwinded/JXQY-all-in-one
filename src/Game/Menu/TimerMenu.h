#pragma once
#include "../../Component/Component.h"

class TimerMenu :
	public ConfigDrivenPanel
{
public:
	TimerMenu();
	virtual ~TimerMenu();


	void startTimer(int seconds);
	void stopTimer();
	void hideTimer();
	void updateTime(int seconds);

	virtual void init() override;

private:
	
	std::shared_ptr<Label> timeLabel = nullptr;

	void freeResource();
	virtual void onUpdate() override;
};
