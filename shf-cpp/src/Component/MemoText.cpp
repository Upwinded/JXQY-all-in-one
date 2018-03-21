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
		mstr[i] = new Label;
		addChild(mstr[i]);
	}
}

MemoText::~MemoText()
{
	freeResource();
	for (size_t i = 0; i < MEMO_LINE; i++)
	{
		delete mstr[i];
		mstr[i] = NULL;
	}
	mstr.resize(0);
	removeAllChild();
}

void MemoText::initFromIni(const std::string & fileName)
{
	freeResource();
	char * s = NULL;
	int len = 0;
	len = PakFile::readFile(fileName, &s);
	if (s == NULL || len == 0)
	{
		printf("no ini file: %s\n", fileName.c_str());
		return;
	}
	INIReader ini = INIReader::INIReader(s);
	delete[] s;
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
