#include "NpcInfoPanel.h"
#include "../../Engine/Engine.h"
#include "../Data/NPC.h"
#include "../GameManager/GameManager.h"

NpcInfoPanel::NpcInfoPanel()
{
	name = "NpcInfoPanel";
	coverMouse = false;
	needEvents = false;
}

NpcInfoPanel::~NpcInfoPanel()
{
}

void NpcInfoPanel::onDraw()
{
	NPC* selectedNPC = nullptr;
	for (size_t i = 0; i < gm->npcManager->npcList.size(); i++)
	{
		if (gm->npcManager->npcList[i] != nullptr && gm->npcManager->npcList[i]->selecting)
		{
			selectedNPC = gm->npcManager->npcList[i].get();
			break;
		}
	}
	if (selectedNPC == nullptr && gm->player && gm->player->selecting)
	{
		selectedNPC = gm->player.get();
	}
	if (selectedNPC == nullptr)
	{
		return;
	}

	bool isBattle = (selectedNPC->kind == nkBattle);
	bool isPartnerCombat = (selectedNPC->kind == nkPartner && gm->global.data.PartnerCombat);
	if (!isBattle && !isPartnerCombat)
	{
		return;
	}

	int w, h;
	engine->getWindowSize(w, h);
	int screenCenterX = w / 2;
	int topY = BAR_TOP_ADJUST;

	drawLifeBar(selectedNPC, screenCenterX, topY);
}

void NpcInfoPanel::drawLifeBar(NPC* npc, int screenCenterX, int topY)
{
	int maxLife = npc->getLifeMax();
	if (maxLife <= 0)
	{
		return;
	}

	float lifePercent = (float)npc->life / (float)maxLife;
	if (lifePercent > 1.0f) lifePercent = 1.0f;
	if (lifePercent < 0.0f) lifePercent = 0.0f;

	int topLeftX = screenCenterX - BAR_WIDTH / 2;

	uint8_t lifeR, lifeG, lifeB;
	if (npc->relation == nrHostile)
	{
		lifeR = 163; lifeG = 18; lifeB = 21;
	}
	else if (npc->relation == nrFriendly)
	{
		lifeR = 16; lifeG = 165; lifeB = 28;
	}
	else
	{
		lifeR = 40; lifeG = 30; lifeB = 245;
	}

	engine->fillRect(topLeftX, topY, BAR_WIDTH, BAR_HEIGHT, 0, 0, 0, 180);

	if (npc->displayLifePercent > lifePercent)
	{
		int lagWidth = (int)(BAR_WIDTH * npc->displayLifePercent);
		int currentWidth = (int)(BAR_WIDTH * lifePercent);
		int lagPartWidth = lagWidth - currentWidth;
		if (lagPartWidth > 0)
		{
			uint8_t lagR, lagG, lagB;
			if (npc->relation == nrHostile)
			{
				lagR = 255; lagG = 120; lagB = 140;
			}
			else if (npc->relation == nrFriendly)
			{
				lagR = 140; lagG = 255; lagB = 120;
			}
			else
			{
				lagR = 140; lagG = 140; lagB = 255;
			}
			engine->fillRect(topLeftX + currentWidth, topY, lagPartWidth, BAR_HEIGHT, lagR, lagG, lagB, 160);
		}
	}

	int lifeWidth = (int)(BAR_WIDTH * lifePercent);
	if (lifeWidth > 0)
	{
		engine->fillRect(topLeftX, topY, lifeWidth, BAR_HEIGHT, lifeR, lifeG, lifeB, 230);
	}

	std::string displayName = npc->getDisplayName();
	if (displayName.empty())
	{
		return;
	}

	_shared_image textImage = engine->createText(displayName, 12, 0xD0FFFFFF);
	if (textImage != nullptr)
	{
		int textW = 0, textH = 0;
		engine->getImageSize(textImage, textW, textH);
		int textX = screenCenterX - textW / 2;
		int textY = topY + (BAR_HEIGHT - textH) / 2;
		engine->drawImage(textImage, textX, textY);
	}
}
