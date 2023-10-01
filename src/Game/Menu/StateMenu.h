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

	std::shared_ptr<ImageContainer> image = nullptr;
	std::shared_ptr<ImageContainer> title = nullptr;

	std::shared_ptr<Label> labLevel = nullptr;
	std::shared_ptr<Label> labExp = nullptr;
	std::shared_ptr<Label> labExpUp = nullptr;
	std::shared_ptr<Label> labAttack = nullptr;
	std::shared_ptr<Label> labDefend = nullptr;
	std::shared_ptr<Label> labEvade = nullptr;
	std::shared_ptr<Label> labLife = nullptr;
	std::shared_ptr<Label> labThew = nullptr;
	std::shared_ptr<Label> labMana = nullptr;
	
private:
	virtual void onEvent();
	void freeResource();
};

