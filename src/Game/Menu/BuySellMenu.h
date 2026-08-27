#pragma once
#include "../../Component/Component.h"
#include "../GameTypes.h"
#include "../Data/GoodsManager.h"
#include "SlotGridController.h"
#include <memory>
#include <vector>

class NPC;

enum BuySellType
{
	bsBuy = 1,
	bsSell = 2,
};

class BuySellMenu :
	public ConfigDrivenPanel
{
	friend class UIFocusTestAccess;
public:
	BuySellMenu();
	virtual ~BuySellMenu();
	int bsKind = bsBuy;
	static BuySellMenu * getInstance();

	std::shared_ptr<ImageContainer> title = nullptr;
	std::shared_ptr<ImageContainer> image = nullptr;
	std::shared_ptr<Button> closeBtn = nullptr;

	std::shared_ptr<Scrollbar> scrollbar = nullptr;

	std::vector<std::shared_ptr<Item>> item;

	GoodsInfo goodsList[BUYSELL_GOODS_COUNT];
	bool numberValid = false;
	bool canSellSelfGoods = true;
	int buyPercent = 100;
	int recyclePercent = 100;

	void clearGoodsList();
	void buy(const std::string & list, std::shared_ptr<NPC> owner = nullptr, bool canSellSelfGoods = true);
	void sell(const std::string & list = "");
	bool addGoodsItem(const std::string & itemName, int num);
	void setGoodsButtonChecked();
	void clearButtonChecked();

	void updateGoods();
	bool sellOneFromPlayerSlot(int playerIndex);
	void clearControllerFocus();
private:
	static constexpr int ShopControllerPaneId = 0;
	static constexpr int PlayerControllerPaneId = 1;

	static BuySellMenu * this_;

	int position = -1;
	int currentListCount = 0;
	std::string currentListFile = "";
	std::weak_ptr<NPC> currentShopOwner;
	bool currentListOwnedByNPC = false;
	int controllerPlayerScrollPosition = -1;
	SlotGridController shopSlotGridController;
	SlotGridController playerSlotGridController;
	ControllerPaneRouter controllerPaneRouter;
	bool saveNumberValidList();
	bool buyOneFromShopSlot(int shopIndex);
	void configureControllerFocus();
	void prepareControllerFocusForRun();
	void showControllerDetails(
		const std::shared_ptr<Goods>& goods, const PElement& anchor);
	void showShopControllerDetails(int shopIndex, int visibleIndex);
	void showPlayerControllerDetails(int playerIndex, int visibleIndex);
	void hideControllerDetails();
	int resolveShopSlotIndex(int visibleIndex) const;
	virtual void onEvent() override;
	virtual bool onHandleEvent(AEvent & e) override;
	virtual bool onHandleUIAction(UIAction action) override;
	virtual void onDrawEnd() override;
	virtual void onWindowResize(int width, int height) override;
	virtual void init() override;
	void freeResource();
};
