#pragma once
#include "../../Component/Panel.h"
class MemoMenu :
	public Panel
{
public:
	MemoMenu();
	~MemoMenu();

	ImageContainer * title = NULL;
	ImageContainer * image = NULL;
	MemoText * memoText = NULL;
	Scrollbar * scrollbar = NULL;

	void reFresh();
	void reset();
	void reRange(int max);

private:
	int position = -1;

	void init();
	virtual void freeResource();

	virtual void onEvent();
};

