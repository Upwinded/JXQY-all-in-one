#include "MenuController.h"
#include "GameManager.h"


MenuController::MenuController()
{
	priority = epMenu;
}

MenuController::~MenuController()
{
	removeAllChild();
	freeResource();
}

bool MenuController::onHandleEvent(AEvent & e)
{
	if (e.eventType == ET_QUIT || (e.eventType == ET_KEYDOWN && e.eventData == KEY_ESCAPE))
	{
		if (menuDisplayed())
		{
			clearMenu();
		}
		else
		{
			bottomMenu->optionBtn->checked = true;
			gm->controller->setPaused(true);
			if (currentDragItem != nullptr)
			{
				currentDragItem->dragEnd();
			}
			if (dragging != TOUCH_UNTOUCHEDID)
			{
				dragging = TOUCH_UNTOUCHEDID;
			}
			bool vis = gm->menu->visible;
			gm->menu->visible = false;
			removeChild(gm->menu->toolTip);
			std::shared_ptr<System> system = std::make_shared<System>();
			gm->addChild(system);
			unsigned int ret = system->run();
			gm->removeChild(system);
			system = nullptr;
			if ((ret & erExit) != 0)
			{
				gm->result = erExit;
				gm->setRunning(false);
			}
			gm->controller->setPaused(false);
			gm->menu->visible = vis;
			bottomMenu->optionBtn->checked = false;
		}
		return true;
	}
	else if (e.eventType == ET_KEYDOWN)
	{
		if (e.eventData == KEY_F1)
		{
			stateMenu->visible = !stateMenu->visible;
			stateMenu->updateLabel();
			practiceMenu->visible = false;
			equipMenu->visible = false;
			bottomMenu->stateBtn->checked = stateMenu->visible;
			bottomMenu->xiulianBtn->checked = false;
			bottomMenu->equipBtn->checked = false;
			return true;
		}
		else if (e.eventData == KEY_F2)
		{
			equipMenu->visible = !equipMenu->visible;
			practiceMenu->visible = false;
			stateMenu->visible = false;
			bottomMenu->equipBtn->checked = equipMenu->visible;
			bottomMenu->xiulianBtn->checked = false;
			bottomMenu->stateBtn->checked = false;
			return true;
		}
		else if (e.eventData == KEY_F3)
		{
			practiceMenu->visible = !practiceMenu->visible;
			equipMenu->visible = false;
			stateMenu->visible = false;
			bottomMenu->xiulianBtn->checked = practiceMenu->visible;
			bottomMenu->equipBtn->checked = false;
			bottomMenu->stateBtn->checked = false;
			return true;
		}
		else if (e.eventData == KEY_F5)
		{
			goodsMenu->visible = !goodsMenu->visible;
			magicMenu->visible = false;
			memoMenu->visible = false;
			bottomMenu->goodsBtn->checked = goodsMenu->visible;
			bottomMenu->notesBtn->checked = false;
			bottomMenu->magicBtn->checked = false;
			return true;
		}
		else if (e.eventData == KEY_F6)
		{
			magicMenu->visible = !magicMenu->visible;
			memoMenu->visible = false;
			goodsMenu->visible = false;
			bottomMenu->magicBtn->checked = magicMenu->visible;
			bottomMenu->goodsBtn->checked = false;
			bottomMenu->notesBtn->checked = false;
			return true;
		}
		else if (e.eventData == KEY_F7)
		{
			memoMenu->visible = !memoMenu->visible;
			magicMenu->visible = false;
			goodsMenu->visible = false;
			bottomMenu->notesBtn->checked = memoMenu->visible;
			bottomMenu->goodsBtn->checked = false;
			bottomMenu->magicBtn->checked = false;
			return true;
		}
	}
	return false;
}

void MenuController::init()
{
#define AddUpMenuChild(A, a); a = std::make_shared<A>(); upMenu->addChild(a); 
#define AddMenuChild(A, a);  a = std::make_shared<A>(); addChild(a); 
	
	freeResource();

	upMenu = std::make_shared<Panel>();
	addChild(upMenu);

	AddUpMenuChild(MsgBox, messageBox);
	AddUpMenuChild(StateMenu, stateMenu);
	AddUpMenuChild(ToolTip, toolTip);
	AddUpMenuChild(MemoMenu, memoMenu);
	AddUpMenuChild(EquipMenu, equipMenu);
	AddUpMenuChild(PracticeMenu, practiceMenu);
	AddUpMenuChild(GoodsMenu, goodsMenu);
	AddUpMenuChild(MagicMenu, magicMenu);
	AddMenuChild(BottomMenu, bottomMenu);
	AddMenuChild(Dialog, dialog);
	dialog->visible = false;
}

#define freeMenu(component); \
	removeChild(component); \
	if (component.get() != nullptr)\
	{\
		component = nullptr; \
	}

#define freeMenuofUp(component); \
	if (upMenu.get() != nullptr)\
	{\
		upMenu->removeChild(component); \
	}\
	if (component.get() != nullptr)\
	{\
		component = nullptr; \
	}

void MenuController::freeResource()
{

	freeMenuofUp(messageBox);
	freeMenuofUp(stateMenu);
	freeMenuofUp(toolTip);
	freeMenuofUp(memoMenu);
	freeMenuofUp(equipMenu);
	freeMenuofUp(practiceMenu);
	freeMenuofUp(goodsMenu);
	freeMenuofUp(magicMenu);

	freeMenu(upMenu);
	freeMenu(bottomMenu);
	freeMenu(dialog);

}

bool MenuController::menuDisplayed()
{
	bool vis = false;
#define addMenuVis(m) \
	if (m != nullptr)\
	{\
		vis = (vis || m->visible);\
	}
	addMenuVis(stateMenu)
	addMenuVis(memoMenu);
	addMenuVis(equipMenu);
	addMenuVis(practiceMenu);
	addMenuVis(goodsMenu);
	addMenuVis(magicMenu);

	return vis;
}

void MenuController::clearMenu()
{
	practiceMenu->visible = false;
	equipMenu->visible = false;
	stateMenu->visible = false;
	magicMenu->visible = false;
	memoMenu->visible = false;
	goodsMenu->visible = false;
	bottomMenu->magicBtn->checked = false;
	bottomMenu->goodsBtn->checked = false;
	bottomMenu->notesBtn->checked = false;
	bottomMenu->stateBtn->checked = false;
	bottomMenu->xiulianBtn->checked = false;
	bottomMenu->equipBtn->checked = false;
}

void MenuController::update()
{
	bottomMenu->updateGoodsItem();
	bottomMenu->updateMagicItem();

	goodsMenu->scrollbar->setPosition(goodsMenu->scrollbar->min);
	goodsMenu->updateGoods();

	magicMenu->scrollbar->setPosition(magicMenu->scrollbar->min);
	magicMenu->updateMagic();

	practiceMenu->updateMagic();
	equipMenu->updateGoods();
	goodsMenu->updateMoney();
	memoMenu->reRange((int)gm->memo.memo.size() > 0 ? (int)gm->memo.memo.size() - 1 : 0);
	memoMenu->reset();
}

void MenuController::showMessage(const std::string& str)
{
	if (messageBox != nullptr)
	{
		messageBox->showMessage(str);
	}
}

void MenuController::hideBottomWnd()
{
	bottomMenu->visible = false;
}

void MenuController::showBottomWnd()
{
	bottomMenu->visible = true;
}
