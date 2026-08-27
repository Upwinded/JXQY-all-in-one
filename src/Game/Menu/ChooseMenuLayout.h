#pragma once

#include <string>
#include <vector>

struct ChooseMenuLayoutRectangle
{
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;

	bool isEmpty() const;
	int right() const;
	int bottom() const;
};

struct ChooseMenuLayoutItem
{
	int originalIndex = -1;
	std::string text;
};

struct ChooseMenuLayoutInput
{
	int viewportWidth = 1280;
	int viewportHeight = 720;
	// The panel template uses viewport coordinates. Message and option templates
	// use panel-local coordinates, matching the menu INI resources.
	ChooseMenuLayoutRectangle preferredPanel = { 420, 590, 440, 120 };
	ChooseMenuLayoutRectangle preferredMessage = { 65, 10, 310, 40 };
	ChooseMenuLayoutRectangle preferredOption = { 65, 52, 310, 22 };
	int messageFontSize = 14;
	int optionFontSize = 14;
	int speakerFontSize = 14;
	int rowGap = 2;
	int columnGap = 8;
	int columnCount = 1;
	int requestedPageIndex = 0;
	// Ordinary YYCS two-option choices share the dialog panel's top edge.
	// Extended and multiple-choice menus keep their safer bottom anchor so
	// growing or paginated content remains clear of the HUD.
	bool anchorPanelToPreferredTop = false;
	// A short, ordinary YYCS choice uses the native dialog rectangles exactly.
	// The layout falls back to the responsive path if any text wraps, the panel
	// is outside the viewport, or the presentation has extended UI elements.
	bool compactDialogAlignedTwoChoice = false;
	bool multipleFooter = false;
	bool showSpeaker = false;
	bool speakerRight = false;
	bool showPortrait = false;
	std::string message;
	std::string speakerName;
	// Hidden items should be omitted by the caller. Empty text is ignored here;
	// every remaining item keeps its caller-provided original index.
	std::vector<ChooseMenuLayoutItem> visibleItems;
};

struct ChooseMenuLayoutPositionedItem
{
	int originalIndex = -1;
	std::string text;
	ChooseMenuLayoutRectangle rect;
};

struct ChooseMenuLayoutOutput
{
	// Every nonempty output rectangle uses viewport coordinates.
	ChooseMenuLayoutRectangle panel;
	ChooseMenuLayoutRectangle message;
	ChooseMenuLayoutRectangle speaker;
	ChooseMenuLayoutRectangle portrait;
	ChooseMenuLayoutRectangle previousPage;
	ChooseMenuLayoutRectangle nextPage;
	ChooseMenuLayoutRectangle clear;
	ChooseMenuLayoutRectangle confirm;
	std::vector<ChooseMenuLayoutPositionedItem> pageItems;
	int pageIndex = 0;
	int pageCount = 1;
	int totalVisibleItems = 0;
	bool speakerRight = false;
	bool contentClipped = false;
};

ChooseMenuLayoutOutput calculateChooseMenuLayout(const ChooseMenuLayoutInput& input);
