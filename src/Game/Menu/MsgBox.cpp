#include "MsgBox.h"


MsgBox::MsgBox()
{
	name = "MsgBox";
	visible = false;
	coverMouse = false;
	needEvents = false;
	init();
}


MsgBox::~MsgBox()
{
	freeResource();
}

void MsgBox::showMessage(const std::string & str, UTime duration)
{
	currentMessage = str;
	showinUTime = duration;
	if (label == nullptr)
	{
		label = getComponentByName<Label>("label");
		if (label)
		{
			label->coverMouse = false;
			setChildRectReferToParent();
		}
	}
	if (label)
	{
		label->visible = true;
		label->activated = true;
		label->setStr(str);
	}
	beginTime = getTime();
	showed = true;
	visible = true;
}

void MsgBox::onUpdate()
{
	const UTime currentTime = getTime();
	if (showed && currentTime < beginTime)
	{
		beginTime = currentTime;
	}
	if (showed && currentTime - beginTime > showinUTime)
	{
		visible = false;
		showed = false;
	}
}

void MsgBox::init()
{
	freeResource();
	loadMenuDefinition("ini\\ui\\message\\msgbox.menu.ini");

	label = getComponentByName<Label>("label");
	if (label)
	{
		label->coverMouse = false;
		label->autoNextLine = true;
	}
	setChildRectReferToParent();
}

void MsgBox::freeResource()
{
	label = nullptr;
	currentMessage.clear();
	ConfigDrivenPanel::freeResource();
}

void MsgBox::onWindowResize(int width, int height)
{
	std::string savedMessage = currentMessage;
	bool savedShowed = showed;
	UTime savedBeginTime = beginTime;
	bool savedVisible = visible;

	init();

	currentMessage = savedMessage;
	showed = savedShowed;
	beginTime = savedBeginTime;
	visible = savedVisible;
	if (label != nullptr)
	{
		label->visible = savedVisible;
		label->activated = savedVisible;
		label->setStr(currentMessage);
	}
}
