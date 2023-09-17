#include "GameManager.h"
#include <thread>

GameManager * GameManager::this_ = nullptr;



GameManager::GameManager()
{
	this_ = this;
	name = "GameManager";
    canCallBack = true;

	drawFullScreen = true;
	rectFullScreen = true;
	priority = epMap;
	result = erNone;
    player = new Player();
	npcManager.setPlayer(player);

	addChild(&controller);
	addChild(&menu);
	addChild(&weather);

	controller.addChild(&camera);

	controller.addChild(player);
	controller.addChild(&map);
	controller.addChild(&npcManager);
	controller.addChild(&objectManager);
	controller.addChild(&effectManager);
}

GameManager::~GameManager()
{
	freeResource();
	removeAllChild();
    this_ = nullptr;
}

void GameManager::initMenuWithThread()
{
	loadThreadOver = false;
	std::thread t(&GameManager::initMenuThread, this);

	t.detach();
	auto beginTime = engine->getTime();

	loadMutex.lock();
	while (!loadThreadOver)
	{
		loadMutex.unlock();
		engine->frameBegin();
		int w, h;
		engine->getWindowSize(w, h);
		std::wstring ws = L"Loading";
		auto t = (engine->getTime() - beginTime) % 1200 / 300;
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
	menu.init();

	controller.init();
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
	auto beginTime = engine->getTime();
	
	loadMutex.lock();
	while (!loadThreadOver)
	{
		loadMutex.unlock();
		engine->frameBegin();
		int w, h;
		engine->getWindowSize(w, h);
		std::wstring ws = L"Loading";
		auto t = (engine->getTime() - beginTime) % 1200 / 300;
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
	menu.update();
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
	MutexLocker mutexLocker(&loadMutex);
	this_->loadThreadOver = true;
}

bool GameManager::loadGame(int index)
{
	if (index < 0)
	{
		return false;
	}

	stopMusic();

	initAllTime();
	timer.setPaused(true);

	SaveFileManager::CopySaveFileFrom(index);
	global.load();
	std::string tempNpcName = global.data.npcName;
	std::string tempObjName = global.data.objName;
	loadMap(global.data.mapName);
	global.data.npcName = tempNpcName;
	global.data.objName = tempObjName;

	effectManager.freeResource();

	

	varList.load();
	memo.load();
	//traps.load();
    
    player->load(global.data.characterIndex);
    magicManager.load(global.data.characterIndex);
    goodsManager.load(global.data.characterIndex);

	npcManager.clearAllNPC();
	partnerManager.load(global.data.characterIndex);
	npcManager.load(global.data.npcName);
	objectManager.load(global.data.objName);

	effectManager.load();
	if (map.data != nullptr)
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
	timer.setPaused(false);
	playMusic(global.data.bgmName);
	if (!inThread)
	{
		menu.update();
	}
	setPlayerScn();

	return true;
}

void GameManager::saveGame(int index)
{
	global.save();
	varList.save();
	memo.save();
	traps.save();
    
    player->save(global.data.characterIndex);

    
	partnerManager.save(global.data.characterIndex);
    
	magicManager.save(global.data.characterIndex);
	goodsManager.save(global.data.characterIndex);
    
    
	npcManager.save(global.data.npcName);
	objectManager.save(global.data.objName);
	effectManager.save();
	if (index > 0)
	{
		SaveFileManager::CopySaveFileTo(index);
	}
}

void GameManager::clearMenu()
{
	menu.clearMenu();
}

bool GameManager::menuDisplayed()
{
	return menu.menuDisplayed();
}

#define freeMenu(component); \
	menu.removeChild(component); \
	if (component != nullptr)\
	{\
		delete component; \
		component = nullptr; \
	}

#define freeBtmMenu(component); \
	btmWnd.removeChild(component); \
	if (component != nullptr)\
	{\
		delete component; \
		component = nullptr; \
	}

void GameManager::freeResource()
{
	controller.freeResource();
	menu.freeResource();

	effectManager.freeResource();
	partnerManager.freeResource();
	npcManager.freeResource();
	magicManager.freeResource();
	objectManager.freeResource();
	player->freeResource();
	weather.freeResource();
	map.freeResource();
}


Point GameManager::getMousePoint(int x, int y)
{
	int w, h;
	engine->getWindowSize(w, h);
	Point cenScreen;
	cenScreen.x = (int)w / 2;
	cenScreen.y = (int)h / 2;
	int xscal, yscal;
	xscal = cenScreen.x / TILE_WIDTH + 2;
	yscal = cenScreen.y / TILE_HEIGHT * 2 + 2;
	int tileHeightScal = 10;

	Point cenTile = camera.position;
	Point pos = map.getMousePosition({ x, y }, camera.position, cenScreen, camera.offset);
	return pos;
}

Point GameManager::getMousePoint()
{
	int x = -1;
	int y = -1;
	engine->getMousePosition(x, y);
	return getMousePoint(x, y);
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

void GameManager::runScript(const std::string & fileName, const std::string & mapName)
{
	clearSelected();
	std::unique_ptr<char[]> s;
	std::string newName = fileName;

	int len = PakFile::readFile(SCRIPT_MAP_FOLDER + mapName + "\\" + fileName, s);
	if (len <= 0 || s == nullptr)
	{
		printf("script: %s not found\n", (SCRIPT_MAP_FOLDER + mapName + "\\" + fileName).c_str());
		s = nullptr;
		int len = PakFile::readFile(SCRIPT_GOODS_FOLDER + fileName, s);
		if (len <= 0 || s == nullptr)
		{
			printf("script: %s not found\n", (SCRIPT_GOODS_FOLDER + fileName).c_str());
			s = nullptr;
			int len = PakFile::readFile(SCRIPT_COMMON_FOLDER + fileName, s);
			if (len <= 0 || s == nullptr)
			{
				printf("script: %s not found\n", (SCRIPT_COMMON_FOLDER + fileName).c_str());
				return;
			}
			printf("run script: %s%s\n", SCRIPT_COMMON_FOLDER, fileName.c_str());
			script.runScript(s, len);
			return;
		}
		printf("run script: %s%s\n", SCRIPT_GOODS_FOLDER, newName.c_str());
		script.runScript(s, len);
		return;
	}
	printf("run script: %s%s\\%s\n", SCRIPT_MAP_FOLDER , mapName.c_str(), newName.c_str());
	script.runScript(s, len);
	return;
}

void GameManager::runScript(const std::string & fileName)
{
	runScript(fileName, mapName);
}

void GameManager::moveScreen(int direction, int distance)
{
	camera.flyTo(direction, distance);
}

void GameManager::loadMap(const std::string & fileName)
{
	global.data.mapName = fileName;
	mapName = convert::GBKToUTF8_InWinOnly(convert::extractFileName(convert::UTF8ToGBK_InWinOnly(fileName)));
	map.load(MAP_FOLDER + fileName);
	camera.followPlayer = true;
	npcManager.clearNPC();
	objectManager.clearObj();
	global.data.npcName = "";
	global.data.objName = "";
	map.createDataMap();
	traps.load();
	enableFight();
	player->beginStand();
}

void GameManager::loadMapWithThread(const std::string & fileName)
{
	loadThreadOver = false;

	std::thread t(&GameManager::loadMapThread, this, fileName);

	t.detach();
	auto beginTime = engine->getTime();

	loadMutex.lock();
	while (!loadThreadOver)
	{
		loadMutex.unlock();
		engine->frameBegin();
		int w, h;
		engine->getWindowSize(w, h);
		std::wstring ws = L"Loading Map";
		auto t = (engine->getTime() - beginTime) % 1200 / 300;
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
	global.data.bgmName = fileName;
	if (strcmp(fileName.c_str(), bgmName.c_str()) == 0)
	{
		
		return;
	}
	engine->stopBGM();
	bgmName = fileName;
	if (fileName == "")
	{
		return;
	}
	std::unique_ptr<char[]> s;
	int len = PakFile::readFile(MUSIC_FOLDER + fileName, s);
	if (s != nullptr && len > 0)
	{
		engine->loadBGM(s, len);
		engine->playBGM();
	}
}

void GameManager::stopMusic()
{
	engine->stopBGM();
	global.data.bgmName = "";
	bgmName = "";
}

void GameManager::playSound(const std::string & fileName)
{
	if (fileName == "")
	{
		return;
	}
	std::string soundName = SOUND_FOLDER + fileName;
	std::unique_ptr<char[]> s;
	int len = PakFile::readFile(soundName, s);
	if (len > 0 && s != nullptr)
	{
		engine->playSound(s, len);
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
	delete player->nextAction;
	player->nextAction = nullptr;
	player->nextDest = ndNone;
	player->destGE = nullptr;
	if (inEvent)
	{
		return;
	}
	if (obj != nullptr)
	{
		scriptObj = obj;
		inEvent = true;
		effectManager.disableAllEffect();
		scriptType = stObject;
		runScript(obj->scriptFile);
		scriptType = stNone;
		inEvent = false;
	}
	scriptObj = nullptr;
	camera.followPlayer = true;
	runEventList();
}

void GameManager::runNPCScript(NPC * npc)
{
	delete player->nextAction;
	player->nextAction = nullptr;
	player->nextDest = ndNone;
	player->destGE = nullptr;
	if (inEvent)
	{
		return;
	}
	if (npc != nullptr && npcManager.findNPC(npc))
	{
		scriptNPC = npc;
		inEvent = true;
		effectManager.disableAllEffect();
		scriptType = stNPC;
		runScript(npc->scriptFile);
		scriptType = stNone;
		inEvent = false;
	}
	scriptNPC = nullptr;
	camera.followPlayer = true;
	runEventList();
}

void GameManager::runNPCDeathScript(NPC * npc, const std::string & scriptName, const std::string & scriptMapName)
{
	if (!npcManager.findNPC(npc))
	{
		return;
	}
	if (scriptName == "")
	{
		return;
	}
	delete player->nextAction;
	player->nextAction = nullptr;
	player->nextDest = ndNone;
	player->destGE = nullptr;
	if (inEvent)
	{
		if (!scriptName.empty())
		{
			EventInfo eventInfo;
			eventInfo.npc = npc;
			eventInfo.scriptName = scriptName;
			eventInfo.scriptMapName = scriptMapName;
			eventList.push_back(eventInfo);
		}
		return;
	}
	if (npc != nullptr)
	{
		if (npcManager.findNPC(npc))
		{
			scriptNPC = npc;
		}
		else
		{
			scriptNPC = nullptr;
		}
		inEvent = true;
		//effectManager.disableAllEffect();
		scriptType = stNPCDeath;
		runScript(scriptName, scriptMapName);
		scriptType = stNone;
		inEvent = false;
	}
	scriptNPC = nullptr;
	camera.followPlayer = true;
	runEventList();
}

void GameManager::runEventList()
{
	if (eventList.size() > 0)
	{
		NPC* deathNPC = eventList[0].npc;
		std::string tempScriptName = eventList[0].scriptName;
		std::string tempScriptMapName = eventList[0].scriptMapName;
		eventList.erase(eventList.begin());
		runNPCDeathScript(deathNPC, tempScriptName, tempScriptMapName);
	}
}

void GameManager::runGoodsScript(Goods * goods)
{
	delete player->nextAction;
	player->nextAction = nullptr;
	player->nextDest = ndNone;
	player->destGE = nullptr;
	if (!(player->isJumping() && player->jumpState == jsJumping))
	{
		player->beginStand();
	}
	if (inEvent)
	{
		return;
	}
	if (goods != nullptr)
	{
		scriptGoods = goods;
		inEvent = true;
		effectManager.disableAllEffect();
		scriptType = stGoods;
		runScript(goods->script);
		scriptType = stNone;
		inEvent = false;
	}
	scriptGoods = nullptr;
	camera.followPlayer = true;
	runEventList();
}

void GameManager::runTrapScript(int idx)
{
	delete player->nextAction;
	player->nextAction = nullptr;
	player->nextDest = ndNone;
	player->destGE = nullptr;
	if (!(player->isJumping() && player->jumpState == jsJumping))
	{
		player->beginStand();
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
	runEventList();
}

void GameManager::playMovie(const std::string & fileName)
{
	if (video != nullptr)
	{
		delete video;
		video = nullptr;
	}
	stopMusic();
	printf("Play Movie %s\n", fileName.c_str());
	video = new VideoPlayer(VIDEO_FOLDER + fileName);
	video->drawFullScreen = true;
	video->run();
	delete video;
	video = nullptr;
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
	auto beginTime = engine->getTime();

	loadMutex.lock();
	while (!loadThreadOver)
	{
		loadMutex.unlock();
		engine->frameBegin();
		int w, h;
		engine->getWindowSize(w, h);
		std::wstring ws = L"Loading Resources";
		auto t = (engine->getTime() - beginTime) % 1200 / 300;
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
		global.data.npcName = fileName;
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
		if (npc != nullptr)
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
		if (npc != nullptr)
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
		if (npc != nullptr)
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

void GameManager::goTo(const std::string & name, int x, int y)
{
	if (name == "")
	{
		NPC * npc = scriptNPC;
		if (npc != nullptr)
		{
			npc->goTo({ x, y });
		}
	}
	else
	{
		auto npc = npcManager.findNPC(name);
		for (size_t i = 0; i < npc.size(); i++)
		{
			npc[i]->goTo({ x, y });
		}
	}
}

void GameManager::goToEx(const std::string & name, int x, int y)
{
	if (name == "")
	{
		NPC * npc = scriptNPC;
		if (npc != nullptr)
		{
			npc->goToEx({ x, y });
		}
	}
	else
	{
		auto npc = npcManager.findNPC(name);
		for (size_t i = 0; i < npc.size(); i++)
		{
			npc[i]->goToEx({ x, y });
		}
	}
}

void GameManager::goToDir(const std::string & name, int dir, int distance)
{
	if (name == "")
	{
		NPC * npc = scriptNPC;
		if (npc != nullptr)
		{
			npc->goToDir(dir, distance);
		}
	}
	else
	{
		auto npc = npcManager.findNPC(name);
		for (size_t i = 0; i < npc.size(); i++)
		{
			npc[i]->goToDir(dir, distance);
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
		npc[i]->followNPC = gm->player->name;
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

void GameManager::attackTo(const std::string & name, int x, int y)
{
	if (name == "")
	{
		NPC * npc = scriptNPC;
		if (npc != nullptr)
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
		if (npc != nullptr)
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
		if (npc != nullptr && npc->isStanding())
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
		if (npc != nullptr)
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
		if (npc != nullptr)
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
		if (npc != nullptr)
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
		if (npc != nullptr)
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
		if (npc != nullptr)
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
		if (npc != nullptr)
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
		if (npc != nullptr)
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

void GameManager::loadPlayer(int index)
{
	player->load(index);
}

void GameManager::savePlayer(int index)
{
	player->save(index);
}

void GameManager::setPlayerPosition(int x, int y)
{
	player->beginStand();
	player->position = { x, y };
	player->offset = { 0, 0 };
	player->haveDest = false;
	gm->npcManager.setPartnerPos(x, y, player->direction);
	map.createDataMap();
}

void GameManager::setPlayerDir(int dir)
{
	player->direction = dir;
}

void GameManager::setPlayerScn()
{
	camera.followPlayer = true;
	auto npc = npcManager.findPlayerNPC();
	if (npc != nullptr)
	{
		camera.followNPC = npc;
	}
	else
	{
		camera.followNPC = player;
	}
}

void GameManager::setLevelFile(const std::string & fileName)
{
	player->levelIni = fileName;
	player->loadLevel(fileName);
}

void GameManager::setMagicLevel(const std::string & magicName, int level)
{
	MagicInfo * m = magicManager.findMagic(magicName);
	if (m != nullptr)
	{
		m->level = level;
	}
}

void GameManager::setPlayerLevel(int level)
{
	player->setLevel(level);
}

void GameManager::setPlayerState(int state)
{
	player->setFight(state);
}

void GameManager::enableRun()
{
	player->canRun = true;
}

void GameManager::disableRun()
{
	player->canRun = false;
}

void GameManager::enableJump()
{
	player->canJump = true;
}

void GameManager::disableJump()
{
	player->canJump = false;
}

void GameManager::enableFight()
{
	player->canFight = true;
}

void GameManager::disableFight()
{
	player->canFight = false;
}

void GameManager::playerGoto(int x, int y)
{
	player->goTo({ x, y });
}

void GameManager::playerGotoEx(int x, int y)
{
	player->goToEx({ x, y });
}

void GameManager::playerRunTo(int x, int y)
{
	player->runTo({ x, y });
}

void GameManager::playerJumpTo(int x, int y)
{
	player->jumpTo({ x, y });
}

void GameManager::playerGotoDir(int dir, int distance)
{
	player->goToDir(dir, distance);
}

void GameManager::addLife(int value)
{
	player->addLife(value);
}

void GameManager::addLifeMax(int value)
{
	player->addLifeMax(value);
}

void GameManager::addThew(int value)
{
	player->addThew(value);
}

void GameManager::addThewMax(int value)
{
	player->addThewMax(value);
}

void GameManager::addMana(int value)
{
	player->addMana(value);
}

void GameManager::addManaMax(int value)
{
	player->addManaMax(value);
}

void GameManager::addAttack(int value)
{
	player->addAttack(value);
}

void GameManager::addDefend(int value)
{
	player->addDefend(value);
}

void GameManager::addEvade(int value)
{
	player->addEvade(value);
}

void GameManager::addExp(int value)
{
	player->addExp(value);
}

void GameManager::addMoney(int value)
{
	if (value == 0)
	{
		return;
	}
	player->addMoney(value);
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
	std::unique_ptr<char[]> s;
	int len = PakFile::readFile(str, s);
	if (len > 0 && s != nullptr)
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
	player->fullLife();
}

void GameManager::fullThew()
{
	player->fullThew();
}

void GameManager::fullMana()
{
	player->fullMana();
}

void GameManager::updateState()
{
	menu.equipMenu->updateGoods();
	menu.stateMenu->updateLabel();
}

void GameManager::saveGoods(int index)
{
	goodsManager.save(index);
}

void GameManager::loadGoods(int index)
{
	goodsManager.load(index);
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
	varList.setInteger("MoneyNum", player->money);
}

void GameManager::setMoneyNum(int value)
{
	player->money = value;
	menu.goodsMenu->updateMoney();
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
	auto beginTime = engine->getTime();

	loadMutex.lock();
	while (!loadThreadOver)
	{
		loadMutex.unlock();
		engine->frameBegin();
		int w, h;
		engine->getWindowSize(w, h);
		std::wstring ws = L"Loading Resources";
		auto t = (engine->getTime() - beginTime) % 1200 / 300;
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
		global.data.objName = fileName;
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
		if (scriptObj != nullptr)
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
	Object * obj = nullptr;
	if (name == "")
	{
		obj = scriptObj;
	}
	else
	{
		obj = objectManager.findObj(name);
	}
	if (obj != nullptr)
	{
		obj->position = { x, y };
		map.createDataMap();
	}
}

void GameManager::setObjectKind(const std::string & name, int kind)
{
	Object * obj = nullptr;
	if (name == "")
	{
		obj = scriptObj;
	}
	else
	{
		obj = objectManager.findObj(name);
	}
	if (obj != nullptr)
	{
		obj->kind = kind;
	}
}

void GameManager::setObjectScript(const std::string & name, const std::string & scriptFile)
{
	Object * obj = nullptr;
	if (name == "")
	{
		obj = scriptObj;
	}
	else
	{
		obj = objectManager.findObj(name);
	}
	if (obj != nullptr)
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
	std::unique_ptr<char[]> s;
	int len = PakFile::readFile(fileName, s);
	if (s == nullptr || len <= 0)
	{
		return;
	}
	INIReader * ini = new INIReader(s);
	std::vector<std::string> talkStr;
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
		talkStr.push_back(str);
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
		std::string s = talkStr[i];
		dialog->run();
	}
	removeChild(dialog);
	delete dialog;
	delete ini;
}

void GameManager::say(const std::string & str, int index)
{
	Dialog dialog;
	addChild(&dialog);
	if (index >= 0)
	{
		dialog.setHead1(dialog.getHeadName(index));
	}
	else
	{
		dialog.setHead1("");
	}
	dialog.setTalkStr(str);
	dialog.run();
	removeChild(&dialog);
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
	menu.showMessage(str);
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
	menu.hideBottomWnd();
}

void GameManager::showBottomWnd()
{
	menu.showBottomWnd();
}

void GameManager::hideMouseCursor()
{
	engine->hideCursor();
}

void GameManager::showMouseCursor()
{
	engine->showCursor();
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
	runEventList();
#ifdef _MOBILE
    if (!inEvent)
    {
		fastSelectingList.resize(0);
        int radius = 2;
		auto tempObjList = objectManager.findRadiusScriptViewObj(player->position, radius);
        for (int i = 0; i < tempObjList.size(); ++i)
		{
			NextAction action;
			action.type = ndObj;
			action.destGE = tempObjList[i];
			action.dest = action.destGE->position;
            action.distance = map.calDistance(player->position, action.destGE->position);
			fastSelectingList.push_back(action);
		}
        auto tempNPCList = npcManager.findRadiusScriptViewNPC(player->position, radius);
        for (int i = 0; i < tempNPCList.size(); ++i)
        {
            NextAction action;
            action.type = ndTalk;
            action.destGE = tempNPCList[i];
            action.dest = action.destGE->position;
            action.distance = map.calDistance(player->position, action.destGE->position);

            fastSelectingList.push_back(action);
        }

        std::sort(fastSelectingList.begin(), fastSelectingList.end(), actionCmp);
        if (fastSelectingList.size() > FASTBTN_COUNT)
        {
            fastSelectingList.resize(FASTBTN_COUNT);
        }
        for (int i = 0; i < fastSelectingList.size(); ++i)
        {
            if (fastSelectingList[i].type == ndTalk)
            {
				controller.setFastSelectBtn(i, true, ((NPC*)fastSelectingList[i].destGE)->npcName);
            }
            else
            {
				controller.setFastSelectBtn(i, true, ((Object *)fastSelectingList[i].destGE)->objName);
            }
        }

		for (int i = fastSelectingList.size(); i < FASTBTN_COUNT; ++i)
		{
			controller.setFastSelectBtn(i, false);
		}
    }
    else
    {
        for (int i = 0; i < FASTBTN_COUNT; ++i)
        {
			controller.setFastSelectBtn(i, false);
        }
    }
#endif
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
		inEvent = true;
		runScript("newgame.txt");
		inEvent = false;
	}
	else
	{
//		loadGameWithThread(gameIndex);
        loadGame(gameIndex);
		weather.fadeInEx();
	}	
	return true;
}

void GameManager::onRun()
{
	player->checkTrap();
}

void GameManager::onExit()
{
	freeResource();
	engine->stopBGM();
}

void GameManager::onEvent()
{
	if (!global.data.canInput)
	{
		return;
	}
}

bool GameManager::onHandleEvent(AEvent * e)
{
	//printf("global input : %d \n", global.data.canInput);
	if (!global.data.canInput)
	{
		return false;
	}
//	if (e->eventType == ET_FINGERDOWN && e->eventData == touchInRectID)
//	{
//		Point pos = getMousePoint(e->eventX, e->eventY);
//		//player->position = pos;
//
//		NextAction act;
//
//		act.action = acJump;
//		act.dest = pos;
//		player->addNextAction(&act);
//		return true;
//	}
//	else
	if (e->eventType == ET_KEYDOWN)
	{
		if (e->eventData == KEY_F12 && (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT)) && !engine->getKeyPress(KEY_LALT) && !engine->getKeyPress(KEY_RALT) && !engine->getKeyPress(KEY_LCTRL) && !engine->getKeyPress(KEY_RCTRL))
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
				player->addExp(10000);
			}
		}
		else if (e->eventData == KEY_R && (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT)) && !engine->getKeyPress(KEY_LALT) && !engine->getKeyPress(KEY_RALT) && !engine->getKeyPress(KEY_LCTRL) && !engine->getKeyPress(KEY_RCTRL))
		{
			if (cheatMode)
			{
				player->addMoney(100000);
			}
		}
	}
	return false;
}

void GameManager::clearBody()
{
	objectManager.clearBody();
}

void GameManager::openBox()
{
	if (scriptObj != nullptr)
	{
		scriptObj->openBox();
	}
}

void GameManager::closeBox()
{
	if (scriptObj != nullptr)
	{
		scriptObj->closeBox();
	}
}
