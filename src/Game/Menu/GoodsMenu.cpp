#include "GoodsMenu.h"
#include "../GameManager/GameManager.h"
#include "../../libconvert/libconvert.h"
#include "BuySellMenu.h"
#include "MenuResource.h"
#include <algorithm>
#include <utility>

GoodsMenu::GoodsMenu()
{
	name = "GoodsMenu";
	visible = false;
	init();
}

GoodsMenu::~GoodsMenu()
{
	freeResource();
}

void GoodsMenu::updateMoney()
{
	if (money)
	{
		money->setStr(convert::formatString("%d", gm->player->money));
	}
}

void GoodsMenu::updateGoods()
{
	if (scrollbar == nullptr)
	{
		return;
	}
	position = scrollbar->position;
	for (size_t i = 0; i < item.size(); i++)
	{
		if (item[i] == nullptr) continue;
		item[i]->dragIndex = gm->goodsManager.storeBegin() + static_cast<int>(i) + scrollbar->position * scrollbar->lineSize;
		updateGoods(static_cast<int>(i));
	}
	refreshControllerTransferHighlight();
}

void GoodsMenu::updateGoods(int index)
{
	if (scrollbar == nullptr || index < 0 || index >= static_cast<int>(item.size()) || item[index] == nullptr)
	{
		return;
	}

	int listIndex = gm->goodsManager.storeBegin() + index + scrollbar->position * scrollbar->lineSize;
	if (!gm->goodsManager.isStoreIndex(listIndex) || listIndex >= gm->goodsManager.listLength())
	{
		item[index]->impImage = nullptr;
		item[index]->setStr("");
		return;
	}

	item[index]->impImage = nullptr;

	updateGoodsNumber(index);
	if (gm->goodsManager.goodsList[listIndex].goods != nullptr)
	{
		item[index]->impImage = MenuResource::createGoodsMenuImage(gm->goodsManager.goodsList[listIndex].goods);
	}

}

void GoodsMenu::updateGoodsNumber(int index)
{
	if (scrollbar == nullptr || index < 0 || index >= static_cast<int>(item.size()) || item[index] == nullptr)
	{
		return;
	}
	int listIndex = gm->goodsManager.storeBegin() + index + scrollbar->position * scrollbar->lineSize;
	if (!gm->goodsManager.isStoreIndex(listIndex) || listIndex >= gm->goodsManager.listLength())
	{
		item[index]->setStr("");
		return;
	}
	if (gm->goodsManager.goodsList[listIndex].goods != nullptr && gm->goodsManager.goodsList[listIndex].number > 0 && gm->goodsManager.goodsList[listIndex].iniFile != "")
	{
		item[index]->setStr(convert::formatString("%d", gm->goodsManager.goodsList[listIndex].number));
	}
	else
	{
		item[index]->setStr("");
		gm->goodsManager.goodsList[listIndex].clear();
	}
}

void GoodsMenu::onEvent()
{
	if (gm != nullptr && gm->menu != nullptr
		&& gm->menu->controllerTransfers().active(ControllerSlotKind::Goods)
		&& currentDragItem != nullptr)
	{
		cancelControllerInteraction();
	}
	if (scrollbar != nullptr && position != scrollbar->position)
	{
		hideControllerItemDetails();
		position = scrollbar->position;
		updateGoods();
	}
	if (scrollbar == nullptr)
	{
		return;
	}
	for (size_t i = 0; i < item.size(); i++)
	{
		if (item[i] == nullptr)
		{
			continue;
		}
		int listIndex = gm->goodsManager.storeBegin() + static_cast<int>(i) + scrollbar->position * scrollbar->lineSize;
		unsigned int ret = item[i]->getResult();
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
		if ((ret & erMouseRDown) || (ret & erClick))
#else
		if (ret & erMouseRDown)
#endif
		{
			cancelControllerInteraction();
			gm->menu->toolTip->visible = false;
			item[i]->resetHint();
			if (BuySellMenu::getInstance() != nullptr && BuySellMenu::getInstance()->visible)
			{
				auto buySellMenu = BuySellMenu::getInstance();
				buySellMenu->clearControllerFocus();
				buySellMenu->sellOneFromPlayerSlot(item[i]->dragIndex);
			}
			else
			{
#ifdef __MOBILE__
				if (gm->goodsManager.goodsListExists(item[i]->dragIndex))
				{
					if (gm->goodsManager.goodsList[item[i]->dragIndex].goods->kind == gkDrug)
					{
						for (int j = gm->goodsManager.bottomBegin(); j <= gm->goodsManager.bottomEnd(); ++j)
						{
							if (!gm->goodsManager.goodsListExists(j))
							{
								gm->goodsManager.exchange(j, item[i]->dragIndex);
								updateGoods(i);
								gm->menu->bottomMenu->updateGoodsItem(gm->goodsManager.bottomSlot(j));
								break;
							}
						}
					}
					else
					{
#endif
						gm->goodsManager.useItem(item[i]->dragIndex);
#ifdef __MOBILE__
					}
				}
#endif
			}
		}
		if (ret & erDropped)
		{
			cancelControllerInteraction();
			gm->menu->toolTip->visible = false;
			item[i]->resetHint();
			if (item[i]->dropType == dtGoods)
			{
				if (gm->goodsManager.goodsListExists(item[i]->dropIndex))
				{
					if (gm->goodsManager.isStoreIndex(item[i]->dropIndex))
					{
						gm->goodsManager.exchange(item[i]->dropIndex, item[i]->dragIndex);
						updateGoods();
					}
					else if (gm->goodsManager.isBottomIndex(item[i]->dropIndex))
					{
						gm->goodsManager.exchange(item[i]->dropIndex, item[i]->dragIndex);
						updateGoods(i);
						gm->menu->bottomMenu->updateGoodsItem();
					}
					else if (gm->goodsManager.isEquipIndex(item[i]->dropIndex))
					{
						bool canExchange = gm->goodsManager.goodsList[item[i]->dragIndex].goods == nullptr;
						std::string message;
						if (!canExchange && gm->goodsManager.goodsListExists(item[i]->dragIndex))
						{
							canExchange = gm->goodsManager.canEquipGoodsAt(
								item[i]->dragIndex,
								gm->goodsManager.equipSlot(item[i]->dropIndex),
								gm->player,
								&message);
						}
						if (canExchange)
						{
							gm->goodsManager.exchange(item[i]->dropIndex, item[i]->dragIndex);
							updateGoods(i);
							gm->menu->equipMenu->updateGoods();
						}
						else if (!message.empty())
						{
							gm->showMessage(message);
						}
					}
				}
			}
			else if (item[i]->dropType == dtBuy)
			{
				auto buySellMenu = BuySellMenu::getInstance();
				if (buySellMenu != nullptr && buySellMenu->visible && buySellMenu->goodsList[item[i]->dropIndex].iniFile != "")
				{
					if (buySellMenu->numberValid && buySellMenu->goodsList[item[i]->dropIndex].number <= 0)
					{
						gm->showMessage("该物品已售罄");
					}
					else if (gm->goodsManager.goodsList[item[i]->dragIndex].iniFile.empty())
					{
						if (gm->goodsManager.buyItem(buySellMenu->goodsList[item[i]->dropIndex].iniFile, 1))
						{
							std::string iniName = buySellMenu->goodsList[item[i]->dropIndex].iniFile;
							updateMoney();
							if (buySellMenu->numberValid)
							{
								buySellMenu->goodsList[item[i]->dropIndex].number--;
								buySellMenu->updateGoods();
							}
							int idx = 0;
							for (int j = 0; j < gm->goodsManager.listLength(); j++)
							{
								if (gm->goodsManager.goodsList[j].iniFile == iniName)
								{
									idx = j;
									break;
								}
							}
							gm->goodsManager.exchange(item[i]->dragIndex, idx);
							updateGoods(i);
							if (gm->goodsManager.isStoreIndex(idx))
							{
								updateGoods();
							}
							else if (gm->goodsManager.isBottomIndex(idx))
							{
								gm->menu->bottomMenu->updateGoodsItem();
							}
							else
							{
								gm->menu->equipMenu->updateGoods();
							}
						}

					}
					else if (gm->goodsManager.goodsList[item[i]->dragIndex].iniFile == buySellMenu->goodsList[item[i]->dropIndex].iniFile)
					{
						if (gm->goodsManager.buyItem(buySellMenu->goodsList[item[i]->dropIndex].iniFile, 1))
						{
							if (buySellMenu->numberValid)
							{
								buySellMenu->goodsList[item[i]->dropIndex].number--;
								buySellMenu->updateGoods();
							}
							updateGoods(i);
							updateMoney();
						}
					}
				}			
			}
			else if (item[i]->dropType == dtSell)
			{
				if (BuySellMenu::getInstance() != nullptr && BuySellMenu::getInstance()->visible && BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].iniFile != "")
				{
					if (gm->goodsManager.goodsList[item[i]->dragIndex].iniFile.empty())
					{
						if (gm->goodsManager.buyItem(BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].iniFile, BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].number))
						{
							std::string iniName = BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].iniFile;
							updateMoney();
							BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].clear();
							BuySellMenu::getInstance()->updateGoods();


							int idx = 0;
							for (int j = 0; j < gm->goodsManager.listLength(); j++)
							{
								if (gm->goodsManager.goodsList[j].iniFile == iniName)
								{
									idx = j;
									break;
								}
							}
							gm->goodsManager.exchange(item[i]->dragIndex, idx);
							updateGoods(i);
							if (gm->goodsManager.isStoreIndex(idx))
							{
								updateGoods();
							}
							else if (gm->goodsManager.isBottomIndex(idx))
							{
								gm->menu->bottomMenu->updateGoodsItem();
							}
							else
							{
								gm->menu->equipMenu->updateGoods();
							}
						}
					}
					else if (gm->goodsManager.goodsList[item[i]->dragIndex].iniFile == BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].iniFile)
					{
						if (gm->goodsManager.buyItem(BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].iniFile, BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].number))
						{
							BuySellMenu::getInstance()->goodsList[item[i]->dropIndex].clear();
							BuySellMenu::getInstance()->updateGoods();
							updateGoods(i);
							updateMoney();
						}
					}
				}			
			}						
		}
	}
}

void GoodsMenu::init()
{
	freeResource();
	loadMenuDefinition("ini\\ui\\goods\\goods.menu.ini");

	scrollbar = getComponentByName<Scrollbar>("scrollbar");
	money = getComponentByName<Label>("money");

	item.clear();
	for (int i = 1;; i++)
	{
		std::string itemName = convert::formatString("item%d", i);
		auto itemComponent = getComponentByName<Item>(itemName);
		if (!itemComponent)
		{
			break;
		}
		itemComponent->dragType = dtGoods;
		itemComponent->canShowHint = true;
		item.push_back(itemComponent);
	}

	if (scrollbar != nullptr)
	{
		scrollbar->pageSize = static_cast<int>(item.size());
		int lineSize = std::max(1, scrollbar->lineSize);
		int visibleCount = std::max(1, scrollbar->pageSize);
		int scrollableCount = std::max(0, gm->global.goodsLayout.storeCount() - visibleCount);
		scrollbar->min = 0;
		scrollbar->max = (scrollableCount + lineSize - 1) / lineSize;
		scrollbar->position = scrollbar->min;
	}

	setChildRectReferToParent();
	configureControllerFocus();
}

void GoodsMenu::freeResource()
{
	slotController.clear();
	scrollbar = nullptr;
	money = nullptr;
	item.clear();
	ConfigDrivenPanel::freeResource();
}

void GoodsMenu::configureControllerFocus()
{
	SlotInteractionBinding binding =
		MenuController::makeControllerSlotInteractionBinding(
			gm,
			ControllerSlotKind::Goods,
			ControllerSlotDomain::GoodsBag);
	binding.grid.focusIdPrefix = "goods-item-";
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
		updateGoods();
	};
	slotController.bind(std::move(binding));
}

bool GoodsMenu::activateControllerFocus(ControllerFocusTarget target)
{
	return (target == ControllerFocusTarget::Default
		|| target == ControllerFocusTarget::GoodsBag)
		&& focusControllerDefault();
}

bool GoodsMenu::focusControllerDefault()
{
	return slotController.activate();
}

bool GoodsMenu::isControllerFocusActive() const
{
	return slotController.isActive();
}

void GoodsMenu::deactivateControllerFocus()
{
	slotController.deactivate();
}

PElement GoodsMenu::controllerFocusedElement() const
{
	return slotController.controllerFocusedElement();
}

std::vector<PElement> GoodsMenu::controllerFocusCandidates() const
{
	return slotController.controllerFocusCandidates();
}

bool GoodsMenu::focusControllerElement(const PElement& element)
{
	return slotController.focusControllerElement(element);
}

SlotGridView GoodsMenu::controllerBagView()
{
	SlotGridView view;
	if (gm == nullptr || gm->menu == nullptr
		|| gm->menu->goodsMenu.get() != this)
	{
		return view;
	}

	const std::shared_ptr<GoodsMenu> owner = gm->menu->goodsMenu;
	std::weak_ptr<GoodsMenu> weakOwner = owner;
	view.items = item;
	view.scrollbar = scrollbar;
	const std::size_t visibleItemCount = view.items.size();
	std::weak_ptr<Scrollbar> weakScrollbar = view.scrollbar;
	view.resolveLogicalIndex =
		[weakOwner, weakScrollbar, visibleItemCount](int visibleIndex)
	{
		auto currentOwner = weakOwner.lock();
		auto currentScrollbar = weakScrollbar.lock();
		if (currentOwner == nullptr || currentScrollbar == nullptr
			|| gm == nullptr || visibleIndex < 0
			|| visibleIndex >= static_cast<int>(visibleItemCount))
		{
			return -1;
		}
		const int listIndex = gm->goodsManager.storeBegin()
			+ visibleIndex
			+ currentScrollbar->position * currentScrollbar->lineSize;
		return gm->goodsManager.isStoreIndex(listIndex)
			&& listIndex < gm->goodsManager.listLength()
			? listIndex
			: -1;
	};
	view.refreshAfterScroll = [weakOwner, weakScrollbar]()
	{
		auto currentOwner = weakOwner.lock();
		auto currentScrollbar = weakScrollbar.lock();
		if (currentOwner == nullptr || currentScrollbar == nullptr
			|| currentOwner->scrollbar != currentScrollbar)
		{
			return;
		}
		currentOwner->position = currentScrollbar->position;
		currentOwner->updateGoods();
	};
	return view;
}

int GoodsMenu::getControllerItemIndex(int visibleIndex) const
{
	if (gm == nullptr || scrollbar == nullptr || visibleIndex < 0
		|| visibleIndex >= static_cast<int>(item.size())
		|| item[visibleIndex] == nullptr)
	{
		return -1;
	}
	const int listIndex = gm->goodsManager.storeBegin()
		+ visibleIndex + scrollbar->position * scrollbar->lineSize;
	return gm->goodsManager.isStoreIndex(listIndex)
		&& listIndex < gm->goodsManager.listLength()
		? listIndex
		: -1;
}

void GoodsMenu::activateControllerItem(int visibleIndex)
{
	const int listIndex = getControllerItemIndex(visibleIndex);
	if (listIndex < 0 || !gm->goodsManager.goodsListExists(listIndex))
	{
		return;
	}
	hideControllerItemDetails();
	gm->goodsManager.useItem(listIndex);
	updateGoods();
}
void GoodsMenu::showControllerItemDetails(int visibleIndex)
{
	const int listIndex = getControllerItemIndex(visibleIndex);
	if (gm->menu == nullptr || gm->menu->toolTip == nullptr
		|| gm->menu->upMenu == nullptr || listIndex < 0
		|| !gm->goodsManager.goodsListExists(listIndex))
	{
		hideControllerItemDetails();
		return;
	}
	gm->menu->showGoodsToolTip(
		getMySharedPtr(),
		gm->goodsManager.goodsList[listIndex].goods,
		item[visibleIndex]);
}

void GoodsMenu::hideControllerItemDetails()
{
	if (gm != nullptr && gm->menu != nullptr)
	{
		gm->menu->hideToolTip();
	}
}

void GoodsMenu::refreshControllerTransferHighlight()
{
	slotController.refreshTransferSelection();
}

void GoodsMenu::cancelControllerInteraction()
{
	if (gm != nullptr && gm->menu != nullptr
		&& gm->menu->controllerTransfers().active(ControllerSlotKind::Goods))
	{
		gm->menu->controllerTransfers().cancel();
	}
	refreshControllerTransferHighlight();
	hideControllerItemDetails();
}

bool GoodsMenu::onHandleUIAction(UIAction action)
{
	return slotController.handleAction(action);
}
