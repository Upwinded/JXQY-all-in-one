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

	npcManager->setPlayer(player);

	addChild(controller);
	addChild(menu);
	addChild(weather);

	controller->addChild(camera);

	controller->addChild(player);

	controller->addChild(map);
	controller->addChild(npcManager);
	controller->addChild(objectManager);
	controller->addChild(effectManager);
}

GameManager::~GameManager()
{
	freeResource();
	removeAllChild();
    this_ = nullptr;
}

void GameManager::initMenuWithThread()
{
	inThread = true;
	loadThreadOver = false;
	loadingDisplaying = true;
	std::string str = u8"创建UI中";
	std::vector<_shared_image> loadingImage;
	loadingImage.push_back(engine->createText(str, 50, 0xFFFFFFFF));
	loadingImage.push_back(engine->createText(str + ".", 50, 0xFFFFFFFF));
	loadingImage.push_back(engine->createText(str + "..", 50, 0xFFFFFFFF));
	loadingImage.push_back(engine->createText(str + "...", 50, 0xFFFFFFFF));

	std::thread t(&GameManager::initMenuThread, this);
	//std::thread t(&GameManager::loadingDisplayThread, this, loadingImage);
	//t.detach();

	loadingDisplayThread(loadingImage);
	t.join();
	inThread = false;

	controller->init();
}

void GameManager::initMenuThread()
{
	this_->initMenu();
    std::lock_guard<std::mutex> locker(this_->loadMutex);
	this_->loadThreadOver = true;
}

void GameManager::initMenu()
{
	menu->init();
	if (!inThread)
	{
		controller->init();
	}
}

GameManager * GameManager::getInstance()
{
	return this_;
}

void GameManager::loadGameWithThread(int index)
{
	inThread = true;
	loadThreadOver = false;
	loadingDisplaying = true;

	std::string str = u8"读取游戏中";
	std::vector<_shared_image> loadingImage;
	loadingImage.push_back(engine->createText(str, 50, 0xFFFFFFFF));
	loadingImage.push_back(engine->createText(str + ".", 50, 0xFFFFFFFF));
	loadingImage.push_back(engine->createText(str + "..", 50, 0xFFFFFFFF));
	loadingImage.push_back(engine->createText(str + "...", 50, 0xFFFFFFFF));

	std::thread t(&GameManager::loadGameThread, this, index);
	//t.detach();

	loadingDisplayThread(loadingImage);
	t.join();

	inThread = false;

	weather->setLum(global.data.mainLum);
	weather->setTime(global.data.mapTime);
	menu->update();

}

void GameManager::setMapPos(int x, int y)
{
	camera->followPlayer = false;
	camera->position = { x + 5, y + 15};
	camera->offset = { 0, 0 };
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
	weather->setTime(t);
}

void GameManager::loadGameThread(int index)
{
	this_->loadGame(index);
	std::lock_guard<std::mutex> locker(this_->loadMutex);
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

	effectManager->freeResource();

	varList.load();
	memo.load();
	//traps.load();

    player->load(global.data.characterIndex);

    magicManager.load(global.data.characterIndex);

    goodsManager.load(global.data.characterIndex);

	npcManager->clearAllNPC();

	partnerManager.load(global.data.characterIndex);

	npcManager->load(global.data.npcName);

	objectManager->load(global.data.objName);

	effectManager->load();

	if (map->data != nullptr)
	{
		map->createDataMap();
	}

	weather->reset();

	clearMenu();

	weather->setFadeLum(global.data.fadeLum);
	
	timer.setPaused(false);
	playMusic(global.data.bgmName);

	if (global.data.snowShow)
	{
		weather->setWeather(wtSnow);
	}
	else if (global.data.rainShow)
	{
		weather->setWeather(wtLightning);
	}

	if (!inThread)
	{
		weather->setLum(global.data.mainLum);
		weather->setTime(global.data.mapTime);
		menu->update();
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
     
	npcManager->save(global.data.npcName);
	objectManager->save(global.data.objName);
	effectManager->save();
	if (index > 0)
	{
		SaveFileManager::CopySaveFileTo(index);
	}
}

void GameManager::clearMenu()
{
	menu->clearMenu();
}

bool GameManager::menuDisplayed()
{
	return menu->menuDisplayed();
}

#define freeMenu(component); \
	menu->removeChild(component); \
	if (component.get() != nullptr)\
	{\
		component = nullptr; \
	}

#define freeBtmMenu(component); \
	btmWnd.removeChild(component); \
	if (component.get() != nullptr)\
	{\
		component = nullptr; \
	}
#define safeFreeResource(a) \
	if (a.get() != nullptr) \
	{\
		a->freeResource();\
	}
void GameManager::freeResource()
{
	camera->followNPC = nullptr;
	safeFreeResource(controller);
	safeFreeResource(menu);
	safeFreeResource(effectManager);
	partnerManager.freeResource();
	safeFreeResource(npcManager);
	safeFreeResource(objectManager);
	safeFreeResource(player);
	magicManager.freeResource();
	safeFreeResource(weather);
	safeFreeResource(map);
}

Point GameManager::getMousePoint(int x, int y)
{
	int w, h;
	engine->getWindowSize(w, h);
	Point cenScreen;
	cenScreen.x = (int)w / 2;
	cenScreen.y = (int)h / 2;

	Point pos = map->getMousePosition({ x, y }, camera->position, cenScreen, camera->offset);
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
	std::lock_guard<std::mutex> locker(this_->loadMutex);
	this_->loadThreadOver = true;
}

void GameManager::loadNPCThread(const std::string & fileName)
{
	this_->loadNPC(fileName);
	std::lock_guard<std::mutex> locker(this_->loadMutex);
	this_->loadThreadOver = true;
}

void GameManager::loadObjectThread(const std::string & fileName)
{
	this_->loadObject(fileName);
	std::lock_guard<std::mutex> locker(this_->loadMutex);
	this_->loadThreadOver = true;
}

void GameManager::runScript(const std::string & fileName, const std::string & mapName)
{
	clearSelected();
	std::unique_ptr<char[]> s;
	std::string newName = fileName;

	int len = PakFile::readFile(SCRIPT_MAP_FOLDER + mapName + "\\" + fileName, s);
	if (len <= 0 || s == nullptr)
	{
		GameLog::write("script: %s not found\n", (SCRIPT_MAP_FOLDER + mapName + "\\" + fileName).c_str());
		s = nullptr;
		int len = PakFile::readFile(SCRIPT_GOODS_FOLDER + fileName, s);
		if (len <= 0 || s == nullptr)
		{
			GameLog::write("script: %s not found\n", (SCRIPT_GOODS_FOLDER + fileName).c_str());
			s = nullptr;
			int len = PakFile::readFile(SCRIPT_COMMON_FOLDER + fileName, s);
			if (len <= 0 || s == nullptr)
			{
				GameLog::write("script: %s not found\n", (SCRIPT_COMMON_FOLDER + fileName).c_str());
				return;
			}
			GameLog::write("run script: %s%s\n", SCRIPT_COMMON_FOLDER, fileName.c_str());
			script.runScript(s, len);
			return;
		}
		GameLog::write("run script: %s%s\n", SCRIPT_GOODS_FOLDER, newName.c_str());
		script.runScript(s, len);
		return;
	}
	GameLog::write("run script: %s%s\\%s\n", SCRIPT_MAP_FOLDER , mapName.c_str(), newName.c_str());
	script.runScript(s, len);
	return;
}

void GameManager::runScript(const std::string & fileName)
{
	runScript(fileName, mapName);
}

void GameManager::moveScreen(int direction, int distance)
{
	camera->flyTo(direction, distance);
}

void GameManager::loadMap(const std::string & fileName)
{
	global.data.mapName = fileName;
	mapName = convert::extractFileName(fileName);

	map->load(MAP_FOLDER + fileName);

	camera->followPlayer = true;

	camera->followNPC = nullptr;

	effectManager->clearEffect();

	npcManager->clearNPC();

	objectManager->clearObj();

	global.data.npcName = "";
	global.data.objName = "";
	map->createDataMap();
	traps.load();
	enableFight();
	player->beginStand();
}

void GameManager::loadMapWithThread(const std::string & fileName)
{
	loadThreadOver = false;

	std::string str = u8"读取地图中";
	std::vector<_shared_image> loadingImage;
	loadingImage.push_back(engine->createText(str, 50, 0xFFFFFFFF));
	loadingImage.push_back(engine->createText(str + ".", 50, 0xFFFFFFFF));
	loadingImage.push_back(engine->createText(str + "..", 50, 0xFFFFFFFF));
	loadingImage.push_back(engine->createText(str + "...", 50, 0xFFFFFFFF));

	std::thread t(&GameManager::loadMapThread, this, fileName);

	//t.detach();

	loadingDisplayThread(loadingImage);
	t.join();
}

void GameManager::playMusic(const std::string & fileName)
{
	global.data.bgmName = fileName;
	if (!global.useWav)
	{
		auto ext = convert::extractFileExt(fileName);
		if (ext.empty() || strcmp(ext.c_str(), ".wav") == 0)
		{
			global.data.bgmName = convert::extractFileName(fileName) + ".mp3";
		}
	}
	GameLog::write("play bgm %s\n", global.data.bgmName.c_str());
	if (strcmp(global.data.bgmName.c_str(), bgmName.c_str()) == 0)
	{
		return;
	}
	engine->stopBGM();
	bgmName = global.data.bgmName;
	if (bgmName == "")
	{
		return;
	}
	std::unique_ptr<char[]> s;
	int len = PakFile::readFile(MUSIC_FOLDER + bgmName, s);
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
	npcManager->clearSelected();
	objectManager->clearSelected();
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

void GameManager::runObjScript(std::shared_ptr<Object> obj)
{

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
		effectManager->disableAllEffect();
		scriptType = stObject;
		runScript(obj->scriptFile);
		scriptType = stNone;
		inEvent = false;
	}
	scriptObj = nullptr;
	camera->followPlayer = true;
	runEventList();
}

void GameManager::runNPCScript(std::shared_ptr<NPC> npc)
{

	player->nextAction = nullptr;
	player->nextDest = ndNone;
	player->destGE = nullptr;
	if (inEvent)
	{
		return;
	}
	if (npc != nullptr && npcManager->findNPC(npc))
	{
		scriptNPC = npc;
		inEvent = true;
		effectManager->disableAllEffect();
		scriptType = stNPC;
		runScript(npc->scriptFile);
		scriptType = stNone;
		inEvent = false;
	}
	scriptNPC = nullptr;
	camera->followPlayer = true;
	runEventList();
}

void GameManager::runNPCDeathScript(std::shared_ptr<NPC> npc, const std::string & scriptName, const std::string & scriptMapName)
{
	//if (!npcManager->findNPC(npc))
	//{
	//	return;
	//}
	if (scriptName == "")
	{
		return;
	}

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
		if (npcManager->findNPC(npc))
		{
			scriptNPC = npc;
		}
		else
		{
			scriptNPC = nullptr;
		}
		inEvent = true;
		//effectManager->disableAllEffect();
		scriptType = stNPCDeath;
		runScript(scriptName, scriptMapName);
		scriptType = stNone;
		inEvent = false;
	}
	scriptNPC = nullptr;
	camera->followPlayer = true;
	runEventList();
}

void GameManager::runEventList()
{
	if (eventList.size() > 0)
	{
		auto deathNPC = eventList[0].npc;
		std::string tempScriptName = eventList[0].scriptName;
		std::string tempScriptMapName = eventList[0].scriptMapName;
		eventList.erase(eventList.begin());
		runNPCDeathScript(deathNPC, tempScriptName, tempScriptMapName);
	}
}

void GameManager::runGoodsScript(std::shared_ptr<Goods> goods)
{
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
		effectManager->disableAllEffect();
		scriptType = stGoods;
		runScript(goods->script);
		scriptType = stNone;
		inEvent = false;
	}
	scriptGoods = nullptr;
	camera->followPlayer = true;
	runEventList();
}

void GameManager::runTrapScript(int idx)
{
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
		effectManager->disableAllEffect();
		traps.set(mapName, idx, "");
		std::string tempMapName = mapName;
		scriptType = stTraps;
		runScript(sname);
		scriptType = stNone;
		inEvent = false;
	}
	scriptMapName = "";
	scriptTrapIndex = 0;
	camera->followPlayer = true;
	runEventList();
}

void GameManager::playMovie(const std::string & fileName)
{
	if (video != nullptr)
	{
		video = nullptr;
	}
	stopMusic();
	GameLog::write("Play Movie %s\n", fileName.c_str());
	video = std::make_shared<VideoPlayer>(VIDEO_FOLDER + fileName);
	video->drawFullScreen = true;
	video->run();

	video = nullptr;
	playMusic(global.data.bgmName);
}

void GameManager::stopMovie()
{
	engine->stopVideo(video->v);
}

void GameManager::loadNPC(const std::string & fileName)
{
	//npcManager->save(global.data.npcName);
	global.data.npcName = fileName;
	npcManager->load(global.data.npcName);
	map->createDataMap();
}

void GameManager::loadNPCWithThread(const std::string & fileName)
{
	loadThreadOver = false;

	std::string str = u8"读取资源中";
	std::vector<_shared_image> loadingImage;
	loadingImage.push_back(engine->createText(str, 50, 0xFFFFFFFF));
	loadingImage.push_back(engine->createText(str + ".", 50, 0xFFFFFFFF));
	loadingImage.push_back(engine->createText(str + "..", 50, 0xFFFFFFFF));
	loadingImage.push_back(engine->createText(str + "...", 50, 0xFFFFFFFF));

	std::thread t(&GameManager::loadNPCThread, this, fileName);

	//t.detach();
	loadingDisplayThread(loadingImage);
	t.join();
}

void GameManager::saveNPC(const std::string & fileName)
{
	if (fileName == "")
	{
		npcManager->save(global.data.npcName);
	}
	else
	{
		global.data.npcName = fileName;
		npcManager->save(fileName);
	}
}

void GameManager::addNPC(const std::string & iniName, int x, int y, int dir)
{
	npcManager->addNPC(iniName, x, y, dir);
}

void GameManager::deleteNPC(const std::string & name)
{
	npcManager->deleteNPC(name);
}

void GameManager::setNPCRes(const std::string & name, const std::string & resName)
{
	if (name == "")
	{
		std::shared_ptr<NPC> npc = scriptNPC;
		if (npc != nullptr)
		{
			npc->npcIni = resName;
			npc->initRes(resName);
		}
	}
	else
	{
		auto npc = npcManager->findNPC(name);
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
		std::shared_ptr<NPC> npc = scriptNPC;
		if (npc != nullptr)
		{
			npc->scriptFile = scriptName;
		}
	}
	else
	{
		auto npc = npcManager->findNPC(name);
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
		std::shared_ptr<NPC> npc = scriptNPC;
		if (npc != nullptr)
		{
			npc->deathScript = scriptName;
		}
	}
	else
	{
		auto npc = npcManager->findNPC(name);
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
		std::shared_ptr<NPC> npc = scriptNPC;
		if (npc != nullptr)
		{
			npc->goTo({ x, y });
		}
	}
	else
	{
		auto npc = npcManager->findNPC(name);
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
		std::shared_ptr<NPC> npc = scriptNPC;
		if (npc != nullptr)
		{
			npc->goToEx({ x, y });
		}
	}
	else
	{
		auto npc = npcManager->findNPC(name);
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
		std::shared_ptr<NPC> npc = scriptNPC;
		if (npc != nullptr)
		{
			npc->goToDir(dir, distance);
		}
	}
	else
	{
		auto npc = npcManager->findNPC(name);
		for (size_t i = 0; i < npc.size(); i++)
		{
			npc[i]->goToDir(dir, distance);
		}
	}
}

void GameManager::followNPC(const std::string & follower, const std::string & leader)
{
	auto npc = npcManager->findNPC(follower);
	for (size_t i = 0; i < npc.size(); i++)
	{
		npc[i]->followNPC = leader;
	}
}

void GameManager::followPlayer(const std::string & follower)
{
	auto npc = npcManager->findNPC(follower);
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
	//npcManager->standAll();
}

void GameManager::attackTo(const std::string & name, int x, int y)
{
	if (name == "")
	{
		std::shared_ptr<NPC> npc = scriptNPC;
		if (npc != nullptr)
		{
			npc->beginAttack({ x, y });
			//npc->eventRun();
		}
	}
	else
	{
		auto npc = npcManager->findNPC(name);
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
		std::shared_ptr<NPC> npc = scriptNPC;
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
		auto npc = npcManager->findNPC(name);
		for (size_t i = 0; i < npc.size(); i++)
		{
			npc[i]->beginStand();
			npc[i]->haveDest = false;
			npc[i]->position = { x, y };
			npc[i]->offset = { 0, 0 };
		}
	}
	map->createDataMap();
}

void GameManager::setNPCDir(const std::string & name, int dir)
{
	if (name == "")
	{
		std::shared_ptr<NPC> npc = scriptNPC;
		if (npc != nullptr && npc->isStanding())
		{
			npc->beginStand();
			npc->haveDest = false;
			npc->direction = dir;
		}
	}
	else
	{
		auto npc = npcManager->findNPC(name);
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
		std::shared_ptr<NPC> npc = scriptNPC;
		if (npc != nullptr)
		{
			npc->kind = kind;
		}
	}
	else
	{
		auto npc = npcManager->findNPC(name);
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
		std::shared_ptr<NPC> npc = scriptNPC;
		if (npc != nullptr)
		{
			npc->level = level;
			npc->setLevel(level);
		}
	}
	else
	{
		auto npc = npcManager->findNPC(name);
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
		std::shared_ptr<NPC> npc = scriptNPC;
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
		auto npc = npcManager->findNPC(name);
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
		std::shared_ptr<NPC> npc = scriptNPC;
		if (npc != nullptr)
		{
			npc->relation = relation;
			npc->beginStand();
		}
	}
	else
	{
		auto npc = npcManager->findNPC(name);
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
		std::shared_ptr<NPC> npc = scriptNPC;
		if (npc != nullptr)
		{
			npc->action = actionType;
		}
	}
	else
	{
		auto npc = npcManager->findNPC(name);
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
		std::shared_ptr<NPC> npc = scriptNPC;
		if (npc != nullptr)
		{
			npc->loadActionFile(fileName, action);
		}
	}
	else
	{
		auto npc = npcManager->findNPC(name);
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
		std::shared_ptr<NPC> npc = scriptNPC;
		if (npc != nullptr)
		{
			npc->doSpecialAction(fileName);
		}
	}
	else
	{
		auto npc = npcManager->findNPC(name);
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
	gm->npcManager->setPartnerPos(x, y, player->direction);
	map->createDataMap();
}

void GameManager::setPlayerDir(int dir)
{
	player->direction = dir;
}

void GameManager::setPlayerScn()
{
	camera->followPlayer = true;
	auto npc = npcManager->findPlayerNPC();
	if (npc != nullptr)
	{
		camera->followNPC = npc;
	}
	else
	{
		camera->followNPC = player;
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
		showMessage(convert::formatString(u8"获得%d两银子！", value));
	}
	else
	{
		showMessage(convert::formatString(u8"失去%d两银子！", -value));
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
	menu->equipMenu->updateGoods();
	menu->stateMenu->updateLabel();
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
	menu->goodsMenu->updateMoney();
}

void GameManager::loadObject(const std::string & fileName)
{
	//objectManager->save(global.data.objName);
	global.data.objName = fileName;
	objectManager->load(global.data.objName);
	map->createDataMap();
}

void GameManager::loadObjectWithThread(const std::string & fileName)
{
	loadThreadOver = false;

	std::string str = u8"读取资源中";
	std::vector<_shared_image> loadingImage;
	loadingImage.push_back(engine->createText(str, 50, 0xFFFFFFFF));
	loadingImage.push_back(engine->createText(str + ".", 50, 0xFFFFFFFF));
	loadingImage.push_back(engine->createText(str + "..", 50, 0xFFFFFFFF));
	loadingImage.push_back(engine->createText(str + "...", 50, 0xFFFFFFFF));

	std::thread t(&GameManager::loadObjectThread, this, fileName);

	//t.detach();

	loadingDisplayThread(loadingImage);
	t.join();
}

void GameManager::saveObject(const std::string & fileName)
{
	if (fileName == "")
	{
		objectManager->save(global.data.objName);
	}
	else
	{
		global.data.objName = fileName;
		objectManager->save(fileName);
	}
	
}

void GameManager::addObject(const std::string & iniName, int x, int y, int dir)
{
	objectManager->addObject(iniName, x, y, dir);
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
	objectManager->deleteObject(objName);
}

void GameManager::setObjectPosition(const std::string & name, int x, int y)
{
	std::shared_ptr<Object> obj = nullptr;
	if (name == "")
	{
		obj = scriptObj;
	}
	else
	{
		obj = objectManager->findObj(name);
	}
	if (obj != nullptr)
	{
		obj->position = { x, y };
		map->createDataMap();
	}
}

void GameManager::setObjectKind(const std::string & name, int kind)
{
	std::shared_ptr<Object> obj = nullptr;
	if (name == "")
	{
		obj = scriptObj;
	}
	else
	{
		obj = objectManager->findObj(name);
	}
	if (obj != nullptr)
	{
		obj->kind = kind;
	}
}

void GameManager::setObjectScript(const std::string & name, const std::string & scriptFile)
{
	std::shared_ptr<Object> obj = nullptr;
	if (name == "")
	{
		obj = scriptObj;
	}
	else
	{
		obj = objectManager->findObj(name);
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
	INIReader ini(s);
	std::vector<std::string> talkStr;
	talkStr.resize(0);

	int talkIndex = 0;
	while (true)
	{
		std::string name = convert::formatString("%d", talkIndex + 1);
		
		std::string str = ini.Get(part, name, "");
		if (str == "")
		{
			break;
		}
		talkStr.push_back(str);
		talkIndex++;
	}
	
	if (talkIndex <= 0)
	{
		return;
	}
	//std::shared_ptr<Dialog> dialog = std::make_shared<Dialog>();
	//addChild(dialog);
	auto dialog = menu->dialog;
	std::string head1 = "";
	std::string head2 = "";

	std::string headStr = "head";
	for (int i = 0; i < talkIndex; i++)
	{
		std::string tstr = convert::formatString("%s%d", headStr.c_str(), i + 1);
		std::string thstr = ini.Get(part, tstr, "NoHeadChange");
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
		dialog->visible = true;
		dialog->run();
		dialog->visible = false;
	}
	//removeChild(dialog);
}

void GameManager::say(const std::string & str, int index)
{
	//auto dialog = std::make_shared<Dialog>();
	//addChild(dialog);
	auto dialog = menu->dialog;
	if (index >= 0)
	{
		dialog->setHead1(dialog->getHeadName(index));
	}
	else
	{
		dialog->setHead1("");
	}
	dialog->setTalkStr(str);
	dialog->visible = true;
	dialog->run();
	dialog->visible = false;
	//removeChild(dialog);
}

void GameManager::fadeIn()
{
	weather->fadeIn();
}

void GameManager::fadeOut()
{
	weather->fadeOut();
}

void GameManager::setFadeLum(int lum)
{
	global.data.fadeLum = lum;
	weather->setFadeLum(lum);
}

void GameManager::setMainLum(int lum)
{
	global.data.mainLum = lum;
	weather->setLum(lum);
}

void GameManager::sleep(unsigned int time)
{
	weather->sleep(time);
}

void GameManager::showMessage(const std::string & str)
{
	menu->showMessage(str);
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
	std::shared_ptr<BuySellMenu> bsMenu = std::make_shared<BuySellMenu>();
	addChild(bsMenu);
	bsMenu->buy(fileName);
	removeChild(bsMenu);
}

void GameManager::sellGoods(const std::string & fileName)
{
	std::shared_ptr<BuySellMenu> bsMenu = std::make_shared<BuySellMenu>();
	addChild(bsMenu);
	bsMenu->sell(fileName);
	removeChild(bsMenu);

}

void GameManager::hideInterface()
{
	clearMenu();
}

void GameManager::hideBottomWnd()
{
	menu->hideBottomWnd();
}

void GameManager::showBottomWnd()
{
	menu->showBottomWnd();
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
		weather->setWeather(wtSnow);
	}
	else
	{
		global.data.snowShow = false;
		global.data.rainShow = false;
		weather->setWeather(wtNone);
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
		weather->setWeather(wtLightning);
	}
	else
	{
		global.data.rainShow = false;
		global.data.snowShow = false;
		weather->setWeather(wtNone);
	}
}

void GameManager::loadingDisplayThread(std::vector<_shared_image> loadingImage)
{
	int w, h;
	engine->getWindowSize(w, h);

	auto beginTime = engine->getTime();

	size_t imgIndex = 0;

	loadMutex.lock();
	while (!loadThreadOver)
	{
		loadMutex.unlock();

		engine->frameBegin();

		if (engine->getTime() - beginTime > 100)
		{
			beginTime = engine->getTime();
			imgIndex++;
			if (imgIndex >= loadingImage.size())
			{
				imgIndex = 0;
			}
		}
		engine->delay(30);

		engine->drawImage(loadingImage[imgIndex], w - 320, h - 70);
		engine->frameEnd();

		loadMutex.lock();
	}
	loadingDisplaying = false;
	loadMutex.unlock();
}

void GameManager::onUpdate()
{
	runEventList();
}

void GameManager::onDraw()
{
	map->drawMap();
}

bool GameManager::onInitial()
{
#ifdef LOAD_WITH_THREAD
	initMenuWithThread();
#else // !LOAD_WITH_THREAD
	initMenu();
#endif // LOAD_WITH_THREAD

	if (gameIndex == 0)
	{
		inEvent = true;
		runScript("newgame.txt");
		inEvent = false;
	}
	else
	{
#ifdef LOAD_WITH_THREAD
		loadGameWithThread(gameIndex);
#else // !LOAD_WITH_THREAD
		loadGame(gameIndex);
#endif // LOAD_WITH_THREAD
		weather->fadeInEx();
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

bool GameManager::onHandleEvent(AEvent & e)
{
	if (!global.data.canInput)
	{
		return false;
	}
	if (e.eventType == ET_KEYDOWN)
	{
		if (e.eventData == KEY_F12 && (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT)) && !engine->getKeyPress(KEY_LALT) && !engine->getKeyPress(KEY_RALT) && !engine->getKeyPress(KEY_LCTRL) && !engine->getKeyPress(KEY_RCTRL))
		{
			cheatMode = !cheatMode;
		}
		else if (e.eventData == KEY_Q && (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT)) && !engine->getKeyPress(KEY_LALT) && !engine->getKeyPress(KEY_RALT) && !engine->getKeyPress(KEY_LCTRL) && !engine->getKeyPress(KEY_RCTRL))
		{
			if (cheatMode)
			{
				fullMana();
				fullLife();
				fullThew();
			}
		}
		else if (e.eventData == KEY_W && (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT)) && !engine->getKeyPress(KEY_LALT) && !engine->getKeyPress(KEY_RALT) && !engine->getKeyPress(KEY_LCTRL) && !engine->getKeyPress(KEY_RCTRL))
		{
			if (cheatMode)
			{
				if (magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic != nullptr && magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].level < 10)
				{
					magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].level += 1;
					menu->practiceMenu->updateExp();
					menu->practiceMenu->updateLevel();
				}
			}
		}
		else if (e.eventData == KEY_E && (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT)) && !engine->getKeyPress(KEY_LALT) && !engine->getKeyPress(KEY_RALT) && !engine->getKeyPress(KEY_LCTRL) && !engine->getKeyPress(KEY_RCTRL))
		{
			if (cheatMode)
			{
				player->addExp(player->levelUpExp - player->exp);
			}
		}
		else if (e.eventData == KEY_R && (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT)) && !engine->getKeyPress(KEY_LALT) && !engine->getKeyPress(KEY_RALT) && !engine->getKeyPress(KEY_LCTRL) && !engine->getKeyPress(KEY_RCTRL))
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
	objectManager->clearBody();
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
