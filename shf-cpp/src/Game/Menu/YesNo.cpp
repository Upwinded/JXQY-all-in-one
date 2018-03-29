#include "YesNo.h"



YesNo::YesNo(const std::wstring & ws)
{
	priority = epMax;
	init(ws);
}


YesNo::~YesNo()
{
	freeResource();
}

void YesNo::init(const std::wstring & ws)
{
	freeResource();
	initFromIni("ini\\ui\\yesno\\window.ini");
	yes = addButton("ini\\ui\\yesno\\BtnYes.ini");
	no = addButton("ini\\ui\\yesno\\BtnNo.ini");
	label = addLabel("ini\\ui\\yesno\\Label.ini");
	label->setStr(ws);
	setChildRect();
}

void YesNo::freeResource()
{
	if (impImage != NULL)
	{
		IMP::clearIMPImage(impImage);
		delete impImage;
		impImage = NULL;
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
	if (e->eventType == KEYDOWN)
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
