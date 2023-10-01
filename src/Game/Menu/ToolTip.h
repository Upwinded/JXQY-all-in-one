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

	std::shared_ptr<ImageContainer> image = nullptr;
	std::shared_ptr<Label> name = nullptr;
	std::shared_ptr<Label> cost = nullptr;
	std::shared_ptr<Label> intro1 = nullptr;
	std::shared_ptr<Label> intro2 = nullptr;

	void setGoods(std::shared_ptr<Goods> goods);
	void setMagic(std::shared_ptr<Magic> magic, int level);
	void init();

private:
	void freeResource();

};

