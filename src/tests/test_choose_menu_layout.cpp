#include "../Component/TextLayout.h"
#include "../Game/Menu/ChooseMenuLayout.h"

#include <algorithm>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace
{
bool check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}

bool isWithinViewport(const ChooseMenuLayoutRectangle& rectangle, int width, int height)
{
	if (rectangle.isEmpty())
	{
		return rectangle.width == 0 && rectangle.height == 0;
	}
	return rectangle.x >= 0
		&& rectangle.y >= 0
		&& rectangle.right() <= width
		&& rectangle.bottom() <= height;
}

bool isInside(
	const ChooseMenuLayoutRectangle& rectangle,
	const ChooseMenuLayoutRectangle& container)
{
	return rectangle.x >= container.x
		&& rectangle.y >= container.y
		&& rectangle.right() <= container.right()
		&& rectangle.bottom() <= container.bottom();
}

bool isSameRectangle(
	const ChooseMenuLayoutRectangle& actual,
	const ChooseMenuLayoutRectangle& expected)
{
	return actual.x == expected.x
		&& actual.y == expected.y
		&& actual.width == expected.width
		&& actual.height == expected.height;
}

bool checkAllRectanglesWithinViewport(
	const ChooseMenuLayoutOutput& layout,
	int width,
	int height,
	const char* message)
{
	std::vector<ChooseMenuLayoutRectangle> rectangles = {
		layout.panel,
		layout.message,
		layout.speaker,
		layout.portrait,
		layout.previousPage,
		layout.nextPage,
		layout.clear,
		layout.confirm
	};
	for (const ChooseMenuLayoutPositionedItem& item : layout.pageItems)
	{
		rectangles.push_back(item.rect);
	}
	for (const ChooseMenuLayoutRectangle& rectangle : rectangles)
	{
		if (!isWithinViewport(rectangle, width, height))
		{
			std::cerr << "FAILED: " << message
				<< " rect=" << rectangle.x << ',' << rectangle.y << ','
				<< rectangle.width << ',' << rectangle.height << '\n';
			return false;
		}
	}
	return true;
}

ChooseMenuLayoutInput makeJxqy2Input(int width, int height)
{
	ChooseMenuLayoutInput input;
	input.viewportWidth = width;
	input.viewportHeight = height;
	input.preferredPanel = { (width - 440) / 2, height - 120, 440, 120 };
	input.preferredMessage = { 36, 18, 384, 28 };
	input.preferredOption = { 36, 52, 384, 24 };
	input.messageFontSize = 17;
	input.optionFontSize = 17;
	input.speakerFontSize = 17;
	input.rowGap = 6;
	return input;
}

ChooseMenuLayoutInput makeYycsInput(int width, int height)
{
	ChooseMenuLayoutInput input;
	input.viewportWidth = width;
	input.viewportHeight = height;
	// JxqyHD positions the native panel at (width - 438) / 2, height - 208.
	input.preferredPanel = { (width - 438) / 2, height - 208, 438, 123 };
	input.preferredMessage = { 65, 30, 310, 22 };
	input.preferredOption = { 65, 52, 310, 22 };
	input.messageFontSize = 18;
	input.optionFontSize = 18;
	input.speakerFontSize = 18;
	input.rowGap = 0;
	return input;
}

bool testYycsDialogAlignedChoice()
{
	bool ok = true;
	ChooseMenuLayoutInput input = makeYycsInput(640, 480);
	input.anchorPanelToPreferredTop = true;
	input.compactDialogAlignedTwoChoice = true;
	input.message = u8"请选择";
	input.visibleItems = {
		{ 0, u8"选项一" },
		{ 1, u8"选项二" }
	};

	const ChooseMenuLayoutOutput layout = calculateChooseMenuLayout(input);
	ok = check(isSameRectangle(layout.panel, { 101, 272, 438, 123 }),
		"ordinary YYCS choice preserves the native dialog panel rectangle") && ok;
	ok = check(isSameRectangle(layout.message, { 166, 302, 310, 22 }),
		"ordinary YYCS choice preserves the native dialog message rectangle") && ok;
	ok = check(layout.pageItems.size() == 2
		&& isSameRectangle(layout.pageItems[0].rect, { 166, 324, 310, 22 })
		&& isSameRectangle(layout.pageItems[1].rect, { 166, 346, 310, 22 }),
		"ordinary YYCS choice follows the dialog's 22-pixel line spacing") && ok;
	ok = check(layout.pageItems.size() == 2
		&& layout.pageItems[0].originalIndex == 0
		&& layout.pageItems[1].originalIndex == 1
		&& layout.pageCount == 1
		&& layout.pageIndex == 0,
		"compact YYCS choice preserves indices without pagination") && ok;
	ok = checkAllRectanglesWithinViewport(layout, 640, 480,
		"dialog-aligned YYCS choice stays inside the 640x480 viewport") && ok;

	input.message = std::string(200, 'L');
	const ChooseMenuLayoutOutput longContentLayout = calculateChooseMenuLayout(input);
	ok = check(longContentLayout.panel.height > input.preferredPanel.height
		&& !isSameRectangle(longContentLayout.message, { 166, 302, 310, 22 }),
		"long YYCS choice content falls back to the responsive layout") && ok;
	ok = check(longContentLayout.pageItems.size() == 2
		&& longContentLayout.pageItems[0].rect.height >= 28
		&& longContentLayout.pageItems[1].rect.height >= 28,
		"responsive YYCS fallback keeps touch-sized option rectangles") && ok;
	return ok;
}

bool testUtf8TextLayout()
{
	bool ok = true;
	ok = check(TextLayout::countUtf8Characters(u8"甲A乙") == 3,
		"UTF-8 character counting treats a multibyte code point as one character") && ok;

	const std::vector<std::string> explicitLines = TextLayout::wrapUtf8Text(
		u8"甲乙<enter>丙丁\n戊己\r\n庚辛",
		2);
	const std::vector<std::string> expectedExplicitLines = {
		u8"甲乙", u8"丙丁", u8"戊己", u8"庚辛"
	};
	ok = check(explicitLines == expectedExplicitLines,
		"exact <enter>, LF, and CRLF all create explicit text lines") && ok;

	const std::vector<std::string> wrappedLines = TextLayout::wrapUtf8Text(u8"甲乙丙丁戊", 2);
	const std::vector<std::string> expectedWrappedLines = { u8"甲乙", u8"丙丁", u8"戊" };
	ok = check(wrappedLines == expectedWrappedLines,
		"UTF-8 wrapping never cuts a multibyte character") && ok;
	const std::vector<std::string> latinWrappedLines = TextLayout::wrapUtf8Text("abcdef", 2);
	const std::vector<std::string> expectedLatinWrappedLines = { "abcde", "f" };
	ok = check(latinWrappedLines == expectedLatinWrappedLines,
		"ASCII text uses the full visible width instead of wrapping like full-width CJK text") && ok;
	const std::vector<std::string> wideLatinLines = TextLayout::wrapUtf8Text(
		std::string(48, 'a'),
		TextLayout::charactersPerLineForWidth(310, 18));
	ok = check(wideLatinLines.size() == 2 &&
		wideLatinLines[0].size() == 42 && wideLatinLines[1].size() == 6,
		"long ASCII prompts fill the native YYCS dialog width before wrapping") && ok;
	ok = check(TextLayout::wrapUtf8Text("", 20).empty(),
		"an empty string produces zero visible rows") && ok;
	ok = check(TextLayout::wrappedLineCount("", 100, 16) == 0,
		"an empty string has zero wrapped lines") && ok;
	ok = check(TextLayout::wrappedTextHeight(u8"甲乙<enter>丙丁", 100, 16, 2) == 34,
		"wrapped text height includes line gaps") && ok;
	ok = check(TextLayout::visibleWrappedLineCount(4, 32, 16) == 2,
		"fixed-height labels draw only complete lines inside their rectangle") && ok;
	ok = check(TextLayout::visibleWrappedLineCount(4, 31, 16) == 1,
		"fixed-height labels do not draw a partially clipped second line") && ok;
	return ok;
}

bool testDesktopLayout()
{
	bool ok = true;
	ChooseMenuLayoutInput input = makeJxqy2Input(1280, 720);
	input.message = u8"第一行<enter>第二行<enter>第三行<enter>第四行<enter>第五行<enter>第六行<enter>第七行";
	for (int index = 0; index < 7; ++index)
	{
		input.visibleItems.push_back({ index, u8"这是一个普通选项" + std::to_string(index) });
	}

	const ChooseMenuLayoutOutput layout = calculateChooseMenuLayout(input);
	ok = checkAllRectanglesWithinViewport(layout, 1280, 720,
		"1280x720 layout keeps every rectangle in the viewport") && ok;
	ok = check(layout.panel.height > input.preferredPanel.height,
		"desktop panel grows to contain multiline text and all options") && ok;
	ok = check(layout.panel.bottom() == 720 - 96,
		"a stale bottom-aligned JXQY2 template is raised above the HUD") && ok;
	ok = check(layout.pageCount == 1,
		"seven desktop choices fit after the panel grows upward") && ok;
	ok = check(layout.pageItems.size() == 7,
		"all desktop choices remain visible") && ok;
	ok = check(layout.message.height >= TextLayout::wrappedTextHeight(
		input.message,
		layout.message.width,
		input.messageFontSize,
		2),
		"message rectangle contains its full wrapped text height") && ok;
	for (const ChooseMenuLayoutPositionedItem& item : layout.pageItems)
	{
		ok = check(isInside(item.rect, layout.panel),
			"desktop option click rectangle stays inside the panel") && ok;
		ok = check(item.rect.height >= 28,
			"desktop option has a touch-sized click rectangle") && ok;
	}
	ok = check(layout.panel.bottom() - layout.pageItems.back().rect.bottom() >= 24,
		"desktop options keep a readable margin above the panel border") && ok;
	return ok;
}

bool testMobilePaginationAndOriginalIndices()
{
	bool ok = true;
	ChooseMenuLayoutInput input = makeYycsInput(1100, 500);
	input.message = u8"请选择";
	input.visibleItems = {
		{ 0, u8"选项一" },
		{ 1, u8"选项二" },
		{ 2, u8"选项三" },
		{ 3, u8"选项四" }
	};
	const ChooseMenuLayoutOutput compactLayout = calculateChooseMenuLayout(input);
	ok = check(compactLayout.panel.bottom() == 500 - 96,
		"an extended YYCS-style menu retains the shared HUD clearance") && ok;
	ok = check(!compactLayout.message.isEmpty() && compactLayout.message.height >= 17,
		"a short choose title always keeps a visible first line") && ok;
	ok = check(compactLayout.message.x == compactLayout.panel.x + 65 &&
		compactLayout.message.right() == compactLayout.panel.x + 375,
		"extended YYCS title stays inside the native dialog text rectangle") && ok;
	ok = check(compactLayout.message.y == compactLayout.panel.y + 32,
		"single-line extended YYCS prompt is centered in its native text rectangle") && ok;
	ok = check(compactLayout.pageItems.size() == 4 &&
		compactLayout.pageItems.front().rect.y > compactLayout.message.y,
		"four-option YYCS layout keeps every option below the centered prompt") && ok;

	input.message = u8"一<enter>二<enter>三<enter>四<enter>五<enter>六<enter>七";
	input.visibleItems.clear();
	for (int index = 0; index < 11; ++index)
	{
		const std::string text = index == 4
			? u8"第一段<enter>第二段<enter>第三段<enter>第四段"
			: u8"比武台选项" + std::to_string(index);
		input.visibleItems.push_back({ index, text });
	}

	const ChooseMenuLayoutOutput firstPage = calculateChooseMenuLayout(input);
	ok = check(firstPage.pageCount > 1,
		"1100x500 long ChoosePlus content is split into pages") && ok;
	ok = check(!firstPage.previousPage.isEmpty() && !firstPage.nextPage.isEmpty(),
		"paginated layout provides previous and next page click rectangles") && ok;
	ok = checkAllRectanglesWithinViewport(firstPage, 1100, 500,
		"1100x500 first page stays in the viewport") && ok;
	ok = check(!firstPage.contentClipped,
		"representative mobile content is paginated without clipping") && ok;

	std::vector<int> observedOriginalIndices;
	for (int pageIndex = 0; pageIndex < firstPage.pageCount; ++pageIndex)
	{
		input.requestedPageIndex = pageIndex;
		const ChooseMenuLayoutOutput page = calculateChooseMenuLayout(input);
		ok = check(page.pageCount == firstPage.pageCount,
			"page count is stable while navigating") && ok;
		ok = check(page.pageIndex == pageIndex,
			"requested page is returned") && ok;
		ok = checkAllRectanglesWithinViewport(page, 1100, 500,
			"every 1100x500 page stays in the viewport") && ok;
		for (const ChooseMenuLayoutPositionedItem& item : page.pageItems)
		{
			observedOriginalIndices.push_back(item.originalIndex);
			const int textHeight = TextLayout::wrappedTextHeight(
				item.text,
				item.rect.width,
				input.optionFontSize,
				2);
			ok = check(item.rect.height >= textHeight + 6,
				"option click rectangle contains the full wrapped text height") && ok;
		}
	}

	std::vector<int> expectedOriginalIndices;
	for (int index = 0; index < 11; ++index)
	{
		expectedOriginalIndices.push_back(index);
	}
	ok = check(observedOriginalIndices == expectedOriginalIndices,
		"pagination preserves every original index in order without loss or duplication") && ok;
	const std::set<int> uniqueIndices(observedOriginalIndices.begin(), observedOriginalIndices.end());
	ok = check(uniqueIndices.size() == observedOriginalIndices.size(),
		"pagination does not duplicate an original index") && ok;

	input.requestedPageIndex = 999;
	const ChooseMenuLayoutOutput clampedPage = calculateChooseMenuLayout(input);
	ok = check(clampedPage.pageIndex == clampedPage.pageCount - 1,
		"out-of-range requested page clamps to the final page") && ok;
	return ok;
}

bool testEmptyItemsAndSparseOriginalIndices()
{
	bool ok = true;
	ChooseMenuLayoutInput input = makeJxqy2Input(800, 600);
	input.message = u8"请选择";
	input.visibleItems = {
		{ 1, "" },
		{ 3, u8"可见甲" },
		{ 8, u8"可见乙" }
	};

	const ChooseMenuLayoutOutput layout = calculateChooseMenuLayout(input);
	ok = check(layout.totalVisibleItems == 2,
		"empty option strings do not create clickable rows") && ok;
	ok = check(layout.pageItems.size() == 2,
		"only nonempty options are positioned") && ok;
	ok = check(layout.pageItems[0].originalIndex == 3 && layout.pageItems[1].originalIndex == 8,
		"caller-provided sparse original indices are not compressed") && ok;

	input.visibleItems = { { 4, "" } };
	const ChooseMenuLayoutOutput emptyLayout = calculateChooseMenuLayout(input);
	ok = check(emptyLayout.totalVisibleItems == 0 && emptyLayout.pageItems.empty(),
		"an all-empty option list safely produces no clickable rows") && ok;
	ok = check(emptyLayout.pageCount == 1,
		"an all-empty option list has one stable empty page") && ok;
	return ok;
}

bool testMulticolumnResizeAndChoosePlusHeader()
{
	bool ok = true;
	ChooseMenuLayoutInput input;
	input.viewportWidth = 800;
	input.viewportHeight = 600;
	input.preferredPanel = { 760, 580, 560, 80 };
	input.preferredMessage = { 50, 14, 456, 18 };
	input.preferredOption = { 50, 32, 456, 18 };
	input.messageFontSize = 16;
	input.optionFontSize = 16;
	input.speakerFontSize = 16;
	input.columnCount = 4;
	input.rowGap = 3;
	input.columnGap = 7;
	input.multipleFooter = true;
	input.showPortrait = true;
	input.showSpeaker = true;
	input.speakerRight = true;
	input.speakerName = u8"酒肆老板";
	input.message = u8"窗口缩放后仍应保持在屏幕内";
	for (int index = 0; index < 9; ++index)
	{
		input.visibleItems.push_back({ 20 + index, u8"多列选项" + std::to_string(index) });
	}

	const ChooseMenuLayoutOutput layout = calculateChooseMenuLayout(input);
	ok = checkAllRectanglesWithinViewport(layout, 800, 600,
		"800x600 resized multicolumn layout stays in the viewport") && ok;
	ok = check(isInside(layout.panel, { 0, 0, 800, 600 }),
		"oversized preferred panel position is clamped back into the viewport") && ok;
	ok = check(layout.pageItems.size() == 9,
		"all multicolumn options remain positioned") && ok;
	ok = check(layout.pageItems[0].rect.y == layout.pageItems[1].rect.y
		&& layout.pageItems[1].rect.y == layout.pageItems[2].rect.y
		&& layout.pageItems[2].rect.y == layout.pageItems[3].rect.y,
		"the first four items share one multicolumn row") && ok;
	ok = check(layout.pageItems[0].rect.right() <= layout.pageItems[1].rect.x
		&& layout.pageItems[1].rect.right() <= layout.pageItems[2].rect.x
		&& layout.pageItems[2].rect.right() <= layout.pageItems[3].rect.x,
		"multicolumn option rectangles do not overlap horizontally") && ok;
	ok = check(!layout.clear.isEmpty() && !layout.confirm.isEmpty(),
		"multiple selection exposes clear and confirm footer rectangles") && ok;
	ok = check(layout.clear.right() <= layout.confirm.x,
		"multiple selection footer controls do not overlap") && ok;
	ok = check(!layout.portrait.isEmpty() && layout.portrait.right() <= layout.speaker.x,
		"ChoosePlus portrait remains at a stable left-side position") && ok;
	ok = check(layout.speakerRight,
		"ChoosePlus speaker alignment retains the requested right-aligned name contract") && ok;
	for (const ChooseMenuLayoutPositionedItem& item : layout.pageItems)
	{
		ok = check(isInside(item.rect, layout.panel),
			"multicolumn option stays inside the resized panel") && ok;
		ok = check(item.rect.width > 0,
			"multicolumn option keeps a usable width") && ok;
	}
	return ok;
}
}

int main()
{
	bool ok = true;
	ok = testUtf8TextLayout() && ok;
	ok = testYycsDialogAlignedChoice() && ok;
	ok = testDesktopLayout() && ok;
	ok = testMobilePaginationAndOriginalIndices() && ok;
	ok = testEmptyItemsAndSparseOriginalIndices() && ok;
	ok = testMulticolumnResizeAndChoosePlusHeader() && ok;
	return ok ? 0 : 1;
}
