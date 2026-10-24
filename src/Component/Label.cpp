#include "Label.h"
#include "../Engine/Engine.h"
#include "ComponentRegistry.h"
#include "TextLayout.h"

#include <algorithm>
#include <limits>

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

const _shared_image& CachedTextTexture::get(
	Engine* engine,
	const std::string& text,
	int fontSize,
	unsigned int color)
{
	if (engine == nullptr || text.empty() || fontSize <= 0)
	{
		clear();
		return image;
	}
	if (image != nullptr
		&& cachedText == text
		&& cachedFontSize == fontSize
		&& cachedColor == color)
	{
		return image;
	}

	image = engine->createText(text, fontSize, color);
	if (image != nullptr)
	{
		cachedText = text;
		cachedFontSize = fontSize;
		cachedColor = color;
	}
	return image;
}

void CachedTextTexture::draw(
	Engine* engine,
	const std::string& text,
	int x,
	int y,
	int fontSize,
	unsigned int color)
{
	const _shared_image& textImage = get(engine, text, fontSize, color);
	if (engine != nullptr && textImage != nullptr)
	{
		engine->drawImage(textImage, x, y);
	}
}

void CachedTextTexture::drawWithAlpha(
	Engine* engine,
	const std::string& text,
	int x,
	int y,
	int fontSize,
	unsigned int color,
	unsigned char alpha)
{
	const _shared_image& textImage = get(engine, text, fontSize, color);
	if (engine != nullptr && textImage != nullptr)
	{
		engine->drawImageWithBlendAlpha(
			textImage, x, y, alpha, SDL_BLENDMODE_BLEND);
	}
}

void CachedTextTexture::clear()
{
	image = nullptr;
	cachedText.clear();
	cachedFontSize = -1;
	cachedColor = 0;
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
	autoNextLine = ini.GetBoolean("Init", "AutoNextLine", autoNextLine);
	if (ini.GetBoolean("Init", "CenterText", false))
	{
		horizontalAlignment = TextHorizontalAlignment::Center;
		verticalAlignment = TextVerticalAlignment::Center;
	}
	setStr(ini.Get("Init", "Text", ""));
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

void Label::setColorTagsEnabled(bool enabled)
{
	if (interpretColorTags != enabled)
	{
		interpretColorTags = enabled;
		invalidateTextLayout();
	}
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
		&& renderedColorTagsEnabled == interpretColorTags
		&& renderedMinimumFontSize == minimumFontSize)
	{
		return;
	}

	strImage.clear();
	renderedFontSize = std::max(1, fontSize);
	renderedLineHeight = renderedFontSize;

	const bool hasColorTags = interpretColorTags
		&& str.find("<color=") != std::string::npos;
	const int smallestFontSize = autoShrink
		? std::clamp(minimumFontSize, 1, renderedFontSize)
		: renderedFontSize;
	std::vector<TextLayout::ColorTaggedTextLine> lines;
	for (;;)
	{
		int measuredWidth = 0;
		int measuredHeight = 0;
		bool measured = engine->measureText("", renderedFontSize, measuredWidth, measuredHeight);
		renderedLineHeight = measured ? std::max(1, measuredHeight) : renderedFontSize;
		const auto measureWidth = [&](const std::string& text)
		{
			int width = 0;
			int height = 0;
			measured = engine->measureText(text, renderedFontSize, width, height) && measured;
			renderedLineHeight = std::max(renderedLineHeight, height);
			return width;
		};
		const int maximumWidth = autoNextLine ? std::max(1, rect.w) : std::numeric_limits<int>::max();
		lines.clear();
		if (hasColorTags)
		{
			lines = TextLayout::wrapColorTaggedUtf8Text(str, maximumWidth, color, measureWidth);
		}
		else if (autoNextLine)
		{
			for (const auto& line : TextLayout::wrapUtf8Text(str, maximumWidth, measureWidth))
			{
				lines.push_back({ { line, color } });
			}
		}
		else if (!str.empty())
		{
			lines.push_back({ { str, color } });
		}
		int widestLine = 0;
		for (const auto& line : lines)
		{
			int width = 0;
			for (const auto& run : line)
			{
				width += measureWidth(run.text);
			}
			widestLine = std::max(widestLine, width);
		}
		const int totalHeight = static_cast<int>(lines.size()) * renderedLineHeight;
		if (renderedFontSize <= smallestFontSize || !measured
			|| ((rect.w <= 0 || widestLine <= rect.w) && (rect.h <= 0 || totalHeight <= rect.h)))
		{
			break;
		}
		--renderedFontSize;
	}

	for (const auto& line : lines)
	{
		std::vector<_shared_image> runImages;
		int lineWidth = 0;
		std::string plainLine;
		for (const auto& run : line)
		{
			plainLine += run.text;
			_shared_image runImage = run.text.empty()
				? nullptr : engine->createText(run.text, renderedFontSize, run.color);
			int runWidth = 0;
			int runHeight = 0;
			if (engine->getImageSize(runImage, runWidth, runHeight))
			{
				lineWidth += runWidth;
				renderedLineHeight = std::max(renderedLineHeight, runHeight);
			}
			runImages.push_back(std::move(runImage));
		}
		if (runImages.empty())
		{
			strImage.push_back(nullptr);
		}
		else if (runImages.size() == 1 && !hasColorTags)
		{
			if (autoShrink && elideOverflow && !autoNextLine && rect.w > 0 && lineWidth > rect.w)
			{
				runImages.front() = createElidedTextImage(engine, plainLine, renderedFontSize, color, rect.w);
			}
			strImage.push_back(runImages.front());
		}
		else if (!engine->beginDrawTalk(std::max(1, lineWidth), renderedLineHeight))
		{
			strImage.push_back(engine->createText(plainLine, renderedFontSize, color));
		}
		else
		{
			int drawX = 0;
			for (const auto& runImage : runImages)
			{
				int runWidth = 0;
				int runHeight = 0;
				if (engine->getImageSize(runImage, runWidth, runHeight))
				{
					engine->drawImage(runImage, drawX, 0);
					drawX += runWidth;
				}
			}
			strImage.push_back(engine->endDrawTalk());
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
	renderedColorTagsEnabled = interpretColorTags;
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
	return static_cast<int>(strImage.size()) * std::max(renderedLineHeight, 1);
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
	const int lineHeight = std::max(renderedLineHeight, 1);
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
		if (rect.w <= 0 || rect.h <= 0 || lineY + imageHeight > rect.y + rect.h)
		{
			break;
		}
		Rect sourceRect = {
			0,
			0,
			std::min(imageWidth, rect.w),
			imageHeight
		};
		if (sourceRect.w > 0 && sourceRect.h > 0)
		{
			Rect destinationRect = { drawX, lineY, sourceRect.w, sourceRect.h };
			engine->drawImage(strImage[i], &sourceRect, &destinationRect);
		}
	}
}
