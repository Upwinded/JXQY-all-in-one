#include "Panel.h"

Panel::Panel()
{
	//stretch = true;
	coverMouse = false;
}

Panel::~Panel()
{
	freeResource();
}

void Panel::setAlign()
{
	int w, h;
	engine->getWindowSize(&w, &h);
	switch (align)
	{
	case alNone:
		break;
	case alClient:
		drawFullScreen = true;
		stretch = true;
		rect.x = 0;
		rect.y = 0;
		rect.w = w;
		rect.h = h;
		break;
	case alLeft:
		rect.x = 0;
		break;
	case alRight:
		rect.x = w - rect.w;
		break;
	case alTop:
		rect.y = 0;
		break;
	case alBottom:
		rect.y = h - rect.h;
		break;
	case alLTCorner:
		rect.x = 0;
		rect.y = 0;
		break;
	case alRTCorner:
		rect.y = 0;
		rect.x = w - rect.w;
		break;
	case alLBCorner:
		rect.x = 0;
		rect.y = h - rect.h;
		break;
	case alRBCorner:
		rect.x = w - rect.w;
		rect.y = h - rect.h;
		break;
	case alCenter:
		rect.x = w / 2 - rect.w / 2;
		rect.y = h / 2 - rect.h / 2;
		break;
	case alLeftCenter:
		rect.x = 0;
		rect.y = h / 2 - rect.h / 2;
		break;
	case alRightCenter:
		rect.x = w - rect.w;
		rect.y = h / 2 - rect.h / 2;
		break;
	case alTopCenter:
		rect.x = w / 2 - rect.w / 2;
		rect.y = 0;
		break;
	case alBottomCenter:
		rect.x = w / 2 - rect.w / 2;
		rect.y = h - rect.h;
		break;
	default:
		break;
	}
	rect.x += alignX;
	rect.y += alignY;
}

Button * Panel::addButton(const std::string& fileName)
{
	Button * button = new Button;
	button->initFromIni(fileName);
	addChild(button);
	return button;
}

Scrollbar * Panel::addScrollbar(const std::string& fileName)
{
	Scrollbar * scrollbar = new Scrollbar;
	scrollbar->initFromIni(fileName);
	addChild(scrollbar);
	return scrollbar;
}

ImageContainer * Panel::addImageContainer(const std::string& fileName)
{
	ImageContainer * imageContainer = new ImageContainer;
	imageContainer->initFromIni(fileName);
	addChild(imageContainer);
	return imageContainer;
}

Item * Panel::addItem(const std::string& fileName)
{
	Item * item = new Item;
	item->initFromIni(fileName);
	addChild(item);
	return item;
}

ListBox * Panel::addListBox(const std::string & fileName)
{
	ListBox * listBox = new ListBox;
	listBox->initFromIni(fileName);
	addChild(listBox);
	return listBox;
}

CheckBox * Panel::addCheckBox(const std::string & fileName)
{
	CheckBox * checkBox = new CheckBox;
	checkBox->initFromIni(fileName);
	addChild(checkBox);
	return checkBox;
}

Label * Panel::addLabel(const std::string & fileName)
{
	Label * component = new Label;
	component->initFromIni(fileName);
	addChild(component);
	return component;
}

TalkLabel * Panel::addTalkLabel(const std::string & fileName)
{
	TalkLabel * component = new TalkLabel;
	component->initFromIni(fileName);
	addChild(component);
	return component;
}

MemoText * Panel::addMemo(const std::string & fileName)
{
	MemoText * component = new MemoText;
	component->initFromIni(fileName);
	addChild(component);
	return component;
}

ColumnImage * Panel::addColumnImage(const std::string & fileName)
{
	ColumnImage * component = new ColumnImage;
	component->initFromIni(fileName);
	addChild(component);
	return component;
}

TransImage * Panel::addTransImage(const std::string & fileName)
{
	TransImage * component = new TransImage;
	component->initFromIni(fileName);
	addChild(component);
	return component;
}

void Panel::freeResource()
{
	if (impImage != NULL)
	{
		IMP::clearIMPImage(impImage);
		delete impImage;
		impImage = NULL;
	}
	result = erNone;
	removeAllChild();
}

void Panel::initFromIni(const std::string & fileName)
{
	freeResource();
	char * s = NULL;
	int len = 0;
	len = PakFile::readFile(fileName, &s);
	if (s == NULL || len == 0)
	{		
		printf("no ini file: %s\n", fileName.c_str());
		return;
	}
	INIReader ini = INIReader::INIReader(s);
	delete[] s;
	align = alNone;

	std::string alignStr = ini.Get("Init", "Align", convert::formatString("%d", (int)align));
	alignX = ini.GetInteger("Init", "AlignX", 0);
	alignY = ini.GetInteger("Init", "AlignY", 0);

	const char* value = alignStr.c_str();
	if (value != NULL)
	{
		char * end;
		long n = strtol(value, &end, 0);
		if (end > value)
		{
			align = (Align)n;
		}
		else
		{
#define checkAlign(a) (alignStr == #a) { align = a; }

			if checkAlign(alNone)
			else if checkAlign(alClient)
			else if checkAlign(alLeft)
			else if checkAlign(alRight)
			else if checkAlign(alTop)
			else if checkAlign(alBottom)
			else if checkAlign(alLTCorner)
			else if checkAlign(alRTCorner)
			else if checkAlign(alLBCorner)
			else if checkAlign(alRBCorner)
			else if checkAlign(alCenter)
			else if checkAlign(alLeftCenter)
			else if checkAlign(alRightCenter)
			else if checkAlign(alTopCenter)
			else if checkAlign(alBottomCenter)
		}	
	}
	
	rect.x = ini.GetInteger("Init", "Left", rect.x);
	rect.y = ini.GetInteger("Init", "Top", rect.y);
	rect.w = ini.GetInteger("Init", "Width", rect.w);
	rect.h = ini.GetInteger("Init", "Height", rect.h);
	name = ini.Get("Init", "Name", name);
	std::string impName = ini.Get("Init", "Image", "");
	impImage = IMP::createIMPImage(impName);

	setAlign();
}

void Panel::resetRect(Element * e, int x, int y)
{
	if (e == NULL)
	{
		return;
	}
	int xp = x, yp = y;
	if (e == this)
	{
		setAlign();
		xp = rect.x - x;
		yp = rect.y - y;
	}
	else
	{
		e->rect.x += x;
		e->rect.y += y;
	}
	for (size_t i = 0; i < e->children.size(); i++)
	{
		resetRect(e->children[i], xp, yp);
	}
}

void Panel::resetRect()
{
	resetRect(this, 0, 0);
}
