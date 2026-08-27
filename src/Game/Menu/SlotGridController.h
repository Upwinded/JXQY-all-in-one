#pragma once

#include "ControllerTransferCoordinator.h"
#include "UIFocusManager.h"
#include "../../Component/Component.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class ControllerActionTarget
{
public:
	virtual ~ControllerActionTarget() = default;

	virtual bool activate() = 0;
	virtual void deactivate() = 0;
	virtual bool isActive() const = 0;
	virtual bool handleAction(UIAction action) = 0;
	virtual PElement controllerFocusedElement() const
	{
		return nullptr;
	}
	virtual std::vector<PElement> controllerFocusCandidates() const
	{
		return {};
	}
	virtual bool focusControllerElement(const PElement&)
	{
		return false;
	}
};

struct SlotGridView
{
	std::vector<std::shared_ptr<Item>> items;
	std::shared_ptr<Scrollbar> scrollbar;
	std::function<int(int visibleIndex)> resolveLogicalIndex;
	std::function<void()> refreshAfterScroll;
};

struct SlotGridBinding
{
	using SlotAction = std::function<void(int logicalIndex, int visibleIndex)>;

	std::string focusIdPrefix;
	std::vector<std::shared_ptr<Item>> items;
	std::shared_ptr<Scrollbar> scrollbar;
	int fixedColumnCount = 1;
	std::function<int(int visibleIndex)> resolveLogicalIndex;
	SlotAction primary;
	SlotAction secondary;
	SlotAction details;
	std::function<void()> hideDetails;
	std::function<void()> refreshAfterScroll;
};

void applySlotGridView(SlotGridBinding& binding, SlotGridView view);

class SlotGridController :
	public ControllerActionTarget
{
public:
	SlotGridController();
	virtual ~SlotGridController() override;

	void bind(SlotGridBinding newBinding);
	// Detach the current component binding but retain logical focus and scroll
	// memory for a config-driven rebuild of the same controller owner.
	void clear();
	// Fully discard both the binding and any retained owner state.
	void reset();
	virtual bool handleAction(UIAction action) override;
	virtual bool activate() override;
	virtual void deactivate() override;
	virtual bool isActive() const override;
	virtual PElement controllerFocusedElement() const override;
	virtual std::vector<PElement> controllerFocusCandidates() const override;
	virtual bool focusControllerElement(const PElement& element) override;
	int focusedVisibleIndex() const;
	int focusedLogicalIndex() const;
	bool focusLogicalIndex(int logicalIndex);
	void setNeighbour(
		int fromVisibleIndex,
		UIFocusDirection direction,
		int toVisibleIndex);
	void refreshSelection(
		const std::function<bool(int logicalIndex)>& selected);

private:
	SlotGridBinding binding;
	UIFocusManager focusManager;
	std::vector<std::string> focusIds;
	bool active = false;
	bool restoreFocusAfterBind = false;
	int preservedVisibleIndex = -1;
	int preservedLogicalIndex = -1;
	int preservedScrollPosition = -1;

	int columnCount() const;
	int resolveLogicalIndex(int visibleIndex) const;
	void preserveBindingState();
	void restoreBindingState();
	bool focusVisibleIndex(int visibleIndex);
	bool navigate(int visibleIndex, UIFocusDirection direction);
	bool scrollRows(int rowDelta);
	void invoke(
		const SlotGridBinding::SlotAction& action,
		int visibleIndex);
};

struct ControllerPaneDescriptor
{
	int id = -1;
	std::function<bool()> isAvailable;
	std::function<bool()> activate;
	std::function<void()> deactivate;
	std::function<bool(UIAction)> handleAction;
	std::function<PElement()> controllerFocusedElement;
	std::function<std::vector<PElement>()> controllerFocusCandidates;
	std::function<bool(const PElement&)> focusControllerElement;
};

enum class ControllerPaneActionPolicy
{
	CyclePanes,
	PassThrough
};

class ControllerPaneRouter :
	public ControllerActionTarget
{
public:
	explicit ControllerPaneRouter(
		ControllerPaneActionPolicy actionPolicy =
			ControllerPaneActionPolicy::CyclePanes);
	virtual ~ControllerPaneRouter() override = default;

	void registerPane(ControllerPaneDescriptor pane);
	// The registered target must outlive this registration. Call clear() while
	// targets are still alive when an active target must be deactivated.
	void registerTargetPane(
		int paneId,
		ControllerActionTarget& target,
		std::function<bool()> isAvailable = {},
		std::function<void()> beforeActivate = {});
	void setDefaultPane(int paneId);
	bool activateDefaultPane();
	bool activatePane(int paneId);
	bool cyclePane(int direction);
	virtual bool activate() override;
	virtual void deactivate() override;
	virtual bool handleAction(UIAction action) override;
	virtual PElement controllerFocusedElement() const override;
	virtual std::vector<PElement> controllerFocusCandidates() const override;
	virtual bool focusControllerElement(const PElement& element) override;
	void suspend();
	bool resume();
	void clear();
	virtual bool isActive() const override;
	bool hasSelectedPane() const;
	int selectedPaneId() const;

private:
	std::vector<ControllerPaneDescriptor> panes;
	ControllerPaneActionPolicy actionPolicy =
		ControllerPaneActionPolicy::CyclePanes;
	int defaultPaneId = -1;
	int currentPaneId = -1;
	bool active = false;

	int findPaneIndex(int paneId) const;
	bool isPaneAvailable(const ControllerPaneDescriptor& pane) const;
};

struct SlotInteractionBinding
{
	SlotGridBinding grid;
	ControllerTransferCoordinator* transfers = nullptr;
	ControllerSlotKind kind = ControllerSlotKind::Goods;
	ControllerSlotDomain domain = ControllerSlotDomain::GoodsBag;
	std::function<std::shared_ptr<const void>()> resolveContext;
	std::function<void(const std::string&)> showMessage;
};

class SlotInteractionController :
	public ControllerActionTarget
{
public:
	SlotInteractionController();
	virtual ~SlotInteractionController() override;

	void bind(SlotInteractionBinding newBinding);
	void clear();
	void reset();
	virtual bool handleAction(UIAction action) override;
	virtual bool activate() override;
	virtual void deactivate() override;
	virtual bool isActive() const override;
	virtual PElement controllerFocusedElement() const override;
	virtual std::vector<PElement> controllerFocusCandidates() const override;
	virtual bool focusControllerElement(const PElement& element) override;
	int focusedVisibleIndex() const;
	int focusedLogicalIndex() const;
	bool focusLogicalIndex(int logicalIndex);
	void setNeighbour(
		int fromVisibleIndex,
		UIFocusDirection direction,
		int toVisibleIndex);
	void refreshTransferSelection();

private:
	SlotGridController gridController;
	ControllerTransferCoordinator* transfers = nullptr;
	ControllerSlotKind kind = ControllerSlotKind::Goods;
	ControllerSlotDomain domain = ControllerSlotDomain::GoodsBag;
	std::function<std::shared_ptr<const void>()> resolveContext;
	SlotGridBinding::SlotAction primary;
	SlotGridBinding::SlotAction secondary;
	std::function<void()> hideDetails;
	std::function<void(const std::string&)> showMessage;

	void invokePrimary(int logicalIndex, int visibleIndex);
	void invokeSecondary(int logicalIndex, int visibleIndex);
	ControllerSlotAddress makeAddress(int logicalIndex) const;
	bool interact(int logicalIndex);
	void clearInteractionBinding();
};
