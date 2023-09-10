#pragma once
#include "BaseComponent.h"
#include "Label.h"
#define MEMO_LINE 5
class MemoText :
	public BaseComponent
{
public:
	MemoText();
	virtual ~MemoText();

	int fontSize = 16;
	unsigned int color = 0xFFFFFFFF;
	int lineSize = 30;

	//std::vector<std::wstring> wstr;
	std::vector<Label *> mstr;
	virtual void initFromIni(const std::string& fileName);
protected:
	void freeResource();

};

