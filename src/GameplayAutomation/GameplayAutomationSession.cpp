#include "GameplayAutomationSession.h"
#if defined(JXQY_ENABLE_AUTOMATION_HOOKS)
#include "../Game/GameManager/GameManager.h"
#include "../Engine/Engine.h"
#include "../Game/GameManager/GameController.h"
#include "../Game/Data/MagicRegionShape.h"
#include "../Game/GameManager/MenuController.h"
#include "../Game/Menu/Dialog.h"
#include "../Game/Menu/ChooseMenu.h"
#include "../Game/Menu/SaveLoad.h"
#include "../Game/Menu/BuySellMenu.h"
#include "../Component/VideoPlayer.h"
#include "../Resource/ResourceManager.h"
#include "../File/log.h"
#include "../JxqyEngineVersion.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <random>

using namespace GameplayAutomation;

namespace
{
std::string traceSessionId()
{
    std::random_device random;
    std::string id = "00000000-0000-4000-8000-000000000000";
    const char digits[] = "0123456789abcdef";
    for (std::size_t i = 0; i < id.size(); ++i)
        if (id[i] != '-' && i != 14 && i != 19) id[i] = digits[random() & 15];
    return id;
}
Value position(Point point)
{
	auto value = object();
	value.objectValues = {{"x", number(point.x)}, {"y", number(point.y)}};
	return value;
}
Point destination(const Value& arguments)
{
	return {integerMember(arguments, "x", 0, 2047), integerMember(arguments, "y", 0, 2047)};
}
bool has(const Value& arguments, const std::string& key)
{
	return arguments.objectValues.count(key) != 0;
}
int optionalInteger(const Value& arguments, const std::string& key, int fallback, int minimum, int maximum)
{
	return has(arguments, key) ? integerMember(arguments, key, minimum, maximum) : fallback;
}
int skillReach(const Magic& magic, int level, const NPC& actor)
{
	if (magic.hasPositionCastLimit(level))
		return MAGIC_MAX_CAST_DISTANCE;
	if (magic.level[level].moveKind == mmkRegion
		&& (magic.level[level].region == mrWave || magic.level[level].region == mrCross))
		return getMagicRegionShapeRange(level);
	const int reach = actor.estimatePhysicalReach(magic, level);
	return reach > 0 ? reach : actor.attackRadius;
}
bool skillPathIsClear(const Magic& magic, int level, Point from, const std::shared_ptr<NPC>& target)
{
	auto* manager = GameManager::getInstance();
	auto& map = *manager->map;
	const Point to = target->getPosition();
	const int moveKind = magic.level[level].moveKind;
	if (moveKind == mmkRegion)
	{
		// Square effects spawn around the aim point; crosses and waves use fixed tiles.
		if (magic.level[level].region == mrSquare) return true;
		if (magic.level[level].region == mrCross)
			return manager->player->isCrossHit(from, to, getMagicRegionShapeRange(level));
		if (magic.level[level].region == mrWave)
		{
			const auto tiles = getWaveMagicRegionTiles(from, NPC::getDirection(from, to), level);
			return std::any_of(tiles.begin(), tiles.end(), [&](const auto& tile) { return tile.position == to; });
		}
	}
	if (!map.canSee(from, to)) return false;
	const bool sector = moveKind == mmkSector || moveKind == mmkRandSector;
	if (magic.passThroughWall > 0 || (!sector && moveKind != mmkFly && moveKind != mmkFlyContinuous && moveKind != mmkFollow))
		return true;
	if (from == to) return map.canFly(to);
	// Sector effects start on the caster's tile; fly effects start one tile ahead.
	// Check the aimed projectile's width sweep, not just character visibility.
	Effect projectile;
	projectile.position = projectile.src = sector ? from : Map::getSubPoint(from, NPC::getDirection(from, to));
	projectile.flyingDirection = Map::getTilePosition(to, projectile.src == to ? from : projectile.src);
	projectile.flyingDirection.y = static_cast<int>(std::round(MapXRatio * projectile.flyingDirection.y));
	const auto path = projectile.getPassPath(projectile.src, {0, 0}, to, {0, 0});
	// Native collision checks the NPC before the obstacle on that same tile.
	// A target standing on a projectile-blocking tile can still receive the hit.
	const bool targetReceivesCollision = magic.carryUser != 3 && magic.meteorMove <= 0
		&& target->getJumpState() != jsJumping
		&& target->isFighterLike() && manager->npcManager->findNPC(target)
		&& (target->kind != nkPartner || manager->global.data.PartnerCombat || magic.attackAll > 0)
		&& WorldInteractionResolver::isNPCValidForIntent(target, WorldInteractionIntent::Attack, manager->player);
	return map.canFly(projectile.src) && (map.canFly(to) || targetReceivesCollision)
		&& std::all_of(path.begin(), path.end(), [&](Point point)
			{ return map.canFly(point) || (point == to && targetReceivesCollision); });
}
const std::map<std::string, UIAction> uiActions = {
	{"Up", UIAction::NavigateUp}, {"Down", UIAction::NavigateDown},
	{"Left", UIAction::NavigateLeft}, {"Right", UIAction::NavigateRight},
	{"Confirm", UIAction::Confirm}, {"Cancel", UIAction::Cancel},
	{"Secondary", UIAction::Secondary}, {"Details", UIAction::Details},
	{"PanelPrevious", UIAction::PanelPrevious}, {"PanelNext", UIAction::PanelNext},
	{"PagePrevious", UIAction::PagePrevious}, {"PageNext", UIAction::PageNext},
	{"ScrollUp", UIAction::ScrollUp}, {"ScrollDown", UIAction::ScrollDown}};
}

GameplayAutomationSession* GameplayAutomationSession::active = nullptr;
std::string GameplayAutomationSession::currentScript;

GameplayAutomationSession::GameplayAutomationSession(
	const std::string& pipeName, const std::filesystem::path& userRoot) :
	pipe(pipeName), outputRoot(userRoot / "automation")
{
	if (active) throw std::runtime_error("automation_session_already_exists");
	std::error_code error;
	std::filesystem::create_directories(outputRoot, error);
	events.open(outputRoot / "events.jsonl", std::ios::binary | std::ios::trunc);
	eventOutputFailed = !events;
	traceFile = std::make_shared<std::ofstream>(outputRoot / "trace.jsonl", std::ios::binary | std::ios::trunc);
	if (*traceFile)
	{
		writer = EditorRun::RuntimeTraceWriter::create(traceSessionId(),
			[file = traceFile](std::string_view bytes)
			{
				file->write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
				file->flush();
				return static_cast<bool>(*file);
			});
	}
	active = this;
	currentScript.clear();
}

GameplayAutomationSession::~GameplayAutomationSession()
{
	active = nullptr;
	if (writer) writer->finish(EditorRun::RuntimeTraceSessionFinishStatus::Completed);
}

bool GameplayAutomationSession::enabled() { return active != nullptr; }
EditorRun::RuntimeTraceWriter* GameplayAutomationSession::traceWriter()
{
	return active ? active->writer.get() : nullptr;
}

void GameplayAutomationSession::contextChanged()
{
	if (active) ++active->context;
}

void GameplayAutomationSession::worldChanged()
{
	if (!active) return;
	++active->generation;
	active->entities.clear();
	active->entityIds.clear();
}

void GameplayAutomationSession::interactionStarted(const std::shared_ptr<Element>& target)
{
	if (!active || !active->worldAction || !target) return;
	auto& action = active->actions.at(active->worldAction);
	if (action.command != "Interact") return;
	const auto found = active->entities.find(integer(member(action.arguments, "targetId")));
	if (found != active->entities.end() && found->second.lock() == target)
		active->finish(action, "succeeded", "interaction_started");
}

void GameplayAutomationSession::targetDefeated(const std::shared_ptr<NPC>& target)
{
	if (!active || !active->worldAction || !target) return;
	auto& action = active->actions.at(active->worldAction);
	if (action.command != "StartCombat" || action.generation != active->generation
		|| action.target.lock() != target) return;
	// Consume the target before a death script can revive, delete or replace it.
	++action.kills;
	action.target.reset();
	action.lastProgress = SDL_GetTicks();
	if (action.kills >= optionalInteger(action.arguments, "kills", 1, 1, 100))
		active->finish(action, "succeeded", "enemies_defeated");
}

void GameplayAutomationSession::frameStarted()
{
	if (!active || !SDL_IsMainThread()) return;
	++active->frame;
	auto* manager = GameManager::getInstance();
	if (manager && manager->inThread.load()) onFrame();
}

void GameplayAutomationSession::manualInput()
{
	if (!active || !active->pipe.isConnected()) return;
	const auto connection = active->pipe.connectionId();
	if (active->manualInputConnection == connection) return;
	active->manualInputConnection = connection;
	active->autoDialogue = false;
	active->cancelWorld("manual_input");
	active->record("manual.input", object());
}

void GameplayAutomationSession::skillUsed(const std::shared_ptr<Magic>& magic)
{
	if (!active || !active->worldAction || !magic) return;
	auto& action = active->actions.at(active->worldAction);
	if (action.command == "CastSkill" && action.magic.lock() == magic)
		action.observedExecution = true;
}

void GameplayAutomationSession::onFrame()
{
	if (!active || !SDL_IsMainThread()) return;
	try { active->tick(); }
	catch (const std::exception& error)
	{
		GameLog::write("Automation frame error: %s\n", error.what());
		active->autoDialogue = false;
		active->cancelWorld("controller_error");
	}
}

Element* GameplayAutomationSession::owner() const
{
	return Element::runningElement.empty() ? nullptr : Element::runningElement.back().get();
}

std::uint64_t GameplayAutomationSession::identify(const std::shared_ptr<Element>& element)
{
	if (!element) return 0;
	const std::weak_ptr<Element> weak = element;
	auto found = entityIds.find(weak);
	if (found != entityIds.end()) return found->second;
	if (entityIds.size() >= 4096)
	{
		for (auto entry = entityIds.begin(); entry != entityIds.end();)
		{
			if (entry->first.expired())
			{
				entities.erase(entry->second);
				entry = entityIds.erase(entry);
			}
			else ++entry;
		}
	}
	if (entityIds.size() >= 16384) throw std::runtime_error("too_many_observed_entities");
	const auto id = nextEntity++;
	entityIds.emplace(weak, id);
	entities.emplace(id, weak);
	return id;
}

std::shared_ptr<Element> GameplayAutomationSession::resolve(std::uint64_t id)
{
	auto found = entities.find(id);
	auto value = found == entities.end() ? nullptr : found->second.lock();
	if (!value) throw std::runtime_error("stale_target");
	return value;
}

bool GameplayAutomationSession::ownsVisibleElement(const std::shared_ptr<Element>& element) const
{
	for (Element* current = element.get(); current; current = current->parent)
	{
		if (!current->visible || !current->activated) return false;
		if (current == owner()) return true;
	}
	return false;
}

bool GameplayAutomationSession::worldInputAllowed() const
{
	auto* manager = GameManager::getInstance();
	auto* engine = Engine::getInstance();
	return engine && engine->isApplicationActive() && engine->isFrameReady()
		&& manager && !manager->inThread.load() && manager->controller
		&& manager->controller->canHandleWorldInput()
		&& !Element::currentRunOwnerBlocksParentInput();
}

void GameplayAutomationSession::requireWorld(const Value& arguments) const
{
	if (integer(member(arguments, "generation")) != static_cast<std::int64_t>(generation))
		throw std::runtime_error("stale_world");
	if (!worldInputAllowed()) throw std::runtime_error("world_input_blocked");
}

void GameplayAutomationSession::requireContext(const Value& arguments) const
{
	if (integer(member(arguments, "context")) != static_cast<std::int64_t>(context))
		throw std::runtime_error("stale_ui_context");
	if (!owner() || !owner()->logicRunning || !Engine::getInstance()->isApplicationActive())
		throw std::runtime_error("ui_input_blocked");
}

Value GameplayAutomationSession::uiState()
{
	auto result = array();
	for (auto* focus : UIFocusManager::instances)
	{
		for (const auto& node : focus->nodes)
		{
			auto element = node.element.lock();
			if (!element || !ownsVisibleElement(element) || !focus->isNodeAvailable(node)) continue;
			auto item = object();
			item.objectValues = {{"id", number(identify(element))}, {"name", string(node.id)},
				{"component", string(element->name)}, {"focused", boolean(focus->getFocusedElement() == element)}};
			if (auto label = std::dynamic_pointer_cast<Item>(element))
			{
				item.objectValues["text"] = string(label->getStr());
				item.objectValues["slot"] = number(label->dragIndex);
				item.objectValues["transferSelected"] = boolean(label->isTransferSelected());
			}
			item.objectValues["x"] = number(element->rect.x);
			item.objectValues["y"] = number(element->rect.y);
			result.arrayValues.push_back(std::move(item));
		}
	}
	return result;
}

void GameplayAutomationSession::updateContext()
{
	std::string signature = owner() ? owner()->name : "none";
	if (auto* dialog = dynamic_cast<Dialog*>(owner()))
		signature += "|" + dialog->currentTalkText + "|" + std::to_string(dialog->index);
	if (auto* choose = dynamic_cast<ChooseMenu*>(owner()))
	{
		signature += "|" + choose->currentMessage;
		for (const auto& option : choose->currentOptions) signature += "|" + option;
	}
	for (auto* focus : UIFocusManager::instances)
		for (const auto& node : focus->nodes)
		{
			auto element = node.element.lock();
			if (element && ownsVisibleElement(element) && focus->isNodeAvailable(node))
			{
				signature += "|" + std::to_string(identify(element));
				if (auto item = std::dynamic_pointer_cast<Item>(element))
					signature += ":" + std::to_string(item->dragIndex) + ":" + item->getStr()
						+ ":" + std::to_string(reinterpret_cast<std::uintptr_t>(item->impImage.get()));
			}
		}
	if (signature != uiSignature)
	{
		uiSignature = std::move(signature);
		++context;
		if (auto* dialog = dynamic_cast<Dialog*>(owner()))
		{
			auto detail = object();
			detail.objectValues = {{"text", string(dialog->currentTalkText)}, {"page", number(dialog->index)},
				{"script", string(currentScript)}, {"context", number(context)}};
			record("dialogue", std::move(detail));
		}
	}
}

Value GameplayAutomationSession::actionState(const Action& action) const
{
	auto value = object();
	value.objectValues = {{"actionId", number(action.id)}, {"command", string(action.command)},
		{"status", string(action.status)}, {"reason", string(action.reason)}, {"kills", number(action.kills)}};
	return value;
}

Value GameplayAutomationSession::observe(const Value& arguments)
{
	fields(arguments, {"variables"});
	auto value = object();
	value.objectValues = {{"frame", number(frame)}, {"generation", number(generation)},
		{"context", number(context)}, {"scene", string(owner() ? owner()->name : "initializing")},
		{"script", string(currentScript)}, {"autoDialogue", boolean(autoDialogue)},
		{"worldInput", boolean(worldInputAllowed())}, {"connected", boolean(pipe.isConnected())},
		{"outputHealthy", boolean(!eventOutputFailed && writer && writer->valid())}};
	const auto& manifest = ResourceManager::instance().getActiveManifest();
	value.objectValues["resourceId"] = string(manifest.id);
	value.objectValues["resourceVersion"] = string(manifest.releaseMetadata.displayVersion);
	value.objectValues["engineVersion"] = string(JxqyBuildVersion::EngineVersion);
	auto* manager = GameManager::getInstance();
	const bool loading = manager && manager->inThread.load();
	value.objectValues["loading"] = boolean(loading);
	if (loading) return value;
	if (manager)
	{
		value.objectValues["cheatModeEnabled"] = boolean(manager->isCheatModeEnabled());
		value.objectValues["cheatInvincibilityEnabled"] = boolean(manager->isCheatInvincibilityEnabled());
		value.objectValues["partnerCombatEnabled"] = boolean(manager->global.data.PartnerCombat);
		auto timer = object();
		timer.objectValues = {{"started", boolean(manager->timerStarted)}, {"hidden", boolean(manager->timerHidden)},
			{"remainingSeconds", number(manager->timerSeconds)}, {"accumulatedMilliseconds", number(manager->timerAccumulated)},
			{"callbackSet", boolean(manager->timeScriptSet)}, {"triggerSeconds", number(manager->timeScriptSeconds)},
			{"script", string(manager->timeScriptFileName)}};
		value.objectValues["timer"] = std::move(timer);
	}
	value.objectValues["ui"] = uiState();
	if (auto* dialog = dynamic_cast<Dialog*>(owner()))
	{
		auto talk = object();
		talk.objectValues = {{"text", string(dialog->currentTalkText)}, {"page", number(dialog->index)},
			{"pages", number(dialog->talkStrList.size())},
			{"complete", boolean(dialog->label && dialog->label->isPageComplete())}};
		value.objectValues["dialogue"] = std::move(talk);
	}
	if (auto* choose = dynamic_cast<ChooseMenu*>(owner()))
	{
		auto choices = array();
		for (std::size_t index = 0; index < choose->currentOptions.size(); ++index)
		{
			if (!choose->isChoiceOptionVisible(index)) continue;
			auto option = object();
			option.objectValues = {{"index", number(index)}, {"text", string(choose->currentOptions[index])}};
			choices.arrayValues.push_back(std::move(option));
		}
		value.objectValues["choices"] = std::move(choices);
		value.objectValues["choiceMessage"] = string(choose->currentMessage);
		value.objectValues["choiceCount"] = number(choose->multipleSelectionMode ? choose->multipleSelection.limit() : 1);
	}
	if (auto* save = dynamic_cast<SaveLoad*>(owner())) value.objectValues["saveSlot"] = number(save->index);
	if (auto* video = dynamic_cast<VideoPlayer*>(owner())) value.objectValues["video"] = string(video->videoFileName);
	if (auto* shop = dynamic_cast<BuySellMenu*>(owner()))
	{
		auto goods = array();
		for (int i = 0; i < BUYSELL_GOODS_COUNT; ++i)
		{
			const auto& item = shop->goodsList[i];
			if (!item.goods) continue;
			auto row = object();
			row.objectValues = {{"slot", number(i)}, {"file", string(item.iniFile)}, {"quantity", number(item.number)}};
			goods.arrayValues.push_back(std::move(row));
		}
		value.objectValues["shop"] = std::move(goods);
	}
	if (!manager || !manager->player) return value;
	value.objectValues["map"] = string(manager->global.data.mapName);
	value.objectValues["inEvent"] = boolean(manager->inEvent);
	auto player = object();
	auto actor = manager->player->getActionActor();
	player.objectValues = {{"position", position(actor->getPosition())},
		{"life", number(actor->life)}, {"lifeMax", number(actor->getLifeMax())},
		{"mana", number(actor->mana)}, {"manaMax", number(actor->getManaMax())},
		{"thew", number(actor->thew)}, {"action", number(static_cast<int>(actor->nowAction))},
		{"sitting", boolean(actor->isSitting())},
		{"level", number(manager->player->level)}, {"money", number(manager->player->money)},
		{"levelFile", string(manager->player->levelIni)},
		{"canFight", boolean(manager->player->canFight)}, {"canJump", boolean(manager->player->canJump)},
		{"controlled", boolean(manager->player->isControllingCharacter())}};
	value.objectValues["player"] = std::move(player);
	auto levelUp = object();
	levelUp.objectValues["effectMode"] = string(
		manifest.levelUpEffectMode == LevelUpEffectMode::Replace ? "Replace" : "Append");
	const auto levelUpCandidates = manifest.getLevelUpEffectCandidates(manager->player->sex);
	auto candidateFiles = array();
	for (const auto& file : levelUpCandidates) candidateFiles.arrayValues.push_back(string(file));
	levelUp.objectValues["candidates"] = std::move(candidateFiles);
	auto levelUpEffects = array();
	if (manager->effectManager)
	{
		for (const auto& effect : manager->effectManager->effectList)
		{
			if (!effect || std::find(levelUpCandidates.begin(), levelUpCandidates.end(),
				effect->magic.iniName) == levelUpCandidates.end()) continue;
			auto row = object();
			row.objectValues = {{"file", string(effect->magic.iniName)},
				{"position", position(effect->position)},
				{"imageLoaded", boolean(effect->magic.flyImage != nullptr)},
				{"durationMs", number(effect->getExplodinUTime())}};
			levelUpEffects.arrayValues.push_back(std::move(row));
		}
	}
	levelUp.objectValues["activeEffects"] = std::move(levelUpEffects);
	value.objectValues["levelUp"] = std::move(levelUp);
	auto targets = array();
	if (manager->npcManager)
		for (auto& npc : manager->npcManager->npcList)
		{
			if (!npc || !npc->isVisibleForRuntime()) continue;
			auto target = object();
			target.objectValues = {{"id", number(identify(npc))}, {"kind", string("npc")},
				{"name", string(npc->npcName)}, {"position", position(npc->getPosition())},
				{"hostile", boolean(npc->isEnemy())}, {"life", number(npc->life)},
				{"attackable", boolean(WorldInteractionResolver::isNPCValidForIntent(npc, WorldInteractionIntent::Attack, actor))},
				{"visibleFromPlayer", boolean(actor->canSee(npc->getPosition()))},
				{"attackRadius", number(npc->attackRadius)},
				{"hasWalkAction", boolean(npc->canDoAction(&npc->res.walk) || npc->canDoAction(&npc->res.awalk))},
				{"interactive", boolean(npc->isInteractive())}, {"action", number(static_cast<int>(npc->nowAction))},
				{"direction", number(npc->direction)}, {"asyncMovement", boolean(npc->haveAsyncDest)}};
			targets.arrayValues.push_back(std::move(target));
		}
	if (manager->objectManager)
		for (auto& item : manager->objectManager->objectList)
		{
			if (!item || !item->visible) continue;
			auto target = object();
			target.objectValues = {{"id", number(identify(item))}, {"kind", string("object")},
				{"name", string(item->objName)}, {"position", position(item->position)}};
			targets.arrayValues.push_back(std::move(target));
		}
	value.objectValues["targets"] = std::move(targets);
	auto inventory = array();
	for (int i = 0; i < manager->goodsManager.listLength(); ++i)
	{
		const auto& goods = manager->goodsManager.goodsList[i];
		if (!goods.goods || goods.number <= 0) continue;
		auto item = object();
		item.objectValues = {{"slot", number(i)}, {"file", string(goods.iniFile)},
			{"quantity", number(goods.number)}, {"cooldownMs", number(goods.remainColdMilliseconds)}};
		inventory.arrayValues.push_back(std::move(item));
	}
	value.objectValues["inventory"] = std::move(inventory);
	auto magic = array();
	for (int i = 0; i < manager->magicManager.listLength(); ++i)
	{
		const auto& entry = manager->magicManager.magicList[i];
		if (!entry.magic) continue;
		auto item = object();
		item.objectValues = {{"slot", number(i)}, {"file", string(entry.iniFile)},
			{"level", number(entry.level)}, {"exp", number(entry.exp)}, {"cooldownMs", number(entry.remainColdMilliseconds)}};
		magic.arrayValues.push_back(std::move(item));
	}
	value.objectValues["magic"] = std::move(magic);
	auto layout = object();
	layout.objectValues = {{"goodsQuickBegin", number(manager->goodsManager.bottomBegin())},
		{"equipmentBegin", number(manager->goodsManager.equipBegin())},
		{"magicQuickBegin", number(manager->magicManager.bottomBegin())},
		{"practiceSlot", number(manager->magicManager.practiceIndex())}};
	value.objectValues["layout"] = std::move(layout);
	auto variables = object();
	if (has(arguments, "variables"))
	{
		const auto& names = member(arguments, "variables");
		if (names.type != Type::Array || names.arrayValues.size() > 128) throw std::runtime_error("invalid_variables");
		for (const auto& name : names.arrayValues)
		{
			if (name.type != Type::String || name.text.size() > 256) throw std::runtime_error("invalid_variable_name");
			variables.objectValues[name.text] = manager->varList.ini
				? string(manager->varList.ini->Get(VARIABLE_SECTION, name.text, "")) : Value{};
		}
	}
	value.objectValues["variables"] = std::move(variables);
	if (worldAction) value.objectValues["worldAction"] = actionState(actions.at(worldAction));
	return value;
}

void GameplayAutomationSession::record(const std::string& event, Value data)
{
	if (eventOutputFailed) return;
	try
	{
		auto value = object();
		value.objectValues = {{"event", string(event)}, {"frame", number(frame)},
			{"milliseconds", number(SDL_GetTicks())}, {"data", std::move(data)}};
		const auto bytes = serialize(value);
		events.write(bytes.data(), bytes.size());
		events.flush();
		if (!events) eventOutputFailed = true;
	}
	catch (...) { eventOutputFailed = true; }
}

void GameplayAutomationSession::finish(Action& action, std::string status, std::string reason)
{
	if (action.status != "running") return;
	action.status = std::move(status);
	action.reason = std::move(reason);
	if (worldAction == action.id)
	{
		if (action.command == "StartCombat" || action.status == "failed" || action.status == "cancelled")
			stopQueuedWorldInput();
		worldAction = 0;
	}
	record("action.finish", actionState(action));
}

void GameplayAutomationSession::cancelWorld(const std::string& reason)
{
	if (!worldAction) return;
	finish(actions.at(worldAction), "cancelled", reason);
}

void GameplayAutomationSession::stopQueuedWorldInput()
{
	auto* manager = GameManager::getInstance();
	if (manager && !manager->inThread.load() && !manager->inEvent && manager->controller && manager->player)
	{
		manager->controller->cancelControllerWorldInteraction(false);
		manager->player->nextAction.reset();
		if (auto actor = manager->player->getActionActor())
		{
			// Finish the active tile step before a replacement skill or movement.
			if ((actor->isWalking() || actor->isRunning()) && actor->stepList.size() > 1)
			{
				actor->stepList.resize(1);
			}
		}
	}
}

void GameplayAutomationSession::processRequest(const std::shared_ptr<AutomationPipe::Request>& request)
{
	try
	{
		if (request->command == "Observe")
		{
			pipe.reply(request, response(request->id, true, observe(request->arguments)));
			return;
		}
		if (request->command == "GetActionStatus")
		{
			fields(request->arguments, {"actionId"});
			auto found = actions.find(integer(member(request->arguments, "actionId")));
			if (found == actions.end()) throw std::runtime_error("unknown_action");
			pipe.reply(request, response(request->id, true, actionState(found->second)));
			return;
		}
		if (request->command == "CancelAction")
		{
			fields(request->arguments, {"actionId"});
			const auto id = integer(member(request->arguments, "actionId"));
			if (!actions.count(id)) throw std::runtime_error("unknown_action");
			if (worldAction == id) cancelWorld("client_cancelled");
			else finish(actions.at(id), "cancelled", "client_cancelled");
			if (captureAction == id) captureAction = 0;
			pipe.reply(request, response(request->id, true, actionState(actions.at(id))));
			return;
		}
		if (actions.size() >= 256)
		{
			auto found = std::find_if(actions.begin(), actions.end(), [](const auto& item)
				{ return item.second.status != "running" && !item.second.executing; });
			if (found == actions.end()) throw std::runtime_error("too_many_actions");
			actions.erase(found);
		}
		Action action;
		action.id = nextAction++;
		action.command = request->command;
		action.arguments = request->arguments;
		action.generation = generation;
		action.started = action.lastProgress = SDL_GetTicks();
		auto& stored = actions.emplace(action.id, std::move(action)).first->second;
		// Reply before a normal UI callback enters a nested run(). Nested frames
		// can then service the next request without executing this one again.
		pipe.reply(request, response(request->id, true, actionState(stored)));
		record("action.start", actionState(stored));
		stored.executing = true;
		try { execute(stored); }
		catch (const std::exception& error) { finish(stored, "failed", error.what()); }
		stored.executing = false;
	}
	catch (const std::exception& error)
	{
		pipe.reply(request, response(request->id, false, string(error.what())));
	}
}

void GameplayAutomationSession::tick()
{
	for (auto& entry : actions)
	{
		auto& action = entry.second;
		if (action.completingDispatch && action.status == "running" && frame > action.dispatchFrame)
			finish(action, "succeeded", "ui_action_dispatched");
	}
	if (captureAction && SDL_GetTicks() - actions.at(captureAction).started > 10000)
	{
		finish(actions.at(captureAction), "failed", "capture_timeout");
		captureAction = 0;
	}
	auto* manager = GameManager::getInstance();
	if (manager && manager->inThread.load())
	{
		if (auto request = pipe.take())
		{
			if (request->command == "Observe" || request->command == "GetActionStatus"
				|| request->command == "CancelAction") processRequest(request);
			else pipe.reply(request, response(request->id, false, string("loading")));
		}
		return;
	}
	updateContext();
	if (manager && lastMap != manager->global.data.mapName)
	{
		lastMap = manager->global.data.mapName;
		record("map.change", string(lastMap));
	}
	if (const auto disconnectedConnection = pipe.takeDisconnected())
	{
		autoDialogue = false;
		cancelWorld("client_disconnected");
		record("client.disconnected", object());
		if (disconnectedConnection == pipe.connectionId() && manualInputConnection != disconnectedConnection
			&& worldInputAllowed() && manager && manager->menu) manager->menu->openSystemMenu();
	}
	if (auto request = pipe.take()) processRequest(request);
	// A nested callback may have switched the complete world and active owner.
	manager = GameManager::getInstance();
	if (manager && manager->inThread.load()) return;
	updateContext();
	if (autoDialogue && pipe.isConnected())
	{
		auto* dialog = dynamic_cast<Dialog*>(owner());
		const auto now = SDL_GetTicks();
		if (dialog && dialog->logicRunning && presentedContext == context
			&& now - lastDialogueAdvance >= static_cast<std::uint64_t>(dialogueInterval)
			&& Engine::getInstance()->isApplicationActive())
		{
			lastDialogueAdvance = now;
			Element::dispatchUIAction(UIAction::Confirm);
			presentedContext = 0;
		}
	}
	updateWorldAction();
}

void GameplayAutomationSession::beforePresent(EngineBase& renderedEngine)
{
	if (!active || !SDL_IsMainThread()) return;
	auto& session = *active;
	auto* manager = GameManager::getInstance();
	if (manager && manager->inThread.load()) return;
	try { session.updateContext(); }
	catch (...) { session.eventOutputFailed = true; return; }
	if (auto* dialog = dynamic_cast<Dialog*>(session.owner()))
	{
		if (dialog->label && dialog->label->isPageComplete()) session.presentedContext = session.context;
		// A typing page also needs its first confirmation to reveal the text.
		else if (dialog->label) session.presentedContext = session.context;
	}
	if (!session.captureAction) return;
	const auto id = session.captureAction;
	session.captureAction = 0;
	auto& action = session.actions.at(id);
	try
	{
		auto* engine = Engine::getInstance();
		std::unique_ptr<char[]> bytes;
		const int size = engine->saveImageToPngMemory(renderedEngine.realScreen,
			renderedEngine.width, renderedEngine.height, bytes);
		if (size <= 0 || !bytes) throw std::runtime_error("capture_failed");
		const auto path = session.outputRoot / ("frame-" + std::to_string(id) + ".png");
		std::ofstream output(path, std::ios::binary | std::ios::trunc);
		output.write(bytes.get(), size);
		output.close();
		if (!output) throw std::runtime_error("capture_write_failed");
		session.finish(action, "succeeded", path.u8string());
	}
	catch (const std::exception& error) { session.finish(action, "failed", error.what()); }
}

bool GameplayAutomationSession::queueSkill(int slot, const std::shared_ptr<NPC>& target, bool requireReach,
	std::string* unavailableReason)
{
	auto unavailable = [unavailableReason](const char* reason)
	{
		if (unavailableReason) *unavailableReason = reason;
		return false;
	};
	auto* manager = GameManager::getInstance();
	auto player = manager->player;
	if (!player->canFight || player->isControllingCharacter()
		|| slot < 0 || slot >= manager->magicManager.bottomCount()) return unavailable("configured_skill_unavailable");
	const int index = manager->magicManager.bottomIndex(slot);
	if (!manager->magicManager.magicListExists(index)) return unavailable("configured_skill_unavailable");
	const auto& magic = manager->magicManager.magicList[index];
	if (!magic.magic || magic.magic->disableUse
		|| magic.level < 1 || magic.level > MAGIC_MAX_LEVEL) return unavailable("configured_skill_unavailable");
	const auto prepared = player->resolveMagicReplacement(magic.magic);
	if (!prepared || !player->canUseMagicByState(prepared, false)) return unavailable("configured_skill_unavailable");
	const auto& cost = prepared->level[magic.level];
	if ((!player->hasUnlimitedCheatResources() && (player->mana < cost.manaCost || player->thew < cost.thewCost))
		|| player->life < cost.lifeCost || !player->canUseMana
		|| (manager->global.feature.rageSystem && player->rage < cost.rageCost)
		|| (!prepared->goodsName.empty() && manager->goodsManager.getItemNum(prepared->goodsName) <= 0))
		return unavailable("skill_resources_unavailable");
	if (magic.remainColdMilliseconds > 0) return unavailable("skill_cooldown");
	const auto goal = target ? target->getPosition() : player->getPosition();
	if (requireReach && target)
	{
		if (Map::calDistance(player->getPosition(), goal) > skillReach(*prepared, magic.level, *player)
			|| !skillPathIsClear(*prepared, magic.level, player->getPosition(), target))
		{
			return unavailable("skill_out_of_range");
		}
	}
	const bool instantCure = cost.moveKind == mmkSelf && cost.specialKind == mskClearAbnormalState;
	if (!instantCure && (!player->canDoAction(acMagic) || player->immobilized || player->petrified
		|| !player->canActToward(goal, player->getMagicActionDirectionCount(prepared))))
		return unavailable("configured_skill_unavailable");
	NextAction next;
	next.action = acMagic;
	next.actionParam = slot;
	next.destGE = target;
	next.dest = target ? target->getPosition() : player->getPosition();
	return player->addNextAction(next) || unavailable("configured_skill_unavailable");
}

void GameplayAutomationSession::execute(Action& action)
{
	const auto& arguments = action.arguments;
	const auto& command = action.command;
	auto* manager = GameManager::getInstance();
	if (command != "CaptureFrame" && manualInputConnection != 0
		&& manualInputConnection == pipe.connectionId()) throw std::runtime_error("manual_input");
	if (command == "SetAutoDialogue")
	{
		fields(arguments, {"enabled", "intervalMs"});
		const bool enabled = boolMember(arguments, "enabled");
		const int interval = optionalInteger(arguments, "intervalMs", 100, 16, 10000);
		autoDialogue = enabled;
		dialogueInterval = interval;
		finish(action, "succeeded", "dialogue_policy_updated");
		return;
	}
	if (command == "CaptureFrame")
	{
		fields(arguments, {});
		if (captureAction) throw std::runtime_error("capture_pending");
		captureAction = action.id;
		return;
	}
	if (command == "Choose")
	{
		fields(arguments, {"context", "options"});
		requireContext(arguments);
		auto* choose = dynamic_cast<ChooseMenu*>(owner());
		if (!choose) throw std::runtime_error("not_a_choice");
		const auto& options = member(arguments, "options");
		if (options.type != Type::Array || options.arrayValues.empty()
			|| options.arrayValues.size() > 64) throw std::runtime_error("invalid_options");
		std::vector<int> selected;
		for (const auto& item : options.arrayValues)
		{
			const auto index = integer(item);
			if (index < 0 || index >= static_cast<std::int64_t>(choose->currentOptions.size())
				|| !choose->isChoiceOptionVisible(static_cast<std::size_t>(index))
				|| std::find(selected.begin(), selected.end(), index) != selected.end())
				throw std::runtime_error("unavailable_option");
			selected.push_back(static_cast<int>(index));
		}
		if (!choose->multipleSelectionMode && selected.size() != 1) throw std::runtime_error("single_choice_required");
		if (choose->multipleSelectionMode && selected.size() != static_cast<std::size_t>(choose->multipleSelection.limit()))
			throw std::runtime_error("selection_count_not_satisfied");
		if (choose->multipleSelectionMode)
		{
			choose->clearMultipleSelection();
			for (int index : selected) choose->toggleMultipleOption(index);
			choose->finishMultipleSelection();
			if (choose->logicRunning) throw std::runtime_error("selection_count_not_satisfied");
		}
		else choose->selectChoice(selected.front());
		finish(action, "succeeded", "choice_selected");
		return;
	}
	if (command == "FocusUI")
	{
		fields(arguments, {"context", "targetId"});
		requireContext(arguments);
		auto target = resolve(integer(member(arguments, "targetId")));
		if (!ownsVisibleElement(target)) throw std::runtime_error("unavailable_widget");
		bool focused = false;
		if (auto* shop = dynamic_cast<BuySellMenu*>(owner()))
			focused = shop->controllerPaneRouter.focusControllerElement(target);
		if (!focused && manager && manager->menu && !Element::currentRunOwnerBlocksParentInput())
			focused = manager->menu->adoptControllerPointerFocus(target);
		if (!focused)
			for (auto* focus : UIFocusManager::instances)
				if (focus->focusElement(target)) { focused = true; break; }
		if (!focused) throw std::runtime_error("widget_cannot_focus");
		finish(action, "succeeded", "focused");
		return;
	}
	if (command == "SendUIAction")
	{
		fields(arguments, {"context", "action"});
		requireContext(arguments);
		auto found = uiActions.find(textMember(arguments, "action"));
		if (found == uiActions.end()) throw std::runtime_error("unknown_ui_action");
		// Nested menus complete dispatch on their first frame. A callback that
		// does not enter run() completes when its normal handler returns.
		action.completingDispatch = true;
		action.dispatchFrame = frame;
		const bool handled = Element::dispatchUIAction(found->second);
		finish(action, handled ? "succeeded" : "failed", handled ? "ui_action_dispatched" : "ui_action_unhandled");
		return;
	}
	if (command == "OpenMenu")
	{
		fields(arguments, {"context", "menu"});
		requireContext(arguments);
		if (!manager || !manager->menu || manager->inEvent || !manager->global.data.canInput
			|| manager->inThread.load() || Element::currentRunOwnerBlocksParentInput())
			throw std::runtime_error("menu_input_blocked");
		const auto menu = textMember(arguments, "menu");
		if (menu != "System" && menu != "Goods" && menu != "Equip" && menu != "Magic"
			&& menu != "Practice" && menu != "Memo") throw std::runtime_error("unknown_menu");
		cancelWorld("menu_opened");
		action.completingDispatch = true;
		action.dispatchFrame = frame;
		if (menu == "System") manager->menu->openSystemMenu();
		else if (menu == "Goods") manager->menu->toggleGoodsView();
		else if (menu == "Equip") manager->menu->toggleEquipView();
		else if (menu == "Magic") manager->menu->toggleMagicView();
		else if (menu == "Practice") manager->menu->togglePracticeView();
		else manager->menu->toggleMemoView();
		finish(action, "succeeded", "menu_dispatched");
		return;
	}
	if (command == "UseItem")
	{
		fields(arguments, {"generation", "slot"});
		requireWorld(arguments);
		const int slot = integerMember(arguments, "slot", 0, manager->goodsManager.listLength() - 1);
		auto detail = object();
		detail.objectValues = {{"slot", number(slot)}, {"file", string(manager->goodsManager.goodsList[slot].iniFile)},
			{"countBefore", number(manager->goodsManager.goodsList[slot].number)},
			{"lifeBefore", number(manager->player->life)}, {"manaBefore", number(manager->player->mana)}};
		if (!manager->goodsManager.useItem(slot)) throw std::runtime_error("item_unavailable");
		detail.objectValues["countAfter"] = number(manager->goodsManager.goodsList[slot].number);
		detail.objectValues["lifeAfter"] = number(manager->player->life);
		detail.objectValues["manaAfter"] = number(manager->player->mana);
		record("item.used", std::move(detail));
		finish(action, "succeeded", "item_used");
		return;
	}
	if (command == "MoveTo" || command == "JumpTo")
		fields(arguments, {"generation", "x", "y", "running", "timeoutMs"});
	else if (command == "Interact")
		fields(arguments, {"generation", "targetId", "side", "running", "timeoutMs"});
	else if (command == "Attack")
		fields(arguments, {"generation", "targetId", "timeoutMs"});
	else if (command == "CastSkill")
		fields(arguments, {"generation", "targetId", "slot", "timeoutMs"});
	else if (command == "ToggleSit")
		fields(arguments, {"generation"});
	else if (command == "StartCombat")
		fields(arguments, {"generation", "targetId", "radius", "kills", "skills", "lifeItem", "manaItem",
			"lifePercent", "manaPercent", "timeoutMs", "allowMeleeFallback"});
	else throw std::runtime_error("unknown_command");
	requireWorld(arguments);
	if (worldAction) throw std::runtime_error("world_action_busy");
	optionalInteger(arguments, "timeoutMs", 60000, 100, 3600000);
	auto player = manager->player;
	auto actor = player->getActionActor();
	// A story defeat can restore Stand without restoring life; native input still works.
	if (!actor || actor->isDying() || actor->isHiding()) throw std::runtime_error("player_dead");
	if (command == "ToggleSit")
	{
		if (player->isControllingCharacter()) throw std::runtime_error("controlled_character");
		if (!manager->controller->tryToggleLegacySit()) throw std::runtime_error("action_rejected");
		finish(action, "succeeded", player->isSitting() ? "sitting" : "standing");
		return;
	}
	action.lastPosition = actor->getPosition();
	bool queued = false;
	const bool running = has(arguments, "running") && boolMember(arguments, "running");
	if (command == "MoveTo" || command == "JumpTo")
	{
		const Point point = destination(arguments);
		if (command == "JumpTo" ? !manager->map->canJump(point) : !manager->map->canWalkForActor(point, actor))
			throw std::runtime_error("blocked_destination");
		if (command == "JumpTo" && !player->canJump) throw std::runtime_error("jump_disabled");
		NextAction next;
		next.action = command == "JumpTo" ? acJump : running && player->canRun && player->canPayRunThewCost() ? acRun : acWalk;
		next.dest = point;
		queued = player->addNextAction(next);
	}
	else if (command == "Interact")
	{
		auto target = resolve(integer(member(arguments, "targetId")));
		const auto side = has(arguments, "side") ? textMember(arguments, "side") : "primary";
		if (side != "primary" && side != "alternate") throw std::runtime_error("invalid_interaction_side");
		const auto intent = side == "alternate" ? WorldInteractionScriptSide::Alternate : WorldInteractionScriptSide::Primary;
		if (auto npc = std::dynamic_pointer_cast<NPC>(target)) queued = manager->queueNPCTalkInteraction(npc, intent, running);
		else if (auto item = std::dynamic_pointer_cast<Object>(target)) queued = manager->queueObjectScriptInteraction(item, intent, running);
	}
	else
	{
		if (!player->canFight) throw std::runtime_error("fight_disabled");
		std::shared_ptr<NPC> target;
		if (has(arguments, "targetId"))
		{
			target = std::dynamic_pointer_cast<NPC>(resolve(integer(member(arguments, "targetId"))));
			if (!target || !manager->npcManager->findNPC(target)
				|| !WorldInteractionResolver::isNPCValidForIntent(target, WorldInteractionIntent::Attack, actor))
				throw std::runtime_error("invalid_enemy");
		}
		action.target = target;
		if (command == "Attack") queued = target && manager->queueNPCAttackInteraction(target, false);
		else if (command == "CastSkill")
		{
			const auto slot = integerMember(arguments, "slot", 0, 63);
			queued = queueSkill(slot, target);
			if (queued) action.magic = manager->magicManager.magicList[manager->magicManager.bottomIndex(slot)].magic;
		}
		else
		{
			if (has(arguments, "allowMeleeFallback")) boolMember(arguments, "allowMeleeFallback");
			optionalInteger(arguments, "radius", 13, 1, 64);
			optionalInteger(arguments, "kills", 1, 1, 100);
			optionalInteger(arguments, "lifePercent", 40, 1, 99);
			optionalInteger(arguments, "manaPercent", 25, 1, 99);
			for (const auto* key : {"lifeItem", "manaItem"}) if (has(arguments, key)) textMember(arguments, key);
			if (has(arguments, "skills"))
			{
				const auto& skills = member(arguments, "skills");
				if (skills.type != Type::Array || skills.arrayValues.size() > 16) throw std::runtime_error("invalid_skills");
				for (const auto& skill : skills.arrayValues)
					if (integer(skill) < 0 || integer(skill) >= manager->magicManager.bottomCount()) throw std::runtime_error("invalid_skill_slot");
			}
			queued = true;
		}
	}
	if (!queued) throw std::runtime_error("action_rejected");
	if (command == "MoveTo") action.lastMoveAttempt = SDL_GetTicks();
	action.dispatchFrame = frame;
	worldAction = action.id;
}

void GameplayAutomationSession::updateWorldAction()
{
	if (!worldAction) return;
	auto& action = actions.at(worldAction);
	if (action.generation != generation)
	{
		finish(action, "cancelled", "world_changed");
		return;
	}
	auto* manager = GameManager::getInstance();
	if (!manager || !manager->player)
	{
		finish(action, "failed", "world_unavailable");
		return;
	}
	if (manager->inThread.load()) return;
	auto actor = manager->player->getActionActor();
	if (!actor || actor->isDying() || actor->isHiding())
	{
		finish(action, "failed", "player_dead");
		return;
	}
	const auto now = SDL_GetTicks();
	const auto& arguments = action.arguments;
	if (action.command == "Interact" && has(arguments, "targetId"))
	{
		auto found = entities.find(integer(member(arguments, "targetId")));
		auto target = found == entities.end() ? nullptr : found->second.lock();
		auto npc = std::dynamic_pointer_cast<NPC>(target);
		auto item = std::dynamic_pointer_cast<Object>(target);
		if ((!npc || !manager->npcManager || !manager->npcManager->findNPC(npc))
			&& (!item || !manager->objectManager || !manager->objectManager->findObj(item)))
		{
			finish(action, "failed", "target_unavailable");
			return;
		}
	}
	if (now - action.started > static_cast<std::uint64_t>(optionalInteger(arguments, "timeoutMs", 60000, 100, 3600000)))
	{
		finish(action, "failed", "action_timeout");
		return;
	}
	if (!worldInputAllowed()) return;
	const Point point = actor->getPosition();
	if (point.x != action.lastPosition.x || point.y != action.lastPosition.y)
	{
		action.lastPosition = point;
		action.lastProgress = now;
	}
	if (action.command == "MoveTo" || action.command == "JumpTo")
	{
		const Point goal = destination(arguments);
		if (point.x == goal.x && point.y == goal.y && actor->isStanding() && frame > action.dispatchFrame)
		{
			finish(action, "succeeded", "arrived");
			return;
		}
		if ((actor->isStanding() || actor->isHurting())
			&& !manager->player->nextAction && manager->player->nextDest == ndNone
			&& now - action.lastMoveAttempt >= 500 && now - action.lastProgress <= 10000)
		{
			action.lastMoveAttempt = now;
			const bool jumping = action.command == "JumpTo";
			if (jumping ? manager->player->canJump && manager->map->canJump(goal)
				: manager->map->canWalkForActor(goal, actor))
			{
				const bool running = has(arguments, "running") && boolMember(arguments, "running");
				NextAction next;
				next.action = jumping ? acJump : running && manager->player->canRun
					&& manager->player->canPayRunThewCost() ? acRun : acWalk;
				next.dest = goal;
				// Queue after an interrupted start; the native action decides when
				// recovery permits movement and resolves every collision again.
				manager->player->addNextAction(next);
			}
		}
	}
	else if (action.command == "Interact")
	{
		if (frame > action.dispatchFrame && actor->isStanding() && !manager->player->nextAction
			&& manager->player->nextDest == ndNone)
		{
			finish(action, "failed", "interaction_not_observed");
			return;
		}
	}
	else if (action.command == "Attack" || action.command == "CastSkill")
	{
		if (action.command == "Attack" && actor->isAttacking()) action.observedExecution = true;
		if (action.observedExecution && actor->isStanding())
		{
			finish(action, "succeeded", "action_finished");
			return;
		}
		if (!action.observedExecution && frame > action.dispatchFrame && actor->isStanding()
			&& !manager->player->nextAction && manager->player->nextDest == ndNone)
		{
			finish(action, "failed", "action_not_executed");
			return;
		}
	}
	else if (action.command == "StartCombat")
	{
		auto target = action.target.lock();
		if (target && target->isDying())
		{
			// Legacy NPCs may start with zero life. Only the native death state
			// waits for NPCManager's actual death result before running scripts.
			return;
		}
		if (target && (!manager->npcManager->findNPC(target)
			|| !WorldInteractionResolver::isNPCValidForIntent(target, WorldInteractionIntent::Attack, actor)))
		{
			finish(action, "failed", "target_unavailable");
			return;
		}
		if (!target)
		{
			if (has(arguments, "targetId") && action.kills == 0)
			{
				finish(action, "failed", "target_unavailable");
				return;
			}
			auto candidates = manager->findWorldInteractionCandidates(WorldInteractionIntent::Attack,
				optionalInteger(arguments, "radius", 13, 1, 64), 2);
			if (candidates.empty())
			{
				finish(action, "failed", "no_enemy_in_range");
				return;
			}
			target = std::min_element(candidates.begin(), candidates.end(), [&](const auto& left, const auto& right)
				{ return Map::calDistance(point, left.npc->getPosition()) < Map::calDistance(point, right.npc->getPosition()); })->npc;
			action.target = target;
			action.lastTargetLife = target->life;
		}
		if (target->life != action.lastTargetLife)
		{
			action.lastTargetLife = target->life;
			action.lastProgress = now;
		}
		bool waitingForManaMedicine = false;
		for (const bool life : {true, false})
		{
			if (!life && manager->player->hasUnlimitedCheatResources()) continue;
			const auto* itemKey = life ? "lifeItem" : "manaItem";
			const int threshold = optionalInteger(arguments, life ? "lifePercent" : "manaPercent", life ? 40 : 25, 1, 99);
			const int current = life ? actor->life : actor->mana;
			const int maximum = life ? actor->getLifeMax() : actor->getManaMax();
			if (maximum <= 0 || static_cast<std::int64_t>(current) * 100 >= static_cast<std::int64_t>(maximum) * threshold) continue;
			if (!has(arguments, itemKey)) continue;
			const auto file = textMember(arguments, itemKey);
			int slot = -1;
			for (int i = 0; i < manager->goodsManager.listLength(); ++i)
				if (manager->goodsManager.goodsList[i].iniFile == file && manager->goodsManager.goodsList[i].number > 0) { slot = i; break; }
			if (slot < 0)
			{
				finish(action, "failed", std::string("item_depleted: ") + file);
				return;
			}
			if (manager->goodsManager.goodsList[slot].remainColdMilliseconds != 0)
			{
				waitingForManaMedicine = waitingForManaMedicine || !life;
				continue;
			}
			const int countBefore = manager->goodsManager.goodsList[slot].number;
			if (manager->goodsManager.useItem(slot))
			{
				auto detail = object();
				detail.objectValues = {{"file", string(file)}, {"countBefore", number(countBefore)},
					{"countAfter", number(manager->goodsManager.goodsList[slot].number)},
					{"before", number(current)}, {"after", number(life ? actor->life : actor->mana)}};
				record("combat.item", std::move(detail));
				return;
			}
		}
		if (!manager->player->nextAction
			&& (actor->isStanding() || actor->isWalking() || actor->isRunning() || actor->isHurting()))
		{
			// Normal input can queue a skill during hurt; Player consumes it only
			// after recovery, without interrupting the current action.
			bool queued = false;
			const bool allowMelee = !has(arguments, "allowMeleeFallback") || boolMember(arguments, "allowMeleeFallback");
			int approachSlot = -1;
			bool coolingDown = false;
			bool resourcesUnavailable = false;
			if (has(arguments, "skills"))
				for (const auto& slot : member(arguments, "skills").arrayValues)
				{
					std::string reason;
					const int skillSlot = static_cast<int>(integer(slot));
					if (queueSkill(skillSlot, target, true, &reason)) { queued = true; break; }
					if (reason == "skill_out_of_range" && approachSlot < 0) approachSlot = skillSlot;
					coolingDown = coolingDown || reason == "skill_cooldown";
					resourcesUnavailable = resourcesUnavailable || reason == "skill_resources_unavailable";
				}
			if (!queued && allowMelee && actor->isStanding()) manager->queueNPCAttackInteraction(target, false);
			else if (!queued && !allowMelee)
			{
				if (approachSlot >= 0 && actor->isStanding())
				{
					const auto& skill = manager->magicManager.magicList[manager->magicManager.bottomIndex(approachSlot)];
					const auto prepared = manager->player->resolveMagicReplacement(skill.magic);
					const int reach = skillReach(*prepared, skill.level, *actor);
					const auto goal = target->getPosition();
					auto path = manager->map->findPath(point, goal, actor->getMoveDirectionCount());
					if (path.empty()) path = manager->map->getRadiusPath(point, goal, reach, actor->getMoveDirectionCount());
					auto canFireFrom = [&](Point candidate)
					{
						return Map::calDistance(candidate, goal) <= reach && manager->map->canWalk(candidate)
							&& skillPathIsClear(*prepared, skill.level, candidate, target);
					};
					const auto approach = std::find_if(path.begin(), path.end(), canFireFrom);
					std::optional<Point> firingPosition;
					if (approach != path.end()) firingPosition = *approach;
					if (!firingPosition && manager->map->data)
					{
						// A shortest path can run behind the same projectile blocker at
						// every step. Check nearby side positions with the normal pathfinder.
						constexpr int maximumSearchRadius = 16;
						constexpr size_t maximumPathChecks = 32;
						constexpr int maximumPathTries = 4096;
						const int radius = std::min(reach, maximumSearchRadius);
						std::vector<Point> candidates;
						for (int y = std::max(0, goal.y - radius * 2);
							y <= std::min(manager->map->data->head.height - 1, goal.y + radius * 2); ++y)
							for (int x = std::max(0, goal.x - radius);
								x <= std::min(manager->map->data->head.width - 1, goal.x + radius); ++x)
							{
								const Point candidate = {x, y};
								if (candidate != point && Map::calDistance(candidate, goal) <= radius && canFireFrom(candidate))
									candidates.push_back(candidate);
							}
						std::stable_sort(candidates.begin(), candidates.end(), [&](Point left, Point right)
						{
							return Map::calDistance(point, left) < Map::calDistance(point, right);
						});
						for (size_t i = 0; i < std::min(candidates.size(), maximumPathChecks); ++i)
							if (!manager->map->findPath(point, candidates[i], actor->getMoveDirectionCount(), maximumPathTries).empty())
							{
								firingPosition = candidates[i];
								break;
							}
					}
					if (!firingPosition)
					{
						finish(action, "failed", "target_unreachable");
						return;
					}
					NextAction next;
					next.action = acWalk;
					next.dest = *firingPosition;
					manager->player->addNextAction(next);
				}
				else if (approachSlot < 0 && !coolingDown && !(resourcesUnavailable && waitingForManaMedicine))
				{
					finish(action, "failed", resourcesUnavailable ? "skill_resources_unavailable" : "configured_skill_unavailable");
					return;
				}
			}
		}
	}
	if (now - action.lastProgress > 10000)
	{
		finish(action, "failed", "no_progress");
	}
}
#endif
