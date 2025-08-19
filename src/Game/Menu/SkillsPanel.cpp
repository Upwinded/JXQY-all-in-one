#include "SkillsPanel.h"
#include "../GameManager/GameManager.h"

SkillsPanel::SkillsPanel()
{
	init();
	priority = epMin;
	coverMouse = false;
	canCallBack = true;
}

SkillsPanel::~SkillsPanel()
{
	freeResource();
}

void SkillsPanel::initFromIni(INIReader& ini)
{
	Panel::initFromIni(ini);
	indicateImp = loadRes(ini.Get("init", "indicate", ""));
}

void SkillsPanel::onChildCallBack(PElement child)
{
#ifdef __MOBILE__
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
//			Point childCenter =  {child->rect.x + child->rect.w / 2, child->rect.y + child->rect.h / 2};
//			dragEndPosition = component->getDragRealPosition() - childCenter;
            Point childPos = { child->rect.x, child->rect.y };
			dragEndPosition = component->getDragPosition() - childPos;
		}
	}
	if (parent != nullptr && parent->canCallBack)
	{
		parent->onChildCallBack(getMySharedPtr());
	}
#endif

}

void SkillsPanel::onDraw()
{
	Panel::onDraw();
	if (rightJumpBtn->isDragging())
	{
		drawJumpIndicate(rightJumpBtn->getDragRealPosition());
	}
	else
	{
		for (size_t i = 0; i < SKILL_PANEL_SKILL_COUNT; i++)
		{
			if (skillBtn[i]->isDragging())
			{
//				Point childCenter = { skillBtn[i]->rect.x + skillBtn[i]->rect.w / 2, skillBtn[i]->rect.y + skillBtn[i]->rect.h / 2 };
//				drawIndicate(skillBtn[i]->getDragRealPosition() - childCenter);
                Point childPos = { skillBtn[i]->rect.x, skillBtn[i]->rect.y };
				drawIndicate(skillBtn[i]->getDragPosition() - childPos);
				break;
			}
		}
	}

}

void SkillsPanel::onUpdate()
{
	if (rightJumpBtn->isDragging())
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
	initFromIniFileName("ini\\ui\\mobile\\skills\\window.ini");
	attackBtn = addComponent<RoundButton>("ini\\ui\\mobile\\skills\\attack.ini");
	sitBtn = addComponent<RoundButton>("ini\\ui\\mobile\\skills\\sit.ini");
	for (size_t i = 0; i < SKILL_PANEL_SKILL_COUNT; i++)
	{
		skillBtn[i] = addComponent<DragRoundButton>(convert::formatString("ini\\ui\\mobile\\skills\\skill%d.ini", i + 1));
	}

	for (int i = 0; i < FASTBTN_COUNT; ++i) {
		fastBtn[i] = addComponent<TextButton>(convert::formatString("ini\\ui\\mobile\\skills\\fastbtn%d.ini", i + 1));
		fastBtn[i]->visible = false;
	}
	rightJumpBtn = addComponent<DragRoundButton>("ini\\ui\\mobile\\skills\\rightjump.ini");
	rightJumpBtn->setIndicateImage("mpc\\character\\jump.mpc");
	setChildRectReferToParent();
}

void SkillsPanel::freeResource()
{
	impImage = nullptr;

	freeCom(attackBtn);
	freeCom(sitBtn);
	for (size_t i = 0; i < SKILL_PANEL_SKILL_COUNT; i++)
	{
		freeCom(skillBtn[i]);
	}
	
	for (int i = 0; i < FASTBTN_COUNT; ++i)
	{
		freeCom(fastBtn[i]);
	}
	freeCom(rightJumpBtn);
}

void SkillsPanel::drawJumpIndicate(Point pos)
{
	auto indicateJumpImp = gm->player->res.jump.image;
	if (indicateJumpImp == nullptr)
	{
		return;
	}
	int xOffset = 0, yOffset = 0;
	auto actionTime = IMP::getIMPImageActionTime(indicateJumpImp);
	auto now = (getTime() - dragBeginTime) * 2;
	auto playerPos = gm->player->position;
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

	Point realTilePos = gm->map->getMousePosition(realPos, gm->player->position, cenScreen, gm->camera->offset);
	dir = gm->player->calDirection(realTilePos);
	switch (state)
	{
	case 0:
	{
		realPos = gm->map->getTilePosition(gm->player->position, gm->camera->position, cenScreen, gm->camera->offset);
		break;
	}
	case 1:
	{
		playerPos = gm->map->getTilePosition(gm->player->position, gm->camera->position, cenScreen, gm->camera->offset);
		float param = ((float)now - actionSplitTime) / actionSplitTime;
		realPos.x = (int)round(param * (realPos.x - playerPos.x) + playerPos.x);
		realPos.y = (int)round(param * (realPos.y - playerPos.y) + playerPos.y);
		break;
	}

	default:
	{
		/*realPos = gm->map->getTilePosition(realTilePos, gm->player->position, cenScreen, gm->camera->offset);*/
		break;
	}
	}
	auto img = IMP::loadImageForDirection(indicateJumpImp, dir, now, &xOffset, &yOffset);
	engine->setImageAlpha(img, 128);
	engine->drawImage(img, realPos.x - xOffset, realPos.y - yOffset);
	engine->setImageAlpha(img, 255);
	//auto tempRect = rect;
	//rect.x = x;
	//rect.y = y;
	//RoundButton::onDraw();
	//rect = tempRect;
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
	Point playerPos = gm->player->getPosition(gm->camera->position, gm->camera->offset);
	playerPos.y -= TILE_HEIGHT / 2;
	Rect dest;
	dest.x = playerPos.x - w / 2;
	dest.y = playerPos.y;
	dest.w = w;
	auto dx = sin(angle) * h * MapXRatio;
	auto dy = cos(angle) * h;

	dest.h = (int)round(hypot(dx, dy));
	Point center;
	center.x = w / 2;
	center.y = 0;
	engine->drawImageEx(img, nullptr, &dest, angle, &center);
}
