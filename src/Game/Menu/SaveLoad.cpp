#include "SaveLoad.h"
#include "../../Engine/Engine.h"
#include "ControllerPromptPresenter.h"
#include "../../Engine/SaveShotSafety.h"
#include "../GameManager/SaveFileManager.h"
#include "../GameManager/GameManager.h"
#include "../Config/Config.h"

#include <vector>

SaveLoad::SaveLoad(bool s, bool l)
{
	name = "SaveLoad";
	save = s;
	load = l;
	focusManager.setInputAwarePresentation();
	init();
}

SaveLoad::~SaveLoad()
{
	freeResource();
}

void SaveLoad::init()
{
	const std::string preferredFocusId = focusManager.getFocusedNodeId();
	freeResource();
	std::string menuDefinition = "ini\\ui\\saveload\\saveload.menu.ini";
	// Title opens this menu before a GameManager exists. In-game menus keep
	// using the current world's skin until the player re-enters the game.
	const std::string qingyuDefinition = "ini\\ui\\qingyu\\saveload\\saveload.menu.ini";
	if (gm == nullptr && Config::useQingyuUi && File::fileExist(qingyuDefinition))
	{
		menuDefinition = qingyuDefinition;
	}
	loadMenuDefinition(menuDefinition);

	saveBtn = getComponentByName<Button>("saveBtn");
	loadBtn = getComponentByName<Button>("loadBtn");
	exitBtn = getComponentByName<Button>("exitBtn");
	snap = getComponentByName<ImageContainer>("snap");
	listBox = getComponentByName<ListBox>("listBox");

	// Older resource lists may still expose a tenth row without automatic-save support.
	if (listBox)
	{
		for (const auto& definition : getComponentDefinitions())
		{
			if (definition.name != "listBox") continue;
			INIReader ini(definition.file);
			const int maximumSlots = ini.GetBoolean("Init", "AutomaticSave", false)
				? SaveSlots::Automatic : SaveSlots::ManualCount;
			if (listBox->itemCount > maximumSlots)
			{
				ini.SetInteger("Init", "ItemCount", maximumSlots);
				listBox->initFromIni(ini);
			}
			break;
		}
	}

	if (snap)
	{
		snap->stretch = true;
		snap->cropContent = true;
	}
	if (!save && saveBtn)
	{
		saveBtn->visible = false;
	}
	if (!load && loadBtn)
	{
		loadBtn->visible = false;
	}

	setChildRectReferToParent();
	if (listBox != nullptr && index >= 0)
	{
		if (listBox->setSelectedIndex(index))
		{
			refreshSelectedSlotPreview();
		}
		else
		{
			index = -1;
		}
	}
	configureFocus(preferredFocusId);
}

void SaveLoad::configureFocus(const std::string& preferredFocusId)
{
	focusManager.clear();
	auto isFocusableAction = [](const PElement& element)
	{
		return element != nullptr && element->visible && element->activated
			&& element->rect.w >= 2 && element->rect.h >= 2;
	};
	UIFocusManager::NavigationHandler slotNavigation =
		[this](UIFocusDirection direction)
	{
		if (direction == UIFocusDirection::Up)
		{
			return moveSlotSelection(-1);
		}
		if (direction == UIFocusDirection::Down)
		{
			return moveSlotSelection(1);
		}
		return false;
	};

	const bool hasLoadAction = load && isFocusableAction(loadBtn);
	const bool hasSaveAction = save && isFocusableAction(saveBtn);
	const bool hasExitAction = isFocusableAction(exitBtn);
	const std::vector<std::string> focusOrder = focusManager.addLinearGroup(
		"save-load-actions",
		UIFocusLinearAxis::Horizontal,
		{
			{
				"load",
				hasLoadAction ? PElement(loadBtn) : PElement(),
				[this]() { requestLoad(); },
				UIFocusManager::ActionHandler(),
				UIFocusManager::ActionHandler(),
				slotNavigation
			},
			{
				"save",
				hasSaveAction ? PElement(saveBtn) : PElement(),
				[this]() { requestSave(); },
				UIFocusManager::ActionHandler(),
				UIFocusManager::ActionHandler(),
				slotNavigation
			},
			{
				"exit",
				hasExitAction ? PElement(exitBtn) : PElement(),
				[this]() { closeMenu(); },
				UIFocusManager::ActionHandler(),
				UIFocusManager::ActionHandler(),
				slotNavigation
			}
		});
	focusManager.applyConfigDrivenFocusNavigation(
		*this,
		{
			{ "loadBtn", "load" },
			{ "saveBtn", "save" },
			{ "exitBtn", "exit" },
		});
	if (!focusOrder.empty())
	{
		focusManager.setDefaultFocus(focusOrder.front());
	}
	focusManager.setCancelHandler([this]() { closeMenu(); });
	if (preferredFocusId.empty()
		|| !focusManager.focusNode(preferredFocusId))
	{
		focusManager.focusDefault();
	}
}

bool SaveLoad::moveSlotSelection(int delta)
{
	if (listBox == nullptr || listBox->itemButton.empty() || delta == 0)
	{
		return false;
	}
	const int itemCount = static_cast<int>(listBox->itemButton.size());
	int selectedIndex = listBox->index;
	if (selectedIndex < 0 || selectedIndex >= itemCount)
	{
		selectedIndex = delta < 0 ? itemCount - 1 : 0;
	}
	else
	{
		const int direction = delta < 0 ? -1 : 1;
		selectedIndex = (selectedIndex + direction + itemCount) % itemCount;
	}
	return selectSlot(selectedIndex);
}

bool SaveLoad::selectSlot(int selectedIndex)
{
	if (listBox == nullptr)
	{
		return false;
	}
	const bool selectionChanged = index != selectedIndex || listBox->index != selectedIndex;
	if (!listBox->setSelectedIndex(selectedIndex))
	{
		return false;
	}
	index = selectedIndex;
	if (selectionChanged)
	{
		refreshSelectedSlotPreview();
	}
	return true;
}

void SaveLoad::refreshSelectedSlotPreview()
{
	if (saveBtn)
	{
		saveBtn->activated = save && index + 1 != SaveSlots::Automatic;
		saveBtn->visible = saveBtn->activated;
		configureFocus(focusManager.getFocusedNodeId());
	}
	if (snap != nullptr)
	{
		snap->impImage = nullptr;
	}
	if (index < 0)
	{
		return;
	}

	std::string imageName = "save/" + SaveSlots::screenshot(index + 1);
	std::string legacyImageName = "save/" + SaveSlots::screenshot(index + 1, ".bmp");

	_shared_image img = nullptr;
	File::visitReadableResources({ imageName, legacyImageName },
		SaveShotSafety::MaximumFileBytes,
		[&](const std::string&, std::unique_ptr<char[]>& data, int size)
		{
			img = engine->loadImageFromMem(data, size);
			if (img == nullptr)
			{
				SaveShotSafety::View saveShot;
				if (SaveShotSafety::parse(data.get(), size, saveShot))
				{
					img = engine->loadSaveShotFromPixels(saveShot.width,
						saveShot.height, saveShot.pixels, saveShot.pixelBytes);
				}
			}
			return img != nullptr;
		});
	if (img != nullptr && snap != nullptr)
	{
		snap->impImage = IMP::createIMPImageFromImage(img);
	}
}

void SaveLoad::requestSave()
{
	if (!save || !SaveSlots::isManual(index + 1))
	{
		return;
	}
	if (gm != nullptr && gm->inEvent)
	{
		gm->showMessage("事件进行中，暂时无法存档");
		return;
	}
	if (gm != nullptr && gm->global.data.saveDisabled)
	{
		gm->showMessage("此处无法存档");
		return;
	}
	if (index >= 0)
	{
		result = erSave;
		logicRunning = false;
	}
}

void SaveLoad::requestLoad()
{
	if (!load || index < 0)
	{
		return;
	}
	if (SaveFileManager::HasSaveFile(index + 1))
	{
		result = erLoad;
		logicRunning = false;
	}
}

void SaveLoad::closeMenu()
{
	result = erOK;
	logicRunning = false;
}

void SaveLoad::onEvent()
{
	if (listBox == nullptr)
	{
		result = erExit;
		logicRunning = false;
		return;
	}
	if (index != listBox->index)
	{
		if (listBox->index < 0)
		{
			index = -1;
			refreshSelectedSlotPreview();
		}
		else
		{
			selectSlot(listBox->index);
		}
	}
	if (exitBtn != nullptr && exitBtn->getResult(erClick))
	{
		closeMenu();
		return;
	}
	if (save && saveBtn != nullptr && saveBtn->getResult(erClick))
	{
		requestSave();
		return;
	}
	if (load && loadBtn != nullptr && loadBtn->getResult(erClick))
	{
		requestLoad();
	}
}

void SaveLoad::freeResource()
{
	controllerPromptTextureCache.itemTextTextures.clear();
	focusManager.clear();
	snap = nullptr;
	saveBtn = nullptr;
	exitBtn = nullptr;
	loadBtn = nullptr;
	listBox = nullptr;
	ConfigDrivenPanel::freeResource();
}

bool SaveLoad::onHandleEvent(AEvent & e)
{
	if ((e.eventType == ET_MOUSEDOWN
			&& e.eventData == MBC_MOUSE_LEFT)
		|| e.eventType == ET_FINGERDOWN)
	{
		adoptUIFocusPointerTarget(
			e.eventType == ET_MOUSEDOWN ? TOUCH_MOUSEID : e.eventData);
	}
	if (dispatchKeyboardUIAction(e, *this))
	{
		return true;
	}
	return false;
}

bool SaveLoad::onHandleUIAction(UIAction action)
{
	return focusManager.handleAction(action);
}

void SaveLoad::onDrawEnd()
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
		{ InputAction::NavigateUp, "选择存档/操作" },
		{ InputAction::Confirm, "执行" },
		{ InputAction::Cancel, "返回" }
	};
	ControllerPromptPresenter::drawBottomBar(
		engine, engine->inputActions(), items, controllerPromptTextureCache);
}

void SaveLoad::onRun()
{
	if (listBox != nullptr && listBox->index < 0 && !listBox->itemButton.empty())
	{
		selectSlot(0);
	}
	focusManager.focusDefault();
}
