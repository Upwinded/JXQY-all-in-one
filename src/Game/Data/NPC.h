﻿#pragma once
#include "GameElement.h"
#include "../GameTypes.h"
#include "Magic.h"
#include "Effect.h"
#include <deque>

enum NPCKind
{
	nkNormal = 0,
	nkBattle = 1,
	nkPlayer = 2,
	nkPartner = 3,
	nkAnimal = 4,
	nkEvent = 5,
};

enum NPCRelation
{
	nrFriendly = 0,
	nrHostile = 1,
	nrNeutral = 2
};

enum PathFinder
{
	pfSingle = 0,
	pfBest = 1,
};

enum NPCLum
{
	nlNone = 0,
	nlRed = 1,
	nlGreen = 2,
	nlBlue = 3,
	nlGray = 4,
	nlAlpha = 5 // >= 5
};

enum NPCActionType
{
	naNone = 0,
	naStroll = 1,
	naGo = 2
};

struct NPCActionRes
{
	std::string imageFile = u8"";
	std::string shadowFile = u8"";
	std::string soundFile = u8"";
	_shared_imp image = nullptr;
	_shared_imp shadow = nullptr;
};

enum ActionCount
{
	acStand = 0,			//站立动作
	acStand1 = 1,			//站立动作2
	acWalk = 2,				//行走动作
	acRun = 3,				//奔跑动作
	acJump = 4,				//跳跃动作
	acAttack = 5,			//攻击动作
	acAttack1 = 6,			//攻击动作2
	acAttack2 = 7,			//攻击动作3
	acMagic = 8,			//施法动作
	acHurt = 9,				//受伤动作
	acSit = 10,				//打坐动作
	acDeath = 11,			//死亡动作
	acSpecial = 12,			//特殊动作（在事件中临时给定）
	acSitting = 13,			//正在坐下动作

	acAStand = 20,			//持剑站立动作
	acAWalk = 21,			//持剑奔跑动作
	acARun = 22,			//持剑跑步动作
	acAJump = 23,			//持剑跳跃动作
	acSpecialAttack = 24,	//武功指定的攻击动作
	acHide = 0xFF,			//死亡后隐藏状态
};

enum SitState
{
	ssSitting = 0,
	ssSat = 1,
};

enum JumpState
{
	jsUp,
	jsJumping,
	jsDown,
};

enum StepState
{
	ssIn,
	ssOut,
};

struct NPCRes
{
	NPCActionRes stand;
	NPCActionRes stand1;
	NPCActionRes walk;
	NPCActionRes run;
	NPCActionRes jump;
	NPCActionRes attack;
	NPCActionRes attack1;
	NPCActionRes attack2;
	NPCActionRes magic;
	NPCActionRes hurt;
	NPCActionRes death;
	NPCActionRes sit;
	NPCActionRes special;
	NPCActionRes specialAttack;
	NPCActionRes astand;
	NPCActionRes awalk;
	NPCActionRes arun;
	NPCActionRes ajump;
};

class NPC :
	public GameElement
{
public:
	NPC();
	virtual ~NPC();

	UTime getUpdateTime();
	UTime updateTime = 0;

	std::shared_ptr<GameElement> destGE = nullptr;
	std::shared_ptr<GameElement> attackTarget = nullptr;

	bool attackDone = false;
	bool haveDest = false;
	Point gotoExDest = { 0, 0 };
	int nowAction = acStand;
	bool selecting = false;
	int npcIndex = 1;

	std::string followNPC = u8"";
	// 作为队友挡住了玩家
	bool isPartnerBlockingPlayer = false;

	Point attackDest = { 0, 0 };
	void calOffset(UTime nowTime, UTime totalTime);
	int getInvertDirection(int dir);
	int calDirection(Point dest);
	UTime calStepLastTime();
	static int calDirection(float angle);
	static int calDirection(Point src, Point dest);
	UTime walkTime = 0;
	std::shared_ptr<Magic> npcMagic = nullptr;
	std::shared_ptr<Magic> npcMagic2 = nullptr;
	bool magicUsed = true;

	std::string npcName = u8"";
	int kind = nkNormal;
	std::string npcIni = u8"";

	int currPos = 0;
	int action = naNone;
	int walkSpeed = 1;
	int standSpeed = 20;
	int pathFinder = pfSingle;
	int dialogRadius = 1;
	std::string scriptFile = u8"";

	int relation = nrFriendly;
	int life = 1000;
	int lifeMax = 1000;
	int thew = 100;
	int thewMax = 100;
	int mana = 100;
	int manaMax = 100;
	int attack = 0;
	int defend = 0;
	int evade = 0;
	int duck = 0;
	int exp = 0;

	virtual int getEvade() { return evade; }
	virtual int getDefend() { return defend; }
	virtual int getAttack() { return attack; }
	virtual int getLifeMax() { return lifeMax; }
	virtual int getManaMax() { return manaMax; }
	virtual int getThewMax() { return thewMax; }

	int levelUpExp = 0;
	int level = 1;
	int attackLevel = 1;	    //技能等级
	int magicLevel = 0;		//无用
	int lum = nlNone;
	int visionRadius = 18;
	int attackRadius = 3;
	std::string bodyIni = u8"";
	std::string flyIni = u8"";
	std::string flyIni2 = u8"";
	std::string magicIni = u8"";
	std::string deathScript = u8"";

	virtual void addLife(int value);
	virtual void addThew(int value);
	virtual void addMana(int value);

	//动作类成员和函数
	bool isSitting();
	bool isStanding();
	bool isAttacking();
	bool isMagicing();
	bool isRunning();
	bool isWalking();
	bool isJumping();
	bool isDying();
	bool isHurting();
	bool isDoingSpecialAction();
	bool canDoAction(NPCActionRes * act);
	bool canDoAction(int act);
	bool canView(Point dest);

	std::deque<Point> stepList;

	void setFight(int f);
	int fight = 0;
	UTime fightTime = 0;
	const UTime fightOverTime = 10000;

	virtual unsigned int eventRun();
	virtual void jumpTo(Point dest);
	virtual void runTo(Point dest);
	virtual void goTo(Point dest);
	virtual void goToEx(Point dest);
	virtual void goToDir(int dir, int distance);
	virtual void attackTo(Point dest, std::shared_ptr<GameElement> target = nullptr);
	virtual void doSpecialAction(const std::string & fileName);
	virtual void setLevel(int lvl);

	virtual void clearStep();

	virtual void beginStand();
	virtual void beginWalk(Point dest);
	virtual void beginHurt(Point dest);
	virtual void beginHurt();
	virtual void checkDie();
	virtual void beginDieScript();
	virtual void beginDie();
	virtual void beginAttack(Point dest, std::shared_ptr<GameElement> target = nullptr);
	virtual void beginSpecial();

	virtual void beginSit();
	virtual void beginMagic(Point dest, std::shared_ptr<GameElement> target = nullptr);
	virtual void beginJump(Point dest);
	virtual void beginRun(Point dest);

	// 开始朝着目标方向走步（不寻路）（走路动作时间重置）
	virtual void beginRadiusStep(Point dest, int radius, bool findNearDir = true);
	
	// 改变目标,朝着目标方向走步（不寻路）（走路动作时间不重置）
	virtual void changeRadiusStep(Point dest, int radius, bool findNearDir = true);

	// 开始朝着目标寻找路径移动（走路动作时间重置）
	virtual void beginRadiusMove(Point dest, int radius, bool isRun = false);

	// 开始朝着目标寻找路径移动（走路动作时间重置）
	virtual void beginRadiusWalk(Point dest, int radius);

	// 开始朝着目标寻找路径移动（走路动作时间重置）
	virtual void beginRadiusRun(Point dest, int radius);

	// 改变目标并朝着目标寻找路径移动（走路动作时间不重置）
	virtual void changeRadiusMove(Point dest, int radius, bool isRun = false, bool dontStandWhenFailed = false);

	UTime lastTimeTryingToFollow = 0;
	bool isFollower();
	bool isFollowAttack(std::shared_ptr<NPC> npc);
	virtual void beginFollowWalk(Point dest);
	virtual void beginFollowRun(Point dest);
	virtual void beginFollowAttack(Point dest);
	virtual void changeFollowWalk(Point dest);
	virtual void changeFollowRun(Point dest);
	virtual void changeFollowAttack(Point dest);

	virtual void hurt(std::shared_ptr<Effect> e);
	virtual void directHurt(std::shared_ptr<Effect> e);
	void addBody();

	virtual void doAttack(Point dest, std::shared_ptr<GameElement> target);
	virtual void useMagic(std::shared_ptr<Magic> m, Point dest, int level, std::shared_ptr<GameElement> target);

	bool frozen = false;
	UTime frozenLastTime = 0;
	float jumpSpeed = 10;
	int runSpeed = 3;
	unsigned int jumpState = 0;
	UTime getActionTime(int act);
	UTime stepBeginTime = 0;
	UTime stepLastTime = 0;
	unsigned int stepState = 0;
	unsigned int sitState = 0;

	virtual void playSound(int act);

	//绘图函数
	_shared_image getActionImage(int * offsetx, int * offsety);
	_shared_image getActionShadow(int * offsetx, int * offsety);
	virtual void draw(Point cenTile, Point cenScreen, PointEx coffset, uint32_t colorStyle);
	void drawNPCAlpha(Point cenTile, Point cenScreen, PointEx coffset);
	
	void limitDir(int * d);
	void limitDir();

	//图像资源函数
	NPCRes res;
	virtual void initFromIni(INIReader * ini, const std::string & section);
	virtual void saveToIni(INIReader * ini, const std::string & section);
	virtual void loadActionRes(NPCActionRes * npcAction);
	virtual void reloadAction();
	virtual void initActionFromIni(NPCActionRes * npcAction, INIReader * iniReader, const std::string & section);
	virtual void loadSpecialAction(const std::string & fileName);
	virtual void initRes(const std::string & fileName);	
	virtual void loadActionFile(const std::string & fileName, int act);

	void freeResource();

protected:
	virtual bool mouseInRect(int x, int y);

	void freeNPCRes();
	void freeNPCAction(NPCActionRes * act);
	virtual void freeActionImage(NPCActionRes * act);

	virtual void onUpdate();
	virtual void updateAction(UTime frameTime);
	virtual void onEvent();

	virtual void onMouseLeftDown(int x, int y);
	virtual void onMouseLeftUp(int x, int y);
	virtual void onMouseMoveIn(int x, int y);
	virtual void onMouseMoveOut();
};

