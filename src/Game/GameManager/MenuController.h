#pragma once
#include "../../Element/Element.h"
#include "../Menu/Menu.h"

class MenuController :
	public Element
{
public:
	MenuController();
	virtual ~MenuController();

	virtual bool onHandleEvent(AEvent & e);

	void init();
	void freeResource();

	bool menuDisplayed();
	void clearMenu();
	void update();
	void showMessage(const std::string& str);
	void hideBottomWnd();
	void showBottomWnd();

	std::shared_ptr<Panel> upMenu = nullptr;

	std::shared_ptr<MsgBox> messageBox = nullptr;
	std::shared_ptr<StateMenu> stateMenu = nullptr;
	std::shared_ptr<ToolTip> toolTip = nullptr;
	std::shared_ptr<MemoMenu> memoMenu = nullptr;
	std::shared_ptr<EquipMenu> equipMenu = nullptr;
	std::shared_ptr<PracticeMenu> practiceMenu = nullptr;
	std::shared_ptr<GoodsMenu> goodsMenu = nullptr;
	std::shared_ptr<MagicMenu> magicMenu = nullptr;

	std::shared_ptr<BottomMenu> bottomMenu = nullptr;

	std::shared_ptr<Dialog> dialog = nullptr;

};

