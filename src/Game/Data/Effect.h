#pragma once
#include "GameElement.h"
#include "Magic.h"
#include <deque>
#include <unordered_map>
#include <vector>

inline constexpr int MaximumPersistedEffectCollectionCount = 4096;

class NPC;
class EffectTestAccess;
class RageSystemTestAccess;

class EffectCasterReference
{
public:
	EffectCasterReference() = default;
	EffectCasterReference(const std::shared_ptr<GameElement>& element) : value(element) {}
	EffectCasterReference(const EffectCasterReference&) = default;
	EffectCasterReference(EffectCasterReference&&) noexcept = default;
	EffectCasterReference& operator=(const EffectCasterReference&) = default;
	EffectCasterReference& operator=(EffectCasterReference&&) noexcept = default;

	EffectCasterReference& operator=(const std::shared_ptr<GameElement>& element)
	{
		value = element;
		return *this;
	}

	std::shared_ptr<GameElement> lock() const { return value; }
	bool expired() const { return value == nullptr; }
	void reset() { value.reset(); }

private:
	std::shared_ptr<GameElement> value;
};

enum class EffectElementReferenceRole
{
	ActiveOnly,
	Caster,
};

class EffectReferenceSaveContext
{
public:
	int registerDetachedCaster(const std::shared_ptr<NPC>& caster);
	const std::vector<std::shared_ptr<NPC>>& getDetachedCasters() const { return detachedCasters; }

private:
	std::unordered_map<const GameElement*, int> detachedCasterIndices;
	std::vector<std::shared_ptr<NPC>> detachedCasters;
};

class EffectReferenceLoadContext
{
public:
	void resizeDetachedCasters(size_t count) { detachedCasters.resize(count); }
	void setDetachedCaster(size_t index, const std::shared_ptr<NPC>& caster);
	std::shared_ptr<NPC> getDetachedCaster(int index) const;

private:
	std::vector<std::shared_ptr<NPC>> detachedCasters;
};

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
	friend class EffectTestAccess;
	friend class RageSystemTestAccess;
public:
	Effect();
	virtual ~Effect();

	UTime runTime = 0;
	UTime runLastTime = 0;
	virtual void eventRun();

	Magic magic;
	EffectCasterReference user;
	std::weak_ptr<GameElement> target;
	std::string fileName = "";
	int doing = ekExploding;
	int level = 0;
	int launcherKind = lkSelf;
	int additionalEffect = maeNone;
	UTime lifeTime = 0;
	UTime beginTime = 0;
	UTime waitTime = 0;
	int damage = 0;
	int damage2 = 0;
	int damage3 = 0;
	int damageMana = 0;
	int evade = 0;
	Point src = { 0, 0 };
	PointEx srcOffset = { 0, 0 };
	Point dest = { 0, 0 };
	PointEx destOffset = { 0, 0 };
	float width = 0.5;
	UTime rangeElapsedMilliseconds = 0;
	UTime flyMagicElapsedMilliseconds = 0;
	int leapTimesRemaining = 0;
	bool leapFlying = false;
	std::vector<std::weak_ptr<NPC>> leapHitTargets;
	std::vector<std::weak_ptr<NPC>> passThroughHitTargets;
	Point magicWhenNewPositionLastTile = { 0, 0 };
	bool magicWhenNewPositionInitialized = false;
	bool explodeMagicTriggered = false;
	bool moveImitateUserPositionInitialized = false;
	Point moveImitateUserLastPosition = { 0, 0 };
	PointEx moveImitateUserLastOffset = { 0, 0 };
	bool moveBackActive = false;
	bool meteorMoveActive = false;
	bool meteorArrivalPending = false;
	std::weak_ptr<NPC> carriedUser;
	bool carryUserActive = false;
	std::weak_ptr<NPC> parasiticTarget;
	UTime parasiticElapsedMilliseconds = 0;
	int parasiticTotalEffect = 0;
	bool circleMoveBaseDirectionInitialized = false;
	Point circleMoveBaseDirection = { 0, 0 };
	bool roundMoveActive = false;
	float roundMoveDegree = 0.0f;
	std::weak_ptr<NPC> summonedNPC;
	bool vibrationTriggered = false;
	bool transportFinished = false;
	bool controlFinished = false;
	std::shared_ptr<MagicDispatchContext> magicDispatchContext = nullptr;

	std::deque<Point> passPath;

	void beginExplode(Point pos);
	void beginFly();
	void beginDrop();
	void initFromMagic(
		std::shared_ptr<Magic> m,
		std::shared_ptr<MagicDispatchContext> dispatchContext = nullptr);
	virtual void initFromIni(INIReader * ini, const std::string & section);
	void initFromIni(
		INIReader* ini,
		const std::string& section,
		const EffectReferenceLoadContext* referenceContext);
	virtual void saveToIni(INIReader * ini, const std::string & section);
	void saveToIni(
		INIReader* ini,
		const std::string& section,
		EffectReferenceSaveContext* referenceContext);
	void restoreRuntimeBindingsAfterLoad();
	void releaseRuntimeBindings();
	static std::shared_ptr<GameElement> loadElementReference(
		const INIReader& ini,
		const std::string& section,
		const std::string& prefix,
		EffectElementReferenceRole role = EffectElementReferenceRole::ActiveOnly,
		const EffectReferenceLoadContext* referenceContext = nullptr);
	static void saveElementReference(
		INIReader& ini,
		const std::string& section,
		const std::string& prefix,
		const std::shared_ptr<GameElement>& element,
		EffectElementReferenceRole role = EffectElementReferenceRole::ActiveOnly,
		EffectReferenceSaveContext* referenceContext = nullptr);
	virtual void playSound(int act);
	
	//技能方向为16个
	int getDirection(Point fDir);
	int getDirection();

	void draw(Point cenTile, Point cenScreen, PointEx coffset, uint32_t colorStyle);
	_shared_image loadImage(int * x, int * y);
	UTime getFlyinUTime();
	UTime getFlyingImageTime();
	UTime getExplodinUTime();
	UTime getSuperImageTime();
	void calTime();
	void calDest();
	auto getPassPath(Point from, PointEx fromOffset, Point to, PointEx toOffset);
	void changeFollowTarget(std::shared_ptr<GameElement> newTarget);
	void attachCarryUser(std::shared_ptr<NPC> npc);
	void clearCarryUser();
	bool beginSummon(Point summonPosition);
	void clearSummonedNPC(bool killNPC);
	void detachSummonedNPCAfterDeath(std::shared_ptr<NPC> npc);
	bool isRangeSpeedUpActive() const;
	bool canBall() const;
	bool canSticky() const;
	bool isSolidObstacle() const;
	bool isEnteringWithMeteor() const { return meteorMoveActive; }
	bool consumeMeteorArrivalPending();
	void finishMeteorArrivalWithoutCollision();
	void initRoundMove(int index);
	bool skipsCharacterCollision() const;
	bool hasAttachedNPC(std::shared_ptr<NPC> npc) const;
	bool handleBallAfterHit(std::shared_ptr<NPC> hitTarget, Point hitPosition);
	bool handleBallWallCollision(Point hitPosition, Point fallbackPosition);
	bool handleStickyAfterHit(std::shared_ptr<NPC> hitTarget);
	bool handleCarryUser4AfterHit(std::shared_ptr<NPC> hitTarget);
	void handleCarryUser4NeighborCollisions();
	bool canBeDiscardedByOppositeMagic() const;
	bool canExchangeUserByOppositeMagic() const;
	bool isOppositeEffect(std::shared_ptr<Effect> other) const;
	bool handleDiscardOppositeMagic(std::shared_ptr<Effect> other);
	bool handleExchangeUserWithOppositeMagic(std::shared_ptr<Effect> other);
	bool canLeap() const;
	bool hasLeapHitTarget(std::shared_ptr<NPC> npc) const;
	bool handleLeapAfterHit(std::shared_ptr<NPC> hitTarget);
	bool canPassThrough() const;
	bool canPassThroughWall() const;
	bool hasPassThroughHitTarget(std::shared_ptr<NPC> npc) const;
	bool handlePassThroughAfterHit(std::shared_ptr<NPC> hitTarget, Point hitPosition);
	bool canParasitic() const;
	bool beginParasitic(std::shared_ptr<NPC> hitTarget, Point hitPosition);
	void recordParasiticDamage(int amount);
	unsigned char getLum();
	bool noLum = false;
	bool vanishing = false;
	bool isTimeStopper() const;
	int getMoveKind() const;
	int getAttachedNPCCount() const;
	int getMovedAttachedNPCCount() const;
	bool hasStickyTarget() const;
	bool hasAnyAttachedNPC() const;
	bool hasMovedAttachedNPC() const;
private:
	struct AttachedNPC
	{
		std::weak_ptr<NPC> npc;
		Point tileOffset = { 0, 0 };
		PointEx offsetDelta = { 0, 0 };
		Point initialPosition = { 0, 0 };
		PointEx initialOffset = { 0, 0 };
		bool hasMoved = false;
		bool preserveOffset = false;
		bool destroyOnObstacle = false;
	};

	_channel channel = nullptr;
	std::weak_ptr<NPC> stickyTarget;
	std::vector<AttachedNPC> attachedNPCs;
	bool selfShieldBoundAfterLoad = false;
	int selfShieldLifeAfterLoad = 0;
	UTime selfShieldRemainingMillisecondsAfterLoad = 0;
	void updateSound();
	void initParam();
	static int calculateThrowHeightOffset(
		double traveledDistance,
		double totalDistance,
		double moveSpeed);

	void freeResource();
	virtual void onUpdate();
	void attachNPCToEffect(std::shared_ptr<NPC> npc, bool preserveOffset, bool destroyOnObstacle);
	void clearAttachedNPCs();
	void updateAttachedNPCs();
	void addDestroyVisualEffect(Point hitPosition);
	void reflectBallFromPoint(Point hitPosition, PointEx normalPoint);
	void reflectBallFromWall(Point hitPosition);
	Point findBallFallbackPosition(Point preferredPosition) const;
	bool updateFollowDirectionToPoint(Point targetPosition, PointEx targetOffset, float stopDistance);
	bool updateFollowDirectionToTarget(std::shared_ptr<NPC> targetNPC);
	bool updateFollowMouseDirection();
	void updateMoveImitateUserPosition();
	bool beginMoveBack();
	void updateMoveBackDirection();
	void updateRandomMoveDirection();
	void beginMeteorMove();
	bool updateMeteorMove(UTime frameTime);
	void updateRoundMovePosition(UTime frameTime);
	void updateSummonEffect();
	void updateCarryUserPosition();
	void updateParasiticEffect(UTime frameTime);
	void finishParasiticEffect();
	void finishTransportEffect();
	void clearTransportEffectState();
	void finishControlEffect();
	void clearControlEffectState();
	bool shouldExplodeWhenLifeFrameEnds() const;
	Point getCircleMoveDirection(Point baseDirection);
	void updateRangeEffect(UTime frameTime);
	void updateFlyMagic(UTime frameTime);
	void updateMagicWhenNewPosition();
	void triggerExplodeMagic(Point position);
	std::shared_ptr<NPC> findNextLeapTarget(Point fromPosition) const;
	void addPassThroughDestroyEffect(Point hitPosition);

	PointEx getCollideOffset(Point pos);
	struct MeteorPathNode
	{
		Point position = { 0, 0 };
		PointEx offset = { 0.0f, 0.0f };
	};
	std::deque<MeteorPathNode> meteorPath;
};
