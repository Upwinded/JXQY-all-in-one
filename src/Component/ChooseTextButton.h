#pragma once
#include "TextButton.h"

class ChooseTextButton :
	public TextButton
{
public:
	ChooseTextButton();
	virtual ~ChooseTextButton();

	void setNormalColor(unsigned int color);
	void setHoverColor(unsigned int color);
	void setPressColor(unsigned int color);
	void setSelected(bool value);
	bool isSelected() const;
	void setNavigationHighlighted(bool value);
	bool isNavigationHighlighted() const;

	virtual void setStr(const std::string& s);
	virtual void initFromIni(INIReader & ini);

protected:
	unsigned int normalColor = 0xFFFFFFFF;
	unsigned int hoverColor = 0xFFFFFF00;
	unsigned int pressColor = 0xFF00FF00;
	bool selected = false;
	bool navigationHighlighted = false;

	Label normalLabel;
	Label hoverLabel;
	Label pressLabel;

	virtual void onDraw();
	virtual void onMouseMoveIn(int x, int y);
	virtual void onMouseMoveOut();
	virtual void onMouseLeftDown(int x, int y);
	virtual void onMouseLeftUp(int x, int y);
	virtual void onClick();
	void drawNavigationHighlight();
};
