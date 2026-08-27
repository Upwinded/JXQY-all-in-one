#include "BottomMenu.h"
#include "ControllerTransferCoordinator.h"
#include "../../libconvert/libconvert.h"
#include "../GameManager/GameManager.h"
#include "../Data/Global.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace
{
bool getNavigationDirection(
	UIAction action,
	UIFocusDirection& direction)
{
	switch (action)
	{
	case UIAction::NavigateUp:
		direction = UIFocusDirection::Up;
		return true;
	case UIAction::NavigateDown:
		direction = UIFocusDirection::Down;
		return true;
	case UIAction::NavigateLeft:
		direction = UIFocusDirection::Left;
		return true;
	case UIAction::NavigateRight:
		direction = UIFocusDirection::Right;
		return true;
	default:
		return false;
	}
}
}

BottomMenu::BottomMenu()
{
	name = "BottomMenu";
	visible = true;
	init();
}

BottomMenu::~BottomMenu()
{
	removeAllChild();
	freeResource();
}

void BottomMenu::updateGoodsItem()
{
	for (size_t i = 0; i < GOODS_TOOLBAR_COUNT; i++)
	{
		updateGoodsItem(i);
	}
}

void BottomMenu::updateGoodsItem(int index)
{
	if (index >= 0 && index < GOODS_TOOLBAR_COUNT)
	{
		if (goodsItem[index] != nullptr)
		{
			int listIndex = gm->goodsManager.bottomIndex(index);

			goodsItem[index]->impImage = nullptr;

			updateGoodsNumber(index);
			if (gm->goodsManager.goodsListExists(listIndex))
			{
				goodsItem[index]->impImage = gm->goodsManager.goodsList[listIndex].goods->createGoodsIcon();
				goodsItem[index]->canDrag = true;
			}		
			else
			{
				goodsItem[index]->canDrag = false;
			}
		}
	}
	refreshControllerTransferHighlight();
}

void BottomMenu::updateGoodsNumber()
{
	for (size_t i = 0; i < GOODS_TOOLBAR_COUNT; i++)
	{
		updateGoodsNumber(i);
	}
}

void BottomMenu::updateGoodsNumber(int index)
{
	if (index >= 0 && index < GOODS_TOOLBAR_COUNT)
	{
		if (goodsItem[index] != nullptr)
		{		
			int listIndex = gm->goodsManager.bottomIndex(index);
			if (gm->goodsManager.goodsListExists(listIndex))
			{
				goodsItem[index]->setStr(convert::formatString("%d", gm->goodsManager.goodsList[listIndex].number));
			}
			else
			{
				goodsItem[index]->setStr("");
				if (listIndex >= 0 && listIndex < gm->goodsManager.listLength())
				{
					gm->goodsManager.goodsList[listIndex].clear();
				}
			}
		}
	}
}

void BottomMenu::updateMagicItem()
{
	for (size_t i = 0; i < MAGIC_TOOLBAR_COUNT; i++)
	{
		updateMagicItem(i);
	}
}

void BottomMenu::updateMagicItem(int index)
{
	if (index >= 0 && index < MAGIC_TOOLBAR_COUNT)
	{
		if (magicItem[index] != nullptr)
		{
			int listIndex = gm->magicManager.bottomIndex(index);

			magicItem[index]->impImage = nullptr;

			if (gm->magicManager.magicListExists(listIndex))
			{
				magicItem[index]->impImage = gm->magicManager.magicList[listIndex].magic->loadIcon();
				magicItem[index]->canDrag = true;
			}
			else
			{
				magicItem[index]->canDrag = false;
			}
		}
	}
	refreshControllerTransferHighlight();
}

void BottomMenu::init()
{
	const std::string preferredMenuButtonFocusId =
		menuButtonFocusActive
			? menuButtonFocusManager.getFocusedNodeId()
			: std::string();
	const int preferredPaneId = controllerPaneRouter.isActive()
		? controllerPaneRouter.selectedPaneId()
		: -1;
	freeResource();
	loadMenuDefinition("ini\\ui\\bottom\\bottom.menu.ini");

	equipBtn = getComponentByName<CheckBox>("equipBtn");
	goodsBtn = getComponentByName<CheckBox>("goodsBtn");
	magicBtn = getComponentByName<CheckBox>("magicBtn");
	notesBtn = getComponentByName<CheckBox>("notesBtn");
	optionBtn = getComponentByName<CheckBox>("optionBtn");
	stateBtn = getComponentByName<CheckBox>("stateBtn");
	xiulianBtn = getComponentByName<CheckBox>("xiulianBtn");

	for (size_t i = 0; i < GOODS_TOOLBAR_COUNT; i++)
	{
		std::string itemName = convert::formatString("goodsItem%d", i + 1);
		goodsItem[i] = getComponentByName<Item>(itemName);
		if (goodsItem[i])
		{
			goodsItem[i]->dragType = dtGoods;
			goodsItem[i]->dragIndex = gm->goodsManager.bottomIndex(i);
			goodsItem[i]->canShowHint = true;
		}
	}

	for (size_t i = 0; i < MAGIC_TOOLBAR_COUNT; i++)
	{
		std::string itemName = convert::formatString("magicItem%d", i + 1);
		magicItem[i] = getComponentByName<Item>(itemName);
		if (magicItem[i])
		{
			magicItem[i]->dragType = dtMagic;
			magicItem[i]->dragIndex = gm->magicManager.bottomIndex(i);
			magicItem[i]->canShowHint = true;
		}
	}

	setChildRectReferToParent();

	if (!GameManager::getInstance()->global.feature.topButtonsLayout && subMenus.empty())
	{
		columnMenu = std::make_shared<ColumnMenu>();
		addChild(columnMenu);
	}

	configureControllerFocus(
		preferredMenuButtonFocusId, preferredPaneId);
}

void BottomMenu::onEvent()
{
	if (currentDragItem != nullptr)
	{
		cancelPointerControllerInteraction();
	}
	for (size_t i = 0; i < GOODS_TOOLBAR_COUNT; i++)
	{
		if (goodsItem[i] == nullptr)
		{
			continue;
		}
		int ret = goodsItem[i]->getResult();
		int listIndex = gm->goodsManager.bottomIndex(i);
		if (ret & erShowHint)
		{
			if (gm->goodsManager.goodsListExists(listIndex))
			{
				gm->menu->showGoodsToolTip(
					getMySharedPtr(),
					gm->goodsManager.goodsList[listIndex].goods,
					goodsItem[i]);
			}
			else
			{
				gm->menu->toolTip->visible = false;
			}
			
		}
		if (ret & erHideHint)
		{
			gm->menu->toolTip->visible = false;
		}
		if ((ret & erMouseLDown) || (ret & erMouseRDown)
			|| (ret & erClick) || (ret & erDropped))
		{
			cancelPointerControllerInteraction();
		}
#ifdef __MOBILE__
		if (ret & erMouseRDown || ret & erClick)
#else
		if (ret & erMouseRDown)
#endif
		{
			gm->menu->toolTip->visible = false;
			goodsItem[i]->resetHint();
			gm->goodsManager.useItem(goodsItem[i]->dragIndex);
		}
		if (ret & erDropped)
		{
			gm->menu->toolTip->visible = false;
			goodsItem[i]->resetHint();
			
			if (goodsItem[i]->dropType == dtGoods)
			{
				if (gm->goodsManager.goodsListExists(goodsItem[i]->dropIndex))
				{	
					if (gm->goodsManager.isStoreIndex(goodsItem[i]->dropIndex))
					{						
						gm->goodsManager.exchange(listIndex, goodsItem[i]->dropIndex);
						gm->menu->goodsMenu->updateGoods();
						updateGoodsItem(i);
					}
					else if (gm->goodsManager.isBottomIndex(goodsItem[i]->dropIndex))
					{
						gm->goodsManager.exchange(listIndex, goodsItem[i]->dropIndex);
						updateGoodsItem();			
					}
					else if (gm->goodsManager.isEquipIndex(goodsItem[i]->dropIndex))
					{
						bool canExchange = gm->goodsManager.goodsList[listIndex].goods == nullptr;
						std::string message;
						if (!canExchange && gm->goodsManager.goodsListExists(listIndex))
						{
							canExchange = gm->goodsManager.canEquipGoodsAt(
								listIndex,
								gm->goodsManager.equipSlot(goodsItem[i]->dropIndex),
								gm->player,
								&message);
						}
						if (canExchange)
						{
							gm->goodsManager.exchange(listIndex, goodsItem[i]->dropIndex);
							gm->menu->equipMenu->updateGoods(gm->goodsManager.equipSlot(goodsItem[i]->dropIndex));
							updateGoodsItem(i);
						}
						else if (!message.empty())
						{
							gm->showMessage(message);
						}
					}
				}			
			}
		}	
	}
	for (size_t i = 0; i < MAGIC_TOOLBAR_COUNT; i++)
	{
		if (magicItem[i] == nullptr)
		{
			continue;
		}
		int ret = magicItem[i]->getResult();
		int listIndex = gm->magicManager.bottomIndex(i);
		if (ret & erShowHint)
		{
			if (gm->magicManager.magicListExists(listIndex))
			{
				gm->menu->showMagicToolTip(
					getMySharedPtr(),
					gm->magicManager.magicList[listIndex].magic,
					gm->magicManager.magicList[listIndex].level,
					magicItem[i]);
			}
			else
			{
				gm->menu->toolTip->visible = false;
			}
			
		}
		if (ret & erHideHint)
		{
			gm->menu->toolTip->visible = false;
		}
		if ((ret & erMouseLDown) || (ret & erMouseRDown)
			|| (ret & erClick) || (ret & erDropped))
		{
			cancelPointerControllerInteraction();
		}
		if (ret & erMouseRDown)
		{
			gm->menu->toolTip->visible = false;
			magicItem[i]->resetHint();
		}
		if (ret & erDropped)
		{	
			gm->menu->toolTip->visible = false;
			magicItem[i]->resetHint();
			if (magicItem[i]->dropType == dtMagic)
			{
				if (gm->magicManager.magicListExists(magicItem[i]->dropIndex))
				{
					if (gm->magicManager.isStoreIndex(magicItem[i]->dropIndex) ||
						gm->magicManager.isBottomIndex(magicItem[i]->dropIndex) ||
						gm->magicManager.isPracticeIndex(magicItem[i]->dropIndex))
					{
						gm->magicManager.exchange(listIndex, magicItem[i]->dropIndex);
						updateMagicItem();
					}
					if (gm->magicManager.isStoreIndex(magicItem[i]->dropIndex))
					{
						gm->menu->magicMenu->updateMagic();
					}
					else if (gm->magicManager.isPracticeIndex(magicItem[i]->dropIndex))
					{
						gm->menu->practiceMenu->updateMagic();
					}
				}
			}	
		}
	}

	if (optionBtn && optionBtn->getResult(erClick))
	{
		cancelPointerControllerInteraction();
		gm->menu->openSystemMenu();
	}
	if (equipBtn && equipBtn->getResult(erClick))
	{
		cancelPointerControllerInteraction();
		gm->menu->toggleEquipView();
	}
	if (stateBtn && stateBtn->getResult(erClick))
	{
		cancelPointerControllerInteraction();
		gm->menu->toggleStateView();
	}
	if (xiulianBtn && xiulianBtn->getResult(erClick))
	{
		cancelPointerControllerInteraction();
		gm->menu->togglePracticeView();
	}
	if (goodsBtn && goodsBtn->getResult(erClick))
	{
		cancelPointerControllerInteraction();
		gm->menu->toggleGoodsView();
	}
	if (magicBtn && magicBtn->getResult(erClick))
	{
		cancelPointerControllerInteraction();
		gm->menu->toggleMagicView();
	}
	if (notesBtn && notesBtn->getResult(erClick))
	{
		cancelPointerControllerInteraction();
		gm->menu->toggleMemoView();
	}
}

void BottomMenu::configureControllerFocus(
	const std::string& preferredMenuButtonFocusId,
	int preferredPaneId)
{
	// The router borrows both slot controllers. Stop routing before replacing
	// either config-driven binding.
	controllerPaneRouter.clear();
	menuButtonFocusManager.clear();
	menuButtonFocusManager.setInputAwarePresentation();
	menuButtonFocusActive = false;

	std::vector<UIFocusNodeBinding> menuButtonBindings =
	{
		{ "bottom-state", stateBtn,
			[this]() { gm->menu->toggleStateView(); } },
		{ "bottom-equip", equipBtn,
			[this]() { gm->menu->toggleEquipView(); } },
		{ "bottom-practice", xiulianBtn,
			[this]() { gm->menu->togglePracticeView(); } },
		{ "bottom-goods", goodsBtn,
			[this]() { gm->menu->toggleGoodsView(); } },
		{ "bottom-magic", magicBtn,
			[this]() { gm->menu->toggleMagicView(); } },
		{ "bottom-memo", notesBtn,
			[this]() { gm->menu->toggleMemoView(); } },
		{ "bottom-options", optionBtn,
			[this]() { gm->menu->openSystemMenu(); } },
	};
	const std::vector<std::string> menuButtonIds =
		menuButtonFocusManager.addVisualSpatialGroup(
			"bottom-menu-buttons",
			menuButtonBindings);
	menuButtonFocusManager.applyConfigDrivenFocusNavigation(
		*this,
		{
			{ "stateBtn", "bottom-state" },
			{ "equipBtn", "bottom-equip" },
			{ "xiulianBtn", "bottom-practice" },
			{ "goodsBtn", "bottom-goods" },
			{ "magicBtn", "bottom-magic" },
			{ "notesBtn", "bottom-memo" },
			{ "optionBtn", "bottom-options" },
		});
	if (!menuButtonIds.empty())
	{
		menuButtonFocusManager.setDefaultFocus(menuButtonIds.front());
	}

	SlotInteractionBinding goodsBinding =
		MenuController::makeControllerSlotInteractionBinding(
			gm,
			ControllerSlotKind::Goods,
			ControllerSlotDomain::GoodsQuick);
	goodsBinding.grid.focusIdPrefix = "bottom-goods-quick-";
	goodsBinding.grid.fixedColumnCount = GOODS_TOOLBAR_COUNT;
	for (const auto& item : goodsItem)
	{
		goodsBinding.grid.items.push_back(item);
	}
	goodsBinding.grid.resolveLogicalIndex = [this](int visibleIndex)
	{
		return getControllerGoodsIndex(visibleIndex);
	};
	goodsBinding.grid.primary = [this](int logicalIndex, int)
	{
		activateControllerGoodsItem(logicalIndex);
	};
	goodsBinding.grid.details = [this](int logicalIndex, int visibleIndex)
	{
		showControllerGoodsDetails(logicalIndex, visibleIndex);
	};
	goodsBinding.grid.hideDetails = [this]() { hideControllerItemDetails(); };
	goodsSlotController.bind(std::move(goodsBinding));

	SlotInteractionBinding magicBinding =
		MenuController::makeControllerSlotInteractionBinding(
			gm,
			ControllerSlotKind::Magic,
			ControllerSlotDomain::MagicQuick);
	magicBinding.grid.focusIdPrefix = "bottom-magic-quick-";
	magicBinding.grid.fixedColumnCount = MAGIC_TOOLBAR_COUNT;
	for (const auto& item : magicItem)
	{
		magicBinding.grid.items.push_back(item);
	}
	magicBinding.grid.resolveLogicalIndex = [this](int visibleIndex)
	{
		return getControllerMagicIndex(visibleIndex);
	};
	magicBinding.grid.details = [this](int logicalIndex, int visibleIndex)
	{
		showControllerMagicDetails(logicalIndex, visibleIndex);
	};
	magicBinding.grid.hideDetails = [this]() { hideControllerItemDetails(); };
	magicSlotController.bind(std::move(magicBinding));

	controllerPaneRouter.registerTargetPane(
		GoodsControllerPaneId,
		goodsSlotController);
	controllerPaneRouter.registerTargetPane(
		MagicControllerPaneId,
		magicSlotController);
	controllerPaneRouter.setDefaultPane(GoodsControllerPaneId);
	refreshControllerTransferHighlight();
	if (!preferredMenuButtonFocusId.empty())
	{
		menuButtonFocusActive = true;
		menuButtonFocusManager.prepareForSemanticActivation();
		if (menuButtonFocusManager.focusNode(preferredMenuButtonFocusId)
			|| menuButtonFocusManager.focusDefault())
		{
			return;
		}
		menuButtonFocusActive = false;
	}
	if (preferredPaneId >= 0)
	{
		controllerPaneRouter.activatePane(preferredPaneId);
	}
}

bool BottomMenu::activateControllerFocus(ControllerFocusTarget target)
{
	if (target == ControllerFocusTarget::MagicQuick)
	{
		return focusControllerMagicQuick();
	}
	if (target == ControllerFocusTarget::GoodsQuick)
	{
		return focusControllerGoodsQuick();
	}
	if (target == ControllerFocusTarget::Default)
	{
		controllerPaneRouter.deactivate();
		menuButtonFocusActive = true;
		menuButtonFocusManager.prepareForSemanticActivation();
		if (menuButtonFocusManager.restoreFocus())
		{
			return true;
		}
		menuButtonFocusActive = false;
		return false;
	}
	return false;
}

bool BottomMenu::focusControllerGoodsQuick()
{
	hideControllerItemDetails();
	menuButtonFocusActive = false;
	menuButtonFocusManager.suspendFocus();
	return controllerPaneRouter.activatePane(GoodsControllerPaneId);
}

bool BottomMenu::focusControllerMagicQuick()
{
	hideControllerItemDetails();
	menuButtonFocusActive = false;
	menuButtonFocusManager.suspendFocus();
	return controllerPaneRouter.activatePane(MagicControllerPaneId);
}

bool BottomMenu::isControllerFocusActive() const
{
	return (menuButtonFocusActive
			&& isUIFocusElementAvailable(
				menuButtonFocusManager.getFocusedElement()))
		|| controllerPaneRouter.isActive();
}

void BottomMenu::deactivateControllerFocus()
{
	controllerPaneRouter.deactivate();
	menuButtonFocusActive = false;
	menuButtonFocusManager.suspendFocus();
	hideControllerItemDetails();
}

PElement BottomMenu::controllerFocusedElement() const
{
	if (menuButtonFocusActive)
	{
		PElement focusedElement = menuButtonFocusManager.getFocusedElement();
		return isUIFocusElementAvailable(focusedElement)
			? focusedElement
			: nullptr;
	}
	return controllerPaneRouter.controllerFocusedElement();
}

std::vector<PElement> BottomMenu::controllerFocusCandidates() const
{
	std::vector<PElement> candidates =
		menuButtonFocusManager.getAvailableFocusElements();
	const std::vector<PElement> slotCandidates =
		controllerPaneRouter.controllerFocusCandidates();
	candidates.insert(
		candidates.end(), slotCandidates.begin(), slotCandidates.end());
	return candidates;
}

bool BottomMenu::focusControllerElement(const PElement& element)
{
	hideControllerItemDetails();
	for (const PElement& candidate :
		menuButtonFocusManager.getAvailableFocusElements())
	{
		if (candidate != element)
		{
			continue;
		}
		controllerPaneRouter.deactivate();
		menuButtonFocusActive = true;
		menuButtonFocusManager.prepareForSemanticActivation();
		if (menuButtonFocusManager.focusElement(element))
		{
			return true;
		}
		menuButtonFocusActive = false;
		return false;
	}
	menuButtonFocusActive = false;
	menuButtonFocusManager.suspendFocus();
	return controllerPaneRouter.focusControllerElement(element);
}

int BottomMenu::getControllerGoodsIndex(int visibleIndex) const
{
	if (gm == nullptr || visibleIndex < 0
		|| visibleIndex >= GOODS_TOOLBAR_COUNT
		|| goodsItem[visibleIndex] == nullptr)
	{
		return -1;
	}
	const int logicalIndex = gm->goodsManager.bottomIndex(visibleIndex);
	return gm->goodsManager.isBottomIndex(logicalIndex)
		&& logicalIndex < gm->goodsManager.listLength()
		? logicalIndex
		: -1;
}

int BottomMenu::getControllerMagicIndex(int visibleIndex) const
{
	if (gm == nullptr || visibleIndex < 0
		|| visibleIndex >= MAGIC_TOOLBAR_COUNT
		|| magicItem[visibleIndex] == nullptr)
	{
		return -1;
	}
	const int logicalIndex = gm->magicManager.bottomIndex(visibleIndex);
	return gm->magicManager.isBottomIndex(logicalIndex)
		&& logicalIndex < gm->magicManager.listLength()
		? logicalIndex
		: -1;
}

void BottomMenu::activateControllerGoodsItem(int logicalIndex)
{
	if (gm == nullptr || gm->menu == nullptr || logicalIndex < 0)
	{
		return;
	}
	hideControllerItemDetails();
	gm->goodsManager.useItem(logicalIndex);
}

void BottomMenu::showControllerGoodsDetails(
	int logicalIndex, int visibleIndex)
{
	if (gm == nullptr || gm->menu == nullptr || gm->menu->toolTip == nullptr
		|| gm->menu->upMenu == nullptr || visibleIndex < 0
		|| visibleIndex >= GOODS_TOOLBAR_COUNT
		|| goodsItem[visibleIndex] == nullptr
		|| !gm->goodsManager.goodsListExists(logicalIndex))
	{
		hideControllerItemDetails();
		return;
	}
	gm->menu->showGoodsToolTip(
		getMySharedPtr(),
		gm->goodsManager.goodsList[logicalIndex].goods,
		goodsItem[visibleIndex]);
}

void BottomMenu::showControllerMagicDetails(
	int logicalIndex, int visibleIndex)
{
	if (gm == nullptr || gm->menu == nullptr || gm->menu->toolTip == nullptr
		|| gm->menu->upMenu == nullptr || visibleIndex < 0
		|| visibleIndex >= MAGIC_TOOLBAR_COUNT
		|| magicItem[visibleIndex] == nullptr
		|| !gm->magicManager.magicListExists(logicalIndex))
	{
		hideControllerItemDetails();
		return;
	}
	gm->menu->showMagicToolTip(
		getMySharedPtr(),
		gm->magicManager.magicList[logicalIndex].magic,
		gm->magicManager.magicList[logicalIndex].level,
		magicItem[visibleIndex]);
}

void BottomMenu::hideControllerItemDetails()
{
	if (gm != nullptr && gm->menu != nullptr)
	{
		gm->menu->hideToolTip();
	}
}

void BottomMenu::refreshControllerTransferHighlight()
{
	goodsSlotController.refreshTransferSelection();
	magicSlotController.refreshTransferSelection();
}

void BottomMenu::cancelPointerControllerInteraction()
{
	if (gm != nullptr && gm->menu != nullptr)
	{
		gm->menu->cancelControllerInteraction();
	}
}

bool BottomMenu::onHandleUIAction(UIAction action)
{
	UIFocusDirection direction = UIFocusDirection::Up;
	const bool directionalAction =
		getNavigationDirection(action, direction);
	if (menuButtonFocusActive)
	{
		if (directionalAction
			&& !menuButtonFocusManager.hasAvailableExplicitNeighbour(direction)
			&& focusSpatialControllerCandidate(direction, true))
		{
			return true;
		}
		return menuButtonFocusManager.handleAction(action);
	}
	if (controllerPaneRouter.handleAction(action))
	{
		return true;
	}
	return directionalAction
		&& focusSpatialControllerCandidate(direction, false);
}

bool BottomMenu::focusSpatialControllerCandidate(
	UIFocusDirection direction,
	bool requireQuickSlotTarget)
{
	const PElement anchor = controllerFocusedElement();
	if (!isUIFocusElementAvailable(anchor))
	{
		return false;
	}

	const std::vector<PElement> candidates =
		controllerFocusCandidates();
	std::optional<UIFocusSpatialScore> bestScore;
	PElement bestCandidate;
	for (std::size_t index = 0; index < candidates.size(); index++)
	{
		const PElement& candidate = candidates[index];
		if (!isUIFocusElementAvailable(candidate)
			|| candidate == anchor)
		{
			continue;
		}
		const std::optional<UIFocusSpatialScore> score =
			scoreUIFocusSpatialCandidate(
				anchor->rect, candidate->rect, direction, index);
		if (score && (!bestScore || *score < *bestScore))
		{
			bestScore = score;
			bestCandidate = candidate;
		}
	}
	if (bestCandidate == nullptr)
	{
		return false;
	}

	if (requireQuickSlotTarget)
	{
		const std::vector<PElement> menuButtonCandidates =
			menuButtonFocusManager.getAvailableFocusElements();
		if (std::find(
				menuButtonCandidates.begin(),
				menuButtonCandidates.end(),
				bestCandidate) != menuButtonCandidates.end())
		{
			// Keep ordinary button-to-button navigation inside its manager so
			// configured neighbours and focus presentation stay authoritative.
			return false;
		}
	}
	return focusControllerElement(bestCandidate);
}

void BottomMenu::freeResource()
{
	// Detach borrowed targets while both controllers are still alive and bound.
	controllerPaneRouter.clear();
	menuButtonFocusManager.clear();
	menuButtonFocusActive = false;
	goodsSlotController.clear();
	magicSlotController.clear();
	columnMenu = nullptr;
	equipBtn = nullptr;
	goodsBtn = nullptr;
	magicBtn = nullptr;
	optionBtn = nullptr;
	notesBtn = nullptr;
	stateBtn = nullptr;
	xiulianBtn = nullptr;
	for (size_t i = 0; i < GOODS_TOOLBAR_COUNT; i++)
	{
		goodsItem[i] = nullptr;
	}
	for (size_t i = 0; i < MAGIC_TOOLBAR_COUNT; i++)
	{
		magicItem[i] = nullptr;
	}
	ConfigDrivenPanel::freeResource();
}
