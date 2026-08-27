#pragma once

#include "../../Element/Element.h"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

class ConfigDrivenPanel;

enum class UIAction
{
	NavigateUp,
	NavigateDown,
	NavigateLeft,
	NavigateRight,
	Confirm,
	Cancel,
	Secondary,
	Details,
	PanelPrevious,
	PanelNext,
	PagePrevious,
	PageNext,
	ScrollUp,
	ScrollDown,
	ScrollLeft,
	ScrollRight
};

enum class UIFocusDirection
{
	Up,
	Down,
	Left,
	Right
};

enum class UIFocusLinearAxis
{
	Horizontal,
	Vertical
};

enum class KeyboardNavigationKeySet
{
	DirectionKeysAndWASD,
	DirectionKeysOnly
};

struct UIFocusNodeBinding
{
	std::string id;
	PElement element;
	std::function<void()> confirm;
	std::function<void()> secondary;
	std::function<void()> details;
	std::function<bool(UIFocusDirection)> navigate;
};

using UIFocusSpatialScore =
	std::tuple<int, long long, long long, long long, std::size_t>;

// Returns no score when the candidate is not in the requested centre-based
// half-plane. Candidates inside a 45-degree direction cone always outrank
// off-axis candidates; rectangle-edge and centre distances then break ties.
std::optional<UIFocusSpatialScore> scoreUIFocusSpatialCandidate(
	const Rect& source,
	const Rect& candidate,
	UIFocusDirection direction,
	std::size_t order);

bool mapKeyboardEventToUIAction(
	const AEvent& event,
	UIAction& action,
	KeyboardNavigationKeySet navigationKeys =
		KeyboardNavigationKeySet::DirectionKeysAndWASD);
bool dispatchKeyboardUIAction(
	const AEvent& event,
	Element& target,
	KeyboardNavigationKeySet navigationKeys =
		KeyboardNavigationKeySet::DirectionKeysAndWASD);
bool isPointerTakeoverEvent(const AEvent& event);
// Keeps semantic focus presentation aligned with the most recent real input
// device. Synthetic mouse refresh events do not take focus away from a
// keyboard or gamepad.
void notifyUIFocusInputEvent(const AEvent& event, Engine* engine);
void resetUIFocusInputPresentation();
bool shouldPresentGamepadFocus(Engine* engine);
bool isUIFocusElementAvailable(const PElement& element);
bool adoptUIFocusPointerTarget(EventTouchID pointerID);
bool dispatchPhysicalUIActions(Engine* engine);
bool dispatchUIActionWithFocusRecovery(
	UIAction action,
	const std::function<bool(UIAction)>& dispatch,
	const std::function<bool()>& isFocusActive,
	const std::function<bool()>& recoverFocus);

class UIFocusManager
{
public:
	using ActionHandler = std::function<void()>;
	using NavigationHandler = std::function<bool(UIFocusDirection)>;

	struct NodeLayout
	{
		std::string groupId;
		int row = -1;
		int column = -1;
	};

	struct Node
	{
		std::string id;
		std::weak_ptr<Element> element;
		NodeLayout layout;
		ActionHandler confirm;
		ActionHandler secondary;
		ActionHandler details;
		NavigationHandler navigate;
		std::array<std::string, 4> neighbours;
	};

	UIFocusManager();
	~UIFocusManager();

	// Production menus opt into input-aware presentation. Their logical focus is
	// always retained, while the yellow highlight is shown only after an active
	// gamepad supplies the most recent semantic action.
	void setInputAwarePresentation(bool enabled = true);
	bool isFocusPresented() const;
	// Controller-owner APIs are semantic entry points even when tests or scripts
	// invoke them without a raw key event. A real pointer takeover still wins.
	void prepareForSemanticActivation();
	void clear();
	void addNode(
		const std::string& id,
		const PElement& element,
		ActionHandler confirm = ActionHandler(),
		ActionHandler secondary = ActionHandler(),
		NavigationHandler navigate = NavigationHandler());
	void addNode(
		const std::string& id,
		const PElement& element,
		const NodeLayout& layout,
		ActionHandler confirm = ActionHandler(),
		ActionHandler secondary = ActionHandler(),
		NavigationHandler navigate = NavigationHandler());
	// Null elements and duplicate or empty IDs are structural omissions. Hidden
	// or disabled elements remain registered and use the normal availability
	// and focus-recovery rules.
	std::vector<std::string> addLinearGroup(
		const std::string& groupId,
		UIFocusLinearAxis axis,
		const std::vector<UIFocusNodeBinding>& bindings,
		bool wrap = true);
	// Registers a linear group in the same way as addLinearGroup(), but derives
	// the navigation order from the current element centres. This lets one
	// controller contract follow resource-specific visual layouts without
	// duplicating a hard-coded order for every resource pack.
	std::vector<std::string> addVisualLinearGroup(
		const std::string& groupId,
		UIFocusLinearAxis axis,
		const std::vector<UIFocusNodeBinding>& bindings,
		bool wrap = true);
	// Registers a non-wrapping two-dimensional group. Directional neighbours
	// are resolved from the current element rectangles instead of a fabricated
	// row/column graph, so irregular resource layouts follow what is drawn.
	std::vector<std::string> addVisualSpatialGroup(
		const std::string& groupId,
		const std::vector<UIFocusNodeBinding>& bindings);
	// Connects two visually adjacent horizontal rows. Every node receives an
	// explicit vertical edge to the horizontally nearest node in the other row.
	void connectAdjacentRows(
		const std::vector<std::string>& upperIds,
		const std::vector<std::string>& lowerIds);
	void setNeighbour(
		const std::string& fromId,
		UIFocusDirection direction,
		const std::string& toId);
	bool hasAvailableExplicitNeighbour(
		UIFocusDirection direction) const;
	// Applies optional component-name links from one config-driven menu scope.
	// Only registered sources and unique registered targets override the graph;
	// invalid declarations leave the existing automatic navigation untouched.
	// A target that is temporarily unavailable is skipped by moveFocus(), then
	// becomes reachable through the same explicit edge when it is available.
	void applyConfigDrivenFocusNavigation(
		const ConfigDrivenPanel& panel,
		const std::map<std::string, std::string>&
			componentFocusNodeIds);
	void setDefaultFocus(const std::string& id);
	void setDetailsHandler(const std::string& id, ActionHandler handler);
	void setCancelHandler(ActionHandler handler);
	void setPanelPreviousHandler(ActionHandler handler);
	void setPanelNextHandler(ActionHandler handler);
	void setPagePreviousHandler(ActionHandler handler);
	void setPageNextHandler(ActionHandler handler);

	bool focusDefault();
	bool focusNode(const std::string& id);
	void suspendFocus();
	bool restoreFocus();
	bool handleAction(UIAction action);
	std::string getFocusedNodeId() const;
	PElement getFocusedElement() const;
	std::vector<PElement> getAvailableFocusElements() const;
	bool focusElement(const PElement& element);

private:
	enum class InputSource
	{
		Unknown,
		Keyboard,
		Gamepad,
		Pointer
	};

	friend bool dispatchKeyboardUIAction(
		const AEvent& event,
		Element& target,
		KeyboardNavigationKeySet navigationKeys);
	friend bool dispatchPhysicalUIActions(Engine* engine);
	friend void notifyUIFocusInputEvent(const AEvent& event, Engine* engine);
	friend void resetUIFocusInputPresentation();
	friend bool shouldPresentGamepadFocus(Engine* engine);
	friend bool adoptUIFocusPointerTarget(EventTouchID pointerID);

	static void beginInputDispatch(InputSource source);
	static void endInputDispatch();
	static void setCurrentInputSource(InputSource source);
	static void synchronizeGamepadAvailability(Engine* engine);
	void activateFocusPresentation(InputSource source);
	void suspendInputAwarePresentation();
	void releaseFocusPresentationOwnership();
	static void restorePreviousFocusPresentationOwner();
	void applyFocusedElementPresentation();
	bool shouldPresentFocus() const;
	static std::size_t directionIndex(UIFocusDirection direction);
	bool isNodeAvailable(const Node& node) const;
	int findNodeIndex(const std::string& id) const;
	int findNearestAvailableNodeIndex(const Node& anchor) const;
	int findSpatialNodeIndex(UIFocusDirection direction) const;
	bool recoverUnavailableFocus();
	bool moveFocus(UIFocusDirection direction);
	void clearFocusedElement();

	std::vector<Node> nodes;
	int focusedNodeIndex = -1;
	std::string defaultFocusId;
	ActionHandler cancelHandler;
	ActionHandler panelPreviousHandler;
	ActionHandler panelNextHandler;
	ActionHandler pagePreviousHandler;
	ActionHandler pageNextHandler;
	bool inputAwarePresentation = false;
	bool focusPresentationActive = true;
	InputSource presentationSource = InputSource::Unknown;

	static std::vector<UIFocusManager*> instances;
	static std::vector<UIFocusManager*> presentationOwners;
	static InputSource currentInputSource;
	static InputSource dispatchInputSource;
};
