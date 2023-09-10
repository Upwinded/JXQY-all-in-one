#include "TextButton.h"

TextButton::TextButton()
{
	name = "textbutton";
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
	if (convert::GetUtf8LetterNumber(s.c_str()) > (size_t)rect.w / label.fontSize)
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

void TextButton::initFromIni(const std::string& fileName)
{
	Button::initFromIni(fileName);
	INIReader ini(fileName);
	label.fontSize = ini.GetInteger("Init", "Font", label.fontSize);
	label.color = ini.GetColor("Init", "Color", label.color);
}

void TextButton::onDraw()
{
	Button::onDraw();
	label.rect = rect;
    label.rect.y += (rect.h - label.fontSize) / 2;
	auto str = label.getStr();
	label.rect.x += (rect.w - (convert::GetUtf8LetterNumber(str.c_str()) * label.fontSize)) / 2;
	label.onDraw();
}

void TextButton::onMouseLeftDown(int x, int y)
{
    if (parent != nullptr && parent->canCallBack)
    {
        result = erMouseLDown;
        parent->onChildCallBack(this);
        result = erNone;
    }
}
