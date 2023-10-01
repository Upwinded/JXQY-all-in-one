#include "Dialog.h"


Dialog::Dialog()
{
	init();
	priority = epMax;
}

Dialog::~Dialog()
{
	freeResource();
}

void Dialog::init()
{
	freeResource();
	initFromIniFileName("ini\\ui\\dialog\\window.ini");
	label = addComponent<TalkLabel>("ini\\ui\\dialog\\label.ini");
	head1 = addComponent<ImageContainer>("ini\\ui\\dialog\\head1.ini");
	head2 = addComponent<ImageContainer>("ini\\ui\\dialog\\head2.ini");
	setChildRectReferToParent();

	readHeadFiles();
}

void Dialog::readHeadFiles()
{
	std::unique_ptr<char[]> s;
	int len = 0;
	std::string fileName = HEAD_FILE_NAME;
	len = PakFile::readFile(fileName, s);
	if (s == nullptr || len == 0)
	{
		GameLog::write("no ini file: %s\n", fileName.c_str());
		return;
	}
	ini = std::make_shared<INIReader>(s);
}

std::string Dialog::getHeadName(int index)
{
	if (ini != nullptr)
	{
		return ini->Get("PORTRAIT", convert::formatString("%d", index), "");
	}
	return "";
}

void Dialog::setTalkStr(const std::string & str)
{
	index = 0;
	talkStrList = label->splitTalkString(str);
	if (talkStrList.size() > 0)
	{
		label->setTalkStr(talkStrList[0]);
	}
	else
	{
		TalkString ts;
		ts.talkChar.resize(0);
		label->setTalkStr(ts);
	}
	/*
	talkIndex = 0;
	talkstr.resize(0);
	std::wstring nextPage = L"<enter>";
	std::vector<std::wstring> twstr = convert::splitWString(wstr, nextPage);
	for (size_t i = 0; i < twstr.size(); i++)
	{
		std::vector<std::wstring> newstr = convert::splitWString(twstr[i], talkStrLen);
		for (size_t j = 0; j < newstr.size(); j++)
		{
			talkStr.push_back(newstr[j]);
		}
	}
	if (talkStr.size() > 0)
	{
	label->setStr(talkStr[0]);
	}
	else
	{
	label->setStr(L"");
	}
	*/
}

void Dialog::setHead1(const std::string & fileName)
{

	head1->impImage = nullptr;

	head2->impImage = nullptr;

	std::string headName = HEAD_FOLDER + fileName;
	head1->impImage = IMP::createIMPImage(headName);

}

void Dialog::setHead2(const std::string & fileName)
{
	head1->impImage = nullptr;
	head2->impImage = nullptr;
	std::string headName = HEAD_FOLDER + fileName;
	head2->impImage = IMP::createIMPImage(headName);
}

void Dialog::freeResource()
{
	ini = nullptr;

	impImage = nullptr;

	freeCom(label);
	freeCom(head1);
	freeCom(head2);
	talkStrList.resize(0);
	talkIndex = 0;
}

void Dialog::onEvent()
{
	if (index >= (int)talkStrList.size())
	{
		result = erOK;
		running = false;
	}
}

bool Dialog::onHandleEvent(AEvent & e)
{
	if (!running)
	{
		return false;
	}
	if (e.eventType == ET_MOUSEDOWN || e.eventType == ET_KEYDOWN || e.eventType == ET_FINGERDOWN)
	{
		index++;
		if (index >= (int)talkStrList.size())
		{
			result = erOK;
			running = false;
		}
		else
		{
			label->setTalkStr(talkStrList[index]);
		}
		return true;
	}
	return false;
}

