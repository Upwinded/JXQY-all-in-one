#include "UIFocusManager.h"
#include "../../Engine/Engine.h"
#include "../../Component/ConfigDrivenPanel.h"
#include "../../Input/PhysicalInputManager.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>

namespace
{
int rectangleCenterX(const Rect& rectangle)
{
	return rectangle.x + rectangle.w / 2;
}

int rectangleCenterY(const Rect& rectangle)
{
	return rectangle.y + rectangle.h / 2;
}

long long intervalGap(
	long long firstBegin,
	long long firstEnd,
	long long secondBegin,
	long long secondEnd)
{
	if (secondBegin > firstEnd)
	{
		return secondBegin - firstEnd;
	}
	if (firstBegin > secondEnd)
	{
		return firstBegin - secondEnd;
	}
	return 0;
}

}

std::optional<UIFocusSpatialScore> scoreUIFocusSpatialCandidate(
	const Rect& source,
	const Rect& candidate,
	UIFocusDirection direction,
	std::size_t order)
{
	const long long deltaX = static_cast<long long>(
		rectangleCenterX(candidate)) - rectangleCenterX(source);
	const long long deltaY = static_cast<long long>(
		rectangleCenterY(candidate)) - rectangleCenterY(source);
	long long primaryCenterDistance = 0;
	long long secondaryCenterDistance = 0;
	long long primaryEdgeGap = 0;
	long long secondaryEdgeGap = 0;
	const long long sourceLeft = source.x;
	const long long sourceTop = source.y;
	const long long sourceRight = sourceLeft + source.w;
	const long long sourceBottom = sourceTop + source.h;
	const long long candidateLeft = candidate.x;
	const long long candidateTop = candidate.y;
	const long long candidateRight = candidateLeft + candidate.w;
	const long long candidateBottom = candidateTop + candidate.h;
	switch (direction)
	{
	case UIFocusDirection::Up:
		primaryCenterDistance = -deltaY;
		secondaryCenterDistance = std::abs(deltaX);
		primaryEdgeGap = std::max(0LL, sourceTop - candidateBottom);
		secondaryEdgeGap = intervalGap(
			sourceLeft, sourceRight, candidateLeft, candidateRight);
		break;
	case UIFocusDirection::Down:
		primaryCenterDistance = deltaY;
		secondaryCenterDistance = std::abs(deltaX);
		primaryEdgeGap = std::max(0LL, candidateTop - sourceBottom);
		secondaryEdgeGap = intervalGap(
			sourceLeft, sourceRight, candidateLeft, candidateRight);
		break;
	case UIFocusDirection::Left:
		primaryCenterDistance = -deltaX;
		secondaryCenterDistance = std::abs(deltaY);
		primaryEdgeGap = std::max(0LL, sourceLeft - candidateRight);
		secondaryEdgeGap = intervalGap(
			sourceTop, sourceBottom, candidateTop, candidateBottom);
		break;
	case UIFocusDirection::Right:
		primaryCenterDistance = deltaX;
		secondaryCenterDistance = std::abs(deltaY);
		primaryEdgeGap = std::max(0LL, candidateLeft - sourceRight);
		secondaryEdgeGap = intervalGap(
			sourceTop, sourceBottom, candidateTop, candidateBottom);
		break;
	}
	if (primaryCenterDistance <= 0)
	{
		return std::nullopt;
	}

	const int directionTier =
		secondaryCenterDistance <= primaryCenterDistance ? 0 : 1;
	const long long edgeDistanceSquared =
		primaryEdgeGap * primaryEdgeGap
		+ secondaryEdgeGap * secondaryEdgeGap;
	const long long centerDistanceSquared =
		deltaX * deltaX + deltaY * deltaY;
	return UIFocusSpatialScore{
		directionTier,
		edgeDistanceSquared,
		secondaryEdgeGap,
		centerDistanceSquared,
		order,
	};
}

std::vector<UIFocusManager*> UIFocusManager::instances;
std::vector<UIFocusManager*> UIFocusManager::presentationOwners;
UIFocusManager::InputSource UIFocusManager::currentInputSource =
	UIFocusManager::InputSource::Unknown;
UIFocusManager::InputSource UIFocusManager::dispatchInputSource =
	UIFocusManager::InputSource::Unknown;

bool mapKeyboardEventToUIAction(
	const AEvent& event,
	UIAction& action,
	KeyboardNavigationKeySet navigationKeys)
{
	if (event.eventType != ET_KEYDOWN)
	{
		return false;
	}

	switch (event.eventData)
	{
	case KEY_UP:
		action = UIAction::NavigateUp;
		return true;
	case KEY_W:
		if (navigationKeys
			== KeyboardNavigationKeySet::DirectionKeysAndWASD)
		{
			action = UIAction::NavigateUp;
			return true;
		}
		return false;
	case KEY_DOWN:
		action = UIAction::NavigateDown;
		return true;
	case KEY_S:
		if (navigationKeys
			== KeyboardNavigationKeySet::DirectionKeysAndWASD)
		{
			action = UIAction::NavigateDown;
			return true;
		}
		return false;
	case KEY_LEFT:
		action = UIAction::NavigateLeft;
		return true;
	case KEY_A:
		if (navigationKeys
			== KeyboardNavigationKeySet::DirectionKeysAndWASD)
		{
			action = UIAction::NavigateLeft;
			return true;
		}
		return false;
	case KEY_RIGHT:
		action = UIAction::NavigateRight;
		return true;
	case KEY_D:
		if (navigationKeys
			== KeyboardNavigationKeySet::DirectionKeysAndWASD)
		{
			action = UIAction::NavigateRight;
			return true;
		}
		return false;
	case KEY_RETURN:
	case KEY_SPACE:
		if (!event.eventRepeat)
		{
			action = UIAction::Confirm;
			return true;
		}
		return false;
	case KEY_ESCAPE:
		if (!event.eventRepeat)
		{
			action = UIAction::Cancel;
			return true;
		}
		return false;
	default:
		return false;
	}
}

bool dispatchKeyboardUIAction(
	const AEvent& event,
	Element& target,
	KeyboardNavigationKeySet navigationKeys)
{
	UIAction action;
	if (!mapKeyboardEventToUIAction(event, action, navigationKeys))
	{
		return false;
	}
	UIFocusManager::setCurrentInputSource(
		UIFocusManager::InputSource::Keyboard);
	UIFocusManager::beginInputDispatch(
		UIFocusManager::InputSource::Keyboard);
	const bool handled = target.handleUIAction(action);
	UIFocusManager::endInputDispatch();
	return handled;
}

bool isPointerTakeoverEvent(const AEvent& event)
{
	if (event.synthetic)
	{
		return false;
	}
	switch (event.eventType)
	{
	case ET_MOUSEMOTION:
	case ET_MOUSEDOWN:
	case ET_MOUSEUP:
	case ET_MOUSEWHEEL:
	case ET_FINGERDOWN:
	case ET_FINGERUP:
	case ET_FINGERMOTION:
	case ET_FINGERCANCEL:
		return true;
	default:
		return false;
	}
}

void notifyUIFocusInputEvent(const AEvent& event, Engine* engine)
{
	if (isPointerTakeoverEvent(event))
	{
		UIFocusManager::setCurrentInputSource(
			UIFocusManager::InputSource::Pointer);
		return;
	}
	if (!event.synthetic && event.eventType == ET_KEYDOWN)
	{
		UIFocusManager::setCurrentInputSource(
			UIFocusManager::InputSource::Keyboard);
	}
	UIFocusManager::synchronizeGamepadAvailability(engine);
}

void resetUIFocusInputPresentation()
{
	UIFocusManager::dispatchInputSource = UIFocusManager::InputSource::Unknown;
	UIFocusManager::setCurrentInputSource(
		UIFocusManager::InputSource::Unknown);
	UIFocusManager::presentationOwners.clear();
}

bool shouldPresentGamepadFocus(Engine* engine)
{
	return engine != nullptr
		&& engine->inputActions().hasActiveGamepad()
		&& UIFocusManager::currentInputSource
			== UIFocusManager::InputSource::Gamepad;
}

bool belongsToCurrentSemanticOwner(const PElement& element)
{
	if (!Element::currentRunOwnerBlocksParentInput())
	{
		return true;
	}
	for (Element* ancestor = element.get(); ancestor != nullptr;
		ancestor = ancestor->parent)
	{
		if (Element::isCurrentRunOwner(ancestor))
		{
			return true;
		}
	}
	return false;
}

bool isUIFocusElementAvailable(const PElement& element)
{
	if (element == nullptr || !element->canDraw
		|| element->rect.w <= 0 || element->rect.h <= 0)
	{
		return false;
	}
	for (Element* ancestor = element.get(); ancestor != nullptr;
		ancestor = ancestor->parent)
	{
		if (!ancestor->visible || !ancestor->activated || !ancestor->canDraw
			|| !ancestor->needEvents)
		{
			return false;
		}
	}

	Engine* engine = Engine::getInstance();
	if (engine != nullptr)
	{
		int windowWidth = 0;
		int windowHeight = 0;
		engine->getWindowSize(windowWidth, windowHeight);
		if (windowWidth > 0 && windowHeight > 0
			&& (static_cast<long long>(element->rect.x) + element->rect.w <= 0
				|| static_cast<long long>(element->rect.y) + element->rect.h <= 0
				|| element->rect.x >= windowWidth
				|| element->rect.y >= windowHeight))
		{
			return false;
		}
	}
	return true;
}

bool adoptUIFocusPointerTarget(EventTouchID pointerID)
{
	UIFocusManager::setCurrentInputSource(
		UIFocusManager::InputSource::Pointer);
	for (auto manager = UIFocusManager::instances.rbegin();
		manager != UIFocusManager::instances.rend(); manager++)
	{
		if (*manager == nullptr)
		{
			continue;
		}
		for (const PElement& element :
			(*manager)->getAvailableFocusElements())
		{
			if (element != nullptr && element->touchingDownID == pointerID)
			{
				return (*manager)->focusElement(element);
			}
		}
	}
	return false;
}

bool dispatchPhysicalUIActions(Engine* engine)
{
	const bool modalInputBarrier = Element::currentRunOwnerBlocksParentInput();
	UIFocusManager::synchronizeGamepadAvailability(engine);
	if (engine == nullptr || !engine->inputActions().hasActiveGamepad())
	{
		return modalInputBarrier;
	}
	for (std::size_t actionIndex = 0;
		actionIndex < GameInput::InputActionCount; actionIndex++)
	{
		const auto& actionState = engine->inputActions().action(
			static_cast<GameInput::InputAction>(actionIndex));
		if (actionState.pressed
			&& actionState.sourceDeviceID > GameInput::UnknownInputDeviceID)
		{
			// Menu-opening actions are handled later by GameController rather than
			// the semantic UI table below. Record the real gamepad activity now so
			// a menu opened in that same frame may present its logical default.
			UIFocusManager::setCurrentInputSource(
				UIFocusManager::InputSource::Gamepad);
			break;
		}
	}

	struct ActionMapping
	{
		GameInput::InputAction inputAction;
		UIAction uiAction;
	};
	static constexpr ActionMapping ActionMappings[] =
	{
		{ GameInput::InputAction::NavigateUp, UIAction::NavigateUp },
		{ GameInput::InputAction::NavigateDown, UIAction::NavigateDown },
		{ GameInput::InputAction::NavigateLeft, UIAction::NavigateLeft },
		{ GameInput::InputAction::NavigateRight, UIAction::NavigateRight },
		{ GameInput::InputAction::Confirm, UIAction::Confirm },
		{ GameInput::InputAction::Cancel, UIAction::Cancel },
		{ GameInput::InputAction::Secondary, UIAction::Secondary },
		{ GameInput::InputAction::ShowDetails, UIAction::Details },
		{ GameInput::InputAction::PreviousPanel, UIAction::PanelPrevious },
		{ GameInput::InputAction::NextPanel, UIAction::PanelNext },
		{ GameInput::InputAction::PreviousPage, UIAction::PagePrevious },
		{ GameInput::InputAction::NextPage, UIAction::PageNext },
		{ GameInput::InputAction::ScrollUp, UIAction::ScrollUp },
		{ GameInput::InputAction::ScrollDown, UIAction::ScrollDown },
		{ GameInput::InputAction::ScrollLeft, UIAction::ScrollLeft },
		{ GameInput::InputAction::ScrollRight, UIAction::ScrollRight },
	};

	std::array<bool, sizeof(ActionMappings) / sizeof(ActionMappings[0])> pressedActions{};
	for (std::size_t index = 0; index < pressedActions.size(); index++)
	{
		pressedActions[index] = engine->inputActions().wasActionPressed(
			ActionMappings[index].inputAction);
	}

	for (std::size_t index = 0; index < pressedActions.size(); index++)
	{
		if (!pressedActions[index])
		{
			continue;
		}
		const auto& mapping = ActionMappings[index];
		UIFocusManager::setCurrentInputSource(
			UIFocusManager::InputSource::Gamepad);
		UIFocusManager::beginInputDispatch(
			UIFocusManager::InputSource::Gamepad);
		const bool handled = Element::dispatchUIAction(mapping.uiAction);
		UIFocusManager::endInputDispatch();
		if (handled)
		{
			engine->consumeInputAction(mapping.inputAction);
			return true;
		}
	}
	return modalInputBarrier;
}

bool dispatchUIActionWithFocusRecovery(
	UIAction action,
	const std::function<bool(UIAction)>& dispatch,
	const std::function<bool()>& isFocusActive,
	const std::function<bool()>& recoverFocus)
{
	if (!dispatch)
	{
		return false;
	}
	if (dispatch(action))
	{
		return true;
	}
	if (!isFocusActive || isFocusActive())
	{
		return false;
	}
	return recoverFocus && recoverFocus() && dispatch(action);
}

UIFocusManager::UIFocusManager()
{
	instances.push_back(this);
}

UIFocusManager::~UIFocusManager()
{
	clear();
	instances.erase(
		std::remove(instances.begin(), instances.end(), this),
		instances.end());
}

void UIFocusManager::setInputAwarePresentation(bool enabled)
{
	if (inputAwarePresentation == enabled)
	{
		return;
	}
	inputAwarePresentation = enabled;
	if (!enabled)
	{
		releaseFocusPresentationOwnership();
		focusPresentationActive = true;
		presentationSource = InputSource::Unknown;
		applyFocusedElementPresentation();
		return;
	}
	releaseFocusPresentationOwnership();
	focusPresentationActive = false;
	presentationSource = currentInputSource;
	applyFocusedElementPresentation();
}

bool UIFocusManager::isFocusPresented() const
{
	return shouldPresentFocus();
}

void UIFocusManager::prepareForSemanticActivation()
{
	// Focus selection below is the ownership boundary. focusNode(),
	// focusDefault(), and restoreFocus() claim presentation only after they
	// have confirmed a concrete available element; a failed activation must
	// not suspend or replace another menu's valid presenter.
}

void UIFocusManager::beginInputDispatch(InputSource source)
{
	dispatchInputSource = source;
}

void UIFocusManager::endInputDispatch()
{
	dispatchInputSource = InputSource::Unknown;
}

void UIFocusManager::setCurrentInputSource(InputSource source)
{
	currentInputSource = source;
	if (source == InputSource::Gamepad)
	{
		return;
	}
	for (UIFocusManager* manager : instances)
	{
		if (manager == nullptr || !manager->inputAwarePresentation)
		{
			continue;
		}
		manager->suspendInputAwarePresentation();
		manager->presentationSource = source;
	}
}

void UIFocusManager::synchronizeGamepadAvailability(Engine* engine)
{
	if (engine != nullptr && engine->inputActions().hasActiveGamepad())
	{
		return;
	}
	if (currentInputSource == InputSource::Gamepad)
	{
		currentInputSource = InputSource::Unknown;
	}
	for (UIFocusManager* manager : instances)
	{
		if (manager == nullptr || !manager->inputAwarePresentation
			|| manager->presentationSource != InputSource::Gamepad)
		{
			continue;
		}
		manager->suspendInputAwarePresentation();
		manager->presentationSource = InputSource::Unknown;
	}
}

void UIFocusManager::activateFocusPresentation(InputSource source)
{
	if (!inputAwarePresentation)
	{
		return;
	}
	currentInputSource = source;
	const PElement focusedElement = getFocusedElement();
	if (source != InputSource::Gamepad
		|| !shouldPresentGamepadFocus(Engine::getInstance())
		|| focusedElement == nullptr
		|| !belongsToCurrentSemanticOwner(focusedElement))
	{
		suspendInputAwarePresentation();
		presentationSource = source;
		return;
	}
	if (!presentationOwners.empty()
		&& presentationOwners.back() != this)
	{
		presentationOwners.back()->suspendInputAwarePresentation();
	}
	presentationOwners.erase(
		std::remove(
			presentationOwners.begin(), presentationOwners.end(), this),
		presentationOwners.end());
	presentationOwners.push_back(this);
	focusPresentationActive = true;
	presentationSource = source;
	applyFocusedElementPresentation();
}

void UIFocusManager::suspendInputAwarePresentation()
{
	if (!inputAwarePresentation)
	{
		return;
	}
	focusPresentationActive = false;
	clearFocusedElement();
}

void UIFocusManager::releaseFocusPresentationOwnership()
{
	const bool wasCurrentOwner = !presentationOwners.empty()
		&& presentationOwners.back() == this;
	presentationOwners.erase(
		std::remove(
			presentationOwners.begin(), presentationOwners.end(), this),
		presentationOwners.end());
	if (wasCurrentOwner)
	{
		restorePreviousFocusPresentationOwner();
	}
}

void UIFocusManager::restorePreviousFocusPresentationOwner()
{
	if (currentInputSource != InputSource::Gamepad
		|| !shouldPresentGamepadFocus(Engine::getInstance()))
	{
		return;
	}
	while (!presentationOwners.empty())
	{
		UIFocusManager* previous = presentationOwners.back();
		if (previous == nullptr
			|| std::find(instances.begin(), instances.end(), previous)
				== instances.end()
			|| !previous->inputAwarePresentation)
		{
			presentationOwners.pop_back();
			continue;
		}
		previous->focusPresentationActive = true;
		previous->presentationSource = InputSource::Gamepad;
		if (previous->restoreFocus())
		{
			return;
		}
		previous->suspendInputAwarePresentation();
		presentationOwners.pop_back();
	}
}

void UIFocusManager::applyFocusedElementPresentation()
{
	if (focusedNodeIndex < 0
		|| focusedNodeIndex >= static_cast<int>(nodes.size()))
	{
		return;
	}
	auto element = nodes[focusedNodeIndex].element.lock();
	if (element != nullptr)
	{
		element->setFocused(shouldPresentFocus());
	}
}

bool UIFocusManager::shouldPresentFocus() const
{
	return !inputAwarePresentation
		|| (focusPresentationActive
			&& presentationSource == InputSource::Gamepad
			&& shouldPresentGamepadFocus(Engine::getInstance()));
}

void UIFocusManager::clear()
{
	releaseFocusPresentationOwnership();
	clearFocusedElement();
	nodes.clear();
	focusedNodeIndex = -1;
	defaultFocusId.clear();
	cancelHandler = ActionHandler();
	panelPreviousHandler = ActionHandler();
	panelNextHandler = ActionHandler();
	pagePreviousHandler = ActionHandler();
	pageNextHandler = ActionHandler();
}

void UIFocusManager::addNode(
	const std::string& id,
	const PElement& element,
	ActionHandler confirm,
	ActionHandler secondary,
	NavigationHandler navigate)
{
	addNode(
		id,
		element,
		NodeLayout(),
		std::move(confirm),
		std::move(secondary),
		std::move(navigate));
}

void UIFocusManager::addNode(
	const std::string& id,
	const PElement& element,
	const NodeLayout& layout,
	ActionHandler confirm,
	ActionHandler secondary,
	NavigationHandler navigate)
{
	if (id.empty() || element == nullptr || findNodeIndex(id) >= 0)
	{
		return;
	}

	Node node;
	node.id = id;
	node.element = element;
	node.layout = layout;
	node.confirm = std::move(confirm);
	node.secondary = std::move(secondary);
	node.navigate = std::move(navigate);
	nodes.push_back(std::move(node));
}

std::vector<std::string> UIFocusManager::addLinearGroup(
	const std::string& groupId,
	UIFocusLinearAxis axis,
	const std::vector<UIFocusNodeBinding>& bindings,
	bool wrap)
{
	std::vector<std::string> registeredIds;
	registeredIds.reserve(bindings.size());
	for (const UIFocusNodeBinding& binding : bindings)
	{
		if (binding.id.empty() || binding.element == nullptr
			|| findNodeIndex(binding.id) >= 0)
		{
			continue;
		}

		const int coordinate = static_cast<int>(registeredIds.size());
		const NodeLayout layout = axis == UIFocusLinearAxis::Horizontal
			? NodeLayout{ groupId, 0, coordinate }
			: NodeLayout{ groupId, coordinate, 0 };
		addNode(
			binding.id,
			binding.element,
			layout,
			binding.confirm,
			binding.secondary,
			binding.navigate);
		if (findNodeIndex(binding.id) >= 0)
		{
			if (binding.details)
			{
				setDetailsHandler(binding.id, binding.details);
			}
			registeredIds.push_back(binding.id);
		}
	}

	if (registeredIds.size() < 2)
	{
		return registeredIds;
	}

	const UIFocusDirection previousDirection =
		axis == UIFocusLinearAxis::Horizontal
		? UIFocusDirection::Left
		: UIFocusDirection::Up;
	const UIFocusDirection nextDirection =
		axis == UIFocusLinearAxis::Horizontal
		? UIFocusDirection::Right
		: UIFocusDirection::Down;
	for (std::size_t index = 0; index < registeredIds.size(); index++)
	{
		if (index > 0)
		{
			setNeighbour(
				registeredIds[index],
				previousDirection,
				registeredIds[index - 1]);
		}
		else if (wrap)
		{
			setNeighbour(
				registeredIds[index],
				previousDirection,
				registeredIds.back());
		}

		if (index + 1 < registeredIds.size())
		{
			setNeighbour(
				registeredIds[index],
				nextDirection,
				registeredIds[index + 1]);
		}
		else if (wrap)
		{
			setNeighbour(
				registeredIds[index],
				nextDirection,
				registeredIds.front());
		}
	}
	return registeredIds;
}

std::vector<std::string> UIFocusManager::addVisualLinearGroup(
	const std::string& groupId,
	UIFocusLinearAxis axis,
	const std::vector<UIFocusNodeBinding>& bindings,
	bool wrap)
{
	std::vector<UIFocusNodeBinding> orderedBindings = bindings;
	std::stable_sort(
		orderedBindings.begin(),
		orderedBindings.end(),
		[axis](const UIFocusNodeBinding& left, const UIFocusNodeBinding& right)
		{
			if (left.element == nullptr || right.element == nullptr)
			{
				return left.element != nullptr && right.element == nullptr;
			}
			const int leftCoordinate = axis == UIFocusLinearAxis::Horizontal
				? rectangleCenterX(left.element->rect)
				: rectangleCenterY(left.element->rect);
			const int rightCoordinate = axis == UIFocusLinearAxis::Horizontal
				? rectangleCenterX(right.element->rect)
				: rectangleCenterY(right.element->rect);
			return leftCoordinate < rightCoordinate;
		});
	return addLinearGroup(groupId, axis, orderedBindings, wrap);
}

std::vector<std::string> UIFocusManager::addVisualSpatialGroup(
	const std::string& groupId,
	const std::vector<UIFocusNodeBinding>& bindings)
{
	std::vector<UIFocusNodeBinding> orderedBindings = bindings;
	std::stable_sort(
		orderedBindings.begin(),
		orderedBindings.end(),
		[](const UIFocusNodeBinding& left, const UIFocusNodeBinding& right)
		{
			if (left.element == nullptr || right.element == nullptr)
			{
				return left.element != nullptr && right.element == nullptr;
			}
			const int leftX = rectangleCenterX(left.element->rect);
			const int rightX = rectangleCenterX(right.element->rect);
			return leftX < rightX;
		});

	int minimumCenterX = std::numeric_limits<int>::max();
	int minimumCenterY = std::numeric_limits<int>::max();
	for (const UIFocusNodeBinding& binding : orderedBindings)
	{
		if (binding.element == nullptr)
		{
			continue;
		}
		minimumCenterX = std::min(
			minimumCenterX, rectangleCenterX(binding.element->rect));
		minimumCenterY = std::min(
			minimumCenterY, rectangleCenterY(binding.element->rect));
	}

	std::vector<std::string> registeredIds;
	registeredIds.reserve(orderedBindings.size());
	for (const UIFocusNodeBinding& binding : orderedBindings)
	{
		if (binding.id.empty() || binding.element == nullptr
			|| findNodeIndex(binding.id) >= 0)
		{
			continue;
		}
		const NodeLayout layout =
		{
			groupId,
			rectangleCenterY(binding.element->rect) - minimumCenterY,
			rectangleCenterX(binding.element->rect) - minimumCenterX,
		};
		addNode(
			binding.id,
			binding.element,
			layout,
			binding.confirm,
			binding.secondary,
			binding.navigate);
		if (findNodeIndex(binding.id) >= 0)
		{
			if (binding.details)
			{
				setDetailsHandler(binding.id, binding.details);
			}
			registeredIds.push_back(binding.id);
		}
	}
	return registeredIds;
}

void UIFocusManager::connectAdjacentRows(
	const std::vector<std::string>& upperIds,
	const std::vector<std::string>& lowerIds)
{
	struct RowNode
	{
		std::string id;
		int centerX = 0;
	};
	auto collectNodes = [this](const std::vector<std::string>& ids)
	{
		std::vector<RowNode> rowNodes;
		rowNodes.reserve(ids.size());
		for (const std::string& id : ids)
		{
			const int nodeIndex = findNodeIndex(id);
			if (nodeIndex < 0)
			{
				continue;
			}
			auto element = nodes[nodeIndex].element.lock();
			if (element != nullptr)
			{
				rowNodes.push_back({ id, rectangleCenterX(element->rect) });
			}
		}
		return rowNodes;
	};
	const std::vector<RowNode> upperNodes = collectNodes(upperIds);
	const std::vector<RowNode> lowerNodes = collectNodes(lowerIds);
	if (upperNodes.empty() || lowerNodes.empty())
	{
		return;
	}
	auto nearestId = [](const RowNode& source, const std::vector<RowNode>& candidates)
	{
		const RowNode* nearest = &candidates.front();
		int nearestDistance = std::abs(source.centerX - nearest->centerX);
		for (std::size_t index = 1; index < candidates.size(); index++)
		{
			const int distance = std::abs(source.centerX - candidates[index].centerX);
			if (distance < nearestDistance)
			{
				nearest = &candidates[index];
				nearestDistance = distance;
			}
		}
		return nearest->id;
	};
	for (const RowNode& upperNode : upperNodes)
	{
		setNeighbour(
			upperNode.id,
			UIFocusDirection::Down,
			nearestId(upperNode, lowerNodes));
	}
	for (const RowNode& lowerNode : lowerNodes)
	{
		setNeighbour(
			lowerNode.id,
			UIFocusDirection::Up,
			nearestId(lowerNode, upperNodes));
	}
}

void UIFocusManager::setNeighbour(
	const std::string& fromId,
	UIFocusDirection direction,
	const std::string& toId)
{
	const int fromIndex = findNodeIndex(fromId);
	if (fromIndex < 0)
	{
		return;
	}
	nodes[fromIndex].neighbours[directionIndex(direction)] = toId;
}

bool UIFocusManager::hasAvailableExplicitNeighbour(
	UIFocusDirection direction) const
{
	if (focusedNodeIndex < 0
		|| focusedNodeIndex >= static_cast<int>(nodes.size())
		|| !isNodeAvailable(nodes[focusedNodeIndex]))
	{
		return false;
	}
	const std::string& neighbourId =
		nodes[focusedNodeIndex].neighbours[directionIndex(direction)];
	const int neighbourIndex = findNodeIndex(neighbourId);
	return !neighbourId.empty() && neighbourIndex >= 0
		&& isNodeAvailable(nodes[neighbourIndex]);
}

void UIFocusManager::applyConfigDrivenFocusNavigation(
	const ConfigDrivenPanel& panel,
	const std::map<std::string, std::string>& componentFocusNodeIds)
{
	const std::vector<ConfigDrivenPanel::ComponentDefinition>& definitions =
		panel.getComponentDefinitions();
	std::map<std::string, std::size_t> componentNameCounts;
	for (const ConfigDrivenPanel::ComponentDefinition& definition : definitions)
	{
		if (!definition.name.empty())
		{
			componentNameCounts[definition.name]++;
		}
	}

	for (const ConfigDrivenPanel::ComponentDefinition& definition : definitions)
	{
		const auto sourceNode = componentFocusNodeIds.find(definition.name);
		const auto sourceCount = componentNameCounts.find(definition.name);
		if (sourceNode == componentFocusNodeIds.end()
			|| sourceNode->second.empty()
			|| sourceCount == componentNameCounts.end()
			|| sourceCount->second != 1
			|| findNodeIndex(sourceNode->second) < 0)
		{
			continue;
		}

		auto applyDirection = [this,
			&componentFocusNodeIds,
			&componentNameCounts,
			&sourceNode](
				UIFocusDirection direction,
				const std::string& targetComponentName)
		{
			if (targetComponentName.empty())
			{
				return;
			}
			const auto targetCount =
				componentNameCounts.find(targetComponentName);
			const auto targetNode =
				componentFocusNodeIds.find(targetComponentName);
			if (targetCount == componentNameCounts.end()
				|| targetCount->second != 1
				|| targetNode == componentFocusNodeIds.end()
				|| targetNode->second.empty()
				|| targetNode->second == sourceNode->second)
			{
				return;
			}
			if (findNodeIndex(targetNode->second) < 0)
			{
				return;
			}
			setNeighbour(
				sourceNode->second,
				direction,
				targetNode->second);
		};

		applyDirection(UIFocusDirection::Up, definition.controllerUp);
		applyDirection(UIFocusDirection::Down, definition.controllerDown);
		applyDirection(UIFocusDirection::Left, definition.controllerLeft);
		applyDirection(UIFocusDirection::Right, definition.controllerRight);
	}
}

void UIFocusManager::setDefaultFocus(const std::string& id)
{
	defaultFocusId = id;
}

void UIFocusManager::setDetailsHandler(const std::string& id, ActionHandler handler)
{
	const int nodeIndex = findNodeIndex(id);
	if (nodeIndex < 0)
	{
		return;
	}
	nodes[nodeIndex].details = std::move(handler);
}

void UIFocusManager::setCancelHandler(ActionHandler handler)
{
	cancelHandler = std::move(handler);
}

void UIFocusManager::setPanelPreviousHandler(ActionHandler handler)
{
	panelPreviousHandler = std::move(handler);
}

void UIFocusManager::setPanelNextHandler(ActionHandler handler)
{
	panelNextHandler = std::move(handler);
}

void UIFocusManager::setPagePreviousHandler(ActionHandler handler)
{
	pagePreviousHandler = std::move(handler);
}

void UIFocusManager::setPageNextHandler(ActionHandler handler)
{
	pageNextHandler = std::move(handler);
}

bool UIFocusManager::focusDefault()
{
	if (!defaultFocusId.empty() && focusNode(defaultFocusId))
	{
		return true;
	}
	for (std::size_t index = 0; index < nodes.size(); index++)
	{
		if (isNodeAvailable(nodes[index]))
		{
			clearFocusedElement();
			focusedNodeIndex = static_cast<int>(index);
			auto element = nodes[index].element.lock();
			if (inputAwarePresentation)
			{
				activateFocusPresentation(currentInputSource);
			}
			else
			{
				element->setFocused(shouldPresentFocus());
			}
			return true;
		}
	}
	return false;
}

bool UIFocusManager::focusNode(const std::string& id)
{
	const int nodeIndex = findNodeIndex(id);
	if (nodeIndex < 0 || !isNodeAvailable(nodes[nodeIndex]))
	{
		return false;
	}

	clearFocusedElement();
	focusedNodeIndex = nodeIndex;
	auto element = nodes[nodeIndex].element.lock();
	if (inputAwarePresentation)
	{
		activateFocusPresentation(currentInputSource);
	}
	else
	{
		element->setFocused(shouldPresentFocus());
	}
	return true;
}

void UIFocusManager::suspendFocus()
{
	releaseFocusPresentationOwnership();
	clearFocusedElement();
	if (inputAwarePresentation)
	{
		focusPresentationActive = false;
		presentationSource = InputSource::Pointer;
	}
}

bool UIFocusManager::restoreFocus()
{
	if (focusedNodeIndex >= 0 && focusedNodeIndex < static_cast<int>(nodes.size())
		&& isNodeAvailable(nodes[focusedNodeIndex]))
	{
		auto element = nodes[focusedNodeIndex].element.lock();
		if (inputAwarePresentation)
		{
			activateFocusPresentation(currentInputSource);
		}
		else
		{
			element->setFocused(shouldPresentFocus());
		}
		return true;
	}
	return recoverUnavailableFocus();
}

bool UIFocusManager::handleAction(UIAction action)
{
	activateFocusPresentation(dispatchInputSource);
	switch (action)
	{
	case UIAction::NavigateUp:
		return moveFocus(UIFocusDirection::Up);
	case UIAction::NavigateDown:
		return moveFocus(UIFocusDirection::Down);
	case UIAction::NavigateLeft:
		return moveFocus(UIFocusDirection::Left);
	case UIAction::NavigateRight:
		return moveFocus(UIFocusDirection::Right);
	case UIAction::Confirm:
	case UIAction::Secondary:
	case UIAction::Details:
		break;
	case UIAction::Cancel:
		if (cancelHandler)
		{
			ActionHandler handler = cancelHandler;
			handler();
			return true;
		}
		return false;
	case UIAction::PanelPrevious:
		if (panelPreviousHandler)
		{
			ActionHandler handler = panelPreviousHandler;
			handler();
			return true;
		}
		return false;
	case UIAction::PanelNext:
		if (panelNextHandler)
		{
			ActionHandler handler = panelNextHandler;
			handler();
			return true;
		}
		return false;
	case UIAction::PagePrevious:
		if (pagePreviousHandler)
		{
			ActionHandler handler = pagePreviousHandler;
			handler();
			return true;
		}
		return false;
	case UIAction::PageNext:
		if (pageNextHandler)
		{
			ActionHandler handler = pageNextHandler;
			handler();
			return true;
		}
		return false;
	case UIAction::ScrollUp:
	case UIAction::ScrollDown:
	case UIAction::ScrollLeft:
	case UIAction::ScrollRight:
		return false;
	}

	if (focusedNodeIndex < 0 || focusedNodeIndex >= static_cast<int>(nodes.size())
		|| !isNodeAvailable(nodes[focusedNodeIndex]))
	{
		if (!recoverUnavailableFocus())
		{
			return false;
		}
	}

	ActionHandler handler;
	if (action == UIAction::Confirm)
	{
		handler = nodes[focusedNodeIndex].confirm;
	}
	else if (action == UIAction::Secondary)
	{
		handler = nodes[focusedNodeIndex].secondary;
	}
	else
	{
		handler = nodes[focusedNodeIndex].details;
	}
	if (!handler)
	{
		return false;
	}
	handler();
	return true;
}

std::string UIFocusManager::getFocusedNodeId() const
{
	if (focusedNodeIndex < 0 || focusedNodeIndex >= static_cast<int>(nodes.size()))
	{
		return "";
	}
	return nodes[focusedNodeIndex].id;
}

PElement UIFocusManager::getFocusedElement() const
{
	if (focusedNodeIndex < 0 || focusedNodeIndex >= static_cast<int>(nodes.size())
		|| !isNodeAvailable(nodes[focusedNodeIndex]))
	{
		return nullptr;
	}
	return nodes[focusedNodeIndex].element.lock();
}

std::vector<PElement> UIFocusManager::getAvailableFocusElements() const
{
	std::vector<PElement> elements;
	elements.reserve(nodes.size());
	for (const Node& node : nodes)
	{
		if (!isNodeAvailable(node))
		{
			continue;
		}
		PElement element = node.element.lock();
		if (element != nullptr
			&& std::find(elements.begin(), elements.end(), element)
				== elements.end())
		{
			elements.push_back(std::move(element));
		}
	}
	return elements;
}

bool UIFocusManager::focusElement(const PElement& element)
{
	if (element == nullptr)
	{
		return false;
	}
	for (std::size_t index = 0; index < nodes.size(); index++)
	{
		if (nodes[index].element.lock() == element)
		{
			return focusNode(nodes[index].id);
		}
	}
	return false;
}

std::size_t UIFocusManager::directionIndex(UIFocusDirection direction)
{
	return static_cast<std::size_t>(direction);
}

bool UIFocusManager::isNodeAvailable(const Node& node) const
{
	return isUIFocusElementAvailable(node.element.lock());
}

int UIFocusManager::findNodeIndex(const std::string& id) const
{
	for (std::size_t index = 0; index < nodes.size(); index++)
	{
		if (nodes[index].id == id)
		{
			return static_cast<int>(index);
		}
	}
	return -1;
}

int UIFocusManager::findNearestAvailableNodeIndex(const Node& anchor) const
{
	if (anchor.layout.groupId.empty()
		|| anchor.layout.row < 0
		|| anchor.layout.column < 0)
	{
		return -1;
	}

	using RecoveryScore = std::tuple<long long, long long, long long, std::size_t>;
	RecoveryScore bestScore =
	{
		std::numeric_limits<long long>::max(),
		std::numeric_limits<long long>::max(),
		std::numeric_limits<long long>::max(),
		std::numeric_limits<std::size_t>::max(),
	};
	int bestIndex = -1;
	for (std::size_t index = 0; index < nodes.size(); index++)
	{
		const Node& candidate = nodes[index];
		if (!isNodeAvailable(candidate)
			|| candidate.layout.groupId != anchor.layout.groupId
			|| candidate.layout.row < 0
			|| candidate.layout.column < 0)
		{
			continue;
		}

		const long long rowDistance = std::abs(
			static_cast<long long>(candidate.layout.row)
				- static_cast<long long>(anchor.layout.row));
		const long long columnDistance = std::abs(
			static_cast<long long>(candidate.layout.column)
				- static_cast<long long>(anchor.layout.column));
		const RecoveryScore score =
		{
			rowDistance + columnDistance,
			rowDistance,
			columnDistance,
			index,
		};
		if (score < bestScore)
		{
			bestScore = score;
			bestIndex = static_cast<int>(index);
		}
	}
	return bestIndex;
}

bool UIFocusManager::recoverUnavailableFocus()
{
	int fallbackNodeIndex = -1;
	if (focusedNodeIndex >= 0
		&& focusedNodeIndex < static_cast<int>(nodes.size()))
	{
		const Node& anchor = nodes[focusedNodeIndex];
		fallbackNodeIndex = findNearestAvailableNodeIndex(anchor);
		clearFocusedElement();
	}
	focusedNodeIndex = -1;
	if (fallbackNodeIndex >= 0)
	{
		return focusNode(nodes[fallbackNodeIndex].id);
	}
	return focusDefault();
}

int UIFocusManager::findSpatialNodeIndex(UIFocusDirection direction) const
{
	if (focusedNodeIndex < 0 || focusedNodeIndex >= static_cast<int>(nodes.size()))
	{
		return -1;
	}
	auto focusedElement = nodes[focusedNodeIndex].element.lock();
	if (focusedElement == nullptr)
	{
		return -1;
	}

	std::optional<UIFocusSpatialScore> bestScore;
	int bestIndex = -1;
	for (std::size_t index = 0; index < nodes.size(); index++)
	{
		if (static_cast<int>(index) == focusedNodeIndex || !isNodeAvailable(nodes[index]))
		{
			continue;
		}
		auto candidate = nodes[index].element.lock();
		const std::optional<UIFocusSpatialScore> score =
			scoreUIFocusSpatialCandidate(
				focusedElement->rect, candidate->rect, direction, index);
		if (!score)
		{
			continue;
		}
		if (!bestScore || *score < *bestScore)
		{
			bestScore = score;
			bestIndex = static_cast<int>(index);
		}
	}
	return bestIndex;
}

bool UIFocusManager::moveFocus(UIFocusDirection direction)
{
	bool focusRecovered = false;
	if (focusedNodeIndex < 0 || focusedNodeIndex >= static_cast<int>(nodes.size())
		|| !isNodeAvailable(nodes[focusedNodeIndex]))
	{
		if (!recoverUnavailableFocus())
		{
			return false;
		}
		focusRecovered = true;
	}

	NavigationHandler navigationHandler = nodes[focusedNodeIndex].navigate;
	if (navigationHandler && navigationHandler(direction))
	{
		return true;
	}
	if (focusedNodeIndex < 0 || focusedNodeIndex >= static_cast<int>(nodes.size())
		|| !isNodeAvailable(nodes[focusedNodeIndex]))
	{
		if (!recoverUnavailableFocus())
		{
			return false;
		}
		focusRecovered = true;
	}

	const std::string explicitNeighbour =
		nodes[focusedNodeIndex].neighbours[directionIndex(direction)];
	if (!explicitNeighbour.empty() && focusNode(explicitNeighbour))
	{
		return true;
	}
	const int spatialNodeIndex = findSpatialNodeIndex(direction);
	return (spatialNodeIndex >= 0 && focusNode(nodes[spatialNodeIndex].id))
		|| focusRecovered;
}

void UIFocusManager::clearFocusedElement()
{
	if (focusedNodeIndex >= 0 && focusedNodeIndex < static_cast<int>(nodes.size()))
	{
		auto element = nodes[focusedNodeIndex].element.lock();
		if (element != nullptr)
		{
			element->setFocused(false);
		}
	}
}
