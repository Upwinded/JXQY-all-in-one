#pragma once
#include "BaseComponent.h"
#include "Button.h"
#include "Label.h"

class TextButton :
	public Button
{
	friend class Label;
public:
	TextButton();
	virtual ~TextButton();
	Label label;

	void setFontSize(int fontSize);
	void setStrColor(unsigned int color);
	virtual void setStr(const std::string& s);
    virtual void setUTF8Str(const std::string& s);
	virtual void initFromIni(INIReader & ini);

	virtual void onDraw();
    virtual void onMouseLeftDown(int x, int y);
	virtual void onClick();

};

