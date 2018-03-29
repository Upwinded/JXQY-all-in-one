#include "ToolTip.h"
//#include "../GameManager/GameManager.h"

ToolTip::ToolTip()
{
	init();
	visible = false;
	//coverMouse = false;
}

ToolTip::~ToolTip()
{
	freeResource();
}

void ToolTip::setGoods(Goods * goods)
{
	if (goods == NULL)
	{
		return;
	}
	std::string imageName = GOODS_RES_FOLDER + goods->image;
	if (image->impImage != NULL)
	{
		IMP::clearIMPImage(image->impImage);
		delete image->impImage;
		image->impImage = NULL;
	}
	image->impImage = IMP::createIMPImage(imageName);
	name->setStr(convert::GBKToUnicode(goods->name));
	std::wstring costStr = L"价格：";
	costStr += convert::GBKToUnicode(convert::formatString("%d", goods->cost));
	cost->setStr(costStr);
	intro1->setStr(convert::GBKToUnicode(goods->effect));
	std::wstring introStr = L" <enter>" + convert::GBKToUnicode(goods->intro);
	intro2->setStr(introStr);
}

void ToolTip::setMagic(Magic * magic, int level)
{
	if (magic == NULL)
	{
		return;
	}
	std::string imageName = MAGIC_RES_FOLDER + magic->image;
	if (image->impImage != NULL)
	{
		IMP::clearIMPImage(image->impImage);
		delete image->impImage;
		image->impImage = NULL;
	}
	image->impImage = IMP::createIMPImage(imageName);
	name->setStr(convert::GBKToUnicode(magic->name));
	std::wstring costStr = L"等级：";
	costStr += convert::GBKToUnicode(convert::formatString("%d", level));
	cost->setStr(costStr);
	intro1->setStr(L"");
	std::wstring introStr = convert::GBKToUnicode(magic->intro);
	intro2->setStr(introStr);
}

void ToolTip::init()
{
	freeResource();
	initFromIni("ini\\ui\\tooltip\\window.ini");
	image = addImageContainer("ini\\ui\\tooltip\\Image.ini");
	image->stretch = true;
	intro1 = addLabel("ini\\ui\\tooltip\\Intro1.ini");
	intro2 = addLabel("ini\\ui\\tooltip\\Intro2.ini");
	intro2->rect.y = intro1->rect.y;
	intro2->autoNextLine = true;
	name = addLabel("ini\\ui\\tooltip\\Name.ini");
	cost = addLabel("ini\\ui\\tooltip\\Cost.ini");
	setChildRect();
}

void ToolTip::freeResource()
{
	if (impImage != NULL)
	{
		IMP::clearIMPImage(impImage);
		delete impImage;
		impImage = NULL;
	}
	freeCom(name);
	freeCom(cost);
	freeCom(intro1);
	freeCom(intro2);
	freeCom(image);
}
