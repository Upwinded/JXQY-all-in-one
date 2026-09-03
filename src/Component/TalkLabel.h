#pragma once
#include "Item.h"
#include "../libconvert/libconvert.h"

#define TALK_W_COUNT 19
#define TALK_H_COUNT 3

// 每个对话字符的结构体，包含字符内容和颜色
struct TalkChar
{
	unsigned int color = 0xFF000000;
	std::string s = "";
	int column = 0;
	int row = 0;
};

// 一页对话内容，由多个字符组成
struct TalkString
{
	std::vector<TalkChar> talkChar;
};

// 对话标签组件，支持逐字打字机效果显示
class TalkLabel :
	public Item
{
public:
	TalkLabel();
	virtual ~TalkLabel();
	virtual void initFromIni(INIReader& ini) override;

	// 设置对话文本（存储数据，启动逐字显示）
	virtual void setTalkStr(TalkString& tString);
	virtual void drawItemStr();

	// 将原始文本按标签和页面大小拆分成多页
	std::vector<TalkString> splitTalkString(const std::string & tString);

	// 立即显示当前页全部文字（跳过打字动画）
	void showAllImmediately();

	// 当前页文字是否已经全部显示完毕
	bool isPageComplete() const { return pageComplete; }

	// 设置每个字符的显示间隔（毫秒）
	void setCharInterval(unsigned int interval) { charInterval = interval; }

protected:
	// 每帧更新，驱动逐字显示逻辑
	virtual void onUpdate() override;

private:
	virtual void setStr(const std::string & ws) {};

	// 将尚未渲染的字符追加到当前页纹理
	void renderUpToIndex(int endIndex);
	int getCharactersPerLine() const;
	int getCharactersPerPage() const;
	int getLineHeight() const;

	// 当前页的对话数据
	TalkString currentTalkData;

	// 当前已显示的字符数
	int displayedCharCount = 0;
	int renderedCharCount = 0;

	// 上次增加字符的时间点
	UTime lastCharTime = 0;

	// 每个字符的显示间隔（毫秒）
	unsigned int charInterval = 60;

	// 当前页是否已经全部显示完毕
	bool pageComplete = true;
	int configuredLineCount = 0;
	int configuredCharactersPerLine = 0;
	int configuredLineHeight = 0;
};
