#pragma once
#include "../../Component/Component.h"

class PracticeMenu :
	public Panel
{
public:
	PracticeMenu();
	virtual ~PracticeMenu();

	std::shared_ptr<ImageContainer> image = nullptr;
	std::shared_ptr<ImageContainer> title = nullptr;

	std::shared_ptr<Label> name = nullptr;
	std::shared_ptr<Label> intro = nullptr;
	std::shared_ptr<Label> level = nullptr;
	std::shared_ptr<Label> exp = nullptr;

	std::shared_ptr<Item> magic = nullptr;

	void updateMagic();
	void updateExp();
	void updateLevel();

private:
	virtual void onEvent();
	virtual void init();
	void freeResource();
};

