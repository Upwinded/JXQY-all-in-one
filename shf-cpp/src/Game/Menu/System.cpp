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
	title = addImageContainer("ini\\ui\\system\\Title.ini");
	returnBtn = addButton("ini\\ui\\system\\Return.ini");
	saveloadBtn = addButton("ini\\ui\\system\\Saveload.ini");
	optionBtn = addButton("ini\\ui\\system\\Option.ini");
	quitBtn = addButton("ini\\ui\\system\\Quit.ini");

	setChildRect();
}

void System::freeResource()
{
	if (impImage != NULL)
	{
		IMP::clearIMPImage(impImage);
		delete impImage;
		impImage = NULL;
	}
	freeCom(title);
	freeCom(returnBtn);
	freeCom(saveloadBtn);
	freeCom(optionBtn);
	freeCom(quitBtn);
}

void System::onEvent()
{
	if (saveloadBtn != NULL && saveloadBtn->getResult(erClick))
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
			GameManager::getInstance()->loadGameWithThread(index + 1);
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
	if (optionBtn != NULL && optionBtn->getResult(erClick))
	{
		Option * option = new Option;
		option->priority = epMax;
		addChild(option);
		option->run();
		removeChild(option);
		delete option;
		option = NULL;
	}
	if (returnBtn != NULL && returnBtn->getResult(erClick))
	{
		running = false;
		result = erOK;
	}
	if (quitBtn != NULL && quitBtn->getResult(erClick))
	{
		running = false;
		result = erExit;
	}

}

bool System::onHandleEvent(AEvent * e)
{
	if (e->eventType == QUIT)
	{
		running = false;
		result = erExit;
		return true;
	}
	else if (e->eventType == KEYDOWN)
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
	int w = 267;
	int h = 200;
	_image snap = engine->beginSaveScreen();
	gm->map.drawMap();
	gm->weather.draw();
	engine->endSaveScreen();
	char * data = NULL;
	int len = engine->saveImageToBMP16(snap, w, h, &data);
	if (len > 0 && data != NULL)
	{
		char * newData = new char[len + BMP16HeadLen + 12];
		memcpy(newData + BMP16HeadLen + 12, data, len);
		delete[] data;
		memcpy(newData, BMP16Head, BMP16HeadLen);
		memcpy(newData + BMP16HeadLen, &w, 4);
		memcpy(newData + BMP16HeadLen + 4, &h, 4);
		int nil = 0xFFFF;
		memcpy(newData + BMP16HeadLen + 8, &nil, 4);
		File::writeFile(imageName, newData, len + BMP16HeadLen + 12);
		
	}
	engine->freeImage(snap);

}
