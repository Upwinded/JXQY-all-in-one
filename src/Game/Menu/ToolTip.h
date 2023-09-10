#pragma once
#include "../../Component/Component.h"
#include "../Data/Goods.h"
#include "../Data/Magic.h"

class ToolTip :
	public Panel
{
public:
	ToolTip();
	virtual ~ToolTip();

	ImageContainer * image = nullptr;
	Label * name = nullptr;
	Label * cost = nullptr;
	Label * intro1 = nullptr;
	Label * intro2 = nullptr;

	void setGoods(Goods * goods);
	void setMagic(Magic * magic, int level);
	void init();

private:
	void freeResource();

};

