#include "Title.h"
#include "VideoPage.h"
#include "TitleTeam.h"
#include "../Script/Script.h"
#include "MainScene.h"

Title::Title()
{
	drawFullScreen = true;
}

Title::~Title()
{
	freeResource();
}

void Title::playTitleBGM()
{
	std::unique_ptr<char[]> s;
	int len = PakFile::readFile("music\\ks64.mp3", s);
	if (s != nullptr && len > 0)
	{
		engine->loadBGM(s, len);
		engine->playBGM();
	}	
}

void Title::init()
{
	initFromIniFileName(u8"ini\\ui\\title\\window.ini");
	initBtn = addComponent<Button>(u8"ini\\ui\\title\\initbtn.ini");
	exitBtn = addComponent<Button>(u8"ini\\ui\\title\\exitbtn.ini");
	loadBtn = addComponent<Button>(u8"ini\\ui\\title\\loadbtn.ini");
	teamBtn = addComponent<Button>(u8"ini\\ui\\title\\teambtn.ini");
	initBtn->stretch = true;
	exitBtn->stretch = true;
	loadBtn->stretch = true;
	teamBtn->stretch = true;
	weather = std::make_shared<Weather>();
	weather->priority = epMax;
	addChild(weather);

	impImage = nullptr;

	std::unique_ptr<char[]> s;
	int len = PakFile::readFile(u8"mpc\\ui\\title\\title.png", s);
	if (len > 0)
	{ 
		impImage = make_shared_imp();
		impImage->frame.resize(1);
		impImage->frame[0].data = std::move(s);
		impImage->frame[0].dataLen = len;
	}
	setTitleAlign();
}

void Title::freeResource()
{
	initBtn = nullptr;
	exitBtn = nullptr;
	loadBtn = nullptr;
	teamBtn = nullptr;
	weather = nullptr;

	impImage = nullptr;

	result = erNone;
	removeAllChild();
}

void Title::setTitleAlign()
{
	int w = 0, h = 0;
	engine->getWindowSize(w, h);

#define setBtnAlign(btn) \
	if ((btn) != nullptr) \
	{\
		(btn)->rect.x = (int)round(((double)(btn)->rect.x) / DEFAULT_WINDOW_WIDTH * w + 0.5);\
		(btn)->rect.y = (int)round(((double)(btn)->rect.y) / DEFAULT_WINDOW_HEIGHT * h + 0.5);\
		(btn)->rect.w = (int)round(((double)(btn)->rect.w) / DEFAULT_WINDOW_WIDTH * w + 0.5);\
		(btn)->rect.h = (int)round(((double)(btn)->rect.h) / DEFAULT_WINDOW_HEIGHT * h + 0.5);\
	}

	setBtnAlign(initBtn)
	setBtnAlign(exitBtn)
	setBtnAlign(loadBtn)
	setBtnAlign(teamBtn)
}

void Title::onEvent()
{
	if (exitBtn != nullptr && exitBtn->getResult(erClick))
	{
		engine->stopBGM();
		running = false;
	}
	if (teamBtn != nullptr && teamBtn->getResult(erClick))
	{
        weather->fadeOut();
		engine->stopBGM();
		auto tt = std::make_shared<TitleTeam>();
		tt->run();
		tryCleanRes();
		playTitleBGM();
        weather->fadeInEx();
	}
	if (initBtn != nullptr && initBtn->getResult(erClick))
	{
		weather->fadeOut();
		engine->stopBGM();
		//重新开始
		auto ms = std::make_shared<MainScene>(0);
		auto ret = ms->run();
		if (ret & erExit)
		{
			running = false;
		}
		else
		{
			tryCleanRes();
			playTitleBGM();
			weather->fadeInEx();
		}
	}
	if (loadBtn != nullptr && loadBtn->getResult(erClick))
	{
		//读取进度
		auto sl = std::make_shared<SaveLoad>(false, true);
		addChild(sl);
		sl->priority = 0;
		unsigned int ret = sl->run();
		if ((ret & erLoad) != 0)
		{
			weather->fadeOut();
			engine->stopBGM();
			auto ms = std::make_shared<MainScene>(sl->index + 1);
			ret = ms->run();
			if (ret & erExit)
			{
				running = false;
			}
			tryCleanRes();
			playTitleBGM();
			weather->fadeInEx();
		}
		removeChild(sl);
		sl = nullptr;
	}
}

bool Title::onInitial()
{
	init();
	weather->fadeIn();
	return true;
}

void Title::onExit()
{
}

void Title::onRun()
{
	auto vp = std::make_shared<VideoPage>("video\\logo.avi");
	vp->run();
	vp = std::make_shared<VideoPage>("video\\title.avi");
	vp->run();
	playTitleBGM();
}

bool Title::onHandleEvent(AEvent & e)
{
	if (e.eventType == ET_KEYDOWN)
	{
		if (e.eventData == KEY_F)
		{	
			if (engine->getKeyPress(KEY_LCTRL) || engine->getKeyPress(KEY_RCTRL))
			{ 
				engine->setWindowFullScreen(!engine->getWindowFullScreen());
				return true;
			}
		}
		if (e.eventData == KEY_M)
		{
			if (engine->getKeyPress(KEY_LCTRL) || engine->getKeyPress(KEY_RCTRL))
			{
				engine->setMouseHardware(!engine->getMouseHardware());
				return true;
			}
		}
	}
	else if (e.eventType == ET_QUIT)
	{
		running = false;
		return true;
	}
	return false;
}
