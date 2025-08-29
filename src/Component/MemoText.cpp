#include "MemoText.h"


MemoText::MemoText()
{
	priority = epMenu;
	name = u8"Memo";
	elementType = etMenu;
	canDrag = true;
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

	rect.x = ini.GetInteger(u8"Init", u8"Left", rect.x);
	rect.y = ini.GetInteger(u8"Init", u8"Top", rect.y);
	rect.w = ini.GetInteger(u8"Init", u8"Width", rect.w);
	rect.h = ini.GetInteger(u8"Init", u8"Height", rect.h);
	name = ini.Get(u8"Init", u8"Name", name);
	fontSize = ini.GetInteger(u8"Init", u8"Font", fontSize);
	color = ini.GetColor(u8"Init", u8"Color", color);
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
	for (size_t i = 0; i < mstr.size(); i++)
	{
		mstr[i] = nullptr;
	}
	mstr.resize(0);
	removeAllChild();
}
