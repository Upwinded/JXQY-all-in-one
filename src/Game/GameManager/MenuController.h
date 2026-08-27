#pragma once
#include "../../Element/Element.h"
#include "../Menu/Menu.h"
#include "../Menu/NpcInfoPanel.h"
#include "../Menu/ControllerFocusParticipant.h"
#include "../Menu/ControllerPromptPresenter.h"
#include "../Menu/ControllerTransferCoordinator.h"
#include "../Menu/SlotGridController.h"

#include <cstdint>
#include <functional>
#include <set>
#include <type_traits>
#include <vector>

class Goods;
class GamepadRPGMenuActionsTestAccess;
class GameManager;
class GamepadWorldRuntimeTestAccess;
class Magic;
class NPC;
class SystemNotice;
class System;

class MenuController :
	public Element
{
	friend class UIFocusTestAccess;
	friend class GamepadRPGMenuActionsTestAccess;
	friend class GamepadWorldRuntimeTestAccess;
public:
	MenuController();
	virtual ~MenuController();

	virtual bool onHandleEvent(AEvent & e);

	void init();
	void freeResource();
	void applyLayoutByGameType();

	bool menuDisplayed();
	bool blocksWorldInput() const;
	bool blocksWorldKeyboardInput() const;
	bool blocksWorldPointerInput() const;
	bool hasActiveControllerPromptOwner();
	void clearMenu();
	void update();
	void updateAfterGameLoad();
	void showMessage(const std::string& str, UTime duration = 3500);
	void showSystemNotice(
		const std::string& str, UTime duration = 7000);
	bool showGoodsToolTip(
		const PElement& owner,
		const std::shared_ptr<Goods>& goods,
		const PElement& anchor);
	bool showMagicToolTip(
		const PElement& owner,
		const std::shared_ptr<Magic>& magic,
		int level,
		const PElement& anchor);
	void hideToolTip();
	void hideBottomWnd();
	void showBottomWnd();
	bool isStateEquipIntegrated() const;
	void toggleStateView();
	void toggleEquipView();
	void togglePracticeView();
	void toggleGoodsView();
	void toggleMagicView();
	void toggleMemoView();
	bool openPartnerEquipment(
		const std::shared_ptr<NPC>& partner,
		bool returnToPartnerList);
	bool closePartnerEquipment(bool restorePrevious);
	void setMapThumbnailVisible(bool visible);
	void toggleMapThumbnailView();
	void openSystemMenu(bool focusOptions = false);
	void openSettings();
	void cancelControllerInteraction();
	// Pointer input selects a logical anchor without turning pointer input into
	// a gamepad-only capture mode. The focus presentation remains governed by
	// the most recent input source.
	bool adoptControllerPointerFocus(
		const PElement& menuElement,
		const PElement& controlElement);
	bool adoptControllerPointerFocus(const PElement& controlElement);
	bool ownsPointerTransaction(EventTouchID pointerID) const;
	ControllerTransferCoordinator& controllerTransfers();
	const ControllerTransferCoordinator& controllerTransfers() const;
	static SlotInteractionBinding makeControllerSlotInteractionBinding(
		GameManager* gameManager,
		ControllerSlotKind kind,
		ControllerSlotDomain domain);

	std::shared_ptr<Panel> upMenu = nullptr;

	std::shared_ptr<MsgBox> messageBox = nullptr;
	std::shared_ptr<SystemNotice> systemNotice = nullptr;
	std::shared_ptr<StateMenu> stateMenu = nullptr;
	std::shared_ptr<ToolTip> toolTip = nullptr;
	std::shared_ptr<MemoMenu> memoMenu = nullptr;
	std::shared_ptr<EquipMenu> equipMenu = nullptr;
	std::shared_ptr<PracticeMenu> practiceMenu = nullptr;
	std::shared_ptr<GoodsMenu> goodsMenu = nullptr;
	std::shared_ptr<MagicMenu> magicMenu = nullptr;

	std::shared_ptr<BottomMenu> bottomMenu = nullptr;
	std::shared_ptr<TopMenu> topMenu = nullptr;
	std::shared_ptr<ColumnMenu> columnMenu = nullptr;

	std::shared_ptr<Dialog> dialog = nullptr;
	std::shared_ptr<ChooseMenu> chooseMenu = nullptr;
	std::shared_ptr<TimerMenu> timerMenu = nullptr;
	std::shared_ptr<BuySellMenu> buySellMenu = nullptr;
	std::shared_ptr<MapThumbnailMenu> mapThumbnailMenu = nullptr;
	std::shared_ptr<GambleMenu> gambleMenu = nullptr;
	std::shared_ptr<PartnerEquipMenu> partnerEquipMenu = nullptr;
	std::shared_ptr<PartnerHeadMenu> partnerHeadMenu = nullptr;
	std::shared_ptr<NpcInfoPanel> npcInfoPanel = nullptr;

private:
	enum class ControllerMenuRole
	{
		None,
		State,
		Equip,
		Practice,
		PartnerList,
		PartnerEquipment,
		Goods,
		Magic,
		Memo
	};
	enum class ControllerMenuGroup
	{
		None,
		Left,
		Right
	};
	enum class ControllerMenuCloseMode
	{
		ResetFocus,
		RestoresFocus
	};
	enum class ControllerMenuPromptMode
	{
		SharedNavigation,
		Exclusive
	};
	enum class ControllerMenuOwnerId
	{
		None,
		State,
		Equip,
		Practice,
		PartnerList,
		PartnerEquipment,
		Goods,
		Magic,
		StandaloneMagic,
		Memo,
		Bottom,
		Top,
		Map
	};
	enum class ControllerMenuHideMode
	{
		LifecycleClose,
		TransferFocusSwitch
	};
	struct ControllerMenuOwner
	{
		PElement element;
		ControllerFocusParticipant* focusParticipant = nullptr;
		ControllerTransferParticipant* transferParticipant = nullptr;

		template<typename OwnerType>
		static ControllerMenuOwner from(
			const std::shared_ptr<OwnerType>& owner)
		{
			static_assert(std::is_base_of_v<Element, OwnerType>,
				"controller menu owners must derive from Element");
			ControllerMenuOwner result;
			result.element = std::static_pointer_cast<Element>(owner);
			if constexpr (std::is_base_of_v<
				ControllerFocusParticipant, OwnerType>)
			{
				result.focusParticipant = owner.get();
			}
			if constexpr (std::is_base_of_v<
				ControllerTransferParticipant, OwnerType>)
			{
				result.transferParticipant = owner.get();
			}
			return result;
		}

		bool matches(const PElement& other) const
		{
			return element == other;
		}
	};
	using ControllerMenuOwnerResolver =
		std::function<ControllerMenuOwner()>;
	struct ControllerMenuOwnerRegistration
	{
		ControllerMenuOwnerId id = ControllerMenuOwnerId::None;
		ControllerMenuOwnerResolver resolve;
	};
	struct VisibleControllerFocusCandidate
	{
		ControllerMenuOwner owner;
		ControllerMenuRole role = ControllerMenuRole::None;
		PElement element;
	};
	struct ControllerMenuDescriptor
	{
		ControllerMenuRole role = ControllerMenuRole::None;
		ControllerMenuGroup group = ControllerMenuGroup::None;
		ControllerFocusTarget focusTarget = ControllerFocusTarget::Default;
		std::vector<ControllerPromptItem> prompts;
		// Semantic owner may change with the active resource layout.
		ControllerMenuOwnerId ownerId = ControllerMenuOwnerId::None;
		// Fixed surface used for same-group conflict hiding. Defaults to owner.
		ControllerMenuOwnerId groupOwnerId = ControllerMenuOwnerId::None;
		// Optional additional conflict group selected by the active layout.
		std::function<ControllerMenuGroup()> resolveAdditionalConflictGroup;
		bool lifecycleUsesAdditionalConflictGroup = true;
		std::function<bool()> isAvailable;
		// Business refresh and cleanup only; the executor owns visibility/focus.
		std::function<void()> prepareOpen;
		std::function<void()> prepareClose;
		// Non-window roles such as partner pages keep explicit lifecycle hooks.
		std::function<bool()> customEnsureOpen;
		std::function<bool()> customClose;
		bool managesVisibility = false;
		ControllerMenuCloseMode closeMode =
			ControllerMenuCloseMode::ResetFocus;
		ControllerMenuPromptMode promptMode =
			ControllerMenuPromptMode::SharedNavigation;
		int fallbackPriority = -1;
		ControllerMenuOwnerId fallbackOwnerId = ControllerMenuOwnerId::None;
		std::function<bool()> isFallbackAvailable;
	};
	struct ControllerTransferDomainDescriptor
	{
		ControllerSlotKind kind = ControllerSlotKind::Goods;
		ControllerSlotDomain domain = ControllerSlotDomain::GoodsBag;
		ControllerMenuOwnerId ownerId = ControllerMenuOwnerId::None;
		ControllerMenuRole role = ControllerMenuRole::None;
		ControllerFocusTarget focusTarget = ControllerFocusTarget::Default;
		std::function<bool()> isAvailable;
		std::function<void()> prepareActivate;
		bool managesMenuVisibility = false;
		bool requiresVisibleOwner = false;
	};

	bool systemMenuOpen = false;
	std::vector<ControllerMenuOwnerRegistration>
		controllerMenuOwnerRegistry;
	std::vector<ControllerMenuDescriptor> controllerMenuDescriptors;
	std::vector<ControllerTransferDomainDescriptor>
		controllerTransferDomainDescriptors;
	ControllerTransferCoordinator controllerTransferCoordinator;
	std::weak_ptr<Element> controllerFocusedMenu;
	ControllerMenuRole controllerFocusedRole = ControllerMenuRole::None;
	std::weak_ptr<Element> controllerMenuBeforeMap;
	ControllerMenuRole controllerRoleBeforeMap = ControllerMenuRole::None;
	std::weak_ptr<Element> controllerMenuBeforePartnerEquipment;
	ControllerMenuRole controllerRoleBeforePartnerEquipment =
		ControllerMenuRole::None;
	std::weak_ptr<Element> controllerMenuBeforeInterfaceHide;
	ControllerMenuRole controllerRoleBeforeInterfaceHide =
		ControllerMenuRole::None;
	bool interfaceHidden = false;
	bool partnerEquipmentReturnPending = false;
	bool stateVisibleBeforePartnerEquipment = false;
	bool equipVisibleBeforePartnerEquipment = false;
	bool practiceVisibleBeforePartnerEquipment = false;
	bool goodsVisibleBeforePartnerEquipment = false;
	bool magicVisibleBeforePartnerEquipment = false;
	bool memoVisibleBeforePartnerEquipment = false;
	struct PointerEventState
	{
		std::weak_ptr<Element> element;
		bool needEvents = true;
	};
	std::vector<PointerEventState> partnerEquipmentPointerEventStates;
	bool partnerEquipmentPointerScopeActive = false;
	unsigned char goodsPriorityBeforePartnerEquipment = epDefault;
	std::set<EventTouchID> ownedPointerTransactions;
	std::uint64_t observedInputLifecycleRevision = 0;
	void synchronizeInputLifecycle();
	virtual void onWindowResize(int width, int height) override;
	void refreshResourceBackedMenuData();
	void cancelWorldInteractionForMenuOpen();
	void releaseSystemMenu(
		std::shared_ptr<System>& system,
		bool menuWasVisible);
	void setPartnerEquipmentPointerScope(bool active);
	void configureControllerMenuOwnerRegistry();
	void configureControllerMenuDescriptors();
	void configureControllerTransferDomainDescriptors();
	const ControllerMenuDescriptor* findControllerMenuDescriptor(
		ControllerMenuRole role) const;
	const ControllerTransferDomainDescriptor*
		findControllerTransferDomainDescriptor(
			ControllerSlotKind kind,
			ControllerSlotDomain domain) const;
	void configureControllerTransferDomains();
	void registerControllerTransferDomain(
		ControllerSlotKind kind,
		ControllerSlotDomain domain);
	bool activateControllerTransferDomain(
		ControllerSlotKind kind,
		ControllerSlotDomain domain);
	bool activatePartnerListControllerPage();
	bool usesIntegratedMagicControllerOwner() const;
	ControllerMenuOwner resolveMagicControllerOwner() const;
	ControllerMenuOwner resolveControllerMenuOwner(
		ControllerMenuOwnerId ownerId) const;
	std::vector<ControllerMenuOwner> collectControllerMenuOwners() const;
	ControllerMenuOwner findControllerMenuOwner(
		const PElement& menuElement) const;
	PElement getControllerFocusedMenu();
	void setControllerFocusedMenu(
		const PElement& menuElement, ControllerMenuRole role);
	bool restoreControllerFocus(
		const PElement& menuElement, ControllerMenuRole role);
	bool restoreControllerFocus(
		ControllerMenuOwner owner, ControllerMenuRole role);
	bool isControllerFocusActive(const PElement& menuElement) const;
	bool hasExclusiveControllerSurface() const;
	ControllerMenuRole resolveControllerRole(
		const PElement& menuElement) const;
	ControllerMenuRole resolveControllerRole(
		const ControllerMenuOwner& owner,
		const PElement& controlElement) const;
	std::vector<VisibleControllerFocusCandidate>
		collectVisibleControllerFocusCandidates() const;
	bool focusVisibleControllerCandidate(
		const VisibleControllerFocusCandidate& candidate);
	bool focusInitialVisibleControllerCandidate(
		UIFocusDirection direction);
	bool moveControllerFocusAcrossMenus(
		const PElement& currentMenu,
		UIFocusDirection direction);
	void adoptPointerFocusFromEvent(const AEvent& event);
	bool closeVisibleUnfocusedControllerSurface(
		const PElement& focusedMenu);
	virtual void onPreviewPointerEvent(AEvent& event) override;
	virtual void onAllPointerInteractionsCanceled() override;
	bool ensureControllerMenuOpen(ControllerMenuRole role);
	bool closeControllerMenuRole(ControllerMenuRole role);
	bool closeControllerMenu(const PElement& menuElement);
	bool switchControllerMenu(int direction);
	bool toggleControllerMenuRole(ControllerMenuRole role);
	ControllerMenuOwner resolveControllerMenuGroupOwner(
		const ControllerMenuDescriptor& descriptor) const;
	std::vector<ControllerMenuGroup> resolveControllerMenuConflictGroups(
		const ControllerMenuDescriptor& descriptor,
		ControllerMenuHideMode mode) const;
	void hideControllerMenuSurface(
		const ControllerMenuDescriptor& descriptor,
		ControllerMenuHideMode mode,
		const PElement& preservedOwner = nullptr);
	void hideControllerMenuGroup(
		ControllerMenuGroup group,
		ControllerMenuHideMode mode =
			ControllerMenuHideMode::LifecycleClose,
		const PElement& preservedOwner = nullptr);
	void updateControllerMenuGroupSelection(
		ControllerMenuGroup group, ControllerMenuRole selectedRole);
	void updateControllerMenuSelection(
		ControllerMenuRole role, bool selected);
	virtual bool onHandleUIAction(UIAction action) override;
	virtual void onDrawEnd() override;
	void updateLeftButtonChecks(bool stateVisible, bool equipVisible, bool practiceVisible);
	void updateRightButtonChecks(bool goodsVisible, bool magicVisible, bool memoVisible);
};
