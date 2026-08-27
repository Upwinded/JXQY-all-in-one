#include "PracticeMenu.h"
#include "../GameManager/GameManager.h"
#include "../../libconvert/libconvert.h"
#include "MenuResource.h"

#include <utility>


PracticeMenu::PracticeMenu()
{
	Element::name = "PracticeMenu";
	visible = false;
	init();
}


PracticeMenu::~PracticeMenu()
{
	freeResource();
}

void PracticeMenu::updateMagic()
{
	if (magic == nullptr)
	{
		refreshControllerTransferHighlight();
		return;
	}

	magic->impImage = nullptr;
	int practiceIndex = gm->magicManager.practiceIndex();

	if (gm->magicManager.magicListExists(practiceIndex))
	{
		if (name) name->setStr(gm->magicManager.magicList[practiceIndex].magic->name);
		if (intro) intro->setStr(gm->magicManager.magicList[practiceIndex].magic->intro);
		if (level) level->setStr(convert::formatString("%d", gm->magicManager.magicList[practiceIndex].level));
		if (exp) exp->setStr(convert::formatString("%d/%d", gm->magicManager.magicList[practiceIndex].exp, gm->magicManager.magicList[practiceIndex].magic->level[gm->magicManager.magicList[practiceIndex].level].levelupExp));
		magic->impImage = MenuResource::createMagicMenuImage(gm->magicManager.magicList[practiceIndex].magic);
	}
	else
	{
		if (name) name->setStr("");
		if (intro) intro->setStr("");
		if (level) level->setStr("");
		if (exp) exp->setStr("");
	}
	refreshControllerTransferHighlight();
}

void PracticeMenu::updateExp()
{
	int practiceIndex = gm->magicManager.practiceIndex();
	if (gm->magicManager.magicListExists(practiceIndex))
	{
		if (exp) exp->setStr(convert::formatString("%d/%d", gm->magicManager.magicList[practiceIndex].exp, gm->magicManager.magicList[practiceIndex].magic->level[gm->magicManager.magicList[practiceIndex].level].levelupExp));
	}
	else
	{
		if (exp) exp->setStr("");
	}
}

void PracticeMenu::updateLevel()
{
	int practiceIndex = gm->magicManager.practiceIndex();
	if (gm->magicManager.magicListExists(practiceIndex))
	{
		if (level) level->setStr(convert::formatString("%d", gm->magicManager.magicList[practiceIndex].level));
	}
	else
	{
		if (level) level->setStr("");
	}
}

void PracticeMenu::onEvent()
{
	if (magic == nullptr) return;
	if (gm != nullptr && gm->menu != nullptr
		&& gm->menu->controllerTransfers().active(ControllerSlotKind::Magic)
		&& currentDragItem != nullptr)
	{
		gm->menu->cancelControllerInteraction();
	}

	unsigned int ret = magic->getResult();
	int practiceIndex = gm->magicManager.practiceIndex();
	if (ret & erShowHint)
	{
		if (gm->magicManager.magicListExists(practiceIndex))
		{
			gm->menu->showMagicToolTip(
				getMySharedPtr(),
				gm->magicManager.magicList[practiceIndex].magic,
				gm->magicManager.magicList[practiceIndex].level,
				magic);
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
	if (ret & erMouseRDown)
	{
		gm->menu->cancelControllerInteraction();
		gm->menu->toolTip->visible = false;
		magic->resetHint();
	}
	if (ret & erDropped)
	{
		gm->menu->cancelControllerInteraction();
		gm->menu->toolTip->visible = false;
		magic->resetHint();
		if (magic->dropType == dtMagic)
		{
			if (gm->magicManager.magicListExists(magic->dropIndex))
			{
				gm->magicManager.exchange(magic->dropIndex, magic->dragIndex);
				updateMagic();
			}
			if (gm->magicManager.isStoreIndex(magic->dropIndex))
			{
				gm->menu->magicMenu->updateMagic();
			}
			else if (gm->magicManager.isBottomIndex(magic->dropIndex))
			{
				gm->menu->bottomMenu->updateMagicItem();
			}
		}
	}
}

void PracticeMenu::init()
{
	freeResource();
	loadMenuDefinition("ini\\ui\\xiulian\\xiulian.menu.ini");

	title = getComponentByName<ImageContainer>("title");
	image = getComponentByName<ImageContainer>("image");
	name = getComponentByName<Label>("name");
	intro = getComponentByName<Label>("intro");
	level = getComponentByName<Label>("level");
	exp = getComponentByName<Label>("exp");
	magic = getComponentByName<Item>("magic");

	if (intro)
	{
		intro->autoNextLine = true;
	}
	if (magic)
	{
		magic->dragIndex = gm->magicManager.practiceIndex();
		magic->dragType = dtMagic;
		magic->canShowHint = true;
	}

	setChildRectReferToParent();
	configureControllerFocus();
}

void PracticeMenu::freeResource()
{
	slotController.clear();
	image = nullptr;
	title = nullptr;
	level = nullptr;
	exp = nullptr;
	name = nullptr;
	intro = nullptr;
	magic = nullptr;
	ConfigDrivenPanel::freeResource();
}

void PracticeMenu::configureControllerFocus()
{
	SlotInteractionBinding binding =
		MenuController::makeControllerSlotInteractionBinding(
			gm,
			ControllerSlotKind::Magic,
			ControllerSlotDomain::Practice);
	binding.grid.focusIdPrefix = "practice-magic-";
	binding.grid.items = { magic };
	binding.grid.resolveLogicalIndex = [this](int)
	{
		return gm != nullptr ? gm->magicManager.practiceIndex() : -1;
	};
	binding.grid.details = [this](int logicalIndex, int)
	{
		showControllerMagicDetails(logicalIndex);
	};
	binding.grid.hideDetails = [this]() { hideControllerMagicDetails(); };
	slotController.bind(std::move(binding));
}

bool PracticeMenu::activateControllerFocus(ControllerFocusTarget target)
{
	return (target == ControllerFocusTarget::Default
		|| target == ControllerFocusTarget::Practice)
		&& focusControllerDefault();
}

bool PracticeMenu::focusControllerDefault()
{
	return slotController.activate();
}

bool PracticeMenu::isControllerFocusActive() const
{
	return slotController.isActive();
}

void PracticeMenu::deactivateControllerFocus()
{
	slotController.deactivate();
}

PElement PracticeMenu::controllerFocusedElement() const
{
	return slotController.controllerFocusedElement();
}

std::vector<PElement> PracticeMenu::controllerFocusCandidates() const
{
	return slotController.controllerFocusCandidates();
}

bool PracticeMenu::focusControllerElement(const PElement& element)
{
	return slotController.focusControllerElement(element);
}

void PracticeMenu::showControllerMagicDetails(int logicalIndex)
{
	if (gm == nullptr || gm->menu == nullptr || gm->menu->toolTip == nullptr
		|| gm->menu->upMenu == nullptr || magic == nullptr
		|| !gm->magicManager.isPracticeIndex(logicalIndex)
		|| !gm->magicManager.magicListExists(logicalIndex))
	{
		hideControllerMagicDetails();
		return;
	}
	gm->menu->showMagicToolTip(
		getMySharedPtr(),
		gm->magicManager.magicList[logicalIndex].magic,
		gm->magicManager.magicList[logicalIndex].level,
		magic);
}

void PracticeMenu::hideControllerMagicDetails()
{
	if (gm != nullptr && gm->menu != nullptr)
	{
		gm->menu->hideToolTip();
	}
}

void PracticeMenu::refreshControllerTransferHighlight()
{
	slotController.refreshTransferSelection();
}

bool PracticeMenu::onHandleUIAction(UIAction action)
{
	return slotController.handleAction(action);
}
