#include "SlotGridController.h"

#include <algorithm>
#include <utility>

void applySlotGridView(SlotGridBinding& binding, SlotGridView view)
{
	binding.items = std::move(view.items);
	binding.scrollbar = std::move(view.scrollbar);
	binding.resolveLogicalIndex = std::move(view.resolveLogicalIndex);
	binding.refreshAfterScroll = std::move(view.refreshAfterScroll);
}

SlotGridController::SlotGridController()
{
	focusManager.setInputAwarePresentation();
}

SlotGridController::~SlotGridController()
{
	reset();
}

void SlotGridController::bind(SlotGridBinding newBinding)
{
	clear();
	binding = std::move(newBinding);
	focusIds.resize(binding.items.size());
	const int gridColumnCount = columnCount();
	for (std::size_t visibleIndex = 0;
		visibleIndex < binding.items.size(); visibleIndex++)
	{
		if (binding.items[visibleIndex] == nullptr)
		{
			continue;
		}
		const int itemIndex = static_cast<int>(visibleIndex);
		focusIds[visibleIndex] = binding.focusIdPrefix
			+ std::to_string(visibleIndex);
		focusManager.addNode(
			focusIds[visibleIndex],
			binding.items[visibleIndex],
			{ binding.focusIdPrefix,
				itemIndex / gridColumnCount,
				itemIndex % gridColumnCount },
			[this, itemIndex]() { invoke(binding.primary, itemIndex); },
			[this, itemIndex]() { invoke(binding.secondary, itemIndex); },
			[this, itemIndex](UIFocusDirection direction)
			{
				return navigate(itemIndex, direction);
			});
		focusManager.setDetailsHandler(
			focusIds[visibleIndex],
			[this, itemIndex]() { invoke(binding.details, itemIndex); });
	}
	for (const auto& focusId : focusIds)
	{
		if (!focusId.empty())
		{
			focusManager.setDefaultFocus(focusId);
			break;
		}
	}
	if (binding.scrollbar != nullptr)
	{
		focusManager.setPagePreviousHandler([this]()
		{
			const int visibleRows = std::max(1,
				(static_cast<int>(binding.items.size()) + columnCount() - 1)
				/ columnCount());
			scrollRows(-visibleRows);
		});
		focusManager.setPageNextHandler([this]()
		{
			const int visibleRows = std::max(1,
				(static_cast<int>(binding.items.size()) + columnCount() - 1)
				/ columnCount());
			scrollRows(visibleRows);
		});
	}
	restoreBindingState();
}

void SlotGridController::clear()
{
	preserveBindingState();
	focusManager.clear();
	binding = SlotGridBinding();
	focusIds.clear();
	active = false;
}

void SlotGridController::reset()
{
	focusManager.clear();
	binding = SlotGridBinding();
	focusIds.clear();
	active = false;
	restoreFocusAfterBind = false;
	preservedVisibleIndex = -1;
	preservedLogicalIndex = -1;
	preservedScrollPosition = -1;
}

bool SlotGridController::handleAction(UIAction action)
{
	if (!active)
	{
		return false;
	}
	if ((action == UIAction::NavigateUp
		|| action == UIAction::NavigateDown
		|| action == UIAction::NavigateLeft
		|| action == UIAction::NavigateRight)
		&& binding.hideDetails)
	{
		binding.hideDetails();
	}
	return focusManager.handleAction(action);
}

bool SlotGridController::activate()
{
	active = true;
	focusManager.prepareForSemanticActivation();
	if (restoreFocusAfterBind)
	{
		restoreFocusAfterBind = false;
		if ((preservedLogicalIndex >= 0
				&& focusLogicalIndex(preservedLogicalIndex))
			|| focusVisibleIndex(preservedVisibleIndex))
		{
			return true;
		}
	}
	if (focusManager.restoreFocus())
	{
		return true;
	}
	active = false;
	return false;
}

void SlotGridController::deactivate()
{
	active = false;
	focusManager.suspendFocus();
}

bool SlotGridController::isActive() const
{
	return active
		&& isUIFocusElementAvailable(focusManager.getFocusedElement());
}

PElement SlotGridController::controllerFocusedElement() const
{
	return focusManager.getFocusedElement();
}

std::vector<PElement> SlotGridController::controllerFocusCandidates() const
{
	return focusManager.getAvailableFocusElements();
}

bool SlotGridController::focusControllerElement(const PElement& element)
{
	return (active || activate()) && focusManager.focusElement(element);
}

int SlotGridController::focusedVisibleIndex() const
{
	const std::string focusedId = focusManager.getFocusedNodeId();
	for (std::size_t visibleIndex = 0;
		visibleIndex < focusIds.size(); visibleIndex++)
	{
		if (!focusIds[visibleIndex].empty()
			&& focusIds[visibleIndex] == focusedId)
		{
			return static_cast<int>(visibleIndex);
		}
	}
	return -1;
}

int SlotGridController::focusedLogicalIndex() const
{
	return resolveLogicalIndex(focusedVisibleIndex());
}

bool SlotGridController::focusLogicalIndex(int logicalIndex)
{
	for (std::size_t visibleIndex = 0;
		visibleIndex < focusIds.size(); visibleIndex++)
	{
		if (!focusIds[visibleIndex].empty()
			&& resolveLogicalIndex(static_cast<int>(visibleIndex)) == logicalIndex)
		{
			return focusManager.focusNode(focusIds[visibleIndex]);
		}
	}
	return false;
}

void SlotGridController::setNeighbour(
	int fromVisibleIndex,
	UIFocusDirection direction,
	int toVisibleIndex)
{
	if (fromVisibleIndex < 0 || toVisibleIndex < 0
		|| fromVisibleIndex >= static_cast<int>(focusIds.size())
		|| toVisibleIndex >= static_cast<int>(focusIds.size())
		|| focusIds[fromVisibleIndex].empty()
		|| focusIds[toVisibleIndex].empty())
	{
		return;
	}
	focusManager.setNeighbour(
		focusIds[fromVisibleIndex], direction, focusIds[toVisibleIndex]);
}

void SlotGridController::refreshSelection(
	const std::function<bool(int logicalIndex)>& selected)
{
	for (std::size_t visibleIndex = 0;
		visibleIndex < binding.items.size(); visibleIndex++)
	{
		if (binding.items[visibleIndex] == nullptr)
		{
			continue;
		}
		const int logicalIndex = resolveLogicalIndex(
			static_cast<int>(visibleIndex));
		binding.items[visibleIndex]->setTransferSelected(
			logicalIndex >= 0 && selected && selected(logicalIndex));
	}
}

int SlotGridController::columnCount() const
{
	if (binding.scrollbar != nullptr)
	{
		return std::max(1, binding.scrollbar->lineSize);
	}
	return std::max(1, binding.fixedColumnCount);
}

int SlotGridController::resolveLogicalIndex(int visibleIndex) const
{
	if (visibleIndex < 0
		|| visibleIndex >= static_cast<int>(binding.items.size())
		|| binding.items[visibleIndex] == nullptr
		|| !binding.resolveLogicalIndex)
	{
		return -1;
	}
	return binding.resolveLogicalIndex(visibleIndex);
}

void SlotGridController::preserveBindingState()
{
	const int visibleIndex = focusedVisibleIndex();
	if (visibleIndex >= 0)
	{
		preservedVisibleIndex = visibleIndex;
		preservedLogicalIndex = resolveLogicalIndex(visibleIndex);
		restoreFocusAfterBind = true;
	}
	if (binding.scrollbar != nullptr)
	{
		preservedScrollPosition = binding.scrollbar->position;
	}
}

void SlotGridController::restoreBindingState()
{
	if (preservedScrollPosition < 0 || binding.scrollbar == nullptr)
	{
		return;
	}
	const int previousPosition = binding.scrollbar->position;
	binding.scrollbar->setPosition(preservedScrollPosition);
	if (binding.scrollbar->position != previousPosition
		&& binding.refreshAfterScroll)
	{
		binding.refreshAfterScroll();
	}
}

bool SlotGridController::focusVisibleIndex(int visibleIndex)
{
	return visibleIndex >= 0
		&& visibleIndex < static_cast<int>(focusIds.size())
		&& !focusIds[visibleIndex].empty()
		&& focusManager.focusNode(focusIds[visibleIndex]);
}

bool SlotGridController::navigate(
	int visibleIndex, UIFocusDirection direction)
{
	if (binding.scrollbar == nullptr || visibleIndex < 0
		|| visibleIndex >= static_cast<int>(binding.items.size()))
	{
		return false;
	}
	const int columns = columnCount();
	if (direction == UIFocusDirection::Up && visibleIndex < columns
		&& binding.scrollbar->position > binding.scrollbar->min)
	{
		return scrollRows(-1);
	}
	if (direction == UIFocusDirection::Down
		&& visibleIndex + columns >= static_cast<int>(binding.items.size())
		&& binding.scrollbar->position < binding.scrollbar->max)
	{
		return scrollRows(1);
	}
	return false;
}

bool SlotGridController::scrollRows(int rowDelta)
{
	if (binding.scrollbar == nullptr || rowDelta == 0)
	{
		return false;
	}
	if (binding.hideDetails)
	{
		binding.hideDetails();
	}
	const int previousPosition = binding.scrollbar->position;
	binding.scrollbar->setPosition(previousPosition + rowDelta);
	if (binding.scrollbar->position != previousPosition
		&& binding.refreshAfterScroll)
	{
		binding.refreshAfterScroll();
	}
	return true;
}

void SlotGridController::invoke(
	const SlotGridBinding::SlotAction& action,
	int visibleIndex)
{
	const int logicalIndex = resolveLogicalIndex(visibleIndex);
	if (logicalIndex >= 0 && action)
	{
		action(logicalIndex, visibleIndex);
	}
}

ControllerPaneRouter::ControllerPaneRouter(
	ControllerPaneActionPolicy newActionPolicy) :
	actionPolicy(newActionPolicy)
{
}

void ControllerPaneRouter::registerPane(ControllerPaneDescriptor pane)
{
	if (pane.id < 0)
	{
		return;
	}
	const int existingIndex = findPaneIndex(pane.id);
	if (existingIndex < 0)
	{
		panes.push_back(std::move(pane));
		return;
	}
	if (active && currentPaneId == pane.id)
	{
		if (panes[existingIndex].deactivate)
		{
			panes[existingIndex].deactivate();
		}
		active = false;
	}
	panes[existingIndex] = std::move(pane);
}

void ControllerPaneRouter::registerTargetPane(
	int paneId,
	ControllerActionTarget& target,
	std::function<bool()> isAvailable,
	std::function<void()> beforeActivate)
{
	ControllerPaneDescriptor pane;
	pane.id = paneId;
	pane.isAvailable = std::move(isAvailable);
	pane.activate = [&target, beforeActivate = std::move(beforeActivate)]()
	{
		if (beforeActivate)
		{
			beforeActivate();
		}
		return target.activate();
	};
	pane.deactivate = [&target]()
	{
		target.deactivate();
	};
	pane.handleAction = [&target](UIAction action)
	{
		return target.handleAction(action);
	};
	pane.controllerFocusedElement = [&target]()
	{
		return target.controllerFocusedElement();
	};
	pane.controllerFocusCandidates = [&target]()
	{
		return target.controllerFocusCandidates();
	};
	pane.focusControllerElement = [&target](const PElement& element)
	{
		return target.focusControllerElement(element);
	};
	registerPane(std::move(pane));
}

void ControllerPaneRouter::setDefaultPane(int paneId)
{
	defaultPaneId = paneId;
}

bool ControllerPaneRouter::activateDefaultPane()
{
	if (defaultPaneId >= 0 && activatePane(defaultPaneId))
	{
		return true;
	}
	for (const auto& pane : panes)
	{
		if (pane.id != defaultPaneId && activatePane(pane.id))
		{
			return true;
		}
	}
	return false;
}

bool ControllerPaneRouter::activatePane(int paneId)
{
	const int targetIndex = findPaneIndex(paneId);
	if (targetIndex < 0 || !isPaneAvailable(panes[targetIndex]))
	{
		return false;
	}
	if (active && currentPaneId == paneId)
	{
		if (panes[targetIndex].activate && panes[targetIndex].activate())
		{
			return true;
		}
		if (panes[targetIndex].deactivate)
		{
			panes[targetIndex].deactivate();
		}
		active = false;
		return false;
	}

	const int previousPaneId = currentPaneId;
	const bool previousPaneWasActive = active;
	if (previousPaneWasActive)
	{
		const int previousIndex = findPaneIndex(previousPaneId);
		if (previousIndex >= 0 && panes[previousIndex].deactivate)
		{
			panes[previousIndex].deactivate();
		}
		active = false;
	}

	if (panes[targetIndex].activate && panes[targetIndex].activate())
	{
		currentPaneId = paneId;
		active = true;
		return true;
	}
	if (panes[targetIndex].deactivate)
	{
		panes[targetIndex].deactivate();
	}

	currentPaneId = previousPaneId;
	if (previousPaneWasActive)
	{
		const int previousIndex = findPaneIndex(previousPaneId);
		if (previousIndex >= 0
			&& isPaneAvailable(panes[previousIndex])
			&& panes[previousIndex].activate)
		{
			if (panes[previousIndex].activate())
			{
				active = true;
			}
			else if (panes[previousIndex].deactivate)
			{
				panes[previousIndex].deactivate();
			}
		}
	}
	return false;
}

bool ControllerPaneRouter::cyclePane(int direction)
{
	if (direction == 0 || panes.size() < 2)
	{
		return false;
	}
	const int currentIndex = findPaneIndex(currentPaneId);
	if (currentIndex < 0)
	{
		return activateDefaultPane();
	}
	const int step = direction < 0 ? -1 : 1;
	const int paneCount = static_cast<int>(panes.size());
	for (int offset = 1; offset < paneCount; offset++)
	{
		const int candidateIndex =
			(currentIndex + step * offset + paneCount) % paneCount;
		if (!isPaneAvailable(panes[candidateIndex]))
		{
			continue;
		}
		if (activatePane(panes[candidateIndex].id))
		{
			return true;
		}
	}
	return false;
}

bool ControllerPaneRouter::activate()
{
	return resume();
}

void ControllerPaneRouter::deactivate()
{
	suspend();
}

bool ControllerPaneRouter::handleAction(UIAction action)
{
	if ((action == UIAction::PanelPrevious
		|| action == UIAction::PanelNext)
		&& actionPolicy == ControllerPaneActionPolicy::PassThrough)
	{
		return false;
	}
	if (active && !isActive())
	{
		suspend();
	}
	if (!active && !resume())
	{
		return false;
	}
	if (action == UIAction::PanelPrevious
		|| action == UIAction::PanelNext)
	{
		cyclePane(action == UIAction::PanelPrevious ? -1 : 1);
		return active;
	}
	const int currentIndex = findPaneIndex(currentPaneId);
	return currentIndex >= 0 && panes[currentIndex].handleAction
		? panes[currentIndex].handleAction(action)
		: false;
}

PElement ControllerPaneRouter::controllerFocusedElement() const
{
	const int currentIndex = findPaneIndex(currentPaneId);
	if (!isActive() || currentIndex < 0
		|| !isPaneAvailable(panes[currentIndex])
		|| !panes[currentIndex].controllerFocusedElement)
	{
		return nullptr;
	}
	return panes[currentIndex].controllerFocusedElement();
}

std::vector<PElement> ControllerPaneRouter::controllerFocusCandidates() const
{
	std::vector<PElement> candidates;
	for (const ControllerPaneDescriptor& pane : panes)
	{
		if (!isPaneAvailable(pane) || !pane.controllerFocusCandidates)
		{
			continue;
		}
		for (const PElement& element : pane.controllerFocusCandidates())
		{
			if (element != nullptr
				&& std::find(candidates.begin(), candidates.end(), element)
					== candidates.end())
			{
				candidates.push_back(element);
			}
		}
	}
	return candidates;
}

bool ControllerPaneRouter::focusControllerElement(const PElement& element)
{
	if (element == nullptr)
	{
		return false;
	}
	for (const ControllerPaneDescriptor& pane : panes)
	{
		if (!isPaneAvailable(pane) || !pane.controllerFocusCandidates
			|| !pane.focusControllerElement)
		{
			continue;
		}
		const std::vector<PElement> candidates =
			pane.controllerFocusCandidates();
		if (std::find(candidates.begin(), candidates.end(), element)
			== candidates.end())
		{
			continue;
		}
		if (!activatePane(pane.id))
		{
			return false;
		}
		return pane.focusControllerElement(element);
	}
	return false;
}

void ControllerPaneRouter::suspend()
{
	if (!active)
	{
		return;
	}
	const int currentIndex = findPaneIndex(currentPaneId);
	if (currentIndex >= 0 && panes[currentIndex].deactivate)
	{
		panes[currentIndex].deactivate();
	}
	active = false;
}

bool ControllerPaneRouter::resume()
{
	if (isActive())
	{
		return true;
	}
	if (currentPaneId >= 0 && activatePane(currentPaneId))
	{
		return true;
	}
	return activateDefaultPane();
}

void ControllerPaneRouter::clear()
{
	suspend();
	panes.clear();
	defaultPaneId = -1;
	currentPaneId = -1;
	active = false;
}

bool ControllerPaneRouter::isActive() const
{
	const int currentIndex = findPaneIndex(currentPaneId);
	if (!active || currentIndex < 0
		|| !isPaneAvailable(panes[currentIndex])
		|| !panes[currentIndex].controllerFocusedElement)
	{
		return false;
	}
	return isUIFocusElementAvailable(
		panes[currentIndex].controllerFocusedElement());
}

bool ControllerPaneRouter::hasSelectedPane() const
{
	return findPaneIndex(currentPaneId) >= 0;
}

int ControllerPaneRouter::selectedPaneId() const
{
	return hasSelectedPane() ? currentPaneId : -1;
}

int ControllerPaneRouter::findPaneIndex(int paneId) const
{
	for (std::size_t paneIndex = 0; paneIndex < panes.size(); paneIndex++)
	{
		if (panes[paneIndex].id == paneId)
		{
			return static_cast<int>(paneIndex);
		}
	}
	return -1;
}

bool ControllerPaneRouter::isPaneAvailable(
	const ControllerPaneDescriptor& pane) const
{
	return pane.activate && (!pane.isAvailable || pane.isAvailable());
}

SlotInteractionController::SlotInteractionController()
{
}

SlotInteractionController::~SlotInteractionController()
{
	reset();
}

void SlotInteractionController::bind(SlotInteractionBinding newBinding)
{
	clear();
	transfers = newBinding.transfers;
	kind = newBinding.kind;
	domain = newBinding.domain;
	resolveContext = std::move(newBinding.resolveContext);
	showMessage = std::move(newBinding.showMessage);
	primary = std::move(newBinding.grid.primary);
	secondary = std::move(newBinding.grid.secondary);
	hideDetails = newBinding.grid.hideDetails;
	newBinding.grid.primary = [this](int logicalIndex, int visibleIndex)
	{
		invokePrimary(logicalIndex, visibleIndex);
	};
	newBinding.grid.secondary = [this](int logicalIndex, int visibleIndex)
	{
		invokeSecondary(logicalIndex, visibleIndex);
	};
	gridController.bind(std::move(newBinding.grid));
	refreshTransferSelection();
}

void SlotInteractionController::clear()
{
	gridController.clear();
	clearInteractionBinding();
}

void SlotInteractionController::reset()
{
	gridController.reset();
	clearInteractionBinding();
}

void SlotInteractionController::clearInteractionBinding()
{
	transfers = nullptr;
	resolveContext = std::function<std::shared_ptr<const void>()>();
	primary = SlotGridBinding::SlotAction();
	secondary = SlotGridBinding::SlotAction();
	hideDetails = std::function<void()>();
	showMessage = std::function<void(const std::string&)>();
}

bool SlotInteractionController::handleAction(UIAction action)
{
	return gridController.handleAction(action);
}

bool SlotInteractionController::activate()
{
	return gridController.activate();
}

void SlotInteractionController::deactivate()
{
	gridController.deactivate();
	if (hideDetails)
	{
		hideDetails();
	}
}

bool SlotInteractionController::isActive() const
{
	return gridController.isActive();
}

PElement SlotInteractionController::controllerFocusedElement() const
{
	return gridController.controllerFocusedElement();
}

std::vector<PElement>
	SlotInteractionController::controllerFocusCandidates() const
{
	return gridController.controllerFocusCandidates();
}

bool SlotInteractionController::focusControllerElement(
	const PElement& element)
{
	return gridController.focusControllerElement(element);
}

int SlotInteractionController::focusedVisibleIndex() const
{
	return gridController.focusedVisibleIndex();
}

int SlotInteractionController::focusedLogicalIndex() const
{
	return gridController.focusedLogicalIndex();
}

bool SlotInteractionController::focusLogicalIndex(int logicalIndex)
{
	return gridController.focusLogicalIndex(logicalIndex);
}

void SlotInteractionController::setNeighbour(
	int fromVisibleIndex,
	UIFocusDirection direction,
	int toVisibleIndex)
{
	gridController.setNeighbour(fromVisibleIndex, direction, toVisibleIndex);
}

void SlotInteractionController::refreshTransferSelection()
{
	if (transfers == nullptr)
	{
		gridController.refreshSelection([](int) { return false; });
		return;
	}
	gridController.refreshSelection([this](int logicalIndex)
	{
		return transfers->isSource(makeAddress(logicalIndex));
	});
}

void SlotInteractionController::invokePrimary(
	int logicalIndex, int visibleIndex)
{
	if (transfers != nullptr && transfers->active(kind)
		&& transfers->hasSource())
	{
		interact(logicalIndex);
		return;
	}
	if (primary)
	{
		primary(logicalIndex, visibleIndex);
	}
}

void SlotInteractionController::invokeSecondary(
	int logicalIndex, int visibleIndex)
{
	if (transfers != nullptr)
	{
		interact(logicalIndex);
		return;
	}
	if (secondary)
	{
		secondary(logicalIndex, visibleIndex);
	}
}

bool SlotInteractionController::interact(int logicalIndex)
{
	if (transfers == nullptr || logicalIndex < 0)
	{
		return false;
	}
	if (hideDetails)
	{
		hideDetails();
	}
	std::string message;
	const bool handled = transfers->interact(makeAddress(logicalIndex), message);
	if (!message.empty() && showMessage)
	{
		showMessage(message);
	}
	return handled;
}

ControllerSlotAddress SlotInteractionController::makeAddress(
	int logicalIndex) const
{
	ControllerSlotAddress address;
	address.kind = kind;
	address.domain = domain;
	address.logicalIndex = logicalIndex;
	if (resolveContext)
	{
		address.context = resolveContext();
	}
	return address;
}
