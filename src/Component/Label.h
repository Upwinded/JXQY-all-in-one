#pragma once
#include "Item.h"

enum class TextHorizontalAlignment
{
	Left,
	Center,
	Right,
};

enum class TextVerticalAlignment
{
	Top,
	Center,
	Bottom,
};

class Label :
	public Item
{
public:
	Label();
	virtual ~Label();

	bool autoNextLine = false;
	bool autoShrink = false;
	bool elideOverflow = false;
	int minimumFontSize = 7;
	TextHorizontalAlignment horizontalAlignment = TextHorizontalAlignment::Left;
	TextVerticalAlignment verticalAlignment = TextVerticalAlignment::Top;

	void initFromIni(INIReader & ini) override;
	virtual void setStr(const std::string & s);
	void refreshTextLayout();
	void invalidateTextLayout();
	int getRenderedTextHeight();
protected:
	void freeResource() override;
	std::vector<_shared_image> strImage;
	int renderedFontSize = 0;
	std::string renderedText;
	int renderedRectWidth = -1;
	int renderedRectHeight = -1;
	int renderedRequestedFontSize = -1;
	unsigned int renderedColor = 0;
	bool renderedAutoNextLine = false;
	bool renderedAutoShrink = false;
	bool renderedElideOverflow = false;
	int renderedMinimumFontSize = -1;
	bool textLayoutValid = false;
	virtual void drawItemStr();
	virtual void onMouseLeftDown(int x, int y);
};
