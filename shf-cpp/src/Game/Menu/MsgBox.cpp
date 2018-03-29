#include "MsgBox.h"


MsgBox::MsgBox()
{
	init();
	visible = false;
	coverMouse = false;
}


MsgBox::~MsgBox()
{
	freeResource();
}

void MsgBox::showMessage(const std::wstring & wstr)
{
	if (label == NULL)
	{
		label = addLabel("ini\\ui\\Message\\label.ini");
		label->coverMouse = false;
		setChildRect();
	}
	label->setStr(wstr);
	beginTime = getTime();
	showed = true;
	visible = true;
}

void MsgBox::onUpdate()
{
	if (showed && getTime() - beginTime > showingTime)
	{
		visible = false;
		showed = false;
	}
}

void MsgBox::init()
{
	freeResource();
	initFromIni("ini\\ui\\Message\\window.ini");
	label = addLabel("ini\\ui\\Message\\label.ini");
	label->coverMouse = false;
	label->autoNextLine = true;
	setChildRect();
}

void MsgBox::freeResource()
{
	if (impImage != NULL)
	{
		IMP::clearIMPImage(impImage);
		delete impImage;
		impImage = NULL;
	}

	freeCom(label);
}
