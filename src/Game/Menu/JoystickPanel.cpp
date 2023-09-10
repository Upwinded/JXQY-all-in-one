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

void JoystickPanel::onChildCallBack(Element* child)
{
	if (child == nullptr) { return; }
	result = child->getResult();
	if (result & erDragEnd)
	{
		auto component = (DragRoundButton*)child;
		dragEndPosition = component->getDragPosition();
	}
	if (parent != nullptr && parent->canCallBack)
	{
		parent->onChildCallBack(this);
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
	initFromIni("ini\\ui\\mobile\\joystick\\window.ini");
	joystick = addJoystick("ini\\ui\\mobile\\joystick\\joystick.ini");
	//addComponent(DragRoundButton, leftJumpBtn, "ini\\ui\\mobile\\skills\\leftjump.ini");
	setChildRectReferToParent();
}

void JoystickPanel::freeResource()
{
	if (impImage != nullptr)
	{
		IMP::clearIMPImage(impImage);
		//delete impImage;
		impImage = nullptr;
	}
	freeCom(joystick);
	//freeCom(leftJumpBtn);
}
