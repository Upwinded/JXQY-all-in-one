#include "ControllerTransferPolicies.h"

#include "../GameManager/GameManager.h"

#include <string>

namespace
{
bool isGoodsAddressValid(
	GameManager* gameManager, const ControllerSlotAddress& address)
{
	if (gameManager == nullptr || address.kind != ControllerSlotKind::Goods
		|| address.logicalIndex < 0
		|| address.logicalIndex >= gameManager->goodsManager.listLength())
	{
		return false;
	}
	switch (address.domain)
	{
	case ControllerSlotDomain::GoodsBag:
		return gameManager->goodsManager.isStoreIndex(address.logicalIndex);
	case ControllerSlotDomain::GoodsQuick:
		return gameManager->goodsManager.isBottomIndex(address.logicalIndex);
	case ControllerSlotDomain::PlayerEquipment:
		return gameManager->goodsManager.isEquipIndex(address.logicalIndex);
	case ControllerSlotDomain::MagicList:
	case ControllerSlotDomain::MagicQuick:
	case ControllerSlotDomain::Practice:
	case ControllerSlotDomain::PartnerBag:
	case ControllerSlotDomain::PartnerEquipment:
	default:
		return false;
	}
}

bool isMagicAddressValid(
	GameManager* gameManager, const ControllerSlotAddress& address)
{
	if (gameManager == nullptr || address.kind != ControllerSlotKind::Magic
		|| address.logicalIndex < 0
		|| address.logicalIndex >= gameManager->magicManager.listLength())
	{
		return false;
	}
	switch (address.domain)
	{
	case ControllerSlotDomain::MagicList:
		return gameManager->magicManager.isStoreIndex(address.logicalIndex);
	case ControllerSlotDomain::MagicQuick:
		return gameManager->magicManager.isBottomIndex(address.logicalIndex);
	case ControllerSlotDomain::Practice:
		return gameManager->magicManager.isPracticeIndex(address.logicalIndex);
	case ControllerSlotDomain::GoodsBag:
	case ControllerSlotDomain::GoodsQuick:
	case ControllerSlotDomain::PlayerEquipment:
	case ControllerSlotDomain::PartnerBag:
	case ControllerSlotDomain::PartnerEquipment:
	default:
		return false;
	}
}

ControllerSlotIdentity identifyGoods(
	GameManager* gameManager, const ControllerSlotAddress& address)
{
	if (!isGoodsAddressValid(gameManager, address)
		|| !gameManager->goodsManager.goodsListExists(address.logicalIndex))
	{
		return ControllerSlotIdentity();
	}
	const GoodsInfo& goodsInfo =
		gameManager->goodsManager.goodsList[address.logicalIndex];
	return
	{
		std::static_pointer_cast<const void>(goodsInfo.goods),
		goodsInfo.iniFile + "#" + std::to_string(goodsInfo.number)
	};
}

ControllerSlotIdentity identifyMagic(
	GameManager* gameManager, const ControllerSlotAddress& address)
{
	if (!isMagicAddressValid(gameManager, address)
		|| !gameManager->magicManager.magicListExists(address.logicalIndex))
	{
		return ControllerSlotIdentity();
	}
	const MagicInfo& magicInfo =
		gameManager->magicManager.magicList[address.logicalIndex];
	return
	{
		std::static_pointer_cast<const void>(magicInfo.magic),
		magicInfo.iniFile
	};
}

bool isPartnerGoodsAddressValid(
	GameManager* gameManager,
	const std::function<std::shared_ptr<NPC>()>& currentPartner,
	const ControllerSlotAddress& address)
{
	if (gameManager == nullptr
		|| address.kind != ControllerSlotKind::PartnerGoods
		|| address.logicalIndex < 0 || !currentPartner)
	{
		return false;
	}
	const std::shared_ptr<NPC> partner = currentPartner();
	if (partner == nullptr || partner->canEquip <= 0
		|| !gameManager->partnerManager.isActivePartner(partner)
		|| address.context
			!= std::static_pointer_cast<const void>(partner))
	{
		return false;
	}
	if (address.domain == ControllerSlotDomain::PartnerBag)
	{
		return address.logicalIndex < gameManager->goodsManager.listLength()
			&& gameManager->goodsManager.isStoreIndex(address.logicalIndex);
	}
	return address.domain == ControllerSlotDomain::PartnerEquipment
		&& address.logicalIndex < GOODS_BODY_COUNT;
}

ControllerSlotIdentity identifyPartnerGoods(
	GameManager* gameManager,
	const std::function<std::shared_ptr<NPC>()>& currentPartner,
	const ControllerSlotAddress& address)
{
	if (!isPartnerGoodsAddressValid(
		gameManager, currentPartner, address))
	{
		return ControllerSlotIdentity();
	}
	if (address.domain == ControllerSlotDomain::PartnerBag)
	{
		if (!gameManager->goodsManager.goodsListExists(address.logicalIndex))
		{
			return ControllerSlotIdentity();
		}
		const GoodsInfo& goodsInfo =
			gameManager->goodsManager.goodsList[address.logicalIndex];
		return
		{
			std::static_pointer_cast<const void>(goodsInfo.goods),
			goodsInfo.iniFile + "#" + std::to_string(goodsInfo.number)
		};
	}

	const std::shared_ptr<NPC> partner = currentPartner();
	const std::string fileName = partner->getEquipmentFileByPartIndex(
		address.logicalIndex);
	if (fileName.empty())
	{
		return ControllerSlotIdentity();
	}
	return
	{
		std::static_pointer_cast<const void>(partner),
		fileName + "#" + std::to_string(address.logicalIndex)
	};
}

bool canEnterEquipmentSlot(
	GameManager* gameManager,
	int goodsIndex,
	int equipmentIndex,
	std::string& message)
{
	if (!gameManager->goodsManager.goodsListExists(goodsIndex))
	{
		return true;
	}
	if (gameManager->player == nullptr)
	{
		message = "角色数据不可用";
		return false;
	}
	return gameManager->goodsManager.canEquipGoodsAt(
		goodsIndex,
		gameManager->goodsManager.equipSlot(equipmentIndex),
		gameManager->player,
		&message);
}
}

ControllerTransferCoordinator::Policy createGoodsControllerTransferPolicy(
	GameManager* gameManager)
{
	ControllerTransferCoordinator::Policy policy;
	policy.domainOrder =
	{
		ControllerSlotDomain::GoodsBag,
		ControllerSlotDomain::GoodsQuick,
		ControllerSlotDomain::PlayerEquipment
	};
	policy.identifySource = [gameManager](const ControllerSlotAddress& address)
	{
		return identifyGoods(gameManager, address);
	};
	policy.submit = [gameManager](
		const ControllerSlotAddress& source,
		const ControllerSlotAddress& target,
		std::string& message)
	{
		if (!isGoodsAddressValid(gameManager, source)
			|| !gameManager->goodsManager.goodsListExists(source.logicalIndex))
		{
			message = "拿起的物品已不存在";
			return ControllerTransferSubmitResult::SourceLost;
		}
		if (!isGoodsAddressValid(gameManager, target))
		{
			message = "物品位置无效";
			return ControllerTransferSubmitResult::Rejected;
		}
		if (gameManager->goodsManager.isEquipIndex(target.logicalIndex)
			&& !canEnterEquipmentSlot(
				gameManager, source.logicalIndex, target.logicalIndex, message))
		{
			return ControllerTransferSubmitResult::Rejected;
		}
		if (gameManager->goodsManager.isEquipIndex(source.logicalIndex)
			&& !canEnterEquipmentSlot(
				gameManager, target.logicalIndex, source.logicalIndex, message))
		{
			return ControllerTransferSubmitResult::Rejected;
		}
		if (source.logicalIndex != target.logicalIndex)
		{
			gameManager->goodsManager.exchange(
				source.logicalIndex, target.logicalIndex);
			gameManager->goodsManager.updateMenu();
		}
		return ControllerTransferSubmitResult::Completed;
	};
	return policy;
}

ControllerTransferCoordinator::Policy createMagicControllerTransferPolicy(
	GameManager* gameManager)
{
	ControllerTransferCoordinator::Policy policy;
	policy.domainOrder =
	{
		ControllerSlotDomain::MagicList,
		ControllerSlotDomain::MagicQuick,
		ControllerSlotDomain::Practice
	};
	policy.identifySource = [gameManager](const ControllerSlotAddress& address)
	{
		return identifyMagic(gameManager, address);
	};
	policy.submit = [gameManager](
		const ControllerSlotAddress& source,
		const ControllerSlotAddress& target,
		std::string& message)
	{
		if (!isMagicAddressValid(gameManager, source)
			|| !gameManager->magicManager.magicListExists(source.logicalIndex))
		{
			message = "拿起的武功已不存在";
			return ControllerTransferSubmitResult::SourceLost;
		}
		if (!isMagicAddressValid(gameManager, target))
		{
			message = "武功位置无效";
			return ControllerTransferSubmitResult::Rejected;
		}
		if (source.logicalIndex != target.logicalIndex)
		{
			gameManager->magicManager.exchange(
				source.logicalIndex, target.logicalIndex);
			gameManager->magicManager.updateMenu();
		}
		return ControllerTransferSubmitResult::Completed;
	};
	return policy;
}

ControllerTransferCoordinator::Policy createPartnerGoodsControllerTransferPolicy(
	GameManager* gameManager,
	std::function<std::shared_ptr<NPC>()> currentPartner,
	std::function<void()> refreshMenus)
{
	ControllerTransferCoordinator::Policy policy;
	policy.domainOrder =
	{
		ControllerSlotDomain::PartnerBag,
		ControllerSlotDomain::PartnerEquipment
	};
	policy.identifySource = [gameManager, currentPartner](
		const ControllerSlotAddress& address)
	{
		return identifyPartnerGoods(gameManager, currentPartner, address);
	};
	policy.submit = [gameManager, currentPartner, refreshMenus](
		const ControllerSlotAddress& source,
		const ControllerSlotAddress& target,
		std::string& message)
	{
		if (!isPartnerGoodsAddressValid(
			gameManager, currentPartner, source)
			|| !identifyPartnerGoods(
				gameManager, currentPartner, source).valid())
		{
			message = "拿起的同伴物品已不存在";
			return ControllerTransferSubmitResult::SourceLost;
		}
		if (!isPartnerGoodsAddressValid(
			gameManager, currentPartner, target))
		{
			message = "同伴物品位置无效";
			return ControllerTransferSubmitResult::Rejected;
		}

		bool completed = false;
		if (source.domain == ControllerSlotDomain::PartnerBag
			&& target.domain == ControllerSlotDomain::PartnerBag)
		{
			if (source.logicalIndex != target.logicalIndex)
			{
				gameManager->goodsManager.exchange(
					source.logicalIndex, target.logicalIndex);
			}
			completed = true;
		}
		else if (source.domain == ControllerSlotDomain::PartnerEquipment
			&& target.domain == ControllerSlotDomain::PartnerEquipment)
		{
			completed = gameManager->partnerManager.exchangePartnerEquipmentSlots(
				currentPartner(),
				source.logicalIndex,
				target.logicalIndex,
				&message);
		}
		else
		{
			const bool sourceIsPlayerBag =
				source.domain == ControllerSlotDomain::PartnerBag;
			const int playerBagIndex = sourceIsPlayerBag
				? source.logicalIndex
				: target.logicalIndex;
			const int equipmentSlotIndex = sourceIsPlayerBag
				? target.logicalIndex
				: source.logicalIndex;
			completed = gameManager->partnerManager
				.exchangePlayerBagWithPartnerEquipment(
					currentPartner(),
					playerBagIndex,
					equipmentSlotIndex,
					sourceIsPlayerBag,
					&message);
		}
		if (!completed)
		{
			return ControllerTransferSubmitResult::Rejected;
		}
		if (refreshMenus)
		{
			refreshMenus();
		}
		return ControllerTransferSubmitResult::Completed;
	};
	return policy;
}
