#include "MagicMenu.h"
#include "../GameManager/GameManager.h"
#include "../../libconvert/libconvert.h"
#include "MenuResource.h"
#include <algorithm>
#include <utility>


MagicMenu::MagicMenu()
{
	name = "MagicMenu";
	visible = false;
	init();
}


MagicMenu::~MagicMenu()
{
	freeResource();
}

void MagicMenu::updateMagic()
{
	if (scrollbar != nullptr)
	{
		position = scrollbar->position;
	}
	for (size_t i = 0; i < item.size(); i++)
	{
		if (item[i] && scrollbar)
		{
			item[i]->dragIndex = gm->magicManager.storeBegin() + static_cast<int>(i) + scrollbar->position * scrollbar->lineSize;
		}
		updateMagic(static_cast<int>(i));
	}
	refreshControllerTransferHighlight();
}

void MagicMenu::updateMagic(int index)
{
	if (index < 0 || index >= static_cast<int>(item.size()))
	{
		return;
	}
	if (item[index] == nullptr || scrollbar == nullptr)
	{
		return;
	}

	item[index]->impImage = nullptr;

	int listIndex = gm->magicManager.storeBegin() + scrollbar->position * scrollbar->lineSize + index;
	if (!gm->magicManager.isStoreIndex(listIndex) || listIndex >= gm->magicManager.listLength())
	{
		return;
	}
	if (gm->magicManager.magicList[listIndex].magic != nullptr)
	{
		item[index]->impImage = MenuResource::createMagicMenuImage(gm->magicManager.magicList[listIndex].magic);
	}
}

void MagicMenu::onEvent()
{
	if (gm != nullptr && gm->menu != nullptr
		&& gm->menu->controllerTransfers().active(ControllerSlotKind::Magic)
		&& currentDragItem != nullptr)
	{
		cancelControllerInteraction();
	}
	if (scrollbar != nullptr && position != scrollbar->position)
	{
		hideControllerItemDetails();
		position = scrollbar->position;
		updateMagic();
	}
	if (scrollbar == nullptr)
	{
		return;
	}

	for (size_t i = 0; i < item.size(); i++)
	{
		if (item[i] == nullptr) continue;

		int listIndex = gm->magicManager.storeBegin() + scrollbar->position * scrollbar->lineSize + static_cast<int>(i);
		int ret = item[i]->getResult();
		if (ret & erShowHint)
		{
			if (scrollbar && gm->magicManager.magicListExists(listIndex))
			{
				gm->menu->showMagicToolTip(
					getMySharedPtr(),
					gm->magicManager.magicList[listIndex].magic,
					gm->magicManager.magicList[listIndex].level,
					item[i]);
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
#ifdef __MOBILE__
		if (ret & erClick || ret & erMouseRDown)
#else
		if (ret & erMouseRDown)
#endif
		{
			cancelControllerInteraction();
			if (gm->magicManager.magicListExists(item[i]->dragIndex))
			{
				if (gm->menu->practiceMenu != nullptr && gm->menu->practiceMenu->visible == true)
				{
					gm->magicManager.exchange(item[i]->dragIndex, gm->magicManager.practiceIndex());
					updateMagic(i);
					gm->menu->practiceMenu->updateMagic();
				}
				else if (gm->menu->bottomMenu != nullptr)
				{
					for (int j = gm->magicManager.bottomBegin(); j <= gm->magicManager.bottomEnd(); ++j)
					{
						if (!gm->magicManager.magicListExists(j))
						{
							gm->magicManager.exchange(item[i]->dragIndex, j);
							updateMagic(i);
							gm->menu->bottomMenu->updateMagicItem(gm->magicManager.bottomSlot(j));
							break;
						}
					}
				}
			}

			gm->menu->toolTip->visible = false;
			item[i]->resetHint();
		}
		if (ret & erDropped)
		{
			cancelControllerInteraction();
			gm->menu->toolTip->visible = false;
			item[i]->resetHint();
			if (item[i]->dropType == dtMagic)
			{
				if (gm->magicManager.magicListExists(item[i]->dropIndex))
				{
					if (gm->magicManager.isStoreIndex(item[i]->dropIndex))
					{
						gm->magicManager.exchange(item[i]->dropIndex, item[i]->dragIndex);
						updateMagic();
					}
					else if (gm->magicManager.isBottomIndex(item[i]->dropIndex))
					{
						gm->magicManager.exchange(item[i]->dropIndex, item[i]->dragIndex);
						updateMagic(i);
						gm->menu->bottomMenu->updateMagicItem();
					}
					else if (gm->magicManager.isPracticeIndex(item[i]->dropIndex))
					{
						gm->magicManager.exchange(item[i]->dropIndex, item[i]->dragIndex);
						updateMagic(i);
						gm->menu->practiceMenu->updateMagic();
					}
				}
			}
		}
	}
}

void MagicMenu::init()
{
	freeResource();
	loadMenuDefinition("ini\\ui\\magic\\magic.menu.ini");

	title = getComponentByName<ImageContainer>("title");
	image = getComponentByName<ImageContainer>("image");
	scrollbar = getComponentByName<Scrollbar>("scrollbar");

	item.clear();
	for (int i = 1;; i++)
	{
		std::string itemName = convert::formatString("item%d", i);
		auto itemComponent = getComponentByName<Item>(itemName);
		if (!itemComponent)
		{
			break;
		}
		itemComponent->dragType = dtMagic;
		itemComponent->canShowHint = true;
		item.push_back(itemComponent);
	}

	if (scrollbar != nullptr)
	{
		scrollbar->pageSize = static_cast<int>(item.size());
		int lineSize = std::max(1, scrollbar->lineSize);
		int visibleCount = std::max(1, scrollbar->pageSize);
		int scrollableCount = std::max(0, gm->global.magicLayout.storeCount() - visibleCount);
		scrollbar->min = 0;
		scrollbar->max = (scrollableCount + lineSize - 1) / lineSize;
		scrollbar->position = scrollbar->min;
	}

	setChildRectReferToParent();
	configureControllerFocus();
}

void MagicMenu::freeResource()
{
	slotController.clear();
	title = nullptr;
	image = nullptr;
	scrollbar = nullptr;
	item.clear();
	ConfigDrivenPanel::freeResource();
}

void MagicMenu::configureControllerFocus()
{
	SlotInteractionBinding binding =
		MenuController::makeControllerSlotInteractionBinding(
			gm,
			ControllerSlotKind::Magic,
			ControllerSlotDomain::MagicList);
	binding.grid.focusIdPrefix = "magic-item-";
	binding.grid.items = item;
	binding.grid.scrollbar = scrollbar;
	binding.grid.resolveLogicalIndex = [this](int visibleIndex)
	{
		return getControllerItemIndex(visibleIndex);
	};
	binding.grid.primary = [this](int, int visibleIndex)
	{
		activateControllerItem(visibleIndex);
	};
	binding.grid.details = [this](int, int visibleIndex)
	{
		showControllerItemDetails(visibleIndex);
	};
	binding.grid.hideDetails = [this]() { hideControllerItemDetails(); };
	binding.grid.refreshAfterScroll = [this]()
	{
		position = scrollbar != nullptr ? scrollbar->position : -1;
		updateMagic();
	};
	slotController.bind(std::move(binding));
}

bool MagicMenu::activateControllerFocus(ControllerFocusTarget target)
{
	return (target == ControllerFocusTarget::Default
		|| target == ControllerFocusTarget::MagicList)
		&& focusControllerDefault();
}

bool MagicMenu::focusControllerDefault()
{
	return slotController.activate();
}

bool MagicMenu::isControllerFocusActive() const
{
	return slotController.isActive();
}

void MagicMenu::deactivateControllerFocus()
{
	slotController.deactivate();
}

PElement MagicMenu::controllerFocusedElement() const
{
	return slotController.controllerFocusedElement();
}

std::vector<PElement> MagicMenu::controllerFocusCandidates() const
{
	return slotController.controllerFocusCandidates();
}

bool MagicMenu::focusControllerElement(const PElement& element)
{
	return slotController.focusControllerElement(element);
}

int MagicMenu::getControllerItemIndex(int visibleIndex) const
{
	if (scrollbar == nullptr || visibleIndex < 0
		|| visibleIndex >= static_cast<int>(item.size())
		|| item[visibleIndex] == nullptr)
	{
		return -1;
	}
	const int listIndex = gm->magicManager.storeBegin()
		+ visibleIndex + scrollbar->position * scrollbar->lineSize;
	return gm->magicManager.isStoreIndex(listIndex)
		&& listIndex < gm->magicManager.listLength()
		? listIndex
		: -1;
}

void MagicMenu::activateControllerItem(int visibleIndex)
{
	const int sourceIndex = getControllerItemIndex(visibleIndex);
	if (sourceIndex < 0 || !gm->magicManager.magicListExists(sourceIndex))
	{
		return;
	}
	hideControllerItemDetails();
	if (gm->menu != nullptr && gm->menu->practiceMenu != nullptr
		&& gm->menu->practiceMenu->visible)
	{
		gm->magicManager.exchange(sourceIndex, gm->magicManager.practiceIndex());
		gm->magicManager.updateMenu();
		return;
	}
	for (int targetIndex = gm->magicManager.bottomBegin();
		targetIndex <= gm->magicManager.bottomEnd(); targetIndex++)
	{
		if (!gm->magicManager.magicListExists(targetIndex))
		{
			gm->magicManager.exchange(sourceIndex, targetIndex);
			gm->magicManager.updateMenu();
			return;
		}
	}
	gm->showMessage("快捷武功栏已满");
}
void MagicMenu::showControllerItemDetails(int visibleIndex)
{
	const int listIndex = getControllerItemIndex(visibleIndex);
	if (gm->menu == nullptr || gm->menu->toolTip == nullptr
		|| gm->menu->upMenu == nullptr || listIndex < 0
		|| !gm->magicManager.magicListExists(listIndex))
	{
		hideControllerItemDetails();
		return;
	}
	gm->menu->showMagicToolTip(
		getMySharedPtr(),
		gm->magicManager.magicList[listIndex].magic,
		gm->magicManager.magicList[listIndex].level,
		item[visibleIndex]);
}

void MagicMenu::hideControllerItemDetails()
{
	if (gm != nullptr && gm->menu != nullptr)
	{
		gm->menu->hideToolTip();
	}
}

void MagicMenu::refreshControllerTransferHighlight()
{
	slotController.refreshTransferSelection();
}

void MagicMenu::cancelControllerInteraction()
{
	if (gm != nullptr && gm->menu != nullptr
		&& gm->menu->controllerTransfers().active(ControllerSlotKind::Magic))
	{
		gm->menu->controllerTransfers().cancel();
	}
	slotController.deactivate();
	refreshControllerTransferHighlight();
	hideControllerItemDetails();
}

bool MagicMenu::onHandleUIAction(UIAction action)
{
	return slotController.handleAction(action);
}
