#include "ToolTip.h"
//#include "../GameManager/GameManager.h"

ToolTip::ToolTip()
{
	visible = false;
	//coverMouse = false;
	init();
}

ToolTip::~ToolTip()
{
	freeResource();
}

void ToolTip::setGoods(std::shared_ptr<Goods> goods)
{
	if (goods == nullptr)
	{
		return;
	}
	std::string imageName = GOODS_RES_FOLDER + goods->image;

	image->impImage = nullptr;

	image->impImage = IMP::createIMPImage(imageName);
	name->setStr(goods->name);
	std::string costStr = "Price:";
	costStr += convert::formatString("%d", goods->cost);
	cost->setStr(costStr);
	intro1->setStr(goods->effect);
	std::string introStr = " <enter>" + goods->intro;
	intro2->setStr(introStr);
}

void ToolTip::setMagic(std::shared_ptr<Magic> magic, int level)
{
	if (magic == nullptr)
	{
		return;
	}
	std::string imageName = MAGIC_RES_FOLDER + magic->image;

	image->impImage = nullptr;

	image->impImage = IMP::createIMPImage(imageName);
	name->setStr(magic->name);
	std::string costStr = "Level:";
	costStr += convert::formatString("%d", level);
	cost->setStr(costStr);
	intro1->setStr("");
	std::string introStr = magic->intro;
	intro2->setStr(introStr);
}

void ToolTip::init()
{
	freeResource();
	initFromIniFileName("ini\\ui\\tooltip\\window.ini");
	image = addComponent<ImageContainer>("ini\\ui\\tooltip\\image.ini");
	image->stretch = true;
	intro1 = addComponent<Label>("ini\\ui\\tooltip\\intro1.ini");
	intro2 = addComponent<Label>("ini\\ui\\tooltip\\intro2.ini");
	intro2->rect.y = intro1->rect.y;
	intro2->autoNextLine = true;
	name = addComponent<Label>("ini\\ui\\tooltip\\name.ini");
	cost = addComponent<Label>("ini\\ui\\tooltip\\cost.ini");
	setChildRectReferToParent();
}

void ToolTip::freeResource()
{

	impImage = nullptr;

	freeCom(name);
	freeCom(cost);
	freeCom(intro1);
	freeCom(intro2);
	freeCom(image);
}
