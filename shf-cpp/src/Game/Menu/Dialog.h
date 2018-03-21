#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"

class Dialog :
	public Panel
{
public:
	Dialog();
	~Dialog();

	ImageContainer * head1 = NULL;
	ImageContainer * head2 = NULL;
	TalkLabel * label = NULL;

	int talkStrLen = 60;
	int talkIndex = 0;
	std::vector<TalkString> talkStr = {};

	void init();

	void setTalkStr(const std::wstring & wstr);

	void setHead1(const std::string & fileName);
	void setHead2(const std::string & fileName);

private:
	virtual void freeResource();
	virtual void onEvent();
	virtual bool onHandleEvent(AEvent * e);

};

