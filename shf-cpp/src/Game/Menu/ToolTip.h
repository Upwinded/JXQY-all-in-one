#pragma once
#include "../../Component/Component.h"
#include "../Data/Goods.h"
#include "../Data/Magic.h"

class ToolTip :
	public Panel
{
public:
	ToolTip();
	~ToolTip();

	ImageContainer * image = NULL;
	Label * name = NULL;
	Label * cost = NULL;
	Label * intro1 = NULL;
	Label * intro2 = NULL;

	void setGoods(Goods * goods);
	void setMagic(Magic * magic, int level);
	void init();

private:
	virtual void freeResource();

};

