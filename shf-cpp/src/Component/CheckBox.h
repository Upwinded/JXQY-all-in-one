#pragma once
#include "Button.h"
class CheckBox :
	public Button
{
public:
	CheckBox();
	~CheckBox();

	bool checked = false;

	virtual void initFromIni(const std::string & fileName);

private:
	virtual void onDraw();
	virtual void onClick();

	virtual void onMouseLeftDown();
	virtual void onMouseLeftUp();
};

