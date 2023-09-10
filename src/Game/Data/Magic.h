#pragma once
#include <string>
#include <vector>
#include "../GameTypes.h"
#include "GameElement.h"

enum MagicMoveKind
{
	mmkPoint = 1,
	mmkFly = 2,
	mmkFlyContinuous = 3,
	mmkCircle = 4,
	mmkHeartCircle = 5,
	mmkHelixCircle = 6,
	mmkSector = 7,
	mmkRandSector = 8,
	mmkLine = 9,
	mmkMoveLine = 10,
	mmkRegion = 11,
	mmkSelf = 13,
	mmkFullScreen = 15,
	mmkFollow = 16,
	mmkThrow = 17,
};

enum MagicSpecialKind
{
	mskAddLife = 1,		// movekind == mmkSelf
	mskAddThew = 2,		// movekind == mmkSelf
	mskAddShield = 3,	// movekind == mmkSelf
	mskFreeze = 1,		// movekind == mmkFly
};

enum MagicRegion
{
	mrSquare = 1,
	mrWave = 3,			// movekind == mmkRegion
	mrCross = 2,		// movekind == mmkRegion	
};

struct MagicLevel
{
	int effect = 0;
	int lifeCost = 0;
	int thewCost = 0;
	int manaCost = 0;
	int levelupExp = 0;

	int moveKind = mmkPoint;
	int specialKind = 0;
	int region = 0;
	int speed = 0;
	int flyingLum = 0;
	int vanishLum = 0;
	int waitFrame = 0;
	int lifeFrame = 0;

	int attackRadius = 0;
};

class Magic
{
public:
	Magic();
	virtual ~Magic();

	void reset();
	void initFromIni(const std::string & fileName);
	void initFromIni(const std::string & fileName, bool loadSpecialMagic);
	std::vector<void *> addEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher, GameElement * target);
	std::vector<void *> addPointEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	std::vector<void *> addFlyEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	std::vector<void *> addContinuousFlyEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	std::vector<void *> addCircleEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	std::vector<void *> addHeartCircleEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	std::vector<void *> addHelixCircleEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	std::vector<void *> addSectorEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher, bool randTime);
	std::vector<void *> addLineEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	std::vector<void *> addMoveLineEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	std::vector<void *> addSquareEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher, int range = -1);
	std::vector<void *> addWaveEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	std::vector<void *> addCrossEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	std::vector<void *> addSelfEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher, int specialKind);
	std::vector<void *> addFullScreenEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	std::vector<void *> addFollowEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher, GameElement * target);
	std::vector<void *> addThrowEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	std::vector<void *> addThrowExplodeEffect(GameElement * user, Point from, Point to, int lvl, int damage, int evade, int launcher);

	double calAngle(Point from, Point to);
	int calDirection(Point from, Point to);
	int calDirection(double angle);
	int calDirection(Point from, Point to, int maxDir);
	int calDirection(double angle, int maxDir);

	void copy(Magic * magic);
	void freeResource();
	void createRes();

	bool imageSelfCreated = false;

	_shared_imp actionImage = nullptr;
	_shared_imp actionShadow = nullptr;

	_shared_imp flyImage = nullptr;
	_shared_imp explodeImage = nullptr;
	_shared_imp superImage = nullptr;

	_shared_imp createActionImage();
	_shared_imp createActionShadow();

	_shared_imp createMagicFlyingImage();
	_shared_imp createMagicVanishImage();
	_shared_imp createMagicSuperModeImage();
	_shared_imp createMagicImage();
	_shared_imp createMagicIcon();

	std::string iniName = "";
	std::string name = "";
	std::string intro = "";

	std::string image = "";
	std::string icon = "";

	std::string flyingImage = "";
	std::string flyingSound = "";
	std::string vanishImage = "";
	std::string vanishSound = "";

	std::string actionFile = "";
	std::string actionShadowFile = "";
	std::string attackFile = "";
	std::string superModeImage = "";


	Magic * specialMagic = nullptr;

	MagicLevel level[MAGIC_MAX_LEVEL + 1];

	double limitAngle(double angle);
	int getDir(double angle, int maxDir);
};

