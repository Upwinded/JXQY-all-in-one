#include "PartnerEquipMenu.h"
#include "../../Engine/Engine.h"
#include "GoodsMenu.h"
#include "../Data/Goods.h"
#include "../Data/NPC.h"
#include "../GameManager/GameManager.h"

#include <utility>

namespace
{
const char* SlotNames[GOODS_BODY_COUNT] = {
	"头",
	"颈",
	"身",
	"背",
	"手",
	"腕",
	"脚",
};
}

PartnerEquipMenu::PartnerEquipMenu()
{
	name = "PartnerEquipMenu";
	setPriority(epMax);
	visible = false;
	init();
}

PartnerEquipMenu::~PartnerEquipMenu()
{
	freeResource();
}

void PartnerEquipMenu::init()
{
	freeResource();

	rect = { 0, 0, 300, 360 };
	align = alRightCenter;
	alignX = -180;
	setAlign();

	makeLabel(titleLabel, { 18, 14, 220, 24 }, 20, 0xFFFFD37F);
	makeLabel(attributeLabel, { 18, 48, 264, 92 }, 16, 0xFFFFFFFF);
	if (attributeLabel != nullptr)
	{
		attributeLabel->autoNextLine = true;
	}

	int startX = 30;
	int startY = 162;
	int itemSize = 42;
	int gapX = 54;
	int gapY = 66;
	for (int i = 0; i < GOODS_BODY_COUNT; i++)
	{
		int row = i / 4;
		int column = i % 4;
		Rect itemRect = { startX + column * gapX, startY + row * gapY, itemSize, itemSize };
		makeItem(item[i], itemRect);
		makeLabel(itemLabel[i], { itemRect.x, itemRect.y + itemSize + 3, itemSize, 18 }, 14, 0xFFD8D0B8);
		if (itemLabel[i] != nullptr)
		{
			itemLabel[i]->setStr(SlotNames[i]);
		}
	}

	makeButton(closeButton, { 200, 304, 70, 30 }, "关闭");

	setChildRectReferToParent();
	configureControllerFocus();
	updateGoods();
}

void PartnerEquipMenu::setPartner(std::shared_ptr<NPC> value)
{
	if (partner != value)
	{
		cancelControllerInteraction();
	}
	partner = value;
	visible = (partner != nullptr && partner->canEquip > 0);
	updateGoods();
	if (visible)
	{
		focusControllerDefault();
	}
}

std::shared_ptr<NPC> PartnerEquipMenu::getPartner() const
{
	return partner;
}

void PartnerEquipMenu::updateGoods()
{
	for (int i = 0; i < GOODS_BODY_COUNT; i++)
	{
		updateGoods(i);
	}
	updateAttributeLabel();
}

void PartnerEquipMenu::updateGoods(int index)
{
	if (index < 0 || index >= GOODS_BODY_COUNT || item[index] == nullptr)
	{
		return;
	}

	item[index]->impImage = nullptr;
	item[index]->setStr("");
	itemGoods[index] = nullptr;

	if (partner == nullptr)
	{
		return;
	}

	std::string fileName = partner->getEquipmentFileByPartIndex(index);
	if (fileName.empty())
	{
		return;
	}

	itemGoods[index] = std::make_shared<Goods>();
	itemGoods[index]->initFromIni(fileName);
	item[index]->impImage = itemGoods[index]->createGoodsIcon();
	item[index]->setStr("1");
}

bool PartnerEquipMenu::equipFromPlayerSlot(int playerGoodsIndex, int slotIndex)
{
	std::string message;
	if (!gm->partnerManager.equipOnePlayerGoodsOnPartner(
		partner, playerGoodsIndex, slotIndex, &message))
	{
		if (!message.empty())
		{
			gm->showMessage(message);
		}
		return false;
	}
	gm->goodsManager.updateMenu();
	updateGoods(slotIndex);
	updateAttributeLabel();
	return true;
}

bool PartnerEquipMenu::unequipSlot(int slotIndex)
{
	std::string message;
	if (!gm->partnerManager.unequipPartnerGoodsToPlayerBag(
		partner, slotIndex, &message))
	{
		if (!message.empty())
		{
			gm->showMessage(message);
		}
		return false;
	}

	updateGoods(slotIndex);
	updateAttributeLabel();
	gm->goodsManager.updateMenu();
	return true;
}

void PartnerEquipMenu::configureControllerFocus()
{
	controllerPaneRouter.clear();

	SlotInteractionBinding playerBinding =
		MenuController::makeControllerSlotInteractionBinding(
			gm,
			ControllerSlotKind::PartnerGoods,
			ControllerSlotDomain::PartnerBag);
	playerBinding.grid.focusIdPrefix = "partner-player-bag-item-";
	SlotGridView playerView;
	if (gm != nullptr && gm->menu != nullptr && gm->menu->goodsMenu != nullptr)
	{
		playerView = gm->menu->goodsMenu->controllerBagView();
	}
	applySlotGridView(playerBinding.grid, std::move(playerView));
	playerBinding.grid.primary = [this](int playerIndex, int)
	{
		if (!gm->goodsManager.goodsListExists(playerIndex)
			|| gm->goodsManager.goodsList[playerIndex].goods == nullptr)
		{
			gm->showMessage("该位置没有装备");
			return;
		}
		const int slotIndex = NPC::getEquipmentPartIndex(
			gm->goodsManager.goodsList[playerIndex].goods->part);
		if (slotIndex < 0)
		{
			gm->showMessage("只能装备武器防具！");
			return;
		}
		equipFromPlayerSlot(playerIndex, slotIndex);
	};
	playerBinding.grid.details = [this](int playerIndex, int visibleIndex)
	{
		showPlayerControllerDetails(playerIndex, visibleIndex);
	};
	playerBinding.grid.hideDetails = [this]() { hideControllerDetails(); };
	playerBinding.resolveContext = [this]() -> std::shared_ptr<const void>
	{
		return std::static_pointer_cast<const void>(partner);
	};
	playerBagSlotController.bind(std::move(playerBinding));

	SlotInteractionBinding equipmentBinding =
		MenuController::makeControllerSlotInteractionBinding(
			gm,
			ControllerSlotKind::PartnerGoods,
			ControllerSlotDomain::PartnerEquipment);
	equipmentBinding.grid.focusIdPrefix = "partner-equipment-item-";
	for (int slotIndex = 0; slotIndex < GOODS_BODY_COUNT; slotIndex++)
	{
		equipmentBinding.grid.items.push_back(item[slotIndex]);
	}
	equipmentBinding.grid.fixedColumnCount = 4;
	equipmentBinding.grid.resolveLogicalIndex = [](int visibleIndex)
	{
		return visibleIndex >= 0 && visibleIndex < GOODS_BODY_COUNT
			? visibleIndex
			: -1;
	};
	equipmentBinding.grid.primary = [this](int slotIndex, int)
	{
		unequipSlot(slotIndex);
	};
	equipmentBinding.grid.details = [this](int slotIndex, int visibleIndex)
	{
		showEquipmentControllerDetails(slotIndex, visibleIndex);
	};
	equipmentBinding.grid.hideDetails = [this]() { hideControllerDetails(); };
	equipmentBinding.resolveContext = [this]() -> std::shared_ptr<const void>
	{
		return std::static_pointer_cast<const void>(partner);
	};
	partnerEquipmentSlotController.bind(std::move(equipmentBinding));

	controllerPaneRouter.registerTargetPane(
		PlayerBagControllerPaneId,
		playerBagSlotController,
		[this]()
		{
			return visible && partner != nullptr && partner->canEquip > 0
				&& gm != nullptr && gm->menu != nullptr
				&& gm->menu->goodsMenu != nullptr
				&& gm->menu->goodsMenu->scrollbar != nullptr
				&& !gm->menu->goodsMenu->item.empty();
		},
		[this]()
		{
			hideControllerDetails();
			if (gm != nullptr && gm->menu != nullptr
				&& gm->menu->goodsMenu != nullptr)
			{
				gm->menu->goodsMenu->updateGoods();
			}
		});
	controllerPaneRouter.registerTargetPane(
		PartnerEquipmentControllerPaneId,
		partnerEquipmentSlotController,
		[this]()
		{
			return visible && partner != nullptr && partner->canEquip > 0;
		},
		[this]()
		{
			hideControllerDetails();
			updateGoods();
		});
	controllerPaneRouter.setDefaultPane(PartnerEquipmentControllerPaneId);
}

bool PartnerEquipMenu::activateControllerFocus(
	ControllerFocusTarget target)
{
	if (target == ControllerFocusTarget::PartnerBag)
	{
		return focusControllerPlayerBag();
	}
	if (target == ControllerFocusTarget::Default
		|| target == ControllerFocusTarget::PartnerEquipment)
	{
		return focusControllerEquipment();
	}
	return false;
}

bool PartnerEquipMenu::focusControllerDefault()
{
	return controllerPaneRouter.activateDefaultPane();
}

bool PartnerEquipMenu::focusControllerPlayerBag()
{
	return controllerPaneRouter.activatePane(PlayerBagControllerPaneId);
}

bool PartnerEquipMenu::focusControllerEquipment()
{
	return controllerPaneRouter.activatePane(
		PartnerEquipmentControllerPaneId);
}

bool PartnerEquipMenu::isControllerFocusActive() const
{
	return controllerPaneRouter.isActive();
}

void PartnerEquipMenu::deactivateControllerFocus()
{
	controllerPaneRouter.suspend();
	hideControllerDetails();
}

PElement PartnerEquipMenu::controllerFocusedElement() const
{
	return controllerPaneRouter.controllerFocusedElement();
}

std::vector<PElement> PartnerEquipMenu::controllerFocusCandidates() const
{
	return controllerPaneRouter.controllerFocusCandidates();
}

bool PartnerEquipMenu::focusControllerElement(const PElement& element)
{
	hideControllerDetails();
	return controllerPaneRouter.focusControllerElement(element);
}

void PartnerEquipMenu::refreshControllerTransferHighlight()
{
	playerBagSlotController.refreshTransferSelection();
	partnerEquipmentSlotController.refreshTransferSelection();
}

void PartnerEquipMenu::showControllerDetails(
	const std::shared_ptr<Goods>& goods,
	const PElement& anchor)
{
	if (gm == nullptr || gm->menu == nullptr || gm->menu->toolTip == nullptr
		|| gm->menu->upMenu == nullptr || goods == nullptr || anchor == nullptr)
	{
		hideControllerDetails();
		return;
	}
	gm->menu->showGoodsToolTip(getMySharedPtr(), goods, anchor);
}

void PartnerEquipMenu::showPlayerControllerDetails(
	int playerIndex, int visibleIndex)
{
	if (!gm->goodsManager.goodsListExists(playerIndex)
		|| gm->menu == nullptr || gm->menu->goodsMenu == nullptr
		|| visibleIndex < 0
		|| visibleIndex >= static_cast<int>(gm->menu->goodsMenu->item.size()))
	{
		hideControllerDetails();
		return;
	}
	showControllerDetails(
		gm->goodsManager.goodsList[playerIndex].goods,
		gm->menu->goodsMenu->item[visibleIndex]);
}

void PartnerEquipMenu::showEquipmentControllerDetails(
	int slotIndex, int visibleIndex)
{
	if (slotIndex < 0 || slotIndex >= GOODS_BODY_COUNT
		|| visibleIndex < 0 || visibleIndex >= GOODS_BODY_COUNT)
	{
		hideControllerDetails();
		return;
	}
	showControllerDetails(itemGoods[slotIndex], item[visibleIndex]);
}

void PartnerEquipMenu::hideControllerDetails()
{
	if (gm != nullptr && gm->menu != nullptr)
	{
		gm->menu->hideToolTip();
	}
}

void PartnerEquipMenu::cancelControllerInteraction()
{
	if (gm != nullptr && gm->menu != nullptr)
	{
		gm->menu->cancelControllerInteraction();
	}
	refreshControllerTransferHighlight();
}

void PartnerEquipMenu::updateAttributeLabel()
{
	if (titleLabel != nullptr)
	{
		titleLabel->setStr(partner != nullptr ? partner->npcName : "伙伴装备");
	}
	if (attributeLabel == nullptr)
	{
		return;
	}
	if (partner == nullptr)
	{
		attributeLabel->setStr("");
		return;
	}

	attributeLabel->setStr(convert::formatString(
		"等级 %d<enter>生命 %d/%d  内力 %d/%d<enter>体力 %d/%d<enter>攻击 %d  防御 %d  闪避 %d",
		partner->level,
		partner->life,
		partner->getLifeMax(),
		partner->mana,
		partner->getManaMax(),
		partner->thew,
		partner->getThewMax(),
		partner->getAttack(),
		partner->getDefend(),
		partner->getEvade()));
}

void PartnerEquipMenu::makeLabel(std::shared_ptr<Label>& label, const Rect& labelRect, int fontSize, unsigned int color)
{
	label = std::make_shared<Label>();
	label->rect = labelRect;
	label->fontSize = fontSize;
	label->color = color;
	label->coverMouse = false;
	addChild(label);
}

void PartnerEquipMenu::makeButton(std::shared_ptr<TextButton>& button, const Rect& buttonRect, const std::string& text)
{
	button = std::make_shared<TextButton>();
	button->rect = buttonRect;
	button->setFontSize(16);
	button->setStrColor(0xFFFFFFFF);
	button->setStr(text);
	addChild(button);
}

void PartnerEquipMenu::makeItem(std::shared_ptr<Item>& slotItem, const Rect& itemRect)
{
	slotItem = std::make_shared<Item>();
	slotItem->rect = itemRect;
	slotItem->dragIndex = -1;
	slotItem->dragType = dtGoods;
	slotItem->canDrag = false;
	slotItem->canDrop = true;
	slotItem->canShowHint = true;
	slotItem->fontSize = 14;
	addChild(slotItem);
}

void PartnerEquipMenu::onEvent()
{
	if (!visible)
	{
		return;
	}

	if (closeButton != nullptr && closeButton->getResult(erClick))
	{
		if (gm != nullptr && gm->menu != nullptr)
		{
			gm->menu->closePartnerEquipment(true);
		}
		else
		{
			visible = false;
		}
		return;
	}

	for (int i = 0; i < GOODS_BODY_COUNT; i++)
	{
		if (item[i] == nullptr)
		{
			continue;
		}

		unsigned int ret = item[i]->getResult();
		if (ret & erShowHint)
		{
			if (itemGoods[i] != nullptr)
			{
				gm->menu->showGoodsToolTip(
					getMySharedPtr(),
					itemGoods[i],
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
		if (ret & erMouseRDown)
		{
			cancelControllerInteraction();
			gm->menu->toolTip->visible = false;
			item[i]->resetHint();
			unequipSlot(i);
		}
		if (ret & erDropped)
		{
			cancelControllerInteraction();
			gm->menu->toolTip->visible = false;
			item[i]->resetHint();
			if (item[i]->dropType == dtGoods)
			{
				equipFromPlayerSlot(item[i]->dropIndex, i);
			}
		}
	}
}

void PartnerEquipMenu::onUpdate()
{
	if (!visible || partner == nullptr || gm == nullptr
		|| gm->partnerManager.isActivePartner(partner))
	{
		return;
	}
	if (gm->menu != nullptr)
	{
		gm->menu->closePartnerEquipment(true);
	}
	else
	{
		deactivateControllerFocus();
		visible = false;
	}
}

bool PartnerEquipMenu::onHandleEvent(AEvent& e)
{
	if (!visible)
	{
		return false;
	}
	if (isPointerTakeoverEvent(e))
	{
		cancelControllerInteraction();
		return false;
	}
	if (visible && e.eventType == ET_KEYDOWN && e.eventData == KEY_ESCAPE)
	{
		if (gm != nullptr && gm->menu != nullptr)
		{
			gm->menu->closePartnerEquipment(true);
		}
		else
		{
			visible = false;
		}
		return true;
	}
	if (dispatchKeyboardUIAction(e, *this))
	{
		return true;
	}
	return false;
}

bool PartnerEquipMenu::onHandleUIAction(UIAction action)
{
	if (!visible)
	{
		return false;
	}
	if (action == UIAction::Cancel)
	{
		if (gm != nullptr && gm->menu != nullptr)
		{
			gm->menu->closePartnerEquipment(true);
		}
		else
		{
			deactivateControllerFocus();
			visible = false;
		}
		return true;
	}
	return controllerPaneRouter.handleAction(action);
}

void PartnerEquipMenu::onDraw()
{
	if (!visible)
	{
		return;
	}

	engine->fillRect(rect.x, rect.y, rect.w, rect.h, 26, 22, 18, 230);
	engine->fillRect(rect.x + 2, rect.y + 2, rect.w - 4, 2, 215, 166, 78, 255);
	engine->fillRect(rect.x + 2, rect.y + rect.h - 4, rect.w - 4, 2, 76, 52, 36, 255);

	for (int i = 0; i < GOODS_BODY_COUNT; i++)
	{
		if (item[i] != nullptr)
		{
			engine->fillRect(item[i]->rect.x, item[i]->rect.y, item[i]->rect.w, item[i]->rect.h, 52, 43, 34, 235);
			engine->fillRect(item[i]->rect.x + 1, item[i]->rect.y + 1, item[i]->rect.w - 2, 1, 128, 104, 66, 255);
		}
	}
	if (closeButton != nullptr)
	{
		engine->fillRect(closeButton->rect.x, closeButton->rect.y, closeButton->rect.w, closeButton->rect.h, 72, 55, 36, 230);
	}
}

void PartnerEquipMenu::onWindowResize(int width, int height)
{
	const bool controllerWasActive = controllerPaneRouter.isActive();
	const int selectedPane = controllerPaneRouter.selectedPaneId();
	const int playerLogicalIndex =
		playerBagSlotController.focusedLogicalIndex();
	const int equipmentLogicalIndex =
		partnerEquipmentSlotController.focusedLogicalIndex();
	controllerPaneRouter.suspend();
	Panel::onWindowResize(width, height);
	configureControllerFocus();
	if (!controllerWasActive || !visible)
	{
		return;
	}
	if (selectedPane < 0 || !controllerPaneRouter.activatePane(selectedPane))
	{
		controllerPaneRouter.activateDefaultPane();
	}
	if (playerLogicalIndex >= 0)
	{
		playerBagSlotController.focusLogicalIndex(playerLogicalIndex);
	}
	if (equipmentLogicalIndex >= 0)
	{
		partnerEquipmentSlotController.focusLogicalIndex(
			equipmentLogicalIndex);
	}
}

void PartnerEquipMenu::freeResource()
{
	controllerPaneRouter.clear();
	playerBagSlotController.clear();
	partnerEquipmentSlotController.clear();
	hideControllerDetails();
	partner = nullptr;
	titleLabel = nullptr;
	attributeLabel = nullptr;
	closeButton = nullptr;
	for (int i = 0; i < GOODS_BODY_COUNT; i++)
	{
		item[i] = nullptr;
		itemLabel[i] = nullptr;
		itemGoods[i] = nullptr;
	}
	Panel::freeResource();
	removeAllChild();
}
