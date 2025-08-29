#include "StateMenu.h"
#include "../GameManager/GameManager.h"

StateMenu::StateMenu()
{
	visible = false;
	init();
}

StateMenu::~StateMenu()
{
	freeResource();
}

void StateMenu::updateLabel()
{
	labLevel->setStr(convert::formatString(u8"%d", gm->player->level));
	labExp->setStr(convert::formatString(u8"%d", gm->player->exp));
	labExpUp->setStr(convert::formatString(u8"%d", gm->player->levelUpExp));
	labAttack->setStr(convert::formatString(u8"%d", gm->player->info.attack));
	labDefend->setStr(convert::formatString(u8"%d", gm->player->info.defend));
	labEvade->setStr(convert::formatString(u8"%d", gm->player->info.evade));
	labLife->setStr(convert::formatString(u8"%d/%d", gm->player->life, gm->player->info.lifeMax));
	labThew->setStr(convert::formatString(u8"%d/%d", gm->player->thew, gm->player->info.thewMax));
	labMana->setStr(convert::formatString(u8"%d/%d", gm->player->mana, gm->player->info.manaMax));
}


void StateMenu::init()
{
	freeResource();
	initFromIniFileName(u8"ini\\ui\\state\\window.ini");
	image = addComponent<ImageContainer>(u8"ini\\ui\\state\\image.ini");
	title = addComponent<ImageContainer>(u8"ini\\ui\\state\\title.ini");
	labLevel = addComponent<Label>(u8"ini\\ui\\state\\lab等级.ini");
	labExp = addComponent<Label>(u8"ini\\ui\\state\\lab经验.ini");
	labExpUp = addComponent<Label>(u8"ini\\ui\\state\\lab升级.ini");
	labAttack = addComponent<Label>(u8"ini\\ui\\state\\lab攻击.ini");
	labDefend = addComponent<Label>(u8"ini\\ui\\state\\lab防御.ini");
	labEvade = addComponent<Label>(u8"ini\\ui\\state\\lab闪避.ini");
	labLife = addComponent<Label>(u8"ini\\ui\\state\\lab生命.ini");
	labThew = addComponent<Label>(u8"ini\\ui\\state\\lab体力.ini");
	labMana = addComponent<Label>(u8"ini\\ui\\state\\lab内力.ini");
	setChildRectReferToParent();
}

void StateMenu::onEvent()
{
	updateLabel();
}

void StateMenu::freeResource()
{
	impImage = nullptr;

	freeCom(image);
	freeCom(title);
	freeCom(labAttack);
	freeCom(labDefend);
	freeCom(labEvade);
	freeCom(labLevel);
	freeCom(labExp);
	freeCom(labExpUp);
	freeCom(labLife);
	freeCom(labMana);
	freeCom(labThew);

}

