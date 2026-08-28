#include "../Game/Data/TimeStopUpdateGate.h"
#include "../Engine/Engine.h"
#include "../Element/Element.h"

#include <algorithm>
#include <cstdarg>
#include <iostream>
#include <memory>

namespace GameLog
{
bool use_log_file = false;

void setLogFilePath(const std::string& fileName)
{
	(void)fileName;
}

void write(const char* format, ...)
{
	(void)format;
}
}

Timer::Timer() = default;
Timer::Timer(Timer* parent)
{
	setParent(parent);
}
Timer::~Timer()
{
	setParent(nullptr);
}
void Timer::setParent(Timer* parent)
{
	if (_parent == parent)
	{
		return;
	}
	if (_parent != nullptr)
	{
		_parent->removeChild(this);
	}
	_parent = parent;
	if (_parent != nullptr)
	{
		_parent->addChild(this);
	}
}
UTime Timer::get()
{
	return _beginTime;
}
void Timer::set(UTime t)
{
	_beginTime = t;
}
bool Timer::getPaused()
{
	return _paused;
}
void Timer::setPaused(bool paused)
{
	_paused = paused;
}
void Timer::reInit()
{
	_beginTime = 0;
}
void Timer::addChild(Timer* timer)
{
	if (timer != nullptr)
	{
		_children.push_back(timer);
	}
}
void Timer::removeChild(Timer* timer)
{
	_children.erase(std::remove(_children.begin(), _children.end(), timer), _children.end());
}
UTime Timer::getTimeReferToParent()
{
	return _beginTime;
}

Engine* Engine::getInstance()
{
	return nullptr;
}
void Engine::requestApplicationQuit()
{
}
void Engine::resetApplicationQuitRequest()
{
}
bool Engine::isApplicationQuitRequested() const
{
	return false;
}
bool Engine::isApplicationActive() const
{
	return true;
}
void Engine::delay(unsigned int milliseconds)
{
	(void)milliseconds;
}
int Engine::getEvent(AEvent& event)
{
	(void)event;
	return 0;
}
void Engine::getMousePosition(int& x, int& y)
{
	x = 0;
	y = 0;
}
std::vector<AEvent> Engine::getAllFingersPosition()
{
	return {};
}
void Engine::frameBegin()
{
}
void Engine::frameEnd()
{
}
void Engine::acknowledgeLogicalResizeEvent(
	std::uint32_t generation,
	int logicalWidth,
	int logicalHeight)
{
	(void)generation;
	(void)logicalWidth;
	(void)logicalHeight;
}

namespace
{
bool check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}

template<typename Tag, typename Tag::type Member>
struct PrivateMemberAccessor
{
	friend typename Tag::type getPrivateMember(Tag)
	{
		return Member;
	}
};

struct ElementPreTreatmentTag
{
	using type = void (Element::*)();
	friend type getPrivateMember(ElementPreTreatmentTag);
};

template struct PrivateMemberAccessor<ElementPreTreatmentTag, &Element::preTreatment>;

void callPreTreatment(Element& element)
{
	(element.*getPrivateMember(ElementPreTreatmentTag{}))();
}

class CountingElement : public Element
{
public:
	int preTreatmentCount = 0;

protected:
	void onPreTreatment() override
	{
		preTreatmentCount++;
	}
};

class GatedParentElement : public CountingElement
{
public:
	bool allowChildren = true;

protected:
	bool shouldUpdateChild(PElement child) override
	{
		(void)child;
		return allowChildren;
	}
};

bool checkElementPreTreatmentUsesChildUpdateGate()
{
	auto parent = std::make_shared<GatedParentElement>();
	auto child = std::make_shared<CountingElement>();
	parent->addChild(child);

	parent->allowChildren = false;
	callPreTreatment(*parent);
	bool ok = check(parent->preTreatmentCount == 1, "parent preTreatment still runs when child is gated");
	ok = check(child->preTreatmentCount == 0, "gated child preTreatment is skipped") && ok;

	parent->allowChildren = true;
	callPreTreatment(*parent);
	ok = check(parent->preTreatmentCount == 2, "parent preTreatment runs again") && ok;
	ok = check(child->preTreatmentCount == 1, "allowed child preTreatment runs") && ok;

	return ok;
}
}

int main()
{
	bool ok = true;

	ok = check(shouldUpdateGameManagerChildDuringTimeStop(false, true), "weather updates without a time stopper") && ok;
	ok = check(!shouldUpdateGameManagerChildDuringTimeStop(true, true), "weather pauses during time stop") && ok;
	ok = check(shouldUpdateGameManagerChildDuringTimeStop(true, false), "non-weather game manager children keep updating") && ok;

	ok = check(shouldUpdateEffectManagerChildDuringTimeStop(false, false), "all effects update before time stop") && ok;
	ok = check(shouldUpdateEffectManagerChildDuringTimeStop(true, true), "active time stopper keeps updating") && ok;
	ok = check(!shouldUpdateEffectManagerChildDuringTimeStop(true, false), "ordinary effects pause during time stop") && ok;

	ok = check(shouldUpdateNpcManagerChildDuringTimeStop(false, false), "NPCs update before time stop") && ok;
	ok = check(shouldUpdateNpcManagerChildDuringTimeStop(true, true), "time-stopper user keeps updating") && ok;
	ok = check(!shouldUpdateNpcManagerChildDuringTimeStop(true, false), "ordinary NPCs pause during time stop") && ok;
	ok = check(!shouldUpdateNpcManagerChildDuringTimeStop(true, false), "ordinary NPC frame time pauses during time stop") && ok;

	ok = check(shouldUpdateObjectManagerChildDuringTimeStop(false), "objects update before time stop") && ok;
	ok = check(!shouldUpdateObjectManagerChildDuringTimeStop(true), "objects pause during time stop") && ok;
	ok = check(!shouldUpdateObjectManagerChildDuringTimeStop(true), "object animation frame time pauses during time stop") && ok;

	ok = check(shouldUpdateGameControllerChildDuringTimeStop(false, true, true, false), "controller children update before time stop") && ok;
	ok = check(!shouldUpdateGameControllerChildDuringTimeStop(true, true, false, false), "object manager is paused by controller") && ok;
	ok = check(shouldUpdateGameControllerChildDuringTimeStop(true, false, true, true), "caster player keeps updating") && ok;
	ok = check(!shouldUpdateGameControllerChildDuringTimeStop(true, false, true, false), "non-caster player pauses") && ok;
	ok = check(shouldUpdateGameControllerChildDuringTimeStop(true, false, false, false), "other controller UI children keep updating") && ok;

	ok = checkElementPreTreatmentUsesChildUpdateGate() && ok;

	return ok ? 0 : 1;
}
