#pragma once
#include "../../Component/Component.h"

#define SKILL_PANEL_NONE -1
#define SKILL_PANEL_ATTACK 5
#define SKILL_PANEL_SIT 6
#define SKILL_PANEL_SKILL_COUNT 5
#define SKILL_PANEL_SKILL1 0
#define SKILL_PANEL_SKILL2 1
#define SKILL_PANEL_SKILL3 2
#define SKILL_PANEL_SKILL4 3
#define SKILL_PANEL_SKILL5 4
#define SKILL_PANEL_JUMP 7

#define SKILL_PANEL_FAST_SELECT 20
#define FASTBTN_COUNT 4

class SkillsPanel :
	public Panel
{
public:
	SkillsPanel();
	virtual ~SkillsPanel();

	std::shared_ptr<RoundButton> attackBtn = nullptr;
	std::shared_ptr<RoundButton> sitBtn = nullptr;
	std::shared_ptr<DragRoundButton> skillBtn[SKILL_PANEL_SKILL_COUNT];
	std::shared_ptr<TextButton> fastBtn[FASTBTN_COUNT] = { nullptr, nullptr, nullptr, nullptr };
	std::shared_ptr<DragRoundButton> rightJumpBtn = nullptr;
private:
	int clickIndex = 0;
	Point dragEndPosition = { 0, 0 };
	bool _jumpBtnDagging = false;

	UTime dragBeginTime = 0;
public:
	Point getDragEndPosition() { return dragEndPosition; }

	int getClickIndex() { return clickIndex; }

protected:
	virtual void initFromIni(INIReader& ini);
	virtual void onChildCallBack(PElement child);
	virtual void onDraw();
	virtual void onUpdate();
	virtual void init();
	void freeResource();
	void drawJumpIndicate(Point pos);
	void drawIndicate(Point pos);

	_shared_imp indicateImp;

};

