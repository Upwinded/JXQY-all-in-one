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

void Label::setStr(const std::wstring & ws)
{
	if (wstr != ws)
	{
		wstr = ws;
		for (size_t i = 0; i < strImage.size(); i++)
		{
			if (strImage[i] != NULL)
			{
				engine->freeImage(strImage[i]);
				strImage[i] = NULL;
			}
		}
		strImage.resize(0);

		int length = rect.w / fontSize;

		if (autoNextLine)
		{
			std::vector<std::wstring> newstrs = convert::splitWString(wstr, L"<enter>");//length);
			engine->beginDrawTalk(rect.w, rect.h);
			int count = 0;
			for (size_t i = 0; i < newstrs.size(); i++)
			{
				std::vector<std::wstring> wstrs2 = convert::splitWString(newstrs[i], length);
				for (size_t j = 0; j < wstrs2.size(); j++)
				{
					for (size_t k = 0; k < wstrs2[j].length(); k++)
					{
						wchar_t * wc = new wchar_t[2];
						wc[0] = wstrs2[j][k];
						wc[1] = L'\0';
						std::wstring ws = wc;
						engine->drawSolidUnicodeText(ws, k * fontSize, count * fontSize, fontSize, color);
					}
					count++;
				}
				/*
				
				for (size_t j = 0; j < wstrs2.size(); j++)
				{
					//wstrs.push_back(wstrs2[j]);
					_image img = engine->createUnicodeText(wstrs2[j], fontSize, color);
					strImage.push_back(img);
				}
				*/
			}
			_image img = engine->endDrawTalk();
			strImage.push_back(img);
		}
		else
		{
			//wstrs.push_back(wstr);
			_image img = engine->createUnicodeText(wstr, fontSize, color);
			strImage.push_back(img);
		}

	}
}

void Label::freeResource()
{
	if (impImage != NULL)
	{
		IMP::clearIMPImage(impImage);
		delete impImage;
		impImage = NULL;
	}
	for (size_t i = 0; i < strImage.size(); i++)
	{
		if (strImage[i] != NULL)
		{
			engine->freeImage(strImage[i]);
			strImage[i] = NULL;
		}
	}
	strImage.resize(0);
}

void Label::drawItemStr()
{
	for (size_t i = 0; i < strImage.size(); i++)
	{
		engine->drawImage(strImage[i], rect.x, rect.y + i * fontSize);
	}
	
}

