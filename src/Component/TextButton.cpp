#include "TextButton.h"

TextButton::TextButton()
{
	name = u8"textbutton";
	priority = epButton;
	elementType = etButton;
	result = erNone;
    stretch = true;
}

TextButton::~TextButton()
{
	
}

void TextButton::setFontSize(int fontSize)
{
	label.fontSize = fontSize;
}

void TextButton::setStrColor(unsigned int color)
{
	label.color = color;
}

void TextButton::setStr(const std::string& s)
{
	auto temps = s;
	if (convert::GetUtf8LetterCount(s.c_str()) > (int)rect.w / label.fontSize)
	{
		//substr(0, rect.w / label.fontSize);
		auto temp_list = convert::splitString(s, convert_max(rect.w / label.fontSize, 1));
		if (temp_list.size() > 0)
		{
			temps = temp_list[0];
		}
	}
    label.setStr(s);
}

void TextButton::setUTF8Str(const std::string& s)
{
	setStr(s);
}

void TextButton::initFromIni(INIReader & ini)
{
	Button::initFromIni(ini);
	label.fontSize = ini.GetInteger("Init", u8"Font", label.fontSize);
	label.color = ini.GetColor("Init", u8"Color", label.color);
}

void TextButton::onDraw()
{
	Button::onDraw();
	label.rect = rect;
    label.rect.y += (rect.h - label.fontSize) / 2;
	auto str = label.getStr();
	label.rect.x += (rect.w - (convert::GetUtf8LetterCount(str.c_str()) * label.fontSize)) / 2;
	label.onDraw();
}

void TextButton::onMouseLeftDown(int x, int y)
{
    if (parent != nullptr && parent->canCallBack)
    {
        result = erMouseLDown;
        parent->onChildCallBack(getMySharedPtr());
        result = erNone;
    }
}

void TextButton::onClick()
{
	if (parent != nullptr && parent->canCallBack)
	{
		result = erClick;
		parent->onChildCallBack(getMySharedPtr());
		result = erNone;
	}
}
