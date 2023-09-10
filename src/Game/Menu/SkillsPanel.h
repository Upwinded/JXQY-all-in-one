#pragma once
#include "../../Component/Component.h"

#define SKILL_PANEL_NONE -1
#define SKILL_PANEL_ATTACK 5
#define SKILL_PANEL_SIT 6
#define SKILL_PANEL_SKIIL1 0
#define SKILL_PANEL_SKIIL2 1
#define SKILL_PANEL_SKIIL3 2
#define SKILL_PANEL_SKIIL4 3
#define SKILL_PANEL_SKIIL5 4

#define SKILL_PANEL_FAST_SELECT 7
#define FASTBTN_COUNT 4

class SkillsPanel :
	public Panel
{
public:
	SkillsPanel();
	virtual ~SkillsPanel();

	RoundButton* attackBtn = nullptr;
	RoundButton* sitBtn = nullptr;
	RoundButton* skillBtn1 = nullptr;
	RoundButton* skillBtn2 = nullptr;
	RoundButton* skillBtn3 = nullptr;
	RoundButton* skillBtn4 = nullptr;
	RoundButton* skillBtn5 = nullptr;
	TextButton* fastBtn[FASTBTN_COUNT] = { nullptr, nullptr, nullptr };
	DragRoundButton* rightJumpBtn = nullptr;
private:
	int clickIndex = 0;
	Point dragEndPosition = { 0, 0 };
public:
	Point getDragEndPosition() { return dragEndPosition; }

	int getClickIndex() { return clickIndex; }
	virtual void onChildCallBack(Element * child);

	virtual void onUpdate();
	virtual void init();
	void freeResource();
};

