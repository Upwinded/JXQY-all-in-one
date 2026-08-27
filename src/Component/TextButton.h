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


	void setFontSize(int fontSize);
	int getFontSize() const;
	void setStrColor(unsigned int color);
	unsigned int getTextColor() const;
	virtual void setStr(const std::string& s);
    virtual void setUTF8Str(const std::string& s);
	virtual void initFromIni(INIReader & ini);
	
protected:
	Label label;
	virtual void onDraw();
    virtual void onMouseLeftDown(int x, int y);
	virtual void onClick();
	

};
