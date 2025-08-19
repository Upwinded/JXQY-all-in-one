#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"

class Dialog :
	public Panel
{
public:
	Dialog();
	virtual ~Dialog();

	std::shared_ptr<INIReader> ini = nullptr;

	std::shared_ptr<ImageContainer> head1 = nullptr;
	std::shared_ptr<ImageContainer> head2 = nullptr;
	std::shared_ptr<TalkLabel> label = nullptr;

	int talkStrLen = 60;
	int talkIndex = 0;
	std::vector<TalkString> talkStrList;

	virtual void init() override;

	void setTalkStr(const std::string & str);

	void setHead1(const std::string & fileName);
	void setHead2(const std::string & fileName);

	std::string getHeadName(int index);

private:
	void readHeadFiles();

	void freeResource();
	virtual void onEvent();
	virtual bool onHandleEvent(AEvent & e);

};

