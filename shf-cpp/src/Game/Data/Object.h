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
	IMPImage * image = NULL;
	IMPImage * shadow = NULL;
};

class Object :
	public GameElement
{
public:
	Object();
	~Object();

	void openBox();
	void closeBox();

	bool selected = false;
	int objIndex = 0;

	unsigned int getUpdateTime();
	unsigned int updateTime = 0;

	void drawAlpha(Point cenTile, Point cenScreen, PointEx coffset);
	void draw(Point cenTile, Point cenScreen, PointEx coffset);

	int nowAction = oaStay;
	_image getActionImage(int * offsetx, int * offsety);
	_image getActionShadow(int * offsetx, int * offsety);

	std::string objName = "";
	std::string objectFile = "";
	std::string scriptFile = "";
	std::string wavFile = "";
	int kind = okBody;

	int lum = olNone;
	int damage = 0;
	int frame = 0;

	int state = 0;

	unsigned int damageTime = 0;
	unsigned int damageInterval = 0;
	unsigned int randSoundTime = 0;

	_music sound = NULL;
	_channel channel = NULL;

	void initSound(const std::string & fileName);
	void initRes(const std::string & fileName);
	virtual void saveToIni(INIReader * ini, const std::string & section);
	virtual void initFromIni(INIReader * ini, const std::string & section);
	
	ObjectRes res;

	virtual bool mouseInRect();

private:
	virtual void freeResource();
	void freeSound();
	void freeRes();

	virtual void onUpdate();
	virtual void onEvent();
	virtual bool onHandleEvent(AEvent * e);

};

