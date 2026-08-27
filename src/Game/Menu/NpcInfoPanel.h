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
	virtual void onDraw() override;

	void drawLifeBar(NPC* npc, int screenCenterX, int topY);

	static constexpr int BAR_WIDTH = 140;
	static constexpr int BAR_HEIGHT = 18;
	static constexpr int BAR_TOP_ADJUST = 4;
};
