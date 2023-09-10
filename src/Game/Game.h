#pragma once
#include "../Element/Element.h"
#include "Scene/Title.h"


class Game
{
public:
	Game();
	virtual ~Game();

	std::string gameTitle = "Sword Heroes' Fate 2 -- By Upwinded";
	std::string gameFont = "font\\font.ttf";

public:

	int run();

};

