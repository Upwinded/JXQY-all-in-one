#include "YesNo.h"



YesNo::YesNo(const std::string & s)
{
	priority = epMax;
	init(s);
}


YesNo::~YesNo()
{
	freeResource();
}

void YesNo::init(const std::string & s)
{
	freeResource();
	initFromIni("ini\\ui\\yesno\\window.ini");
	yes = addButton("ini\\ui\\yesno\\btnYes.ini");
	no = addButton("ini\\ui\\yesno\\btnNo.ini");
	label = addLabel("ini\\ui\\yesno\\label.ini");
	label->setStr(s);
	setChildRectReferToParent();
}

void YesNo::freeResource()
{
	if (impImage != nullptr)
	{
		IMP::clearIMPImage(impImage);
		//delete impImage;
		impImage = nullptr;
	}
	freeCom(yes);
	freeCom(no);
	freeCom(label);
}

void YesNo::onEvent()
{
	if (yes->getResult(erClick))
	{
		running = false;
		result = erOK;
	}
	else if (no->getResult(erClick))
	{
		running = false;
		result = erExit;
	}
}

bool YesNo::onHandleEvent(AEvent * e)
{
	if (e->eventType == ET_KEYDOWN)
	{
		if (e->eventData == KEY_ESCAPE)
		{
			running = false;
			result = erExit;
			return true;
		}
	}
	return false;
}
