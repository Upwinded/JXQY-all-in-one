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

class Engine;

class CachedTextTexture
{
public:
	const _shared_image& get(
		Engine* engine,
		const std::string& text,
		int fontSize,
		unsigned int color);
	void draw(
		Engine* engine,
		const std::string& text,
		int x,
		int y,
		int fontSize,
		unsigned int color);
	void drawWithAlpha(
		Engine* engine,
		const std::string& text,
		int x,
		int y,
		int fontSize,
		unsigned int color,
		unsigned char alpha);
	void clear();

private:
	_shared_image image = nullptr;
	std::string cachedText;
	int cachedFontSize = -1;
	unsigned int cachedColor = 0;
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
	void setColorTagsEnabled(bool enabled);
	bool colorTagsEnabled() const { return interpretColorTags; }
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
	bool renderedColorTagsEnabled = false;
	int renderedMinimumFontSize = -1;
	bool textLayoutValid = false;
	bool interpretColorTags = true;
	virtual void drawItemStr();
	virtual void onMouseLeftDown(int x, int y);
};
