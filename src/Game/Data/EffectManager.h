#pragma once
#include "Effect.h"
#include <vector>
#include "CollisionDetector.h"

class MagicDerivedRuntimeTestAccess;

class EffectManager :
	public Element
{
public:
	EffectManager();
	virtual ~EffectManager();

	std::vector<std::shared_ptr<Effect>> effectList;

	void pauseAllEffect();
	void resumeAllEffect();
	void addEffect(std::shared_ptr<Effect> effect);
	void deleteEffect(std::shared_ptr<Effect> effect);
	void clearEffect();
	void addTrailMagic(
		std::shared_ptr<Magic> magic,
		std::shared_ptr<GameElement> user,
		int level,
		int damage,
		int evade,
		int launcher,
		std::shared_ptr<MagicDispatchContext> dispatchContext = nullptr);
	void addDelayedMagic(
		std::shared_ptr<Magic> magic,
		std::shared_ptr<GameElement> user,
		Point from,
		Point to,
		int level,
		int launcher,
		std::shared_ptr<GameElement> target,
		UTime delayMilliseconds,
		std::shared_ptr<MagicDispatchContext> dispatchContext = nullptr);

	bool load();
	bool save();
	void loadFromIni(INIReader& ini);
	void saveToIni(INIReader& ini);
	size_t getPendingTrailMagicCount() const { return trailMagicList.size(); }
	size_t getPendingDelayedMagicCount() const { return delayedMagicList.size(); }
	std::shared_ptr<GameElement> getPendingTrailMagicUser(size_t index) const;
	std::shared_ptr<GameElement> getPendingDelayedMagicUser(size_t index) const;

	void disableAllEffect();
	bool hasSolidEffectAt(Point position) const;
	bool hasActiveTimeStopper();
	bool isActiveTimeStopper(std::shared_ptr<Effect> effect);
	std::shared_ptr<Effect> getActiveTimeStopperEffect();
	std::shared_ptr<GameElement> getActiveTimeStopperUser();

	const EffectMap& createMap(int x, int y, int w, int h);

	void freeResource();
	virtual void onUpdate();

protected:
	virtual bool shouldUpdateChild(PElement child) override;

private:
	friend class MagicDerivedRuntimeTestAccess;

	struct TrailMagicInfo
	{
		EffectCasterReference user;
		std::shared_ptr<Magic> magic;
		Point lastPosition = { 0, 0 };
		UTime remainingTime = 0;
		int level = 1;
		int damage = 0;
		int evade = 0;
		int launcher = lkSelf;
		std::shared_ptr<MagicDispatchContext> dispatchContext = nullptr;
	};

	struct DelayedMagicInfo
	{
		EffectCasterReference user;
		std::weak_ptr<GameElement> target;
		std::shared_ptr<Magic> magic;
		Point from = { 0, 0 };
		Point to = { 0, 0 };
		UTime remainingTime = 0;
		int level = 1;
		int launcher = lkSelf;
		std::shared_ptr<MagicDispatchContext> dispatchContext = nullptr;
	};

	EffectMap cachedEffectMap;
	int cachedWidth = 0;
	int cachedHeight = 0;
	std::weak_ptr<Effect> timeStopperEffect;
	std::vector<TrailMagicInfo> trailMagicList;
	std::vector<DelayedMagicInfo> delayedMagicList;

	bool isTimeStopperCandidate(std::shared_ptr<Effect> effect) const;
	void updateTrailMagic();
	void updateDelayedMagic();
};
