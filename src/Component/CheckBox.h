#pragma once
#include "Button.h"
class CheckBox :
	public Button
{
public:
	CheckBox();
	virtual ~CheckBox();

	bool checked = false;

	virtual void initFromIni(INIReader & ini);

private:
	virtual void onDraw();
	virtual void onClick();

	virtual void onMouseLeftDown(int x, int y);
	virtual void onMouseLeftUp(int x, int y);
};

