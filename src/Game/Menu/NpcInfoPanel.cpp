#include "NpcInfoPanel.h"
#include "../../Engine/Engine.h"
#include "../Data/NPC.h"
#include "../GameManager/GameManager.h"
#include "../GameManager/GameController.h"
#include "../../File/INIReader.h"

NpcInfoPanel::NpcInfoPanel()
{
	name = "NpcInfoPanel";
	coverMouse = false;
	needEvents = false;
	std::unique_ptr<char[]> content;
	if (File::readFile("ini\\ui\\npcinfo\\window.ini", content) > 0 && content)
	{
		INIReader ini(content);
		const int width = ini.GetInteger("Init", "Width", lifeBarWidth);
		const int height = ini.GetInteger("Init", "Height", lifeBarHeight);
		if (ini.ParseError() == 0 && width > 0 && height > 0)
		{
			lifeBarWidth = width;
			lifeBarHeight = height;
			lifeBarLeftAdjust = ini.GetInteger("Init", "AlignX", lifeBarLeftAdjust);
			lifeBarTopAdjust = ini.GetInteger("Init", "AlignY", lifeBarTopAdjust);
		}
	}
}

NpcInfoPanel::~NpcInfoPanel()
{
}

void NpcInfoPanel::onDraw()
{
	auto friendlyTarget = gm->controller != nullptr ? gm->controller->getFriendlyMagicTarget() : nullptr;
	NPC* selectedNPC = friendlyTarget.get();
	for (size_t i = 0; selectedNPC == nullptr && i < gm->npcManager->npcList.size(); i++)
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
	if (friendlyTarget == nullptr && !isBattle && !isPartnerCombat)
	{
		return;
	}

	int w, h;
	engine->getWindowSize(w, h);
	int screenCenterX = w / 2;
	int topY = lifeBarTopAdjust;

	drawLifeBar(selectedNPC, screenCenterX, topY);
	if (friendlyTarget != nullptr)
	{
		const auto position = friendlyTarget->getScreenPosition(gm->camera->position, gm->camera->offset);
		engine->fillRect(position.x - 10, position.y - 1, 20, 2, 80, 255, 100, 230);
		engine->fillRect(position.x - 1, position.y - 6, 2, 12, 80, 255, 100, 230);
	}
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

	int topLeftX = screenCenterX - lifeBarWidth / 2 + lifeBarLeftAdjust;

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

	engine->fillRect(topLeftX, topY, lifeBarWidth, lifeBarHeight, 0, 0, 0, 180);

	if (npc->displayLifePercent > lifePercent)
	{
		int lagWidth = (int)(lifeBarWidth * npc->displayLifePercent);
		int currentWidth = (int)(lifeBarWidth * lifePercent);
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
			engine->fillRect(topLeftX + currentWidth, topY, lagPartWidth, lifeBarHeight, lagR, lagG, lagB, 160);
		}
	}

	int lifeWidth = (int)(lifeBarWidth * lifePercent);
	if (lifeWidth > 0)
	{
		engine->fillRect(topLeftX, topY, lifeWidth, lifeBarHeight, lifeR, lifeG, lifeB, 230);
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
		int textY = topY + (lifeBarHeight - textH) / 2;
		engine->drawImage(textImage, textX, textY);
	}
}
