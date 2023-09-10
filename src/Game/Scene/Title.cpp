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
	initFromIni(R"(ini\ui\title\window.ini)");
	initBtn = addButton(R"(ini\ui\title\initbtn.ini)");
	exitBtn = addButton(R"(ini\ui\title\exitbtn.ini)");
	loadBtn = addButton(R"(ini\ui\title\loadbtn.ini)");
	teamBtn = addButton(R"(ini\ui\title\teambtn.ini)");
	initBtn->stretch = true;
	exitBtn->stretch = true;
	loadBtn->stretch = true;
	teamBtn->stretch = true;
	weather = new Weather;
	weather->priority = epMax;
	addChild(weather);
	if (impImage != nullptr)
	{
		IMP::clearIMPImage(impImage);
		//delete impImage;
		impImage = nullptr;
	}
	std::unique_ptr<char[]> s;
	int len = PakFile::readFile(R"(mpc\ui\title\title.png)", s);
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
	delete initBtn;
	initBtn = nullptr;
	delete exitBtn;
	exitBtn = nullptr;
	delete loadBtn;
	loadBtn = nullptr;
	delete teamBtn;
	teamBtn = nullptr;
	delete weather;
	weather = nullptr;
	if (impImage != nullptr)
	{
		IMP::clearIMPImage(impImage);
		//delete impImage;
		impImage = nullptr;
	}
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
		auto tt = new TitleTeam;
		tt->run();
		delete tt;
		playTitleBGM();
        weather->fadeInEx();
	}
	if (initBtn != nullptr && initBtn->getResult(erClick))
	{
		weather->fadeOut();
		engine->stopBGM();
		//重新开始
		auto ms = new MainScene(0);
		auto ret = ms->run();
		delete ms;
		if (ret & erExit)
		{
			running = false;
		}
		playTitleBGM();
		weather->fadeInEx();
	}
	if (loadBtn != nullptr && loadBtn->getResult(erClick))
	{
		//读取进度
		auto sl = new SaveLoad(false, true);
		addChild(sl);
		sl->priority = 0;
		unsigned int ret = sl->run();
		if ((ret & erLoad) != 0)
		{
			weather->fadeOut();
			engine->stopBGM();
			auto ms = new MainScene(sl->index + 1);
			ret = ms->run();
			delete ms;
			if (ret & erExit)
			{
				running = false;
			}
			playTitleBGM();
			weather->fadeInEx();
		}
		removeChild(sl);
		delete sl;	
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
	auto vp = new VideoPage("video\\logo.avi");
	vp->run();
	delete vp;
	vp = new VideoPage("video\\title.avi");
	vp->run();
	delete vp;
	playTitleBGM();
}

bool Title::onHandleEvent(AEvent * e)
{
	if (e == nullptr)
	{
		return false;
	}
	if (e->eventType == ET_KEYDOWN)
	{
		if (e->eventData == KEY_F)
		{	
			if (engine->getKeyPress(KEY_LCTRL) || engine->getKeyPress(KEY_RCTRL))
			{ 
				engine->setWindowFullScreen(!engine->getWindowFullScreen());
				return true;
			}
		}
		if (e->eventData == KEY_M)
		{
			if (engine->getKeyPress(KEY_LCTRL) || engine->getKeyPress(KEY_RCTRL))
			{
				engine->setMouseHardware(!engine->getMouseHardware());
				return true;
			}
		}
	}
	else if (e->eventType == ET_QUIT)
	{
		running = false;
		return true;
	}
	return false;
}
