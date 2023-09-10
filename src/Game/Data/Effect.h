#pragma once
#include "GameElement.h"
#include "Magic.h"

enum EffectKind
{
	ekFlying = 0,
	ekExploding = 1,
	ekHiding = 2,
	ekSuperMode = 3,
	ekThrowing = 4,
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
	virtual ~Effect();

	UTime runTime = 0;
	UTime runLastTime = 0;
	virtual void eventRun();

	UTime getUpdateTime();
	UTime updateTime = 0;

	Magic magic;
	GameElement * user = nullptr;
	GameElement * target = nullptr;
	std::string fileName = "";
	int doing = ekExploding;
	int level = 0;
	int launcherKind = lkSelf;
	UTime lifeTime = 0;
	UTime beginTime = 0;
	UTime waitTime = 0;
	int damage = 0;
	int evade = 0;
	Point src = { 0, 0 };
	PointEx srcOffset = { 0, 0 };
	Point dest = { 0, 0 };
	PointEx destOffset = { 0, 0 };
	double width = 0.5;

	std::deque<Point> passPath;

	void beginExplode();
	void beginFly();
	void beginDrop();
	void initFromMagic(Magic * m);
	virtual void initFromIni(INIReader * ini, const std::string & section);
	virtual void saveToIni(INIReader * ini, const std::string & section);
	virtual void playSound(int act);
	
	//技能方向为16个
	int getDirection(Point fDir);
	int calDirection();

	void draw(Point cenTile, Point cenScreen, PointEx coffset);
	_shared_image loadImage(int * x, int * y);
	UTime getFlyingTime();
	UTime getFlyingImageTime();
	UTime getExplodingTime();
	UTime getSuperImageTime();
	void calTime();
	void calDest();
	auto getPassPath(Point from, PointEx fromOffset, Point to, PointEx toOffset);
	void changeFollowTarget(GameElement * newTarget);
	unsigned char getLum();
	bool noLum = false;
	int getMoveKind();
private:
	void initParam();

	void freeResource();
	virtual void onUpdate();

};

