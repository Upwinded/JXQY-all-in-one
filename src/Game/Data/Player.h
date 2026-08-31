#pragma once
#include "NPC.h"
#include "Object.h"
#include <map>

// ===================== 玩家属性与升级结构体 =====================

// 主要记录主角加上装备后的属性
struct PlayerInfo
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
};

struct LevelInfo
{
	int levelUpExp = 0;
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
	std::string newMagic = "";
};

enum NextDest
{
	ndNone = 0, 
	ndTalk = 1,
	ndObj = 2,
	ndAttack = 3,
};

struct NextAction
{
	NPCActionType action = NPCActionType::acStand;
	Point dest = { 0, 0 };
	NextDest destKind = ndNone;
	int actionParam = 0;
	std::weak_ptr<GameElement> destGE;
	int distance = 0;
	bool useRightScript = false;
	bool strictWorldInteraction = false;
};

// ===================== 常量定义 =====================

#define JUMP_THEW_COST 10
#define ATTACK_THEW_COST 10
#define RUN_THEW_COST 2
#define SIT_THEW_COST 5
#define SIT_MANA_ADD_RATE 0.004
#define THEW_RECOVERY_RATE 0.004
#define THEW_RECOVERY_MIN 1
#define THEW_RECOVERY_INTERVAL 40
#define MANA_RECOVERY_INTERVAL 40
#define EQUIPMENT_MANA_RESTORE_INTERVAL 1000
#define EQUIPMENT_MANA_RESTORE_RATE 0.02
#define MAGIC_RESTORE_INTERVAL 1000

#define MIN_THEW_RATE_TO_RUN 0.3
#define MIN_THEW_LIMIT_TO_RUN 50
#define PATH_FIND_FAIL_COOLDOWN 200

// ===================== Player 类 =====================

class Player :
	public NPC
{
	friend class RageSystemTestAccess;
	friend class WorldInteractionRuntimeTestAccess;
public:
	Player();
	virtual ~Player();

	// ---- 装备后属性 ----
	PlayerInfo info;
	void calInfo();
	void resetEquipmentGrantedMagicSync();
	void updateLevel();
	virtual void setLevel(int lvl);
	virtual void fullLife();
	virtual void fullThew();
	virtual void fullMana();
	virtual void addLifeMax(int value);
	virtual void addThewMax(int value);
	virtual void addManaMax(int value);
	virtual void addLife(int value);
	void addLifeWithoutDeath(int value) override;
	virtual void addThew(int value);
	virtual void addMana(int value);
	virtual void addAttack(int value, int type = 1);
	virtual void addDefend(int value, int type = 1);
	virtual void addEvade(int value);
	virtual void addMoney(int value);

	virtual int getEvade() {return applyTemporaryEvadeModifiers(info.evade);}
	virtual int getDefend() {return applyTemporaryDefendModifiers(info.defend);}
	virtual int getDefend2() {return info.defend2;}
	virtual int getDefend3() {return info.defend3;}
	virtual int getAttack() {return applyTemporaryAttackModifiers(info.attack);}
	virtual int getAttack2() {return info.attack2;}
	virtual int getAttack3() {return info.attack3;}
	virtual int getLifeMax() {return info.lifeMax;}
	virtual int getManaMax() {return info.manaMax;}
	virtual int getThewMax() {return info.thewMax;}
	bool ignoresRunThewCost() const { return equipmentIgnoresRunThewCost; }

	// ---- 经验与升级 ----
	virtual void addExp(int aExp);
	bool addExperienceToNextLevel();
	virtual void levelUp();
	std::string levelIni = "";
	std::vector<LevelInfo> levelList;
	void loadLevel(const std::string& fileName);
	void limitAttribute();

	// ---- 动作队列 ----
	std::shared_ptr<NextAction> nextAction = nullptr;
	NextDest nextDest = ndNone;
	bool nextDestUseRightScript = false;
	bool nextDestStrictWorldInteraction = false;
	bool nextDestRequestedRunning = false;
	bool addNextAction(NextAction& act);
	void cancelQueuedInteraction(bool strictOnly = false);
	bool handleQueuedInteractionAtCurrentPosition();

	// ---- 移动 ----
	// begin 系列：首次开始移动（重置动作时间）
	virtual void beginStand();
	void forceBeginStand();
	virtual void beginWalk(Point dest);
	virtual void beginRun(Point dest);
	virtual void beginJump(Point dest);
	// change 系列：移动中切换目标（保持动作时间，通过 action retarget）
	virtual bool changeWalk(Point dest);
	virtual bool changeRun(Point dest);

	// ---- 战斗 ----
	virtual void beginAttack(Point dest, std::shared_ptr<GameElement> target = nullptr);
	virtual void beginMagic(Point dest, std::shared_ptr<GameElement> target = nullptr);
	virtual bool doSpecialAttack(Point dest, std::shared_ptr<GameElement> target = nullptr);
	std::shared_ptr<Magic> prepareSpecialAttackMagicForAction(Point dest, std::shared_ptr<GameElement> target);
	bool releasePreparedSpecialAttackMagic(Point dest, std::shared_ptr<GameElement> target);
	virtual std::shared_ptr<Magic> resolveMagicReplacement(std::shared_ptr<Magic> magic);
	bool tryConsumeMagicCost(std::shared_ptr<Magic> magic, int level, bool showMessage);
	void setRage(int value);
	void addRage(int value);
	float getCriticalChancePercent() const;
	int getCriticalDamagePercent() const;
	int applyCriticalDamage(int damage, int roll, bool* wasCritical = nullptr) const;
	virtual void beginHurt(Point dest);
	virtual void hurt(std::shared_ptr<Effect> e);
	virtual void hurtLife(int damage);
	bool canHurt();
	void suppressTrapAtScriptPosition();
	void checkTrap();

	// ---- 交互 ----
	virtual void triggerObject(std::shared_ptr<Object> obj, bool useRightScript = false);
	virtual void talkTo(std::shared_ptr<NPC> npc, bool useRightScript = false);
	void partnerAvoidBlockingPlayer(Point dest);
	std::shared_ptr<NPC> getControlledCharacter() const;
	std::shared_ptr<NPC> getActionActor() const;
	bool isControllingCharacter() const;
	void beginControlCharacter(std::shared_ptr<NPC> target, std::shared_ptr<Effect> effect);
	void endControlCharacter(const Effect* effect = nullptr);

	// ---- 能力开关 ----
	bool canRun = true;
	bool canJump = true;
	bool canFight = true;
	bool canUseMana = true;
	int walkIsRun = 0;
	bool isRunDisabled() const { return !canRun; }
	bool isJumpDisabled() const { return !canJump; }
	bool isFightDisabled() const { return !canFight; }
	void setRunDisabled(bool disabled) { canRun = !disabled; }
	void setJumpDisabled(bool disabled) { canJump = !disabled; }
	void setFightDisabled(bool disabled) { canFight = !disabled; }

	// ---- 魔法 ----
	int magicIndex = 0;
	Point magicDest = { 0, 0 };
	int magic = 40;
	int rage = 0;
	int rageMax = 100;
	std::weak_ptr<Effect> attributeChangeEffect;

	// ---- 经济 ----
	int money = 0;

	// ---- 恢复 ----
	UTime recoveryTime = 0;
	float recoveryAccumulator = 0.0f;
	UTime equipmentManaRestoreTime = 0;
	UTime magicRestoreTime = 0;
	void resetRecoveryTime(UTime time = 0);
	void recoverWhenStandingOrWalking();
	UTime lastPathFindFailTime = 0;

	// ---- 死亡 ----
	virtual void handleDeath();
	virtual void beginDie();

	// ---- 绘制 ----
	virtual void drawAlpha(
		Point cenTile,
		Point cenScreen,
		PointEx coffset,
		uint32_t colorStyle);
	virtual void draw(Point cenTile, Point cenScreen, PointEx coffset, uint32_t colorStyle);

	// ---- 存档 ----
	virtual bool load(
		int index = -1,
		std::string* failureReason = nullptr);
	bool loadInitialTemplate(
		int index,
		std::string* failureReason = nullptr);
	virtual bool save(int index = -1);
	void freeResource();

private:
	bool loadFromFile(
		const std::string& fileName,
		const std::string& displayName,
		std::string* failureReason);
	bool usesStrictLevelUpThreshold() const;
	void recordActualDamageForRage(int damage);
	void clearControlledNextAction();
	void dispatchControlledAction(const NextAction& act);
	void processControlledNextAction();
	bool resumeStrictQueuedInteraction();
	void controlActorTalkTo(std::shared_ptr<NPC> actor, std::shared_ptr<NPC> npc, bool useRightScript);
	void controlActorTriggerObject(std::shared_ptr<NPC> actor, std::shared_ptr<Object> obj, bool useRightScript);
	void triggerTouchObjects();
	// 移动的统一内部实现，begin 和 change 系列最终都走这里
	bool startMoveInternal(Point dest, bool running, bool isRetarget);
	bool equipmentIgnoresRunThewCost = false;
	bool runThewLowMessageShown = false;
	UTime lastRunThewLowMessageTime = 0;
	bool equipmentRestoresMana = false;
	int equipmentAttackAdditionalEffect = maeNone;
	int magicAddLifeRestorePercent = 0;
	int magicAddThewRestorePercent = 0;
	int magicAddManaRestorePercent = 0;
	std::map<std::string, std::string> equipmentMagicReplacements;
	std::map<std::string, int> equipmentMagicIniWhenUseCounts;
	bool equipmentMagicIniWhenUseCountsInitialized = false;
	std::weak_ptr<NPC> controlledCharacter;
	std::weak_ptr<Effect> controlledMagicEffect;
	std::shared_ptr<NextAction> controlledNextAction = nullptr;
	bool controlCharacterSessionActive = false;
	bool scriptPositionTrapSuppressed = false;
	Point scriptTrapPosition = { 0, 0 };
	std::string scriptTrapMapName;
	void syncEquipmentGrantedMagic(const std::map<std::string, int>& activeCounts);

	// ---- 每帧更新 ----
	virtual void updateAction(UTime frameTime);
	virtual void onUpdate();
};
