#pragma once
#include "../../Component/Component.h"
class StateMenu :
	public Panel
{
public:
	StateMenu();
	virtual ~StateMenu();

	virtual void updateLabel();

	virtual void init();

	ImageContainer * image = nullptr;
	ImageContainer * title = nullptr;

	Label * labLevel = nullptr;
	Label * labExp = nullptr;
	Label * labExpUp = nullptr;
	Label * labAttack = nullptr;
	Label * labDefend = nullptr;
	Label * labEvade = nullptr;
	Label * labLife = nullptr;
	Label * labThew = nullptr;
	Label * labMana = nullptr;
	
private:
	virtual void onEvent();
	void freeResource();
};

