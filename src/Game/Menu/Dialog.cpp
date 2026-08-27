#include "Dialog.h"
#include "UIFocusManager.h"
#include "../../File/log.h"
#include "../../libconvert/libconvert.h"
#include "../GameTypes.h"
#include <algorithm>

Dialog::Dialog()
{
	name = "Dialog";
	setPriority(epMax);
	init();
}

Dialog::~Dialog()
{
	freeResource();
}

void Dialog::init()
{
	freeResource();
	loadMenuDefinition("ini\\ui\\dialog\\dialog.menu.ini");

	label = getComponentByName<TalkLabel>("label");
	head1 = getComponentByName<ImageContainer>("head1");
	head2 = getComponentByName<ImageContainer>("head2");

	setChildRectReferToParent();

	readHeadFiles();
}

void Dialog::readHeadFiles()
{
	std::unique_ptr<char[]> s;
	int len = 0;
	std::string fileName = HEAD_FILE_NAME;
	len = File::readFile(fileName, s);
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
	currentTalkText = str;
	index = 0;
	if (label)
	{
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
	}
}

void Dialog::setHead1(const std::string & fileName)
{
	head1FileName = fileName;
	head2FileName.clear();
	if (head1) head1->impImage = nullptr;
	if (head2) head2->impImage = nullptr;

	if (head1 && !fileName.empty())
	{
		std::string headName = HEAD_FOLDER_ASF + fileName;
		head1->impImage = IMP::createIMPImage(headName);
	}
}

void Dialog::setHead2(const std::string & fileName)
{
	head1FileName.clear();
	head2FileName = fileName;
	if (head1) head1->impImage = nullptr;
	if (head2) head2->impImage = nullptr;
	if (head2 && !fileName.empty())
	{
		std::string headName = HEAD_FOLDER_ASF + fileName;
		head2->impImage = IMP::createIMPImage(headName);
	}
}

void Dialog::freeResource()
{
	ini = nullptr;
	label = nullptr;
	head1 = nullptr;
	head2 = nullptr;
	currentTalkText.clear();
	head1FileName.clear();
	head2FileName.clear();
	talkStrList.resize(0);
	talkIndex = 0;
	ConfigDrivenPanel::freeResource();
}

void Dialog::onWindowResize(int width, int height)
{
	std::string savedTalkText = currentTalkText;
	std::string savedHead1FileName = head1FileName;
	std::string savedHead2FileName = head2FileName;
	int savedIndex = index;
	bool savedVisible = visible;
	bool savedLogicRunning = logicRunning;

	init();

	visible = savedVisible;
	logicRunning = savedLogicRunning;
	if (!savedHead1FileName.empty())
	{
		setHead1(savedHead1FileName);
	}
	else if (!savedHead2FileName.empty())
	{
		setHead2(savedHead2FileName);
	}
	if (!savedTalkText.empty())
	{
		setTalkStr(savedTalkText);
		if (!talkStrList.empty() && label != nullptr)
		{
			index = std::max(0, std::min(savedIndex, (int)talkStrList.size() - 1));
			label->setTalkStr(talkStrList[index]);
		}
	}
}

void Dialog::onEvent()
{
	if (index >= (int)talkStrList.size())
	{
		result = erOK;
		logicRunning = false;
	}
}

bool Dialog::onHandleEvent(AEvent & e)
{
	if (!logicRunning)
	{
		return false;
	}
	if (e.eventType == ET_MOUSEDOWN || e.eventType == ET_FINGERDOWN || (e.eventType == ET_KEYDOWN && (e.eventData == KEY_SPACE || !e.eventRepeat)))
	{
		return advanceDialog();
	}
	return false;
}

bool Dialog::onHandleUIAction(UIAction action)
{
	if (!logicRunning)
	{
		return false;
	}
	if (action == UIAction::Confirm)
	{
		return advanceDialog();
	}
	// Dialog cancellation is intentionally disabled so a controller cannot
	// accidentally skip script-owned conversation state.
	return action == UIAction::Cancel;
}

bool Dialog::advanceDialog()
{
	// If the page is still typing, the first confirmation only reveals it.
	if (label && !label->isPageComplete())
	{
		label->showAllImmediately();
		return true;
	}

	index++;
	if (index >= (int)talkStrList.size())
	{
		result = erOK;
		logicRunning = false;
	}
	else if (label)
	{
		label->setTalkStr(talkStrList[index]);
	}
	return true;
}
