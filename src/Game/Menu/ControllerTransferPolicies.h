#pragma once

#include "ControllerTransferCoordinator.h"

#include <functional>
#include <memory>

class GameManager;
class NPC;

ControllerTransferCoordinator::Policy createGoodsControllerTransferPolicy(
	GameManager* gameManager);
ControllerTransferCoordinator::Policy createMagicControllerTransferPolicy(
	GameManager* gameManager);
ControllerTransferCoordinator::Policy createPartnerGoodsControllerTransferPolicy(
	GameManager* gameManager,
	std::function<std::shared_ptr<NPC>()> currentPartner,
	std::function<void()> refreshMenus);
