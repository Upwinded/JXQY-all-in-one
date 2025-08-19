#include "GameController.h"
#include "GameManager.h"

GameController::GameController()
{
	coverMouse = true;
	rectFullScreen = true;
	canCallBack = true;
	priority = epController;
	result = erNone;
}

GameController::~GameController()
{
	removeAllChild();
}

#define freeComponent(component); \
	removeChild(component); \
	if (component.get() != nullptr)\
	{\
		component = nullptr; \
	}
void GameController::freeResource()
{

#ifdef __MOBILE__
	freeComponent(joystickPanel);
	freeComponent(skillPanel);
#endif // __MOBILE__
}

void GameController::init()
{
	freeResource();

#ifdef __MOBILE__

	joystickPanel = std::make_shared<JoystickPanel>();
	addChild(joystickPanel);
	skillPanel = std::make_shared<SkillsPanel>();
	addChild(skillPanel);

#endif // __MOBILE__

#ifdef __MOBILE__

	if (gm->menu->bottomMenu != nullptr)
	{
		skillPanel->skillBtn[0]->drawItem = gm->menu->bottomMenu->magicItem[0];
		skillPanel->skillBtn[1]->drawItem = gm->menu->bottomMenu->magicItem[1];
		skillPanel->skillBtn[2]->drawItem = gm->menu->bottomMenu->magicItem[2];
		skillPanel->skillBtn[3]->drawItem = gm->menu->bottomMenu->magicItem[3];
		skillPanel->skillBtn[4]->drawItem = gm->menu->bottomMenu->magicItem[4];
	}

#endif // __MOBILE__
}

void GameController::onChildCallBack(PElement child)
{
	if (child == nullptr || !gm->global.data.canInput) { return; }
#ifdef __MOBILE__
	auto ret = child->getResult();
	if (child == joystickPanel)
	{
		if (ret & erMouseLDown)
		{
			gm->player->nextAction = nullptr;
		}
	}
	else if (child == skillPanel)
	{
		if (ret & erDragEnd)
		{
			auto dragPos = skillPanel->getDragEndPosition();

			switch (skillPanel->getClickIndex()) {
				case SKILL_PANEL_JUMP: {
					Point pos = gm->getMousePoint(dragPos.x, dragPos.y);
					NextAction act;
					act.action = acJump;
					act.dest = pos;
					gm->player->addNextAction(act);
					break;
				}
				case SKILL_PANEL_SKILL1:
				case SKILL_PANEL_SKILL2:
				case SKILL_PANEL_SKILL3:
				case SKILL_PANEL_SKILL4:
				case SKILL_PANEL_SKILL5:
				{
					if (gm->player->nowAction != acDeath && gm->player->nowAction != acHide)
					{
						auto angle = atan2(-dragPos.x, dragPos.y);
						float distance = 400;
						auto pos = getPlayerRelativePosition(angle, distance, MapXRatio);

						NextAction act;
						act.action = acMagic;
						act.destGE = nullptr;
						act.dest = pos;
						act.type = skillPanel->getClickIndex();
						gm->player->addNextAction(act);
					}
					break;
				}
			}
		}
		else if (ret & erClick)
		{
			switch (skillPanel->getClickIndex())
			{
				case SKILL_PANEL_JUMP:
				{
                    int dir = gm->player->direction;
                    auto angle = dir * pi / 4;
                    float distance = 400;
                    auto pos = getPlayerRelativePosition(angle, distance, TILE_WIDTH / TILE_HEIGHT);
					NextAction act;
					act.action = acJump;
					act.dest = pos;
					gm->player->addNextAction(act);
					break;
				}
				case SKILL_PANEL_SIT:
				{
					if (gm->player->isSitting())
					{
						gm->player->standUp();
					}
					else
					{
						gm->player->beginSit();
					}
					break;
				}
				case SKILL_PANEL_ATTACK:
				{
					if (gm->player->nowAction != acDeath && gm->player->nowAction != acHide)
					{
						NextAction act;
						if (gm->player->canRun && (gm->player->thew > (int)round((float)gm->player->info.thewMax * MIN_THEW_RATE_TO_RUN) || gm->player->thew > MIN_THEW_LIMIT_TO_RUN))
						{
							act.action = acRun;
						}
						else
						{
							act.action = acWalk;
						}
						std::shared_ptr<NPC> tempNPC = nullptr;
						if (gm->npcManager->clickIndex >= 0)
						{
							tempNPC = gm->npcManager->npcList[gm->npcManager->clickIndex];
						}
						else
						{
							tempNPC = gm->npcManager->findNearestViewNPC(lkEnemy, gm->player->position, 15);
						}
						act.type = ndAttack;
						if (tempNPC != nullptr)
						{
							act.dest = tempNPC->position;
							act.destGE = tempNPC;
						}
						else
						{
							act.dest = gm->map->getSubPoint(gm->player->position, gm->player->direction);
						}
						gm->player->addNextAction(act);
					}
					break;
				}
                case SKILL_PANEL_SKILL1:
                case SKILL_PANEL_SKILL2:
                case SKILL_PANEL_SKILL3:
                case SKILL_PANEL_SKILL4:
                case SKILL_PANEL_SKILL5:
				{
					if (gm->player->nowAction != acDeath && gm->player->nowAction != acHide)
					{
						NextAction act;
						act.action = acMagic;
						std::shared_ptr<NPC> tempNPC = nullptr;
						if (gm->npcManager->clickIndex >= 0)
						{
							tempNPC = gm->npcManager->npcList[gm->npcManager->clickIndex];
						}
						else
						{
							tempNPC = gm->npcManager->findNearestViewNPC(lkEnemy, gm->player->position, 15);
						}
						if (tempNPC != nullptr)
						{
							act.dest = tempNPC->position;
							act.destGE = tempNPC;
						}
						else
						{
							act.dest = gm->map->getSubPoint(gm->player->position, gm->player->direction);
						}
						act.type = skillPanel->getClickIndex();
						gm->player->addNextAction(act);
					}
					break;
				}
				case SKILL_PANEL_FAST_SELECT:
				case SKILL_PANEL_FAST_SELECT + 1:
				case SKILL_PANEL_FAST_SELECT + 2:
				case SKILL_PANEL_FAST_SELECT + 3:
				{
					NextAction act;
					if (gm->player->canRun && (gm->player->thew > (int)round((float)gm->player->info.thewMax * MIN_THEW_RATE_TO_RUN) || gm->player->thew > MIN_THEW_LIMIT_TO_RUN))
					{
						act.action = acRun;
					}
					else
					{
						act.action = acWalk;
					}
					int index = skillPanel->getClickIndex() - SKILL_PANEL_FAST_SELECT;
					if (gm->fastSelectingList.size() > index && gm->fastSelectingList[index].destGE != nullptr)
					{
						if (gm->player->nowAction != acDeath && gm->player->nowAction != acHide)
						{
							act.type = gm->fastSelectingList[index].type;
							if (((act.type == ndTalk) && (gm->npcManager->findNPC(std::dynamic_pointer_cast<NPC>(gm->fastSelectingList[index].destGE))))
								|| ((act.type == ndObj) && (gm->objectManager->findObj(std::dynamic_pointer_cast<Object>(gm->fastSelectingList[index].destGE)))))
							{
								act.dest = gm->fastSelectingList[index].destGE->position;
								act.destGE = gm->fastSelectingList[index].destGE;
							}
							else
							{
								act.destGE = nullptr;
							}
							gm->player->addNextAction(act);
						}
					}
					break;
				}
			}
		}
	}
#endif
}

void GameController::onEvent()
{
	if (!gm->global.data.canInput)
	{
		return;
	}
	if (MouseAlreadyDown)
	{
		if (!engine->getMousePressed(MBC_MOUSE_LEFT))
		{
			MouseAlreadyDown = false;
		}
	}
//#ifndef __MOBILE__
	if (dragging == TOUCH_UNTOUCHEDID && MouseAlreadyDown && engine->getMousePressed(MBC_MOUSE_LEFT) && touchingID != TOUCH_UNTOUCHEDID && !engine->getKeyPress(KEY_LALT) && !engine->getKeyPress(KEY_RALT))
	{
		if (gm->player->nowAction != acDeath && gm->player->nowAction != acHide)
		{
			int w, h;
			engine->getWindowSize(w, h);
			Point cenScreen;
			cenScreen.x = (int)w / 2;
			cenScreen.y = (int)h / 2;
			int x, y;
			engine->getMousePosition(x, y);
			Point pos = gm->map->getMousePosition({ x, y }, gm->camera->position, cenScreen, gm->camera->offset);
			if (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT))
			{
				NextAction act;
				act.action = acRun;
				act.dest = pos;
				gm->player->addNextAction(act);
			}
			else
			{
				NextAction act;
				act.action = acWalk;
				act.dest = pos;
				gm->player->addNextAction(act);
			}
		}
	}
	else if (dragging == TOUCH_UNTOUCHEDID && engine->getMousePressed(MBC_MOUSE_LEFT) && gm->npcManager->clickIndex >= 0 && !engine->getKeyPress(KEY_LALT) && !engine->getKeyPress(KEY_RALT))
	{
		if (gm->player->nowAction != acDeath && gm->player->nowAction != acHide)
		{
			NextAction act;
			if (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT))
			{
				act.action = acRun;
			}
			else
			{
				act.action = acWalk;
			}
			std::shared_ptr<NPC> tempNPC = gm->npcManager->npcList[gm->npcManager->clickIndex];
			if (tempNPC != nullptr)
			{
				act.destGE = tempNPC;
				if (tempNPC->kind == nkBattle && tempNPC->relation == nrHostile)
				{
					act.type = ndAttack;
				}
				else
				{
					act.type = ndTalk;
				}
				act.dest = tempNPC->position;
				gm->player->addNextAction(act);
			}
		}
	}
	else if (dragging == TOUCH_UNTOUCHEDID && engine->getMousePressed(MBC_MOUSE_LEFT) && gm->objectManager->clickIndex >= 0 && !engine->getKeyPress(KEY_LALT) && !engine->getKeyPress(KEY_RALT))
	{
		NextAction act;

		if (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT))
		{
			act.action = acRun;
		}
		else
		{
			act.action = acWalk;
		}
		std::shared_ptr<Object> tempObject = gm->objectManager->objectList[gm->objectManager->clickIndex];
		if (tempObject != nullptr)
		{
			act.destGE = tempObject;
			act.type = ndObj;
			act.dest = tempObject->position;
			gm->player->addNextAction(act);
		}
	}

	bool KeyStep = true;
	Point dest = gm->player->position;
	int line = std::abs(dest.y % 2);
	NextAction act;
	if (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT))
	{
		act.action = acARun;
	}
	else
	{
		act.action = acAWalk;
	}

	if (engine->getKeyPress(KEY_UP) && engine->getKeyPress(KEY_LEFT))
	{
		dest.x += -1 + line;
		dest.y -= 1;
	}
	else if (engine->getKeyPress(KEY_UP) && engine->getKeyPress(KEY_RIGHT))
	{
		dest.x += line;
		dest.y -= 1;
	}
	else if (engine->getKeyPress(KEY_DOWN) && engine->getKeyPress(KEY_LEFT))
	{
		dest.x += -1 + line;
		dest.y += 1;
	}
	else if (engine->getKeyPress(KEY_DOWN) && engine->getKeyPress(KEY_RIGHT))
	{
		dest.x += line;
		dest.y += 1;
	}
	else if (engine->getKeyPress(KEY_UP))
	{
		dest.y -= 2;
	}
	else if (engine->getKeyPress(KEY_DOWN))
	{
		dest.y += 2;
	}
	else if (engine->getKeyPress(KEY_LEFT))
	{
		dest.x -= 1;
	}
	else if (engine->getKeyPress(KEY_RIGHT))
	{
		dest.x += 1;
	}
	else
	{
		KeyStep = false;
	}

	if (KeyStep && gm->map->canWalk(dest))
	{
		act.dest = dest;
		gm->player->addNextAction(act);
	}
//#endif
    
#ifdef __MOBILE__
    // NextAction act;
	bool joystickAction = true;
	if (joystickPanel->joystick->isRunning())
	{
		if (gm->player->canRun)
		{
			act.action = acARun;
		}
		else
		{
			act.action = acAWalk;
		}
	}
	else if (joystickPanel->joystick->isWalking())
	{
		act.action = acAWalk;
	}
	else
	{
		joystickAction = false;
	}
	if (joystickAction && (gm->player->nextAction == nullptr || gm->player->nextAction->action == acRun|| gm->player->nextAction->action == acWalk || gm->player->nextAction->action == acARun|| gm->player->nextAction->action == acAWalk))
	{
		auto dirList = joystickPanel->joystick->getDirectionList();
		bool steping = false;
		for (size_t i = 0; i < dirList.size(); i++)
		{
			auto step = gm->map->getSubPoint(gm->player->position, dirList[i]);
			if (gm->player->position == step)
			{
				break;
			}
			if (!gm->map->isInMap(step))
			{
				continue;
			}
			bool breakOut = false;
			for (auto iter = gm->map->dataMap.tile[step.y][step.x].stepNPCList.begin(); iter != gm->map->dataMap.tile[step.y][step.x].stepNPCList.end(); iter++)
			{
				if ((*iter) == gm->player)
				{
                    breakOut = true;
                    break;
				}
			}
            if (!breakOut && gm->map->canWalkDirectlyTo(gm->player->position, dirList[i]))
			{
				act.dest = step;
				steping = true;
				gm->player->addNextAction(act);
				break;
			}
			if (breakOut)
			{
				break;
			}
		}
		if (!steping)
		{
			if (dirList.size() > 0 && gm->player->isStanding())
			{
				gm->player->direction = *dirList.begin();
			}

		}
	}


	if (!gm->inEvent)
	{
		gm->fastSelectingList.resize(0);
		int radius = 2;
		auto tempObjList = gm->objectManager->findRadiusScriptViewObj(gm->player->position, radius);
		for (int i = 0; i < tempObjList.size(); ++i)
		{
			NextAction action;
			action.type = ndObj;
			action.destGE = tempObjList[i];
			action.dest = action.destGE->position;
			action.distance = gm->map->calDistance(gm->player->position, action.destGE->position);
			gm->fastSelectingList.push_back(action);
		}
		auto tempNPCList = gm->npcManager->findRadiusScriptViewNPC(gm->player->position, radius);
		for (int i = 0; i < tempNPCList.size(); ++i)
		{
			NextAction action;
			action.type = ndTalk;
			action.destGE = tempNPCList[i];
			action.dest = action.destGE->position;
			action.distance = gm->map->calDistance(gm->player->position, action.destGE->position);

			gm->fastSelectingList.push_back(action);
		}

		std::sort(gm->fastSelectingList.begin(), gm->fastSelectingList.end(), gm->actionCmp);
		if (gm->fastSelectingList.size() > FASTBTN_COUNT)
		{
			gm->fastSelectingList.resize(FASTBTN_COUNT);
		}
		for (int i = 0; i < gm->fastSelectingList.size(); ++i)
		{
			if (gm->fastSelectingList[i].type == ndTalk)
			{
				setFastSelectBtn(i, true, std::dynamic_pointer_cast<NPC>(gm->fastSelectingList[i].destGE)->npcName);
			}
			else
			{
				setFastSelectBtn(i, true, std::dynamic_pointer_cast<Object>(gm->fastSelectingList[i].destGE)->objName);
			}
		}

		for (int i = gm->fastSelectingList.size(); i < FASTBTN_COUNT; ++i)
		{
			setFastSelectBtn(i, false);
		}
	}
	else
	{
		for (int i = 0; i < FASTBTN_COUNT; ++i)
		{
			setFastSelectBtn(i, false);
		}
	}
#endif
}

void GameController::onUpdate()
{
	visible = !gm->inEvent;
}

bool GameController::onHandleEvent(AEvent & e)
{
	if (!gm->global.data.canInput)
	{
		return false;
	}

	if (e.eventType == ET_KEYDOWN)
	{
		if (e.eventData == KEY_V)
		{
			if (gm->player->isSitting())
			{
				gm->player->standUp();
			}
			else
			{
				gm->player->beginSit();
			}
			return true;
		}
		else if (e.eventData == KEY_A || 
				e.eventData == KEY_S || 
				e.eventData == KEY_D ||
				e.eventData == KEY_F ||
				e.eventData == KEY_G)
		{
			NextAction act;
			act.action = acMagic;
			if (gm->npcManager->clickIndex >= 0)
			{
				act.dest = gm->npcManager->npcList[gm->npcManager->clickIndex]->position;
				act.destGE = gm->npcManager->npcList[gm->npcManager->clickIndex];
			}
			else
			{
				act.dest = gm->getMousePoint();
			}
			switch (e.eventData)
			{
			case KEY_A:
				act.type = 0;
				break;
			case KEY_S:
				act.type = 1;
				break;
			case KEY_D:
				act.type = 2;
				break;
			case KEY_F:
				act.type = 3;
				break;
			case KEY_G:
				act.type = 4;
				break;
			default:
				act.type = 0;
				break;
			} 
			_last_magic_index = act.type;
			gm->player->addNextAction(act);
			return true;
		}
		else if (e.eventData == KEY_Z || e.eventData == KEY_X || e.eventData == KEY_C)
		{
			int itemIndex = GOODS_COUNT;
			switch (e.eventData)
			{
			case KEY_Z: itemIndex += 0; break;
			case KEY_X: itemIndex += 1; break;
			case KEY_C: itemIndex += 2; break;
			default:
				break;
			}
			gm->goodsManager.useItem(itemIndex);
			return true;
		}
	}
	else if (dragging == TOUCH_UNTOUCHEDID && e.eventType == ET_MOUSEDOWN && touchingID != TOUCH_UNTOUCHEDID)
	{
		if (e.eventData == MBC_MOUSE_LEFT)
		{
			MouseAlreadyDown = true;
			Point pos = gm->getMousePoint();

			NextAction act;

			if (engine->getKeyPress(KEY_LALT) || engine->getKeyPress(KEY_RALT))
			{
				act.action = acJump;
			}
			else if (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT))
			{
				act.action = acRun;
			}
			else
			{
				act.action = acWalk;
			}

			act.dest = pos;
			gm->player->addNextAction(act);
			return true;
		}
		else if (e.eventData == MBC_MOUSE_RIGHT)
		{
			if (_last_magic_index >= 0)
			{
				NextAction act;
				act.action = acMagic;
				if (gm->npcManager->clickIndex >= 0)
				{
					act.dest = gm->npcManager->npcList[gm->npcManager->clickIndex]->position;
					act.destGE = gm->npcManager->npcList[gm->npcManager->clickIndex];
				}
				else
				{
					act.dest = gm->getMousePoint();
				}
				act.type = _last_magic_index;
				gm->player->addNextAction(act);
				return true;
			}
		}
	}
	else if (dragging == TOUCH_UNTOUCHEDID && e.eventType == ET_MOUSEDOWN && gm->npcManager->clickIndex >= 0)
	{
		NextAction act;
		if (engine->getKeyPress(KEY_LALT) || engine->getKeyPress(KEY_RALT))
		{
			act.action = acJump;
		}
		else if (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT))
		{
			act.action = acRun;
		}
		else
		{
			act.action = acWalk;
		}
		if (act.action != acJump)
		{
			act.destGE = gm->npcManager->npcList[gm->npcManager->clickIndex];
			if (gm->npcManager->npcList[gm->npcManager->clickIndex]->kind == nkBattle && gm->npcManager->npcList[gm->npcManager->clickIndex]->relation == nrHostile)
			{
				act.type = ndAttack;
			}
			else
			{
				act.type = ndTalk;
			}
		}
		act.dest = gm->npcManager->npcList[gm->npcManager->clickIndex]->position;
		gm->player->addNextAction(act);
	}
	else if (dragging == TOUCH_UNTOUCHEDID && e.eventType == ET_MOUSEDOWN && gm->objectManager->clickIndex >= 0)
	{
		NextAction act;
		if (engine->getKeyPress(KEY_LALT) || engine->getKeyPress(KEY_RALT))
		{
			act.action = acJump;
		}
		else if (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT))
		{
			act.action = acRun;
		}
		else
		{
			act.action = acWalk;
		}
		if (act.action != acJump)
		{
			act.destGE = gm->objectManager->objectList[gm->objectManager->clickIndex];
			act.type = ndObj;
		}
		act.dest = gm->objectManager->objectList[gm->objectManager->clickIndex]->position;
		gm->player->addNextAction(act);
	}
	return false;
}

#ifdef __MOBILE__
void GameController::setFastSelectBtn(int index, bool sVisible, std::string str)
{
	skillPanel->fastBtn[index]->visible = sVisible;
	if (skillPanel->fastBtn[index]->visible)
	{
		skillPanel->fastBtn[index]->setUTF8Str(str);
	}
}
#endif // __MOBILE__

Point GameController::getPlayerRelativePosition(float angle, float distance, float xFactor)
{
	Point relativePos;
	relativePos.x = (int)round(-sin(angle) * distance * xFactor);
	relativePos.y = (int)round(cos(angle) * distance);
	auto playerPos = gm->player->getPosition(gm->camera->position, gm->camera->offset);
	playerPos.y -= TILE_HEIGHT / 2;
	relativePos = relativePos + playerPos;
	relativePos = gm->getMousePoint(relativePos.x, relativePos.y);
	return relativePos;
}


