#pragma once
#include "GameElement.h"

enum ObjectKind
{
	okOrnament = 0,
	okBox = 1,
	okBody = 2,
	okSound = 3,
	okRndSound = 4,
	okDoor = 5,
	okTrap = 6
};

enum ObjectLum
{
	olNone = 0,
	olRed = 1,
	olGreen = 2,
	olBlue = 3,
	olGray = 4,
	olAlpha = 5 // >= 5
};

enum ObjectAction
{
	oaStay = 0,
	oaPlaying = 1,
	oaOpening = 2,
	oaClosing = 3,
};

struct ObjectRes
{
	std::string imageFile = "";
	std::string shadowFile = "";
	std::string soundFile = "";
	_shared_imp image = nullptr;
	_shared_imp shadow = nullptr;
};

class Object :
	public GameElement
{
public:
	Object();
	virtual ~Object();

	void openBox();
	void closeBox();

	bool selecting = false;
	int objIndex = 0;

	UTime getUpdateTime();
	UTime updateTime = 0;

	void drawAlpha(Point cenTile, Point cenScreen, PointEx coffset);
	void draw(Point cenTile, Point cenScreen, PointEx coffset);

	int nowAction = oaStay;
	_shared_image getActionImage(int * offsetx, int * offsety);
	_shared_image getActionShadow(int * offsetx, int * offsety);

	std::string objName = "";
	std::string objectFile = "";
	std::string scriptFile = "";
	std::string wavFile = "";
	int kind = okBody;

	int lum = olNone;
	int damage = 0;
	int frame = 0;

	int state = 0;

	UTime damageTime = 0;
	UTime damageInterval = 0;
	UTime randSoundTime = 0;

	_music sound = nullptr;
	_channel channel = nullptr;

	void initSound(const std::string & fileName);
	void initRes(const std::string & fileName);
	virtual void saveToIni(INIReader * ini, const std::string & section);
	virtual void initFromIni(INIReader * ini, const std::string & section);
	
	ObjectRes res;

	virtual bool mouseInRect(int x, int y);

private:
	void freeResource();
	void freeSound();
	void freeRes();

	virtual void onUpdate();
	virtual void onEvent();
	virtual void onMouseLeftDown(int x, int y);
	virtual void onMouseMoveOut();

};

