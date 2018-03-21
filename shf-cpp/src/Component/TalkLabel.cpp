#ifndef _SCL_SECURE_NO_WARNINGS
#define _SCL_SECURE_NO_WARNINGS
#endif 
#include "TalkLabel.h"



TalkLabel::TalkLabel()
{
	name = "TalkLabel";
	priority = epLabel;
	coverMouse = false;
	canDrag = false;
	canDrop = false;
}


TalkLabel::~TalkLabel()
{
	freeResource();
}

void TalkLabel::setStr(TalkString tString)
{
	if (impImage != NULL)
	{
		IMP::clearIMPImage(impImage);
		delete impImage;
		impImage = NULL;
	}
	engine->beginDrawTalk(rect.w, rect.h);
	int length = TALK_W_COUNT;
	int x = 0;
	int y = 0;
	for (size_t i = 0; i < tString.talkWChar.size(); i++)
	{
		wchar_t * we = new wchar_t[2];
		we[0] = tString.talkWChar[i].c;
		we[1] = L'\0';
		std::wstring ws = L"";
		ws += we;
		engine->drawSolidUnicodeText(ws, x * fontSize, y * fontSize, fontSize, tString.talkWChar[i].color);
		x++;
		if (x >= length)
		{
			y++;
			x = 0;
		}
	}
	_image img = engine->endDrawTalk();
	impImage = IMP::createIMPImageFromImage(img);
}

void TalkLabel::drawItemStr()
{
	//已将字符串保存到image
}

std::vector<TalkString> TalkLabel::splitTalkString(const std::wstring & tString)
{
	std::vector<std::wstring> ws = convert::splitWString(tString, L"<Enter>");
	std::vector<std::wstring> nws;
	for (size_t i = 0; i < ws.size(); i++)
	{
		std::vector<std::wstring> rws = convert::splitWString(ws[i], L"<enter>");
		for (size_t j = 0; j < rws.size(); j++)
		{
			nws.push_back(rws[j]);
		}
	}
	ws = nws;
	std::vector<TalkString> result;
	result.resize(0);
	int length = (TALK_W_COUNT) * (TALK_H_COUNT);
	for (size_t i = 0; i < ws.size(); i++)
	{
		int count = 0;
		unsigned int col = color;
		int j = 0;
		while(j < (int)ws[i].length())
		{	
			if (ws[i][j] == L'<' && ws[i].find(L"<color=Red>", j) == j)
			{
				col = 0xFFFF0000;
				std::wstring cc = L"<color=Red>";
				j += cc.length();
			}
			else if (ws[i][j] == L'<' && ws[i].find(L"<color=Black>", j) == j)
			{
				col = 0xFF000000;
				std::wstring cc = L"<color=Black>";
				j += cc.length();
			}
			else if (ws[i][j] == L'<' && ws[i].find(L"<color=Default>", j) == j)
			{
				col = color;
				std::wstring cc = L"<color=Defaul>";
				j += cc.length();
			}
			else
			{
				if (count == 0)
				{
					result.resize(result.size() + 1);
				}
				TalkWChar twc;
				twc.color = col;
				twc.c = ws[i][j];
				std::wstring ss = &ws[i][j];
				result[result.size() - 1].talkWChar.push_back(twc);
				count++;
				j++;
			}		
			if (count >= length)
			{
				count = 0;
			}
		}
	}
	return result;

}
