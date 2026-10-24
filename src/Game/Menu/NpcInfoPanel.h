#pragma once

#include "../../Element/Element.h"

class NPC;

class NpcInfoPanel :
	public Element
{
public:
	NpcInfoPanel();
	virtual ~NpcInfoPanel();

private:
	friend class GamepadEssentialUITestAccess;

	virtual void onDraw() override;

	void drawLifeBar(NPC* npc, int screenCenterX, int topY);

	int lifeBarWidth = 140;
	int lifeBarHeight = 18;
	int lifeBarLeftAdjust = 0;
	int lifeBarTopAdjust = 50;
};
