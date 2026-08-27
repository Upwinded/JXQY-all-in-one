#include "EquipMenu.h"
#include "../GameManager/GameManager.h"
#include "../../libconvert/libconvert.h"
#include "../Data/Global.h"
#include "MenuResource.h"
#include "../../File/INIReader.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace
{
std::string normalizeEquipmentPart(std::string part)
{
	std::transform(part.begin(), part.end(), part.begin(),
		[](unsigned char ch)
		{
			return static_cast<char>(std::tolower(ch));
		});
	return part;
}
}

EquipMenu::EquipMenu()
{
	name = "EquipMenu";
	visible = false;
	init();
}


EquipMenu::~EquipMenu()
{
	freeResource();
}

void EquipMenu::updateGoods()
{
	updatePanelImage();
	updatePlayerNameDisplay();

	for (size_t i = 0; i < GOODS_BODY_COUNT; i++)
	{
		updateGoods(i);
	}
	refreshControllerTransferHighlight();
}

void EquipMenu::updateGoods(int index)
{
	if (index < 0 || index >= GOODS_BODY_COUNT)
	{
		return;
	}
	if (item[index] == nullptr)
	{
		return;
	}
	int listIndex = gm->goodsManager.equipIndex(index);
	if (listIndex < 0 || listIndex >= gm->goodsManager.listLength())
	{
		return;
	}
	item[index]->impImage = nullptr;
	if (gm->goodsManager.goodsList[listIndex].iniFile.empty() || gm->goodsManager.goodsList[listIndex].goods == nullptr || gm->goodsManager.goodsList[listIndex].number <= 0)
	{
		item[index]->setStr("");
		gm->goodsManager.goodsList[listIndex].clear();
	}
	else
	{
		item[index]->impImage = MenuResource::createGoodsMenuImage(gm->goodsManager.goodsList[listIndex].goods);
		item[index]->setStr(convert::formatString("%d", gm->goodsManager.goodsList[listIndex].number));
	}
	gm->player->calInfo();
	gm->menu->stateMenu->updateLabel();
	updateDataBindings();
}

void EquipMenu::updateMagicDisplay()
{
	updatePanelImage();

	for (int i = 0; i < static_cast<int>(magicDisplayItem.size()); i++)
	{
		updateMagicDisplay(i);
	}
	refreshControllerTransferHighlight();
}

void EquipMenu::updateMagicDisplay(int index)
{
	if (index < 0 || index >= static_cast<int>(magicDisplayItem.size()))
	{
		return;
	}
	if (magicDisplayItem[index] == nullptr)
	{
		magicDisplayIniFile[index].clear();
		return;
	}

	std::string iniFile;
	int listIndex = getMagicDisplayStartIndex() + index;
	if (gm->magicManager.magicListExists(listIndex))
	{
		iniFile = gm->magicManager.magicList[listIndex].iniFile;
	}

	magicDisplayItem[index]->canDrag = false;
	magicDisplayItem[index]->dragIndex = listIndex;
	if (iniFile.empty())
	{
		if (!magicDisplayIniFile[index].empty())
		{
			magicDisplayIniFile[index].clear();
			magicDisplayItem[index]->impImage = nullptr;
		}
		return;
	}

	magicDisplayItem[index]->canDrag = true;
	if (magicDisplayIniFile[index] == iniFile)
	{
		return;
	}

	magicDisplayIniFile[index] = iniFile;
	magicDisplayItem[index]->impImage = MenuResource::createMagicMenuImage(gm->magicManager.magicList[listIndex].magic);
}

int EquipMenu::getMagicDisplayStartIndex() const
{
	if (magicScrollbar == nullptr)
	{
		return gm->magicManager.storeBegin();
	}
	return gm->magicManager.storeBegin() + magicScrollbar->position * magicScrollbar->lineSize;
}

void EquipMenu::quickEquipMagic(int sourceIndex)
{
	if (!gm->magicManager.magicListExists(sourceIndex))
	{
		return;
	}

	if (gm->menu->practiceMenu != nullptr && gm->menu->practiceMenu->visible)
	{
		gm->magicManager.exchange(sourceIndex, gm->magicManager.practiceIndex());
		updateMagicDisplay();
		gm->menu->practiceMenu->updateMagic();
		updateMagicRelatedMenus(sourceIndex);
		return;
	}

	if (gm->menu->bottomMenu == nullptr)
	{
		return;
	}

	for (int index = gm->magicManager.bottomBegin(); index <= gm->magicManager.bottomEnd(); index++)
	{
		if (!gm->magicManager.magicListExists(index))
		{
			gm->magicManager.exchange(sourceIndex, index);
			updateMagicDisplay();
			gm->menu->bottomMenu->updateMagicItem(gm->magicManager.bottomSlot(index));
			updateMagicRelatedMenus(sourceIndex);
			return;
		}
	}
	gm->showMessage("快捷武功栏已满");
}

void EquipMenu::updateMagicRelatedMenus(int sourceIndex)
{
	if (gm->menu->magicMenu != nullptr)
	{
		gm->menu->magicMenu->updateMagic();
	}
	if (gm->magicManager.isBottomIndex(sourceIndex) && gm->menu->bottomMenu != nullptr)
	{
		gm->menu->bottomMenu->updateMagicItem();
	}
	else if (gm->magicManager.isPracticeIndex(sourceIndex) && gm->menu->practiceMenu != nullptr)
	{
		gm->menu->practiceMenu->updateMagic();
	}
}

void EquipMenu::loadNewSwordPartnerNames()
{
	newSwordPartnerNames.clear();
	newSwordPartnerNames.resize(4);

	INIReader partnerIni("partneridx.ini");
	for (int index = 1; index <= 3; index++)
	{
		newSwordPartnerNames[index] = partnerIni.Get("Init", convert::formatString("%d", index), "");
	}

	INIReader playerNameIni("ini\\playername.ini");
	for (int index = 1; index <= 3; index++)
	{
		if (newSwordPartnerNames[index].empty())
		{
			newSwordPartnerNames[index] = playerNameIni.Get("Name", convert::formatString("Name%d", index + 1), "");
		}
	}
}

int EquipMenu::getNewSwordPartnerIndex(const std::string& partnerName) const
{
	if (partnerName.empty())
	{
		return -1;
	}

	for (int index = 1; index < static_cast<int>(newSwordPartnerNames.size()) && index < 4; index++)
	{
		if (newSwordPartnerNames[index] == partnerName)
		{
			return index;
		}
	}
	return -1;
}

void EquipMenu::updatePlayerNameDisplay()
{
	if (gm == nullptr || !gm->global.feature.equipPlayerNameImages || playerNameImages.empty())
	{
		return;
	}

	bool available[4] = { false, false, false, false };
	int currentIndex = gm->global.data.characterIndex;
	if (currentIndex < 0)
	{
		currentIndex = 0;
	}
	if (currentIndex >= 0 && currentIndex < 4)
	{
		available[currentIndex] = true;
	}

	auto partners = gm->partnerManager.findPartnersFromNPCManager();
	for (auto& partner : partners)
	{
		if (partner == nullptr)
		{
			continue;
		}
		int partnerIndex = getNewSwordPartnerIndex(partner->npcName);
		if (partnerIndex >= 0 && partnerIndex < 4)
		{
			available[partnerIndex] = true;
		}
	}

	for (int index = 0; index < static_cast<int>(playerNameImages.size()) && index < 4; index++)
	{
		if (playerNameImages[index] == nullptr)
		{
			continue;
		}
		if (index == currentIndex)
		{
			playerNameImages[index]->frameIndex = 2;
		}
		else
		{
			playerNameImages[index]->frameIndex = available[index] ? 1 : 0;
		}
	}
}

int EquipMenu::getPartIndex(const std::string & part)
{
	std::string normalizedPart = normalizeEquipmentPart(part);
	if (normalizedPart == "head")
	{
		return 0;
	}
	else if (normalizedPart == "neck")
	{
		return 1;
	}
	else if (normalizedPart == "body")
	{
		return 2;
	}
	else if (normalizedPart == "back")
	{
		return 3;
	}
	else if (normalizedPart == "hand")
	{
		return 4;
	}
	else if (normalizedPart == "wrist")
	{
		return 5;
	}
	else if (normalizedPart == "foot")
	{
		return 6;
	}
	return -1;
}

void EquipMenu::updatePanelImage()
{
	if (gm == nullptr || !gm->global.feature.characterPanelImages)
	{
		return;
	}

	int panelIndex = gm->global.data.characterIndex;
	if (panelIndex < 0)
	{
		panelIndex = 0;
	}
	if (loadedPanelIndex == panelIndex)
	{
		return;
	}

	std::string fileName = "panel7.asf";
	if (panelIndex > 0)
	{
		fileName = convert::formatString("panel7%c.asf", (char)('a' + panelIndex));
	}
	impImage = IMP::createIMPImage(std::string("asf\\ui\\common\\") + fileName);
	loadedPanelIndex = panelIndex;
}

void EquipMenu::onEvent()
{
	if (!visible)
	{
		return;
	}

	updateDataBindings();
	updatePlayerNameDisplay();
	updateMagicDisplay();
	if (gm != nullptr && gm->menu != nullptr
		&& gm->menu->controllerTransfers().active()
		&& currentDragItem != nullptr)
	{
		gm->menu->cancelControllerInteraction();
	}

	for (size_t i = 0; i < GOODS_BODY_COUNT; i++)
	{
		if (item[i] == nullptr) continue;

		unsigned int ret = item[i]->getResult();
		int listIndex = gm->goodsManager.equipIndex(static_cast<int>(i));
		if (ret & erShowHint)
		{
			if (gm->goodsManager.goodsListExists(listIndex))
			{
				gm->menu->showGoodsToolTip(
					getMySharedPtr(),
					gm->goodsManager.goodsList[listIndex].goods,
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
		if ((ret & erClick) && (ret & erMouseRDown) && false)
		{
			for (int j = gm->goodsManager.storeBegin(); j <= gm->goodsManager.storeEnd(); ++j)
			{
				if (gm->goodsManager.goodsList[j].iniFile.empty())
				{
					gm->goodsManager.exchange(j, item[i]->dragIndex);
					updateGoods(i);
					gm->menu->goodsMenu->updateGoods();
					break;
				}
			}
			gm->menu->toolTip->visible = false;
			item[i]->resetHint();
		}
#else
		if (ret & erMouseRDown)
		{
			gm->menu->cancelControllerInteraction();
			gm->menu->toolTip->visible = false;
			item[i]->resetHint();
		}
#endif

		if (ret & erDropped)
		{
			gm->menu->cancelControllerInteraction();
			gm->menu->toolTip->visible = false;
			item[i]->resetHint();
			if (item[i]->dropType == dtGoods)
			{
				if (gm->goodsManager.goodsListExists(item[i]->dropIndex))
				{
					std::string message;
					if (gm->goodsManager.canEquipGoodsAt(item[i]->dropIndex, i, gm->player, &message))
					{
						gm->goodsManager.exchange(item[i]->dropIndex, item[i]->dragIndex);
						updateGoods(i);
					}
					else if (!message.empty())
					{
						gm->showMessage(message);
					}
					if (gm->goodsManager.isStoreIndex(item[i]->dropIndex))
					{
						updateGoods(i);
						gm->menu->goodsMenu->updateGoods();
					}
					else if (gm->goodsManager.isBottomIndex(item[i]->dropIndex))
					{
						updateGoods(i);
						gm->menu->bottomMenu->updateGoodsItem();
					}
					else
					{
						updateGoods();
					}
				}
			}
		}
	}

	for (int i = 0; i < static_cast<int>(magicDisplayItem.size()); i++)
	{
		if (magicDisplayItem[i] == nullptr)
		{
			continue;
		}

		unsigned int ret = magicDisplayItem[i]->getResult();
		if (ret & erShowHint)
		{
			int listIndex = magicDisplayItem[i]->dragIndex;
			if (gm->magicManager.magicListExists(listIndex))
			{
				gm->menu->showMagicToolTip(
					getMySharedPtr(),
					gm->magicManager.magicList[listIndex].magic,
					gm->magicManager.magicList[listIndex].level,
					magicDisplayItem[i]);
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
		if ((ret & erClick) || (ret & erMouseRDown))
#else
		if (ret & erMouseRDown)
#endif
		{
			gm->menu->cancelControllerInteraction();
			quickEquipMagic(magicDisplayItem[i]->dragIndex);
			gm->menu->toolTip->visible = false;
			magicDisplayItem[i]->resetHint();
		}
		if (ret & erDropped)
		{
			gm->menu->cancelControllerInteraction();
			gm->menu->toolTip->visible = false;
			magicDisplayItem[i]->resetHint();
			if (magicDisplayItem[i]->dropType == dtMagic)
			{
				int targetIndex = magicDisplayItem[i]->dragIndex;
				int sourceIndex = magicDisplayItem[i]->dropIndex;
				if (gm->magicManager.isStoreIndex(targetIndex) && gm->magicManager.magicListExists(sourceIndex))
				{
					gm->magicManager.exchange(sourceIndex, targetIndex);
					updateMagicDisplay();
					updateMagicRelatedMenus(sourceIndex);
				}
			}
		}
	}
}

void EquipMenu::init()
{
	freeResource();
	loadMenuDefinition("ini\\ui\\equip\\equip.menu.ini");
	updatePanelImage();

	image = getComponentByName<ImageContainer>("image");
	title = getComponentByName<ImageContainer>("title");
	magicScrollbar = getComponentByName<Scrollbar>("magicScrollbar");
	playerNameImages.clear();
	for (int i = 0; i < 4; i++)
	{
		playerNameImages.push_back(getComponentByName<ImageContainer>(convert::formatString("showname%02d", i)));
	}
	loadNewSwordPartnerNames();

	for (size_t i = 0; i < GOODS_BODY_COUNT; i++)
	{
		std::string itemName = convert::formatString("item%d", i + 1);
		item[i] = getComponentByName<Item>(itemName);
		if (item[i])
		{
			item[i]->dragType = dtGoods;
			item[i]->dragIndex = gm->goodsManager.equipIndex(static_cast<int>(i));
			item[i]->canShowHint = true;
		}
	}

	magicDisplayItem.clear();
	magicDisplayIniFile.clear();
	for (int i = 1;; i++)
	{
		std::string itemName = convert::formatString("magic%02d", i);
		auto magicItem = getComponentByName<Item>(itemName);
		if (!magicItem)
		{
			break;
		}
		magicItem->dragType = dtMagic;
		magicItem->dragIndex = gm->magicManager.storeBegin() + i - 1;
		magicItem->canDrop = true;
		magicItem->canShowHint = true;
		magicDisplayItem.push_back(magicItem);
		magicDisplayIniFile.emplace_back();
	}

	if (magicScrollbar != nullptr)
	{
		magicScrollbar->pageSize = static_cast<int>(magicDisplayItem.size());
		int lineSize = std::max(1, magicScrollbar->lineSize);
		int visibleCount = std::max(1, magicScrollbar->pageSize);
		int scrollableCount = std::max(0, gm->global.magicLayout.storeCount() - visibleCount);
		magicScrollbar->min = 0;
		magicScrollbar->max = (scrollableCount + lineSize - 1) / lineSize;
		magicScrollbar->position = magicScrollbar->min;
	}

	setChildRectReferToParent();
	configureControllerFocus();
	updatePlayerNameDisplay();
	updateMagicDisplay();
}

void EquipMenu::freeResource()
{
	// The router borrows both slot controllers. Detach it before clearing the
	// borrowed targets so an active target can still be deactivated safely.
	controllerPaneRouter.clear();
	equipmentSlotController.clear();
	magicSlotController.clear();
	image = nullptr;
	title = nullptr;
	magicScrollbar = nullptr;
	playerNameImages.clear();
	for (size_t i = 0; i < GOODS_BODY_COUNT; i++)
	{
		item[i] = nullptr;
	}
	magicDisplayItem.clear();
	magicDisplayIniFile.clear();
	newSwordPartnerNames.clear();
	loadedPanelIndex = -1;
	ConfigDrivenPanel::freeResource();
}

void EquipMenu::configureControllerFocus()
{
	// Rebinding a config-driven panel replaces the borrowed target contents.
	// Clear the router first so it cannot retain an active old binding.
	controllerPaneRouter.clear();

	std::vector<std::shared_ptr<Item>> equipmentItems;
	equipmentItems.reserve(GOODS_BODY_COUNT);
	for (const auto& equipmentItem : item)
	{
		equipmentItems.push_back(equipmentItem);
	}

	SlotInteractionBinding equipmentBinding =
		MenuController::makeControllerSlotInteractionBinding(
			gm,
			ControllerSlotKind::Goods,
			ControllerSlotDomain::PlayerEquipment);
	equipmentBinding.grid.focusIdPrefix = "equipment-item-";
	equipmentBinding.grid.items = std::move(equipmentItems);
	equipmentBinding.grid.fixedColumnCount = 3;
	equipmentBinding.grid.resolveLogicalIndex = [this](int visibleIndex)
	{
		if (gm == nullptr || visibleIndex < 0 || visibleIndex >= GOODS_BODY_COUNT)
		{
			return -1;
		}
		const int logicalIndex = gm->goodsManager.equipIndex(visibleIndex);
		return gm->goodsManager.isEquipIndex(logicalIndex) ? logicalIndex : -1;
	};
	equipmentBinding.grid.primary = [this](int logicalIndex, int visibleIndex)
	{
		activateControllerEquipment(logicalIndex, visibleIndex);
	};
	equipmentBinding.grid.details = [this](int logicalIndex, int visibleIndex)
	{
		showControllerEquipmentDetails(logicalIndex, visibleIndex);
	};
	equipmentBinding.grid.hideDetails = [this]() { hideControllerDetails(); };
	equipmentSlotController.bind(std::move(equipmentBinding));
	configureEquipmentControllerNeighbours();

	SlotInteractionBinding magicBinding =
		MenuController::makeControllerSlotInteractionBinding(
			gm,
			ControllerSlotKind::Magic,
			ControllerSlotDomain::MagicList);
	magicBinding.grid.focusIdPrefix = "integrated-magic-item-";
	magicBinding.grid.items = magicDisplayItem;
	magicBinding.grid.scrollbar = magicScrollbar;
	magicBinding.grid.resolveLogicalIndex = [this](int visibleIndex)
	{
		return getControllerMagicIndex(visibleIndex);
	};
	magicBinding.grid.primary = [this](int logicalIndex, int visibleIndex)
	{
		activateControllerMagic(logicalIndex, visibleIndex);
	};
	magicBinding.grid.details = [this](int logicalIndex, int visibleIndex)
	{
		showControllerMagicDetails(logicalIndex, visibleIndex);
	};
	magicBinding.grid.hideDetails = [this]() { hideControllerDetails(); };
	magicBinding.grid.refreshAfterScroll = [this]() { updateMagicDisplay(); };
	magicSlotController.bind(std::move(magicBinding));

	controllerPaneRouter.registerTargetPane(
		EquipmentControllerPaneId,
		equipmentSlotController);
	controllerPaneRouter.registerTargetPane(
		MagicControllerPaneId,
		magicSlotController);
	controllerPaneRouter.setDefaultPane(EquipmentControllerPaneId);
}

void EquipMenu::configureEquipmentControllerNeighbours()
{
	auto connect = [this](int from, UIFocusDirection direction, int to)
	{
		equipmentSlotController.setNeighbour(from, direction, to);
	};
	const bool threeColumnLegacyLayout = item[0] != nullptr && item[2] != nullptr
		&& item[2]->rect.x < item[0]->rect.x;
	if (threeColumnLegacyLayout)
	{
		connect(0, UIFocusDirection::Right, 1);
		connect(0, UIFocusDirection::Down, 2);
		connect(1, UIFocusDirection::Left, 0);
		connect(1, UIFocusDirection::Down, 4);
		connect(2, UIFocusDirection::Up, 0);
		connect(2, UIFocusDirection::Right, 3);
		connect(2, UIFocusDirection::Down, 5);
		connect(3, UIFocusDirection::Up, 0);
		connect(3, UIFocusDirection::Left, 2);
		connect(3, UIFocusDirection::Right, 4);
		connect(3, UIFocusDirection::Down, 6);
		connect(4, UIFocusDirection::Up, 1);
		connect(4, UIFocusDirection::Left, 3);
		connect(4, UIFocusDirection::Down, 6);
		connect(5, UIFocusDirection::Up, 2);
		connect(5, UIFocusDirection::Right, 6);
		connect(6, UIFocusDirection::Up, 4);
		connect(6, UIFocusDirection::Left, 5);
		return;
	}

	connect(0, UIFocusDirection::Right, 1);
	connect(0, UIFocusDirection::Down, 5);
	connect(1, UIFocusDirection::Left, 0);
	connect(1, UIFocusDirection::Down, 4);
	connect(2, UIFocusDirection::Up, 0);
	connect(2, UIFocusDirection::Left, 5);
	connect(2, UIFocusDirection::Right, 4);
	connect(2, UIFocusDirection::Down, 6);
	connect(3, UIFocusDirection::Up, 4);
	connect(3, UIFocusDirection::Left, 6);
	connect(4, UIFocusDirection::Up, 1);
	connect(4, UIFocusDirection::Left, 2);
	connect(4, UIFocusDirection::Down, 3);
	connect(5, UIFocusDirection::Up, 0);
	connect(5, UIFocusDirection::Right, 2);
	connect(5, UIFocusDirection::Down, 6);
	connect(6, UIFocusDirection::Up, 5);
	connect(6, UIFocusDirection::Right, 3);
}

bool EquipMenu::activateControllerFocus(ControllerFocusTarget target)
{
	if (target == ControllerFocusTarget::MagicList)
	{
		return focusControllerMagicList();
	}
	if (target == ControllerFocusTarget::Default
		|| target == ControllerFocusTarget::PlayerEquipment)
	{
		return focusControllerEquipment();
	}
	return false;
}

bool EquipMenu::focusControllerEquipment()
{
	return controllerPaneRouter.activatePane(EquipmentControllerPaneId);
}

bool EquipMenu::focusControllerMagicList()
{
	return controllerPaneRouter.activatePane(MagicControllerPaneId);
}

bool EquipMenu::isControllerFocusActive() const
{
	return controllerPaneRouter.isActive();
}

void EquipMenu::deactivateControllerFocus()
{
	controllerPaneRouter.deactivate();
	hideControllerDetails();
}

PElement EquipMenu::controllerFocusedElement() const
{
	return controllerPaneRouter.controllerFocusedElement();
}

std::vector<PElement> EquipMenu::controllerFocusCandidates() const
{
	return controllerPaneRouter.controllerFocusCandidates();
}

bool EquipMenu::focusControllerElement(const PElement& element)
{
	hideControllerDetails();
	return controllerPaneRouter.focusControllerElement(element);
}

bool EquipMenu::controllerFocusElementMatchesTarget(
	const PElement& element,
	ControllerFocusTarget target) const
{
	auto contains = [&element](const std::vector<PElement>& candidates)
	{
		return std::find(candidates.begin(), candidates.end(), element)
			!= candidates.end();
	};
	if (target == ControllerFocusTarget::Default
		|| target == ControllerFocusTarget::PlayerEquipment)
	{
		return contains(equipmentSlotController.controllerFocusCandidates());
	}
	if (target == ControllerFocusTarget::MagicList)
	{
		return contains(magicSlotController.controllerFocusCandidates());
	}
	return false;
}

void EquipMenu::activateControllerEquipment(int logicalIndex, int)
{
	if (gm == nullptr || gm->menu == nullptr
		|| !gm->goodsManager.isEquipIndex(logicalIndex))
	{
		return;
	}
	if (!gm->goodsManager.goodsListExists(logicalIndex))
	{
		return;
	}
	std::string message;
	if (!gm->goodsManager.unequipToFirstStoreSlot(logicalIndex, &message)
		&& !message.empty())
	{
		gm->showMessage(message);
	}
}
void EquipMenu::showControllerEquipmentDetails(
	int logicalIndex, int visibleIndex)
{
	if (gm == nullptr || gm->menu == nullptr || gm->menu->toolTip == nullptr
		|| gm->menu->upMenu == nullptr || visibleIndex < 0
		|| visibleIndex >= GOODS_BODY_COUNT || item[visibleIndex] == nullptr
		|| !gm->goodsManager.goodsListExists(logicalIndex))
	{
		hideControllerDetails();
		return;
	}
	gm->menu->showGoodsToolTip(
		getMySharedPtr(),
		gm->goodsManager.goodsList[logicalIndex].goods,
		item[visibleIndex]);
}

int EquipMenu::getControllerMagicIndex(int visibleIndex) const
{
	if (gm == nullptr || visibleIndex < 0
		|| visibleIndex >= static_cast<int>(magicDisplayItem.size())
		|| magicDisplayItem[visibleIndex] == nullptr)
	{
		return -1;
	}
	const int logicalIndex = getMagicDisplayStartIndex() + visibleIndex;
	return gm->magicManager.isStoreIndex(logicalIndex)
		&& logicalIndex < gm->magicManager.listLength()
		? logicalIndex : -1;
}

void EquipMenu::activateControllerMagic(int logicalIndex, int)
{
	if (gm == nullptr || gm->menu == nullptr
		|| !gm->magicManager.isStoreIndex(logicalIndex))
	{
		return;
	}
	hideControllerDetails();
	quickEquipMagic(logicalIndex);
}
void EquipMenu::showControllerMagicDetails(int logicalIndex, int visibleIndex)
{
	if (gm == nullptr || gm->menu == nullptr || gm->menu->toolTip == nullptr
		|| gm->menu->upMenu == nullptr || visibleIndex < 0
		|| visibleIndex >= static_cast<int>(magicDisplayItem.size())
		|| magicDisplayItem[visibleIndex] == nullptr
		|| !gm->magicManager.magicListExists(logicalIndex))
	{
		hideControllerDetails();
		return;
	}
	gm->menu->showMagicToolTip(
		getMySharedPtr(),
		gm->magicManager.magicList[logicalIndex].magic,
		gm->magicManager.magicList[logicalIndex].level,
		magicDisplayItem[visibleIndex]);
}

void EquipMenu::hideControllerDetails()
{
	if (gm != nullptr && gm->menu != nullptr)
	{
		gm->menu->hideToolTip();
	}
}

void EquipMenu::refreshControllerTransferHighlight()
{
	equipmentSlotController.refreshTransferSelection();
	magicSlotController.refreshTransferSelection();
}

bool EquipMenu::onHandleUIAction(UIAction action)
{
	return controllerPaneRouter.handleAction(action);
}
