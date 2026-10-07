#pragma once
#include "Button.h"
#include "Label.h"
class CheckBox :
	public Button
{
public:
	bool hasText() const { return !textLabel.getStr().empty(); }
	CheckBox();
	virtual ~CheckBox();

	bool checked = false;

	virtual void initFromIni(INIReader & ini);

private:
	Label textLabel;
	virtual void onDraw();
	virtual void onClick();

	virtual void onMouseLeftDown(int x, int y);
	virtual void onMouseLeftUp(int x, int y);
};
