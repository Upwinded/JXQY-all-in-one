#pragma once
#include "GameElement.h"
#include "../GameTypes.h"
#include "Magic.h"
#include "Effect.h"
#include "NPCAction/NPCActionType.h"
#include "NPCAction/NPCActionManager.h"
#include <deque>
#include <climits>
#include <cstdint>
#include <vector>
#include <optional>
#include <unordered_map>
#include <initializer_list>

#define NPC_PATH_FIND_FAIL_COOLDOWN 80

class Goods;

// ===================== 枚举类型 =====================

enum NPCKind
{
	nkNormal = 0,
	nkBattle = 1,
	nkPlayer = 2,
	nkPartner = 3,
	nkGroundAnimal = 4,
	nkAnimal = nkGroundAnimal,
	nkEvent = 5,
	nkAfraidPlayerAnimal = 6,
	nkFlyingAnimal = 7,
};

enum NPCRelation
{
	nrFriendly = 0,
	nrHostile = 1,
	nrNeutral = 2,
	nrNone = 3,
};

enum PathFinder
{
	pfSingle = 0,
	pfBest = 1,
};

enum NPCPathType
{
	nptPathOneStep = 0,
	nptSimpleMaxNpcTry = 1,
	nptPerfectMaxNpcTry = 2,
	nptPerfectMaxPlayerTry = 3,
	nptPathStraightLine = 4,
	nptEnd = 5,
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

enum NPCStrollIntent
{
	nsiNone = 0,
	nsiStroll = 1,
	nsiGo = 2,
	nsiLegacyStand = 6,
};

enum AttackReleaseMode
{
	armCombatTarget,
	armGroundTarget,
	armLockedRelease
};

enum RadiusMoveResult
{
	rmrMoved,
	rmrAlreadyInRadius,
	rmrFailed
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

enum NPCPlanState
{
	npsNone = 0,
	npsApproaching,
	npsInAttackRange,
};

struct NPCAttackOption
{
	std::shared_ptr<Magic> magic = nullptr;
	int configuredUseDistance = 0;
	bool hasExplicitUseDistance = false;
	int moveKind = 0;
	int region = 0;
	int shapeRange = 0;
	int sourceIndex = 0;
	bool isTargetAttack = true;
	bool useAdditionalEffect = false;

	NPCAttackOption() {}
	bool operator<(const NPCAttackOption& other) const
	{
		return sourceIndex < other.sourceIndex;
	}
};

struct NPCMagicToUseWhenAttacked
{
	std::string sourceFile = "";
	std::shared_ptr<Magic> magic = nullptr;
	int direction = 0;
};

struct SkillScore
{
	int moveCost = INT_MAX;
	bool canHitNow = false;
	bool isInertia = false;

	bool isBetterThan(const SkillScore& other) const
	{
		if (canHitNow != other.canHitNow)
		{
			return canHitNow > other.canHitNow;
		}
		if (moveCost != other.moveCost)
		{
			return moveCost < other.moveCost;
		}
		if (isInertia != other.isInertia)
		{
			return isInertia > other.isInertia;
		}
		return false;
	}
};

struct AttackCandidateInfo
{
	NPCAttackOption option;
	Point desiredPosition = { 0, 0 };
	bool positionValid = false;
	bool canHitNow = false;
	bool desiredPositionCanHit = false;
	bool approachOnly = false;
	bool requiresExactPosition = false;
	int effectiveDistance = 0;
	int moveCost = INT_MAX;
};

struct NPCActionPlan
{
	NPCPlanState state = npsNone;
	NPCAttackOption selectedOption;
	bool hasSelectedOption = false;
	std::weak_ptr<GameElement> planTarget;
	UTime planStartTime = 0;
	Point desiredAttackPosition = { 0, 0 };
	bool requiresExactPosition = false;
	bool approachOnly = false;
	Point planOriginTargetPosition = { 0, 0 };
	Point lastTargetPosition = { 0, 0 };
	Point lastNpcPosition = { 0, 0 };
	UTime lastReplanTime = 0;

	static constexpr UTime PlanTimeout = 2000;
	static constexpr int ReplanMoveThreshold = 3;

	bool isActive() const { return state != npsNone; }
	bool isExpired(UTime currentTime) const
	{
		return isActive() && (currentTime - planStartTime >= PlanTimeout);
	}
	void reset()
	{
		state = npsNone;
		selectedOption = NPCAttackOption();
		hasSelectedOption = false;
		planTarget.reset();
		planStartTime = 0;
		desiredAttackPosition = { 0, 0 };
		requiresExactPosition = false;
		approachOnly = false;
		planOriginTargetPosition = { 0, 0 };
		lastTargetPosition = { 0, 0 };
		lastNpcPosition = { 0, 0 };
		lastReplanTime = 0;
	}
};

// ===================== 动作类型快捷常量 =====================

static constexpr NPCActionType acStand = NPCActionType::acStand;
static constexpr NPCActionType acStand1 = NPCActionType::acStand1;
static constexpr NPCActionType acWalk = NPCActionType::acWalk;
static constexpr NPCActionType acRun = NPCActionType::acRun;
static constexpr NPCActionType acJump = NPCActionType::acJump;
static constexpr NPCActionType acAttack = NPCActionType::acAttack;
static constexpr NPCActionType acAttack1 = NPCActionType::acAttack1;
static constexpr NPCActionType acAttack2 = NPCActionType::acAttack2;
static constexpr NPCActionType acMagic = NPCActionType::acMagic;
static constexpr NPCActionType acHurt = NPCActionType::acHurt;
static constexpr NPCActionType acSit = NPCActionType::acSit;
static constexpr NPCActionType acDeath = NPCActionType::acDeath;
static constexpr NPCActionType acSpecial = NPCActionType::acSpecial;
static constexpr NPCActionType acSitting = NPCActionType::acSitting;
static constexpr NPCActionType acAStand = NPCActionType::acAStand;
static constexpr NPCActionType acAWalk = NPCActionType::acAWalk;
static constexpr NPCActionType acARun = NPCActionType::acARun;
static constexpr NPCActionType acAJump = NPCActionType::acAJump;
static constexpr NPCActionType acSpecialAttack = NPCActionType::acSpecialAttack;
static constexpr NPCActionType acBounce = NPCActionType::acBounce;
static constexpr NPCActionType acMagicForcedMove = NPCActionType::acMagicForcedMove;
static constexpr NPCActionType acHide = NPCActionType::acHide;

// ===================== 资源与状态结构体 =====================

struct NPCActionRes
{
	std::string imageFile = "";
	std::string shadowFile = "";
	std::string soundFile = "";
	_shared_imp imagePackage = nullptr;
	_shared_imp shadowPackage = nullptr;
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

// 战斗状态，带有超时自动恢复
struct NPCFightState
{
private:
	bool isFightState = false;
	UTime fightStateLastTime = 0;
public:
	bool get() { return isFightState; }
	void set(bool isFightState_) 
	{
		const UTime fightMaxLastTime = 10000;
		isFightState = isFightState_;
		if (isFightState)
		{
			fightStateLastTime = fightMaxLastTime;
		}
		else
		{
			fightStateLastTime = 0;
		}
	}

	void update(UTime frameTime)
	{
		if (isFightState)
		{
			if (frameTime >= fightStateLastTime)
			{
				isFightState = false;
				fightStateLastTime = 0;
			}
			else
			{
				fightStateLastTime -= frameTime;
			}
		}
	}
};

// ===================== NPC 类 =====================

struct NPCLevelInfo
{
	int levelUpExp = 0;
	int exp = 0;
	int life = 0;
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
};

struct NPCEquipmentAttributes
{
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

	void reset()
	{
		lifeMax = 0;
		thewMax = 0;
		manaMax = 0;
		attack = 0;
		attack2 = 0;
		attack3 = 0;
		defend = 0;
		defend2 = 0;
		defend3 = 0;
		evade = 0;
	}
};

struct NPCMagicEffectBonus
{
	int percent = 0;
	int amount = 0;

	void add(int addPercent, int addAmount, int count = 1)
	{
		if (count <= 0)
		{
			return;
		}
		auto addRepeated = [count](int current, int value)
		{
			const int64_t total = static_cast<int64_t>(current) +
				static_cast<int64_t>(value) * count;
			if (total > INT_MAX)
			{
				return INT_MAX;
			}
			if (total < INT_MIN)
			{
				return INT_MIN;
			}
			return static_cast<int>(total);
		};
		percent = addRepeated(percent, addPercent);
		amount = addRepeated(amount, addAmount);
	}
};

struct NPCMagicForcedMoveState
{
	bool active = false;
	Point destination = { 0, 0 };
	float speed = 32.0f;
	std::weak_ptr<GameElement> caster;
	std::shared_ptr<Magic> endMagic = nullptr;
	int level = 1;
	int launcherKind = lkNeutral;
	int endDirectionMode = 0;
	int endHurt = 0;
	int touchHurt = 0;
	int touchDistance = 0;
	bool hasTouchDirection = false;
	PointEx touchDirection = { 0.0f, 0.0f };
	bool blockCharactersOnPath = false;
	bool hasBlockedCharacterPosition = false;
	Point blockedCharacterPosition = { 0, 0 };
	Point startPosition = { 0, 0 };
	PointEx startOffset = { 0.0f, 0.0f };
	PointEx lineOffset = { 0.0f, 0.0f };
	PointEx drawOffset = { 0.0f, 0.0f };
	float totalProjectedDistance = 0.0f;
	float movedProjectedDistance = 0.0f;
	std::shared_ptr<MagicDispatchContext> magicDispatchContext = nullptr;
};

class NPC :
	public GameElement
{
	friend class NPCActionBase;
	friend class NPCActionStand;
	friend class NPCActionWalk;
	friend class NPCActionRun;
	friend class NPCActionJump;
	friend class NPCActionAttack;
	friend class NPCActionBounce;
	friend class NPCActionMagic;
	friend class NPCActionHurt;
	friend class NPCActionDeath;
	friend class NPCActionHide;
	friend class NPCActionSit;
	friend class NPCActionSpecial;
	friend class NPCActionManager;
protected:
	using GameElement::position;
	using GameElement::offset;
public:
	NPC();
	virtual ~NPC();

	// ---- 基础标识 ----
	std::string npcName = "";
	std::string showName = "";
	int kind = nkNormal;
	std::string npcIni = "";
	int sex = 0;
	int relation = nrFriendly;
	bool detachedEffectCaster = false;
	int kindValue = 0;
	int kindValueMax = 0;
	std::string talkContent = "";
	std::string bagGoods = "";
	int steal = 0;
	int eloquence = 0;
	int leechcraft = 0;
	int autoRunScript = 0;
	bool hasAutoRunScriptField = false;
	int arm = 0;
	bool hasArmField = false;
	int evadeN = 0;
	bool hasEvadeNField = false;
	int gengu = 0;
	bool hasGenguField = false;
	int neixi = 0;
	bool hasNeixiField = false;
	int physique = 0;
	bool hasPhysiqueField = false;
	bool isSignalShow = false;
	int signalIndex = 0;
	std::string signalType = "";
	std::string getDisplayName() const;
	static bool isPlayerKind(int candidateKind)
	{
		return candidateKind == nkPlayer;
	}
	static bool isEnemyKindRelation(int candidateKind, int candidateRelation)
	{
		return candidateKind == nkBattle && candidateRelation == nrHostile;
	}
	static bool isNoneFighterKindRelation(int candidateKind, int candidateRelation)
	{
		return candidateKind == nkBattle && candidateRelation == nrNone;
	}
	static bool isFighterFriendKindRelation(int candidateKind, int candidateRelation)
	{
		return (candidateKind == nkBattle || candidateKind == nkPartner) && candidateRelation == nrFriendly;
	}
	static bool isFighterLikeKind(int candidateKind)
	{
		return candidateKind == nkBattle || candidateKind == nkPartner;
	}
	static bool isInteractiveKindRelation(int candidateKind, int candidateRelation, bool hasScript, bool hasRightScript)
	{
		return hasScript || hasRightScript
			|| isEnemyKindRelation(candidateKind, candidateRelation)
			|| isFighterFriendKindRelation(candidateKind, candidateRelation)
			|| isNoneFighterKindRelation(candidateKind, candidateRelation);
	}
	static bool shouldDrawLifeBarKindRelation(
		int candidateKind,
		int candidateRelation,
		bool partnerCombat)
	{
		const bool combatKind = candidateKind == nkBattle
			|| (candidateKind == nkPartner && partnerCombat);
		return combatKind && candidateRelation != nrNeutral;
	}
	static bool isObstacleKind(int candidateKind, bool candidateVisibleByVariable)
	{
		return candidateKind != nkFlyingAnimal && candidateVisibleByVariable;
	}
	static bool isVisibleForRuntimeState(bool candidateVisibleByVariable, UTime candidateInvisibleMilliseconds)
	{
		return candidateVisibleByVariable && candidateInvisibleMilliseconds == 0;
	}
	static bool isObstacleKindRuntime(int candidateKind, bool candidateVisibleByVariable, UTime candidateInvisibleMilliseconds)
	{
		return isObstacleKind(candidateKind, isVisibleForRuntimeState(candidateVisibleByVariable, candidateInvisibleMilliseconds));
	}
	bool isEnemy() const
	{
		return isEnemyKindRelation(kind, relation);
	}
	bool isNoneFighter() const
	{
		return isNoneFighterKindRelation(kind, relation);
	}
	bool isFighterFriend() const
	{
		return isFighterFriendKindRelation(kind, relation);
	}
	bool isFighterLike() const
	{
		return isFighterLikeKind(kind);
	}
	bool isInteractive() const
	{
		return isInteractiveKindRelation(kind, relation, !scriptFile.empty(), !scriptFileRight.empty());
	}
	bool isInvisibleByMagic() const
	{
		return invisibleMilliseconds > 0;
	}
	bool isTransporting() const;
	bool isVisibleForRuntime() const
	{
		return !scriptHidden
			&& isVisibleForRuntimeState(isVisibleByVariable, invisibleMilliseconds)
			&& !isTransporting();
	}
	bool isObstacleForCharacter() const
	{
		return !scriptHidden
			&& isObstacleKindRuntime(kind, isVisibleByVariable, invisibleMilliseconds)
			&& !isTransporting();
	}
	void setScriptHidden(bool hidden);

	// ---- 动作管理 ----
	std::unique_ptr<NPCActionManager> actionManager;
	NPCActionType nowAction = NPCActionType::acStand;
	NPCActionRes scriptSpecialActionOverlayResource;
	bool scriptSpecialActionOverlayActive = false;
	bool scriptSpecialActionOverlaySupersededByAction = false;
	UTime scriptSpecialActionOverlayElapsed = 0;
	UTime scriptSpecialActionOverlayDuration = 0;
	int scriptSpecialActionOverlayDirection = 0;
	uint64_t scriptSpecialActionUnderlyingActionRevision = 0;
	bool eventRunUntilScriptSpecialActionEnds = false;
	NPCFightState fightState;

	// ---- 步进系统 ----
	// 两阶段步进: ssOut(离开当前格) -> ssIn(进入下一格)
	// offset 通过 percent = nowTime / stepLastTime 在两阶段中做线性插值
	std::deque<Point> stepList;
	UTime stepBeginTime = 0;
	UTime stepLastTime = 0;
	UTime walkTime = 0;
	bool processingStepIn = false;
	void calOffset(UTime nowTime, UTime totalTime);
	bool resumingMove = false;
	NPCActionType previousMoveAction = NPCActionType::acStand;
	int savedDirection = 0;
	UTime hurtBeginStepTime = 0;
	unsigned int savedStepState = 0;
	std::vector<Point> savedStepPositions;

	// ---- 时间 ----
	UTime lastPathFindFailTime = 0;

	// ---- 属性 ----
	int life = 1000;
	int lifeMax = 1000;
	int thew = 100;
	int thewMax = 100;
	int mana = 100;
	int manaMax = 100;
	float displayLifePercent = 1.0f;
	_shared_image criticalDamageTipImage = nullptr;
	UTime criticalDamageTipBeginTime = 0;
	int attack = 0;
	int attack2 = 0;
	int attack3 = 0;
	int defend = 0;
	int defend2 = 0;
	int defend3 = 0;
	int evade = 0;
	int duck = 0;
	int dodgeBeginFrame = 0;
	bool hasDodgeBeginFrameField = false;
	int dodgeEndFrame = 0;
	bool hasDodgeEndFrameField = false;
	int exp = 0;
	int expBonus = 0;
	bool hasExpBonusField = false;
	int levelUpExp = 0;
	int canLevelUp = 0;
	int level = 1;
	std::string npcLevelIni = "";
	std::vector<NPCLevelInfo> npcLevelList;
	int attackLevel = 1;
	int magicLevel = 0;
	int walkSpeed = 1;
	int runSpeed = 3;
	int standSpeed = 20;
	int attackSpeed = 1;
	int idle = 0;
	int idledFrame = 0;
	int aiType = 0;
	int group = 0;
	int noAutoAttackPlayer = 0;
	int stopFindingTarget = 0;
	bool hasAttackSpeedField = false;
	float jumpSpeed = 10;

	virtual int getEvade() { return applyTemporaryEvadeModifiers(evade + equipmentAttributes.evade); }
	virtual int getDefend() { return applyTemporaryDefendModifiers(defend + equipmentAttributes.defend); }
	virtual int getDefend2() { return defend2 + equipmentAttributes.defend2; }
	virtual int getDefend3() { return defend3 + equipmentAttributes.defend3; }
	virtual int getAttack() { return applyTemporaryAttackModifiers(attack + equipmentAttributes.attack); }
	virtual int getAttack2() { return attack2 + equipmentAttributes.attack2; }
	virtual int getAttack3() { return attack3 + equipmentAttributes.attack3; }
	virtual int getLifeMax() { return lifeMax + equipmentAttributes.lifeMax; }
	virtual int getManaMax() { return manaMax + equipmentAttributes.manaMax; }
	virtual int getThewMax() { return thewMax + equipmentAttributes.thewMax; }
	int applyTemporaryAttackModifiers(int value) const;
	int applyTemporaryDefendModifiers(int value) const;
	int applyTemporaryEvadeModifiers(int value) const;
	int getTemporarySpeedAddPercent() const;
	int applyMagicEffectBonus(const Magic& magic, int effect) const;
	void resetEquipmentMagicEffectBonuses();
	void addEquipmentMagicEffectBonus(const Goods& goods, int count = 1);
	void addEquipmentRuntimeEffect(
		const Goods& goods,
		const std::string& sourceFile = "",
		int count = 1);
	void addMagicPassiveRuntimeEffect(const Magic& magic, const std::string& sourceFile = "");
	float getMoveSpeedFold() const;
	float getAdjustedWalkSpeed() const;
	float getAdjustedRunSpeed() const;
	bool hasActiveRangeSpeedUp() const;
	void applyRangeSpeedUp(std::shared_ptr<Effect> effect);
	void clearRangeSpeedUp();
	int getRangeSpeedUpPercent() const;
	void updateEquipmentLifeRestore(UTime frameTime);
	int getAttackAdditionalEffect() const { return attackAdditionalEffect; }
	bool isRandMoveRandAttack() const { return aiType == 1 || aiType == 2; }
	bool isNotFightBackWhenBeHit() const { return aiType == 2; }
	void updateIdleFrame();
	bool canStartIdleAttack();
	void applyBounceFromEffect(const Effect& effect);
	void applyBounceFlyFromEffect(const Effect& effect);
	void updateBounceMovement(UTime frameTime);
	void clearBounceState();
	void applyBounceCollisionHurt(Point blockedPosition);
	bool isMagicForcedMoving() const;
	void beginMagicForcedMove(Point destination,
		float speed,
		std::weak_ptr<GameElement> caster,
		std::shared_ptr<Magic> endMagic,
		int level,
		int launcherKind,
		int endDirectionMode,
		int endHurt,
		int touchHurt,
		int touchDistance,
		bool blockCharactersOnPath = false,
		PointEx touchDirection = { 0.0f, 0.0f },
		std::shared_ptr<MagicDispatchContext> dispatchContext = nullptr);
	void updateMagicForcedMovement(UTime frameTime);
	void clearMagicForcedMoveState();
	void finishMagicForcedMove();
	void applyEffectActionLocks(const Effect& effect);
	void updateActionLockTimers(UTime frameTime);
	void applyEffectRuntimeStates(const Effect& effect);
	void updateMagicRuntimeStateTimers(UTime frameTime);
	void clearMagicRuntimeStates();
	void applyMagicInvisibility(UTime milliseconds, bool visibleWhenAttack);
	void revealMagicInvisibilityOnAction();
	void applyTemporaryMorph(const Magic& magic, UTime milliseconds);
	void applyTemporaryNpcRes(const Magic& magic);
	void clearTemporaryNpcRes(bool refreshAction = true);
	void applyTemporaryFlyIniChange(std::shared_ptr<Effect> effect);
	void clearTemporaryFlyIniChange(bool rebuildOptions = true);
	void applyTransportEffect(std::shared_ptr<Effect> effect);
	void clearTransportEffect(const Effect* effect = nullptr);
	void applyTemporaryMagicListReplacement(const Magic& magic);
	void clearTemporaryMagicListReplacement(bool rebuildOptions = true);
	static bool shouldApplyTemporaryMagicListReplacement(int kind, const std::string& replacement)
	{
		return !replacement.empty();
	}
	void applyTemporaryOppositeRelation(UTime milliseconds);
	void clearTemporaryOppositeRelation();
	void triggerMagicWhenKillEnemy(const Effect& effect);
	void releaseMagicWhenBeAttacked(std::shared_ptr<Magic> magic, int magicDirection, const Effect& effect);
	void triggerMagicWhenBeAttacked(const Effect& effect);
	void triggerMagicWhenDeath();
	int calculateEffectDamage(std::shared_ptr<Effect> effect);
	int applyCriticalDamageFromEffect(const std::shared_ptr<Effect>& effect, int damage, bool* wasCritical = nullptr);
	void applyEffectManaDamage(std::shared_ptr<Effect> effect);
	void applyEffectRestore(std::shared_ptr<Effect> effect, int damageAmount);
	void applySideEffectDamage(int effectType, int amount);
	void recordMagicHitForChange(const Magic& magic, int level);
	bool shouldUseChangeMagic(const Magic& magic, int level) const;
	void consumeChangeMagicHitCount(const Magic& magic);
	void clearChangeMagicHitCounts();

	virtual void addLife(int value);
	// Applies an exact signed life adjustment, clamps at zero, and never starts death.
	virtual void addLifeWithoutDeath(int value);
	virtual void addThew(int value);
	virtual void addMana(int value);
	virtual void hurtLife(int damage);
	void loadLevel(const std::string& fileName);
	void setPropToLevel(int lvl);

	// ---- Partner equipment ----
	int canEquip = 0;
	std::string headEquip = "";
	std::string neckEquip = "";
	std::string bodyEquip = "";
	std::string backEquip = "";
	std::string handEquip = "";
	std::string wristEquip = "";
	std::string footEquip = "";
	std::string backgroundTextureEquip = "";
	NPCEquipmentAttributes equipmentAttributes;
	int equipmentAddMagicEffectPercent = 0;
	int equipmentAddMagicEffectAmount = 0;
	std::unordered_map<std::string, NPCMagicEffectBonus> equipmentAddMagicEffectByName;
	std::unordered_map<std::string, NPCMagicEffectBonus> equipmentAddMagicEffectByType;
	int equipmentChangeMoveSpeedPercent = 0;
	int addMoveSpeedPercent = 0;
	std::weak_ptr<Effect> rangeSpeedUpEffect;
	float equipmentExtraLifeRestorePercent = 0.0f;
	UTime equipmentLifeRestoreElapsedMilliseconds = 0;
	int attackAdditionalEffect = maeNone;
	PointEx bounceDirection = { 0.0f, 0.0f };
	float bounceVelocity = 0.0f;
	int bounceCollisionHurt = 0;
	int bounceCollisionLauncherKind = lkNeutral;
	std::weak_ptr<GameElement> bounceCollisionLauncher;
	bool lastBounceBlockedByCharacter = false;
	Point lastBounceBlockedCharacterPosition = { 0, 0 };
	Point lastBounceEndPosition = { 0, 0 };
	NPCMagicForcedMoveState magicForcedMove;
	bool lastMagicForcedMoveBlockedByCharacter = false;
	Point lastMagicForcedMoveBlockedCharacterPosition = { 0, 0 };
	Point lastMagicForcedMoveEndPosition = { 0, 0 };
	int lastMagicForcedMoveTouchTargetCount = 0;
	int lastMagicForcedMoveTouchHurtCount = 0;
	std::unordered_map<std::string, int> changeMagicHitCounts;
	struct ChangeMagicHitVisualIcon
	{
		UTime elapsedMilliseconds = 0;
		UTime vanishBeginTime = 0;
		int direction = 0;
	};
	struct ChangeMagicHitVisualState
	{
		_shared_imp flyingImage = nullptr;
		_shared_imp vanishImage = nullptr;
		UTime beginTime = 0;
		int threshold = 0;
		int radius = 0;
		int angleSpeed = 0;
		std::vector<ChangeMagicHitVisualIcon> icons;
	};
	std::unordered_map<std::string, ChangeMagicHitVisualState> changeMagicHitVisuals;
	std::unordered_map<std::string, ChangeMagicHitVisualState> changeMagicHitVanishVisuals;
	UTime disableMoveMilliseconds = 0;
	UTime disableSkillMilliseconds = 0;
	UTime blindMilliseconds = 0;
	UTime weakMilliseconds = 0;
	UTime morphMilliseconds = 0;
	UTime invisibleMilliseconds = 0;
	bool isVisibleWhenAttack = false;
	UTime changeToOppositeMilliseconds = 0;
	int originalRelationBeforeOppositeChange = nrFriendly;
	std::shared_ptr<Magic> weakMagic = nullptr;
	std::shared_ptr<Magic> morphMagic = nullptr;
	std::optional<NPCRes> originalResBeforeMorph;
	std::string temporaryNpcResFile;
	std::weak_ptr<Effect> temporaryFlyIniChangeEffect;
	std::weak_ptr<Effect> hiddenByCarryEffect;
	std::weak_ptr<Effect> transportEffect;
	std::weak_ptr<Effect> summonedByMagicEffect;
	// Runtime provenance remains true after the owning Effect detaches on death.
	// Summoned NPCs are transient and must never become save candidates merely
	// because their weak owner reference expired.
	bool transientSummonedNPC = false;
	std::vector<std::string> temporaryFlyIniReplacements;
	std::string temporaryMagicListReplacement;
	std::vector<std::string> equipmentFlyIniReplacements;
	std::vector<std::string> equipmentFlyIni2Replacements;
	std::vector<NPCMagicToUseWhenAttacked> equipmentMagicToUseWhenAttacked;
	std::unordered_map<std::string, std::deque<std::weak_ptr<NPC>>> summonedNPCsByMagic;

	static int getEquipmentPartIndex(const std::string& part);
	static const char* getEquipmentPartName(int partIndex);
	std::string getEquipmentFileByPartIndex(int partIndex) const;
	bool setEquipmentFileByPartIndex(int partIndex, const std::string& fileName);
	void updateEquipmentAttributes(bool adjustCurrentValues = false);
	void adjustCurrentAttributesAfterMaximumChange(
		int previousLifeMax,
		int previousThewMax,
		int previousManaMax);

	// ---- 交互与视野 ----
	int dialogRadius = 1;
	int visionRadius = 18;
	int attackRadius = 3;
	static constexpr UTime MemoryPositionChaseTimeout = 8000;
	static constexpr int AttackDistanceTolerance = 1;
	int lum = nlNone;
	bool selecting = false;
	bool isVisibleByVariable = true;
	bool scriptHidden = false;
	std::string visibleVariableName = "";
	int visibleVariableValue = 0;
	bool canSee(Point dest) const;
	static bool isTalkDistanceReached(int distance, int dialogRadius, int canInteractDirectly)
	{
		return canInteractDirectly > 0 || distance <= dialogRadius;
	}
	bool canTalkAtDistance(int distance) const { return isTalkDistanceReached(distance, dialogRadius, canInteractDirectly); }
	void stopMovement();
	void stopMovementPreservingOffset();
	void updateVisibleByVariable();
	bool isHiddenByCarryMagic() const;
	void setHiddenByCarryMagic(std::shared_ptr<Effect> effect);
	void clearHiddenByCarryMagic(const Effect* effect = nullptr);
	void addToDataMap();

	// ---- 事件脚本 ----
	std::string scriptFile = "";
	std::string scriptFileRight = "";
	std::string timerScriptFile = "";
	std::string buyIniFile = "";
	std::string buyIniString = "";
	std::string dropIni = "";
	int noDropWhenDie = 0;
	Point keepAttackPosition = { 0, 0 };
	UTime hurtPlayerInterval = 0;
	UTime hurtPlayerIntervalTimer = 0;
	int hurtPlayerLife = 0;
	int hurtPlayerRadius = 1;
	std::string deathScript = "";
	UTime reviveMilliseconds = 0;
	UTime leftMillisecondsToRevive = 0;
	UTime lifeMilliseconds = 0;
	int isBodyIniAdded = 0;
	bool noAddBody = false;
	int canInteractDirectly = 0;
	UTime timerScriptInterval = DEFAULT_NPC_OBJ_TIME_SCRIPT_INTERVAL;
	UTime timerScriptElapsed = 0;
	bool updateReviveCountdown(UTime frameTime);
	void reviveFromDeath();

	// ---- 跟随系统 ----
	std::string followNPC = "";
	bool isPartnerBlockingPlayer = false;
	bool partnerOwnerFollowPriorityActive = false;
	int partnerOwnerFollowBestDistance = INT_MAX;
	UTime partnerOwnerFollowLastProgressTime = 0;
	UTime partnerOwnerFollowRetryTime = 0;
	UTime lastTimeTryingToFollow = 0;
	UTime nextFollowCheckTime = 0;
	UTime lastBattleScanTime = 0;
	bool isFollower();
	bool isFollowAttack(std::shared_ptr<NPC> npc);
	virtual void beginFollowWalk(Point dest);
	virtual void beginFollowRun(Point dest);
	virtual void beginFollowAttack(Point dest);
	virtual void changeFollowWalk(Point dest);
	virtual void changeFollowRun(Point dest);
	virtual void changeFollowAttack(Point dest);

	// ---- 寻路方式 ----
	int pathFinder = pfSingle;
	static int resolvePathTypeForState(int npcKind, int npcPathFinder, bool npcHasFixedPath, bool npcIsEnemy)
	{
		if (npcKind == nkFlyingAnimal)
		{
			return nptPathStraightLine;
		}
		if (npcPathFinder == pfBest || npcKind == nkPartner || npcKind == nkAfraidPlayerAnimal)
		{
			return nptPerfectMaxNpcTry;
		}
		if (npcKind == nkNormal || npcKind == nkEvent)
		{
			return nptPerfectMaxPlayerTry;
		}
		if (npcPathFinder == pfSingle || npcHasFixedPath || npcIsEnemy)
		{
			return nptPathOneStep;
		}
		return nptPerfectMaxNpcTry;
	}
	static bool usePathFinderForPathType(int pathType)
	{
		return pathType != nptPathOneStep;
	}
	static int getPathSearchMaxTryForPathType(int pathType, bool temporaryDisableRestrict = false)
	{
		if (temporaryDisableRestrict && pathType == nptPerfectMaxPlayerTry)
		{
			return -1;
		}
		switch (pathType)
		{
		case nptPathOneStep:
			return 10;
		case nptSimpleMaxNpcTry:
		case nptPerfectMaxNpcTry:
		case nptPathStraightLine:
			return 100;
		case nptPerfectMaxPlayerTry:
			return 500;
		default:
			return 128 * 128;
		}
	}
	bool usePathFinder() const;
	int resolvePathType() const;
	int resolveDestinationPathType() const;
	std::deque<Point> findPathByType(Point dest, int pathType = nptEnd, bool temporaryDisableRestrict = false) const;
	bool canEnterMoveStep(Point step) const;
	bool isDestinationMapPositionBlockedByCharacter() const;

	// ---- 行为状态 ----
	bool frozen = false;
	UTime frozenLastTime = 0;
	bool frozenVisualEffect = true;
	bool immobilized = false;
	UTime immobilizedLastTime = 0;
	bool immobilizedVisualEffect = true;
	bool poisoned = false;
	UTime poisonedLastTime = 0;
	UTime poisonedDamageTimer = 0;
	bool poisonedVisualEffect = true;
	std::weak_ptr<GameElement> poisonedBy;
	std::string poisonedByCharacterName = "";
	bool isAIDisabled = false;
	std::weak_ptr<GameElement> lastCombatTarget;
	UTime lastCombatTargetTime = 0;
	PointEx lastCombatMagicDirection = { 0.0f, 0.0f };
	bool hasLastCombatMagicDirection = false;
	// currentCombatTarget: 当前战斗目标，记录NPC正在交战的对象，跨行动计划持久存在，
	// 用于判断仇恨关系、受击反击等高层战斗逻辑。
	// 与 actionPlan.planTarget 的区别：
	//   - currentCombatTarget 是全局战斗状态，生命周期由战斗开始/结束控制；
	//   - actionPlan.planTarget 是当前行动计划的执行目标，生命周期仅限于单次计划，
	//     用于战术层面的走位与攻击决策，计划重置后即失效。
	std::weak_ptr<GameElement> currentCombatTarget;
	UTime currentCombatTargetTime = 0;
	std::weak_ptr<GameElement> lastKnownCombatTarget;
	UTime lastKnownCombatTargetTime = 0;
	Point lastKnownCombatTargetPosition = { 0, 0 };
	bool hasLastKnownCombatTargetPosition = false;
	bool petrified = false;
	UTime petrifiedLastTime = 0;
	bool petrifiedVisualEffect = true;
	int invincible = 0;
	std::vector<std::weak_ptr<Effect>> shieldEffects;
	std::weak_ptr<Effect> shieldEffect;
	int shieldLife = 0;
	UTime shieldLastTime = 0;
	UTime shieldBeginTime = 0;
	bool hasActiveSelfMagic(int specialKind) const;
	void clearFrozenState();
	void clearImmobilizedState();
	void clearPoisonedState();
	void clearPetrifiedState();
	void clearAbnormalState();
	void rememberPoisonSource(std::shared_ptr<GameElement> source);
	void awardDefeatedNpcExperience(std::shared_ptr<Effect> effect);
	void rewardPoisonKillExperience();
	bool applyPreDamageMagicStatus(const Effect& effect, int effectLevel);
	void applyAdditionalAttackEffect(const Effect& effect, int effectLevel);
	bool useSpecialDeath = false;
	std::string specialDeathAction = "";
	bool attackDone = false;
	bool haveAsyncDest = false;
	Point gotoExDest = { 0, 0 };
	Point attackDest = { 0, 0 };
	std::weak_ptr<GameElement> destGE;
	AttackReleaseMode attackReleaseMode = armCombatTarget;

	// ---- 漫游 ----
	int strollIntent = nsiNone;
	std::string fixedPos = "";
	std::vector<Point> fixedPathTilePositions;
	std::vector<Point> actionPathTilePositions;
	size_t currentFixedPosIndex = 0;
	Point destinationMapPosition = { 0, 0 };

	// ---- 魔法 ----
	std::shared_ptr<Magic> npcMagic = nullptr;
	std::shared_ptr<Magic> npcMagic2 = nullptr;
	std::string magicToUseWhenLifeLowFile = "";
	std::shared_ptr<Magic> magicToUseWhenLifeLow = nullptr;
	int lifeLowPercent = 20;
	int keepRadiusWhenLifeLow = 0;
	int keepRadiusWhenFriendDeath = 0;
	std::string magicToUseWhenBeAttackedFile = "";
	int magicDirectionWhenBeAttacked = 0;
	std::shared_ptr<Magic> magicToUseWhenBeAttacked = nullptr;
	std::string magicToUseWhenDeathFile = "";
	int magicDirectionWhenDeath = 0;
	std::shared_ptr<Magic> magicToUseWhenDeath = nullptr;
	bool magicUsed = true;
	std::vector<NPCAttackOption> attackOptions;
	std::shared_ptr<Magic> preparedAttackMagic = nullptr;
	bool hasPreparedAttackMagic = false;
	bool preparedAttackUsesAdditionalEffect = false;
	std::shared_ptr<Magic> preparedMagicAction = nullptr;
	Point preparedMagicActionDest = { 0, 0 };
	int preparedMagicActionLevel = 1;
	int preparedMagicActionListIndex = -1;
	std::weak_ptr<GameElement> preparedMagicActionTarget;
	std::weak_ptr<GameElement> keepDistanceCharacterWhenFriendDeath;

	// ---- 攻击策略 ----
	NPCActionPlan actionPlan;
	NPCAttackOption lastUsedAttackOption;
	bool hasLastUsedAttackOption = false;
	UTime nextSelfBuffTime = 0;
	static constexpr UTime SelfBuffMinInterval = 5000;
	static constexpr UTime SelfBuffMaxInterval = 10000;
	int getClampedAttackLevel() const { return attackLevel < 1 ? 1 : (attackLevel > MAGIC_MAX_LEVEL ? MAGIC_MAX_LEVEL : attackLevel); }
	int getAttackOptionShapeRange(int moveKind, int region, int level) const;
	int estimatePhysicalReach(const Magic& magic, int level) const;
	int calcEffectiveUseDistance(const NPCAttackOption& option) const;
	int getMaxAttackOptionDistance() const;
	std::vector<AttackCandidateInfo> buildAttackCandidates(Point targetPosition) const;
	bool getFallbackApproachInfo(Point targetPosition, Point& outPosition, int& outDistance) const;
	void rememberCombatTargetPosition(std::shared_ptr<GameElement> target);
	void rememberCombatTargetInvestigationPosition(std::shared_ptr<GameElement> target, Point investigationPosition);
	void rememberDamageSource(std::shared_ptr<Effect> effect);
	void clearCombatTargetMemory();
	bool hasActiveCombatWork();
	static constexpr int PartnerAbandonCombatTileDistance = 10;
	static constexpr UTime PartnerOwnerFollowStallTimeoutMilliseconds = 3000;
	static constexpr UTime PartnerOwnerFollowRetryCooldownMilliseconds = 2000;
	static bool shouldAbandonPartnerCombat(int npcKind, bool partnerCombatEnabled,
		bool canFollowPlayer, int playerDistance)
	{
		return npcKind == nkPartner && partnerCombatEnabled && canFollowPlayer
			&& playerDistance > PartnerAbandonCombatTileDistance;
	}
	static bool shouldKeepPartnerOwnerFollowPriority(int npcKind, bool partnerCombatEnabled,
		bool canFollowPlayer, bool ownerFollowPriorityActive, int playerDistance,
		int partnerFollowRadius)
	{
		if (npcKind != nkPartner || !partnerCombatEnabled || !canFollowPlayer)
		{
			return false;
		}
		return ownerFollowPriorityActive
			? playerDistance > partnerFollowRadius
			: playerDistance > PartnerAbandonCombatTileDistance;
	}
	static bool hasPartnerOwnerFollowStalled(UTime currentTime, UTime lastProgressTime)
	{
		return currentTime >= lastProgressTime
			&& currentTime - lastProgressTime >= PartnerOwnerFollowStallTimeoutMilliseconds;
	}
	static bool canRetryPartnerOwnerFollow(UTime currentTime, UTime retryTime)
	{
		return retryTime == 0 || currentTime >= retryTime;
	}
	bool abandonPartnerCombatForPlayerFollow();
	static bool shouldPrioritizeCombatMovement(int npcKind, bool partnerCombatEnabled, bool hasCombatWork)
	{
		return npcKind == nkPartner && partnerCombatEnabled && hasCombatWork;
	}
	bool isWithinCombatChaseLimit(Point targetPosition, Point npcPosition) const;
	bool isCurrentPathWithinCombatChaseLimit(Point targetPosition) const;
	bool isRememberedCombatTarget(std::shared_ptr<GameElement> target) const;
	bool canChaseCombatTargetFromMemory(std::shared_ptr<GameElement> target) const;
	std::optional<Point> getCombatTargetMemoryPosition(std::shared_ptr<GameElement> target) const;
	bool canReleaseCombatAttack(std::shared_ptr<GameElement> target, AttackReleaseMode releaseMode) const;
	void prepareImmediateAttackPlan(std::shared_ptr<GameElement> target, const NPCAttackOption& option, Point targetPosition);
	bool isLifeLowForAI() const;
	bool tryKeepDistanceWhenLifeLow(std::shared_ptr<GameElement> target);
	bool tryKeepDistanceWhenFriendDeath();
	bool tryUseMagicWhenLifeLow();
	void parseMagicList(const std::string& listString, int sourceIndexStart = 2);
	void addAttackOption(const std::string& magicFileName, int distance, int sourceIndex, bool useAdditionalEffect = false);
	void rebuildAttackOptions();
	bool canMagicHitTarget(const NPCAttackOption& option, Point casterPosition, Point targetPosition, int level) const;
	bool isTargetValid(std::shared_ptr<GameElement> target) const;
	bool isCombatTargetValid(std::shared_ptr<GameElement> target) const;
	bool canAnyAttackOptionHitTarget(Point targetPosition) const;
	std::optional<NPCAttackOption> findReadyAttackOption(Point targetPosition) const;
	bool shouldUseSelfBuff(const NPCAttackOption& option) const;
	bool trySelfBuff();
	std::shared_ptr<Magic> selectAttackMagicForAction(Point dest, std::shared_ptr<GameElement> target, AttackReleaseMode releaseMode);
	bool releaseAttackMagic(std::shared_ptr<Magic> selectedMagic, Point dest, std::shared_ptr<GameElement> target, bool applyAdditionalEffect);
	bool isCrossHit(Point casterPosition, Point targetPosition, int crossRange) const;
	bool isTooCloseForAttackOption(const NPCAttackOption& option, Point casterPosition, Point targetPosition) const;
	int getMinEngageDistance() const;
	bool skillRequiresLineOfSight(const NPCAttackOption& option) const;
	std::optional<Point> findBestAttackPosition(const NPCAttackOption& option, Point casterPosition, Point targetPosition, int level) const;
	bool evaluateAndPlan(std::shared_ptr<GameElement> target);
	bool executeActionPlan(std::shared_ptr<GameElement> target);

	// ---- 动作状态查询 ----
	bool isSitting();
	bool isStanding();
	bool isAttacking();
	bool isMagicing();
	bool isRunning();
	bool isWalking();
	bool isJumping();
	bool isDying();
	bool isHiding();
	bool isHurting();
	bool isBouncing() const;
	bool isDoingSpecialAction();
	bool canDoAction(NPCActionRes * act);
	bool canDoAction(NPCActionType act);

	// ---- 动作时间 ----
	UTime getActionTime(int act);
	UTime getActionTime(NPCActionType act) { return getActionTime(static_cast<int>(act)); }

	// ---- 外观资源 ----
	std::string bodyIni = "";
	std::string flyIni = "";
	std::string flyIni2 = "";
	std::string flyInis = "";
	std::string magicIni = "";
	NPCRes res;

	// ---- 方向计算 ----
	static int getImageDirectionCount(const _shared_imp& imagePackage)
	{
		if (imagePackage == nullptr)
		{
			return -1;
		}
		return imagePackage->directions > 0 ? imagePackage->directions : 1;
	}
	static int getActionDirectionCount(const NPCActionRes& action)
	{
		return getImageDirectionCount(action.imagePackage);
	}
	static int reduceMinimumDirectionCount(int currentDirectionCount, int candidateDirectionCount)
	{
		if (candidateDirectionCount <= 0)
		{
			return currentDirectionCount;
		}
		if (currentDirectionCount == -1 || currentDirectionCount > candidateDirectionCount)
		{
			return candidateDirectionCount;
		}
		return currentDirectionCount;
	}
	static int getMinimumActionDirectionCount(std::initializer_list<const NPCActionRes*> actions)
	{
		int directionCount = -1;
		for (auto action : actions)
		{
			if (action == nullptr)
			{
				continue;
			}
			directionCount = reduceMinimumDirectionCount(directionCount, getActionDirectionCount(*action));
		}
		return directionCount == -1 ? 0 : directionCount;
	}
	static bool canMoveInDirection(int direction, int directionCount)
	{
		direction = GameElement::normalizeDir(direction);
		switch (directionCount)
		{
		case 1:
			return direction == 0;
		case 2:
			return direction == 0 || direction == 4;
		case 4:
			return direction == 0 || direction == 2 || direction == 4 || direction == 6;
		default:
			return direction >= 0 && direction < directionCount;
		}
	}
	int getMoveDirectionCount() const
	{
		return getMinimumActionDirectionCount({ &res.walk, &res.awalk, &res.run, &res.arun });
	}
	int getAttackDirectionCount() const
	{
		return getMinimumActionDirectionCount({ &res.attack, &res.attack1, &res.attack2 });
	}
	int getUseMagicDirectionCount() const
	{
		return getMinimumActionDirectionCount({ &res.magic });
	}
	int getJumpDirectionCount() const
	{
		return getMinimumActionDirectionCount({ &res.jump, &res.ajump });
	}
	static int selectAttackActionDirectionCount(int attackDirectionCount, int useActionDirectionCount)
	{
		return useActionDirectionCount > 0 ? useActionDirectionCount : attackDirectionCount;
	}
	static int selectMagicActionDirectionCount(int magicDirectionCount, int useActionDirectionCount, int actionDirectionCount)
	{
		if (useActionDirectionCount > 0)
		{
			return useActionDirectionCount;
		}
		if (actionDirectionCount > 0)
		{
			return actionDirectionCount;
		}
		return magicDirectionCount;
	}
	int getAttackActionDirectionCount(std::shared_ptr<Magic> magic) const
	{
		int useActionDirectionCount = magic != nullptr ? getImageDirectionCount(magic->useActionImage) : -1;
		return selectAttackActionDirectionCount(getAttackDirectionCount(), useActionDirectionCount);
	}
	int getMagicActionDirectionCount(std::shared_ptr<Magic> magic) const
	{
		int useActionDirectionCount = magic != nullptr ? getImageDirectionCount(magic->useActionImage) : -1;
		int actionDirectionCount = magic != nullptr ? getImageDirectionCount(magic->getActionImageForNPC(this)) : -1;
		return selectMagicActionDirectionCount(getUseMagicDirectionCount(), useActionDirectionCount, actionDirectionCount);
	}
	bool canActToward(Point dest, int directionCount) const
	{
		return canMoveInDirection(getDirection(position, dest), directionCount);
	}
	int getInvertDirection(int dir);
	int getDirection(Point dest);
	static int getDirection(float angle);
	static int getDirection(Point from, Point to);
	void limitDir(int * d);
	void limitDir();

	// ---- 脚本调用接口（供外部脚本触发动作） ----
	virtual unsigned int eventRun();
	virtual void jumpTo(Point dest);
	virtual void runTo(Point dest);
	virtual void goTo(Point dest);
	virtual void goToEx(Point dest);
	virtual void goToDir(int dir, int distance);
	virtual void attackTo(Point dest, std::shared_ptr<GameElement> target = nullptr);
	virtual bool startScriptSpecialAction(const std::string& fileName);
	virtual void doSpecialAction(const std::string & fileName);
	virtual void setLevel(int lvl);
	void setFixedPos(const std::string& value);
	bool hasFixedPath() const;
	bool beginFixedPathWalk();
	void setDestinationMapPosition(Point destination);
	void clearDestinationMapPosition();
	bool hasDestinationMapPosition() const;
	bool tryMoveToDestinationMapPosition();
	bool isAIEnabled() const;
	void setAIDisabled(bool disabled);
	void clearActionPathTilePositions();
	int getActionPathTilePositionCount() const { return static_cast<int>(actionPathTilePositions.size()); }
	bool ensureActionPathTilePositions(bool isFlyer, int count = 8, int maxOffset = -1);
	bool beginRandWalkFromActionPath(bool isFlyer, int count = 8, int maxOffset = -1, bool cachePath = true);

	// ---- 动作切换（begin 系列：重置动作时间，进入对应 action） ----
	virtual void clearStep();
	void cancelMoveResumeState();
	virtual void beginStand();
	virtual void beginWalk(Point dest);
	virtual void beginRun(Point dest);
	virtual void beginJump(Point dest);
	virtual void beginAttack(Point dest, std::shared_ptr<GameElement> target = nullptr);
	virtual void beginMagic(Point dest, std::shared_ptr<GameElement> target = nullptr);
	virtual void beginSit();
	virtual void beginHurt(Point dest);
	virtual void beginHurt();
	virtual void handleDeath();
	virtual void beginDieScript();
	virtual void beginDie();
	virtual void beginSpecial();
	virtual std::shared_ptr<Magic> resolveMagicReplacement(std::shared_ptr<Magic> magic);

	// ---- 半径移动（朝目标走/跑到指定半径范围内） ----
	// begin 系列：重置动作时间
	virtual RadiusMoveResult beginRadiusStep(Point dest, int radius, bool findNearDir = true);
	virtual RadiusMoveResult beginRadiusMove(Point dest, int radius, bool isRun = false);
	virtual RadiusMoveResult beginRadiusWalk(Point dest, int radius);
	virtual RadiusMoveResult beginRadiusRun(Point dest, int radius);
	// change 系列：保持动作时间，只更新路径
	virtual bool changeRadiusStep(Point dest, int radius, bool findNearDir = true);
	virtual RadiusMoveResult changeRadiusMove(Point dest, int radius, bool isRun = false, bool dontStandWhenFailed = true);

	// ---- 后退 ----
	virtual bool beginRetreatStep(Point from, std::shared_ptr<GameElement> target = nullptr, int retreatDistance = 0);
	virtual bool beginRetreatWalk(Point from, std::shared_ptr<GameElement> target = nullptr, int retreatDistance = 0);

private:
	bool executeRetreatMovement(Point retreatDest);
	bool tryAttackOrStand(Point from, std::shared_ptr<GameElement> target);
	std::optional<Point> findRetreatDestination(Point from, int retreatDistance) const;
	void finishScriptSpecialActionOverlay();
	void pauseScriptSpecialActionUnderlyingAction(UTime frameTime);
	void updateScriptSpecialActionOverlay(UTime frameTime);

public:

	// ---- 战斗 ----
	virtual void hurt(std::shared_ptr<Effect> e);
	virtual void directHurt(std::shared_ptr<Effect> e);
	void addBody(UTime millisecondsToRemove = 0);
	virtual bool doAttack(Point dest, std::shared_ptr<GameElement> target, AttackReleaseMode releaseMode = armCombatTarget);
	virtual void useMagic(std::shared_ptr<Magic> m, Point dest, int level, std::shared_ptr<GameElement> target);
	std::shared_ptr<Magic> prepareAttackMagicForAction(Point dest, std::shared_ptr<GameElement> target, AttackReleaseMode releaseMode);
	bool releasePreparedAttackMagic(Point dest, std::shared_ptr<GameElement> target);
	void setPreparedAttackMagic(std::shared_ptr<Magic> magic, bool useAdditionalEffect);
	void clearPreparedAttackMagic();
	void setPreparedMagicAction(std::shared_ptr<Magic> magic, Point dest, int level, std::shared_ptr<GameElement> target, int listIndex = -1);
	void clearPreparedMagicAction();
	bool canUseMagicByState(std::shared_ptr<Magic> magic, bool showMessage);
	int summonedNpcsCount(const Magic& magic);
	void addSummonedNpc(const Magic& magic, std::shared_ptr<NPC> npc);
	void removeFirstSummonedNpc(const Magic& magic);

	// ---- 位置与偏移 ----
	unsigned int getJumpState() const { return jumpState; }
	unsigned int getStepState() const { return stepState; }
	unsigned int getSitState() const { return sitState; }
	std::vector<Point> getStepPositions() const;
	Point getPosition() const { return position; }
	void setPosition(Point newPos, bool forceToStand = true);
	PointEx getOffset() const { return offset; }
	PointEx getDrawOffset() const;
	void setOffset(PointEx newOffset);
	void removeFromDataMap();

	// ---- 音效 ----
	virtual void playSound(NPCActionType act);

	// ---- 绘制 ----
	_shared_image getActionImage(int * offsetx, int * offsety);
	_shared_image getActionShadow(int * offsetx, int * offsety);
	virtual void draw(Point cenTile, Point cenScreen, PointEx coffset, uint32_t colorStyle);
	void drawNPCAlpha(Point cenTile, Point cenScreen, PointEx coffset);
	void resetSignalImage();

	// ---- 资源加载/保存 ----
	virtual void initFromIni(INIReader * ini, const std::string & section);
	virtual void saveToIni(INIReader * ini, const std::string & section);
	virtual void loadActionRes(NPCActionRes * npcAction);
	virtual void reloadAction();
	virtual void initActionFromIni(NPCActionRes * npcAction, INIReader * iniReader, const std::string & section);
	virtual void loadSpecialAction(const std::string & fileName);
	virtual void initRes(const std::string & fileName);	
	bool loadNpcResFromIni(const std::string& fileName, NPCRes& targetRes);
	virtual void loadActionFile(const std::string & fileName, int act);
	void freeResource();

protected:
	unsigned int jumpState = 0;
	unsigned int stepState = 0;
	unsigned int sitState = 0;
	_shared_imp signalImagePackage = nullptr;
	int loadedSignalIndex = 0;
	bool signalImageLoadAttempted = false;

	virtual bool mouseInRect(int x, int y);
	_shared_image getSignalImage();
	void drawSignalTip(Point screenPosition, PointEx drawOffset, int actionOffsetY);
	void drawChangeMagicHitVisuals(Point screenPosition, PointEx drawOffset);
	void showCriticalDamageTip(int damage);
	void drawCriticalDamageTip(Point screenPosition, PointEx drawOffset, int actionOffsetY);

	void freeNPCRes();
	void freeNPCRes(NPCRes& npcRes);
	void freeNPCAction(NPCActionRes * act);
	virtual void freeActionImage(NPCActionRes * act);
	bool updateScriptSpecialActionOverlayForFrame(UTime frameTime);
	void updateEventRunState();

	// ---- 每帧更新 ----
	virtual void onUpdate();
	virtual void updateAction(UTime frameTime);
	virtual void onEvent();

	// ---- 鼠标事件 ----
	virtual void onMouseLeftDown(int x, int y);
	virtual void onMouseLeftUp(int x, int y);
	virtual void onMouseMoveIn(int x, int y);
	virtual void onMouseMoveOut();
};
