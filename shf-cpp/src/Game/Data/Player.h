#pragma once
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
	std::string newMagic = "";
};

struct LevelList
{
	int levelCount = 0;
	std::vector<LevelInfo> level = {};
};

struct NextAction
{
	int action = acStand;
	Point dest = { 0, 0 };
	int type = 0;
	GameElement * destGE = NULL;
};

#define JUMP_THEW_COST 10
#define ATTACK_THEW_COST 10
#define RUN_THEW_COST 3
#define SIT_THEW_COST 25
#define SIT_MANA_ADD 25
#define THEW_RECOVERY 10
#define THEW_RECOVERY_INTERVAL 300
#define MANA_RECOVERY_INTERVAL 100

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
	~Player();

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

	virtual void updateAction(unsigned int frameTime);
	virtual void onUpdate();
	NextAction * nextAction = NULL;
	virtual void freeResource();

	virtual void hurt(Effect * e);
	virtual void hurtLife(int damage);
	virtual void addExp(int aExp);
	virtual void levelUp();

	void addNextAction(NextAction * act);

	int nextDest = ndNone;
	//GameElement * destGE = NULL; //在NPC中已定义
	Effect * shieldEffect = NULL;

	int magicIndex = 0;
	Point magicDest = { 0, 0 };
	int magic = 40;
	int money = 0;
	bool canRun = true;
	bool canJump = true;
	bool canFight = true;

	int shieldLife = 0;
	unsigned int shieldLastTime = 0;
	unsigned int shieldBeginTime = 0;

	unsigned int recoveryTime = 0;

	virtual void changeWalk(Point dest);
	virtual void changeRun(Point dest);

	virtual void standUp();

	virtual void runObj(Object * obj);
	virtual void talkTo(NPC * npc);
	virtual void beginStand();
	virtual void beginWalk(Point dest);
	virtual void beginMagic(Point dest, GameElement * target = nullptr);
	virtual void beginJump(Point dest);
	virtual void beginRun(Point dest);
	virtual void beginAttack(Point dest, GameElement * target = nullptr);
	virtual void beginHurt(Point dest);
	bool canHurt();
	void checkTrap();

	std::string levelIni = "";

	LevelList levelList;

	void limitAttribute();

	void loadLevel(const std::string& fileName);
	virtual void doSpecialAttack(Point dest, GameElement * target = nullptr);

	virtual void drawAlpha(Point cenTile, Point cenScreen, PointEx coffset);
	virtual void draw(Point cenTile, Point cenScreen, PointEx coffset);

	virtual void playSound(int act);

	virtual void checkDie();
	virtual void beginDie();
	virtual void load(const std::string & fileName = "");
	virtual void save(const std::string & fileName = "");

};

