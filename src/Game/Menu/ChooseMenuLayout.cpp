#include "ChooseMenuLayout.h"

#include "../../Component/TextLayout.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

namespace
{
constexpr int VIEWPORT_MARGIN = 10;
constexpr int DESKTOP_PANEL_BOTTOM_CLEARANCE = 96;
constexpr int MINIMUM_CONTENT_PADDING = 8;
constexpr int MINIMUM_PANEL_EDGE_PADDING = 24;
constexpr int MINIMUM_TOUCH_HEIGHT = 28;
constexpr int TEXT_VERTICAL_PADDING = 3;
constexpr int TEXT_LINE_GAP = 2;
constexpr int SECTION_GAP = 4;
constexpr int FOOTER_GAP = 4;
constexpr int PORTRAIT_SIZE = 72;

struct ChooseMenuLayoutRow
{
	std::size_t firstItemIndex = 0;
	std::size_t itemCount = 0;
	int height = 0;
};

struct ChooseMenuLayoutPage
{
	std::size_t firstRowIndex = 0;
	std::size_t rowCount = 0;
	int contentHeight = 0;
};

int clampInteger(int value, int minimum, int maximum)
{
	if (maximum < minimum)
	{
		return minimum;
	}
	return std::max(minimum, std::min(value, maximum));
}

ChooseMenuLayoutRectangle clampRectangle(
	const ChooseMenuLayoutRectangle& rectangle,
	int viewportWidth,
	int viewportHeight)
{
	ChooseMenuLayoutRectangle result = rectangle;
	result.x = clampInteger(result.x, 0, std::max(0, viewportWidth));
	result.y = clampInteger(result.y, 0, std::max(0, viewportHeight));
	result.width = clampInteger(result.width, 0, std::max(0, viewportWidth - result.x));
	result.height = clampInteger(result.height, 0, std::max(0, viewportHeight - result.y));
	return result;
}

int preferredRightPadding(const ChooseMenuLayoutInput& input)
{
	const int messageRightPadding = input.preferredPanel.width
		- input.preferredMessage.x - input.preferredMessage.width;
	const int optionRightPadding = input.preferredPanel.width
		- input.preferredOption.x - input.preferredOption.width;
	return std::max(MINIMUM_CONTENT_PADDING, std::min(messageRightPadding, optionRightPadding));
}

int calculateRowsHeight(
	const std::vector<ChooseMenuLayoutRow>& rows,
	std::size_t firstRow,
	std::size_t rowCount,
	int rowGap)
{
	if (rowCount == 0)
	{
		return 0;
	}

	int height = 0;
	for (std::size_t rowOffset = 0; rowOffset < rowCount; ++rowOffset)
	{
		height += rows[firstRow + rowOffset].height;
	}
	height += static_cast<int>(rowCount - 1) * rowGap;
	return height;
}

std::vector<ChooseMenuLayoutPage> paginateRows(
	const std::vector<ChooseMenuLayoutRow>& rows,
	int availableHeight,
	int rowGap,
	bool& contentClipped)
{
	std::vector<ChooseMenuLayoutPage> pages;
	std::size_t nextRowIndex = 0;
	while (nextRowIndex < rows.size())
	{
		ChooseMenuLayoutPage page;
		page.firstRowIndex = nextRowIndex;
		while (nextRowIndex < rows.size())
		{
			const int boundedRowHeight = std::min(rows[nextRowIndex].height, availableHeight);
			if (boundedRowHeight < rows[nextRowIndex].height)
			{
				contentClipped = true;
			}
			const int additionalHeight = boundedRowHeight
				+ (page.rowCount == 0 ? 0 : rowGap);
			if (page.rowCount > 0 && page.contentHeight + additionalHeight > availableHeight)
			{
				break;
			}
			page.contentHeight += additionalHeight;
			++page.rowCount;
			++nextRowIndex;
			if (page.contentHeight >= availableHeight)
			{
				break;
			}
		}
		pages.push_back(page);
	}
	if (pages.empty())
	{
		pages.push_back({});
	}
	return pages;
}

ChooseMenuLayoutRectangle makeFooterHalf(
	int panelX,
	int contentX,
	int y,
	int contentWidth,
	int height,
	int columnGap,
	bool rightHalf)
{
	const int leftWidth = std::max(1, (contentWidth - columnGap) / 2);
	const int rightWidth = std::max(1, contentWidth - columnGap - leftWidth);
	if (rightHalf)
	{
		return { panelX + contentX + leftWidth + columnGap, y, rightWidth, height };
	}
	return { panelX + contentX, y, leftWidth, height };
}

bool rectangleFitsInside(
	const ChooseMenuLayoutRectangle& rectangle,
	const ChooseMenuLayoutRectangle& container)
{
	return rectangle.width > 0
		&& rectangle.height > 0
		&& rectangle.x >= container.x
		&& rectangle.y >= container.y
		&& rectangle.right() <= container.right()
		&& rectangle.bottom() <= container.bottom();
}

ChooseMenuLayoutRectangle toViewportRectangle(
	const ChooseMenuLayoutRectangle& panel,
	const ChooseMenuLayoutRectangle& panelLocalRectangle)
{
	return {
		panel.x + panelLocalRectangle.x,
		panel.y + panelLocalRectangle.y,
		panelLocalRectangle.width,
		panelLocalRectangle.height
	};
}

bool canUseCompactDialogAlignedTwoChoice(
	const ChooseMenuLayoutInput& input,
	const std::vector<ChooseMenuLayoutItem>& visibleItems,
	int viewportWidth,
	int viewportHeight)
{
	if (!input.compactDialogAlignedTwoChoice
		|| !input.anchorPanelToPreferredTop
		|| input.multipleFooter
		|| input.showSpeaker
		|| input.showPortrait
		|| input.columnCount != 1
		|| visibleItems.size() != 2)
	{
		return false;
	}

	const ChooseMenuLayoutRectangle viewport = { 0, 0, viewportWidth, viewportHeight };
	if (!rectangleFitsInside(input.preferredPanel, viewport))
	{
		return false;
	}

	const ChooseMenuLayoutRectangle panelLocalBounds = {
		0, 0, input.preferredPanel.width, input.preferredPanel.height
	};
	const int rowGap = std::max(0, input.rowGap);
	const ChooseMenuLayoutRectangle secondOption = {
		input.preferredOption.x,
		input.preferredOption.y + input.preferredOption.height + rowGap,
		input.preferredOption.width,
		input.preferredOption.height
	};
	if (!rectangleFitsInside(input.preferredMessage, panelLocalBounds)
		|| !rectangleFitsInside(input.preferredOption, panelLocalBounds)
		|| !rectangleFitsInside(secondOption, panelLocalBounds))
	{
		return false;
	}

	if (!input.message.empty()
		&& (TextLayout::wrappedLineCount(
			input.message,
			input.preferredMessage.width,
			input.messageFontSize) != 1
			|| TextLayout::wrappedTextHeight(
				input.message,
				input.preferredMessage.width,
				input.messageFontSize,
				TEXT_LINE_GAP) > input.preferredMessage.height))
	{
		return false;
	}

	for (const ChooseMenuLayoutItem& item : visibleItems)
	{
		if (TextLayout::wrappedLineCount(
			item.text,
			input.preferredOption.width,
			input.optionFontSize) != 1
			|| TextLayout::wrappedTextHeight(
				item.text,
				input.preferredOption.width,
				input.optionFontSize,
				TEXT_LINE_GAP) > input.preferredOption.height)
		{
			return false;
		}
	}
	return true;
}
}

bool ChooseMenuLayoutRectangle::isEmpty() const
{
	return width <= 0 || height <= 0;
}

int ChooseMenuLayoutRectangle::right() const
{
	return x + width;
}

int ChooseMenuLayoutRectangle::bottom() const
{
	return y + height;
}

ChooseMenuLayoutOutput calculateChooseMenuLayout(const ChooseMenuLayoutInput& input)
{
	ChooseMenuLayoutOutput output;
	output.speakerRight = input.speakerRight;

	const int viewportWidth = std::max(1, input.viewportWidth);
	const int viewportHeight = std::max(1, input.viewportHeight);
	const int horizontalMargin = std::min(VIEWPORT_MARGIN, viewportWidth / 4);
	const int verticalMargin = std::min(VIEWPORT_MARGIN, viewportHeight / 4);
	const int maximumPanelWidth = std::max(1, viewportWidth - horizontalMargin * 2);
	const int maximumPanelHeight = std::max(1, viewportHeight - verticalMargin * 2);

	std::vector<ChooseMenuLayoutItem> visibleItems;
	visibleItems.reserve(input.visibleItems.size());
	for (const ChooseMenuLayoutItem& item : input.visibleItems)
	{
		if (!item.text.empty())
		{
			visibleItems.push_back(item);
		}
	}
	output.totalVisibleItems = static_cast<int>(std::min<std::size_t>(
		visibleItems.size(),
		static_cast<std::size_t>(std::numeric_limits<int>::max())));
	if (canUseCompactDialogAlignedTwoChoice(
		input, visibleItems, viewportWidth, viewportHeight))
	{
		output.panel = input.preferredPanel;
		if (!input.message.empty())
		{
			output.message = toViewportRectangle(output.panel, input.preferredMessage);
		}
		const int rowGap = std::max(0, input.rowGap);
		for (std::size_t itemIndex = 0; itemIndex < visibleItems.size(); ++itemIndex)
		{
			ChooseMenuLayoutRectangle optionRectangle = input.preferredOption;
			optionRectangle.y += static_cast<int>(itemIndex)
				* (input.preferredOption.height + rowGap);
			output.pageItems.push_back({
				visibleItems[itemIndex].originalIndex,
				visibleItems[itemIndex].text,
				toViewportRectangle(output.panel, optionRectangle)
			});
		}
		return output;
	}
	const int maximumUsefulColumnCount = visibleItems.empty()
		? 1
		: output.totalVisibleItems;
	const int columnCount = clampInteger(input.columnCount, 1, maximumUsefulColumnCount);
	const int rowGap = std::max(0, input.rowGap);
	const int columnGap = std::max(0, input.columnGap);
	const int leftPadding = std::max(
		MINIMUM_CONTENT_PADDING,
		std::min(input.preferredMessage.x, input.preferredOption.x));
	const int rightPadding = preferredRightPadding(input);
	const int preferredOptionWidth = std::max(1, input.preferredOption.width);
	const long long desiredOptionsWidth = static_cast<long long>(preferredOptionWidth) * columnCount
		+ static_cast<long long>(columnGap) * (columnCount - 1);
	const long long desiredPanelWidth = std::max<long long>(
		std::max(1, input.preferredPanel.width),
		static_cast<long long>(leftPadding) + desiredOptionsWidth + rightPadding);
	const int panelWidth = static_cast<int>(std::min<long long>(maximumPanelWidth, desiredPanelWidth));
	const int contentLeft = std::min(leftPadding, std::max(0, panelWidth - 1));
	const int contentWidth = std::max(1, panelWidth - contentLeft - rightPadding);
	const int effectiveColumnGap = std::min(
		columnGap,
		std::max(0, (contentWidth - columnCount) / std::max(1, columnCount - 1)));
	const int optionColumnWidth = std::max(
		1,
		(contentWidth - effectiveColumnGap * (columnCount - 1)) / columnCount);

	std::vector<ChooseMenuLayoutRow> rows;
	for (std::size_t firstItemIndex = 0; firstItemIndex < visibleItems.size(); firstItemIndex += columnCount)
	{
		ChooseMenuLayoutRow row;
		row.firstItemIndex = firstItemIndex;
		row.itemCount = std::min<std::size_t>(
			static_cast<std::size_t>(columnCount),
			visibleItems.size() - firstItemIndex);
		row.height = MINIMUM_TOUCH_HEIGHT;
		for (std::size_t itemOffset = 0; itemOffset < row.itemCount; ++itemOffset)
		{
			const ChooseMenuLayoutItem& item = visibleItems[firstItemIndex + itemOffset];
			const int textHeight = TextLayout::wrappedTextHeight(
				item.text,
				optionColumnWidth,
				std::max(1, input.optionFontSize),
				TEXT_LINE_GAP);
			row.height = std::max(row.height, textHeight + TEXT_VERTICAL_PADDING * 2);
		}
		rows.push_back(row);
	}

	const int topPadding = std::max(MINIMUM_CONTENT_PADDING, input.preferredMessage.y);
	const bool speakerVisible = input.showSpeaker && !input.speakerName.empty();
	const bool portraitVisible = input.showPortrait;
	const int portraitSize = portraitVisible
		? std::min(PORTRAIT_SIZE, std::max(1, contentWidth / 3))
		: 0;
	const int portraitGap = portraitVisible ? SECTION_GAP : 0;
	const int textHeaderWidth = std::max(1, contentWidth - portraitSize - portraitGap);
	const int speakerHeight = speakerVisible ? std::max(1, input.speakerFontSize) : 0;
	const int speakerMessageGap = speakerVisible && !input.message.empty() ? SECTION_GAP : 0;
	const int measuredMessageHeight = TextLayout::wrappedTextHeight(
		input.message,
		textHeaderWidth,
		std::max(1, input.messageFontSize),
		TEXT_LINE_GAP);
	const int preferredMessageHeight = input.message.empty()
		? 0
		: std::max(0, input.preferredMessage.height);
	const int messageHeight = std::max(measuredMessageHeight, preferredMessageHeight);
	const int textHeaderHeight = speakerHeight + speakerMessageGap + messageHeight;
	const int unboundedHeaderHeight = std::max(portraitSize, textHeaderHeight);
	const int headerOptionsGap = unboundedHeaderHeight > 0 && !rows.empty()
		? std::max(
			SECTION_GAP,
			input.preferredOption.y - input.preferredMessage.y - input.preferredMessage.height)
		: 0;
	const int bottomPadding = MINIMUM_PANEL_EDGE_PADDING;
	const int footerHeight = std::max(MINIMUM_TOUCH_HEIGHT, std::max(1, input.preferredOption.height));
	const int multipleFooterHeight = input.multipleFooter ? footerHeight : 0;
	const int multipleFooterGap = input.multipleFooter ? FOOTER_GAP : 0;
	const int minimumRowsHeight = rows.empty() ? 0 : MINIMUM_TOUCH_HEIGHT;
	const int heightWithoutHeaderOrNavigation = topPadding + headerOptionsGap
		+ multipleFooterGap + multipleFooterHeight + bottomPadding;
	const int maximumHeaderHeightWithoutNavigation = std::max(
		0,
		maximumPanelHeight - heightWithoutHeaderOrNavigation - minimumRowsHeight);
	int headerHeight = std::min(unboundedHeaderHeight, maximumHeaderHeightWithoutNavigation);
	if (headerHeight < unboundedHeaderHeight)
	{
		output.contentClipped = true;
	}
	const int baseHeightWithoutNavigation = topPadding + headerHeight + headerOptionsGap
		+ calculateRowsHeight(rows, 0, rows.size(), rowGap)
		+ multipleFooterGap + multipleFooterHeight + bottomPadding;
	const bool paginationNeeded = baseHeightWithoutNavigation > maximumPanelHeight;
	const int navigationHeight = paginationNeeded ? footerHeight : 0;
	const int navigationGap = paginationNeeded ? FOOTER_GAP : 0;
	const int maximumHeaderHeight = std::max(
		0,
		maximumPanelHeight - heightWithoutHeaderOrNavigation
			- navigationGap - navigationHeight - minimumRowsHeight);
	if (headerHeight > maximumHeaderHeight)
	{
		headerHeight = maximumHeaderHeight;
		output.contentClipped = true;
	}
	const int fixedHeight = topPadding + headerHeight + headerOptionsGap
		+ navigationGap + navigationHeight
		+ multipleFooterGap + multipleFooterHeight + bottomPadding;
	const int availableRowsHeight = std::max(1, maximumPanelHeight - fixedHeight);
	std::vector<ChooseMenuLayoutPage> pages = paginateRows(
		rows,
		availableRowsHeight,
		rowGap,
		output.contentClipped);
	output.pageCount = static_cast<int>(pages.size());
	output.pageIndex = clampInteger(input.requestedPageIndex, 0, output.pageCount - 1);

	int maximumPageContentHeight = 0;
	for (const ChooseMenuLayoutPage& page : pages)
	{
		maximumPageContentHeight = std::max(maximumPageContentHeight, page.contentHeight);
	}
	const int requiredPanelHeight = fixedHeight + maximumPageContentHeight;
	const int panelHeight = std::min(
		maximumPanelHeight,
		std::max(std::max(1, input.preferredPanel.height), requiredPanelHeight));
	const int preferredPanelCenterX = input.preferredPanel.x + input.preferredPanel.width / 2;
	const int panelX = clampInteger(
		preferredPanelCenterX - panelWidth / 2,
		horizontalMargin,
		viewportWidth - horizontalMargin - panelWidth);
	int panelY = 0;
	if (input.anchorPanelToPreferredTop)
	{
		panelY = clampInteger(
			input.preferredPanel.y,
			verticalMargin,
			viewportHeight - verticalMargin - panelHeight);
	}
	else
	{
		const int preferredPanelBottom = input.preferredPanel.y + input.preferredPanel.height;
		const int minimumBottomClearance = std::min(
			DESKTOP_PANEL_BOTTOM_CLEARANCE,
			std::max(verticalMargin, viewportHeight / 4));
		const int anchoredPanelBottom = std::min(
			preferredPanelBottom,
			viewportHeight - minimumBottomClearance);
		panelY = clampInteger(
			anchoredPanelBottom - panelHeight,
			verticalMargin,
			viewportHeight - verticalMargin - panelHeight);
	}
	output.panel = clampRectangle({ panelX, panelY, panelWidth, panelHeight }, viewportWidth, viewportHeight);

	const int visiblePortraitSize = std::min(portraitSize, headerHeight);
	const int visiblePortraitGap = visiblePortraitSize > 0 ? portraitGap : 0;
	const int headerTextX = panelX + contentLeft + visiblePortraitSize + visiblePortraitGap;
	const int visibleTextHeaderWidth = std::max(
		1,
		contentWidth - visiblePortraitSize - visiblePortraitGap);
	int nextHeaderY = panelY + topPadding;
	if (portraitVisible && visiblePortraitSize > 0)
	{
		output.portrait = clampRectangle(
			{ panelX + contentLeft, panelY + topPadding, visiblePortraitSize, visiblePortraitSize },
			viewportWidth,
			viewportHeight);
	}
	const int visibleSpeakerHeight = std::min(speakerHeight, headerHeight);
	if (speakerVisible && visibleSpeakerHeight > 0)
	{
		output.speaker = clampRectangle(
			{ headerTextX, nextHeaderY, visibleTextHeaderWidth, visibleSpeakerHeight },
			viewportWidth,
			viewportHeight);
		nextHeaderY += visibleSpeakerHeight;
	}
	const int visibleSpeakerMessageGap = visibleSpeakerHeight > 0 && !input.message.empty()
		? std::min(speakerMessageGap, std::max(0, panelY + topPadding + headerHeight - nextHeaderY))
		: 0;
	nextHeaderY += visibleSpeakerMessageGap;
	const int visibleMessageHeight = std::min(
		messageHeight,
		std::max(0, panelY + topPadding + headerHeight - nextHeaderY));
	if (!input.message.empty() && visibleMessageHeight > 0)
	{
		// Native single-line templates reserve more height than the glyphs need.
		// Center the first line inside that existing rectangle so four-option YYCS
		// panels do not leave the prompt pressed against the parchment's top edge.
		// Multiline messages already consume their measured height and keep zero inset.
		const int visibleMeasuredMessageHeight = std::min(
			visibleMessageHeight,
			measuredMessageHeight);
		const int messageTopInset = std::max(
			0,
			(visibleMessageHeight - visibleMeasuredMessageHeight) / 2);
		output.message = clampRectangle(
			{
				headerTextX,
				nextHeaderY + messageTopInset,
				visibleTextHeaderWidth,
				visibleMessageHeight - messageTopInset
			},
			viewportWidth,
			viewportHeight);
	}

	int footerBottom = panelY + panelHeight - bottomPadding;
	if (input.multipleFooter)
	{
		const int footerY = footerBottom - multipleFooterHeight;
		output.clear = clampRectangle(
			makeFooterHalf(panelX, contentLeft, footerY, contentWidth, multipleFooterHeight, effectiveColumnGap, false),
			viewportWidth,
			viewportHeight);
		output.confirm = clampRectangle(
			makeFooterHalf(panelX, contentLeft, footerY, contentWidth, multipleFooterHeight, effectiveColumnGap, true),
			viewportWidth,
			viewportHeight);
		footerBottom = footerY - multipleFooterGap;
	}
	if (paginationNeeded)
	{
		const int navigationY = footerBottom - navigationHeight;
		output.previousPage = clampRectangle(
			makeFooterHalf(panelX, contentLeft, navigationY, contentWidth, navigationHeight, effectiveColumnGap, false),
			viewportWidth,
			viewportHeight);
		output.nextPage = clampRectangle(
			makeFooterHalf(panelX, contentLeft, navigationY, contentWidth, navigationHeight, effectiveColumnGap, true),
			viewportWidth,
			viewportHeight);
	}

	const ChooseMenuLayoutPage& currentPage = pages[static_cast<std::size_t>(output.pageIndex)];
	int rowY = panelY + topPadding + headerHeight + headerOptionsGap;
	for (std::size_t rowOffset = 0; rowOffset < currentPage.rowCount; ++rowOffset)
	{
		const ChooseMenuLayoutRow& row = rows[currentPage.firstRowIndex + rowOffset];
		const int positionedRowHeight = std::min(row.height, availableRowsHeight);
		for (std::size_t columnIndex = 0; columnIndex < row.itemCount; ++columnIndex)
		{
			const ChooseMenuLayoutItem& item = visibleItems[row.firstItemIndex + columnIndex];
			ChooseMenuLayoutPositionedItem positionedItem;
			positionedItem.originalIndex = item.originalIndex;
			positionedItem.text = item.text;
			positionedItem.rect = clampRectangle(
				{
					panelX + contentLeft + static_cast<int>(columnIndex) * (optionColumnWidth + effectiveColumnGap),
					rowY,
					optionColumnWidth,
					positionedRowHeight
				},
				viewportWidth,
				viewportHeight);
			output.pageItems.push_back(positionedItem);
		}
		rowY += positionedRowHeight + rowGap;
	}

	return output;
}
