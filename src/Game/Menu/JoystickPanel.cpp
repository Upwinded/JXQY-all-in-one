#include "JoystickPanel.h"


JoystickPanel::JoystickPanel()
{
	name = "JoystickPanel";
	init();
	coverMouse = false;
	setPriority(epController);
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
	loadMenuDefinition("ini\\ui\\mobile\\joystick\\joystick.menu.ini");

	joystick = getComponentByName<Joystick>("joystick");

	setChildRectReferToParent();
}

void JoystickPanel::freeResource()
{
	joystick = nullptr;
	ConfigDrivenPanel::freeResource();
}
