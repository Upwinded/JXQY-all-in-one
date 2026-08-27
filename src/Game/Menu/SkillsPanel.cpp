#include "SkillsPanel.h"
#include "../../Engine/Engine.h"
#include "../GameManager/GameManager.h"
#include "MinimapToggleButton.h"

SkillsPanel::SkillsPanel()
{
	name = "SkillsPanel";
	init();
	setPriority(epController);
	coverMouse = false;
	canCallBack = true;
}

SkillsPanel::~SkillsPanel()
{
	freeResource();
}

void SkillsPanel::resetInput()
{
	cancelPointerInteraction();
	clickIndex = SKILL_PANEL_NONE;
	dragEndPosition = { 0, 0 };
	_jumpBtnDagging = false;
	dragBeginTime = 0;
}

void SkillsPanel::initFromIni(INIReader& ini)
{
	ConfigDrivenPanel::initFromIni(ini);
	indicateImp = loadRes(ini.Get("init", "indicate", ""));
}

void SkillsPanel::onChildCallBack(PElement child)
{
	if (child == nullptr) { return; }
	result = child->getResult();
	if ((result & erClick) || (result & erMouseLDown) || (result & erDragEnd))
	{
		if (child == attackBtn)
		{
			clickIndex = SKILL_PANEL_ATTACK;
		}
		else if (child == sitBtn)
		{
			clickIndex = SKILL_PANEL_SIT;
		}
		else if (child == skillBtn[0])
		{
			clickIndex = SKILL_PANEL_SKILL1;
		}
		else if (child == skillBtn[1])
		{
			clickIndex = SKILL_PANEL_SKILL2;
		}
		else if (child == skillBtn[2])
		{
			clickIndex = SKILL_PANEL_SKILL3;
		}
		else if (child == skillBtn[3])
		{
			clickIndex = SKILL_PANEL_SKILL4;
		}
		else if (child == skillBtn[4])
		{
			clickIndex = SKILL_PANEL_SKILL5;
		}
		else if (child == rightJumpBtn)
		{
			clickIndex = SKILL_PANEL_JUMP;
		}
		else if (child == minimapButton)
		{
			clickIndex = SKILL_PANEL_MINIMAP;
		}
		else
        {
            for (int i = 0; i < FASTBTN_COUNT; ++i)
            {
                if (child == fastBtn[i])
                {
                    clickIndex = SKILL_PANEL_FAST_SELECT + i;
                }
            }
		}
	}
	if ((result & erDragEnd))
	{
		auto component = std::dynamic_pointer_cast<DragRoundButton>(child);
		if (child == rightJumpBtn)
		{
			dragEndPosition = component->getDragRealPosition();
		}
		else if (child == skillBtn[0] || child == skillBtn[1] || child == skillBtn[2] || child == skillBtn[3] || child == skillBtn[4])
		{
            Point childPos = { child->rect.x, child->rect.y };
			dragEndPosition = component->getDragPosition() - childPos;
		}
	}
	if (parent != nullptr && parent->canCallBack)
	{
		parent->onChildCallBack(getMySharedPtr());
	}
}

void SkillsPanel::onDraw()
{
	ConfigDrivenPanel::onDraw();
	if (rightJumpBtn && rightJumpBtn->isDragging())
	{
		drawJumpIndicate(rightJumpBtn->getDragRealPosition());
	}
	else
	{
		for (size_t i = 0; i < SKILL_PANEL_SKILL_COUNT; i++)
		{
			if (skillBtn[i] && skillBtn[i]->isDragging())
			{
                Point childPos = { skillBtn[i]->rect.x, skillBtn[i]->rect.y };
				drawIndicate(skillBtn[i]->getDragPosition() - childPos);
				break;
			}
		}
	}

}

void SkillsPanel::onUpdate()
{
	if (rightJumpBtn && rightJumpBtn->isDragging())
	{
		if (!_jumpBtnDagging)
		{
			dragBeginTime = getTime();
		}
		_jumpBtnDagging = true;
	}
	else
	{
		_jumpBtnDagging = false;
	}
}

void SkillsPanel::init()
{
	freeResource();
	loadMenuDefinition("ini\\ui\\mobile\\skills\\skills.menu.ini");

	attackBtn = getComponentByName<RoundButton>("attackBtn");
	sitBtn = getComponentByName<RoundButton>("sitBtn");
	for (size_t i = 0; i < SKILL_PANEL_SKILL_COUNT; i++)
	{
		std::string skillName = convert::formatString("skillBtn%d", i + 1);
		skillBtn[i] = getComponentByName<DragRoundButton>(skillName);
	}

	for (int i = 0; i < FASTBTN_COUNT; ++i)
	{
		std::string fastName = convert::formatString("fastBtn%d", i + 1);
		fastBtn[i] = getComponentByName<TextButton>(fastName);
		if (fastBtn[i]) fastBtn[i]->visible = false;
	}
	rightJumpBtn = getComponentByName<DragRoundButton>("rightJumpBtn");
	if (rightJumpBtn) rightJumpBtn->setIndicateImage("mpc\\character\\jump.mpc");

	minimapButton = std::make_shared<MinimapToggleButton>();
	addChild(minimapButton);

	setChildRectReferToParent();
}

void SkillsPanel::freeResource()
{
	attackBtn = nullptr;
	sitBtn = nullptr;
	for (size_t i = 0; i < SKILL_PANEL_SKILL_COUNT; i++)
	{
		skillBtn[i] = nullptr;
	}
	
	for (int i = 0; i < FASTBTN_COUNT; ++i)
	{
		fastBtn[i] = nullptr;
	}
	rightJumpBtn = nullptr;
	minimapButton = nullptr;
	ConfigDrivenPanel::freeResource();
}

void SkillsPanel::drawJumpIndicate(Point pos)
{
	auto indicateJumpImp = gm->player->res.jump.imagePackage;
	if (indicateJumpImp == nullptr)
	{
		return;
	}
	int xOffset = 0, yOffset = 0;
	auto actionTime = IMP::getIMPImageActionTime(indicateJumpImp);
	auto now = (getTime() - dragBeginTime) * 2;
	auto playerPos = gm->player->getPosition();
	actionTime = actionTime > 0 ? actionTime : 1;
	now = now % actionTime;
	auto actionSplitTime = actionTime / 3;
	actionSplitTime = actionSplitTime > 0 ? actionSplitTime : 1;
	auto state = now / actionSplitTime;
	int dir = 0;
	int w = 0;
	int h = 0;
	engine->getWindowSize(w, h);
	Point cenScreen;
	cenScreen.x = (int)w / 2;
	cenScreen.y = (int)h / 2;
	Point realPos = pos;

	Point realTilePos = gm->map->getMousePosition(realPos, gm->player->getPosition(), cenScreen, gm->camera->offset);
	dir = gm->player->getDirection(realTilePos);
	switch (state)
	{
	case 0:
	{
		realPos = gm->map->getTilePosition(gm->player->getPosition(), gm->camera->position, cenScreen, gm->camera->offset);
		break;
	}
	case 1:
	{
		playerPos = gm->map->getTilePosition(gm->player->getPosition(), gm->camera->position, cenScreen, gm->camera->offset);
		float param = ((float)now - actionSplitTime) / actionSplitTime;
		realPos.x = (int)round(param * (realPos.x - playerPos.x) + playerPos.x);
		realPos.y = (int)round(param * (realPos.y - playerPos.y) + playerPos.y);
		break;
	}

	default:
	{
		break;
	}
	}
	auto img = IMP::loadImageForDirection(indicateJumpImp, dir, now, &xOffset, &yOffset);
	engine->setImageAlpha(img, 128);
	engine->drawImage(img, realPos.x - xOffset, realPos.y - yOffset);
	engine->setImageAlpha(img, 255);
}


void SkillsPanel::drawIndicate(Point pos)
{
	if (indicateImp == nullptr)
	{
		return;
	}
	pos.x = (int)round((float)pos.x * MapXRatio);
	auto angle = atan2(-pos.x, pos.y);
	auto img = IMP::loadImageForTime(indicateImp, getTime());
	if (img == nullptr)
	{
		return;
	}
	int w, h;
	engine->getImageSize(img, w, h);
	Point playerPos = gm->player->getScreenPosition(gm->camera->position, gm->camera->offset);
	playerPos.y -= TILE_HEIGHT / 2;
	Rect dest;
	dest.x = playerPos.x - w / 2;
	dest.y = playerPos.y;
	dest.w = w;
	auto dx = sin(angle) * h * MapXRatio;
	auto dy = cos(angle) * h;

	dest.h = (int)round(hypot(dx, dy));
	// The source artwork has its arrowhead at the top edge. Anchor its bottom
	// edge at the player and rotate by half a turn so the arrowhead marks the
	// selected destination instead of pointing back toward the player.
	dest.y = playerPos.y - dest.h;
	Point center;
	center.x = w / 2;
	center.y = dest.h;
	engine->drawImageEx(
		img, nullptr, &dest, angle + 3.14159265358979323846, &center);
}
