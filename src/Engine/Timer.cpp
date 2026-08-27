#include "Timer.h"

#include <SDL3/SDL_timer.h>

Timer::Timer()
{
	reInit();
}

Timer::Timer(Timer* parent)
{
	setParent(parent);
	reInit();
}

Timer::~Timer()
{
	if (_parent != nullptr)
	{
		_parent->removeChild(this);
	}
	for (auto child : _children)
	{
		child->_parent = nullptr;
	}
	_children.clear();
}

void Timer::setParent(Timer* parent)
{
	auto now = getTimeReferToParent() - _beginTime;
	if (_parent != nullptr)
	{
		_parent->removeChild(this);
	}
	_parent = parent;
	if (_parent != nullptr)
	{
		_parent->addChild(this);
	}
	_beginTime = getTimeReferToParent() - now;
}

UTime Timer::get()
{
	if (_paused)
	{
		return _pauseBeginTime - _beginTime;
	}
	return getTimeReferToParent() - _beginTime;
}

void Timer::set(UTime time)
{
	_beginTime += get() - time;
}

bool Timer::getPaused()
{
	return _paused;
}

void Timer::setPaused(bool paused)
{
	if (paused == _paused)
	{
		return;
	}
	if (paused)
	{
		_pauseBeginTime = getTimeReferToParent();
		_paused = paused;
	}
	else
	{
		_paused = paused;
		_beginTime += getTimeReferToParent() - _pauseBeginTime;
	}
}

void Timer::reInit()
{
	_beginTime = getTimeReferToParent();
	_pauseBeginTime = _beginTime;
}

void Timer::addChild(Timer* timer)
{
	if (timer != nullptr)
	{
		timer->_parent = this;
		_children.push_back(timer);
	}
}

void Timer::removeChild(Timer* timer)
{
	if (timer != nullptr && timer->_parent == this)
	{
		timer->_parent = nullptr;
		size_t index = 0;
		while (index < _children.size())
		{
			if (_children[index] == timer)
			{
				_children.erase(_children.begin() + index);
			}
			else
			{
				index++;
			}
		}
	}
}

UTime Timer::getTimeReferToParent()
{
	if (_parent != nullptr)
	{
		return _parent->get();
	}
	return SDL_GetTicks();
}
