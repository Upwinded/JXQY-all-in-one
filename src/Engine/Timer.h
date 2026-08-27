#pragma once

#include <vector>

#include "../Types/CommonTypes.h"

class Timer
{
public:
	Timer();
	Timer(Timer* parent);
	virtual ~Timer();

	void setParent(Timer* parent);
	UTime get();
	void set(UTime time);
	bool getPaused();
	void setPaused(bool paused);
	void reInit();

private:
	void addChild(Timer* timer);
	void removeChild(Timer* timer);
	UTime getTimeReferToParent();

	UTime _beginTime = 0;
	bool _paused = false;
	UTime _pauseBeginTime = 0;
	Timer* _parent = nullptr;
	std::vector<Timer*> _children;
};
