#pragma once
#include <string>
#include <vector>
#include "../GameTypes.h"
#include "GameElement.h"
#include "MagicRegionFile.h"

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
	mmkTrail = 19,
	mmkTransport = 20,
	mmkControl = 21,
	mmkSummon = 22,
	mmkTimeStop = 23,
	mmkVMove = 24,
};

enum MagicSpecialKind
{
	mskAddLife = 1,
	mskAddThew = 2,
	mskAddDamageReduceShield = 3,
	mskInvisibleKeepHidden = 4,
	mskInvisibleVisibleWhenAttack = 5,
	mskBlockDamage = 6,
	mskMorphByEffect = 7,
	mskClearAbnormalState = 8,
	mskChangeFlyIni = 9,
	mskImmobilize = 10,
	mskAddShield = 11,
	mskChangeAttributes = 99,
	mskFreeze = 1,
	mskPoison = 2,
	mskPetrify = 3,
};

inline bool isLifeFrameSelfAnchoredSpecialKind(int specialKind)
{
	return specialKind == mskAddDamageReduceShield
		|| specialKind == mskBlockDamage
		|| specialKind == mskAddShield;
}

enum MagicAddonEffect
{
	maeNone = 0,
	maeFrozen = 1,
	maePoison = 2,
	maePetrified = 3,
};

enum MagicRegion
{
	mrSquare = 1,
	mrCross = 2,		// movekind == mmkRegion
	mrWave = 3,			// movekind == mmkRegion
	mrTriangle = 4,		// movekind == mmkRegion
	mrVType = 5,		// movekind == mmkRegion
	mrRegionFile = 6,	// movekind == mmkRegion
};

class Magic;
struct MagicDispatchContext;
struct MagicLoadContext;

struct MagicLinkedLevel
{
	std::string attackFile = "";
	std::string flyMagicFile = "";
	std::string parasiticMagicFile = "";
	std::string randMagicFile = "";
	std::string secondMagicFile = "";
	std::string magicWhenNewPositionFile = "";
	std::string magicToUseWhenKillEnemyFile = "";
	std::string bounceFlyEndMagicFile = "";
	std::string changeMagicFile = "";
	std::string jumpEndMagicFile = "";

	UTime flyInterval = 0;
	int parasitic = 0;
	UTime parasiticInterval = 1000;
	int parasiticMaxEffect = 0;
	int randMagicProbability = 0;
	UTime secondMagicDelay = 0;
	int magicDirectionWhenKillEnemy = 0;
	int bounceFly = 0;
	int bounceFlySpeed = 32;
	int bounceFlyEndHurt = 0;
	int bounceFlyTouchHurt = 0;
	int magicDirectionWhenBounceFlyEnd = 0;
	int jumpToTarget = 0;
	int jumpMoveSpeed = 32;
	int hitCountToChangeMagic = 0;
	int hitCountFlyRadius = 0;
	int hitCountFlyAngleSpeed = 0;

	std::shared_ptr<Magic> specialMagic = nullptr;
	std::shared_ptr<Magic> flyMagic = nullptr;
	std::shared_ptr<Magic> parasiticMagic = nullptr;
	std::shared_ptr<Magic> randMagic = nullptr;
	std::shared_ptr<Magic> secondMagic = nullptr;
	std::shared_ptr<Magic> magicWhenNewPosition = nullptr;
	std::shared_ptr<Magic> magicToUseWhenKillEnemy = nullptr;
	std::shared_ptr<Magic> bounceFlyEndMagic = nullptr;
	std::shared_ptr<Magic> changeMagic = nullptr;
	std::shared_ptr<Magic> jumpEndMagic = nullptr;
};

struct MagicLevel
{
	int effect = 0;
	int effectExt = 0;
	int effect2 = 0;
	int effect3 = 0;
	int effectMana = 0;
	int lifeMax = 0;
	int thewMax = 0;
	int manaMax = 0;
	int attack = 0;
	int attack2 = 0;
	int attack3 = 0;
	int defend = 0;
	int defend2 = 0;
	int defend3 = 0;
	int evade = 0;
	int addThewRestorePercent = 0;
	int addManaRestorePercent = 0;
	int addLifeRestorePercent = 0;
	int rangeAddLife = 0;
	int rangeAddMana = 0;
	int rangeAddThew = 0;
	UTime rangeFreezeMilliseconds = 0;
	UTime rangePoisonMilliseconds = 0;
	UTime rangePetrifyMilliseconds = 0;
	int rangeDamage = 0;
	int leapTimes = 0;
	int leapFrame = 0;
	int effectReducePercentage = 0;
	int lifeCost = 0;
	int thewCost = 0;
	int manaCost = 0;
	int rageCost = 0;
	bool hasRageCost = false;
	int critChanceAddValue = 0;
	bool hasCritChanceAddValue = false;
	int critDamageAddPercent = 0;
	bool hasCritDamageAddPercent = false;
	int levelupExp = 0;
	int count = 0;
	int rangeAddRage = 0;
	bool hasRangeAddRage = false;

	int moveKind = mmkPoint;
	int specialKind = 0;
	int specialKindValue = 0;
	UTime specialKindMilliseconds = 0;
	int alphaBlend = 0;
	int region = 0;
	int speed = 0;
	int flyingLum = 0;
	int vanishLum = 0;
	int waitFrame = 0;
	int lifeFrame = 0;

	int attackRadius = 0;
};

class Effect;
class NPC;

class Magic
{
public:
	static constexpr int MaxLinkedMagicLoadDepth = 16;
	static constexpr size_t MaxLinkedMagicLoadNodes = 256;
	static constexpr int MaxDerivedMagicRuntimeDepth = 16;
	static constexpr size_t MaxDerivedMagicRuntimeNodes = 1024;

	Magic();
	virtual ~Magic();

	void reset();
	void initFromIni(const std::string& fileName);
	void initFromIni(const std::string & fileName, bool loadLinkedMagic);
	static std::vector<std::shared_ptr<Effect>> addEffect(
		std::shared_ptr<Magic> srcMagic,
		std::shared_ptr<GameElement> user,
		Point from,
		Point to,
		int lvl,
		int damage,
		int evade,
		int launcher,
		std::shared_ptr<GameElement> target,
		std::shared_ptr<MagicDispatchContext> dispatchContext = nullptr);
	static std::vector<std::shared_ptr<Effect>> addPointEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	static std::vector<std::shared_ptr<Effect>> addFlyEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	static std::vector<std::shared_ptr<Effect>> addContinuousFlyEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	static std::vector<std::shared_ptr<Effect>> addCircleEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	static std::vector<std::shared_ptr<Effect>> addHeartCircleEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	static std::vector<std::shared_ptr<Effect>> addHelixCircleEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	static std::vector<std::shared_ptr<Effect>> addSectorEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher, bool randTime);
	static std::vector<std::shared_ptr<Effect>> addLineEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	static std::vector<std::shared_ptr<Effect>> addMoveLineEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	static std::vector<std::shared_ptr<Effect>> addVMoveEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	static std::vector<std::shared_ptr<Effect>> addRoundMoveEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	static std::vector<std::shared_ptr<Effect>> addSquareEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher, int range = -1);
	static std::vector<std::shared_ptr<Effect>> addWaveEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	static std::vector<std::shared_ptr<Effect>> addCrossEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	static std::vector<std::shared_ptr<Effect>> addTriangleEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	static std::vector<std::shared_ptr<Effect>> addVTypeEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	static std::vector<std::shared_ptr<Effect>> addRegionFileEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	static std::vector<std::shared_ptr<Effect>> addSelfEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher, int specialKind);
	static std::vector<std::shared_ptr<Effect>> addFullScreenEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	static std::vector<std::shared_ptr<Effect>> addFollowEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher, std::shared_ptr<GameElement> target);
	static std::vector<std::shared_ptr<Effect>> addThrowEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	static std::vector<std::shared_ptr<Effect>> addThrowExplodeEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	static std::vector<std::shared_ptr<Effect>> addTransportEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	static std::vector<std::shared_ptr<Effect>> addControlEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher, std::shared_ptr<GameElement> target);
	static std::vector<std::shared_ptr<Effect>> addSummonEffect(std::shared_ptr<Magic> srcMagic, std::shared_ptr<GameElement> user, Point from, Point to, int lvl, int damage, int evade, int launcher);
	static std::shared_ptr<Effect> createFixedEffect(
		std::shared_ptr<Magic> srcMagic,
		std::shared_ptr<GameElement> user,
		Point position,
		PointEx offset,
		int lvl,
		int damage,
		int evade,
		int launcher,
		std::shared_ptr<MagicDispatchContext> dispatchContext = nullptr);
	static std::shared_ptr<MagicDispatchContext> createRootDispatchContext(
		const std::shared_ptr<Magic>& magic);
	static std::shared_ptr<MagicDispatchContext> createDerivedDispatchContext(
		const std::shared_ptr<MagicDispatchContext>& parentContext,
		const std::shared_ptr<Magic>& childMagic,
		const char* relationship);
	static int calculatePrimaryEffectAmount(
		const std::shared_ptr<Magic>& magic,
		const std::shared_ptr<GameElement>& user,
		int level,
		int fallbackAmount = 0);

	static float getAngle(Point from, Point to);
	static int getDirection(Point from, Point to);
	static int getDirection(float angle);
	static int getDirection(Point from, Point to, int maxDir);
	static int getDirection(float angle, int maxDir);

	void copy(Magic& magic);
	void freeResource();
	void loadRes();
	static std::string resolvePlayerActionFileName(
		const std::string& actionFile,
		const std::string& npcIni)
	{
		if (actionFile.empty())
		{
			return "";
		}

		size_t indexEnd = npcIni.find_last_of('.');
		if (indexEnd == std::string::npos)
		{
			indexEnd = npcIni.size();
		}
		size_t indexBegin = indexEnd;
		while (indexBegin > 0 && npcIni[indexBegin - 1] >= '0' && npcIni[indexBegin - 1] <= '9')
		{
			indexBegin--;
		}
		const std::string index = indexBegin < indexEnd
			? npcIni.substr(indexBegin, indexEnd - indexBegin)
			: "1";

		std::string resolved = actionFile;
		const size_t slash = resolved.find_last_of("/\\");
		const size_t extension = resolved.find_last_of('.');
		if (extension != std::string::npos &&
			(slash == std::string::npos || extension > slash))
		{
			resolved.insert(extension, index);
		}
		else
		{
			resolved += index;
		}
		return resolved;
	}
	_shared_imp getActionImageForNPC(const NPC* npc) const;
	UTime getSpecialKindDurationMilliseconds(int lvl) const;
	const MagicLinkedLevel& getLinkedLevel(int lvl) const;
	const std::string& getExplodeMagicFileForLevel(int lvl) const;
	std::shared_ptr<Magic> getExplodeMagicForLevel(int lvl) const;

	bool imageSelfCreated = false;

	_shared_imp actionImage = nullptr;
	_shared_imp actionShadow = nullptr;
	_shared_imp useActionImage = nullptr;

	_shared_imp flyImage = nullptr;
	_shared_imp explodeImage = nullptr;
	_shared_imp superImage = nullptr;
	_shared_imp leapImage = nullptr;
	_shared_imp hitCountFlyingImage = nullptr;
	_shared_imp hitCountVanishImage = nullptr;

	_shared_imp loadActionImage();
	_shared_imp loadActionShadow();
	_shared_imp loadUseActionImage();

	_shared_imp loadFlyingImage();
	_shared_imp loadVanishImage();
	_shared_imp loadSuperModeImage();
	_shared_imp loadLeapImage();
	_shared_imp loadHitCountFlyingImage();
	_shared_imp loadHitCountVanishImage();
	_shared_imp loadImage();
	_shared_imp loadIcon();

	std::string iniName = "";
	std::string experienceOwnerMagicFile = "";
	bool loadSucceeded = false;
	std::string name = "";
	std::string type = "";
	std::string injuryType = "";
	bool hasInjuryType = false;
	int spriteType = 0;
	bool hasSpriteType = false;
	int attribute = 0;
	bool hasAttribute = false;
	std::string scriptFile = "";
	bool hasScriptFile = false;
	std::string intro = "";

	std::string image = "";
	std::string icon = "";

	std::string flyingImage = "";
	std::string flyingSound = "";
	std::string vanishImage = "";
	std::string vanishSound = "";
	std::string leapImageFile = "";

	std::string actionFile = "";
	std::string actionShadowFile = "";
	std::string useActionFile = "";
	std::string attackFile = "";
	std::string flyMagicFile = "";
	std::string explodeMagicFile = "";
	std::string explodeMagicFilesByLevel[MAGIC_MAX_LEVEL + 1];
	std::string parasiticMagicFile = "";
	std::string randMagicFile = "";
	std::string secondMagicFile = "";
	std::string magicWhenNewPositionFile = "";
	std::string magicToUseWhenKillEnemyFile = "";
	std::string bounceFlyEndMagicFile = "";
	std::string changeMagicFile = "";
	std::string jumpEndMagicFile = "";
	std::string replaceMagic = "";
	std::string specialKind9ReplaceFlyIni = "";
	std::string specialKind9ReplaceFlyIni2 = "";
	std::string flyIni = "";
	std::string flyIni2 = "";
	std::string magicToUseWhenBeAttackedFile = "";
	int magicDirectionWhenBeAttacked = 0;
	std::string hitCountFlyingImageFile = "";
	std::string hitCountVanishImageFile = "";
	UTime flyInterval = 0;
	UTime secondMagicDelay = 0;
	std::string superModeImage = "";
	std::string superModeSound = "";
	std::string regionFileName = "";
	MagicRegionFile regionFile;
	bool regionFileLoaded = false;
	unsigned int keepMilliseconds = 0;
	int maxLevel = 0;
	std::string goodsName = "";
	std::string npcFile = "";
	std::string npcIni = "";
	int maxCount = 0;
	int bodyRadius = 0;
	int disableUse = 0;
	int lifeFullToUse = 0;
	int vibratingScreen = 0;
	int additionalEffect = maeNone;
	int belong = 0;
	int bounce = 0;
	int bounceHurt = 0;
	int bounceFly = 0;
	int bounceFlySpeed = 32;
	int bounceFlyEndHurt = 0;
	int bounceFlyTouchHurt = 0;
	int magicDirectionWhenBounceFlyEnd = 0;
	int carryUser = 0;
	int carryUserSpriteIndex = 0;
	int hideUserWhenCarry = 0;
	int ball = 0;
	int sticky = 0;
	int solid = 0;
	int discardOppositeMagic = 0;
	int exchangeUser = 0;
	int noSpecialKindEffect = 0;
	int randMagicProbability = 0;
	int sideEffectType = 0;
	int sideEffectPercent = 0;
	int sideEffectProbability = 0;
	int noInterruption = 0;
	UTime disableMoveMilliseconds = 0;
	UTime disableSkillMilliseconds = 0;
	UTime coldMilliSeconds = 0;
	int dieAfterUse = 0;
	int restoreType = 0;
	int restorePercent = 0;
	int restoreProbability = 0;
	int attackAddPercent = 0;
	int defendAddPercent = 0;
	int evadeAddPercent = 0;
	int speedAddPercent = 0;
	UTime morphMilliseconds = 0;
	UTime weakMilliseconds = 0;
	int weakAttackPercent = 0;
	int weakDefendPercent = 0;
	UTime blindMilliseconds = 0;
	int parasitic = 0;
	UTime parasiticInterval = 1000;
	int parasiticMaxEffect = 0;
	int magicDirectionWhenKillEnemy = 0;
	UTime changeToFriendMilliseconds = 0;
	int attackAll = 0;
	int traceEnemy = 0;
	int traceSpeed = 0;
	UTime traceEnemyDelayMilliseconds = 0;
	int followMouse = 0;
	int moveImitateUser = 0;
	int moveBack = 0;
	int randomMoveDegree = 0;
	int meteorMove = 0;
	int meteorMoveDir = 5;
	int circleMoveColockwise = 0;
	int circleMoveAnticlockwise = 0;
	int roundMoveColockwise = 0;
	int roundMoveAnticlockwise = 0;
	int roundMoveCount = 1;
	int roundMoveDegreeSpeed = 1;
	int roundRadius = 0;
	int beginAtMouse = 0;
	int beginAtUser = 0;
	int beginAtUserAddDirectionOffset = 0;
	int beginAtUserAddUserDirectionOffset = 0;
	int noExplodeWhenLifeFrameEnd = 0;
	int explodeWhenLifeFrameEnd = 0;
	int passThrough = 0;
	int passThroughWithDestroyEffect = 0;
	int passThroughWall = 0;
	int reviveBodyRadius = 0;
	int reviveBodyMaxCount = 0;
	UTime reviveBodyLifeMilliseconds = 0;
	int rangeEffect = 0;
	int rangeRadius = 0;
	int rangeSpeedUp = 0;
	UTime rangeTimeInterval = 0;
	int jumpToTarget = 0;
	int jumpMoveSpeed = 32;
	int hitCountToChangeMagic = 0;
	int hitCountFlyRadius = 0;
	int hitCountFlyAngleSpeed = 0;


	std::shared_ptr<Magic> specialMagic = nullptr;
	std::shared_ptr<Magic> flyMagic = nullptr;
	std::shared_ptr<Magic> explodeMagic = nullptr;
	std::shared_ptr<Magic> explodeMagicsByLevel[MAGIC_MAX_LEVEL + 1];
	std::shared_ptr<Magic> parasiticMagic = nullptr;
	std::shared_ptr<Magic> randMagic = nullptr;
	std::shared_ptr<Magic> secondMagic = nullptr;
	std::shared_ptr<Magic> magicWhenNewPosition = nullptr;
	std::shared_ptr<Magic> magicToUseWhenKillEnemy = nullptr;
	std::shared_ptr<Magic> bounceFlyEndMagic = nullptr;
	std::shared_ptr<Magic> changeMagic = nullptr;
	std::shared_ptr<Magic> jumpEndMagic = nullptr;

	MagicLinkedLevel linkedLevel[MAGIC_MAX_LEVEL + 1];
	MagicLevel level[MAGIC_MAX_LEVEL + 1];

	static float normalizeAngle(float angle);

private:
	void initFromIniWithContext(
		const std::string& fileName,
		bool loadLinkedMagic,
		bool loadAttackFile,
		const std::string& experienceOwnerOverride,
		MagicLoadContext& loadContext,
		int loadDepth);
};
