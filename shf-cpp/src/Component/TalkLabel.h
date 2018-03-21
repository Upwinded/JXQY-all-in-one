#pragma once
#include "Item.h"
#include "../libconvert/libconvert.h"

#define TALK_W_COUNT 19
#define TALK_H_COUNT 3

struct TalkWChar
{
	unsigned int color = 0xFF000000;
	wchar_t c = L'\0';
};

struct TalkString
{
	std::vector<TalkWChar> talkWChar;
};

class TalkLabel :
	public Item
{
public:
	TalkLabel();
	~TalkLabel();
	virtual void setStr(TalkString tString);

	virtual void drawItemStr();
	std::vector<TalkString> splitTalkString(const std::wstring & tString);

private:

	virtual void setStr(const std::wstring & ws) {};
	
};

