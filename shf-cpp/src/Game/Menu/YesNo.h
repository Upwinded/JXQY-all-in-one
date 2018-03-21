#pragma once
#include "../../Component/Component.h"
class YesNo :
	public Panel
{
public:
	YesNo(const std::wstring & ws = L"");
	~YesNo();

	Button * yes = NULL;
	Button * no = NULL;
	Label * label = NULL;

	void init(const std::wstring & ws = L"");

private:

	virtual void freeResource();
	virtual void onEvent();
	virtual bool onHandleEvent(AEvent * e);
};

