#pragma once
#include "../../Component/Component.h"
class StateMenu :
	public Panel
{
public:
	StateMenu();
	~StateMenu();

	virtual void updateLabel();

	virtual void init();

	ImageContainer * image = NULL;
	ImageContainer * title = NULL;

	Label * labLevel = NULL;
	Label * labExp = NULL;
	Label * labExpUp = NULL;
	Label * labAttack = NULL;
	Label * labDefend = NULL;
	Label * labEvade = NULL;
	Label * labLife = NULL;
	Label * labThew = NULL;
	Label * labMana = NULL;
	
private:
	virtual void onEvent();
	virtual void freeResource();
};

