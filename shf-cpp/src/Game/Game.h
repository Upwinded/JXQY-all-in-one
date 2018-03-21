#pragma once
#include "../Element/Element.h"
#include "Scene/Title.h"

class Game
{
public:
	Game();
	~Game();

	std::string gameTitle = "Sword Heroes' Fate 2 Ver.Alpha1.02 -- By Upwinded";
	std::string gameFont = "Font\\Font.ttf";

public:

	int run();

};

