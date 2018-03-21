#pragma once
#include "Effect.h"
#include <vector>
#include "CollisionDetector.h"

class EffectManager :
	public Element
{
public:
	EffectManager();
	~EffectManager();

	std::vector<Effect *> effectList;

	void pauseAllEffect();
	void resumeAllEffect();
	void addEffect(Effect * effect);
	void deleteEffect(Effect * effect);
	void removeEffect(Effect * effect);
	void clearEffect();

	void load();
	void save();

	void disableAllEffect();

	EffectMap createMap(int x, int y, int w, int h);

	virtual void freeResource();
	virtual void onUpdate();
};

