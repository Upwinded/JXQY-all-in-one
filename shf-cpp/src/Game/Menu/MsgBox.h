#pragma once
#include "../../Component/Panel.h"
class MsgBox :
	public Panel
{
public:
	MsgBox();
	~MsgBox();

	Label * label = NULL;
	bool showed = false;
	unsigned int beginTime = 0;
	unsigned int showingTime = 3500;
	void showMessage(const std::wstring & wstr);
	virtual void onUpdate();
	virtual void init();
	virtual void freeResource();
};

