#pragma once
#include "../../Component/Component.h"
#include "../Data/Data.h"
#include "../../libconvert/libconvert.h"
#include "../GameManager/GameManager.h"

class MainScene :
	public Element
{
public:
	MainScene(int index);
	virtual ~MainScene();

	std::shared_ptr<GameManager> game = nullptr;

private:
	int gameIndex = -1;
	virtual bool onInitial();
	virtual void onUpdate();

};


