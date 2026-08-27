#include "BuySellMenu.h"
#include "../../Engine/Engine.h"
#include "ControllerPromptPresenter.h"
#include "GoodsMenu.h"
#include "MenuResource.h"
#include "../Data/BuySellInventory.h"
#include "../Data/NPC.h"
#include "../GameManager/GameManager.h"
#include "../GameManager/SaveFileManager.h"
#include <algorithm>
#include <climits>
#include <cstdint>
#include <utility>

BuySellMenu * BuySellMenu::this_ = nullptr;

namespace
{
void addSaleProceeds(int itemCount, int unitPrice)
{
	if (gm == nullptr || gm->player == nullptr || itemCount <= 0 || unitPrice <= 0)
	{
		return;
	}
	const int64_t proceeds = static_cast<int64_t>(itemCount) * unitPrice;
	const int64_t updatedMoney = static_cast<int64_t>(gm->player->money) + proceeds;
	gm->player->money = static_cast<int>(
		std::clamp<int64_t>(updatedMoney, INT_MIN, INT_MAX));
}

int saturatingInventoryCountAdd(int current, int added)
{
	const int64_t total = static_cast<int64_t>(std::max(0, current)) + added;
	if (total <= 0)
	{
		return 0;
	}
	return static_cast<int>(std::min<int64_t>(total, INT_MAX));
}

std::string resolveBuySellListPath(const std::string& list)
{
	std::string savePath =
		SaveFileManager::CurrentPath() + list;
	if (File::fileExist(savePath))
	{
		return savePath;
	}
	return std::string(BUYSELL_FOLDER) + list;
}

bool readTextFile(const std::string& fileName, std::string& text)
{
	text.clear();
	std::unique_ptr<char[]> content;
	int len = File::readFile(fileName, content);
	if (len <= 0 || content == nullptr)
	{
		return false;
	}
	text.assign(content.get(), len);
	return true;
}

void loadInventoryGoods(const BuySellInventoryData& inventory,
	GoodsInfo goodsList[BUYSELL_GOODS_COUNT],
	int& currentListCount,
	bool& numberValid,
	int& buyPercent,
	int& recyclePercent,
	bool forceNumberValid)
{
	currentListCount = std::max(0, std::min(inventory.count, BUYSELL_GOODS_COUNT));
	numberValid = forceNumberValid || inventory.numberValid;
	buyPercent = inventory.buyPercent;
	recyclePercent = inventory.recyclePercent;
	for (int i = 0; i < currentListCount; i++)
	{
		if (i >= static_cast<int>(inventory.items.size()))
		{
			continue;
		}
		goodsList[i].iniFile = inventory.items[i].iniFile;
		goodsList[i].number = numberValid ? inventory.items[i].number : std::max(1, inventory.items[i].number);
		if (!goodsList[i].iniFile.empty())
		{
			goodsList[i].goods = std::make_shared<Goods>();
			goodsList[i].goods->initFromIni(goodsList[i].iniFile);
			if (!goodsList[i].goods->loadSucceeded)
			{
				goodsList[i].clear();
			}
		}
		else
		{
			goodsList[i].number = 0;
		}
	}
}

BuySellInventoryData makeInventoryData(const GoodsInfo goodsList[BUYSELL_GOODS_COUNT],
	int currentListCount,
	bool numberValid,
	int buyPercent,
	int recyclePercent)
{
	BuySellInventoryData inventory;
	inventory.count = std::max(0, std::min(currentListCount, BUYSELL_GOODS_COUNT));
	inventory.numberValid = numberValid;
	inventory.buyPercent = buyPercent;
	inventory.recyclePercent = recyclePercent;
	inventory.items.resize(inventory.count);
	for (int i = 0; i < inventory.count; i++)
	{
		inventory.items[i].iniFile = goodsList[i].iniFile;
		inventory.items[i].number = goodsList[i].number;
	}
	return inventory;
}
}

BuySellMenu::BuySellMenu()
{
	name = "BuySellMenu";
	init();
	setPriority(epMax);
	visible = false;
	this_ = this;
}

BuySellMenu::~BuySellMenu()
{
	freeResource();
	this_ = nullptr;
}

BuySellMenu * BuySellMenu::getInstance()
{
	return this_;
}

void BuySellMenu::clearGoodsList()
{
	for (size_t i = 0; i < BUYSELL_GOODS_COUNT; i++)
	{
		goodsList[i].clear();
	}
	currentListCount = 0;
	currentListFile = "";
	currentShopOwner.reset();
	currentListOwnedByNPC = false;
	numberValid = false;
	canSellSelfGoods = true;
	buyPercent = 100;
	recyclePercent = 100;
}

bool BuySellMenu::saveNumberValidList()
{
	if (!numberValid || currentListCount <= 0)
	{
		return true;
	}

	BuySellInventoryData inventory = makeInventoryData(goodsList, currentListCount, true, buyPercent, recyclePercent);
	std::string inventoryText = BuySellInventory::serializeText(inventory);
	if (currentListOwnedByNPC)
	{
		auto owner = currentShopOwner.lock();
		if (owner != nullptr)
		{
			owner->buyIniString = BuySellInventory::encodeString(inventoryText);
			return true;
		}
		return false;
	}

	if (currentListFile.empty())
	{
		return false;
	}
	if (!File::writeFileChecked(
			SaveFileManager::CurrentPath() + currentListFile,
			inventoryText.data(),
			static_cast<int>(inventoryText.size())))
	{
		return false;
	}
	SaveFileManager::AppendFile(currentListFile);
	return true;
}

bool BuySellMenu::buyOneFromShopSlot(int shopIndex)
{
	if (gm == nullptr || gm->menu == nullptr || gm->menu->goodsMenu == nullptr
		|| shopIndex < 0 || shopIndex >= BUYSELL_GOODS_COUNT
		|| goodsList[shopIndex].iniFile.empty())
	{
		return false;
	}

	if (bsKind == bsBuy)
	{
		if (numberValid && goodsList[shopIndex].number <= 0)
		{
			gm->showMessage("该物品已售罄");
			return true;
		}
		if (!gm->goodsManager.buyItem(goodsList[shopIndex].iniFile, 1))
		{
			return true;
		}
		if (numberValid)
		{
			goodsList[shopIndex].number--;
			updateGoods();
		}
	}
	else if (bsKind == bsSell)
	{
		if (goodsList[shopIndex].number <= 0)
		{
			return false;
		}
		if (!gm->goodsManager.buyItem(goodsList[shopIndex].iniFile, 1))
		{
			return true;
		}
		goodsList[shopIndex].number--;
		if (goodsList[shopIndex].number <= 0)
		{
			goodsList[shopIndex].clear();
		}
		updateGoods();
	}
	else
	{
		return false;
	}

	gm->menu->goodsMenu->updateGoods();
	gm->menu->goodsMenu->updateMoney();
	return true;
}

bool BuySellMenu::sellOneFromPlayerSlot(int playerIndex)
{
	if (gm == nullptr || gm->player == nullptr || gm->menu == nullptr
		|| gm->menu->goodsMenu == nullptr
		|| !visible)
	{
		return false;
	}
	if (bsKind != bsSell && !canSellSelfGoods)
	{
		gm->showMessage("当前只能买物品");
		return true;
	}
	if (playerIndex < 0 || playerIndex >= gm->goodsManager.listLength()
		|| !gm->goodsManager.isStoreIndex(playerIndex))
	{
		return false;
	}
	if (!gm->goodsManager.goodsListExists(playerIndex))
	{
		return false;
	}

	GoodsInfo& playerGoods = gm->goodsManager.goodsList[playerIndex];
	const int sellPrice = playerGoods.goods->getSellPrice(recyclePercent);
	if (sellPrice <= 0 || !addGoodsItem(playerGoods.iniFile, 1))
	{
		return true;
	}

	gm->player->addMoney(sellPrice);
	playerGoods.number--;
	if (playerGoods.number <= 0)
	{
		playerGoods.clear();
	}
	gm->goodsManager.refreshEquipmentEffects();
	gm->menu->goodsMenu->updateGoods();
	gm->menu->goodsMenu->updateMoney();
	return true;
}

void BuySellMenu::buy(const std::string & list, std::shared_ptr<NPC> owner, bool canSell)
{
	bsKind = bsBuy;
	if (scrollbar) scrollbar->setPosition(scrollbar->min);
	for (size_t i = 0; i < item.size(); i++)
	{
		if (item[i])
		{
			item[i]->dragType = dtBuy;
			item[i]->dragIndex = i;
		}
	}
	addChild(gm->menu->goodsMenu);
	clearGoodsList();
	canSellSelfGoods = canSell;
	currentListFile = list;
	currentShopOwner = owner;

	bool loaded = false;
	if (owner != nullptr && !owner->buyIniString.empty())
	{
		std::string decoded;
		BuySellInventoryData inventory;
		if (BuySellInventory::decodeString(owner->buyIniString, decoded) &&
			BuySellInventory::parseText(decoded, inventory))
		{
			loadInventoryGoods(inventory, goodsList, currentListCount, numberValid, buyPercent, recyclePercent, true);
			currentListOwnedByNPC = true;
			loaded = true;
		}
	}
	if (!loaded)
	{
		std::string listName = resolveBuySellListPath(list);
		std::string inventoryText;
		BuySellInventoryData inventory;
		if (readTextFile(listName, inventoryText) && BuySellInventory::parseText(inventoryText, inventory))
		{
			loadInventoryGoods(inventory, goodsList, currentListCount, numberValid, buyPercent, recyclePercent, false);
		}
	}
	updateGoods();
	clearButtonChecked();
	setGoodsButtonChecked();
	visible = true;
	prepareControllerFocusForRun();
	GameLog::write("BuySellMenu::buy start");
	run();
	clearControllerFocus();
	GameLog::write("BuySellMenu::buy over");
	if (!saveNumberValidList())
	{
		gm->showMessage("商店库存保存失败");
	}
	clearButtonChecked();
	visible = false;
	gm->menu->toolTip->visible = false;
	gm->menu->upMenu->addChild(gm->menu->toolTip);
	gm->menu->upMenu->addChild(gm->menu->goodsMenu);
	GameLog::write("BuySellMenu::buy free");
}

void BuySellMenu::sell(const std::string & list)
{
	bsKind = bsSell;
	if (scrollbar) scrollbar->setPosition(scrollbar->min);
	for (size_t i = 0; i < item.size(); i++)
	{
		if (item[i])
		{
			item[i]->dragType = dtSell;
			item[i]->dragIndex = i;
		}
	}
	addChild(gm->menu->goodsMenu);
	clearGoodsList();
	currentListFile = list;
	if (list != "")
	{
		std::string listName = resolveBuySellListPath(list);
		std::string inventoryText;
		BuySellInventoryData inventory;
		if (readTextFile(listName, inventoryText) && BuySellInventory::parseText(inventoryText, inventory))
		{
			loadInventoryGoods(inventory, goodsList, currentListCount, numberValid, buyPercent, recyclePercent, true);
		}
	}
	updateGoods();
	clearButtonChecked();
	setGoodsButtonChecked();
	visible = true;
	prepareControllerFocusForRun();
	run();
	clearControllerFocus();
	if (!saveNumberValidList())
	{
		gm->showMessage("商店库存保存失败");
	}
	clearButtonChecked();
	visible = false;
	gm->menu->toolTip->visible = false;
	gm->menu->upMenu->addChild(gm->menu->toolTip);
	gm->menu->upMenu->addChild(gm->menu->goodsMenu);
}

bool BuySellMenu::addGoodsItem(const std::string & itemName, int num)
{
	for (size_t i = 0; i < BUYSELL_GOODS_COUNT; i++)
	{
		if (goodsList[i].iniFile == itemName)
		{
			goodsList[i].number = saturatingInventoryCountAdd(goodsList[i].number, num);
			if (goodsList[i].number <= 0)
			{
				goodsList[i].clear();
			}
			updateGoods();
			return true;
		}
	}
	if (num <= 0)
	{
		return false;
	}
	for (size_t i = 0; i < BUYSELL_GOODS_COUNT; i++)
	{
		if (goodsList[i].iniFile.empty())
		{
			goodsList[i].number = num;
			goodsList[i].iniFile = itemName;
			goodsList[i].goods = std::make_shared<Goods>();
			goodsList[i].goods->initFromIni(itemName);
			if (!goodsList[i].goods->loadSucceeded)
			{
				goodsList[i].clear();
				return false;
			}
			if (static_cast<int>(i) >= currentListCount)
			{
				currentListCount = static_cast<int>(i) + 1;
			}
			updateGoods();
			return true;
		}
	}
	return false;
}

void BuySellMenu::setGoodsButtonChecked()
{
	gm->menu->cancelControllerInteraction();
	// The ordinary inventory menus are refreshed lazily after a game load.
	// Buy/sell opens the goods panel directly instead of going through the
	// MenuController descriptor, so refresh it at this entry point as well.
	gm->menu->goodsMenu->updateGoods();
	gm->menu->goodsMenu->updateMoney();
	gm->menu->goodsMenu->visible = true;
	if (gm->menu->topMenu && gm->menu->topMenu->goodsBtn) gm->menu->topMenu->goodsBtn->checked = true;
	if (gm->menu->bottomMenu->goodsBtn) gm->menu->bottomMenu->goodsBtn->checked = true;
}

void BuySellMenu::clearButtonChecked()
{
	gm->menu->cancelControllerInteraction();
	gm->menu->practiceMenu->visible = false;
	gm->menu->equipMenu->visible = false;
	gm->menu->stateMenu->visible = false;
	gm->menu->magicMenu->visible = false;
	gm->menu->memoMenu->visible = false;
	gm->menu->goodsMenu->visible = false;
	if (gm->menu->topMenu)
	{
		if (gm->menu->topMenu->magicBtn) gm->menu->topMenu->magicBtn->checked = false;
		if (gm->menu->topMenu->goodsBtn) gm->menu->topMenu->goodsBtn->checked = false;
		if (gm->menu->topMenu->notesBtn) gm->menu->topMenu->notesBtn->checked = false;
		if (gm->menu->topMenu->stateBtn) gm->menu->topMenu->stateBtn->checked = false;
		if (gm->menu->topMenu->xiulianBtn) gm->menu->topMenu->xiulianBtn->checked = false;
		if (gm->menu->topMenu->equipBtn) gm->menu->topMenu->equipBtn->checked = false;
	}
	if (gm->menu->bottomMenu->magicBtn) gm->menu->bottomMenu->magicBtn->checked = false;
	if (gm->menu->bottomMenu->goodsBtn) gm->menu->bottomMenu->goodsBtn->checked = false;
	if (gm->menu->bottomMenu->notesBtn) gm->menu->bottomMenu->notesBtn->checked = false;
	if (gm->menu->bottomMenu->stateBtn) gm->menu->bottomMenu->stateBtn->checked = false;
	if (gm->menu->bottomMenu->xiulianBtn) gm->menu->bottomMenu->xiulianBtn->checked = false;
	if (gm->menu->bottomMenu->equipBtn) gm->menu->bottomMenu->equipBtn->checked = false;
}

void BuySellMenu::updateGoods()
{
	for (size_t i = 0; i < item.size(); i++)
	{
		if (item[i] == nullptr || scrollbar == nullptr) continue;

		int listIndex = i + scrollbar->position * scrollbar->lineSize;
		item[i]->dragIndex = listIndex;

		item[i]->impImage = nullptr;

		item[i]->setStr("");
		if (listIndex >= 0 && listIndex < BUYSELL_GOODS_COUNT &&
			goodsList[listIndex].iniFile != "" && goodsList[listIndex].goods != nullptr)
		{
			item[i]->impImage = MenuResource::createGoodsMenuImage(
				goodsList[listIndex].goods);
			if (bsKind == bsSell || numberValid)
			{
				item[i]->setStr(convert::formatString("%d", goodsList[listIndex].number));
			}
		}
	}
}

int BuySellMenu::resolveShopSlotIndex(int visibleIndex) const
{
	if (scrollbar == nullptr || visibleIndex < 0
		|| visibleIndex >= static_cast<int>(item.size())
		|| item[visibleIndex] == nullptr)
	{
		return -1;
	}
	const int shopIndex = visibleIndex
		+ scrollbar->position * scrollbar->lineSize;
	return shopIndex >= 0 && shopIndex < BUYSELL_GOODS_COUNT
		? shopIndex
		: -1;
}

void BuySellMenu::configureControllerFocus()
{
	controllerPaneRouter.clear();

	SlotGridBinding shopBinding;
	shopBinding.focusIdPrefix = "buy-sell-shop-item-";
	shopBinding.items = item;
	shopBinding.scrollbar = scrollbar;
	shopBinding.resolveLogicalIndex = [this](int visibleIndex)
	{
		return resolveShopSlotIndex(visibleIndex);
	};
	shopBinding.primary = [this](int shopIndex, int)
	{
		buyOneFromShopSlot(shopIndex);
	};
	shopBinding.details = [this](int shopIndex, int visibleIndex)
	{
		showShopControllerDetails(shopIndex, visibleIndex);
	};
	shopBinding.hideDetails = [this]() { hideControllerDetails(); };
	shopBinding.refreshAfterScroll = [this]()
	{
		position = scrollbar != nullptr ? scrollbar->position : -1;
		updateGoods();
	};
	shopSlotGridController.bind(std::move(shopBinding));

	SlotGridBinding playerBinding;
	playerBinding.focusIdPrefix = "buy-sell-player-item-";
	SlotGridView playerView;
	if (gm != nullptr && gm->menu != nullptr && gm->menu->goodsMenu != nullptr)
	{
		playerView = gm->menu->goodsMenu->controllerBagView();
	}
	applySlotGridView(playerBinding, std::move(playerView));
	controllerPlayerScrollPosition = playerBinding.scrollbar != nullptr
		? playerBinding.scrollbar->position
		: -1;
	playerBinding.primary = [this](int playerIndex, int)
	{
		sellOneFromPlayerSlot(playerIndex);
	};
	playerBinding.details = [this](int playerIndex, int visibleIndex)
	{
		showPlayerControllerDetails(playerIndex, visibleIndex);
	};
	playerBinding.hideDetails = [this]() { hideControllerDetails(); };
	std::weak_ptr<Scrollbar> playerScrollbar = playerBinding.scrollbar;
	std::function<void()> refreshPlayerBag =
		std::move(playerBinding.refreshAfterScroll);
	playerBinding.refreshAfterScroll =
		[this,
		playerScrollbar,
		refreshPlayerBag = std::move(refreshPlayerBag)]()
	{
		auto currentScrollbar = playerScrollbar.lock();
		controllerPlayerScrollPosition = currentScrollbar != nullptr
			? currentScrollbar->position
			: -1;
		if (refreshPlayerBag)
		{
			refreshPlayerBag();
		}
	};
	playerSlotGridController.bind(std::move(playerBinding));

	controllerPaneRouter.registerTargetPane(
		ShopControllerPaneId,
		shopSlotGridController,
		[this]()
		{
			return scrollbar != nullptr && !item.empty();
		},
		[this]()
		{
			hideControllerDetails();
		});
	controllerPaneRouter.registerTargetPane(
		PlayerControllerPaneId,
		playerSlotGridController,
		[this]()
		{
			return gm != nullptr && gm->menu != nullptr
				&& gm->menu->goodsMenu != nullptr
				&& gm->menu->goodsMenu->scrollbar != nullptr
				&& !gm->menu->goodsMenu->item.empty();
		},
		[this]()
		{
			hideControllerDetails();
		});
	controllerPaneRouter.setDefaultPane(ShopControllerPaneId);
}

void BuySellMenu::prepareControllerFocusForRun()
{
	if (gm != nullptr && gm->menu != nullptr && gm->menu->goodsMenu != nullptr)
	{
		gm->menu->goodsMenu->deactivateControllerFocus();
	}
	position = scrollbar != nullptr ? scrollbar->position : -1;
	configureControllerFocus();
	controllerPaneRouter.activateDefaultPane();
}

void BuySellMenu::clearControllerFocus()
{
	controllerPaneRouter.suspend();
	hideControllerDetails();
}

void BuySellMenu::showControllerDetails(
	const std::shared_ptr<Goods>& goods, const PElement& anchor)
{
	if (gm == nullptr || gm->menu == nullptr || gm->menu->toolTip == nullptr
		|| gm->menu->upMenu == nullptr || goods == nullptr || anchor == nullptr)
	{
		hideControllerDetails();
		return;
	}
	gm->menu->showGoodsToolTip(getMySharedPtr(), goods, anchor);
}

void BuySellMenu::showShopControllerDetails(
	int shopIndex, int visibleIndex)
{
	const bool validGoods = shopIndex >= 0
		&& shopIndex < BUYSELL_GOODS_COUNT
		&& goodsList[shopIndex].goods != nullptr
		&& !goodsList[shopIndex].iniFile.empty()
		&& (bsKind != bsSell || goodsList[shopIndex].number > 0);
	if (gm == nullptr || gm->menu == nullptr || gm->menu->toolTip == nullptr
		|| gm->menu->upMenu == nullptr || !validGoods
		|| visibleIndex < 0 || visibleIndex >= static_cast<int>(item.size())
		|| item[visibleIndex] == nullptr)
	{
		hideControllerDetails();
		return;
	}
	showControllerDetails(goodsList[shopIndex].goods, item[visibleIndex]);
}

void BuySellMenu::showPlayerControllerDetails(
	int playerIndex, int visibleIndex)
{
	if (gm == nullptr || gm->menu == nullptr || gm->menu->goodsMenu == nullptr
		|| gm->menu->toolTip == nullptr || gm->menu->upMenu == nullptr
		|| !gm->goodsManager.goodsListExists(playerIndex)
		|| visibleIndex < 0
		|| visibleIndex >= static_cast<int>(gm->menu->goodsMenu->item.size())
		|| gm->menu->goodsMenu->item[visibleIndex] == nullptr)
	{
		hideControllerDetails();
		return;
	}
	showControllerDetails(
		gm->goodsManager.goodsList[playerIndex].goods,
		gm->menu->goodsMenu->item[visibleIndex]);
}

void BuySellMenu::hideControllerDetails()
{
	if (gm != nullptr && gm->menu != nullptr)
	{
		gm->menu->hideToolTip();
	}
}

void BuySellMenu::onEvent()
{
	if (!visible)
	{
		return;
	}
	if (currentDragItem != nullptr)
	{
		clearControllerFocus();
	}
	if (scrollbar != nullptr && position != scrollbar->position)
	{
		clearControllerFocus();
		position = scrollbar->position;
		updateGoods();
	}
	if (gm != nullptr && gm->menu != nullptr && gm->menu->goodsMenu != nullptr
		&& gm->menu->goodsMenu->scrollbar != nullptr
		&& controllerPlayerScrollPosition
			!= gm->menu->goodsMenu->scrollbar->position)
	{
		clearControllerFocus();
		controllerPlayerScrollPosition =
			gm->menu->goodsMenu->scrollbar->position;
	}
	if (closeBtn && closeBtn->getResult(erClick))
	{
		clearControllerFocus();
		logicRunning = false;
	}
	for (size_t i = 0; i < item.size(); i++)
	{
		if (item[i] == nullptr) continue;

		int listIndex = scrollbar ? scrollbar->position * scrollbar->lineSize + static_cast<int>(i) : -1;
		unsigned int ret = item[i]->getResult();
		if (ret & (erClick | erMouseRDown | erDropped))
		{
			clearControllerFocus();
		}
		if (ret & erShowHint)
		{
			if (scrollbar)
			{
				if (bsKind == bsBuy)
				{
					if (listIndex >= 0 && listIndex < BUYSELL_GOODS_COUNT && goodsList[listIndex].iniFile != "" && goodsList[listIndex].goods != nullptr)
					{
						gm->menu->showGoodsToolTip(
							getMySharedPtr(),
							goodsList[listIndex].goods,
							item[i]);
					}
					else
					{
						gm->menu->toolTip->visible = false;
					}
				}
				else
				{
					if (listIndex >= 0 && listIndex < BUYSELL_GOODS_COUNT && goodsList[listIndex].iniFile != "" && goodsList[listIndex].goods != nullptr && goodsList[listIndex].number > 0)
					{
						gm->menu->showGoodsToolTip(
							getMySharedPtr(),
							goodsList[listIndex].goods,
							item[i]);
					}
					else
					{
						gm->menu->toolTip->visible = false;
					}
				}
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
			gm->menu->toolTip->visible = false;
			item[i]->resetHint();
			buyOneFromShopSlot(listIndex);
		}
		if (ret & erDropped)
		{
			if ((bsKind == bsSell || canSellSelfGoods) && scrollbar)
			{
				int sellPrice = gm->goodsManager.goodsListExists(item[i]->dropIndex)
					? gm->goodsManager.goodsList[item[i]->dropIndex].goods->getSellPrice(recyclePercent)
					: 0;
				if (listIndex >= 0 && listIndex < BUYSELL_GOODS_COUNT && item[i]->dropType == dtGoods && sellPrice > 0)
				{
					if (gm->goodsManager.goodsList[item[i]->dropIndex].iniFile == goodsList[listIndex].iniFile)
					{
						goodsList[listIndex].number = saturatingInventoryCountAdd(
							goodsList[listIndex].number,
							gm->goodsManager.goodsList[item[i]->dropIndex].number);
						if (listIndex >= currentListCount)
						{
							currentListCount = listIndex + 1;
						}
						addSaleProceeds(
							gm->goodsManager.goodsList[item[i]->dropIndex].number, sellPrice);
						gm->goodsManager.goodsList[item[i]->dropIndex].clear();
						gm->goodsManager.refreshEquipmentEffects();
						gm->menu->goodsMenu->updateGoods();
						gm->menu->goodsMenu->updateMoney();
						updateGoods();

					}
					else if (goodsList[listIndex].iniFile.empty())
					{
						goodsList[listIndex].iniFile = gm->goodsManager.goodsList[item[i]->dropIndex].iniFile;
						goodsList[listIndex].goods = std::make_shared<Goods>(
							*gm->goodsManager.goodsList[item[i]->dropIndex].goods);
						goodsList[listIndex].number = gm->goodsManager.goodsList[item[i]->dropIndex].number;
						if (listIndex >= currentListCount)
						{
							currentListCount = listIndex + 1;
						}
						addSaleProceeds(
							gm->goodsManager.goodsList[item[i]->dropIndex].number, sellPrice);
						gm->goodsManager.goodsList[item[i]->dropIndex].clear();
						gm->goodsManager.refreshEquipmentEffects();
						gm->menu->goodsMenu->updateGoods();
						gm->menu->goodsMenu->updateMoney();
						updateGoods();
					}
				}
			}
		}
	}

}

bool BuySellMenu::onHandleEvent(AEvent & e)
{
	if (!visible)
	{
		return false;
	}
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

bool BuySellMenu::onHandleUIAction(UIAction action)
{
	if (!visible)
	{
		return false;
	}
	if (action == UIAction::Cancel)
	{
		clearControllerFocus();
		logicRunning = false;
		return true;
	}
	if (action == UIAction::Secondary)
	{
		hideControllerDetails();
		return true;
	}
	return controllerPaneRouter.handleAction(action);
}

void BuySellMenu::onDrawEnd()
{
	if (!visible || engine == nullptr)
	{
		return;
	}
	if (!ControllerPromptPresenter::canPresentForOwner(
		this, ControllerPromptOwnerPolicy::CurrentRunOwner))
	{
		return;
	}
	static const std::vector<ControllerPromptItem> PromptItems =
	{
		{ GameInput::InputAction::NavigateUp, "选择" },
		{ GameInput::InputAction::Confirm, "买入/卖出一个" },
		{ GameInput::InputAction::ShowDetails, "详情" },
		{ GameInput::InputAction::PreviousPanel, "切换区域" },
		{ GameInput::InputAction::NextPanel, "切换区域" },
		{ GameInput::InputAction::Cancel, "离开" },
	};
	int windowWidth = 0;
	int windowHeight = 0;
	engine->getWindowSize(windowWidth, windowHeight);
	ControllerPromptDrawOptions options;
	options.width = std::min(windowWidth - 16, 900);
	options.x = std::max(8, (windowWidth - options.width) / 2);
	options.height = std::min(48, windowHeight);
	options.y = std::max(0, windowHeight - options.height - 8);
	options.fontSize = windowWidth < 720 ? 12 : 14;
	ControllerPromptPresenter::draw(
		engine, engine->inputActions(), PromptItems, options);
}

void BuySellMenu::onWindowResize(int width, int height)
{
	GoodsInfo savedGoodsList[BUYSELL_GOODS_COUNT];
	for (int i = 0; i < BUYSELL_GOODS_COUNT; i++)
	{
		savedGoodsList[i] = goodsList[i];
	}
	int savedBsKind = bsKind;
	int savedPosition = scrollbar != nullptr ? scrollbar->position : position;
	int savedCurrentListCount = currentListCount;
	std::string savedCurrentListFile = currentListFile;
	std::weak_ptr<NPC> savedShopOwner = currentShopOwner;
	bool savedListOwnedByNPC = currentListOwnedByNPC;
	bool savedNumberValid = numberValid;
	bool savedCanSellSelfGoods = canSellSelfGoods;
	int savedBuyPercent = buyPercent;
	int savedRecyclePercent = recyclePercent;
	int savedControllerPaneId = controllerPaneRouter.selectedPaneId();
	bool savedVisible = visible;
	bool savedLogicRunning = logicRunning;
	bool savedEventOccupied = eventOccupied;

	init();

	bsKind = savedBsKind;
	for (int i = 0; i < BUYSELL_GOODS_COUNT; i++)
	{
		goodsList[i] = savedGoodsList[i];
	}
	currentListCount = savedCurrentListCount;
	currentListFile = std::move(savedCurrentListFile);
	currentShopOwner = std::move(savedShopOwner);
	currentListOwnedByNPC = savedListOwnedByNPC;
	numberValid = savedNumberValid;
	canSellSelfGoods = savedCanSellSelfGoods;
	buyPercent = savedBuyPercent;
	recyclePercent = savedRecyclePercent;
	if (scrollbar != nullptr)
	{
		savedPosition = std::max(scrollbar->min, std::min(savedPosition, scrollbar->max));
		scrollbar->setPosition(savedPosition);
		position = scrollbar->position;
	}
	for (size_t i = 0; i < item.size(); i++)
	{
		if (item[i] == nullptr)
		{
			continue;
		}
		item[i]->dragType = bsKind == bsBuy ? dtBuy : dtSell;
		item[i]->dragIndex = static_cast<int>(i);
	}
	visible = savedVisible;
	logicRunning = savedLogicRunning;
	eventOccupied = savedEventOccupied;
	updateGoods();
	if (visible)
	{
		if (gm != nullptr && gm->menu != nullptr
			&& gm->menu->goodsMenu != nullptr)
		{
			addChild(gm->menu->goodsMenu);
		}
		setGoodsButtonChecked();
		if (savedControllerPaneId < 0
			|| !controllerPaneRouter.activatePane(savedControllerPaneId))
		{
			controllerPaneRouter.activateDefaultPane();
		}
	}
}

void BuySellMenu::init()
{
	freeResource();
	loadMenuDefinition("ini\\ui\\buysell\\buysell.menu.ini");

	title = getComponentByName<ImageContainer>("title");
	image = getComponentByName<ImageContainer>("image");
	closeBtn = getComponentByName<Button>("closeBtn");
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
		itemComponent->dragType = dtSell;
		itemComponent->canShowHint = true;
		item.push_back(itemComponent);
	}

	if (scrollbar != nullptr)
	{
		scrollbar->pageSize = static_cast<int>(item.size());
		int lineSize = std::max(1, scrollbar->lineSize);
		int visibleCount = std::max(1, scrollbar->pageSize);
		int scrollableCount = std::max(0, BUYSELL_GOODS_COUNT - visibleCount);
		scrollbar->min = 0;
		scrollbar->max = (scrollableCount + lineSize - 1) / lineSize;
		scrollbar->position = scrollbar->min;
	}

	setChildRectReferToParent();
	configureControllerFocus();
}

void BuySellMenu::freeResource()
{
	controllerPaneRouter.clear();
	shopSlotGridController.clear();
	playerSlotGridController.clear();
	hideControllerDetails();
	controllerPlayerScrollPosition = -1;
	title = nullptr;
	image = nullptr;
	closeBtn = nullptr;
	scrollbar = nullptr;
	item.clear();
	clearGoodsList();
	ConfigDrivenPanel::freeResource();
}
