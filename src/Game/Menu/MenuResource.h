#pragma once

#include "../Data/Global.h"
#include "../GameManager/GameManager.h"
#include "../../File/File.h"

#include <algorithm>
#include <string>
#include <memory>

namespace MenuResource
{
inline void updateMagicCooldown(const std::shared_ptr<Item>& item, MagicManager& manager, int index)
{
	if (item == nullptr)
	{
		return;
	}
	const MagicInfo* info = manager.magicListExists(index) ? &manager.magicList[index] : nullptr;
	const UTime remaining = info != nullptr ? info->remainColdMilliseconds : 0;
	// Read the existing cast timer. Moving a skill between slots must not restart it.
	item->cooldownFraction = remaining > 0
		? static_cast<float>(remaining) / static_cast<float>(std::max(remaining, info->magic->coldMilliSeconds))
		: 0.0f;
	item->setStr(remaining > 0 ? std::to_string(remaining / 1000 + (remaining % 1000 != 0)) : "");
}

inline std::string selectByMenuProfile(const std::string& defaultFile,
	const std::string& yycsFile,
	const std::string& xjqyFile)
{
	int menuResourceProfile = mrpDefault;
	auto gameManager = GameManager::getInstance();
	if (gameManager != nullptr)
	{
		menuResourceProfile = gameManager->global.feature.menuResourceProfile;
	}

	std::string candidate;
	if (menuResourceProfile == mrpXjxqy)
	{
		candidate = xjqyFile;
	}
	else if (menuResourceProfile == mrpYycs)
	{
		candidate = yycsFile;
	}

	if (!candidate.empty() && File::fileExist(candidate))
	{
		return candidate;
	}
	return defaultFile;
}

inline bool shouldUseLargeMenuImage()
{
	auto gameManager = GameManager::getInstance();
	return gameManager != nullptr && gameManager->global.feature.largeMenuImages;
}

inline _shared_imp createGoodsMenuImage(const std::shared_ptr<Goods>& goods)
{
	if (goods == nullptr)
	{
		return nullptr;
	}

	if (shouldUseLargeMenuImage())
	{
		_shared_imp image = goods->createGoodsImage();
		if (image != nullptr)
		{
			return image;
		}
	}
	return goods->createGoodsIcon();
}

inline _shared_imp createMagicMenuImage(const std::shared_ptr<Magic>& magic)
{
	if (magic == nullptr)
	{
		return nullptr;
	}

	if (shouldUseLargeMenuImage())
	{
		_shared_imp image = magic->loadImage();
		if (image != nullptr)
		{
			return image;
		}
	}
	return magic->loadIcon();
}
}
