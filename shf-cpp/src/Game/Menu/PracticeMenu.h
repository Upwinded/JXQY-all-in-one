#pragma once
#include "../../Component/Component.h"

class PracticeMenu :
	public Panel
{
public:
	PracticeMenu();
	~PracticeMenu();

	ImageContainer * image = NULL;
	ImageContainer * title = NULL;

	Label * name = NULL;
	Label * intro = NULL;
	Label * level = NULL;
	Label * exp = NULL;

	Item * magic = NULL;

	void updateMagic();
	void updateExp();
	void updateLevel();

private:
	virtual void onEvent();
	virtual void init();
	virtual void freeResource();
};

