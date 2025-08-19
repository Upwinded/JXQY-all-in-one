#pragma once
#include "../../Element/Element.h"
#include "../../Component/Component.h"
#include "../Data/Data.h"
#include "../../libconvert/libconvert.h"
#include "../Script/Script.h"
#include "../../Weather/Weather.h"
#include "GameController.h"
#include "MenuController.h"
#include "../Menu/Menu.h"
#include <mutex>
#include "SaveFileManager.h"

#define gm GameManager::getInstance()

struct EventInfo
{
	std::shared_ptr<NPC> npc = nullptr;
	std::string scriptMapName = "";
	std::string scriptName = "";
};

enum ScriptType
{
	stNone,
	stNPC,
	stNPCDeath,
	stObject,
	stTraps,
	stGoods,
};

class GameManager :
	public Element
{
public:
	GameManager();
	virtual ~GameManager();

	void init();

	int gameIndex = 0;

	void initMenuWithThread();
	void initMenuThread();
	void initMenu();

	static GameManager * this_;
	static GameManager * getInstance();

	std::string mapFolderName = "";

	std::atomic<bool> inThread;
	std::mutex loadMutex;
	std::atomic<bool> loadThreadOver;
	bool loadGame(int index);

	void loadGameThread(int index);
	void saveGame(int index);

	void clearMenu();
	bool menuDisplayed();

	void freeResource();

#ifdef __MOBILE__
	static bool actionCmp(NextAction a, NextAction b)
	{
		return a.distance < b.distance;
	}

	std::vector<NextAction> fastSelectingList;

#endif // __MOBILE__

	// Children
	std::shared_ptr<MenuController> menu = nullptr; // = std::make_shared<MenuController>();
	std::shared_ptr<GameController> controller = nullptr; // = std::make_shared<GameController>();
	
	std::shared_ptr<Weather> weather = nullptr; // = std::make_shared<Weather>();

	std::shared_ptr<Camera> camera = nullptr; // = std::make_shared<Camera>();

	std::shared_ptr<Map> map = nullptr; // = std::make_shared<Map>();

	std::shared_ptr<NPCManager> npcManager = nullptr; // = std::make_shared<NPCManager>();
	std::shared_ptr<ObjectManager> objectManager = nullptr; // = std::make_shared<ObjectManager>();
	std::shared_ptr<EffectManager> effectManager = nullptr; // = std::make_shared<EffectManager>();

	std::shared_ptr<VideoPlayer> video = nullptr;

	std::shared_ptr<Player> player = nullptr; // = std::make_shared<Player>();

	Global global;
	Memo memo;
	Traps traps;


	GoodsManager goodsManager;
	MagicManager magicManager;
	PartnerManager partnerManager;

	VariableList varList;
	Script script;

	Point getMousePoint();
	Point getMousePoint(int x, int y);
	void loadMap(const std::string & fileName);
	void loadMapThread(const std::string & fileName);
	void loadNPC(const std::string & fileName);
	void loadNPCThread(const std::string & fileName);
	void loadObject(const std::string & fileName);
	void loadObjectThread(const std::string & fileName);

	void returnToDesktop();

	void clearSelected();

	bool inEvent = false;
	std::shared_ptr<Object> scriptObj = nullptr;
	void runObjScript(std::shared_ptr<Object> obj);
	std::shared_ptr<NPC> scriptNPC = nullptr;
	void runNPCScript(std::shared_ptr<NPC> npc);
	void runNPCDeathScript(std::shared_ptr<NPC> npc, const std::string & scriptName, const std::string & scriptMapName);
	void runEventList();
	std::shared_ptr<Goods> scriptGoods = nullptr;
	void runGoodsScript(std::shared_ptr<Goods> goods);
	std::string scriptMapName = "";
	int scriptTrapIndex = 0;
	void runTrapScript(int idx);
	std::vector<EventInfo> eventList;
	ScriptType scriptType = stNone;

	bool cheatMode = false;
private:
	std::string bgmName = "";

public:
//脚本函数实现

	//流程控制
	int getVar(const std::string & varName);
	void assign(const std::string & varName, int value);
	void add(const std::string & varName, int value);

	//人物对话
	void talk(const std::string & part);
	void say(const std::string & str, int index = -1);

	//工具函数
	void fadeIn();
	void fadeOut();
	void setFadeLum(int lum);
	void setMainLum(int lum);
	void playMusic(const std::string & fileName);
	void stopMusic();
	void playSound(const std::string & fileName);
	void runScript(const std::string & fileName);
	void runScript(const std::string & fileName, const std::string & mapName);
	void moveScreen(int direction, int distance);
	void sleep(unsigned int time);
	void playMovie(const std::string & fileName);
	void stopMovie();
	
	//地图函数
	void loadMapWithThread(const std::string & fileName);
	void loadGameWithThread(int index);
	void setMapPos(int x, int y);
	void setMapTrap(int idx, const std::string & trapFile);
	void saveMapTrap();
	void setMapTime(unsigned char t);
	void changeASFColor(uint8_t r, uint8_t g, uint8_t b);
	void changeMapColor(uint8_t r, uint8_t g, uint8_t b);

	//物品函数
	void loadObjectWithThread(const std::string & fileName);
	void saveObject(const std::string & fileName = "");
	void addObject(const std::string & iniName, int x, int y, int dir);
	void deleteObject(const std::string & name);
	void setObjectPosition(const std::string & name, int x, int y);
	void setObjectKind(const std::string & name, int kind);
	void setObjectScript(const std::string & name, const std::string & scriptFile);
	void clearBody();
	void openBox();
	void closeBox();

	//人物函数
	void loadNPCWithThread(const std::string & fileName);
	void saveNPC(const std::string & fileName = "");
	void addNPC(const std::string & iniName, int x, int y, int dir);
	void deleteNPC(const std::string & name);
	void setNPCRes(const std::string & name, const std::string & resName);
	void setNPCScript(const std::string & name, const std::string & scriptName);
	void setNPCDeathScript(const std::string & name, const std::string & scriptName);
	void goTo(const std::string & name, int x, int y);
	void goToEx(const std::string & name, int x, int y);
	void goToDir(const std::string & name, int dir, int distance);
	void followNPC(const std::string & follower, const std::string & leader);
	void followPlayer(const std::string & follower);
	void enableNPCAI();
	void disableNPCAI();
	void attackTo(const std::string & name, int x, int y);
	void setNPCPosition(const std::string & name, int x, int y);
	void setNPCDir(const std::string & name, int dir);
	void setNPCKind(const std::string & name, int kind);
	void setNPCLevel(const std::string & name, int level);
	void setNPCAction(const std::string & name, int action); //只支持站立（0）和死亡（11）
	void setNPCRelation(const std::string & name, int relation);
	void setNPCActionType(const std::string & name, int actionType); //暂时不支持走动
	void setNPCActionFile(const std::string & name, int action, const std::string & fileName);
	void npcSpecialAction(const std::string & name, const std::string & fileName);
	void changeLife(const std::string& name, int value);
	void changeMana(const std::string& name, int value);
	void changeThew(const std::string& name, int value);

	//主角函数
	void loadPlayer(int index);
	void savePlayer(int index);
	void setPlayerPosition(int x, int y);
	void setPlayerDir(int dir);
	void setPlayerScn();
	void setPlayerLum(unsigned char lum) {};
	void setLevelFile(const std::string & fileName);
	void setMagicLevel(const std::string & magicName, int level);
	void setPlayerLevel(int level);
	void setPlayerState(int state);
	void enableRun();
	void disableRun();
	void enableJump();
	void disableJump();
	void enableFight();
	void disableFight();
	void playerGoto(int x, int y);
	void playerGotoEx(int x, int y);
	void playerRunTo(int x, int y);
	void playerJumpTo(int x, int y);
	void playerGotoDir(int dir, int distance);

	//改变主角属性函数
	void addLife(int value);
	void addLifeMax(int value);
	void addThew(int value);
	void addThewMax(int value);
	void addMana(int value);
	void addManaMax(int value);
	void addAttack(int value);
	void addDefend(int value);
	void addEvade(int value);
	void addExp(int value);
	void addMoney(int value);
	void addRandMoney(int mMin, int mMax);
	void addGoods(const std::string & name);
	void addRandGoods(const std::string & fileName);
	void deleteGoods(const std::string & name);
	void addMagic(const std::string & name);
	void addOneMagic(const std::string& playerName, const std::string& magicName);
	void deleteMagic(const std::string & name);
	void addMagicExp(const std::string & name, int addexp);
	void fullLife();
	void fullThew();
	void fullMana();
	void updateState();
	void saveGoods(int index);
	void loadGoods(int index);
	void clearGoods();
	void getGoodsNum(const std::string & name);
	void getMoneyNum();
	void setMoneyNum(int value);

	//界面函数
	void showMessage(const std::string & str);
	void addToMemo(const std::string & str);
	void clearMemo();
	void buyGoods(const std::string & fileName);
	void sellGoods(const std::string & fileName = "");
	void returnToTitle();
	void enableInput();
	void disableInput();
	void hideInterface();
	void hideBottomWnd();
	void showBottomWnd();
	void hideMouseCursor();
	void showMouseCursor();

	// 天气函数
	void showSnow(int bsnow);
	void showRandomSnow();
	void showRain(int brain);

	void beginRain(const std::string& configFileName);
	void endRain();

	void checkYear(const std::string& varName);

private:
	void loadingDisplayThread(std::vector<_shared_image> loadingImage);
	std::atomic<bool> loadingDisplaying;
private:

	virtual void onUpdate();
	virtual void onDraw();
	virtual bool onInitial();
	virtual void onRun();
	virtual void onExit();
	virtual void onEvent();
	virtual bool onHandleEvent(AEvent & e);

};

