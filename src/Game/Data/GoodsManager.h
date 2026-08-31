#pragma once
#include "Goods.h"
#include <memory>
#include <vector>

class NPC;

struct GoodsInfo
{
	void clear()
	{
		iniFile = "";
		number = 0;
		goods = nullptr;
		remainColdMilliseconds = 0;
	}

	void updateColdTime(UTime frameTime)
	{
		if (frameTime == 0 || remainColdMilliseconds == 0)
		{
			return;
		}
		if (remainColdMilliseconds > frameTime)
		{
			remainColdMilliseconds -= frameTime;
		}
		else
		{
			remainColdMilliseconds = 0;
		}
	}

	std::string iniFile = "";
	int number = 0;
	std::shared_ptr<Goods> goods = nullptr;
	UTime remainColdMilliseconds = 0;
};

class GoodsManager
{
public:
	GoodsManager();
	virtual ~GoodsManager();

	void freeResource();

	bool load(int index, std::string* failureReason = nullptr);
	bool save(int index);
	GoodsInfo * findGoods(const std::string & itemName);
	void clearItem(bool adjustCurrentValues = true);
	int getItemNum(const std::string & itemName);
	int getItemNumByDisplayName(const std::string& name);
	void setItemNum(const std::string & itemName, int num);
	void exchange(int index1, int index2);
	void sellItem(const std::string & itemName);
	bool addItem(const std::string & itemName, int num);
	void clearItemByFileName(const std::string& itemName);
	void deleteItem(const std::string & itemName);
	void deleteItemByDisplayName(const std::string& name, int count = 0);
	bool buyItem(const std::string & itemName, int num);
	bool useItem(int itemIndex);
	void updateColdTimes(UTime frameTime);
	void refreshEquipmentEffects(bool adjustCurrentValues = true);
	bool canUseGoods(int itemIndex, std::shared_ptr<NPC> user, std::string* message = nullptr) const;
	bool canEquipGoodsAt(int itemIndex, int partIndex, std::shared_ptr<NPC> user, std::string* message = nullptr) const;
	bool canEquipGoodsAt(
		const std::shared_ptr<Goods>& goods,
		int partIndex,
		std::shared_ptr<NPC> user,
		std::string* message = nullptr) const;
	bool unequipToFirstStoreSlot(int equipmentIndex, std::string* message = nullptr);

	void updateMenu(int idx);
	void updateMenu();
	void configureLayout();
	int listLength() const;
	int storeBegin() const;
	int storeEnd() const;
	int bottomCount() const;
	int bottomBegin() const;
	int bottomEnd() const;
	int equipBegin() const;
	int equipEnd() const;
	int bottomIndex(int index) const;
	int equipIndex(int index) const;
	int bottomSlot(int index) const;
	int equipSlot(int index) const;
	bool isStoreIndex(int index) const;
	bool isBottomIndex(int index) const;
	bool isEquipIndex(int index) const;
	std::vector<GoodsInfo> goodsList;
	bool goodsListExists(int index);
};
