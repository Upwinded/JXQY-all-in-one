#pragma once
#include "../Element/Element.h"
#include "Scene/Title.h"


class Game
{
public:
	Game();
	virtual ~Game();

	std::string gameTitle = u8"Sword Heroes' Fate -- By Upwinded";
	std::string gameFont = u8"font\\font.ttf";

public:

	int run();

};

