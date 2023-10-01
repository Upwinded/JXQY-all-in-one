#include "MemoText.h"


MemoText::MemoText()
{
	priority = epMenu;
	name = "Memo";
	elementType = etMenu;
	canDrag = true;
	mstr.resize(MEMO_LINE);
	for (size_t i = 0; i < MEMO_LINE; i++)
	{
		mstr[i] = std::make_shared<Label>();
		addChild(mstr[i]);
	}
}

MemoText::~MemoText()
{
	freeResource();
}

void MemoText::initFromIni(INIReader & ini)
{
	freeResource();
	mstr.resize(MEMO_LINE);
	for (size_t i = 0; i < MEMO_LINE; i++)
	{
		mstr[i] = std::make_shared<Label>();
		addChild(mstr[i]);
	}

	rect.x = ini.GetInteger("Init", "Left", rect.x);
	rect.y = ini.GetInteger("Init", "Top", rect.y);
	rect.w = ini.GetInteger("Init", "Width", rect.w);
	rect.h = ini.GetInteger("Init", "Height", rect.h);
	name = ini.Get("Init", "Name", name);
	fontSize = ini.GetInteger("Init", "Font", fontSize);
	color = ini.GetColor("Init", "Color", color);
	for (size_t i = 0; i < MEMO_LINE; i++)
	{
		mstr[i]->rect.x = 0;
		mstr[i]->rect.y = i * lineSize;
		mstr[i]->rect.w = rect.w;
		mstr[i]->rect.h = lineSize;
		mstr[i]->fontSize = fontSize;
		mstr[i]->color = color;
	}
}

void MemoText::freeResource()
{
	for (size_t i = 0; i < MEMO_LINE; i++)
	{
		mstr[i] = nullptr;
	}
	mstr.resize(0);
	removeAllChild();
}
