#include "Player.h"
#include "Map.h"
#include "../GameManager/GameManager.h"

Player::Player()
{
	name = "player";
	coverMouse = false;
	priority = epPlayer;
	//因为player的绘制在地图层中，不单独绘制
	visible = false;
	needEvents = false;
	nowAction = acStand;
	npcIndex = 0;
}

Player::~Player()
{
	freeResource();
}

void Player::calInfo()
{
	info.attack = attack;
	info.defend = defend;
	info.evade = evade;
	info.lifeMax = lifeMax;
	info.thewMax = thewMax;
	info.manaMax = manaMax;
	for (size_t i = 0; i < GOODS_BODY_COUNT; i++)
	{
		if (gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + i].iniFile == "" || gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + i].goods == nullptr || gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + i].number <= 0)
		{
			continue;
		}
		else
		{
			info.attack += gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + i].goods->attack;
			info.defend += gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + i].goods->defend;
			info.evade += gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + i].goods->evade;
			info.lifeMax += gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + i].goods->lifeMax;
			info.thewMax += gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + i].goods->thewMax;
			info.manaMax += gm->goodsManager.goodsList[GOODS_COUNT + GOODS_TOOLBAR_COUNT + i].goods->manaMax;
		}
	}
}

void Player::updateLevel()
{
	if (level >= levelList.levelCount)
	{
		return;
	}
	if (level < 1)
	{
		setLevel(1);
	}
	else
	{
		attack += levelList.level[level].attack - levelList.level[level - 1].attack;
		defend += levelList.level[level].defend - levelList.level[level - 1].defend;
		evade += levelList.level[level].evade - levelList.level[level - 1].evade;
		lifeMax += levelList.level[level].lifeMax - levelList.level[level - 1].lifeMax;
		thewMax += levelList.level[level].thewMax - levelList.level[level - 1].thewMax;
		manaMax += levelList.level[level].manaMax - levelList.level[level - 1].manaMax;
		levelUpExp = levelList.level[level].levelUpExp;
		level++;
		calInfo();
		fullLife();
		fullThew();
		fullMana();
		gm->menu->stateMenu->updateLabel();
	}
}

void Player::setLevel(int lvl)
{
	if (lvl < 1)
	{
		lvl = 1;
	}
	else if (level >= levelList.levelCount)
	{
		lvl = levelList.levelCount;
	}
	level = lvl;
	attack = levelList.level[level - 1].attack;
	defend = levelList.level[level - 1].defend;
	evade = levelList.level[level - 1].evade;
	lifeMax = levelList.level[level - 1].lifeMax;
	thewMax = levelList.level[level - 1].thewMax;
	manaMax = levelList.level[level - 1].manaMax;
	levelUpExp = levelList.level[level - 1].levelUpExp;
	calInfo();
	fullLife();
	fullThew();
	fullMana();
	gm->menu->stateMenu->updateLabel();
}

void Player::fullLife()
{
	life = info.lifeMax;
	gm->menu->stateMenu->updateLabel();
}

void Player::fullThew()
{
	thew = info.thewMax;
	gm->menu->stateMenu->updateLabel();
}

void Player::fullMana()
{
	mana = info.manaMax;
	gm->menu->stateMenu->updateLabel();
}

void Player::addLifeMax(int value)
{
	lifeMax += value;
	calInfo();
	fullLife();
	gm->menu->stateMenu->updateLabel();
}

void Player::addThewMax(int value)
{
	thewMax += value;
	calInfo();
	fullThew();
	gm->menu->stateMenu->updateLabel();
}

void Player::addManaMax(int value)
{
	manaMax += value;
	calInfo();
	fullMana();
	gm->menu->stateMenu->updateLabel();
}

void Player::addLife(int value)
{
    if (value < 0)
    {
        value = (int)round(value * DAMAGE_RATE);
    }
	life += value;
	limitAttribute();
	gm->menu->stateMenu->updateLabel();
	if (life <= 0)
	{
		beginDie();
	}
}

void Player::addThew(int value)
{
	thew += value;
	limitAttribute();
	gm->menu->stateMenu->updateLabel();
}

void Player::addMana(int value)
{
	mana += value;
	limitAttribute();
	gm->menu->stateMenu->updateLabel();
}

void Player::addAttack(int value)
{
	attack += value;
	calInfo();
	gm->menu->stateMenu->updateLabel();
}

void Player::addDefend(int value)
{
	defend += value;
	calInfo();
	gm->menu->stateMenu->updateLabel();
}

void Player::addEvade(int value)
{
	evade += value;
	calInfo();
	gm->menu->stateMenu->updateLabel();
}

void Player::addMoney(int value)
{
	money += value;
	gm->menu->goodsMenu->updateMoney();
}

void Player::updateAction(UTime frameTime)
{
	if (isStanding())
	{
		if (getUpdateTime() - recoveryTime > THEW_RECOVERY_INTERVAL)
		{
			recoveryTime += THEW_RECOVERY_INTERVAL;
			addThew(convert_max((int)round(THEW_RECOVERY_RATE * info.thewMax), THEW_RECOVERY_MIN));
		}
		if (fight > 0 && nowAction == acAStand)
		{
			if (getUpdateTime() - fightTime > fightOverTime)
			{
				fight = 0;
				nowAction = acStand;
			}
		}
		if ((nowAction == acStand || nowAction == acStand1) && fight > 0 && canDoAction(acAStand))
		{
			nowAction = acAStand;
		}
		if (running)
		{
			haveDest = false;
		}
		if (haveDest)
		{
			if (position != gotoExDest)
			{
				if (gm->map->canWalk(gotoExDest))
				{
					beginWalk(gotoExDest);
				}
			}
			else
			{
				haveDest = false;
			}
		}
		else if (getUpdateTime() - actionBeginTime > actionLastTime)
		{
			actionBeginTime += actionLastTime;
			if (nowAction == acStand)
			{
				if (canDoAction(&res.stand1))
				{
					nowAction = acStand1;
					actionLastTime = getActionTime(acStand1);
				}
			}
			else if (nowAction == acStand1)
			{
				nowAction = acStand;
				actionLastTime = getActionTime(acStand);
			}
			playSound(nowAction);
		}
	}
	else if (isWalking() || isRunning())
	{
		if (isWalking() && getUpdateTime() - recoveryTime > THEW_RECOVERY_INTERVAL)
		{
			recoveryTime += THEW_RECOVERY_INTERVAL;
			addThew(convert_max((int)round(THEW_RECOVERY_RATE * info.thewMax), THEW_RECOVERY_MIN));
		}
		if (fight > 0)
		{
			if (getUpdateTime() - fightTime > fightOverTime)
			{
				
				fight = 0;
				if (nowAction == acAWalk)
				{
					nowAction = acWalk;
				}
				else if (nowAction == acARun)
				{
					nowAction = acRun;
				}
				
			}
		}
		if (nowAction == acWalk  && fight > 0 && canDoAction(acAWalk))
		{
			nowAction = acAWalk;
		}
		else if (nowAction == acRun && fight > 0 && canDoAction(acARun))
		{
			nowAction = acARun;
		}

		if (getUpdateTime() - actionBeginTime >= actionLastTime)
		{
			playSound(nowAction);
			actionBeginTime += actionLastTime;
		}
		if (getUpdateTime() - stepBeginTime > stepLastTime)
		{
			if (stepState == ssIn)
			{
				stepList.erase(stepList.begin());
				if (gm->map->haveTraps(position) && !gm->inEvent)
				{
					nextAction = nullptr;
					if (isRunning())
					{
						recoveryTime = getUpdateTime();
					}					
					stepList.resize(0);
					actionBeginTime = stepBeginTime + stepLastTime;
					beginStand();
					offset = { 0, 0 };
					gm->runTrapScript(gm->map->getTrapIndex(position));
				}
				else if (nextAction == nullptr && nextDest != ndNone
				 && ((nextDest == ndAttack && gm->npcManager->findNPC(std::dynamic_pointer_cast<NPC>(destGE)) && Map::calDistance(position, (std::dynamic_pointer_cast<NPC>(destGE)->position)) <= attackRadius)
					 || (nextDest == ndTalk && gm->npcManager->findNPC(std::dynamic_pointer_cast<NPC>(destGE)) && Map::calDistance(position, (std::dynamic_pointer_cast<NPC>(destGE)->position)) <= (std::dynamic_pointer_cast<NPC>(destGE))->dialogRadius)
					 || (nextDest == ndObj && gm->objectManager->findObj(std::dynamic_pointer_cast<Object>(destGE)) && Map::calDistance(position, (std::dynamic_pointer_cast<Object>(destGE))->position) <= 1)))
				{

					int nd = nextDest;
					nextDest = ndNone;

					if (nd == ndObj)
					{
						std::shared_ptr<Object> tempObj = std::dynamic_pointer_cast<Object>(destGE);
						destGE = nullptr;
						beginStand();
						runObj(tempObj);
					}
					else
					{
						std::shared_ptr<NPC> tempNPC = std::dynamic_pointer_cast<NPC>(destGE);
						destGE = nullptr;
						beginStand();
						if (nd == ndAttack)
						{
							beginAttack(tempNPC->position, tempNPC);
						}
						else
						{
							talkTo(tempNPC);
						}
					}			
				}
				else if (nextAction == nullptr && stepList.size() > 0 && gm->map->canWalk(stepList[0]))
				{
					if (isRunning() && thew < RUN_THEW_COST && !gm->inEvent)
					{					
						recoveryTime = getUpdateTime();
						stepList.resize(0);
						beginStand();
						gm->showMessage(u8"体力不足！");
					}
					else
					{
						direction = calDirection(stepList[0]);
						if (direction % 2 == 0)
						{
							int dir1 = direction + 1;
							int dir2 = direction - 1;
                            
							limitDir(&dir1);
							limitDir(&dir2);
							if (gm->map->canPass(gm->map->getSubPoint(position, dir1)) && gm->map->canPass(gm->map->getSubPoint(position, dir2)))
							{
								stepState = ssOut;
								gm->map->addStepToDataMap(stepList[0], std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
								direction = calDirection(stepList[0]);
								stepBeginTime += stepLastTime;
								calStepLastTime();
								calOffset(getUpdateTime() - stepBeginTime, stepLastTime);
							}
							else
							{
								if (isRunning())
								{
									recoveryTime = getUpdateTime();
								}
								stepList.resize(0);
								beginStand();
							}
						}
						else
						{
							if (isRunning() && !gm->inEvent)
							{
								thew -= RUN_THEW_COST;
							}
							stepState = ssOut;
							gm->map->addStepToDataMap(stepList[0], std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
							direction = calDirection(stepList[0]);
							stepBeginTime += stepLastTime;
							calStepLastTime();
							calOffset(getUpdateTime() - stepBeginTime, stepLastTime);
						}					
					}		
				}
				else
				{
					if (nextAction != nullptr && (nextAction->action == acWalk || nextAction->action == acAWalk))
					{
						offset = { 0, 0 };
						Point dest = nextAction->dest;
						nextDest = nextAction->type;
						destGE = nextAction->destGE;

						nextAction = nullptr;
						stepList.resize(0);
						if (isRunning())
						{
							recoveryTime = getUpdateTime();
						}
						changeWalk(dest);						
					}
					else if (nextAction != nullptr && (nextAction->action == acRun || nextAction->action == acARun) && canRun)
					{
						offset = { 0, 0 };
						Point dest = nextAction->dest;
						nextDest = nextAction->type;
						destGE = nextAction->destGE;

						nextAction = nullptr;
						stepList.resize(0);
						changeRun(dest);
					}
					else
					{
						if (isRunning())
						{
							recoveryTime = getUpdateTime();
						}
						stepList.resize(0);
						beginStand();
					}
				}
			}
			else if (stepState == ssOut)
			{
				gm->map->deleteNPCFromDataMap(position, std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
				gm->map->deleteStepFromDataMap(stepList[0], std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
				position = stepList[0];
				gm->map->addNPCToDataMap(position, std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
				stepState = ssIn;
				stepBeginTime += stepLastTime;
				calOffset(getUpdateTime() - stepBeginTime, stepLastTime);
			}
		}
		else
		{
			calOffset(getUpdateTime() - stepBeginTime, stepLastTime);
		}
	}
	else if (nowAction == acJump || nowAction == acAJump)
	{
		if (fight > 0)
		{
			if (getUpdateTime() - fightTime > fightOverTime)
			{
				fight = 0;
				nowAction = acJump;
			}
		}
		if (nowAction == acJump && fight > 0 && canDoAction(acAJump))
		{
			nowAction = acAJump;
		}

		if (getUpdateTime() - actionBeginTime > actionLastTime)
		{
			if (jumpState != jsDown)
			{
				jumpState = jsDown;
				gm->map->deleteNPCFromDataMap(position, std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
				gm->map->deleteStepFromDataMap(stepList[0], std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
				position = stepList[0];
				offset = { 0.0, 0.0 };
				stepList.resize(0);
				gm->map->addNPCToDataMap(position, std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
			}
			recoveryTime = getUpdateTime();
			beginStand();
			if (gm->map->haveTraps(position))
			{
				nextAction = nullptr;

				gm->runTrapScript(gm->map->getTrapIndex(position));
			}
		}
		else if (getUpdateTime() - actionBeginTime > 2 * actionLastTime / 3)
		{
			if (jumpState != jsDown)
			{
				jumpState = jsDown;
				gm->map->deleteNPCFromDataMap(position, std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
				gm->map->deleteStepFromDataMap(stepList[0], std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
				position = stepList[0];
				offset = { 0.0, 0.0 };				
				stepList.resize(0);
				gm->map->addNPCToDataMap(position, std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
			}
		}
		else if (getUpdateTime() - actionBeginTime > actionLastTime / 3)
		{
			gm->map->deleteNPCFromDataMap(position, std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
			if (jumpState != jsJumping)
			{			
				updateJumpingPosition(getUpdateTime() - actionBeginTime - actionLastTime / 3, jumpSpeed);
				jumpState = jsJumping;
			}
			else
			{
				updateJumpingPosition(frameTime, jumpSpeed);
			}	
			gm->map->addNPCToDataMap(position, std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
		}
		else
		{
			jumpState = jsUp;
		}
	}
	else if (nowAction == acAttack || nowAction == acAttack1 || nowAction == acAttack2 || nowAction == acSpecialAttack || nowAction == acMagic)
	{
		auto tempActionLastTime = actionLastTime;
		if (gm->global.gameType == GAME_JXQY2)
		{
			tempActionLastTime = PLAYER_MAGIC_DELAY;
		}
		if (!attackDone && nowAction == acMagic && getUpdateTime() - actionBeginTime >= tempActionLastTime)
		{
			attackDone = true;
			if (magicIndex >= 0)
			{
				useMagic(gm->magicManager.magicList[MAGIC_COUNT + magicIndex].magic, magicDest, gm->magicManager.magicList[MAGIC_COUNT + magicIndex].level, attackTarget);
			}			
		}
		if (getUpdateTime() - actionBeginTime >= actionLastTime)
		{
			if (!attackDone)
			{
				attackDone = true;
				if (nowAction == acMagic)
				{
					if (magicIndex >= 0)
					{
						useMagic(gm->magicManager.magicList[MAGIC_COUNT + magicIndex].magic, magicDest, gm->magicManager.magicList[MAGIC_COUNT + magicIndex].level, attackTarget);
					}
				}
				else
				{
					if (nowAction == acAttack2 || nowAction == acSpecialAttack)
					{
						doSpecialAttack(magicDest, attackTarget);
					}
					else
					{
						doAttack(magicDest, attackTarget);
					}
				}
			}		
			
			fight = 1;
			fightTime = getUpdateTime();
			nowAction = acAStand;
			actionBeginTime += actionLastTime;
			actionLastTime = getActionTime(acAStand);	
			recoveryTime = getUpdateTime();
		}
	}
	else if (nowAction == acSitting)
	{
		if (getUpdateTime() - actionBeginTime > actionLastTime)
		{
			nowAction = acSit;
			sitState = ssSat;
			actionBeginTime += actionLastTime;
			//此处应花费体力恢复内力
			if (mana == info.manaMax)
			{
				beginStand();
			}
			else if (thew < SIT_THEW_COST)
			{
				gm->showMessage(u8"体力不足！");
				beginStand();
			}
			else
			{
				thew -= SIT_THEW_COST;
				addMana(convert_max((int)round(SIT_MANA_ADD_RATE * info.manaMax), SIT_THEW_COST));
				if (mana == info.manaMax)
				{
					beginStand();
				}
			}
		}
	}
	else if (nowAction == acSit)
	{
		if (getUpdateTime() - actionBeginTime > MANA_RECOVERY_INTERVAL)
		{
			actionBeginTime += MANA_RECOVERY_INTERVAL;
			//此处应花费体力恢复内力
			if (mana == info.manaMax)
			{
				beginStand();
			}
			else if (thew < SIT_THEW_COST)
			{
				gm->showMessage(u8"体力不足！");
				beginStand();
			}
			else
			{
				thew -= SIT_THEW_COST;
				addMana(convert_max((int)round(SIT_MANA_ADD_RATE * info.manaMax), SIT_THEW_COST));
				if (mana == info.manaMax)
				{
					beginStand();
				}
			}
		}
		if (nextAction != nullptr)
		{

			beginStand();
		}
	}
	else if (nowAction == acHurt)
	{
		if (getUpdateTime() - actionBeginTime > actionLastTime)
		{
			fight = 1;
			fightTime = getUpdateTime();
			nowAction = acAStand;
			actionBeginTime += actionLastTime;
			actionLastTime = getActionTime(acAStand);
			recoveryTime = getUpdateTime();
		}
	}
	else if (nowAction == acDeath)
	{
		if (getUpdateTime() - actionBeginTime > actionLastTime)
		{
//			result |= erLifeExhaust;
			nowAction = acHide;
			addBody();
		}
	}
	else if (nowAction == acSpecial)
	{
		if (getUpdateTime() - actionBeginTime > actionLastTime)
		{
			if (fight > 0)
			{
				if (getUpdateTime() - fightTime > fightOverTime)
				{
					fight = 0;
				}
			}
			recoveryTime = getUpdateTime();
			beginStand();
		}
	}
}

void Player::onUpdate()
{
	auto ft = getFrameTime();
	updateTime = getTime();
	if (getResult() & erRunDeathScript)
	{
		if (!gm->inEvent)
		{
			gm->runNPCDeathScript(std::dynamic_pointer_cast<NPC>(getMySharedPtr()), deathScript, gm->mapFolderName);
		}
		else
		{		
			EventInfo eventInfo;
			eventInfo.npc = std::dynamic_pointer_cast<NPC>(getMySharedPtr());
            eventInfo.scriptName = deathScript;
            eventInfo.scriptMapName = gm->mapFolderName;
			gm->eventList.push_back(eventInfo);
		}
	}
	if (shieldLife > 0)
	{
		if (getTime() - shieldBeginTime >= shieldLastTime)
		{
			shieldLife = 0;
			gm->effectManager->deleteEffect(shieldEffect);
			shieldEffect = nullptr;
		}
		else
		{
			shieldBeginTime += ft;
			shieldLastTime -= ft;
		}
	}
	else if (shieldEffect != nullptr)
	{
		shieldLife = 0;
		gm->effectManager->deleteEffect(shieldEffect);
		shieldEffect = nullptr;
	}

	if (frozen && nowAction != acDeath)
	{
		if (frozenLastTime >= ft)
		{
			frozenLastTime -= ft;
			timer.set(getUpdateTime() - ft);
			updateTime -= ft;
			LastFrameTime -= ft;
			return;
		}
		else
		{
			frozen = false;
			auto lastT = ft - frozenLastTime;
			timer.set(getUpdateTime() - frozenLastTime);
			updateTime -= frozenLastTime;
			LastFrameTime -= frozenLastTime;
			frozenLastTime = 0;
			ft = lastT;
		}
	}

	updateAction(ft);

	//站立时处理接下来的人物动作
	if (nextAction != nullptr && isStanding())
	{
		if (nextAction->action == acWalk || nextAction->action == acAWalk)
		{
			nextDest = nextAction->type;
			destGE = nextAction->destGE;
			beginWalk(nextAction->dest);
		}
		else if (nextAction->action == acRun || nextAction->action == acARun)
		{
			nextDest = nextAction->type;
			destGE = nextAction->destGE;
			beginRun(nextAction->dest);
		}
		else if (nextAction->action == acJump || nextAction->action == acAJump)
		{
			nextDest = ndNone;
			destGE = nullptr;
			beginJump(nextAction->dest);
		}
		else if (nextAction->action == acAttack || nextAction->action == acAttack1 || nextAction->action == acAttack2 || nextAction->action == acSpecialAttack)
		{
            if (canFight)
            {
                nextDest = ndNone;
                destGE = nextAction->destGE;
                beginAttack(nextAction->dest, destGE);
                destGE = nullptr;
            }
		}
		else if (nextAction->action == acMagic)
		{
			if (canFight)
            {
                nextDest = ndNone;
                destGE = nextAction->destGE;
                if (nextAction->type >= 0 && nextAction->type < MAGIC_TOOLBAR_COUNT)
                {
                    if (gm->magicManager.magicList[MAGIC_COUNT + nextAction->type].magic == nullptr || gm->magicManager.magicList[MAGIC_COUNT + nextAction->type].level < 1 || gm->magicManager.magicList[MAGIC_COUNT + nextAction->type].level > MAGIC_MAX_LEVEL)
                    {
                        magicIndex = -1;
                        magicDest = nextAction->dest;
                        beginMagic(nextAction->dest, nextAction->destGE);
                    }
                    else if (mana < gm->magicManager.magicList[MAGIC_COUNT + nextAction->type].magic->level[gm->magicManager.magicList[MAGIC_COUNT + nextAction->type].level].manaCost)
                    {
                        gm->showMessage(u8"内力不足！");
                    }
                    else if (thew < gm->magicManager.magicList[MAGIC_COUNT + nextAction->type].magic->level[gm->magicManager.magicList[MAGIC_COUNT + nextAction->type].level].thewCost)
                    {
                        gm->showMessage(u8"体力不足！");
                    }
                    else if (life < gm->magicManager.magicList[MAGIC_COUNT + nextAction->type].magic->level[gm->magicManager.magicList[MAGIC_COUNT + nextAction->type].level].lifeCost)
                    {
                        gm->showMessage(u8"生命不足！");
                    }
                    else
                    {
                        mana -= gm->magicManager.magicList[MAGIC_COUNT + nextAction->type].magic->level[gm->magicManager.magicList[MAGIC_COUNT + nextAction->type].level].manaCost;
                        thew -= gm->magicManager.magicList[MAGIC_COUNT + nextAction->type].magic->level[gm->magicManager.magicList[MAGIC_COUNT + nextAction->type].level].thewCost;
                        life -= gm->magicManager.magicList[MAGIC_COUNT + nextAction->type].magic->level[gm->magicManager.magicList[MAGIC_COUNT + nextAction->type].level].lifeCost;
                        beginMagic(nextAction->dest, nextAction->destGE);
                        magicIndex = nextAction->type;
                        magicDest = nextAction->dest;
                    }
                }
            }
		}
		else if (nextAction->action == acSit || nextAction->action == acSitting)
		{
			nextDest = ndNone;
			destGE = nullptr;
			beginSit();
		}
		else if (nextAction->action == acHurt)
		{
			nextDest = ndNone;
			destGE = nullptr;
			beginHurt(nextAction->dest);
		}
		else if (nextAction->action == acDeath)
		{
			nextDest = ndNone;
			destGE = nullptr;
			beginDie();
		}
		else if (nextAction->action == acSpecial)
		{
			nextDest = ndNone;
			destGE = nullptr;
			beginSpecial();
		}

		nextAction = nullptr;
	}

	if (running && isStanding())
	{
		running = false;
	}
}

void Player::freeResource()
{
	nowAction = acStand;
	offset = { 0, 0 };
	npcMagic = nullptr;
	npcMagic2 = nullptr;
	if (gm != nullptr)
	{
		gm->magicManager.tryCleanAttackMagic();
	}
	followNPC = "";
	freeNPCRes();
	stepList.resize(0);

	shieldLastTime = 0;
	shieldBeginTime = 0;
	shieldLife = 0;
	shieldEffect = nullptr;

	nextAction = nullptr;

	fight = 0;
	recoveryTime = getUpdateTime();
}

void Player::hurt(std::shared_ptr<Effect> e)
{
	if (nowAction == acDeath || nowAction == acHide || e == nullptr)
	{
		return;
	}
	bool addexp = false;
	if (e->launcherKind == lkSelf)
	{
		addexp = true;
	}
	int damage = e->damage;
	damage -= getDefend();
	damage = damage > 10 ? damage : 10;
	int evd = getEvade() - e->evade;
	if (evd < 0)
	{
		evd = 0;
	}

	if (engine->getRand(convert_min(100, evd + 50)) > evd)
	{
		if (shieldLife > 0)
		{
			if (damage > shieldLife)
			{
				damage -= shieldLife;
				shieldLife = 0;
			}
			else
			{
				shieldLife -= damage;
				damage = 0;
				return;
			}
		}

        addLife(-damage);
		if (life <= 0)
		{
			life = 0;
			if (addexp)
			{
				gm->player->addExp(exp);
				gm->magicManager.addPracticeExp(exp);
				gm->magicManager.addUseExp(e, exp);
			}
		}
		else
		{
			int d = calDirection(atan2(e->flyingDirection.x, -e->flyingDirection.y));
			Point fd = gm->map->getSubPoint(position, d);
			if (engine->getRand(100) > evd)
			{
				if (!frozen && e->magic.level[e->level].moveKind == 2 && e->magic.level[e->level].specialKind == 1)
				{
					frozen = true;
					frozenLastTime = (e->level + 1) * 500;
				}
				else if (!frozen && engine->getRand(100) > getEvade())
				{
					beginHurt(fd);
				}
			}
		}
	}
}

void Player::hurtLife(int damage)
{
	damage -= defend;
    if (shieldLife > 0)
	{
		if (damage > shieldLife)
		{
			damage -= shieldLife;
			shieldLife = 0;
		}
		else
		{
			shieldLife -= damage;
			return;
		}
	}
	if (damage > 0)
	{
		addLife(-damage);
	}
}

void Player::addExp(int aExp)
{
	exp += aExp;
	bool up = false;
	while (exp >= levelUpExp && level < levelList.levelCount)
	{
		updateLevel();
		up = true;
	}
	if (up)
	{
		levelUp();
	}		
}

void Player::levelUp()
{
	int imgIdx = engine->getRand(5);
	std::shared_ptr<Effect> e = std::make_shared<Effect>();
	e->level = 1;
	e->magic.level[e->level].moveKind = mmkSelf;
	e->magic.level[e->level].specialKind = -1;
	switch (imgIdx)
	{
	case 0:
		e->magic.flyingImage = u8"levelup-白色气团a.mpc";
		break;
	case 1:
		e->magic.flyingImage = u8"levelup-白色气团b.mpc";
		break;
	case 2:
		e->magic.flyingImage = u8"levelup-蓝白色星星.mpc";
		break;
	case 3:
		e->magic.flyingImage = u8"levelup-蓝色星星.mpc";
		break;
	case 4:
		e->magic.flyingImage = u8"levelup-三连星.mpc";
		break;
	default:
		break;
	}
	e->user = gm->player;
	e->magic.createRes();
	e->doing = ekExploding;
	e->lifeTime = e->getExplodinUTime();
	e->flyingDirection = { 0, 0 };
	e->position = gm->player->position;
	e->offset = gm->player->offset;
	gm->effectManager->addEffect(e);
	e->beginTime = e->getTime();
	gm->showMessage(convert::formatString(u8"%s的等级得到提升！", npcName.c_str()));
}

void Player::addNextAction(NextAction& act)
{

	if (act.action == acRun || act.action == acWalk || act.action == acARun || act.action == acAWalk)
	{
		if (act.type == ndNone && !gm->map->canWalk(act.dest))
		{
			return;
		}
	}

	nextAction = std::make_shared<NextAction>();
	nextAction->action = act.action;
	nextAction->dest = act.dest;
	nextAction->type = act.type;
	nextAction->destGE = act.destGE;
}


void Player::changeWalk(Point dest)
{
	if (!canDoAction(acWalk))
	{
		return;
	}
	if (nextDest != ndNone && destGE != nullptr
		&& ((nextDest == ndAttack && gm->npcManager->findNPC(std::dynamic_pointer_cast<NPC>(destGE)) && Map::calDistance(position, (std::dynamic_pointer_cast<NPC>(destGE))->position) <= attackRadius)
			|| (nextDest == ndTalk && gm->npcManager->findNPC(std::dynamic_pointer_cast<NPC>(destGE)) && Map::calDistance(position, (std::dynamic_pointer_cast<NPC>(destGE))->position) <= (std::dynamic_pointer_cast<NPC>(destGE))->dialogRadius)
			|| (nextDest == ndObj && gm->objectManager->findObj(std::dynamic_pointer_cast<Object>(destGE)) && Map::calDistance(position, (std::dynamic_pointer_cast<Object>(destGE))->position) <= 1))) 
	{
		int nd = nextDest;
		nextDest = ndNone;

		if (nd == ndObj)
		{
			std::shared_ptr<Object> tempObj = std::dynamic_pointer_cast<Object>(destGE);
			destGE = nullptr;
			beginStand();
			runObj(tempObj);
		}
		else
		{
			std::shared_ptr<NPC> tempNPC = std::dynamic_pointer_cast<NPC>(destGE);
			destGE = nullptr;
			beginStand();
			if (nd == ndAttack)
			{
				beginAttack(tempNPC->position, tempNPC);
			}
			else
			{
				talkTo(tempNPC);
			}
		}
	}
	else
	{
		auto tempList = gm->map->getPath(position, dest);
		if (tempList.size() > 0)
		{
			stepList = tempList;
			direction = calDirection(stepList[0]);
			stepState = ssOut;
			stepBeginTime += stepLastTime;
			nowAction = acWalk;
			calStepLastTime();
			gm->map->addStepToDataMap(stepList[0], std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
			if (fight > 0 && canDoAction(acAWalk))
			{
				nowAction = acAWalk;
				actionLastTime = getActionTime(acAWalk);
			}
			else
			{
				fight = 0;
				nowAction = acWalk;
				actionLastTime = getActionTime(acWalk);
			}
			calOffset(getUpdateTime() - stepBeginTime, stepLastTime);
		}
		else
		{
			beginStand();
		}
	}
}

void Player::changeRun(Point dest)
{
	if (!canRun || !canDoAction(acRun))
	{
		return;
	}
	if (nextDest != ndNone && destGE != nullptr
		&& ((nextDest == ndAttack && gm->npcManager->findNPC(std::dynamic_pointer_cast<NPC>(destGE)) && Map::calDistance(position, (std::dynamic_pointer_cast<NPC>(destGE))->position) <= attackRadius)
			|| (nextDest == ndTalk && gm->npcManager->findNPC(std::dynamic_pointer_cast<NPC>(destGE)) && Map::calDistance(position, (std::dynamic_pointer_cast<NPC>(destGE))->position) <= (std::dynamic_pointer_cast<NPC>(destGE))->dialogRadius)
			|| (nextDest == ndObj && gm->objectManager->findObj(std::dynamic_pointer_cast<Object>(destGE)) && Map::calDistance(position, (std::dynamic_pointer_cast<Object>(destGE))->position) <= 1))) 
	{
		int nd = nextDest;
		nextDest = ndNone;

		if (nd == ndObj)
		{
			std::shared_ptr<Object> tempObj = std::dynamic_pointer_cast<Object>(destGE);
			destGE = nullptr;
			beginStand();
			runObj(tempObj);
		}
		else
		{
			std::shared_ptr<NPC> tempNPC = std::dynamic_pointer_cast<NPC>(destGE);
			destGE = nullptr;
			beginStand();
			if (nd == ndAttack)
			{
				beginAttack(tempNPC->position, tempNPC);
			}
			else
			{
				talkTo(tempNPC);
			}
		}
	}
	else
	{
		auto tempList = gm->map->getPath(position, dest);
		if (tempList.size() > 0)
		{
			if (!gm->inEvent)
			{
				if (thew < RUN_THEW_COST)
				{
					gm->showMessage(u8"体力不足！");
					if (isRunning())
					{
						recoveryTime = getUpdateTime();
					}
					beginStand();
					return;
				}
				thew -= RUN_THEW_COST;
			}		
			stepList = tempList;
			direction = calDirection(stepList[0]);
			stepState = ssOut;
			stepBeginTime += stepLastTime;
			nowAction = acRun;
			calStepLastTime();
			gm->map->addStepToDataMap(stepList[0], std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
			if (fight > 0 && canDoAction(acARun))
			{
				nowAction = acARun;
				actionLastTime = getActionTime(acARun);
			}
			else
			{
				fight = 0;
				nowAction = acRun;
				actionLastTime = getActionTime(acRun);
			}
			calOffset(getUpdateTime() - stepBeginTime, stepLastTime);
		}
		else
		{
			beginStand();
			recoveryTime = getUpdateTime();
		}
	}
}

void Player::standUp()
{
	recoveryTime = getUpdateTime();
	beginStand();
}

void Player::runObj(std::shared_ptr<Object> obj)
{
	if (obj == nullptr)
	{
		return;
	}
	beginStand();
	direction = calDirection(position, obj->position);

	if (obj->scriptFile != "")
	{
		gm->runObjScript(obj);
	}
}

void Player::talkTo(std::shared_ptr<NPC> npc)
{
	if (npc == nullptr)
	{
		return;
	}
	beginStand();
	npc->beginStand();
	direction = calDirection(position, npc->position);
	int tempDir = npc->direction;
	npc->direction = direction + 4;
	if (npc->direction > 7)
	{
		npc->direction -= 8;
	}
	if (npc->scriptFile != "")
	{
		gm->runNPCScript(npc);
	}
	if (gm->npcManager->findNPC(npc) && npc->action == naNone && npc->isStanding())
	{
		npc->direction = tempDir;
	}
}

void Player::beginStand()
{
	destGE = nullptr;
	clearStep();
	stepList.resize(0);
	offset = { 0, 0 };
	actionBeginTime = getUpdateTime();
	if (!isWalking() || !isStanding())
	{
		recoveryTime = getUpdateTime();
	}
	if (fight > 0 && canDoAction(acAStand))
	{
		nowAction = acAStand;
		actionLastTime = getActionTime(acAStand);
		playSound(acAStand);
	}
	else
	{
		fight = 0;
		nowAction = acStand;
		actionLastTime = getActionTime(acStand);
		playSound(acStand);
	}
}

void Player::beginWalk(Point dest)
{
	if (!canDoAction(acWalk) || frozen)
	{
		return;
	}
	if (nextDest != ndNone && destGE != nullptr
		&& ((nextDest == ndAttack && gm->npcManager->findNPC(std::dynamic_pointer_cast<NPC>(destGE)) && Map::calDistance(position, (std::dynamic_pointer_cast<NPC>(destGE))->position) <= attackRadius)
			|| (nextDest == ndTalk && gm->npcManager->findNPC(std::dynamic_pointer_cast<NPC>(destGE)) && Map::calDistance(position, (std::dynamic_pointer_cast<NPC>(destGE))->position) <= (std::dynamic_pointer_cast<NPC>(destGE))->dialogRadius)
			|| (nextDest == ndObj && gm->objectManager->findObj(std::dynamic_pointer_cast<Object>(destGE)) && Map::calDistance(position, (std::dynamic_pointer_cast<Object>(destGE))->position) <= 1))) 
	{
		int nd = nextDest;
		nextDest = ndNone;

		if (nd == ndObj)
		{
			std::shared_ptr<Object> tempObj = std::dynamic_pointer_cast<Object>(destGE);
			destGE = nullptr;
			beginStand();
			runObj(tempObj);
		}
		else
		{
			std::shared_ptr<NPC> tempNPC = std::dynamic_pointer_cast<NPC>(destGE);
			destGE = nullptr;
			beginStand();
			if (nd == ndAttack)
			{
				beginAttack(tempNPC->position, tempNPC);
			}
			else
			{
				talkTo(tempNPC);
			}
		}
	}
	else
	{
		auto tempList = gm->map->getPath(position, dest);
		if (tempList.size() > 0)
		{
			if (!gm->map->canWalk(tempList[0]))
			{
				tempList.resize(0);
				beginStand();
				return;
			}
			if (!isWalking() || !isStanding())
			{
				recoveryTime = getUpdateTime();
			}
			stepList = tempList;
			direction = calDirection(stepList[0]);
			stepState = ssOut;
			nowAction = acWalk;
			calStepLastTime();
			stepBeginTime = getUpdateTime();
			actionBeginTime = getUpdateTime();
			gm->map->addStepToDataMap(stepList[0], std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
			if (fight > 0 && canDoAction(acAWalk))
			{
				nowAction = acAWalk;
				actionLastTime = getActionTime(acAWalk);
				playSound(acAWalk);
			}
			else
			{
				fight = 0;
				nowAction = acWalk;
				actionLastTime = getActionTime(acWalk);
				playSound(acWalk);
			}
		}
	}
	
}

void Player::beginMagic(Point dest, std::shared_ptr<GameElement> target)
{
	if (!canFight || !canDoAction(acMagic) || frozen)
	{
		return;
	}
	attackTarget = target;
	attackDone = false;
	clearStep();
	stepList.resize(0);
	offset = { 0, 0 };
	direction = calDirection(dest);
	actionBeginTime = getUpdateTime();
	nowAction = acMagic;
	actionLastTime = getActionTime(acMagic);
	fight = 1;
	fightTime = getUpdateTime();
	playSound(acMagic);
}

void Player::beginJump(Point dest)
{
	if (!canJump || !canDoAction(acJump) || frozen)
	{
		return;
	}
	if (!gm->inEvent)
	{
		if (thew < JUMP_THEW_COST)
		{
			gm->showMessage(u8"体力不足！");
			return;
		}
		thew -= JUMP_THEW_COST;
	}	
	Point step = gm->map->getJumpPath(position, dest);
	stepList.resize(1);
	stepList[0] = step;
	direction = calDirection(stepList[0]);
	actionBeginTime = getUpdateTime();
	gm->map->addStepToDataMap(stepList[0], std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
	if (fight > 0 && canDoAction(acAJump))
	{
		nowAction = acAJump;
		actionLastTime = getActionTime(acAJump);
		playSound(acAJump);
	}
	else
	{
		fight = 0;
		nowAction = acJump;
		actionLastTime = getActionTime(acJump);
		playSound(acJump);
	}
	flyingDirection = Map::getTilePosition(step, position, { 0, 0 }, { 0, 0 });
	jumpSpeed = hypot((double)flyingDirection.x / TILE_WIDTH, (double)flyingDirection.y / TILE_WIDTH) / (double)(actionLastTime / 3) / Config::getGameSpeed();
	jumpState = 0;
}

void Player::beginRun(Point dest)
{
	if (!canRun || !canDoAction(acRun) || frozen)
	{
		return;
	}
	if (nextDest != ndNone && destGE != nullptr
		&& ((nextDest == ndAttack && gm->npcManager->findNPC(std::dynamic_pointer_cast<NPC>(destGE)) && Map::calDistance(position, (std::dynamic_pointer_cast<NPC>(destGE))->position) <= attackRadius)
			|| (nextDest == ndTalk && gm->npcManager->findNPC(std::dynamic_pointer_cast<NPC>(destGE)) && Map::calDistance(position, (std::dynamic_pointer_cast<NPC>(destGE))->position) <= (std::dynamic_pointer_cast<NPC>(destGE))->dialogRadius)
			|| (nextDest == ndObj && gm->objectManager->findObj(std::dynamic_pointer_cast<Object>(destGE)) && Map::calDistance(position, (std::dynamic_pointer_cast<Object>(destGE))->position) <= 1))) 
	{
		int nd = nextDest;
		nextDest = ndNone;

		if (nd == ndObj)
		{
			std::shared_ptr<Object> tempObj = std::dynamic_pointer_cast<Object>(destGE);
			destGE = nullptr;
			beginStand();
			runObj(tempObj);
		}
		else
		{
			std::shared_ptr<NPC> tempNPC = std::dynamic_pointer_cast<NPC>(destGE);
			destGE = nullptr;
			beginStand();
			if (nd == ndAttack)
			{
				beginAttack(tempNPC->position, tempNPC);
			}
			else
			{
				talkTo(tempNPC);
			}
		}
	}
	else
	{
		auto tempList = gm->map->getPath(position, dest);
		if (tempList.size() > 0)
		{
			if (!gm->map->canWalk(tempList[0]))
			{
				tempList.resize(0);
				beginStand();
				return;
			}
            if (!gm->inEvent)
            {
                if (thew < RUN_THEW_COST)
                {
                    gm->showMessage(u8"体力不足！");
                    return;
                }
                thew -= RUN_THEW_COST;
            }
			stepList = tempList;
			direction = calDirection(stepList[0]);
			stepState = ssOut;
			actionBeginTime = getUpdateTime();
			nowAction = acRun;
			calStepLastTime();
			stepBeginTime = getUpdateTime();
			actionBeginTime = getUpdateTime();
			gm->map->addStepToDataMap(stepList[0], std::dynamic_pointer_cast<NPC>(getMySharedPtr()));
			if (fight > 0 && canDoAction(acARun))
			{
				nowAction = acARun;
				actionLastTime = getActionTime(acARun);
				playSound(acARun);
			}
			else
			{
				fight = 0;
				nowAction = acRun;
				actionLastTime = getActionTime(acRun);
				playSound(acRun);
			}
		}
	}
}

void Player::beginAttack(Point dest, std::shared_ptr<GameElement> target)
{
	if (!canFight || !canDoAction(acAttack) || frozen)
	{
		return;
	}
	attackTarget = target;
	if (thew < ATTACK_THEW_COST)
	{
		gm->showMessage(u8"体力不足！");
		return;
	}
	thew -= ATTACK_THEW_COST;
    
    std::vector<int> attackList;
    attackList.push_back(0);
    
	if (canDoAction(acAttack1))
	{
        attackList.push_back(1);
	}
    
    if (canDoAction(acAttack2))
    {
        attackList.push_back(2);
    }

	int attackNum = 0;
	if (attackList.size() > 0)
	{
        attackNum = attackList[engine->getRand((int)attackList.size() - 1)];
	}

	clearStep();

	stepList.resize(0);
	offset = { 0, 0 };
	actionBeginTime = getUpdateTime();
	direction = calDirection(dest);
	attackDone = false;
	magicDest = dest;
	if (attackNum == 0)
	{
		nowAction = acAttack;
		actionLastTime = getActionTime(acAttack);
		playSound(acAttack);
	}
	else if (attackNum == 1)
	{
		nowAction = acAttack1;
		actionLastTime = getActionTime(acAttack1);
		playSound(acAttack1);
	}
	else
	{
		if (gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic != nullptr && gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic->actionImage != nullptr)
		{
			res.specialAttack.image = gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic->actionImage;
			res.specialAttack.shadow = gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic->actionImage;
			nowAction = acSpecialAttack;
			actionLastTime = getActionTime(acSpecialAttack);
			playSound(acAttack2);
		}
		else
		{
			nowAction = acAttack2;
			actionLastTime = getActionTime(acAttack2);
			playSound(acAttack2);
		}	
	}
}

void Player::beginHurt(Point dest)
{
	if (isHurting() || !canDoAction(acHurt) || !canHurt() || frozen)
	{
		return;
	}
	destGE = nullptr;
	clearStep();
	stepList.resize(0);
	offset = { 0, 0 };
	direction = calDirection(dest);
	actionBeginTime = getUpdateTime();
	nowAction = acHurt;
	actionLastTime = getActionTime(acHurt);
	fight = 1;
	fightTime = getUpdateTime();
	playSound(acHurt);
}

bool Player::canHurt()
{
	if (isJumping() && (jumpState == jsJumping || (jumpState == jsDown && gm->map->haveTraps(position))))
	{
		return false;
	}
	else if ((isWalking() || isRunning()) && stepState == ssIn && gm->map->haveTraps(position))
	{
		return false;
	}
	return true;
}

void Player::checkTrap()
{
	if (gm->map->haveTraps(position))
	{
		gm->runTrapScript(gm->map->getTrapIndex(position));
	}
}

void Player::limitAttribute()
{
	if (life > info.lifeMax)
	{
		life = info.lifeMax;
	}
	if (thew > info.thewMax)
	{
		thew = info.thewMax;
	}
	if (mana > info.manaMax)
	{
		mana = info.manaMax;
	}
}

void Player::loadLevel(const std::string& fileName)
{
	levelList.levelCount = 0;
	levelList.level.resize(0);
	std::string iniName = LEVEL_FOLDER + fileName;
	std::unique_ptr<char[]> s;
	int len = PakFile::readFile(iniName, s);
	kind = nkPlayer;
	if (s != nullptr && len > 0)
	{
		INIReader ini(s);
		std::string section = "Head";
		levelList.levelCount = ini.GetInteger(section, "Levels", 0);
		levelList.level.resize(levelList.levelCount);
		
		for (size_t i = 0; i < levelList.level.size(); i++)
		{
			std::string section = convert::formatString("Level%d", i + 1);
			levelList.level[i].levelUpExp = ini.GetInteger(section, "LevelUpExp", 0);
			levelList.level[i].lifeMax = ini.GetInteger(section, "LifeMax", 0);
			levelList.level[i].thewMax = ini.GetInteger(section, "ThewMax", 0);
			levelList.level[i].manaMax = ini.GetInteger(section, "ManaMax", 0);
			levelList.level[i].attack = ini.GetInteger(section, "Attack", 0);
			levelList.level[i].defend = ini.GetInteger(section, "Defend", 0);
			levelList.level[i].evade = ini.GetInteger(section, "Evade", 0);
			levelList.level[i].newMagic = ini.Get(section, "NewMagic", "");
		}
	}

}

void Player::doSpecialAttack(Point dest, std::shared_ptr<GameElement> target)
{
	if (gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic == nullptr || gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic->specialMagic == nullptr)
	{
		doAttack(dest, target);
		return;
	}
	int launcher = 0;
	if (kind == nkPlayer)
	{
		launcher = lkSelf;
	}
	else
	{
		if (relation == nrFriendly)
		{
			launcher = lkFriend;
		}
		else if (relation == nrHostile)
		{
			launcher = lkEnemy;
		}
		else if (relation == nrNeutral)
		{
			launcher = lkNeutral;
		}
	}
	Magic::addEffect(gm->magicManager.magicList[MAGIC_COUNT + MAGIC_TOOLBAR_COUNT].magic->specialMagic, std::dynamic_pointer_cast<NPC>(getMySharedPtr()), position, dest, attackLevel, attack, evade, launcher, target);
}

void Player::drawAlpha(Point cenTile, Point cenScreen, PointEx coffset)
{
	Point tile = position;
	Point pos = Map::getTilePosition(tile, cenTile, cenScreen, coffset);
	int offsetX, offsetY;
	_shared_image image = getActionImage(&offsetX, &offsetY);
	engine->setImageAlpha(image, 128);
	engine->drawImage(image, pos.x + (int)round(offset.x) - offsetX, pos.y + (int)round(offset.y) - offsetY);
	engine->setImageAlpha(image, 255);
}

void Player::draw(Point cenTile, Point cenScreen, PointEx coffset, uint32_t colorStyle)
{
	Point tile = position;
	Point pos = Map::getTilePosition(tile, cenTile, cenScreen, coffset);
	int offsetX, offsetY;
	_shared_image image = getActionShadow(&offsetX, &offsetY);
	engine->drawImage(image, pos.x + (int)round(offset.x) - offsetX, pos.y + (int)round(offset.y) - offsetY);
	image = getActionImage(&offsetX, &offsetY);
	if ((colorStyle & 0xFFFFFF) == 0xFFFFFF)
	{
		engine->drawImage(image, pos.x + (int)round(offset.x) - offsetX, pos.y + (int)round(offset.y) - offsetY);
	}
	else
	{
		engine->drawImageWithColor(image, pos.x + (int)round(offset.x) - offsetX, pos.y + (int)round(offset.y) - offsetY, (colorStyle >> 16) & 0xFF, (colorStyle >> 8) & 0xFF, colorStyle & 0xFF);
	}
	
}

void Player::playSound(int act)
{
	switch (act)
	{
	case acStand:
		playSoundFile(res.stand.soundFile);
		break;
	case acStand1:
		playSoundFile(res.stand1.soundFile);
		break;
	case acWalk:
		playSoundFile(res.walk.soundFile);
		break;
	case acRun:
		playSoundFile(res.run.soundFile);
		break;
	case acJump:
		playSoundFile(res.jump.soundFile);
		break;
	case acAttack:
		playSoundFile(res.attack.soundFile);
		break;
	case acAttack1:
		playSoundFile(res.attack1.soundFile);
		break;
	case acAttack2:
		playSoundFile(res.attack2.soundFile);
		break;
	case acMagic:
		playSoundFile(res.magic.soundFile);
		break;
	case acHurt:
		playSoundFile(res.hurt.soundFile);
		break;
	case acDeath:
		playSoundFile(res.death.soundFile);
		break;
	case acSit:
		playSoundFile(res.sit.soundFile);
		break;
	case acSpecial:
		playSoundFile(res.special.soundFile);
		break;
	case acSitting:
		break;
	case acAStand:
		playSoundFile(res.astand.soundFile);
		break;
	case acAWalk:
		playSoundFile(res.awalk.soundFile);
		break;
	case acARun:
		playSoundFile(res.arun.soundFile);
		break;
	case acAJump:
		playSoundFile(res.ajump.soundFile);
		break;
	default:
		break;
	}
}

void Player::checkDie()
{
	beginDie();
}

void Player::beginDie()
{
	if (nowAction == acDeath || nowAction == acHide)
	{
		return;
	}
	if (!canDoAction(acDeath))
	{
		return;
	}
	clearStep();
	stepList.resize(0);
	offset = { 0, 0 };
	playSound(acDeath);
	nowAction = acDeath;
	actionLastTime = getActionTime(acDeath);
	actionBeginTime = getUpdateTime();
	if (deathScript != "")
	{
		result |= erRunDeathScript;
	}
}

void Player::load(int index)
{
	freeResource();
	std::string fName = SAVE_CURRENT_FOLDER;
    fName += PLAYER_INI_NAME;
    if (index >= 0)
    {
        fName += convert::formatString("%d", index);
    }
    fName += PLAYER_INI_EXT;

	std::unique_ptr<char[]> s;
	int len = PakFile::readFile(fName, s);

	if (len <= 0 || s == nullptr)
	{
		return;
	}
	INIReader ini(s);

	std::string section = "Init";
	initFromIni(&ini, section);

	magic = ini.GetInteger(section, "Magic", 0);
	money = ini.GetInteger(section, "Money", 0);
	canRun = ini.GetBoolean(section, "CanRun", true);
	canJump = ini.GetBoolean(section, "CanJump", true);
	canFight = ini.GetBoolean(section, "CanFight", true);
	fight = ini.GetInteger(section, "Fight", 0);
	levelIni = ini.Get(section, "LevelIni", "");

	loadLevel(levelIni);
	nowAction = acStand;
	
}

void Player::save(int index)
{
	INIReader ini;
	std::string section = "Init";

	Point tempPos = position;
	if (isJumping() && jumpState == jsJumping)
	{
		position = stepList[0];
	}

	saveToIni(&ini, section);

	position = tempPos;

	ini.SetInteger(section, "Magic", magic);
	ini.SetInteger(section, "Money", money);
	ini.SetBoolean(section, "CanRun", canRun);
	ini.SetBoolean(section, "CanJump", canJump);
	ini.SetBoolean(section, "CanFight", canFight);
	ini.SetInteger(section, "Fight", fight);
	ini.Set(section, "LevelIni", levelIni);

	std::string fName = PLAYER_INI_NAME;
    if (index >= 0)
    {
        fName += convert::formatString("%d", index);
    }
    fName += PLAYER_INI_EXT;
	ini.saveToFile(SaveFileManager::CurrentPath() + fName);
    
    SaveFileManager::AppendFile(fName);
}
