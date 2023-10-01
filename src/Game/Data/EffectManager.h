#pragma once
#include "Effect.h"
#include <vector>
#include "CollisionDetector.h"

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

	void load();
	void save();

	void disableAllEffect();

	EffectMap createMap(int x, int y, int w, int h);

	void freeResource();
	virtual void onUpdate();
};

