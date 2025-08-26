#pragma once
#include "Item.h"
#include "../libconvert/libconvert.h"

#define TALK_W_COUNT 19
#define TALK_H_COUNT 3

struct TalkChar
{
	unsigned int color = 0xFF000000;
	std::string s = u8"";
};

struct TalkString
{
	std::vector<TalkChar> talkChar;
};

class TalkLabel :
	public Item
{
public:
	TalkLabel();
	virtual ~TalkLabel();
	virtual void setTalkStr(TalkString& tString);
	virtual void drawItemStr();
	std::vector<TalkString> splitTalkString(const std::string & tString);

private:

	virtual void setStr(const std::string & ws) {};
	
};

