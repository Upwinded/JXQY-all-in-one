#pragma once
#include "../../Component/Panel.h"
class MemoMenu :
	public Panel
{
public:
	MemoMenu();
	virtual ~MemoMenu();

	ImageContainer * title = nullptr;
	ImageContainer * image = nullptr;
	MemoText * memoText = nullptr;
	Scrollbar * scrollbar = nullptr;

	void reFresh();
	void reset();
	void reRange(int max);

private:
	int position = -1;

	void init();
	void freeResource();

	virtual void onEvent();
};

