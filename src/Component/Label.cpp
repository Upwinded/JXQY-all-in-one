#include "Label.h"



Label::Label()
{
	name = "Label";
	priority = epLabel;
	coverMouse = false;
	canDrag = false;
	canDrop = false;
}

Label::~Label()
{
	freeResource();
}

void Label::setStr(const std::string & s)
{
	if (str.compare(s) != 0)
	{
		str = s;
		for (size_t i = 0; i < strImage.size(); i++)
		{
			if (strImage[i] != nullptr)
			{
				//engine->freeImage(strImage[i]);
				strImage[i] = nullptr;
			}
		}
		strImage.resize(0);

		int length = rect.w / fontSize;

		if (autoNextLine)
		{
			auto newstrs = convert::splitString(str, "<enter>");//length);
			if (engine->beginDrawTalk(rect.w, rect.h))
			{
				int count = 0;
				for (size_t i = 0; i < newstrs.size(); i++)
				{
					auto strs2 = convert::splitString(newstrs[i], length);
					for (size_t j = 0; j < strs2.size(); j++)
					{
						engine->drawTalk(strs2[j], 0, count * fontSize, fontSize, color);
						count++;
					}
				}
				auto img = engine->endDrawTalk();
				strImage.push_back(img);
			}
		}
		else
		{
			//wstrs.push_back(wstr);
			_shared_image img = engine->createText(str, fontSize, color);
			strImage.push_back(img);
		}
	}
}

void Label::onMouseLeftDown(int x, int y)
{
	if (parent != nullptr && parent->canCallBack)
	{
		result |= erMouseLDown;
		parent->onChildCallBack(getMySharedPtr());
	}
}

void Label::freeResource()
{
	impImage = nullptr;
	for (size_t i = 0; i < strImage.size(); i++)
	{
		if (strImage[i] != nullptr)
		{
			//engine->freeImage(strImage[i]);
			strImage[i] = nullptr;
		}
	}
	strImage.resize(0);
	Item::freeResource();
}

void Label::drawItemStr()
{
	for (size_t i = 0; i < strImage.size(); i++)
	{
		engine->drawImage(strImage[i], rect.x, rect.y + i * fontSize);
	}
}

