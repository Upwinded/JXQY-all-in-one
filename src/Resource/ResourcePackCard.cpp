#include "ResourcePackCard.h"
#include "../Engine/Engine.h"

#include <algorithm>

namespace
{
constexpr int TitleFontSize = 30;
constexpr int AuthorVersionFontSize = 20;
constexpr int TitleMinimumFontSize = 16;
constexpr int AuthorVersionMinimumFontSize = 12;
constexpr int HorizontalPaddingMinimum = 24;
constexpr int HorizontalPaddingMaximum = 48;
constexpr int HorizontalPaddingDivisor = 14;
constexpr int LineGap = 8;
constexpr int SelectionBoxSize = 22;
constexpr int SelectionBoxLeftInset = 10;
constexpr int SelectionBoxLaneWidth = 34;
constexpr int SelectionBoxOuterPadding = 3;
constexpr int DescriptionButtonWidth = 72;
constexpr int DescriptionButtonHeight = 44;
constexpr int DescriptionButtonGap = 10;
constexpr int OnlineOnlyBadgeWidth = 60;
constexpr int RecentSelectionBadgeWidth = 74;
constexpr int CardBadgeHeight = 22;
constexpr int CardBadgeFontSize = 14;
constexpr int CardBadgeGap = 6;
constexpr int CardBadgeTopInset = 6;
constexpr int MinimumVisibleTitleWidth = 36;
constexpr int MinimumTextWidth = 1;

int clampInt(int value, int minimum, int maximum)
{
	return std::max(minimum, std::min(value, maximum));
}
}

ResourcePackCard::ResourcePackCard()
{
	name = "resourcepackcard";
	setPriority(epButton);
	elementType = etButton;
	canCallBack = true;
	hoverSoundEnabled = false;
	stretch = true;

	titleLabel = std::make_shared<Label>();
	authorVersionLabel = std::make_shared<Label>();
	descriptionButton = std::make_shared<FlatTextButton>();
	for (const auto& label : { titleLabel, authorVersionLabel })
	{
		label->coverMouse = false;
		label->autoShrink = true;
		label->elideOverflow = true;
		label->horizontalAlignment = TextHorizontalAlignment::Center;
		label->verticalAlignment = TextVerticalAlignment::Center;
		addChild(label);
	}
	titleLabel->minimumFontSize = TitleMinimumFontSize;
	authorVersionLabel->minimumFontSize = AuthorVersionMinimumFontSize;
	descriptionButton->name = "resourcepackcard-description";
	descriptionButton->setFontSize(16);
	descriptionButton->setUTF8Str(u8"简介");
	descriptionButton->visible = false;
	descriptionButton->activated = false;
	addChild(descriptionButton);
}

ResourcePackCard::~ResourcePackCard()
{
}

void ResourcePackCard::setContent(const ResourcePackCardContent& value)
{
	content = value;
	titleLabel->setStr(content.title);
	authorVersionLabel->setStr(content.authorAndVersion);
	updateDescriptionButtonLayout();
}

void ResourcePackCard::setLayout(const Rect& value)
{
	rect = value;
	updateDescriptionButtonLayout();
}

void ResourcePackCard::setSelected(bool value)
{
	selected = value;
}

bool ResourcePackCard::isSelected() const
{
	return selected;
}

bool ResourcePackCard::ownsPointerInteraction(
	EventTouchID pointerID) const
{
	return hasPointerDownInTree(pointerID);
}

bool ResourcePackCard::ownsBodyPointerInteraction(
	EventTouchID pointerID) const
{
	return touchingDownID == pointerID;
}

bool ResourcePackCard::isDescriptionActionPoint(int x, int y) const
{
	return descriptionButton != nullptr
		&& descriptionButton->visible
		&& descriptionButton->activated
		&& x >= descriptionButton->rect.x
		&& x < descriptionButton->rect.x + descriptionButton->rect.w
		&& y >= descriptionButton->rect.y
		&& y < descriptionButton->rect.y + descriptionButton->rect.h;
}

bool ResourcePackCard::takeDescriptionActionRequested()
{
	const bool requested = descriptionActionRequested;
	descriptionActionRequested = false;
	return requested;
}

void ResourcePackCard::setFrameImages(_shared_image normal, _shared_image selectedImage)
{
	normalFrameImage = normal;
	selectedFrameImage = selectedImage;
}

void ResourcePackCard::onChildCallBack(PElement child)
{
	if (child != descriptionButton || child == nullptr
		|| (child->getResult() & erClick) == 0)
	{
		return;
	}

	descriptionActionRequested = true;
	if (parent != nullptr)
	{
		parent->onChildCallBack(getMySharedPtr());
	}
}

void ResourcePackCard::onDraw()
{
	updateDescriptionButtonLayout();
	const bool hovered = touchingID != TOUCH_UNTOUCHEDID;
	const bool pressed = touchingDownID != TOUCH_UNTOUCHEDID;
	_shared_image frameImage = selected ? selectedFrameImage : normalFrameImage;
	if (frameImage == nullptr)
	{
		frameImage = normalFrameImage;
	}

	if (frameImage != nullptr)
	{
		engine->drawImage(frameImage, nullptr, &rect);
		if (selected)
		{
			engine->fillRect(rect.x + 6, rect.y + 6,
				std::max(1, rect.w - 12), std::max(1, rect.h - 12),
				255, 222, 142, 48);
		}
	}
	else if (selected)
	{
		engine->fillRect(rect.x - 2, rect.y - 2, rect.w + 4, rect.h + 4, 229, 198, 128, 210);
		engine->fillRect(rect.x, rect.y, rect.w, rect.h, 96, 50, 42, 244);
		engine->fillRect(rect.x, rect.y, 5, rect.h, 232, 204, 130, 255);
	}
	else
	{
		engine->fillRect(rect.x - 1, rect.y - 1, rect.w + 2, rect.h + 2, 112, 94, 66, 120);
		engine->fillRect(rect.x, rect.y, rect.w, rect.h, 40, 31, 27, 220);
	}

	drawSelectionBox();
	if (hovered)
	{
		drawHoverBorder(pressed);
	}
	if (selected)
	{
		drawSelectionBorder();
	}
	updateTextLayout(hovered, frameImage != nullptr);
}

void ResourcePackCard::onDrawEnd()
{
	drawOnlineOnlyBadge();
	drawRecentSelectionBadge();
}

void ResourcePackCard::updateDescriptionButtonLayout()
{
	if (descriptionButton == nullptr)
	{
		return;
	}

	const int horizontalPadding = getHorizontalPadding();
	Rect selectionBoxRect;
	const int selectionBoxLaneWidth =
		getSelectionBoxRect(selectionBoxRect) ? SelectionBoxLaneWidth : 0;
	const int requiredWidth =
		horizontalPadding * 2 + selectionBoxLaneWidth
		+ MinimumTextWidth + DescriptionButtonGap
		+ DescriptionButtonWidth;
	const bool canShow =
		content.showDescriptionAction
		&& rect.w >= requiredWidth
		&& rect.h >= DescriptionButtonHeight;
	descriptionButton->visible = canShow;
	descriptionButton->activated = canShow;
	if (!canShow)
	{
		descriptionButton->rect = { 0, 0, 0, 0 };
		descriptionButton->cancelPointerInteraction();
		return;
	}

	descriptionButton->rect =
	{
		rect.x + rect.w - horizontalPadding - DescriptionButtonWidth,
		rect.y + (rect.h - DescriptionButtonHeight) / 2,
		DescriptionButtonWidth,
		DescriptionButtonHeight
	};
}

void ResourcePackCard::updateTextLayout(bool hovered, bool usingFrameImage)
{
	if (rect.w <= 0 || rect.h <= 0)
	{
		onlineOnlyBadgeVisible = false;
		recentSelectionBadgeVisible = false;
		for (const auto& label : { titleLabel, authorVersionLabel })
		{
			label->visible = false;
		}
		return;
	}

	unsigned int titleColor = 0xFFFFFFFF;
	unsigned int authorVersionColor = 0xFF919BA8;
	if (usingFrameImage)
	{
		titleColor = selected || hovered ? 0xFF21140B : 0xFF2E2116;
		authorVersionColor = selected || hovered
			? 0xFF4D2C18 : 0xFF4C3826;
	}
	else if (selected || hovered)
	{
		titleColor = 0xFFFFF2C8;
		authorVersionColor = 0xFFD0B774;
	}

	const int horizontalPadding = getHorizontalPadding();
	Rect selectionBoxRect;
	const int selectionBoxLaneWidth =
		getSelectionBoxRect(selectionBoxRect) ? SelectionBoxLaneWidth : 0;
	const int contentX =
		rect.x + horizontalPadding + selectionBoxLaneWidth;
	const int descriptionButtonLaneWidth =
		descriptionButton != nullptr && descriptionButton->visible
			? DescriptionButtonWidth + DescriptionButtonGap
			: 0;
	const int contentWidth = std::max(1,
		rect.w - horizontalPadding * 2 - selectionBoxLaneWidth
			- descriptionButtonLaneWidth);
	int lineGap = std::min(LineGap, std::max(0, rect.h / 10));
	int titleFontSize = TitleFontSize;
	int authorVersionFontSize = AuthorVersionFontSize;
	if (titleFontSize + lineGap + authorVersionFontSize > rect.h)
	{
		const int availableFontHeight = std::max(1, rect.h - lineGap);
		titleFontSize = std::max(1, availableFontHeight * 3 / 5);
		authorVersionFontSize = availableFontHeight - titleFontSize;
	}
	const bool showAuthorVersion = authorVersionFontSize > 0;
	const bool badgeHeightAvailable =
		rect.h >= CardBadgeTopInset * 2 + CardBadgeHeight;
	onlineOnlyBadgeVisible = content.onlineOnly
		&& badgeHeightAvailable
		&& contentWidth >= OnlineOnlyBadgeWidth
			+ CardBadgeGap + MinimumVisibleTitleWidth;
	const int onlineOnlyBadgeLaneWidth = onlineOnlyBadgeVisible
		? OnlineOnlyBadgeWidth + CardBadgeGap : 0;
	recentSelectionBadgeVisible = content.wasRecentlySelected
		&& badgeHeightAvailable
		&& contentWidth - onlineOnlyBadgeLaneWidth
			>= RecentSelectionBadgeWidth
				+ CardBadgeGap + MinimumVisibleTitleWidth;
	const int badgeLaneWidth = onlineOnlyBadgeLaneWidth
		+ (recentSelectionBadgeVisible
			? RecentSelectionBadgeWidth + CardBadgeGap : 0);
	const int titleWidth = std::max(1,
		contentWidth - badgeLaneWidth);
	const int contentHeight = titleFontSize
		+ (showAuthorVersion ? lineGap + authorVersionFontSize : 0);
	int lineY = rect.y + std::max(0, (rect.h - contentHeight) / 2);

	titleLabel->visible = true;
	titleLabel->fontSize = titleFontSize;
	titleLabel->color = titleColor;
	titleLabel->rect = { contentX, lineY, titleWidth, titleFontSize };
	lineY += titleFontSize + lineGap;

	authorVersionLabel->visible = showAuthorVersion;
	if (showAuthorVersion)
	{
		authorVersionLabel->fontSize = authorVersionFontSize;
		authorVersionLabel->color = authorVersionColor;
		authorVersionLabel->rect =
			{ contentX, lineY, contentWidth, authorVersionFontSize };
	}
}

void ResourcePackCard::drawHoverBorder(bool pressed)
{
	const uint8_t borderAlpha = pressed ? 255 : (selected ? 230 : 190);
	engine->fillRect(rect.x - 2, rect.y - 2, rect.w + 4, 2, 244, 210, 132, borderAlpha);
	engine->fillRect(rect.x - 2, rect.y + rect.h, rect.w + 4, 2, 244, 210, 132, borderAlpha);
	engine->fillRect(rect.x - 2, rect.y, 2, rect.h, 244, 210, 132, borderAlpha);
	engine->fillRect(rect.x + rect.w, rect.y, 2, rect.h, 244, 210, 132, borderAlpha);
	engine->fillRect(rect.x + 4, rect.y + 4, std::max(1, rect.w - 8), std::max(1, rect.h - 8),
		255, 226, 154, pressed ? 52 : (selected ? 22 : 32));
}

void ResourcePackCard::drawSelectionBox()
{
	Rect selectionBoxRect;
	if (!getSelectionBoxRect(selectionBoxRect))
	{
		return;
	}

	const int boxX = selectionBoxRect.x + SelectionBoxOuterPadding;
	const int boxY = selectionBoxRect.y + SelectionBoxOuterPadding;
	const uint8_t borderRed = selected ? 255 : 136;
	const uint8_t borderGreen = selected ? 214 : 112;
	const uint8_t borderBlue = selected ? 92 : 68;

	engine->fillRect(
		boxX - 3, boxY - 3,
		SelectionBoxSize + 6, SelectionBoxSize + 6,
		45, 24, 13, 255);
	engine->fillRect(
		boxX - 1, boxY - 1,
		SelectionBoxSize + 2, SelectionBoxSize + 2,
		borderRed, borderGreen, borderBlue, 255);
	engine->fillRect(
		boxX + 2, boxY + 2,
		SelectionBoxSize - 4, SelectionBoxSize - 4,
		72, 42, 24, 255);
	if (selected)
	{
		engine->fillRect(
			boxX + 6, boxY + 6,
			SelectionBoxSize - 12, SelectionBoxSize - 12,
			255, 236, 150, 255);
	}
}

void ResourcePackCard::drawSelectionBorder()
{
	constexpr int ShadowWidth = 6;
	constexpr int HighlightInset = 2;
	constexpr int HighlightWidth = 3;

	if (rect.w <= 0 || rect.h <= 0)
	{
		return;
	}

	const int horizontalShadowWidth = std::min(ShadowWidth, rect.h);
	const int verticalShadowWidth = std::min(ShadowWidth, rect.w);
	engine->fillRect(rect.x, rect.y, rect.w, horizontalShadowWidth,
		68, 31, 12, 255);
	engine->fillRect(rect.x, rect.y + rect.h - horizontalShadowWidth,
		rect.w, horizontalShadowWidth, 68, 31, 12, 255);
	engine->fillRect(rect.x, rect.y, verticalShadowWidth, rect.h,
		68, 31, 12, 255);
	engine->fillRect(rect.x + rect.w - verticalShadowWidth, rect.y,
		verticalShadowWidth, rect.h, 68, 31, 12, 255);

	if (rect.w <= HighlightInset * 2 ||
		rect.h <= HighlightInset * 2)
	{
		return;
	}

	const int horizontalHighlightWidth = std::min(
		HighlightWidth, rect.h - HighlightInset * 2);
	const int verticalHighlightWidth = std::min(
		HighlightWidth, rect.w - HighlightInset * 2);
	engine->fillRect(rect.x + HighlightInset, rect.y + HighlightInset,
		rect.w - HighlightInset * 2, horizontalHighlightWidth,
		255, 214, 92, 255);
	engine->fillRect(rect.x + HighlightInset,
		rect.y + rect.h - HighlightInset - horizontalHighlightWidth,
		rect.w - HighlightInset * 2, horizontalHighlightWidth,
		255, 214, 92, 255);
	engine->fillRect(rect.x + HighlightInset, rect.y + HighlightInset,
		verticalHighlightWidth, rect.h - HighlightInset * 2,
		255, 214, 92, 255);
	engine->fillRect(
		rect.x + rect.w - HighlightInset - verticalHighlightWidth,
		rect.y + HighlightInset,
		verticalHighlightWidth, rect.h - HighlightInset * 2,
		255, 214, 92, 255);
}

void ResourcePackCard::drawOnlineOnlyBadge()
{
	Rect badgeRect;
	if (!getOnlineOnlyBadgeRect(badgeRect))
	{
		return;
	}

	engine->fillRect(
		badgeRect.x, badgeRect.y, badgeRect.w, badgeRect.h,
		116, 190, 224, 255);
	engine->fillRect(
		badgeRect.x + 1, badgeRect.y + 1,
		badgeRect.w - 2, badgeRect.h - 2,
		38, 92, 132, 245);
	engine->drawText(
		u8"未下载", badgeRect.x + 7, badgeRect.y + 2,
		CardBadgeFontSize, 0xFFF4FBFF);
}

void ResourcePackCard::drawRecentSelectionBadge()
{
	Rect badgeRect;
	if (!getRecentSelectionBadgeRect(badgeRect))
	{
		return;
	}

	engine->fillRect(
		badgeRect.x, badgeRect.y, badgeRect.w, badgeRect.h,
		246, 177, 89, 255);
	engine->fillRect(
		badgeRect.x + 1, badgeRect.y + 1,
		badgeRect.w - 2, badgeRect.h - 2,
		184, 58, 38, 245);
	engine->drawText(
		u8"上次选择", badgeRect.x + 5, badgeRect.y + 2,
		CardBadgeFontSize, 0xFFFFF1D2);
}

int ResourcePackCard::getHorizontalPadding() const
{
	if (rect.w <= MinimumTextWidth)
	{
		return 0;
	}

	const int preferredPadding = clampInt(
		rect.w / HorizontalPaddingDivisor,
		HorizontalPaddingMinimum,
		HorizontalPaddingMaximum);
	return std::min(
		preferredPadding,
		(rect.w - MinimumTextWidth) / 2);
}

bool ResourcePackCard::getSelectionBoxRect(
	Rect& selectionBoxRect) const
{
	const int horizontalPadding = getHorizontalPadding();
	const int outerSize =
		SelectionBoxSize + SelectionBoxOuterPadding * 2;
	if (rect.h < outerSize ||
		rect.w < horizontalPadding * 2
			+ SelectionBoxLaneWidth + MinimumTextWidth)
	{
		selectionBoxRect = { 0, 0, 0, 0 };
		return false;
	}

	selectionBoxRect =
	{
		rect.x + SelectionBoxLeftInset - SelectionBoxOuterPadding,
		rect.y + (rect.h - SelectionBoxSize) / 2
			- SelectionBoxOuterPadding,
		outerSize,
		outerSize
	};
	return true;
}

bool ResourcePackCard::getOnlineOnlyBadgeRect(
	Rect& badgeRect) const
{
	if (!onlineOnlyBadgeVisible
		|| titleLabel == nullptr
		|| !titleLabel->visible)
	{
		badgeRect = { 0, 0, 0, 0 };
		return false;
	}

	badgeRect =
	{
		titleLabel->rect.x + titleLabel->rect.w + CardBadgeGap,
		rect.y + CardBadgeTopInset,
		OnlineOnlyBadgeWidth,
		CardBadgeHeight
	};
	return true;
}

bool ResourcePackCard::getRecentSelectionBadgeRect(
	Rect& badgeRect) const
{
	if (!recentSelectionBadgeVisible
		|| titleLabel == nullptr
		|| !titleLabel->visible)
	{
		badgeRect = { 0, 0, 0, 0 };
		return false;
	}

	badgeRect =
	{
		titleLabel->rect.x + titleLabel->rect.w
			+ CardBadgeGap
			+ (onlineOnlyBadgeVisible
				? OnlineOnlyBadgeWidth + CardBadgeGap : 0),
		rect.y + CardBadgeTopInset,
		RecentSelectionBadgeWidth,
		CardBadgeHeight
	};
	return true;
}
