#include "Label.h"
#include "../Engine/Engine.h"
#include "ComponentRegistry.h"
#include "TextLayout.h"

#include <algorithm>

namespace
{
	bool registeredLabel = []
	{
		ComponentRegistry::getInstance().registerType("Label",
			[]() -> std::shared_ptr<BaseComponent> { return std::make_shared<Label>(); });
		return true;
	}();

_shared_image createElidedTextImage(Engine* engine, const std::string& text,
	int fontSize, unsigned int color, int maximumWidth)
{
	if (engine == nullptr || text.empty() || maximumWidth <= 0)
	{
		return nullptr;
	}

	std::vector<std::size_t> characterEnds;
	for (std::size_t offset = 0; offset < text.size();)
	{
		++offset;
		while (offset < text.size()
			&& (static_cast<unsigned char>(text[offset]) & 0xC0) == 0x80)
		{
			++offset;
		}
		characterEnds.push_back(offset);
	}

	const std::string ellipsis = u8"…";
	_shared_image fittedImage = nullptr;
	int low = 0;
	int high = static_cast<int>(characterEnds.size());
	while (low <= high)
	{
		const int characterCount = low + (high - low) / 2;
		const std::size_t prefixLength = characterCount > 0
			? characterEnds[characterCount - 1]
			: 0;
		_shared_image candidateImage = engine->createText(
			text.substr(0, prefixLength) + ellipsis, fontSize, color);
		int candidateWidth = 0;
		int candidateHeight = 0;
		if (candidateImage == nullptr
			|| !engine->getImageSize(candidateImage, candidateWidth, candidateHeight))
		{
			return nullptr;
		}
		if (candidateWidth <= maximumWidth)
		{
			fittedImage = candidateImage;
			low = characterCount + 1;
		}
		else
		{
			high = characterCount - 1;
		}
	}
	return fittedImage;
}
}

Label::Label()
{
	name = "Label";
	setPriority(epLabel);
	coverMouse = false;
	canDrag = false;
	canDrop = false;
}

Label::~Label()
{
	freeResource();
}

void Label::initFromIni(INIReader & ini)
{
	Item::initFromIni(ini);
	autoShrink = ini.GetBoolean("Init", "AutoShrink", autoShrink);
	elideOverflow = ini.GetBoolean("Init", "ElideOverflow", elideOverflow);
	minimumFontSize = ini.GetInteger("Init", "MinimumFont", minimumFontSize);
}

void Label::setStr(const std::string & s)
{
	if (str != s)
	{
		str = s;
		invalidateTextLayout();
	}
	refreshTextLayout();
}

void Label::refreshTextLayout()
{
	if (textLayoutValid
		&& renderedText == str
		&& renderedRectWidth == rect.w
		&& renderedRectHeight == rect.h
		&& renderedRequestedFontSize == fontSize
		&& renderedColor == color
		&& renderedAutoNextLine == autoNextLine
		&& renderedAutoShrink == autoShrink
		&& renderedElideOverflow == elideOverflow
		&& renderedMinimumFontSize == minimumFontSize)
	{
		return;
	}

	for (auto& image : strImage)
	{
		image = nullptr;
	}
	strImage.clear();
	renderedFontSize = fontSize;

	if (autoNextLine)
	{
		int charactersPerLine = TextLayout::charactersPerLineForWidth(rect.w, fontSize);
		auto lines = TextLayout::wrapUtf8Text(str, charactersPerLine);
		for (const auto& line : lines)
		{
			strImage.push_back(line.empty() ? nullptr : engine->createText(line, fontSize, color));
		}
	}
	else if (!str.empty())
	{
		if (autoShrink)
		{
			const int smallestFontSize = std::clamp(minimumFontSize, 1, std::max(1, fontSize));
			int textFontSize = std::max(1, fontSize);
			_shared_image img = nullptr;
			for (; textFontSize >= smallestFontSize; textFontSize--)
			{
				img = engine->createText(str, textFontSize, color);
				int textWidth = 0;
				int textHeight = 0;
				if (!engine->getImageSize(img, textWidth, textHeight)
					|| rect.w <= 0 || rect.h <= 0
					|| (textWidth <= rect.w && textHeight <= rect.h))
				{
					break;
				}
			}
			textFontSize = std::max(textFontSize, smallestFontSize);
			if (elideOverflow && img != nullptr && rect.w > 0)
			{
				int textWidth = 0;
				int textHeight = 0;
				if (engine->getImageSize(img, textWidth, textHeight) && textWidth > rect.w)
				{
					img = createElidedTextImage(engine, str, textFontSize, color, rect.w);
				}
			}
			renderedFontSize = textFontSize;
			strImage.push_back(img);
		}
		else
		{
			strImage.push_back(engine->createText(str, fontSize, color));
		}
	}

	renderedText = str;
	renderedRectWidth = rect.w;
	renderedRectHeight = rect.h;
	renderedRequestedFontSize = fontSize;
	renderedColor = color;
	renderedAutoNextLine = autoNextLine;
	renderedAutoShrink = autoShrink;
	renderedElideOverflow = elideOverflow;
	renderedMinimumFontSize = minimumFontSize;
	textLayoutValid = true;
}

void Label::invalidateTextLayout()
{
	textLayoutValid = false;
}

int Label::getRenderedTextHeight()
{
	refreshTextLayout();
	return static_cast<int>(strImage.size()) * std::max(renderedFontSize, 1);
}

void Label::onMouseLeftDown(int x, int y)
{
	if (parent != nullptr && parent->canCallBack)
	{
		result |= erMouseLDown;
		parent->onChildCallBack(getMySharedPtr());
	}
}

void Label::freeResource()
{
	impImage = nullptr;
	for (size_t i = 0; i < strImage.size(); i++)
	{
		if (strImage[i] != nullptr)
		{
			//engine->freeImage(strImage[i]);
			strImage[i] = nullptr;
		}
	}
	strImage.resize(0);
	invalidateTextLayout();
	Item::freeResource();
}

void Label::drawItemStr()
{
	refreshTextLayout();
	const int lineHeight = renderedFontSize > 0 ? renderedFontSize : fontSize;
	const int contentHeight = static_cast<int>(strImage.size()) * std::max(lineHeight, 1);
	int startY = rect.y;
	if (verticalAlignment == TextVerticalAlignment::Center)
	{
		startY += std::max(0, (rect.h - contentHeight) / 2);
	}
	else if (verticalAlignment == TextVerticalAlignment::Bottom)
	{
		startY += std::max(0, rect.h - contentHeight);
	}
	for (size_t i = 0; i < strImage.size(); i++)
	{
		int lineY = startY + static_cast<int>(i) * lineHeight;
		if (strImage[i] == nullptr)
		{
			continue;
		}
		if (!autoNextLine && horizontalAlignment == TextHorizontalAlignment::Left)
		{
			engine->drawImage(strImage[i], rect.x, lineY);
			continue;
		}
		int imageWidth = 0;
		int imageHeight = 0;
		if (!engine->getImageSize(strImage[i], imageWidth, imageHeight))
		{
			continue;
		}
		int drawX = rect.x;
		if (horizontalAlignment == TextHorizontalAlignment::Center)
		{
			drawX += std::max(0, (rect.w - imageWidth) / 2);
		}
		else if (horizontalAlignment == TextHorizontalAlignment::Right)
		{
			drawX += std::max(0, rect.w - imageWidth);
		}
		if (!autoNextLine)
		{
			engine->drawImage(strImage[i], drawX, lineY);
			continue;
		}
		if (rect.w <= 0 || rect.h <= 0 || lineY >= rect.y + rect.h)
		{
			break;
		}
		Rect sourceRect = {
			0,
			0,
			std::min(imageWidth, rect.w),
			std::min(imageHeight, rect.y + rect.h - lineY)
		};
		if (sourceRect.w > 0 && sourceRect.h > 0)
		{
			Rect destinationRect = { drawX, lineY, sourceRect.w, sourceRect.h };
			engine->drawImage(strImage[i], &sourceRect, &destinationRect);
		}
	}
}
