#include "System.h"
#include "../../Engine/Engine.h"
#include "SaveLoad.h"
#include "ControllerPromptPresenter.h"
#include "../GameManager/GameManager.h"

System::System(bool focusOptionsValue)
	: focusOptions(focusOptionsValue)
{
	name = "System";
	setPriority(epMax);
	focusManager.setInputAwarePresentation();
	init();
}


System::~System()
{
	freeResource();
}

void System::init()
{
	const std::string preferredFocusId = focusManager.getFocusedNodeId();
	freeResource();
	loadMenuDefinition("ini\\ui\\system\\system.menu.ini");

	title = getComponentByName<ImageContainer>("title");
	returnBtn = getComponentByName<Button>("returnBtn");
	saveloadBtn = getComponentByName<Button>("saveloadBtn");
	optionBtn = getComponentByName<Button>("optionBtn");
	quitBtn = getComponentByName<Button>("quitBtn");

	setChildRectReferToParent();
	configureFocus(preferredFocusId);
}

void System::freeResource()
{
	focusManager.clear();
	title = nullptr;
	returnBtn = nullptr;
	saveloadBtn = nullptr;
	optionBtn = nullptr;
	quitBtn = nullptr;
	ConfigDrivenPanel::freeResource();
}

void System::configureFocus(const std::string& preferredFocusId)
{
	focusManager.clear();
	const std::vector<std::string> focusOrder = focusManager.addVisualLinearGroup(
		"system-actions",
		UIFocusLinearAxis::Vertical,
		{
			{ "return", returnBtn, [this]() { closeToGame(); } },
			{ "save-load", saveloadBtn, [this]() { openSaveLoad(); } },
			{ "options", optionBtn, [this]() { openOptions(); } },
			{ "return-to-title", quitBtn, [this]() { returnToTitle(); } }
		});
	focusManager.applyConfigDrivenFocusNavigation(
		*this,
		{
			{ "returnBtn", "return" },
			{ "saveloadBtn", "save-load" },
			{ "optionBtn", "options" },
			{ "quitBtn", "return-to-title" },
		});
	if (focusOptions && optionBtn != nullptr)
	{
		focusManager.setDefaultFocus("options");
	}
	else if (returnBtn != nullptr)
	{
		focusManager.setDefaultFocus("return");
	}
	else if (!focusOrder.empty())
	{
		focusManager.setDefaultFocus(focusOrder.front());
	}
	focusManager.setCancelHandler([this]() { closeToGame(); });
	if (preferredFocusId.empty()
		|| !focusManager.focusNode(preferredFocusId))
	{
		focusManager.focusDefault();
	}
}

void System::closeToGame()
{
	logicRunning = false;
	result = erOK;
}

void System::openSaveLoad()
{
	auto saveLoad = std::make_shared<SaveLoad>(true, true);
	saveLoad->setPriority(0);
	addChild(saveLoad);
	const unsigned int returnValue = saveLoad->run();
	if ((returnValue & erLoad) != 0)
	{
		index = saveLoad->index;
		result = erLoad;
		logicRunning = false;
	}
	else if ((returnValue & erSave) != 0)
	{
		index = saveLoad->index;
		result = erSave;
		if (GameManager::getInstance()->saveGame(index + 1))
		{
			saveScreen();
		}
		else
		{
			handleSaveFailure();
		}
	}
	removeChild(saveLoad);
}

void System::openOptions()
{
	auto option = std::make_shared<Option>();
	option->setPriority(epMax);
	addChild(option);
	option->run();
	removeChild(option);
}

void System::returnToTitle()
{
	logicRunning = false;
	result = erReturnToTitle;
}

void System::onEvent()
{
	if (saveloadBtn != nullptr && saveloadBtn->getResult(erClick))
	{
		openSaveLoad();
	}
	if (optionBtn != nullptr && optionBtn->getResult(erClick))
	{
		openOptions();
	}
	if (returnBtn != nullptr && returnBtn->getResult(erClick))
	{
		closeToGame();
	}
	if (quitBtn != nullptr && quitBtn->getResult(erClick))
	{
		returnToTitle();
	}
}

void System::handleSaveFailure()
{
	gm->showMessage("存档失败");
	result = erOK;
	logicRunning = false;
}

bool System::onHandleEvent(AEvent & e)
{
	if ((e.eventType == ET_MOUSEDOWN
			&& e.eventData == MBC_MOUSE_LEFT)
		|| e.eventType == ET_FINGERDOWN)
	{
		adoptUIFocusPointerTarget(
			e.eventType == ET_MOUSEDOWN ? TOUCH_MOUSEID : e.eventData);
	}
	if (e.eventType == ET_QUIT)
	{
		logicRunning = false;
		result = erExit;
		return true;
	}
	else if (dispatchKeyboardUIAction(e, *this))
	{
		return true;
	}
	return false;
}

bool System::onHandleUIAction(UIAction action)
{
	return focusManager.handleAction(action);
}

void System::onDrawEnd()
{
	if (!visible || engine == nullptr
		|| !Element::isCurrentRunOwner(this)
		|| !focusManager.isFocusPresented())
	{
		return;
	}
	using GameInput::InputAction;
	const std::vector<ControllerPromptItem> items =
	{
		{ InputAction::NavigateUp, "选择" },
		{ InputAction::Confirm, "确认" },
		{ InputAction::Cancel, "返回游戏" }
	};
	ControllerPromptPresenter::drawBottomBar(
		engine, engine->inputActions(), items);
}

void System::onRun()
{
	focusManager.focusDefault();
}

void System::saveScreen()
{
	std::string imageName = SHOT_FOLDER + convert::formatString(SHOT_PNG, index + 1);
	int w = 260;
	int h = 200;
	if (!engine->beginSaveScreen())
	{
		return;
	}
	gm->map->drawMap();
	gm->weather->draw();
	auto snap = engine->endSaveScreen();
	if (snap == nullptr)
	{
		return;
	}
	std::unique_ptr<char[]> data;
	int len = engine->saveImageToPngMemory(snap, w, h, data);
	if (len > 0 && data.get() != nullptr)
	{
		File::writeFile(imageName, data, len);
	}
}
