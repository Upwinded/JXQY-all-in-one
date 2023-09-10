#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"

class Dialog :
	public Panel
{
public:
	Dialog();
	virtual ~Dialog();

	ImageContainer * head1 = nullptr;
	ImageContainer * head2 = nullptr;
	TalkLabel * label = nullptr;

	int talkStrLen = 60;
	int talkIndex = 0;
	std::vector<TalkString> talkStrList;

	void init();

	void setTalkStr(const std::string & str);

	void setHead1(const std::string & fileName);
	void setHead2(const std::string & fileName);

private:
	void freeResource();
	virtual void onEvent();
	virtual bool onHandleEvent(AEvent * e);

};

