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

void TalkLabel::setTalkStr(TalkString& tString)
{
	if (impImage != nullptr)
	{
		IMP::clearIMPImage(impImage);
		//delete impImage;
		impImage = nullptr;
	}
	if (!engine->beginDrawTalk(rect.w, rect.h))
	{
		impImage = nullptr;
	}
	int length = TALK_W_COUNT;
	int x = 0;
	int y = 0;
	for (size_t i = 0; i < tString.talkChar.size(); i++)
	{
		engine->drawSolidText(tString.talkChar[i].s, x * fontSize, y * fontSize, fontSize, tString.talkChar[i].color);
		x++;
		if (x >= length)
		{
			y++;
			x = 0;
		}
	}
	auto img = engine->endDrawTalk();
	impImage = IMP::createIMPImageFromImage(img);
}

void TalkLabel::drawItemStr()
{
	//已将字符串保存到image
}

std::vector<TalkString> TalkLabel::splitTalkString(const std::string & tString)
{
	auto s = convert::splitString(tString, "<Enter>");
	std::vector<std::string> nws;
	for (size_t i = 0; i < s.size(); i++)
	{
		auto rws = convert::splitString(s[i], "<enter>");
		for (size_t j = 0; j < rws.size(); j++)
		{
			nws.push_back(rws[j]);
		}
	}
	s = nws;
	std::vector<TalkString> result;
	result.resize(0);
	int length = (TALK_W_COUNT) * (TALK_H_COUNT);
	for (size_t i = 0; i < s.size(); i++)
	{
		int count = 0;
		unsigned int col = color;
		int j = 0;
		while(j < (int)s[i].length())
		{	
			if (s[i][j] == '<' && s[i].find("<color=red>", j) == j)
			{
				col = 0xFFFF0000;
				std::string cc = "<color=red>";
				j += cc.length();
			}
			else if (s[i][j] == L'<' && s[i].find("<color=black>", j) == j)
			{
				col = 0xFF000000;
				std::string cc = "<color=black>";
				j += cc.length();
			}
			else if (s[i][j] == L'<' && s[i].find("<color=default>", j) == j)
			{
				col = color;
				std::string cc = "<color=default>";
				j += cc.length();
			}
			else
			{
				if (count == 0)
				{
					result.resize(result.size() + 1);
				}
				TalkChar twc;
				twc.color = col;
				unsigned char c = (unsigned char) *(s[i].c_str() + j);
				size_t utf8_char_length = convert_max(convert::GetUtf8CharLength(c), 1);

				std::unique_ptr<char[]> temp_str(new char[utf8_char_length + 1]);
				temp_str[utf8_char_length] = 0x0;
				memcpy(temp_str.get(), &s[i][j], convert_min(utf8_char_length, s[i].length() - j));
				twc.s = temp_str.get();
				char temp_c[6] = {0, 0, 0, 0, 0, 0};
				for (size_t k = 0; k < convert_min(utf8_char_length, s[i].length() - j); k++)
				{
					temp_c[k] = temp_str[k];//*(twc.s.c_str() + i);
				}
				result[result.size() - 1].talkChar.push_back(twc);
				count++;
				j += utf8_char_length;
			}		
			if (count >= length)
			{
				count = 0;
			}
		}
	}
	return result;

}
