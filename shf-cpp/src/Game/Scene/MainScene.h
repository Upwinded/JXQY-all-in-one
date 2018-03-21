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
	~MainScene();

	GameManager * game = NULL;

private:
	int gameIndex = -1;
	virtual bool onInitial();
	virtual void onUpdate();

};


