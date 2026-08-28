#include "../Engine/Engine.h"
#include "../File/File.h"
#include "../Game/GameManager/GameManager.h"
#include "../Game/Menu/ChooseMenu.h"
#include "../Game/Menu/MenuSurfaceCatalog.h"
#include "../Game/Menu/Option.h"
#include "../Game/Menu/SaveLoad.h"
#include "../Game/Menu/System.h"
#include "../Game/Menu/YesNo.h"
#include "../Game/Scene/Title.h"
#include "../Resource/ResourceManager.h"
#include "../Resource/ResourceSelectScene.h"
#include "HeadlessPhysicalInputTestHarness.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

class GamepadSurfaceContractTestAccess
{
public:
	static bool prepareResourceSelection(
		ResourceSelectScene& scene, int width, int height)
	{
		scene.freeResource();
		scene.rect = { 0, 0, width, height };
		scene.updateLayout(width, height);
		scene.createControls();
		scene.buildResourceList();
		scene.configureFocus();
		return scene.resourceList != nullptr
			&& scene.exitButton != nullptr
			&& scene.cheatHelpButton != nullptr
			&& scene.checkUpdatesButton != nullptr
			&& scene.saveManagementButton != nullptr;
	}

	static std::string focusedResourceCandidate(
		const ResourceSelectScene& scene)
	{
		const std::string focusId = scene.focusManager.getFocusedNodeId();
		if (focusId != "resource-list")
		{
			return focusId;
		}
		if (scene.resourceList == nullptr)
		{
			return "";
		}
		return "resource-card-"
			+ std::to_string(scene.resourceList->getSelectedIndex());
	}

	static std::vector<std::pair<std::string, PElement>>
		resourceCandidates(const ResourceSelectScene& scene)
	{
		std::vector<std::pair<std::string, PElement>> candidates;
		if (scene.resourceList != nullptr)
		{
			for (const auto& card : scene.resourceList->cards)
			{
				if (card != nullptr)
				{
					candidates.push_back(
					{
						"resource-card-" + std::to_string(card->index),
						card
					});
				}
			}
		}
		candidates.push_back(
			{ "resource-remove", scene.resourceRemoveButton });
		candidates.push_back({ "cheat-help", scene.cheatHelpButton });
		candidates.push_back(
			{ "save-management", scene.saveManagementButton });
		candidates.push_back(
			{ "display-settings", scene.displaySettingsButton });
		candidates.push_back(
			{ "check-updates", scene.checkUpdatesButton });
		candidates.push_back({ "exit", scene.exitButton });
		candidates.push_back(
			{ "enable-external", scene.enableExternalButton });
		for (int linkIndex = 0;
			linkIndex < static_cast<int>(scene.externalLinkButtons.size());
			linkIndex++)
		{
			candidates.push_back(
			{
				"external-link-" + std::to_string(linkIndex),
				scene.externalLinkButtons[linkIndex]
			});
		}
		return candidates;
	}

	static std::vector<std::string> resourceCandidateKeys(
		const ResourceSelectScene& scene)
	{
		std::vector<std::string> keys;
		for (const auto& candidate : resourceCandidates(scene))
		{
			if (candidate.second != nullptr)
			{
				keys.push_back(candidate.first);
			}
		}
		return keys;
	}

	static std::optional<Rect> resourceCandidateRect(
		const ResourceSelectScene& scene,
		const std::string& candidateKey)
	{
		for (const auto& candidate : resourceCandidates(scene))
		{
			if (candidate.first == candidateKey
				&& candidate.second != nullptr)
			{
				return candidate.second->rect;
			}
		}
		return std::nullopt;
	}

	static std::size_t resourceCardCount(
		const ResourceSelectScene& scene)
	{
		return scene.resourceList != nullptr
			? scene.resourceList->cards.size() : 0;
	}

	static bool resourceExitButtonPresent(
		const ResourceSelectScene& scene)
	{
		return scene.exitButton != nullptr;
	}

	static bool prepareOrdinaryChoice(
		ChooseMenu& menu,
		const std::string& message,
		const std::vector<std::string>& options,
		const std::vector<bool>& visibleOptions)
	{
		ChooseMenu::SelectionConfiguration configuration;
		configuration.message = message;
		configuration.options = options;
		configuration.visibleOptions = visibleOptions;
		return menu.prepareSelection(configuration);
	}

	static bool prepareMultipleChoice(
		ChooseMenu& menu,
		const std::string& message,
		const std::vector<std::string>& options,
		const std::vector<bool>& visibleOptions,
		int columnCount,
		int selectionCount)
	{
		ChooseMenu::SelectionConfiguration configuration;
		configuration.message = message;
		configuration.options = options;
		configuration.visibleOptions = visibleOptions;
		configuration.multiple = true;
		configuration.columnCount = columnCount;
		configuration.selectionCount = selectionCount;
		return menu.prepareSelection(configuration);
	}

	static UIFocusManager* choiceFocusManager(ChooseMenu& menu)
	{
		return &menu.focusManager;
	}

	static std::vector<std::pair<std::string, PElement>>
		choiceCandidates(const ChooseMenu& menu)
	{
		std::vector<std::pair<std::string, PElement>> candidates;
		for (const auto& button : menu.choiceButtons)
		{
			if (button != nullptr)
			{
				candidates.push_back(
				{
					"choice-" + std::to_string(button->index),
					button
				});
			}
		}
		candidates.push_back(
			{ "previous-page", menu.previousPageButton });
		candidates.push_back(
			{ "next-page", menu.nextPageButton });
		candidates.push_back(
			{ "clear", menu.multipleClearButton });
		candidates.push_back(
			{ "confirm", menu.multipleConfirmButton });
		return candidates;
	}

	static std::optional<Rect> choiceCandidateRect(
		const ChooseMenu& menu,
		const std::string& candidateKey)
	{
		for (const auto& candidate : choiceCandidates(menu))
		{
			if (candidate.first == candidateKey
				&& candidate.second != nullptr)
			{
				return candidate.second->rect;
			}
		}
		return std::nullopt;
	}

	static int choicePageIndex(const ChooseMenu& menu)
	{
		return menu.currentPageIndex;
	}

	static int choicePageCount(const ChooseMenu& menu)
	{
		return menu.currentPageCount;
	}
};

namespace
{
using MenuSurfaceCatalog::ControllerInteractionKind;
using MenuSurfaceCatalog::SurfaceId;

struct ExpectedControllerPolicy
{
	SurfaceId id;
	const char* key;
	ControllerInteractionKind kind;
};

constexpr std::array<ExpectedControllerPolicy, 36>
	ExpectedControllerPolicies =
{{
	{ SurfaceId::StartupResourceSelect, "startup.resource-select",
		ControllerInteractionKind::FocusGraph },
	{ SurfaceId::StartupTitle, "startup.title",
		ControllerInteractionKind::FocusGraph },
	{ SurfaceId::TitleTeam, "title.team",
		ControllerInteractionKind::ActionOnly },
	{ SurfaceId::VideoPlayback, "video.playback",
		ControllerInteractionKind::ActionOnly },
	{ SurfaceId::ControllerHelp, "controller-help",
		ControllerInteractionKind::ActionOnly },
	{ SurfaceId::System, "system",
		ControllerInteractionKind::FocusGraph },
	{ SurfaceId::Option, "option",
		ControllerInteractionKind::FocusGraph },
	{ SurfaceId::SaveLoadTitle, "save-load.title",
		ControllerInteractionKind::FocusGraph },
	{ SurfaceId::SaveLoadSystem, "save-load.system",
		ControllerInteractionKind::FocusGraph },
	{ SurfaceId::GlobalYesNo, "global.yes-no",
		ControllerInteractionKind::FocusGraph },
	{ SurfaceId::Dialog, "dialog",
		ControllerInteractionKind::ActionOnly },
	{ SurfaceId::Choice, "choice",
		ControllerInteractionKind::FocusGraph },
	{ SurfaceId::BuySell, "buy-sell",
		ControllerInteractionKind::FocusGraph },
	{ SurfaceId::GambleNormal, "gamble.normal",
		ControllerInteractionKind::FocusGraph },
	{ SurfaceId::GambleDice, "gamble.dice",
		ControllerInteractionKind::ActionOnly },
	{ SurfaceId::GambleFish, "gamble.fish",
		ControllerInteractionKind::ActionOnly },
	{ SurfaceId::PartnerEquipment, "partner-equipment",
		ControllerInteractionKind::FocusGraph },
	{ SurfaceId::RpgState, "rpg.state",
		ControllerInteractionKind::Passive },
	{ SurfaceId::RpgEquipment, "rpg.equipment",
		ControllerInteractionKind::FocusGraph },
	{ SurfaceId::RpgPractice, "rpg.practice",
		ControllerInteractionKind::FocusGraph },
	{ SurfaceId::RpgGoods, "rpg.goods",
		ControllerInteractionKind::FocusGraph },
	{ SurfaceId::RpgMagic, "rpg.magic",
		ControllerInteractionKind::FocusGraph },
	{ SurfaceId::RpgMemo, "rpg.memo",
		ControllerInteractionKind::FocusGraph },
	{ SurfaceId::HudBottom, "hud.bottom",
		ControllerInteractionKind::FocusGraph },
	{ SurfaceId::HudTop, "hud.top",
		ControllerInteractionKind::FocusGraph },
	{ SurfaceId::HudColumn, "hud.column",
		ControllerInteractionKind::Passive },
	{ SurfaceId::PartnerHead, "partner-head",
		ControllerInteractionKind::FocusGraph },
	{ SurfaceId::MapThumbnail, "map-thumbnail",
		ControllerInteractionKind::FocusGraph },
	{ SurfaceId::SystemNotice, "system-notice",
		ControllerInteractionKind::Passive },
	{ SurfaceId::Message, "message",
		ControllerInteractionKind::Passive },
	{ SurfaceId::Timer, "timer",
		ControllerInteractionKind::Passive },
	{ SurfaceId::Tooltip, "tooltip",
		ControllerInteractionKind::Passive },
	{ SurfaceId::NpcInfo, "npc-info",
		ControllerInteractionKind::Passive },
	{ SurfaceId::MobileJoystick, "mobile.joystick",
		ControllerInteractionKind::PointerOnly },
	{ SurfaceId::MobileSkills, "mobile.skills",
		ControllerInteractionKind::PointerOnly },
	{ SurfaceId::LoadingTextOverlay, "loading-text-overlay",
		ControllerInteractionKind::Passive }
}};

const char* interactionKindName(ControllerInteractionKind kind)
{
	switch (kind)
	{
	case ControllerInteractionKind::FocusGraph:
		return "FocusGraph";
	case ControllerInteractionKind::ActionOnly:
		return "ActionOnly";
	case ControllerInteractionKind::Passive:
		return "Passive";
	case ControllerInteractionKind::PointerOnly:
		return "PointerOnly";
	}
	return "Unknown";
}

bool checkContract(bool condition, const std::string& message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}

bool hasExpectedInteractionStrategy(
	const MenuSurfaceCatalog::SurfacePolicy& surface,
	ControllerInteractionKind kind)
{
	using MenuSurfaceCatalog::DefaultFocusPolicy;
	using MenuSurfaceCatalog::FocusPolicy;
	using MenuSurfaceCatalog::ModalKind;
	using MenuSurfaceCatalog::SurfaceScope;
	using MenuSurfaceCatalog::WorldPointerPolicy;
	using MenuSurfaceCatalog::WorldSemanticPolicy;

	switch (kind)
	{
	case ControllerInteractionKind::FocusGraph:
		return (surface.focusPolicy == FocusPolicy::Scoped
				|| surface.focusPolicy == FocusPolicy::VisibleNonModalSet)
			&& surface.defaultFocusPolicy != DefaultFocusPolicy::None;
	case ControllerInteractionKind::ActionOnly:
		return surface.modalKind == ModalKind::Modal
			&& surface.worldSemanticPolicy == WorldSemanticPolicy::Block
			&& surface.focusPolicy == FocusPolicy::None
			&& surface.defaultFocusPolicy == DefaultFocusPolicy::None;
	case ControllerInteractionKind::Passive:
		return surface.focusPolicy == FocusPolicy::None
			&& surface.defaultFocusPolicy == DefaultFocusPolicy::None;
	case ControllerInteractionKind::PointerOnly:
		return surface.scope == SurfaceScope::TouchControls
			&& surface.modalKind == ModalKind::NonModal
			&& surface.worldPointerPolicy == WorldPointerPolicy::HitTestOnly
			&& surface.focusPolicy == FocusPolicy::PointerOnly
			&& surface.defaultFocusPolicy == DefaultFocusPolicy::None;
	}
	return false;
}

bool testAllSurfaceClassifications()
{
	bool ok = true;
	ok = checkContract(
		MenuSurfaceCatalog::kSurfacePolicies.size()
				== ExpectedControllerPolicies.size()
			&& static_cast<std::size_t>(SurfaceId::Count)
				== ExpectedControllerPolicies.size(),
		"surface=<catalog> phase=count expected=36 actual="
			+ std::to_string(MenuSurfaceCatalog::kSurfacePolicies.size()))
		&& ok;

	std::array<std::size_t, 4> kindCounts = {};
	std::set<std::string> uniqueKeys;
	for (std::size_t index = 0;
		index < ExpectedControllerPolicies.size();
		index++)
	{
		const ExpectedControllerPolicy& expected =
			ExpectedControllerPolicies[index];
		const auto& surface = MenuSurfaceCatalog::kSurfacePolicies[index];
		const bool rowMatches =
			static_cast<std::size_t>(expected.id) == index
			&& surface.id == expected.id
			&& surface.key == expected.key
			&& surface.controllerInteractionKind == expected.kind
			&& MenuSurfaceCatalog::find(expected.id) == &surface
			&& MenuSurfaceCatalog::find(surface.key) == &surface;
		ok = checkContract(
			rowMatches,
			"surface=" + std::string(expected.key)
				+ " phase=classification expected="
				+ interactionKindName(expected.kind) + " actual="
				+ interactionKindName(surface.controllerInteractionKind)
				+ " index=" + std::to_string(index))
			&& ok;
		ok = checkContract(
			uniqueKeys.insert(std::string(surface.key)).second,
			"surface=" + std::string(surface.key)
				+ " phase=unique-key expected=unique actual=duplicate")
			&& ok;
		ok = checkContract(
			hasExpectedInteractionStrategy(
				surface, surface.controllerInteractionKind),
			"surface=" + std::string(surface.key)
				+ " phase=strategy expected="
				+ interactionKindName(surface.controllerInteractionKind)
				+ " actual=incompatible-surface-policy")
			&& ok;
		const bool expectedKeyboardBarrier =
			surface.modalKind == MenuSurfaceCatalog::ModalKind::Modal
			|| surface.modalKind
				== MenuSurfaceCatalog::ModalKind::RootScene;
		ok = checkContract(
			MenuSurfaceCatalog::blocksWorldKeyboardInput(surface.id)
				== expectedKeyboardBarrier,
			"surface=" + std::string(surface.key)
				+ " phase=keyboard-barrier expected="
				+ (expectedKeyboardBarrier ? "block" : "allow")
				+ " actual="
				+ (MenuSurfaceCatalog::blocksWorldKeyboardInput(surface.id)
					? "block" : "allow"))
			&& ok;
		if (surface.controllerInteractionKind
			!= ControllerInteractionKind::FocusGraph)
		{
			const bool declaresDirectionalFocusGraph =
				surface.focusPolicy
					== MenuSurfaceCatalog::FocusPolicy::Scoped
				|| surface.focusPolicy
					== MenuSurfaceCatalog::FocusPolicy::VisibleNonModalSet
				|| surface.defaultFocusPolicy
					!= MenuSurfaceCatalog::DefaultFocusPolicy::None;
			ok = checkContract(
				!declaresDirectionalFocusGraph,
				"surface=" + std::string(surface.key)
					+ " phase=forbidden-directional-focus"
					+ " expected=none actual=declared")
				&& ok;
		}
		const std::size_t kindIndex =
			static_cast<std::size_t>(surface.controllerInteractionKind);
		if (kindIndex < kindCounts.size())
		{
			kindCounts[kindIndex]++;
		}
	}

	const std::array<std::size_t, 4> expectedKindCounts =
		{ 20, 6, 8, 2 };
	ok = checkContract(
		kindCounts == expectedKindCounts,
		"surface=<catalog> phase=kind-counts expected=20/6/8/2 actual="
			+ std::to_string(kindCounts[0]) + "/"
			+ std::to_string(kindCounts[1]) + "/"
			+ std::to_string(kindCounts[2]) + "/"
			+ std::to_string(kindCounts[3]))
		&& ok;
	const auto loadNpcBinding = std::find_if(
		MenuSurfaceCatalog::kScriptBindings.begin(),
		MenuSurfaceCatalog::kScriptBindings.end(),
		[](const MenuSurfaceCatalog::ScriptBinding& binding)
		{
			return binding.registration == "LoadNpc";
		});
	ok = checkContract(
		loadNpcBinding != MenuSurfaceCatalog::kScriptBindings.end()
			&& loadNpcBinding->policyKeyOrExemption
				== "exempt:no-interface-created",
		"surface=LoadNpc phase=script-binding"
			" expected=no-interface-created actual=loading-overlay")
		&& ok;
	return ok;
}

struct PhysicalButtonStep
{
	const char* label;
	SDL_GamepadButton button;
	GameInput::InputAction inputAction;
	bool observeRelease = false;
};

struct TraversalPathStep
{
	PhysicalButtonStep action;
	std::string expectedTarget;
};

struct PhysicalFocusFixture
{
	PElement owner;
	UIFocusManager* focusManager = nullptr;
	std::vector<std::pair<std::string, PElement>> candidates;
	std::function<std::string()> focusedKeyProvider;
	std::function<std::vector<std::pair<std::string, PElement>>()>
		candidateProvider;
	std::function<std::vector<std::string>()> candidateKeyProvider;
	std::function<std::optional<Rect>(const std::string&)>
		candidateRectProvider;
	std::function<std::string()> stateKeyProvider;

	std::string focusedKey() const
	{
		if (focusedKeyProvider)
		{
			return focusedKeyProvider();
		}
		if (focusManager != nullptr)
		{
			return focusManager->getFocusedNodeId();
		}
		std::string focused;
		for (const auto& candidate : candidates)
		{
			if (candidate.second == nullptr || !candidate.second->isFocused())
			{
				continue;
			}
			if (!focused.empty())
			{
				return "<multiple>";
			}
			focused = candidate.first;
		}
		return focused;
	}

	std::vector<std::string> availableCandidateKeys() const
	{
		if (candidateKeyProvider)
		{
			std::vector<std::string> keys = candidateKeyProvider();
			std::sort(keys.begin(), keys.end());
			return keys;
		}
		std::vector<std::string> keys;
		const std::vector<std::pair<std::string, PElement>>
			currentCandidates = candidateProvider
				? candidateProvider() : candidates;
		for (const auto& candidate : currentCandidates)
		{
			if (isUIFocusElementAvailable(candidate.second))
			{
				keys.push_back(candidate.first);
			}
		}
		std::sort(keys.begin(), keys.end());
		return keys;
	}

	std::string stateKey() const
	{
		return stateKeyProvider ? stateKeyProvider() : "";
	}

	std::optional<Rect> candidateRect(
		const std::string& candidateKey) const
	{
		if (candidateRectProvider)
		{
			return candidateRectProvider(candidateKey);
		}
		const std::vector<std::pair<std::string, PElement>>
			currentCandidates = candidateProvider
				? candidateProvider() : candidates;
		for (const auto& candidate : currentCandidates)
		{
			if (candidate.first == candidateKey
				&& candidate.second != nullptr)
			{
				return candidate.second->rect;
			}
		}
		return std::nullopt;
	}
};

struct PhysicalTraversalSpec
{
	std::string surfaceKey;
	std::string variant;
	std::string expectedDefault;
	std::vector<std::string> expectedCandidates;
	std::vector<PhysicalButtonStep> directions;
	std::function<PhysicalFocusFixture()> createFixture;
	std::string expectedInitialDefault;
	std::vector<TraversalPathStep> setupPath;
	bool assertDirectionalHalfPlane = false;
	std::vector<PhysicalButtonStep> boundaryActions;
};

std::string joinStrings(const std::vector<std::string>& values)
{
	std::ostringstream output;
	for (std::size_t index = 0; index < values.size(); index++)
	{
		if (index > 0)
		{
			output << ',';
		}
		output << values[index];
	}
	return output.str();
}

int rectangleCenterX(const Rect& rectangle)
{
	return rectangle.x + rectangle.w / 2;
}

int rectangleCenterY(const Rect& rectangle)
{
	return rectangle.y + rectangle.h / 2;
}

bool isTargetInDirectionalHalfPlane(
	const Rect& source,
	const Rect& target,
	GameInput::InputAction direction)
{
	switch (direction)
	{
	case GameInput::InputAction::NavigateUp:
		return rectangleCenterY(target) < rectangleCenterY(source);
	case GameInput::InputAction::NavigateDown:
		return rectangleCenterY(target) > rectangleCenterY(source);
	case GameInput::InputAction::NavigateLeft:
		return rectangleCenterX(target) < rectangleCenterX(source);
	case GameInput::InputAction::NavigateRight:
		return rectangleCenterX(target) > rectangleCenterX(source);
	default:
		return false;
	}
}

std::string describeDirectionalCentres(
	const Rect& source,
	const Rect& target)
{
	return "source@("
		+ std::to_string(rectangleCenterX(source))
		+ "," + std::to_string(rectangleCenterY(source))
		+ ")->target@("
		+ std::to_string(rectangleCenterX(target))
		+ "," + std::to_string(rectangleCenterY(target))
		+ ")";
}

std::string describePath(const std::vector<TraversalPathStep>& path)
{
	if (path.empty())
	{
		return "<default>";
	}
	std::ostringstream output;
	for (std::size_t index = 0; index < path.size(); index++)
	{
		if (index > 0)
		{
			output << ',';
		}
		output << path[index].action.label;
	}
	return output.str();
}

bool traversalFailure(
	const PhysicalTraversalSpec& spec,
	const char* phase,
	const std::string& expected,
	const std::string& actual,
	const std::string& source,
	const std::string& action,
	const std::vector<TraversalPathStep>& path)
{
	std::cerr << "FAILED: surface=" << spec.surfaceKey
		<< " variant=" << spec.variant
		<< " phase=" << phase
		<< " source=" << (source.empty() ? "<none>" : source)
		<< " action=" << (action.empty() ? "<none>" : action)
		<< " expected=" << expected
		<< " actual=" << actual
		<< " path=" << describePath(path)
		<< " owner=headless-running-owner\n";
	return false;
}

struct PhysicalTapResult
{
	bool pressed = false;
	bool consumed = false;
};

PhysicalTapResult tapPhysicalButton(
	HeadlessPhysicalInputTest::FrameDriver& frameDriver,
	VirtualGamepadTest::VirtualGamepad& gamepad,
	GameInput::PhysicalInputManager& inputManager,
	const PhysicalButtonStep& step)
{
	PhysicalTapResult result;
	HeadlessPhysicalInputTest::FrameCallbacks callbacks;
	callbacks.afterInputUpdate =
		[&result, &step](
			const GameInput::PhysicalInputManager& frameInputManager)
		{
			result.pressed =
				frameInputManager.wasActionPressed(step.inputAction);
		};
	callbacks.afterDispatch =
		[&result, &inputManager, &step](bool)
		{
			result.consumed =
				!inputManager.wasActionPressed(step.inputAction);
		};
	if (step.observeRelease)
	{
		frameDriver.tapButton(gamepad, step.button, {}, callbacks);
	}
	else
	{
		frameDriver.tapButton(gamepad, step.button, callbacks);
	}
	return result;
}

bool prepareFixtureForPath(
	const PhysicalTraversalSpec& spec,
	const std::vector<TraversalPathStep>& path,
	PhysicalFocusFixture& fixture,
	HeadlessPhysicalInputTest::FrameDriver& frameDriver,
	VirtualGamepadTest::VirtualGamepad& gamepad,
	GameInput::PhysicalInputManager& inputManager)
{
	static const PhysicalButtonStep PresentFocus =
	{
		"Y",
		SDL_GAMEPAD_BUTTON_NORTH,
		GameInput::InputAction::ShowDetails
	};
	const PhysicalTapResult presentationTap = tapPhysicalButton(
		frameDriver, gamepad, inputManager, PresentFocus);
	const std::string presentedDefault = fixture.focusedKey();
	const std::string initialDefault = spec.expectedInitialDefault.empty()
		? spec.expectedDefault : spec.expectedInitialDefault;
	if (!presentationTap.pressed
		|| presentedDefault != initialDefault)
	{
		return traversalFailure(
			spec,
			"default",
			initialDefault,
			presentedDefault,
			"",
			PresentFocus.label,
			{});
	}

	std::vector<TraversalPathStep> completedSetup;
	for (const TraversalPathStep& setupStep : spec.setupPath)
	{
		const std::string source = fixture.focusedKey();
		const PhysicalTapResult tap = tapPhysicalButton(
			frameDriver, gamepad, inputManager, setupStep.action);
		const std::string actualTarget = fixture.focusedKey();
		completedSetup.push_back(setupStep);
		if (!tap.pressed || !tap.consumed
			|| actualTarget != setupStep.expectedTarget)
		{
			return traversalFailure(
				spec,
				"setup",
				setupStep.expectedTarget,
				actualTarget,
				source,
				setupStep.action.label,
				completedSetup);
		}
	}
	if (fixture.focusedKey() != spec.expectedDefault)
	{
		return traversalFailure(
			spec,
			"setup-default",
			spec.expectedDefault,
			fixture.focusedKey(),
			initialDefault,
			"<setup>",
			completedSetup);
	}

	for (const TraversalPathStep& pathStep : path)
	{
		const std::string source = fixture.focusedKey();
		const PhysicalTapResult tap = tapPhysicalButton(
			frameDriver, gamepad, inputManager, pathStep.action);
		const std::string actualTarget = fixture.focusedKey();
		if (!tap.pressed || !tap.consumed
			|| actualTarget != pathStep.expectedTarget)
		{
			return traversalFailure(
				spec,
				"replay",
				pathStep.expectedTarget,
				actualTarget,
				source,
				pathStep.action.label,
				path);
		}
	}
	return true;
}

bool testPhysicalCandidateReachability(
	const PhysicalTraversalSpec& spec,
	HeadlessPhysicalInputTest::FrameDriver& frameDriver,
	VirtualGamepadTest::VirtualGamepad& gamepad,
	GameInput::PhysicalInputManager& inputManager)
{
	std::vector<std::string> expectedCandidates = spec.expectedCandidates;
	std::sort(expectedCandidates.begin(), expectedCandidates.end());
	{
		PhysicalFocusFixture fixture = spec.createFixture();
		if (fixture.owner == nullptr)
		{
			return traversalFailure(
				spec, "entry", "owner", "<null>", "", "", {});
		}
		HeadlessPhysicalInputTest::ScopedRunningOwner runningOwner(
			fixture.owner);
		if (!prepareFixtureForPath(
				spec,
				{},
				fixture,
				frameDriver,
				gamepad,
				inputManager))
		{
			return false;
		}
		const std::vector<std::string> availableCandidates =
			fixture.availableCandidateKeys();
		if (availableCandidates != expectedCandidates)
		{
			return traversalFailure(
				spec,
				"candidate-set",
				joinStrings(expectedCandidates),
				joinStrings(availableCandidates),
				"",
				"",
				{});
		}
		if (fixture.focusManager != nullptr
			&& fixture.focusManager->getAvailableFocusElements().size()
				!= expectedCandidates.size())
		{
			return traversalFailure(
				spec,
				"candidate-count",
				std::to_string(expectedCandidates.size()),
				std::to_string(
					fixture.focusManager
						->getAvailableFocusElements().size()),
				"",
				"",
				{});
		}
	}

	std::map<std::string, std::vector<TraversalPathStep>> paths;
	std::deque<std::string> pending;
	paths.emplace(
		spec.expectedDefault,
		std::vector<TraversalPathStep>());
	pending.push_back(spec.expectedDefault);

	while (!pending.empty())
	{
		const std::string source = pending.front();
		pending.pop_front();
		const std::vector<TraversalPathStep> sourcePath = paths[source];
		for (const PhysicalButtonStep& direction : spec.directions)
		{
			PhysicalFocusFixture fixture = spec.createFixture();
			if (fixture.owner == nullptr)
			{
				return traversalFailure(
					spec, "entry", "owner", "<null>",
					source, direction.label, sourcePath);
			}
			HeadlessPhysicalInputTest::ScopedRunningOwner runningOwner(
				fixture.owner);
			if (!prepareFixtureForPath(
					spec,
					sourcePath,
					fixture,
					frameDriver,
					gamepad,
					inputManager))
			{
				return false;
			}
			if (fixture.focusedKey() != source)
			{
				return traversalFailure(
					spec,
					"source",
					source,
					fixture.focusedKey(),
					source,
					direction.label,
					sourcePath);
			}

			const PhysicalTapResult tap = tapPhysicalButton(
				frameDriver, gamepad, inputManager, direction);
			const std::string target = fixture.focusedKey();
			if (!tap.pressed || target.empty() || target == "<multiple>")
			{
				return traversalFailure(
					spec,
					"edge",
					"single-focused-candidate",
					target.empty() ? "<none>" : target,
					source,
					direction.label,
					sourcePath);
			}
			if (target != source && !tap.consumed)
			{
				return traversalFailure(
					spec,
					"consume",
					"consumed",
					"unconsumed",
					source,
					direction.label,
					sourcePath);
			}
			if (spec.assertDirectionalHalfPlane && target != source)
			{
				const std::optional<Rect> sourceRect =
					fixture.candidateRect(source);
				const std::optional<Rect> targetRect =
					fixture.candidateRect(target);
				if (!sourceRect || !targetRect)
				{
					return traversalFailure(
						spec,
						"spatial-half-plane",
						"source-and-target-real-rects",
						(!sourceRect ? std::string("missing-source:")
							: std::string("missing-target:"))
							+ (!sourceRect ? source : target),
						source,
						direction.label,
						sourcePath);
				}
				if (!isTargetInDirectionalHalfPlane(
						*sourceRect,
						*targetRect,
						direction.inputAction))
				{
					return traversalFailure(
						spec,
						"spatial-half-plane",
						std::string("target-centre-in-") + direction.label
							+ "-half-plane",
						describeDirectionalCentres(
							*sourceRect,
							*targetRect),
						source,
						direction.label,
						sourcePath);
				}
			}
			if (std::find(
					expectedCandidates.begin(),
					expectedCandidates.end(),
					target) == expectedCandidates.end())
			{
				return traversalFailure(
					spec,
					"edge-target",
					joinStrings(expectedCandidates),
					target,
					source,
					direction.label,
					sourcePath);
			}
			if (paths.find(target) == paths.end())
			{
				std::vector<TraversalPathStep> targetPath = sourcePath;
				targetPath.push_back({ direction, target });
				paths.emplace(target, std::move(targetPath));
				pending.push_back(target);
			}
		}
	}

	std::vector<std::string> reachedCandidates;
	for (const auto& entry : paths)
	{
		reachedCandidates.push_back(entry.first);
	}
	if (reachedCandidates != expectedCandidates)
	{
		return traversalFailure(
			spec,
			"reach",
			joinStrings(expectedCandidates),
			joinStrings(reachedCandidates),
			spec.expectedDefault,
			"BFS",
			{});
	}

	for (const PhysicalButtonStep& boundaryAction :
		spec.boundaryActions)
	{
		PhysicalFocusFixture fixture = spec.createFixture();
		if (fixture.owner == nullptr)
		{
			return traversalFailure(
				spec, "entry", "owner", "<null>",
				spec.expectedDefault, boundaryAction.label, {});
		}
		HeadlessPhysicalInputTest::ScopedRunningOwner runningOwner(
			fixture.owner);
		if (!prepareFixtureForPath(
				spec,
				{},
				fixture,
				frameDriver,
				gamepad,
				inputManager))
		{
			return false;
		}
		const std::string focusBefore = fixture.focusedKey();
		const std::string stateBefore = fixture.stateKey();
		const PhysicalTapResult tap = tapPhysicalButton(
			frameDriver, gamepad, inputManager, boundaryAction);
		const std::string focusAfter = fixture.focusedKey();
		const std::string stateAfter = fixture.stateKey();
		if (!tap.pressed || !tap.consumed
			|| focusAfter != focusBefore
			|| stateAfter != stateBefore)
		{
			return traversalFailure(
				spec,
				"boundary",
				focusBefore + "@" + stateBefore,
				focusAfter + "@" + stateAfter,
				focusBefore,
				boundaryAction.label,
				spec.setupPath);
		}
	}
	return true;
}

PElement scrollbarFocusElement(const std::shared_ptr<Scrollbar>& scrollbar)
{
	return scrollbar != nullptr ? PElement(scrollbar->slideBtn) : PElement();
}

PhysicalFocusFixture makeResourceSelectionFixture()
{
	auto scene = std::make_shared<ResourceSelectScene>();
	if (!GamepadSurfaceContractTestAccess::prepareResourceSelection(
			*scene, 800, 600))
	{
		return {};
	}
	scene->setRunning(true);
	PhysicalFocusFixture fixture;
	fixture.owner = scene;
	fixture.focusedKeyProvider =
		[scene]()
		{
			return GamepadSurfaceContractTestAccess::
				focusedResourceCandidate(*scene);
		};
	fixture.candidateProvider =
		[scene]()
		{
			return GamepadSurfaceContractTestAccess::
				resourceCandidates(*scene);
		};
	fixture.candidateKeyProvider =
		[scene]()
		{
			return GamepadSurfaceContractTestAccess::
				resourceCandidateKeys(*scene);
		};
	fixture.candidateRectProvider =
		[scene](const std::string& candidateKey)
		{
			return GamepadSurfaceContractTestAccess::
				resourceCandidateRect(*scene, candidateKey);
		};
	return fixture;
}

PhysicalFocusFixture makeOrdinaryChoiceFixture(
	const std::string& message,
	const std::vector<std::string>& options)
{
	auto menu = std::make_shared<ChooseMenu>();
	if (!GamepadSurfaceContractTestAccess::prepareOrdinaryChoice(
			*menu,
			message,
			options,
			std::vector<bool>(options.size(), true)))
	{
		return {};
	}
	menu->setRunning(true);
	PhysicalFocusFixture fixture;
	fixture.owner = menu;
	fixture.focusManager =
		GamepadSurfaceContractTestAccess::choiceFocusManager(*menu);
	fixture.candidateProvider =
		[menu]()
		{
			return GamepadSurfaceContractTestAccess::
				choiceCandidates(*menu);
		};
	fixture.candidateRectProvider =
		[menu](const std::string& candidateKey)
		{
			return GamepadSurfaceContractTestAccess::
				choiceCandidateRect(*menu, candidateKey);
		};
	fixture.stateKeyProvider =
		[menu]()
		{
			return "page-"
				+ std::to_string(
					GamepadSurfaceContractTestAccess::
						choicePageIndex(*menu))
				+ "-of-"
				+ std::to_string(
					GamepadSurfaceContractTestAccess::
						choicePageCount(*menu));
		};
	return fixture;
}

PhysicalFocusFixture makeMultipleChoiceFixture(
	const std::string& message,
	const std::vector<std::string>& options,
	int columnCount,
	int selectionCount)
{
	auto menu = std::make_shared<ChooseMenu>();
	if (!GamepadSurfaceContractTestAccess::prepareMultipleChoice(
			*menu,
			message,
			options,
			std::vector<bool>(options.size(), true),
			columnCount,
			selectionCount))
	{
		return {};
	}
	menu->setRunning(true);
	PhysicalFocusFixture fixture;
	fixture.owner = menu;
	fixture.focusManager =
		GamepadSurfaceContractTestAccess::choiceFocusManager(*menu);
	fixture.candidateProvider =
		[menu]()
		{
			return GamepadSurfaceContractTestAccess::
				choiceCandidates(*menu);
		};
	fixture.candidateRectProvider =
		[menu](const std::string& candidateKey)
		{
			return GamepadSurfaceContractTestAccess::
				choiceCandidateRect(*menu, candidateKey);
		};
	fixture.stateKeyProvider =
		[menu]()
		{
			return "page-"
				+ std::to_string(
					GamepadSurfaceContractTestAccess::
						choicePageIndex(*menu))
				+ "-of-"
				+ std::to_string(
					GamepadSurfaceContractTestAccess::
						choicePageCount(*menu));
		};
	return fixture;
}

std::vector<std::string> makePaginatedChoiceOptions(int count)
{
	std::vector<std::string> options;
	options.reserve(static_cast<std::size_t>(count));
	for (int index = 0; index < count; index++)
	{
		options.push_back(
			"Physical controller pagination option "
			+ std::to_string(index)
			+ " deliberately uses enough text to keep every page boundary"
				" and footer navigation in the production layout.");
	}
	return options;
}

std::vector<std::string> makeMultipleChoiceOptions()
{
	return
	{
		"Multiple option 0",
		"Multiple option 1",
		"Multiple option 2",
		"Multiple option 3"
	};
}

std::vector<PhysicalTraversalSpec> makePhysicalTraversalSpecs()
{
	const std::vector<PhysicalButtonStep> fourDirections =
	{
		{ "Up", SDL_GAMEPAD_BUTTON_DPAD_UP,
			GameInput::InputAction::NavigateUp },
		{ "Down", SDL_GAMEPAD_BUTTON_DPAD_DOWN,
			GameInput::InputAction::NavigateDown },
		{ "Left", SDL_GAMEPAD_BUTTON_DPAD_LEFT,
			GameInput::InputAction::NavigateLeft },
		{ "Right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
			GameInput::InputAction::NavigateRight }
	};
	const std::vector<PhysicalButtonStep> verticalDirections =
	{
		fourDirections[0],
		fourDirections[1]
	};
	const std::vector<PhysicalButtonStep> horizontalDirections =
	{
		fourDirections[2],
		fourDirections[3]
	};
	const PhysicalButtonStep confirmAction =
	{
		"A",
		SDL_GAMEPAD_BUTTON_SOUTH,
		GameInput::InputAction::Confirm
	};
	const PhysicalButtonStep previousPanelAction =
	{
		"LB",
		SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
		GameInput::InputAction::PreviousPanel,
		true
	};
	const PhysicalButtonStep nextPanelAction =
	{
		"RB",
		SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
		GameInput::InputAction::NextPanel,
		true
	};
	std::vector<std::string> optionCandidates =
	{
		"music",
		"sound",
		"speed",
		"player-alpha",
		"dynamic-loading"
	};
	optionCandidates.push_back("touch-controls");
	optionCandidates.push_back("cheat-settings");

	std::vector<PhysicalTraversalSpec> specs;
	// Rect half-plane checks apply to non-wrapping spatial contracts.
	// Scrolling lists can replace the visible row in place, while Title,
	// System, Option, and SaveLoad intentionally use wrapping linear groups.
	// Validate those surfaces by logical reachability rather than by comparing
	// the before/after screen coordinates.
	specs.push_back(
	{
		"startup.resource-select",
		"11-enabled-packs",
		"resource-card-0",
		{
			"resource-card-0",
			"resource-card-1",
			"resource-card-2",
			"resource-card-3",
			"resource-card-4",
			"resource-card-5",
			"resource-card-6",
			"resource-card-7",
			"resource-card-8",
			"resource-card-9",
			"resource-card-10",
			"resource-remove",
			"cheat-help",
			"save-management",
			"display-settings",
			"check-updates",
			"exit",
#if defined(__ANDROID__) || \
	defined(JXQY_TEST_ANDROID_EXTERNAL_RESOURCE_UI)
			"enable-external",
#endif
			"external-link-0",
			"external-link-1",
			"external-link-2",
			"external-link-3",
		},
		fourDirections,
		[]()
		{
			return makeResourceSelectionFixture();
		},
		{},
		{},
		false
	});
	specs.push_back(
	{
		"startup.title",
		"default",
		"new-game",
		{ "new-game", "load-game", "team", "exit" },
		fourDirections,
		[]()
		{
			auto title = std::make_shared<Title>(true);
			title->init();
			title->setRunning(true);
			return PhysicalFocusFixture
			{
				title,
				nullptr,
				{
					{ "new-game",
						title->getComponentByName<Button>("initBtn") },
					{ "load-game",
						title->getComponentByName<Button>("loadBtn") },
					{ "team",
						title->getComponentByName<Button>("teamBtn") },
					{ "exit",
						title->getComponentByName<Button>("exitBtn") }
				}
			};
		}
	});
	specs.push_back(
	{
		"system",
		"default",
		"return",
		{ "return", "save-load", "options", "return-to-title" },
		fourDirections,
		[]()
		{
			auto system = std::make_shared<System>();
			system->setRunning(true);
			return PhysicalFocusFixture
			{
				system,
				&system->focusManager,
				{
					{ "return", system->returnBtn },
					{ "save-load", system->saveloadBtn },
					{ "options", system->optionBtn },
					{ "return-to-title", system->quitBtn }
				}
			};
		}
	});
	specs.push_back(
	{
		"system",
		"options-entry",
		"options",
		{ "return", "save-load", "options", "return-to-title" },
		fourDirections,
		[]()
		{
			auto system = std::make_shared<System>(true);
			system->setRunning(true);
			return PhysicalFocusFixture
			{
				system,
				&system->focusManager,
				{
					{ "return", system->returnBtn },
					{ "save-load", system->saveloadBtn },
					{ "options", system->optionBtn },
					{ "return-to-title", system->quitBtn }
				}
			};
		}
	});
	specs.push_back(
	{
		"option",
		"JXQY2",
		"music",
		optionCandidates,
		// Horizontal input changes and persists slider values. Reachability only
		// needs the production vertical graph, so this fail-closed test leaves
		// configuration mutation to the isolated option action tests.
		verticalDirections,
		[]()
		{
			auto option = std::make_shared<Option>();
			option->setRunning(true);
			std::vector<std::pair<std::string, PElement>> candidates =
			{
				{ "music", scrollbarFocusElement(option->music) },
				{ "sound", scrollbarFocusElement(option->sound) },
				{ "speed", scrollbarFocusElement(option->speed) },
				{ "player-alpha", option->playerAlpha },
				{ "dynamic-loading", option->dyLoad }
			};
			candidates.push_back(
				{ "touch-controls", option->touchControlsButton });
			candidates.push_back(
				{ "cheat-settings", option->cheatSettingsButton });
			return PhysicalFocusFixture
			{
				option,
				&option->focusManager,
				std::move(candidates)
			};
		}
	});
	specs.push_back(
	{
		"save-load.title",
		"combined",
		"load",
		{ "load", "save", "exit" },
		horizontalDirections,
		[]()
		{
			auto saveLoad = std::make_shared<SaveLoad>(true, true);
			saveLoad->setRunning(true);
			return PhysicalFocusFixture
			{
				saveLoad,
				nullptr,
				{
					{ "load", saveLoad->loadBtn },
					{ "save", saveLoad->saveBtn },
					{ "exit", saveLoad->exitBtn }
				}
			};
		}
	});
	specs.push_back(
	{
		"save-load.system",
		"load-only",
		"load",
		{ "load", "exit" },
		horizontalDirections,
		[]()
		{
			auto saveLoad = std::make_shared<SaveLoad>(false, true);
			saveLoad->setRunning(true);
			return PhysicalFocusFixture
			{
				saveLoad,
				nullptr,
				{
					{ "load", saveLoad->loadBtn },
					{ "save", saveLoad->saveBtn },
					{ "exit", saveLoad->exitBtn }
				}
			};
		}
	});
	specs.push_back(
	{
		"save-load.system",
		"save-only",
		"save",
		{ "save", "exit" },
		horizontalDirections,
		[]()
		{
			auto saveLoad = std::make_shared<SaveLoad>(true, false);
			saveLoad->setRunning(true);
			return PhysicalFocusFixture
			{
				saveLoad,
				nullptr,
				{
					{ "load", saveLoad->loadBtn },
					{ "save", saveLoad->saveBtn },
					{ "exit", saveLoad->exitBtn }
				}
			};
		}
	});
	specs.push_back(
	{
		"global.yes-no",
		"default",
		"no",
		{ "yes", "no" },
		fourDirections,
		[]()
		{
			auto yesNo = std::make_shared<YesNo>(
				"Controller surface contract");
			yesNo->setRunning(true);
			return PhysicalFocusFixture
			{
				yesNo,
				nullptr,
				{
					{ "yes", yesNo->yes },
					{ "no", yesNo->no }
				}
			};
		},
		{},
		{},
		true
	});

	const std::vector<std::string> singlePageOptions =
	{
		"Single page option 0",
		"Single page option 1",
		"Single page option 2",
		"Single page option 3",
		"Single page option 4"
	};
	{
		PhysicalFocusFixture probe = makeOrdinaryChoiceFixture(
			"Single page physical traversal",
			singlePageOptions);
		auto menu = std::dynamic_pointer_cast<ChooseMenu>(probe.owner);
		if (menu == nullptr
			|| GamepadSurfaceContractTestAccess::choicePageCount(*menu) != 1)
		{
			throw std::runtime_error(
				"choice single-page fixture did not remain on one page");
		}
	}
	specs.push_back(
	{
		"choice",
		"single-page",
		"choice-0",
		{
			"choice-0",
			"choice-1",
			"choice-2",
			"choice-3",
			"choice-4"
		},
		fourDirections,
		[singlePageOptions]()
		{
			return makeOrdinaryChoiceFixture(
				"Single page physical traversal",
				singlePageOptions);
		},
		{},
		{},
		true
	});

	const std::vector<std::string> paginatedOptions =
		makePaginatedChoiceOptions(24);
	struct ExpectedPaginatedChoicePage
	{
		std::vector<int> optionIndices;
		std::vector<std::string> footerCandidates;
		std::string defaultFocus;
	};
	const std::array<ExpectedPaginatedChoicePage, 4>
		expectedPaginatedPages =
	{{
		{
			{ 0, 1, 2, 3, 4, 5, 6 },
			{ "next-page" },
			"choice-0"
		},
		{
			{ 7, 8, 9, 10, 11, 12, 13 },
			{ "previous-page", "next-page" },
			"choice-7"
		},
		{
			{ 14, 15, 16, 17, 18, 19, 20 },
			{ "previous-page", "next-page" },
			"choice-14"
		},
		{
			{ 21, 22, 23 },
			{ "previous-page" },
			"choice-21"
		}
	}};
	const std::array<const char*, 4> paginatedPageVariants =
	{{
		"paginated-first-page",
		"paginated-middle-page-1",
		"paginated-middle-page-2",
		"paginated-last-page"
	}};
	PhysicalFocusFixture paginatedProbe = makeOrdinaryChoiceFixture(
		"Paginated physical traversal",
		paginatedOptions);
	auto paginatedProbeMenu =
		std::dynamic_pointer_cast<ChooseMenu>(paginatedProbe.owner);
	if (paginatedProbeMenu == nullptr
		|| GamepadSurfaceContractTestAccess::choicePageCount(
			*paginatedProbeMenu)
			!= static_cast<int>(expectedPaginatedPages.size())
		|| GamepadSurfaceContractTestAccess::choicePageIndex(
			*paginatedProbeMenu) != 0
		|| paginatedProbe.focusedKey()
			!= expectedPaginatedPages.front().defaultFocus)
	{
		throw std::runtime_error(
			"choice pagination fixture violated the explicit four-page"
				" default-focus contract");
	}
	for (std::size_t pageIndex = 0;
		pageIndex < expectedPaginatedPages.size();
		pageIndex++)
	{
		const ExpectedPaginatedChoicePage& expectedPage =
			expectedPaginatedPages[pageIndex];
		PhysicalTraversalSpec pageSpec;
		pageSpec.surfaceKey = "choice";
		pageSpec.variant = paginatedPageVariants[pageIndex];
		pageSpec.expectedDefault = expectedPage.defaultFocus;
		for (int optionIndex : expectedPage.optionIndices)
		{
			pageSpec.expectedCandidates.push_back(
				"choice-" + std::to_string(optionIndex));
		}
		pageSpec.expectedCandidates.insert(
			pageSpec.expectedCandidates.end(),
			expectedPage.footerCandidates.begin(),
			expectedPage.footerCandidates.end());
		pageSpec.directions = fourDirections;
		pageSpec.createFixture =
			[paginatedOptions]()
			{
				return makeOrdinaryChoiceFixture(
					"Paginated physical traversal",
					paginatedOptions);
			};
		pageSpec.expectedInitialDefault = "choice-0";
		pageSpec.assertDirectionalHalfPlane = true;
		for (std::size_t transitionIndex = 1;
			transitionIndex <= pageIndex;
			transitionIndex++)
		{
			pageSpec.setupPath.push_back(
			{
				nextPanelAction,
				expectedPaginatedPages[transitionIndex].defaultFocus
			});
		}
		if (pageIndex == 0)
		{
			pageSpec.boundaryActions.push_back(
				previousPanelAction);
		}
		if (pageIndex + 1 == expectedPaginatedPages.size())
		{
			pageSpec.boundaryActions.push_back(
				nextPanelAction);
		}
		specs.push_back(std::move(pageSpec));
	}

	const std::vector<std::string> multipleOptions =
		makeMultipleChoiceOptions();
	{
		PhysicalFocusFixture probe = makeMultipleChoiceFixture(
			"Multiple footer physical traversal",
			multipleOptions,
			2,
			2);
		auto menu = std::dynamic_pointer_cast<ChooseMenu>(probe.owner);
		if (menu == nullptr
			|| GamepadSurfaceContractTestAccess::choicePageCount(*menu) != 1)
		{
			throw std::runtime_error(
				"choice multiple-footer fixture did not remain on one page");
		}
	}
	PhysicalTraversalSpec multipleSpec;
	multipleSpec.surfaceKey = "choice";
	multipleSpec.variant = "multiple-footer";
	multipleSpec.expectedDefault = "choice-1";
	multipleSpec.expectedCandidates =
	{
		"choice-0",
		"choice-1",
		"choice-2",
		"choice-3",
		"clear",
		"confirm"
	};
	multipleSpec.directions = fourDirections;
	multipleSpec.createFixture =
		[multipleOptions]()
		{
			return makeMultipleChoiceFixture(
				"Multiple footer physical traversal",
				multipleOptions,
				2,
				2);
		};
	multipleSpec.expectedInitialDefault = "choice-0";
	multipleSpec.assertDirectionalHalfPlane = true;
	multipleSpec.setupPath =
	{
		{ confirmAction, "choice-0" },
		{ fourDirections[3], "choice-1" },
		{ confirmAction, "choice-1" }
	};
	specs.push_back(std::move(multipleSpec));
	return specs;
}

bool testPhysicalMultipleChoiceFooterActions(
	HeadlessPhysicalInputTest::FrameDriver& frameDriver,
	VirtualGamepadTest::VirtualGamepad& gamepad,
	GameInput::PhysicalInputManager& inputManager)
{
	const PhysicalButtonStep confirmAction =
	{
		"A",
		SDL_GAMEPAD_BUTTON_SOUTH,
		GameInput::InputAction::Confirm
	};
	const PhysicalButtonStep downAction =
	{
		"Down",
		SDL_GAMEPAD_BUTTON_DPAD_DOWN,
		GameInput::InputAction::NavigateDown
	};
	const PhysicalButtonStep leftAction =
	{
		"Left",
		SDL_GAMEPAD_BUTTON_DPAD_LEFT,
		GameInput::InputAction::NavigateLeft
	};
	const PhysicalButtonStep rightAction =
	{
		"Right",
		SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
		GameInput::InputAction::NavigateRight
	};
	const std::vector<std::string> multipleOptions =
		makeMultipleChoiceOptions();
	PhysicalTraversalSpec actionSpec;
	actionSpec.surfaceKey = "choice";
	actionSpec.variant = "multiple-footer-actions";
	actionSpec.expectedInitialDefault = "choice-0";
	actionSpec.expectedDefault = "choice-1";
	actionSpec.setupPath =
	{
		{ confirmAction, "choice-0" },
		{ rightAction, "choice-1" },
		{ confirmAction, "choice-1" }
	};
	actionSpec.createFixture =
		[multipleOptions]()
		{
			return makeMultipleChoiceFixture(
				"Multiple footer physical actions",
				multipleOptions,
				2,
				2);
		};

	const std::vector<TraversalPathStep> clearPath =
	{
		{ downAction, "choice-3" },
		{ leftAction, "choice-2" },
		{ downAction, "clear" }
	};
	{
		PhysicalFocusFixture fixture = actionSpec.createFixture();
		auto menu = std::dynamic_pointer_cast<ChooseMenu>(fixture.owner);
		if (menu == nullptr)
		{
			return traversalFailure(
				actionSpec,
				"clear-action-entry",
				"ChooseMenu",
				"<null>",
				"",
				confirmAction.label,
				clearPath);
		}
		HeadlessPhysicalInputTest::ScopedRunningOwner runningOwner(
			fixture.owner);
		if (!prepareFixtureForPath(
				actionSpec,
				clearPath,
				fixture,
				frameDriver,
				gamepad,
				inputManager))
		{
			return false;
		}
		const PhysicalTapResult tap = tapPhysicalButton(
			frameDriver,
			gamepad,
			inputManager,
			confirmAction);
		const std::vector<std::string> remainingCandidates =
			fixture.availableCandidateKeys();
		const std::vector<std::string> expectedRemainingCandidates =
		{
			"choice-0",
			"choice-1",
			"choice-2",
			"choice-3"
		};
		if (!tap.pressed || !tap.consumed
			|| !menu->visible
			|| !menu->getMultipleSelection().empty()
			|| fixture.focusedKey() != "choice-0"
			|| remainingCandidates != expectedRemainingCandidates)
		{
			return traversalFailure(
				actionSpec,
				"clear-action",
				"pressed+consumed+selection-empty+focus-choice-0"
					"+candidates-choice-0..3",
				"pressed=" + std::to_string(tap.pressed)
					+ ",consumed=" + std::to_string(tap.consumed)
					+ ",visible=" + std::to_string(menu->visible)
					+ ",selection-count="
					+ std::to_string(
						menu->getMultipleSelection().size())
					+ ",focus=" + fixture.focusedKey()
					+ ",candidates="
					+ joinStrings(remainingCandidates),
				"clear",
				confirmAction.label,
				clearPath);
		}
	}

	const std::vector<TraversalPathStep> confirmPath =
	{
		{ downAction, "choice-3" },
		{ downAction, "confirm" }
	};
	{
		PhysicalFocusFixture fixture = actionSpec.createFixture();
		auto menu = std::dynamic_pointer_cast<ChooseMenu>(fixture.owner);
		if (menu == nullptr)
		{
			return traversalFailure(
				actionSpec,
				"confirm-action-entry",
				"ChooseMenu",
				"<null>",
				"",
				confirmAction.label,
				confirmPath);
		}
		HeadlessPhysicalInputTest::ScopedRunningOwner runningOwner(
			fixture.owner);
		if (!prepareFixtureForPath(
				actionSpec,
				confirmPath,
				fixture,
				frameDriver,
				gamepad,
				inputManager))
		{
			return false;
		}
		const PhysicalTapResult tap = tapPhysicalButton(
			frameDriver,
			gamepad,
			inputManager,
			confirmAction);
		const std::vector<int> expectedSelection = { 0, 1 };
		if (!tap.pressed || !tap.consumed
			|| menu->visible
			|| menu->getMultipleSelection() != expectedSelection
			|| !fixture.focusedKey().empty())
		{
			return traversalFailure(
				actionSpec,
				"confirm-action",
				"pressed+consumed+closed+selection-0,1+no-focus",
				"pressed=" + std::to_string(tap.pressed)
					+ ",consumed=" + std::to_string(tap.consumed)
					+ ",visible=" + std::to_string(menu->visible)
					+ ",selection-count="
					+ std::to_string(
						menu->getMultipleSelection().size())
					+ ",focus=" + fixture.focusedKey(),
				"confirm",
				confirmAction.label,
				confirmPath);
		}
	}
	return true;
}

void configureGameManager(
	GameManager& gameManager,
	const ResourceManifest& manifest)
{
	gameManager.global.useWav = manifest.useWav;
	gameManager.global.applyResourceManifestFeatures(manifest);
	gameManager.global.loadUiSettings();
	gameManager.goodsManager.configureLayout();
	gameManager.magicManager.configureLayout();
	gameManager.global.data.canInput = true;
}

bool testLowRiskPhysicalFocusGraphs()
{
	bool ok = checkContract(
		(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0,
		"surface=<physical-focus-graphs> phase=headless expected=no-video actual=video");
	Engine::getInstance()->setWindowSize(800, 600);

	const std::filesystem::path repositoryRoot =
		std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
	const std::filesystem::path assetsRoot = repositoryRoot / "assets";
	File::setAssetsCollectionRoot(assetsRoot.generic_string());
	File::setActiveResourceRoot("");
	File::setCommonResourceRoot("");
	File::setResourceFallbackRoots({});
	File::setUiResourceFallbackRoots({});

	ResourceManager& resourceManager = ResourceManager::instance();
	if (!checkContract(
			resourceManager.initialize(assetsRoot.generic_string()),
			"surface=<physical-focus-graphs> phase=resource-init"
				" expected=production-collection actual=failed")
		|| !checkContract(
			resourceManager.setActiveResourcePackById("JXQY2"),
			"surface=<physical-focus-graphs> phase=resource-select"
				" expected=JXQY2 actual=failed"))
	{
		return false;
	}
	if (!checkContract(
			resourceManager.getDiscoveredPacks().size() == 11,
			"surface=startup.resource-select"
				" variant=11-enabled-packs phase=pack-count"
				" expected=11 actual="
				+ std::to_string(
					resourceManager.getDiscoveredPacks().size())))
	{
		return false;
	}
	{
		PhysicalFocusFixture resourceProbe =
			makeResourceSelectionFixture();
		auto resourceProbeScene =
			std::dynamic_pointer_cast<ResourceSelectScene>(
				resourceProbe.owner);
		constexpr std::size_t ExpectedResourceCandidateCount = 21;
		if (!checkContract(
				resourceProbeScene != nullptr
					&& GamepadSurfaceContractTestAccess::resourceCardCount(
						*resourceProbeScene) == 11
					&& GamepadSurfaceContractTestAccess::
						resourceExitButtonPresent(*resourceProbeScene)
					&& GamepadSurfaceContractTestAccess::resourceCandidateKeys(
						*resourceProbeScene).size() ==
						ExpectedResourceCandidateCount,
				"surface=startup.resource-select"
					" variant=11-enabled-packs phase=dynamic-controls"
					" expected="
					+ std::to_string(ExpectedResourceCandidateCount)
					+ "-focus-candidates"
					" actual="
					+ (resourceProbeScene == nullptr
						? std::string("<no-scene>")
						: std::to_string(
							GamepadSurfaceContractTestAccess::
								resourceCandidateKeys(
									*resourceProbeScene).size())
							+ "-focus-candidates")))
		{
			return false;
		}
	}

	GameManager gameManager;
	configureGameManager(gameManager, resourceManager.getActiveManifest());

	VirtualGamepadTest::SDLSession sdlSession;
	VirtualGamepadTest::VirtualGamepad gamepad(
		"JXQY Menu Surface Contract Pad");
	auto& inputManager = const_cast<GameInput::PhysicalInputManager&>(
		Engine::getInstance()->inputActions());
	HeadlessPhysicalInputTest::ScopedPhysicalInputManager inputScope(
		inputManager);
	if (!checkContract(
			inputScope.isInitialized(),
			"surface=<physical-focus-graphs> phase=input-init"
				" expected=initialized actual=failed"))
	{
		return false;
	}

	std::uint64_t nowMilliseconds = SDL_GetTicks();
	HeadlessPhysicalInputTest::FrameDriver frameDriver(
		inputManager,
		nowMilliseconds,
		[]()
		{
			return dispatchPhysicalUIActions(Engine::getInstance());
		},
		{});
	frameDriver.runFrame();

	for (const PhysicalTraversalSpec& spec : makePhysicalTraversalSpecs())
	{
		ok = testPhysicalCandidateReachability(
			spec, frameDriver, gamepad, inputManager) && ok;
	}
	ok = testPhysicalMultipleChoiceFooterActions(
		frameDriver, gamepad, inputManager) && ok;

	ok = checkContract(
		(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0,
		"surface=<physical-focus-graphs> phase=headless-finish"
			" expected=no-video actual=video")
		&& ok;
	return ok;
}
}

bool runGamepadSurfaceContractTests()
{
	try
	{
		bool ok = testAllSurfaceClassifications();
		ok = testLowRiskPhysicalFocusGraphs() && ok;
		return ok;
	}
	catch (const std::exception& exception)
	{
		std::cerr << "FAILED: surface=<gamepad-surface-contract>"
			<< " phase=exception expected=none actual="
			<< exception.what() << '\n';
		return false;
	}
}
