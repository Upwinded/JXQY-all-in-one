#pragma once
#include "../../Component/Component.h"

class JoystickPanel :
	public Panel
{
public:
	JoystickPanel();
	virtual ~JoystickPanel();
private:
public:
	Joystick * joystick = nullptr;
public:
	std::vector<int> getDirectionList();
	bool isRunning();
	bool isWalking();
private:
	Point dragEndPosition = { 0, 0 };
	//DragRoundButton* leftJumpBtn = nullptr;
public:
	Point getDragEndPosition() { return dragEndPosition; }
	virtual void onChildCallBack(Element* child);
protected:
	virtual void onUpdate();
	virtual void init();
	void freeResource();
	virtual void onEvent();

};

