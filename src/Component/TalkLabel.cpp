#ifndef _SCL_SECURE_NO_WARNINGS
#define _SCL_SECURE_NO_WARNINGS
#endif 
#include "TalkLabel.h"
#include "../Engine/Engine.h"
#include "../libconvert/libconvert.h"
#include "ComponentRegistry.h"
#include <regex>

namespace
{
	bool registeredTalkLabel = []
	{
		ComponentRegistry::getInstance().registerType("TalkLabel",
			[]() -> std::shared_ptr<BaseComponent> { return std::make_shared<TalkLabel>(); });
		return true;
	}();
}



TalkLabel::TalkLabel()
{
	name = "TalkLabel";
	setPriority(epLabel);
	coverMouse = false;
	canDrag = false;
	canDrop = false;
}


TalkLabel::~TalkLabel()
{
	freeResource();
}

void TalkLabel::initFromIni(INIReader& ini)
{
	Item::initFromIni(ini);
	// An explicit resource contract stays stable if the panel is later scaled.
	configuredLineCount = ini.GetInteger("Init", "LineCount", 0);
	configuredCharactersPerLine =
		ini.GetInteger("Init", "CharactersPerLine", 0);
	configuredLineHeight = ini.GetInteger("Init", "LineHeight", 0);
}

void TalkLabel::setTalkStr(TalkString& tString)
{
	// 保存当前页数据，重置逐字显示状态
	currentTalkData = tString;
	displayedCharCount = 0;
	renderedCharCount = 0;
	lastCharTime = getTime();
	pageComplete = (currentTalkData.talkChar.empty());
	impImage = nullptr;

	// 初始渲染0个字符（清空显示）
	renderUpToIndex(0);
}

void TalkLabel::showAllImmediately()
{
	// 立即显示所有字符，标记当前页完成
	int totalCount = static_cast<int>(currentTalkData.talkChar.size());
	if (displayedCharCount < totalCount)
	{
		displayedCharCount = totalCount;
		renderUpToIndex(displayedCharCount);
		pageComplete = true;
	}
}

void TalkLabel::renderUpToIndex(int endIndex)
{
	int totalChars = static_cast<int>(currentTalkData.talkChar.size());
	if (totalChars == 0)
	{
		return;
	}

	endIndex = convert_max(0, convert_min(endIndex, totalChars));
	_shared_image talkImage = IMP::loadImage(impImage, 0);
	int imageWidth = 0;
	int imageHeight = 0;
	const bool targetSizeMatches = talkImage != nullptr &&
		engine->getImageSize(talkImage, imageWidth, imageHeight) &&
		imageWidth == rect.w && imageHeight == rect.h;
	const bool createTarget = !targetSizeMatches ||
		endIndex < renderedCharCount;
	if (createTarget)
	{
		talkImage = nullptr;
		impImage = nullptr;
		renderedCharCount = 0;
	}

	if (endIndex == renderedCharCount && talkImage != nullptr)
	{
		return;
	}

	const bool beganDrawing = createTarget
		? engine->beginDrawTalk(rect.w, rect.h)
		: engine->beginDrawTalk(talkImage);
	if (!beganDrawing)
	{
		return;
	}

	const int lineHeight = getLineHeight();
	for (int i = renderedCharCount; i < endIndex; i++)
	{
		const TalkChar& character = currentTalkData.talkChar[i];
		engine->drawTalk(
			character.s,
			character.column * fontSize,
			character.row * lineHeight,
			fontSize,
			character.color);
	}
	auto renderedImage = engine->endDrawTalk();
	if (renderedImage == nullptr)
	{
		return;
	}
	if (talkImage == nullptr || renderedImage.get() != talkImage.get())
	{
		impImage = IMP::createIMPImageFromImage(renderedImage);
	}
	renderedCharCount = endIndex;
}

int TalkLabel::getCharactersPerLine() const
{
	if (configuredCharactersPerLine > 0)
	{
		return configuredCharactersPerLine;
	}
	if (fontSize <= 0 || rect.w <= 0)
	{
		return TALK_W_COUNT;
	}
	return convert_max(rect.w / fontSize, 1);
}

int TalkLabel::getLineHeight() const
{
	return configuredLineHeight > 0
		? configuredLineHeight
		: convert_max(fontSize, 1);
}

int TalkLabel::getCharactersPerPage() const
{
	int lines = configuredLineCount;
	if (lines <= 0)
	{
		lines = TALK_H_COUNT;
		if (getLineHeight() > 0 && rect.h > 0)
		{
			lines = convert_max(rect.h / getLineHeight(), 1);
		}
	}
	return getCharactersPerLine() * lines;
}

void TalkLabel::onUpdate()
{
	if (pageComplete || currentTalkData.talkChar.empty())
	{
		return;
	}

	UTime currentTime = getTime();
	int totalChars = static_cast<int>(currentTalkData.talkChar.size());
	int prevCount = displayedCharCount;

	// 根据时间流逝计算应该额外显示的字符数
	while (displayedCharCount < totalChars)
	{
		UTime elapsed = currentTime - lastCharTime;
		if (elapsed < charInterval)
		{
			break;
		}
		displayedCharCount++;
		lastCharTime += charInterval;
	}

	// 仅在字符数发生变化时才重新渲染，避免每帧都做昂贵的纹理操作
	if (displayedCharCount != prevCount)
	{
		renderUpToIndex(displayedCharCount);
	}

	// 检查是否已经全部显示完毕
	if (displayedCharCount >= totalChars)
	{
		pageComplete = true;
	}
}

void TalkLabel::drawItemStr()
{
	//已将字符串保存到image
}

std::vector<TalkString> TalkLabel::splitTalkString(const std::string & tString)
{
	std::string text = tString;
	convert::replaceAllString(text, "<Enter>", "<enter>");
	convert::replaceAllString(text, "\r\n", "<enter>");
	convert::replaceAllString(text, "\r", "<enter>");
	convert::replaceAllString(text, "\n", "<enter>");
	
	std::regex reg(R"(<(enter|color=[^>]+)>)", std::regex::icase);
	std::sregex_iterator end;
	
	std::vector<TalkString> result;
	// Preserve the historical empty-dialog contract without creating an empty
	// trailing page when a non-empty dialog ends exactly on a page boundary.
	result.resize(1);
	const int charactersPerLine = getCharactersPerLine();
	const int lineCount = convert_max(
		getCharactersPerPage() / charactersPerLine,
		1);
	int pageIndex = 0;
	int column = 0;
	int row = 0;
	unsigned int col = color;
	size_t pos = 0;
	
	std::regex regColor(R"(color=([0-9]+),([0-9]+),([0-9]+))", std::regex::icase);
	std::regex regColorAlpha(R"(color=([0-9]+),([0-9]+),([0-9]+),([0-9]+))", std::regex::icase);
	
	auto advanceLine = [&]()
	{
		column = 0;
		row++;
		if (row >= lineCount)
		{
			row = 0;
			pageIndex++;
		}
	};
	auto appendText = [&](const std::string& content)
	{
		for (size_t k = 0; k < content.length();)
		{
			if (column >= charactersPerLine)
			{
				advanceLine();
			}
			unsigned char c = content[k];
			size_t utf8Len = convert_max(
				convert::GetUtf8CharLength(c), 1);
			utf8Len = convert_min(
				utf8Len, content.length() - k);
			if (result.size() <= static_cast<size_t>(pageIndex))
			{
				result.resize(static_cast<size_t>(pageIndex) + 1);
			}
			TalkChar character;
			character.color = col;
			character.s = content.substr(k, utf8Len);
			character.column = column;
			character.row = row;
			result[static_cast<size_t>(pageIndex)].talkChar.push_back(
				character);
			column++;
			k += utf8Len;
		}
	};

	auto it = std::sregex_iterator(text.begin(), text.end(), reg);
	for (; it != end; ++it)
	{
		const std::smatch& match = *it;
		std::string beforeText = text.substr(pos, match.position() - pos);
		appendText(beforeText);
		
		std::string tag = convert::lowerCase(match[1].str());
		if (tag == "enter")
		{
			advanceLine();
		}
		else if (tag == "color=red")
		{
			col = 0xFFFF0000;
		}
		else if (tag == "color=green")
		{
			col = 0xFF00FF00;
		}
		else if (tag == "color=blue")
		{
			col = 0xFF0000FF;
		}
		else if (tag == "color=yellow")
		{
			col = 0xFFFFFF00;
		}
		else if (tag == "color=white")
		{
			col = 0xFFFFFFFF;
		}
		else if (tag == "color=black")
		{
			col = 0xFF000000;
		}
		else if (tag == "color=default")
		{
			col = color;
		}
		else
		{
			std::smatch colorMatch;
			if (std::regex_match(tag, colorMatch, regColorAlpha))
			{
				int r = 0;
				int g = 0;
				int b = 0;
				int a = 0;
				if (convert::parseInteger(colorMatch[1].str(), r) &&
					convert::parseInteger(colorMatch[2].str(), g) &&
					convert::parseInteger(colorMatch[3].str(), b) &&
					convert::parseInteger(colorMatch[4].str(), a))
				{
					col = ((a & 0xFF) << 24) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
				}
			}
			else if (std::regex_match(tag, colorMatch, regColor))
			{
				int r = 0;
				int g = 0;
				int b = 0;
				if (convert::parseInteger(colorMatch[1].str(), r) &&
					convert::parseInteger(colorMatch[2].str(), g) &&
					convert::parseInteger(colorMatch[3].str(), b))
				{
					col = 0xFF000000 | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
				}
			}
		}
		
		pos = match.position() + match.length();
	}
	
	appendText(text.substr(pos));
	
	return result;
}
