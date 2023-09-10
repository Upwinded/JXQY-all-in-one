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

void MsgBox::showMessage(const std::string & str)
{
	if (label == nullptr)
	{
		label = addLabel("ini\\ui\\message\\label.ini");
		label->coverMouse = false;
		setChildRectReferToParent();
	}
	label->setStr(str);
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
	initFromIni("ini\\ui\\message\\window.ini");
	label = addLabel("ini\\ui\\message\\label.ini");
	label->coverMouse = false;
	label->autoNextLine = true;
	setChildRectReferToParent();
}

void MsgBox::freeResource()
{
	if (impImage != nullptr)
	{
		IMP::clearIMPImage(impImage);
		//delete impImage;
		impImage = nullptr;
	}

	freeCom(label);
}
