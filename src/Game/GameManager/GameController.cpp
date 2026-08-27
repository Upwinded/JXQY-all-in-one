#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <array>
#include "../../Engine/Engine.h"
#include <cmath>
#include "GameController.h"
#include "GameManager.h"
#include "../Menu/ControllerPromptPresenter.h"
#include "../Menu/MinimapToggleButton.h"
#include "../Menu/MapThumbnailMenu.h"
#include "../Data/MobileTouchInteraction.h"
#include "../Data/TimeStopUpdateGate.h"
#include "MenuController.h"
#include "../../Input/PhysicalInputManager.h"

namespace
{
bool shouldUseFastInteractionRightScript(const NPC& npc)
{
	return npc.scriptFile.empty() && !npc.scriptFileRight.empty();
}

NextDest resolveFastInteractionNPCDestinationKind(const NPC& npc)
{
	const bool rightOnlyScript =
		shouldUseFastInteractionRightScript(npc);
	return (npc.isEnemy() || npc.isNoneFighter()) && !rightOnlyScript
		? ndAttack
		: ndTalk;
}

std::shared_ptr<NPC> getClickedNPC(GameManager* gameManager)
{
	if (gameManager == nullptr || gameManager->npcManager == nullptr)
	{
		return nullptr;
	}
	const int index = gameManager->npcManager->clickIndex;
	if (index < 0 || static_cast<std::size_t>(index)
		>= gameManager->npcManager->npcList.size())
	{
		return nullptr;
	}
	return gameManager->npcManager->npcList[index];
}

std::shared_ptr<NPC> getPointerHitNPC(
	GameManager* gameManager, int x, int y)
{
	if (gameManager == nullptr || gameManager->npcManager == nullptr)
	{
		return nullptr;
	}

	Element* hitElement =
		gameManager->npcManager->findPointerHitTargetInTree(x, y);
	NPC* hitNPC = dynamic_cast<NPC*>(hitElement);
	if (hitNPC == nullptr)
	{
		return nullptr;
	}
	for (const std::shared_ptr<NPC>& npc :
		gameManager->npcManager->npcList)
	{
		if (npc.get() == hitNPC)
		{
			return npc;
		}
	}
	return nullptr;
}

std::shared_ptr<Object> getClickedObject(GameManager* gameManager)
{
	if (gameManager == nullptr || gameManager->objectManager == nullptr)
	{
		return nullptr;
	}
	const int index = gameManager->objectManager->clickIndex;
	if (index < 0 || static_cast<std::size_t>(index)
		>= gameManager->objectManager->objectList.size())
	{
		return nullptr;
	}
	return gameManager->objectManager->objectList[index];
}
}

GameController::GameController()
{
	name = "GameController";
	coverMouse = true;
	rectFullScreen = true;
	canCallBack = true;
	setPriority(epController);
	setPointerEventPreviewEnabled(true);
	result = erNone;
}

GameController::~GameController()
{
	removeAllChild();
}

bool GameController::shouldUpdateChild(PElement child)
{
	const bool hasActiveTimeStopper = gm != nullptr
		&& gm->effectManager != nullptr
		&& gm->effectManager->hasActiveTimeStopper();
	auto caster = hasActiveTimeStopper ? gm->effectManager->getActiveTimeStopperUser() : nullptr;
	return shouldUpdateGameControllerChildDuringTimeStop(hasActiveTimeStopper,
		gm != nullptr && child == gm->objectManager,
		gm != nullptr && child == gm->player,
		gm != nullptr && caster != nullptr && caster.get() == gm->player.get());
}

#define freeComponent(component); \
	removeChild(component); \
	if (component.get() != nullptr)\
	{\
		component = nullptr; \
	}
void GameController::freeResource()
{
	virtualControlPointerTransactions.clear();
	mouseWorldInputSuppressedUntilRelease = false;

	freeComponent(joystickPanel);
	freeComponent(skillPanel);
}

void GameController::init()
{
	freeResource();
	controllerFocusedTarget.reset();
	worldInputContextObserved = false;
	worldInputWasEnabled = false;
	mouseWorldInputSuppressedUntilRelease = false;

	joystickPanel = std::make_shared<JoystickPanel>();
	addChild(joystickPanel);
	skillPanel = std::make_shared<SkillsPanel>();
	addChild(skillPanel);

	if (gm->menu->bottomMenu != nullptr)
	{
		skillPanel->skillBtn[0]->drawItem = gm->menu->bottomMenu->magicItem[0];
		skillPanel->skillBtn[1]->drawItem = gm->menu->bottomMenu->magicItem[1];
		skillPanel->skillBtn[2]->drawItem = gm->menu->bottomMenu->magicItem[2];
		skillPanel->skillBtn[3]->drawItem = gm->menu->bottomMenu->magicItem[3];
		skillPanel->skillBtn[4]->drawItem = gm->menu->bottomMenu->magicItem[4];
	}
	setTouchControlsVisible(touchControlsVisible);
}

void GameController::setTouchControlsVisible(bool visible)
{
	const bool visibilityChanged = touchControlsVisible != visible;
	touchControlsVisible = visible;
	if (visibilityChanged && engine != nullptr)
	{
		isolateHeldMouseForTouchControlsVisibilityChange(
			engine->getMousePressed(MBC_MOUSE_LEFT));
	}
	if (!visible)
	{
		cancelTouchControlPointerTransactions();
	}
	if (joystickPanel != nullptr)
	{
		joystickPanel->visible = visible;
	}
	if (skillPanel != nullptr)
	{
		skillPanel->visible = visible;
	}
}

void GameController::isolateHeldMouseForTouchControlsVisibilityChange(
	bool leftMousePressed)
{
	if (!leftMousePressed)
	{
		return;
	}
	MouseAlreadyDown = false;
	mouseWorldInputSuppressedUntilRelease = true;
}

void GameController::resetTouchControlsInputState()
{
	for (FastInteractionPressBinding& binding :
		fastInteractionPressBindings)
	{
		binding = {};
	}
	if (joystickPanel != nullptr)
	{
		joystickPanel->cancelPointerInteraction();
		if (joystickPanel->joystick != nullptr)
		{
			joystickPanel->joystick->resetInput();
		}
	}
	if (skillPanel != nullptr)
	{
		skillPanel->resetInput();
	}
	if (gm != nullptr)
	{
		gm->fastSelectingList.clear();
	}
}

void GameController::cancelTouchControlInput()
{
	cancelTouchControlPointerTransactions();
}

void GameController::cancelTouchControlPointerTransactions()
{
	const bool virtualMouseOwned =
		virtualControlPointerTransactions.find(TOUCH_MOUSEID)
			!= virtualControlPointerTransactions.end()
		|| (joystickPanel != nullptr
			&& joystickPanel->hasPointerDownInTree(TOUCH_MOUSEID))
		|| (skillPanel != nullptr
			&& skillPanel->hasPointerDownInTree(TOUCH_MOUSEID));
	resetTouchControlsInputState();
	virtualControlPointerTransactions.clear();
	if (virtualMouseOwned)
	{
		// A mouse press acquired by a virtual control must not become a world
		// press merely because that control is hidden before button-up.
		MouseAlreadyDown = false;
		mouseWorldInputSuppressedUntilRelease = true;
	}
}

bool GameController::synchronizeInputLifecycle()
{
	const std::uint64_t currentInputLifecycleRevision =
		engine->inputActions().inputLifecycleRevision();
	if (currentInputLifecycleRevision == observedInputLifecycleRevision)
	{
		return false;
	}

	// Lifecycle transitions invalidate every pointer transaction in the scene,
	// not only the two mobile-control panels. This path is non-committing and
	// preserves scene/business result flags.
	if (gm != nullptr)
	{
		gm->cancelPointerInteraction();
	}
	else
	{
		cancelPointerInteraction();
	}
	resetTouchControlsInputState();
	cancelPendingControllerInteraction(true);
	virtualControlPointerTransactions.clear();
	MouseAlreadyDown = false;
	mouseWorldInputSuppressedUntilRelease =
		engine->getMousePressed(MBC_MOUSE_LEFT);
	if (gm != nullptr && gm->menu != nullptr)
	{
		gm->menu->cancelControllerInteraction();
	}
	observedInputLifecycleRevision = currentInputLifecycleRevision;
	return true;
}

void GameController::toggleTouchControls()
{
	setTouchControlsVisible(!touchControlsVisible);
}

bool GameController::canHandleWorldInput() const
{
	return gm != nullptr
		&& gm->global.data.canInput
		&& !gm->inEvent
		&& !gm->isGameplayPaused()
		&& gm->player != nullptr
		&& gm->map != nullptr
		&& (gm->menu == nullptr || !gm->menu->blocksWorldInput());
}

void GameController::handlePhysicalMovement(
	float axisX, float axisY, float magnitude, bool running)
{
	if (magnitude <= 0.0f || gm->player == nullptr || gm->map == nullptr)
	{
		return;
	}

	constexpr int DirectionScale = 1000;
	auto directionList = getMobileJoystickDirectionCandidates(
		static_cast<int>(std::lround(axisX * DirectionScale)),
		static_cast<int>(std::lround(axisY * DirectionScale)),
		DirectionScale);
	if (directionList.empty())
	{
		return;
	}

	auto actionActor = gm->player->getActionActor();
	if (actionActor == nullptr || actionActor->nowAction == acDeath || actionActor->nowAction == acHide)
	{
		return;
	}
	controllerFocusedTarget.reset();
	if (gm->player->nextAction != nullptr
		&& gm->player->nextAction->action != acRun
		&& gm->player->nextAction->action != acWalk
		&& gm->player->nextAction->action != acARun
		&& gm->player->nextAction->action != acAWalk)
	{
		return;
	}
	gm->player->cancelQueuedInteraction();

	NextAction action;
	action.action = running && gm->player->canRun ? acARun : acAWalk;
	const Point actorPosition = actionActor->getPosition();
	for (int direction : directionList)
	{
		Point step = gm->map->getSubPoint(actorPosition, direction);
		if (step == actorPosition || !gm->map->isInMap(step))
		{
			continue;
		}

		bool actorAlreadyOccupiesStep = false;
		for (const auto& stepNPC : gm->map->dataMap.tile[step.y][step.x].stepNPCList)
		{
			if (stepNPC == actionActor)
			{
				actorAlreadyOccupiesStep = true;
				break;
			}
		}
		if (actorAlreadyOccupiesStep)
		{
			break;
		}
		if (gm->map->canWalkDirectlyTo(actorPosition, direction))
		{
			action.dest = step;
			gm->player->addNextAction(action);
			return;
		}
	}

	if (actionActor->isStanding())
	{
		actionActor->direction = directionList.front();
	}
}

void GameController::handlePhysicalAttack(bool running)
{
	gm->player->cancelQueuedInteraction();
	if (gm->queueBestWorldInteraction(
		WorldInteractionIntent::Attack, running,
		KeyboardAutoInteractionTileDistance, 2, controllerFocusedTarget))
	{
		return;
	}

	auto actionActor = gm->player != nullptr ? gm->player->getActionActor() : nullptr;
	if (actionActor == nullptr || actionActor->nowAction == acDeath || actionActor->nowAction == acHide)
	{
		return;
	}

	NextAction action;
	action.action = acAttack;
	action.dest = gm->map->getSubPoint(actionActor->getPosition(), actionActor->direction);
	gm->player->addNextAction(action);
}

void GameController::handlePhysicalSkill(
	int skillIndex, float aimX, float aimY, float aimMagnitude)
{
	if (gm->player == nullptr || skillIndex < 0
		|| skillIndex >= gm->magicManager.bottomCount())
	{
		return;
	}
	if (gm->player->isControllingCharacter())
	{
		gm->showMessage("控制中不能使用武功");
		return;
	}
	auto actionActor = gm->player->getActionActor();
	if (actionActor == nullptr || actionActor->nowAction == acDeath || actionActor->nowAction == acHide)
	{
		return;
	}
	gm->player->cancelQueuedInteraction();

	NextAction action;
	action.action = acMagic;
	action.actionParam = skillIndex;
	if (aimMagnitude > 0.0f)
	{
		const float angle = std::atan2(-aimX, aimY);
		controllerFocusedTarget.reset();
		action.destGE.reset();
		action.dest = getPlayerRelativePosition(angle, 400.0f, MapXRatio);
	}
	else
	{
		auto candidates = gm->findWorldInteractionCandidates(
			WorldInteractionIntent::Attack,
			KeyboardAutoInteractionTileDistance, 2, controllerFocusedTarget);
		if (!candidates.empty() && candidates.front().npc != nullptr)
		{
			action.destGE = candidates.front().npc;
			action.dest = candidates.front().npc->getPosition();
		}
		else
		{
			action.dest = gm->map->getSubPoint(
				actionActor->getPosition(), actionActor->direction);
		}
	}

	_last_magic_index = skillIndex;
	gm->player->addNextAction(action);
}

void GameController::handlePhysicalJump(float axisX, float axisY, float magnitude)
{
	auto actionActor = gm->player != nullptr ? gm->player->getActionActor() : nullptr;
	if (actionActor == nullptr || actionActor->nowAction == acDeath || actionActor->nowAction == acHide)
	{
		return;
	}
	gm->player->cancelQueuedInteraction();

	float angle = static_cast<float>(actionActor->direction) * static_cast<float>(M_PI) / 4.0f;
	if (magnitude >= GameInput::PhysicalInputManager::DirectionalJumpThreshold)
	{
		angle = std::atan2(-axisX, axisY);
	}

	NextAction action;
	action.action = acJump;
	action.dest = getPlayerRelativePosition(
		angle, 400.0f, static_cast<float>(TILE_WIDTH) / static_cast<float>(TILE_HEIGHT));
	gm->player->addNextAction(action);
}

void GameController::useQuickItem(int slotIndex)
{
	if (slotIndex < 0 || slotIndex >= gm->goodsManager.bottomCount())
	{
		return;
	}
	gm->goodsManager.useItem(gm->goodsManager.bottomBegin() + slotIndex);
}

void GameController::prepareLegacyWorldAction()
{
	cancelPendingControllerInteraction(true);
}

bool GameController::tryToggleLegacySit()
{
	if (gm == nullptr || gm->player == nullptr)
	{
		return false;
	}
	const bool wasSitting = gm->player->isSitting();
	if (wasSitting)
	{
		gm->player->beginStand();
	}
	else
	{
		gm->player->beginSit();
	}
	if (gm->player->isSitting() == wasSitting)
	{
		return false;
	}
	prepareLegacyWorldAction();
	return true;
}

bool GameController::submitLegacyWorldAction(NextAction& action)
{
	if (gm == nullptr || gm->player == nullptr)
	{
		return false;
	}
	if (!gm->player->addNextAction(action))
	{
		return false;
	}
	prepareLegacyWorldAction();
	return true;
}

void GameController::handleLegacyHeldMouseMovement(
	bool leftMousePressed,
	bool pointerInputOwnedByUI)
{
	if (MouseAlreadyDown && !leftMousePressed)
	{
		MouseAlreadyDown = false;
	}
	if (!leftMousePressed
		|| pointerInputOwnedByUI
		|| (gm->menu != nullptr && gm->menu->blocksWorldPointerInput()))
	{
		return;
	}

	if (dragging == TOUCH_UNTOUCHEDID
		&& MouseAlreadyDown
		&& touchingID != TOUCH_UNTOUCHEDID
		&& !engine->getKeyPress(KEY_LALT)
		&& !engine->getKeyPress(KEY_RALT))
	{
		auto actionActor = gm->player->getActionActor();
		if (actionActor != nullptr
			&& actionActor->nowAction != acDeath
			&& actionActor->nowAction != acHide)
		{
			int width = 0;
			int height = 0;
			engine->getWindowSize(width, height);
			Point centerScreen;
			centerScreen.x = width / 2;
			centerScreen.y = height / 2;
			int mouseX = 0;
			int mouseY = 0;
			engine->getMousePosition(mouseX, mouseY);
			Point position = gm->map->getMousePosition(
				{ mouseX, mouseY },
				gm->camera->position,
				centerScreen,
				gm->camera->offset);
			NextAction action;
			action.action = engine->getKeyPress(KEY_LSHIFT)
				|| engine->getKeyPress(KEY_RSHIFT)
				? acRun
				: acWalk;
			action.dest = position;
			submitLegacyWorldAction(action);
		}
	}
	else if (dragging == TOUCH_UNTOUCHEDID
		&& getClickedNPC(gm) != nullptr
		&& !engine->getKeyPress(KEY_LALT)
		&& !engine->getKeyPress(KEY_RALT))
	{
		auto actionActor = gm->player->getActionActor();
		if (actionActor != nullptr
			&& actionActor->nowAction != acDeath
			&& actionActor->nowAction != acHide)
		{
			NextAction action;
			action.action = engine->getKeyPress(KEY_LSHIFT)
				|| engine->getKeyPress(KEY_RSHIFT)
				? acRun
				: acWalk;
			std::shared_ptr<NPC> target = getClickedNPC(gm);
			if (target != nullptr)
			{
				action.destGE = target;
				const bool useRightScript =
					target->scriptFile == ""
					&& target->scriptFileRight != "";
				if (useRightScript)
				{
					action.destKind = ndTalk;
					action.useRightScript = true;
				}
				else if (target->isEnemy() || target->isNoneFighter())
				{
					action.destKind = ndAttack;
				}
				else
				{
					action.destKind = ndTalk;
				}
				action.dest = target->getPosition();
				submitLegacyWorldAction(action);
			}
		}
	}
	else if (dragging == TOUCH_UNTOUCHEDID
		&& getClickedObject(gm) != nullptr
		&& !engine->getKeyPress(KEY_LALT)
		&& !engine->getKeyPress(KEY_RALT))
	{
		NextAction action;
		action.action = engine->getKeyPress(KEY_LSHIFT)
			|| engine->getKeyPress(KEY_RSHIFT)
			? acRun
			: acWalk;
		std::shared_ptr<Object> target = getClickedObject(gm);
		if (target != nullptr)
		{
			action.destGE = target;
			action.destKind = ndObj;
			action.dest = target->position;
			action.useRightScript =
				target->shouldUseRightScriptForPrimaryInteraction();
			submitLegacyWorldAction(action);
		}
	}
}

void GameController::handleLegacyKeyboardMovement(
	bool up, bool down, bool left, bool right, bool running)
{
	if (gm == nullptr || gm->player == nullptr || gm->map == nullptr)
	{
		return;
	}

	auto actionActor = gm->player->getActionActor();
	Point destination = actionActor != nullptr
		? actionActor->getPosition()
		: gm->player->getPosition();
	const int line = std::abs(destination.y % 2);
	bool hasMovement = true;
	if (up && left)
	{
		destination.x += -1 + line;
		destination.y -= 1;
	}
	else if (up && right)
	{
		destination.x += line;
		destination.y -= 1;
	}
	else if (down && left)
	{
		destination.x += -1 + line;
		destination.y += 1;
	}
	else if (down && right)
	{
		destination.x += line;
		destination.y += 1;
	}
	else if (up)
	{
		destination.y -= 2;
	}
	else if (down)
	{
		destination.y += 2;
	}
	else if (left)
	{
		destination.x -= 1;
	}
	else if (right)
	{
		destination.x += 1;
	}
	else
	{
		hasMovement = false;
	}

	if (!hasMovement
		|| !gm->map->canWalkForActor(destination, actionActor))
	{
		return;
	}

	NextAction action;
	action.action = running ? acARun : acAWalk;
	action.dest = destination;
	submitLegacyWorldAction(action);
}

void GameController::dispatchPhysicalWorldAction(
	GameInput::InputAction action,
	const GameInput::GamepadAxisState& axes)
{
	switch (action)
	{
	case GameInput::InputAction::AttackPrimary:
		handlePhysicalAttack(axes.running);
		break;
	case GameInput::InputAction::CastSkill1:
	case GameInput::InputAction::CastSkill2:
	case GameInput::InputAction::CastSkill3:
	case GameInput::InputAction::CastSkill4:
	case GameInput::InputAction::CastSkill5:
		handlePhysicalSkill(
			static_cast<int>(action)
				- static_cast<int>(GameInput::InputAction::CastSkill1),
			axes.rightStick.x,
			axes.rightStick.y,
			axes.rightStick.magnitude);
		break;
	case GameInput::InputAction::UseQuickItem1:
	case GameInput::InputAction::UseQuickItem2:
	case GameInput::InputAction::UseQuickItem3:
		useQuickItem(
			static_cast<int>(action)
				- static_cast<int>(GameInput::InputAction::UseQuickItem1));
		break;
	case GameInput::InputAction::Jump:
		handlePhysicalJump(
			axes.leftStick.x,
			axes.leftStick.y,
			axes.leftStick.magnitude);
		break;
	case GameInput::InputAction::ToggleMiniMap:
		toggleMinimap();
		break;
	case GameInput::InputAction::ToggleSit:
		if (!gm->player->isControllingCharacter())
		{
			if (gm->player->isSitting())
			{
				gm->player->beginStand();
			}
			else
			{
				gm->player->beginSit();
			}
		}
		break;
	default:
		break;
	}
}

bool GameController::queueControllerInteraction(WorldInteractionIntent intent, bool running)
{
	// A fresh interaction intent replaces the previous strict auto-interaction
	// even when the new intent has no valid target.
	gm->player->cancelQueuedInteraction(true);
	auto candidates = gm->findWorldInteractionCandidates(
		intent, KeyboardAutoInteractionTileDistance, 2, controllerFocusedTarget);
	if (candidates.empty())
	{
		return false;
	}

	const WorldInteractionCandidate& candidate = candidates.front();
	bool queued = false;
	if (candidate.object != nullptr)
	{
		queued = gm->queueObjectScriptInteraction(
			candidate.object,
			intent == WorldInteractionIntent::Alternate
				? WorldInteractionScriptSide::Alternate
				: WorldInteractionScriptSide::Primary,
			running);
	}
	else if (candidate.npc != nullptr)
	{
		queued = gm->queueNPCTalkInteraction(
			candidate.npc,
			intent == WorldInteractionIntent::Alternate
				? WorldInteractionScriptSide::Alternate
				: WorldInteractionScriptSide::Primary,
			running);
	}
	if (queued)
	{
		controllerFocusedTarget = candidate.getTarget();
	}
	return queued;
}

void GameController::cycleControllerInteractionTarget()
{
	auto candidates = gm->findWorldInteractionCandidates(
		WorldInteractionIntent::Primary, KeyboardAutoInteractionTileDistance, 2);
	if (candidates.empty())
	{
		controllerFocusedTarget.reset();
		gm->showMessage("附近没有可交互目标");
		return;
	}

	auto currentTarget = controllerFocusedTarget.lock();
	std::size_t nextIndex = 0;
	for (std::size_t index = 0; index < candidates.size(); index++)
	{
		if (candidates[index].getTarget() == currentTarget)
		{
			nextIndex = (index + 1) % candidates.size();
			break;
		}
	}
	const auto& candidate = candidates[nextIndex];
	controllerFocusedTarget = candidate.getTarget();
	if (candidate.object != nullptr)
	{
		gm->showMessage(candidate.object->objName);
	}
	else if (candidate.npc != nullptr)
	{
		gm->showMessage(candidate.npc->npcName);
	}
}

void GameController::cancelPendingControllerInteraction(bool clearTarget)
{
	if (gm != nullptr && gm->player != nullptr)
	{
		gm->player->cancelQueuedInteraction(true);
	}
	if (clearTarget)
	{
		controllerFocusedTarget.reset();
	}
}

void GameController::cancelControllerWorldInteraction()
{
	// A menu transition invalidates any contact that started in the world or
	// virtual controls. Cancel it without callbacks before an in-tree modal can
	// capture the eventual release and leave a lower control held forever.
	resetTouchControlsInputState();
	if (gm != nullptr && gm->player != nullptr)
	{
		gm->player->cancelQueuedInteraction(false);
	}
	controllerFocusedTarget.reset();
	MouseAlreadyDown = false;
	mouseWorldInputSuppressedUntilRelease =
		engine != nullptr && engine->getMousePressed(MBC_MOUSE_LEFT);
}

void GameController::validateControllerFocusedTarget()
{
	auto target = controllerFocusedTarget.lock();
	if (target == nullptr)
	{
		controllerFocusedTarget.reset();
		return;
	}

	auto actionActor = gm != nullptr && gm->player != nullptr
		? gm->player->getActionActor()
		: nullptr;
	bool valid = actionActor != nullptr && gm->map != nullptr;
	Point targetPosition = {};
	auto object = std::dynamic_pointer_cast<Object>(target);
	if (valid && object != nullptr)
	{
		valid = gm->objectManager != nullptr && gm->objectManager->findObj(object)
			&& WorldInteractionResolver::isObjectValidForIntent(
				object, WorldInteractionIntent::Primary);
		targetPosition = object->position;
	}
	else if (valid)
	{
		auto npc = std::dynamic_pointer_cast<NPC>(target);
		valid = npc != nullptr && gm->npcManager != nullptr && gm->npcManager->findNPC(npc)
			&& WorldInteractionResolver::isNPCValidForIntent(
				npc, WorldInteractionIntent::Primary, actionActor);
		if (valid)
		{
			targetPosition = npc->getPosition();
		}
	}
	if (valid)
	{
		// Check only the retained target. Rebuilding and sorting the full
		// candidate list here would put an avoidable O(N log N) scan on every
		// world-input frame that has a controller focus.
		const Point actorPosition = actionActor->getPosition();
		valid = Map::calDistance(actorPosition, targetPosition)
				<= KeyboardAutoInteractionTileDistance
			&& gm->map->canSee(actorPosition, targetPosition);
	}
	if (!valid)
	{
		controllerFocusedTarget.reset();
	}
}

void GameController::toggleMinimap()
{
	if (gm->menu == nullptr || gm->menu->mapThumbnailMenu == nullptr)
	{
		return;
	}
	gm->menu->toggleMapThumbnailView();
}

void GameController::handlePhysicalInput()
{
	const auto& input = engine->inputActions();
	if (!input.hasActiveGamepad())
	{
		return;
	}

	if (engine->consumeInputAction(GameInput::InputAction::OpenSystemMenu))
	{
		if (gm->menu != nullptr)
		{
			gm->menu->openSystemMenu();
		}
		return;
	}
	if (gm->menu != nullptr && gm->menu->mapThumbnailMenu != nullptr
		&& gm->menu->mapThumbnailMenu->visible
		&& engine->consumeInputAction(GameInput::InputAction::ToggleMiniMap))
	{
		toggleMinimap();
		return;
	}
	if (engine->consumeInputAction(GameInput::InputAction::OpenSettings))
	{
		if (!canHandleWorldInput())
		{
			return;
		}
		if (gm->menu != nullptr)
		{
			gm->menu->openSettings();
		}
		return;
	}
	if (!canHandleWorldInput())
	{
		return;
	}
	if (gm->menu != nullptr)
	{
		if (engine->consumeInputAction(GameInput::InputAction::OpenMemo))
		{
			gm->menu->toggleMemoView();
			return;
		}
		if (engine->consumeInputAction(GameInput::InputAction::OpenEquip))
		{
			gm->menu->toggleEquipView();
			return;
		}
		if (engine->consumeInputAction(GameInput::InputAction::OpenGoods))
		{
			gm->menu->toggleGoodsView();
			return;
		}
	}

	const auto& moveState = input.action(GameInput::InputAction::Move);
	if (moveState.down)
	{
		handlePhysicalMovement(
			moveState.axis.leftStick.x,
			moveState.axis.leftStick.y,
			moveState.axis.leftStick.magnitude,
			moveState.axis.running);
	}

	if (engine->consumeInputAction(GameInput::InputAction::CycleInteractionTarget))
	{
		cycleControllerInteractionTarget();
	}
	if (engine->consumeInputAction(GameInput::InputAction::InteractPrimary))
	{
		queueControllerInteraction(
			WorldInteractionIntent::Primary,
			input.action(GameInput::InputAction::InteractPrimary).axis.running);
	}
	if (engine->consumeInputAction(GameInput::InputAction::InteractAlternate))
	{
		if (!queueControllerInteraction(
			WorldInteractionIntent::Alternate,
			input.action(GameInput::InputAction::InteractAlternate).axis.running))
		{
			gm->showMessage("附近没有可用的右侧交互目标");
		}
	}
	static constexpr std::array<GameInput::InputAction, 12> WorldActions =
	{
		GameInput::InputAction::AttackPrimary,
		GameInput::InputAction::CastSkill1,
		GameInput::InputAction::CastSkill2,
		GameInput::InputAction::CastSkill3,
		GameInput::InputAction::CastSkill4,
		GameInput::InputAction::CastSkill5,
		GameInput::InputAction::UseQuickItem1,
		GameInput::InputAction::UseQuickItem2,
		GameInput::InputAction::UseQuickItem3,
		GameInput::InputAction::Jump,
		GameInput::InputAction::ToggleMiniMap,
		GameInput::InputAction::ToggleSit,
	};
	for (GameInput::InputAction action : WorldActions)
	{
		if (engine->consumeInputAction(action))
		{
			dispatchPhysicalWorldAction(action, input.action(action).axis);
		}
	}
}

void GameController::onChildCallBack(PElement child)
{
	if (child == nullptr || !gm->global.data.canInput) { return; }
	auto ret = child->getResult();
	if (child == joystickPanel)
	{
		if (ret & erMouseLDown)
		{
			prepareLegacyWorldAction();
			gm->player->nextAction = nullptr;
		}
	}
	else if (child == skillPanel)
	{
		int clickIndex = skillPanel->getClickIndex();
		if ((ret & erMouseLDown)
			&& clickIndex >= SKILL_PANEL_FAST_SELECT
			&& clickIndex < SKILL_PANEL_FAST_SELECT + FASTBTN_COUNT)
		{
			const int index = clickIndex - SKILL_PANEL_FAST_SELECT;
			FastInteractionPressBinding& binding =
				fastInteractionPressBindings[index];
			binding = {};
			if (gm->fastSelectingList.size()
				> static_cast<std::size_t>(index))
			{
				const NextAction& currentBinding =
					gm->fastSelectingList[index];
				if (!currentBinding.destGE.expired())
				{
					binding.target = currentBinding.destGE;
					binding.destinationKind =
						static_cast<int>(currentBinding.destKind);
					binding.useRightScript =
						currentBinding.useRightScript;
					binding.active = true;
				}
			}
		}
		if ((ret & erClick) && clickIndex == SKILL_PANEL_MINIMAP)
		{
			auto minimapButton = skillPanel->minimapButton;
			if (minimapButton != nullptr && gm->menu != nullptr && gm->menu->mapThumbnailMenu != nullptr)
			{
				gm->menu->setMapThumbnailVisible(minimapButton->checked);
			}
			else if (minimapButton != nullptr)
			{
				minimapButton->setChecked(false);
			}
			return;
		}

		if (ret & erDragEnd)
		{
			auto dragPos = skillPanel->getDragEndPosition();

			switch (clickIndex) {
				case SKILL_PANEL_JUMP: {
					Point pos = gm->getMousePoint(dragPos.x, dragPos.y);
					NextAction act;
					act.action = acJump;
					act.dest = pos;
					submitLegacyWorldAction(act);
					break;
				}
				case SKILL_PANEL_SKILL1:
				case SKILL_PANEL_SKILL2:
				case SKILL_PANEL_SKILL3:
				case SKILL_PANEL_SKILL4:
				case SKILL_PANEL_SKILL5:
				{
					if (gm->player->nowAction != acDeath && gm->player->nowAction != acHide)
					{
						auto angle = atan2(-dragPos.x, dragPos.y);
						float distance = 400;
						auto pos = getPlayerRelativePosition(angle, distance, MapXRatio);

						NextAction act;
						act.action = acMagic;
						act.destGE.reset();
						act.dest = pos;
						act.actionParam = clickIndex;
						submitLegacyWorldAction(act);
					}
					break;
				}
			}
		}
		else if (ret & erClick)
		{
			switch (clickIndex)
			{
				case SKILL_PANEL_JUMP:
				{
                    int dir = gm->player->direction;
                    auto angle = dir * M_PI / 4;
                    float distance = 400;
                    auto pos = getPlayerRelativePosition(angle, distance, TILE_WIDTH / TILE_HEIGHT);
					NextAction act;
					act.action = acJump;
					act.dest = pos;
					submitLegacyWorldAction(act);
					break;
				}
				case SKILL_PANEL_SIT:
				{
					tryToggleLegacySit();
					break;
				}
				case SKILL_PANEL_ATTACK:
				{
					auto actionActor = gm->player->getActionActor();
					if (actionActor != nullptr
						&& actionActor->nowAction != acDeath
						&& actionActor->nowAction != acHide)
					{
						NextAction act;
						std::shared_ptr<NPC> tempNPC = nullptr;
						if (std::shared_ptr<NPC> clickedNPC = getClickedNPC(gm))
						{
							tempNPC = clickedNPC;
						}
						else
						{
							tempNPC = gm->npcManager->findNearestViewNPC(
								lkEnemy, actionActor->getPosition(), 15);
						}
						if (tempNPC != nullptr)
						{
							const bool canRunToTarget = gm->player->canRun
								&& (gm->player->thew > (int)round(
									(float)gm->player->info.thewMax
									* MIN_THEW_RATE_TO_RUN)
									|| gm->player->thew > MIN_THEW_LIMIT_TO_RUN);
							act.action = canRunToTarget ? acRun : acWalk;
							act.destKind = ndAttack;
							act.dest = tempNPC->getPosition();
							act.destGE = tempNPC;
						}
						else
						{
							act.action = acAttack;
							act.dest = gm->map->getSubPoint(
								actionActor->getPosition(), actionActor->direction);
						}
						submitLegacyWorldAction(act);
					}
					break;
				}
                case SKILL_PANEL_SKILL1:
                case SKILL_PANEL_SKILL2:
                case SKILL_PANEL_SKILL3:
                case SKILL_PANEL_SKILL4:
                case SKILL_PANEL_SKILL5:
				{
					if (gm->player->nowAction != acDeath && gm->player->nowAction != acHide)
					{
						NextAction act;
						act.action = acMagic;
						std::shared_ptr<NPC> tempNPC = nullptr;
						if (std::shared_ptr<NPC> clickedNPC = getClickedNPC(gm))
						{
							tempNPC = clickedNPC;
						}
						else
						{
							tempNPC = gm->npcManager->findNearestViewNPC(lkEnemy, gm->player->getPosition(), 15);
						}
						if (tempNPC != nullptr)
						{
							act.dest = tempNPC->getPosition();
							act.destGE = tempNPC;
						}
						else
						{
							act.dest = gm->map->getSubPoint(gm->player->getPosition(), gm->player->direction);
						}
						act.actionParam = clickIndex;
						submitLegacyWorldAction(act);
					}
					break;
				}
				case SKILL_PANEL_FAST_SELECT:
				case SKILL_PANEL_FAST_SELECT + 1:
				case SKILL_PANEL_FAST_SELECT + 2:
				case SKILL_PANEL_FAST_SELECT + 3:
				{
					const int index =
						clickIndex - SKILL_PANEL_FAST_SELECT;
					const FastInteractionPressBinding binding =
						fastInteractionPressBindings[index];
					fastInteractionPressBindings[index] = {};
					if (!binding.active)
					{
						break;
					}
					NextAction act;
					if (gm->player->canRun && (gm->player->thew > (int)round((float)gm->player->info.thewMax * MIN_THEW_RATE_TO_RUN) || gm->player->thew > MIN_THEW_LIMIT_TO_RUN))
					{
						act.action = acRun;
					}
					else
					{
						act.action = acWalk;
					}
					auto fastDestGEPtr = binding.target.lock();
					if (fastDestGEPtr != nullptr)
					{
						if (gm->player->nowAction != acDeath && gm->player->nowAction != acHide)
						{
							act.destKind = static_cast<NextDest>(
								binding.destinationKind);
							auto actionActor =
								gm->player->getActionActor();
							const Point actorPosition =
								actionActor != nullptr
									? actionActor->getPosition()
									: gm->player->getPosition();
							bool valid = false;
							if (act.destKind == ndTalk || act.destKind == ndAttack)
							{
								auto destNPC = std::dynamic_pointer_cast<NPC>(fastDestGEPtr);
								const auto currentCandidates =
									gm->npcManager->findRadiusFastSelectionNPC(
										actorPosition,
										FastInteractionTileDistance);
								valid = destNPC != nullptr
									&& std::find(
										currentCandidates.begin(),
										currentCandidates.end(),
										destNPC)
										!= currentCandidates.end();
								if (valid)
								{
									valid =
										resolveFastInteractionNPCDestinationKind(
											*destNPC)
											== act.destKind
										&& shouldUseFastInteractionRightScript(
											*destNPC)
											== binding.useRightScript;
								}
							}
							else if (act.destKind == ndObj)
							{
								auto destObj = std::dynamic_pointer_cast<Object>(fastDestGEPtr);
								const auto currentCandidates =
									gm->objectManager->findRadiusScriptViewObj(
										actorPosition,
										FastInteractionTileDistance);
								valid = destObj != nullptr
									&& std::find(
										currentCandidates.begin(),
										currentCandidates.end(),
										destObj)
										!= currentCandidates.end();
								if (valid)
								{
									valid =
										destObj->shouldUseRightScriptForPrimaryInteraction()
											== binding.useRightScript;
								}
							}
							if (!valid)
							{
								break;
							}
							act.dest = fastDestGEPtr->position;
							act.destGE = binding.target;
							act.useRightScript =
								binding.useRightScript;
							submitLegacyWorldAction(act);
						}
					}
					break;
				}
			}
		}
	}
}

void GameController::onEvent()
{
	// onEvent reads the virtual joystick below. Synchronize first so a contact
	// held across background/focus loss cannot enqueue one stale NextAction.
	synchronizeInputLifecycle();
	if (!gm->global.data.canInput)
	{
		return;
	}
	const bool worldInputBlocked =
		gm->menu != nullptr && gm->menu->blocksWorldInput();
	if (mouseWorldInputSuppressedUntilRelease
		&& !engine->getMousePressed(MBC_MOUSE_LEFT))
	{
		mouseWorldInputSuppressedUntilRelease = false;
	}
	bool pointerInputOwnedByUI = gm->menu != nullptr
		&& (gm->menu->ownsPointerTransaction(TOUCH_MOUSEID)
			|| gm->menu->hasPointerDownInTree(TOUCH_MOUSEID));
	pointerInputOwnedByUI = pointerInputOwnedByUI
		|| virtualControlPointerTransactions.find(TOUCH_MOUSEID)
			!= virtualControlPointerTransactions.end()
		|| (joystickPanel != nullptr
			&& joystickPanel->hasPointerDownInTree(TOUCH_MOUSEID))
		|| (skillPanel != nullptr
			&& skillPanel->hasPointerDownInTree(TOUCH_MOUSEID));
	pointerInputOwnedByUI = pointerInputOwnedByUI
		|| mouseWorldInputSuppressedUntilRelease;
	const bool leftMousePressed =
		engine->getMousePressed(MBC_MOUSE_LEFT);
	handleLegacyHeldMouseMovement(
		leftMousePressed,
		pointerInputOwnedByUI);
	if (!worldInputBlocked)
	{
		// 键盘控制移动
		handleLegacyKeyboardMovement(
			engine->getKeyPress(KEY_UP),
			engine->getKeyPress(KEY_DOWN),
			engine->getKeyPress(KEY_LEFT),
			engine->getKeyPress(KEY_RIGHT),
			engine->getKeyPress(KEY_LSHIFT)
				|| engine->getKeyPress(KEY_RSHIFT));
	}
//#endif
    
	NextAction act;
	if (!touchControlsVisible || joystickPanel == nullptr || skillPanel == nullptr)
	{
		gm->fastSelectingList.clear();
		return;
	}
	bool joystickAction = true;
	if (joystickPanel->joystick->isRunning())
	{
		if (gm->player->canRun)
		{
			act.action = acARun;
		}
		else
		{
			act.action = acAWalk;
		}
	}
	else if (joystickPanel->joystick->isWalking())
	{
		act.action = acAWalk;
	}
	else
	{
		joystickAction = false;
	}
	if (joystickAction && (gm->player->nextAction == nullptr || gm->player->nextAction->action == acRun|| gm->player->nextAction->action == acWalk || gm->player->nextAction->action == acARun|| gm->player->nextAction->action == acAWalk))
	{
		auto dirList = joystickPanel->joystick->getDirectionList();
		bool steping = false;
		auto actionActor = gm->player->getActionActor();
		Point playerPos = actionActor != nullptr ? actionActor->getPosition() : gm->player->getPosition();
		for (size_t i = 0; i < dirList.size(); i++)
		{
			auto step = gm->map->getSubPoint(playerPos, dirList[i]);
			if (playerPos == step)
			{
				break;
			}
			if (!gm->map->isInMap(step))
			{
				continue;
			}
			bool breakOut = false;
			for (auto iter = gm->map->dataMap.tile[step.y][step.x].stepNPCList.begin(); iter != gm->map->dataMap.tile[step.y][step.x].stepNPCList.end(); iter++)
			{
				if ((*iter) == actionActor)
				{
                    breakOut = true;
                    break;
				}
			}
            if (!breakOut && actionActor != nullptr && gm->map->canWalkDirectlyTo(actionActor->getPosition(), dirList[i]))
			{
				act.dest = step;
				steping = true;
				submitLegacyWorldAction(act);
				break;
			}
			if (breakOut)
			{
				break;
			}
		}
		if (!steping)
		{
			if (dirList.size() > 0 && actionActor != nullptr && actionActor->isStanding())
			{
				if (actionActor != nullptr)
				{
					actionActor->direction = *dirList.begin();
				}
			}

		}
	}


	if (!gm->inEvent)
	{
		gm->fastSelectingList.resize(0);
		const int radius = FastInteractionTileDistance;
		auto actionActor = gm->player->getActionActor();
		Point playerPos = actionActor != nullptr ? actionActor->getPosition() : gm->player->getPosition();
		auto tempObjList = gm->objectManager->findRadiusScriptViewObj(playerPos, radius);
		for (int i = 0; i < tempObjList.size(); ++i)
		{
			NextAction action;
			action.destKind = ndObj;
			action.destGE = tempObjList[i];
			action.dest = tempObjList[i]->position;
			action.distance = gm->map->calDistance(playerPos, tempObjList[i]->position);
			action.useRightScript = tempObjList[i]->shouldUseRightScriptForPrimaryInteraction();
			gm->fastSelectingList.push_back(action);
		}
		auto tempNPCList = gm->npcManager->findRadiusFastSelectionNPC(playerPos, radius);
		for (int i = 0; i < tempNPCList.size(); ++i)
		{
			NextAction action;
			action.destKind =
				resolveFastInteractionNPCDestinationKind(*tempNPCList[i]);
			action.destGE = tempNPCList[i];
			// NPC::position 为 protected，必须使用 public getPosition() 访问。
			action.dest = tempNPCList[i]->getPosition();
			action.distance = gm->map->calDistance(playerPos, tempNPCList[i]->getPosition());
			action.useRightScript =
				shouldUseFastInteractionRightScript(*tempNPCList[i]);

			gm->fastSelectingList.push_back(action);
		}

		std::sort(gm->fastSelectingList.begin(), gm->fastSelectingList.end(), gm->actionCmp);
		if (gm->fastSelectingList.size() > FASTBTN_COUNT)
		{
			gm->fastSelectingList.resize(FASTBTN_COUNT);
		}
		for (int i = 0; i < gm->fastSelectingList.size(); ++i)
		{
			auto fastItemPtr = gm->fastSelectingList[i].destGE.lock();
			if (!fastItemPtr)
			{
				setFastSelectBtn(i, false, "");
				continue;
			}
			if (gm->fastSelectingList[i].destKind == ndTalk
				|| gm->fastSelectingList[i].destKind == ndAttack)
			{
				auto fastNPC = std::dynamic_pointer_cast<NPC>(fastItemPtr);
				if (fastNPC)
				{
					setFastSelectBtn(
						i,
						true,
						fastNPC->npcName,
						fastNPC->isEnemy());
				}
				else
				{
					setFastSelectBtn(i, false, "");
				}
			}
			else
			{
				auto fastObj = std::dynamic_pointer_cast<Object>(fastItemPtr);
				if (fastObj)
				{
					setFastSelectBtn(i, true, fastObj->objName);
				}
				else
				{
					setFastSelectBtn(i, false, "");
				}
			}
		}

		for (int i = gm->fastSelectingList.size(); i < FASTBTN_COUNT; ++i)
		{
			setFastSelectBtn(i, false);
		}
	}
	else
	{
		for (int i = 0; i < FASTBTN_COUNT; ++i)
		{
			setFastSelectBtn(i, false);
		}
	}
}

void GameController::processPhysicalInputFrame()
{
	const auto& input = engine->inputActions();
	synchronizeInputLifecycle();
	if (!input.isInputContextActive())
	{
		worldInputWasEnabled = false;
		return;
	}
	const std::uint32_t currentGamepadInstanceID =
		input.activeGamepadID();
	if (activeGamepadInstanceID != 0
		&& currentGamepadInstanceID != activeGamepadInstanceID)
	{
		cancelPendingControllerInteraction(true);
		if (gm != nullptr && gm->menu != nullptr)
		{
			gm->menu->cancelControllerInteraction();
		}
	}
	activeGamepadInstanceID = currentGamepadInstanceID;
	if (gm != nullptr && gm->menu != nullptr
		&& (gm->inEvent || !gm->global.data.canInput))
	{
		gm->menu->cancelControllerInteraction();
	}
	const bool semanticInputBlocked = isFrameSemanticInputBlocked();
	const bool worldInputContextEnabled = canHandleWorldInput();
	if (worldInputContextObserved
		&& worldInputWasEnabled != worldInputContextEnabled)
	{
		// Entering and leaving a world-input barrier are both context changes.
		// Releasing only on entry lets a stick or button pressed while a menu or
		// story context is active leak into gameplay on the first return frame.
		engine->releasePhysicalInputsForContextTransition();
	}
	worldInputContextObserved = true;
	worldInputWasEnabled = worldInputContextEnabled;
	// A semantic UI action is a one-frame consumption decision, not a durable
	// input-context transition. It blocks the physical aliases below without
	// arming the neutral gate that real menu/story context changes require.
	const bool worldInputEnabled =
		worldInputContextEnabled && !semanticInputBlocked;
	if (!worldInputEnabled)
	{
		cancelPendingControllerInteraction(true);
	}
	else
	{
		validateControllerFocusedTarget();
	}
	if (!semanticInputBlocked)
	{
		handlePhysicalInput();
	}
}

void GameController::onUpdate()
{
	const bool shouldBeVisible = !gm->inEvent;
	if (visible && !shouldBeVisible)
	{
		// Once this observer becomes hidden, pointer-up/cancel no longer visits
		// it. End all virtual-control transactions at the visibility boundary
		// so a release during the story event cannot leave UI ownership behind.
		cancelTouchControlPointerTransactions();
	}
	visible = shouldBeVisible;

	// 同步小地图按钮 checked 状态与 MapThumbnailMenu 可见性。
	// 处理以下场景中菜单被关闭时按钮状态同步：
	// - MapThumbnailMenu 自身关闭按钮
	// - MenuController::clearMenu()（ESC/退出）
	// - 切换地图或退出游戏时菜单被清理
	// - F8/M/TAB 快捷键切换（桌面端兼容，移动端一般无键盘）
	if (skillPanel != nullptr && skillPanel->minimapButton != nullptr)
	{
		auto minimapButton = skillPanel->minimapButton;
		bool menuVisible = false;
		if (gm->menu != nullptr && gm->menu->mapThumbnailMenu != nullptr)
		{
			menuVisible = gm->menu->mapThumbnailMenu->visible;
		}
		if (minimapButton->checked != menuVisible)
		{
			minimapButton->setChecked(menuVisible);
		}
		// 游戏进入事件剧情时隐藏按钮，避免遮挡对话
		minimapButton->visible = !gm->inEvent;
	}
}

void GameController::onDrawEnd()
{
	if (!visible || engine == nullptr
		|| !shouldPresentGamepadFocus(engine)
		|| !canHandleWorldInput()
		|| Element::currentRunOwnerBlocksParentInput()
		|| (gm->menu != nullptr
			&& gm->menu->hasActiveControllerPromptOwner()))
	{
		return;
	}

	ControllerPromptPresenter::drawBottomBar(
		engine,
		engine->inputActions(),
		ControllerPromptPresenter::worldPromptItems());
}

void GameController::onPreviewPointerEvent(AEvent& event)
{
	synchronizeInputLifecycle();
	EventTouchID completedPointerID = TOUCH_UNTOUCHEDID;
	if (event.eventType == ET_MOUSEUP
		&& event.eventData == MBC_MOUSE_LEFT)
	{
		completedPointerID = TOUCH_MOUSEID;
		mouseWorldInputSuppressedUntilRelease = false;
	}
	else if (event.eventType == ET_FINGERUP
		|| event.eventType == ET_FINGERCANCEL)
	{
		completedPointerID = event.eventData;
	}
	if (completedPointerID != TOUCH_UNTOUCHEDID)
	{
		virtualControlPointerTransactions.erase(completedPointerID);
	}
	if (event.eventType == ET_MOUSEDOWN
		&& event.eventData == MBC_MOUSE_LEFT)
	{
		mouseWorldInputSuppressedUntilRelease = false;
	}
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
		&& ((joystickPanel != nullptr
				&& joystickPanel->hasPointerDownInTree(pointerID))
			|| (skillPanel != nullptr
				&& skillPanel->hasPointerDownInTree(pointerID))))
	{
		virtualControlPointerTransactions.insert(pointerID);
	}
}

bool GameController::onHandleEvent(AEvent & e)
{
	synchronizeInputLifecycle();
	EventTouchID completedPointerID = TOUCH_UNTOUCHEDID;
	if (e.eventType == ET_MOUSEUP
		&& e.eventData == MBC_MOUSE_LEFT)
	{
		completedPointerID = TOUCH_MOUSEID;
	}
	else if (e.eventType == ET_FINGERUP
		|| e.eventType == ET_FINGERCANCEL)
	{
		completedPointerID = e.eventData;
	}
	if (completedPointerID != TOUCH_UNTOUCHEDID)
	{
		virtualControlPointerTransactions.erase(completedPointerID);
	}
	if (!gm->global.data.canInput)
	{
		return false;
	}
	const bool isWorldPointerDown =
		e.eventType == ET_MOUSEDOWN || e.eventType == ET_FINGERDOWN;
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
		&& ((joystickPanel != nullptr
				&& joystickPanel->hasPointerDownInTree(pointerID))
			|| (skillPanel != nullptr
				&& skillPanel->hasPointerDownInTree(pointerID))))
	{
		virtualControlPointerTransactions.insert(pointerID);
		// A visible virtual control owns this concrete pointer transaction.
		// Its onEvent() callback still commits later in the frame; returning
		// here only prevents the same down edge from reaching world input.
		return true;
	}
	if (gm->menu != nullptr
		&& (isWorldPointerDown
			? gm->menu->blocksWorldPointerInput()
			: e.eventType == ET_KEYDOWN
				? gm->menu->blocksWorldKeyboardInput()
				: gm->menu->blocksWorldInput()))
	{
		return false;
	}

	if (e.eventType == ET_MOUSEDOWN || e.eventType == ET_FINGERDOWN)
	{
		gm->map->addWaterRipple(static_cast<float>(e.eventX), static_cast<float>(e.eventY));
	}

	if (e.eventType == ET_KEYDOWN)
	{
		if (e.eventData == KEY_V)
		{
			if (gm->player->isControllingCharacter())
			{
				return true;
			}
			tryToggleLegacySit();
			return true;
		}
		else if (e.eventData == KEY_Q || e.eventData == KEY_E)
		{
			bool hasShift = engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT);
			bool hasAlt = engine->getKeyPress(KEY_LALT) || engine->getKeyPress(KEY_RALT);
			bool useRightScript = engine->getKeyPress(KEY_LCTRL) || engine->getKeyPress(KEY_RCTRL);
			if (!hasShift && !hasAlt)
			{
				bool queued = e.eventData == KEY_Q
					? gm->queueNearestObjectInteraction(useRightScript, false, KeyboardAutoInteractionTileDistance)
					: gm->queueNearestNPCInteraction(useRightScript, false, KeyboardAutoInteractionTileDistance);
				if (queued)
				{
					return true;
				}
			}
		}
		else if (e.eventData == KEY_A || 
				e.eventData == KEY_S || 
				e.eventData == KEY_D ||
				e.eventData == KEY_F ||
				e.eventData == KEY_G)
		{
			if (gm->player->isControllingCharacter())
			{
				gm->showMessage("控制中不能使用武功");
				return true;
			}
			NextAction act;
			act.action = acMagic;
			if (std::shared_ptr<NPC> target = getClickedNPC(gm))
			{
				act.dest = target->getPosition();
				act.destGE = target;
			}
			else
			{
				act.dest = gm->getMousePoint();
			}
			switch (e.eventData)
			{
			case KEY_A:
				act.actionParam = 0;
				break;
			case KEY_S:
				act.actionParam = 1;
				break;
			case KEY_D:
				act.actionParam = 2;
				break;
			case KEY_F:
				act.actionParam = 3;
				break;
			case KEY_G:
				act.actionParam = 4;
				break;
			default:
				act.actionParam = 0;
				break;
			} 
			if (act.actionParam >= gm->magicManager.bottomCount())
			{
				return true;
			}
			_last_magic_index = act.actionParam;
			submitLegacyWorldAction(act);
			return true;
		}
		else if (e.eventData == KEY_Z || e.eventData == KEY_X || e.eventData == KEY_C)
		{
			int itemIndex = gm->goodsManager.bottomBegin();
			switch (e.eventData)
			{
			case KEY_Z: itemIndex += 0; break;
			case KEY_X: itemIndex += 1; break;
			case KEY_C: itemIndex += 2; break;
			default:
				break;
			}
			if (itemIndex < gm->goodsManager.bottomBegin() + gm->goodsManager.bottomCount())
			{
				if (gm->goodsManager.useItem(itemIndex))
				{
					prepareLegacyWorldAction();
				}
			}
			return true;
		}
	}
	else if (dragging == TOUCH_UNTOUCHEDID
		&& e.eventType == ET_MOUSEDOWN
		&& getPointerHitNPC(gm, e.eventX, e.eventY) == nullptr
		&& touchingID != TOUCH_UNTOUCHEDID)
	{
		if (e.eventData == MBC_MOUSE_LEFT)
		{
			MouseAlreadyDown = true;
			Point pos = gm->getMousePoint();

			NextAction act;

			if (engine->getKeyPress(KEY_LALT) || engine->getKeyPress(KEY_RALT))
			{
				act.action = acJump;
			}
			else if (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT))
			{
				act.action = acRun;
			}
			else
			{
				act.action = acWalk;
			}

			act.dest = pos;
			submitLegacyWorldAction(act);
			return true;
		}
		else if (e.eventData == MBC_MOUSE_RIGHT)
		{
			if (std::shared_ptr<NPC> tempNPC = getClickedNPC(gm))
			{
				if (tempNPC != nullptr && tempNPC->scriptFileRight != "")
				{
					NextAction act;
					if (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT))
					{
						act.action = acRun;
					}
					else
					{
						act.action = acWalk;
					}
					act.destGE = tempNPC;
					act.destKind = ndTalk;
					act.dest = tempNPC->getPosition();
					act.useRightScript = true;
					submitLegacyWorldAction(act);
					return true;
				}
			}
			if (std::shared_ptr<Object> tempObject = getClickedObject(gm))
			{
				if (tempObject != nullptr && tempObject->scriptFileRight != "")
				{
					NextAction act;
					if (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT))
					{
						act.action = acRun;
					}
					else
					{
						act.action = acWalk;
					}
					act.destGE = tempObject;
					act.destKind = ndObj;
					act.dest = tempObject->position;
					act.useRightScript = true;
					submitLegacyWorldAction(act);
					return true;
				}
			}
			if (_last_magic_index >= 0)
			{
				NextAction act;
				act.action = acMagic;
				if (std::shared_ptr<NPC> target = getClickedNPC(gm))
				{
					act.dest = target->getPosition();
					act.destGE = target;
				}
				else
				{
					act.dest = gm->getMousePoint();
				}
				act.actionParam = _last_magic_index;
				submitLegacyWorldAction(act);
				return true;
			}
		}
	}
	else if (std::shared_ptr<NPC> clickedNPC =
			getPointerHitNPC(gm, e.eventX, e.eventY);
		dragging == TOUCH_UNTOUCHEDID && e.eventType == ET_MOUSEDOWN
		&& clickedNPC != nullptr)
	{
		NextAction act;
		if (engine->getKeyPress(KEY_LALT) || engine->getKeyPress(KEY_RALT))
		{
			act.action = acJump;
		}
		else if (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT))
		{
			act.action = acRun;
		}
		else
		{
			act.action = acWalk;
		}
		if (act.action != acJump)
		{
			act.destGE = clickedNPC;
			bool useRightScript = clickedNPC->scriptFileRight != ""
				&& e.eventData == MBC_MOUSE_RIGHT;
			if (!useRightScript && clickedNPC->scriptFile == ""
				&& clickedNPC->scriptFileRight != "")
			{
				useRightScript = true;
			}
			if (useRightScript)
			{
				act.destKind = ndTalk;
				act.useRightScript = true;
			}
			else if (clickedNPC->isEnemy() || clickedNPC->isNoneFighter())
			{
				act.destKind = ndAttack;
			}
			else
			{
				act.destKind = ndTalk;
			}
		}
		act.dest = clickedNPC->getPosition();
		submitLegacyWorldAction(act);
	}
	else if (std::shared_ptr<Object> clickedObject = getClickedObject(gm);
		dragging == TOUCH_UNTOUCHEDID && e.eventType == ET_MOUSEDOWN
		&& clickedObject != nullptr)
	{
		NextAction act;
		if (engine->getKeyPress(KEY_LALT) || engine->getKeyPress(KEY_RALT))
		{
			act.action = acJump;
		}
		else if (engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT))
		{
			act.action = acRun;
		}
		else
		{
			act.action = acWalk;
		}
		if (act.action != acJump)
		{
			act.destGE = clickedObject;
			act.destKind = ndObj;
			act.useRightScript = clickedObject->scriptFileRight != ""
				&& e.eventData == MBC_MOUSE_RIGHT;
			if (!act.useRightScript
				&& clickedObject->shouldUseRightScriptForPrimaryInteraction())
			{
				act.useRightScript = true;
			}
		}
		act.dest = clickedObject->position;
		submitLegacyWorldAction(act);
	}
	else if (e.eventType == ET_FINGERDOWN && touchingID == e.eventData)
	{
		auto player = gm->player;
		auto actionActor = player->getActionActor();

		if (actionActor != nullptr && actionActor->nowAction != acDeath && actionActor->nowAction != acHide)
		{
			NextAction act;
			if (!player->isControllingCharacter() && player->canRun && (player->thew > (int)round((float)player->info.thewMax * MIN_THEW_RATE_TO_RUN) || player->thew > MIN_THEW_LIMIT_TO_RUN))
			{
				act.action = acRun;
			}
			else
			{
				act.action = acWalk;
			}
			Point pos = gm->getMousePoint(e.eventX, e.eventY);
			act.destGE.reset();
			act.dest = pos;
			submitLegacyWorldAction(act);
		}
		return true;
	}

	return false;
}


void GameController::setFastSelectBtn(
	int index,
	bool sVisible,
	std::string str,
	bool hostile)
{
	constexpr unsigned int DefaultFastSelectTextColor = 0xFF000000;
	constexpr unsigned int HostileFastSelectTextColor = 0xFFFF0000;
	if (skillPanel == nullptr || index < 0 || index >= FASTBTN_COUNT ||
		skillPanel->fastBtn[index] == nullptr)
	{
		return;
	}
	auto& button = skillPanel->fastBtn[index];
	button->setStrColor(hostile
		? HostileFastSelectTextColor
		: DefaultFastSelectTextColor);
	button->visible = sVisible;
	if (button->visible)
	{
		button->setUTF8Str(str);
	}
	else
	{
		button->setUTF8Str("");
	}
}


Point GameController::getPlayerRelativePosition(float angle, float distance, float xFactor)
{
	Point relativePos;
	relativePos.x = (int)round(-sin(angle) * distance * xFactor);
	relativePos.y = (int)round(cos(angle) * distance);
	auto actionActor = gm->player->getActionActor();
	auto playerPos = actionActor != nullptr
		? actionActor->getScreenPosition(gm->camera->position, gm->camera->offset)
		: gm->player->getScreenPosition(gm->camera->position, gm->camera->offset);
	playerPos.y -= TILE_HEIGHT / 2;
	relativePos = relativePos + playerPos;
	relativePos = gm->getMousePoint(relativePos.x, relativePos.y);
	return relativePos;
}
