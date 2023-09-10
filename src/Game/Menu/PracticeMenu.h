#pragma once
#include "../../Component/Component.h"

class PracticeMenu :
	public Panel
{
public:
	PracticeMenu();
	virtual ~PracticeMenu();

	ImageContainer * image = nullptr;
	ImageContainer * title = nullptr;

	Label * name = nullptr;
	Label * intro = nullptr;
	Label * level = nullptr;
	Label * exp = nullptr;

	Item * magic = nullptr;

	void updateMagic();
	void updateExp();
	void updateLevel();

private:
	virtual void onEvent();
	virtual void init();
	void freeResource();
};

