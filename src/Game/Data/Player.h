﻿#pragma once
#include "NPC.h"
#include "Object.h"

//主要记录主角加上装备后的属性
struct PlayerInfo
{
	int lifeMax = 0;
	int thewMax = 0;
	int manaMax = 0;
	int attack = 0;
	int defend = 0;
	int evade = 0;
};

struct LevelInfo
{
	int levelUpExp = 0;
	int lifeMax = 0;
	int thewMax = 0;
	int manaMax = 0;
	int attack = 0;
	int defend = 0;
	int evade = 0;
	std::string newMagic = u8"";
};

struct LevelList
{
	int levelCount = 0;
	std::vector<LevelInfo> level;
};

struct NextAction
{
	int action = acStand;
	Point dest = { 0, 0 };
	int type = 0;
	std::shared_ptr<GameElement> destGE = nullptr;
	int distance = 0;
};

#define JUMP_THEW_COST 10
#define ATTACK_THEW_COST 10
#define RUN_THEW_COST 2
#define SIT_THEW_COST 5
#define SIT_MANA_ADD_RATE 0.004
#define THEW_RECOVERY_RATE 0.004
#define THEW_RECOVERY_MIN 1
#define THEW_RECOVERY_INTERVAL 40
#define MANA_RECOVERY_INTERVAL 40

#define MIN_THEW_RATE_TO_RUN 0.3
#define MIN_THEW_LIMIT_TO_RUN 50

enum NextDest
{
	ndNone = 0, 
	ndTalk = 1,
	ndObj = 2,
	ndAttack = 3,
};


class Player :
	public NPC
{
public:
	Player();
	virtual ~Player();

	//PlayerInfo存储了人物加上装备后的属性
	PlayerInfo info;
	void calInfo();
	void updateLevel();
	virtual void setLevel(int lvl);
	virtual void fullLife();
	virtual void fullThew();
	virtual void fullMana();
	virtual void addLifeMax(int value);
	virtual void addThewMax(int value);
	virtual void addManaMax(int value);
	virtual void addLife(int value);
	virtual void addThew(int value);
	virtual void addMana(int value);
	virtual void addAttack(int value);
	virtual void addDefend(int value);
	virtual void addEvade(int value);
	virtual void addMoney(int value);

	virtual int getEvade() {return info.evade;}
	virtual int getDefend() {return info.defend;}
	virtual int getAttack() {return info.attack;}
	virtual int getLifeMax() {return info.lifeMax;}
	virtual int getManaMax() {return info.manaMax;}
	virtual int getThewMax() {return info.thewMax;}

	virtual void updateAction(UTime frameTime);
	virtual void onUpdate();
	std::shared_ptr<NextAction> nextAction = nullptr;
	void freeResource();

	virtual void hurt(std::shared_ptr<Effect> e);
	virtual void hurtLife(int damage);
	virtual void addExp(int aExp);
	virtual void levelUp();

	void addNextAction(NextAction& act);

	int nextDest = ndNone;
	//std::shared_ptr<GameElement> destGE = nullptr; //在NPC中已定义
	std::shared_ptr<Effect> shieldEffect = nullptr;

	int magicIndex = 0;
	Point magicDest = { 0, 0 };
	int magic = 40;
	int money = 0;
	bool canRun = true;
	bool canJump = true;
	bool canFight = true;

	int shieldLife = 0;
	UTime shieldLastTime = 0;
	UTime shieldBeginTime = 0;

	UTime recoveryTime = 0;

	virtual void changeWalk(Point dest);
	virtual void changeRun(Point dest);

	virtual void standUp();

	virtual void runObj(std::shared_ptr<Object> obj);
	virtual void talkTo(std::shared_ptr<NPC> npc);
	virtual void beginStand();
	virtual void beginWalk(Point dest);
	virtual void beginMagic(Point dest, std::shared_ptr<GameElement> target = nullptr);
	virtual void beginJump(Point dest);
	virtual void beginRun(Point dest);
	virtual void beginAttack(Point dest, std::shared_ptr<GameElement> target = nullptr);
	virtual void beginHurt(Point dest);
	bool canHurt();
	void checkTrap();

	std::string levelIni = u8"";

	LevelList levelList;

	void limitAttribute();

//	virtual bool mouseInRect(int x, int y) { return false; }

	void loadLevel(const std::string& fileName);
	virtual void doSpecialAttack(Point dest, std::shared_ptr<GameElement> target = nullptr);

	virtual void drawAlpha(Point cenTile, Point cenScreen, PointEx coffset);
	virtual void draw(Point cenTile, Point cenScreen, PointEx coffset, uint32_t colorStyle);

	virtual void playSound(int act);

	virtual void checkDie();
	virtual void beginDie();
	virtual void load(int index = -1);
	virtual void save(int index = -1);

};

