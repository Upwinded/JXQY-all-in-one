#include "MenuController.h"
#include "../../Engine/Engine.h"
#include "../Menu/SystemNotice.h"
#include "GameController.h"
#include "GameManager.h"
#include "../Data/Global.h"
#include "../Menu/ControllerPromptPresenter.h"
#include "../Menu/ControllerFocusParticipant.h"
#include "../Menu/ControllerTransferPolicies.h"
#include "../Menu/MenuSurfaceCatalog.h"
#include "../../Input/PhysicalInputManager.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <set>
#include <tuple>
#include <utility>

namespace
{
bool rectanglesOverlap(const Rect& left, const Rect& right)
{
	return left.x < right.x + right.w
		&& right.x < left.x + left.w
		&& left.y < right.y + right.h
		&& right.y < left.y + left.h;
}
}

MenuController::MenuController()
{
	name = "menu controller";
	setPriority(epMenu);
	setPointerEventPreviewEnabled(true);
	configureControllerMenuOwnerRegistry();
	configureControllerMenuDescriptors();
	configureControllerTransferDomainDescriptors();
}

MenuController::~MenuController()
{
	removeAllChild();
	freeResource();
}

void MenuController::configureControllerMenuOwnerRegistry()
{
	controllerMenuOwnerRegistry.clear();
	auto addOwner = [this](
		ControllerMenuOwnerId id,
		ControllerMenuOwnerResolver resolve)
	{
		controllerMenuOwnerRegistry.push_back(
			{ id, std::move(resolve) });
	};

	addOwner(ControllerMenuOwnerId::State, [this]()
	{
		return ControllerMenuOwner::from(stateMenu);
	});
	addOwner(ControllerMenuOwnerId::Equip, [this]()
	{
		return ControllerMenuOwner::from(equipMenu);
	});
	addOwner(ControllerMenuOwnerId::Practice, [this]()
	{
		return ControllerMenuOwner::from(practiceMenu);
	});
	addOwner(ControllerMenuOwnerId::PartnerList, [this]()
	{
		return ControllerMenuOwner::from(partnerHeadMenu);
	});
	addOwner(ControllerMenuOwnerId::PartnerEquipment, [this]()
	{
		return ControllerMenuOwner::from(partnerEquipMenu);
	});
	addOwner(ControllerMenuOwnerId::Goods, [this]()
	{
		return ControllerMenuOwner::from(goodsMenu);
	});
	addOwner(ControllerMenuOwnerId::Magic, [this]()
	{
		return resolveMagicControllerOwner();
	});
	addOwner(ControllerMenuOwnerId::StandaloneMagic, [this]()
	{
		return ControllerMenuOwner::from(magicMenu);
	});
	addOwner(ControllerMenuOwnerId::Memo, [this]()
	{
		return ControllerMenuOwner::from(memoMenu);
	});
	addOwner(ControllerMenuOwnerId::Bottom, [this]()
	{
		return ControllerMenuOwner::from(bottomMenu);
	});
	addOwner(ControllerMenuOwnerId::Top, [this]()
	{
		return ControllerMenuOwner::from(topMenu);
	});
	addOwner(ControllerMenuOwnerId::Map, [this]()
	{
		return ControllerMenuOwner::from(mapThumbnailMenu);
	});
}

void MenuController::configureControllerMenuDescriptors()
{
	using GameInput::InputAction;
	controllerMenuDescriptors.clear();
	auto addDescriptor = [this](ControllerMenuDescriptor descriptor)
	{
		controllerMenuDescriptors.push_back(std::move(descriptor));
	};
	auto addManagedDescriptor = [&addDescriptor](
		ControllerMenuDescriptor descriptor)
	{
		descriptor.managesVisibility = true;
		addDescriptor(std::move(descriptor));
	};

	ControllerMenuDescriptor stateDescriptor;
	stateDescriptor.role = ControllerMenuRole::State;
	stateDescriptor.group = ControllerMenuGroup::Left;
	stateDescriptor.ownerId = ControllerMenuOwnerId::State;
	stateDescriptor.isAvailable = [this]()
	{
		return !isStateEquipIntegrated();
	};
	stateDescriptor.prepareOpen = [this]()
	{
		if (stateMenu != nullptr)
		{
			stateMenu->updateLabel();
		}
	};
	stateDescriptor.fallbackPriority = 5;
	addManagedDescriptor(std::move(stateDescriptor));

	ControllerMenuDescriptor equipDescriptor;
	equipDescriptor.role = ControllerMenuRole::Equip;
	equipDescriptor.group = ControllerMenuGroup::Left;
	equipDescriptor.focusTarget = ControllerFocusTarget::PlayerEquipment;
	equipDescriptor.ownerId = ControllerMenuOwnerId::Equip;
	equipDescriptor.prompts =
	{
		{ InputAction::NavigateUp, "选择" },
		{ InputAction::Confirm, "卸下" },
		{ InputAction::Secondary, "拿起/交换" },
		{ InputAction::ShowDetails, "详情" }
	};
	equipDescriptor.resolveAdditionalConflictGroup = [this]()
	{
		return gm != nullptr
			&& gm->global.feature.hideRightMenusWithIntegratedEquip
			? ControllerMenuGroup::Right
			: ControllerMenuGroup::None;
	};
	equipDescriptor.prepareOpen = [this]()
	{
		if (equipMenu != nullptr)
		{
			equipMenu->updateGoods();
			equipMenu->updateMagicDisplay();
		}
	};
	equipDescriptor.fallbackPriority = 3;
	addManagedDescriptor(std::move(equipDescriptor));

	ControllerMenuDescriptor practiceDescriptor;
	practiceDescriptor.role = ControllerMenuRole::Practice;
	practiceDescriptor.group = ControllerMenuGroup::Left;
	practiceDescriptor.focusTarget = ControllerFocusTarget::Practice;
	practiceDescriptor.ownerId = ControllerMenuOwnerId::Practice;
	practiceDescriptor.prompts =
	{
		{ InputAction::Secondary, "拿起/交换" },
		{ InputAction::ShowDetails, "详情" }
	};
	practiceDescriptor.isAvailable = [this]()
	{
		return gm != nullptr
			&& !gm->global.feature.practiceMenuDisabled;
	};
	practiceDescriptor.prepareOpen = [this]()
	{
		if (practiceMenu != nullptr)
		{
			practiceMenu->updateMagic();
		}
	};
	practiceDescriptor.fallbackPriority = 4;
	addManagedDescriptor(std::move(practiceDescriptor));

	ControllerMenuDescriptor partnerListDescriptor;
	partnerListDescriptor.role = ControllerMenuRole::PartnerList;
	partnerListDescriptor.group = ControllerMenuGroup::Left;
	partnerListDescriptor.focusTarget = ControllerFocusTarget::PartnerList;
	partnerListDescriptor.ownerId = ControllerMenuOwnerId::PartnerList;
	partnerListDescriptor.prompts =
	{
		{ InputAction::NavigateUp, "选择同伴" },
		{ InputAction::Confirm, "更换装备" }
	};
	partnerListDescriptor.isAvailable = [this]()
	{
		return partnerHeadMenu != nullptr
			&& partnerHeadMenu->hasControllerPartners();
	};
	partnerListDescriptor.customEnsureOpen = [this]()
	{
		return activatePartnerListControllerPage();
	};
	partnerListDescriptor.customClose = [this]()
	{
		if (partnerHeadMenu == nullptr)
		{
			return false;
		}
		partnerHeadMenu->deactivateControllerFocus();
		return true;
	};
	addDescriptor(std::move(partnerListDescriptor));

	ControllerMenuDescriptor partnerEquipmentDescriptor;
	partnerEquipmentDescriptor.role = ControllerMenuRole::PartnerEquipment;
	partnerEquipmentDescriptor.ownerId =
		ControllerMenuOwnerId::PartnerEquipment;
	partnerEquipmentDescriptor.focusTarget =
		ControllerFocusTarget::PartnerEquipment;
	partnerEquipmentDescriptor.prompts =
	{
		{ InputAction::NavigateUp, "选择" },
		{ InputAction::Confirm, "装备/卸下" },
		{ InputAction::Secondary, "精确交换" },
		{ InputAction::ShowDetails, "详情" },
		{ InputAction::PreviousPanel, "切换区域" },
		{ InputAction::NextPanel, "切换区域" },
		{ InputAction::Cancel, "返回" }
	};
	partnerEquipmentDescriptor.customClose = [this]()
	{
		return closePartnerEquipment(true);
	};
	partnerEquipmentDescriptor.closeMode =
		ControllerMenuCloseMode::RestoresFocus;
	partnerEquipmentDescriptor.promptMode =
		ControllerMenuPromptMode::Exclusive;
	addDescriptor(std::move(partnerEquipmentDescriptor));

	ControllerMenuDescriptor goodsDescriptor;
	goodsDescriptor.role = ControllerMenuRole::Goods;
	goodsDescriptor.group = ControllerMenuGroup::Right;
	goodsDescriptor.focusTarget = ControllerFocusTarget::GoodsBag;
	goodsDescriptor.ownerId = ControllerMenuOwnerId::Goods;
	goodsDescriptor.prompts =
	{
		{ InputAction::NavigateUp, "选择" },
		{ InputAction::Confirm, "使用/装备" },
		{ InputAction::Secondary, "拿起/交换" },
		{ InputAction::ShowDetails, "详情" }
	};
	goodsDescriptor.resolveAdditionalConflictGroup = [this]()
	{
		return gm != nullptr
			&& gm->global.feature.hideRightMenusWithIntegratedEquip
			? ControllerMenuGroup::Left
			: ControllerMenuGroup::None;
	};
	goodsDescriptor.lifecycleUsesAdditionalConflictGroup = false;
	goodsDescriptor.prepareOpen = [this]()
	{
		if (goodsMenu != nullptr)
		{
			goodsMenu->updateGoods();
			goodsMenu->updateMoney();
		}
	};
	goodsDescriptor.prepareClose = [this]()
	{
		if (goodsMenu != nullptr)
		{
			goodsMenu->cancelControllerInteraction();
		}
	};
	goodsDescriptor.fallbackPriority = 0;
	addManagedDescriptor(std::move(goodsDescriptor));

	ControllerMenuDescriptor magicDescriptor;
	magicDescriptor.role = ControllerMenuRole::Magic;
	magicDescriptor.group = ControllerMenuGroup::Right;
	magicDescriptor.focusTarget = ControllerFocusTarget::MagicList;
	magicDescriptor.ownerId = ControllerMenuOwnerId::Magic;
	magicDescriptor.groupOwnerId = ControllerMenuOwnerId::StandaloneMagic;
	magicDescriptor.prompts =
	{
		{ InputAction::NavigateUp, "选择" },
		{ InputAction::Confirm, "设为快捷武功" },
		{ InputAction::Secondary, "拿起/交换" },
		{ InputAction::ShowDetails, "详情" }
	};
	magicDescriptor.resolveAdditionalConflictGroup = [this]()
	{
		return usesIntegratedMagicControllerOwner()
			? ControllerMenuGroup::Left
			: ControllerMenuGroup::None;
	};
	magicDescriptor.prepareOpen = [this]()
	{
		if (usesIntegratedMagicControllerOwner())
		{
			if (equipMenu != nullptr)
			{
				equipMenu->updateGoods();
				equipMenu->updateMagicDisplay();
			}
			return;
		}
		if (magicMenu != nullptr)
		{
			magicMenu->updateMagic();
		}
	};
	magicDescriptor.prepareClose = [this]()
	{
		if (magicMenu != nullptr)
		{
			magicMenu->cancelControllerInteraction();
		}
	};
	magicDescriptor.fallbackPriority = 1;
	magicDescriptor.fallbackOwnerId =
		ControllerMenuOwnerId::StandaloneMagic;
	magicDescriptor.isFallbackAvailable = [this]()
	{
		return !usesIntegratedMagicControllerOwner();
	};
	addManagedDescriptor(std::move(magicDescriptor));

	ControllerMenuDescriptor memoDescriptor;
	memoDescriptor.role = ControllerMenuRole::Memo;
	memoDescriptor.group = ControllerMenuGroup::Right;
	memoDescriptor.ownerId = ControllerMenuOwnerId::Memo;
	memoDescriptor.prompts =
	{
		{ InputAction::NavigateUp, "滚动", { InputAction::ScrollUp } },
		{ InputAction::PreviousPage, "上一页" },
		{ InputAction::NextPage, "下一页" }
	};
	memoDescriptor.fallbackPriority = 2;
	addManagedDescriptor(std::move(memoDescriptor));
}

void MenuController::configureControllerTransferDomainDescriptors()
{
	controllerTransferDomainDescriptors.clear();
	auto addManagedDomain = [this](
		ControllerSlotKind kind,
		ControllerSlotDomain domain,
		ControllerMenuOwnerId ownerId,
		ControllerMenuRole role,
		ControllerFocusTarget focusTarget)
	{
		ControllerTransferDomainDescriptor descriptor;
		descriptor.kind = kind;
		descriptor.domain = domain;
		descriptor.ownerId = ownerId;
		descriptor.role = role;
		descriptor.focusTarget = focusTarget;
		descriptor.managesMenuVisibility = true;
		controllerTransferDomainDescriptors.push_back(
			std::move(descriptor));
	};
	auto addExistingDomain = [this](
		ControllerSlotKind kind,
		ControllerSlotDomain domain,
		ControllerMenuOwnerId ownerId,
		ControllerMenuRole role,
		ControllerFocusTarget focusTarget,
		std::function<bool()> isAvailable,
		std::function<void()> prepareActivate)
	{
		ControllerTransferDomainDescriptor descriptor;
		descriptor.kind = kind;
		descriptor.domain = domain;
		descriptor.ownerId = ownerId;
		descriptor.role = role;
		descriptor.focusTarget = focusTarget;
		descriptor.isAvailable = std::move(isAvailable);
		descriptor.prepareActivate = std::move(prepareActivate);
		descriptor.requiresVisibleOwner = true;
		controllerTransferDomainDescriptors.push_back(
			std::move(descriptor));
	};

	addManagedDomain(
		ControllerSlotKind::Goods,
		ControllerSlotDomain::GoodsBag,
		ControllerMenuOwnerId::Goods,
		ControllerMenuRole::Goods,
		ControllerFocusTarget::GoodsBag);
	addExistingDomain(
		ControllerSlotKind::Goods,
		ControllerSlotDomain::GoodsQuick,
		ControllerMenuOwnerId::Bottom,
		ControllerMenuRole::None,
		ControllerFocusTarget::GoodsQuick,
		std::function<bool()>(),
		[this]()
		{
			if (bottomMenu != nullptr)
			{
				bottomMenu->updateGoodsItem();
			}
		});
	addManagedDomain(
		ControllerSlotKind::Goods,
		ControllerSlotDomain::PlayerEquipment,
		ControllerMenuOwnerId::Equip,
		ControllerMenuRole::Equip,
		ControllerFocusTarget::PlayerEquipment);
	addExistingDomain(
		ControllerSlotKind::PartnerGoods,
		ControllerSlotDomain::PartnerBag,
		ControllerMenuOwnerId::PartnerEquipment,
		ControllerMenuRole::PartnerEquipment,
		ControllerFocusTarget::PartnerBag,
		[this]()
		{
			return partnerEquipMenu != nullptr
				&& partnerEquipMenu->getPartner() != nullptr;
		},
		std::function<void()>());
	addExistingDomain(
		ControllerSlotKind::PartnerGoods,
		ControllerSlotDomain::PartnerEquipment,
		ControllerMenuOwnerId::PartnerEquipment,
		ControllerMenuRole::PartnerEquipment,
		ControllerFocusTarget::PartnerEquipment,
		[this]()
		{
			return partnerEquipMenu != nullptr
				&& partnerEquipMenu->getPartner() != nullptr;
		},
		std::function<void()>());
	addManagedDomain(
		ControllerSlotKind::Magic,
		ControllerSlotDomain::MagicList,
		ControllerMenuOwnerId::Magic,
		ControllerMenuRole::Magic,
		ControllerFocusTarget::MagicList);
	addExistingDomain(
		ControllerSlotKind::Magic,
		ControllerSlotDomain::MagicQuick,
		ControllerMenuOwnerId::Bottom,
		ControllerMenuRole::None,
		ControllerFocusTarget::MagicQuick,
		std::function<bool()>(),
		[this]()
		{
			if (bottomMenu != nullptr)
			{
				bottomMenu->updateMagicItem();
			}
		});
	addManagedDomain(
		ControllerSlotKind::Magic,
		ControllerSlotDomain::Practice,
		ControllerMenuOwnerId::Practice,
		ControllerMenuRole::Practice,
		ControllerFocusTarget::Practice);
}

const MenuController::ControllerMenuDescriptor*
MenuController::findControllerMenuDescriptor(ControllerMenuRole role) const
{
	const auto descriptor = std::find_if(
		controllerMenuDescriptors.begin(),
		controllerMenuDescriptors.end(),
		[role](const ControllerMenuDescriptor& candidate)
		{
			return candidate.role == role;
		});
	return descriptor != controllerMenuDescriptors.end()
		? &(*descriptor)
		: nullptr;
}

const MenuController::ControllerTransferDomainDescriptor*
MenuController::findControllerTransferDomainDescriptor(
	ControllerSlotKind kind,
	ControllerSlotDomain domain) const
{
	const auto descriptor = std::find_if(
		controllerTransferDomainDescriptors.begin(),
		controllerTransferDomainDescriptors.end(),
		[kind, domain](
			const ControllerTransferDomainDescriptor& candidate)
		{
			return candidate.kind == kind && candidate.domain == domain;
		});
	return descriptor != controllerTransferDomainDescriptors.end()
		? &(*descriptor)
		: nullptr;
}

void MenuController::onPreviewPointerEvent(AEvent& event)
{
	synchronizeInputLifecycle();
	EventTouchID completedPointerID = TOUCH_UNTOUCHEDID;
	if (event.eventType == ET_MOUSEUP
		&& event.eventData == MBC_MOUSE_LEFT)
	{
		completedPointerID = TOUCH_MOUSEID;
	}
	else if (event.eventType == ET_FINGERUP
		|| event.eventType == ET_FINGERCANCEL)
	{
		completedPointerID = event.eventData;
	}
	if (completedPointerID != TOUCH_UNTOUCHEDID)
	{
		ownedPointerTransactions.erase(completedPointerID);
	}
	adoptPointerFocusFromEvent(event);
	EventTouchID pointerID = TOUCH_UNTOUCHEDID;
	if (event.eventType == ET_MOUSEDOWN
		&& event.eventData == MBC_MOUSE_LEFT)
	{
		pointerID = TOUCH_MOUSEID;
	}
	else if (event.eventType == ET_FINGERDOWN)
	{
		pointerID = event.eventData;
	}
	if (pointerID != TOUCH_UNTOUCHEDID
		&& (hasPointerDownInTree(pointerID)
			|| blocksWorldPointerInput()))
	{
		ownedPointerTransactions.insert(pointerID);
	}
}

bool MenuController::onHandleEvent(AEvent & e)
{
	synchronizeInputLifecycle();
	if (e.eventType == ET_MOUSEDOWN
		&& e.eventData == MBC_MOUSE_RIGHT
		&& findPointerHitTargetInTree(e.eventX, e.eventY) != nullptr)
	{
		// Right-button presses do not acquire touchingDownID. Resolve the
		// concrete child-first hit from this event's coordinates so a newly
		// opened or moved control does not depend on stale hover state.
		return true;
	}
	EventTouchID pointerID = TOUCH_UNTOUCHEDID;
	if (e.eventType == ET_MOUSEDOWN
		&& e.eventData == MBC_MOUSE_LEFT)
	{
		pointerID = TOUCH_MOUSEID;
	}
	else if (e.eventType == ET_FINGERDOWN)
	{
		pointerID = e.eventData;
	}
	if (pointerID != TOUCH_UNTOUCHEDID
		&& hasPointerDownInTree(pointerID))
	{
		// Pointer acquisition is the event-consumption boundary for non-modal
		// menus. A concrete UI hit stops the raw event before it reaches the
		// world; an outside click has no acquisition and continues normally.
		return true;
	}
	if ((e.eventType == ET_MOUSEDOWN
			|| e.eventType == ET_FINGERDOWN)
		&& blocksWorldPointerInput())
	{
		// Full pointer capture belongs only to an explicitly cataloged modal
		// surface. It is independent of logical focus and input source.
		return true;
	}
	if (e.eventType == ET_KEYDOWN && hasExclusiveControllerSurface())
	{
		// The concrete modal child had first chance to handle its own keys.
		// Anything it rejected must not fall through to lower RPG-menu
		// shortcuts merely because the modal has no mapping for that key.
		return true;
	}
	if (e.eventType == ET_KEYDOWN && e.eventData == KEY_ESCAPE)
	{
		if (partnerEquipMenu != nullptr && partnerEquipMenu->visible)
		{
			return closePartnerEquipment(true);
		}
		if (controllerFocusedRole == ControllerMenuRole::PartnerList
			&& controllerFocusedMenu.lock() == partnerHeadMenu)
		{
			return closeControllerMenu(partnerHeadMenu);
		}
		if (menuDisplayed())
		{
			clearMenu();
		}
		else
		{
			openSystemMenu();
		}
		return true;
	}
	else if (e.eventType == ET_KEYDOWN)
	{
		if (e.eventData == KEY_F1)
		{
			toggleStateView();
			return true;
		}
		else if (e.eventData == KEY_F2)
		{
			toggleEquipView();
			return true;
		}
		else if (e.eventData == KEY_F3)
		{
			togglePracticeView();
			return true;
		}
		else if (e.eventData == KEY_F4)
		{
			if (partnerHeadMenu != nullptr && partnerHeadMenu->openFirstPartnerEquipMenu())
			{
				return true;
			}
			return false;
		}
		else if (e.eventData == KEY_F5)
		{
			toggleGoodsView();
			return true;
		}
		else if (e.eventData == KEY_F6)
		{
			toggleMagicView();
			return true;
		}
		else if (e.eventData == KEY_F7)
		{
			toggleMemoView();
			return true;
		}
		else if (e.eventData == KEY_F8 || e.eventData == KEY_M || e.eventData == KEY_TAB)
		{
			toggleMapThumbnailView();
			return true;
		}

		if (menuDisplayed() && getControllerFocusedMenu() != nullptr
			&& dispatchKeyboardUIAction(
				e,
				*this,
				KeyboardNavigationKeySet::DirectionKeysOnly))
		{
			return true;
		}
	}
	return false;
}

void MenuController::onAllPointerInteractionsCanceled()
{
	ownedPointerTransactions.clear();
}

void MenuController::openSystemMenu(bool focusOptions)
{
	if (gm == nullptr)
	{
		return;
	}
	if (gm->inEvent)
	{
		showMessage("事件进行中，暂时无法打开系统菜单");
		return;
	}
	if (systemMenuOpen)
	{
		return;
	}

	systemMenuOpen = true;
	cancelWorldInteractionForMenuOpen();
	cancelControllerInteraction();
	if (topMenu != nullptr && topMenu->optionBtn != nullptr)
	{
		topMenu->optionBtn->checked = true;
	}
	if (bottomMenu != nullptr && bottomMenu->optionBtn != nullptr)
	{
		bottomMenu->optionBtn->checked = true;
	}
	gm->setGameplayPaused(true);
	if (currentDragItem != nullptr)
	{
		currentDragItem->dragEnd();
	}
	if (dragging != TOUCH_UNTOUCHEDID)
	{
		dragging = TOUCH_UNTOUCHEDID;
	}
	bool menuWasVisible = gm->menu->visible;
	gm->menu->visible = false;
	removeChild(gm->menu->toolTip);
	std::shared_ptr<System> system = std::make_shared<System>(focusOptions);
	gm->addChild(system);
	unsigned int systemResult = system->run();
	const int selectedSaveIndex = system->index;
	releaseSystemMenu(system, menuWasVisible);
	gm->handleSystemResult(systemResult, selectedSaveIndex);
	if (topMenu != nullptr && topMenu->optionBtn != nullptr)
	{
		topMenu->optionBtn->checked = false;
	}
	if (bottomMenu != nullptr && bottomMenu->optionBtn != nullptr)
	{
		bottomMenu->optionBtn->checked = false;
	}
	systemMenuOpen = false;
}

void MenuController::releaseSystemMenu(
	std::shared_ptr<System>& system,
	bool menuWasVisible)
{
	gm->removeChild(system);
	gm->menu->visible = menuWasVisible;
	// Destroying the modal focus manager restores the previous presentation
	// owner. Its menu hierarchy must already be visible so a still-valid
	// non-default logical focus is not mistaken for an unavailable target.
	system = nullptr;
}

void MenuController::openSettings()
{
	openSystemMenu(true);
}

void MenuController::init()
{
#define AddUpMenuChild(A, a); a = std::make_shared<A>(); upMenu->addChild(a);
#define AddMenuChild(A, a);  a = std::make_shared<A>(); addChild(a);
	
	freeResource();
	if (engine != nullptr)
	{
		observedInputLifecycleRevision =
			engine->inputActions().inputLifecycleRevision();
	}

	AddMenuChild(Panel, upMenu);
	// A system notice is the only top overlay. Story dialogs stay above ordinary
	// in-scene panels, while normal gameplay messages retain their old layer.
	upMenu->setPriority(epMax + 2);

	AddUpMenuChild(MsgBox, messageBox);
	AddUpMenuChild(StateMenu, stateMenu);
	AddUpMenuChild(ToolTip, toolTip);
	AddUpMenuChild(MemoMenu, memoMenu);
	AddUpMenuChild(EquipMenu, equipMenu);
	AddUpMenuChild(PracticeMenu, practiceMenu);
	AddUpMenuChild(GoodsMenu, goodsMenu);
	AddUpMenuChild(MagicMenu, magicMenu);
	AddMenuChild(BottomMenu, bottomMenu);
	if (GameManager::getInstance()->global.feature.topButtonsLayout)
	{
		AddMenuChild(TopMenu, topMenu);
		AddMenuChild(ColumnMenu, columnMenu);
	}
	AddMenuChild(Dialog, dialog);
	dialog->setPriority(epMax + 1);
	dialog->visible = false;
	AddMenuChild(SystemNotice, systemNotice);
	systemNotice->setPriority(epMax);
	AddUpMenuChild(ChooseMenu, chooseMenu);
	AddUpMenuChild(TimerMenu, timerMenu);
	AddUpMenuChild(BuySellMenu, buySellMenu);
	buySellMenu->visible = false;
	AddUpMenuChild(MapThumbnailMenu, mapThumbnailMenu);
	mapThumbnailMenu->visible = false;
	AddUpMenuChild(GambleMenu, gambleMenu);
	gambleMenu->visible = false;
	AddUpMenuChild(PartnerEquipMenu, partnerEquipMenu);
	partnerEquipMenu->visible = false;
	AddUpMenuChild(PartnerHeadMenu, partnerHeadMenu);
	AddUpMenuChild(NpcInfoPanel, npcInfoPanel);
	configureControllerTransferDomains();
}

#define freeMenu(component); \
	removeChild(component); \
	if (component.get() != nullptr)\
	{\
		component = nullptr; \
	}

#define freeMenuofUp(component); \
	if (upMenu.get() != nullptr)\
	{\
		upMenu->removeChild(component); \
	}\
	if (component.get() != nullptr)\
	{\
		component = nullptr; \
	}

void MenuController::freeResource()
{
	setPartnerEquipmentPointerScope(false);
	controllerTransferCoordinator.clear();
	controllerFocusedMenu.reset();
	controllerFocusedRole = ControllerMenuRole::None;
	controllerMenuBeforeMap.reset();
	controllerRoleBeforeMap = ControllerMenuRole::None;
	controllerMenuBeforePartnerEquipment.reset();
	controllerRoleBeforePartnerEquipment = ControllerMenuRole::None;
	controllerMenuBeforeInterfaceHide.reset();
	controllerRoleBeforeInterfaceHide = ControllerMenuRole::None;
	interfaceHidden = false;
	partnerEquipmentReturnPending = false;
	stateVisibleBeforePartnerEquipment = false;
	equipVisibleBeforePartnerEquipment = false;
	practiceVisibleBeforePartnerEquipment = false;
	goodsVisibleBeforePartnerEquipment = false;
	magicVisibleBeforePartnerEquipment = false;
	memoVisibleBeforePartnerEquipment = false;
	ownedPointerTransactions.clear();
	observedInputLifecycleRevision = 0;

	freeMenuofUp(messageBox);
	freeMenuofUp(stateMenu);
	freeMenuofUp(toolTip);
	freeMenuofUp(memoMenu);
	freeMenuofUp(equipMenu);
	freeMenuofUp(practiceMenu);
	freeMenuofUp(goodsMenu);
	freeMenuofUp(magicMenu);
	freeMenuofUp(chooseMenu);
	freeMenuofUp(timerMenu);
	freeMenuofUp(buySellMenu);
	freeMenuofUp(mapThumbnailMenu);
	freeMenuofUp(gambleMenu);
	freeMenuofUp(partnerEquipMenu);
	freeMenuofUp(partnerHeadMenu);
	freeMenuofUp(npcInfoPanel);
	freeMenu(upMenu);
	freeMenu(bottomMenu);
	freeMenu(topMenu);
	freeMenu(columnMenu);
	freeMenu(dialog);
	freeMenu(systemNotice);

}

bool MenuController::isStateEquipIntegrated() const
{
	if (equipMenu == nullptr)
	{
		return false;
	}

	bool equipContainsStateLabels = equipMenu->getComponentByName<Label>("labLife") != nullptr
		|| equipMenu->getComponentByName<Label>("labThew") != nullptr
		|| equipMenu->getComponentByName<Label>("labMana") != nullptr;
	if (!equipContainsStateLabels)
	{
		return false;
	}

	if (topMenu != nullptr && topMenu->stateBtn == nullptr)
	{
		return true;
	}
	if (bottomMenu != nullptr && bottomMenu->stateBtn == nullptr && gm->global.feature.topButtonsLayout)
	{
		return true;
	}
	return gm->global.feature.stateEquipIntegratedLayout;
}

bool MenuController::usesIntegratedMagicControllerOwner() const
{
	return gm != nullptr
		&& gm->global.feature.magicButtonOpensIntegratedEquip
		&& isStateEquipIntegrated();
}

MenuController::ControllerMenuOwner
MenuController::resolveMagicControllerOwner() const
{
	return usesIntegratedMagicControllerOwner()
		? ControllerMenuOwner::from(equipMenu)
		: ControllerMenuOwner::from(magicMenu);
}

MenuController::ControllerMenuOwner
MenuController::resolveControllerMenuOwner(
	ControllerMenuOwnerId ownerId) const
{
	if (ownerId == ControllerMenuOwnerId::None)
	{
		return ControllerMenuOwner();
	}
	const auto registration = std::find_if(
		controllerMenuOwnerRegistry.begin(),
		controllerMenuOwnerRegistry.end(),
		[ownerId](const ControllerMenuOwnerRegistration& candidate)
		{
			return candidate.id == ownerId;
		});
	return registration != controllerMenuOwnerRegistry.end()
		&& registration->resolve
		? registration->resolve()
		: ControllerMenuOwner();
}

std::vector<MenuController::ControllerMenuOwner>
MenuController::collectControllerMenuOwners() const
{
	std::vector<ControllerMenuOwner> owners;
	auto collect = [&owners](ControllerMenuOwner owner)
	{
		if (owner.element == nullptr)
		{
			return;
		}
		auto existing = std::find_if(
			owners.begin(), owners.end(),
			[&owner](const ControllerMenuOwner& candidate)
			{
				return candidate.element.get() == owner.element.get();
			});
		if (existing == owners.end())
		{
			owners.push_back(std::move(owner));
			return;
		}
		if (existing->focusParticipant == nullptr)
		{
			existing->focusParticipant = owner.focusParticipant;
		}
		if (existing->transferParticipant == nullptr)
		{
			existing->transferParticipant = owner.transferParticipant;
		}
	};

	for (const ControllerMenuOwnerRegistration& registration :
		controllerMenuOwnerRegistry)
	{
		if (registration.resolve)
		{
			collect(registration.resolve());
		}
	}
	return owners;
}

MenuController::ControllerMenuOwner MenuController::findControllerMenuOwner(
	const PElement& menuElement) const
{
	if (menuElement == nullptr)
	{
		return ControllerMenuOwner();
	}
	for (ControllerMenuOwner& owner : collectControllerMenuOwners())
	{
		if (owner.matches(menuElement))
		{
			return owner;
		}
	}
	ControllerMenuOwner owner;
	owner.element = menuElement;
	return owner;
}

void MenuController::updateLeftButtonChecks(bool stateVisible, bool equipVisible, bool practiceVisible)
{
	if (topMenu != nullptr)
	{
		if (topMenu->stateBtn) topMenu->stateBtn->checked = stateVisible;
		if (topMenu->equipBtn) topMenu->equipBtn->checked = equipVisible;
		if (topMenu->xiulianBtn) topMenu->xiulianBtn->checked = practiceVisible;
	}
	if (bottomMenu != nullptr)
	{
		if (bottomMenu->stateBtn) bottomMenu->stateBtn->checked = stateVisible;
		if (bottomMenu->equipBtn) bottomMenu->equipBtn->checked = equipVisible;
		if (bottomMenu->xiulianBtn) bottomMenu->xiulianBtn->checked = practiceVisible;
	}
}

void MenuController::updateRightButtonChecks(bool goodsVisible, bool magicVisible, bool memoVisible)
{
	if (topMenu != nullptr)
	{
		if (topMenu->goodsBtn) topMenu->goodsBtn->checked = goodsVisible;
		if (topMenu->magicBtn) topMenu->magicBtn->checked = magicVisible;
		if (topMenu->notesBtn) topMenu->notesBtn->checked = memoVisible;
	}
	if (bottomMenu != nullptr)
	{
		if (bottomMenu->goodsBtn) bottomMenu->goodsBtn->checked = goodsVisible;
		if (bottomMenu->magicBtn) bottomMenu->magicBtn->checked = magicVisible;
		if (bottomMenu->notesBtn) bottomMenu->notesBtn->checked = memoVisible;
	}
}

void MenuController::toggleStateView()
{
	toggleControllerMenuRole(isStateEquipIntegrated()
		? ControllerMenuRole::Equip
		: ControllerMenuRole::State);
}

void MenuController::toggleEquipView()
{
	toggleControllerMenuRole(ControllerMenuRole::Equip);
}

void MenuController::togglePracticeView()
{
	if (gm != nullptr && gm->global.feature.practiceMenuDisabled)
	{
		if (partnerEquipMenu != nullptr && partnerEquipMenu->visible)
		{
			closePartnerEquipment(false);
		}
		cancelControllerInteraction();
		if (practiceMenu != nullptr)
		{
			practiceMenu->deactivateControllerFocus();
			practiceMenu->visible = false;
		}
		updateLeftButtonChecks(false, false, false);
		return;
	}
	toggleControllerMenuRole(ControllerMenuRole::Practice);
}

void MenuController::toggleGoodsView()
{
	toggleControllerMenuRole(ControllerMenuRole::Goods);
}

void MenuController::toggleMagicView()
{
	toggleControllerMenuRole(ControllerMenuRole::Magic);
}

void MenuController::toggleMemoView()
{
	toggleControllerMenuRole(ControllerMenuRole::Memo);
}

bool MenuController::openPartnerEquipment(
	const std::shared_ptr<NPC>& partner,
	bool returnToPartnerList)
{
	if (partner == nullptr || partner->canEquip <= 0
		|| partnerEquipMenu == nullptr || goodsMenu == nullptr)
	{
		return false;
	}

	cancelControllerInteraction();
	if (!partnerEquipmentReturnPending)
	{
		stateVisibleBeforePartnerEquipment =
			stateMenu != nullptr && stateMenu->visible;
		equipVisibleBeforePartnerEquipment =
			equipMenu != nullptr && equipMenu->visible;
		practiceVisibleBeforePartnerEquipment =
			practiceMenu != nullptr && practiceMenu->visible;
		goodsVisibleBeforePartnerEquipment = goodsMenu->visible;
		magicVisibleBeforePartnerEquipment =
			magicMenu != nullptr && magicMenu->visible;
		memoVisibleBeforePartnerEquipment =
			memoMenu != nullptr && memoMenu->visible;
		if (returnToPartnerList)
		{
			controllerMenuBeforePartnerEquipment = partnerHeadMenu;
			controllerRoleBeforePartnerEquipment =
				ControllerMenuRole::PartnerList;
		}
		else
		{
			auto previousMenu = controllerFocusedMenu.lock();
			if (previousMenu != partnerEquipMenu)
			{
				controllerMenuBeforePartnerEquipment = previousMenu;
				controllerRoleBeforePartnerEquipment =
					controllerFocusedRole;
			}
		}
		partnerEquipmentReturnPending = true;
	}

	if (stateMenu != nullptr) stateMenu->visible = false;
	if (equipMenu != nullptr) equipMenu->visible = false;
	if (practiceMenu != nullptr) practiceMenu->visible = false;
	if (magicMenu != nullptr) magicMenu->visible = false;
	if (memoMenu != nullptr) memoMenu->visible = false;
	goodsMenu->visible = true;
	goodsMenu->updateGoods();
	goodsMenu->updateMoney();
	setPartnerEquipmentPointerScope(true);
	partnerEquipMenu->setPartner(partner);
	if (!partnerEquipMenu->visible)
	{
		closePartnerEquipment(true);
		return false;
	}
	if (!partnerEquipMenu->focusControllerDefault())
	{
		closePartnerEquipment(true);
		return false;
	}
	setControllerFocusedMenu(
		partnerEquipMenu, ControllerMenuRole::PartnerEquipment);
	updateLeftButtonChecks(false, false, false);
	updateRightButtonChecks(true, false, false);
	cancelWorldInteractionForMenuOpen();
	return true;
}

bool MenuController::closePartnerEquipment(bool restorePrevious)
{
	if (partnerEquipMenu == nullptr)
	{
		return false;
	}
	const bool wasOpen = partnerEquipMenu->visible
		|| partnerEquipmentReturnPending;
	if (!wasOpen)
	{
		return false;
	}

	auto previousMenu = controllerMenuBeforePartnerEquipment.lock();
	const ControllerMenuRole previousRole =
		controllerRoleBeforePartnerEquipment;
	const bool restoreState = partnerEquipmentReturnPending;
	const bool savedStateVisible = stateVisibleBeforePartnerEquipment;
	const bool savedEquipVisible = equipVisibleBeforePartnerEquipment;
	const bool savedPracticeVisible = practiceVisibleBeforePartnerEquipment;
	const bool savedGoodsVisible = goodsVisibleBeforePartnerEquipment;
	const bool savedMagicVisible = magicVisibleBeforePartnerEquipment;
	const bool savedMemoVisible = memoVisibleBeforePartnerEquipment;

	cancelControllerInteraction();
	goodsMenu->cancelPointerInteraction();
	partnerEquipMenu->cancelPointerInteraction();
	partnerEquipMenu->deactivateControllerFocus();
	partnerEquipMenu->visible = false;
	setPartnerEquipmentPointerScope(false);
	if (controllerFocusedMenu.lock() == partnerEquipMenu)
	{
		controllerFocusedMenu.reset();
		controllerFocusedRole = ControllerMenuRole::None;
	}
	controllerMenuBeforePartnerEquipment.reset();
	controllerRoleBeforePartnerEquipment = ControllerMenuRole::None;
	partnerEquipmentReturnPending = false;

	if (restoreState)
	{
		if (stateMenu != nullptr) stateMenu->visible = savedStateVisible;
		if (equipMenu != nullptr) equipMenu->visible = savedEquipVisible;
		if (practiceMenu != nullptr)
		{
			practiceMenu->visible = savedPracticeVisible;
		}
		if (goodsMenu != nullptr) goodsMenu->visible = savedGoodsVisible;
		if (magicMenu != nullptr) magicMenu->visible = savedMagicVisible;
		if (memoMenu != nullptr) memoMenu->visible = savedMemoVisible;
		updateLeftButtonChecks(
			savedStateVisible,
			savedEquipVisible,
			savedPracticeVisible);
		updateRightButtonChecks(
			savedGoodsVisible,
			savedMagicVisible,
			savedMemoVisible);
	}

	if (!restorePrevious)
	{
		if (previousMenu != nullptr && previousMenu->visible
			&& previousMenu->activated)
		{
			restoreControllerFocus(previousMenu, previousRole);
		}
		getControllerFocusedMenu();
		return true;
	}
	if (previousRole == ControllerMenuRole::PartnerList
		&& activatePartnerListControllerPage())
	{
		return true;
	}
	if (previousMenu != nullptr && previousMenu->visible
		&& previousMenu->activated)
	{
		if (restoreControllerFocus(previousMenu, previousRole))
		{
			return true;
		}
	}
	getControllerFocusedMenu();
	return true;
}

void MenuController::setMapThumbnailVisible(bool newVisible)
{
	if (mapThumbnailMenu == nullptr || mapThumbnailMenu->visible == newVisible)
	{
		return;
	}
	if (newVisible)
	{
		cancelControllerInteraction();
		auto currentMenu = getControllerFocusedMenu();
		if (currentMenu != mapThumbnailMenu)
		{
			controllerMenuBeforeMap = currentMenu;
			controllerRoleBeforeMap = controllerFocusedRole;
		}
		mapThumbnailMenu->setControllerVisible(true);
		if (mapThumbnailMenu->isControllerFocusActive())
		{
			setControllerFocusedMenu(
				mapThumbnailMenu, ControllerMenuRole::None);
		}
		else
		{
			getControllerFocusedMenu();
		}
		cancelWorldInteractionForMenuOpen();
		return;
	}

	const PElement currentMenu = controllerFocusedMenu.lock();
	const ControllerMenuRole currentRole = controllerFocusedRole;
	mapThumbnailMenu->setControllerVisible(false);
	auto previousMenu = controllerMenuBeforeMap.lock();
	const ControllerMenuRole previousRole = controllerRoleBeforeMap;
	controllerMenuBeforeMap.reset();
	controllerRoleBeforeMap = ControllerMenuRole::None;
	if (currentMenu != mapThumbnailMenu)
	{
		if (currentMenu != nullptr && currentMenu->visible
			&& currentMenu->activated
			&& isControllerFocusActive(currentMenu))
		{
			controllerFocusedMenu = currentMenu;
			controllerFocusedRole = currentRole;
			return;
		}
		controllerFocusedMenu.reset();
		controllerFocusedRole = ControllerMenuRole::None;
		getControllerFocusedMenu();
		return;
	}
	controllerFocusedMenu.reset();
	controllerFocusedRole = ControllerMenuRole::None;
	if (previousMenu != nullptr && previousMenu->visible
		&& previousMenu->activated)
	{
		if (restoreControllerFocus(previousMenu, previousRole))
		{
			return;
		}
	}
	getControllerFocusedMenu();
}

void MenuController::toggleMapThumbnailView()
{
	setMapThumbnailVisible(
		mapThumbnailMenu != nullptr && !mapThumbnailMenu->visible);
}

void MenuController::applyLayoutByGameType()
{
	cancelControllerInteraction();
	bool needTopButtons = GameManager::getInstance()->global.feature.topButtonsLayout;
	bool hasTopButtons = (topMenu != nullptr);

	if (needTopButtons == hasTopButtons)
	{
		configureControllerTransferDomains();
		return;
	}

	// Rebuild BottomMenu with the correct INI
	removeChild(bottomMenu);
	bottomMenu = std::make_shared<BottomMenu>();
	addChild(bottomMenu);

	if (needTopButtons)
	{
		// Create TopMenu and independent ColumnMenu for YYCS/XJXQY
		topMenu = std::make_shared<TopMenu>();
		addChild(topMenu);
		columnMenu = std::make_shared<ColumnMenu>();
		addChild(columnMenu);
	}
	else
	{
		// Destroy TopMenu and independent ColumnMenu
		freeMenu(topMenu);
		freeMenu(columnMenu);
	}
	configureControllerTransferDomains();
}

bool MenuController::menuDisplayed()
{
	bool vis = false;
#define addMenuVis(m) \
	if (m != nullptr)\
	{\
		vis = (vis || m->visible);\
	}
	addMenuVis(stateMenu)
	addMenuVis(memoMenu);
	addMenuVis(equipMenu);
	addMenuVis(practiceMenu);
	addMenuVis(goodsMenu);
	addMenuVis(magicMenu);
	addMenuVis(mapThumbnailMenu);
	addMenuVis(partnerEquipMenu);
	vis = vis || (controllerFocusedRole == ControllerMenuRole::PartnerList
		&& controllerFocusedMenu.lock() == partnerHeadMenu);

	return vis;
}

bool MenuController::blocksWorldInput() const
{
	auto isVisible = [](const PElement& element)
	{
		return element != nullptr && element->visible;
	};
	auto blocksSemanticInput = [&isVisible](
		const PElement& element,
		MenuSurfaceCatalog::SurfaceId surfaceId)
	{
		return isVisible(element)
			&& MenuSurfaceCatalog::blocksWorldSemanticInput(surfaceId);
	};

	return blocksSemanticInput(
			stateMenu, MenuSurfaceCatalog::SurfaceId::RpgState)
		|| blocksSemanticInput(
			memoMenu, MenuSurfaceCatalog::SurfaceId::RpgMemo)
		|| blocksSemanticInput(
			equipMenu, MenuSurfaceCatalog::SurfaceId::RpgEquipment)
		|| blocksSemanticInput(
			practiceMenu, MenuSurfaceCatalog::SurfaceId::RpgPractice)
		|| blocksSemanticInput(
			goodsMenu, MenuSurfaceCatalog::SurfaceId::RpgGoods)
		|| blocksSemanticInput(
			magicMenu, MenuSurfaceCatalog::SurfaceId::RpgMagic)
		|| blocksSemanticInput(
			dialog, MenuSurfaceCatalog::SurfaceId::Dialog)
		|| blocksSemanticInput(
			chooseMenu, MenuSurfaceCatalog::SurfaceId::Choice)
		|| blocksSemanticInput(
			buySellMenu, MenuSurfaceCatalog::SurfaceId::BuySell)
		|| blocksSemanticInput(
			gambleMenu, MenuSurfaceCatalog::SurfaceId::GambleNormal)
		|| blocksSemanticInput(
			partnerEquipMenu,
			MenuSurfaceCatalog::SurfaceId::PartnerEquipment)
		|| blocksSemanticInput(
			mapThumbnailMenu,
			MenuSurfaceCatalog::SurfaceId::MapThumbnail);
}

bool MenuController::blocksWorldKeyboardInput() const
{
	auto blocksKeyboardInput = [](const PElement& element,
		MenuSurfaceCatalog::SurfaceId surfaceId)
	{
		return element != nullptr && element->visible
			&& MenuSurfaceCatalog::blocksWorldKeyboardInput(surfaceId);
	};

	return blocksKeyboardInput(
			dialog, MenuSurfaceCatalog::SurfaceId::Dialog)
		|| blocksKeyboardInput(
			chooseMenu, MenuSurfaceCatalog::SurfaceId::Choice)
		|| blocksKeyboardInput(
			buySellMenu, MenuSurfaceCatalog::SurfaceId::BuySell)
		|| blocksKeyboardInput(
			gambleMenu, MenuSurfaceCatalog::SurfaceId::GambleNormal)
		|| blocksKeyboardInput(
			partnerEquipMenu,
			MenuSurfaceCatalog::SurfaceId::PartnerEquipment);
}

bool MenuController::blocksWorldPointerInput() const
{
	auto isVisible = [](const PElement& element)
	{
		return element != nullptr && element->visible;
	};
	auto blocksPointerInput = [&isVisible](
		const PElement& element,
		MenuSurfaceCatalog::SurfaceId surfaceId)
	{
		return isVisible(element)
			&& MenuSurfaceCatalog::blocksWorldPointerInput(surfaceId);
	};

	// Ordinary RPG side panels historically coexist with mouse interaction in
	// the uncovered world area. Only modal surfaces own the whole pointer input
	// context; semantic controller/keyboard actions remain blocked by
	// blocksWorldInput() while any ordinary panel is open.
	return blocksPointerInput(
			dialog, MenuSurfaceCatalog::SurfaceId::Dialog)
		|| blocksPointerInput(
			chooseMenu, MenuSurfaceCatalog::SurfaceId::Choice)
		|| blocksPointerInput(
			buySellMenu, MenuSurfaceCatalog::SurfaceId::BuySell)
		|| blocksPointerInput(
			gambleMenu, MenuSurfaceCatalog::SurfaceId::GambleNormal)
		|| blocksPointerInput(
			partnerEquipMenu,
			MenuSurfaceCatalog::SurfaceId::PartnerEquipment);
}

bool MenuController::hasActiveControllerPromptOwner()
{
	if (!visible)
	{
		return false;
	}
	auto isVisible = [](const PElement& element)
	{
		return element != nullptr && element->visible && element->activated;
	};
	if (isVisible(dialog) || isVisible(chooseMenu) || isVisible(buySellMenu)
		|| isVisible(gambleMenu) || isVisible(partnerEquipMenu))
	{
		return true;
	}
	if (mapThumbnailMenu != nullptr && mapThumbnailMenu->visible
		&& mapThumbnailMenu->isControllerFocusActive())
	{
		return true;
	}
	if (controllerTransferCoordinator.active())
	{
		return true;
	}
	return getControllerFocusedMenu() != nullptr;
}

void MenuController::clearMenu()
{
	closePartnerEquipment(false);
	cancelControllerInteraction();
	controllerFocusedMenu.reset();
	controllerFocusedRole = ControllerMenuRole::None;
	controllerMenuBeforeMap.reset();
	controllerRoleBeforeMap = ControllerMenuRole::None;
	if (partnerHeadMenu != nullptr)
	{
		partnerHeadMenu->deactivateControllerFocus();
	}
	if (practiceMenu != nullptr) practiceMenu->visible = false;
	if (equipMenu != nullptr) equipMenu->visible = false;
	if (stateMenu != nullptr) stateMenu->visible = false;
	if (magicMenu != nullptr) magicMenu->visible = false;
	if (memoMenu != nullptr) memoMenu->visible = false;
	if (goodsMenu != nullptr) goodsMenu->visible = false;
	if (mapThumbnailMenu != nullptr)
	{
		mapThumbnailMenu->setControllerVisible(false);
	}
	if (toolTip != nullptr)
	{
		toolTip->hide();
	}
	updateLeftButtonChecks(false, false, false);
	updateRightButtonChecks(false, false, false);
}

PElement MenuController::getControllerFocusedMenu()
{
	auto isVisible = [](const PElement& element)
	{
		return element != nullptr && element->visible && element->activated;
	};
	// Nested modal owners dispatch their own semantic actions. Do not route an
	// underlying ordinary menu while one of them is active.
	if (isVisible(dialog) || isVisible(chooseMenu) || isVisible(buySellMenu)
		|| isVisible(gambleMenu))
	{
		cancelControllerInteraction();
		return nullptr;
	}
	if (controllerTransferCoordinator.active())
	{
		PElement transferOwner = controllerTransferCoordinator.activeOwner();
		if (isVisible(transferOwner))
		{
			return transferOwner;
		}
		controllerTransferCoordinator.cancel();
	}

	auto focusedMenu = controllerFocusedMenu.lock();
	if (partnerHeadMenu != nullptr && focusedMenu == partnerHeadMenu
		&& !partnerHeadMenu->hasControllerPartners())
	{
		partnerHeadMenu->deactivateControllerFocus();
		focusedMenu.reset();
	}
	if (isVisible(focusedMenu)
		&& isControllerFocusActive(focusedMenu))
	{
		return focusedMenu;
	}
	controllerFocusedMenu.reset();
	controllerFocusedRole = ControllerMenuRole::None;

	// Partner equipment is an in-scene modal that deliberately keeps the
	// player's bag visible underneath it. If every modal candidate becomes
	// unavailable, leave focus empty instead of restoring a lower surface.
	if (isVisible(partnerEquipMenu))
	{
		if (restoreControllerFocus(
			partnerEquipMenu, ControllerMenuRole::PartnerEquipment))
		{
			return partnerEquipMenu;
		}
		return nullptr;
	}

	std::vector<const ControllerMenuDescriptor*> fallbackDescriptors;
	for (const ControllerMenuDescriptor& descriptor :
		controllerMenuDescriptors)
	{
		if (descriptor.fallbackPriority >= 0)
		{
			fallbackDescriptors.push_back(&descriptor);
		}
	}
	std::sort(fallbackDescriptors.begin(), fallbackDescriptors.end(),
		[](const ControllerMenuDescriptor* first,
			const ControllerMenuDescriptor* second)
		{
			return first->fallbackPriority < second->fallbackPriority;
		});
	for (const ControllerMenuDescriptor* descriptor : fallbackDescriptors)
	{
		if (descriptor->isFallbackAvailable
			&& !descriptor->isFallbackAvailable())
		{
			continue;
		}
		const ControllerMenuOwner fallbackOwner = resolveControllerMenuOwner(
			descriptor->fallbackOwnerId != ControllerMenuOwnerId::None
				? descriptor->fallbackOwnerId
				: descriptor->ownerId);
		if (isVisible(fallbackOwner.element)
			&& restoreControllerFocus(
				fallbackOwner.element, descriptor->role))
		{
			return fallbackOwner.element;
		}
	}
	return nullptr;
}

void MenuController::setControllerFocusedMenu(
	const PElement& menuElement, ControllerMenuRole role)
{
	for (const ControllerMenuOwner& owner : collectControllerMenuOwners())
	{
		if (owner.element == menuElement || owner.focusParticipant == nullptr)
		{
			continue;
		}
		owner.focusParticipant->deactivateControllerFocus();
	}
	controllerFocusedMenu = menuElement;
	controllerFocusedRole = menuElement != nullptr ? role : ControllerMenuRole::None;
}

bool MenuController::restoreControllerFocus(
	const PElement& menuElement, ControllerMenuRole role)
{
	return restoreControllerFocus(
		findControllerMenuOwner(menuElement), role);
}

bool MenuController::restoreControllerFocus(
	ControllerMenuOwner owner, ControllerMenuRole role)
{
	const ControllerMenuDescriptor* descriptor =
		findControllerMenuDescriptor(role);
	if (descriptor != nullptr)
	{
		ControllerMenuOwner descriptorOwner =
			resolveControllerMenuOwner(descriptor->ownerId);
		if (!descriptorOwner.matches(owner.element))
		{
			return false;
		}
		owner = std::move(descriptorOwner);
	}
	if (owner.element == nullptr || owner.focusParticipant == nullptr
		|| !owner.focusParticipant->activateControllerFocus(
			descriptor != nullptr
				? descriptor->focusTarget
				: ControllerFocusTarget::Default))
	{
		return false;
	}
	setControllerFocusedMenu(owner.element, role);
	return true;
}

bool MenuController::isControllerFocusActive(
	const PElement& menuElement) const
{
	const ControllerMenuOwner owner = findControllerMenuOwner(menuElement);
	if (owner.focusParticipant != nullptr)
	{
		return owner.focusParticipant->isControllerFocusActive();
	}
	return false;
}

bool MenuController::hasExclusiveControllerSurface() const
{
	auto isVisible = [](const PElement& element)
	{
		return element != nullptr && element->visible && element->activated;
	};
	return isVisible(dialog)
		|| isVisible(chooseMenu)
		|| isVisible(buySellMenu)
		|| isVisible(gambleMenu)
		|| isVisible(partnerEquipMenu);
}

MenuController::ControllerMenuRole MenuController::resolveControllerRole(
	const PElement& menuElement) const
{
	if (menuElement == nullptr)
	{
		return ControllerMenuRole::None;
	}
	if (controllerFocusedMenu.lock() == menuElement
		&& controllerFocusedRole != ControllerMenuRole::None)
	{
		return controllerFocusedRole;
	}
	for (const ControllerMenuDescriptor& descriptor :
		controllerMenuDescriptors)
	{
		const ControllerMenuOwner owner =
			resolveControllerMenuOwner(descriptor.ownerId);
		if (owner.element == menuElement)
		{
			return descriptor.role;
		}
	}
	return ControllerMenuRole::None;
}

MenuController::ControllerMenuRole MenuController::resolveControllerRole(
	const ControllerMenuOwner& owner,
	const PElement& controlElement) const
{
	if (owner.element == nullptr || owner.focusParticipant == nullptr
		|| controlElement == nullptr)
	{
		return resolveControllerRole(owner.element);
	}
	for (const ControllerMenuDescriptor& descriptor :
		controllerMenuDescriptors)
	{
		const ControllerMenuOwner descriptorOwner =
			resolveControllerMenuOwner(descriptor.ownerId);
		if (descriptorOwner.matches(owner.element)
			&& owner.focusParticipant->controllerFocusElementMatchesTarget(
				controlElement, descriptor.focusTarget))
		{
			return descriptor.role;
		}
	}
	return resolveControllerRole(owner.element);
}

std::vector<MenuController::VisibleControllerFocusCandidate>
MenuController::collectVisibleControllerFocusCandidates() const
{
	std::vector<VisibleControllerFocusCandidate> candidates;
	std::set<const Element*> registeredElements;
	PElement mapOccluder;
	if (mapThumbnailMenu != nullptr && mapThumbnailMenu->visible
		&& mapThumbnailMenu->activated)
	{
		const std::vector<PElement> mapCandidates =
			mapThumbnailMenu->controllerFocusCandidates();
		if (!mapCandidates.empty()
			&& isUIFocusElementAvailable(mapCandidates.front()))
		{
			mapOccluder = mapCandidates.front();
		}
	}
	for (const ControllerMenuOwner& owner : collectControllerMenuOwners())
	{
		if (owner.element == nullptr || owner.focusParticipant == nullptr
			|| !owner.element->visible || !owner.element->activated)
		{
			continue;
		}
		for (const PElement& element :
			owner.focusParticipant->controllerFocusCandidates())
		{
			if (!isUIFocusElementAvailable(element)
				|| (mapOccluder != nullptr
					&& owner.element != mapThumbnailMenu
					&& rectanglesOverlap(
						element->rect, mapOccluder->rect))
				|| !registeredElements.insert(element.get()).second)
			{
				continue;
			}
			candidates.push_back(
				{ owner, resolveControllerRole(owner, element), element });
		}
	}
	return candidates;
}

bool MenuController::focusVisibleControllerCandidate(
	const VisibleControllerFocusCandidate& candidate)
{
	if (candidate.owner.element == nullptr
		|| candidate.owner.focusParticipant == nullptr
		|| !candidate.owner.element->visible
		|| !candidate.owner.element->activated
		|| !isUIFocusElementAvailable(candidate.element)
		|| !candidate.owner.focusParticipant->focusControllerElement(
			candidate.element))
	{
		return false;
	}
	setControllerFocusedMenu(candidate.owner.element, candidate.role);
	if (candidate.role != ControllerMenuRole::None)
	{
		updateControllerMenuSelection(candidate.role, true);
	}
	return true;
}

bool MenuController::focusInitialVisibleControllerCandidate(
	UIFocusDirection direction)
{
	const std::vector<VisibleControllerFocusCandidate> candidates =
		collectVisibleControllerFocusCandidates();
	if (candidates.empty())
	{
		return false;
	}

	int left = std::numeric_limits<int>::max();
	int top = std::numeric_limits<int>::max();
	int right = std::numeric_limits<int>::min();
	int bottom = std::numeric_limits<int>::min();
	for (const VisibleControllerFocusCandidate& candidate : candidates)
	{
		if (candidate.element == nullptr)
		{
			continue;
		}
		left = std::min(left, candidate.element->rect.x);
		top = std::min(top, candidate.element->rect.y);
		right = std::max(
			right,
			candidate.element->rect.x + candidate.element->rect.w);
		bottom = std::max(
			bottom,
			candidate.element->rect.y + candidate.element->rect.h);
	}
	if (left >= right || top >= bottom)
	{
		return false;
	}

	// With no current focus, treat the pressed direction as entering the
	// visible control cloud from the opposite screen edge. This keeps the
	// first HUD target consistent with its displayed position instead of the
	// owner-registration order.
	Rect entryAnchor = { left, top, right - left, bottom - top };
	switch (direction)
	{
	case UIFocusDirection::Up:
		entryAnchor.y = bottom + 1;
		entryAnchor.h = 1;
		break;
	case UIFocusDirection::Down:
		entryAnchor.y = top - 2;
		entryAnchor.h = 1;
		break;
	case UIFocusDirection::Left:
		entryAnchor.x = right + 1;
		entryAnchor.w = 1;
		break;
	case UIFocusDirection::Right:
		entryAnchor.x = left - 2;
		entryAnchor.w = 1;
		break;
	}

	std::optional<UIFocusSpatialScore> bestScore;
	const VisibleControllerFocusCandidate* bestCandidate = nullptr;
	for (std::size_t index = 0; index < candidates.size(); index++)
	{
		const VisibleControllerFocusCandidate& candidate = candidates[index];
		const std::optional<UIFocusSpatialScore> score =
			scoreUIFocusSpatialCandidate(
				entryAnchor,
				candidate.element->rect,
				direction,
				index);
		if (score && (!bestScore || *score < *bestScore))
		{
			bestScore = score;
			bestCandidate = &candidate;
		}
	}
	return bestCandidate != nullptr
		&& focusVisibleControllerCandidate(*bestCandidate);
}

bool MenuController::moveControllerFocusAcrossMenus(
	const PElement& currentMenu,
	UIFocusDirection direction)
{
	if (currentMenu == nullptr || controllerTransferCoordinator.active()
		|| hasExclusiveControllerSurface())
	{
		return false;
	}
	const ControllerMenuOwner currentOwner =
		findControllerMenuOwner(currentMenu);
	PElement anchor = currentOwner.focusParticipant != nullptr
		? currentOwner.focusParticipant->controllerFocusedElement()
		: nullptr;
	if (!isUIFocusElementAvailable(anchor))
	{
		anchor = currentMenu;
	}
	if (anchor == nullptr || anchor->rect.w <= 0 || anchor->rect.h <= 0)
	{
		return false;
	}

	std::optional<UIFocusSpatialScore> bestScore;
	const VisibleControllerFocusCandidate* bestCandidate = nullptr;
	const auto candidates = collectVisibleControllerFocusCandidates();
	for (std::size_t index = 0; index < candidates.size(); index++)
	{
		const VisibleControllerFocusCandidate& candidate = candidates[index];
		if (candidate.element == anchor)
		{
			continue;
		}
		const std::optional<UIFocusSpatialScore> score =
			scoreUIFocusSpatialCandidate(
				anchor->rect,
				candidate.element->rect,
				direction,
				index);
		if (!score)
		{
			continue;
		}
		if (!bestScore || *score < *bestScore)
		{
			bestScore = score;
			bestCandidate = &candidate;
		}
	}
	return bestCandidate != nullptr
		&& focusVisibleControllerCandidate(*bestCandidate);
}

bool MenuController::adoptControllerPointerFocus(
	const PElement& menuElement,
	const PElement& controlElement)
{
	if (menuElement == nullptr || controlElement == nullptr
		|| (hasExclusiveControllerSurface()
			&& menuElement != partnerEquipMenu))
	{
		return false;
	}
	for (const VisibleControllerFocusCandidate& candidate :
		collectVisibleControllerFocusCandidates())
	{
		if (candidate.owner.element == menuElement
			&& candidate.element == controlElement)
		{
			return focusVisibleControllerCandidate(candidate);
		}
	}
	return false;
}

bool MenuController::adoptControllerPointerFocus(
	const PElement& controlElement)
{
	if (!visible || !activated || controlElement == nullptr)
	{
		return false;
	}
	const bool exclusiveSurfaceVisible = hasExclusiveControllerSurface();
	for (const VisibleControllerFocusCandidate& candidate :
		collectVisibleControllerFocusCandidates())
	{
		if (exclusiveSurfaceVisible
			&& candidate.owner.element != partnerEquipMenu)
		{
			continue;
		}
		if (candidate.element == controlElement)
		{
			return focusVisibleControllerCandidate(candidate);
		}
	}
	return false;
}

bool MenuController::ownsPointerTransaction(
	EventTouchID pointerID) const
{
	return ownedPointerTransactions.find(pointerID)
		!= ownedPointerTransactions.end();
}

void MenuController::adoptPointerFocusFromEvent(const AEvent& event)
{
	EventTouchID pointerId = TOUCH_UNTOUCHEDID;
	bool requirePointerDown = true;
	Element* pointerHitTarget = nullptr;
	if (event.eventType == ET_MOUSEDOWN
		&& event.eventData == MBC_MOUSE_LEFT)
	{
		pointerId = TOUCH_MOUSEID;
	}
	else if (event.eventType == ET_MOUSEDOWN
		&& event.eventData == MBC_MOUSE_RIGHT)
	{
		pointerId = TOUCH_MOUSEID;
		requirePointerDown = false;
		pointerHitTarget =
			findPointerHitTargetInTree(event.eventX, event.eventY);
	}
	else if (event.eventType == ET_FINGERDOWN)
	{
		pointerId = event.eventData;
	}
	if (pointerId == TOUCH_UNTOUCHEDID)
	{
		return;
	}
	const bool exclusiveSurfaceVisible = hasExclusiveControllerSurface();
	for (const VisibleControllerFocusCandidate& candidate :
		collectVisibleControllerFocusCandidates())
	{
		if (exclusiveSurfaceVisible
			&& candidate.owner.element != partnerEquipMenu)
		{
			continue;
		}
		if (candidate.element != nullptr
			&& (requirePointerDown
				? candidate.element->touchingDownID == pointerId
				: [&candidate, pointerHitTarget]()
				{
					for (Element* target = pointerHitTarget;
						target != nullptr;
						target = target->parent)
					{
						if (target == candidate.element.get())
						{
							return true;
						}
					}
					return false;
				}()))
		{
			focusVisibleControllerCandidate(candidate);
			return;
		}
	}
}

MenuController::ControllerMenuOwner
MenuController::resolveControllerMenuGroupOwner(
	const ControllerMenuDescriptor& descriptor) const
{
	return resolveControllerMenuOwner(
		descriptor.groupOwnerId != ControllerMenuOwnerId::None
			? descriptor.groupOwnerId
			: descriptor.ownerId);
}

std::vector<MenuController::ControllerMenuGroup>
MenuController::resolveControllerMenuConflictGroups(
	const ControllerMenuDescriptor& descriptor,
	ControllerMenuHideMode mode) const
{
	std::vector<ControllerMenuGroup> groups;
	if (descriptor.group != ControllerMenuGroup::None)
	{
		groups.push_back(descriptor.group);
	}
	const bool includeAdditionalGroup =
		mode == ControllerMenuHideMode::TransferFocusSwitch
		|| descriptor.lifecycleUsesAdditionalConflictGroup;
	const ControllerMenuGroup additionalGroup =
		includeAdditionalGroup && descriptor.resolveAdditionalConflictGroup
			? descriptor.resolveAdditionalConflictGroup()
			: ControllerMenuGroup::None;
	if (additionalGroup != ControllerMenuGroup::None
		&& std::find(groups.begin(), groups.end(), additionalGroup)
			== groups.end())
	{
		groups.push_back(additionalGroup);
	}
	return groups;
}

void MenuController::hideControllerMenuSurface(
	const ControllerMenuDescriptor& descriptor,
	ControllerMenuHideMode mode,
	const PElement& preservedOwner)
{
	ControllerMenuOwner owner = resolveControllerMenuGroupOwner(descriptor);
	if (owner.element == nullptr || !owner.element->visible)
	{
		return;
	}
	if (owner.element == preservedOwner)
	{
		return;
	}
	if (mode == ControllerMenuHideMode::LifecycleClose
		&& descriptor.prepareClose)
	{
		descriptor.prepareClose();
	}
	if (owner.focusParticipant != nullptr)
	{
		owner.focusParticipant->deactivateControllerFocus();
	}
	owner.element->visible = false;
}

void MenuController::hideControllerMenuGroup(
	ControllerMenuGroup group,
	ControllerMenuHideMode mode,
	const PElement& preservedOwner)
{
	if (group == ControllerMenuGroup::None)
	{
		return;
	}
	for (const ControllerMenuDescriptor& descriptor :
		controllerMenuDescriptors)
	{
		if (descriptor.managesVisibility && descriptor.group == group)
		{
			hideControllerMenuSurface(
				descriptor, mode, preservedOwner);
		}
	}
	updateControllerMenuGroupSelection(group, ControllerMenuRole::None);
}

void MenuController::updateControllerMenuGroupSelection(
	ControllerMenuGroup group, ControllerMenuRole selectedRole)
{
	if (group == ControllerMenuGroup::Left)
	{
		updateLeftButtonChecks(
			selectedRole == ControllerMenuRole::State,
			selectedRole == ControllerMenuRole::Equip,
			selectedRole == ControllerMenuRole::Practice);
	}
	else if (group == ControllerMenuGroup::Right)
	{
		updateRightButtonChecks(
			selectedRole == ControllerMenuRole::Goods,
			selectedRole == ControllerMenuRole::Magic,
			selectedRole == ControllerMenuRole::Memo);
	}
}

void MenuController::updateControllerMenuSelection(
	ControllerMenuRole role, bool selected)
{
	const ControllerMenuDescriptor* descriptor =
		findControllerMenuDescriptor(role);
	if (descriptor == nullptr)
	{
		return;
	}
	updateControllerMenuGroupSelection(
		descriptor->group,
		selected ? role : ControllerMenuRole::None);
	if (role == ControllerMenuRole::Magic
		&& usesIntegratedMagicControllerOwner())
	{
		updateControllerMenuGroupSelection(
			ControllerMenuGroup::Left,
			selected
				? ControllerMenuRole::Equip
				: ControllerMenuRole::None);
	}
	else if (role == ControllerMenuRole::Equip && gm != nullptr
		&& gm->global.feature.hideRightMenusWithIntegratedEquip)
	{
		updateControllerMenuGroupSelection(
			ControllerMenuGroup::Right,
			selected
				? ControllerMenuRole::Magic
				: ControllerMenuRole::None);
	}
}

bool MenuController::ensureControllerMenuOpen(ControllerMenuRole role)
{
	const ControllerMenuDescriptor* descriptor =
		findControllerMenuDescriptor(role);
	if (descriptor == nullptr
		|| (descriptor->isAvailable && !descriptor->isAvailable()))
	{
		return false;
	}
	if (!descriptor->managesVisibility)
	{
		if (!descriptor->customEnsureOpen
			|| !descriptor->customEnsureOpen())
		{
			return false;
		}
		cancelWorldInteractionForMenuOpen();
		return true;
	}
	ControllerMenuOwner owner =
		resolveControllerMenuOwner(descriptor->ownerId);
	if (owner.element == nullptr)
	{
		return false;
	}
	if (owner.element->visible)
	{
		const bool focusActivated = owner.focusParticipant != nullptr
			&& owner.focusParticipant->activateControllerFocus(
				descriptor->focusTarget);
		if (focusActivated)
		{
			setControllerFocusedMenu(owner.element, role);
		}
		else
		{
			if (owner.focusParticipant != nullptr)
			{
				owner.focusParticipant->deactivateControllerFocus();
			}
			getControllerFocusedMenu();
		}
		updateControllerMenuSelection(role, true);
		return true;
	}

	cancelControllerInteraction();
	for (ControllerMenuGroup conflictGroup :
		resolveControllerMenuConflictGroups(
			*descriptor, ControllerMenuHideMode::LifecycleClose))
	{
		hideControllerMenuGroup(conflictGroup);
	}
	if (descriptor->prepareOpen)
	{
		descriptor->prepareOpen();
	}
	owner = resolveControllerMenuOwner(descriptor->ownerId);
	if (owner.element == nullptr)
	{
		return false;
	}
	owner.element->visible = true;
	const bool focusActivated = owner.focusParticipant != nullptr
		&& owner.focusParticipant->activateControllerFocus(
			descriptor->focusTarget);
	if (focusActivated)
	{
		setControllerFocusedMenu(owner.element, role);
	}
	else
	{
		if (owner.focusParticipant != nullptr)
		{
			owner.focusParticipant->deactivateControllerFocus();
		}
		getControllerFocusedMenu();
	}
	updateControllerMenuSelection(role, true);
	cancelWorldInteractionForMenuOpen();
	return true;
}

bool MenuController::closeControllerMenuRole(ControllerMenuRole role)
{
	const ControllerMenuDescriptor* descriptor =
		findControllerMenuDescriptor(role);
	if (descriptor == nullptr)
	{
		return false;
	}
	if (!descriptor->managesVisibility)
	{
		return descriptor->customClose && descriptor->customClose();
	}
	const ControllerMenuOwner owner =
		resolveControllerMenuOwner(descriptor->ownerId);
	if (owner.element == nullptr || !owner.element->visible)
	{
		return false;
	}

	cancelControllerInteraction();
	hideControllerMenuGroup(descriptor->group);
	ControllerMenuOwner semanticOwner =
		resolveControllerMenuOwner(descriptor->ownerId);
	ControllerMenuOwner groupOwner =
		resolveControllerMenuGroupOwner(*descriptor);
	if (semanticOwner.element != nullptr
		&& semanticOwner.element != groupOwner.element)
	{
		if (semanticOwner.focusParticipant != nullptr)
		{
			semanticOwner.focusParticipant->deactivateControllerFocus();
		}
		semanticOwner.element->visible = false;
	}
	updateControllerMenuSelection(role, false);
	return true;
}

bool MenuController::closeControllerMenu(const PElement& menuElement)
{
	if (menuElement == nullptr)
	{
		return false;
	}
	if (menuElement == mapThumbnailMenu)
	{
		setMapThumbnailVisible(false);
		return true;
	}

	const ControllerMenuDescriptor* descriptor =
		findControllerMenuDescriptor(controllerFocusedRole);
	if (descriptor == nullptr
		|| !resolveControllerMenuOwner(
			descriptor->ownerId).matches(menuElement)
		|| !closeControllerMenuRole(controllerFocusedRole))
	{
		return false;
	}
	if (descriptor->closeMode == ControllerMenuCloseMode::RestoresFocus)
	{
		return true;
	}
	controllerFocusedMenu.reset();
	controllerFocusedRole = ControllerMenuRole::None;
	getControllerFocusedMenu();
	return true;
}

bool MenuController::switchControllerMenu(int direction)
{
	if (direction == 0)
	{
		return false;
	}
	const PElement focusedMenu = getControllerFocusedMenu();
	if (focusedMenu == nullptr || focusedMenu != controllerFocusedMenu.lock())
	{
		return false;
	}
	const ControllerMenuDescriptor* currentDescriptor =
		findControllerMenuDescriptor(controllerFocusedRole);
	if (currentDescriptor == nullptr
		|| currentDescriptor->group == ControllerMenuGroup::None
		|| !resolveControllerMenuOwner(
			currentDescriptor->ownerId).matches(focusedMenu))
	{
		return false;
	}
	std::vector<ControllerMenuRole> group;
	for (const ControllerMenuDescriptor& descriptor :
		controllerMenuDescriptors)
	{
		if (descriptor.group != currentDescriptor->group)
		{
			continue;
		}
		if (descriptor.isAvailable && !descriptor.isAvailable())
		{
			continue;
		}
		group.push_back(descriptor.role);
	}
	if (group.size() < 2)
	{
		return true;
	}
	auto current = std::find(group.begin(), group.end(), controllerFocusedRole);
	std::size_t currentIndex = current == group.end()
		? 0
		: static_cast<std::size_t>(std::distance(group.begin(), current));
	const std::size_t nextIndex = direction < 0
		? (currentIndex + group.size() - 1) % group.size()
		: (currentIndex + 1) % group.size();

	if (focusedMenu != nullptr && focusedMenu->visible)
	{
		if (!closeControllerMenu(focusedMenu))
		{
			return false;
		}
	}
	if (ensureControllerMenuOpen(group[nextIndex]))
	{
		return true;
	}
	ensureControllerMenuOpen(currentDescriptor->role);
	return false;
}

bool MenuController::toggleControllerMenuRole(ControllerMenuRole role)
{
	const ControllerMenuDescriptor* descriptor =
		findControllerMenuDescriptor(role);
	if (descriptor == nullptr
		|| descriptor->ownerId == ControllerMenuOwnerId::None)
	{
		return false;
	}
	if (partnerEquipMenu != nullptr && partnerEquipMenu->visible
		&& role != ControllerMenuRole::PartnerEquipment
		&& !closePartnerEquipment(false))
	{
		return false;
	}
	const ControllerMenuOwner owner =
		resolveControllerMenuOwner(descriptor->ownerId);
	if (owner.element != nullptr && owner.element->visible)
	{
		ControllerMenuRole sharedOwnerRole = ControllerMenuRole::None;
		if (controllerFocusedMenu.lock() == owner.element)
		{
			const ControllerMenuDescriptor* focusedDescriptor =
				findControllerMenuDescriptor(controllerFocusedRole);
			if (focusedDescriptor != nullptr
				&& resolveControllerMenuOwner(
					focusedDescriptor->ownerId).matches(owner.element))
			{
				sharedOwnerRole = controllerFocusedRole;
			}
		}
		if (!closeControllerMenuRole(role))
		{
			return false;
		}
		if (sharedOwnerRole != ControllerMenuRole::None
			&& sharedOwnerRole != role)
		{
			updateControllerMenuSelection(sharedOwnerRole, false);
		}
		const PElement focusedMenu = controllerFocusedMenu.lock();
		if (focusedMenu == owner.element
			|| (focusedMenu != nullptr && !focusedMenu->visible))
		{
			controllerFocusedMenu.reset();
			controllerFocusedRole = ControllerMenuRole::None;
			getControllerFocusedMenu();
		}
		return true;
	}
	return ensureControllerMenuOpen(role);
}

bool MenuController::activatePartnerListControllerPage()
{
	if (partnerHeadMenu == nullptr || !partnerHeadMenu->visible
		|| !partnerHeadMenu->activated
		|| !partnerHeadMenu->hasControllerPartners())
	{
		return false;
	}
	closePartnerEquipment(false);
	cancelControllerInteraction();
	if (stateMenu != nullptr) stateMenu->visible = false;
	if (equipMenu != nullptr)
	{
		equipMenu->deactivateControllerFocus();
		equipMenu->visible = false;
	}
	if (practiceMenu != nullptr)
	{
		practiceMenu->deactivateControllerFocus();
		practiceMenu->visible = false;
	}
	updateLeftButtonChecks(false, false, false);
	if (!partnerHeadMenu->focusControllerDefault())
	{
		getControllerFocusedMenu();
		return false;
	}
	setControllerFocusedMenu(
		partnerHeadMenu, ControllerMenuRole::PartnerList);
	return true;
}

void MenuController::cancelControllerInteraction()
{
	controllerTransferCoordinator.cancel();
	if (toolTip != nullptr)
	{
		toolTip->hide();
	}
}

void MenuController::cancelWorldInteractionForMenuOpen()
{
	if (gm != nullptr && gm->controller != nullptr)
	{
		gm->controller->cancelControllerWorldInteraction();
	}
	if (gm != nullptr && gm->npcManager != nullptr)
	{
		gm->npcManager->cancelPointerInteraction();
	}
	if (gm != nullptr && gm->objectManager != nullptr)
	{
		gm->objectManager->cancelPointerInteraction();
	}
}

void MenuController::setPartnerEquipmentPointerScope(bool active)
{
	if (upMenu == nullptr || goodsMenu == nullptr)
	{
		return;
	}
	if (active == partnerEquipmentPointerScopeActive)
	{
		return;
	}
	if (active)
	{
		partnerEquipmentPointerScopeActive = true;
		partnerEquipmentPointerEventStates.clear();
		// The borrowed Goods branch remains usable inside this modal, but a
		// contact acquired before the modal boundary must not commit after it.
		goodsMenu->cancelPointerInteraction();
		if (partnerEquipMenu != nullptr)
		{
			partnerEquipMenu->cancelPointerInteraction();
		}
		auto disablePointerBranch = [this](const PElement& element)
		{
			if (element == nullptr)
			{
				return;
			}
			partnerEquipmentPointerEventStates.push_back(
				{ element, element->needEvents });
			element->cancelPointerInteraction();
			element->needEvents = false;
		};
		for (const PElement& element : children)
		{
			if (element != upMenu)
			{
				disablePointerBranch(element);
			}
		}
		for (const PElement& element : upMenu->children)
		{
			if (element != partnerEquipMenu && element != goodsMenu)
			{
				disablePointerBranch(element);
			}
		}
		goodsPriorityBeforePartnerEquipment = goodsMenu->getPriority();
		goodsMenu->setPriority(epMax + 1);
		return;
	}
	for (const PointerEventState& state :
		partnerEquipmentPointerEventStates)
	{
		if (auto element = state.element.lock())
		{
			element->needEvents = state.needEvents;
		}
	}
	partnerEquipmentPointerEventStates.clear();
	partnerEquipmentPointerScopeActive = false;
	goodsMenu->setPriority(goodsPriorityBeforePartnerEquipment);
}

ControllerTransferCoordinator& MenuController::controllerTransfers()
{
	return controllerTransferCoordinator;
}

const ControllerTransferCoordinator& MenuController::controllerTransfers() const
{
	return controllerTransferCoordinator;
}

SlotInteractionBinding MenuController::makeControllerSlotInteractionBinding(
	GameManager* gameManager,
	ControllerSlotKind kind,
	ControllerSlotDomain domain)
{
	SlotInteractionBinding binding;
	binding.transfers = gameManager != nullptr && gameManager->menu != nullptr
		? &gameManager->menu->controllerTransfers()
		: nullptr;
	binding.kind = kind;
	binding.domain = domain;
	binding.showMessage = [](const std::string& message)
	{
		GameManager* currentGameManager = GameManager::getInstance();
		if (currentGameManager != nullptr)
		{
			currentGameManager->showMessage(message);
		}
	};
	return binding;
}

void MenuController::registerControllerTransferDomain(
	ControllerSlotKind kind,
	ControllerSlotDomain domain)
{
	const ControllerTransferDomainDescriptor* descriptor =
		findControllerTransferDomainDescriptor(kind, domain);
	if (descriptor == nullptr)
	{
		return;
	}
	ControllerTransferCoordinator::DomainBinding binding;
	binding.kind = kind;
	binding.domain = domain;
	binding.activate = [this, kind, domain]()
	{
		return activateControllerTransferDomain(kind, domain);
	};
	binding.deactivate = [this, kind, domain]()
	{
		const ControllerTransferDomainDescriptor* currentDescriptor =
			findControllerTransferDomainDescriptor(kind, domain);
		const ControllerMenuOwner owner = currentDescriptor != nullptr
			? resolveControllerMenuOwner(currentDescriptor->ownerId)
			: ControllerMenuOwner();
		assert(owner.element == nullptr
			|| owner.transferParticipant != nullptr);
		if (owner.transferParticipant != nullptr)
		{
			owner.transferParticipant->deactivateControllerFocus();
		}
	};
	binding.owner = [this, kind, domain]()
	{
		const ControllerTransferDomainDescriptor* currentDescriptor =
			findControllerTransferDomainDescriptor(kind, domain);
		return currentDescriptor != nullptr
			? resolveControllerMenuOwner(currentDescriptor->ownerId).element
			: PElement();
	};
	binding.refreshSelection = [this, kind, domain]()
	{
		const ControllerTransferDomainDescriptor* currentDescriptor =
			findControllerTransferDomainDescriptor(kind, domain);
		const ControllerMenuOwner owner = currentDescriptor != nullptr
			? resolveControllerMenuOwner(currentDescriptor->ownerId)
			: ControllerMenuOwner();
		assert(owner.element == nullptr
			|| owner.transferParticipant != nullptr);
		if (owner.transferParticipant != nullptr)
		{
			owner.transferParticipant->refreshControllerTransferHighlight();
		}
	};
	controllerTransferCoordinator.registerDomain(std::move(binding));
}

void MenuController::configureControllerTransferDomains()
{
	controllerTransferCoordinator.clear();
	controllerTransferCoordinator.setPolicy(
		ControllerSlotKind::Goods,
		createGoodsControllerTransferPolicy(gm));
	controllerTransferCoordinator.setPolicy(
		ControllerSlotKind::Magic,
		createMagicControllerTransferPolicy(gm));
	controllerTransferCoordinator.setPolicy(
		ControllerSlotKind::PartnerGoods,
		createPartnerGoodsControllerTransferPolicy(
			gm,
			[this]() -> std::shared_ptr<NPC>
			{
				return partnerEquipMenu != nullptr
					? partnerEquipMenu->getPartner()
					: nullptr;
			},
			[this]()
			{
				if (goodsMenu != nullptr)
				{
					goodsMenu->updateGoods();
				}
				if (partnerEquipMenu != nullptr)
				{
					partnerEquipMenu->updateGoods();
				}
			}));

	for (const ControllerTransferDomainDescriptor& descriptor :
		controllerTransferDomainDescriptors)
	{
		registerControllerTransferDomain(
			descriptor.kind, descriptor.domain);
	}
}

bool MenuController::activateControllerTransferDomain(
	ControllerSlotKind kind,
	ControllerSlotDomain domain)
{
	const ControllerTransferDomainDescriptor* descriptor =
		findControllerTransferDomainDescriptor(kind, domain);
	if (descriptor == nullptr
		|| (descriptor->isAvailable && !descriptor->isAvailable()))
	{
		return false;
	}
	// Partner equipment is a modal transfer owner. Other transfer kinds must
	// not start beneath it or close it from Coordinator::cycleDomain(), because
	// that lifecycle path cancels and resets the coordinator re-entrantly.
	if (descriptor->kind != ControllerSlotKind::PartnerGoods
		&& partnerEquipMenu != nullptr && partnerEquipMenu->visible)
	{
		return false;
	}
	const ControllerMenuDescriptor* menuDescriptor = nullptr;
	if (descriptor->managesMenuVisibility)
	{
		menuDescriptor = findControllerMenuDescriptor(descriptor->role);
		if (menuDescriptor == nullptr || !menuDescriptor->managesVisibility
			|| menuDescriptor->ownerId != descriptor->ownerId
			|| (menuDescriptor->isAvailable
				&& !menuDescriptor->isAvailable()))
		{
			return false;
		}
	}

	ControllerMenuOwner owner =
		resolveControllerMenuOwner(descriptor->ownerId);
	if (owner.element == nullptr || !owner.element->activated
		|| owner.focusParticipant == nullptr
		|| owner.transferParticipant == nullptr
		|| (descriptor->requiresVisibleOwner && !owner.element->visible))
	{
		return false;
	}

	if (menuDescriptor != nullptr && menuDescriptor->prepareOpen)
	{
		menuDescriptor->prepareOpen();
		owner = resolveControllerMenuOwner(descriptor->ownerId);
		if (owner.element == nullptr || !owner.element->activated
			|| owner.focusParticipant == nullptr
			|| owner.transferParticipant == nullptr)
		{
			return false;
		}
	}
	if (descriptor->prepareActivate)
	{
		descriptor->prepareActivate();
	}
	const bool ownerWasVisible = owner.element->visible;
	if (menuDescriptor != nullptr)
	{
		// Effective focus eligibility includes ancestor visibility. Expose the
		// target before activating its exact node, then commit conflict hiding
		// only after activation succeeds.
		owner.element->visible = true;
	}
	const bool ownerFocusWasActive =
		owner.focusParticipant->isControllerFocusActive();
	const PElement previousFocusedMenu = controllerFocusedMenu.lock();
	const ControllerMenuRole previousFocusedRole = controllerFocusedRole;
	if (!owner.focusParticipant->activateControllerFocus(
		descriptor->focusTarget))
	{
		if (ownerFocusWasActive && previousFocusedMenu == owner.element)
		{
			restoreControllerFocus(
				previousFocusedMenu, previousFocusedRole);
		}
		else if (!ownerFocusWasActive)
		{
			owner.focusParticipant->deactivateControllerFocus();
		}
		if (menuDescriptor != nullptr)
		{
			owner.element->visible = ownerWasVisible;
		}
		return false;
	}
	if (menuDescriptor != nullptr)
	{
		for (ControllerMenuGroup conflictGroup :
			resolveControllerMenuConflictGroups(
				*menuDescriptor,
				ControllerMenuHideMode::TransferFocusSwitch))
		{
			hideControllerMenuGroup(
				conflictGroup,
				ControllerMenuHideMode::TransferFocusSwitch,
				owner.element);
		}
		owner.element->visible = true;
	}
	if (descriptor->role != ControllerMenuRole::None)
	{
		setControllerFocusedMenu(owner.element, descriptor->role);
		updateControllerMenuSelection(descriptor->role, true);
	}
	return true;
}

void MenuController::synchronizeInputLifecycle()
{
	if (engine == nullptr)
	{
		return;
	}
	const std::uint64_t currentRevision =
		engine->inputActions().inputLifecycleRevision();
	if (currentRevision == observedInputLifecycleRevision)
	{
		return;
	}
	observedInputLifecycleRevision = currentRevision;
	cancelControllerInteraction();
	ownedPointerTransactions.clear();
}

bool MenuController::closeVisibleUnfocusedControllerSurface(
	const PElement& focusedMenu)
{
	if (focusedMenu == mapThumbnailMenu)
	{
		return false;
	}
	if (memoMenu != nullptr && memoMenu->visible
		&& !memoMenu->isControllerFocusActive()
		&& closeControllerMenuRole(ControllerMenuRole::Memo))
	{
		if (controllerFocusedMenu.lock() == memoMenu)
		{
			controllerFocusedMenu.reset();
			controllerFocusedRole = ControllerMenuRole::None;
		}
		getControllerFocusedMenu();
		return true;
	}
	if (stateMenu != nullptr && stateMenu->visible
		&& closeControllerMenuRole(ControllerMenuRole::State))
	{
		if (controllerFocusedMenu.lock() == stateMenu)
		{
			controllerFocusedMenu.reset();
			controllerFocusedRole = ControllerMenuRole::None;
		}
		getControllerFocusedMenu();
		return true;
	}
	return false;
}

bool MenuController::onHandleUIAction(UIAction action)
{
	synchronizeInputLifecycle();
	UIFocusDirection direction = UIFocusDirection::Up;
	bool directionalAction = true;
	switch (action)
	{
	case UIAction::NavigateUp:
		direction = UIFocusDirection::Up;
		break;
	case UIAction::NavigateDown:
		direction = UIFocusDirection::Down;
		break;
	case UIAction::NavigateLeft:
		direction = UIFocusDirection::Left;
		break;
	case UIAction::NavigateRight:
		direction = UIFocusDirection::Right;
		break;
	default:
		directionalAction = false;
		break;
	}
	PElement focusedMenu = getControllerFocusedMenu();
	if (gm != nullptr && (gm->inEvent || !gm->global.data.canInput))
	{
		cancelControllerInteraction();
		return focusedMenu != nullptr;
	}
	if (action == UIAction::Cancel
		&& focusedMenu == nullptr
		&& partnerEquipMenu != nullptr && partnerEquipMenu->visible)
	{
		return closePartnerEquipment(true);
	}
	if (action == UIAction::Cancel
		&& controllerFocusedRole == ControllerMenuRole::None
		&& !controllerTransferCoordinator.active()
		&& closeVisibleUnfocusedControllerSurface(focusedMenu))
	{
		return true;
	}
	if (focusedMenu == nullptr)
	{
		// A direction press may explicitly enter the always-visible HUD focus
		// graph. Confirm/attack and other shared world actions must never create
		// that focus implicitly.
		if (directionalAction && !hasExclusiveControllerSurface())
		{
			return focusInitialVisibleControllerCandidate(direction);
		}
		return false;
	}
	if (controllerTransferCoordinator.active())
	{
		if (action == UIAction::Cancel)
		{
			if (!controllerTransferCoordinator.cancelSource())
			{
				controllerTransferCoordinator.cancel();
			}
			if (toolTip != nullptr)
			{
				toolTip->hide();
			}
			return true;
		}
		if (action == UIAction::PanelPrevious)
		{
			controllerTransferCoordinator.cycleDomain(-1);
			return true;
		}
		if (action == UIAction::PanelNext)
		{
			controllerTransferCoordinator.cycleDomain(1);
			return true;
		}
		if (dispatchUIActionWithFocusRecovery(
			action,
			[&focusedMenu](UIAction recoveredAction)
			{
				return focusedMenu->handleUIAction(recoveredAction);
			},
			[this, &focusedMenu]()
			{
				return isControllerFocusActive(focusedMenu);
			},
			[this]()
			{
				return controllerTransferCoordinator.reactivateCurrentDomain();
			}))
		{
			return true;
		}
		return true;
	}
	if (directionalAction && mapThumbnailMenu != nullptr
		&& mapThumbnailMenu->visible && focusedMenu != mapThumbnailMenu)
	{
		// The map is drawn above ordinary RPG panels. Use the filtered global
		// spatial graph while it is open so a panel-local focus manager cannot
		// move onto a control hidden below the map.
		moveControllerFocusAcrossMenus(focusedMenu, direction);
		return true;
	}
	// Pointer interaction and config-driven window resizing both suspend or
	// rebuild slot focus while leaving the same visible menu as the semantic
	// input owner. Recover that owner once, then replay the fresh action so the
	// user never needs a second gamepad press.
	if (dispatchUIActionWithFocusRecovery(
		action,
		[&focusedMenu](UIAction recoveredAction)
		{
			return focusedMenu->handleUIAction(recoveredAction);
		},
		[this, &focusedMenu]()
		{
			return isControllerFocusActive(focusedMenu);
		},
		[this, &focusedMenu]()
		{
			return restoreControllerFocus(
				focusedMenu, controllerFocusedRole);
		}))
	{
		return true;
	}
	if (directionalAction
		&& moveControllerFocusAcrossMenus(focusedMenu, direction))
	{
		return true;
	}
	if (action == UIAction::Cancel)
	{
		if (closeControllerMenu(focusedMenu))
		{
			return true;
		}
		if (controllerFocusedRole == ControllerMenuRole::None)
		{
			const ControllerMenuOwner owner =
				findControllerMenuOwner(focusedMenu);
			if (owner.focusParticipant != nullptr)
			{
				owner.focusParticipant->deactivateControllerFocus();
			}
			controllerFocusedMenu.reset();
			controllerFocusedRole = ControllerMenuRole::None;
			return true;
		}
		return false;
	}
	if (action == UIAction::PanelPrevious)
	{
		return switchControllerMenu(-1);
	}
	if (action == UIAction::PanelNext)
	{
		return switchControllerMenu(1);
	}
	return false;
}

void MenuController::onDrawEnd()
{
	if (!visible || engine == nullptr
		|| !shouldPresentGamepadFocus(engine))
	{
		return;
	}
	auto isVisible = [](const PElement& element)
	{
		return element != nullptr && element->visible && element->activated;
	};
	std::vector<ControllerPromptItem> items;
	using GameInput::InputAction;
	const bool chooseOwnsPrompt = isVisible(chooseMenu)
		&& Element::isCurrentRunOwner(chooseMenu.get());
	const bool dialogOwnsPrompt = isVisible(dialog)
		&& Element::isCurrentRunOwner(dialog.get());
	if (chooseOwnsPrompt)
	{
		items =
		{
			{ InputAction::NavigateUp, "选择" },
			{ InputAction::Confirm, "确认" }
		};
		if (chooseMenu->hasMultiplePages())
		{
			items.push_back({ InputAction::PreviousPage, "翻页",
				{ InputAction::NextPage } });
		}
	}
	else if (dialogOwnsPrompt)
	{
		items =
		{
			{ InputAction::Confirm, "继续" }
		};
	}
	else if (Element::currentRunOwnerBlocksParentInput())
	{
		// A nested modal such as System, Option, SaveLoad, or YesNo owns its
		// own prompt bar. Do not redraw an underlying HUD/menu prompt over it.
		return;
	}
	else
	{
		const PElement focusedMenu = getControllerFocusedMenu();
		if (isVisible(buySellMenu) || isVisible(gambleMenu)
			|| (mapThumbnailMenu != nullptr
				&& focusedMenu == mapThumbnailMenu
				&& mapThumbnailMenu->isControllerFocusActive()))
		{
			return;
		}
		if (controllerTransferCoordinator.active())
		{
			PElement transferOwner =
				controllerTransferCoordinator.activeOwner();
			if (!isVisible(transferOwner))
			{
				return;
			}
			items.push_back({ InputAction::NavigateUp,
				controllerTransferCoordinator.hasSource()
					? "选择目标" : "选择来源" });
			if (controllerTransferCoordinator.hasSource())
			{
				items.push_back({ InputAction::Confirm, "放置/交换" });
				items.push_back({ InputAction::Secondary, "放置/交换" });
			}
			else
			{
				items.push_back({ InputAction::Secondary, "拿起" });
			}
			items.push_back({ InputAction::ShowDetails, "详情" });
			items.push_back({ InputAction::PreviousPanel, "切换区域" });
			items.push_back({ InputAction::NextPanel, "切换区域" });
			items.push_back({ InputAction::Cancel,
				controllerTransferCoordinator.hasSource()
					? "取消拿起" : "退出编辑" });
		}
		else
		{
			if (!isVisible(focusedMenu))
			{
				return;
			}
			const ControllerMenuDescriptor* descriptor =
				findControllerMenuDescriptor(controllerFocusedRole);
			if (descriptor != nullptr)
			{
				items.insert(items.end(),
					descriptor->prompts.begin(),
					descriptor->prompts.end());
				if (descriptor->promptMode
					== ControllerMenuPromptMode::SharedNavigation)
				{
					items.push_back({
						InputAction::PreviousPanel, "上一菜单" });
					items.push_back({
						InputAction::NextPanel, "下一菜单" });
					items.push_back({ InputAction::Cancel, "关闭" });
				}
			}
			else if (focusedMenu == bottomMenu)
			{
				items =
				{
					{ InputAction::NavigateUp, "选择" },
					{ InputAction::Confirm, "打开/使用" },
					{ InputAction::Secondary, "拿起/交换" },
					{ InputAction::Cancel, "退出导航" }
				};
			}
			else if (focusedMenu == topMenu)
			{
				items =
				{
					{ InputAction::NavigateUp, "选择" },
					{ InputAction::Confirm, "打开" },
					{ InputAction::Cancel, "退出导航" }
				};
			}
			else
			{
				return;
			}
			items.push_back({ InputAction::OpenSystemMenu, "系统" });
		}
	}

	if (items.empty())
	{
		return;
	}
	ControllerPromptPresenter::drawBottomBar(
		engine, engine->inputActions(), items);
}

void MenuController::update()
{
	bottomMenu->updateGoodsItem();
	bottomMenu->updateMagicItem();

	goodsMenu->scrollbar->setPosition(goodsMenu->scrollbar->min);
	goodsMenu->updateGoods();

	magicMenu->scrollbar->setPosition(magicMenu->scrollbar->min);
	magicMenu->updateMagic();

	practiceMenu->updateMagic();
	equipMenu->updateGoods();
	equipMenu->updateMagicDisplay();
	if (partnerEquipMenu != nullptr)
	{
		partnerEquipMenu->updateGoods();
	}
	goodsMenu->updateMoney();
	memoMenu->reRange((int)gm->memo.memo.size() > 0 ? (int)gm->memo.memo.size() - 1 : 0);
	memoMenu->reset();
}

void MenuController::updateAfterGameLoad()
{
	if (bottomMenu != nullptr)
	{
		bottomMenu->updateGoodsItem();
		bottomMenu->updateMagicItem();
	}

	if (goodsMenu != nullptr)
	{
		goodsMenu->scrollbar->setPosition(goodsMenu->scrollbar->min);
		if (goodsMenu->visible)
		{
			goodsMenu->updateGoods();
			goodsMenu->updateMoney();
		}
	}
	if (magicMenu != nullptr)
	{
		magicMenu->scrollbar->setPosition(magicMenu->scrollbar->min);
		if (magicMenu->visible)
		{
			magicMenu->updateMagic();
		}
	}
	if (practiceMenu != nullptr && practiceMenu->visible)
	{
		practiceMenu->updateMagic();
	}
	if (equipMenu != nullptr && equipMenu->visible)
	{
		equipMenu->updateGoods();
		equipMenu->updateMagicDisplay();
	}
	if (partnerEquipMenu != nullptr && partnerEquipMenu->visible)
	{
		partnerEquipMenu->updateGoods();
	}
	if (memoMenu != nullptr)
	{
		memoMenu->reRange(
			static_cast<int>(gm->memo.memo.size()) > 0
				? static_cast<int>(gm->memo.memo.size()) - 1
				: 0);
		memoMenu->reset();
	}
}

void MenuController::onWindowResize(int width, int height)
{
	(void)width;
	(void)height;
	refreshResourceBackedMenuData();
}

void MenuController::refreshResourceBackedMenuData()
{
	if (bottomMenu != nullptr)
	{
		bottomMenu->updateGoodsItem();
		bottomMenu->updateMagicItem();
	}
	if (goodsMenu != nullptr)
	{
		goodsMenu->updateGoods();
		goodsMenu->updateMoney();
	}
	if (magicMenu != nullptr)
	{
		magicMenu->updateMagic();
	}
	if (practiceMenu != nullptr)
	{
		practiceMenu->updateMagic();
	}
	if (equipMenu != nullptr)
	{
		equipMenu->updateGoods();
		equipMenu->updateMagicDisplay();
	}
	if (partnerEquipMenu != nullptr)
	{
		partnerEquipMenu->updateGoods();
	}

	updateLeftButtonChecks(
		stateMenu != nullptr && stateMenu->visible,
		equipMenu != nullptr && equipMenu->visible,
		practiceMenu != nullptr && practiceMenu->visible);
	updateRightButtonChecks(
		goodsMenu != nullptr && goodsMenu->visible,
		magicMenu != nullptr && magicMenu->visible,
		memoMenu != nullptr && memoMenu->visible);
}

void MenuController::showMessage(const std::string& str, UTime duration)
{
	if (messageBox != nullptr)
	{
		messageBox->showMessage(str, duration);
	}
}

void MenuController::showSystemNotice(
	const std::string& str, UTime duration)
{
	if (systemNotice != nullptr)
	{
		systemNotice->showMessage(
			std::string(u8"系统：") + str,
			duration);
	}
}

bool MenuController::showGoodsToolTip(
	const PElement& owner,
	const std::shared_ptr<Goods>& goods,
	const PElement& anchor)
{
	if (toolTip == nullptr || upMenu == nullptr || owner == nullptr
		|| goods == nullptr || anchor == nullptr)
	{
		hideToolTip();
		return false;
	}
	toolTip->showForOwner(owner);
	upMenu->addChild(toolTip);
	toolTip->setGoods(goods);
	toolTip->placeNearElement(anchor);
	return true;
}

bool MenuController::showMagicToolTip(
	const PElement& owner,
	const std::shared_ptr<Magic>& magic,
	int level,
	const PElement& anchor)
{
	if (toolTip == nullptr || upMenu == nullptr || owner == nullptr
		|| magic == nullptr || anchor == nullptr)
	{
		hideToolTip();
		return false;
	}
	toolTip->showForOwner(owner);
	upMenu->addChild(toolTip);
	toolTip->setMagic(magic, level);
	toolTip->placeNearElement(anchor);
	return true;
}

void MenuController::hideToolTip()
{
	if (toolTip != nullptr)
	{
		toolTip->hide();
	}
}

void MenuController::hideBottomWnd()
{
	closePartnerEquipment(false);
	cancelControllerInteraction();
	if (!interfaceHidden)
	{
		controllerMenuBeforeInterfaceHide = getControllerFocusedMenu();
		controllerRoleBeforeInterfaceHide = controllerFocusedRole;
		interfaceHidden = true;
	}
	if (bottomMenu != nullptr)
	{
		bottomMenu->deactivateControllerFocus();
		bottomMenu->visible = false;
	}
	if (topMenu != nullptr)
	{
		topMenu->deactivateControllerFocus();
		topMenu->visible = false;
	}
	if (columnMenu) columnMenu->visible = false;
	if (partnerHeadMenu)
	{
		partnerHeadMenu->deactivateControllerFocus();
		partnerHeadMenu->visible = false;
	}
}

void MenuController::showBottomWnd()
{
	if (bottomMenu) bottomMenu->visible = true;
	if (topMenu) topMenu->visible = true;
	if (columnMenu) columnMenu->visible = true;
	if (partnerHeadMenu) partnerHeadMenu->visible = true;
	auto previousMenu = controllerMenuBeforeInterfaceHide.lock();
	const ControllerMenuRole previousRole =
		controllerRoleBeforeInterfaceHide;
	controllerMenuBeforeInterfaceHide.reset();
	controllerRoleBeforeInterfaceHide = ControllerMenuRole::None;
	const bool restorePrevious = interfaceHidden;
	interfaceHidden = false;
	if (restorePrevious && previousMenu != nullptr
		&& previousMenu->visible && previousMenu->activated
		&& restoreControllerFocus(previousMenu, previousRole))
	{
		return;
	}
	getControllerFocusedMenu();
}
