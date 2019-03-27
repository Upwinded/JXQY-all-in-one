#include "GameManager.h"
#include <thread>

GameManager * GameManager::this_ = NULL;

GameManager::GameManager()
{
	this_ = this;
	name = "GameManager";

	drawFullScreen = true;
	rectFullScreen = true;
	priority = epMap;
	result = erNone;

	addChild(&controller);
	addChild(&menu);
	addChild(&btmWnd);
	addChild(&weather);

	controller.addChild(&camera);
	controller.addChild(&player);
	controller.addChild(&map);
	controller.addChild(&npcManager);
	controller.addChild(&objectManager);
	controller.addChild(&effectManager);
}

GameManager::~GameManager()
{
	removeAllChild();
}

void GameManager::initMenuWithThread()
{
	loadThreadOver = false;
	std::thread t(&GameManager::initMenuThread, this);

	t.detach();
	unsigned int beginTime = engine->getTime();

	loadMutex.lock();
	while (!loadThreadOver)
	{
		loadMutex.unlock();
		engine->frameBegin();
		int w, h;
		engine->getWindowSize(&w, &h);
		std::wstring ws = L"载入中";
		unsigned int t = (engine->getTime() - beginTime) % 1200 / 300;
		if (t == 1)
		{
			ws += L".";
		}
		else if (t == 2)
		{
			ws += L"..";
		}
		else if (t == 3)
		{
			ws += L"...";
		}
		engine->drawUnicodeText(ws, w - 220, h - 70, 50, 0xFFFFFFFF);
		engine->frameEnd();
		loadMutex.lock();
	}
	loadMutex.unlock();
}

void GameManager::initMenuThread()
{
	this_->initMenu();
	loadMutex.lock();
	this_->loadThreadOver = true;
	loadMutex.unlock();
}

void GameManager::initMenu()
{
	messageBox = new MsgBox;
	menu.addChild(messageBox);
	stateMenu = new StateMenu;
	menu.addChild(stateMenu);
	toolTip = new ToolTip;
	memoMenu = new MemoMenu;
	menu.addChild(memoMenu);
	equipMenu = new EquipMenu;
	menu.addChild(equipMenu);
	practiceMenu = new PracticeMenu;
	menu.addChild(practiceMenu);
	goodsMenu = new GoodsMenu;
	menu.addChild(goodsMenu);
	magicMenu = new MagicMenu;
	menu.addChild(magicMenu);

	bottomMenu = new BottomMenu;
	btmWnd.addChild(bottomMenu);
	columnMenu = new ColumnMenu;
	btmWnd.addChild(columnMenu);
}

GameManager * GameManager::getInstance()
{
	return this_;
}

void GameManager::loadGameWithThread(int index)
{
	inThread = true;
	loadThreadOver = false;
	std::thread t(&GameManager::loadGameThread, this, index);
	
	t.detach();
	unsigned int beginTime = engine->getTime();
	
	loadMutex.lock();
	while (!loadThreadOver)
	{
		loadMutex.unlock();
		engine->frameBegin();
		int w, h;
		engine->getWindowSize(&w, &h);
		std::wstring ws = L"载入中";
		unsigned int t = (engine->getTime() - beginTime) % 1200 / 300;
		if (t == 1)
		{
			ws += L".";
		}
		else if (t == 2)
		{
			ws += L"..";
		}
		else if (t == 3)
		{
			ws += L"...";
		}
		engine->drawUnicodeText(ws, w - 220, h - 70, 50, 0xFFFFFFFF);
		engine->frameEnd();
		loadMutex.lock();
	}
	loadMutex.unlock();
	inThread = false;
	bottomMenu->updateGoodsItem();
	bottomMenu->updateMagicItem();

	goodsMenu->scrollbar->setPosition(goodsMenu->scrollbar->min);
	goodsMenu->updateGoods();

	magicMenu->scrollbar->setPosition(magicMenu->scrollbar->min);
	magicMenu->updateMagic();

	practiceMenu->updateMagic();
	equipMenu->updateGoods();
	goodsMenu->updateMoney();
	memoMenu->reRange((int)memo.memo.size() > 0 ? (int)memo.memo.size() - 1 : 0);
	memoMenu->reset();
}

void GameManager::setMapPos(int x, int y)
{
	camera.followPlayer = false;
	camera.position = { x + 5, y + 15};
	camera.offset = { 0, 0 };
}

void GameManager::setMapTrap(int idx, const std::string & trapFile)
{
	traps.set(mapName, idx, trapFile);
}

void GameManager::saveMapTrap()
{
	traps.save();
}

void GameManager::setMapTime(unsigned char t)
{
	global.data.mapTime = t;
	weather.setTime(t);
}

void GameManager::loadGameThread(int index)
{
	this_->loadGame(index);
	loadMutex.lock();
	this_->loadThreadOver = true;
	loadMutex.unlock();
}

bool GameManager::loadGame(int index)
{
	if (index < 0)
	{
		return false;
	}
	initAllTime();
	engine->setTimePaused(&timer, true);
	convert::deleteAll(DEFAULT_FOLDER);
	convert::copyTo(convert::formatString(SAVE_FOLDER, index), DEFAULT_FOLDER);
	global.load();
	std::string tempNpcName = global.data.npcName;
	std::string tempObjName = global.data.objName;
	loadMap(global.data.mapName);
	global.data.npcName = tempNpcName;
	global.data.objName = tempObjName;

	effectManager.freeResource();

	magicManager.load();
	goodsManager.load();

	varList.load();
	memo.load();
	//traps.load();

	player.load();
	partnerManager.load();
	npcManager.load(global.data.npcName);
	objectManager.load(global.data.objName);

	effectManager.load();
	if (map.data != NULL)
	{
		map.createDataMap();
	}
	
	weather.reset();
	clearMenu();

	if (global.data.snowShow)
	{
		weather.setWeather(wtSnow);
	}
	else if (global.data.rainShow)
	{
		weather.setWeather(wtLightning);
	}
	weather.setFadeLum(global.data.fadeLum);
	weather.setLum(global.data.mainLum);
	weather.setTime(global.data.mapTime);
	engine->setTimePaused(&timer, false);
	playMusic(global.data.bgmName);
	if (!inThread)
	{
		bottomMenu->updateGoodsItem();
		bottomMenu->updateMagicItem();

		goodsMenu->scrollbar->setPosition(goodsMenu->scrollbar->min);
		goodsMenu->updateGoods();

		magicMenu->scrollbar->setPosition(magicMenu->scrollbar->min);
		magicMenu->updateMagic();

		practiceMenu->updateMagic();
		equipMenu->updateGoods();
		goodsMenu->updateMoney();
		memoMenu->reRange((int)memo.memo.size() > 0 ? (int)memo.memo.size() - 1 : 0);
		memoMenu->reset();
	}
	return true;
}

void GameManager::saveGame(int index)
{
	global.save();
	varList.save();
	memo.save();
	traps.save();
	player.save();
	partnerManager.save();
	magicManager.save();
	goodsManager.save();
	npcManager.save(global.data.npcName);
	objectManager.save(global.data.objName);
	effectManager.save();
	if (index > 0)
	{
		convert::deleteAll(convert::formatString(SAVE_FOLDER, index));
		convert::copyTo(DEFAULT_FOLDER, convert::formatString(SAVE_FOLDER, index));
	}
}

void GameManager::clearMenu()
{
	practiceMenu->visible = false;
	equipMenu->visible = false;
	stateMenu->visible = false;
	magicMenu->visible = false;
	memoMenu->visible = false;
	goodsMenu->visible = false;
	bottomMenu->magicBtn->checked = false;
	bottomMenu->goodsBtn->checked = false;
	bottomMenu->notesBtn->checked = false;
	bottomMenu->stateBtn->checked = false;
	bottomMenu->xiulianBtn->checked = false;
	bottomMenu->equipBtn->checked = false;
}

bool GameManager::menuDisplayed()
{
	bool vis = false;
#define addMenuVis(m) \
	if (m != NULL)\
	{\
		vis = vis || m->visible;\
	}
	addMenuVis(stateMenu)
	addMenuVis(memoMenu);
	addMenuVis(equipMenu);
	addMenuVis(practiceMenu);
	addMenuVis(goodsMenu);
	addMenuVis(magicMenu);
	return vis;
}


Point GameManager::getMousePoint()
{
	int w, h;
	engine->getWindowSize(&w, &h);
	Point cenScreen;
	cenScreen.x = (int)w / 2;
	cenScreen.y = (int)h / 2;
	int xscal, yscal;
	xscal = cenScreen.x / TILE_WIDTH + 2;
	yscal = cenScreen.y / TILE_HEIGHT * 2 + 2;
	int tileHeightScal = 10;
	Point cenTile = camera.position;
	int x, y;
	engine->getMouse(&x, &y);
	Point pos = map.getMousePosition({ x, y }, camera.position, cenScreen, camera.offset);
	return pos;
}

void GameManager::loadMapThread(const std::string & fileName)
{
	this_->loadMap(fileName);
	loadMutex.lock();
	this_->loadThreadOver = true;
	loadMutex.unlock();
}

void GameManager::loadNPCThread(const std::string & fileName)
{
	this_->loadNPC(fileName);
	loadMutex.lock();
	this_->loadThreadOver = true;
	loadMutex.unlock();
}

void GameManager::loadObjectThread(const std::string & fileName)
{
	this_->loadObject(fileName);
	loadMutex.lock();
	this_->loadThreadOver = true;
	loadMutex.unlock();
}

void GameManager::runScript(const std::string & fileName)
{
	clearSelected();
	char * s = NULL;
	int len = PakFile::readFile(SCRIPT_MAP_FOLDER + mapName + "\\" + fileName, &s);
	if (len <= 0 || s == NULL)
	{
		s = NULL;
		int len = PakFile::readFile(SCRIPT_GOODS_FOLDER + fileName, &s);
		if (len <= 0 || s == NULL)
		{
			s = NULL;
			int len = PakFile::readFile(SCRIPT_COMMON_FOLDER + fileName, &s);
			if (len <= 0 || s == NULL)
			{
				return;
			}
			printf("run script: %s%s\n", SCRIPT_COMMON_FOLDER, fileName.c_str());
			script.runScript(s, len);
			delete[] s;
			return;
		}
		printf("run script: %s%s\n", SCRIPT_GOODS_FOLDER, fileName.c_str());
		script.runScript(s, len);
		delete[] s;
		return;
	}
	printf("run script: %s%s\\%s\n", SCRIPT_MAP_FOLDER , mapName.c_str(), fileName.c_str());
	script.runScript(s, len);
	delete[] s;
	return;
}

void GameManager::moveScreen(int direction, int distance)
{
	Point pos = { 0, 0 };
	switch (direction)
	{
	case 0:
		pos.y += distance / 2;
		break;
	case 1:
		pos.y += (int)((double)distance * sqrt(2) / 4);
		pos.x -= (int)((double)distance * sqrt(2) / 2);
		break;
	case 2:
		pos.x -= distance;
		break;
	case 3:
		pos.y -= (int)((double)distance * sqrt(2) / 4);
		pos.x -= (int)((double)distance * sqrt(2) / 2);
		break;
	case 4:
		pos.y -= distance / 2;
		break;
	case 5:
		pos.y -= (int)((double)distance * sqrt(2) / 4);
		pos.x += (int)((double)distance * sqrt(2) / 2);
		break;
	case 6:
		pos.x += distance;
		break;
	case 7:
		pos.y += (int)((double)distance * sqrt(2) / 4);
		pos.x += (int)((double)distance * sqrt(2) / 2);
		break;
	default:
		break;
	}
	pos.y -= TILE_HEIGHT / 2;
	Point dest = map.getMousePosition(pos, camera.position, { 0, 0 }, { 0, 0 });
	camera.flyTo(dest);
}

void GameManager::loadMap(const std::string & fileName)
{

	global.data.mapName = fileName;
	mapName = convert::extractFileName(fileName);
	map.load(MAP_FOLDER + fileName);
	camera.followPlayer = true;
	npcManager.clearNPC();
	objectManager.clearObj();
	global.data.npcName = "";
	global.data.objName = "";
	map.createDataMap();
	traps.load();
	player.canFight = true;
	player.beginStand();
}

void GameManager::loadMapWithThread(const std::string & fileName)
{
	loadThreadOver = false;

	std::thread t(&GameManager::loadMapThread, this, fileName);

	t.detach();
	unsigned int beginTime = engine->getTime();

	loadMutex.lock();
	while (!loadThreadOver)
	{
		loadMutex.unlock();
		engine->frameBegin();
		int w, h;
		engine->getWindowSize(&w, &h);
		std::wstring ws = L"载入地图中";
		unsigned int t = (engine->getTime() - beginTime) % 1200 / 300;
		if (t == 1)
		{
			ws += L".";
		}
		else if (t == 2)
		{
			ws += L"..";
		}
		else if (t == 3)
		{
			ws += L"...";
		}
		engine->drawUnicodeText(ws, w - 300, h - 70, 50, 0xFFFFFFFF);
		engine->frameEnd();
		loadMutex.lock();
	}
	loadMutex.unlock();

}

void GameManager::playMusic(const std::string & fileName)
{
	engine->stopBGM();
	global.data.bgmName = fileName;
	if (fileName == "")
	{
		return;
	}
	char * s = NULL;
	int len = PakFile::readFile(MUSIC_FOLDER + fileName, &s);
	if (s != NULL && len > 0)
	{
		engine->loadBGM(s, len);
		delete[] s;
		engine->playBGM();
	}
}

void GameManager::stopMusic()
{
	engine->stopBGM();
	global.data.bgmName = "";
}

void GameManager::playSound(const std::string & fileName)
{
	if (fileName == "")
	{
		return;
	}
	std::string soundName = SOUND_FOLDER + fileName;
	char * s = NULL;
	int len = PakFile::readFile(soundName, &s);
	if (len > 0 && s != NULL)
	{
		engine->playSound(s, len);
		delete s;
	}
}

void GameManager::returnToDesktop()
{
	result = erExit;
	running = false;
}

void GameManager::clearSelected()
{
	npcManager.clearSelected();
	objectManager.clearSelected();
}

void GameManager::returnToTitle()
{
	result = erOK;
	running = false;
}

void GameManager::enableInput()
{
	global.data.canInput = true;
}

void GameManager::disableInput()
{
	global.data.canInput = false;
}

void GameManager::runObjScript(Object * obj)
{
	delete player.nextAction;
	player.nextAction = NULL;
	player.nextDest = ndNone;
	player.destGE = NULL;
	if (inEvent)
	{
		return;
	}
	if (obj != NULL)
	{
		scriptObj = obj;
		inEvent = true;
		effectManager.disableAllEffect();
		scriptType = stObject;
		runScript(obj->scriptFile);
		scriptType = stNone;
		inEvent = false;
	}
	scriptObj = NULL;
	camera.followPlayer = true;
	if (eventList.size() > 0)
	{
		NPC * deathNPC = eventList[0].deathNPC;
		eventList.erase(eventList.begin());
		runNPCDeathScript(deathNPC);
	}
}

void GameManager::runNPCScript(NPC * npc)
{
	delete player.nextAction;
	player.nextAction = NULL;
	player.nextDest = ndNone;
	player.destGE = NULL;
	if (inEvent)
	{
		return;
	}
	if (npc != NULL)
	{
		scriptNPC = npc;
		inEvent = true;
		effectManager.disableAllEffect();
		scriptType = stNPC;
		runScript(npc->scriptFile);
		scriptType = stNone;
		inEvent = false;
	}
	scriptNPC = NULL;
	camera.followPlayer = true;
	if (eventList.size() > 0)
	{
		NPC * deathNPC = eventList[0].deathNPC;
		eventList.erase(eventList.begin());
		runNPCDeathScript(deathNPC);
	}
}

void GameManager::runNPCDeathScript(NPC * npc)
{
	delete player.nextAction;
	player.nextAction = NULL;
	player.nextDest = ndNone;
	player.destGE = NULL;
	if (inEvent)
	{
		if (npc != NULL)
		{
			EventInfo eventInfo;
			eventInfo.deathNPC = npc;
			eventList.push_back(eventInfo);
		}
		return;
	}
	if (npc != NULL)
	{
		scriptNPC = npc;
		inEvent = true;
		//effectManager.disableAllEffect();
		scriptType = stNPCDeath;
		runScript(npc->deathScript);
		scriptType = stNone;
		inEvent = false;
	}
	scriptNPC = NULL;
	camera.followPlayer = true;
	if (eventList.size() > 0)
	{
		NPC * deathNPC = eventList[0].deathNPC;
		eventList.erase(eventList.begin());
		runNPCDeathScript(deathNPC);
	}
}

void GameManager::runGoodsScript(Goods * goods)
{
	delete player.nextAction;
	player.nextAction = NULL;
	player.nextDest = ndNone;
	player.destGE = NULL;
	if (!(player.isJumping() && player.jumpState == jsJumping))
	{
		player.beginStand();
	}
	if (inEvent)
	{
		return;
	}
	if (goods != NULL)
	{
		scriptGoods = goods;
		inEvent = true;
		effectManager.disableAllEffect();
		scriptType = stGoods;
		runScript(goods->script);
		scriptType = stNone;
		inEvent = false;
	}
	scriptGoods = NULL;
	camera.followPlayer = true;
	if (eventList.size() > 0)
	{
		NPC * deathNPC = eventList[0].deathNPC;
		eventList.erase(eventList.begin());
		runNPCDeathScript(deathNPC);
	}
}

void GameManager::runTrapScript(int idx)
{
	delete player.nextAction;
	player.nextAction = NULL;
	player.nextDest = ndNone;
	player.destGE = NULL;
	if (!(player.isJumping() && player.jumpState == jsJumping))
	{
		player.beginStand();
	}
	if (inEvent)
	{
		return;
	}
	if (idx == 0)
	{
		return;
	}
	scriptMapName = mapName;
	scriptTrapIndex = idx;
	std::string sname = traps.get(mapName, idx);
	if (sname != "")
	{
		inEvent = true;
		effectManager.disableAllEffect();
		traps.set(mapName, idx, "");
		std::string tempMapName = mapName;
		scriptType = stTraps;
		runScript(sname);
		scriptType = stNone;
		inEvent = false;
	}
	scriptMapName = "";
	scriptTrapIndex = 0;
	camera.followPlayer = true;
	if (eventList.size() > 0)
	{
		NPC * deathNPC = eventList[0].deathNPC;
		eventList.erase(eventList.begin());
		runNPCDeathScript(deathNPC);
	}
}

void GameManager::playMovie(const std::string & fileName)
{
	if (video != NULL)
	{
		delete video;
		video = NULL;
	}
	stopMusic();
	printf("Play Movie %s\n", fileName.c_str());
	video = new VideoPlayer(VIDEO_FOLDER + fileName);
	video->drawFullScreen = true;
	video->run();
	delete video;
	video = NULL;
	playMusic(global.data.bgmName);
}

void GameManager::stopMovie()
{
	engine->stopVideo(video->v);
}

void GameManager::loadNPC(const std::string & fileName)
{
	//npcManager.save(global.data.npcName);
	global.data.npcName = fileName;
	npcManager.load(global.data.npcName);
	map.createDataMap();
}

void GameManager::loadNPCWithThread(const std::string & fileName)
{
	loadThreadOver = false;

	std::thread t(&GameManager::loadNPCThread, this, fileName);

	t.detach();
	unsigned int beginTime = engine->getTime();

	loadMutex.lock();
	while (!loadThreadOver)
	{
		loadMutex.unlock();
		engine->frameBegin();
		int w, h;
		engine->getWindowSize(&w, &h);
		std::wstring ws = L"载入资源中";
		unsigned int t = (engine->getTime() - beginTime) % 1200 / 300;
		if (t == 1)
		{
			ws += L".";
		}
		else if (t == 2)
		{
			ws += L"..";
		}
		else if (t == 3)
		{
			ws += L"...";
		}
		engine->drawUnicodeText(ws, w - 300, h - 70, 50, 0xFFFFFFFF);
		engine->frameEnd();
		loadMutex.lock();
	}
	loadMutex.unlock();
}

void GameManager::saveNPC(const std::string & fileName)
{
	if (fileName == "")
	{
		npcManager.save(global.data.npcName);
	}
	else
	{
		npcManager.save(fileName);
	}
}

void GameManager::addNPC(const std::string & iniName, int x, int y, int dir)
{
	npcManager.addNPC(iniName, x, y, dir);
}

void GameManager::deleteNPC(const std::string & name)
{
	npcManager.deleteNPC(name);
}

void GameManager::setNPCRes(const std::string & name, const std::string & resName)
{
	if (name == "")
	{
		NPC * npc = scriptNPC;
		if (npc != NULL)
		{
			npc->npcIni = resName;
			npc->initRes(resName);
		}
	}
	else
	{
		auto npc = npcManager.findNPC(name);
		for (size_t i = 0; i < npc.size(); i++)
		{
			npc[i]->npcIni = resName;
			npc[i]->initRes(resName);
		}
	}
}

void GameManager::setNPCScript(const std::string & name, const std::string & scriptName)
{
	if (name == "")
	{
		NPC * npc = scriptNPC;
		if (npc != NULL)
		{
			npc->scriptFile = scriptName;
		}
	}
	else
	{
		auto npc = npcManager.findNPC(name);
		for (size_t i = 0; i < npc.size(); i++)
		{
			npc[i]->scriptFile = scriptName;
		}
	}
}

void GameManager::setNPCDeathScript(const std::string & name, const std::string & scriptName)
{
	if (name == "")
	{
		NPC * npc = scriptNPC;
		if (npc != NULL)
		{
			npc->deathScript = scriptName;
		}
	}
	else
	{
		auto npc = npcManager.findNPC(name);
		for (size_t i = 0; i < npc.size(); i++)
		{
			npc[i]->deathScript = scriptName;
		}
	}
}

void GameManager::npcGoto(const std::string & name, int x, int y)
{
	if (name == "")
	{
		NPC * npc = scriptNPC;
		if (npc != NULL)
		{
			npc->npcGoto({ x, y });
		}
	}
	else
	{
		auto npc = npcManager.findNPC(name);
		for (size_t i = 0; i < npc.size(); i++)
		{
			npc[i]->npcGoto({ x, y });
		}
	}
}

void GameManager::npcGotoEx(const std::string & name, int x, int y)
{
	if (name == "")
	{
		NPC * npc = scriptNPC;
		if (npc != NULL)
		{
			npc->npcGotoEx({ x, y });
		}
	}
	else
	{
		auto npc = npcManager.findNPC(name);
		for (size_t i = 0; i < npc.size(); i++)
		{
			npc[i]->npcGotoEx({ x, y });
		}
	}
}

void GameManager::npcGotoDir(const std::string & name, int dir, int distance)
{
	if (name == "")
	{
		NPC * npc = scriptNPC;
		if (npc != NULL)
		{
			npc->npcGotoDir(dir, distance);
		}
	}
	else
	{
		auto npc = npcManager.findNPC(name);
		for (size_t i = 0; i < npc.size(); i++)
		{
			npc[i]->npcGotoDir(dir, distance);
		}
	}
}

void GameManager::followNPC(const std::string & follower, const std::string & leader)
{
	auto npc = npcManager.findNPC(follower);
	for (size_t i = 0; i < npc.size(); i++)
	{
		npc[i]->followNPC = leader;
	}
}

void GameManager::followPlayer(const std::string & follower)
{
	auto npc = npcManager.findNPC(follower);
	for (size_t i = 0; i < npc.size(); i++)
	{
		npc[i]->followNPC = gm->player.name;
	}
}

void GameManager::enableNPCAI()
{
	global.data.NPCAI = true;
}

void GameManager::disableNPCAI()
{
	global.data.NPCAI = false;
	//npcManager.standAll();
}

void GameManager::npcAttack(const std::string & name, int x, int y)
{
	if (name == "")
	{
		NPC * npc = scriptNPC;
		if (npc != NULL)
		{
			npc->beginAttack({ x, y });
			//npc->eventRun();
		}
	}
	else
	{
		auto npc = npcManager.findNPC(name);
		for (size_t i = 0; i < npc.size(); i++)
		{
			npc[i]->beginAttack({ x, y });
			//npc[i]->eventRun();
		}

	}
}

void GameManager::setNPCPosition(const std::string & name, int x, int y)
{
	if (name == "")
	{
		NPC * npc = scriptNPC;
		if (npc != NULL)
		{
			npc->haveDest = false;
			npc->beginStand();
			npc->position = { x, y };
			npc->offset = { 0, 0 };
		}
	}
	else
	{
		auto npc = npcManager.findNPC(name);
		for (size_t i = 0; i < npc.size(); i++)
		{
			npc[i]->beginStand();
			npc[i]->haveDest = false;
			npc[i]->position = { x, y };
			npc[i]->offset = { 0, 0 };
		}
	}
	map.createDataMap();
}

void GameManager::setNPCDir(const std::string & name, int dir)
{
	if (name == "")
	{
		NPC * npc = scriptNPC;
		if (npc != NULL && npc->isStanding())
		{
			npc->beginStand();
			npc->haveDest = false;
			npc->direction = dir;
		}
	}
	else
	{
		auto npc = npcManager.findNPC(name);
		for (size_t i = 0; i < npc.size(); i++)
		{
			if (npc[i]->isStanding())
			{
				npc[i]->haveDest = false;
				npc[i]->direction = dir;
			}
		}
	}
}

void GameManager::setNPCKind(const std::string & name, int kind)
{
	if (name == "")
	{
		NPC * npc = scriptNPC;
		if (npc != NULL)
		{
			npc->kind = kind;
		}
	}
	else
	{
		auto npc = npcManager.findNPC(name);
		for (size_t i = 0; i < npc.size(); i++)
		{
			npc[i]->kind = kind;
		}
	}
}

void GameManager::setNPCLevel(const std::string & name, int level)
{
	if (name == "")
	{
		NPC * npc = scriptNPC;
		if (npc != NULL)
		{
			npc->level = level;
			npc->setLevel(level);
		}
	}
	else
	{
		auto npc = npcManager.findNPC(name);
		for (size_t i = 0; i < npc.size(); i++)
		{
			npc[i]->level = level;
			npc[i]->setLevel(level);
		}
	}
}

void GameManager::setNPCAction(const std::string & name, int action)
{
	if (name == "")
	{
		NPC * npc = scriptNPC;
		if (npc != NULL)
		{
			if (action == acStand)
			{
				npc->beginStand();
			}
			else if (action == acDeath)
			{
				npc->beginDie();
			}
		}
	}
	else
	{
		auto npc = npcManager.findNPC(name);
		for (size_t i = 0; i < npc.size(); i++)
		{
			if (action == acStand)
			{
				npc[i]->beginStand();
			}
			else if (action == acDeath)
			{
				npc[i]->beginDie();
			}
		}
	}
}

void GameManager::setNPCRelation(const std::string & name, int relation)
{
	if (name == "")
	{
		NPC * npc = scriptNPC;
		if (npc != NULL)
		{
			npc->relation = relation;
			npc->beginStand();
		}
	}
	else
	{
		auto npc = npcManager.findNPC(name);
		for (size_t i = 0; i < npc.size(); i++)
		{
			npc[i]->relation = relation;
			npc[i]->beginStand();
		}
	}
}

void GameManager::setNPCActionType(const std::string & name, int actionType)
{
	if (name == "")
	{
		NPC * npc = scriptNPC;
		if (npc != NULL)
		{
			npc->action = actionType;
		}
	}
	else
	{
		auto npc = npcManager.findNPC(name);
		for (size_t i = 0; i < npc.size(); i++)
		{
			npc[i]->action = actionType;
		}
	}
}

void GameManager::setNPCActionFile(const std::string & name, int action, const std::string & fileName)
{
	if (name == "")
	{
		NPC * npc = scriptNPC;
		if (npc != NULL)
		{
			npc->loadActionFile(fileName, action);
		}
	}
	else
	{
		auto npc = npcManager.findNPC(name);
		for (size_t i = 0; i < npc.size(); i++)
		{
			npc[i]->loadActionFile(fileName, action);
		}
	}

}

void GameManager::npcSpecialAction(const std::string & name, const std::string & fileName)
{
	if (name == "")
	{
		NPC * npc = scriptNPC;
		if (npc != NULL)
		{
			npc->doSpecialAction(fileName);
		}
	}
	else
	{
		auto npc = npcManager.findNPC(name);
		for (size_t i = 0; i < npc.size(); i++)
		{
			npc[i]->doSpecialAction(fileName);
		}
	}
}

void GameManager::loadPlayer(const std::string & fileName)
{
	player.load(fileName);
}

void GameManager::savePlayer(const std::string & fileName)
{
	player.save(fileName);
}

void GameManager::setPlayerPosition(int x, int y)
{
	player.beginStand();
	player.position = { x, y };
	player.offset = { 0, 0 };
	player.haveDest = false;
	gm->npcManager.setPartnerPos(x, y, player.direction);
	map.createDataMap();
}

void GameManager::setPlayerDir(int dir)
{
	player.direction = dir;
}

void GameManager::setPlayerScn()
{
	camera.followPlayer = true;
}

void GameManager::setLevelFile(const std::string & fileName)
{
	player.levelIni = fileName;
	player.loadLevel(fileName);
}

void GameManager::setMagicLevel(const std::string & magicName, int level)
{
	MagicInfo * m = magicManager.findMagic(magicName);
	if (m != NULL)
	{
		m->level = level;
	}
}

void GameManager::setPlayerLevel(int level)
{
	player.setLevel(level);
}

void GameManager::setPlayerState(int state)
{
	player.setFight(state);
}

void GameManager::enableRun()
{
	player.canRun = true;
}

void GameManager::disableRun()
{
	player.canRun = false;
}

void GameManager::enableJump()
{
	player.canJump = true;
}

void GameManager::disableJump()
{
	player.canJump = false;
}

void GameManager::enableFight()
{
	player.canFight = true;
}

void GameManager::disableFight()
{
	player.canFight = false;
}

void GameManager::playerGoto(int x, int y)
{
	player.npcGoto({ x, y });
}

void GameManager::playerGotoEx(int x, int y)
{
	player.npcGotoEx({ x, y });
}

void GameManager::playerRunTo(int x, int y)
{
	player.npcRunTo({ x, y });
}

void GameManager::playerJumpTo(int x, int y)
{
	player.npcJumpTo({ x, y });
}

void GameManager::playerGotoDir(int dir, int distance)
{
	player.npcGotoDir(dir, distance);
}

void GameManager::addLife(int value)
{
	player.addLife(value);
}

void GameManager::addLifeMax(int value)
{
	player.addLifeMax(value);
}

void GameManager::addThew(int value)
{
	player.addThew(value);
}

void GameManager::addThewMax(int value)
{
	player.addThewMax(value);
}

void GameManager::addMana(int value)
{
	player.addMana(value);
}

void GameManager::addManaMax(int value)
{
	player.addManaMax(value);
}

void GameManager::addAttack(int value)
{
	player.addAttack(value);
}

void GameManager::addDefend(int value)
{
	player.addDefend(value);
}

void GameManager::addEvade(int value)
{
	player.addEvade(value);
}

void GameManager::addExp(int value)
{
	player.addExp(value);
}

void GameManager::addMoney(int value)
{
	if (value == 0)
	{
		return;
	}
	player.addMoney(value);
	if (value > 0)
	{
		showMessage(convert::formatString("获得%d两银子！", value));
	}
	else
	{
		showMessage(convert::formatString("失去%d两银子！", -value));
	}
}

void GameManager::addRandMoney(int mMin, int mMax)
{
	int value = 0;
	if (mMax == mMin)
	{
		value = mMin;
	}
	else
	{
		value = rand() % std::abs(mMax - mMin) + (mMax > mMin ? mMin : mMax);
	}
	addMoney(value);
}

void GameManager::addGoods(const std::string & name)
{
	goodsManager.addItem(name, 1);
}

void GameManager::addRandGoods(const std::string & fileName)
{
	std::string str = BUYSELL_FOLDER;
	str += fileName;
	char * s = NULL;
	int len = PakFile::readFile(str, &s);
	if (len > 0 && s != NULL)
	{
		INIReader ini(s);
		int count = ini.GetInteger("Header", "Count", 0);
		if (count > 0)
		{
			int idx = rand() % count;
			std::string section = convert::formatString("%d", idx + 1);
			std::string name = ini.Get(section, "IniFile", "");
			addGoods(name);
		}
		delete s;
	}
	
}

void GameManager::deleteGoods(const std::string & name)
{
	goodsManager.deleteItem(name);
}

void GameManager::addMagic(const std::string & name)
{
	magicManager.addMagic(name);
}

void GameManager::deleteMagic(const std::string & name)
{
	magicManager.deleteMagic(name);
}

void GameManager::addMagicExp(const std::string & name, int addexp)
{
	magicManager.addMagicExp(name, addexp);
}

void GameManager::fullLife()
{
	player.fullLife();
}

void GameManager::fullThew()
{
	player.fullThew();
}

void GameManager::fullMana()
{
	player.fullMana();
}

void GameManager::updateState()
{
	gm->equipMenu->updateGoods();
	gm->stateMenu->updateLabel();
}

void GameManager::saveGoods(const std::string & fileName)
{
	goodsManager.save(fileName);
}

void GameManager::loadGoods(const std::string & fileName)
{
	goodsManager.load(fileName);
}

void GameManager::clearGoods()
{
	goodsManager.clearItem();
}

void GameManager::getGoodsNum(const std::string & name)
{
	varList.setInteger("GoodsNum", goodsManager.getItemNum(name));
}

void GameManager::getMoneyNum()
{
	varList.setInteger("MoneyNum", player.money);
}

void GameManager::setMoneyNum(int value)
{
	player.money = value;
	goodsMenu->updateMoney();
}

void GameManager::loadObject(const std::string & fileName)
{
	//objectManager.save(global.data.objName);
	global.data.objName = fileName;
	objectManager.load(global.data.objName);
	map.createDataMap();
}

void GameManager::loadObjectWithThread(const std::string & fileName)
{
	loadThreadOver = false;

	std::thread t(&GameManager::loadObjectThread, this, fileName);

	t.detach();
	unsigned int beginTime = engine->getTime();

	loadMutex.lock();
	while (!loadThreadOver)
	{
		loadMutex.unlock();
		engine->frameBegin();
		int w, h;
		engine->getWindowSize(&w, &h);
		std::wstring ws = L"载入资源中";
		unsigned int t = (engine->getTime() - beginTime) % 1200 / 300;
		if (t == 1)
		{
			ws += L".";
		}
		else if (t == 2)
		{
			ws += L"..";
		}
		else if (t == 3)
		{
			ws += L"...";
		}
		engine->drawUnicodeText(ws, w - 300, h - 70, 50, 0xFFFFFFFF);
		engine->frameEnd();
		loadMutex.lock();
	}
	loadMutex.unlock();
}

void GameManager::saveObject(const std::string & fileName)
{
	if (fileName == "")
	{
		objectManager.save(global.data.objName);
	}
	else
	{
		objectManager.save(fileName);
	}
	
}

void GameManager::addObject(const std::string & iniName, int x, int y, int dir)
{
	objectManager.addObject(iniName, x, y, dir);
}

void GameManager::deleteObject(const std::string & name)
{
	std::string objName = name;
	if (name == "")
	{
		if (scriptObj != NULL)
		{
			objName = scriptObj->name;
		}
		else
		{
			return;
		}
	}
	objectManager.deleteObject(objName);
}

void GameManager::setObjectPosition(const std::string & name, int x, int y)
{
	Object * obj = NULL;
	if (name == "")
	{
		obj = scriptObj;
	}
	else
	{
		obj = objectManager.findObj(name);
	}
	if (obj != NULL)
	{
		obj->position = { x, y };
		map.createDataMap();
	}
}

void GameManager::setObjectKind(const std::string & name, int kind)
{
	Object * obj = NULL;
	if (name == "")
	{
		obj = scriptObj;
	}
	else
	{
		obj = objectManager.findObj(name);
	}
	if (obj != NULL)
	{
		obj->kind = kind;
	}
}

void GameManager::setObjectScript(const std::string & name, const std::string & scriptFile)
{
	Object * obj = NULL;
	if (name == "")
	{
		obj = scriptObj;
	}
	else
	{
		obj = objectManager.findObj(name);
	}
	if (obj != NULL)
	{
		obj->scriptFile = scriptFile;
	}
}

int GameManager::getVar(const std::string & varName)
{
	return varList.getInteger(varName);
}

void GameManager::assign(const std::string & varName, int value)
{
	varList.setInteger(varName, value);
}

void GameManager::add(const std::string & varName, int value)
{
	assign(varName, getVar(varName) + value);
}

void GameManager::talk(const std::string & part)
{
	std::string fileName = SCRIPT_FOLDER;
	fileName += "map\\"; 
	fileName += mapName + "\\" + TALK_FILE;
	char * s = NULL;
	int len = PakFile::readFile(fileName, &s);
	if (s == NULL || len <= 0)
	{
		return;
	}
	INIReader * ini = new INIReader(s);
	delete[] s;
	std::vector<std::wstring> talkStr;
	talkStr.resize(0);

	int talkIndex = 0;
	while (true)
	{
		std::string name = convert::formatString("%d", talkIndex + 1);
		
		std::string str = ini->Get(part, name, "");
		if (str == "")
		{
			break;
		}
		talkStr.push_back(convert::GBKToUnicode(str));
		talkIndex++;
	}
	
	if (talkIndex <= 0)
	{
		delete ini;
		return;
	}
	Dialog * dialog = new Dialog;
	addChild(dialog);
	std::string head1 = "";
	std::string head2 = "";

	std::string headStr = "head";
	for (int i = 0; i < talkIndex; i++)
	{
		std::string tstr = convert::formatString("%s%d", headStr.c_str(), i + 1);
		std::string thstr = ini->Get(part, tstr, "NoHeadChange");
		if (thstr != "NoHeadChange")
		{
			if (i % 2 == 1)
			{
				head1 = thstr;
			}
			else
			{
				head2 = thstr;
			}
		}
		if (i % 2 == 1)
		{
			dialog->setHead1(head1);
		}
		else
		{
			dialog->setHead2(head2);
		}
		dialog->setTalkStr(talkStr[i]);
		std::string s = convert::unicodeToGBK(talkStr[i]);
		dialog->run();
	}
	removeChild(dialog);
	delete dialog;
	delete ini;
}

void GameManager::say(const std::string & str)
{
	Dialog * dialog = new Dialog;
	addChild(dialog);
	dialog->setHead1("");
	dialog->setTalkStr(convert::GBKToUnicode(str));
	dialog->run();
	removeChild(dialog);
	delete dialog;
}

void GameManager::fadeIn()
{
	weather.fadeIn();
}

void GameManager::fadeOut()
{
	weather.fadeOut();
}

void GameManager::setFadeLum(int lum)
{
	global.data.fadeLum = lum;
	weather.setFadeLum(lum);
}

void GameManager::setMainLum(int lum)
{
	global.data.mainLum = lum;
	weather.setLum(lum);
}

void GameManager::sleep(unsigned int time)
{
	weather.sleep(time);
}

void GameManager::showMessage(const std::string & str)
{
	if (messageBox != NULL)
	{
		messageBox->showMessage(convert::GBKToUnicode(str));
	}
}

void GameManager::addToMemo(const std::string & str)
{
	memo.add(str);
}

void GameManager::clearMemo()
{
	memo.clear();
}

void GameManager::buyGoods(const std::string & fileName)
{
	BuySellMenu * bsMenu = new BuySellMenu;
	addChild(bsMenu);
	bsMenu->buy(fileName);
	removeChild(bsMenu);
	delete bsMenu;
}

void GameManager::sellGoods(const std::string & fileName)
{
	BuySellMenu * bsMenu = new BuySellMenu;
	addChild(bsMenu);
	bsMenu->sell(fileName);
	removeChild(bsMenu);
	delete bsMenu;
}

void GameManager::hideInterface()
{
	clearMenu();
}

void GameManager::hideBottomWnd()
{
	btmWnd.visible = false;
}

void GameManager::showBottomWnd()
{
	btmWnd.visible = true;
}

void GameManager::hideMouseCursor()
{
	engine->hideMouse();
}

void GameManager::showMouseCursor()
{
	engine->showMouse();
}

void GameManager::showSnow(int bsnow)
{
	if (bsnow)
	{
		global.data.snowShow = true;
		global.data.rainShow = false;
		weather.setWeather(wtSnow);
	}
	else
	{
		global.data.snowShow = false;
		global.data.rainShow = false;
		weather.setWeather(wtNone);
	}
}

void GameManager::showRandomSnow()
{
	showSnow(rand() % 2);
}

void GameManager::showRain(int brain)
{
	if (brain)
	{
		global.data.rainShow = true;
		global.data.snowShow = false;
		weather.setWeather(wtLightning);
	}
	else
	{
		global.data.rainShow = false;
		global.data.snowShow = false;
		weather.setWeather(wtNone);
	}
}

void GameManager::onUpdate()
{
	if (eventList.size() > 0)
	{
		NPC * deathNPC = eventList[0].deathNPC;
		eventList.erase(eventList.begin());
		runNPCDeathScript(deathNPC);
	}
}

void GameManager::onDraw()
{
	map.drawMap();
}

bool GameManager::onInitial()
{
	initMenu();
	if (gameIndex == 0)
	{
		runScript("newGame.txt");
	}
	else
	{
		loadGameWithThread(gameIndex);
		weather.fadeInEx();
	}	
	return true;
}

void GameManager::onRun()
{
	player.checkTrap();
}

#define freeMenu(component); \
	menu.removeChild(component); \
	if (component != NULL)\
	{\
		delete component; \
		component = NULL; \
	}

#define freeBtmMenu(component); \
	btmWnd.removeChild(component); \
	if (component != NULL)\
	{\
		delete component; \
		component = NULL; \
	}


void GameManager::onExit()
{
	freeMenu(messageBox);
	freeMenu(stateMenu);
	freeMenu(toolTip);
	freeMenu(memoMenu);
	freeMenu(equipMenu);
	freeMenu(practiceMenu);
	freeMenu(goodsMenu);
	freeMenu(magicMenu);

	freeBtmMenu(bottomMenu);
	freeBtmMenu(columnMenu);

	effectManager.freeResource();
	partnerManager.freeResource();
	npcManager.freeResource();
	magicManager.freeResource();
	objectManager.freeResource();
	player.freeResource();
	weather.freeResource();
	map.freeResource();

	engine->stopBGM();
}

void GameManager::onEvent()
{
	if (MouseAlreadyDown)
	{
		if (!engine->getMousePress(MOUSE_LEFT))
		{
			MouseAlreadyDown = false;
		}
	}
	if (!dragging && MouseAlreadyDown && engine->getMousePress(MOUSE_LEFT) && mouseMoveIn && !engine->getKeyPress(KEY_LALT) && !engine->getKeyPress(KEY_RALT))
	{
		if (player.nowAction != acDeath && player.nowAction != acHide)
		{
			int w, h;
			engine->getWindowSize(&w, &h);
			Point cenScreen;
			cenScreen.x = (int)w / 2;
			cenScreen.y = (int)h / 2;
			int xscal, yscal;
			xscal = cenScreen.x / TILE_WIDTH + 2;
			yscal = cenScreen.y / TILE_HEIGHT * 2 + 2;
			int tileHeightScal = 10;
			Point cenTile = camera.position;
			int x, y;
			engine->getMouse(&x, &y);
			Point pos = map.getMousePosition({ x, y }, camera.position, cenScreen, camera.offset);
			if (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT))
			{
				NextAction act;
				act.action = acRun;
				act.dest = pos;
				player.addNextAction(&act);
			}
			else
			{
				NextAction act;
				act.action = acWalk;
				act.dest = pos;
				player.addNextAction(&act);
			}
		}
	}
	else if (!dragging && MouseAlreadyDown && engine->getMousePress(MOUSE_LEFT) && npcManager.clickIndex >= 0 && !engine->getKeyPress(KEY_LALT) && !engine->getKeyPress(KEY_RALT))
	{
		if (player.nowAction != acDeath && player.nowAction != acHide)
		{
			NextAction act;
			if (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT))
			{
				act.action = acRun;
			}
			else
			{
				act.action = acWalk;
			}
			act.destGE = npcManager.npcList[npcManager.clickIndex];
			if (npcManager.npcList[npcManager.clickIndex]->kind == nkBattle && npcManager.npcList[npcManager.clickIndex]->relation == nrHostile)
			{
				act.type = ndAttack;
			}
			else
			{
				act.type = ndTalk;
			}
			act.dest = npcManager.npcList[npcManager.clickIndex]->position;
			player.addNextAction(&act);
		}

	}
	else if (!dragging && MouseAlreadyDown && engine->getMousePress(MOUSE_LEFT) && objectManager.clickIndex >= 0 && !engine->getKeyPress(KEY_LALT) && !engine->getKeyPress(KEY_RALT))
	{
		NextAction act;

		if (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT))
		{
			act.action = acRun;
		}
		else
		{
			act.action = acWalk;
		}
		act.destGE = objectManager.objectList[objectManager.clickIndex];
		act.type = ndObj;
		act.dest = objectManager.objectList[objectManager.clickIndex]->position;
		player.addNextAction(&act);
	}

	bool KeyStep = true;
	Point dest = player.position;
	int line = std::abs(dest.y % 2);
	NextAction act;
	if (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT))
	{
		act.action = acARun;
	}
	else
	{
		act.action = acAWalk;
	}

	if (engine->getKeyPress(KEY_UP) && engine->getKeyPress(KEY_LEFT))
	{
		dest.x += -1 + line;
		dest.y -= 1;
	}
	else if (engine->getKeyPress(KEY_UP) && engine->getKeyPress(KEY_RIGHT))
	{
		dest.x += line;
		dest.y -= 1;
	}
	else if (engine->getKeyPress(KEY_DOWN) && engine->getKeyPress(KEY_LEFT))
	{
		dest.x += -1 + line;
		dest.y += 1;
	}
	else if (engine->getKeyPress(KEY_DOWN) && engine->getKeyPress(KEY_RIGHT))
	{
		dest.x += line;
		dest.y += 1;
	}
	else if (engine->getKeyPress(KEY_UP))
	{
		dest.y -= 2;
	}
	else if (engine->getKeyPress(KEY_DOWN))
	{
		dest.y += 2;
	}
	else if (engine->getKeyPress(KEY_LEFT))
	{
		dest.x -= 1;
	}
	else if (engine->getKeyPress(KEY_RIGHT))
	{
		dest.x += 1;
	}
	else
	{
		KeyStep = false;
	}

	if (KeyStep && map.canWalk(dest))
	{
		act.dest = dest;
		player.addNextAction(&act);
	}
}

bool GameManager::onHandleEvent(AEvent * e)
{
	if (e->eventType == QUIT)
	{
		if (menuDisplayed())
		{
			clearMenu();
		}
		else
		{
			controller.setPaused(true);
			if (dragging)
			{
				dragging = false;
			}
			bool vis = menu.visible;
			menu.visible = false;
			toolTip->changeParent(NULL);
			System * system = new System;
			addChild(system);
			unsigned int ret = system->run();
			removeChild(system);
			delete system;
			system = NULL;
			if ((ret & erExit) != 0)
			{
				result = erExit;
				running = false;
			}
			controller.setPaused(false);
			menu.visible = vis;
		}
		return true;
	}
	else if (e->eventType == KEYDOWN)
	{
		if (e->eventData == KEY_V)
		{
			if (player.isSitting())
			{
				player.standUp();
			}
			else
			{
				player.beginSit();
			}
			return true;
		}
		else if (e->eventData == KEY_A)
		{
			NextAction act;
			act.action = acMagic;
			if (npcManager.clickIndex >= 0)
			{
				act.dest = npcManager.npcList[npcManager.clickIndex]->position;
				act.destGE = npcManager.npcList[npcManager.clickIndex];
			}
			else
			{
				act.dest = getMousePoint();
			}
			act.type = 0;
			player.addNextAction(&act);
			return true;
		}
		else if (e->eventData == KEY_S)
		{
			NextAction act;
			act.action = acMagic;
			if (npcManager.clickIndex >= 0)
			{
				act.dest = npcManager.npcList[npcManager.clickIndex]->position;
				act.destGE = npcManager.npcList[npcManager.clickIndex];
			}
			else
			{
				act.dest = getMousePoint();
			}
			act.type = 1;
			player.addNextAction(&act);
			return true;
		}
		else if (e->eventData == KEY_D)
		{
			NextAction act;
			act.action = acMagic;
			if (npcManager.clickIndex >= 0)
			{
				act.dest = npcManager.npcList[npcManager.clickIndex]->position;
				act.destGE = npcManager.npcList[npcManager.clickIndex];
			}
			else
			{
				act.dest = getMousePoint();
			}
			act.type = 2;
			player.addNextAction(&act);
			return true;
		}
		else if (e->eventData == KEY_F)
		{
			NextAction act;
			act.action = acMagic;
			if (npcManager.clickIndex >= 0)
			{
				act.dest = npcManager.npcList[npcManager.clickIndex]->position;
				act.destGE = npcManager.npcList[npcManager.clickIndex];
			}
			else
			{
				act.dest = getMousePoint();
			}
			act.type = 3;
			player.addNextAction(&act);
			return true;
		}
		else if (e->eventData == KEY_G)
		{
			NextAction act;
			act.action = acMagic;
			if (npcManager.clickIndex >= 0)
			{
				act.dest = npcManager.npcList[npcManager.clickIndex]->position;
				act.destGE = npcManager.npcList[npcManager.clickIndex];
			}
			else
			{
				act.dest = getMousePoint();
			}
			act.type = 4;
			player.addNextAction(&act);
			return true;
		}
		else if (e->eventData == KEY_Z)
		{
			goodsManager.useItem(GOODS_COUNT);
			return true;
		}
		else if (e->eventData == KEY_X)
		{
			goodsManager.useItem(GOODS_COUNT + 1);
			return true;
		}
		else if (e->eventData == KEY_C)
		{
			goodsManager.useItem(GOODS_COUNT + 2);
			return true;
		}
		else if (e->eventData == KEY_ESCAPE)
		{
			if (menuDisplayed())
			{
				clearMenu();
			}
			else
			{
				controller.setPaused(true);
				if (dragging)
				{
					dragging = false;
				}
				bool vis = menu.visible;
				menu.visible = false;
				toolTip->changeParent(NULL);
				System * system = new System;
				addChild(system);
				unsigned int ret = system->run();
				removeChild(system);
				delete system;
				system = NULL;
				if ((ret & erExit) != 0)
				{
					result = erExit;
					running = false;
				}
				controller.setPaused(false);
				menu.visible = vis;
			}			
			return true;
		}
		else if (e->eventData == KEY_F1)
		{
			stateMenu->visible = !stateMenu->visible;
			stateMenu->updateLabel();
			practiceMenu->visible = false;
			equipMenu->visible = false;
			bottomMenu->stateBtn->checked = stateMenu->visible;
			bottomMenu->xiulianBtn->checked = false;
			bottomMenu->equipBtn->checked = false;
			return true;
		}
		else if (e->eventData == KEY_F2)
		{
			equipMenu->visible = !equipMenu->visible;
			practiceMenu->visible = false;
			stateMenu->visible = false;
			bottomMenu->equipBtn->checked = equipMenu->visible;
			bottomMenu->xiulianBtn->checked = false;
			bottomMenu->stateBtn->checked = false;
			return true;
		}
		else if (e->eventData == KEY_F3)
		{
			practiceMenu->visible = !practiceMenu->visible;
			equipMenu->visible = false;
			stateMenu->visible = false;
			bottomMenu->xiulianBtn->checked = practiceMenu->visible;
			bottomMenu->equipBtn->checked = false;
			bottomMenu->stateBtn->checked = false;
			return true;
		}
		else if (e->eventData == KEY_F5)
		{
			goodsMenu->visible = !goodsMenu->visible;
			magicMenu->visible = false;
			memoMenu->visible = false;
			bottomMenu->goodsBtn->checked = goodsMenu->visible;
			bottomMenu->notesBtn->checked = false;
			bottomMenu->magicBtn->checked = false;
			return true;
		}
		else if (e->eventData == KEY_F6)
		{
			magicMenu->visible = !magicMenu->visible;
			memoMenu->visible = false;
			goodsMenu->visible = false;
			bottomMenu->magicBtn->checked = magicMenu->visible;
			bottomMenu->goodsBtn->checked = false;
			bottomMenu->notesBtn->checked = false;
			return true;
		}
		else if (e->eventData == KEY_F7)
		{
			memoMenu->visible = !memoMenu->visible;
			magicMenu->visible = false;
			goodsMenu->visible = false;
			bottomMenu->notesBtn->checked = memoMenu->visible;
			bottomMenu->goodsBtn->checked = false;
			bottomMenu->magicBtn->checked = false;
			return true;
		}
		else if (e->eventData == KEY_F12 && (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT)) && !engine->getKeyPress(KEY_LALT) && !engine->getKeyPress(KEY_RALT) && !engine->getKeyPress(KEY_LCTRL) && !engine->getKeyPress(KEY_RCTRL))
		{
			cheatMode = !cheatMode;
		}
		else if (e->eventData == KEY_Q && (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT)) && !engine->getKeyPress(KEY_LALT) && !engine->getKeyPress(KEY_RALT) && !engine->getKeyPress(KEY_LCTRL) && !engine->getKeyPress(KEY_RCTRL))
		{
			if (cheatMode)
			{
				fullMana();
				fullLife();
				fullThew();
			}		
		}
		else if (e->eventData == KEY_W && (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT)) && !engine->getKeyPress(KEY_LALT) && !engine->getKeyPress(KEY_RALT) && !engine->getKeyPress(KEY_LCTRL) && !engine->getKeyPress(KEY_RCTRL))
		{
			if (cheatMode)
			{
				magicManager.addPracticeExp(100000);
			}
		}
		else if (e->eventData == KEY_E && (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT)) && !engine->getKeyPress(KEY_LALT) && !engine->getKeyPress(KEY_RALT) && !engine->getKeyPress(KEY_LCTRL) && !engine->getKeyPress(KEY_RCTRL))
		{
			if (cheatMode)
			{
				player.addExp(10000);
			}
		}
		else if (e->eventData == KEY_R && (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT)) && !engine->getKeyPress(KEY_LALT) && !engine->getKeyPress(KEY_RALT) && !engine->getKeyPress(KEY_LCTRL) && !engine->getKeyPress(KEY_RCTRL))
		{
			if (cheatMode)
			{
				player.addMoney(100000);
			}
		}
	}
	else if (!dragging && e->eventType == MOUSEDOWN && mouseMoveIn)
	{
		if (e->eventData == MOUSE_LEFT)
		{
			MouseAlreadyDown = true;
			Point pos = getMousePoint();
			//player.position = pos;

			NextAction act;
			
			if (engine->getKeyPress(KEY_LALT) || engine->getKeyPress(KEY_RALT))
			{
				act.action = acJump;
			}
			else if (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT))
			{
				act.action = acRun;
			}
			else
			{
				act.action = acWalk;
			}
			
			act.dest = pos;
			player.addNextAction(&act);
			return true;
		}
	}
	else if (!dragging && e->eventType == MOUSEDOWN && npcManager.clickIndex >= 0)
	{
		NextAction act;
		if (engine->getKeyPress(KEY_LALT) || engine->getKeyPress(KEY_RALT))
		{
			act.action = acJump;
		}
		else if (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT))
		{
			act.action = acRun;
		}
		else
		{
			act.action = acWalk;
		}
		if (act.action != acJump)
		{
			act.destGE= npcManager.npcList[npcManager.clickIndex];
			if (npcManager.npcList[npcManager.clickIndex]->kind == nkBattle && npcManager.npcList[npcManager.clickIndex]->relation == nrHostile)
			{
				act.type = ndAttack;
			}
			else
			{
				act.type = ndTalk;
			}
		}
		act.dest = npcManager.npcList[npcManager.clickIndex]->position;
		player.addNextAction(&act);
	}
	else if (!dragging && e->eventType == MOUSEDOWN && objectManager.clickIndex >= 0)
	{
		NextAction act;
		if (engine->getKeyPress(KEY_LALT) || engine->getKeyPress(KEY_RALT))
		{
			act.action = acJump;
		}
		else if (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT))
		{
			act.action = acRun;
		}
		else
		{
			act.action = acWalk;
		}
		if (act.action != acJump)
		{
			act.destGE = objectManager.objectList[objectManager.clickIndex];
			act.type = ndObj;
		}
		act.dest = objectManager.objectList[objectManager.clickIndex]->position;
		player.addNextAction(&act);
	}
	return false;
}

void GameManager::clearBody()
{
	objectManager.clearBody();
}

void GameManager::openBox()
{
	if (scriptObj != NULL)
	{
		scriptObj->openBox();
	}
}

void GameManager::closeBox()
{
	if (scriptObj != NULL)
	{
		scriptObj->closeBox();
	}
}
