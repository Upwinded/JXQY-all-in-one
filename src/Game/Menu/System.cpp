#include "System.h"
#include "SaveLoad.h"
#include "../GameManager/GameManager.h"


System::System()
{
	priority = epMax;
	init();
}


System::~System()
{
	freeResource();
}

void System::init()
{
	freeResource();
	initFromIni("ini\\ui\\system\\window.ini");
	title = addImageContainer("ini\\ui\\system\\title.ini");
	returnBtn = addButton("ini\\ui\\system\\return.ini");
	saveloadBtn = addButton("ini\\ui\\system\\saveload.ini");
	optionBtn = addButton("ini\\ui\\system\\option.ini");
	quitBtn = addButton("ini\\ui\\system\\quit.ini");

	setChildRectReferToParent();
}

void System::freeResource()
{
	if (impImage != nullptr)
	{
		IMP::clearIMPImage(impImage);
		//delete impImage;
		impImage = nullptr;
	}
	freeCom(title);
	freeCom(returnBtn);
	freeCom(saveloadBtn);
	freeCom(optionBtn);
	freeCom(quitBtn);
}

void System::onEvent()
{
	if (saveloadBtn != nullptr && saveloadBtn->getResult(erClick))
	{
		SaveLoad * sl = new SaveLoad(true, true);
		addChild(sl);
		sl->priority = 0;
		unsigned int ret = sl->run();
		if ((ret & erLoad) != 0)
		{
			index = sl->index;
			result = erLoad;
			running = false;
			visible = false;
//			GameManager::getInstance()->loadGameWithThread(index + 1);
			GameManager::getInstance()->weather.fadeOut();
			GameManager::getInstance()->loadGame(index + 1);
			GameManager::getInstance()->weather.fadeInEx();
		}
		else if ((ret & erSave) != 0)
		{
			index = sl->index;
			result = erSave;
			GameManager::getInstance()->saveGame(index + 1);
			saveScreen();
		}
		removeChild(sl);
		delete sl;
	}
	if (optionBtn != nullptr && optionBtn->getResult(erClick))
	{
		Option * option = new Option;
		option->priority = epMax;
		addChild(option);
		option->run();
		removeChild(option);
		delete option;
		option = nullptr;
	}
	if (returnBtn != nullptr && returnBtn->getResult(erClick))
	{
		running = false;
		result = erOK;
	}
	if (quitBtn != nullptr && quitBtn->getResult(erClick))
	{
		running = false;
		result = erExit;
	}

}

bool System::onHandleEvent(AEvent * e)
{
	if (e->eventType == ET_QUIT)
	{
		running = false;
		result = erExit;
		return true;
	}
	else if (e->eventType == ET_KEYDOWN)
	{
		if (e->eventData == KEY_ESCAPE)
		{
			running = false;
			result = erOK;
			return true;
		}
	}
	return false;
}

void System::saveScreen()
{
	std::string imageName = SHOT_FOLDER + convert::formatString(SHOT_BMP, index + 1);
	int w = 260;
	int h = 200;
	engine->beginSaveScreen();
	gm->map.drawMap();
	gm->weather.draw();
	auto snap = engine->endSaveScreen();
	std::unique_ptr<char[]> data;
	int len = engine->saveImageToPixels(snap, w, h, data);
	if (len > 0 && data.get() != nullptr)
	{
		auto newData = std::make_unique<char[]>(len + SAVE_SHOT_HEAD_LEN + 12);
		memcpy(newData.get() + SAVE_SHOT_HEAD_LEN + 12, data.get(), len);

		memcpy(newData.get(), SAVE_SHOT_HEAD, SAVE_SHOT_HEAD_LEN);
		memcpy(newData.get() + SAVE_SHOT_HEAD_LEN, &w, 4);
		memcpy(newData.get() + SAVE_SHOT_HEAD_LEN + 4, &h, 4);
		int nil = 0xFFFF;
		memcpy(newData.get() + SAVE_SHOT_HEAD_LEN + 8, &nil, 4);
		File::writeFile(imageName, newData, len + SAVE_SHOT_HEAD_LEN + 12);
		
	}
	//engine->freeImage(snap);

}
