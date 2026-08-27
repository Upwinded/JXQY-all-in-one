#pragma once
#include "../../Component/Component.h"

class JoystickPanel :
	public ConfigDrivenPanel
{
public:
	JoystickPanel();
	virtual ~JoystickPanel();
private:
public:
	std::shared_ptr<Joystick> joystick = nullptr;
public:
	std::vector<int> getDirectionList();
	bool isRunning();
	bool isWalking();
	virtual void onChildCallBack(PElement child);
protected:
	virtual void onUpdate() override;
	virtual void init() override;
	void freeResource();
	virtual void onEvent() override;
};
