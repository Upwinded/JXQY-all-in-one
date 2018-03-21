#pragma once
#include "GameElement.h"
#include "Magic.h"

enum EffectKind
{
	ekFlying = 0,
	ekExploding = 1,
	ekHiding = 2,
	ekSuperMode = 3,
	ekFlyEnd = 4, //介于飞行与爆炸的中间状态，用于一些飞行时无法碰撞的技能，设置一个独立检测碰撞的状态
	ekThrowing = 5,
};

enum LauncherKind
{
	lkEnemy = 0,
	lkFriend = 1,
	lkSelf = 2,
	lkNeutral = 3,
};

class Effect :
	public GameElement
{
public:
	Effect();
	~Effect();

	unsigned int runTime = 0;
	unsigned int runLastTime = 0;
	virtual void eventRun();

	unsigned int getUpdateTime();
	unsigned int updateTime = 0;

	Magic magic;
	GameElement * user = nullptr;
	std::string fileName = "";
	int doing = ekExploding;
	int level = 0;
	int launcherKind = lkSelf;
	unsigned int lifeTime = 0;
	unsigned int beginTime = 0;
	unsigned int waitTime = 0;
	int damage = 0;
	int evade = 0;
	Point src = { 0, 0 };
	Point dest = { 0, 0 };

	std::deque<Point> passPath = {};

	void beginExplode();
	void beginFly();
	void initFromMagic(Magic * m);
	virtual void initFromIni(INIReader * ini, const std::string & section);
	virtual void saveToIni(INIReader * ini, const std::string & section);
	virtual void playSound(int act);
	
	//技能方向为16个
	int getDirection(Point fDir);
	int calDirection();

	void draw(Point cenTile, Point cenScreen, PointEx coffset);
	_image loadImage(int * x, int * y);
	unsigned int getFlyingTime();
	unsigned int getFlyingImageTime();
	unsigned int getExplodingTime();
	unsigned int getSuperImageTime();
	void calTime();
	void calDest();

	unsigned char getLum();
private:
	void initParam();

	virtual void freeResource();
	virtual void onUpdate();

	

};

