#pragma once
#include "../Element/Element.h"
#include "Label.h"
#define MEMO_LINE 5
class MemoText :
	public Element
{
public:
	MemoText();
	~MemoText();

	int fontSize = 16;
	unsigned int color = 0xFFFFFFFF;
	int lineSize = 30;

	//std::vector<std::wstring> wstr = {};
	std::vector<Label *> mstr;
	virtual void initFromIni(const std::string & fileName);

};

