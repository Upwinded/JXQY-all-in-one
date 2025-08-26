#include "JoystickPanel.h"


JoystickPanel::JoystickPanel()
{
	init();
	coverMouse = false;
	priority = epMin;
	canCallBack = true;
}


JoystickPanel::~JoystickPanel()
{
	freeResource();
}


std::vector<int> JoystickPanel::getDirectionList()
{
	if (joystick != nullptr)
	{
		return joystick->getDirectionList();
	}
	return std::vector<int>(0);
}

bool JoystickPanel::isRunning()
{
	if (joystick != nullptr)
	{
		return joystick->isRunning();
	}
	return false;
}

bool JoystickPanel::isWalking()
{
	if (joystick != nullptr)
	{
		return joystick->isWalking();
	}
	return false;
}

void JoystickPanel::onChildCallBack(PElement child)
{
	if (child == nullptr) { return; }
	result = child->getResult();
	if (parent != nullptr && parent->canCallBack)
	{
		parent->onChildCallBack(getMySharedPtr());
	}
}

void JoystickPanel::onUpdate()
{

}

void JoystickPanel::onEvent()
{

}

void JoystickPanel::init()
{
	freeResource();
	initFromIniFileName("ini\\ui\\mobile\\joystick\\window.ini");
	joystick = addComponent<Joystick>("ini\\ui\\mobile\\joystick\\joystick.ini");
	//addComponent(DragRoundButton, leftJumpBtn, u8"ini\\ui\\mobile\\skills\\leftjump.ini");
	setChildRectReferToParent();
}

void JoystickPanel::freeResource()
{
	impImage = nullptr;

	freeCom(joystick);
	//freeCom(leftJumpBtn);
}
