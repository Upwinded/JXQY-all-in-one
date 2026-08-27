#pragma once
#include "../../Component/Component.h"

class Dialog :
	public ConfigDrivenPanel
{
public:
	Dialog();
	virtual ~Dialog();

	virtual void init() override;

	void setTalkStr(const std::string & str);

	void setHead1(const std::string & fileName);
	void setHead2(const std::string & fileName);
	
	std::string getHeadName(int index);

private:

	std::shared_ptr<INIReader> ini = nullptr;

	std::shared_ptr<ImageContainer> head1 = nullptr;
	std::shared_ptr<ImageContainer> head2 = nullptr;
	std::shared_ptr<TalkLabel> label = nullptr;
	std::string currentTalkText;
	std::string head1FileName;
	std::string head2FileName;

	int talkStrLen = 60;
	int talkIndex = 0;
	std::vector<TalkString> talkStrList;


	void readHeadFiles();
	bool advanceDialog();

	void freeResource();
	virtual void onEvent() override;
	virtual bool onHandleEvent(AEvent & e) override;
	virtual bool onHandleUIAction(UIAction action) override;
	virtual void onWindowResize(int width, int height) override;
};
