#pragma once
#include "../../Element/Element.h"
#include "../Menu/Menu.h"

class MenuController :
	public Element
{
public:
	MenuController();
	virtual ~MenuController();

	virtual bool onHandleEvent(AEvent* e);

	void init();
	void freeResource();

	bool menuDisplayed();
	void clearMenu();
	void update();
	void showMessage(const std::string& str);
	void hideBottomWnd();
	void showBottomWnd();

	Panel upMenu;

	MsgBox* messageBox = nullptr;
	StateMenu* stateMenu = nullptr;
	ToolTip* toolTip = nullptr;
	MemoMenu* memoMenu = nullptr;
	EquipMenu* equipMenu = nullptr;
	PracticeMenu* practiceMenu = nullptr;
	GoodsMenu* goodsMenu = nullptr;
	MagicMenu* magicMenu = nullptr;

	BottomMenu* bottomMenu = nullptr;

};

