#include "../Component/Button.h"
#include "../Component/TextLayout.h"
#include "../Element/Element.h"
#include "../Engine/Engine.h"
#include "../File/File.h"
#include "../File/INIReader.h"
#include "../JxqyEngineVersion.h"
#include "../Resource/ResourceManager.h"
#include "../Game/Game.h"
#include "../Game/Data/MemoPersistence.h"
#include "../Game/Data/BuySellInventory.h"
#include "../Game/Data/CollisionDetector.h"
#include "../Game/GameManager/ScriptRuntimeState.h"
#include "../Game/GameManager/SaveGeneration.h"
#include "../Game/GameManager/GameManager.h"
#include "../Game/Data/ColorStyle.h"
#include "../Game/Data/NewYearPeriod.h"
#include "../Game/Menu/SaveLoad.h"
#include "../Game/Menu/BuySellMenu.h"
#include "../Game/Menu/System.h"
#include "../Game/Menu/SystemNotice.h"
#include "../Game/Menu/SkillsPanel.h"
#include "../Game/Scene/MainScene.h"
#include "HeadlessPhysicalInputTestHarness.h"
#include "MapV3ContractFixture.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

class CoreLifecycleTestAccess
{
public:
	static std::size_t scriptMessageCount(const SystemNotice& notice)
	{
		return notice.messages.size();
	}
	static _shared_image noticeTextImage(const SystemNotice& notice)
	{
		return notice.textImage;
	}
	static void update(Element& element)
	{
		element.update();
	}
	static void advanceActorFrame(Element& element, UTime elapsed)
	{
		element.setTime(element.getTime() + elapsed);
		element.frameTime = elapsed;
		element.onUpdate();
	}
	static void handleMenuEvent(Element& element)
	{
		element.onEvent();
	}
	static void beginElementFrame(Element& element)
	{
		element.onPreTreatment();
	}

	static void registerScriptProbe(Script& script, const std::string& name, lua_CFunction callback)
	{
		script.registerLuaFunction(name, callback);
	}

	static int chooseExWithAutomation(lua_State* state)
	{
		return Script::lua_ChooseEx(state);
	}

	static int observeRandomReward(lua_State* state)
	{
		const int result = Script::lua_AddRandGoods(state);
		gm->varList.setInteger("ObservedRandomReward", gm->varList.getInteger("ObservedRandomReward") + 1);
		return result;
	}

	static int observeObjectScriptBinding(lua_State* state)
	{
		const int result = Script::lua_SetObjScript(state);
		gm->varList.setInteger("ObservedObjectBinding", gm->varList.getInteger("ObservedObjectBinding") + 1);
		return result;
	}

	static int observeFengchiGuardBinding(lua_State* state)
	{
		const int result = Script::lua_SetNpcScript(state);
		const auto guards = gm->npcManager->findNPC(u8"守门家丁");
		gm->varList.setInteger("GuardBindingCount", gm->varList.getInteger("GuardBindingCount") + 1);
		if (guards.size() != 1 || guards.front()->scriptFile != u8"大门家丁.txt")
		{
			gm->varList.setInteger("BadGuardBindingCount", gm->varList.getInteger("BadGuardBindingCount") + 1);
		}
		return result;
	}

	static std::pair<std::string, std::string> dialogContent(const Dialog& dialog)
	{
		return { dialog.currentTalkText, dialog.head1FileName.empty()
			? dialog.head2FileName : dialog.head1FileName };
	}

	static std::vector<std::string> selectChoice(ChooseMenu& menu, int selection)
	{
		std::vector<std::string> content{ menu.currentMessage };
		content.insert(content.end(), menu.currentOptions.begin(), menu.currentOptions.end());
		menu.selectChoice(selection);
		return content;
	}

	static std::string choiceSpeaker(const ChooseMenu& menu)
	{
		return menu.currentSpeakerName;
	}
	static void prepareRenderedChoice(ChooseMenu& menu, bool speaker, const std::vector<std::string>& options)
	{
		ChooseMenu::SelectionConfiguration configuration;
		configuration.choosePlus = speaker;
		configuration.speakerName = u8"酒肆老板";
		configuration.dialogPosition = 1;
		configuration.message = speaker ? u8"……" : u8"偷取：武当山下酒肆老板";
		configuration.options = options;
		menu.prepareSelection(configuration);
		menu.focusManager.clear();
	}
	static bool chooseWithSemanticActions(ChooseMenu& menu, int selection)
	{
		bool handled = true;
		for (int index = 0; index < selection; ++index)
		{
			handled = menu.onHandleUIAction(UIAction::NavigateDown) && handled;
		}
		return menu.onHandleUIAction(UIAction::Confirm) && handled;
	}

	static int sendEngineEvent(Uint32 eventType)
	{
		SDL_Event event = {};
		event.type = eventType;
		const int result =
			Engine::engineAppEventHandler(&event);
		Engine::getInstance()->
			queueApplicationLifecycleRequest(
				eventType);
		return result;
	}

	static void setRunningElements(std::vector<PElement> elements)
	{
		Element::runningElement = std::move(elements);
	}

	static void clearRunningElements()
	{
		Element::runningElement.clear();
	}

	static void handleEvents(Element& element)
	{
		element.allHandleEvents();
	}

	static void frame(Element& element)
	{
		element.frame();
	}

	static void draw(Element& element)
	{
		element.drawAll();
	}
	static void drawSubtree(Element& element)
	{
		element.drawSelf();
	}

	static void resize(Element& element, int width, int height)
	{
		element.resizeAll(width, height);
	}

	static void beginSyntheticDrag(const PElement& dragItem)
	{
		Element::dragging = TOUCH_MOUSEID;
		Element::currentDragItem = dragItem;
		Element::dragDownPosition = { 0, 0 };
		Element::dragTouchPosition = { 1, 1 };
	}

	static void endSyntheticDrag()
	{
		Element::dragging = TOUCH_UNTOUCHEDID;
		Element::currentDragItem.reset();
		Element::dragDownPosition = { 0, 0 };
		Element::dragTouchPosition = { 0, 0 };
	}

	static bool pendingLogicalResizeEvent()
	{
		return Engine::getInstance()->
			hasPendingLogicalResizeEvent();
	}

	static bool pendingLogicalScreenTextureResize()
	{
		return Engine::getInstance()->
			pendingLogicalScreenTextureResize;
	}

	static void setPendingLogicalResizeState(
		bool resizeEvent,
		bool screenTextureResize)
	{
		Engine* engine = Engine::getInstance();
		if (resizeEvent)
		{
			(void)engine->markLogicalResizePending();
		}
		else
		{
			engine->acknowledgedLogicalResizeGeneration.store(
				engine->logicalResizeGeneration.load(
					std::memory_order_acquire),
				std::memory_order_release);
		}
		engine->pendingLogicalScreenTextureResize =
			screenTextureResize;
	}

	static void finalizeLogicalResizeEventPump(
		bool resizeEventGenerated,
		std::uint32_t queuedResizeGeneration)
	{
		Engine::getInstance()->
			finalizeLogicalResizeEventPump(
				resizeEventGenerated,
				queuedResizeGeneration);
	}

	static std::uint32_t recordLogicalResizeEvent()
	{
		return Engine::getInstance()->
			recordLogicalResizeEvent();
	}

	static void getLogicalSize(int& width, int& height)
	{
		Engine* engine = Engine::getInstance();
		width = engine->EngineBase::width;
		height = engine->EngineBase::height;
	}

	static void setLogicalSize(int width, int height)
	{
		Engine* engine = Engine::getInstance();
		engine->EngineBase::width = width;
		engine->EngineBase::height = height;
	}

	static SDL_Renderer* exchangeRenderer(
		SDL_Renderer* renderer)
	{
		return Engine::renderer.exchange(renderer);
	}

	static _shared_image exchangeLogicalScreen(_shared_image screen)
	{
		return std::exchange(Engine::getInstance()->realScreen, std::move(screen));
	}

	static _shared_image captureSaveBackground(GameManager& gameManager)
	{
		return gameManager.scriptAPI.captureSaveBackground();
	}

	static void setMousePosition(int x, int y)
	{
		Engine* engine = Engine::getInstance();
		engine->mouseX = x;
		engine->mouseY = y;
	}

	static void drawGameScene(GameManager& gameManager)
	{
		gameManager.onDraw();
	}

	static bool timerPaused(Element& element)
	{
		return element.timer.getPaused();
	}

	static bool applicationMediaPaused()
	{
		return Engine::getInstance()->applicationMediaPaused.load();
	}

	static bool canPrepareRenderFrame()
	{
		return Engine::getInstance()->canPrepareRenderFrame();
	}

	static bool logicRunning(const Element& element)
	{
		return element.logicRunning;
	}

	static void setLogicRunning(
		Element& element,
		bool running)
	{
		element.logicRunning = running;
	}

	static std::size_t runningElementCount()
	{
		return Element::runningElement.size();
	}

	static void setFrameTime(Element& element, UTime frameTime)
	{
		element.frameTime = frameTime;
	}

	static void updateGameManager(GameManager& gameManager)
	{
		gameManager.onUpdate();
	}

	static void setLastLoadFailureMessage(
		GameManager& gameManager,
		std::string message)
	{
		gameManager.setLastLoadFailureMessage(
			std::move(message));
	}

	static bool shouldUpdateGameManagerChild(GameManager& gameManager, const PElement& child)
	{
		return gameManager.shouldUpdateChild(child);
	}

	static void handleSystemEvent(System& system)
	{
		system.onEvent();
	}

	static void handleSystemSaveFailure(System& system)
	{
		system.handleSaveFailure();
	}

	static void updateMainScene(MainScene& mainScene)
	{
		mainScene.onUpdate();
	}

	static void handleSaveLoadEvent(SaveLoad& saveLoad)
	{
		saveLoad.onEvent();
	}

	static bool beginPointerInteraction(
		Element& element, EventTouchID pointerID, int x, int y)
	{
		return element.checkAllTouchDown(pointerID, x, y);
	}

	static GameLoading::LoadingTaskResult runExclusiveLoadingTask(
		GameManager& gameManager,
		GameLoading::ExclusiveLoadingRunner::Worker worker,
		std::function<GameLoading::LoadingTaskResult(
			const std::function<bool()>& ownerCheckpoint)>
			successFinalizer = {},
		const std::function<void()>&
			loadingPresentationPumpObserver = {},
		const _shared_image& presentationBackground = nullptr)
	{
		return gameManager.scriptAPI.runExclusiveLoadingTask(
			{},
			std::move(worker),
			std::move(successFinalizer),
			loadingPresentationPumpObserver,
			presentationBackground);
	}

	static unsigned int loadingPresentationWaitMilliseconds(
		UTime currentTime,
		UTime lastPresentationTime)
	{
		return ScriptAPI::loadingPresentationWaitMilliseconds(
			currentTime,
			lastPresentationTime);
	}

	static void resetExclusiveLoadingInputState(GameManager& gameManager)
	{
		gameManager.resetExclusiveLoadingInputState();
	}

	static bool setHeadlessFramePump(bool enabled)
	{
		return Engine::isBackGround.exchange(enabled);
	}

	static bool hasWindowCloseConfirmationHandler()
	{
		return static_cast<bool>(
			Element::windowCloseConfirmationHandler);
	}

	static bool acceptsWindowCloseWithoutScenePolicy(
		Element& sceneRoot)
	{
		return Element::windowCloseConfirmationHandler &&
			Element::windowCloseConfirmationHandler(
				sceneRoot);
	}

	static GameLoading::LoadingTaskResult
		finishGameLoad(
			GameManager& gameManager,
			const std::string& preparedDirectory,
			const std::function<bool(
				const std::string& generationDirectory,
					const std::function<bool()>&
						ownerCheckpoint)>&
							generationLoadOverride,
			const std::function<bool()>& ownerCheckpoint = {})
	{
		return gameManager.scriptAPI.
			finishGameLoad(
				preparedDirectory,
				ownerCheckpoint,
				generationLoadOverride);
	}

	static bool runOwnerWorldCommit(
		GameManager& gameManager,
		const std::function<bool(
			const std::function<void()>& beforeMutation,
			const std::function<void()>& commitCompleted)>&
				commit,
		bool failCloseOnPartialFailure = true)
	{
		return gameManager.scriptAPI.runOwnerWorldCommit(
			"test world",
			commit,
			failCloseOnPartialFailure);
	}

	static bool loadEmptyNpcWithCheckpoint(
		GameManager& gameManager,
		const std::function<bool()>& checkpoint)
	{
		return gameManager.scriptAPI.loadNPCWithPreparationCheckpoint("", checkpoint);
	}

	static bool loadPreparedMapWithActorReset(
		GameManager& gameManager,
		bool replaceAllForSaveLoad)
	{
		return gameManager.scriptAPI.loadMapWithFailurePolicy(
			"actor-reset.map",
			false,
			true,
			{},
			[](
				const std::function<void()>& beforeMutation,
				const std::function<bool()>& preparationCheckpoint)
			{
				if (preparationCheckpoint &&
					!preparationCheckpoint())
				{
					return false;
				}
				beforeMutation();
				return true;
			},
			false,
			"actor-reset",
			replaceAllForSaveLoad
				? ScriptAPI::MapActorResetMode::
					ReplaceAllForSaveLoad
				: ScriptAPI::MapActorResetMode::
					PreservePartners);
	}
};

namespace
{
constexpr auto ExclusiveLoadingWorkerTimeout =
	std::chrono::seconds(5);

class LoadingWorkerExitSignal final
{
public:
	explicit LoadingWorkerExitSignal(
		std::atomic<bool>& workerExited)
		: exited(&workerExited)
	{
	}

	~LoadingWorkerExitSignal()
	{
		exited->store(true);
	}

private:
	std::atomic<bool>* exited = nullptr;
};

bool check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}

class ScopedActiveResourceRoot final
{
public:
	ScopedActiveResourceRoot()
		: previousRoot(File::getActiveResourceRoot())
	{
		const auto uniqueSuffix =
			std::chrono::steady_clock::now().
				time_since_epoch().count();
		root =
			std::filesystem::temp_directory_path() /
			("jxqy-save-load-rollback-" +
				std::to_string(uniqueSuffix));
		std::error_code error;
		std::filesystem::create_directories(
			root,
			error);
		active = !error;
		if (active)
		{
			File::setActiveResourceRoot(
				root.u8string());
		}
	}

	~ScopedActiveResourceRoot()
	{
		if (!active)
		{
			return;
		}
		File::setActiveResourceRoot(previousRoot);
		std::error_code error;
		std::filesystem::remove_all(root, error);
	}

	ScopedActiveResourceRoot(
		const ScopedActiveResourceRoot&) = delete;
	ScopedActiveResourceRoot& operator=(
		const ScopedActiveResourceRoot&) = delete;

	bool valid() const
	{
		return active;
	}

private:
	std::string previousRoot;
	std::filesystem::path root;
	bool active = false;
};

class ReloadCountingPlayer final : public Player
{
public:
	void reloadAction() override
	{
		++reloadActionCount;
	}

	int reloadActionCount = 0;
};

class ReloadCountingNPC final : public NPC
{
public:
	void reloadAction() override
	{
		++reloadActionCount;
	}

	int reloadActionCount = 0;
};

void installCountingPlayer(
	GameManager& gameManager,
	const std::shared_ptr<ReloadCountingPlayer>& player)
{
	gameManager.controller->removeChild(gameManager.player);
	gameManager.player = player;
	gameManager.npcManager->setPlayer(player);
	gameManager.controller->addChild(player);
}

bool runEmptyNpcLoadCancellationTests()
{
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid(), "empty NPC cancellation test creates an isolated resource root"))
	{
		return false;
	}
	bool ok = true;
	for (bool throwFromCheckpoint : { false, true })
	{
		GameManager gameManager;
		auto ordinaryNpc = std::make_shared<NPC>();
		gameManager.npcManager->addNPC(ordinaryNpc);
		gameManager.global.data.npcName = "keep-on-cancel.npc";
		int checkpoints = 0;
		const bool loaded = CoreLifecycleTestAccess::loadEmptyNpcWithCheckpoint(gameManager, [&]()
		{
			++checkpoints;
			if (throwFromCheckpoint)
			{
				throw std::runtime_error("cancel empty NPC load");
			}
			return false;
		});
		ok = check(!loaded && checkpoints == 1 &&
			gameManager.npcManager->npcList.size() == 1 &&
			gameManager.npcManager->npcList.front() == ordinaryNpc &&
			gameManager.global.data.npcName == "keep-on-cancel.npc" &&
			gameManager.getLastLoadFailureMessage().empty(),
			"cancelled or throwing checkpoint rejects empty LoadNpc before any world mutation") && ok;
	}
	return ok;
}

bool runMapActorResetModeTests()
{
	ScopedActiveResourceRoot resourceRoot;
	if (!check(
			resourceRoot.valid(),
			"map actor reset test created an isolated resource root"))
	{
		return false;
	}

	bool ok = true;
	{
		GameManager gameManager;
		gameManager.map->data = std::make_shared<MapData>();
		auto player = std::make_shared<ReloadCountingPlayer>();
		installCountingPlayer(gameManager, player);

		auto partner = std::make_shared<ReloadCountingNPC>();
		partner->kind = nkPartner;
		gameManager.npcManager->addNPC(partner);
		auto ordinaryNpc = std::make_shared<ReloadCountingNPC>();
		ordinaryNpc->kind = nkNormal;
		gameManager.npcManager->addNPC(ordinaryNpc);

		const bool loaded =
			CoreLifecycleTestAccess::loadPreparedMapWithActorReset(
				gameManager,
				false);
		ok = check(
			loaded &&
				player->reloadActionCount == 1 &&
				partner->reloadActionCount == 1 &&
				ordinaryNpc->reloadActionCount == 0 &&
				gameManager.npcManager->npcList.size() == 1 &&
				gameManager.npcManager->npcList.front() == partner,
			"ordinary map replacement preserves partners and reloads retained actor actions") &&
			ok;
	}
	{
		GameManager gameManager;
		gameManager.map->data = std::make_shared<MapData>();
		auto player = std::make_shared<ReloadCountingPlayer>();
		installCountingPlayer(gameManager, player);

		auto partner = std::make_shared<ReloadCountingNPC>();
		partner->kind = nkPartner;
		gameManager.npcManager->addNPC(partner);
		auto ordinaryNpc = std::make_shared<ReloadCountingNPC>();
		ordinaryNpc->kind = nkNormal;
		gameManager.npcManager->addNPC(ordinaryNpc);

		const bool loaded =
			CoreLifecycleTestAccess::loadPreparedMapWithActorReset(
				gameManager,
				true);
		ok = check(
			loaded &&
				player->reloadActionCount == 0 &&
				partner->reloadActionCount == 0 &&
				ordinaryNpc->reloadActionCount == 0 &&
				gameManager.npcManager->npcList.empty(),
			"full save map replacement discards old actors without reloading their actions") &&
			ok;
	}
	return ok;
}

bool writeVirtualFile(
	const std::string& path,
	const std::string& contents)
{
	return File::writeFileChecked(
		path,
		contents.data(),
		static_cast<int>(contents.size()));
}

std::string readVirtualFile(
	const std::string& path)
{
	std::unique_ptr<char[]> data;
	int length = 0;
	if (!File::readFile(
			path,
			data,
			length) ||
		length < 0 ||
		(data == nullptr && length > 0))
	{
		return {};
	}
	return std::string(
		data == nullptr ? "" : data.get(),
		static_cast<std::size_t>(length));
}

class ScriptMovementNPC final : public NPC
{
public:
	bool failInitialization = false;
	bool completeMovement = false;
	int waits = 0;
	Point requestedDestination;
	void beginWalk(Point destination) override
	{
		requestedDestination = destination;
		NPC::beginWalk(destination);
	}
	void beginRun(Point destination) override
	{
		requestedDestination = destination;
		NPC::beginRun(destination);
	}
	void beginJump(Point destination) override
	{
		requestedDestination = destination;
		NPC::beginJump(destination);
	}
	unsigned int eventRun() override
	{
		++waits;
		if (waits > 25)
		{
			// Bound the old retry bug; the test must fail instead of hanging the suite.
			setPosition(requestedDestination, false);
			return failInitialization ? erInitError : erExit;
		}
		return NPC::eventRun();
	}
protected:
	bool onInitial() override
	{
		return !failInitialization;
	}
	void onRun() override
	{
		if (!completeMovement)
		{
			Engine::getInstance()->requestApplicationQuit();
			return;
		}
		// Advance actual NPC updates without wall-clock animation waits.
		for (int frame = 0; frame < 200 && logicRunning; ++frame)
		{
			gm->setTime(gm->getTime() + 50);
			setTime(getTime() + 50);
			frameTime = 50;
			NPC::onUpdate();
		}
		if (logicRunning)
		{
			result |= erInitError;
			logicRunning = false;
		}
	}
};

template<class Actor>
class ScriptReplacementActor final : public Actor
{
public:
	int waits = 0;
	int frames = 0;
	bool exhaustedFrames = false;
	std::function<void(int)> beforeFrame;
protected:
	void onRun() override
	{
		++waits;
		for (; frames < 400 && this->logicRunning; ++frames)
		{
			gm->setTime(gm->getTime() + 50);
			if (beforeFrame)
			{
				beforeFrame(frames);
			}
			CoreLifecycleTestAccess::advanceActorFrame(*this, 50);
		}
		if (this->logicRunning)
		{
			exhaustedFrames = true;
			this->result |= erInitError;
			this->logicRunning = false;
		}
	}
};

bool runScriptAsyncMovementReplacementTests()
{
	bool ok = true;
	for (bool actualPlayer : { false, true })
	{
		for (const char* replacement : { "playergoto(4,8);", "playerrunto(4,8);", "playerjumpto(4,8);",
			"playerruntoex(4,8);", "playergotoex(4,8);", "playerrunto(-1,8);", "playerjumpto(-1,8);",
			"playerruntoex(-1,8);" })
		{
			Element::resetApplicationQuitState();
			GameManager gameManager;
			gameManager.map->data = std::make_shared<MapData>();
			gameManager.map->data->head.width = 16;
			gameManager.map->data->head.height = 16;
			gameManager.map->data->tile.assign(16, std::vector<MapTile>(16));
			gameManager.map->createDataMap();
			auto npc = std::make_shared<ScriptReplacementActor<NPC>>();
			auto player = std::make_shared<ScriptReplacementActor<Player>>();
			std::shared_ptr<NPC> actor = actualPlayer ? std::static_pointer_cast<NPC>(player) : npc;
			if (actualPlayer)
			{
				gameManager.player = player;
			}
			else
			{
				gameManager.npcManager->npcList.push_back(npc);
			}
			actor->kind = nkPlayer;
			actor->npcName = "ReplacementActor";
			actor->life = 100;
			actor->thew = 100;
			actor->setPosition({4,4}, false);
			gameManager.global.data.NPCAI = false;
			NPCActionRes action;
			action.imagePackage = std::make_shared<IMPImage>();
			action.imagePackage->directions = 8;
			action.imagePackage->interval = 100;
			action.imagePackage->frame.resize(24);
			actor->res.stand = actor->res.walk = actor->res.run = actor->res.jump = action;
			gameManager.map->createDataMap();
			gameManager.global.data.canInput = false;
			const auto execute = [&](const std::string& source)
			{
				auto bytes = std::make_unique<char[]>(source.size());
				std::copy(source.begin(), source.end(), bytes.get());
				return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
			};
			ok = check(execute("npcgotoex('ReplacementActor',9,10); playergotoex(10,10);") == LUA_OK && actor->haveAsyncDest
				&& actor->isWalking() && actor->gotoExDest == Point{10,10},
				"async replacement fixture starts the real pending GotoEx movement") && ok;
			CoreLifecycleTestAccess::advanceActorFrame(*actor, 50);
			ok = check(execute(replacement) == LUA_OK && !npc->exhaustedFrames && !player->exhaustedFrames
				&& !gameManager.global.data.canInput,
				"replacement movement returns without losing the original input lock or exhausting its frame bound") && ok;
			for (int frame = 0; frame < 400; ++frame)
			{
				CoreLifecycleTestAccess::advanceActorFrame(*actor, 50);
			}
			ok = check(actor->getPosition() == Point{4,8} && actor->isStanding() && !actor->haveAsyncDest,
				(std::string("a newer movement must not resume the old GotoEx destination: ")
					+ (actualPlayer ? "Player " : "player-kind NPC ") + replacement
					+ " actual=" + std::to_string(actor->getPosition().x) + "," + std::to_string(actor->getPosition().y)).c_str()) && ok;
		}
	}
	return ok;
}

bool runScriptActionMovementReplacementTests()
{
	bool ok = true;
	for (const auto profile : { ScriptNpcActionProfile::Legacy, ScriptNpcActionProfile::Yycs, ScriptNpcActionProfile::Xjxqy })
	{
		std::vector<int> actions{ -1, 0, 1, 2, 3, 4 };
		if (profile != ScriptNpcActionProfile::Legacy)
		{
			actions.insert(actions.end(), { 12, 13, 14, 15 });
		}
		for (bool actualPlayer : { false, true })
		{
			for (int action : actions)
			{
				Element::resetApplicationQuitState();
				GameManager gameManager;
				gameManager.global.npcActionProfile = profile;
				gameManager.global.data.NPCAI = false;
				gameManager.global.data.canInput = false;
				gameManager.map->data = std::make_shared<MapData>();
				gameManager.map->data->head.width = gameManager.map->data->head.height = 16;
				gameManager.map->data->tile.assign(16, std::vector<MapTile>(16));
				gameManager.map->createDataMap();
				auto npc = std::make_shared<ScriptReplacementActor<NPC>>();
				auto player = std::make_shared<ScriptReplacementActor<Player>>();
				std::shared_ptr<NPC> actor = actualPlayer ? std::static_pointer_cast<NPC>(player) : npc;
				if (actualPlayer)
				{
					gameManager.player = player;
				}
				else
				{
					gameManager.npcManager->npcList.push_back(npc);
				}
				actor->npcName = "ActionReplacementActor";
				actor->life = actor->thew = 100;
				actor->setPosition({4,4}, false);
				NPCActionRes animation;
				animation.imagePackage = std::make_shared<IMPImage>();
				animation.imagePackage->directions = 8;
				animation.imagePackage->interval = 100;
				animation.imagePackage->frame.resize(24);
				actor->res.stand = actor->res.walk = actor->res.run = actor->res.jump = animation;
				gameManager.map->createDataMap();
				const auto execute = [&](const std::string& source)
				{
					auto bytes = std::make_unique<char[]>(source.size());
					std::copy(source.begin(), source.end(), bytes.get());
					return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
				};
				ok = check(execute("npcgotoex('ActionReplacementActor',10,10);") == LUA_OK
					&& actor->haveAsyncDest && actor->isWalking(), "SetNpcAction replacement starts a real pending GotoEx") && ok;
				CoreLifecycleTestAccess::advanceActorFrame(*actor, 50);
				const auto oldPath = actor->stepList;
				ok = check(execute("setnpcaction('ActionReplacementActor',999); setnpcaction('MissingActor',0);") == LUA_OK
					&& actor->haveAsyncDest && actor->stepList == oldPath,
					"unsupported actions and absent targets must not cancel the active movement") && ok;
				const bool requestsStand = action < 2 || action == 12
					|| (profile == ScriptNpcActionProfile::Xjxqy && action == 13);
				const Point expected = requestsStand ? actor->getPosition() : Point{4,8};
				const std::string command = "setnpcaction('ActionReplacementActor'," + std::to_string(action) + ",4,8);";
				ok = check(execute(command) == LUA_OK && npc->waits == 0 && player->waits == 0
					&& !gameManager.global.data.canInput, "SetNpcAction stays non-blocking and preserves the input lock") && ok;
				bool returnedToOldDestination = false;
				for (int frame = 0; frame < 400; ++frame)
				{
					CoreLifecycleTestAccess::advanceActorFrame(*actor, 50);
					returnedToOldDestination = returnedToOldDestination || actor->getPosition() == Point{10,10};
				}
				ok = check(actor->getPosition() == expected && actor->isStanding() && !actor->haveAsyncDest
					&& !returnedToOldDestination,
					("SetNpcAction must not resume an earlier GotoEx: profile=" + std::to_string(static_cast<int>(profile))
						+ (actualPlayer ? " Player " : " NPC ") + command
						+ " actual=" + std::to_string(actor->getPosition().x) + "," + std::to_string(actor->getPosition().y)).c_str()) && ok;
				std::cout << "Script action replacement checked: " << static_cast<int>(profile)
					<< (actualPlayer ? " Player " : " NPC ") << action << std::endl;
			}
		}
	}
	return ok;
}

bool runScriptMovementContractTests()
{
	bool ok = true;
	for (bool canInput : { false, true })
	{
		for (const std::string outcome : { "quit", "initialization", "arrival", "blocked", "missing-images" })
		{
			const bool failInitialization = outcome == "initialization";
			const bool cannotMove = outcome == "blocked" || outcome == "missing-images";
			for (const char* source : { "playergoto(6,6);", "playerrunto(6,6);", "playerjumpto(6,6);",
				"playergotodir(4,2);", "npcgoto('MotionActor',6,6);", "npcgotodir('',4,2);",
				"playergotoex(6,6);", "playerruntoex(6,6);", "npcgotoex(6,6);" })
			{
				Element::resetApplicationQuitState();
				GameManager gameManager;
				gameManager.map->data = std::make_shared<MapData>();
				gameManager.map->data->head.width = 12;
				gameManager.map->data->head.height = 12;
				gameManager.map->data->tile.assign(12, std::vector<MapTile>(12));
				gameManager.map->createDataMap();
				if (outcome == "blocked")
				{
					for (auto& row : gameManager.map->data->tile)
					{
						for (auto& tile : row)
						{
							tile.obstacle = 0x80;
						}
					}
					gameManager.map->data->tile[4][4].obstacle = 0;
				}
				auto actor = std::make_shared<ScriptMovementNPC>();
				actor->npcName = "MotionActor";
				actor->kind = nkPlayer;
				actor->life = 100;
				actor->setPosition({ 4, 4 }, false);
				actor->failInitialization = failInitialization;
				actor->completeMovement = outcome != "quit";
				NPCActionRes action;
				action.imagePackage = std::make_shared<IMPImage>();
				action.imagePackage->directions = 8;
				action.imagePackage->interval = 100;
				action.imagePackage->frame.resize(8);
				actor->res.stand = actor->res.walk = actor->res.run = actor->res.jump = action;
				if (outcome == "missing-images")
				{
					actor->res.walk = actor->res.run = actor->res.jump = NPCActionRes{};
				}
				gameManager.npcManager->npcList.push_back(actor);
				gameManager.map->createDataMap();
				gameManager.scriptNPC = actor;
				ok = check(gameManager.map->canWalk({ 6, 6 }) == (outcome != "blocked"),
					"movement fixture uses the actual map walk-obstacle bits") && ok;
				gameManager.global.data.canInput = canInput;
				const std::string script = std::string(source) + "addmoney(1);";
				gameManager.player->setMoney(0);
				auto bytes = std::make_unique<char[]>(script.size());
				std::copy(script.begin(), script.end(), bytes.get());
				const int scriptResult = gameManager.script.runScript(bytes, static_cast<int>(script.size()));
				const bool asynchronous = script.find("ex(") != std::string::npos;
				const bool quits = !asynchronous && outcome == "quit";
				const bool jumps = script.find("jumpto(") != std::string::npos;
				const int expectedWaits = asynchronous ? 0 : (outcome == "blocked" && !jumps ? 20
					: (!cannotMove || jumps ? 1 : 0));
				const bool arrives = !asynchronous && outcome == "arrival";
				ok = check(scriptResult == (quits ? LUA_ERRRUN : LUA_OK)
					&& actor->waits == expectedWaits
					&& actor->getPosition() == (arrives ? actor->requestedDestination : Point{ 4, 4 })
					&& gameManager.player->money == (quits ? 0 : 1)
					&& gameManager.global.data.canInput == canInput,
					(std::string("movement contract ") + outcome + ": " + source).c_str()) && ok;
				if (asynchronous)
				{
					ok = check(actor->stepList.empty() == cannotMove,
						"asynchronous movement prepares a path only when movement is available") && ok;
				}
				else
				{
					const unsigned int expectedResult = quits ? erExit : (failInitialization ? erInitError : erNone);
					ok = check((actor->result & (erExit | erInitError)) == expectedResult
						&& (!(arrives || cannotMove) || actor->isStanding()),
						"blocking movement terminates on arrival, rejected movement or the nested terminal result") && ok;
				}
				Element::resetApplicationQuitState();
			}
		}
	}
	return ok;
}

bool runScriptBlockedMovementRetryTests()
{
	bool ok = true;
	for (bool actualPlayer : { false, true })
	{
		for (bool running : { false, true })
		{
			for (const std::string scenario : { "temporary", "second-block", "permanent", "actor-clock-frozen", "hidden", "quit" })
			{
				Element::resetApplicationQuitState();
				GameManager game;
				game.global.data.NPCAI = false;
				game.global.data.canInput = false;
				game.map->data = std::make_shared<MapData>();
				game.map->data->head.width = 16;
				game.map->data->head.height = 48;
				game.map->data->tile.assign(48, std::vector<MapTile>(16));
				for (auto& row : game.map->data->tile)
				{
					for (auto& tile : row) tile.obstacle = toObstacle;
					row[4].obstacle = 0;
				}
				auto npc = std::make_shared<ScriptReplacementActor<NPC>>();
				auto player = std::make_shared<ScriptReplacementActor<Player>>();
				std::shared_ptr<NPC> actor = actualPlayer ? std::static_pointer_cast<NPC>(player) : npc;
				if (actualPlayer) game.player = player;
				else game.npcManager->npcList.push_back(npc);
				actor->npcName = "RetryActor";
				actor->kind = actualPlayer ? nkPlayer : nkNormal;
				actor->pathFinder = pfBest;
				actor->life = actor->thew = 10000;
				actor->setPosition({ 4, 4 }, false);
				actor->visibleVariableName = "RetryActorVisible";
				actor->visibleVariableValue = 1;
				game.varList.ensureInitialized();
				game.varList.setInteger("RetryActorVisible", 1);
				NPCActionRes animation;
				animation.imagePackage = std::make_shared<IMPImage>();
				animation.imagePackage->directions = 8;
				animation.imagePackage->interval = 100;
				animation.imagePackage->frame.resize(24);
				actor->res.stand = actor->res.walk = actor->res.run = animation;
				auto blocker = std::make_shared<NPC>();
				blocker->kind = nkNormal;
				blocker->life = 100;
				blocker->setPosition(scenario == "hidden" ? Point{ 12, 12 } : Point{ 4, 6 }, false);
				game.npcManager->npcList.push_back(blocker);
				auto secondBlocker = std::make_shared<NPC>();
				secondBlocker->kind = nkNormal;
				secondBlocker->life = 100;
				secondBlocker->setPosition({ 12, 10 }, false);
				if (scenario == "second-block") game.npcManager->npcList.push_back(secondBlocker);
				game.map->createDataMap();
				std::optional<UTime> secondBlockedSince;
				const auto advance = [&](int frame)
				{
					if ((scenario == "temporary" || scenario == "second-block") && frame == 5) blocker->setPosition({ 12, 12 });
					if (scenario == "second-block")
					{
						if (frame == 15) secondBlocker->setPosition({ 4, 20 });
						if (actor->getPosition().y >= 16 && actor->getPosition().y < 40
							&& actor->isStanding() && !secondBlockedSince)
							secondBlockedSince = game.getTime();
						if (secondBlockedSince && game.getTime() - *secondBlockedSince >= 1500)
							secondBlocker->setPosition({ 12, 10 });
					}
					if (scenario == "actor-clock-frozen") actor->setTime(0);
					if (scenario == "hidden" && frame == 3)
					{
						game.varList.setInteger("RetryActorVisible", 0);
						actor->updateVisibleByVariable();
					}
					if (scenario == "quit" && frame == 3) Engine::getInstance()->requestApplicationQuit();
				};
				npc->beforeFrame = player->beforeFrame = advance;
				if (running && !actualPlayer)
				{
					actor->runTo({ 4, 40 });
				}
				else
				{
					const std::string source = actualPlayer ? (running ? "playerrunto(4,40);" : "playergoto(4,40);")
						: "npcgoto('RetryActor',4,40);";
					auto bytes = std::make_unique<char[]>(source.size());
					std::copy(source.begin(), source.end(), bytes.get());
					const int result = game.script.runScript(bytes, static_cast<int>(source.size()));
					ok = check(result == (scenario == "quit" ? LUA_ERRRUN : LUA_OK), "retry script preserves quit propagation") && ok;
				}
				const UTime elapsed = game.getTime();
				const bool arrives = scenario == "temporary" || scenario == "second-block";
				ok = check(!npc->exhaustedFrames && !player->exhaustedFrames
					&& (scenario != "second-block" || secondBlockedSince.has_value())
					&& (scenario == "hidden" ? !actor->isVisibleByVariable && actor->getPosition() != Point{ 4, 40 }
						: actor->getPosition() == (arrives ? Point{ 4, 40 } : Point{ 4, 4 }))
					&& !game.global.data.canInput && (scenario == "hidden" || actor->isStanding())
					&& (arrives ? elapsed > 2000 : (scenario == "quit" || scenario == "hidden") ? elapsed < 2000 : elapsed == 2000),
					("blocked movement retry " + scenario + (actualPlayer ? " Player" : " NPC")
						+ (running ? " run" : " walk")).c_str()) && ok;
				std::cout << "Script retry checked: " << scenario << (actualPlayer ? " Player" : " NPC")
					<< (running ? " run" : " walk") << " elapsed=" << elapsed << std::endl;
			}
		}
	}
	Element::resetApplicationQuitState();
	return ok;
}

bool runScriptOccupiedDestinationTests()
{
	bool ok = true;
	for (int movement = 0; movement < 3; ++movement)
	{
		for (const std::string scenario : { "arrival", "near-occupied", "far-occupied", "corner-detour", "async-occupied" })
		{
			Element::resetApplicationQuitState();
			GameManager game;
			game.global.data.NPCAI = false;
			game.global.data.canInput = false;
			game.map->data = std::make_shared<MapData>();
			game.map->data->head.width = game.map->data->head.height = 16;
			game.map->data->tile.assign(16, std::vector<MapTile>(16));
			game.map->createDataMap();
			auto npc = std::make_shared<ScriptReplacementActor<NPC>>();
			auto player = std::make_shared<ScriptReplacementActor<Player>>();
			const bool actualPlayer = movement != 0;
			std::shared_ptr<NPC> actor = actualPlayer ? std::static_pointer_cast<NPC>(player) : npc;
			if (actualPlayer) game.player = player;
			else game.npcManager->npcList.push_back(npc);
			actor->npcName = "BlockedActor";
			actor->kind = actualPlayer ? nkPlayer : nkBattle;
			actor->pathFinder = pfSingle;
			actor->life = actor->thew = 100;
			const Point start = scenario == "near-occupied" ? Point{ 4, 10 } : Point{ 4, 4 };
			const Point destination = scenario == "corner-detour" ? Point{ 5, 4 } : Point{ 4, 12 };
			actor->setPosition(start, false);
			NPCActionRes animation;
			animation.imagePackage = std::make_shared<IMPImage>();
			animation.imagePackage->directions = 8;
			animation.imagePackage->interval = 100;
			animation.imagePackage->frame.resize(24);
			actor->res.stand = actor->res.walk = actor->res.run = animation;
			const bool occupied = scenario.find("occupied") != std::string::npos;
			auto blocker = std::make_shared<NPC>();
			if (occupied)
			{
				blocker->kind = nkNormal;
				blocker->life = 100;
				blocker->setPosition(destination, false);
				game.npcManager->npcList.push_back(blocker);
			}
			if (scenario == "corner-detour") game.map->data->tile[3][4].obstacle = toObstacle;
			game.map->createDataMap();
			const bool asynchronous = scenario == "async-occupied";
			const std::string command = movement == 0 ? (asynchronous ? "npcgotoex('BlockedActor'," : "npcgoto('BlockedActor',")
				: movement == 1 ? (asynchronous ? "playergotoex(" : "playergoto(")
				: (asynchronous ? "playerruntoex(" : "playerrunto(");
			const std::string source = command + std::to_string(destination.x) + "," + std::to_string(destination.y) + ");addmoney(1);";
			auto bytes = std::make_unique<char[]>(source.size());
			std::copy(source.begin(), source.end(), bytes.get());
			game.player->setMoney(0);
			const int result = game.script.runScript(bytes, static_cast<int>(source.size()));
			const std::string context = "script occupied destination " + scenario + ": " + command;
			ok = check(result == LUA_OK && !npc->exhaustedFrames && !player->exhaustedFrames
				&& !game.global.data.canInput && game.player->money == 1, context.c_str()) && ok;
			if (asynchronous)
			{
				ok = check(npc->waits == 0 && player->waits == 0 && actor->getPosition() == start
					&& (actor->isWalking() || actor->isRunning()), "Ex movement still returns before advancing frames") && ok;
				for (int frame = 0; frame < 200; ++frame) CoreLifecycleTestAccess::advanceActorFrame(*actor, 50);
				ok = check(actor->getPosition() != destination && (movement == 2 || actor->haveAsyncDest),
					"occupied Ex destination retains its existing pending GotoEx behavior") && ok;
				if (movement != 2)
				{
					blocker->setPosition({ 12, 12 });
					for (int frame = 0; frame < 200; ++frame) CoreLifecycleTestAccess::advanceActorFrame(*actor, 50);
					ok = check(actor->getPosition() == destination && actor->isStanding() && !actor->haveAsyncDest,
						"GotoEx still resumes and arrives after the occupied destination clears") && ok;
				}
			}
			else
			{
				bool adjacent = false;
				for (int direction = 0; direction < 8; ++direction)
					adjacent = adjacent || Map::getSubPoint(actor->getPosition(), direction) == destination;
				ok = check((occupied ? adjacent && actor->getPosition() != destination : actor->getPosition() == destination)
					&& (scenario != "far-occupied" || actor->getPosition() != start)
					&& actor->isStanding() && actor->getOffset().is_zero() && actor->stepList.empty(),
					(context + " must finish standing at arrival or beside the occupied destination").c_str()) && ok;
				bool releasedSteps = true;
				for (const auto& row : game.map->dataMap.tile)
					for (const auto& tile : row) releasedSteps = releasedSteps && tile.stepNPCList.empty();
				ok = check(releasedSteps, "synchronous movement releases all reserved step tiles") && ok;
			}
			std::cout << context << " position=" << actor->getPosition().x << "," << actor->getPosition().y
				<< " frames=" << npc->frames + player->frames << " passed=" << ok << std::endl;
		}
	}
	return ok;
}

bool runScriptInterfaceVisibilityTests()
{
	bool ok = true;
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	for (const char* pack : { "jxqy2", "yycs", "xjxqy" })
	{
		ScopedActiveResourceRoot resourceRoot;
		File::setResourceFallbackRoots({ (assetsRoot / pack).u8string(), (assetsRoot / "common").u8string() });
		ResourceManifest manifest;
		if (!check(resourceRoot.valid() && manifest.loadFromFile("game_profile.ini"),
			"script interface restoration uses each actual resource profile")) return false;
		GameManager game;
		game.global.applyResourceManifestFeatures(manifest);
		game.global.data.canInput = false;
		game.menu->init();
		game.menu->showBottomWnd();
		const auto hudVisible = [&]()
		{
			return game.menu->bottomMenu->visible
				&& (!game.menu->topMenu || game.menu->topMenu->visible)
				&& (!game.menu->columnMenu || game.menu->columnMenu->visible);
		};
		const auto run = [&](const std::string& source)
		{
			auto bytes = std::make_unique<char[]>(source.size());
			std::copy(source.begin(), source.end(), bytes.get());
			return game.script.runScript(bytes, static_cast<int>(source.size()));
		};
		CoreLifecycleTestAccess::registerScriptProbe(game.script, "hudvisible", [](lua_State* state)
		{
			lua_pushboolean(state, gm->menu->bottomMenu->visible
				&& (!gm->menu->topMenu || gm->menu->topMenu->visible)
				&& (!gm->menu->columnMenu || gm->menu->columnMenu->visible));
			return 1;
		});
		ok = check(writeVirtualFile("script/common/hud-child.lua",
			"hideinterface(); assert(not hudvisible());"),
			"HUD nesting regression writes a real child script") && ok;
		game.menu->goodsMenu->visible = true;
		ok = check(run("hideinterface(); assert(not hudvisible()); runscript('hud-child.lua');"
			"assert(not hudvisible());") == LUA_OK && !game.menu->goodsMenu->visible
			&& hudVisible() && !game.global.data.canInput,
			"HideInterface remains hidden across a child return and restores only after the outer script without unlocking input") && ok;
		ok = check(run("hidebottomwnd(); assert(not hudvisible()); runscript('hud-child.lua');"
			"assert(not hudvisible());") == LUA_OK && hudVisible(),
			"HideBottomWnd also restores only after the outer script in every profile") && ok;
		ok = check(run("showbottomwnd()") == LUA_OK && hudVisible(),
			"explicit ShowBottomWnd restores the HUD in every profile") && ok;
		ok = check(run("hideinterface(); hidebottomwnd(); assert(not hudvisible());") == LUA_OK && hudVisible(),
			"mixed hide instructions share the same outer-script restoration") && ok;
		ok = check(run("showinterface(); hideinterface(); showinterface(); assert(hudvisible());") == LUA_OK
			&& game.menu->bottomMenu->visible,
			"paired ShowInterface still restores the HUD inside the script") && ok;
		ok = check(run("hideinterface(); error('HUD restoration failure probe');") == LUA_ERRRUN
			&& game.menu->bottomMenu->visible && !game.script.running,
			"runtime errors also restore temporary hiding and unwind the script running state") && ok;
		ResolvedTraceScriptSource tracedSource;
		tracedSource.identity.virtualPath = "script/common/hud-trace.lua";
		const std::string tracedText = "hideinterface(); runscript('hud-child.lua'); assert(not hudvisible());";
		tracedSource.bytes.assign(tracedText.begin(), tracedText.end());
		ok = check(game.script.runResolvedTraceScriptSource(std::move(tracedSource)).succeeded()
			&& game.menu->bottomMenu->visible && !game.script.running,
			"the traced gameplay and editor entry paths use the same outer-script restoration") && ok;
		ok = check(run("hidebottomwnd(); returntotitle();") == LUA_OK && !game.menu->bottomMenu->visible
			&& game.result == erOK,
			"returning to title preserves the ending's hidden HUD") && ok;
	}
	return ok;
}

bool runScriptArenaGateDetourTests()
{
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid(), "arena movement isolates writable files")) return false;
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	File::setResourceFallbackRoots({ (assetsRoot / "jxqy2").u8string() });
	GameManager game;
	game.global.data.NPCAI = false;
	game.global.data.canInput = false;
	std::unique_ptr<char[]> mapBytes, npcBytes;
	int mapLength = 0;
	if (!check(File::readFile(u8"map/凤池山庄-比武场.map", mapBytes, mapLength)
		&& game.map->load(mapBytes, mapLength)
		&& game.npcManager->load("fengchibw.npc")
		&& game.objectManager->load("fengchibw.obj")
		&& File::readFile("ini/save/fengchibw.npc", npcBytes) > 0,
		"arena detour loads the real map, spectators and opened gate")) return false;
	INIReader table(npcBytes);
	auto actor = std::make_shared<ScriptReplacementActor<NPC>>();
	actor->initFromIni(&table, "npc007");
	game.npcManager->deleteNPC(u8"秋依水");
	game.npcManager->addNPC(actor);
	game.npcManager->findNPC(u8"赵无双").front()->setPosition({ 17, 78 }, false);
	game.player->setPosition({ 18, 65 }, false);
	NPCActionRes animation;
	animation.imagePackage = std::make_shared<IMPImage>();
	animation.imagePackage->directions = 8;
	animation.imagePackage->interval = 100;
	animation.imagePackage->frame.resize(24);
	actor->res.stand = actor->res.walk = actor->res.awalk = animation;
	game.map->createDataMap();
	if (!check(actor->pathFinder == pfSingle && actor->getPosition() == Point{ 18, 79 }
		&& !game.map->canWalk({ 17, 75 }) && game.map->canWalk({ 16, 73 }),
		"the actual single-step actor faces the opened gate obstacle and a free destination")) return false;
	actor->goTo({ 16, 73 });
	bool ok = check(!actor->exhaustedFrames && actor->getPosition() == Point{ 16, 73 }
		&& actor->isStanding() && actor->getOffset().is_zero() && !game.global.data.canInput,
		"blocking scripted movement reaches the arena staging tile without a two-tile loop or input unlock");
	game.npcManager->npcList = { actor };
	game.objectManager->clearObj();
	game.map->data = std::make_shared<MapData>();
	game.map->data->head.width = 12;
	game.map->data->head.height = 260;
	game.map->data->tile.assign(260, std::vector<MapTile>(12));
	actor->walkSpeed = 8;
	for (int pathFinder : { pfSingle, pfBest })
	{
		actor->setPosition({ 4, 2 }, false);
		actor->pathFinder = pathFinder;
		actor->frames = 0;
		actor->exhaustedFrames = false;
		game.map->createDataMap();
		if (pathFinder == pfBest)
			ok = check(actor->findPathByType({ 4, 250 }).empty(),
				"ordinary NPC AI retains its limited search budget") && ok;
		actor->goTo({ 4, 250 });
		ok = check(!actor->exhaustedFrames && actor->getPosition() == Point{ 4, 250 }
			&& actor->isStanding() && actor->getOffset().is_zero(),
			"blocking script movement reaches destinations beyond the ordinary NPC search budget") && ok;
		actor->setPosition({ 4, 2 }, false);
		game.map->createDataMap();
		actor->goToEx({ 4, 250 });
		for (int frame = 0; frame < 400 && actor->haveAsyncDest; ++frame)
		{
			game.setTime(game.getTime() + 50);
			CoreLifecycleTestAccess::advanceActorFrame(*actor, 50);
		}
		ok = check(actor->getPosition() == Point{ 4, 250 } && actor->isStanding()
			&& actor->getOffset().is_zero() && !actor->haveAsyncDest && actor->pathFinder == pathFinder,
			"GotoEx temporarily uses the script search budget and completes without changing PathFinder") && ok;
		actor->setPosition({ 4, 2 }, false);
		game.map->createDataMap();
		if (pathFinder == pfBest)
			ok = check(actor->findPathByType({ 4, 250 }).empty(),
				"completed GotoEx restores the ordinary NPC AI search budget") && ok;
	}
	return ok;
}

bool runScriptPrisonDetourTests()
{
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid(), "prison movement isolates writable files")) return false;
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	File::setResourceFallbackRoots({ (assetsRoot / u8"剑二改承合版").u8string(), (assetsRoot / "jxqy2").u8string() });
	bool ok = true;
	const std::vector<std::pair<std::string, Point>> destinations = {
		{ u8"杀手", { 15, 133 } }, { u8"杀手1", { 17, 129 } },
		{ u8"杀手3", { 16, 135 } }, { u8"杀手2", { 18, 131 } }
	};
	for (bool asynchronous : { false, true })
	{
		for (const auto& [name, destination] : destinations)
		{
			GameManager game;
			game.global.data.NPCAI = false;
			game.global.data.canInput = false;
			std::unique_ptr<char[]> mapBytes, npcBytes;
			int mapLength = 0;
			if (!check(File::readFile(u8"map/临安大牢第1层.map", mapBytes, mapLength)
				&& game.map->load(mapBytes, mapLength)
				&& game.npcManager->load("linandalao12.npc")
				&& game.objectManager->load("linandalao12.obj")
				&& File::readFile("ini/save/linandalao12.npc", npcBytes) > 0,
				"prison detour loads the real map, companions and objects")) return false;
			INIReader table(npcBytes);
			auto actor = std::make_shared<ScriptReplacementActor<NPC>>();
			const auto section = name == u8"杀手" ? "npc043" : name == u8"杀手1" ? "npc011"
				: name == u8"杀手3" ? "npc002" : "npc044";
			actor->initFromIni(&table, section);
			game.npcManager->deleteNPC(name);
			game.npcManager->addNPC(actor);
			game.player->setPosition({ 19, 138 }, false);
			NPCActionRes animation;
			animation.imagePackage = std::make_shared<IMPImage>();
			animation.imagePackage->directions = 8;
			animation.imagePackage->interval = 100;
			animation.imagePackage->frame.resize(24);
			actor->res.stand = actor->res.walk = actor->res.awalk = animation;
			game.map->createDataMap();
			ok = check(actor->kind == nkBattle && actor->pathFinder == pfSingle
				&& !actor->usePathFinder() && game.map->canWalk(destination),
				"prison attacker retains single-step AI and has a free scripted destination") && ok;
			const std::string source = std::string(asynchronous ? "npcgotoex('" : "npcgoto('") + name
				+ "'," + std::to_string(destination.x) + "," + std::to_string(destination.y) + ");";
			auto bytes = std::make_unique<char[]>(source.size());
			std::copy(source.begin(), source.end(), bytes.get());
			ok = check(game.script.runScript(bytes, static_cast<int>(source.size())) == LUA_OK,
				"prison movement uses the real script API") && ok;
			for (int frame = 0; asynchronous && frame < 400 && actor->haveAsyncDest; ++frame)
			{
				game.setTime(game.getTime() + 50);
				CoreLifecycleTestAccess::advanceActorFrame(*actor, 50);
			}
			const auto arrived = actor->getPosition();
			std::cout << "Prison scripted movement: async=" << asynchronous << " name=" << name
				<< " arrived=" << arrived.x << "," << arrived.y << std::endl;
			ok = check(!actor->exhaustedFrames && arrived == destination && actor->isStanding()
				&& actor->getOffset().is_zero() && !actor->haveAsyncDest
				&& actor->pathFinder == pfSingle && !actor->usePathFinder() && !game.global.data.canInput,
				"scripted prison attacker reaches its staging tile without changing combat AI or unlocking input") && ok;
		}
	}
	return ok;
}

bool runMovementRetargetReservationTests()
{
	bool ok = true;
	for (const std::string scenario : { "walk-walk", "walk-run", "run-walk", "run-run",
		"npc-lua", "npc-lua-after-frame", "player-lua", "player-change-walk" })
	{
		for (bool steppedIn : { false, true })
		{
			const bool luaMovement = scenario.find("lua") != std::string::npos;
			if (luaMovement && steppedIn) continue;
			const bool actualPlayer = scenario.find("player-") == 0;
			const bool preserveCurrentStep = scenario == "player-change-walk";
			GameManager game;
			game.global.data.NPCAI = false;
			game.map->data = std::make_shared<MapData>();
			game.map->data->head.width = game.map->data->head.height = 16;
			game.map->data->tile.assign(16, std::vector<MapTile>(16));
			std::shared_ptr<NPC> actor;
			if (actualPlayer)
			{
				game.player = std::make_shared<Player>();
				actor = game.player;
			}
			else actor = std::make_shared<NPC>();
			actor->npcName = "RetargetActor";
			actor->kind = actualPlayer ? nkPlayer : nkBattle;
			actor->pathFinder = pfSingle;
			actor->life = actor->thew = 100;
			actor->setPosition({ 4, 10 }, false);
			actor->setTime(1000);
			NPCActionRes animation;
			animation.imagePackage = std::make_shared<IMPImage>();
			animation.imagePackage->directions = 8;
			animation.imagePackage->interval = 100;
			animation.imagePackage->frame.resize(24);
			actor->res.stand = actor->res.walk = actor->res.run = actor->res.awalk = actor->res.arun = animation;
			if (!actualPlayer) game.npcManager->addNPC(actor);
			game.map->createDataMap();
			const auto execute = [&](const std::string& source)
			{
				auto bytes = std::make_unique<char[]>(source.size());
				std::copy(source.begin(), source.end(), bytes.get());
				return game.script.runScript(bytes, static_cast<int>(source.size()));
			};
			const auto reservationCount = [&](Point position)
			{
				const auto& reservations = game.map->dataMap.tile[position.y][position.x].stepNPCList;
				return std::count(reservations.begin(), reservations.end(), actor);
			};
			const Point originalDestination{ 4, preserveCurrentStep ? 6 : 8 };
			const Point replacementDestination{ 8, 10 };
			Point oldReservation{ 4, 8 };
			bool casePassed = true;
			if (scenario == "player-lua" || scenario == "npc-lua")
			{
				casePassed = check(execute(actualPlayer ? "playergotoex(4,8);playergotoex(8,10);"
					: "npcgotoex('RetargetActor',4,8);npcgotoex('RetargetActor',8,10);") == LUA_OK,
					"consecutive real Lua movement calls replace the destination") && casePassed;
			}
			else
			{
				if (scenario == "npc-lua-after-frame")
					casePassed = check(execute("npcgotoex('RetargetActor',4,8);") == LUA_OK, "first NpcGotoEx starts") && casePassed;
				else if (scenario.find("run-") == 0) actor->beginRun(originalDestination);
				else actor->beginWalk(originalDestination);
				if (steppedIn) CoreLifecycleTestAccess::advanceActorFrame(*actor, actor->stepLastTime);
				else if (scenario == "npc-lua-after-frame") CoreLifecycleTestAccess::advanceActorFrame(*actor, 50);
				const auto oldPositions = actor->getStepPositions();
				if (!check(oldPositions.size() == 1 && !actor->stepList.empty()
					&& actor->getStepState() == (steppedIn ? ssIn : ssOut), "retarget fixture reaches the real movement half-step")) return false;
				oldReservation = oldPositions.front();
				const Point oldFirstStep = actor->stepList.front();
				casePassed = check(reservationCount(oldReservation) == 1, "the old movement has a real tile reservation") && casePassed;
				if (preserveCurrentStep)
				{
					casePassed = check(game.player->changeWalk(replacementDestination) && !actor->stepList.empty()
						&& actor->stepList.front() == oldFirstStep && actor->getStepPositions() == oldPositions,
						"player retargeting preserves the current half-step and its reservation") && casePassed;
				}
				else if (scenario == "npc-lua-after-frame")
					casePassed = check(execute("npcgotoex('RetargetActor',8,10);") == LUA_OK, "second NpcGotoEx replaces a moving actor") && casePassed;
				else if (scenario == "walk-run" || scenario == "run-run") actor->beginRun(replacementDestination);
				else actor->beginWalk(replacementDestination);
			}
			const auto currentPositions = actor->getStepPositions();
			casePassed = check(currentPositions.size() == 1, "retargeted movement keeps one current half-step reservation") && casePassed;
			for (const Point position : currentPositions)
				casePassed = check(reservationCount(position) == 1 && !game.map->canWalk(position),
					"the current movement reservation exists immediately and blocks other actors") && casePassed;
			for (int frame = 0; frame < 400 && (!actor->isStanding() || actor->haveAsyncDest); ++frame)
				CoreLifecycleTestAccess::advanceActorFrame(*actor, 50);
			const auto remainingReservations = reservationCount(oldReservation);
			const bool oldTileWalkable = game.map->canWalk(oldReservation);
			casePassed = check(actor->getPosition() == replacementDestination && actor->isStanding()
				&& actor->getOffset().is_zero() && actor->stepList.empty() && !actor->haveAsyncDest
				&& remainingReservations == 0 && oldTileWalkable,
				"retargeted movement reaches B and releases the old tile for other actors") && casePassed;
			std::cout << "Movement retarget: scenario=" << scenario << " phase=" << (steppedIn ? "ssIn" : "ssOut")
				<< " oldReservation=" << oldReservation.x << "," << oldReservation.y
				<< " remainingReservations=" << remainingReservations << " canWalk=" << oldTileWalkable
				<< " position=" << actor->getPosition().x << "," << actor->getPosition().y
				<< " passed=" << casePassed << std::endl;
			ok = casePassed && ok;
		}
	}
	return ok;
}

bool runPartnerYieldMovementTests()
{
	bool ok = true;
	for (int directions : { 1, 2, 4, 8 })
	{
		for (const std::string scenario : { "clear", "wall", "occupied", "cooldown" })
		{
			GameManager game;
			game.global.data.NPCAI = false;
			game.map->data = std::make_shared<MapData>();
			game.map->data->head.width = game.map->data->head.height = 16;
			game.map->data->tile.assign(16, std::vector<MapTile>(16));
			for (auto& row : game.map->data->tile)
				for (auto& tile : row) tile.obstacle = toObstacle;
			for (int y = 4; y <= 10; ++y) game.map->data->tile[y][4].obstacle = 0;
			for (int y = 5; y <= 9; y += 2) game.map->data->tile[y][3].obstacle = toTrans;
			auto partner = std::make_shared<NPC>();
			for (const auto& actor : { std::static_pointer_cast<NPC>(game.player), partner })
			{
				actor->kind = actor == partner ? nkPartner : nkPlayer;
				actor->relation = nrFriendly;
				actor->life = actor->thew = 100;
				actor->setTime(1000);
				actor->setPosition({ 4, actor == partner ? 8 : 10 }, false);
				NPCActionRes animation;
				animation.imagePackage = std::make_shared<IMPImage>();
				animation.imagePackage->directions = actor == partner ? directions : 8;
				animation.imagePackage->interval = 100;
				animation.imagePackage->frame.resize(24);
				actor->res.stand = actor->res.walk = animation;
				if (actor != partner || directions == 8) actor->res.run = animation;
				actor->beginStand();
			}
			game.npcManager->addNPC(partner);
			if (scenario == "wall") game.map->data->tile[6][4].obstacle = toObstacle;
			if (scenario == "occupied")
			{
				auto blocker = std::make_shared<NPC>();
				blocker->kind = nkNormal;
				blocker->life = 100;
				blocker->setPosition({ 4, 6 }, false);
				game.npcManager->addNPC(blocker);
			}
			if (scenario == "cooldown") partner->lastPathFindFailTime = 999;
			game.map->createDataMap();
			const Point destination{ 4, 4 };
			game.player->beginWalk(destination);
			const bool canYield = directions != 1 && scenario == "clear";
			ok = check(canYield ? !partner->isStanding() && partner->isPartnerBlockingPlayer
				: partner->isStanding() && partner->stepList.empty() && !partner->isPartnerBlockingPlayer,
				"partner yielding preserves movement directions, obstacles and failed-request state") && ok;
			if (scenario == "cooldown")
				ok = check(partner->lastPathFindFailTime == 999, "yielding respects the existing path failure cooldown") && ok;
			if (!canYield) continue;
			for (int frame = 0; frame < 100 && game.player->getPosition().y >= 8; ++frame)
			{
				if (game.player->isStanding()) game.player->beginWalk(destination);
				CoreLifecycleTestAccess::advanceActorFrame(*game.player, 50);
				CoreLifecycleTestAccess::advanceActorFrame(*partner, 50);
			}
			ok = check(game.player->getPosition().y < 8 && game.player->getPosition() != partner->getPosition()
				&& !partner->hasDestinationMapPosition() && !partner->haveAsyncDest,
				"normal movement steps let the player pass the yielded tile without overlapping or leaving a scripted destination") && ok;
		}
	}

	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	const auto packRoot = assetsRoot / std::filesystem::u8path(u8"潇湘行");
	if (!std::filesystem::exists(packRoot / "game_profile.ini"))
	{
		std::cout << "SKIP: optional production partner-yield maze resources are absent\n";
		return ok;
	}
	for (bool running : { false, true })
	{
		ScopedActiveResourceRoot resourceRoot;
		if (!check(resourceRoot.valid(), "partner-yield maze isolates resource and save writes")) return false;
		File::setResourceFallbackRoots({ packRoot.u8string(), (assetsRoot / "jxqy2").u8string(), (assetsRoot / "yycs").u8string() });
		GameManager game;
		ResourceManifest manifest;
		if (!check(manifest.loadFromFile("game_profile.ini"), "partner yielding uses the actual maze feature profile")) return false;
		game.global.applyResourceManifestFeatures(manifest);
		if (!check(game.scriptAPI.loadMap(u8"天忍教-地下迷宫1.map", false)
			&& game.scriptAPI.loadNPC("trj-1.npc") && game.player->loadInitialTemplate(0), "load real maze, NPCs and player movement resources")) return false;
		game.scriptAPI.setPlayerLevel(80);
		game.player->beginStand();
		game.player->setPosition({ 84, 133 }, false);
		game.varList.ensureInitialized();
		game.varList.setInteger("zdjzfy", 1);
		std::unique_ptr<char[]> bytes;
		if (!check(File::readFile("ini/save/zd.npc", bytes) > 0, "read the actual companion template")) return false;
		INIReader ini(bytes);
		auto partner = std::make_shared<NPC>();
		partner->initFromIni(&ini, "NPC034");
		partner->kind = nkPartner;
		partner->relation = nrFriendly;
		partner->beginStand();
		partner->setPosition({ 84, 132 }, false);
		game.npcManager->addNPC(partner);
		game.map->createDataMap();
		const Point destination{ 84, 119 };
		ok = check(partner->isVisibleForRuntime() && partner->findPathByType(destination).empty()
			&& !partner->findPathByType(destination, nptPerfectMaxPlayerTry).empty()
			&& game.map->findPath(game.player->getPosition(), destination, game.player->getMoveDirectionCount()).empty(),
			"visible companion blocks a real winding route beyond the ordinary NPC search budget") && ok;
		if (running) game.player->beginRun(destination);
		else game.player->beginWalk(destination);
		ok = check(partner->isRunning() && partner->isPartnerBlockingPlayer
			&& partner->findPathByType(destination, nptPerfectMaxNpcTry).empty(),
			"the real player entry starts yielding without changing an explicit ordinary NPC search request") && ok;
		for (int elapsed = 0; elapsed < 20000 && game.player->getPosition() != destination; elapsed += 20)
		{
			if (game.player->isStanding())
			{
				if (running) game.player->beginRun(destination);
				else game.player->beginWalk(destination);
			}
			CoreLifecycleTestAccess::advanceActorFrame(*game.player, 20);
			const auto actors = game.npcManager->npcList;
			for (const auto& actor : actors) if (actor) CoreLifecycleTestAccess::advanceActorFrame(*actor, 20);
			CoreLifecycleTestAccess::advanceActorFrame(*game.npcManager, 20);
		}
		ok = check(game.player->getPosition() == destination && game.player->life > 0
			&& !partner->isPartnerBlockingPlayer && !partner->hasDestinationMapPosition() && !partner->haveAsyncDest,
			"walking and running traverse the actual maze after yielding and restore normal partner following") && ok;
		if (!running)
		{
			// This corridor has no side tile: an enemy and the player can enclose the partner.
			game.global.data.NPCAI = false;
			game.player->beginStand();
			game.player->setPosition({ 84, 133 }, false);
			partner->beginStand();
			partner->setPosition({ 83, 130 }, false);
			partner->isPartnerBlockingPlayer = true;
			auto enemy = game.npcManager->npcList.front();
			enemy->beginStand();
			enemy->kind = nkBattle;
			enemy->relation = nrHostile;
			enemy->life = 10000;
			enemy->setPosition({ 82, 129 }, false);
			game.map->createDataMap();
			ok = check(game.map->getRadiusPath(game.player->getPosition(), enemy->getPosition(),
				1, game.player->getMoveDirectionCount()).empty(), "the occupied corridor has no walking path to melee range") && ok;
			for (int frame = 0; frame < 100; ++frame)
			{
				game.player->nextDest = ndAttack;
				game.player->destGE = enemy;
				game.player->beginWalk(enemy->getPosition());
				CoreLifecycleTestAccess::advanceActorFrame(*game.player, 20);
				CoreLifecycleTestAccess::advanceActorFrame(*partner, 20);
			}
			ok = check(game.player->getPosition() == Point{ 84, 133 } && partner->getPosition() == Point{ 83, 130 }
				&& game.player->isStanding() && partner->isStanding() && partner->stepList.empty(),
				"repeated yielding requests do not cross the enemy, player or corridor walls") && ok;
			game.magicManager.addMagic(u8"001春城何处不飞花.ini");
			auto* learned = game.magicManager.findMagic(u8"001春城何处不飞花.ini");
			if (!check(learned && learned->magic && learned->magic->loadSucceeded, "load the actual learned maze skill")) return false;
			game.magicManager.exchange(static_cast<int>(learned - game.magicManager.magicList.data()), game.magicManager.bottomIndex(0));
			bool released = false;
			for (int elapsed = 0; elapsed < 4000 && enemy->life == 10000; elapsed += 20)
			{
				if (game.player->isStanding() && !game.player->nextAction)
				{
					NextAction action;
					action.action = acMagic;
					action.actionParam = 0;
					action.destKind = ndNone;
					action.dest = enemy->getPosition();
					ok = check(game.player->addNextAction(action), "accept normal ground-targeted magic from the quickbar") && ok;
				}
				CoreLifecycleTestAccess::advanceActorFrame(*game.player, 20);
				const auto actors = game.npcManager->npcList;
				for (const auto& actor : actors) if (actor) CoreLifecycleTestAccess::advanceActorFrame(*actor, 20);
				const auto effects = game.effectManager->effectList;
				for (const auto& effect : effects)
				{
					released = released || effect->user.lock() == game.player;
					CoreLifecycleTestAccess::advanceActorFrame(*effect, 20);
				}
				CoreLifecycleTestAccess::advanceActorFrame(*game.effectManager, 20);
				CoreLifecycleTestAccess::advanceActorFrame(*game.npcManager, 20);
			}
			ok = check(released && enemy->life < 10000 && game.player->getPosition() == Point{ 84, 133 }
				&& partner->getPosition() == Point{ 83, 130 },
				"the real ground-targeted skill can hit beyond its approach radius without walking through the enclosed partner") && ok;
		}
	}
	File::setResourceFallbackRoots({});
	return ok;
}

bool runJumpMovementReservationTests()
{
	bool ok = true;
	for (bool actualPlayer : { false, true })
	{
		for (const std::string scenario : { "jump-walk", "jump-run", "jump-lua", "jump-jump",
			"walk-jump", "run-jump", "walk-jump-ssIn", "run-jump-ssIn", "walk-jump-same-step", "hurt-jump" })
		{
			GameManager game;
			game.global.data.NPCAI = false;
			game.map->data = std::make_shared<MapData>();
			game.map->data->head.width = game.map->data->head.height = 16;
			game.map->data->tile.assign(16, std::vector<MapTile>(16));
			std::shared_ptr<NPC> actor = actualPlayer ? std::static_pointer_cast<NPC>(game.player) : std::make_shared<NPC>();
			actor->npcName = "JumpReservationActor";
			actor->kind = actualPlayer ? nkPlayer : nkBattle;
			actor->pathFinder = pfSingle;
			actor->jumpRadius = 10;
			actor->life = actor->thew = 100;
			actor->setPosition({ 4, 10 }, false);
			actor->setTime(1000);
			NPCActionRes animation;
			animation.imagePackage = std::make_shared<IMPImage>();
			animation.imagePackage->directions = 8;
			animation.imagePackage->interval = 100;
			animation.imagePackage->frame.resize(24);
			actor->res.stand = actor->res.walk = actor->res.run = actor->res.jump = actor->res.hurt = animation;
			actor->res.awalk = actor->res.arun = actor->res.ajump = animation;
			if (!actualPlayer) game.npcManager->addNPC(actor);
			game.map->createDataMap();
			const bool beginsJumping = scenario.find("jump-") == 0;
			const bool beginsHurting = scenario == "hurt-jump";
			const bool steppedIn = scenario.find("ssIn") != std::string::npos;
			const bool asynchronous = scenario == "jump-lua";
			const bool jumpToPreviousStep = scenario == "walk-jump-same-step";
			const Point originalDestination{ 4, beginsJumping ? 6 : 8 };
			const Point replacementDestination{ 8, 10 };
			if (beginsJumping) actor->beginJump(originalDestination);
			else if (scenario.find("run-") == 0) actor->beginRun(originalDestination);
			else actor->beginWalk(originalDestination);
			CoreLifecycleTestAccess::advanceActorFrame(*actor, steppedIn ? actor->stepLastTime : 50);
			const auto oldPositions = actor->getStepPositions();
			if (beginsHurting) actor->beginHurt();
			if (!check(oldPositions.size() == 1 && !actor->stepList.empty()
				&& (!beginsJumping || actor->isJumping()) && (!beginsHurting || actor->isHurting())
				&& (!steppedIn || actor->getStepState() == ssIn), "jump fixture enters its real reserved movement phase")) return false;
			const Point oldReservation = oldPositions.front();
			const auto oldPath = actor->stepList;
			const auto savedPositions = actor->savedStepPositions;
			const int oldDirection = actor->direction;
			const int oldThew = actor->thew;
			const UTime oldActionBeginTime = actor->actionBeginTime;
			const auto reservationCount = [&](Point position)
			{
				const auto& reservations = game.map->dataMap.tile[position.y][position.x].stepNPCList;
				return std::count(reservations.begin(), reservations.end(), actor);
			};
			bool casePassed = check(reservationCount(oldReservation) == 1, "jump fixture starts with an actual tile reservation");
			if (scenario == "jump-walk") actor->beginWalk(replacementDestination);
			else if (scenario == "jump-run") actor->beginRun(replacementDestination);
			else if (asynchronous)
			{
				const std::string source = actualPlayer ? "playergotoex(8,10);" : "npcgotoex('JumpReservationActor',8,10);";
				auto bytes = std::make_unique<char[]>(source.size());
				std::copy(source.begin(), source.end(), bytes.get());
				casePassed = check(game.script.runScript(bytes, static_cast<int>(source.size())) == LUA_OK && actor->haveAsyncDest,
					"real GotoEx queues its destination during Jump") && casePassed;
			}
			else actor->beginJump(jumpToPreviousStep ? originalDestination : replacementDestination);
			if (beginsJumping || beginsHurting)
			{
				casePassed = check((beginsJumping ? actor->isJumping() : actor->isHurting())
					&& actor->stepList == oldPath && actor->direction == oldDirection && actor->thew == oldThew
					&& actor->actionBeginTime == oldActionBeginTime && actor->savedStepPositions == savedPositions
					&& reservationCount(oldReservation) == 1,
					"a rejected movement preserves Jump/Hurt path, direction, stamina and reservations") && casePassed;
			}
			else
			{
				casePassed = check(actor->isJumping() && !actor->stepList.empty()
					&& actor->stepList.front() == (jumpToPreviousStep ? originalDestination : replacementDestination)
					&& (jumpToPreviousStep ? reservationCount(oldReservation) == 1
						: reservationCount(oldReservation) == 0 && game.map->canWalk(oldReservation)),
					"Walk/Run to Jump releases the old step before reserving its accepted landing tile") && casePassed;
			}
			Point landing{ -1, -1 };
			for (int frame = 0; frame < 400 && (!actor->isStanding() || actor->haveAsyncDest); ++frame)
			{
				CoreLifecycleTestAccess::advanceActorFrame(*actor, 50);
				if (landing.x < 0 && actor->getJumpState() == jsDown) landing = actor->getPosition();
			}
			const Point expectedFinish = (beginsJumping || beginsHurting || jumpToPreviousStep) && !asynchronous ? originalDestination : replacementDestination;
			casePassed = check(actor->isStanding() && !actor->haveAsyncDest && actor->getPosition() == expectedFinish
				&& actor->stepList.empty() && reservationCount(oldReservation) == 0 && !actor->resumingMove
				&& (beginsHurting || landing == (beginsJumping || jumpToPreviousStep ? originalDestination : replacementDestination)),
				"Jump lands at its accepted destination and queued GotoEx continues only after landing") && casePassed;
			// Leave the landing tile so its actual occupant cannot mask a stale step reservation.
			if (actor->getPosition() != replacementDestination)
			{
				actor->beginWalk(replacementDestination);
				for (int frame = 0; frame < 400 && !actor->isStanding(); ++frame)
					CoreLifecycleTestAccess::advanceActorFrame(*actor, 50);
			}
			casePassed = check(actor->getPosition() == replacementDestination && actor->isStanding()
				&& actor->getOffset().is_zero() && reservationCount(oldReservation) == 0 && game.map->canWalk(oldReservation),
				"old Jump/movement tiles remain walkable after the actor leaves") && casePassed;
			std::cout << "Jump reservation: actor=" << (actualPlayer ? "Player" : "NPC") << " scenario=" << scenario
				<< " landing=" << landing.x << "," << landing.y << " oldReservation=" << oldReservation.x << "," << oldReservation.y
				<< " remainingReservations=" << reservationCount(oldReservation) << " canWalk=" << game.map->canWalk(oldReservation)
				<< " passed=" << casePassed << std::endl;
			ok = casePassed && ok;
		}
	}
	return ok;
}

bool runHurtMovementRetargetTests()
{
	bool ok = true;
	for (bool running : { false, true })
	{
		for (bool steppedIn : { false, true })
		{
			for (const std::string request : { "walk", "run", "lua-goto" })
			{
				GameManager game;
				game.global.data.NPCAI = false;
				game.global.data.canInput = true;
				game.map->data = std::make_shared<MapData>();
				game.map->data->head.width = game.map->data->head.height = 16;
				game.map->data->tile.assign(16, std::vector<MapTile>(16));
				auto actor = std::make_shared<ScriptReplacementActor<NPC>>();
				actor->npcName = "HurtRetargetActor";
				actor->kind = nkBattle;
				actor->pathFinder = pfSingle;
				actor->life = actor->thew = 100;
				actor->setPosition({ 4, 10 }, false);
				NPCActionRes animation;
				animation.imagePackage = std::make_shared<IMPImage>();
				animation.imagePackage->directions = 8;
				animation.imagePackage->interval = 100;
				animation.imagePackage->frame.resize(24);
				actor->res.stand = actor->res.walk = actor->res.run = actor->res.hurt = animation;
				NPCActionRes battleAnimation = animation;
				battleAnimation.imagePackage = std::make_shared<IMPImage>();
				battleAnimation.imagePackage->directions = 8;
				battleAnimation.imagePackage->interval = 100;
				battleAnimation.imagePackage->frame.resize(32);
				actor->res.awalk = actor->res.arun = battleAnimation;
				if (running && !steppedIn && request == "walk") actor->res.walk = actor->res.run = NPCActionRes{};
				game.npcManager->addNPC(actor);
				game.map->createDataMap();
				const Point originalDestination{ 4, 8 };
				const Point replacementDestination{ 8, 10 };
				if (running) actor->beginRun(originalDestination);
				else actor->beginWalk(originalDestination);
				if (steppedIn) CoreLifecycleTestAccess::advanceActorFrame(*actor, actor->stepLastTime);
				const auto reservedPositions = actor->getStepPositions();
				if (!check((actor->isWalking() || actor->isRunning()) && reservedPositions.size() == 1
					&& actor->getStepState() == (steppedIn ? ssIn : ssOut), "Hurt retarget fixture reaches the real requested movement half-step")) return false;
				const Point oldReservation = reservedPositions.front();
				actor->beginHurt();
				const auto hurtPath = actor->stepList;
				const int hurtDirection = actor->direction;
				if (!check(actor->isHurting() && actor->resumingMove && !game.map->canWalk(oldReservation),
					"Hurt retains the interrupted movement and its actual reserved tile")) return false;
				bool casePassed = true;
				bool pathChangedDuringHurt = false;
				bool recoveredBattleMovement = false;
				const auto finishMovement = [&]()
				{
					for (int frame = 0; frame < 200 && !actor->isStanding(); ++frame)
					{
						const bool wasHurting = actor->isHurting();
						CoreLifecycleTestAccess::advanceActorFrame(*actor, 50);
						if (wasHurting && !actor->isHurting())
							recoveredBattleMovement = actor->nowAction == (running ? acARun : acAWalk) && actor->actionLastTime > 0;
					}
					return actor->isStanding();
				};
				if (request == "lua-goto")
				{
					const std::string source = "npcgoto('HurtRetargetActor',8,10);addmoney(1);";
					auto bytes = std::make_unique<char[]>(source.size());
					std::copy(source.begin(), source.end(), bytes.get());
					game.player->setMoney(0);
					casePassed = check(game.script.runScript(bytes, static_cast<int>(source.size())) == LUA_OK
						&& !actor->exhaustedFrames && actor->waits > 0 && game.player->money == 1 && game.global.data.canInput,
						"NpcGoto during Hurt waits, continues Lua and restores input") && casePassed;
				}
				else
				{
					if (request == "run") actor->beginRun(replacementDestination);
					else actor->beginWalk(replacementDestination);
					pathChangedDuringHurt = actor->stepList != hurtPath;
					casePassed = check(actor->isHurting() && actor->resumingMove && !pathChangedDuringHurt
						&& actor->direction == hurtDirection && actor->savedStepPositions == reservedPositions,
						"a movement rejected during Hurt preserves its path, direction and saved reservations") && casePassed;
					casePassed = check(finishMovement() && actor->getPosition() == originalDestination,
						"the interrupted movement finishes at its original destination after Hurt") && casePassed;
					casePassed = check(recoveredBattleMovement,
						"Hurt recovery selects the real battle walk/run animation with a nonzero duration") && casePassed;
					// Reissue after Hurt so both the original and replacement tiles are vacated normally.
					if (request == "run") actor->beginRun(replacementDestination);
					else actor->beginWalk(replacementDestination);
					casePassed = check(finishMovement(), "a new movement is accepted after Hurt recovery") && casePassed;
				}
				const auto& reservations = game.map->dataMap.tile[oldReservation.y][oldReservation.x].stepNPCList;
				const auto remainingReservations = std::count(reservations.begin(), reservations.end(), actor);
				const bool oldTileWalkable = game.map->canWalk(oldReservation);
				casePassed = check(actor->getPosition() == replacementDestination && actor->isStanding()
					&& actor->getOffset().is_zero() && actor->stepList.empty() && !actor->resumingMove
					&& remainingReservations == 0 && oldTileWalkable,
					"Hurt retarget reaches B and releases the old tile for other characters") && casePassed;
				std::cout << "Hurt retarget: initial=" << (running ? "run" : "walk")
					<< " phase=" << (steppedIn ? "ssIn" : "ssOut") << " request=" << request
					<< " oldReservation=" << oldReservation.x << "," << oldReservation.y
					<< " pathChangedDuringHurt=" << (request == "lua-goto" ? "not-observed" : pathChangedDuringHurt ? "1" : "0")
					<< " recoveredBattleMovement=" << (request == "lua-goto" ? "not-observed" : recoveredBattleMovement ? "1" : "0")
					<< " position=" << actor->getPosition().x << "," << actor->getPosition().y
					<< " remainingReservations=" << remainingReservations << " canWalk=" << oldTileWalkable
					<< " passed=" << casePassed << std::endl;
				ok = casePassed && ok;
			}
		}
	}
	return ok;
}

bool runProductionMedicineChestTests()
{
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid(), "medicine chest tests use an isolated resource root"))
	{
		return false;
	}
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	bool ok = true;
	for (const char* pack : { "yycs", u8"江湖余尘", u8"江湖余尘二" })
	{
		const auto packRoot = assetsRoot / std::filesystem::u8path(pack);
		if (!std::filesystem::exists(packRoot / "game_profile.ini"))
		{
			std::cout << "SKIP: optional production medicine chest pack is absent: " << pack << '\n';
			continue;
		}
		bool prepared = true;
		for (const char* relativePath : {
			u8"script/map/map_030_悲魔山庄/悲魔药箱.txt",
			u8"script/common/关宝箱.txt",
			u8"ini/goods/goods-m10-珊瑚.ini",
			u8"ini/goods/goods-m09-冰蚕.ini",
			"ini/save/map030_obj.obj" })
		{
			std::ifstream input(packRoot / std::filesystem::u8path(relativePath), std::ios::binary);
			const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
			prepared = check(!contents.empty() && writeVirtualFile(relativePath, contents),
				"medicine chest fixture copies actual script, item and object-list bytes") && prepared;
		}
		if (!prepared)
		{
			ok = false;
			continue;
		}
		GameManager gameManager;
		gameManager.mapFolderName = u8"map_030_悲魔山庄";
		INIReader objects("ini/save/map030_obj.obj");
		auto chest = std::make_shared<Object>();
		chest->kind = objects.GetInteger("OBJ010", "Kind", -1);
		chest->scriptFile = objects.Get("OBJ010", "ScriptFile", "");
		gameManager.objectManager->objectList.push_back(chest);
		ok = check(chest->kind == okBox && chest->scriptFile == u8"悲魔药箱.txt",
			"production object list binds the medicine chest to its map-local script") && ok;
		gameManager.runObjScript(chest);
		ok = check(gameManager.goodsManager.getItemNum(u8"goods-m10-珊瑚.ini") == 1 &&
			gameManager.goodsManager.getItemNum(u8"goods-m09-冰蚕.ini") == 1 &&
			chest->nowAction == oaOpening && chest->scriptFile == u8"关宝箱.txt",
			"production medicine chest opens, awards both medicines once and replaces its script") && ok;
		gameManager.runObjScript(chest);
		ok = check(gameManager.goodsManager.getItemNum(u8"goods-m10-珊瑚.ini") == 1 &&
			gameManager.goodsManager.getItemNum(u8"goods-m09-冰蚕.ini") == 1 &&
			chest->scriptFile.empty(),
			"production empty-chest interaction clears its script without repeating rewards") && ok;
		std::cout << "Medicine chest production script checked: " << pack << '\n';
	}
	return ok;
}

bool runProductionXiaoxiangTrapIsolationTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	const auto packRoot = assetsRoot / std::filesystem::u8path(u8"潇湘行");
	if (!std::filesystem::exists(packRoot / "game_profile.ini"))
	{
		std::cout << "SKIP: optional production Xiaoxiang trap resources are absent\n";
		return true;
	}
	ScopedActiveResourceRoot resourceRoot;
	SaveFileManager::CurrentPathScope currentPath("save/xiaoxiang_trap_isolation");
	if (!check(resourceRoot.valid() && currentPath.valid(), "Xiaoxiang trap checks isolate all save writes"))
	{
		return false;
	}
	File::setResourceFallbackRoots({ packRoot.u8string(), (assetsRoot / "jxqy2").u8string(),
		(assetsRoot / "yycs").u8string() });
	struct TrapGuard
	{
		const char* map;
		int index;
		const char* file;
	};
	const TrapGuard guards[] = {
		{ u8"中都", 14, u8"trap-14.txt" },
		{ u8"中都", 15, u8"trap-15.txt" },
		{ u8"中都", 7, u8"trap-7.txt" },
		{ u8"中都", 11, u8"地图陷阱11.txt" },
		{ u8"中都", 3, u8"酒店门口地图陷阱.txt" },
		{ u8"中都", 5, u8"龙音寺门口地图陷阱5.txt" },
		{ u8"临安城", 1, u8"first.txt" },
		{ u8"临安城", 4, u8"trap4.txt" },
		{ u8"临安城", 5, u8"trap5.txt" },
		{ u8"临安大牢第1层", 1, u8"地图陷阱1.txt" },
		{ u8"主角家-狂沙镇", 1, u8"地图切换.txt" },
		{ u8"主角家-狂沙镇", 2, u8"地图切换1.txt" },
		{ u8"主角家-狂沙镇", 3, u8"地图切换2.txt" },
		{ u8"凤池山庄", 5, u8"datingtalk.txt" },
		{ u8"凤池山庄", 4, u8"maptrap4.txt" },
		{ u8"凤池山庄", 7, u8"后厅.txt" },
		{ u8"大牢出口1", 1, u8"trap2-to大牢.txt" },
		{ u8"天忍教-地下迷宫2", 3, u8"地图陷阱3.txt" },
		{ u8"天忍教-地下迷宫3", 3, u8"地图陷阱3.txt" },
		{ u8"狂沙镇", 6, u8"告示牌.txt" },
		{ u8"狂沙镇", 4, u8"柴嵩交谈.txt" },
		{ u8"狂沙镇-龙门客栈", 3, u8"对话.txt" },
		{ u8"矿山", 2, u8"地图陷阱2.txt" },
		{ u8"葬马岗", 2, u8"地图陷阱2.txt" },
		{ u8"铁门寨", 2, u8"地图切换1.txt" },
		{ u8"铁门寨", 3, u8"进入山寨.txt" },
		{ u8"长安", 2, u8"trap-2.txt" },
		{ u8"风雪山庄", 2, u8"地图陷阱2.txt" },
		{ u8"龙门客栈", 3, u8"柴嵩.txt" }
	};
	GameManager gameManager;
	bool ok = gameManager.traps.loadInitialTemplate();
	gameManager.varList.clearExcept({});
	for (const auto& guard : guards)
	{
		const std::string relative = std::string("script/map/") + guard.map + "/" + guard.file;
		std::ifstream local(packRoot / std::filesystem::u8path(relative), std::ios::binary);
		const std::string expected((std::istreambuf_iterator<char>(local)), std::istreambuf_iterator<char>());
		std::unique_ptr<char[]> resolved;
		const int length = File::readFile(relative, resolved);
		if (!check(!expected.empty() && length == static_cast<int>(expected.size()) && resolved
			&& std::string(resolved.get(), length) == expected,
			"the real resource chain resolves the local trap guard before its base-game script"))
		{
			ok = false;
			continue;
		}
		ok = check(gameManager.traps.get(guard.map, guard.index).empty(),
			"the new-game template excludes each guarded original-story trap") && ok;
		gameManager.mapFolderName = guard.map;
		gameManager.global.data.mapName = std::string(guard.map) + ".map";
		gameManager.player->setPosition({ 8, 9 });
		gameManager.player->setMoney(1234);
		gameManager.varList.setInteger("ChaiSongTalk", 7654321);
		gameManager.traps.set(guard.map, guard.index, guard.file);
		gameManager.traps.set(guard.map, 255, "unrelated-current-map.txt");
		gameManager.traps.set("unrelated-map", guard.index, "unrelated-other-map.txt");
		gameManager.traps.beginMapVisit();
		// Restore a stale binding directly; this does not bypass or test old-save version admission.
		gameManager.runTrapScript(guard.index);
		ok = check(gameManager.traps.get(guard.map, guard.index).empty()
			&& !gameManager.traps.hasTriggered(guard.index) && !gameManager.inEvent
			&& gameManager.mapFolderName == guard.map && gameManager.player->getPosition() == Point{ 8, 9 }
			&& gameManager.player->money == 1234 && gameManager.varList.getInteger("ChaiSongTalk") == 7654321,
			"actual trap dispatch clears only the stale binding without running original story actions") && ok;
		gameManager.traps.freeResource();
		ok = check(gameManager.traps.load() && gameManager.traps.get(guard.map, guard.index).empty()
			&& gameManager.traps.get(guard.map, 255) == "unrelated-current-map.txt"
			&& gameManager.traps.get("unrelated-map", guard.index) == "unrelated-other-map.txt",
			"the guard persists its removal while preserving unrelated trap definitions") && ok;
	}
	std::cout << "Xiaoxiang production trap guards checked: " << std::size(guards) << '\n';
	return ok;
}

bool runProductionXiaoxiangMoneyPickupTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	const auto packRoot = assetsRoot / std::filesystem::u8path(u8"潇湘行");
	if (!std::filesystem::exists(packRoot / "game_profile.ini"))
	{
		std::cout << "SKIP: optional production Xiaoxiang money resources are absent\n";
		return true;
	}
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid(), "Xiaoxiang money pickups use isolated writable resources"))
	{
		return false;
	}
	File::setResourceFallbackRoots({ packRoot.u8string(), (assetsRoot / "jxqy2").u8string(),
		(assetsRoot / "yycs").u8string() });
	bool ok = true;
	int checkedPickups = 0;
	for (const char* tableName : { "home.obj", "trj3.obj", "twb-1.obj", "zd.obj" })
	{
		SaveFileManager::CurrentPathScope currentPath(std::string("save/xiaoxiang_money_") + tableName);
		GameManager gameManager;
		INIReader table(std::string("ini/save/") + tableName);
		gameManager.global.data.mapName = table.Get("Head", "Map", "");
		gameManager.mapFolderName = std::filesystem::u8path(gameManager.global.data.mapName).stem().u8string();
		// Exercise the actual object/script/image resources; sound output is not an acceptance item here.
		CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, "playsound", [](lua_State*) { return 0; });
		if (!check(currentPath.valid() && gameManager.objectManager->load(tableName)
			&& gameManager.objectManager->objectList.size() == static_cast<size_t>(table.GetInteger("Head", "Count", 0)),
			"load each actual Xiaoxiang object table with its original names and bindings"))
		{
			ok = false;
			continue;
		}
		std::vector<size_t> pickups;
		std::vector<std::string> expectedScripts;
		for (size_t index = 0; index < gameManager.objectManager->objectList.size(); ++index)
		{
			const auto& object = gameManager.objectManager->objectList[index];
			expectedScripts.push_back(object->scriptFile);
			if (object->scriptFile == u8"拣到银子.txt")
			{
				pickups.push_back(index);
				ok = check(object->res.image != nullptr, "the bound money object has its real image resource") && ok;
			}
		}
		ok = check(!pickups.empty(), "each selected production table actually binds money pickups") && ok;
		checkedPickups += static_cast<int>(pickups.size());
		gameManager.player->setMoney(1000);
		for (int stage = 0; stage < 3; ++stage)
		{
			for (const size_t index : pickups)
			{
				const auto object = gameManager.objectManager->objectList[index];
				const int beforeMoney = gameManager.player->money;
				gameManager.runObjScript(object);
				expectedScripts[index] = stage == 0 ? u8"关宝箱.txt" : "";
				const int reward = gameManager.player->money - beforeMoney;
				ok = check((stage == 0 ? reward >= 10 && reward <= 100 : reward == 0)
					&& object->scriptFile == expectedScripts[index]
					&& !gameManager.inEvent && gameManager.scriptObj == nullptr,
					"actual money pickup awards once, then clears its binding without repeating the reward") && ok;
				for (size_t other = 0; other < expectedScripts.size(); ++other)
				{
					ok = check(gameManager.objectManager->objectList[other]->scriptFile == expectedScripts[other],
						"empty-name rebinding changes only the interacting object, including duplicate names") && ok;
				}
			}
			const int savedMoney = gameManager.player->money;
			const auto oldObjects = gameManager.objectManager->objectList;
			ok = check(gameManager.player->save(0) && gameManager.objectManager->save(tableName),
				"save the actual player and object files after each pickup stage") && ok;
			gameManager.player->setMoney(0);
			gameManager.objectManager->clearObj();
			if (!check(gameManager.player->load(0) && gameManager.objectManager->load(tableName)
				&& gameManager.player->money == savedMoney
				&& gameManager.objectManager->objectList.size() == oldObjects.size(),
				"reload money and pickup bindings from isolated files after discarding their in-memory state"))
			{
				ok = false;
				break;
			}
			for (size_t index = 0; index < oldObjects.size(); ++index)
			{
				const auto& restored = gameManager.objectManager->objectList[index];
				ok = check(restored != oldObjects[index] && restored->scriptFile == expectedScripts[index]
					&& restored->objName == oldObjects[index]->objName
					&& restored->getPosition() == oldObjects[index]->getPosition(),
					"new object instances preserve the saved binding, name and map position") && ok;
			}
		}
		std::cout << "Xiaoxiang money pickup table checked: " << tableName << " pickups=" << pickups.size() << '\n';
	}
	File::setResourceFallbackRoots({});
	return check(checkedPickups == 26, "all 26 production money bindings in the four local tables were exercised") && ok;
}

bool runProductionRandomRewardContinuationTests()
{
	const auto packRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
		"assets" / std::filesystem::u8path(u8"新月无痕");
	if (!std::filesystem::exists(packRoot / "game_profile.ini"))
	{
		std::cout << "SKIP: optional production random-reward pack is absent\n";
		return true;
	}
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid(), "random reward continuation uses an isolated resource root"))
	{
		return false;
	}
	const auto copyResource = [&](const std::string& path)
	{
		std::ifstream input(packRoot / std::filesystem::u8path(path), std::ios::binary);
		const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		return check(!contents.empty() && writeVirtualFile(path, contents),
			"copy actual random-reward script, table or item bytes");
	};
	if (!copyResource(u8"ini/buy/特卖.ini"))
	{
		return false;
	}
	BuySellInventoryData productionTable;
	if (!check(BuySellInventory::parseText(readVirtualFile(u8"ini/buy/特卖.ini"), productionTable)
		&& productionTable.count == 30 && !productionTable.items[0].iniFile.empty()
		&& productionTable.items[29].iniFile.empty(), "production random table has an empty thirtieth choice"))
	{
		return false;
	}
	if (!copyResource("ini/goods/" + productionTable.items[0].iniFile))
	{
		return false;
	}
	struct DeathReward
	{
		const char* npcFile;
		const char* section;
		const char* map;
		const char* script;
	};
	const DeathReward cases[] = {
		{ "duanjiazhuan.npc", "NPC066", u8"段家庄", u8"段环山死.txt" },
		{ "twddxmg.npc", "NPC177", u8"天王岛-地下迷宫", u8"新建 文本文档.txt" },
		{ "zhongdukill.npc", "NPC128", u8"中都夜", u8"邂逅夜明珠.txt" },
		{ "zhongdukill.npc", "NPC129", u8"中都夜", u8"中都夜金兵头目.txt" },
	};
	bool ok = true;
	for (const auto& item : cases)
	{
		const std::string scriptPath = std::string(item.section) == "NPC066"
			? std::string("script/map/") + item.map + "/" + item.script
			: std::string("script/common/") + item.script;
		if (!copyResource("ini/save/" + std::string(item.npcFile)) || !copyResource(scriptPath))
		{
			ok = false;
			continue;
		}
		INIReader npcTable("ini/save/" + std::string(item.npcFile));
		ok = check(npcTable.Get(item.section, "DeathScript", "") == item.script,
			"production NPC section binds the tested death reward") && ok;
		for (int selectedIndex : { 0, 29 })
		{
			// Exercise both possible reward outcomes deterministically, not the RNG distribution.
			// Only the isolated table is reduced to one actual selected production row.
			BuySellInventoryData selectedTable;
			selectedTable.count = 1;
			selectedTable.items.push_back(productionTable.items[selectedIndex]);
			ok = check(writeVirtualFile(u8"ini/buy/特卖.ini", BuySellInventory::serializeText(selectedTable)),
				"write deterministic selection fixture without changing production resources") && ok;
			GameManager gameManager;
			gameManager.varList.ensureInitialized();
			gameManager.varList.setInteger("duanhuanshan", 41);
			CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, "addrandgoods",
				CoreLifecycleTestAccess::observeRandomReward);
			CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, "setobjscript",
				CoreLifecycleTestAccess::observeObjectScriptBinding);
			gameManager.mapFolderName = item.map;
			gameManager.player->money = 100;
			gameManager.player->exp = 7;
			gameManager.player->levelUpExp = 100000;
			gameManager.global.data.mapTime = 0;
			auto npc = std::make_shared<NPC>();
			npc->npcName = npcTable.Get(item.section, "Name", "");
			gameManager.npcManager->npcList.push_back(npc);
			// Keep rewards, variables, AI and map time real; omit combat, movement and presentation.
			for (const char* command : { "sleep", "fadeout", "fadein", "talk", "delnpc", "delobj",
				"addnpc", "setplayerpos", "setplayerdir", "playergoto", "npcattack" })
			{
				CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, command, [](lua_State*) { return 0; });
			}
			gameManager.runNPCDeathScript(npc, item.script, item.map);
			ok = check(gameManager.goodsManager.getItemNum(productionTable.items[0].iniFile)
				== (selectedIndex == 0 ? 1 : 0) && !gameManager.inEvent && gameManager.scriptNPC == nullptr,
				"actual death script completes with the selected reward or no reward") && ok;
			ok = check(gameManager.varList.getInteger("ObservedRandomReward") == 1
				&& gameManager.varList.getInteger("ObservedObjectBinding") == (std::string(item.section) == "NPC066" ? 0 : 1),
				"each bound death script reaches the real reward and subsequent binding calls") && ok;
			ok = check(gameManager.player->money == (std::string(item.section) == "NPC177" ? 10100 : 100)
				&& gameManager.player->exp == (std::string(item.section) == "NPC177" ? 10007 : 7),
				"empty random choice does not discard the woodman's money and experience") && ok;
			ok = check(gameManager.varList.getInteger("DuanHuanShan") == (std::string(item.section) == "NPC066" ? 1 : 0)
				&& gameManager.varList.getInteger("duanhuanshan") == 41 && gameManager.global.data.NPCAI
				&& gameManager.global.data.mapTime == (std::string(item.section) == "NPC129" ? 3 : 0),
				"empty random choice preserves story continuation, variable case, AI and map-time updates") && ok;
			std::cout << "Random reward continuation checked: " << item.script << " slot=" << selectedIndex + 1 << '\n';
		}
	}
	return ok;
}

bool runProductionZhuangDialogueTests()
{
	const auto packRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
		"assets" / std::filesystem::u8path(u8"江湖余尘");
	if (!std::filesystem::exists(packRoot / "game_profile.ini"))
	{
		std::cout << "SKIP: optional production Zhuang dialogue pack is absent\n";
		return true;
	}
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid(), "Zhuang dialogue uses an isolated resource root"))
	{
		return false;
	}
	for (const char* relativePath : {
		u8"script/map/map_022_清平乡/庄允城对话.txt",
		u8"script/map/map_022_清平乡/庄允城不满.txt",
		u8"script/map/map_022_清平乡/庄允城救人.txt",
		"ini/save/qingpingxiang.npc" })
	{
		std::ifstream input(packRoot / std::filesystem::u8path(relativePath), std::ios::binary);
		const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		if (!check(!contents.empty() && writeVirtualFile(relativePath, contents),
			"copy actual Zhuang dialogue scripts and NPC-list bytes"))
		{
			return false;
		}
	}
	// The map is synthetic. Dialogue choices use the real Lua/API automation
	// path; Say presentation is observed without opening a modal window.
	auto mapBytes = MapV3ContractFixture::build();
	std::fill(mapBytes.begin() + MapV3ContractFixture::BaseHeaderLength,
		mapBytes.begin() + MapV3ContractFixture::HeaderLength + MapV3ContractFixture::NameLength, std::uint8_t{ 0 });
	const std::string mapName = u8"map_022_清平乡.map";
	bool ok = check(writeVirtualFile("map/" + mapName,
		std::string(reinterpret_cast<const char*>(mapBytes.data()), mapBytes.size())),
		"write isolated Zhuang save/load map");
	for (const auto selections : { std::pair<int, int>{ 0, 1 }, { 1, 0 }, { 1, 1 }, { 2, 1 }, { 3, 1 } })
	{
		for (const bool asynchronous : { false, true })
		{
			if (asynchronous && selections.first != 0 && selections != std::pair<int, int>{ 1, 1 })
			{
				continue;
			}
			ok = check(File::clearDirectoryFiles("save/game") && File::clearDirectoryFiles("save/rpg1"),
				"reset isolated Zhuang save generations") && ok;
			GameManager gameManager;
			gameManager.setAutomationHooksEnabled(true);
			gameManager.varList.ensureInitialized();
			gameManager.traps.beginMapVisit();
			gameManager.global.data.mapName = mapName;
			gameManager.mapFolderName = u8"map_022_清平乡";
			gameManager.varList.setInteger("TestInitialChoice", selections.first);
			gameManager.varList.setInteger("TestFollowupChoice", selections.second);
			CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, "chooseex", [](lua_State* state)
			{
				const std::string variable = luaL_checkstring(state, lua_gettop(state));
				gm->varList.setInteger("__automation_choose_enabled", 1);
				gm->varList.setInteger("__automation_choose_selection", gm->varList.getInteger(
					variable == "XX" ? "TestInitialChoice" : "TestFollowupChoice"));
				gm->varList.setInteger("TestChoiceCount", gm->varList.getInteger("TestChoiceCount") + 1);
				const std::string message = luaL_checkstring(state, 1);
				if (message == u8"庄允城：滚出去！别自找没趣！")
				{
					gm->varList.setInteger("TestAngryDialogueCount", gm->varList.getInteger("TestAngryDialogueCount") + 1);
				}
				return CoreLifecycleTestAccess::chooseExWithAutomation(state);
			});
			CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, "say", [](lua_State* state)
			{
				const std::string message = luaL_checkstring(state, 1);
				if (message == u8"庄允城：哼，算你识相！")
				{
					gm->varList.setInteger("TestAngryLeaveCount", gm->varList.getInteger("TestAngryLeaveCount") + 1);
				}
				return 0;
			});
			if (!check(gameManager.scriptAPI.loadNPC("qingpingxiang.npc"), "load the actual Qingping NPC list"))
			{
				ok = false;
				continue;
			}
			const auto targets = gameManager.npcManager->findNPC(u8"庄允城");
			if (!check(targets.size() == 1 && targets.front()->scriptFile == u8"庄允城对话.txt",
				"actual NPC list has one Zhuang bound to the opening dialogue"))
			{
				ok = false;
				continue;
			}
			const bool rescue = selections.first == 0;
			const bool offendedLeave = selections.first == 1 && selections.second == 1;
			const bool attack = selections.first == 2 || (selections.first == 1 && selections.second == 0);
			const int originalKind = targets.front()->kind;
			const int originalRelation = targets.front()->relation;
			const std::string expectedScript = rescue ? u8"庄允城救人.txt" :
				(offendedLeave ? u8"庄允城不满.txt" : u8"庄允城对话.txt");
			gameManager.runNPCScript(targets.front());
			ok = check(targets.front()->scriptFile == expectedScript &&
				gameManager.varList.getInteger("TestChoiceCount") == (selections.first == 1 ? 2 : 1) &&
				gameManager.varList.getInteger("__automation_choose_complete") == 1 &&
				gameManager.varList.getInteger("zhaixinglou") == (rescue ? 1 : 0) &&
				gameManager.varList.getInteger("yulin") == (rescue ? 2 : 0) &&
				targets.front()->kind == (attack ? 1 : originalKind) &&
				targets.front()->relation == (attack ? 1 : originalRelation),
				"all five actual dialogue branches retain their binding, choice, rescue and combat effects") && ok;
			if (rescue)
			{
				const auto memo = gameManager.memo.memo;
				std::string joined;
				for (const auto& line : memo)
				{
					joined += line;
				}
				ok = check(joined == u8"●帮清平乡的庄允城去摘星楼救他的女儿。" && gameManager.saveGame(1),
					"the actual rescue branch creates and saves the complete quest memo") && ok;
				gameManager.memo.clear();
				const bool loaded = asynchronous ? gameManager.scriptAPI.loadGameAsync(1) : gameManager.loadGame(1);
				ok = check(loaded && gameManager.memo.memo == memo &&
					gameManager.varList.getInteger("zhaixinglou") == 1 && gameManager.varList.getInteger("yulin") == 2,
					"sync and async full reload restore the actual rescue memo and quest variables together") && ok;
				continue;
			}
			if (!offendedLeave)
			{
				continue;
			}
			gameManager.runNPCScript(targets.front());
			ok = check(gameManager.varList.getInteger("TestAngryDialogueCount") == 1 &&
				gameManager.varList.getInteger("TestAngryLeaveCount") == 1,
				"offend then leave switches the next interaction to the actual angry dialogue") && ok;
			ok = check(gameManager.saveGame(1), "save the actual offended NPC binding to a complete slot") && ok;
			const bool loaded = asynchronous ? gameManager.scriptAPI.loadGameAsync(1) : gameManager.loadGame(1);
			const auto restored = gameManager.npcManager->findNPC(u8"庄允城");
			if (!check(loaded && restored.size() == 1 && restored.front()->scriptFile == u8"庄允城不满.txt",
				"sync and async full save reload retain the angry dialogue binding"))
			{
				ok = false;
				continue;
			}
			gameManager.runNPCScript(restored.front());
			ok = check(gameManager.varList.getInteger("TestAngryDialogueCount") == 2 &&
				gameManager.varList.getInteger("TestAngryLeaveCount") == 2,
				"the next interaction after full reload still executes the angry leave branch") && ok;
			gameManager.varList.setInteger("TestFollowupChoice", 0);
			gameManager.runNPCScript(restored.front());
			const auto guards = gameManager.npcManager->findNPC(u8"保镖");
			ok = check(restored.front()->kind == 1 && restored.front()->relation == 1 && !guards.empty() &&
				std::all_of(guards.begin(), guards.end(), [](const auto& guard)
				{
					return guard->kind == 1 && guard->relation == 1;
				}), "the rebound angry attack branch still makes Zhuang and his guards hostile") && ok;
		}
	}
	if (ok)
	{
		std::cout << "Zhuang production dialogue branches and full sync/async save reload passed\n";
	}
	return ok;
}

bool runProductionHanboReturnBranchTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	bool ok = true;
	for (const char* pack : { "yycs", u8"江湖余尘", u8"江湖余尘二" })
	{
		const auto packRoot = assetsRoot / std::filesystem::u8path(pack);
		if (!std::filesystem::exists(packRoot / "game_profile.ini"))
		{
			std::cout << "SKIP: optional Hanbo return pack is absent: " << pack << '\n';
			continue;
		}
		for (const char* name : { "trap13.txt", u8"寒波谷归来2.txt", u8"寒波谷归来3.txt" })
		{
			const auto path = packRoot / std::filesystem::u8path(u8"script/map/map_030_悲魔山庄") /
				std::filesystem::u8path(name);
			std::ifstream input(path, std::ios::binary);
			const std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
			if (!check(!source.empty(), "Hanbo branch probe reads the actual production script"))
			{
				ok = false;
				continue;
			}
			for (const int event : { 0, 2039, 2040 })
			{
				GameManager gameManager;
				gameManager.varList.ensureInitialized();
				gameManager.varList.setInteger("Event", event);
				gameManager.varList.setInteger("2040", 0);
				// Stop at the first scene command: this checks the real script's
				// entry guard without pretending to play its UI and combat scene.
				for (const char* command : { "mergenpc", "runscript" })
				{
					CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, command, [](lua_State* state)
					{
						gm->varList.setInteger("HanboBranchReached", 1);
						return luaL_error(state, "Hanbo branch probe reached scene entry");
					});
				}
				auto bytes = std::make_unique<char[]>(source.size());
				std::copy(source.begin(), source.end(), bytes.get());
				const int result = gameManager.script.runScript(bytes, static_cast<int>(source.size()));
				ok = check(result == (event == 2040 ? LUA_ERRRUN : LUA_OK) &&
					gameManager.varList.getInteger("HanboBranchReached") == (event == 2040 ? 1 : 0),
					"Hanbo return enters only at Event 2040, independently of a numeric-name variable") && ok;
			}
			std::cout << "Hanbo return entry checked: " << pack << '/' << name << '\n';
		}
	}
	return ok;
}

bool runScriptDispatchCharacterizationTests()
{
	ScopedActiveResourceRoot resourceRoot;
	SaveFileManager::CurrentPathScope currentPath("save\\dispatch_probe");
	if (!check(resourceRoot.valid() && currentPath.valid(),
			"script dispatch probes use an isolated resource and save root"))
	{
		return false;
	}
	GameManager gameManager;
	gameManager.varList.ensureInitialized();
	CoreLifecycleTestAccess::setFrameTime(gameManager, 1);
	auto execute = [&](const std::string& source)
	{
		auto bytes = std::make_unique<char[]>(source.size());
		std::copy(source.begin(), source.end(), bytes.get());
		return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
	};
	bool ok = check(writeVirtualFile("script/common/dispatch-child.lua",
		"assign('ChildObservedStage',getvar('ParentStage'));"),
		"nested dispatch probe writes its real child script");
	ok = check(execute("assign('ParentStage',1); runscript('dispatch-child.lua'); "
		"assign('ParentStage',2);") == LUA_OK &&
		gameManager.varList.getInteger("ChildObservedStage") == 1 &&
		gameManager.varList.getInteger("ParentStage") == 2,
		"C++ RunScript executes the child before the parent continuation") && ok;

	// Characterize F03 through the production queue and serializer. This is an
	// explicit reentry probe, not a simulated C# scheduler or a UI save test.
	CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, "test_dispatch_snapshot", [](lua_State* state)
	{
		const bool saved = gm->saveScriptRuntimeState();
		INIReader snapshot(SaveFileManager::CurrentPath() + GLOBAL_INI);
		const auto tasks = readParallelScriptRuntimeStates(snapshot);
		lua_pushinteger(state, saved ? static_cast<lua_Integer>(tasks.size()) : -1);
		return 1;
	});
	CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, "test_dispatch_reenter", [](lua_State*)
	{
		gm->runScriptTaskList();
		return 0;
	});
	ok = check(writeVirtualFile("script/common/dispatch-first.lua",
		"assign('DispatchOrder',1); "
		"assign('ActiveBatchSnapshot',test_dispatch_snapshot()); "
		"runparallelscript('dispatch-new.lua'); "
		"assign('NewTaskSnapshot',test_dispatch_snapshot()); "
		"test_dispatch_reenter();") &&
		writeVirtualFile("script/common/dispatch-second.lua",
			"assign('DispatchOrder',getvar('DispatchOrder')*10+2);") &&
		writeVirtualFile("script/common/dispatch-new.lua",
			"assign('DispatchOrder',getvar('DispatchOrder')*10+3);") &&
		writeVirtualFile("script/common/dispatch-delayed.lua",
			"assign('DelayedExecuted',1);"),
		"parallel dispatch probe writes real active, queued, and delayed scripts") && ok;
	ok = check(execute("runparallelscript('dispatch-first.lua'); "
		"runparallelscript('dispatch-delayed.lua',10000); "
		"runparallelscript('dispatch-second.lua');") == LUA_OK &&
		gameManager.scriptTaskList.size() == 3,
		"parallel probe schedules an ordered three-task batch through Lua") && ok;
	gameManager.runScriptTaskList();
	ok = check(gameManager.varList.getInteger("ActiveBatchSnapshot") == 0 &&
		gameManager.varList.getInteger("NewTaskSnapshot") == 1,
		"F03 snapshot excludes the active batch but includes newly enqueued tasks") && ok;
	ok = check(gameManager.varList.getInteger("DispatchOrder") == 132 &&
		gameManager.varList.getInteger("DelayedExecuted") == 0 &&
		gameManager.scriptTaskList.size() == 1 &&
		gameManager.scriptTaskList.front().scriptName == "dispatch-delayed.lua" &&
		gameManager.scriptTaskList.front().remainingMilliseconds == 9999,
		"F03 reentry executes new tasks before the remaining outer batch") && ok;
	return ok;
}

class RecordingDialog final : public Dialog
{
public:
	std::vector<std::pair<std::string, std::string>> entries;

private:
	void onRun() override
	{
		entries.push_back(CoreLifecycleTestAccess::dialogContent(*this));
		logicRunning = false;
	}
};

bool runProductionXiaoxiangPrisonRouteTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	const auto packRoot = assetsRoot / std::filesystem::u8path(u8"潇湘行");
	if (!std::filesystem::exists(packRoot / "game_profile.ini"))
	{
		std::cout << "SKIP: optional production Xiaoxiang prison resources are absent\n";
		return true;
	}
	const bool previousLoadAsync = Config::loadAsync;
	bool ok = true;
	for (bool asynchronous : { false, true })
	{
		Config::loadAsync = asynchronous;
		for (int branch = 0; branch < 4; ++branch)
		{
			// Separate roots also isolate each branch's save namespace without holding a path lock across worker reads.
			ScopedActiveResourceRoot resourceRoot;
			if (!check(resourceRoot.valid(), "Xiaoxiang prison routes isolate resource and save writes"))
			{
				ok = false;
				continue;
			}
			File::setResourceFallbackRoots({ packRoot.u8string(), (assetsRoot / "jxqy2").u8string(),
				(assetsRoot / "yycs").u8string() });
			GameManager gameManager;
			gameManager.varList.ensureInitialized();
			gameManager.varList.setInteger("fcsz", branch == 0 ? 0 : 1);
			gameManager.varList.setInteger("ylpl", branch == 2 ? 1 : 0);
			gameManager.varList.setInteger("DaLaoChuKouFirst", 77);
			auto dialog = std::make_shared<RecordingDialog>();
			gameManager.menu->dialog = dialog;
			// Keep map, actor, object, trap, dialogue and save operations real; skip audiovisual timing and walking.
			for (const char* command : { "fadeout", "fadein", "sleep", "playmusic", "displaymessage", "playergotodir", "npcgoto" })
			{
				CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, command, [](lua_State*) { return 0; });
			}
			const std::string startingMap = branch == 3 ? u8"临安大牢第1层.map" : u8"临安城.map";
			if (!check(gameManager.traps.loadInitialTemplate()
				&& gameManager.scriptAPI.loadMap(startingMap, false), "load actual starting map and untouched initial traps"))
			{
				ok = false;
				continue;
			}
			if (branch == 3)
			{
				ok = check(gameManager.npcManager->load("ladl.npc"), "load the actual Shao Qifeng death binding") && ok;
				const auto victims = gameManager.npcManager->findNPC(u8"邵骑风");
				if (!check(!victims.empty() && victims.front()->deathScript == u8"邵骑风死亡.txt",
					"actual prison NPC binds the rescue script"))
				{
					ok = false;
					continue;
				}
				gameManager.runNPCDeathScript(victims.front(), victims.front()->deathScript, gameManager.mapFolderName);
				ok = check(gameManager.mapFolderName == u8"临安城" && gameManager.varList.getInteger("lacsj") == 1
					&& gameManager.traps.get(u8"临安城", 9) == "trap9.txt",
					"actual rescue returns to Linan and enables the intended home story") && ok;
				const std::string expectedDeparture = u8"赵无双：……好，秋姐姐，我们一同回去吧。";
				ok = check(std::any_of(dialog->entries.begin(), dialog->entries.end(), [&](const auto& entry)
					{ return entry.first.find(expectedDeparture) != std::string::npos; }),
					"actual rescue dialogue leaves Zhao Wushuang alive") && ok;
			}
			else
			{
				ok = check(gameManager.traps.get(u8"临安城", 3) == u8"临安-凤池山庄.txt",
					"use the real Linan entrance trap, not an injected legacy-exit binding") && ok;
				gameManager.runTrapScript(3);
				ok = check(gameManager.mapFolderName == (branch == 0 ? u8"临安城" : u8"临安-凤池山庄"),
					"real prerequisite and two later entrance branches select the intended map") && ok;
			}
			ok = check(gameManager.traps.get(u8"临安-凤池山庄", 3).empty()
				&& gameManager.varList.getInteger("DaLaoChuKouFirst") == 77,
				"normal entrances and rescue never enable the inherited graveyard entrance") && ok;
			if (branch == 1 || branch == 2)
			{
				const auto objects = gameManager.objectManager->objectList;
				int trapTiles = 0;
				for (int y = 0; y < static_cast<int>(gameManager.map->data->tile.size()); ++y)
				{
					for (int x = 0; x < static_cast<int>(gameManager.map->data->tile[y].size()); ++x)
					{
						if (gameManager.map->data->tile[y][x].trap != 3)
						{
							continue;
						}
						++trapTiles;
						gameManager.player->setPosition({ x, y });
						gameManager.runTrapScript(3);
						ok = check(gameManager.mapFolderName == u8"临安-凤池山庄"
							&& gameManager.objectManager->objectList == objects && !gameManager.inEvent,
							"every actual unbound trap-3 tile leaves the current map and objects intact") && ok;
					}
				}
				ok = check(trapTiles == 6, "the actual Fengchi approach map contains six inactive trap-3 tiles") && ok;
			}
			ok = check(gameManager.traps.save(), "save the real route trap state") && ok;
			gameManager.traps.set(u8"临安-凤池山庄", 3, u8"trap3至大牢出口.txt");
			gameManager.traps.freeResource();
			ok = check(gameManager.traps.load() && gameManager.traps.get(u8"临安-凤池山庄", 3).empty(),
				"file reload preserves the absent graveyard binding after discarding an injected in-memory value") && ok;
			std::cout << "Xiaoxiang prison route checked: async=" << asynchronous << " branch=" << branch << std::endl;
		}
	}
	Config::loadAsync = previousLoadAsync;
	return ok;
}

bool runProductionXiaoxiangMissingObjectRouteTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	const auto packRoot = assetsRoot / std::filesystem::u8path(u8"潇湘行");
	if (!std::filesystem::exists(packRoot / "game_profile.ini"))
	{
		std::cout << "SKIP: optional production Xiaoxiang object-route resources are absent\n";
		return true;
	}
	const bool previousLoadAsync = Config::loadAsync;
	bool ok = true;
	for (bool asynchronous : { false, true })
	{
		Config::loadAsync = asynchronous;
		for (int branch = 0; branch < 3; ++branch)
		{
			ScopedActiveResourceRoot resourceRoot;
			if (!check(resourceRoot.valid(), "missing-object routes isolate all save writes"))
			{
				ok = false;
				continue;
			}
			File::setResourceFallbackRoots({ packRoot.u8string(), (assetsRoot / "jxqy2").u8string(),
				(assetsRoot / "yycs").u8string() });
			GameManager gameManager;
			ResourceManifest manifest;
			ok = check(manifest.loadFromFile("game_profile.ini"), "load actual Xiaoxiang route feature profile") && ok;
			gameManager.global.applyResourceManifestFeatures(manifest);
			gameManager.varList.ensureInitialized();
			gameManager.varList.setInteger("fcsz", 4);
			gameManager.varList.setInteger("event", 2);
			gameManager.varList.setInteger("fcszdrsw", 0);
			gameManager.varList.setInteger("laly", 1);
			gameManager.varList.setInteger("bwcly", branch == 2 ? 1 : 0);
			gameManager.varList.setInteger("zdtj", 1);
			gameManager.varList.setInteger("DaLaoChuKouFirst", 77);
			auto dialog = std::make_shared<RecordingDialog>();
			gameManager.menu->dialog = dialog;
			// Keep actual loads, bindings, dialogue, rewards and variables; omit presentation waits and walking.
			for (const char* command : { "fadeout", "fadein", "sleep", "playmusic", "displaymessage", "npcgoto" })
			{
				CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, command, [](lua_State*) { return 0; });
			}
			const bool fengchi = branch == 0;
			if (!check(gameManager.traps.loadInitialTemplate()
				&& gameManager.scriptAPI.loadMap(fengchi ? u8"临安-凤池山庄.map" : u8"临安城.map", false)
				&& gameManager.scriptAPI.loadNPC(fengchi ? "lafcsz.npc" : "la.npc")
				&& gameManager.scriptAPI.loadObject(fengchi ? "lafcsz.obj" : "la.obj")
				&& !gameManager.objectManager->objectList.empty(),
				"start each route with its actual nonempty NPC and object tables"))
			{
				ok = false;
				continue;
			}
			if (fengchi)
			{
				ok = check(gameManager.traps.get(u8"临安-凤池山庄", 2) == u8"trap2至凤池.txt",
					"Fengchi transition uses the formal trap binding") && ok;
				gameManager.runTrapScript(2);
			}
			else
			{
				const auto actors = gameManager.npcManager->findNPC(u8"陆游");
				if (!check(!actors.empty() && actors.front()->scriptFile == u8"陆游对话.txt"
					&& actors.front()->isVisibleByVariable, "Lu You's actual visible NPC owns the sparring entry"))
				{
					ok = false;
					continue;
				}
				gameManager.runNPCScript(actors.front(), "", false);
			}
			const auto checkMissingObjects = [&](const char* fileName)
			{
				gameManager.scriptAPI.saveObject();
				std::unique_ptr<char[]> bytes;
				int length = 0;
				return check(gameManager.objectManager->objectList.empty() && gameManager.global.data.objName.empty()
					&& !SaveFileManager::ReadNpcObjFile(fileName, bytes, length),
					"missing destination objects neither retain old objects nor create a phantom table on SaveObj");
			};
			ok = check(gameManager.mapFolderName == (fengchi ? u8"凤池山庄" : u8"比武场")
				&& gameManager.player->getPosition() == (fengchi ? Point{ 54, 248 } : Point{ 18, 56 })
				&& !gameManager.inEvent, "actual entry script continues past missing LoadObj to its intended destination") && ok;
			ok = checkMissingObjects(fengchi ? "fcsz-4.obj" : u8"比武场.obj") && ok;
			const auto beforeReload = gameManager.npcManager->npcList;
			const std::string savedNpcName = gameManager.global.data.npcName;
			gameManager.scriptAPI.saveNPC();
			ok = check(!savedNpcName.empty() && !readVirtualFile(SaveFileManager::CurrentPath() + savedNpcName).empty(),
				"write actual destination NPCs to the isolated current-save directory") && ok;
			gameManager.npcManager->freeResource();
			if (asynchronous) gameManager.scriptAPI.loadNPCAsync(savedNpcName);
			else ok = check(gameManager.scriptAPI.loadNPC(savedNpcName), "reload the destination NPC file") && ok;
			ok = check(!gameManager.npcManager->npcList.empty()
				&& std::none_of(gameManager.npcManager->npcList.begin(), gameManager.npcManager->npcList.end(),
					[&](const auto& actor) { return std::find(beforeReload.begin(), beforeReload.end(), actor) != beforeReload.end(); }),
				"file readback reconstructs destination NPCs instead of retaining live objects") && ok;
			std::vector<std::shared_ptr<NPC>> defeated;
			const std::string expectedDeath = fengchi ? u8"敌人死亡.txt"
				: (branch == 1 ? u8"陆游普攻死亡.txt" : u8"陆游武功死亡.txt");
			for (const auto& actor : gameManager.npcManager->npcList)
			{
				if (actor && actor->deathScript == expectedDeath) defeated.push_back(actor);
			}
			if (!check(defeated.size() == (fengchi ? 2u : 1u), "saved NPCs retain the exact formal progression death bindings"))
			{
				ok = false;
				continue;
			}
			// Dispatch the actual bound outcomes; combat and death-animation execution are not asserted here.
			for (const auto& actor : defeated)
			{
				gameManager.runNPCDeathScript(actor, actor->deathScript, gameManager.mapFolderName);
			}
			if (fengchi)
			{
				ok = check(gameManager.mapFolderName == u8"临安大牢第1层"
					&& gameManager.varList.getInteger("fcszdrsw") == 2, "two actual enemy outcomes enter the surviving prison branch") && ok;
				ok = checkMissingObjects("ladl.obj") && ok;
				const auto guards = gameManager.npcManager->findNPC(u8"邵骑风");
				if (!check(!guards.empty() && guards.front()->deathScript == u8"邵骑风死亡.txt",
					"the missing prison object table does not remove the rescue NPC binding"))
				{
					ok = false;
					continue;
				}
				gameManager.runNPCDeathScript(guards.front(), guards.front()->deathScript, gameManager.mapFolderName);
				ok = check(gameManager.varList.getInteger("lacsj") == 1
					&& gameManager.traps.get(u8"临安城", 9) == "trap9.txt", "prison rescue continues to the intended Linan event") && ok;
			}
			else
			{
				ok = check(gameManager.varList.getInteger("bwcly") == branch,
					"each sparring outcome advances the actual Lu You quest stage") && ok;
				if (branch == 2)
				{
					const auto* learned = gameManager.magicManager.findMagic(u8"001小楼一夜听春雨.ini");
					ok = check(learned && learned->level == 9, "second sparring grants its real level-nine skill reward") && ok;
				}
			}
			ok = check(gameManager.mapFolderName == u8"临安城" && !gameManager.objectManager->objectList.empty()
				&& gameManager.global.data.objName == "la.obj" && !gameManager.inEvent
				&& gameManager.varList.getInteger("DaLaoChuKouFirst") == 77
				&& gameManager.traps.get(u8"临安-凤池山庄", 3).empty(),
				"completed routes restore Linan objects without enabling the inherited graveyard story") && ok;
			std::cout << "Xiaoxiang missing-object route checked: async=" << asynchronous << " branch=" << branch
				<< " map=" << gameManager.mapFolderName << " objects=" << gameManager.objectManager->objectList.size()
				<< " dialogues=" << dialog->entries.size() << std::endl;
		}
	}
	Config::loadAsync = previousLoadAsync;
	return ok;
}

bool runProductionXiaoxiangTournamentRouteTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	const auto packRoot = assetsRoot / std::filesystem::u8path(u8"潇湘行");
	if (!std::filesystem::exists(packRoot / "game_profile.ini"))
	{
		std::cout << "SKIP: optional production Xiaoxiang tournament resources are absent\n";
		return true;
	}
	const bool previousLoadAsync = Config::loadAsync;
	bool ok = true;
	for (bool asynchronous : { false, true })
	{
		Config::loadAsync = asynchronous;
		ScopedActiveResourceRoot resourceRoot;
		if (!check(resourceRoot.valid(), "tournament route isolates resource and save writes"))
		{
			ok = false;
			continue;
		}
		File::setResourceFallbackRoots({ packRoot.u8string(), (assetsRoot / "jxqy2").u8string(),
			(assetsRoot / "yycs").u8string() });
		GameManager gameManager;
		ResourceManifest manifest;
		ok = check(manifest.loadFromFile("game_profile.ini"), "load actual tournament feature profile") && ok;
		gameManager.global.applyResourceManifestFeatures(manifest);
		gameManager.varList.ensureInitialized();
		gameManager.varList.setInteger("fcsz", 3);
		gameManager.varList.setInteger("dxcml", 1);
		gameManager.varList.setInteger("DaLaoChuKouFirst", 77);
		auto dialog = std::make_shared<RecordingDialog>();
		gameManager.menu->dialog = dialog;
		// Retain camera commands, loads and event dispatch; skip presentation delays and scripted walking.
		for (const char* command : { "fadeout", "fadein", "sleep", "playmusic", "npcgoto" })
		{
			CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, command, [](lua_State*) { return 0; });
		}
		CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, "displaymessage", [](lua_State*)
		{
			if (gm->global.data.npcName == "fcsz-3.npc")
			{
				const auto messengers = gm->npcManager->findNPC(u8"门人");
				const bool validManor = gm->mapFolderName == u8"凤池山庄"
					&& gm->objectManager->objectList.empty() && gm->global.data.objName.empty()
					&& gm->player->getPosition() == Point{ 114, 122 }
					&& !messengers.empty() && messengers.front()->isVisibleByVariable;
				gm->varList.setInteger("ObservedTournamentManor", validManor ? 1 : -1);
			}
			return 0;
		});
		CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, "fadein", [](lua_State*)
		{
			if (gm->global.data.npcName == "fcsz-2.npc" && gm->mapFolderName == u8"凤池山庄"
				&& gm->npcManager->findNPC(u8"秋").empty() && gm->npcManager->findNPC(u8"唐").empty()
				&& gm->npcManager->findNPC(u8"赵").empty())
			{
				gm->varList.setInteger("ObservedTournamentDeparture", 1);
			}
			return 0;
		});
		if (!check(gameManager.traps.loadInitialTemplate()
			&& gameManager.scriptAPI.loadMap(u8"临安-凤池山庄.map", false)
			&& gameManager.scriptAPI.loadNPC("lafcsz.npc")
			&& gameManager.scriptAPI.loadObject("lafcsz.obj")
			&& !gameManager.objectManager->objectList.empty()
			&& gameManager.traps.get(u8"临安-凤池山庄", 2) == u8"trap2至凤池.txt",
			"start tournament through the actual approach map, nonempty objects and entrance trap"))
		{
			ok = false;
			continue;
		}
		gameManager.runTrapScript(2);
		const auto entryActors = gameManager.npcManager->findNPC(u8"赵");
		if (!check(gameManager.mapFolderName == u8"凤池山庄" && gameManager.global.data.npcName == "fcsz-2.npc"
			&& gameManager.global.data.objName == "fcsz.obj" && !gameManager.objectManager->objectList.empty()
			&& entryActors.size() == 1 && entryActors.front()->scriptFile == u8"赵无双对话.txt"
			&& entryActors.front()->isVisibleByVariable, "the stage-three approach selects Zhao's actual tournament entry"))
		{
			ok = false;
			continue;
		}
		gameManager.runNPCScript(entryActors.front(), "", false);
		ok = check(gameManager.varList.getInteger("ObservedTournamentDeparture") == 1,
			"the real short-name departure removes all three actors before the scene changes") && ok;
		ok = check(gameManager.mapFolderName == u8"凤池山庄-比武场"
			&& gameManager.player->getPosition() == Point{ 16, 79 }
			&& gameManager.objectManager->objectList.empty() && gameManager.global.data.objName.empty(),
			"missing tournament objects do not retain the manor's nonempty object list") && ok;
		ok = check(std::any_of(dialog->entries.begin(), dialog->entries.end(), [](const auto& entry)
			{ return entry.first == u8"柴嵩：无双，我送你去你爹爹那吧。"; }),
			"the current entry retains its reviewed dialogue repair absent from raw conversion") && ok;

		struct TournamentStage
		{
			const char* npcFile;
			const char* defeatedName;
			const char* deathScript;
			bool playerFight;
		};
		const TournamentStage stages[] = {
			{ "bwcz.npc", u8"赵无双", u8"赵无双败.txt", false },
			{ "bwcq.npc", u8"秋依水", u8"秋依水败.txt", false },
			{ "bwct.npc", u8"唐影", u8"唐影败.txt", false },
			{ "bwcm.npc", u8"孟廷威", u8"孟廷威败.txt", false },
			{ "bwcf.npc", u8"飞云", u8"飞云败.txt", true },
			{ "bwcd.npc", u8"飞云", u8"飞云死亡.txt", false }
		};
		int completedStages = 0;
		for (const auto& stage : stages)
		{
			const auto actors = gameManager.npcManager->findNPC(stage.defeatedName);
			if (!check(gameManager.global.data.npcName == stage.npcFile && actors.size() == 1
				&& actors.front()->deathScript == stage.deathScript && actors.front()->isVisibleByVariable
				&& actors.front()->life > 0 && actors.front()->kind == nkBattle
				&& gameManager.global.data.saveDisabled && gameManager.global.data.canInput == stage.playerFight
				&& !gameManager.inEvent && gameManager.eventList.empty(),
				"each actual tournament table supplies its live bound opponent and correct input/save state"))
			{
				ok = false;
				break;
			}
			const auto actor = actors.front();
			// Exercise life exhaustion and the normal NPC event queue, not a direct call to a chosen script.
			actor->hurtLife(actor->life + actor->defend);
			ok = check(actor->isDying() && (actor->result & erRunDeathScript),
				"the actual tournament opponent enters death and requests its bound outcome") && ok;
			gameManager.npcManager->onUpdate();
			if (!check(gameManager.eventList.size() == 1 && gameManager.eventList.front().npc == actor
				&& gameManager.eventList.front().scriptName == stage.deathScript && actor->deathScript.empty(),
				"NPC manager queues exactly the real outcome binding once"))
			{
				ok = false;
				break;
			}
			gameManager.runEventList();
			++completedStages;
			std::cout << "Xiaoxiang tournament stage checked: async=" << asynchronous
				<< " stage=" << completedStages << " from=" << stage.npcFile
				<< " to=" << gameManager.global.data.npcName << std::endl;
		}
		ok = check(completedStages == 6 && gameManager.mapFolderName == u8"临安城"
			&& gameManager.global.data.npcName == "la.npc" && gameManager.global.data.objName == "la.obj"
			&& !gameManager.objectManager->objectList.empty() && gameManager.player->getPosition() == Point{ 153, 309 }
			&& gameManager.global.data.canInput && !gameManager.global.data.saveDisabled
			&& !gameManager.inEvent && gameManager.eventList.empty(),
			"all six bound death outcomes return to Linan with control and saving restored") && ok;
		ok = check(gameManager.varList.getInteger("ObservedTournamentManor") == 1
			&& gameManager.varList.getInteger("dxcml") == 2
			&& gameManager.varList.getInteger("DaLaoChuKouFirst") == 77
			&& gameManager.traps.get(u8"临安-凤池山庄", 3).empty(),
			"the missing return-manor objects retain the messenger and advance only the intended story") && ok;
		ok = check(std::any_of(dialog->entries.begin(), dialog->entries.end(), [](const auto& entry)
			{ return entry.first == u8"漱心堂弟子：长老有信给门主。"; })
			&& std::any_of(gameManager.memo.memo.begin(), gameManager.memo.memo.end(), [](const auto& entry)
			{ return entry.find(u8"前往稻香村密林。") != std::string::npos; }),
			"the actual messenger dialogue and next destination memo both execute") && ok;
		for (const char* missingFile : { u8"比武场.obj", "fcsz-3.obj" })
		{
			std::unique_ptr<char[]> bytes;
			int length = 0;
			ok = check(!SaveFileManager::ReadNpcObjFile(missingFile, bytes, length),
				"the script's SaveObj calls do not publish phantom missing tables") && ok;
		}
		ok = check(!readVirtualFile(SaveFileManager::CurrentPath() + "fcsz-3.npc").empty(),
			"the return-manor NPC state is saved even though its object file is absent") && ok;
		std::cout << "Xiaoxiang tournament route checked: async=" << asynchronous
			<< " stages=" << completedStages << " dialogues=" << dialog->entries.size()
			<< " objects=" << gameManager.objectManager->objectList.size() << std::endl;
	}
	Config::loadAsync = previousLoadAsync;
	return ok;
}

class RecordingChooseMenu final : public ChooseMenu
{
public:
	int requestedSelection = 0;
	std::vector<std::vector<std::string>> entries;

private:
	void onRun() override
	{
		entries.push_back(CoreLifecycleTestAccess::selectChoice(*this, requestedSelection));
	}
};

bool runProductionXiaoxiangCompanionHistoryTests(int requestedLoadMode)
{
	class BattleObservingPlayer final : public Player
	{
	public:
		int guardCollisions = 0;
		void hurt(std::shared_ptr<Effect> effect) override
		{
			const auto caster = effect ? std::dynamic_pointer_cast<NPC>(effect->user.lock()) : nullptr;
			if (caster && caster->npcName == u8"家丁") ++guardCollisions;
			Player::hurt(effect);
		}
	};
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	const auto packRoot = assetsRoot / std::filesystem::u8path(u8"潇湘行");
	if (!std::filesystem::exists(packRoot / "game_profile.ini"))
	{
		std::cout << "SKIP: optional production Xiaoxiang companion-history resources are absent\n";
		return true;
	}
	for (bool asynchronous : { false, true })
	{
		if (requestedLoadMode >= 0 && requestedLoadMode != static_cast<int>(asynchronous)) continue;
		ScopedActiveResourceRoot resourceRoot;
		if (!check(resourceRoot.valid(), "companion history isolates working files and complete save slots")) return false;
		File::setResourceFallbackRoots({ packRoot.u8string(), (assetsRoot / "jxqy2").u8string(), (assetsRoot / "yycs").u8string() });
		GameManager game;
		auto battlePlayer = std::make_shared<BattleObservingPlayer>();
		game.player = battlePlayer;
		ResourceManifest manifest;
		if (!check(manifest.loadFromFile("game_profile.ini"), "companion history uses the actual Xiaoxiang profile")) return false;
		game.global.applyResourceManifestFeatures(manifest);
		game.varList.ensureInitialized();
		auto dialog = std::make_shared<RecordingDialog>();
		game.menu->dialog = dialog;
		auto choices = std::make_shared<RecordingChooseMenu>();
		game.menu->chooseMenu = choices;
		// Keep real bindings and world changes; presentation and scripted walking are not this check's acceptance layer.
		for (const char* command : { "fadeout", "fadein", "sleep", "playmusic", "playmovie", "displaymessage", "npcgoto", "npcgotoex", "playergoto", "playerrunto", "movescreenex" })
		{
			CoreLifecycleTestAccess::registerScriptProbe(game.script, command, [](lua_State*) { return 0; });
		}
		game.runScript("newgame.txt", "");
		if (!check(game.getLastLoadFailureMessage().empty() && game.mapFolderName == u8"狂沙镇"
			&& choices->entries.size() == 2 && game.varList.getInteger("XuanZhe") == 0
			&& game.varList.getInteger("xz") == 0 && game.npcManager->findNPC(u8"长歌门弟子").empty(),
			"the original new-game entry loads the initial chapter and completes its two real choices")) return false;
		auto runBound = [&](const char* script)
		{
			for (const auto& actor : game.npcManager->npcList)
			{
				if (actor) actor->updateVisibleByVariable();
			}
			game.npcManager->onUpdate();
			const auto& actors = game.npcManager->npcList;
			const auto found = std::find_if(actors.begin(), actors.end(), [&](const auto& actor)
				{ return actor && actor->scriptFile == script && actor->isVisibleByVariable; });
			std::cout << "Xiaoxiang companion history: async=" << asynchronous << " map=" << game.mapFolderName
				<< " script=" << script << std::endl;
			if (!check(found != actors.end(), "the next chapter interaction must have a visible, actually bound NPC")) return false;
			const auto actor = *found;
			game.runNPCScript(actor, "", false);
			return check(!game.inEvent, "the real chapter dialogue returns normally");
		};
		int healingItemsUsed = 0;
		int staminaItemsUsed = 0;
		auto fightBound = [&](const char* deathScript, const std::function<bool()>& complete,
			int maximumBattleTime = 240000, bool navigateCombatMaze = false)
		{
			bool released = false;
			int movementPreflightFallbacks = 0;
			int stationarySkillRequests = 0;
			int learnedSkillRadius = game.player->attackRadius;
			std::weak_ptr<NPC> pursuedTarget;
			if (navigateCombatMaze)
			{
				auto* learned = game.magicManager.findMagic(u8"001春城何处不飞花.ini");
				if (!check(learned && learned->magic && learned->magic->loadSucceeded,
					"maze combat uses the ranged opening's actually learned skill")) return false;
				learnedSkillRadius = std::max(1, learned->magic->level[learned->level].attackRadius);
				const int sourceIndex = static_cast<int>(learned - game.magicManager.magicList.data());
				const int bottomIndex = game.magicManager.bottomIndex(0);
				if (sourceIndex != bottomIndex) game.magicManager.exchange(sourceIndex, bottomIndex);
			}
			for (int elapsed = 0; elapsed < maximumBattleTime && !complete()
				&& game.player->life > 0; elapsed += 20)
			{
				const bool useLearnedSkill = navigateCombatMaze && elapsed % 10000 < 8000;
				const int combatRadius = useLearnedSkill ? learnedSkillRadius : game.player->attackRadius;
				if (game.player->life < game.player->lifeMax * 9 / 10)
				{
					auto* medicine = game.goodsManager.findGoods(u8"goods-yaowu-8-皇家密药.ini");
					if (medicine && game.goodsManager.useItem(static_cast<int>(medicine - game.goodsManager.goodsList.data())))
					{
						++healingItemsUsed;
					}
				}
				if (navigateCombatMaze && game.player->thew < game.player->thewMax / 2)
				{
					auto* medicine = game.goodsManager.findGoods(u8"goods-yaowu-12-百年何首乌.ini");
					if (medicine && game.goodsManager.useItem(static_cast<int>(medicine - game.goodsManager.goodsList.data())))
						++staminaItemsUsed;
				}
				std::shared_ptr<NPC> target;
				int distance = 1000000;
				Point waypoint;
				bool walkWaypoint = false;
				const bool choosingAction = game.player->isStanding() && !game.player->nextAction;
				std::vector<std::shared_ptr<NPC>> candidates;
				for (const auto& actor : game.npcManager->npcList)
				{
					if (!actor) continue;
					actor->updateVisibleByVariable();
					if (!actor->isVisibleByVariable || actor->relation != nrHostile || actor->life <= 0
						|| (!navigateCombatMaze && actor->deathScript != deathScript)) continue;
					candidates.push_back(actor);
				}
				const auto previousTarget = pursuedTarget.lock();
				std::stable_sort(candidates.begin(), candidates.end(), [&](const auto& left, const auto& right)
					{
						if (navigateCombatMaze && previousTarget)
						{
							if (left == previousTarget) return right != previousTarget;
							if (right == previousTarget) return false;
						}
						return Map::calDistance(game.player->getPosition(), left->getPosition())
							< Map::calDistance(game.player->getPosition(), right->getPosition());
					});
				for (const auto& actor : candidates)
				{
					const int nextDistance = Map::calDistance(game.player->getPosition(), actor->getPosition());
					std::deque<Point> path;
					if (navigateCombatMaze && choosingAction && nextDistance > combatRadius)
					{
						path = game.map->getRadiusPath(game.player->getPosition(), actor->getPosition(),
							combatRadius, game.player->getMoveDirectionCount());
						if (path.empty()) continue;
					}
					target = actor;
					distance = nextDistance;
					walkWaypoint = !path.empty();
					if (walkWaypoint) waypoint = path[std::min<std::size_t>(7, path.size() - 1)];
					break;
				}
				if (!target && !candidates.empty())
				{
					// Still issue a normal movement request when the preflight finds no path:
					// Player's movement entry also asks blocking partners to move aside.
					target = candidates.front();
					distance = Map::calDistance(game.player->getPosition(), target->getPosition());
					++movementPreflightFallbacks;
				}
				if (navigateCombatMaze) pursuedTarget = target;
				if (target && game.player->isStanding() && !game.player->nextAction)
				{
					NextAction action;
					action.action = distance <= combatRadius ? acAttack : acWalk;
					action.destKind = ndAttack;
					action.destGE = target;
					action.dest = target->getPosition();
					// Mix the learned spiral skill with normal attacks instead of
					// repeatedly casting one projectile pattern from the same tile.
					if (useLearnedSkill && (action.action == acAttack || !walkWaypoint))
					{
						action.action = acMagic;
						action.actionParam = 0;
						if (distance > combatRadius)
						{
							// A blocked corridor still permits the normal ground-targeted
							// projectile cast; AttackRadius only governs approaching a target.
							action.destKind = ndNone;
							action.destGE.reset();
							++stationarySkillRequests;
						}
					}
					if (walkWaypoint)
					{
						// Choose a nearby corridor waypoint as a player would click it;
						// do not repeatedly target an enemy across the maze wall.
						action.destKind = ndNone;
						action.destGE.reset();
						action.dest = waypoint;
					}
					if (!check(game.player->addNextAction(action), "the history battle accepts normal player targeting")) return false;
				}
				CoreLifecycleTestAccess::advanceActorFrame(*game.player, 20);
				const auto actors = game.npcManager->npcList;
				for (const auto& actor : actors)
				{
					if (actor) CoreLifecycleTestAccess::advanceActorFrame(*actor, 20);
				}
				const auto effects = game.effectManager->effectList;
				for (const auto& effect : effects)
				{
					released = released || effect->user.lock() == game.player;
					CoreLifecycleTestAccess::advanceActorFrame(*effect, 20);
				}
				CoreLifecycleTestAccess::advanceActorFrame(*game.effectManager, 20);
				CoreLifecycleTestAccess::advanceActorFrame(*game.npcManager, 20);
				game.runEventList();
				if (elapsed % 40000 == 0)
				{
					std::cout << "Xiaoxiang history battle: async=" << asynchronous << " deathScript=" << deathScript
						<< " elapsed=" << elapsed
						<< " life=" << game.player->life << " targetLife=" << (target ? target->life : 0)
						<< " distance=" << distance << " position=" << game.player->getPosition().x << "," << game.player->getPosition().y
						<< " action=" << static_cast<int>(game.player->nowAction) << " pending=" << static_cast<bool>(game.player->nextAction)
						<< " thew=" << game.player->thew << "/" << game.player->thewMax << " mana=" << game.player->mana
						<< " effects=" << game.effectManager->effectList.size() << std::endl;
					if (target && game.player->isStanding() && !game.player->nextAction)
					{
						const Point playerPosition = game.player->getPosition();
						std::cout << "Xiaoxiang blocked movement: map=" << game.mapFolderName
							<< " player=" << playerPosition.x << "," << playerPosition.y
							<< " target=" << target->npcName << " at=" << target->getPosition().x
							<< "," << target->getPosition().y
							<< " magic=" << target->flyIni << " secondary=" << target->flyIni2
							<< " path=" << game.map->getRadiusPath(playerPosition, target->getPosition(), combatRadius, game.player->getMoveDirectionCount()).size()
							<< " walkAllowed=" << game.player->canDoAction(acWalk)
							<< " immobilized=" << game.player->immobilized << " petrified=" << game.player->petrified
							<< " frozen=" << game.player->frozen << std::endl;
						for (const auto& partner : game.partnerManager.findPartnersFromNPCManager())
						{
							std::cout << "partner=" << partner->npcName
								<< " at=" << partner->getPosition().x << "," << partner->getPosition().y
								<< " action=" << static_cast<int>(partner->nowAction)
								<< " visible=" << partner->isVisibleForRuntime()
								<< " yielding=" << partner->isPartnerBlockingPlayer
								<< " steps=" << partner->stepList.size();
							for (const Point reserved : partner->getStepPositions())
							{
								std::cout << " reserved=" << reserved.x << "," << reserved.y;
							}
							std::cout << std::endl;
						}
						for (int direction = 0; direction < 8; ++direction)
						{
							const Point tilePosition = Map::getSubPoint(playerPosition, direction);
							if (!game.map->isInMap(tilePosition)) continue;
							const auto& tile = game.map->dataMap.tile[tilePosition.y][tilePosition.x];
							std::cout << "neighbor=" << tilePosition.x << "," << tilePosition.y
								<< " walk=" << game.map->canWalk(tilePosition);
							for (const auto& occupant : tile.npcList)
							{
								std::cout << " npc=" << occupant->npcName << ":" << occupant->kind
									<< " at=" << occupant->getPosition().x << "," << occupant->getPosition().y
									<< " action=" << static_cast<int>(occupant->nowAction);
							}
							for (const auto& occupant : tile.stepNPCList)
							{
								std::cout << " step=" << occupant->npcName << ":" << occupant->kind
									<< " at=" << occupant->getPosition().x << "," << occupant->getPosition().y
									<< " action=" << static_cast<int>(occupant->nowAction);
							}
							std::cout << std::endl;
						}
					}
				}
			}
			std::cout << "Xiaoxiang battle result: async=" << asynchronous << " deathScript=" << deathScript
				<< " completed=" << complete() << " life=" << game.player->life << " healingItems=" << healingItemsUsed
				<< " staminaItems=" << staminaItemsUsed << " movementPreflightFallbacks=" << movementPreflightFallbacks
				<< " stationarySkillRequests=" << stationarySkillRequests << std::endl;
			return check(released && game.player->life > 0 && complete(),
				"actual history combat dispatches the bound outcome without injected damage or plot variables");
		};
		// Use the chosen original level table, not hand-made combat attributes.
		// This proves story progression rather than ordinary first-play difficulty.
		game.scriptAPI.setPlayerLevel(80);
		if (!runBound(u8"客栈老板对话.txt") || !check(game.varList.getInteger("gotmz") == 1
			&& choices->entries.size() == 3, "the original rest choice unlocks Tiemen")) return false;
		game.runTrapScript(3);
		game.runTrapScript(2);
		if (!check(game.mapFolderName == u8"铁门寨" && game.global.data.npcName == "tmz-ylpl.npc",
			"the original town and wilderness exits lead to the first Tiemen wave")) return false;
		game.runTrapScript(4);
		if (!fightBound(u8"耶律辟离死亡.txt", [&] { return game.varList.getInteger("tmzylsw") == 11; })
			|| !fightBound(u8"天忍教弟子死亡.txt", [&] { return game.varList.getInteger("tmztrsw") == 11; })) return false;
		const auto& objects = game.objectManager->objectList;
		const auto leader = std::find_if(objects.begin(), objects.end(), [](const auto& object)
			{ return object && object->scriptFile == u8"山寨头领对话.txt"; });
		if (!check(leader != objects.end(), "both actual Tiemen waves enable the wounded leader's object interaction")) return false;
		game.runObjScript(*leader, "", false);
		if (!check(game.varList.getInteger("tmz") == 1 && game.varList.getInteger("hksz") == 1
			&& game.saveGame(1) && (asynchronous ? game.scriptAPI.loadGameAsync(1) : game.loadGame(1)),
			"the actual leader interaction unlocks pursuit and survives a complete save reload")) return false;
		game.runTrapScript(1);
		game.runTrapScript(1);
		game.runTrapScript(2);
		if (!check(game.mapFolderName == u8"狂沙镇-龙门客栈", "the original Tiemen return starts the six-guard ambush")
			|| !fightBound(u8"杀手死亡.txt", [&] { return game.varList.getInteger("tmz") == 2; })) return false;
		game.runTrapScript(2);
		if (!runBound(u8"老板对话.txt") || !runBound(u8"老板对话.txt")) return false;
		game.runTrapScript(2);
		if (!check(game.mapFolderName == u8"长安" && game.varList.getInteger("yangbo") == 1,
			"the inn meal and original Changan entrance reveal Yang Bo") || !runBound(u8"杨博对话.txt")) return false;
		game.runTrapScript(7);
		game.runTrapScript(1);
		if (!check(game.mapFolderName == u8"无名山庄" && game.global.data.npcName == "wmsz.npc",
			"Yang Bo's actual information unlocks the first Yixin visit")
			|| !fightBound(u8"家丁死亡.txt", [&] { return game.varList.getInteger("wmszjdsw") == 17; })) return false;
		if (!check(game.mapFolderName == u8"别离村-翠烟门" && game.varList.getInteger("cajdlb") == 1,
			"all seventeen real guards lead to the first Yixin reconnaissance outcome")) return false;
		game.runTrapScript(2);
		if (!runBound(u8"酒店老板对话.txt") || !runBound(u8"飞云对话.txt") || !runBound(u8"酒店老板对话.txt")) return false;
		game.runTrapScript(7);
		game.runTrapScript(1);
		if (!check(game.global.data.npcName == "wmsz-1.npc", "the actual tavern conversation selects the second Yixin visit")
			|| !fightBound(u8"家丁死亡2.txt", [&] { return game.varList.getInteger("wmszjdswe") == 10; })
			|| !runBound(u8"庄主对话.txt")
			|| !fightBound(u8"庄主死亡.txt", [&] { return game.varList.getInteger("wmszny") == 1; })) return false;
		game.runTrapScript(3);
		if (!check(game.varList.getInteger("yssze") == 2 && game.varList.getInteger("cajdlb") == 4,
			"the actual inner-court investigation completes the second Yixin visit")) return false;
		game.runTrapScript(2);
		game.runTrapScript(2);
		if (!runBound(u8"酒店老板对话.txt") || !check(game.varList.getInteger("fxsz") == 1
			&& game.saveGame(2) && (asynchronous ? game.scriptAPI.loadGameAsync(2) : game.loadGame(2)),
			"the real tavern follow-up unlocks Fengxue and preserves the whole opening history on reload")) return false;
		game.runTrapScript(3);
		game.runTrapScript(2);
		if (!check(game.mapFolderName == u8"风雪山庄" && game.global.data.npcName == "fxsz.npc",
			"the original Changan and western-outskirts exits reach Fengxue without chapter relocation")) return false;
		game.runTrapScript(3);
		game.runTrapScript(4);
		if (!fightBound(u8"赵无双死亡.txt", [&] { return game.varList.getInteger("fxszzws") == 1; })
			|| !runBound(u8"赵升权对话.txt")
			|| !fightBound(u8"赵升权死亡.txt", [&] { return game.varList.getInteger("fxszzsq") == 1; })
			|| !runBound(u8"赵无双对话.txt")) return false;
		if (!check(game.varList.getInteger("event") == 1 && game.varList.getInteger("gozd") == 1,
			"actual Fengxue duels produce the first ending prerequisite and Zhongdu invitation")) return false;
		if (!check(game.saveGame(1) && (asynchronous ? game.scriptAPI.loadGameAsync(1) : game.loadGame(1))
			&& game.varList.getInteger("event") == 1,
			"the naturally earned first prerequisite survives complete reload")) return false;
		game.runTrapScript(1);
		if (!check(game.mapFolderName == u8"长安西郊", "the completed Fengxue chapter permits its real exit")) return false;
		game.runTrapScript(1);
		game.runTrapScript(7);
		game.runTrapScript(1);
		if (!check(game.mapFolderName == u8"无名山庄" && game.varList.getInteger("yssze") == 2,
			"the completed early history permits the original return through Changan and Yixin")) return false;
		game.runTrapScript(1);
		if (!check(game.mapFolderName == u8"中都" && game.varList.getInteger("gozd") == 2,
			"the original entrance consumes the naturally earned Zhongdu invitation")) return false;
		if (!runBound(u8"玄慈对话.txt")) return false;
		if (!check(game.mapFolderName == u8"中都地下迷宫" && game.varList.getInteger("event") == 2,
			"the actual Xuanci reward produces the second ending prerequisite")) return false;
		if (!runBound(u8"玄慈对话.txt")
			|| !fightBound(u8"欧阳桐死亡.txt", [&] { return game.npcManager->findNPC(u8"玄慈").empty(); })) return false;
		game.runTrapScript(1);
		if (!check(game.mapFolderName == u8"中都" && game.npcManager->findNPC(u8"玄慈").empty()
			&& game.saveGame(2) && (asynchronous ? game.scriptAPI.loadGameAsync(2) : game.loadGame(2))
			&& game.varList.getInteger("event") == 2,
			"the real underground exit restores the saved city and both prerequisites survive reload")) return false;
		if (!runBound(u8"家丁对话.txt") || !runBound(u8"老人对话.txt")) return false;
		if (!check(game.varList.getInteger("zdjd") == 1 && game.varList.getInteger("zdfy") == 1,
			"the initial gatekeeper and rental story reveal Fei Yun without injected chapter variables")) return false;
		if (!runBound(u8"飞云对话.txt") || !runBound(u8"王二李五对话.txt")
			|| !runBound(u8"飞云新对话.txt") || !runBound(u8"客栈老板对话.txt")
			|| !runBound(u8"家丁对话.txt")) return false;
		if (!check(game.traps.get(u8"中都", 8) == u8"院墙.txt",
			"the real inn and gatekeeper dialogues unlock the courtyard trap")) return false;
		game.runTrapScript(8);
		if (!runBound(u8"飞云柴嵩抢人.txt")) return false;
		std::vector<std::shared_ptr<NPC>> guards;
		for (const auto& actor : game.npcManager->npcList)
		{
			if (actor)
			{
				actor->updateVisibleByVariable();
				if (actor->deathScript == u8"家丁死亡.txt") guards.push_back(actor);
			}
		}
		game.npcManager->onUpdate();
		if (!check(guards.size() == 8 && std::all_of(guards.begin(), guards.end(), [](const auto& actor)
			{ return actor->isVisibleByVariable && actor->relation == nrHostile && actor->life > 0; }),
			"the original rescue dialogue activates the eight bound guards")) return false;
		if (!check(game.saveGame(3) && (asynchronous ? game.scriptAPI.loadGameAsync(3) : game.loadGame(3)),
			"reload the activated battle with all eight guards, original obstacles and story bindings")) return false;
		if (!check(game.player == battlePlayer && game.global.data.NPCAI && game.player->canFight,
			"the original chapter and reloaded profile permit actual combat")) return false;
		bool playerReleased = false, guardReleased = false, playerWasHit = false;
		int movementFrames = 0;
		for (int elapsed = 0; elapsed < 240000 && game.mapFolderName == u8"中都" && game.player->life > 0; elapsed += 20)
		{
			std::shared_ptr<NPC> target;
			int distance = 1000000;
			for (const auto& actor : game.npcManager->npcList)
			{
				if (!actor || actor->deathScript != u8"家丁死亡.txt" || actor->life <= 0) continue;
				const int candidateDistance = Map::calDistance(game.player->getPosition(), actor->getPosition());
				if (candidateDistance < distance) { target = actor; distance = candidateDistance; }
			}
			if (target && game.player->isStanding() && !game.player->nextAction)
			{
				NextAction action;
				action.action = distance <= game.player->attackRadius ? acAttack : acWalk;
				action.destKind = ndAttack;
				action.destGE = target;
				action.dest = target->getPosition();
				if (!check(game.player->addNextAction(action), "normal player action queue accepts the battle target")) return false;
			}
			const auto position = game.player->getPosition();
			const int life = game.player->life;
			CoreLifecycleTestAccess::advanceActorFrame(*game.player, 20);
			const auto actors = game.npcManager->npcList;
			for (const auto& actor : actors)
			{
				if (actor) CoreLifecycleTestAccess::advanceActorFrame(*actor, 20);
			}
			const auto effects = game.effectManager->effectList;
			for (const auto& effect : effects)
			{
				const auto caster = std::dynamic_pointer_cast<NPC>(effect->user.lock());
				playerReleased = playerReleased || caster == game.player;
				guardReleased = guardReleased || (caster && caster->npcName == u8"家丁");
				CoreLifecycleTestAccess::advanceActorFrame(*effect, 20);
			}
			CoreLifecycleTestAccess::advanceActorFrame(*game.effectManager, 20);
			CoreLifecycleTestAccess::advanceActorFrame(*game.npcManager, 20);
			playerWasHit = playerWasHit || game.player->life < life;
			movementFrames += game.player->getPosition() != position ? 1 : 0;
			game.runEventList();
			if (elapsed % 20000 == 0)
			{
				std::cout << "Xiaoxiang guard battle: async=" << asynchronous << " elapsed=" << elapsed
					<< " defeated=" << game.varList.getInteger("zdjdsw") << " life=" << game.player->life
					<< " targetLife=" << (target ? target->life : 0) << " distance=" << distance
					<< " movementFrames=" << movementFrames << std::endl;
			}
		}
		std::cout << "Xiaoxiang guard battle result: async=" << asynchronous << " playerReleased=" << playerReleased
			<< " guardReleased=" << guardReleased << " guardCollisions=" << battlePlayer->guardCollisions
			<< " lifeLossObserved=" << playerWasHit << " movementFrames=" << movementFrames << std::endl;
		// High-level defence may absorb damage. Observe the real hurt callback,
		// rather than changing character attributes merely to force life loss.
		if (!check(playerReleased && guardReleased && battlePlayer->guardCollisions > 0 && movementFrames > 0,
			"real queued walking and both sides' attacks produce collisions before the chapter transition")) return false;
		if (!check(game.mapFolderName == u8"临安城" && game.varList.getInteger("zdjdsw") == 8,
			"all eight actual outcomes produce the Linan arrival cutscene and bindings")) return false;
		if (!runBound(u8"若雪对话.txt") || !runBound(u8"飞云对话.txt")) return false;
		if (!check(game.varList.getInteger("lafy") == 1 && game.varList.getInteger("fcsz") == 1,
			"the actual arrival dialogues establish both chapter variables")) return false;
		game.runTrapScript(3);
		if (!check(game.mapFolderName == u8"临安-凤池山庄", "the city exit follows the unlocked chapter route")) return false;
		game.runTrapScript(2);
		if (!runBound(u8"杨长老对话.txt") || !runBound(u8"邵骑风对话.txt")
			|| !runBound(u8"独孤剑对话.txt") || !runBound(u8"史忠良对话.txt")
			|| !runBound(u8"飞云独孤剑消息.txt")) return false;
		const auto companions = game.npcManager->findNPC(u8"飞云");
		if (!check(game.mapFolderName == u8"临安-凤池山庄" && game.varList.getInteger("fcsz") == 2
			&& companions.size() == 1 && companions.front()->kind == nkPartner,
			"the complete first manor visit recruits one Fei Yun through the original script")) return false;
		if (!check(game.saveGame(1), "save the chapter after real recruitment")) return false;
		game.npcManager->freeResource();
		game.varList.setInteger("lafy", -1);
		if (!check(asynchronous ? game.scriptAPI.loadGameAsync(1) : game.loadGame(1),
			"reload the recruited party and its earlier map working copies")) return false;
		const auto reloaded = game.npcManager->findNPC(u8"飞云");
		if (!check(reloaded.size() == 1 && reloaded.front()->kind == nkPartner
			&& game.varList.getInteger("lafy") == 1, "recruitment and arrival state survive complete reload")) return false;
		game.runTrapScript(2);
		if (!runBound(u8"独孤剑对话2.txt")) return false;
		if (!check(game.mapFolderName == u8"临安城" && game.npcManager->findNPC(u8"飞云").empty(),
			"returning to the actual saved Linan list does not duplicate the departed Fei Yun")) return false;
		if (!runBound(u8"若雪飞云不回.txt")) return false;
		if (!check(game.saveGame(2), "save the next chapter after the original departure")) return false;
		game.npcManager->freeResource();
		if (!check((asynchronous ? game.scriptAPI.loadGameAsync(2) : game.loadGame(2))
			&& game.npcManager->findNPC(u8"飞云").empty() && game.varList.getInteger("lafy") == 1,
			"departure reload preserves real map history rather than hiding a duplicate with lafy zero")) return false;
		if (!runBound(u8"家丁对话.txt") || !runBound(u8"赵兴对话.txt")
			|| !runBound(u8"杨堂主对话.txt") || !runBound(u8"新飞云对话.txt")) return false;
		if (!check(game.varList.getInteger("dxc") == 1 && game.varList.getInteger("laly") == 1,
			"the actual Linan investigation dialogues unlock Daoxiang")) return false;
		game.runTrapScript(2);
		if (!check(game.mapFolderName == u8"稻香村" && game.global.data.npcName == "dxc-2.npc",
			"the unlocked city exit loads the actual investigation list")) return false;
		if (!runBound(u8"弟子对话.txt") || !runBound(u8"天忍教弟子对话.txt")) return false;
		const auto disciples = game.npcManager->findNPC(u8"天忍教弟子");
		if (!check(disciples.size() == 1 && disciples.front()->relation == nrHostile
			&& disciples.front()->deathScript == u8"天忍教弟子死亡.txt",
			"the spy introduction activates the real disciple death binding")) return false;
		if (!fightBound(u8"天忍教弟子死亡.txt", [&] { return game.varList.getInteger("dxcss") == 1; })) return false;
		if (!runBound(u8"天忍教杀手对话.txt")) return false;
		if (!check(game.varList.getInteger("dxcml") == 1,
			"the guide dialogue unlocks the forest")) return false;
		if (!check(game.npcManager->findNPC(u8"天忍教引路人").empty()
			&& game.npcManager->findNPC(u8"天忍教杀手").size() == 3,
			"only the guide departs while all three future opponents remain")) return false;
		if (!check(game.saveGame(4) && (asynchronous ? game.scriptAPI.loadGameAsync(4) : game.loadGame(4))
			&& game.npcManager->findNPC(u8"天忍教引路人").empty()
			&& game.npcManager->findNPC(u8"天忍教杀手").size() == 3,
			"complete save reload preserves the guide departure and hidden future opponents")) return false;
		game.runTrapScript(2);
		if (!check(game.mapFolderName == u8"别离村迷宫" && game.global.data.npcName == "blcmg-1.npc",
			"the original forest exit saves the investigated village working copy")) return false;
		if (!runBound(u8"天忍教杀手对话.txt")) return false;
		if (!check(game.mapFolderName == u8"稻香村" && game.varList.getInteger("dxcsse") == 1,
			"the forest reconnaissance returns to the saved village and reveals the three assassins")) return false;
		int assassins = 0;
		for (const auto& actor : game.npcManager->npcList)
		{
			if (!actor) continue;
			actor->updateVisibleByVariable();
			if (actor->isVisibleByVariable && actor->deathScript == u8"天忍教杀手死亡.txt"
				&& actor->scriptFile == u8"杀手对话.txt") ++assassins;
		}
		std::cout << "Xiaoxiang reconnaissance return: async=" << asynchronous
			<< " visibleAssassins=" << assassins << std::endl;
		if (!check(assassins == 3,
			"the three actually bound assassins survive the earlier guide departure and map reload")) return false;
		if (!runBound(u8"杀手对话.txt")) return false;
		if (!check(game.saveGame(5) && (asynchronous ? game.scriptAPI.loadGameAsync(5) : game.loadGame(5)),
			"reload the active three-assassin battle with the complete earlier map history")) return false;
		if (!fightBound(u8"天忍教杀手死亡.txt", [&] { return game.varList.getInteger("dxcsssw") == 3; })) return false;
		if (!check(game.varList.getInteger("dxcmr") == 1,
			"the three actual deaths reveal the sect contact exactly as written")) return false;
		if (!runBound(u8"门人对话.txt")) return false;
		if (!check(game.mapFolderName == u8"临安城" && game.global.data.npcName == "la.npc"
			&& game.varList.getInteger("fcsz") == 3 && game.varList.getInteger("laly") == 0,
			"the actual post-investigation reunion restores Linan and unlocks the tournament")) return false;
		if (!check(game.saveGame(6) && (asynchronous ? game.scriptAPI.loadGameAsync(6) : game.loadGame(6))
			&& game.varList.getInteger("fcsz") == 3 && game.varList.getInteger("dxcsssw") == 3,
			"the completed investigation and tournament invitation survive a full reload")) return false;
		game.runTrapScript(3);
		game.runTrapScript(2);
		if (!check(game.mapFolderName == u8"凤池山庄" && game.global.data.npcName == "fcsz-2.npc",
			"the investigation invitation selects the actual tournament gathering")) return false;
		if (!runBound(u8"赵无双对话.txt")) return false;
		struct BattleStage
		{
			const char* table;
			const char* deathScript;
			bool playerParticipates;
		};
		for (const auto& stage : {
			BattleStage{ "bwcz.npc", u8"赵无双败.txt", false },
			BattleStage{ "bwcq.npc", u8"秋依水败.txt", false },
			BattleStage{ "bwct.npc", u8"唐影败.txt", false },
			BattleStage{ "bwcm.npc", u8"孟廷威败.txt", false },
			BattleStage{ "bwcf.npc", u8"飞云败.txt", true },
			BattleStage{ "bwcd.npc", u8"飞云死亡.txt", false } })
		{
			if (!check(game.global.data.npcName == stage.table && game.global.data.saveDisabled
				&& game.global.data.canInput == stage.playerParticipates,
				"the actual tournament outcome selects the next stage and input rules")) return false;
			bool released = false;
			for (int elapsed = 0; elapsed < 600000 && game.global.data.npcName == stage.table
				&& game.player->life > 0; elapsed += 20)
			{
				if (stage.playerParticipates && game.player->isStanding() && !game.player->nextAction)
				{
					const auto& actors = game.npcManager->npcList;
					const auto found = std::find_if(actors.begin(), actors.end(), [&](const auto& actor)
						{ return actor && actor->deathScript == stage.deathScript && actor->life > 0; });
					if (found != actors.end())
					{
						NextAction action;
						const int distance = Map::calDistance(game.player->getPosition(), (*found)->getPosition());
						action.action = distance <= game.player->attackRadius ? acAttack : acWalk;
						action.destKind = ndAttack;
						action.destGE = *found;
						action.dest = (*found)->getPosition();
						if (!check(game.player->addNextAction(action), "the tournament accepts the participating player's normal action")) return false;
					}
				}
				CoreLifecycleTestAccess::advanceActorFrame(*game.player, 20);
				const auto actors = game.npcManager->npcList;
				for (const auto& actor : actors)
				{
					if (actor) CoreLifecycleTestAccess::advanceActorFrame(*actor, 20);
				}
				const auto effects = game.effectManager->effectList;
				for (const auto& effect : effects)
				{
					released = released || !effect->user.expired();
					CoreLifecycleTestAccess::advanceActorFrame(*effect, 20);
				}
				CoreLifecycleTestAccess::advanceActorFrame(*game.effectManager, 20);
				CoreLifecycleTestAccess::advanceActorFrame(*game.npcManager, 20);
				game.runEventList();
				if (elapsed % 60000 == 0)
				{
					std::cout << "Xiaoxiang actual tournament: async=" << asynchronous << " stage=" << stage.table
						<< " elapsed=" << elapsed << " playerLife=" << game.player->life << " actors=";
					for (const auto& actor : game.npcManager->npcList)
					{
						if (actor && actor->relation != nrNeutral && actor->kind == nkBattle)
							std::cout << actor->npcName << ':' << actor->life << '/' << actor->relation << ' ';
					}
					std::cout << std::endl;
				}
			}
			if (!check(released && game.global.data.npcName != stage.table && game.player->life > 0,
				"actual attacks complete the bound tournament stage without injected death events")) return false;
		}
		if (!check(game.mapFolderName == u8"临安城" && game.global.data.npcName == "la.npc"
			&& game.varList.getInteger("dxcml") == 2 && game.global.data.canInput && !game.global.data.saveDisabled,
			"all six actual tournament battles lead to the messenger and renewed forest investigation")) return false;
		if (!check(game.saveGame(7) && (asynchronous ? game.scriptAPI.loadGameAsync(7) : game.loadGame(7))
			&& game.varList.getInteger("dxcml") == 2,
			"the completed actual tournament and earlier chapter working copies survive full reload")) return false;
		game.runTrapScript(2);
		game.runTrapScript(2);
		if (!check(game.mapFolderName == u8"别离村迷宫" && game.global.data.npcName == "blcmg-3.npc"
			&& game.varList.getInteger("ylpl") == 1 && game.varList.getInteger("dxc") == 2,
			"the actual post-tournament forest confrontation starts the Yelu pursuit")) return false;
		// The pursuit script does not require defeating the forest ambush before
		// leaving. Follow its bound exit without inventing a battle counter.
		game.runTrapScript(1);
		if (!check(game.mapFolderName == u8"稻香村" && game.global.data.npcName == "dxc-3.npc",
			"the pursuit returns through its actual Daoxiang chapter table")) return false;
		game.runTrapScript(1);
		game.runTrapScript(3);
		if (!runBound(u8"耶律辟离对话.txt")
			|| !fightBound(u8"耶律辟离死亡.txt", [&] { return game.varList.getInteger("fcsz") == 4; })) return false;
		const auto pursuitCompanions = game.npcManager->findNPC(u8"飞云");
		if (!check(pursuitCompanions.size() == 1 && pursuitCompanions.front()->kind == nkPartner
			&& game.saveGame(1) && (asynchronous ? game.scriptAPI.loadGameAsync(1) : game.loadGame(1))
			&& game.varList.getInteger("event") == 2,
			"the actual Yelu outcome recruits one companion and retains both earned prerequisites on reload")) return false;
		game.runTrapScript(2);
		if (!check(game.mapFolderName == u8"凤池山庄" && game.global.data.npcName == "fcsz-4.npc",
			"the pursuit enters the real late-manor battle")) return false;
		if (!runBound(u8"门卫对话.txt")
			|| !fightBound(u8"敌人死亡.txt", [&] { return game.varList.getInteger("fcszdrsw") == 2; })) return false;
		if (!check(game.mapFolderName == u8"临安大牢第1层" && game.global.data.npcName == "ladl-3.npc"
			&& game.global.data.NPCAI && game.objectManager->objectList.empty()
			&& game.saveGame(2) && (asynchronous ? game.scriptAPI.loadGameAsync(2) : game.loadGame(2)),
			"both actual manor deaths lead to the successful prison branch and reload its active rescue battle")) return false;
		if (!fightBound(u8"邵骑风死亡.txt", [&] { return game.varList.getInteger("lacsj") == 1; })) return false;
		const auto rescuedCompanions = game.npcManager->findNPC(u8"飞云");
		if (!check(game.mapFolderName == u8"临安城" && game.global.data.npcName == "la.npc"
			&& rescuedCompanions.size() == 1 && rescuedCompanions.front()->kind == nkPartner
			&& game.npcManager->findNPC(u8"秋依水").empty() && game.npcManager->findNPC(u8"赵无双").empty()
			&& game.npcManager->findNPC(u8"唐影").empty() && game.traps.get(u8"临安城", 9) == "trap9.txt",
			"the actual prison victory and departures restore the earlier saved Linan list without duplicate companions")) return false;
		if (!check(game.saveGame(3) && (asynchronous ? game.scriptAPI.loadGameAsync(3) : game.loadGame(3))
			&& game.varList.getInteger("event") == 2 && game.varList.getInteger("lacsj") == 1
			&& game.npcManager->findNPC(u8"飞云").size() == 1,
			"the continuous recruitment, departure, tournament, pursuit and rescue history survives complete reload")) return false;
		std::cout << "Xiaoxiang rescue history passed: async=" << asynchronous << " fullSaves=14" << std::endl;
		// Supply original medicine through the normal inventory/use path for the
		// late combat fixture, without changing damage, health or plot outcomes.
		if (!check(game.goodsManager.addItem(u8"goods-yaowu-8-皇家密药.ini", 5000),
			"the final-chapter fixture loads the original healing item definition")) return false;
		if (!check(game.goodsManager.addItem(u8"goods-yaowu-12-百年何首乌.ini", 5000),
			"the final-chapter fixture loads original stamina medicine for its learned skill")) return false;
		game.runTrapScript(9);
		if (!check(game.varList.getInteger("lawb") == 1 && game.traps.get(u8"临安城", 9).empty(),
			"the actual post-rescue home scene unlocks the governor confrontation")) return false;
		if (!runBound(u8"卫兵对话.txt") || !fightBound("", [&]
			{
				const auto guards = game.npcManager->findNPC(u8"卫兵");
				return std::none_of(guards.begin(), guards.end(), [](const auto& guard) { return guard->life > 0; });
			})) return false;
		if (!runBound(u8"赵节对话.txt")
			|| !fightBound(u8"赵节死亡.txt", [&] { return game.varList.getInteger("ladxmg") == 1; })) return false;
		game.runTrapScript(11);
		if (!check(game.mapFolderName == u8"临安地下迷宫" && game.global.data.npcName == "lamg.npc"
			&& game.saveGame(4) && (asynchronous ? game.scriptAPI.loadGameAsync(4) : game.loadGame(4)),
			"the real Zhao Jie escape opens and reloads the underground confrontation")) return false;
		if (!fightBound(u8"赵节死亡.txt", [&] { return game.varList.getInteger("zdjzfy") == 1; })) return false;
		const auto returnedNamesakes = game.npcManager->findNPC(u8"飞云");
		if (!check(game.mapFolderName == u8"中都"
			&& std::none_of(returnedNamesakes.begin(), returnedNamesakes.end(), [](const auto& actor) { return actor->kind == nkPartner; }),
			"the original underground victory departs the companion and returns to the saved Zhongdu chapter")) return false;
		if (!runBound(u8"飞云决战对话.txt")) return false;
		game.runTrapScript(13);
		if (!runBound(u8"南宫飞云对话.txt") || !check(game.varList.getInteger("trj") == 1,
			"actual tavern and street investigation reveal Tianren before departure")) return false;
		const auto finalCompanions = game.npcManager->findNPC(u8"南宫飞云");
		if (!check(finalCompanions.size() == 1 && finalCompanions.front()->kind == nkPartner
			&& finalCompanions.front()->flyIni == u8"player-magic-长剑.ini",
			"the final-chapter Fei Yun is the real valid-magic template, not an earlier missing-definition namesake")) return false;
		game.runTrapScript(12);
		game.runTrapScript(2);
		game.runTrapScript(2);
		if (!check(game.mapFolderName == u8"天忍教-地下迷宫1" && game.global.data.npcName == "trj-1.npc",
			"the original wilderness and headquarters entries activate the first Tianren wave")
			|| !fightBound(u8"天忍教弟子死亡.txt", [&] { return game.varList.getInteger("trjdzsw") == 27; }, 1200000, true)) return false;
		if (!check(game.global.data.npcName == "trj-1a.npc", "the 27 actual deaths start the original ambush")) return false;
		if (!fightBound("", [&]
			{
				return game.mapFolderName != u8"天忍教-地下迷宫1"
					|| std::none_of(game.npcManager->npcList.begin(), game.npcManager->npcList.end(), [](const auto& actor)
					{ return actor && actor->relation == nrHostile && actor->life > 0; });
			}, 1200000, true)) return false;
		// The ambush says to fight our way out; its original exit is already open
		// and can be crossed by a real battle movement. Do not invent a kill quota.
		if (game.mapFolderName == u8"天忍教-地下迷宫1") game.runTrapScript(2);
		if (!check(game.mapFolderName == u8"天忍教-地下迷宫2" && game.npcManager->findNPC(u8"南宫飞云").empty()
			&& game.traps.get(u8"天忍教-地下迷宫2", 2).empty(),
			"the original second-floor entry departs Fei Yun and locks the last floor")) return false;
		if (!runBound(u8"耶律辟离对话.txt")
			|| !fightBound(u8"耶律辟离死亡.txt", [&] { return game.varList.getInteger("trjylpl") == 13; }, 2400000, true)) return false;
		game.runTrapScript(2);
		if (!check(game.mapFolderName == u8"天忍教-地下迷宫3"
			&& game.saveGame(5) && (asynchronous ? game.scriptAPI.loadGameAsync(5) : game.loadGame(5)),
			"all 13 actual second-floor outcomes open the last floor and preserve its ending object on reload")) return false;
		const auto endingObject = std::find_if(game.objectManager->objectList.begin(), game.objectManager->objectList.end(),
			[](const auto& object) { return object && object->scriptFile == u8"飞云尸体.txt"; });
		if (!check(endingObject != game.objectManager->objectList.end(), "the last floor contains the original bound ending object")) return false;
		const auto body = *endingObject;
		game.result = erNone;
		CoreLifecycleTestAccess::setLogicRunning(game, true);
		game.runObjScript(body, "", false);
		const bool completedStory = std::any_of(dialog->entries.begin(), dialog->entries.end(), [](const auto& entry)
			{ return entry.first.find(u8"全剧终") != std::string::npos; });
		if (!check(completedStory && game.result == erOK && !CoreLifecycleTestAccess::logicRunning(game)
			&& game.map->data == nullptr && game.eventList.empty(),
			"the original ending object completes the epilogue, frees the map and returns to title")) return false;
		std::cout << "Xiaoxiang companion history passed: async=" << asynchronous << " fullSaves=16 healingItems="
			<< healingItemsUsed << " staminaItems=" << staminaItemsUsed << std::endl;
	}
	return true;
}

bool runProductionXiaoxiangPrisonEndingTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	const auto packRoot = assetsRoot / std::filesystem::u8path(u8"潇湘行");
	if (!std::filesystem::exists(packRoot / "game_profile.ini"))
	{
		std::cout << "SKIP: optional production Xiaoxiang prison-ending resources are absent\n";
		return true;
	}
	const bool previousLoadAsync = Config::loadAsync;
	bool ok = true;
	for (bool asynchronous : { false, true })
	{
		Config::loadAsync = asynchronous;
		for (int eventStage : { 0, 1, 2 })
		{
			ScopedActiveResourceRoot resourceRoot;
			if (!check(resourceRoot.valid(), "prison ending isolates all resource and save writes"))
			{
				ok = false;
				continue;
			}
			File::setResourceFallbackRoots({ packRoot.u8string(), (assetsRoot / "jxqy2").u8string(),
				(assetsRoot / "yycs").u8string() });
			GameManager gameManager;
			ResourceManifest manifest;
			ok = check(manifest.loadFromFile("game_profile.ini"), "load actual prison-ending feature profile") && ok;
			gameManager.global.applyResourceManifestFeatures(manifest);
			gameManager.varList.ensureInitialized();
			ok = check(gameManager.player->loadInitialTemplate(0), "load the real prison-route protagonist") && ok;
			gameManager.varList.setInteger("fcsz", 4);
			gameManager.varList.setInteger("event", eventStage);
			gameManager.varList.setInteger("fcszdrsw", 0);
			gameManager.varList.setInteger("DaLaoChuKouFirst", 77);
			auto dialog = std::make_shared<RecordingDialog>();
			gameManager.menu->dialog = dialog;
			for (const char* command : { "fadeout", "sleep", "playmusic", "displaymessage", "npcgoto" })
			{
				CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, command, [](lua_State*) { return 0; });
			}
			CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, "fadein", [](lua_State*)
			{
				const char* prisonLists[] = { "ladl.npc", "ladl-1.npc", "ladl-2.npc", "ladl-3.npc" };
				for (int index = 0; index < 4; ++index)
				{
					if (gm->global.data.npcName == prisonLists[index] && !gm->npcManager->npcList.empty()
						&& gm->mapFolderName == u8"临安大牢第1层" && gm->objectManager->objectList.empty()
						&& gm->global.data.objName.empty())
					{
						gm->varList.setInteger("ObservedPrisonLists",
							gm->varList.getInteger("ObservedPrisonLists") | (1 << index));
					}
				}
				if (gm->global.data.npcName == "lafcsz-5.npc" && gm->mapFolderName == u8"临安-凤池山庄")
				{
					int present = 0;
					int ordinary = 0;
					for (const char* name : { u8"秋依水", u8"赵无双", u8"唐影" })
					{
						const auto actors = gm->npcManager->findNPC(name);
						present += static_cast<int>(actors.size());
						ordinary += static_cast<int>(std::count_if(actors.begin(), actors.end(), [](const auto& actor)
							{ return actor->kind != nkPartner; }));
					}
					const int observed = gm->varList.getInteger("ObservedRescueDeparture");
					gm->varList.setInteger("ObservedRescueDeparture", observed | (present == 3 && ordinary == 3 ? 1 : 0)
						| (present == 0 ? 2 : 0));
				}
				return 0;
			});
			if (!check(gameManager.traps.loadInitialTemplate()
				&& gameManager.scriptAPI.loadMap(u8"临安-凤池山庄.map", false)
				&& gameManager.scriptAPI.loadNPC("lafcsz.npc") && gameManager.scriptAPI.loadObject("lafcsz.obj")
				&& !gameManager.objectManager->objectList.empty()
				&& gameManager.traps.get(u8"临安-凤池山庄", 2) == u8"trap2至凤池.txt",
				"prison ending begins with the real approach tables and entrance trap"))
			{
				ok = false;
				continue;
			}
			gameManager.runTrapScript(2);
			std::vector<std::shared_ptr<NPC>> gatekeepers;
			std::vector<std::shared_ptr<NPC>> opponents;
			for (const auto& actor : gameManager.npcManager->npcList)
			{
				if (!actor) continue;
				if (actor->scriptFile == u8"门卫对话.txt") gatekeepers.push_back(actor);
				if (actor->deathScript == u8"敌人死亡.txt") opponents.push_back(actor);
			}
			if (!check(gameManager.global.data.npcName == "fcsz-4.npc" && gatekeepers.size() == 1
				&& gatekeepers.front()->npcName == u8"家丁" && opponents.size() == 2,
				"the actual late-manor table binds one gatekeeper and two progression opponents"))
			{
				ok = false;
				continue;
			}
			gameManager.runNPCScript(gatekeepers.front(), "", false);
			ok = check(std::all_of(opponents.begin(), opponents.end(), [](const auto& actor)
				{ return actor->relation == nrHostile && actor->life > 0 && actor->isVisibleByVariable; }),
				"the formal gatekeeper dialogue activates both real opponents before any damage") && ok;
			gameManager.result = erNone;
			CoreLifecycleTestAccess::setLogicRunning(gameManager, true);
			int defeatedCount = 0;
			for (const auto& actor : opponents)
			{
				actor->hurtLife(actor->life + actor->defend);
				gameManager.npcManager->onUpdate();
				if (!check(gameManager.eventList.size() == 1 && gameManager.eventList.front().npc == actor
					&& gameManager.eventList.front().scriptName == u8"敌人死亡.txt" && actor->deathScript.empty(),
					"lethal damage queues the formal enemy death binding once"))
				{
					ok = false;
					break;
				}
				gameManager.runEventList();
				++defeatedCount;
				ok = check(gameManager.varList.getInteger("fcszdrsw") == defeatedCount,
					"each enemy outcome increments the actual case-sensitive progression variable once") && ok;
				if (defeatedCount == 1)
				{
					ok = check(gameManager.mapFolderName == u8"凤池山庄" && gameManager.result == erNone
						&& CoreLifecycleTestAccess::logicRunning(gameManager),
						"one defeated enemy cannot prematurely trigger the prison ending") && ok;
				}
			}
			ok = check(defeatedCount == 2 && gameManager.varList.getInteger("ObservedPrisonLists") == 15
				&& gameManager.varList.getInteger("DaLaoChuKouFirst") == 77
				&& gameManager.traps.get(u8"临安-凤池山庄", 3).empty() && !gameManager.inEvent,
				"all four real prison NPC stages load without stale objects or inherited graveyard entry") && ok;
			const bool terminalDialogue = std::any_of(dialog->entries.begin(), dialog->entries.end(), [](const auto& entry)
				{ return entry.first.find(u8"冰冷的长枪刺入腹中") != std::string::npos; });
			if (eventStage <= 1)
			{
				ok = check(terminalDialogue && gameManager.map->data == nullptr && gameManager.result == erOK
					&& !CoreLifecycleTestAccess::logicRunning(gameManager) && gameManager.eventList.empty(),
					"the bad-ending branch completes its actual narration, FreeMap and ReturnToTitle") && ok;
			}
			else
			{
				ok = check(!terminalDialogue && gameManager.map->data != nullptr && gameManager.result == erNone
					&& CoreLifecycleTestAccess::logicRunning(gameManager) && gameManager.global.data.NPCAI,
					"event two remains playable and never takes the bad-ending exit") && ok;
				const auto rescueOpponents = gameManager.npcManager->findNPC(u8"邵骑风");
				if (!check(rescueOpponents.size() == 1 && rescueOpponents.front()->deathScript == u8"邵骑风死亡.txt",
					"the surviving branch retains the actual rescue fight binding"))
				{
					ok = false;
					continue;
				}
				const auto opponent = rescueOpponents.front();
				opponent->hurtLife(opponent->life + opponent->defend);
				gameManager.npcManager->onUpdate();
				ok = check(gameManager.eventList.size() == 1
					&& gameManager.eventList.front().scriptName == u8"邵骑风死亡.txt",
					"rescue outcome also follows the real damage-to-event path") && ok;
				gameManager.runEventList();
				ok = check(gameManager.mapFolderName == u8"临安城" && gameManager.global.data.objName == "la.obj"
					&& gameManager.varList.getInteger("lacsj") == 1
					&& gameManager.traps.get(u8"临安城", 9) == "trap9.txt" && gameManager.result == erNone
					&& CoreLifecycleTestAccess::logicRunning(gameManager),
					"the adjacent surviving branch returns to Linan instead of ending the session") && ok;
				const auto feiYuns = gameManager.npcManager->findNPC(u8"飞云");
				ok = check(gameManager.varList.getInteger("ObservedRescueDeparture") == 3
					&& std::count_if(feiYuns.begin(), feiYuns.end(), [](const auto& actor)
						{ return actor->kind == nkPartner && actor->isVisibleByVariable; }) == 1,
					"the rescue scene removes its three ordinary departures and keeps exactly one visible Fei Yun partner") && ok;
				// la.npc also defines a conditional, non-partner namesake for an earlier scene.
				ok = check(feiYuns.size() == 2 && std::count_if(feiYuns.begin(), feiYuns.end(), [](const auto& actor)
					{ return actor->kind != nkPartner && actor->visibleVariableName == "lafy"
						&& actor->visibleVariableValue == 1 && !actor->isVisibleByVariable; }) == 1,
					"the isolated lafy-zero checkpoint keeps the table's earlier Fei Yun hidden, not removed") && ok;
				std::cout << "Rescue departure mask=" << gameManager.varList.getInteger("ObservedRescueDeparture")
					<< " player=" << gameManager.player->npcName << " targets=";
				for (const auto& actor : gameManager.npcManager->findNPC(u8"飞云"))
				{
					std::cout << actor->kind << ":visible=" << actor->isVisibleByVariable << ",";
				}
				std::cout << std::endl;
			}
			std::unique_ptr<char[]> missingBytes;
			int missingLength = 0;
			ok = check(!SaveFileManager::ReadNpcObjFile("ladl.obj", missingBytes, missingLength),
				"neither prison outcome creates a replacement for the missing object table") && ok;
			std::cout << "Xiaoxiang prison ending checked: async=" << asynchronous << " event=" << eventStage
				<< " terminal=" << terminalDialogue << " dialogues=" << dialog->entries.size() << std::endl;
		}
	}
	Config::loadAsync = previousLoadAsync;
	return ok;
}

bool runProductionXiaoxiangLegacyEntranceTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	const auto packRoot = assetsRoot / std::filesystem::u8path(u8"潇湘行");
	if (!std::filesystem::exists(packRoot / "game_profile.ini"))
	{
		std::cout << "SKIP: optional production Xiaoxiang legacy-entrance resources are absent\n";
		return true;
	}
	const bool previousLoadAsync = Config::loadAsync;
	bool ok = true;
	for (bool asynchronous : { false, true })
	{
		Config::loadAsync = asynchronous;
		for (int branch = 0; branch < 4; ++branch)
		{
			ScopedActiveResourceRoot resourceRoot;
			if (!check(resourceRoot.valid(), "legacy-entrance routes isolate resource and save writes"))
			{
				ok = false;
				continue;
			}
			File::setResourceFallbackRoots({ packRoot.u8string(), (assetsRoot / "jxqy2").u8string(),
				(assetsRoot / "yycs").u8string() });
			GameManager gameManager;
			ResourceManifest manifest;
			ok = check(manifest.loadFromFile("game_profile.ini"), "load actual legacy-entrance feature profile") && ok;
			gameManager.global.applyResourceManifestFeatures(manifest);
			gameManager.varList.ensureInitialized();
			gameManager.varList.setInteger("ChangAnYangQiYe", 1);
			gameManager.varList.setInteger("ChangAnYaYi", 0);
			gameManager.varList.setInteger("dxcml", branch == 0 ? 0 : branch - 1);
			auto dialog = std::make_shared<RecordingDialog>();
			gameManager.menu->dialog = dialog;
			for (const char* command : { "fadeout", "fadein", "sleep", "playmusic", "displaymessage", "npcgoto", "playergoto" })
			{
				CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, command, [](lua_State*) { return 0; });
			}
			const bool changan = branch == 0;
			if (!check(gameManager.traps.loadInitialTemplate()
				&& gameManager.scriptAPI.loadMap(changan ? u8"别离村-翠烟门.map" : u8"稻香村.map", false)
				&& gameManager.scriptAPI.loadNPC(changan ? "blccym.npc" : "dxc.npc")
				&& gameManager.scriptAPI.loadObject(changan ? "blccym.obj" : "dxc.obj")
				&& gameManager.traps.get(changan ? u8"别离村-翠烟门" : u8"稻香村", 2)
					== (changan ? u8"trap2至翠烟门.txt" : u8"稻香村-霹雳堂.txt"),
				"start from actual approach maps, tables and original entrance bindings"))
			{
				ok = false;
				continue;
			}
			gameManager.runTrapScript(2);
			if (changan)
			{
				if (!check(gameManager.mapFolderName == u8"长安" && gameManager.global.data.npcName == "ca.npc"
					&& gameManager.global.data.objName == "ca.obj" && gameManager.map->data != nullptr,
					"the mod's real approach selects its local Changan tables"))
				{
					ok = false;
					continue;
				}
				const auto fighters = gameManager.npcManager->findNPC(u8"高级拳师");
				ok = check(fighters.size() == 1 && fighters.front()->attackOptions.size() == 1
					&& fighters.front()->attackOptions.front().magic
					&& fighters.front()->attackOptions.front().magic->loadSucceeded
					&& fighters.front()->attackOptions.front().magic->flyImage
					&& fighters.front()->attackOptions.front().magic->explodeImage,
					"the real Changan fighter loads the published Nulei spell and both effect animations") && ok;
				const std::pair<const char*, const char*> localDialogs[] = {
					{ u8"衙役1对话.txt", u8"差役甲：知府大人今天不会升堂，别来凑热闹了！" },
					{ u8"衙役2对话.txt", u8"差役乙：已经半个月没有人来打官司了，兄弟们到哪里去打秋风啊？" },
					{ u8"居民3对话.txt", u8"居民：干衙役这行麻烦的就是休息时间没个准，我都快受不了了。" }
				};
				for (const auto& localDialog : localDialogs)
				{
					const auto found = std::find_if(gameManager.npcManager->npcList.begin(),
						gameManager.npcManager->npcList.end(), [&](const auto& actor)
						{ return actor && actor->scriptFile == localDialog.first && actor->isVisibleByVariable; });
					if (!check(found != gameManager.npcManager->npcList.end(), "find each visible NPC by its actual local binding"))
					{
						ok = false;
						continue;
					}
					const auto actor = *found;
					const auto previousDialogs = dialog->entries.size();
					gameManager.runNPCScript(actor, "", false);
					ok = check(dialog->entries.size() == previousDialogs + 1 && dialog->entries.back().first == localDialog.second
						&& gameManager.traps.get(u8"长安", 10).empty()
						&& gameManager.varList.getInteger("ChangAnYaYi") == 0,
						"local guard dialogue does not inherit the JXQY2 prison quest even with its prerequisite set") && ok;
				}
				const auto currentNpcs = gameManager.npcManager->npcList;
				const auto currentObjects = gameManager.objectManager->objectList;
				int trapTiles = 0;
				for (int y = 0; y < static_cast<int>(gameManager.map->data->tile.size()); ++y)
				{
					for (int x = 0; x < static_cast<int>(gameManager.map->data->tile[y].size()); ++x)
					{
						if (gameManager.map->data->tile[y][x].trap != 10) continue;
						++trapTiles;
						gameManager.player->setPosition({ x, y });
						gameManager.runTrapScript(10);
					}
				}
				gameManager.runTrapScript(10);
				ok = check(gameManager.mapFolderName == u8"长安" && gameManager.npcManager->npcList == currentNpcs
					&& gameManager.objectManager->objectList == currentObjects
					&& gameManager.traps.get(u8"长安", 10).empty(),
					"all real trap-ten tiles and an unbound dispatch leave the current mod world intact") && ok;
				std::cout << "Xiaoxiang Changan entrance checked: async=" << asynchronous
					<< " local-dialogues=" << dialog->entries.size() << " trap10-tiles=" << trapTiles << std::endl;
			}
			else
			{
				// The first branch explicitly saves its loaded list under a new current name.
				const char* expectedNpcFiles[] = { "temp_blcml.npc", "blcmg-1.npc", "blcmg-3.npc" };
				ok = check(gameManager.mapFolderName == u8"别离村迷宫"
					&& gameManager.global.data.npcName == expectedNpcFiles[branch - 1]
					&& gameManager.global.data.objName == "blcmg.obj" && gameManager.map->data != nullptr
					&& !gameManager.npcManager->npcList.empty(),
					"all three named Pili entrances load the mod forest, not Moonlight's thunder hall") && ok;
				if (branch == 1)
				{
					ok = check(File::fileExist(SaveFileManager::CurrentPath() + "temp_blcml.npc"),
						"the original SaveNpc command writes the renamed forest list to isolated working state") && ok;
				}
				if (branch == 3)
				{
					std::string questMemo;
					for (const auto& line : gameManager.memo.memo)
					{
						questMemo += line;
					}
					ok = check(gameManager.varList.getInteger("ylpl") == 1 && gameManager.varList.getInteger("dxcml") == 0
						&& gameManager.varList.getInteger("dxc") == 2
						&& questMemo == u8"●速回凤池山庄，追杀耶律辟离。",
						"the third real entrance finishes Yelu's escape and supplies the next quest") && ok;
					std::cout << "Xiaoxiang forest quest checked: ylpl=" << gameManager.varList.getInteger("ylpl")
						<< " dxcml=" << gameManager.varList.getInteger("dxcml")
						<< " dxc=" << gameManager.varList.getInteger("dxc")
						<< " memo=" << questMemo << std::endl;
				}
				std::cout << "Xiaoxiang named Pili entrance checked: async=" << asynchronous
					<< " dxcml=" << branch - 1 << " npc=" << gameManager.global.data.npcName
					<< " dialogues=" << dialog->entries.size() << std::endl;
			}
			ok = check(!gameManager.timeScriptSet && !gameManager.timerStarted && gameManager.timeScriptFileName.empty()
				&& gameManager.npcManager->findNPC(u8"霹雳堂爆炸").empty() && !gameManager.inEvent
				&& gameManager.result != erOK && gameManager.result != erExit,
				"the actual mod entries neither arm the inherited explosion nor request a title/application exit") && ok;
			const auto savedMap = gameManager.global.data.mapName;
			const auto savedNpcs = gameManager.global.data.npcName;
			const auto savedObjects = gameManager.global.data.objName;
			const auto savedPlayerPosition = gameManager.player->getPosition();
			const auto savedForestStage = gameManager.varList.getInteger("dxc");
			if (!check(gameManager.saveGame(1), "save each actual Changan/forest route as a complete game slot")) return false;
			gameManager.varList.setInteger("dxc", -99);
			gameManager.npcManager->freeResource();
			const bool restored = asynchronous ? gameManager.scriptAPI.loadGameAsync(1) : gameManager.loadGame(1);
			ok = check(restored && gameManager.global.data.mapName == savedMap
				&& gameManager.global.data.npcName == savedNpcs && gameManager.global.data.objName == savedObjects
				&& gameManager.player->getPosition() == savedPlayerPosition && !gameManager.npcManager->npcList.empty()
				&& gameManager.varList.getInteger("dxc") == savedForestStage
				&& gameManager.traps.get(u8"长安", 10).empty() && !gameManager.timeScriptSet,
				"complete route reload restores the world and plot state without reviving inherited entrances") && ok;
			if (changan)
			{
				const auto restoredFighters = gameManager.npcManager->findNPC(u8"高级拳师");
				ok = check(restoredFighters.size() == 1 && restoredFighters.front()->attackOptions.size() == 1
					&& restoredFighters.front()->attackOptions.front().magic->loadSucceeded,
					"complete route reload retains a usable Nulei attack definition") && ok;
			}
			std::cout << "Xiaoxiang complete route save: async=" << asynchronous << " branch=" << branch
				<< " passed=" << ok << std::endl;
		}
	}
	Config::loadAsync = previousLoadAsync;
	return ok;
}

struct MoonlightDepartureFrames
{
	GameManager* game = nullptr;
	int frames = 0;
	bool exhausted = false;
	bool luyiStartedAtExpectedPosition = false;
	bool sangqinStartedAtExpectedPosition = false;
};

template<class Actor>
class MoonlightDepartureActor final : public Actor
{
public:
	MoonlightDepartureFrames* scene = nullptr;
	void beginWalk(Point destination) override
	{
		if (destination == Point{ 7, 25 })
		{
			if (this->npcName == u8"路异" && this->getPosition() == Point{ 8, 28 })
				scene->luyiStartedAtExpectedPosition = true;
			if (this->npcName == u8"桑芹" && this->getPosition() == Point{ 8, 30 })
				scene->sangqinStartedAtExpectedPosition = true;
		}
		Actor::beginWalk(destination);
	}
protected:
	void onRun() override
	{
		// Replace only the wall-clock wait; movement, AI and tile occupancy stay real.
		while (this->logicRunning && scene->frames < 2400)
		{
			++scene->frames;
			CoreLifecycleTestAccess::advanceActorFrame(*scene->game->player, 50);
			const auto actors = scene->game->npcManager->npcList;
			for (const auto& actor : actors)
				if (actor) CoreLifecycleTestAccess::advanceActorFrame(*actor, 50);
			CoreLifecycleTestAccess::advanceActorFrame(*scene->game->npcManager, 50);
		}
		if (this->logicRunning)
		{
			if (!scene->exhausted)
			{
				std::cout << "Moonlight departure frame bound: waiting=" << this->npcName
					<< " Event=" << scene->game->varList.getInteger("Event")
					<< " canInput=" << scene->game->global.data.canInput;
				for (const auto& actor : scene->game->npcManager->npcList)
					if (actor && (actor->npcName == u8"路异" || actor->npcName == u8"桑芹"))
						std::cout << " " << actor->npcName << "=" << actor->getPosition().x << ","
							<< actor->getPosition().y << " path=" << actor->stepList.size();
				std::cout << " exitWalkable=" << scene->game->map->canWalk({ 7, 25 }) << std::endl;
			}
			scene->exhausted = true;
			this->result |= erInitError;
			this->logicRunning = false;
		}
	}
};

bool runProductionMoonlightDepartureTests()
{
	const auto packRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets/yycs";
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid(), "Moonlight departure isolates working files")) return false;
	File::setResourceFallbackRoots({ packRoot.u8string() });
	GameManager game;
	MoonlightDepartureFrames scene;
	scene.game = &game;
	auto player = std::make_shared<MoonlightDepartureActor<Player>>();
	player->scene = &scene;
	game.player = player;
	ResourceManifest manifest;
	if (!check(manifest.loadFromFile("game_profile.ini"), "load actual Moonlight departure profile")) return false;
	game.global.applyResourceManifestFeatures(manifest);
	game.global.data.characterIndex = 1;
	game.varList.ensureInitialized();
	game.varList.setInteger("Event", 298);
	game.global.data.canInput = true;
	game.global.data.NPCAI = true;
	game.talkTextList.load();
	game.menu->dialog = std::make_shared<RecordingDialog>();
	for (const char* command : { "sleep", "playsound", "movescreen", "npcspecialaction" })
		CoreLifecycleTestAccess::registerScriptProbe(game.script, command, [](lua_State*) { return 0; });
	if (!check(player->loadInitialTemplate(1)
		&& game.scriptAPI.loadMap(u8"map_059_禁地一层.map", false)
		&& game.scriptAPI.loadNPC("jindi-wuyoujiao.npc")
		&& game.scriptAPI.loadObject("map059_obj.obj"), "load real forbidden-area scene and Nalan player")) return false;
	player->setPosition({ 6, 39 });
	// Keep every NPC from the actual scene, including both roaming mice.
	const auto loadedActors = game.npcManager->npcList;
	std::vector<std::shared_ptr<MoonlightDepartureActor<NPC>>> actors;
	for (const auto& original : loadedActors)
	{
		INIReader snapshot;
		original->saveToIni(&snapshot, "Init");
		auto actor = std::make_shared<MoonlightDepartureActor<NPC>>();
		actor->scene = &scene;
		actor->initFromIni(&snapshot, "Init");
		actors.push_back(actor);
	}
	game.npcManager->clearNPC();
	for (const auto& actor : actors) game.npcManager->addNPC(actor);
	std::unique_ptr<char[]> partnerBytes;
	int partnerLength = 0;
	if (!check(File::readFile("ini/save/seashore-yangyf.npc", partnerBytes, partnerLength),
		"read actual Yang partner introduced by the preceding seaside story")) return false;
	INIReader partnerIni(partnerBytes);
	auto partner = std::make_shared<MoonlightDepartureActor<NPC>>();
	partner->scene = &scene;
	partner->initFromIni(&partnerIni, "NPC000");
	partner->kind = nkPartner;
	partner->setPosition({ 6, 40 });
	game.npcManager->addNPC(partner);
	game.map->createDataMap();
	bool ok = check(actors.size() == 5 && player->npcName == u8"纳兰真"
		&& actors[3]->npcName == u8"桑芹" && actors[4]->npcName == u8"路异"
		&& actors[3]->kind == nkBattle && actors[4]->kind == nkBattle
		&& actors[3]->pathFinder == pfSingle && actors[4]->pathFinder == pfSingle,
		"departure retains the actual character identities and single-step pathfinding");
	game.scriptAPI.runScript(u8"事件30_3.txt");
	ok = check(!scene.exhausted, "real Moonlight departure must complete without the test frame bound") && ok;
	ok = check(scene.luyiStartedAtExpectedPosition && scene.sangqinStartedAtExpectedPosition,
		"real preceding Lua movements put Luyi at 8,28 and Sangqin at 8,30") && ok;
	ok = check(game.varList.getInteger("Event") == 300 && game.global.data.canInput
		&& game.npcManager->findNPC(u8"路异").empty() && game.npcManager->findNPC(u8"桑芹").empty(),
		"departure reaches Event300, restores input and removes both departing actors") && ok;
	bool sangqinBesideExit = false;
	for (int direction = 0; direction < 8; ++direction)
		sangqinBesideExit = sangqinBesideExit || Map::getSubPoint(actors[3]->getPosition(), direction) == Point{ 7, 25 };
	ok = check(actors[4]->getPosition() == Point{ 7, 25 } && sangqinBesideExit
		&& actors[3]->isStanding() && actors[3]->getOffset().is_zero()
		&& player->getPosition() == Point{ 9, 31 }
		&& actors[2]->scriptFile == u8"纳兰潜凛对话.txt" && partner->scriptFile == u8"杨影枫对话.txt",
		"original departure stops Sangqin beside Luyi's occupied exit and installs the next dialogue bindings") && ok;
	ok = check(game.map->canWalk({ 7, 25 }), "departure releases the exit tile occupancy") && ok;
	std::cout << "Moonlight real departure: frames=" << scene.frames << " Event=" << game.varList.getInteger("Event")
		<< " Sangqin=" << actors[3]->getPosition().x << "," << actors[3]->getPosition().y
		<< " bound=" << scene.exhausted << " passed=" << ok << std::endl;
	return ok;
}

bool runProductionMoonlightTrapRouteTests()
{
	const auto packRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets/yycs";
	if (!std::filesystem::exists(packRoot / "game_profile.ini"))
	{
		std::cout << "SKIP: optional production Moonlight route resources are absent\n";
		return true;
	}
	const bool previousLoadAsync = Config::loadAsync;
	bool ok = true;
	for (bool asynchronous : { false, true })
	{
		Config::loadAsync = asynchronous;
		for (bool forbiddenArea : { false, true })
		{
			ScopedActiveResourceRoot resourceRoot;
			if (!check(resourceRoot.valid(), "Moonlight routes isolate all working resource writes"))
			{
				ok = false;
				continue;
			}
			File::setResourceFallbackRoots({ packRoot.u8string() });
			GameManager gameManager;
			ResourceManifest manifest;
			ok = check(manifest.loadFromFile("game_profile.ini"), "load actual Moonlight profile") && ok;
			gameManager.global.applyResourceManifestFeatures(manifest);
			gameManager.varList.ensureInitialized();
			gameManager.varList.setInteger("Event", 296);
			gameManager.talkTextList.load();
			auto dialog = std::make_shared<RecordingDialog>();
			gameManager.menu->dialog = dialog;
			// Keep map/entity/variable/trap execution real; omit only timed presentation.
			for (const char* command : { "fadeout", "fadein", "sleep", "playmusic", "playsound",
				"npcgoto", "npcgotoex", "playergoto", "movescreen", "npcspecialaction" })
			{
				CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, command, [](lua_State*) { return 0; });
			}
			if (!check(gameManager.traps.loadInitialTemplate()
				&& gameManager.scriptAPI.loadMap(u8"map_050_忘忧岛.map", false)
				&& gameManager.scriptAPI.loadNPC("map050.npc")
				&& gameManager.scriptAPI.loadObject("map050_obj.obj"), "load real island approach and entity tables"))
			{
				ok = false;
				continue;
			}
			gameManager.runTrapScript(4);
			ok = check(gameManager.mapFolderName == u8"map_057_连接地图"
				&& gameManager.player->getPosition() == Point{ 8, 47 }, "Event296 island entrance selects the real connecting map") && ok;
			if (forbiddenArea)
			{
				gameManager.runTrapScript(2);
			}
			const std::string expectedMap = forbiddenArea ? u8"map_058_禁地" : u8"map_057_连接地图";
			const int expectedEvent = forbiddenArea ? 298 : 296;
			const auto walkToTrap = [&](int targetIndex)
			{
				const auto map = gameManager.map;
				if (!check(map->data != nullptr, "route retains a loaded map")) return false;
				const int width = map->data->head.width;
				const int height = map->data->head.height;
				const Point start = gameManager.player->getPosition();
				if (!check(map->isInMap(start), "route starts inside the actual map")) return false;
				std::vector<int> previous(width * height, -1);
				std::vector<Point> frontier{ start };
				previous[start.y * width + start.x] = start.y * width + start.x;
				int found = -1;
				for (size_t cursor = 0; cursor < frontier.size() && found < 0; ++cursor)
				{
					const Point from = frontier[cursor];
					for (int direction = 0; direction < 8; ++direction)
					{
						if (!map->canWalkDirectlyTo(from, direction)) continue;
						const Point to = Map::getSubPoint(from, direction);
						const int cell = to.y * width + to.x;
						if (previous[cell] >= 0) continue;
						const int trapIndex = map->getTrapIndex(to);
						// Do not claim a path that must first execute another bound event.
						if (trapIndex != 0 && trapIndex != targetIndex && !gameManager.traps.hasTriggered(trapIndex)
							&& !gameManager.traps.get(gameManager.mapFolderName, trapIndex).empty()) continue;
						previous[cell] = from.y * width + from.x;
						frontier.push_back(to);
						if (trapIndex == targetIndex)
						{
							found = cell;
							break;
						}
					}
				}
				if (!check(found >= 0, "real walking/corner/entity rules reach target without crossing another bound event")) return false;
				std::vector<Point> path;
				for (int cell = found; cell != previous[cell]; cell = previous[cell])
				{
					path.push_back({ cell % width, cell / width });
				}
				std::reverse(path.begin(), path.end());
				std::cout << "Moonlight route async=" << asynchronous << " map=" << gameManager.mapFolderName
					<< " trap=" << targetIndex << " from=" << start.x << ',' << start.y
					<< " to=" << found % width << ',' << found / width << " steps=" << path.size() << std::endl;
				for (Point step : path)
				{
					gameManager.player->setPosition(step, false);
					gameManager.player->checkTrap();
				}
				return true;
			};
			if (forbiddenArea)
			{
				ok = walkToTrap(3) && ok;
				ok = check(gameManager.mapFolderName == expectedMap
					&& gameManager.varList.getInteger("Event") == expectedEvent,
					"the approach's real trap3 is inert in the tracking chapter") && ok;
			}
			const size_t dialogCount = dialog->entries.size();
			if (!walkToTrap(11))
			{
				ok = false;
				continue;
			}
			std::cout << "Moonlight trap11 result map=" << gameManager.mapFolderName
				<< " Event=" << gameManager.varList.getInteger("Event") << " added-dialogues="
				<< dialog->entries.size() - dialogCount << std::endl;
			if (!check(gameManager.mapFolderName == expectedMap
				&& gameManager.varList.getInteger("Event") == expectedEvent && dialog->entries.size() == dialogCount,
				"tracking Nalan must not teleport to the wild forest or execute Awan's letter dialogue"))
			{
				ok = false;
				continue;
			}
			ok = walkToTrap(2) && ok;
			ok = check(gameManager.mapFolderName == (forbiddenArea ? u8"map_059_禁地一层" : u8"map_058_禁地")
				&& gameManager.varList.getInteger("Event") == (forbiddenArea ? 300 : 298),
				"the real next exit still advances Nalan's tracking story, including its nested script") && ok;
		}
	}
	Config::loadAsync = previousLoadAsync;
	return ok;
}

bool runProductionNewSwordBoatRouteTests()
{
	const auto packRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets/xjxqy";
	if (!std::filesystem::exists(packRoot / "game_profile.ini"))
	{
		std::cout << "SKIP: optional production New Sword boat resources are absent\n";
		return true;
	}
	const bool previousLoadAsync = Config::loadAsync;
	bool ok = true;
	for (bool asynchronous : { false, true })
	{
		Config::loadAsync = asynchronous;
		ScopedActiveResourceRoot resourceRoot;
		if (!check(resourceRoot.valid(), "boat route isolates working NPC lists, inventory and variables"))
		{
			ok = false;
			continue;
		}
		File::setResourceFallbackRoots({ packRoot.u8string() });
		GameManager gameManager;
		ResourceManifest manifest;
		ok = check(manifest.loadFromFile("game_profile.ini"), "load actual New Sword profile") && ok;
		gameManager.global.applyResourceManifestFeatures(manifest);
		gameManager.varList.ensureInitialized();
		INIReader playerTemplate("ini\\save\\player0.ini");
		ok = check(playerTemplate.Get("Init", "Name", "") == u8"独孤剑", "load the actual initial player definition") && ok;
		gameManager.player->initFromIni(&playerTemplate, "Init");
		auto dialog = std::make_shared<RecordingDialog>();
		gameManager.menu->dialog = dialog;
		for (const char* command : { "fadeout", "fadein", "sleep", "playmusic", "npcgoto", "npcgotoex",
			"playergoto", "playergotodir" })
		{
			CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, command, [](lua_State*) { return 0; });
		}
		const auto loadVillage = [&]()
		{
			return gameManager.scriptAPI.loadMap(u8"map034_1_海边小渔村.map", false)
				&& gameManager.scriptAPI.loadNPC("map034_1.npc") && gameManager.scriptAPI.loadObject("map034_1.obj");
		};
		const auto walkHouseEvent = [&]()
		{
			const auto map = gameManager.map;
			if (!check(gameManager.mapFolderName == u8"map036_渔夫家" && map->data != nullptr,
				"walk starts inside the real fisherman's house")) return false;
			const Point start = gameManager.player->getPosition();
			for (int y = 0; y < map->data->head.height; ++y)
			{
				for (int x = 0; x < map->data->head.width; ++x)
				{
					const Point target{ x, y };
					if (map->getTrapIndex(target) != 2 || !map->canWalk(target)) continue;
					const auto path = map->findPath(start, target);
					if (path.empty()) continue;
					Point from = start;
					bool validPath = true;
					for (Point step : path)
					{
						const int direction = NPC::getDirection(from, step);
						const int index = map->getTrapIndex(step);
						if (Map::getSubPoint(from, direction) != step || !map->canWalkDirectlyTo(from, direction)
							|| (index != 0 && index != 2 && !gameManager.traps.hasTriggered(index)
								&& !gameManager.traps.get(gameManager.mapFolderName, index).empty()))
						{
							validPath = false;
							break;
						}
						from = step;
					}
					if (!validPath) continue;
					std::cout << "New Sword house walk async=" << asynchronous << " Event="
						<< gameManager.varList.getInteger("Event") << " from=" << start.x << ',' << start.y
						<< " target=" << x << ',' << y << " path-steps=" << path.size() << std::endl;
					for (Point step : path)
					{
						const bool enteringTarget = map->getTrapIndex(step) == 2 && map->haveTraps(step);
						gameManager.player->setPosition(step, false);
						gameManager.player->checkTrap();
						// Event120 cancels its own binding, which also clears the triggered flag.
						if (enteringTarget || gameManager.mapFolderName != u8"map036_渔夫家") return true;
					}
				}
			}
			return check(false, "real house entrance can reach its automatic story trap without bypassing another event");
		};
		gameManager.varList.setInteger("Event", 80);
		if (!check(gameManager.traps.loadInitialTemplate() && loadVillage(), "load real village and initial trap definitions"))
		{
			ok = false;
			continue;
		}
		gameManager.runTrapScript(2);
		auto wang = gameManager.npcManager->findNPC(u8"渔夫老王");
		if (!check(wang.size() == 1 && wang.front()->scriptFile == u8"渔夫老王对话.txt",
			"the initial house really binds the reviewed Wang dialogue"))
		{
			ok = false;
			continue;
		}
		gameManager.runNPCScript(wang.front(), "", false);
		ok = check(gameManager.varList.getInteger("Event") == 90 && gameManager.varList.getInteger("TalkLaow") == 1,
			"Wang's initial refusal advances the real quest") && ok;
		gameManager.runTrapScript(1);
		// Checkpoint after persuading Jia; his separate city quest is not simulated here.
		gameManager.varList.setInteger("Event", 120);
		gameManager.scriptAPI.addNPC(u8"npc075_贾老实.ini", 29, 17, 4);
		gameManager.scriptAPI.setNPCKind(u8"贾老实", 3);
		gameManager.scriptAPI.setNPCRelation(u8"贾老实", 2);
		gameManager.runTrapScript(2);
		ok = walkHouseEvent() && ok;
		ok = check(gameManager.varList.getInteger("Event") == 125 && gameManager.npcManager->findNPC(u8"渔夫老王").empty(),
			"Jia's actual house event moves Wang out before the first departure") && ok;
		gameManager.runTrapScript(1);
		INIReader savedHouse(SaveFileManager::CurrentPath() + "map036.npc");
		ok = check(File::fileExist(SaveFileManager::CurrentPath() + "map036.npc")
			&& savedHouse.GetInteger("Head", "Count", -1) == 1 && savedHouse.Get("NPC000", "Name", "") == u8"贾老实",
			"leaving the house saves Jia but no Wang, rather than reusing its initial Wang template") && ok;
		wang = gameManager.npcManager->findNPC(u8"渔夫老王");
		if (!check(wang.size() == 1, "the Event125 dock table supplies Wang for departure"))
		{
			ok = false;
			continue;
		}
		gameManager.runNPCScript(wang.front(), "", false);
		ok = check(gameManager.mapFolderName == u8"map037_碧霞岛" && gameManager.varList.getInteger("Event") == 130,
			"the dock dialogue really takes the player to Bixia Island") && ok;
		// Checkpoint at the last rescue death callback; the intervening battles are outside this test.
		ok = check(gameManager.scriptAPI.loadMap(u8"map039_碧霞岛山洞秘道.map", false)
			&& gameManager.scriptAPI.loadNPC("map039.npc") && gameManager.scriptAPI.loadObject("map039.obj"),
			"load the actual rescue callback's world") && ok;
		gameManager.scriptAPI.addNPC(u8"npc004_张琳心.ini", 3, 41, 2);
		gameManager.varList.setInteger("map039Enemy", 3);
		gameManager.runScript(u8"杀手死亡.txt");
		ok = check(gameManager.mapFolderName == u8"map034_1_海边小渔村" && gameManager.varList.getInteger("Event") == 180
			&& std::any_of(dialog->entries.begin(), dialog->entries.end(), [](const auto& entry)
				{ return entry.first.find(u8"熟鸡蛋能不能孵出小鸡") != std::string::npos; }),
			"the real rescue callback already resolves the egg story at the dock and writes Event180") && ok;
		gameManager.runTrapScript(2);
		ok = check(gameManager.npcManager->findNPC(u8"渔夫老王").empty(),
			"Event180 revisit reads the saved house and cannot invoke its stale Wang branch") && ok;
		gameManager.runTrapScript(1);
		// Checkpoint after Zhang's funeral; restore that chapter's companion, not a fresh house table.
		gameManager.varList.setInteger("Event", 270);
		gameManager.scriptAPI.setNPCKind(u8"何梅", nkNormal);
		gameManager.scriptAPI.deleteNPC(u8"何梅");
		gameManager.scriptAPI.addNPC(u8"npc004_张琳心.ini", 29, 17, 4);
		gameManager.scriptAPI.setNPCKind(u8"张琳心", 3);
		gameManager.runTrapScript(2);
		auto furong = gameManager.npcManager->findNPC(u8"段芙蓉");
		ok = check(gameManager.global.data.npcName == "map036_event270.npc" && furong.size() == 1
			&& furong.front()->scriptFile.empty() && gameManager.npcManager->findNPC(u8"渔夫老王").empty(),
			"Event270 uses its actual alternate table: Furong has no click script and Wang is absent") && ok;
		const int previousSwords = gameManager.goodsManager.getItemNum(u8"goods057_越女剑.ini");
		ok = walkHouseEvent() && ok;
		ok = check(gameManager.mapFolderName == u8"map044_石塘镇" && gameManager.varList.getInteger("Event") == 280
			&& gameManager.goodsManager.getItemNum(u8"goods057_越女剑.ini") == previousSwords + 1,
			"automatic trap2 and its nested Furong script grant one sword and complete the boat trip") && ok;
		furong = gameManager.npcManager->findNPC(u8"段芙蓉");
		ok = check(furong.size() == 1 && furong.front()->scriptFile == u8"段芙蓉对话.txt",
			"the arrival retains Furong and rebinds her to the destination dialogue") && ok;
		if (!furong.empty()) gameManager.runNPCScript(furong.front(), "", false);
		ok = loadVillage() && ok;
		gameManager.runTrapScript(2);
		ok = check(gameManager.npcManager->findNPC(u8"渔夫老王").empty(), "Event280 revisit does not resurrect the saved-away Wang") && ok;
		ok = walkHouseEvent() && ok;
		ok = check(gameManager.mapFolderName == u8"map036_渔夫家" && gameManager.varList.getInteger("Event") == 280
			&& gameManager.goodsManager.getItemNum(u8"goods057_越女剑.ini") == previousSwords + 1 && !gameManager.inEvent,
			"destination dialogue and a later house visit neither repeat the boat trip nor duplicate its reward") && ok;
		std::cout << "New Sword boat route complete async=" << asynchronous << " Event=" << gameManager.varList.getInteger("Event")
			<< " swords=" << gameManager.goodsManager.getItemNum(u8"goods057_越女剑.ini") << " dialogues=" << dialog->entries.size() << std::endl;
	}
	Config::loadAsync = previousLoadAsync;
	return ok;
}

bool runProductionActionResourceTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	if (!std::filesystem::exists(assetsRoot / "yycs/game_profile.ini")
		|| !std::filesystem::exists(assetsRoot / "xjxqy/game_profile.ini"))
	{
		std::cout << "SKIP: optional production action resources are absent\n";
		return true;
	}
	bool ok = true;
	for (const auto [moonlight, walking] : { std::pair{false, false}, std::pair{false, true},
		std::pair{true, false}, std::pair{true, true} })
	{
		ScopedActiveResourceRoot resourceRoot;
		SaveFileManager::CurrentPathScope currentPath("save/action_resource_contracts");
		if (!check(resourceRoot.valid() && currentPath.valid(), "action-resource checks isolate resource and save writes"))
		{
			return false;
		}
		File::setResourceFallbackRoots({ (assetsRoot / (moonlight ? "yycs" : "xjxqy")).u8string() });
		GameManager gameManager;
		ResourceManifest manifest;
		ok = check(manifest.loadFromFile("game_profile.ini"), "read the real action profile") && ok;
		gameManager.global.applyResourceManifestFeatures(manifest);
		gameManager.global.data.NPCAI = false;
		gameManager.varList.ensureInitialized();
		gameManager.map->data = std::make_shared<MapData>();
		gameManager.map->data->head.width = gameManager.map->data->head.height = 100;
		gameManager.map->data->tile.assign(100, std::vector<MapTile>(100));
		gameManager.map->createDataMap();
		const auto execute = [&](const std::string& source)
		{
			auto bytes = std::make_unique<char[]>(source.size());
			std::copy(source.begin(), source.end(), bytes.get());
			return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
		};
		const auto executeSourceLine = [&](const std::string& path, const std::string& line)
		{
			std::unique_ptr<char[]> bytes;
			const int length = File::readFile(path, bytes);
			return check(length > 0 && std::string(bytes.get(), length).find(line) != std::string::npos,
				"action call is present verbatim in the current production script") && execute(line) == LUA_OK;
		};
		auto actor = std::make_shared<ScriptReplacementActor<NPC>>();
		std::unique_ptr<char[]> actorBytes;
		const std::string actorFile = moonlight ? u8"ini/npc/张仲天.ini" : u8"ini/npc/npc074_假和尚.ini";
		if (!check(File::readFile(actorFile, actorBytes) > 0, "read actual actor template"))
		{
			ok = false;
			continue;
		}
		INIReader actorDefinition(actorBytes);
		actor->initFromIni(&actorDefinition, "Init");
		actor->setPosition({8,8}, false);
		gameManager.npcManager->npcList.push_back(actor);
		gameManager.map->createDataMap();
		actor->actionManager->restartActionIgnoringTransitions(acStand);
		if (walking)
		{
			actor->goToEx({8,18});
			ok = check(actor->isWalking() && actor->haveAsyncDest, "real resource-refresh fixture starts walking") && ok;
		}
		CoreLifecycleTestAccess::advanceActorFrame(*actor, 50);
		const auto previousPath = actor->stepList;
		const auto previousPosition = actor->getPosition();
		const auto previousStepBeginTime = actor->stepBeginTime;
		const auto previousRevision = actor->actionManager->getActionRevision();
		const UTime previousAnimationTime = actor->actionLastTime;
		const auto checkResourceRefresh = [&]()
		{
			std::cout << "Resource animation refresh: " << (moonlight ? "YYCS" : "XJXQY")
				<< (walking ? " walk " : " stand ") << previousAnimationTime << " -> "
				<< actor->getActionTime(actor->nowAction) << " cached=" << actor->actionLastTime << std::endl;
			return check(actor->actionLastTime == actor->getActionTime(actor->nowAction)
				&& actor->actionBeginTime == actor->getTime(), "resource replacement resets the current animation clock and duration")
				&& check(actor->stepList == previousPath && actor->getPosition() == previousPosition
					&& actor->stepBeginTime == previousStepBeginTime
					&& actor->actionManager->getActionRevision() == previousRevision
					&& (!walking || actor->haveAsyncDest), "animation refresh must not restart the movement or invalidate its path");
		};
		const std::string replacement = moonlight ? u8"npc082-张仲天死啦.ini" : u8"npcres074_吃狗肉的假和尚.ini";
		const std::string expectedStand = moonlight ? "mpc166-3.asf" : "mpc403.asf";
		if (moonlight)
		{
			ok = executeSourceLine(u8"script/map/map_012_惠安镇/巧遇紫轩.txt",
				u8"npcspecialaction(\"张仲天\",\"mpc166-2.asf\");") && ok;
			ok = check(actor->scriptSpecialActionOverlayActive && actor->waits == 0,
				"real ordinary special action starts a non-blocking animation") && ok;
			ok = executeSourceLine(u8"script/map/map_012_惠安镇/巧遇紫轩.txt",
				u8"setnpcres(\"张仲天\",\"npc082-张仲天死啦.ini\");") && ok;
			ok = checkResourceRefresh() && ok;
			CoreLifecycleTestAccess::advanceActorFrame(*actor, actor->scriptSpecialActionOverlayDuration + 1);
			ok = check(!actor->scriptSpecialActionOverlayActive, "the real one-shot completes after the resource change") && ok;
			gameManager.global.data.canInput = false;
			ok = check(execute(u8"npcspecialactionex('张仲天','mpc166-2.asf'); assign('SpecialContinued',1);") == LUA_OK
				&& actor->waits == 1 && !actor->exhaustedFrames && !actor->scriptSpecialActionOverlayActive
				&& !gameManager.global.data.canInput && gameManager.varList.getInteger("SpecialContinued") == 1,
				"real-resource Ex waits to completion and preserves a pre-existing input lock") && ok;
			ok = executeSourceLine(u8"script/map/map_011_连接地图/trap02.txt",
				u8"setnpcactiontype(\"张仲天\",0);") && ok;
		}
		else
		{
			ok = executeSourceLine(u8"script/map/map065_少林寺/trap05.txt",
				u8"setnpcres(\"假和尚\",\"npcres074_吃狗肉的假和尚.ini\");") && ok;
			ok = checkResourceRefresh() && ok;
		}
		ok = check(actor->npcIni == replacement && actor->res.stand.imageFile == expectedStand
			&& actor->res.stand.imagePackage != nullptr, "SetNpcRes loads the real replacement standing image") && ok;
		if (walking)
		{
			for (int frame = 0; frame < 400; ++frame)
			{
				CoreLifecycleTestAccess::advanceActorFrame(*actor, 50);
			}
			ok = check(actor->getPosition() == Point{8,18} && actor->isStanding() && !actor->haveAsyncDest,
				"a resource refresh and special-action wait preserve actual arrival at the issued destination") && ok;
		}
		ok = check(gameManager.npcManager->save("action-actor.npc"), "save the changed actor resource and Action field") && ok;
		gameManager.npcManager->clearNPC(true);
		ok = check(gameManager.npcManager->load("action-actor.npc"), "reload the saved actor from disk") && ok;
		const auto restored = gameManager.npcManager->findNPC(actor->npcName);
		ok = check(restored.size() == 1 && restored.front() != actor && restored.front()->npcIni == replacement
			&& restored.front()->res.stand.imageFile == expectedStand
			&& (!moonlight || restored.front()->strollIntent == 0),
			"a new NPC instance restores the replacement resource and script Action setting") && ok;
		if (moonlight)
		{
			std::unique_ptr<char[]> tableBytes;
			if (!check(File::readFile("ini/save/qiangdao.npc", tableBytes) > 0, "read the actual bandit leader definition"))
			{
				ok = false;
				continue;
			}
			INIReader table(tableBytes);
			auto leader = std::make_shared<NPC>();
			leader->initFromIni(&table, "NPC000");
			gameManager.npcManager->npcList.push_back(leader);
			leader->actionManager->restartActionIgnoringTransitions(acStand);
			CoreLifecycleTestAccess::advanceActorFrame(*leader, 50);
			const auto previousDeathSound = leader->res.death.soundFile;
			for (const std::string line : { u8"setnpcactionfile(\"龙寨主\",0,\"npc023_强盗头子坐着.asf\");",
				u8"setnpcactionfile(\"龙寨主\",1,\"npc023_强盗头子坐着.asf\");",
				u8"setnpcactionfile(\"龙寨主\",11,\"npc023_强盗头子浓烟.asf\");" })
			{
				ok = executeSourceLine(u8"script/map/map_010_山洞内部/trap02.txt", line) && ok;
				ok = check(leader->actionLastTime == leader->getActionTime(leader->nowAction)
					&& leader->actionBeginTime == leader->getTime(), "action-file override refreshes the current looping animation") && ok;
			}
			ok = check(leader->res.stand.imageFile == u8"npc023_强盗头子坐着.asf"
				&& leader->res.stand1.imageFile == leader->res.stand.imageFile
				&& leader->res.death.imageFile == u8"npc023_强盗头子浓烟.asf"
				&& leader->res.stand.imagePackage && leader->res.death.imagePackage,
				"actual cutscene action-file overrides load standing and death resources") && ok;
			ok = check(!previousDeathSound.empty() && leader->res.death.soundFile == previousDeathSound,
				"replacing the real bandit death image preserves its configured death sound") && ok;
		}
		std::cout << "Production action resources checked: " << (moonlight ? "YYCS" : "XJXQY") << std::endl;
	}
	return ok;
}

bool runProductionExplicitAttackLists()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	bool ok = true;
	for (const std::string pack : { u8"江湖余尘", u8"江湖余尘二", u8"潇湘行" })
	{
		ScopedActiveResourceRoot resourceRoot;
		SaveFileManager::CurrentPathScope currentPath("save/explicit_attack_lists");
		if (!check(resourceRoot.valid() && currentPath.valid(), "explicit attack-list loading isolates resource and save roots")) return false;
		const auto packRoot = assetsRoot / std::filesystem::u8path(pack);
		std::vector<std::string> roots{ packRoot.u8string() };
		if (pack == u8"江湖余尘")
		{
			if (const char* dependencies = std::getenv("JXQY_TEST_ARENA_DEPENDENCY_ROOT")) roots.insert(roots.begin(), dependencies);
		}
		if (pack == u8"潇湘行") roots.push_back((assetsRoot / "jxqy2").u8string());
		roots.push_back((assetsRoot / "yycs").u8string());
		roots.push_back((assetsRoot / "common").u8string());
		File::setResourceFallbackRoots(roots);
		GameManager gameManager;
		ResourceManifest manifest;
		if (!check(manifest.loadFromFile("game_profile.ini"), "load the real explicit-list resource profile")) return false;
		gameManager.global.applyResourceManifestFeatures(manifest);
		for (const auto& folder : { "ini/npc", "ini/save" })
		{
			for (const auto& entry : std::filesystem::recursive_directory_iterator(packRoot / folder))
			{
				if (!entry.is_regular_file() || (entry.path().extension() != ".ini" && entry.path().extension() != ".npc")) continue;
				const auto relative = std::filesystem::relative(entry.path(), packRoot).generic_u8string();
				std::unique_ptr<char[]> data;
				if (File::readFile(relative, data) <= 0) continue;
				INIReader definition(data);
				for (const auto& section : definition.GetSectionNames())
				{
					const auto list = definition.Get(section, "FlyInis", "");
					if (list.empty()) continue;
					// Only the actual attack-list fields are under test here, not the
					// unrelated NPC animation, equipment, or story initialization.
					NPC actor;
					actor.attackRadius = definition.GetInteger(section, "AttackRadius", 0);
					if (actor.attackRadius == 0) actor.attackRadius = 1;
					actor.attackLevel = definition.GetInteger(section, "AttackLevel", 0);
					actor.flyInis = list;
					actor.rebuildAttackOptions();
					for (const auto& option : actor.attackOptions)
					{
						if (!option.hasExplicitUseDistance) continue;
						const int effective = actor.calcEffectiveUseDistance(option);
						ok = check(effective > 0 && effective <= option.configuredUseDistance,
							"a real explicit attack-list entry retains a positive bounded use distance") && ok;
						std::ostringstream observation;
						observation << "Explicit attack list:\tpack=" << pack << "\tpath=" << relative << "\tsection=" << section
							<< "\tindex=" << option.sourceIndex << "\tfile=" << option.magic->iniName
							<< "\tradius=" << actor.attackRadius << "\tconfigured=" << option.configuredUseDistance
							<< "\tprevious=" << std::min(effective, actor.attackRadius) << "\teffective=" << effective;
						std::cout << observation.str() << std::endl;
					}
				}
			}
		}
	}
	return ok;
}

bool runAttackSaveBoundaryContracts()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	struct SaveCase { std::string pack, file, section; bool player = false; };
	const SaveCase cases[] = {
		{ "yycs", "ini/save/map025_2.npc", "NPC044" },
		{ "yycs", "ini/save/map030_4.npc", "NPC010" },
		{ "yycs", "ini/save/map030_wedfight.npc", "NPC105" },
		{ "yycs", "ini/save/map050_heart3.npc", "NPC000" },
		{ "yycs", "ini/save/wudangshanding1.npc", "NPC007" },
		{ "xjxqy", u8"ini/npc/npc011_方勉.ini", "Init" },
		{ u8"江湖余尘二", "ini/save/map016.npc", "NPC002" },
		{ "yycs", "ini/save/player0.ini", "Init", true },
		{ "xjxqy", "ini/save/player0.ini", "Init", true },
		{ u8"江湖余尘二", "ini/save/player0.ini", "Init", true }
	};
	bool ok = true;
	for (const auto& fixture : cases)
	{
		const auto packRoot = assetsRoot / std::filesystem::u8path(fixture.pack);
		if (!std::filesystem::exists(packRoot / "game_profile.ini"))
		{
			std::cout << "SKIP: optional attack-save production pack is absent: " << fixture.pack << '\n';
			continue;
		}
		ScopedActiveResourceRoot resourceRoot;
		SaveFileManager::CurrentPathScope currentPath("save/attack_save_boundaries");
		if (!check(resourceRoot.valid() && currentPath.valid(), "attack-save checks isolate all file writes")) return false;
		std::vector<std::string> roots{ packRoot.u8string() };
		if (fixture.pack == u8"江湖余尘二") roots.push_back((assetsRoot / "yycs").u8string());
		roots.push_back((assetsRoot / "common").u8string());
		File::setResourceFallbackRoots(roots);
		GameManager gameManager;
		ResourceManifest manifest;
		std::unique_ptr<char[]> bytes;
		if (!check(manifest.loadFromFile("game_profile.ini") && File::readFile(fixture.file, bytes) > 0,
			"read the production profile and complete attack-save actor")) return false;
		gameManager.global.applyResourceManifestFeatures(manifest);
		gameManager.global.data.NPCAI = false;
		gameManager.global.data.canInput = false;
		gameManager.varList.ensureInitialized();
		gameManager.map->data = std::make_shared<MapData>();
		gameManager.map->data->head.width = gameManager.map->data->head.height = 200;
		gameManager.map->data->tile.assign(200, std::vector<MapTile>(200));
		INIReader definition(bytes);
		const auto describe = [](const NPC& actor)
		{
			std::ostringstream text;
			text << actor.npcName << '|' << actor.npcIni << '|' << actor.flyIni << '|' << actor.flyIni2 << '|' << actor.flyInis
				<< '|' << actor.attackRadius << '|' << actor.attackLevel << '|' << actor.state << '|' << actor.strollIntent
				<< '|' << actor.getPosition().x << '|' << actor.getPosition().y << '|' << actor.direction
				<< '|' << actor.life << '|' << actor.thew << '|' << actor.mana << '|' << actor.scriptFile << '|' << actor.deathScript;
			for (const auto& option : actor.attackOptions)
				text << '|' << option.magic->iniName << ':' << option.configuredUseDistance << ':' << actor.calcEffectiveUseDistance(option);
			return text.str();
		};
		for (bool afterRelease : { false, true })
		{
			for (bool reusePlayer : { false, true })
			{
				if (!fixture.player && reusePlayer) continue;
				gameManager.effectManager->freeResource();
				gameManager.npcManager->clearNPC(true);
				std::shared_ptr<NPC> actor;
				std::shared_ptr<NPC> target;
				if (fixture.player)
				{
					if (!check(gameManager.player->loadInitialTemplate(0), "load the actual player for an attack snapshot")) return false;
					actor = gameManager.player;
					target = std::make_shared<NPC>();
					target->kind = nkBattle;
					target->relation = nrHostile;
					target->isAIDisabled = true;
					target->lifeMax = target->life = 100000000;
					gameManager.npcManager->addNPC(target);
				}
				else
				{
					actor = std::make_shared<NPC>();
					actor->initFromIni(&definition, fixture.section);
					actor->relation = nrHostile;
					actor->isAIDisabled = true;
					gameManager.npcManager->addNPC(actor);
					target = gameManager.player;
					gameManager.player->info.lifeMax = target->life = 100000000;
				}
				actor->setPosition({ 80, 80 }, false);
				const int distance = fixture.player ? 1 : actor->getMaxAttackOptionDistance();
				Point destination = actor->getPosition();
				for (int step = 0; step < distance; ++step) destination = Map::getSubPoint(destination, 0);
				target->setPosition(destination, false);
				gameManager.map->createDataMap();
				actor->beginAttack(destination, target);
				if (!check(actor->isAttacking() && actor->hasPreparedAttackMagic && actor->actionLastTime > 1,
					"the complete production actor admits a real attack before snapshotting")) { ok = false; continue; }
				const UTime duration = actor->actionLastTime;
				const UTime elapsed = afterRelease ? duration : duration / 2;
				CoreLifecycleTestAccess::advanceActorFrame(*actor, elapsed);
				ok = check(actor->isAttacking() == !afterRelease && gameManager.effectManager->effectList.empty() == !afterRelease,
					"attack snapshots distinguish an unreleased animation from a completed release") && ok;
				const auto original = actor;
				const auto beforeSave = describe(*actor);
				const bool usesNativeProtocol = actor->usesNativeAttackProtocol();
				const auto revision = actor->actionManager->getActionRevision();
				const auto prepared = actor->preparedAttackMagic;
				const bool nativeProtocol = actor->preparedAttackUsesNativeProtocol;
				const auto effectCount = gameManager.effectManager->effectList.size();
				if (!check(fixture.player ? gameManager.player->save(0) : gameManager.npcManager->save("attack-boundary.npc"),
					"write an attack snapshot through the actual Player or NPC file writer")) return false;
				ok = check(actor->actionManager->getActionRevision() == revision && actor->preparedAttackMagic == prepared
					&& actor->preparedAttackUsesNativeProtocol == nativeProtocol && describe(*actor) == beforeSave
					&& gameManager.effectManager->effectList.size() == effectCount,
					"saving neither cancels, advances nor reselects the live attack") && ok;
				const std::string savedPath = SaveFileManager::CurrentPath() + (fixture.player ? "player0.ini" : "attack-boundary.npc");
				const auto savedText = readVirtualFile(savedPath);
				INIReader saved(savedPath);
				const std::string section = fixture.player ? "Init" : "NPC000";
				ok = check(!savedText.empty() && saved.ParseError() == 0 && saved.HasSection(section)
					&& saved.Get(section, "FlyIni", "") == actor->flyIni && saved.Get(section, "FlyIni2", "") == actor->flyIni2
					&& saved.Get(section, "FlyInis", "") == actor->flyInis
					&& saved.GetInteger(section, "State", -1) == actor->state && saved.GetInteger(section, "Action", -1) == actor->strollIntent,
					"on-disk snapshots retain attack definitions and persistent State/Action rather than animation enums") && ok;
				CoreLifecycleTestAccess::advanceActorFrame(*actor, duration - elapsed + 1);
				ok = check(actor->isStanding() && !gameManager.effectManager->effectList.empty()
					&& (!afterRelease || gameManager.effectManager->effectList.size() == effectCount),
					"the live actor still completes exactly its existing attack after saving") && ok;
				// Entity-file readback is tested separately from the effect snapshot.
				gameManager.effectManager->freeResource();
				if (fixture.player)
				{
					if (!reusePlayer)
					{
						gameManager.controller->removeChild(gameManager.player);
						gameManager.player = std::make_shared<Player>();
						gameManager.npcManager->setPlayer(gameManager.player);
						gameManager.controller->addChild(gameManager.player);
					}
					else
					{
						actor->beginAttack(destination, target);
						CoreLifecycleTestAccess::advanceActorFrame(*actor, actor->actionLastTime / 2);
						ok = check(actor->isAttacking() && actor->hasPreparedAttackMagic && gameManager.effectManager->effectList.empty(),
							"the reused player is in another live attack when its snapshot is loaded") && ok;
					}
					if (!check(gameManager.player->load(0), "read the saved player file using the production loader")) return false;
					actor = gameManager.player;
				}
				else
				{
					gameManager.npcManager->clearNPC(true);
					if (!check(gameManager.npcManager->load("attack-boundary.npc") && gameManager.npcManager->npcList.size() == 1,
						"read the saved NPC file into a newly constructed actor")) return false;
					actor = gameManager.npcManager->npcList.front();
				}
				ok = check((actor == original) == reusePlayer && actor->isStanding() && !actor->hasPreparedAttackMagic
					&& !actor->preparedAttackUsesNativeProtocol && actor->usesNativeAttackProtocol() == usesNativeProtocol && describe(*actor) == beforeSave,
					"fresh and reused actors retain saved definitions and attributes without restoring a stale prepared attack") && ok;
				CoreLifecycleTestAccess::advanceActorFrame(*actor, duration + 1);
				ok = check(gameManager.effectManager->effectList.empty(), "reading an entity file does not replay an old attack into the world") && ok;
				actor->idledFrame = actor->idle;
				if (fixture.player) actor->beginAttack(destination, target);
				else
				{
					ok = check(actor->evaluateAndPlan(target) && actor->executeActionPlan(target), "the restored NPC can plan and execute a new attack") && ok;
				}
				ok = check(actor->isAttacking(), "the restored actor starts a new attack at the original release distance") && ok;
				CoreLifecycleTestAccess::advanceActorFrame(*actor, actor->actionLastTime + 1);
				ok = check(!gameManager.effectManager->effectList.empty() && actor->isStanding(), "the new post-load attack actually releases and returns to stand") && ok;
				std::cout << "Attack save boundary:\tpack=" << fixture.pack << "\tfile=" << fixture.file << "\tsection=" << fixture.section
					<< "\tplayer=" << fixture.player << "\tafterRelease=" << afterRelease << "\treused=" << reusePlayer
					<< "\tnative=" << usesNativeProtocol << "\tbytes=" << savedText.size() << "\tnewEffects=" << gameManager.effectManager->effectList.size() << std::endl;
			}
		}
	}
	return ok;
}

bool runNativeNpcAttackProtocolContracts()
{
	const auto packRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets/yycs";
	if (!std::filesystem::exists(packRoot / "game_profile.ini"))
	{
		std::cout << "SKIP: optional native NPC protocol resources are absent\n";
		return true;
	}
	ScopedActiveResourceRoot resourceRoot;
	SaveFileManager::CurrentPathScope currentPath("save/native_npc_protocol");
	if (!check(resourceRoot.valid() && currentPath.valid(), "native NPC protocol checks isolate resource/save writes")) return false;
	File::setResourceFallbackRoots({ packRoot.u8string(), (packRoot.parent_path() / "common").u8string() });
	GameManager gameManager;
	ResourceManifest manifest;
	if (!check(manifest.loadFromFile("game_profile.ini"), "read native NPC protocol resource manifest")) return false;
	gameManager.global.data.NPCAI = false;
	gameManager.varList.ensureInitialized();
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = gameManager.map->data->head.height = 100;
	gameManager.map->data->tile.assign(100, std::vector<MapTile>(100));
	gameManager.player->info.lifeMax = gameManager.player->life = 100000000;
	const std::string attackFile = u8"magic-符咒攻击.ini", healFile = u8"player-magic-清心咒.ini";
	bool ok = true;
	auto reset = [&](const char* file = "wudangshanding1.npc", const char* section = "NPC007")
	{
		gameManager.effectManager->freeResource();
		gameManager.npcManager->clearNPC(true);
		for (auto& row : gameManager.map->data->tile) for (auto& tile : row) tile.obstacle = 0;
		std::unique_ptr<char[]> bytes;
		if (File::readFile(std::string("ini/save/") + file, bytes) <= 0) return std::shared_ptr<NPC>();
		INIReader definition(bytes);
		auto actor = std::make_shared<NPC>();
		actor->initFromIni(&definition, section);
		actor->relation = nrHostile;
		actor->isAIDisabled = true;
		actor->setPosition({ 40, 40 }, false);
		gameManager.npcManager->addNPC(actor);
		Point target = actor->getPosition();
		for (int step = 0; step < actor->attackRadius; ++step) target = Map::getSubPoint(target, 0);
		gameManager.player->setPosition(target, false);
		gameManager.map->createDataMap();
		for (int frame = 0; frame < actor->idle; ++frame) actor->updateIdleFrame();
		return actor;
	};
	auto releasedOnly = [&](const std::string& file)
	{
		const auto& effects = gameManager.effectManager->effectList;
		return file.empty() ? effects.empty() : !effects.empty() && std::all_of(effects.begin(), effects.end(),
			[&](const auto& effect) { return effect->magic.iniName == file; });
	};
	for (bool enabled : { false, true })
	{
		manifest.features["nativenpcattackatanimationend"] = enabled;
		gameManager.global.applyResourceManifestFeatures(manifest);
		for (bool clearAtRelease : { false, true })
		{
			auto actor = reset();
			if (!check(actor != nullptr, "load the real native actor for late-field checks")) return false;
			gameManager.scriptAPI.changeFlyIni2(actor->npcName, attackFile);
			actor->beginAttack(gameManager.player->getPosition(), gameManager.player);
			if (!check(actor->isAttacking() && gameManager.effectManager->effectList.empty(),
				"native and ordinary protocols begin an animation without early effects")) { ok = false; continue; }
			const UTime duration = actor->actionLastTime;
			gameManager.scriptAPI.changeFlyIni(actor->npcName, clearAtRelease ? "" : healFile);
			gameManager.scriptAPI.changeFlyIni2(actor->npcName, clearAtRelease ? "" : healFile);
			if (duration > 1) CoreLifecycleTestAccess::advanceActorFrame(*actor, duration - 1);
			ok = check(gameManager.effectManager->effectList.empty(), "changing native fields does not release before the last frame") && ok;
			CoreLifecycleTestAccess::advanceActorFrame(*actor, 1);
			const std::string expected = enabled ? (clearAtRelease ? "" : healFile) : attackFile;
			ok = check(releasedOnly(expected) && actor->isStanding(),
				"late native fields determine release, including clearing both slots; the ordinary protocol keeps its admitted magic") && ok;
			std::cout << "Native protocol contract:\tenabled=" << enabled << "\tclear=" << clearAtRelease
				<< "\texpected=" << expected << "\teffects=" << gameManager.effectManager->effectList.size() << std::endl;
		}
		auto actor = reset();
		if (!check(actor != nullptr, "load native actor for healing and range checks")) return false;
		gameManager.scriptAPI.changeFlyIni(actor->npcName, healFile);
		actor->beginAttack(gameManager.player->getPosition(), gameManager.player);
		ok = check(actor->isAttacking() == enabled, "the native protocol admits full-life healing without the ordinary need filter") && ok;
		if (actor->isAttacking())
		{
			CoreLifecycleTestAccess::advanceActorFrame(*actor, actor->actionLastTime + 1);
			ok = check(releasedOnly(healFile), "both native slots set to healing give a deterministic full-life release") && ok;
			ok = check(actor->life == actor->getLifeMax(), "native full-life healing cannot exceed the effective life maximum") && ok;
		}
		actor = reset();
		actor->equipmentAttributes.lifeMax = 50;
		actor->life = actor->getLifeMax() - 3;
		actor->addLife(10);
		ok = check(actor->life == actor->getLifeMax(), "NPC healing caps at the effective maximum including equipment") && ok;
		actor->addLife(std::numeric_limits<int>::max());
		ok = check(actor->life == actor->getLifeMax(), "large NPC healing saturates without signed overflow") && ok;
		actor->life = 100;
		actor->addLife(10);
		ok = check(actor->life == 110, "NPC healing below the maximum retains the requested amount") && ok;
		actor->invincible = 1;
		actor->addLife(-20);
		ok = check(actor->life == 110, "NPC healing normalization preserves invincible damage rejection") && ok;
		actor->invincible = 0;
		actor->addLife(-20);
		ok = check(actor->life == 90, "NPC healing normalization preserves ordinary damage") && ok;
		actor = reset();
		actor->life = actor->getLifeMax() / 2;
		ok = check(actor->trySelfBuff() == !enabled, "native secondary healing is not also cast by the ordinary self-buff fallback") && ok;
		actor = reset("map030_wedfight.npc", "NPC105");
		const auto ready = actor->findReadyAttackOption(gameManager.player->getPosition());
		ok = check(ready.has_value() && ready->magic == (enabled ? actor->npcMagic : actor->npcMagic2),
			"native planning uses NPC radius without replacing the primary with the physically ready secondary") && ok;
		for (bool blocked : { false, true })
		{
			actor = reset();
			gameManager.scriptAPI.changeFlyIni2(actor->npcName, "");
			Point target = actor->getPosition();
			for (int step = 0; step < 4; ++step) target = Map::getSubPoint(target, 0);
			gameManager.player->setPosition(target, false);
			if (blocked)
			{
				for (int direction = 0; direction < 8; ++direction)
				{
					const Point neighbor = Map::getSubPoint(actor->getPosition(), direction);
					gameManager.map->data->tile[neighbor.y][neighbor.x].obstacle = 0x40;
				}
			}
			gameManager.map->createDataMap();
			ok = check(actor->evaluateAndPlan(gameManager.player) && actor->executeActionPlan(gameManager.player),
				"single native attack plans remain executable with open or blocked retreat") && ok;
			ok = check(actor->isWalking() == (enabled && !blocked) && actor->isAttacking() == (!enabled || blocked),
				"native close targets cause retreat to NPC radius, while blocked retreat still attacks") && ok;
		}
		for (int replacement = 0; replacement < 5; ++replacement)
		{
			actor = reset();
			gameManager.scriptAPI.changeFlyIni2(actor->npcName, attackFile);
			if (replacement == 0) actor->flyInis = attackFile + ":6";
			if (replacement == 1) actor->temporaryMagicListReplacement = attackFile + ":6";
			if (replacement == 2) actor->equipmentFlyIniReplacements = { attackFile };
			if (replacement == 3) actor->temporaryFlyIniReplacements = { attackFile };
			if (replacement == 4) actor->equipmentFlyIni2Replacements = { attackFile };
			actor->rebuildAttackOptions();
			actor->beginAttack(gameManager.player->getPosition(), gameManager.player);
			if (!check(actor->isAttacking(), "explicit/replacement lists still admit their original attack")) { ok = false; continue; }
			gameManager.scriptAPI.changeFlyIni(actor->npcName, healFile);
			gameManager.scriptAPI.changeFlyIni2(actor->npcName, healFile);
			CoreLifecycleTestAccess::advanceActorFrame(*actor, actor->actionLastTime + 1);
			ok = check(releasedOnly(attackFile), "explicit and replacement lists retain their prepared attack under either resource mode") && ok;
		}
		actor = reset();
		actor->beginAttack(gameManager.player->getPosition(), gameManager.player);
		actor->actionManager->forceChangeAction(acStand);
		CoreLifecycleTestAccess::advanceActorFrame(*actor, 10000);
		ok = check(gameManager.effectManager->effectList.empty(), "interrupted native attacks cannot release later") && ok;
		actor->immobilized = true;
		actor->beginAttack(gameManager.player->getPosition(), gameManager.player);
		ok = check(!actor->isAttacking() && !actor->hasPreparedAttackMagic, "native mode preserves disabled-action admission") && ok;
		actor = reset();
		auto player = gameManager.player;
		player->attackRadius = 6;
		player->flyIni = player->flyIni2 = attackFile;
		player->npcMagic = player->npcMagic2 = gameManager.magicManager.loadAttackMagic(attackFile);
		player->rebuildAttackOptions();
		const auto playerMagic = player->prepareAttackMagicForAction(actor->getPosition(), actor, armLockedRelease);
		ok = check(playerMagic != nullptr && !player->usesNativeAttackProtocol(), "Player keeps its ordinary prepared protocol under either feature setting") && ok;
		player->npcMagic = player->npcMagic2 = gameManager.magicManager.loadAttackMagic(healFile);
		ok = check(player->releasePreparedAttackMagic(actor->getPosition(), actor) && releasedOnly(attackFile),
			"changing Player native fields does not replace its admitted attack") && ok;
		if (enabled)
		{
			for (const auto& [file, section] : { std::pair{ "event430.npc", "NPC001" }, { "map006_1.npc", "NPC007" },
				{ "map042_1.npc", "NPC023" }, { "map047_1.npc", "NPC084" } })
			{
				actor = reset(file, section);
				if (!check(actor != nullptr && actor->npcMagic && actor->npcMagic->loadSucceeded && actor->flyIni2.empty(),
					"single-attack controls use complete original NPC definitions")) { ok = false; continue; }
				ok = check(actor->evaluateAndPlan(gameManager.player) && actor->executeActionPlan(gameManager.player) && actor->isAttacking(),
					"single native attacks release at their own unchanged NPC radius") && ok;
				CoreLifecycleTestAccess::advanceActorFrame(*actor, actor->actionLastTime + 1);
				ok = check(releasedOnly(actor->flyIni), "single native attacks still produce their original magic effect") && ok;
				std::cout << "Native single attack:\tfile=" << file << "\tsection=" << section << "\tradius=" << actor->attackRadius
					<< "\tmagic=" << actor->flyIni << "\teffects=" << gameManager.effectManager->effectList.size() << std::endl;
			}
		}
	}
	return ok;
}

bool runMoonlightNativeAttackObservations()
{
	const auto packRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets/yycs";
	if (!std::filesystem::exists(packRoot / "game_profile.ini"))
	{
		std::cout << "SKIP: optional Moonlight native attack resources are absent\n";
		return true;
	}
	ScopedActiveResourceRoot resourceRoot;
	SaveFileManager::CurrentPathScope currentPath("save/moonlight_native_attacks");
	if (!check(resourceRoot.valid() && currentPath.valid(), "Moonlight native attack observations isolate resource and save writes")) return false;
	File::setResourceFallbackRoots({ packRoot.u8string(), (packRoot.parent_path() / "common").u8string() });
	GameManager gameManager;
	ResourceManifest manifest;
	if (!check(manifest.loadFromFile("game_profile.ini"), "read the actual Moonlight feature profile")) return false;
	gameManager.global.applyResourceManifestFeatures(manifest);
	gameManager.global.data.NPCAI = false;
	gameManager.varList.ensureInitialized();
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = gameManager.map->data->head.height = 100;
	gameManager.map->data->tile.assign(100, std::vector<MapTile>(100));
	gameManager.map->createDataMap();
	gameManager.player->info.lifeMax = gameManager.player->life = 100000000;
	bool ok = true;
	for (const auto& [file, section] : { std::pair{ "map025_2.npc", "NPC044" }, { "map030_4.npc", "NPC010" },
		{ "map030_wedfight.npc", "NPC105" }, { "map050_heart3.npc", "NPC000" }, { "wudangshanding1.npc", "NPC007" } })
	{
		std::unique_ptr<char[]> bytes;
		if (!check(File::readFile(std::string("ini/save/") + file, bytes) > 0, "load each actual Moonlight native pair")) return false;
		INIReader definition(bytes);
		gameManager.npcManager->clearNPC(true);
		auto actor = std::make_shared<NPC>();
		actor->initFromIni(&definition, section);
		actor->relation = nrHostile;
		actor->isAIDisabled = true;
		gameManager.npcManager->addNPC(actor);
		if (!check(actor->attackOptions.size() == 2 && actor->npcMagic && actor->npcMagic2
			&& actor->npcMagic->loadSucceeded && actor->npcMagic2->loadSucceeded && actor->flyInis.empty(),
			"the production definition contains exactly two usable native attacks")) return false;
		for (int lifePercent : { 100, 75, 50 })
		{
			for (int distance : { 1, actor->attackRadius })
			{
				int primary = 0, secondary = 0, approaching = 0, waiting = 0, rejected = 0, preparedSecondary = 0;
				int healingReleases = 0, primaryReady = 0, secondaryReady = 0;
				for (int sample = 0; sample < 64; ++sample)
				{
					gameManager.effectManager->freeResource();
					actor->actionManager->restartActionIgnoringTransitions(acStand);
					actor->clearCombatTargetMemory();
					actor->hasLastUsedAttackOption = false;
					actor->setPosition({ 40, 40 }, false);
					actor->life = actor->getLifeMax() * lifePercent / 100;
					const int startingLife = actor->life;
					Point destination = actor->getPosition();
					for (int step = 0; step < distance; ++step) destination = Map::getSubPoint(destination, 0);
					gameManager.player->setPosition(destination, false);
					gameManager.map->createDataMap();
					// Each independent trial starts after the original Idle delay.
					// Keep its resource value and use the normal frame counter.
					for (int frame = 0; frame < actor->idle; ++frame) actor->updateIdleFrame();
					const auto candidates = actor->buildAttackCandidates(destination);
					if (sample == 0)
					{
						for (const auto& candidate : candidates)
						{
							if (candidate.option.magic == actor->npcMagic) primaryReady = candidate.canHitNow;
							if (candidate.option.magic == actor->npcMagic2) secondaryReady = candidate.canHitNow;
						}
					}
					if (!actor->evaluateAndPlan(gameManager.player) || !actor->executeActionPlan(gameManager.player))
					{
						++rejected;
						continue;
					}
					if (!actor->isAttacking())
					{
						if (actor->isWalking() || actor->isRunning()) ++approaching;
						else ++waiting;
						continue;
					}
					if (actor->preparedAttackMagic == actor->npcMagic2) ++preparedSecondary;
					// Advance the real animation once to its release point. These are
					// independent selection trials, not a continuous battle or hit-rate test.
					CoreLifecycleTestAccess::advanceActorFrame(*actor, actor->actionLastTime + 1);
					bool releasedPrimary = false, releasedSecondary = false;
					for (const auto& effect : gameManager.effectManager->effectList)
					{
						if (effect->user.lock() != actor) continue;
						releasedPrimary = releasedPrimary || effect->magic.iniName == actor->npcMagic->iniName;
						releasedSecondary = releasedSecondary || effect->magic.iniName == actor->npcMagic2->iniName;
					}
					if (releasedPrimary) ++primary;
					if (releasedSecondary) ++secondary;
					if (actor->life > startingLife) ++healingReleases;
					ok = check(releasedPrimary != releasedSecondary,
						"each accepted production attack animation releases one of its native entries") && ok;
				}
				std::ostringstream observation;
				observation << "Moonlight native attacks:\tfile=" << file << "\tsection=" << section << "\tlifePercent=" << lifePercent
					<< "\tdistance=" << distance << "\tprimaryReady=" << primaryReady << "\tsecondaryReady=" << secondaryReady
					<< "\tprimary=" << primary << "\tsecondary=" << secondary << "\tpreparedSecondary=" << preparedSecondary
					<< "\thealingReleases=" << healingReleases << "\tapproaching=" << approaching << "\twaiting=" << waiting << "\trejected=" << rejected;
				std::cout << observation.str() << std::endl;
				ok = check(primary + secondary + approaching + waiting + rejected == 64,
					"every native-pair selection trial has an observed outcome") && ok;
			}
		}
	}
	return ok;
}

bool runMissedAttackMotionContracts()
{
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid(), "missed-attack motion checks isolate resource writes")) return false;
	GameManager gameManager;
	gameManager.global.data.NPCAI = false;
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = gameManager.map->data->head.height = 40;
	gameManager.map->data->tile.assign(40, std::vector<MapTile>(40));
	bool ok = true;
	for (bool playerTarget : { false, true })
	{
		for (int motion : { 0, 1, 2 })
		{
			gameManager.npcManager->clearNPC(true);
			gameManager.player = std::make_shared<Player>();
			std::shared_ptr<NPC> target = playerTarget ? gameManager.player : std::make_shared<NPC>();
			if (!playerTarget) gameManager.npcManager->addNPC(target);
			target->lifeMax = target->life = 1000;
			target->evade = gameManager.player->info.evade = 1000;
			target->setPosition({ 10, 10 }, false);
			gameManager.map->createDataMap();
			auto effect = std::make_shared<Effect>();
			effect->level = 1;
			effect->evade = 0;
			effect->damage = 10;
			effect->flyingDirection = { 0, 1000 };
			effect->magic.bounce = motion == 1 ? 500 : 0;
			effect->magic.bounceFly = effect->magic.linkedLevel[1].bounceFly = motion == 2 ? 3 : 0;
			target->hurt(effect);
			ok = check(target->life == 1000, "evade difference above 100 guarantees no damage without replacing the random engine") && ok;
			ok = check(target->isBouncing() == (motion == 1) && target->isMagicForcedMoving() == (motion == 2),
				"a missed NPC/Player attack retains configured Bounce/BounceFly while an untagged attack adds no movement") && ok;
			std::cout << "Miss motion:\tplayer=" << playerTarget << "\tmotion=" << motion
				<< "\tbounced=" << target->isBouncing() << "\tforced=" << target->isMagicForcedMoving() << std::endl;
		}
	}
	return ok;
}

bool runProductionExplicitAttackCombat()
{
	class CollisionObservingPlayer final : public Player
	{
	public:
		int collisions = 0;
		std::string firstCollisionMagic;
		void hurt(std::shared_ptr<Effect> effect) override
		{
			if (effect && collisions++ == 0) firstCollisionMagic = effect->magic.iniName;
			Player::hurt(effect);
		}
	};
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	struct CombatCase
	{
		std::string pack, file, section;
		int options;
	};
	const CombatCase cases[] = {
		{ u8"江湖余尘二", "ini/save/map016.npc", "NPC002", 3 },
		{ u8"潇湘行", "ini/save/zddxmg.npc", "NPC003", 2 },
		{ u8"潇湘行", "ini/save/zddxmg.npc", "NPC015", 2 },
		{ u8"潇湘行", "ini/save/zddxmg.npc", "NPC017", 2 },
		{ u8"潇湘行", "ini/save/ca.npc", "NPC036", 1 },
		{ u8"潇湘行", "ini/save/tmz.npc", "NPC003", 1 }
	};
	bool ok = true;
	for (const auto& fixture : cases)
	{
		const auto packRoot = assetsRoot / std::filesystem::u8path(fixture.pack);
		if (!std::filesystem::exists(packRoot / "game_profile.ini"))
		{
			std::cout << "SKIP: optional explicit-combat production pack is absent: " << fixture.pack << '\n';
			continue;
		}
		ScopedActiveResourceRoot resourceRoot;
		SaveFileManager::CurrentPathScope currentPath("save/explicit_attack_combat");
		if (!check(resourceRoot.valid() && currentPath.valid(), "explicit combat isolates all resource and save writes")) return false;
		std::vector<std::string> roots{ packRoot.u8string() };
		if (fixture.pack == u8"潇湘行") roots.push_back((assetsRoot / "jxqy2").u8string());
		roots.push_back((assetsRoot / "yycs").u8string());
		roots.push_back((assetsRoot / "common").u8string());
		File::setResourceFallbackRoots(roots);
		GameManager gameManager;
		ResourceManifest manifest;
		std::unique_ptr<char[]> bytes;
		if (!check(manifest.loadFromFile("game_profile.ini") && File::readFile(fixture.file, bytes) > 0,
			"load the actual explicit-combat profile and NPC list")) return false;
		gameManager.global.applyResourceManifestFeatures(manifest);
		gameManager.global.data.NPCAI = false;
		gameManager.varList.ensureInitialized();
		auto target = std::make_shared<CollisionObservingPlayer>();
		gameManager.player = target;
		INIReader definition(bytes);
		const auto mapName = definition.Get("Head", "Map", "");
		if (!check(gameManager.scriptAPI.loadMap(mapName, false) && gameManager.player->loadInitialTemplate(0),
			"load the actual combat map and player action resources")) return false;
		gameManager.global.data.canInput = false;
		auto actor = std::make_shared<NPC>();
		actor->initFromIni(&definition, fixture.section);
		gameManager.npcManager->addNPC(actor);
		const auto originalActor = actor;
		const Point originalPosition = actor->getPosition();
		const int originalRelation = actor->relation;
		const auto describeAttacks = [](const NPC& npc)
		{
			std::ostringstream result;
			result << npc.flyIni << '|' << npc.flyIni2 << '|' << npc.flyInis << '|' << npc.attackRadius << '|' << npc.attackLevel;
			for (const auto& option : npc.attackOptions)
			{
				result << '|' << option.magic->iniName << ':' << option.configuredUseDistance << ':'
					<< option.hasExplicitUseDistance << ':' << npc.calcEffectiveUseDistance(option) << ':' << option.moveKind;
			}
			return result.str();
		};
		if (!check(actor->attackOptions.size() == fixture.options
			&& actor->attackRadius == definition.GetInteger(fixture.section, "AttackRadius", 1)
			&& std::all_of(actor->attackOptions.begin(), actor->attackOptions.end(), [](const auto& option)
				{ return option.magic && option.magic->loadSucceeded; }), "the full NPC loads every native and explicit attack")) return false;
		const auto originalAttacks = describeAttacks(*actor);
		if (!check(gameManager.npcManager->save("explicit-combat.npc"), "save the untouched actor through the real NPC file writer")) return false;
		for (bool reloaded : { false, true })
		{
			if (reloaded)
			{
				gameManager.effectManager->freeResource();
				gameManager.npcManager->clearNPC(true);
				if (!check(gameManager.npcManager->load("explicit-combat.npc") && gameManager.npcManager->npcList.size() == 1,
					"reload the saved production actor into a new NPC instance")) return false;
				actor = gameManager.npcManager->npcList.front();
				ok = check(actor != originalActor && actor->getPosition() == originalPosition && actor->relation == originalRelation
					&& describeAttacks(*actor) == originalAttacks, "NPC readback retains position, relation, attack level and all effective distances") && ok;
			}
			// -1 observes the unrestricted scheduler. Other cases select one real
			// entry before animation, so every magic is exercised without depending
			// on random selection. Neither test asserts the story's combat trigger.
			const bool hasCharge = actor->attackOptions.back().magic->carryUser == 2;
			for (int slot = -1; slot < fixture.options + (hasCharge ? 1 : 0); ++slot)
			{
				const bool forcedMiss = slot == fixture.options;
				const int selected = forcedMiss ? fixture.options - 1 : slot;
				gameManager.effectManager->freeResource();
				gameManager.global.data.NPCAI = selected < 0;
				actor->isAIDisabled = selected >= 0;
				actor->actionManager->restartActionIgnoringTransitions(acStand);
				actor->clearCombatTargetMemory();
				actor->clearBounceState();
				actor->setPosition(originalPosition, false);
				actor->relation = nrHostile;
				actor->life = actor->getLifeMax();
				actor->idledFrame = actor->idle;
				gameManager.player->actionManager->restartActionIgnoringTransitions(acStand);
				gameManager.player->clearBounceState();
				gameManager.player->info.lifeMax = gameManager.player->life = 100000000;
				gameManager.player->info.evade = forcedMiss ? 100000 : 0;
				target->collisions = 0;
				target->firstCollisionMagic.clear();
				gameManager.player->setPosition({ -1, -1 }, false);
				gameManager.map->createDataMap();
				const int distance = selected < 0 ? actor->getMaxAttackOptionDistance()
					: actor->calcEffectiveUseDistance(actor->attackOptions[selected]);
				Point start{ -1, -1 }, destination{ -1, -1 };
				int nearest = 1000000;
				// Find a nearby straight lane on untouched map obstacles. Other NPCs,
				// objects, trap bindings and the surrounding cutscene are not loaded.
				for (int y = 0; y < gameManager.map->data->head.height; ++y)
				{
					for (int x = 0; x < gameManager.map->data->head.width; ++x)
					{
						const Point candidate{ x, y };
						const int separation = Map::calDistance(originalPosition, candidate);
						if (separation >= nearest || !gameManager.map->canWalkForActor(candidate, actor)) continue;
						for (int direction = 0; direction < 8; ++direction)
						{
							Point end = candidate;
							bool clear = true;
							for (int step = 0; step < distance; ++step)
							{
								end = Map::getSubPoint(end, direction);
								clear = clear && gameManager.map->canWalkForActor(end, actor) && gameManager.map->canFly(end);
							}
							if (clear && gameManager.map->canSee(candidate, end))
							{
								start = candidate; destination = end; nearest = separation;
								break;
							}
						}
					}
				}
				if (!check(start.x >= 0 && Map::calDistance(start, destination) == distance,
					"the actual map provides a clear lane at the real effective attack distance")) return false;
				actor->setPosition(start, false);
				gameManager.player->setPosition(destination, false);
				gameManager.map->createDataMap();
				if (selected >= 0)
				{
					const auto& option = actor->attackOptions[selected];
					ok = check(actor->canMagicHitTarget(option, start, destination, actor->getClampedAttackLevel()),
						"the selected production magic is reachable at its effective distance") && ok;
					actor->prepareImmediateAttackPlan(gameManager.player, option, destination);
					ok = check(actor->executeActionPlan(gameManager.player) && actor->isAttacking()
						&& actor->preparedAttackMagic == option.magic, "the selected production entry starts a real attack animation") && ok;
				}
				int firstRelease = -1, releases = 0, actorMoves = 0, playerMoves = 0;
				bool carried = false, bounced = false;
				std::string firstMagic;
				for (int elapsed = 20; elapsed <= 8000; elapsed += 20)
				{
					const Point previousActorPosition = actor->getPosition(), previousPlayerPosition = gameManager.player->getPosition();
					const auto previousEffects = gameManager.effectManager->effectList;
					CoreLifecycleTestAccess::advanceActorFrame(*gameManager.player, 20);
					CoreLifecycleTestAccess::advanceActorFrame(*actor, 20);
					bool released = false;
					const auto effects = gameManager.effectManager->effectList;
					for (const auto& effect : effects)
					{
						if (effect->user.lock() == actor)
						{
							carried = carried || effect->carryUserActive;
							if (std::find(previousEffects.begin(), previousEffects.end(), effect) == previousEffects.end())
							{
								if (firstRelease < 0) { firstRelease = elapsed; firstMagic = effect->magic.iniName; }
								released = true;
							}
						}
						CoreLifecycleTestAccess::advanceActorFrame(*effect, 20);
					}
					CoreLifecycleTestAccess::advanceActorFrame(*gameManager.effectManager, 20);
					CoreLifecycleTestAccess::advanceActorFrame(*gameManager.npcManager, 20);
					if (released) ++releases;
					if (actor->getPosition() != previousActorPosition) ++actorMoves;
					if (gameManager.player->getPosition() != previousPlayerPosition) ++playerMoves;
					bounced = bounced || gameManager.player->bounceVelocity > 0;
				}
				const int damage = 100000000 - gameManager.player->life;
				std::cout << "Explicit combat:\tpack=" << fixture.pack << "\tsection=" << fixture.section << "\treloaded=" << reloaded
					<< "\tselected=" << selected << "\tforcedMiss=" << forcedMiss << "\tdistance=" << distance << "\tstart=" << start.x << ',' << start.y
					<< "\ttarget=" << destination.x << ',' << destination.y << "\treleases=" << releases << "\tfirstRelease=" << firstRelease
					<< "\tfirstMagic=" << firstMagic << "\tdamage=" << damage << "\tactorMoves=" << actorMoves
					<< "\tplayerMoves=" << playerMoves << "\tcarried=" << carried << "\tbounced=" << bounced
					<< "\tcollisions=" << target->collisions << "\tfirstCollisionMagic=" << target->firstCollisionMagic << std::endl;
				// Player hit rolls include zero even at zero evade. Observe actual
				// collision without replacing that legitimate random damage branch.
				ok = check(firstRelease > 0 && target->collisions > 0,
					"the full production NPC really releases and collides with a player before and after file readback") && ok;
				if (forcedMiss) ok = check(damage == 0, "the real charge collides but guaranteed evasion keeps player life unchanged") && ok;
				if (selected >= 0)
				{
					ok = check(firstMagic == actor->attackOptions[selected].magic->iniName && releases == 1
						&& target->firstCollisionMagic == firstMagic,
						"one selected normal attack releases its locked native or explicit magic exactly once") && ok;
					if (actor->attackOptions[selected].magic->carryUser == 2)
					{
						ok = check(carried && bounced && actorMoves > 0 && playerMoves > 0,
							"the real charge carries its caster and collision applies the configured bounce to the player") && ok;
					}
				}
				else ok = check(releases > 1 && damage > 0,
					"the unrestricted production scheduler repeatedly attacks and damages the player on the real map") && ok;
			}
		}
	}
	return ok;
}

bool runProductionAttackDistanceObservations()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	struct DistanceCase
	{
		std::string pack;
		std::string file;
		std::string section;
		int attackRadius;
		// Published stand-off distances only, not a shared selection algorithm:
		// Moonlight HD uses one NPC radius and chooses FlyIni2 at release (1/8).
		// New Sword and MG select from the closest UseDistance group before attack.
		std::vector<int> publishedDistances;
	};
	const DistanceCase cases[] = {
		{ "xjxqy", u8"ini/npc/npc011_方勉.ini", "Init", 2, { 2, 2 } },
		{ "yycs", "ini/save/map050_heart3.npc", "NPC000", 6, { 6, 6 } },
		{ u8"江湖余尘", u8"ini/save/tmx_map_066_比武台.npc", "NPC000", 15, { 1, 6, 10, 10, 10, 10, 20 } }
	};
	bool ok = true;
	for (const auto& fixture : cases)
	{
		if (fixture.pack == u8"江湖余尘" && std::getenv("JXQY_TEST_ARENA_DEPENDENCY_ROOT") == nullptr
			&& !std::filesystem::exists(assetsRoot / std::filesystem::u8path(fixture.pack) / "ini/magic/test01.ini"))
		{
			std::cout << "SKIP: optional isolated arena dependencies are absent for distance observations\n";
			continue;
		}
		ScopedActiveResourceRoot resourceRoot;
		SaveFileManager::CurrentPathScope currentPath("save/attack_distance_observations");
		if (!check(resourceRoot.valid() && currentPath.valid(), "attack-distance observations isolate all resource and save writes")) return false;
		std::vector<std::string> roots = { (assetsRoot / std::filesystem::u8path(fixture.pack)).u8string() };
		if (fixture.pack == u8"江湖余尘")
		{
			if (const char* dependencies = std::getenv("JXQY_TEST_ARENA_DEPENDENCY_ROOT")) roots.insert(roots.begin(), dependencies);
			roots.push_back((assetsRoot / "yycs").u8string());
		}
		roots.push_back((assetsRoot / "common").u8string());
		File::setResourceFallbackRoots(roots);
		GameManager gameManager;
		ResourceManifest manifest;
		if (!check(manifest.loadFromFile("game_profile.ini"), "load the real attack-distance resource profile")) return false;
		gameManager.global.applyResourceManifestFeatures(manifest);
		gameManager.global.data.NPCAI = false;
		gameManager.map->data = std::make_shared<MapData>();
		gameManager.map->data->head.width = gameManager.map->data->head.height = 240;
		gameManager.map->data->tile.assign(240, std::vector<MapTile>(240));
		gameManager.map->createDataMap();
		std::unique_ptr<char[]> bytes;
		if (!check(File::readFile(fixture.file, bytes) > 0, "read the actual distance actor definition")) return false;
		INIReader definition(bytes);
		auto actor = std::make_shared<NPC>();
		actor->initFromIni(&definition, fixture.section);
		actor->setPosition({ 100, 100 }, false);
		gameManager.npcManager->addNPC(actor);
		// These definitions begin neutral. Activate only the test combat condition
		// to observe the real planner; this does not prove the story entrance.
		actor->relation = nrHostile;
		actor->isAIDisabled = true;
		gameManager.player->life = 1000;
		if (!check(actor->attackRadius == fixture.attackRadius && actor->attackOptions.size() == fixture.publishedDistances.size(),
			"the production actor loads its complete original attack list and NPC radius"))
		{
			ok = false;
			continue;
		}
		for (size_t index = 0; index < actor->attackOptions.size(); ++index)
		{
			const auto& option = actor->attackOptions[index];
			std::ostringstream observation;
			observation << "Attack distance option: pack=" << fixture.pack << " index=" << index
				<< " file=" << option.magic->iniName << " published=" << fixture.publishedDistances[index]
				<< " configured=" << option.configuredUseDistance << " explicit=" << option.hasExplicitUseDistance
				<< " effective=" << actor->calcEffectiveUseDistance(option)
				<< " physical=" << actor->estimatePhysicalReach(*option.magic, actor->getClampedAttackLevel())
				<< " move=" << option.moveKind;
			std::cout << observation.str() << std::endl;
		}
		// Observe the current algorithm rather than making its differences from
		// the published nearest-distance rule into a required behavior.
		for (bool fullLife : { true, false })
		{
			actor->life = fullLife ? actor->getLifeMax() : actor->getLifeMax() / 2;
			for (int distance : { 1, 2, 3, 4, 5, 6, 8, 10, 12, 15, 16, 18, 20, 22 })
			{
				actor->actionManager->restartActionIgnoringTransitions(acStand);
				actor->setPosition({ 100, 100 }, false);
				actor->clearCombatTargetMemory();
				actor->idledFrame = actor->idle;
				Point destination = actor->getPosition();
				for (int step = 0; step < distance; ++step) destination = Map::getSubPoint(destination, 0);
				gameManager.player->setPosition(destination, false);
				ok = check(gameManager.map->calDistance(actor->getPosition(), destination) == distance,
					"the observation lane uses actual map distance, not pixel or coordinate assumptions") && ok;
				int nearest = fixture.publishedDistances.front();
				for (int candidate : fixture.publishedDistances)
				{
					if (std::abs(candidate - distance) < std::abs(nearest - distance)) nearest = candidate;
				}
				const auto candidates = actor->buildAttackCandidates(destination);
				const bool planned = actor->evaluateAndPlan(gameManager.player);
				const int selected = planned && actor->actionPlan.hasSelectedOption ? actor->actionPlan.selectedOption.sourceIndex : -1;
				const bool executed = planned && actor->executeActionPlan(gameManager.player);
				std::ostringstream observation;
				observation << "Attack distance choices: pack=" << fixture.pack << " fullLife=" << fullLife << " distance=" << distance
					<< " publishedGroup=" << nearest << " canSee=" << actor->canSee(destination)
					<< " selected=" << selected << " executed=" << executed << " action=" << static_cast<int>(actor->nowAction)
					<< " ready=";
				for (const auto& candidate : candidates)
				{
					if (candidate.canHitNow) observation << candidate.option.sourceIndex << ',';
				}
				std::cout << observation.str() << std::endl;
			}
		}
		// Advance the real action scheduler and effects, without repeatedly forcing
		// plans or attacks. The blank arena isolates movement from story activation.
		for (bool fullLife : { true, false })
		{
			for (const std::string scenario : { "near-open", "near-blocked", "approach-open", "approach-blocked", "lose-return", "outside-vision" })
			{
				gameManager.effectManager->freeResource();
				gameManager.npcManager->freeResource();
				for (auto& row : gameManager.map->data->tile)
				{
					for (auto& tile : row) tile.obstacle = 0;
				}
				gameManager.map->createDataMap();
				actor = std::make_shared<NPC>();
				actor->initFromIni(&definition, fixture.section);
				gameManager.npcManager->addNPC(actor);
				actor->setPosition({ 100, 100 }, false);
				actor->relation = nrHostile;
				actor->life = fullLife ? actor->getLifeMax() : actor->getLifeMax() / 2;
				actor->isAIDisabled = false;
				gameManager.global.data.NPCAI = true;
				gameManager.player->actionManager->restartActionIgnoringTransitions(acStand);
				gameManager.player->info.lifeMax = gameManager.player->life = 100000000;
				const bool blocked = scenario.find("blocked") != std::string::npos;
				const int initialDistance = scenario.find("near") == 0 ? 1
					: (scenario == "outside-vision" ? actor->visionRadius + 2 : fixture.attackRadius + 3);
				Point destination = actor->getPosition();
				for (int step = 0; step < initialDistance; ++step) destination = Map::getSubPoint(destination, 0);
				gameManager.player->setPosition(destination, false);
				if (blocked)
				{
					for (int direction = 0; direction < 8; ++direction)
					{
						const Point neighbor = Map::getSubPoint(actor->getPosition(), direction);
						// Block walking, but retain line of sight and projectile passage.
						gameManager.map->data->tile[neighbor.y][neighbor.x].obstacle = 0x40;
						ok = check(!gameManager.map->canWalk(neighbor) && gameManager.map->canFly(neighbor),
							"the movement barrier blocks walking without blocking projectiles") && ok;
					}
				}
				ok = check(actor->kind == nkBattle && actor->isAIEnabled()
					&& actor->attackOptions.size() == fixture.publishedDistances.size(),
					"continuous combat retains the actual battle actor and all production attacks") && ok;
				int firstRelease = -1, moves = 0, attacks = 0, releases = 0, furthestDistance = initialDistance;
				int lostDistance = -1, lostReleases = 0, returnedReleases = 0;
				std::string firstMagic;
				// Leave enough time after return for the longest retreat and random
				// production animation; do not impose a four-second reaction deadline.
				const int duration = scenario == "lose-return" ? 16000 : 10000;
				for (int elapsed = 20; elapsed <= duration; elapsed += 20)
				{
					if (scenario == "lose-return" && elapsed == 1000) gameManager.player->setPosition({ 160, 160 }, false);
					if (scenario == "lose-return" && elapsed == 6000)
					{
						lostDistance = gameManager.map->calDistance(actor->getPosition(), destination);
						gameManager.player->setPosition(destination, false);
					}
					const Point previousPosition = actor->getPosition();
					const bool wasAttacking = actor->isAttacking();
					const auto previousEffects = gameManager.effectManager->effectList;
					CoreLifecycleTestAccess::advanceActorFrame(*gameManager.player, 20);
					const auto actors = gameManager.npcManager->npcList;
					for (const auto& activeActor : actors) CoreLifecycleTestAccess::advanceActorFrame(*activeActor, 20);
					if (actor->getPosition() != previousPosition) ++moves;
					if (!wasAttacking && actor->isAttacking()) ++attacks;
					furthestDistance = std::max(furthestDistance, gameManager.map->calDistance(actor->getPosition(), destination));
					const auto effects = gameManager.effectManager->effectList;
					bool released = false;
					for (const auto& effect : effects)
					{
						if (effect->user.lock() == actor && std::find(previousEffects.begin(), previousEffects.end(), effect) == previousEffects.end())
						{
							if (firstRelease < 0) { firstRelease = elapsed; firstMagic = effect->magic.iniName; }
							released = true;
						}
						CoreLifecycleTestAccess::advanceActorFrame(*effect, 20);
					}
					CoreLifecycleTestAccess::advanceActorFrame(*gameManager.effectManager, 20);
					CoreLifecycleTestAccess::advanceActorFrame(*gameManager.npcManager, 20);
					if (released)
					{
						++releases;
						if (scenario == "lose-return" && elapsed >= 1000 && elapsed < 6000) ++lostReleases;
						if (scenario == "lose-return" && elapsed >= 6000) ++returnedReleases;
					}
				}
				if (scenario == "near-open" || scenario == "near-blocked" || scenario == "approach-open")
				{
					ok = check(firstRelease > 0 && releases > 1,
						"a visible reachable target receives repeated real releases, not just successful plans") && ok;
				}
				if (scenario == "lose-return")
				{
					ok = check(returnedReleases > 0,
						"the actual scheduler reacquires and releases after the player returns to the last visible location") && ok;
				}
				if (scenario == "outside-vision")
				{
					ok = check(firstRelease == -1 && attacks == 0,
						"a never-seen distant player does not create a combat target or an attack") && ok;
				}
				if (blocked) ok = check(moves == 0, "continuous movement respects the closed walk-only barrier") && ok;
				std::ostringstream motion;
				motion << "Attack motion: pack=" << fixture.pack << " fullLife=" << fullLife << " scenario=" << scenario
					<< " duration=" << duration << " initial=" << initialDistance << " final=" << gameManager.map->calDistance(actor->getPosition(), destination)
					<< " furthest=" << furthestDistance << " moves=" << moves << " attacks=" << attacks << " releases=" << releases
					<< " firstRelease=" << firstRelease << " firstMagic=" << (firstMagic.empty() ? "none" : firstMagic)
					<< " damage=" << 100000000 - gameManager.player->life << " lostDistance=" << lostDistance
					<< " lostReleases=" << lostReleases << " returnedReleases=" << returnedReleases << " action=" << static_cast<int>(actor->nowAction);
				std::cout << motion.str() << std::endl;
				if (fixture.pack == u8"江湖余尘" && scenario == "approach-blocked")
				{
					ok = check(firstRelease > 0 && firstMagic == u8"wd_120_召唤.ini" && moves == 0
						&& actor->attackRadius == fixture.attackRadius,
						"the original arena radius and explicit summon distance allow casting when retreat is blocked") && ok;
				}
			}
		}
	}
	return ok;
}

bool runProductionAttackFileContracts()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	bool ok = true;
	int totalCalls = 0;
	for (const auto& pack : {std::string("xjxqy"), std::string("yycs"), std::string(u8"江湖余尘"), std::string(u8"江湖余尘二")})
	{
		const auto packRoot = assetsRoot / std::filesystem::u8path(pack);
		if (!std::filesystem::exists(packRoot / "game_profile.ini"))
		{
			std::cout << "SKIP: optional production attack-file pack is absent: " << pack << '\n';
			continue;
		}
		ScopedActiveResourceRoot resourceRoot;
		SaveFileManager::CurrentPathScope currentPath("save/attack_file_contracts");
		if (!check(resourceRoot.valid() && currentPath.valid(), "attack-file contracts isolate resource and save writes"))
		{
			return false;
		}
		File::setResourceFallbackRoots({packRoot.u8string()});
		GameManager gameManager;
		ResourceManifest manifest;
		ok = check(manifest.loadFromFile("game_profile.ini"), "read the production attack-file profile") && ok;
		gameManager.global.applyResourceManifestFeatures(manifest);
		gameManager.global.data.NPCAI = false;
		gameManager.global.data.canInput = false;
		auto first = std::make_shared<NPC>();
		auto duplicate = std::make_shared<NPC>();
		first->kind = duplicate->kind = nkNormal;
		first->life = duplicate->life = 100;
		gameManager.npcManager->npcList = {first, duplicate};
		const auto execute = [&](const std::string& source)
		{
			auto bytes = std::make_unique<char[]>(source.size());
			std::copy(source.begin(), source.end(), bytes.get());
			return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
		};
		const auto validAttack = [](const NPC& npc, const std::string& file)
		{
			return npc.flyIni == file && npc.npcMagic && npc.npcMagic->loadSucceeded
				&& std::any_of(npc.attackOptions.begin(), npc.attackOptions.end(), [&](const auto& option)
					{ return option.magic == npc.npcMagic; });
		};
		int packCalls = 0;
		std::string lastMagic;
		// The audited calls are literal two-string statements. Execute their exact
		// current text in controlled target topologies, not the surrounding cutscene.
		for (const auto& entry : std::filesystem::recursive_directory_iterator(packRoot / "script"))
		{
			if (!entry.is_regular_file() || entry.path().extension() != ".txt") continue;
			std::ifstream input(entry.path(), std::ios::binary);
			for (std::string line; std::getline(input, line); )
			{
				const auto start = line.find_first_not_of(" \t");
				if (start == std::string::npos || line.find("setnpcmagicfile(\"", start) != start) continue;
				const auto fields = convert::splitString(line, "\"");
				if (!check(fields.size() == 5, "production SetNpcMagicFile retains its audited two-string shape"))
				{
					ok = false;
					continue;
				}
				++packCalls;
				lastMagic = fields[3];
				first->npcName = duplicate->npcName = gameManager.global.resolveScriptCharacterName(fields[1]);
				gameManager.player->npcName = "unrelated-attack-player";
				first->flyIni.clear();
				first->npcMagic.reset();
				first->rebuildAttackOptions();
				duplicate->flyIni = "unchanged-duplicate.ini";
				ok = check(execute(line) == LUA_OK && validAttack(*first, lastMagic)
					&& duplicate->flyIni == "unchanged-duplicate.ini" && !gameManager.global.data.canInput,
					"the actual attack-file line loads a usable magic for only the first matching NPC") && ok;
				gameManager.player->npcName = first->npcName;
				first->flyIni = "unchanged-first.ini";
				ok = check(execute(line) == LUA_OK && validAttack(*gameManager.player, lastMagic)
					&& first->flyIni == "unchanged-first.ini" && duplicate->flyIni == "unchanged-duplicate.ini",
					"the same actual line prefers the named player over identically named NPCs") && ok;
			}
		}
		totalCalls += packCalls;
		ok = check(packCalls == (pack == "xjxqy" ? 20 : 6), "all audited production attack-file calls execute") && ok;
		if (lastMagic.empty()) continue;
		first->npcName = duplicate->npcName = gameManager.player->npcName = "AttackTarget";
		gameManager.scriptNPC = first;
		ok = check(execute("setnpcmagicfile('" + lastMagic + "'); setnpcmagicfile();") == LUA_OK
			&& validAttack(*first, lastMagic) && duplicate->flyIni == "unchanged-duplicate.ini",
			"one-argument SetNpcMagicFile uses its owner and missing arguments leave it unchanged") && ok;
		ok = check(execute("setnpcmagicfile('');") == LUA_OK && first->flyIni.empty()
			&& !first->npcMagic && first->attackOptions.empty(), "explicit empty attack file clears the primary magic") && ok;
		ok = check(execute("setnpcmagicfile('missing-attack-contract.ini');") == LUA_OK
			&& first->flyIni == "missing-attack-contract.ini" && first->attackOptions.empty(),
			"missing attack resources do not become usable attack options or abort the script") && ok;
		const std::string secondary = u8"player-magic-长剑.ini";
		ok = check(execute("changeflyini('AttackTarget','" + lastMagic + "'); "
			"changeflyini2('AttackTarget','" + secondary + "'); "
			"addflyinis('AttackTarget','" + lastMagic + "',5); "
			"addflyinis('AttackTarget','" + lastMagic + "',5);") == LUA_OK,
			"the three batch attack APIs execute through the real Lua registrations") && ok;
		const auto validBatch = [&](const NPC& npc)
		{
			return validAttack(npc, lastMagic) && npc.flyIni2 == secondary && npc.npcMagic2 && npc.npcMagic2->loadSucceeded
				&& npc.attackOptions.size() == 4 && npc.flyInis == lastMagic + ":5;" + lastMagic + ":5;"
				&& npc.attackOptions[2].configuredUseDistance == 5 && npc.attackOptions[3].configuredUseDistance == 5;
		};
		ok = check(validBatch(*first) && validBatch(*duplicate) && gameManager.player->flyIni == lastMagic
			&& gameManager.player->flyIni2.empty() && gameManager.player->flyInis.empty(),
			"batch APIs change all matching NPCs, retain repeated entries and exclude the identically named player") && ok;
		ok = check(gameManager.npcManager->save("attacks.npc") && gameManager.player->save(0),
			"save changed NPC attack lists and the player's primary magic") && ok;
		gameManager.scriptNPC.reset();
		gameManager.npcManager->clearNPC(true);
		gameManager.player = std::make_shared<Player>();
		ok = check(gameManager.player->load(0) && gameManager.npcManager->load("attacks.npc"),
			"reload attack files into newly created Player and NPC instances") && ok;
		const auto& loaded = gameManager.npcManager->npcList;
		ok = check(loaded.size() == 2 && loaded[0] != first && loaded[1] != duplicate
			&& validBatch(*loaded[0]) && validBatch(*loaded[1]) && validAttack(*gameManager.player, lastMagic),
			"FlyIni, FlyIni2 and repeated FlyInis survive file readback and rebuild usable attack options") && ok;
		std::cout << "Production attack-file calls checked: " << pack << " " << packCalls << std::endl;
	}
	std::cout << "Total production attack-file calls checked: " << totalCalls << std::endl;
	return ok;
}

bool runProductionScriptAttackContracts()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	bool ok = true;
	for (const auto& pack : {std::string("xjxqy"), std::string("yycs"), std::string(u8"江湖余尘"), std::string(u8"江湖余尘二")})
	{
		const auto packRoot = assetsRoot / std::filesystem::u8path(pack);
		if (!std::filesystem::exists(packRoot / "game_profile.ini"))
		{
			std::cout << "SKIP: optional production script-attack pack is absent: " << pack << '\n';
			continue;
		}
		for (bool playerActor : {false, true})
		{
			ScopedActiveResourceRoot resourceRoot;
			SaveFileManager::CurrentPathScope currentPath("save/script_attack_contracts");
			if (!check(resourceRoot.valid() && currentPath.valid(), "script attacks isolate resource and save writes")) return false;
			std::vector<std::string> fallbackRoots = {packRoot.u8string()};
			if (pack == u8"江湖余尘" || pack == u8"江湖余尘二")
			{
				fallbackRoots.push_back((assetsRoot / "yycs").u8string());
			}
			fallbackRoots.push_back((assetsRoot / "common").u8string());
			File::setResourceFallbackRoots(fallbackRoots);
			GameManager gameManager;
			ResourceManifest manifest;
			ok = check(manifest.loadFromFile("game_profile.ini"), "read actual script-attack profile") && ok;
			gameManager.global.applyResourceManifestFeatures(manifest);
			gameManager.global.data.NPCAI = false;
			gameManager.global.data.canInput = false;
			gameManager.varList.ensureInitialized();
			gameManager.map->data = std::make_shared<MapData>();
			gameManager.map->data->head.width = gameManager.map->data->head.height = 200;
			gameManager.map->data->tile.assign(200, std::vector<MapTile>(200));
			gameManager.map->createDataMap();
			ok = check(gameManager.player->loadInitialTemplate(0), "read the actual protagonist and action resources") && ok;
			std::shared_ptr<NPC> actor = gameManager.player;
			if (!playerActor)
			{
				std::unique_ptr<char[]> bytes;
				if (!check(File::readFile("ini/save/player0.ini", bytes) > 0, "read an actual actor template for the NPC execution path")) return false;
				INIReader definition(bytes);
				actor = std::make_shared<NPC>();
				actor->initFromIni(&definition, "Init");
				actor->kind = nkNormal;
				gameManager.npcManager->npcList.push_back(actor);
				gameManager.player->npcName = "unrelated-script-attack-player";
			}
			const bool newSword = pack == "xjxqy";
			const std::string initialAttackMagic = actor->flyIni;
			const std::string name = newSword ? u8"独孤剑" : (pack == u8"江湖余尘" ? "#name" : u8"杨影枫");
			actor->npcName = gameManager.global.resolveScriptCharacterName(name);
			gameManager.scriptNPC = actor;
			const Point position = newSword ? Point{34,14} : Point{24,104};
			const Point destination = newSword ? Point{35,20} : Point{25,100};
			const std::string magicFile = newSword ? u8"magic001_衡山有雪.ini" : u8"player-magic-云生结海.ini";
			const std::string setMagic = "setnpcmagicfile(\"" + name + "\",\"" + magicFile + "\");";
			const std::string attack = "npcattack(\"" + name + "\"," + std::to_string(destination.x) + "," + std::to_string(destination.y) + ");";
			std::unique_ptr<char[]> scriptBytes;
			const auto scriptFile = newSword ? u8"script/map/map001_衡山/begin.txt" : u8"script/map/map_033_落叶谷/trap07.txt";
			const int length = File::readFile(scriptFile, scriptBytes);
			ok = check(length > 0 && std::string(scriptBytes.get(), length).find(setMagic) != std::string::npos
				&& std::string(scriptBytes.get(), length).find(attack) != std::string::npos,
				"the selected magic replacement and attack are verbatim production statements") && ok;
			const auto execute = [&](const std::string& source)
			{
				auto bytes = std::make_unique<char[]>(source.size());
				std::copy(source.begin(), source.end(), bytes.get());
				return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
			};
			std::shared_ptr<Magic> linkedSpecialMagic;
			bool checkedLinkedAttack = false;
			int attempts = playerActor && !newSword ? 18 : 9;
			for (int attempt = 0; attempt < attempts; ++attempt)
			{
				const bool withPractice = attempt >= 9;
				if (attempt == 9)
				{
					auto practiceMagic = gameManager.magicManager.loadAttackMagic(magicFile);
					if (!check(practiceMagic && practiceMagic->loadSucceeded,
						"load the actual practice magic before checking its linked attack")) return false;
					linkedSpecialMagic = practiceMagic->getLinkedLevel(actor->attackLevel).specialMagic;
					if (!check(pack == "yycs" ? linkedSpecialMagic && linkedSpecialMagic->loadSucceeded : !linkedSpecialMagic,
						"moonlight has the linked cloud attack while the two MOD overrides do not")) return false;
					const int practiceIndex = gameManager.magicManager.practiceIndex();
					gameManager.magicManager.magicList.resize(static_cast<size_t>(practiceIndex + 1));
					auto& practice = gameManager.magicManager.magicList[practiceIndex];
					practice.magic = practiceMagic;
					practice.iniFile = magicFile;
					practice.level = actor->attackLevel;
				}
				const bool ownerArguments = attempt % 9 == 8;
				actor->setPosition(position, false);
				actor->actionManager->restartActionIgnoringTransitions(acStand);
				actor->thew = actor->thewMax;
				gameManager.map->createDataMap();
				gameManager.effectManager->freeResource();
				gameManager.varList.setInteger("AfterScriptAttack", 0);
				const std::string command = ownerArguments
					? "npcattack(" + std::to_string(destination.x) + "," + std::to_string(destination.y) + ");" : attack;
				const std::string primaryMagic = withPractice ? initialAttackMagic : magicFile;
				const std::string setPrimaryMagic = withPractice
					? "setnpcmagicfile(\"" + name + "\",\"" + primaryMagic + "\");" : setMagic;
				const int result = execute(setPrimaryMagic + command + "assign('AfterScriptAttack',1);");
				const bool started = actor->isAttacking();
				ok = check(result == LUA_OK && started && actor->actionLastTime > 1
					&& gameManager.effectManager->effectList.empty() && !gameManager.global.data.canInput
					&& gameManager.varList.getInteger("AfterScriptAttack") == 1,
					(ownerArguments ? "two-argument owner attack starts the same real non-blocking animation"
					: "actual named attack starts a non-blocking animation before producing its magic")) && ok;
				if (!started) continue;
				const UTime duration = actor->actionLastTime;
				const auto attackAction = actor->nowAction;
				const auto attackRevision = actor->actionManager->getActionRevision();
				const int attackThew = actor->thew;
				const int attackDirection = actor->direction;
				const Point pendingDestination = playerActor ? gameManager.player->magicDest : actor->attackDest;
				ok = check(execute("npcattack(\"" + name + "\",100,80);") == LUA_OK
					&& actor->actionManager->getActionRevision() == attackRevision
					&& actor->thew == attackThew && actor->direction == attackDirection
					&& (playerActor ? gameManager.player->magicDest : actor->attackDest) == pendingDestination,
					"a rejected second attack preserves stamina, facing and the in-flight destination") && ok;
				const bool expectLinkedAttack = withPractice && linkedSpecialMagic
					&& (attackAction == acAttack2 || attackAction == acSpecialAttack);
				checkedLinkedAttack = checkedLinkedAttack || expectLinkedAttack;
				// Ordinary attacks retain the prepared magic; the explicit native
				// protocol instead reads this replacement at its last frame.
				const std::string replacementMagic = withPractice ? magicFile : u8"player-magic-长剑.ini";
				const std::string expectedPrimaryMagic = !playerActor && gameManager.global.feature.nativeNpcAttackAtAnimationEnd
					? replacementMagic : primaryMagic;
				ok = check(execute("setnpcmagicfile(\"" + name + "\",\"" + replacementMagic + "\");") == LUA_OK,
					"change the primary magic after this attack has begun") && ok;
				CoreLifecycleTestAccess::advanceActorFrame(*actor, duration - 1);
				ok = check(actor->isAttacking() && gameManager.effectManager->effectList.empty(),
					"the prepared magic is not released before the animation ends") && ok;
				CoreLifecycleTestAccess::advanceActorFrame(*actor, 1);
				const auto count = gameManager.effectManager->effectList.size();
				const auto hasEffect = [&](const std::string& file)
				{
					return std::any_of(gameManager.effectManager->effectList.begin(), gameManager.effectManager->effectList.end(),
						[&](const auto& effect) { return effect && effect->magic.iniName == file; });
				};
				ok = check(actor->isStanding() && count > 0
					&& hasEffect(expectedPrimaryMagic) && (!expectLinkedAttack || hasEffect(linkedSpecialMagic->iniName))
					&& std::all_of(gameManager.effectManager->effectList.begin(), gameManager.effectManager->effectList.end(),
						[&](const auto& effect) { return effect && effect->user.lock() == actor
							&& (effect->magic.iniName == expectedPrimaryMagic || (expectLinkedAttack && effect->magic.iniName == linkedSpecialMagic->iniName)); }),
					"the action releases the exact primary required by its resource protocol plus any linked attack before returning to stand") && ok;
				CoreLifecycleTestAccess::advanceActorFrame(*actor, duration + 100);
				ok = check(gameManager.effectManager->effectList.size() == count,
					"the same completed attack does not release again on the next update") && ok;
				std::cout << "Production script attack checked: " << pack << (playerActor ? " Player " : " NPC ")
					<< (ownerArguments ? "owner" : "named") << " action=" << static_cast<int>(attackAction)
					<< " practice=" << withPractice << " effects=" << count;
				for (const auto& effect : gameManager.effectManager->effectList)
				{
					std::cout << " " << effect->magic.iniName;
				}
				std::cout << std::endl;
				// Retain real random action selection, but require evidence for the linked branch.
				if (attempt + 1 == attempts && linkedSpecialMagic && !checkedLinkedAttack && attempts < 64)
				{
					++attempts;
				}
			}
			ok = check(!linkedSpecialMagic || checkedLinkedAttack, "the production linked AttackFile branch was actually exercised") && ok;
			gameManager.effectManager->freeResource();
			gameManager.scriptNPC.reset();
			const auto actionRevision = actor->actionManager->getActionRevision();
			ok = check(execute("npcattack(25,100);npcattack();npcattack(25);npcattack('missing-script-attack-target',25,100);") == LUA_OK
				&& actor->isStanding() && actor->actionManager->getActionRevision() == actionRevision
				&& gameManager.effectManager->effectList.empty(),
				"missing owners, missing named targets and incomplete attack parameters do not start an action") && ok;
		}
	}
	return ok;
}

bool runProductionScriptMagicContracts()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	bool ok = true;
	for (const auto& pack : {std::string("xjxqy"), std::string("yycs"), std::string(u8"潇湘行")})
	{
		ScopedActiveResourceRoot resourceRoot;
		SaveFileManager::CurrentPathScope currentPath("save/script_magic_contracts");
		if (!check(resourceRoot.valid() && currentPath.valid(), "script magic isolates resource and save writes")) return false;
		std::vector<std::string> roots = {(assetsRoot / std::filesystem::u8path(pack)).u8string()};
		if (pack == u8"潇湘行")
		{
			roots.push_back((assetsRoot / "jxqy2").u8string());
			roots.push_back((assetsRoot / "yycs").u8string());
		}
		roots.push_back((assetsRoot / "common").u8string());
		File::setResourceFallbackRoots(roots);
		GameManager gameManager;
		ResourceManifest manifest;
		if (!check(manifest.loadFromFile("game_profile.ini"), "load the production script-magic profile")) return false;
		gameManager.global.applyResourceManifestFeatures(manifest);
		gameManager.global.data.NPCAI = false;
		gameManager.global.data.canInput = false;
		gameManager.varList.ensureInitialized();
		gameManager.map->data = std::make_shared<MapData>();
		gameManager.map->data->head.width = gameManager.map->data->head.height = 200;
		gameManager.map->data->tile.assign(200, std::vector<MapTile>(200));
		gameManager.map->createDataMap();
		if (!check(gameManager.player->loadInitialTemplate(0), "load the actual protagonist for script magic")) return false;
		auto player = gameManager.player;
		player->npcName = "ScriptMagicPlayer";
		const std::string file = pack == "xjxqy" ? u8"magic001_衡山有雪.ini" : u8"player-magic-云生结海.ini";
		auto* learned = gameManager.magicManager.addPrimaryMagic(file, false, false);
		if (!check(learned && learned->magic && learned->magic->loadSucceeded, "learn an actual production magic outside the toolbar")) return false;
		learned->level = 1;
		auto sourceMagic = learned->magic;
		// Keep real animation/effect resources, but make costs and cooldown observable.
		sourceMagic->level[1].manaCost = 5;
		sourceMagic->level[1].thewCost = 3;
		sourceMagic->level[1].lifeCost = 2;
		sourceMagic->coldMilliSeconds = 10000;
		const auto execute = [&](const std::string& source)
		{
			auto bytes = std::make_unique<char[]>(source.size());
			std::copy(source.begin(), source.end(), bytes.get());
			return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
		};
		const std::string cast = "usemagic('" + file + "',25,20);";
		const auto reset = [&]()
		{
			player->actionManager->restartActionIgnoringTransitions(acStand);
			player->clearAbnormalState();
			player->immobilized = player->petrified = false;
			player->disableSkillMilliseconds = 0;
			player->canFight = player->canUseMana = true;
			player->setPosition({24,24}, false);
			player->magicIndex = -1;
			player->info.lifeMax = player->info.manaMax = player->info.thewMax = 100;
			player->life = player->mana = player->thew = 100;
			gameManager.magicManager.findPrimaryMagic(file)->remainColdMilliseconds = 0;
			gameManager.effectManager->freeResource();
		};
		reset();
		ok = check(execute(cast + "assign('AfterScriptMagic',1);") == LUA_OK && player->nowAction == acMagic
			&& player->magicIndex == -1 && player->actionLastTime > 1 && player->mana == 100
			&& player->thew == 100 && player->life == 100 && gameManager.effectManager->effectList.empty()
			&& gameManager.varList.getInteger("AfterScriptMagic") == 1 && !gameManager.global.data.canInput,
			"script magic starts a non-blocking real animation without selecting a toolbar slot or paying early") && ok;
		if (player->nowAction == acMagic)
		{
			const auto revision = player->actionManager->getActionRevision();
			const int direction = player->direction;
			ok = check(execute("usemagic('" + file + "',70,90);") == LUA_OK
				&& player->actionManager->getActionRevision() == revision && player->direction == direction
				&& player->magicDest == Point{25,20} && player->preparedMagicAction == sourceMagic,
				"a busy script cast cannot overwrite the already prepared destination or magic") && ok;
			const UTime duration = player->actionLastTime;
			const UTime trigger = gameManager.global.feature.magicTriggerAtAnimationEnd
				? duration : std::min<UTime>(duration, PLAYER_MAGIC_DELAY);
			CoreLifecycleTestAccess::advanceActorFrame(*player, trigger - 1);
			ok = check(player->mana == 100 && gameManager.effectManager->effectList.empty(), "no script magic cost or effect before its configured release frame") && ok;
			CoreLifecycleTestAccess::advanceActorFrame(*player, 1);
			const auto count = gameManager.effectManager->effectList.size();
			ok = check(count > 0 && player->mana == 95 && player->thew == 97 && player->life == 98
				&& gameManager.magicManager.findPrimaryMagic(file)->remainColdMilliseconds == 10000,
				"the actual release consumes each cost once and starts the source magic cooldown") && ok;
			CoreLifecycleTestAccess::advanceActorFrame(*player, duration + 1);
			ok = check(gameManager.effectManager->effectList.size() == count && player->mana == 95,
				"finishing the cast does not release or charge again") && ok;
			const auto finishedRevision = player->actionManager->getActionRevision();
			ok = check(execute(cast) == LUA_OK && player->actionManager->getActionRevision() == finishedRevision
				&& player->mana == 95, "the existing script cooldown gate remains effective") && ok;
		}
		reset();
		player->canFight = false;
		ok = check(execute(cast) == LUA_OK && player->nowAction == acMagic,
			"disabling player combat input does not prohibit an explicit story-script cast") && ok;
		for (int blocked = 1; blocked < 8; ++blocked)
		{
			reset();
			if (blocked == 1) player->canUseMana = false;
			if (blocked == 2) player->immobilized = true;
			if (blocked == 3) player->petrified = true;
			if (blocked == 4) player->disableSkillMilliseconds = 1000;
			if (blocked >= 5) player->actionManager->restartActionIgnoringTransitions(
				blocked == 5 ? acAttack : (blocked == 6 ? acHurt : acDeath));
			const auto revision = player->actionManager->getActionRevision();
			const int direction = player->direction;
			ok = check(execute(cast) == LUA_OK && player->actionManager->getActionRevision() == revision
				&& player->direction == direction && player->mana == 100 && player->thew == 100
				&& gameManager.effectManager->effectList.empty(), "disabled or busy actors reject script magic without side effects") && ok;
		}
		reset();
		ok = check(execute("usemagic();usemagic('unlearned-script-magic.ini');") == LUA_OK
			&& player->isStanding() && player->mana == 100 && gameManager.effectManager->effectList.empty(),
			"missing arguments and unlearned magic remain safe no-ops") && ok;
		for (bool extraArgument : {false, true})
		{
			reset();
			player->direction = 2;
			const Point front = Map::getSubPoint(player->getPosition(), player->direction);
			ok = check(execute("usemagic('" + file + (extraArgument ? "',999);" : "');")) == LUA_OK
				&& player->nowAction == acMagic && player->preparedMagicActionDest == front,
				"one-argument and legacy two-argument script casts both target the front tile") && ok;
		}
		reset();
		const auto actionImage = player->res.magic.imagePackage;
		player->res.magic.imagePackage.reset();
		ok = check(execute(cast) == LUA_OK && player->isStanding() && player->mana == 100
			&& gameManager.effectManager->effectList.empty(), "missing actor cast animation cannot release ordinary script magic") && ok;
		player->res.magic.imagePackage = actionImage;
		const auto useActionImage = sourceMagic->useActionImage;
		sourceMagic->useActionImage = std::make_shared<IMPImage>();
		sourceMagic->useActionImage->directions = 1;
		Point unsupported = player->getPosition();
		for (int direction = 0; direction < 8; ++direction)
		{
			const Point point = Map::getSubPoint(player->getPosition(), direction);
			if (player->getDirection(point) != 0) { unsupported = point; break; }
		}
		ok = check(execute("usemagic('" + file + "'," + std::to_string(unsupported.x) + "," + std::to_string(unsupported.y) + ");") == LUA_OK
			&& player->isStanding() && player->mana == 100 && gameManager.effectManager->effectList.empty(),
			"a spell action with one direction cannot cast toward an unsupported direction") && ok;
		sourceMagic->useActionImage = useActionImage;
		const auto originalLevel = sourceMagic->level[1];
		sourceMagic->level[1].moveKind = mmkSelf;
		sourceMagic->level[1].specialKind = mskInvisibleVisibleWhenAttack;
		sourceMagic->level[1].effect = 10000;
		for (bool atEnd : {false, true})
		{
			reset();
			const bool originalTrigger = gameManager.global.feature.magicTriggerAtAnimationEnd;
			gameManager.global.feature.magicTriggerAtAnimationEnd = atEnd;
			player->applyMagicInvisibility(10000, true);
			ok = check(execute(cast) == LUA_OK && player->nowAction == acMagic,
				"prepare an invisibility spell while an earlier reveal-on-action invisibility is active") && ok;
			const UTime duration = player->actionLastTime;
			const UTime trigger = atEnd ? duration : std::min<UTime>(duration, PLAYER_MAGIC_DELAY);
			CoreLifecycleTestAccess::advanceActorFrame(*player, trigger);
			ok = check(player->invisibleMilliseconds == 10000 && player->isVisibleWhenAttack,
				"releasing the spell clears old invisibility before granting the new invisibility") && ok;
			CoreLifecycleTestAccess::advanceActorFrame(*player, duration + 1);
			ok = check(player->invisibleMilliseconds > 0 && player->isVisibleWhenAttack,
				"ending the casting animation does not erase newly granted invisibility") && ok;
			gameManager.global.feature.magicTriggerAtAnimationEnd = originalTrigger;
		}
		sourceMagic->level[1] = originalLevel;
		const int originalFullLife = sourceMagic->lifeFullToUse;
		const int originalDisabled = sourceMagic->disableUse;
		const bool originalTrigger = gameManager.global.feature.magicTriggerAtAnimationEnd;
		sourceMagic->lifeFullToUse = 1;
		// Inject admission flags into this isolated in-memory skill, retaining
		// the production protagonist animation and magic effect resources.
		for (bool atEnd : { false, true })
		{
			gameManager.global.feature.magicTriggerAtAnimationEnd = atEnd;
			for (int disabled : { 1, -1 })
			{
				reset();
				sourceMagic->disableUse = disabled;
				player->life = 99;
				ok = check(execute(cast) == LUA_OK && player->isStanding() && player->mana == 100,
					"full-life script magic rejects a new under-full-life cast without costs") && ok;
				player->life = 100;
				ok = check(execute(cast) == LUA_OK && player->nowAction == acMagic && player->actionLastTime > 1,
					"DisableUse does not block explicit script casting admitted at full life") && ok;
				if (player->nowAction != acMagic || player->actionLastTime <= 1) continue;
				const UTime duration = player->actionLastTime;
				const UTime trigger = atEnd ? duration : std::min<UTime>(duration, PLAYER_MAGIC_DELAY);
				player->life = 90;
				CoreLifecycleTestAccess::advanceActorFrame(*player, trigger - 1);
				ok = check(player->mana == 100 && player->life == 90 && gameManager.effectManager->effectList.empty(),
					"losing life mid-animation neither charges early nor releases early") && ok;
				CoreLifecycleTestAccess::advanceActorFrame(*player, 1);
				const auto count = gameManager.effectManager->effectList.size();
				ok = check(count > 0 && player->mana == 95 && player->thew == 97 && player->life == 88,
					"an admitted full-life cast survives mid-animation life loss and releases after LifeCost") && ok;
				CoreLifecycleTestAccess::advanceActorFrame(*player, duration + 1);
				ok = check(gameManager.effectManager->effectList.size() == count && player->mana == 95 && player->life == 88,
					"full-life admission does not cause duplicate release or payment at animation end") && ok;
			}
		}
		sourceMagic->disableUse = originalDisabled;
		sourceMagic->lifeFullToUse = originalFullLife;
		gameManager.global.feature.magicTriggerAtAnimationEnd = originalTrigger;
		for (bool interrupt : {false, true})
		{
			reset();
			ok = check(execute(cast) == LUA_OK && player->nowAction == acMagic, "prepare a cast before release-time cancellation or cost failure") && ok;
			if (interrupt) player->actionManager->forceChangeAction(acStand);
			else player->mana = 0;
			CoreLifecycleTestAccess::advanceActorFrame(*player, player->actionLastTime + 1);
			ok = check(gameManager.effectManager->effectList.empty() && player->thew == 100 && player->life == 100
				&& gameManager.magicManager.findPrimaryMagic(file)->remainColdMilliseconds == 0,
				"interruption or insufficient mana at release produces no effect, other cost or cooldown") && ok;
		}
		reset();
		gameManager.magicManager.replaceMagicList(u8"player-magic-长剑.ini");
		player->info.lifeMax = player->info.manaMax = player->info.thewMax = 100;
		player->life = player->mana = player->thew = 100;
		ok = check(execute(cast) == LUA_OK && player->nowAction == acMagic,
			"script magic can animate a learned primary magic while a replacement list is active") && ok;
		gameManager.magicManager.stopReplaceMagicList();
		CoreLifecycleTestAccess::advanceActorFrame(*player, player->actionLastTime + 1);
		ok = check(!gameManager.effectManager->effectList.empty()
			&& gameManager.magicManager.findPrimaryMagic(file)->remainColdMilliseconds == 10000,
			"restoring the primary list during animation keeps cooldown attached to the cast magic") && ok;
		reset();
		gameManager.magicManager.replaceMagicList(u8"player-magic-长剑.ini");
		auto* replacement = gameManager.magicManager.findMagic(u8"player-magic-长剑.ini");
		if (!check(replacement && replacement->magic && replacement->magic->loadSucceeded, "load a real replacement-list toolbar magic")) return false;
		replacement->remainColdMilliseconds = 0;
		replacement->magic->coldMilliSeconds = 700;
		player->magicIndex = 0;
		player->canFight = false;
		player->beginMagic({25,20});
		ok = check(player->isStanding(), "combat input restrictions still block normal toolbar casting") && ok;
		player->canFight = true;
		const int replacementDisabled = replacement->magic->disableUse;
		// Replacing the list recalculates and clamps the protagonist's attributes.
		// Compare with the actual post-replacement values, not reset()'s maxima.
		const int manualMana = player->mana;
		const int manualLife = player->life;
		const int manualThew = player->thew;
		for (int disabled : { 1, -1 })
		{
			replacement->magic->disableUse = disabled;
			player->beginMagic({25,20});
			ok = check(player->isStanding() && player->mana == manualMana && player->life == manualLife
				&& player->thew == manualThew && gameManager.effectManager->effectList.empty(),
				"manual toolbar casting rejects any nonzero source DisableUse without animation, cost or effect") && ok;
		}
		replacement->magic->disableUse = replacementDisabled;
		player->beginMagic({25,20});
		ok = check(player->nowAction == acMagic, "normal toolbar casting shares the real animation path") && ok;
		gameManager.magicManager.stopReplaceMagicList();
		CoreLifecycleTestAccess::advanceActorFrame(*player, player->actionLastTime + 1);
		ok = check(gameManager.magicManager.findPrimaryMagic(file)->remainColdMilliseconds == 0,
			"a replacement-list cast never writes its cooldown into the restored primary list") && ok;
		gameManager.magicManager.replaceMagicList(u8"player-magic-长剑.ini");
		ok = check(gameManager.magicManager.findMagic(u8"player-magic-长剑.ini")->remainColdMilliseconds == 700,
			"a completed toolbar cast updates its own cached list even after that list was hidden") && ok;
		gameManager.magicManager.stopReplaceMagicList();
		reset();
		if (pack == u8"潇湘行")
		{
			auto* cure = gameManager.magicManager.addPrimaryMagic(u8"001燕归.ini", false, false);
			if (!check(cure && cure->magic && cure->magic->loadSucceeded, "load Xiaoxiang's actual instant abnormal-state cure")) return false;
			cure->level = 1;
			player->petrified = player->immobilized = true;
			player->disableSkillMilliseconds = 1000;
			const auto revision = player->actionManager->getActionRevision();
			ok = check(execute(u8"usemagic('001燕归.ini');") == LUA_OK && !player->petrified && !player->immobilized
				&& player->actionManager->getActionRevision() == revision && player->thew == 88,
				"actual YanGui cures disabled states immediately without waiting for a cast animation") && ok;
		}
		reset();
		gameManager.magicManager.clearPrimaryMagicList();
		player->canFight = false;
		player->mana = 0;
		ok = check(execute("npcusemagic('ScriptMagicPlayer','" + file + "',25,20,1);") == LUA_OK
			&& player->isStanding() && player->mana == 0 && !gameManager.effectManager->effectList.empty(),
			"NpcUseMagic remains a direct file-based extension without learned slots, animation or player costs") && ok;
		std::cout << "Production script magic checked: " << pack << std::endl;
	}
	return ok;
}

bool runProductionDialogueTextTests()
{
	const auto packRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
		"assets" / std::filesystem::u8path(u8"江湖余尘");
	if (!std::filesystem::exists(packRoot / "game_profile.ini"))
	{
		std::cout << "SKIP: optional production Yuchen dialogue pack is absent\n";
		return true;
	}
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid() && writeVirtualFile(HEAD_FILE_NAME,
		"[PORTRAIT]\n2=player.imp\n32=qiangwei.imp\n42=meng.imp\n94=scholar.imp\n"),
		"dialogue text tests create an isolated root and portrait lookup"))
	{
		return false;
	}
	GameManager gameManager;
	gameManager.varList.ensureInitialized();
	gameManager.player->npcName = u8"杨影枫";
	auto dialog = std::make_shared<RecordingDialog>();
	gameManager.menu->dialog = dialog;
	const auto execute = [&](const std::string& source)
	{
		auto bytes = std::make_unique<char[]>(source.size());
		std::copy(source.begin(), source.end(), bytes.get());
		return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
	};
	struct DialogueCase
	{
		const char* path;
		const char* fragment;
		const char* expectedText;
		const char* expectedPortrait;
	};
	const DialogueCase cases[] = {
		{ u8"map_033_落叶谷/trap09.txt", u8"唉……正所谓",
			u8"孟知秋: 唉……正所谓人在江湖，身不由己。", "meng.imp" },
		{ u8"map_033_落叶谷/家丁小白对话.txt", u8"听说《武道德经》",
			u8"小白: 听说《武道德经》是一本武学奇书，公子有没有看过？", "" },
		{ u8"map_033_落叶谷/家丁小白对话.txt", u8"\"#name\",\"……\",2,0",
			u8"杨影枫: ……", "player.imp" },
		{ u8"map_033_落叶谷(破坏后)/蔷薇对话.txt", u8"听说是一种西域奇毒",
			u8"蔷薇: 听说是一种西域奇毒，名叫穿肠雪蛛毒……", "qiangwei.imp" },
		{ u8"map_034_天池/王炜对话.txt", u8"物莫大于天地日月",
			u8"王炜: 物莫大于天地日月，而子美云“日月笼中鸟，乾坤水上萍。”这是为什么呢？", "" },
		{ u8"map_050_忘忧岛/忘忧岛书生对话.txt", u8"我师父临走时",
			u8"书生: 我师父临走时给我留下一联，让我对其下联，可是我一直想不出来。上联是<enter>一群游鱼逐波去。", "scholar.imp" },
		{ u8"map_050_忘忧岛/忘忧岛书生对话.txt", u8"你的上联是",
			u8"杨影枫: 你的上联是<enter>一群游鱼逐波去。", "player.imp" },
		{ u8"map_050_忘忧岛/忘忧岛书生对话.txt", u8"我的下联是",
			u8"杨影枫: 我的下联是", "player.imp" }
	};
	bool ok = true;
	for (const auto& test : cases)
	{
		std::ifstream input(packRoot / "script/map" / std::filesystem::u8path(test.path), std::ios::binary);
		const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		ok = check(!contents.empty() && writeVirtualFile(std::string("script/map/") + test.path, contents),
			"copy the actual production dialogue file") && ok;
		int matches = 0;
		for (const auto& line : convert::splitString(contents, "\n"))
		{
			if (line.find("say(") == std::string::npos || line.find(test.fragment) == std::string::npos)
			{
				continue;
			}
			++matches;
			dialog->entries.clear();
			const std::pair<std::string, std::string> expected{ test.expectedText, test.expectedPortrait };
			ok = check(execute(line) == LUA_OK && dialog->entries.size() == 1 && dialog->entries.front() == expected,
				"actual production Say keeps the complete text, speaker and portrait") && ok;
		}
		ok = check(matches > 0, "the expected production Say call is present") && ok;
	}
	// Run the whole scholar script as well. Only the choice interaction is
	// supplied by the test; Say, Memo and variable updates use the real runtime.
	CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, "select", [](lua_State* state)
	{
		gm->varList.setInteger(luaL_checkstring(state, 4), 0);
		return 0;
	});
	gameManager.varList.setInteger("Event", 420);
	gameManager.varList.setInteger("SubEvent20", 0);
	dialog->entries.clear();
	ok = check(gameManager.script.runScript(u8"script/map/map_050_忘忧岛/忘忧岛书生对话.txt") == LUA_OK &&
		gameManager.varList.getInteger("SubEvent20") == 2 &&
		std::find(dialog->entries.begin(), dialog->entries.end(),
			std::make_pair(std::string(cases[5].expectedText), std::string(cases[5].expectedPortrait))) != dialog->entries.end() &&
		!gameManager.memo.memo.empty(),
		"the actual scholar quest introduction displays the whole couplet and records the quest") && ok;
	gameManager.varList.setInteger("SubEvent20", 5);
	dialog->entries.clear();
	ok = check(gameManager.script.runScript(u8"script/map/map_050_忘忧岛/忘忧岛书生对话.txt") == LUA_OK &&
		gameManager.varList.getInteger("SubEvent20") == 10 &&
		std::find(dialog->entries.begin(), dialog->entries.end(),
			std::make_pair(std::string(cases[6].expectedText), std::string(cases[6].expectedPortrait))) != dialog->entries.end(),
		"the actual scholar answer branch preserves the displayed couplet and quest progression") && ok;
	return ok;
}

bool runProductionLegacyTalkTests()
{
	const auto packRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
		"assets" / std::filesystem::u8path(u8"剑二改承合版");
	if (!std::filesystem::exists(packRoot / "game_profile.ini"))
	{
		std::cout << "SKIP: optional production Chenghe dialogue pack is absent\n";
		return true;
	}
	ScopedActiveResourceRoot resourceRoot;
	SaveFileManager::CurrentPathScope currentPath("save\\legacy_talk_contracts");
	if (!check(resourceRoot.valid() && currentPath.valid(), "legacy Talk uses an isolated resource and save root"))
	{
		return false;
	}
	for (const char* path : { u8"script/map/长安/maptrap7.txt", u8"script/map/长安/talk.txt" })
	{
		std::ifstream input(packRoot / std::filesystem::u8path(path), std::ios::binary);
		const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		if (!check(!contents.empty() && writeVirtualFile(path, contents), "copy actual Chenghe trap and dialogue table"))
		{
			return false;
		}
	}
	const std::string expected = u8"飞云：飞云：接下来就是要去翠烟门了.....<enter>"
		u8"对了，上官豹那边，事情也该开始了吧，不如顺路过去看看好戏吧。";
	bool ok = true;
	for (int fightState : { 0, 1, 2 })
	{
		GameManager gameManager;
		gameManager.varList.ensureInitialized();
		gameManager.varList.setInteger("CAFight", fightState);
		gameManager.varList.setInteger("cafight", 91);
		gameManager.mapFolderName = u8"长安";
		gameManager.traps.set(gameManager.mapFolderName, 7, "maptrap7.txt");
		auto dialog = std::make_shared<RecordingDialog>();
		gameManager.menu->dialog = dialog;
		// Keep Talk, variables and trap dispatch real; omit staged motion and fades.
		for (const char* command : { "playergoto", "npcgoto", "npcgotoex", "movescreen", "sleep", "fadeout", "fadein" })
		{
			CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, command, [](lua_State*) { return 0; });
		}
		gameManager.runTrapScript(7);
		ok = check(!dialog->entries.empty() && dialog->entries.front().first == expected,
			"the actual ChangAn trap displays the complete SGBTS introduction in every battle state") && ok;
		ok = check(gameManager.varList.getInteger("CAFight") == (fightState == 0 ? 1 : fightState) &&
			gameManager.varList.getInteger("cafight") == 91 && !gameManager.inEvent &&
			gameManager.traps.get(gameManager.mapFolderName, 7) == "maptrap7.txt",
			"the actual ChangAn trap continues each branch without changing variable case or binding") && ok;
		if (fightState == 0)
		{
			dialog->entries.clear();
			gameManager.scriptAPI.talk("SGBTS");
			ok = check(dialog->entries.size() == 1 && dialog->entries.front().first == expected,
				"the actual SGBTS table preserves its second source line as a dialogue line break") && ok;
		}
	}
	return ok;
}

bool runProductionCaocaoDialogueTests()
{
	const auto packRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets/xjxqy";
	if (!std::filesystem::exists(packRoot / "game_profile.ini"))
	{
		std::cout << "SKIP: optional production Caocao dialogue pack is absent\n";
		return true;
	}
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid(), "Caocao uses an isolated resource root"))
	{
		return false;
	}
	for (const char* path : { u8"script/map/map100_天王岛/草草对话.txt", "ini/save/map100.npc",
		u8"ini/goods/goods412_一只护腕.ini", u8"ini/goods/goods413_另一只护腕.ini",
		u8"ini/goods/goods201_一年生草药.ini" })
	{
		std::ifstream input(packRoot / std::filesystem::u8path(path), std::ios::binary);
		const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		if (!check(!contents.empty() && writeVirtualFile(path, contents), "copy actual Caocao script, NPC list and quest goods"))
		{
			return false;
		}
	}
	bool ok = true;
	for (int scenario : { 0, 1, 2 })
	{
		SaveFileManager::CurrentPathScope currentPath("save\\caocao_contracts_" + std::to_string(scenario));
		if (!check(currentPath.valid(), "each Caocao branch has its own saved NPC list"))
		{
			return false;
		}
		const bool hasFirstBracelet = scenario != 0;
		const int initialMoney = scenario == 2 ? 500 : 2000;
		GameManager gameManager;
		gameManager.varList.ensureInitialized();
		gameManager.varList.setInteger("Saychake", 3);
		gameManager.varList.setInteger("Talkcaocao", 0);
		gameManager.varList.setInteger("talkcaocao", 92);
		gameManager.player->money = initialMoney;
		gameManager.mapFolderName = u8"map100_天王岛";
		auto dialog = std::make_shared<RecordingDialog>();
		gameManager.menu->dialog = dialog;
		for (const char* command : { "npcgoto", "fadeout", "fadein" })
		{
			CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, command, [](lua_State*) { return 0; });
		}
		if (!check(gameManager.scriptAPI.loadNPC("map100.npc") &&
			gameManager.goodsManager.addItem(u8"goods413_另一只护腕.ini", 1) &&
			(!hasFirstBracelet || gameManager.goodsManager.addItem(u8"goods412_一只护腕.ini", 1)),
			"load Caocao's production NPC and the selected quest items"))
		{
			return false;
		}
		auto actors = gameManager.npcManager->findNPC(u8"草草");
		if (!check(actors.size() == 1 && actors.front()->scriptFile == u8"草草对话.txt", "actual NPC binds the Caocao dialogue"))
		{
			return false;
		}
		gameManager.runNPCScript(actors.front());
		const auto numericDialogs = std::count_if(dialog->entries.begin(), dialog->entries.end(),
			[](const auto& entry) { return entry.first == "500"; });
		ok = check(numericDialogs == (hasFirstBracelet ? 1 : 0) &&
			gameManager.varList.getInteger("Talkcaocao") == (hasFirstBracelet ? 2 : 1) &&
			gameManager.varList.getInteger("talkcaocao") == 92 &&
			gameManager.player->money == std::max(0, initialMoney - (hasFirstBracelet ? 1000 : 0)) &&
			gameManager.goodsManager.getItemNum(u8"goods412_一只护腕.ini") == 0 &&
			gameManager.goodsManager.getItemNum(u8"goods413_另一只护腕.ini") == 0 &&
			gameManager.npcManager->findNPC(u8"草草").empty() == hasFirstBracelet,
			"published Say(500) displays literal text but both real bracelet branches still finish correctly") && ok;
		if (scenario == 2)
		{
			const char* medicine = u8"goods201_一年生草药.ini";
			ok = check(gameManager.goodsManager.addItem(medicine, 1),
				"stage one actual twenty-tael herb for the follow-up sale") && ok;
			gameManager.goodsManager.sellItem(medicine);
			ok = check(gameManager.player->money == 10 && gameManager.goodsManager.getItemNum(medicine) == 0 &&
				!gameManager.goodsManager.buyItem(medicine, 1) && gameManager.player->money == 10,
				"after the bracelet task a ten-tael sale is retained, but cannot buy a twenty-tael herb") && ok;
			gameManager.scriptAPI.addMoney(10);
			ok = check(gameManager.player->money == 20 && gameManager.goodsManager.buyItem(medicine, 1) &&
				gameManager.player->money == 0 && gameManager.goodsManager.getItemNum(medicine) == 1 &&
				gameManager.varList.getInteger("Talkcaocao") == 2,
				"new income enables the actual herb purchase without paying an unintended negative balance") && ok;
		}
	}
	return ok;
}

bool runPlayerAttributeScriptContracts()
{
	ScopedActiveResourceRoot resourceRoot;
	SaveFileManager::CurrentPathScope currentPath("save\\attribute_contracts");
	if (!check(resourceRoot.valid() && currentPath.valid(), "attribute contracts use isolated resource and save roots"))
	{
		return false;
	}
	GameManager gameManager;
	gameManager.varList.ensureInitialized();
	auto player = gameManager.player;
	player->npcName = "Hero";
	player->lifeMax = player->thewMax = player->manaMax = 100;
	player->attack = player->defend = player->evade = 10;
	player->attack2 = player->attack3 = player->defend2 = player->defend3 = 0;
	player->calInfo();
	player->life = player->thew = player->mana = 50;
	const auto execute = [&](const std::string& source)
	{
		auto bytes = std::make_unique<char[]>(source.size());
		std::copy(source.begin(), source.end(), bytes.get());
		return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
	};
	bool ok = check(gameManager.menu->stateMenu == nullptr && execute("addlife(20); addthew(20); addmana(20);") == LUA_OK &&
		player->life == 70 && player->thew == 70 && player->mana == 70,
		"real attribute APIs add to the player rather than a script owner");
	ok = check(execute("addthew(-1000); addmana(-1000);") == LUA_OK && player->thew == 0 && player->mana == 0,
		"depleting stamina and mana cannot create negative balances") && ok;
	ok = check(execute("addthew(5); addmana(5);") == LUA_OK && player->thew == 5 && player->mana == 5,
		"restoration after depletion is usable immediately rather than paying an invisible deficit") && ok;
	ok = check(execute("fulllife(); fullthew(); fullmana();") == LUA_OK &&
		player->life == 100 && player->thew == 100 && player->mana == 100,
		"the three full-attribute commands restore their computed maxima") && ok;
	ok = check(execute("addthew(2147483647); addmana(2147483647); addlife(2147483647);") == LUA_OK &&
		player->life == 100 && player->thew == 100 && player->mana == 100,
		"current-attribute additions saturate before clamping instead of overflowing") && ok;
	ok = check(execute("addattack(5); addattack(6,2); addattack(7,3); addattack(99,4);"
		"adddefend(-20); adddefend(8,2); adddefend(9,3); adddefend(99,4); addevade(-20);") == LUA_OK &&
		player->attack == 15 && player->attack2 == 6 && player->attack3 == 7 &&
		player->defend == 0 && player->defend2 == 8 && player->defend3 == 9 && player->evade == 0,
		"attack/defence types retain their independent fields and defence/evasion floor at zero") && ok;
	ok = check(execute("addlifemax(-1000); addthewmax(-1000); addmanamax(-1000);") == LUA_OK &&
		player->lifeMax == 1 && player->thewMax == 1 && player->manaMax == 1 &&
		player->life == 1 && player->thew == 1 && player->mana == 1,
		"maximum reductions keep at least one point and clamp the current values immediately") && ok;
	ok = check(execute("addlifemax(99); addthewmax(99); addmanamax(99);") == LUA_OK &&
		player->life == 1 && player->thew == 1 && player->mana == 1,
		"raising maxima does not silently restore current attributes") && ok;
	player->life = player->thew = player->mana = 10;
	auto magic = std::make_shared<Magic>();
	magic->level[1].manaCost = 3;
	ok = check(execute("limitmana(1);") == LUA_OK && !player->canUseMana &&
		!player->tryConsumeMagicCost(magic, 1, false) && player->mana == 10 &&
		execute("fullmana();") == LUA_OK && player->mana == 100 && !player->canUseMana,
		"LimitMana forbids casting even after mana is restored") && ok;
	ok = check(execute("limitmana(0);") == LUA_OK && player->canUseMana &&
		player->tryConsumeMagicCost(magic, 1, false) && player->mana == 97,
		"clearing LimitMana permits the ordinary spell cost path again") && ok;
	execute("limitmana(1);");
	const auto snapshot = [&]()
	{
		return std::vector<int>{ player->life, player->thew, player->mana,
			player->lifeMax, player->thewMax, player->manaMax,
			player->attack, player->attack2, player->attack3, player->defend, player->defend2, player->defend3,
			player->evade, player->canUseMana ? 1 : 0 };
	};
	const auto saved = snapshot();
	ok = check(player->save(0), "save the real attribute and CanUseMana fields") && ok;
	player->life = player->mana = player->thew = 99;
	player->attack = player->defend = 999;
	player->canUseMana = true;
	ok = check(player->load(0) && snapshot() == saved, "all reviewed attribute fields survive player-file save/load") && ok;
	player->calInfo();
	const auto savedMenu = gameManager.menu;
	gameManager.menu.reset();
	player->addThew(-1000);
	player->addMana(-1000);
	player->addLifeWithoutDeath(-1000);
	player->fullLife();
	player->fullThew();
	player->fullMana();
	ok = check(player->life == 100 && player->thew == 100 && player->mana == 100,
		"attribute data changes also work with no menu controller") && ok;
	gameManager.menu = savedMenu;
	gameManager.global.addLifeMode = ScriptAddLifeMode::DirectClamp;
	player->invincible = 10;
	ok = check(execute("addlife(-1000);") == LUA_OK && player->life == 0,
		"DirectClamp script damage depletes life even when invincibility is set") && ok;
	// This isolated root has no death animation. Stage the death state to test
	// revival without treating missing animation resources as a gameplay result.
	player->actionManager->resetActionIgnoringTransitions(acDeath);
	ok = check(execute("fulllife();") == LUA_OK && player->life == 100 && !player->isDying(),
		"FullLife restores life and exits a staged death state") && ok;
	gameManager.global.addLifeMode = ScriptAddLifeMode::PlayerRules;
	ok = check(execute("addlife(-1000);") == LUA_OK && player->life == 100,
		"PlayerRules script damage still respects invincibility") && ok;
	player->levelList.resize(3);
	for (int index = 0; index < 3; ++index)
	{
		auto& detail = player->levelList[index];
		detail.levelUpExp = index == 2 ? 0 : (index + 1) * 100;
		detail.lifeMax = detail.thewMax = detail.manaMax = (index + 1) * 100;
		detail.attack = (index + 1) * 10;
	}
	for (const auto threshold : { LevelUpThresholdMode::GreaterThan, LevelUpThresholdMode::GreaterThanOrEqual })
	{
		gameManager.global.levelUpThresholdMode = threshold;
		player->exp = 0;
		ok = check(execute("setnpclevel('Hero',1); addattack(13); addexp(100);") == LUA_OK &&
			player->level == (threshold == LevelUpThresholdMode::GreaterThan ? 1 : 2) && player->exp == 100,
			"AddExp uses the configured strict or inclusive first level threshold") && ok;
		ok = check(execute("addexp(101);") == LUA_OK && player->level == 3 && player->exp == 201 &&
			player->attack == 43 && player->life == 300 && player->thew == 300 && player->mana == 300 &&
			execute("addexp(999);") == LUA_OK && player->exp == 201,
			"multi-level experience preserves permanent bonuses, restores resources and stops at a zero threshold") && ok;
	}
	gameManager.global.levelUpThresholdMode = LevelUpThresholdMode::GreaterThan;
	player->exp = 0;
	ok = check(execute("setnpclevel('Hero',1); addexp(200);") == LUA_OK && player->level == 2,
		"characterization: C++ applies the strict comparison again at an exact later threshold") && ok;
	auto owner = std::make_shared<NPC>();
	owner->npcName = "Owner";
	owner->npcLevelList.resize(2);
	owner->npcLevelList[1].lifeMax = 123;
	owner->npcLevelList[1].thewMax = 45;
	owner->npcLevelList[1].manaMax = 67;
	owner->npcLevelList[1].attack = 89;
	gameManager.scriptNPC = owner;
	ok = check(execute("setnpclevel('',2);") == LUA_OK && owner->level == 2 && owner->life == 123 &&
		owner->thew == 45 && owner->mana == 67 && owner->attack == 89 && player->level == 2,
		"empty-name SetNpcLevel updates the script owner and its level-table attributes") && ok;
	gameManager.scriptNPC.reset();
	ok = check(execute("setnpclevel('',9); setnpclevel('Missing',9);") == LUA_OK && player->level == 2,
		"missing SetNpcLevel targets do not change the player") && ok;
	return ok;
}

bool runMoneyAndMessageScriptContracts()
{
	ScopedActiveResourceRoot resourceRoot;
	SaveFileManager::CurrentPathScope currentPath("save\\money_contracts");
	if (!check(resourceRoot.valid() && currentPath.valid() && writeVirtualFile("talkindex.txt", "[10,0]<color=Red>Indexed notice\n"),
		"money and message contracts create an isolated dialogue table"))
	{
		return false;
	}
	GameManager gameManager;
	gameManager.varList.ensureInitialized();
	const auto execute = [&](const std::string& source)
	{
		auto bytes = std::make_unique<char[]>(source.size());
		std::copy(source.begin(), source.end(), bytes.get());
		return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
	};
	bool ok = check(gameManager.menu->goodsMenu == nullptr && execute(
		"setmoneynum(100); addmoney(37); addrandmoney(5,5); getmoneynum();"
		"assign('cash',91); getmoneynum('Cash');") == LUA_OK &&
		gameManager.player->money == 142 && gameManager.varList.getInteger("MoneyNum") == 142 &&
		gameManager.varList.getInteger("Cash") == 142 && gameManager.varList.getInteger("cash") == 91,
		"real money APIs work before the inventory menu exists and preserve output name case");
	ok = check(execute("setmoney(10); addrandmoney(-2,-4);") == LUA_OK &&
		gameManager.player->money >= 6 && gameManager.player->money <= 8 &&
		execute("setmoney(0); addrandmoney(-2,-4);") == LUA_OK && gameManager.player->money == 0,
		"reversed random bounds remain inclusive while the resulting balance cannot fall below zero") && ok;
	ok = check(execute("setmoneynum(2147483647); addmoney(1);") == LUA_OK &&
		gameManager.player->money == std::numeric_limits<int>::max() &&
		execute("setmoneynum(-2147483648); addmoney(-1);") == LUA_OK &&
		gameManager.player->money == 0,
		"money writes saturate at zero and INT_MAX without signed overflow") && ok;
	const auto savedMenu = gameManager.menu;
	gameManager.menu.reset();
	gameManager.scriptAPI.setMoneyNum(10);
	gameManager.player->addMoney(-3);
	ok = check(gameManager.player->money == 7, "direct money updates do not require a menu controller") && ok;
	gameManager.menu = savedMenu;
	gameManager.player->setMoney(std::numeric_limits<std::int64_t>::min());
	ok = check(gameManager.player->money == 0, "wide money writes clamp the negative boundary") && ok;
	gameManager.player->setMoney(std::numeric_limits<std::int64_t>::max());
	ok = check(gameManager.player->money == std::numeric_limits<int>::max(),
		"wide sale and settlement totals clamp before narrowing to the stored money type") && ok;
	for (int savedMoney : { 0, -5, std::numeric_limits<int>::max() })
	{
		gameManager.player->money = savedMoney;
		if (!check(gameManager.player->save(0), "save the actual player money field"))
		{
			return false;
		}
		gameManager.player->money = 789;
		ok = check(gameManager.player->load(0) && gameManager.player->money == std::max(0, savedMoney),
			"player-file loading normalizes a negative money payload while preserving zero and INT_MAX") && ok;
	}

	auto messageBox = std::make_shared<MsgBox>();
	gameManager.menu->messageBox = messageBox;
	ok = check(execute("showmessage(10);") == LUA_OK && messageBox->currentMessage == "<color=Red>Indexed notice" &&
		messageBox->showinUTime == 3500 && messageBox->visible,
		"ShowMessage resolves its table index and forwards color markup to the normal overlay") && ok;
	ok = check(execute("showmessage(11);") == LUA_OK && messageBox->currentMessage == "<color=Red>Indexed notice",
		"missing ShowMessage indices leave the previous notification unchanged") && ok;
	ok = check(execute("showmessage('Literal notice');") == LUA_OK && messageBox->currentMessage == "Literal notice" &&
		execute("displaymessage(10);") == LUA_OK && messageBox->currentMessage == "10",
		"ShowMessage retains its literal extension while DisplayMessage does not treat numbers as indices") && ok;
	ok = check(execute("messagebox('alias one');") == LUA_OK && messageBox->currentMessage == "alias one" &&
		execute("message('alias two');") == LUA_OK && messageBox->currentMessage == "alias two",
		"literal notification aliases share DisplayMessage") && ok;
	auto scriptMessages = std::make_shared<SystemNotice>(SystemNotice::Mode::ScriptMessages);
	gameManager.menu->scriptMessages = scriptMessages;
	gameManager.menu->systemNotice = std::make_shared<SystemNotice>();
	gameManager.menu->showSystemNotice("engine notice", 7000);
	scriptMessages->setTime(100);
	ok = check(execute("showsystemmsg('<color=Green>first');") == LUA_OK &&
		execute("showsystemmessage('<color=Red>second',5000);") == LUA_OK &&
		CoreLifecycleTestAccess::scriptMessageCount(*scriptMessages) == 2 &&
		scriptMessages->currentMessage == "<color=Green>first\n<color=Red>second" &&
		messageBox->currentMessage == "alias two" &&
		gameManager.menu->systemNotice->currentMessage == u8"系统：engine notice",
		"system message aliases append independently without overwriting ordinary or engine notices") && ok;
	scriptMessages->setTime(1100);
	ok = check(execute("showsystemmsg('short',100);") == LUA_OK,
		"a later short message enters the same queue") && ok;
	scriptMessages->setTime(1200);
	CoreLifecycleTestAccess::update(*scriptMessages);
	ok = check(CoreLifecycleTestAccess::scriptMessageCount(*scriptMessages) == 2 &&
		scriptMessages->currentMessage == "<color=Green>first\n<color=Red>second",
		"expiry removes a later short message without removing earlier long messages") && ok;
	scriptMessages->setTime(3100);
	CoreLifecycleTestAccess::update(*scriptMessages);
	ok = check(scriptMessages->currentMessage == "<color=Red>second" && scriptMessages->visible,
		"the default 3000 ms duration expires independently at its boundary") && ok;
	ok = check(execute("showsystemmsg('expired',-1);") == LUA_OK,
		"negative system-message duration is clamped to zero") && ok;
	CoreLifecycleTestAccess::update(*scriptMessages);
	ok = check(scriptMessages->currentMessage == "<color=Red>second",
		"zero duration removes only its own entry on update") && ok;
	scriptMessages->setTime(5100);
	CoreLifecycleTestAccess::update(*scriptMessages);
	ok = check(!scriptMessages->visible && scriptMessages->currentMessage.empty(),
		"the last message expires without extending its original 5000 ms duration") && ok;
	for (int index = 0; index < 16; ++index)
	{
		scriptMessages->showMessage("notice-" + std::to_string(index), 5000);
	}
	ok = check(CoreLifecycleTestAccess::scriptMessageCount(*scriptMessages) == 15 &&
		scriptMessages->currentMessage.find("notice-0\n") == std::string::npos &&
		scriptMessages->currentMessage.find("notice-1\n") == 0,
		"the sixteenth message evicts only the oldest entry, matching the C# limit") && ok;
	scriptMessages->setTime(10);
	CoreLifecycleTestAccess::update(*scriptMessages);
	ok = check(CoreLifecycleTestAccess::scriptMessageCount(*scriptMessages) == 15,
		"a reset element clock does not underflow every message's lifetime") && ok;
	scriptMessages->dismiss();
	ok = check(CoreLifecycleTestAccess::scriptMessageCount(*scriptMessages) == 0 && !scriptMessages->visible,
		"dismiss clears both text and pending system messages") && ok;
	return ok;
}

bool runProductionMoneyGateTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	if (!std::filesystem::exists(assetsRoot / "yycs/game_profile.ini") ||
		!std::filesystem::exists(assetsRoot / std::filesystem::u8path(u8"江湖余尘/game_profile.ini")))
	{
		std::cout << "SKIP: optional production money-gate packs are absent\n";
		return true;
	}
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid(), "money-gate scripts use an isolated resource root"))
	{
		return false;
	}
	const char* pharmacyScript = u8"script/map/map_012_惠安镇/惠安镇药店老板对话.txt";
	const char* loanScript = u8"script/map/map_022_清平乡/阿牛再借钱.txt";
	for (const auto& inputFile : std::vector<std::pair<const char*, const char*>>{
		{ "yycs", pharmacyScript }, { "yycs", u8"ini/goods/goods-e13-金创药.ini" },
		{ "yycs", "talkindex.txt" }, { u8"江湖余尘", loanScript } })
	{
		std::ifstream input(assetsRoot / std::filesystem::u8path(inputFile.first) /
			std::filesystem::u8path(inputFile.second), std::ios::binary);
		const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		if (!check(!contents.empty() && writeVirtualFile(inputFile.second, contents),
			"copy actual pharmacy/loan scripts and the pharmacy reward/table"))
		{
			return false;
		}
	}
	bool ok = true;
	for (int balance : { 0, 999, 1000, 1001 })
	{
		GameManager gameManager;
		gameManager.varList.ensureInitialized();
		gameManager.talkTextList.load();
		gameManager.player->setMoney(balance);
		gameManager.varList.setInteger("Event", 402);
		gameManager.menu->dialog = std::make_shared<RecordingDialog>();
		CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, "playergoto", [](lua_State*) { return 0; });
		const bool affordable = balance >= 1000;
		ok = check(gameManager.script.runScript(pharmacyScript) == LUA_OK &&
			gameManager.player->money == balance - (affordable ? 1000 : 0) &&
			gameManager.varList.getInteger("Event") == (affordable ? 404 : 402) &&
			gameManager.goodsManager.getItemNum(u8"goods-e13-金创药.ini") == (affordable ? 1 : 0),
			"the actual YYCS pharmacy still refuses insufficient funds and advances only after a paid purchase") && ok;
	}
	for (int balance : { 99, 100 })
	{
		GameManager gameManager;
		gameManager.varList.ensureInitialized();
		gameManager.setAutomationHooksEnabled(true);
		gameManager.player->setMoney(balance);
		gameManager.menu->dialog = std::make_shared<RecordingDialog>();
		for (const char* command : { "fadeout", "fadein" })
		{
			CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, command, [](lua_State*) { return 0; });
		}
		CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, "chooseex", [](lua_State* state)
		{
			gm->varList.setInteger("__automation_choose_enabled", 1);
			gm->varList.setInteger("__automation_choose_selection", 0);
			return CoreLifecycleTestAccess::chooseExWithAutomation(state);
		});
		ok = check(gameManager.script.runScript(loanScript) == LUA_OK &&
			gameManager.player->money == (balance == 100 ? 0 : 99) &&
			gameManager.varList.getInteger("anfc") == (balance == 100 ? 1 : 0),
			"the actual Yuchen loan uses its funds check and quest variable rather than a negative player balance") && ok;
		if (balance == 100)
		{
			ok = check(gameManager.script.runScript(loanScript) == LUA_OK && gameManager.player->money == 5000,
				"the actual loan repayment still awards the full promised income after lending") && ok;
		}
	}
	return ok;
}

bool runMapThumbnailLoadingTests()
{
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid(), "thumbnail loading uses an isolated map and menu root")) return false;
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets/jxqy2";
	for (const char* file : { "mapthumbnail.menu.ini", "window.ini", "mapname.ini", "thumbnail.ini", "closebtn.ini" })
	{
		const std::string path = std::string("ini/ui/mapthumbnail/") + file;
		std::ifstream input(assetsRoot / path, std::ios::binary);
		const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		if (!check(!contents.empty() && writeVirtualFile(path, contents), "copy actual thumbnail menu definitions")) return false;
	}
	auto bytes = MapV3ContractFixture::build();
	constexpr size_t tileOffset = MapV3ContractFixture::HeaderLength + MapV3ContractFixture::MpcCount * MapV3ContractFixture::InfoLength;
	bytes.resize(tileOffset + 16 * 16 * MapV3ContractFixture::TileLength);
	std::fill(bytes.begin() + MapV3ContractFixture::BaseHeaderLength, bytes.end(), std::uint8_t{ 0 });
	MapV3ContractFixture::writeInt32(bytes, 64, 16 * 16 * MapV3ContractFixture::TileLength);
	MapV3ContractFixture::writeInt32(bytes, 68, 16);
	MapV3ContractFixture::writeInt32(bytes, 72, 16);
	if (!check(writeVirtualFile("map/thumbnail.map", std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size())) &&
		SDL_Init(0), "prepare a map and offscreen SDL rendering")) return false;
	auto surface = make_shared_surface(SDL_CreateSurface(800, 600, SDL_PIXELFORMAT_ARGB8888));
	SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface.get()) : nullptr;
	if (!check(renderer != nullptr, "create a software renderer without a window")) return false;
	auto previousRenderer = CoreLifecycleTestAccess::exchangeRenderer(renderer);
	bool ok = true;
	for (bool diamond : { false, true })
	{
		Map largeMap;
		largeMap.data = std::make_shared<MapData>();
		largeMap.data->head.width = 200;
		largeMap.data->head.height = 400;
		largeMap.data->tile.resize(400);
		for (auto& row : largeMap.data->tile)
		{
			row.resize(200);
		}
		largeMap.mapMpc = std::make_shared<MapMpc>();
		const int tileWidth = diamond ? TILE_WIDTH : 256;
		const int tileHeight = diamond ? TILE_HEIGHT : 256;
		std::vector<uint8_t> tilePixels(tileWidth * tileHeight * 4);
		for (std::size_t index = 0; index < tilePixels.size(); index += 4)
		{
			tilePixels[index] = 192;
			tilePixels[index + 1] = 80;
			tilePixels[index + 2] = 32;
			const int x = static_cast<int>(index / 4) % tileWidth;
			const int y = static_cast<int>(index / 4) / tileWidth;
			const bool inside = !diamond ||
				std::abs(2 * x + 1 - tileWidth) * tileHeight +
				std::abs(2 * y + 1 - tileHeight) * tileWidth <= tileWidth * tileHeight;
			tilePixels[index + 3] = inside ? 255 : 0;
		}
		largeMap.mapMpc->mpc[0].img = IMP::createIMPImageFromImage(
			Engine::getInstance()->createImageFromPixelData(tilePixels.data(), tileWidth, tileHeight));
		for (int row = 0; row < 400; row += diamond ? 1 : 16)
		{
			for (int column = 0; column < 200; column += diamond ? 1 : 4)
			{
				largeMap.data->tile[row][column].layer[0].mpc = 1;
			}
		}
		largeMap.generateThumbnail();
		auto image = IMP::loadImage(largeMap.getThumbnailImage(), 0);
		ok = check(image && SDL_SetRenderTarget(renderer, image.get()),
			"generate the large-map sampling regression image") && ok;
		auto pixels = make_shared_surface(SDL_RenderReadPixels(renderer, nullptr));
		SDL_SetRenderTarget(renderer, nullptr);
		int gaps = 0;
		for (int y = 20; pixels && y < 220; ++y)
		{
			for (int x = 20; x < 300; ++x)
			{
				Uint8 red = 0, green = 0, blue = 0, alpha = 0;
				if (!SDL_ReadSurfacePixel(pixels.get(), x, y, &red, &green, &blue, &alpha) ||
					alpha < (diamond ? 128 : 250) || red < 25 || green < 70 || blue < 170)
				{
					++gaps;
				}
			}
		}
		// Pixel-sized diamond boundaries may alias; periodic seams must not uncover 1% of the map.
		ok = check(pixels && (diamond ? gaps < 560 : gaps == 0),
			"downscaled square and diamond tiles preserve the continuous thumbnail interior") && ok;
	}
	{
		GameManager gameManager;
		gameManager.global.feature.menuResourceProfile = mrpDefault;
		ok = check(gameManager.scriptAPI.loadMap("thumbnail.map", false), "load the thumbnail map through the script entry") && ok;
		auto first = gameManager.map->getThumbnailImage();
		ok = check(first != nullptr, "map loading creates the thumbnail before a menu exists") && ok;
		MapThumbnailMenu menu;
		menu.setControllerVisible(true);
		auto container = menu.getComponentByName<ImageContainer>("thumbnailContainer");
		ok = check(container && container->impImage == first && gameManager.map->getThumbnailImage() == first,
			"first opening binds the image prepared by map loading") && ok;
		menu.setControllerVisible(false);
		menu.init();
		menu.setControllerVisible(true);
		container = menu.getComponentByName<ImageContainer>("thumbnailContainer");
		ok = check(container && container->impImage == first && gameManager.map->getThumbnailImage() == first,
			"rebuilding and reopening the menu reuse the map thumbnail") && ok;
		ok = check(gameManager.scriptAPI.loadMap("thumbnail.map", false) && gameManager.map->getThumbnailImage() != first,
			"reloading the same map name replaces its thumbnail") && ok;
		CoreLifecycleTestAccess::update(menu);
		ok = check(container && container->impImage == gameManager.map->getThumbnailImage(),
			"a visible menu follows same-name map reloads") && ok;
		first = gameManager.map->getThumbnailImage();
		Map::PreparedLoadCandidate candidate;
		bool prepared = false;
		std::thread worker([&]() { prepared = Map::prepareLoadCandidate("map/thumbnail.map", candidate); });
		worker.join();
		ok = check(prepared && first == gameManager.map->getThumbnailImage(),
			"worker preparation preserves the currently displayed thumbnail") && ok;
		auto renderTarget = Engine::getInstance()->createCanvasImage(32, 32);
		ok = check(Engine::getInstance()->setSharedImageAsRenderTarget(renderTarget),
			"select the caller's rendering target") && ok;
		const SDL_Rect viewport = { 1, 2, 20, 24 };
		const SDL_Rect clip = { 2, 3, 8, 9 };
		SDL_SetRenderScale(renderer, 1.25f, 0.75f);
		SDL_SetRenderViewport(renderer, &viewport);
		SDL_SetRenderClipRect(renderer, &clip);
		ok = check(
			gameManager.map->commitPreparedLoadCandidate(std::move(candidate)) &&
			gameManager.map->getThumbnailImage() != first && SDL_GetRenderTarget(renderer) == renderTarget.get(),
			"main-thread map commit prepares a new thumbnail and restores the rendering target") && ok;
		float scaleX = 0.0f;
		float scaleY = 0.0f;
		SDL_Rect restoredViewport;
		SDL_Rect restoredClip;
		ok = check(SDL_GetRenderScale(renderer, &scaleX, &scaleY) &&
			SDL_GetRenderViewport(renderer, &restoredViewport) &&
			SDL_GetRenderClipRect(renderer, &restoredClip) &&
			scaleX == 1.25f && scaleY == 0.75f &&
			std::memcmp(&viewport, &restoredViewport, sizeof(viewport)) == 0 &&
			std::memcmp(&clip, &restoredClip, sizeof(clip)) == 0,
			"thumbnail rendering preserves the caller's scale, viewport and clipping") && ok;
		SDL_SetRenderTarget(renderer, nullptr);
		first = gameManager.map->getThumbnailImage();
		ok = check(!gameManager.scriptAPI.loadMap("missing-thumbnail.map", false) && gameManager.map->getThumbnailImage() == first,
			"failed map preparation retains the active map thumbnail") && ok;
		CoreLifecycleTestAccess::exchangeRenderer(nullptr);
		ok = check(gameManager.scriptAPI.loadMap("thumbnail.map", false) && !gameManager.map->getThumbnailImage(),
			"unavailable rendering clears the old thumbnail while allowing map loading") && ok;
		CoreLifecycleTestAccess::exchangeRenderer(renderer);
		CoreLifecycleTestAccess::update(menu);
		menu.setControllerVisible(false);
		menu.setControllerVisible(true);
		ok = check(container && !container->impImage && !gameManager.map->getThumbnailImage(),
			"menu updates clear stale images and reopening does not retry generation") && ok;
		if (container)
		{
			container->rect = { 40, 40, 320, 240 };
			gameManager.player->setPosition({ 4, 8 }, false);
			auto friendly = std::make_shared<NPC>();
			friendly->relation = nrFriendly;
			friendly->setPosition({ 0, 0 }, false);
			auto hostile = std::make_shared<NPC>();
			hostile->relation = nrHostile;
			hostile->setPosition({ 15, 15 }, false);
			gameManager.npcManager->npcList = { friendly, hostile };
			Engine::getInstance()->renderClear(0, 0, 0, 255);
			CoreLifecycleTestAccess::drawSubtree(menu);
			auto pixels = make_shared_surface(SDL_RenderReadPixels(renderer, nullptr));
			Uint8 red = 0, green = 0, blue = 0, alpha = 0;
			ok = check(pixels && SDL_ReadSurfacePixel(pixels.get(), 120, 160, &red, &green, &blue, &alpha) &&
				green > 200 && red < 20 && blue < 20,
				"without a map image, the player is drawn at the map-coordinate position") && ok;
			ok = check(pixels && SDL_ReadSurfacePixel(pixels.get(), 40, 40, &red, &green, &blue, &alpha) &&
				green > 180 && blue > 200 && red < 20,
				"without a map image, a top-left NPC is not lost to edge feathering") && ok;
			ok = check(pixels && SDL_ReadSurfacePixel(pixels.get(), 350, 265, &red, &green, &blue, &alpha) &&
				red > 200 && green < 20 && blue < 20,
				"without a map image, the last map row and column still show their NPC") && ok;
			if (const char* directory = std::getenv("JXQY_MAP_THUMBNAIL_CAPTURE_DIR"))
			{
				std::filesystem::create_directories(directory);
				const auto path = std::filesystem::path(directory) / "markers-without-map.png";
				ok = check(pixels && IMG_SavePNG(pixels.get(), path.u8string().c_str()),
					"capture the actual menu after thumbnail creation failed") && ok;
			}
		}
	}
	if (const char* directory = std::getenv("JXQY_MAP_THUMBNAIL_CAPTURE_DIR"))
	{
		struct CaptureMap
		{
			const char* resource;
			const char* file;
			const char* output;
		};
		for (const auto& capture : {
			CaptureMap{ "jxqy2", u8"map/临安城.map", "jxqy2-linan.png" },
			CaptureMap{ "yycs", u8"map/map_012_惠安镇.map", "yycs-huian.png" },
			CaptureMap{ "xjxqy", u8"map/map016_临安城.map", "xjxqy-linan.png" }
		})
		{
			File::setActiveResourceRoot((assetsRoot.parent_path() / capture.resource).u8string());
			GameManager gameManager;
			const auto started = std::chrono::steady_clock::now();
			ok = check(gameManager.map->load(capture.file), "load a real large map for thumbnail inspection") && ok;
			auto image = IMP::loadImage(gameManager.map->getThumbnailImage(), 0);
			int width = 0, height = 0;
			ok = check(image && Engine::getInstance()->getImageSize(image, width, height) &&
				width == 320 && height == 240,
				"a real large map produces the fixed-size thumbnail") && ok;
			if (image && SDL_SetRenderTarget(renderer, image.get()))
			{
				auto pixels = make_shared_surface(SDL_RenderReadPixels(renderer, nullptr));
				SDL_SetRenderTarget(renderer, nullptr);
				const auto path = std::filesystem::path(directory) / capture.output;
				ok = check(pixels && IMG_SavePNG(pixels.get(), path.u8string().c_str()),
					"capture the runtime thumbnail from actual map resources") && ok;
			}
			const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - started).count();
			std::cout << "ThumbnailCapture\t" << capture.resource << "\tms=" << elapsed << '\n';
		}
	}
	CoreLifecycleTestAccess::exchangeRenderer(previousRenderer);
	SDL_DestroyRenderer(renderer);
	return ok;
}

bool runSaveFeedbackRenderingTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid() && writeVirtualFile("map/save-feedback.map", "save fixture"),
		"save feedback isolates its writable map and slots")) return false;
	File::setResourceFallbackRoots({ (assetsRoot / "jxqy2").u8string() });
	File::setUiResourceFallbackRoots({ (assetsRoot / "jxqy2").u8string() }, true,
		(assetsRoot / "common").u8string());
	const bool initializeTtf = TTF_WasInit() == 0;
	if (!check(SDL_Init(0) && (!initializeTtf || TTF_Init()), "initialize save feedback text rendering")) return false;
	auto surface = make_shared_surface(SDL_CreateSurface(800, 600, SDL_PIXELFORMAT_ARGB8888));
	SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface.get()) : nullptr;
	if (!check(renderer != nullptr, "create the save feedback software renderer")) return false;
	Engine* engine = Engine::getInstance();
	auto previousRenderer = CoreLifecycleTestAccess::exchangeRenderer(renderer);
	int previousWidth = 0, previousHeight = 0;
	CoreLifecycleTestAccess::getLogicalSize(previousWidth, previousHeight);
	CoreLifecycleTestAccess::setLogicalSize(800, 600);
	auto logicalScreen = engine->createCanvasImage();
	auto previousScreen = CoreLifecycleTestAccess::exchangeLogicalScreen(logicalScreen);
	engine->setFontName((assetsRoot / "engine/font/font.ttf").string());
	const bool previousTheme = Config::useQingyuUi;
	bool ok = check(logicalScreen != nullptr, "create the actual logical frame target");
	{
		class SaveFeedbackGame final : public GameManager
		{
		public:
			int drawCount = 0;
			int updateCount = 0;
			bool throwOnDraw = false;
		protected:
			void onDraw() override
			{
				++drawCount;
				if (throwOnDraw) throw std::runtime_error("save background fixture");
				engine->fillRect(0, 0, 800, 600, 29, 71, 113, 255);
			}
			void onUpdate() override { ++updateCount; }
		};
		SaveFeedbackGame game;
		game.menu->visible = false;
		game.weather->setLum(31);
		game.global.data.mapName = "save-feedback.map";
		game.varList.ensureInitialized();
		game.traps.beginMapVisit();
		const auto readImage = [&](const _shared_image& image)
		{
			const auto originalTarget = SDL_GetRenderTarget(renderer);
			const bool selected = image && SDL_SetRenderTarget(renderer, image.get());
			auto pixels = make_shared_surface(selected ? SDL_RenderReadPixels(renderer, nullptr) : nullptr);
			SDL_SetRenderTarget(renderer, originalTarget);
			return pixels;
		};
		const auto sameBackground = [&](const _shared_surface& expected)
		{
			auto actual = readImage(logicalScreen);
			// The loading label occupies the bottom-right 320 x 70 area.
			return expected && actual && expected->format == actual->format
				&& expected->pitch == actual->pitch && expected->h == actual->h
				&& std::memcmp(expected->pixels, actual->pixels,
					static_cast<std::size_t>(expected->pitch) * 500) == 0;
		};
		for (const bool qingyu : { false, true })
		{
			Config::useQingyuUi = qingyu;
			auto menu = std::make_shared<SaveLoad>(true, true);
			menu->setPriority(0);
			game.addChild(menu);
			ok = check(menu->saveBtn && menu->listBox && menu->rect.w > 0,
				"load the actual classic or Qingyu save menu") && ok;
			engine->frameBegin();
			auto background = CoreLifecycleTestAccess::captureSaveBackground(game);
			auto expected = readImage(background);
			Uint8 red = 0, green = 0, blue = 0, alpha = 0;
			ok = check(expected && SDL_ReadSurfacePixel(expected.get(), 4, 4, &red, &green, &blue, &alpha)
				&& red == 29 && green == 71 && blue == 113,
				"the captured background contains the scene outside the save menu") && ok;
			std::size_t menuPixels = 0;
			for (int y = 100; expected && y < 500; y += 4)
			{
				for (int x = 100; x < 700; x += 4)
				{
					if (SDL_ReadSurfacePixel(expected.get(), x, y, &red, &green, &blue, &alpha)
						&& (red != 29 || green != 71 || blue != 113)) ++menuPixels;
				}
			}
			ok = check(menuPixels > 100, "the static background includes actual save menu pixels") && ok;
			// Reproduce the clear performed when SaveLoad::run returns to System.
			engine->frameBegin();
			const int drawsBeforeSave = game.drawCount;
			ok = check(game.scriptAPI.saveGameWithFeedback(qingyu ? 2 : 1)
				&& sameBackground(expected) && game.drawCount == drawsBeforeSave + 1 && game.updateCount == 0,
				"manual saving redraws its cleared menu once and preserves it in the presented frame") && ok;
			auto savedPixels = readImage(logicalScreen);
			std::size_t statusPixels = 0;
			for (int y = 530; savedPixels && y < 600; ++y)
			{
				for (int x = 480; x < 800; ++x)
				{
					if (SDL_ReadSurfacePixel(savedPixels.get(), x, y, &red, &green, &blue, &alpha)
						&& red > 230 && green > 230 && blue > 230) ++statusPixels;
				}
			}
			ok = check(statusPixels > 20, "the saved frame overlays the real saving text on the retained background") && ok;
			const int drawsBeforeCheckpoint = game.drawCount;
			int loadingFrames = 0;
			const auto result = CoreLifecycleTestAccess::runExclusiveLoadingTask(game,
				[](const GameLoading::LoadingCancellationToken&) { return GameLoading::LoadingTaskResult::success(); },
				[&](const std::function<bool()>& checkpoint)
				{
					ok = check(sameBackground(expected), "the first loading frame retains the save menu") && ok;
					const int framesBeforeCheckpoint = loadingFrames;
					SDL_Delay(20);
					ok = check(checkpoint() && loadingFrames > framesBeforeCheckpoint && sameBackground(expected),
						"the owner checkpoint presents a second frame with the same save menu") && ok;
					return GameLoading::LoadingTaskResult::success();
				}, [&]() { ++loadingFrames; }, background);
			ok = check(result.succeeded() && game.drawCount == drawsBeforeCheckpoint && game.updateCount == 0,
				"save checkpoints copy the static image without scene draws or updates") && ok;
			game.removeChild(menu);
		}
		CoreLifecycleTestAccess::runExclusiveLoadingTask(game,
			[](const GameLoading::LoadingCancellationToken&) { return GameLoading::LoadingTaskResult::success(); });
		auto loadingPixels = readImage(logicalScreen);
		Uint8 red = 255, green = 255, blue = 255, alpha = 0;
		ok = check(loadingPixels && SDL_ReadSurfacePixel(loadingPixels.get(), 4, 4, &red, &green, &blue, &alpha)
			&& red == 0 && green == 0 && blue == 0, "ordinary loading retains its black background") && ok;
		engine->frameBegin();
		const auto targetBeforeFailure = engine->getRenderTarget();
		game.throwOnDraw = true;
		ok = check(!CoreLifecycleTestAccess::captureSaveBackground(game)
			&& engine->getRenderTarget() == targetBeforeFailure && game.scriptAPI.saveGameWithFeedback(3),
			"background draw failure restores the target and does not fail saving") && ok;
	}
	Config::useQingyuUi = previousTheme;
	engine->setFontName("");
	SDL_SetRenderTarget(renderer, nullptr);
	CoreLifecycleTestAccess::exchangeLogicalScreen(previousScreen);
	logicalScreen.reset();
	CoreLifecycleTestAccess::setLogicalSize(previousWidth, previousHeight);
	CoreLifecycleTestAccess::exchangeRenderer(previousRenderer);
	SDL_DestroyRenderer(renderer);
	if (initializeTtf) TTF_Quit();
	File::setResourceFallbackRoots({});
	File::setUiResourceFallbackRoots({});
	return ok;
}

bool runSystemMessageRenderingTests()
{
	const auto fontPath = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
		"assets/engine/font/font.ttf";
	std::ifstream input(fontPath, std::ios::binary);
	const std::string fontBytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid() && !fontBytes.empty() && writeVirtualFile("font/font.ttf", fontBytes),
		"system-message rendering uses the actual bundled font in an isolated root"))
	{
		return false;
	}
	const bool initializeTtf = TTF_WasInit() == 0;
	if (!check(SDL_Init(0) && (!initializeTtf || TTF_Init()), "initialize software text rendering"))
	{
		return false;
	}
	auto surface = make_shared_surface(SDL_CreateSurface(800, 600, SDL_PIXELFORMAT_ARGB8888));
	SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface.get()) : nullptr;
	bool ok = check(renderer != nullptr, "create an offscreen software renderer for message pixels");
	if (renderer != nullptr)
	{
		auto previousRenderer = CoreLifecycleTestAccess::exchangeRenderer(renderer);
		int previousWidth = 0;
		int previousHeight = 0;
		CoreLifecycleTestAccess::getLogicalSize(previousWidth, previousHeight);
		Engine* engine = Engine::getInstance();
		engine->setFontName(fontPath.string());
		for (const auto size : { Point{ 640, 480 }, Point{ 400, 360 }, Point{ 800, 600 } })
		{
			CoreLifecycleTestAccess::setLogicalSize(size.x, size.y);
			{
				GameManager gameManager;
				gameManager.player->setPosition({ 2, 3 }, false);
				gameManager.camera->position = { 20, 22 };
				gameManager.camera->offset = { 12.0f, -7.0f };
				int previousMouseX = 0, previousMouseY = 0;
				engine->getMousePosition(previousMouseX, previousMouseY);
				const auto capture = [&]()
				{
					SDL_Rect crop{ 0, 0, 240, 40 };
					auto pixels = make_shared_surface(SDL_RenderReadPixels(renderer, &crop));
					return pixels ? std::string(static_cast<const char*>(pixels->pixels),
						static_cast<std::size_t>(pixels->pitch) * pixels->h) : std::string();
				};
				const auto drawScene = [&](bool show)
				{
					SDL_SetRenderDrawColor(renderer, 24, 32, 40, 255);
					SDL_RenderClear(renderer);
					gameManager.scriptAPI.setShowMapPos(show ? 1 : 0);
					CoreLifecycleTestAccess::drawGameScene(gameManager);
				};
				for (const Point mouse : { Point{ size.x / 2, size.y / 2 }, Point{ size.x / 2 + 96, size.y / 2 + 48 } })
				{
					CoreLifecycleTestAccess::setMousePosition(mouse.x, mouse.y);
					const Point mapPoint = gameManager.getMousePoint();
					drawScene(false);
					const auto hidden = capture();
					engine->drawText(convert::formatString("Map: %d, %d", mapPoint.x, mapPoint.y), 8, 8, 18, 0xD0FFFFFF);
					const auto expected = capture();
					drawScene(true);
					const auto shown = capture();
					ok = check(mapPoint != gameManager.player->getPosition() && !hidden.empty() &&
						expected != hidden && shown == expected,
						"SetShowMapPos renders the cursor's map tile rather than the player's tile at each layout") && ok;
					gameManager.player->setPosition({ 7, 9 }, false);
					drawScene(true);
					ok = check(capture() == shown,
						"moving only the player cannot change the cursor-coordinate overlay") && ok;
				}
				CoreLifecycleTestAccess::setMousePosition(previousMouseX, previousMouseY);
			}
			SystemNotice messages(SystemNotice::Mode::ScriptMessages);
			SystemNotice warning;
			for (const char* message : { u8"<color=Red>攻击+5", u8"<color=Red>生命+10",
				u8"<color=Red>内力+10", u8"<color=Red>体力+10" })
			{
				messages.showMessage(message, 5000);
			}
			warning.showMessage(u8"系统：独立的引擎提示");
			const auto cachedImage = CoreLifecycleTestAccess::noticeTextImage(messages);
			int textWidth = 0;
			int textHeight = 0;
			ok = check(engine->getImageSize(cachedImage, textWidth, textHeight) &&
				textHeight == 4 * 21 && textWidth <= messages.rect.w && textHeight <= messages.rect.h,
				"four reward lines fit the real font texture in desktop and compact layouts") && ok;
			CoreLifecycleTestAccess::update(messages);
			ok = check(CoreLifecycleTestAccess::noticeTextImage(messages) == cachedImage,
				"unchanged message frames reuse the cached text texture") && ok;
			SDL_SetRenderDrawColor(renderer, 24, 32, 40, 255);
			SDL_RenderClear(renderer);
			CoreLifecycleTestAccess::draw(warning);
			CoreLifecycleTestAccess::draw(messages);
			auto pixels = make_shared_surface(SDL_RenderReadPixels(renderer, nullptr));
			const int firstY = messages.rect.y + messages.rect.h - textHeight;
			for (int line = 0; line < 4; ++line)
			{
				int redPixels = 0;
				for (int y = firstY + line * 21; pixels && y < firstY + (line + 1) * 21; ++y)
				{
					for (int x = messages.rect.x; x < messages.rect.x + textWidth; ++x)
					{
						Uint8 red = 0, green = 0, blue = 0, alpha = 0;
						if (SDL_ReadSurfacePixel(pixels.get(), x, y, &red, &green, &blue, &alpha) &&
							red > 150 && green < 60 && blue < 60)
						{
							++redPixels;
						}
					}
				}
				ok = check(redPixels > 20, "each of the four reward lines has visible red text pixels") && ok;
			}
			if (const char* artifactDirectory = std::getenv("JXQY_TEST_ARTIFACT_DIRECTORY"))
			{
				const auto directory = std::filesystem::u8path(artifactDirectory);
				std::filesystem::create_directories(directory);
				const auto path = directory / ("system-messages-" + std::to_string(size.x) + "x" + std::to_string(size.y) + ".png");
				SDL_Rect crop{ 0, 0, size.x, size.y };
				auto cropped = make_shared_surface(SDL_RenderReadPixels(renderer, &crop));
				ok = check(cropped && IMG_SavePNG(cropped.get(), path.string().c_str()),
					"save the optional composed system-message screenshot") && ok;
			}
			messages.dismiss();
			messages.showMessage("<color=Red><color=BeginRangeDefault>RED", 5000);
			messages.showMessage("DEFAULT", 5000);
			SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
			SDL_RenderClear(renderer);
			CoreLifecycleTestAccess::draw(messages);
			pixels = make_shared_surface(SDL_RenderReadPixels(renderer, nullptr));
			int defaultPixels = 0;
			for (int y = messages.rect.y + messages.rect.h - 21; pixels && y < messages.rect.y + messages.rect.h; ++y)
			{
				for (int x = messages.rect.x; x < messages.rect.x + 100; ++x)
				{
					Uint8 red = 0, green = 0, blue = 0, alpha = 0;
					if (SDL_ReadSurfacePixel(pixels.get(), x, y, &red, &green, &blue, &alpha) &&
						red > 150 && green > 150 && blue > 150)
					{
						++defaultPixels;
					}
				}
			}
			ok = check(defaultPixels > 20, "an unclosed red range cannot leak into the next message's default color") && ok;
		}
		CoreLifecycleTestAccess::setLogicalSize(previousWidth, previousHeight);
		engine->setFontName("");
		CoreLifecycleTestAccess::exchangeRenderer(previousRenderer);
		SDL_DestroyRenderer(renderer);
	}
	if (initializeTtf)
	{
		TTF_Quit();
	}
	return ok;
}

bool runChooseMenuRenderingTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	ScopedActiveResourceRoot resourceRoot;
	bool ok = check(resourceRoot.valid(), "choice rendering uses an isolated resource root");
	for (const char* path : { "ini/ui/choose/choose.menu.ini", "ini/ui/choose/window.ini", "ini/ui/choose/label.ini",
		"ini/ui/choose/btna.ini", "ini/ui/choose/btnb.ini", "asf/ui/dialog/panel.asf" })
	{
		std::ifstream input(assetsRoot / "yycs" / path, std::ios::binary);
		const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		ok = check(!contents.empty() && writeVirtualFile(path, contents), "copy actual YYCS choice template and parchment") && ok;
	}
	const bool initializeTtf = TTF_WasInit() == 0;
	if (!ok || !SDL_Init(0) || (initializeTtf && !TTF_Init()))
	{
		return false;
	}
	auto surface = make_shared_surface(SDL_CreateSurface(1280, 720, SDL_PIXELFORMAT_ARGB8888));
	SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface.get()) : nullptr;
	if (!check(renderer != nullptr, "create offscreen choice renderer"))
	{
		return false;
	}
	auto previousRenderer = CoreLifecycleTestAccess::exchangeRenderer(renderer);
	int previousWidth = 0, previousHeight = 0;
	CoreLifecycleTestAccess::getLogicalSize(previousWidth, previousHeight);
	Engine* engine = Engine::getInstance();
	engine->setFontName((assetsRoot / "engine/font/font.ttf").string());
	for (const Point size : { Point{1280, 720}, Point{640, 480}, Point{400, 360} })
	{
		CoreLifecycleTestAccess::setLogicalSize(size.x, size.y);
		for (bool speaker : { false, true })
		{
			ChooseMenu menu;
			const std::vector<std::string> options = speaker
				? std::vector<std::string>{u8"偷取", u8"查看友好度", u8"查看友好度上限", u8"减1000友好", u8"学习天赋【偷取】", u8"增加【偷取】经验"}
				: std::vector<std::string>{u8"黄钟牛 (50%)", u8"Goods-e07-钱袋.ini (60%)", u8"布鞋 (30%)", u8"离开"};
			CoreLifecycleTestAccess::prepareRenderedChoice(menu, speaker, options);
			SDL_SetRenderDrawColor(renderer, 24, 32, 40, 255);
			SDL_RenderClear(renderer);
			menu.drawImagetoRect(menu.rect, true);
			auto background = make_shared_surface(SDL_RenderReadPixels(renderer, nullptr));
			// Compare two single-pass frames; drawing the parchment twice also
			// changes its translucent border pixels, which are not glyphs.
			SDL_SetRenderDrawColor(renderer, 24, 32, 40, 255);
			SDL_RenderClear(renderer);
			CoreLifecycleTestAccess::draw(menu);
			auto composed = make_shared_surface(SDL_RenderReadPixels(renderer, nullptr));
			int textPixels = 0, outsidePaperPixels = 0;
			for (int y = 0; background && composed && y < size.y; ++y)
			{
				for (int x = 0; x < size.x; ++x)
				{
					Uint8 r = 0, g = 0, b = 0, a = 0, cr = 0, cg = 0, cb = 0, ca = 0;
					SDL_ReadSurfacePixel(background.get(), x, y, &r, &g, &b, &a);
					SDL_ReadSurfacePixel(composed.get(), x, y, &cr, &cg, &cb, &ca);
					if (r != cr || g != cg || b != cb || a != ca)
					{
						++textPixels;
						if (r < 140 || g < 140 || b < 120) ++outsidePaperPixels;
					}
				}
			}
			ok = check(textPixels > 100 && outsidePaperPixels == 0,
				"actual choice glyphs remain on light parchment, not the border or transparent margins") && ok;
			std::cout << "ChoicePixels size=" << size.x << 'x' << size.y << " options=" << options.size()
				<< " text=" << textPixels << " outside=" << outsidePaperPixels << std::endl;
			if (const char* directory = std::getenv("JXQY_TEST_ARTIFACT_DIRECTORY"))
			{
				std::filesystem::create_directories(std::filesystem::u8path(directory));
				const auto path = std::filesystem::u8path(directory) / ("choice-" + std::to_string(options.size())
					+ "-" + std::to_string(size.x) + "x" + std::to_string(size.y) + ".png");
				SDL_Rect crop{0, 0, size.x, size.y};
				auto image = make_shared_surface(SDL_RenderReadPixels(renderer, &crop));
				ok = check(image && IMG_SavePNG(image.get(), path.string().c_str()), "save actual composed choice image") && ok;
			}
		}
	}
	CoreLifecycleTestAccess::setLogicalSize(previousWidth, previousHeight);
	engine->setFontName("");
	CoreLifecycleTestAccess::exchangeRenderer(previousRenderer);
	SDL_DestroyRenderer(renderer);
	if (initializeTtf) TTF_Quit();
	return ok;
}

bool runProductionModCooldownTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid(), "cooldown tests isolate writable resources")) return false;
	const std::vector<std::string> roots{ (assetsRoot / std::filesystem::u8path(u8"潇湘行")).u8string(),
		(assetsRoot / "jxqy2").u8string(), (assetsRoot / "yycs").u8string() };
	File::setResourceFallbackRoots(roots);
	File::setUiResourceFallbackRoots({ roots[0], roots[2] }, true, (assetsRoot / "common").u8string());
	const bool initializeTtf = TTF_WasInit() == 0;
	if (!SDL_Init(0) || (initializeTtf && !TTF_Init())) return false;
	auto surface = make_shared_surface(SDL_CreateSurface(1280, 720, SDL_PIXELFORMAT_ARGB8888));
	SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface.get()) : nullptr;
	if (!check(renderer != nullptr, "create an offscreen cooldown renderer")) return false;
	auto previousRenderer = CoreLifecycleTestAccess::exchangeRenderer(renderer);
	int previousWidth = 0, previousHeight = 0;
	CoreLifecycleTestAccess::getLogicalSize(previousWidth, previousHeight);
	CoreLifecycleTestAccess::setLogicalSize(1280, 720);
	Engine* engine = Engine::getInstance();
	engine->setFontName((assetsRoot / "engine/font/font.ttf").string());
	bool ok = true;
	{
		GameManager game;
		ResourceManifest manifest;
		ok = check(manifest.loadFromFile("game_profile.ini"), "read the actual Xiaoxiang cooldown profile") && ok;
		game.global.applyResourceManifestFeatures(manifest);
		game.global.loadUiSettings();
		auto& manager = game.magicManager;
		manager.configureLayout();
		game.menu->magicMenu = std::make_shared<MagicMenu>();
		game.menu->bottomMenu = std::make_shared<BottomMenu>();
		game.menu->practiceMenu = std::make_shared<PracticeMenu>();
		auto list = game.menu->magicMenu;
		auto bottom = game.menu->bottomMenu;
		auto practice = game.menu->practiceMenu;
		game.controller->init();
		game.controller->setTouchControlsVisible(true);
		auto touch = game.controller->skillPanel;
		list->visible = bottom->visible = practice->visible = true;
		const auto refresh = [&]()
		{
			CoreLifecycleTestAccess::advanceActorFrame(*list, 0);
			CoreLifecycleTestAccess::advanceActorFrame(*bottom, 0);
			CoreLifecycleTestAccess::advanceActorFrame(*practice, 0);
			CoreLifecycleTestAccess::advanceActorFrame(*touch, 0);
		};
		for (const auto& fixture : { std::make_pair(u8"001达摩真经.ini", 60000u),
			std::make_pair(u8"001如何偏爱来潇湘路.ini", 100000u) })
		{
			manager.clearMagicList();
			game.scriptAPI.addMagic(fixture.first);
			auto* learned = manager.findPrimaryMagic(fixture.first);
			if (!check(learned && learned->magic->coldMilliSeconds == fixture.second
				&& !list->item.empty() && bottom->magicItem[0] && practice->magic,
				"real cooldown definitions and all three menu item bindings load")) { ok = false; continue; }
			const auto magic = learned->magic;
			const int store = static_cast<int>(learned - manager.magicList.data());
			manager.exchange(store, manager.bottomIndex(0));
			manager.updateMenu();
			manager.finishMagicUse(magic, magic->coldMilliSeconds, true);
			refresh();
			auto item = bottom->magicItem[0];
			ok = check(IMP::loadImageForTime(item->impImage, 0) != nullptr,
				"slot transfer refreshes the real spell icon before desktop and touch drawing") && ok;
			ok = check(item->getStr() == std::to_string(fixture.second / 1000) && item->cooldownFraction == 1.0f,
				"the toolbar displays the real 60/100-second cast timer") && ok;
			const auto textImage = item->strImage;
			manager.updateColdTimes(1);
			refresh();
			ok = check(textImage && item->strImage == textImage && manager.findPrimaryMagic(fixture.first)->remainColdMilliseconds == fixture.second - 1,
				"subsecond UI updates reuse text and do not restart or decrement the gameplay timer") && ok;
			manager.updateColdTimes(999);
			refresh();
			ok = check(item->getStr() == std::to_string(fixture.second / 1000 - 1), "the displayed countdown follows elapsed gameplay time") && ok;
			bottom->visible = false;
			manager.updateColdTimes(1000);
			CoreLifecycleTestAccess::advanceActorFrame(*touch, 0);
			ok = check(touch->skillBtn[0] && touch->skillBtn[0]->drawItem == item
				&& item->getStr() == std::to_string(fixture.second / 1000 - 2),
				"production touch controls update their shared item while the desktop toolbar is hidden") && ok;
			bottom->visible = true;
			if (const char* directory = std::getenv("JXQY_TEST_ARTIFACT_DIRECTORY"))
			{
				std::filesystem::create_directories(std::filesystem::u8path(directory));
				SDL_SetRenderDrawColor(renderer, 48, 56, 64, 255);
				SDL_RenderClear(renderer);
				CoreLifecycleTestAccess::drawSubtree(*bottom);
				CoreLifecycleTestAccess::drawSubtree(*touch);
				auto pixels = make_shared_surface(SDL_RenderReadPixels(renderer, nullptr));
				const auto path = std::filesystem::u8path(directory) / ("mod-cooldown-" + std::to_string(fixture.second) + ".png");
				ok = check(pixels && IMG_SavePNG(pixels.get(), path.string().c_str()), "save the actual cooldown toolbar composition") && ok;
			}
			manager.exchange(manager.bottomIndex(0), store);
			manager.updateMenu();
			refresh();
			ok = check(item->getStr().empty() && item->cooldownFraction == 0.0f
				&& list->item[0]->getStr() == std::to_string(fixture.second / 1000 - 2),
				"moving a cooling skill clears the old toolbar and updates its list item") && ok;
			manager.exchange(store, manager.practiceIndex());
			manager.updateMenu();
			refresh();
			ok = check(list->item[0]->getStr().empty() && practice->magic->getStr() == std::to_string(fixture.second / 1000 - 2),
				"the practice slot follows the same learned skill timer") && ok;
			manager.updateColdTimes(fixture.second - 2001);
			refresh();
			ok = check(practice->magic->getStr() == "1", "the last millisecond remains visibly unavailable") && ok;
			manager.updateColdTimes(1);
			refresh();
			ok = check(practice->magic->getStr().empty() && practice->magic->cooldownFraction == 0.0f
				&& manager.findPrimaryMagic(fixture.first)->remainColdMilliseconds == 0,
				"expiry removes both countdown and shade without changing the learned skill") && ok;
		}
	}
	CoreLifecycleTestAccess::setLogicalSize(previousWidth, previousHeight);
	engine->setFontName("");
	CoreLifecycleTestAccess::exchangeRenderer(previousRenderer);
	SDL_DestroyRenderer(renderer);
	if (initializeTtf) TTF_Quit();
	return ok;
}

bool runProductionYuchenTalentVariableTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	ScopedActiveResourceRoot resourceRoot;
	SaveFileManager::CurrentPathScope currentPath("save\\talent_variables");
	if (!check(resourceRoot.valid() && currentPath.valid(), "talent variable tests isolate saves")) return false;
	File::setResourceFallbackRoots({ (assetsRoot / std::filesystem::u8path(u8"江湖余尘")).u8string(), (assetsRoot / "yycs").u8string() });
	GameManager game;
	game.varList.ensureInitialized();
	game.varList.setInteger("koucai", 4);
	game.varList.setInteger("touqie", 3);
	game.varList.setInteger("yiliao", 2);
	CoreLifecycleTestAccess::registerScriptProbe(game.script, "say", [](lua_State*) { return 0; });
	struct Fixture { const char* file; const char* section; const char* map; const char* result; int expected; };
	const Fixture fixtures[] = {
		{ "ini/save/map001-luren.npc", "NPC002", u8"map_001_凌绝峰连接地图", "KoucaiTalentLevel", 0 },
		{ "ini/save/map002.npc", "NPC002", u8"map_002_凌绝峰峰顶", "LeechcraftDifference", -1 }
	};
	for (const auto& fixture : fixtures)
	{
		std::unique_ptr<char[]> bytes;
		if (!check(File::readFile(fixture.file, bytes) > 0, "read the actual talent query NPC")) return false;
		INIReader ini(bytes);
		auto npc = std::make_shared<NPC>();
		npc->initFromIni(&ini, fixture.section);
		game.npcManager->addNPC(npc);
		game.mapFolderName = fixture.map;
		game.runNPCScript(npc, npc->scriptFile);
		if (!check(!game.inEvent && game.varList.getInteger(fixture.result) == fixture.expected
			&& game.varList.getInteger("koucai") == 4 && game.varList.getInteger("touqie") == 3
			&& game.varList.getInteger("yiliao") == 2, "actual talent and medical queries preserve independently earned abilities")) return false;
	}
	if (!check(game.varList.save(), "save the actual ability variables")) return false;
	game.varList.setInteger("koucai", 0);
	return check(game.varList.load() && game.varList.getInteger("koucai") == 4,
		"later dialogue checks and file reload retain the earned eloquence");
}

bool runProductionYiheSystemMessageTests()
{
	const auto packRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
		"assets" / std::filesystem::u8path(u8"江湖余尘");
	if (!std::filesystem::exists(packRoot / "game_profile.ini"))
	{
		std::cout << "SKIP: optional production Yihe dialogue pack is absent\n";
		return true;
	}
	ScopedActiveResourceRoot resourceRoot;
	const char* scriptPath = u8"script/map/map_017_连接地图/议和对话.txt";
	std::ifstream input(packRoot / std::filesystem::u8path(scriptPath), std::ios::binary);
	const std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	if (!check(resourceRoot.valid() && !source.empty() && writeVirtualFile(scriptPath, source),
		"Yihe contracts use the unchanged production script in an isolated resource root"))
	{
		return false;
	}
	GameManager gameManager;
	gameManager.varList.ensureInitialized();
	gameManager.setAutomationHooksEnabled(true);
	gameManager.menu->dialog = std::make_shared<RecordingDialog>();
	gameManager.menu->messageBox = std::make_shared<MsgBox>();
	gameManager.menu->scriptMessages = std::make_shared<SystemNotice>(SystemNotice::Mode::ScriptMessages);
	gameManager.menu->showMessage("ordinary notice");
	// Supply the two choices and omit only fade waits. Reward, variable,
	// dialogue and system-message APIs execute unchanged.
	CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, "chooseex", [](lua_State* state)
	{
		const std::string output = luaL_checkstring(state, lua_gettop(state));
		gm->varList.setInteger("__automation_choose_enabled", 1);
		gm->varList.setInteger("__automation_choose_selection", output == "XUANZE" ? 1 : 0);
		return CoreLifecycleTestAccess::chooseExWithAutomation(state);
	});
	for (const char* command : { "fadeout", "fadein" })
	{
		CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, command, [](lua_State*) { return 0; });
	}
	const int attack = gameManager.player->attack;
	const int lifeMax = gameManager.player->lifeMax;
	const int manaMax = gameManager.player->manaMax;
	const int thewMax = gameManager.player->thewMax;
	auto messages = gameManager.menu->scriptMessages;
	messages->setTime(100);
	bool ok = check(gameManager.script.runScript(scriptPath) == LUA_OK &&
		gameManager.varList.getInteger("yh") == 1 &&
		gameManager.varList.getInteger("XUANZE") == 1 && gameManager.varList.getInteger("xuanze") == 0 &&
		gameManager.player->attack == attack + 5 && gameManager.player->lifeMax == lifeMax + 10 &&
		gameManager.player->manaMax == manaMax + 10 && gameManager.player->thewMax == thewMax + 10,
		"the actual Yihe teaching branch awards all four attributes and preserves the two choice variables");
	ok = check(CoreLifecycleTestAccess::scriptMessageCount(*messages) == 4 &&
		messages->currentMessage == u8"<color=Red>攻击+5\n<color=Red>生命+10\n<color=Red>内力+10\n<color=Red>体力+10" &&
		gameManager.menu->messageBox->currentMessage == "ordinary notice",
		"the actual Yihe teaching branch retains all four colored reward messages independently of ordinary notices") && ok;
	messages->setTime(5099);
	CoreLifecycleTestAccess::update(*messages);
	ok = check(CoreLifecycleTestAccess::scriptMessageCount(*messages) == 4,
		"all four Yihe messages survive until their requested 5000 ms boundary") && ok;
	messages->setTime(5100);
	CoreLifecycleTestAccess::update(*messages);
	ok = check(CoreLifecycleTestAccess::scriptMessageCount(*messages) == 0 && !messages->visible,
		"all four Yihe messages expire at their requested boundary") && ok;
	return ok;
}

bool runProductionNewSwordYangYingRouteTests()
{
	const auto packRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets/xjxqy";
	if (!std::filesystem::exists(packRoot / "game_profile.ini"))
	{
		std::cout << "SKIP: optional production New Sword Yang Ying resources are absent\n";
		return true;
	}
	const bool previousLoadAsync = Config::loadAsync;
	bool ok = true;
	for (bool asynchronous : { false, true })
	{
		Config::loadAsync = asynchronous;
		for (int hallEntrance : { 2, 4 })
		{
			ScopedActiveResourceRoot resourceRoot;
			if (!check(resourceRoot.valid(), "Yang Ying route isolates working NPC tables and variables"))
			{
				ok = false;
				continue;
			}
			File::setResourceFallbackRoots({ packRoot.u8string() });
			GameManager gameManager;
			ResourceManifest manifest;
			ok = check(manifest.loadFromFile("game_profile.ini"), "load actual New Sword profile") && ok;
			gameManager.global.applyResourceManifestFeatures(manifest);
			gameManager.varList.ensureInitialized();
			ok = check(gameManager.player->loadInitialTemplate(0) && gameManager.player->npcName == u8"独孤剑",
				"load the full initial player, including its movement permissions") && ok;
			auto dialog = std::make_shared<RecordingDialog>();
			gameManager.menu->dialog = dialog;
			auto choices = std::make_shared<RecordingChooseMenu>();
			choices->requestedSelection = 1;
			gameManager.menu->chooseMenu = choices;
			for (const char* command : { "sleep", "playmusic", "playsound", "npcgoto", "npcgotoex",
				"playergoto", "playergotoex", "npcspecialaction" })
			{
				CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, command, [](lua_State*) { return 0; });
			}
			CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, "fadeout", [](lua_State*)
			{
				auto& variables = GameManager::getInstance()->varList;
				variables.setInteger("__testFadeBalance", variables.getInteger("__testFadeBalance") + 1);
				return 0;
			});
			CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, "fadein", [](lua_State*)
			{
				auto& variables = GameManager::getInstance()->varList;
				variables.setInteger("__testFadeBalance", variables.getInteger("__testFadeBalance") - 1);
				return 0;
			});
			const auto pathToTrap = [&](int targetIndex)
			{
				std::vector<Point> path;
				const auto map = gameManager.map;
				if (map->data == nullptr) return path;
				const int width = map->data->head.width;
				const int height = map->data->head.height;
				const Point start = gameManager.player->getPosition();
				if (!map->isInMap(start)) return path;
				std::vector<int> previous(width * height, -1);
				std::vector<Point> frontier{ start };
				previous[start.y * width + start.x] = start.y * width + start.x;
				for (size_t cursor = 0; cursor < frontier.size(); ++cursor)
				{
					const Point from = frontier[cursor];
					for (int direction = 0; direction < 8; ++direction)
					{
						if (!map->canWalkDirectlyTo(from, direction)) continue;
						const Point to = Map::getSubPoint(from, direction);
						const int cell = to.y * width + to.x;
						if (previous[cell] >= 0) continue;
						const int index = map->getTrapIndex(to);
						if (index != 0 && index != targetIndex && !gameManager.traps.hasTriggered(index)
							&& !gameManager.traps.get(gameManager.mapFolderName, index).empty()) continue;
						previous[cell] = from.y * width + from.x;
						frontier.push_back(to);
						if (index == targetIndex)
						{
							for (int step = cell; step != previous[step]; step = previous[step])
							{
								path.push_back({ step % width, step / width });
							}
							std::reverse(path.begin(), path.end());
							return path;
						}
					}
				}
				return path;
			};
			const auto walkToTrap = [&](int index)
			{
				const auto path = pathToTrap(index);
				std::cout << "Yang Ying walk async=" << asynchronous << " entrance=" << hallEntrance << " map="
					<< gameManager.mapFolderName << " Event=" << gameManager.varList.getInteger("Event")
					<< " trap=" << index << " steps=" << path.size() << std::endl;
				if (!check(!path.empty(), "actual Yang Ying route reaches its next trap without skipping another bound event")) return false;
				for (Point step : path)
				{
					gameManager.player->setPosition(step, false);
					gameManager.player->checkTrap();
				}
				return true;
			};
			const auto hasStage = [&](const char* map, int event)
			{
				return check(gameManager.mapFolderName == map && gameManager.varList.getInteger("Event") == event
					&& gameManager.varList.getInteger("__testFadeBalance") == 0 && !gameManager.inEvent,
					"actual Yang Ying stage finishes at its expected map/event with balanced fade commands");
			};
			const auto talkToYang = [&]()
			{
				auto actors = gameManager.npcManager->findNPC(u8"杨瑛");
				if (!check(actors.size() == 1 && actors.front()->scriptFile == u8"杨瑛的同伴对话.txt",
					"exactly one Yang Ying has the complete companion dialogue, not the hall placeholder")) return false;
				gameManager.runNPCScript(actors.front(), "", false);
				return true;
			};
			gameManager.varList.setInteger("Event", 560);
			if (!check(gameManager.traps.loadInitialTemplate()
				&& gameManager.scriptAPI.loadMap(u8"map121_洞庭湖底.map", false)
				&& gameManager.scriptAPI.loadNPC("map121.npc") && gameManager.scriptAPI.loadObject("map121.obj"),
				"start at the actual pre-island checkpoint"))
			{
				ok = false;
				continue;
			}
			gameManager.runTrapScript(2);
			ok = hasStage(u8"map100_天王岛", 560) && ok;
			const Point landing = gameManager.player->getPosition();
			const int previousThew = gameManager.player->thew;
			gameManager.player->beginJump(Map::getSubPoint(landing, 5));
			ok = check(!gameManager.player->canJump && !gameManager.player->isJumping()
				&& gameManager.player->getPosition() == landing && gameManager.player->thew == previousThew,
				"the production player's disabled jump cannot bypass the island password trap") && ok;
			const auto bypass = pathToTrap(hallEntrance);
			std::cout << "Yang Ying walking guard bypass entrance=" << hallEntrance << " steps=" << bypass.size() << std::endl;
			ok = check(bypass.empty(), "initial walking route cannot enter the hall before another bound island event") && ok;
			ok = walkToTrap(3) && ok;
			ok = check(gameManager.varList.getInteger("Event") == 570 && choices->entries.size() == 2,
				"the real password trap and two correct menu selections grant entry") && ok;
			ok = walkToTrap(hallEntrance) && ok;
			ok = hasStage(u8"map101_天王帮大殿", 580) && ok;
			ok = check(gameManager.npcManager->findNPC(u8"杨瑛").empty(), "first audience removes the hall's placeholder-bound Yang Ying") && ok;
			ok = walkToTrap(1) && ok;
			ok = walkToTrap(hallEntrance == 2 ? 4 : 2) && ok;
			ok = hasStage(u8"map101_天王帮大殿", 580) && ok;
			ok = check(gameManager.npcManager->findNPC(u8"杨瑛").empty(), "revisit through the other entrance reads the saved list without resurrecting Yang Ying") && ok;
			ok = walkToTrap(2) && ok;
			ok = hasStage(u8"map102_后花园和小树林", 580) && ok;
			gameManager.runTrapScript(1);
			ok = hasStage(u8"map102_后花园和小树林", 580) && ok;
			ok = talkToYang() && ok;
			ok = hasStage(u8"map102_后花园和小树林", 600) && ok;
			gameManager.runTrapScript(1);
			ok = hasStage(u8"map102_后花园和小树林", 600) && ok;
			ok = talkToYang() && ok;
			ok = hasStage(u8"map102_后花园和小树林", 605) && ok;
			gameManager.runTrapScript(1);
			ok = hasStage(u8"map102_后花园和小树林", 605) && ok;
			ok = walkToTrap(2) && ok;
			ok = hasStage(u8"map103_杨瑛寝宫", 605) && ok;
			ok = walkToTrap(2) && ok;
			auto feng = gameManager.npcManager->findNPC(u8"封玉书");
			if (!check(feng.size() == 1 && feng.front()->deathScript == u8"封玉书死亡.txt", "the actual room trap arms Feng's combat and death continuation"))
			{
				ok = false;
				continue;
			}
			// The combat itself is a checkpoint; execute the real prepared NPC death callback.
			gameManager.runNPCDeathScript(feng.front(), feng.front()->deathScript, gameManager.mapFolderName);
			ok = hasStage(u8"map103_杨瑛寝宫", 610) && ok;
			ok = walkToTrap(1) && ok;
			ok = walkToTrap(1) && ok;
			ok = hasStage(u8"map101_天王帮大殿", 610) && ok;
			ok = talkToYang() && ok;
			ok = hasStage(u8"map101_天王帮大殿", 610) && ok;
			ok = check(std::none_of(dialog->entries.begin(), dialog->entries.end(), [](const auto& entry)
				{ return entry.first.find(u8"恭喜你，独孤大哥") != std::string::npos; }),
				"the first audience, garden chain and companion revisit never execute the incomplete hall dialogue") && ok;
			std::cout << "Yang Ying route complete async=" << asynchronous << " entrance=" << hallEntrance
				<< " Event=" << gameManager.varList.getInteger("Event") << " dialogues=" << dialog->entries.size() << std::endl;
		}
	}
	Config::loadAsync = previousLoadAsync;
	return ok;
}

// Real movement/pathfinding/animation updates, with a bounded clock instead of
// wall-clock modal waits. Presentation is recorded separately below.
void advanceSwordTwoScene(UTime milliseconds)
{
	auto* manager = GameManager::getInstance();
	for (UTime elapsed = 0; elapsed < milliseconds; elapsed += 50)
	{
		const auto actors = manager->npcManager->npcList;
		for (const auto& actor : actors)
		{
			if (actor != nullptr) CoreLifecycleTestAccess::advanceActorFrame(*actor, 50);
		}
		CoreLifecycleTestAccess::advanceActorFrame(*manager->player, 50);
	}
}

template<class Actor>
class SwordTwoSceneActor final : public Actor
{
	int modalRuns = 0;
public:
	unsigned int eventRun() override
	{
		if (++modalRuns > 100)
		{
			std::cout << "T56 repeated movement actor=" << this->npcName << " position="
				<< this->getPosition().x << ',' << this->getPosition().y << std::endl;
			GameManager::getInstance()->varList.setInteger("__T56MovementTimeout", 1);
			return erInitError;
		}
		return Actor::eventRun();
	}
private:
	void onRun() override
	{
		for (int frame = 0; frame < 800 && this->logicRunning; ++frame) advanceSwordTwoScene(50);
		if (this->logicRunning)
		{
			std::cout << "T56 exhausted movement actor=" << this->npcName << " position="
				<< this->getPosition().x << ',' << this->getPosition().y << std::endl;
			GameManager::getInstance()->varList.setInteger("__T56MovementTimeout", 1);
			this->result |= erInitError;
			this->logicRunning = false;
		}
	}
};

void useSwordTwoSceneClock(size_t index)
{
	auto* manager = GameManager::getInstance();
	auto original = manager->npcManager->npcList[index];
	INIReader snapshot;
	original->saveToIni(&snapshot, "actor");
	auto actor = std::make_shared<SwordTwoSceneActor<NPC>>();
	actor->initFromIni(&snapshot, "actor");
	manager->npcManager->removeChild(original);
	manager->npcManager->npcList[index] = actor;
	manager->npcManager->addChild(actor);
	manager->map->createDataMap();
}

bool runSwordTwoHistoricalRouteTests(const std::filesystem::path& assetsRoot, const char* directory)
{
	if (!std::filesystem::exists(assetsRoot / std::filesystem::u8path(directory) / "game_profile.ini"))
	{
		std::cout << "SKIP: optional Sword2 historical route resources are absent\n";
		return true;
	}
	bool ok = true;
	for (int scenario : { 0, 1, 2 })
	{
		ScopedActiveResourceRoot resourceRoot;
		if (!check(resourceRoot.valid(), "T56 routes isolate all working saves")) return false;
		File::setResourceFallbackRoots({ (assetsRoot / std::filesystem::u8path(directory)).u8string(),
			(assetsRoot / "jxqy2").u8string(), (assetsRoot / "yycs").u8string() });
		GameManager manager;
		ResourceManifest manifest;
		ok = check(manifest.loadFromFile("game_profile.ini"), "T56 loads the actual pack profile") && ok;
		manager.global.applyResourceManifestFeatures(manifest);
		manager.varList.ensureInitialized();
		manager.player = std::make_shared<SwordTwoSceneActor<Player>>();
		ok = check(manager.player->loadInitialTemplate(0) && manager.traps.loadInitialTemplate(),
			"T56 loads the actual player and initial trap definitions") && ok;
		manager.menu->dialog = std::make_shared<RecordingDialog>();
		for (const char* command : { "fadeout", "fadein", "playmusic", "playsound", "hideinterface", "hidebottomwnd" })
		{
			CoreLifecycleTestAccess::registerScriptProbe(manager.script, command, [](lua_State*) { return 0; });
		}
		CoreLifecycleTestAccess::registerScriptProbe(manager.script, "addnpc", [](lua_State* state)
		{
			const auto before = gm->npcManager->npcList.size();
			gm->scriptAPI.addNPC(lua_tostring(state, 1), static_cast<int>(lua_tointeger(state, 2)),
				static_cast<int>(lua_tointeger(state, 3)), static_cast<int>(lua_tointeger(state, 4)));
			if (gm->npcManager->npcList.size() > before) useSwordTwoSceneClock(before);
			return 0;
		});
		CoreLifecycleTestAccess::registerScriptProbe(manager.script, "sleep", [](lua_State* state)
		{
			if (gm->mapFolderName == u8"凤池山庄-比武场" && gm->varList.getInteger("__T56Defeat") == 1
				&& lua_tointeger(state, 1) == 3000)
			{
				gm->varList.setInteger("__T56DefeatAI", gm->global.data.NPCAI ? 1 : 0);
			}
			advanceSwordTwoScene(static_cast<UTime>(lua_tointeger(state, 1)));
			if (gm->varList.getInteger("__T56Defeat") == 1)
			{
				const auto dugu = gm->npcManager->findNPC(u8"独孤剑");
				if (!dugu.empty() && dugu.front()->currentCombatTarget.lock() == gm->player)
					gm->varList.setInteger("__T56AttackedSpectator", 1);
			}
			return 0;
		});
		CoreLifecycleTestAccess::registerScriptProbe(manager.script, "talk", [](lua_State* state)
		{
			const std::string part = lua_tostring(state, 1);
			if (part == "HappyDie2" || part == "HappyDie3")
			{
				const Point expected = part == "HappyDie2" ? Point{23,57} : Point{22,57};
				const bool settled = gm->player->getPosition() == expected && gm->player->isStanding();
				gm->varList.setInteger("__T56" + part, settled ? 1 : 0);
				if (part == "HappyDie3") gm->varList.setInteger("__T56Facing", gm->player->direction);
				std::cout << "T56 " << part << " player=" << gm->player->getPosition().x << ','
					<< gm->player->getPosition().y << " settled=" << settled << " dir=" << gm->player->direction << '\n';
			}
			gm->scriptAPI.talk(part);
			return 0;
		});
		CoreLifecycleTestAccess::registerScriptProbe(manager.script, "stopmusic", [](lua_State*)
		{
			if (gm->mapFolderName == u8"天忍教-地下迷宫3")
			{
				gm->varList.setInteger("__T56ExitSettled", gm->player->getPosition() == Point{28,65}
					&& gm->player->isStanding() ? 1 : 0);
			}
			return 0;
		});
		CoreLifecycleTestAccess::registerScriptProbe(manager.script, "playmovie", [](lua_State* state)
		{
			if (std::string(lua_tostring(state, 1)) == "happyend.avi")
				gm->varList.setInteger("__T56HappyMovie", 1);
			return 0;
		});
		CoreLifecycleTestAccess::registerScriptProbe(manager.script, "returntotitle", [](lua_State*)
		{
			GameManager::getInstance()->varList.setInteger("__T56Title", 1);
			return 0;
		});
		const char* map = scenario == 0 ? u8"龙门客栈.map" : scenario == 1 ? u8"凤池山庄-比武场.map" : u8"天忍教-地下迷宫3.map";
		const char* npc = scenario == 0 ? "lmkz.npc" : scenario == 1 ? "fengchibw.npc" : "trj-dixiamigong-3.npc";
		const char* object = scenario == 0 ? "lmkz.obj" : scenario == 1 ? "fengchibw.obj" : "trj-dixiamigong-3.obj";
		if (!check(manager.scriptAPI.loadMap(map, false) && manager.scriptAPI.loadNPC(npc)
			&& manager.scriptAPI.loadObject(object), "T56 loads the actual scene map, NPC and object lists"))
		{
			ok = false;
			continue;
		}
		for (size_t index = 0; index < manager.npcManager->npcList.size(); ++index) useSwordTwoSceneClock(index);
		manager.global.data.NPCAI = false;
		std::cout << "T56 route pack=" << directory << " scenario=" << scenario << std::endl;
		if (scenario == 0)
		{
			// Real entry checkpoint; actual Chai Song and quest scripts establish the branch variables.
			manager.player->setPosition({8,17}, false);
			manager.runTrapScript(3);
			ok = check(manager.varList.getInteger("LMKZOQieHuan") == 1, "Chai Song dialogue enables Longmen travel") && ok;
			manager.inEvent = true;
			const auto questResult = manager.script.runScript(u8"script/map/龙门客栈/去葬马岗对话.txt");
			manager.inEvent = false;
			ok = check(questResult == LUA_OK && manager.varList.getInteger("LMKZOZhanMaGang") == 1,
				"the actual burial-hill quest enables out3") && ok;
			for (int trap : { 1, 2 })
			{
				Point cell{-1,-1};
				for (int y = 0; y < manager.map->data->head.height && cell.x < 0; ++y)
					for (int x = 0; x < manager.map->data->head.width && cell.x < 0; ++x)
						if (manager.map->getTrapIndex({x,y}) == trap && manager.map->canWalk({x,y})) cell = {x,y};
				ok = check(cell.x >= 0, "each Longmen exit has actual walkable trap tiles") && ok;
				for (int attempt = 0; attempt < 2; ++attempt)
				{
					const auto dialog = std::static_pointer_cast<RecordingDialog>(manager.menu->dialog);
					const auto before = dialog->entries.size();
					manager.player->setPosition(cell, false);
					manager.player->checkTrap();
					ok = check(dialog->entries.size() > before && !manager.traps.hasTriggered(trap)
						&& manager.mapFolderName == u8"龙门客栈", "both repeated out3 exits retain their quest guard") && ok;
				}
			}
			ok = check(manager.traps.save() && manager.traps.load()
				&& !manager.traps.hasTriggered(1) && !manager.traps.hasTriggered(2), "Longmen reactivation survives file save/load") && ok;
			ok = check(manager.script.runScript(u8"script/map/龙门客栈/燕若雪talk1.txt") == LUA_OK
				&& manager.varList.getInteger("LMKZOZhanMaGang") == 0, "the actual return dialogue releases the quest guard") && ok;
			manager.runTrapScript(2);
			ok = check(manager.mapFolderName == u8"龙门客栈-长安", "the eastern exit remains usable after quest completion") && ok;
		}
		else if (scenario == 1)
		{
			// Checkpoint: 独孤剑.txt has started the spectated fight (FCBW12,
			// AI on, player at 15,77). Do not claim the earlier tournament rounds.
			manager.player->setPosition({15,77}, false);
			manager.varList.setInteger("FCBW", 12);
			manager.global.data.NPCAI = true;
			manager.global.data.canInput = false;
			// The real Tang Li defeat script prepares the Zhao fight, including
			// actor positions, relations, Kind, gates and AI; do not fabricate it.
			for (const char* name : { u8"唐离", u8"赵升权" })
			{
				const auto actors = manager.npcManager->findNPC(name);
				if (!check(actors.size() == 1 && !actors.front()->deathScript.empty(), "the real contestant has one death callback"))
				{
					ok = false;
					break;
				}
				auto defeated = actors.front();
				const auto deathScript = defeated->deathScript;
				manager.npcManager->deleteNPC(name);
				if (std::string(name) == u8"赵升权") manager.varList.setInteger("__T56Defeat", 1);
				manager.runNPCDeathScript(defeated, deathScript, manager.mapFolderName);
			}
			ok = check(manager.varList.getInteger("__T56DefeatAI") == 0,
				"Zhao's actual defeat continuation disables AI before its three-second wait") && ok;
			ok = check(manager.varList.getInteger("__T56AttackedSpectator") == 0,
				"Dugu does not acquire the spectating protagonist as a combat target after Zhao loses") && ok;
			ok = check(manager.mapFolderName == u8"凤池山庄" && manager.global.data.NPCAI
				&& manager.global.data.canInput && !manager.inEvent,
				"the actual tournament ending returns to the manor and restores AI/input") && ok;
		}
		else
		{
			manager.varList.setInteger("HappyEnding", 1);
			const auto actors = manager.npcManager->findNPC(u8"完颜宏烈");
			if (!check(actors.size() == 1 && actors.front()->deathScript == u8"大结局.txt",
				"the actual final boss binds the ending script"))
			{
				ok = false;
				continue;
			}
			auto defeated = actors.front();
			manager.npcManager->deleteNPC(u8"完颜宏烈");
			manager.runNPCDeathScript(defeated, u8"大结局.txt", manager.mapFolderName);
			ok = check(manager.varList.getInteger("__T56HappyDie2") == 1, "the protagonist reaches Ruoxue before HappyDie2") && ok;
			ok = check(manager.varList.getInteger("__T56HappyDie3") == 1 && manager.varList.getInteger("__T56Facing") == 5,
				"HappyDie3 starts after the protagonist reaches the intended tile and faces Ruoxue") && ok;
			ok = check(manager.varList.getInteger("__T56ExitSettled") == 1, "the ending waits for the protagonist to finish leaving") && ok;
			ok = check(manager.varList.getInteger("__T56HappyMovie") == 1 && manager.varList.getInteger("__T56Title") == 1,
				"the complete happy ending reaches its movie and title continuation") && ok;
		}
		ok = check(manager.varList.getInteger("__T56MovementTimeout") == 0, "T56 real movement completes within the scene clock bound") && ok;
	}
	return ok;
}

bool runProductionSwordTwoHistoricalScriptTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	bool ok = true;
	for (const char* directory : { "jxqy2", u8"剑二改承合版", u8"新月无痕" })
	{
		const auto packRoot = assetsRoot / std::filesystem::u8path(directory);
		if (!std::filesystem::exists(packRoot / "game_profile.ini"))
		{
			std::cout << "SKIP: optional historical-script pack " << directory << " is absent\n";
			continue;
		}
		ScopedActiveResourceRoot resourceRoot;
		if (!check(resourceRoot.valid(), "historical script checks isolate working files"))
		{
			return false;
		}
		File::setResourceFallbackRoots({ packRoot.u8string(), (assetsRoot / "jxqy2").u8string(),
			(assetsRoot / "yycs").u8string() });
		GameManager gameManager;
		gameManager.varList.ensureInitialized();
		auto dialog = std::make_shared<RecordingDialog>();
		gameManager.menu->dialog = dialog;
		if (!check(gameManager.player->loadInitialTemplate(0)
			&& gameManager.scriptAPI.loadMap(u8"凤池山庄.map", false), "load the actual manor map and player"))
		{
			ok = false;
			continue;
		}
		for (const char* command : { "playergoto", "fadeout", "fadein", "playmusic", "stopmusic" })
		{
			CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, command, [](lua_State*) { return 0; });
		}
		CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, "setnpcscript",
			CoreLifecycleTestAccess::observeFengchiGuardBinding);
		ok = check(gameManager.script.runScript(u8"script/map/凤池山庄/maptrap4.txt") == LUA_OK
			&& gameManager.varList.getInteger("FromFengChi") == 4
			&& gameManager.varList.getInteger("GuardBindingCount") == 2
			&& gameManager.varList.getInteger("BadGuardBindingCount") == 0,
			"both real guard bindings use the existing script filename, including the intermediate binding") && ok;
		gameManager.mapFolderName = u8"霹雳堂";
		dialog->entries.clear();
		gameManager.scriptAPI.talk("LeiTongDied");
		ok = check(dialog->entries.size() == 17
			&& dialog->entries[9].second == u8"雷同.mpc"
			&& dialog->entries[10].second == u8"雷同.mpc"
			&& dialog->entries[11].second == u8"南宫飞云.mpc"
			&& dialog->entries[16].second == u8"雷同.mpc",
			"the actual confession keeps Lei Tong's portrait for consecutive lines and then switches speakers") && ok;
	}
	ok = runSwordTwoHistoricalRouteTests(assetsRoot, "jxqy2") && ok;
	return ok;
}

bool runProductionMoonlightRescueReturnTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	bool ok = true;
	for (int legacyResult : { 0, 1 })
	{
		ScopedActiveResourceRoot resourceRoot;
		if (!check(resourceRoot.valid(), "rescue return isolates saves")) return false;
		File::setResourceFallbackRoots({ (assetsRoot / "yycs").u8string() });
		GameManager game;
		ResourceManifest manifest;
		if (!check(manifest.loadFromFile("game_profile.ini"), "load Moonlight game profile")) return false;
		game.global.applyResourceManifestFeatures(manifest);
		game.varList.ensureInitialized();
		game.menu->dialog = std::make_shared<RecordingDialog>();
		for (const char* command : { "fadeout", "fadein", "sleep", "playmusic", "stopmusic", "playsound", "npcgoto", "npcgotoex", "playergoto", "npcspecialaction" })
			CoreLifecycleTestAccess::registerScriptProbe(game.script, command, [](lua_State*) { return 0; });
		if (!check(game.player->loadInitialTemplate(0) && game.traps.loadInitialTemplate()
			&& game.scriptAPI.loadMap(u8"map_022_清平乡.map", false) && game.scriptAPI.loadNPC("qingpingxiang.npc"), "load Qingping rescue checkpoint")) return false;
		game.varList.setInteger("Map022Enemy", 2);
		game.varList.setInteger("Result", legacyResult);
		game.runScript(u8"清平乡无赖死亡脚本.txt");
		ok = check(game.varList.getInteger("Event") == 170 && !game.player->canRun && !game.player->canJump,
			"defeating the last thug starts the walking escort home") && ok;
		ok = check(game.scriptAPI.loadMap(u8"map_018_连接地图.map", false) && game.scriptAPI.loadNPC("map018.npc"), "reach Hanbo entrance checkpoint") && ok;
		game.runTrapScript(2);
		ok = check(game.mapFolderName == u8"map_019_寒波谷", "Event170 escort can enter Hanbo despite a stale legacy Result") && ok;
		if (game.mapFolderName == u8"map_019_寒波谷")
		{
			game.runTrapScript(4);
			ok = check(game.varList.getInteger("Event") == 185 && game.npcManager->findNPC(u8"紫轩").size() == 1
				&& game.player->canRun && game.player->canJump,
				"actual arrival dialogue completes the Bajiao escort") && ok;
		}
		std::cout << "Moonlight rescue return Result=" << legacyResult << " Event=" << game.varList.getInteger("Event") << std::endl;
	}
	return ok;
}

bool runProductionNewSwordFeedbackTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	if (!check(std::filesystem::exists(assetsRoot / "xjxqy/game_profile.ini"), "feedback regression requires New Sword resources")) return false;
	const bool previousLoadAsync = Config::loadAsync;
	bool ok = true;
	for (bool asynchronous : { false, true })
	{
		for (int scenario : { 0, 1, 2, 3 })
		{
			Config::loadAsync = asynchronous;
			ScopedActiveResourceRoot resourceRoot;
			if (!check(resourceRoot.valid(), "feedback routes isolate working files")) return false;
			File::setResourceFallbackRoots({ (assetsRoot / "xjxqy").u8string(), (assetsRoot / "yycs").u8string() });
			GameManager game;
			ResourceManifest manifest;
			ok = check(manifest.loadFromFile("game_profile.ini"), "load New Sword profile") && ok;
			game.global.applyResourceManifestFeatures(manifest);
			game.varList.ensureInitialized();
			game.menu->dialog = std::make_shared<RecordingDialog>();
			// Omit timed presentation, retaining actual map loads, actors, state and saves.
			for (const char* command : { "fadeout", "fadein", "sleep", "playmusic", "stopmusic", "playsound",
				"npcgoto", "npcgotoex", "playergoto", "playerrunto", "playergotodir", "npcspecialaction", "movescreen" })
			{
				CoreLifecycleTestAccess::registerScriptProbe(game.script, command, [](lua_State*) { return 0; });
			}
			CoreLifecycleTestAccess::registerScriptProbe(game.script, "playmovie", [](lua_State*)
			{
				gm->varList.setInteger("FeedbackMovieCount", gm->varList.getInteger("FeedbackMovieCount") + 1);
				return 0;
			});
			CoreLifecycleTestAccess::registerScriptProbe(game.script, "choose", [](lua_State* state)
			{
				gm->varList.setInteger(lua_tostring(state, lua_gettop(state)), gm->varList.getInteger("FeedbackChoice"));
				return 0;
			});
			CoreLifecycleTestAccess::registerScriptProbe(game.script, "returntotitle", [](lua_State*)
			{
				gm->varList.setInteger("FeedbackReturnedToTitle", 1);
				return 0;
			});
			if (!check(game.player->loadInitialTemplate(1) && game.player->save(1)
				&& game.magicManager.save(1) && game.goodsManager.save(1)
				&& game.player->loadInitialTemplate(0) && game.traps.loadInitialTemplate(), "seed new-game character snapshots and traps")) return false;
			game.global.data.characterIndex = 0;
			const auto load = [&](const char* map, const char* npc, const char* object)
			{
				return game.scriptAPI.loadMap(map, false) && game.scriptAPI.loadNPC(npc) && game.scriptAPI.loadObject(object);
			};
			const auto reload = [&]()
			{
				return game.saveGame(1) && (asynchronous ? game.scriptAPI.loadGameAsync(1) : game.loadGame(1));
			};
			if (scenario == 3)
			{
				ok = check(load(u8"map065_少林寺.map", "map065.npc", "map065.obj"), "load Shaolin entrance") && ok;
				game.scriptAPI.addNPC(u8"npc004_张琳心.ini", 25, 45, 0);
				game.scriptAPI.setNPCKind(u8"张琳心", nkPartner);
				game.varList.setInteger("Event", 380);
				game.runTrapScript(4);
				game.runTrapScript(2);
				ok = check(game.varList.getInteger("Event") == 390 && game.npcManager->findNPC(u8"无虚大师").size() == 1,
					"the first Dharma Hall scene creates the master") && ok;
				game.runTrapScript(1);
				game.runTrapScript(5);
				game.runScript(u8"假和尚死亡.txt");
				ok = check(game.varList.getInteger("Event") == 400 && reload(), "capture the spy and reload a complete save") && ok;
				game.runTrapScript(1);
				game.runTrapScript(4);
				const auto master = game.npcManager->findNPC(u8"无虚大师");
				ok = check(master.size() == 1 && master.front()->scriptFile == u8"无虚大师对话.txt",
					"returning after capture restores the saved master and progression dialogue") && ok;
				if (master.size() == 1) game.runNPCScript(master.front(), "", false);
				ok = check(game.mapFolderName == u8"map069_少林寺塔林" && game.varList.getInteger("Event") == 405,
					"the master dialogue advances to the traitor confrontation") && ok;
			}
			else
			{
				ok = check(load(u8"map082_金兵大营.map", "map082.npc", "map082.obj"), "load camp entrance checkpoint") && ok;
				game.scriptAPI.addNPC(u8"npc004_张琳心.ini", 7, 24, 0);
				game.scriptAPI.setNPCKind(u8"张琳心", nkPartner);
				game.varList.setInteger("Event", 470);
				game.varList.setInteger("FeedbackChoice", scenario == 1 ? 1 : 0);
				game.runTrapScript(3);
				ok = check(game.mapFolderName == u8"map084_金兵主帅营" && reload(), "enter the real duel and reload its complete save") && ok;
				const auto heroine = game.npcManager->findNPC(u8"张琳心");
				const auto enemy = game.npcManager->findNPC(u8"南宫灭");
				if (!check(heroine.size() == 1 && enemy.size() == 1, "duel keeps one heroine and one opponent")) { ok = false; continue; }
				ok = check(!game.global.data.PartnerCombat, "scene does not enable combat for every partner") && ok;
				heroine.front()->beginStand();
				CoreLifecycleTestAccess::advanceActorFrame(*heroine.front(), 1000);
				ok = check(heroine.front()->currentCombatTarget.lock() == enemy.front(), "heroine actively targets Nangong Mie") && ok;
				auto effect = std::make_shared<Effect>();
				effect->user = enemy.front();
				effect->launcherKind = lkEnemy;
				effect->position = heroine.front()->getPosition();
				effect->doing = ekFlying;
				effect->width = 1;
				effect->lifeTime = 100000;
				ok = check(CollisionDetector::detectCollision(heroine.front(), effect), "ordinary enemy projectile can hit the heroine") && ok;
				if (scenario == 2)
				{
					game.runNPCDeathScript(enemy.front(), enemy.front()->deathScript, game.mapFolderName);
					ok = check(game.varList.getInteger("FeedbackReturnedToTitle") == 1, "defeating Nangong retains the original losing ending") && ok;
				}
				else
				{
					auto defeated = scenario == 0 ? heroine.front() : std::static_pointer_cast<NPC>(game.player);
					game.runNPCDeathScript(defeated, defeated->deathScript, game.mapFolderName);
					ok = check(game.varList.getInteger("Event") == 480 && game.varList.getInteger("FeedbackMovieCount") == 1
						&& game.player->npcName == u8"张琳心"
						&& game.mapFolderName == (scenario == 0 ? u8"map088_长白山" : u8"map086_天山"),
						"either protagonist defeat continues the intended medicine choice exactly once") && ok;
					// Resume at the medicine-return checkpoint, using the actual return and player-switch script.
					game.varList.setInteger("Event", 520);
					game.runTrapScript(1);
					ok = check(game.player->npcName == u8"独孤剑" && game.npcManager->findNPC(u8"张琳心").size() == 1 && reload()
						&& game.npcManager->findNPC(u8"张琳心").size() == 1,
						"medicine return creates exactly one companion before and after save reload") && ok;
				}
			}
			std::cout << "New Sword feedback route async=" << asynchronous << " scenario=" << scenario << " Event="
				<< game.varList.getInteger("Event") << " map=" << game.mapFolderName << std::endl;
		}
	}
	Config::loadAsync = previousLoadAsync;
	return ok;
}

bool runProductionSwordTwoPartnerDepartureTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	const bool previousLoadAsync = Config::loadAsync;
	bool ok = true;
	for (const char* directory : { "jxqy2", u8"剑二改承合版", u8"新月无痕" })
	{
		const auto packRoot = assetsRoot / std::filesystem::u8path(directory);
		if (!std::filesystem::exists(packRoot / "game_profile.ini"))
		{
			std::cout << "SKIP: optional partner-departure pack " << directory << " is absent\n";
			continue;
		}
		for (bool asynchronous : { false, true })
		{
			Config::loadAsync = asynchronous;
			ScopedActiveResourceRoot resourceRoot;
			if (!check(resourceRoot.valid(), "partner departure isolates working files"))
			{
				ok = false;
				continue;
			}
			File::setResourceFallbackRoots({ packRoot.u8string(), (assetsRoot / "jxqy2").u8string(),
				(assetsRoot / "yycs").u8string() });
			GameManager gameManager;
			ResourceManifest manifest;
			ok = check(manifest.loadFromFile("game_profile.ini"), "load the actual departure profile") && ok;
			gameManager.global.applyResourceManifestFeatures(manifest);
			gameManager.varList.ensureInitialized();
			ok = check(gameManager.player->loadInitialTemplate(0), "load the formal protagonist") && ok;
			auto dialog = std::make_shared<RecordingDialog>();
			gameManager.menu->dialog = dialog;
			// Only timed presentation is omitted; entity creation, Kind, deletion and saves use real scripts.
			for (const char* command : { "fadeout", "fadein", "sleep", "playmusic", "stopmusic" })
			{
				CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, command, [](lua_State*) { return 0; });
			}
			if (!check(gameManager.traps.loadInitialTemplate()
				&& gameManager.scriptAPI.loadMap(u8"中都夜.map", false)
				&& gameManager.scriptAPI.loadNPC("zhongduye.npc")
				&& gameManager.scriptAPI.loadObject("zhongduye.obj"), "load the real night courtyard"))
			{
				ok = false;
				continue;
			}
			const auto heroine = gameManager.npcManager->findNPC(u8"燕若雪");
			if (!check(heroine.size() == 1 && heroine.front()->scriptFile == u8"燕若雪.txt",
				"the actual courtyard binds the progression dialogue"))
			{
				ok = false;
				continue;
			}
			gameManager.varList.setInteger("ZhongDuHouHuaYuan", 3);
			gameManager.varList.setInteger("ZhongDuTroublesRoom", 3);
			gameManager.runNPCScript(heroine.front(), "", false);
			const auto companion = gameManager.npcManager->findNPC(u8"柴嵩");
			if (!check(companion.size() == 1 && companion.front()->kind != nkPartner
				&& companion.front()->scriptFile == u8"柴嵩.txt"
				&& gameManager.varList.getInteger("ZhongDuHouHuaYuan") == 4,
				"the real heroine branch creates and binds Chai Song from its formal template"))
			{
				ok = false;
				continue;
			}
			gameManager.runNPCScript(companion.front(), "", false);
			ok = check(companion.front()->kind == nkPartner
				&& gameManager.varList.getInteger("ZhongDuTroublesRoom") == 4,
				"Chai Song's real dialogue makes him a partner before departure") && ok;
			gameManager.runNPCScript(heroine.front(), "", false);
			ok = check(gameManager.varList.getInteger("ZhongDuHouHuaYuan") == 5
				&& gameManager.global.data.npcName == "zhongdukill.npc" && !gameManager.inEvent,
				"the departure branch completes and loads the actual ambush list") && ok;
			ok = check(gameManager.npcManager->findNPC(u8"柴嵩").empty()
				&& gameManager.npcManager->findNPC(u8"燕若雪").empty()
				&& gameManager.npcManager->findNPC(u8"婕儿").empty(),
				"DelNpc removes the departing partner and ordinary actors before the ambush") && ok;
			ok = check(gameManager.partnerManager.save(1)
				&& gameManager.npcManager->save("departure-revisit.npc")
				&& gameManager.scriptAPI.loadNPC("departure-revisit.npc")
				&& gameManager.partnerManager.load(1)
				&& gameManager.npcManager->findNPC(u8"柴嵩").empty(),
				"the departed companion stays absent after ordinary and partner snapshot reload") && ok;
			std::cout << "Sword-two partner departure: pack=" << directory << " async=" << asynchronous
				<< " remaining=" << gameManager.npcManager->findNPC(u8"柴嵩").size()
				<< " dialogues=" << dialog->entries.size() << std::endl;
		}
	}
	Config::loadAsync = previousLoadAsync;
	return ok;
}

bool runProductionMoonlightEndingTwoRouteTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	const bool previousLoadAsync = Config::loadAsync;
	bool ok = true;
	for (const char* directory : { "yycs", u8"江湖余尘", u8"江湖余尘二" })
	{
		const auto packRoot = assetsRoot / std::filesystem::u8path(directory);
		if (!std::filesystem::exists(packRoot / "game_profile.ini"))
		{
			std::cout << "SKIP: optional ending-two production pack " << directory << " is absent\n";
			continue;
		}
		for (bool asynchronous : { false, true })
		{
			Config::loadAsync = asynchronous;
			ScopedActiveResourceRoot resourceRoot;
			if (!check(resourceRoot.valid(), "ending-two routes isolate all working files"))
			{
				ok = false;
				continue;
			}
			File::setResourceFallbackRoots({ packRoot.u8string(), (assetsRoot / "yycs").u8string() });
			GameManager gameManager;
			ResourceManifest manifest;
			ok = check(manifest.loadFromFile("game_profile.ini"), "load actual ending-two resource profile") && ok;
			gameManager.global.applyResourceManifestFeatures(manifest);
			gameManager.varList.ensureInitialized();
			ok = check(gameManager.player->loadInitialTemplate(0), "load the complete production player template") && ok;
			gameManager.talkTextList.load();
			auto dialog = std::make_shared<RecordingDialog>();
			gameManager.menu->dialog = dialog;
			gameManager.menu->messageBox = std::make_shared<MsgBox>();
			// These checkpoints exercise real dispatch, entities and working files, not movement or combat animation.
			for (const char* command : { "fadeout", "fadein", "sleep", "playmusic", "playsound", "movescreen",
				"npcgoto", "npcgotoex", "playergoto", "playergotoex", "playerrunto", "npcspecialaction" })
			{
				CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, command, [](lua_State*) { return 0; });
			}
			const auto loadCheckpoint = [&](const char* map, const char* npc, const char* object)
			{
				return check(gameManager.scriptAPI.loadMap(map, false)
					&& gameManager.scriptAPI.loadNPC(npc) && gameManager.scriptAPI.loadObject(object),
					"load actual ending-two checkpoint map and entity tables");
			};
			const auto hasEvent = [&](int event)
			{
				return check(gameManager.varList.getInteger("Event") == event && !gameManager.inEvent,
					"the actual production branch completes at its expected Event");
			};
			ok = check(gameManager.traps.loadInitialTemplate(), "load formal trap bindings") && ok;
			gameManager.varList.setInteger("Result", 0);
			gameManager.varList.setInteger("Event", 562);
			gameManager.varList.setInteger("SenseVal", 1175);
			gameManager.varList.setInteger("EvilVal", 0);
			// The return checkpoint includes the accompanying Zhen; the entrance changes her Kind before the conversation.
			gameManager.scriptAPI.addNPC(u8"纳兰真.ini", 43, 143, 1);
			gameManager.scriptAPI.setNPCKind(u8"纳兰真", nkPartner);
			if (!loadCheckpoint(u8"map_065_天山古道.map", "map_tianshangudao.npc", "map_tianshangudao.obj"))
			{
				ok = false;
				continue;
			}
			gameManager.runTrapScript(2);
			ok = check(gameManager.mapFolderName == u8"map_030_悲魔山庄"
				&& gameManager.traps.get(gameManager.mapFolderName, 7) == u8"纳兰真对话.txt",
				"the real Event562 entrance arms the ending-decision trap") && ok;
			gameManager.runTrapScript(7);
			ok = hasEvent(1800) && ok;
			ok = check(gameManager.varList.getInteger("Result") == 2,
				"the current ending-two decision enters Event1800, not the legacy 700-series") && ok;
			// Intervening travel and combat are explicit checkpoints, not claimed as a continuous playthrough.
			gameManager.varList.setInteger("Event", 1801);
			if (!loadCheckpoint(u8"map_038_连接地图.map", "map038.npc", "map038_obj.obj"))
			{
				ok = false;
				continue;
			}
			gameManager.runTrapScript(2);
			ok = check(gameManager.mapFolderName == u8"map_039_飞龙堡"
				&& gameManager.global.data.npcName == "map039_fadennz.npc",
				"the current fortress entrance selects its real battle list, not the legacy rescue list") && ok;
			std::shared_ptr<NPC> finalEnemy;
			for (const auto& actor : gameManager.npcManager->npcList)
			{
				if (actor != nullptr && actor->deathScript == u8"死亡.txt") finalEnemy = actor;
			}
			if (!check(finalEnemy != nullptr, "the real fortress encounter has its configured death continuation"))
			{
				ok = false;
				continue;
			}
			gameManager.varList.setInteger("NpcCount", 1);
			gameManager.runNPCDeathScript(finalEnemy, finalEnemy->deathScript, gameManager.mapFolderName);
			ok = hasEvent(1802) && ok;
			ok = check(gameManager.npcManager->findNPC(u8"纳兰真").empty(),
				"the normal battle continuation removes the actor bound to the old Event750 rescue") && ok;
			if (!loadCheckpoint(u8"map_038_连接地图.map", "map038.npc", "map038_obj.obj"))
			{
				ok = false;
				continue;
			}
			gameManager.runTrapScript(2);
			ok = hasEvent(1802) && ok;
			ok = check(gameManager.npcManager->findNPC(u8"纳兰真").empty()
				&& gameManager.global.data.npcName == "map039_fadennz.npc",
				"revisiting the current fortress entrance does not resurrect the legacy rescue actor") && ok;
			gameManager.varList.setInteger("Event", 1803);
			// Match the companion state established by map_036/杀手死亡.txt at the rescue checkpoint.
			gameManager.scriptAPI.addNPC(u8"蔷薇.ini", 48, 38, 3);
			gameManager.scriptAPI.setNPCKind(u8"蔷薇", nkPartner);
			if (!loadCheckpoint(u8"map_030_悲魔山庄.map", "beimo.npc", "map030_obj.obj"))
			{
				ok = false;
				continue;
			}
			auto maids = gameManager.npcManager->findNPC(u8"侍女可意");
			if (!check(maids.size() == 1 && maids.front()->scriptFile == u8"侍女可意对话.txt",
				"the real maid binding advances the current return route"))
			{
				ok = false;
				continue;
			}
			gameManager.runNPCScript(maids.front(), "", false);
			ok = hasEvent(1804) && ok;
			if (!loadCheckpoint(u8"map_032_天山.map", "map032.npc", "map032_obj.obj"))
			{
				ok = false;
				continue;
			}
			const auto valleyCompanions = gameManager.npcManager->findNPC(u8"蔷薇");
			ok = check(valleyCompanions.size() == 1 && valleyCompanions.front()->kind == nkPartner,
				"the rescue checkpoint carries exactly one Qiang Wei partner to the valley entrance") && ok;
			gameManager.runTrapScript(1);
			std::cout << "Ending-two valley partner before=" << valleyCompanions.size()
				<< " after=" << gameManager.npcManager->findNPC(u8"蔷薇").size()
				<< " Event=" << gameManager.varList.getInteger("Event") << std::endl;
			ok = check(gameManager.mapFolderName == u8"map_033_落叶谷"
				&& gameManager.global.data.npcName == "map033.npc"
				&& gameManager.npcManager->findNPC(u8"李总管").size() == 1
				&& gameManager.npcManager->findNPC(u8"蔷薇").size() == 1
				&& gameManager.traps.get(gameManager.mapFolderName, 9) == "trap09.txt",
				"Event1804 loads the available valley list and its current audience trap") && ok;
			gameManager.runTrapScript(9);
			ok = hasEvent(1805) && ok;
			gameManager.runTrapScript(1);
			ok = check(gameManager.mapFolderName == u8"map_032_天山",
				"the real valley exit saves its current actors before the later return checkpoint") && ok;
			gameManager.varList.setInteger("Event", 1818);
			gameManager.runTrapScript(1);
			ok = check(gameManager.global.data.npcName == "map033.npc"
				&& gameManager.npcManager->findNPC(u8"李总管").size() == 1
				&& gameManager.npcManager->findNPC(u8"蔷薇").size() == 1
				&& gameManager.traps.get(gameManager.mapFolderName, 19) == "trap09.txt",
				"the later current confrontation also avoids the missing legacy table") && ok;
			gameManager.runTrapScript(19);
			ok = hasEvent(1819) && ok;
			std::cout << "Ending-two checkpoints complete pack=" << directory << " async=" << asynchronous
				<< " Result=" << gameManager.varList.getInteger("Result") << " Event="
				<< gameManager.varList.getInteger("Event") << " dialogues=" << dialog->entries.size() << std::endl;
		}
	}
	Config::loadAsync = previousLoadAsync;
	return ok;
}

bool runProductionPlayerNameTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	const auto packRoot = assetsRoot / std::filesystem::u8path(u8"江湖余尘");
	if (!std::filesystem::exists(packRoot / "game_profile.ini"))
	{
		std::cout << "SKIP: optional production Yuchen player-name resources are absent\n";
		return true;
	}
	ScopedActiveResourceRoot resourceRoot;
	SaveFileManager::CurrentPathScope currentPath("save\\player_name_contracts");
	if (!check(resourceRoot.valid() && currentPath.valid(), "player-name story uses isolated resource and save roots"))
	{
		return false;
	}
	for (const char* path : { "game_profile.ini", "ini/save/player0.ini", "ini/save/player1.ini",
		"ini/save/seashore-yangyf.npc", u8"script/map/map_051_海边/纳兰真对话.txt", "talkindex.txt" })
	{
		const auto sourceRoot = std::string(path) == "talkindex.txt" ? assetsRoot / "yycs" : packRoot;
		std::ifstream input(sourceRoot / std::filesystem::u8path(path), std::ios::binary);
		const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		if (!check(!contents.empty() && writeVirtualFile(path, contents), "copy actual seaside script, player templates and NPC list"))
		{
			return false;
		}
	}
	GameManager gameManager;
	ResourceManifest manifest;
	bool ok = check(manifest.loadFromFile("game_profile.ini") && manifest.scriptPlayerName == u8"杨影枫",
		"the production pack declares the fixed protagonist name");
	gameManager.global.applyResourceManifestFeatures(manifest);
	gameManager.varList.ensureInitialized();
	gameManager.mapFolderName = u8"map_051_海边";
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = 32;
	gameManager.map->data->head.height = 80;
	gameManager.map->data->tile.assign(80, std::vector<MapTile>(32));
	gameManager.map->createDataMap();
	gameManager.talkTextList.load();
	auto dialog = std::make_shared<RecordingDialog>();
	auto choice = std::make_shared<RecordingChooseMenu>();
	gameManager.menu->dialog = dialog;
	gameManager.menu->chooseMenu = choice;
	gameManager.menu->messageBox = std::make_shared<MsgBox>();
	// Seed the normal new-game player snapshots; equipment is irrelevant to this scene.
	if (!check(gameManager.player->loadInitialTemplate(1) && gameManager.player->save(1)
		&& gameManager.magicManager.save(1) && gameManager.goodsManager.save(1)
		&& gameManager.player->loadInitialTemplate(0), "seed actual Yang and Nalan player snapshots"))
	{
		return false;
	}
	gameManager.global.data.characterIndex = 0;
	gameManager.varList.setInteger("Event", 294);
	gameManager.varList.setInteger("SelectVal", 1);
	// Keep the entire real Lua branch, including PlayerChange and LoadNpc.
	// Only omit waits and fades; this test does not claim visual/map-path acceptance.
	for (const char* command : { "fadein", "fadeout", "sleep" })
	{
		CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, command, [](lua_State*) { return 0; });
	}
	ok = check(gameManager.script.runScript(u8"script/map/map_051_海边/纳兰真对话.txt") == LUA_OK,
		"execute the complete production seaside role-switch branch") && ok;
	auto targets = gameManager.npcManager->findNPC("#name");
	ok = check(gameManager.global.data.characterIndex == 1 && gameManager.player->npcName == u8"纳兰真"
		&& gameManager.player->getPosition() == Point{4,52}
		&& targets.size() == 1 && targets.front() != gameManager.player
		&& targets.front()->npcName == u8"杨影枫" && targets.front()->getPosition() == Point{4,53}
		&& targets.front()->direction == 3 && targets.front()->kind == nkPartner
		&& gameManager.varList.getInteger("Event") == 296
		&& gameManager.traps.get(gameManager.mapFolderName, 1) == "trap01.txt",
		"the actual story promotes Yang to partner while Nalan remains the controlled player and Event advances") && ok;
	ok = check(gameManager.player->save(1) && gameManager.partnerManager.save(1)
		&& gameManager.npcManager->save("name-roundtrip.npc"), "save player, partner and ordinary NPC snapshots") && ok;
	gameManager.partnerManager.clearCurrentPartners();
	ok = check(gameManager.npcManager->findNPC("#name").empty(), "remove the in-memory partner before snapshot reload") && ok;
	ok = check(gameManager.player->load(1) && gameManager.scriptAPI.loadNPC("name-roundtrip.npc")
		&& gameManager.partnerManager.load(1)
		&& gameManager.npcManager->findNPC("#name").size() == 1
		&& gameManager.npcManager->findNPC("#name").front() != targets.front()
		&& gameManager.npcManager->findNPC("#name").front()->npcName == u8"杨影枫"
		&& gameManager.npcManager->findNPC("#name").front()->getPosition() == Point{4,53}
		&& gameManager.player->npcName == u8"纳兰真", "player and partner snapshot reload recreates canonical story identities") && ok;
	const auto execute = [&](const std::string& source)
	{
		auto bytes = std::make_unique<char[]>(source.size());
		std::copy(source.begin(), source.end(), bytes.get());
		return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
	};
	dialog->entries.clear();
	ok = check(execute("say('#name','#name/#Name',2,0); chooseex('#name','{$Event == 296}#name','#Name','NameChoice'); "
		"showmessage('#name'); memo('#name task');") == LUA_OK
		&& dialog->entries.size() == 1 && dialog->entries.front().first == u8"杨影枫: 杨影枫/#Name"
		&& choice->entries == std::vector<std::vector<std::string>>{{u8"杨影枫",u8"杨影枫","#Name"}}
		&& gameManager.menu->messageBox->currentMessage == u8"杨影枫"
		&& gameManager.varList.getInteger("NameChoice") == 0,
		"rendered speaker, body, choice and message use the fixed name after the heroine switch") && ok;
	ok = check(execute("chooseplus('#name',2,0,'#name','Accept','NameChoice');") == LUA_OK
		&& CoreLifecycleTestAccess::choiceSpeaker(*choice) == u8"杨影枫",
		"ChoosePlus speaker also remains the protagonist after switching players") && ok;
	ok = check(execute(u8"setnpcscript('#name','#name.lua','seashore-yangyf.npc'); follownpc('纳兰真','#name');") == LUA_OK
		&& gameManager.global.save(), "execute offline name binding and save global state") && ok;
	INIReader savedNpcs(SaveFileManager::CurrentPath() + "seashore-yangyf.npc");
	INIReader savedGlobal(SaveFileManager::CurrentPath() + GLOBAL_INI);
	ok = check(savedNpcs.ParseError() == 0 && savedNpcs.Get("NPC000", "ScriptFile", "") == "#name.lua"
		&& gameManager.player->followNPC == u8"杨影枫" && savedGlobal.ParseError() == 0
		&& !savedGlobal.HasKey("Script", "PlayerName"),
		"offline NPC targeting and follower identity resolve names without replacing paths or saving resource configuration") && ok;
	ok = check(gameManager.memo.save() && gameManager.memo.load()
		&& std::any_of(gameManager.memo.memo.begin(), gameManager.memo.memo.end(), [](const auto& line)
			{ return line.find(u8"杨影枫 task") != std::string::npos; }), "resolved memo text survives snapshot save/load") && ok;
	ok = check(execute("delmemo('#name task'); setnpckind('#name',0); delnpc('#name'); playerchange(0); setnpcpos('#name',5,51);") == LUA_OK
		&& gameManager.global.data.characterIndex == 0 && gameManager.player->npcName == u8"杨影枫"
		&& gameManager.player->getPosition() == Point{5,51}
		&& gameManager.npcManager->findNPC("#name") == std::vector<std::shared_ptr<NPC>>{gameManager.player}
		&& std::none_of(gameManager.memo.memo.begin(), gameManager.memo.memo.end(), [](const auto& line)
			{ return line.find(u8"杨影枫 task") != std::string::npos; }),
		"switching back reuses the saved protagonist and resolves the same memo deletion key") && ok;
	return ok;
}

bool runActorStateScriptContracts()
{
	ScopedActiveResourceRoot resourceRoot;
	SaveFileManager::CurrentPathScope currentPath("save\\actor_state_contracts");
	if (!check(resourceRoot.valid() && currentPath.valid(), "actor-state contracts use isolated resource and save roots"))
	{
		return false;
	}
	GameManager gameManager;
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = 16;
	gameManager.map->data->head.height = 16;
	gameManager.map->data->tile.assign(16, std::vector<MapTile>(16));
	gameManager.map->createDataMap();
	NPCActionRes action;
	action.imagePackage = std::make_shared<IMPImage>();
	action.imagePackage->directions = 8;
	action.imagePackage->interval = 100;
	action.imagePackage->frame.resize(8);
	const auto makeNpc = [&](const std::string& name, Point position)
	{
		auto npc = std::make_shared<NPC>();
		npc->npcName = name;
		npc->kind = nkNormal;
		npc->life = 100;
		npc->setPosition(position, false);
		npc->res.stand = npc->res.walk = action;
		gameManager.npcManager->npcList.push_back(npc);
		return npc;
	};
	auto owner = makeNpc("Owner", {4,4});
	auto first = makeNpc("Named", {6,4});
	auto duplicate = makeNpc("Named", {8,4});
	auto player = gameManager.player;
	player->npcName = "Named";
	player->setPosition({2,2}, false);
	player->life = 100;
	player->res.stand = player->res.walk = action;
	gameManager.map->createDataMap();
	gameManager.scriptNPC = owner;
	const auto execute = [&](const std::string& source)
	{
		auto bytes = std::make_unique<char[]>(source.size());
		std::copy(source.begin(), source.end(), bytes.get());
		return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
	};
	first->direction = duplicate->direction = 5;
	bool ok = check(execute("setplayerdir(3); setplayerdir(-1);") == LUA_OK
		&& player->direction == 3 && first->direction == 5 && duplicate->direction == 5,
		"SetPlayerDir targets the actual player and preserves the legacy negative wildcard");
	player->beginWalk({2,6});
	ok = check(player->isWalking(), "actor-state fixture starts real player walking before state changes") && ok;
	ok = check(execute("setplayerstate(1);") == LUA_OK && player->fightState.get() && player->isWalking(),
		"characterize C++ SetPlayerState: change the fight flag without forcing the C# standing transition") && ok;
	player->fightState.update(9999);
	ok = check(player->fightState.get(), "C++ fight state retains its existing ten-second timeout") && ok;
	player->fightState.update(1);
	ok = check(!player->fightState.get(), "C++ fight state expires at ten seconds") && ok;
	player->haveAsyncDest = true;
	ok = check(execute("watch('Named','Owner');") == LUA_OK
		&& player->direction == player->getDirection(owner->getPosition())
		&& owner->direction == owner->getDirection(player->getPosition())
		&& first->direction == 5 && duplicate->direction == 5 && player->isWalking() && player->haveAsyncDest,
		"Watch faces both first named targets without cancelling their current action or async target") && ok;
	owner->direction = 7;
	ok = check(execute("watch('Named','Owner',1); watch('Named','Owner',1,0);") == LUA_OK
		&& owner->direction == 7, "C++ Watch uses the third parameter even when a fourth is present") && ok;
	player->direction = 3;
	ok = check(execute("watch('Named','Owner',2); watch('named','Owner'); watch('Named','Missing');") == LUA_OK
		&& player->direction == 3 && owner->direction == 7, "unknown Watch mode and absent or differently cased names are no-ops") && ok;
	player->beginStand();
	player->haveAsyncDest = false;
	player->destinationMapPosition = {1,1};
	player->keepAttackPosition = {2,2};
	first->haveAsyncDest = duplicate->haveAsyncDest = true;
	ok = check(execute("setnpcdestination('Named',10,11); setkeepattack('Named',12,13); setnpcdestination('',-1,9);") == LUA_OK
		&& first->destinationMapPosition == Point{10,11} && duplicate->destinationMapPosition == Point{10,11}
		&& !first->haveAsyncDest && !duplicate->haveAsyncDest
		&& first->keepAttackPosition == Point{12,13} && duplicate->keepAttackPosition == Point{12,13}
		&& player->destinationMapPosition == Point{1,1} && player->keepAttackPosition == Point{2,2}
		&& owner->destinationMapPosition == Point{4,9},
		"destination and keep-attack update all named NPCs but not the player; empty destination uses the owner and wildcard") && ok;
	ok = check(execute("follownpc('Named','Owner'); follownpc('Named');") == LUA_OK
		&& player->followNPC == "Owner" && owner->followNPC == "Named" && owner->isFollower()
		&& first->followNPC.empty(), "FollowNpc uses the first named follower or the one-argument script owner") && ok;
	ok = check(execute("follownpc('Owner','Missing');") == LUA_OK && owner->followNPC == "Missing"
		&& !owner->isFollower() && owner->followNPC.empty(), "C++ clears a missing follow name when resolving the follower") && ok;
	ok = check(execute("follownpc('Owner','Named'); setplayerstate(1); setwalkisrun(1);") == LUA_OK
		&& gameManager.player->save(0) && gameManager.npcManager->save("actor-state.npc"),
		"save actual player and ordinary NPC snapshots after actor-state commands") && ok;
	gameManager.npcManager->npcList.clear();
	gameManager.scriptNPC.reset();
	player->walkIsRun = 0;
	player->fightState.set(false);
	ok = check(player->load(0) && player->walkIsRun == 1 && player->fightState.get()
		&& gameManager.scriptAPI.loadNPC("actor-state.npc"), "player walk/fight settings and NPC snapshots load from disk") && ok;
	auto restored = gameManager.npcManager->findNPC("Named");
	ok = check(restored.size() == 3 && restored[0] == player && restored[1] != first && restored[2] != duplicate
		&& restored[1]->destinationMapPosition == Point{10,11} && restored[2]->destinationMapPosition == Point{10,11}
		&& restored[1]->keepAttackPosition == Point{12,13} && restored[2]->keepAttackPosition == Point{12,13},
		"new NPC instances restore both destination and keep-attack coordinates") && ok;
	auto restoredOwner = gameManager.npcManager->findNPC("Owner");
	ok = check(restoredOwner.size() == 1 && restoredOwner.front() != owner
		&& restoredOwner.front()->destinationMapPosition == Point{4,9} && restoredOwner.front()->followNPC.empty(),
		"the owner destination persists but the ordinary NPC follow link remains transient") && ok;
	ok = check(execute("setplayerstate(0); setwalkisrun(0);") == LUA_OK && !player->fightState.get() && player->walkIsRun == 0,
		"the same commands can clear restored player state") && ok;
	return ok;
}

bool runProductionWalkIsRunTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	if (!std::filesystem::exists(assetsRoot / std::filesystem::u8path(u8"江湖余尘/game_profile.ini")))
	{
		std::cout << "SKIP: optional production walk-is-run resources are absent\n";
		return true;
	}
	bool ok = true;
	for (const char* pack : { u8"江湖余尘", u8"江湖余尘二", u8"潇湘行" })
	{
		ScopedActiveResourceRoot resourceRoot;
		SaveFileManager::CurrentPathScope currentPath("save\\walk_is_run_contracts");
		if (!check(resourceRoot.valid() && currentPath.valid(), "walk-is-run goods use isolated resource and save roots"))
		{
			return false;
		}
		const std::string itemName = std::string(pack) == u8"江湖余尘二" ? u8"0神行太保.ini" : u8"神行太保.ini";
		for (const std::string path : { std::string("ini/save/player0.ini"), "ini/goods/" + itemName,
			std::string(u8"script/goods/神行太保.txt") })
		{
			std::ifstream input(assetsRoot / std::filesystem::u8path(pack) / std::filesystem::u8path(path), std::ios::binary);
			const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
			if (!check(!contents.empty() && writeVirtualFile(path, contents), "copy actual walk-is-run goods, script and player template"))
			{
				return false;
			}
		}
		GameManager gameManager;
		gameManager.varList.ensureInitialized();
		auto choice = std::make_shared<RecordingChooseMenu>();
		gameManager.menu->chooseMenu = choice;
		gameManager.map->data = std::make_shared<MapData>();
		gameManager.map->data->head.width = 12;
		gameManager.map->data->head.height = 12;
		gameManager.map->data->tile.assign(12, std::vector<MapTile>(12));
		gameManager.map->createDataMap();
		ok = check(gameManager.player->loadInitialTemplate(0) && gameManager.goodsManager.addItem(itemName, 1),
			"load the production player and acquire the actual walk-is-run item") && ok;
		gameManager.varList.setInteger("xuanzhe", 77);
		for (const int selection : { 0, 1, 0 })
		{
			choice->requestedSelection = selection;
			choice->entries.clear();
			ok = check(gameManager.goodsManager.useItem(gameManager.goodsManager.storeBegin())
				&& choice->entries == std::vector<std::vector<std::string>>{{ u8"是否使用神行太保？", u8"神行状态。", u8"正常状态。" }}
				&& gameManager.player->walkIsRun == 1 - selection
				&& gameManager.varList.getInteger("XuanZhe") == selection
				&& gameManager.varList.getInteger("xuanzhe") == 77
				&& gameManager.goodsManager.getItemNum(itemName) == 1,
				(std::string("actual goods-use choice toggles running without consuming the item: ") + pack).c_str()) && ok;
			// Exercise the production movement-intent entry, not only the stored flag.
			NPCActionRes action;
			action.imagePackage = std::make_shared<IMPImage>();
			action.imagePackage->directions = 8;
			action.imagePackage->interval = 100;
			action.imagePackage->frame.resize(8);
			gameManager.player->res.stand = gameManager.player->res.walk = gameManager.player->res.run = action;
			gameManager.player->setPosition({4,4}, false);
			gameManager.player->thew = 100;
			gameManager.player->setRunDisabled(false);
			NextAction movement;
			movement.action = acWalk;
			movement.dest = {4,6};
			ok = check(gameManager.player->addNextAction(movement)
				&& movement.action == (selection == 0 ? acRun : acWalk),
				"the actual movement-intent path obeys the production item choice") && ok;
			gameManager.player->setRunDisabled(true);
			movement.action = acWalk;
			ok = check(gameManager.player->addNextAction(movement) && movement.action == acWalk,
				"the item cannot bypass the script run-disable flag") && ok;
			gameManager.player->setRunDisabled(false);
			ok = check(gameManager.player->save(0) && gameManager.goodsManager.save(0) && gameManager.varList.save(),
				"save actual player, goods and case-sensitive choice variables") && ok;
			gameManager.player->walkIsRun = 10;
			gameManager.varList.setInteger("XuanZhe", 99);
			gameManager.goodsManager.deleteItem(itemName);
			ok = check(gameManager.player->load(0) && gameManager.goodsManager.load(0) && gameManager.varList.load()
				&& gameManager.player->walkIsRun == 1 - selection
				&& gameManager.goodsManager.getItemNum(itemName) == 1
				&& gameManager.varList.getInteger("XuanZhe") == selection
				&& gameManager.varList.getInteger("xuanzhe") == 77,
				"walk-is-run and its reusable item survive disk reload and can be toggled again") && ok;
		}
	}
	return ok;
}

bool runObjectAnimationScriptContracts()
{
	ScopedActiveResourceRoot resourceRoot;
	SaveFileManager::CurrentPathScope currentPath("save\\object_animation_contracts");
	if (!check(resourceRoot.valid() && currentPath.valid() &&
		writeVirtualFile("ini/objres/animation-contract.ini", "[Common]\nImage=animation-contract.asf\n"),
		"object animation contracts use isolated resource and save roots"))
	{
		return false;
	}
	GameManager gameManager;
	auto image = std::make_shared<IMPImage>();
	image->directions = 2;
	image->interval = 100;
	image->frame.resize(10);
	gameManager.objectManager->objectImageList["animation-contract.asf"] = image;
	const auto makeBox = [&](const std::string& name)
	{
		INIReader definition;
		definition.Set("Init", "ObjName", name);
		definition.Set("Init", "ObjFile", "animation-contract.ini");
		definition.SetInteger("Init", "Kind", okBox);
		definition.SetInteger("Init", "Dir", 1);
		definition.SetInteger("Init", "Frame", 5);
		auto object = std::make_shared<Object>();
		object->initFromIni(&definition, "Init");
		gameManager.objectManager->objectList.push_back(object);
		return object;
	};
	auto owner = makeBox("Owner");
	auto first = makeBox("Named");
	auto duplicate = makeBox("Named");
	gameManager.scriptObj = owner;
	const auto execute = [&](const std::string& source)
	{
		auto bytes = std::make_unique<char[]>(source.size());
		std::copy(source.begin(), source.end(), bytes.get());
		return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
	};
	bool ok = check(execute("openbox(); openobj('Named');") == LUA_OK &&
		owner->nowAction == oaOpening && first->nowAction == oaOpening && duplicate->nowAction == oaStay,
		"OpenBox uses the owner and OpenObj uses the first exact named target");
	ok = check(execute("closebox('named'); openbox('Missing');") == LUA_OK && first->nowAction == oaOpening,
		"object names remain case-sensitive and missing names do not fall back to the owner") && ok;
	gameManager.scriptObj.reset();
	ok = check(execute("closebox();") == LUA_OK && owner->nowAction == oaOpening,
		"ownerless CloseBox is a no-op") && ok;
	gameManager.scriptObj = owner;
	ok = check(execute("closebox('');") == LUA_OK && owner->nowAction == oaClosing,
		"C++ retains its explicit empty-name owner extension") && ok;
	for (const bool closing : { false, true })
	{
		owner->setTime(1000);
		if (closing)
		{
			owner->closeBox();
		}
		else
		{
			owner->openBox();
		}
		owner->setTime(1200);
		INIReader saved;
		owner->saveToIni(&saved, "Object");
		Object restored;
		restored.setTime(5000);
		restored.initFromIni(&saved, "Object");
		ok = check(restored.actionLastTime == 500 && restored.getActionElapsedMilliseconds() == 200 &&
			restored.nowAction == (closing ? oaClosing : oaOpening),
			"loading an in-flight box restores its image duration and persisted elapsed time") && ok;
		CoreLifecycleTestAccess::update(restored);
		ok = check(restored.nowAction == (closing ? oaClosing : oaOpening),
			"the first update after loading must not finish an incomplete box animation") && ok;
		restored.setTime(5301);
		CoreLifecycleTestAccess::update(restored);
		ok = check(restored.nowAction == oaStay && restored.frame == (closing ? 5 : 9),
			"a restored opening/closing animation reaches its own direction endpoint after the remaining time") && ok;
		restored.saveToIni(&saved, "Object");
		Object settled;
		settled.initFromIni(&saved, "Object");
		CoreLifecycleTestAccess::update(settled);
		ok = check(settled.nowAction == oaStay && settled.frame == restored.frame,
			"completed box frames survive reloading without replay") && ok;
	}
	return ok;
}

bool runProductionObjectAnimationTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	if (!std::filesystem::exists(assetsRoot / "yycs/game_profile.ini"))
	{
		std::cout << "SKIP: optional production object animation resources are absent\n";
		return true;
	}
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid(), "production object animation has an isolated resource root"))
	{
		return false;
	}
	for (const char* path : { "ini/obj/lock_6_1.ini", "ini/obj/lock_6_2.ini",
		u8"ini/objres/obj-通天塔开关6.ini", u8"asf/object/通天塔开关6.asf" })
	{
		std::ifstream input(assetsRoot / "yycs" / std::filesystem::u8path(path), std::ios::binary);
		const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		if (!check(!contents.empty() && writeVirtualFile(path, contents), "copy the actual tower switch definitions and image"))
		{
			return false;
		}
	}
	bool ok = true;
	for (const char* pack : { "yycs", u8"江湖余尘", u8"江湖余尘二" })
	{
		const char* path = u8"script/map/map_046_通天塔第六层/trap09.txt";
		std::ifstream input(assetsRoot / std::filesystem::u8path(pack) / std::filesystem::u8path(path), std::ios::binary);
		const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		if (!check(!contents.empty() && writeVirtualFile(path, contents), "copy the actual tower switch branch script"))
		{
			return false;
		}
		for (const bool reset : { false, true })
		{
			SaveFileManager::CurrentPathScope currentPath(std::string("save\\object_animation_") + pack + (reset ? "_reset" : "_open"));
			GameManager gameManager;
			gameManager.varList.ensureInitialized();
			gameManager.mapFolderName = u8"map_046_通天塔第六层";
			auto first = gameManager.objectManager->addObject("lock_6_1.ini", 1, 1, 0);
			auto second = gameManager.objectManager->addObject("lock_6_2.ini", 2, 2, 0);
			if (!check(currentPath.valid() && first != nullptr && second != nullptr && second->res.image != nullptr &&
				second->res.image->frame.size() > 1, "load actual multi-frame tower switch resources"))
			{
				return false;
			}
			auto choice = std::make_shared<RecordingChooseMenu>();
			choice->requestedSelection = 0;
			gameManager.menu->chooseMenu = choice;
			gameManager.menu->dialog = std::make_shared<RecordingDialog>();
			gameManager.varList.setInteger("Event", 3182);
			gameManager.varList.setInteger("EvilValue", reset ? 0 : 120);
			gameManager.varList.setInteger("lock6", 77);
			for (int index = 1; index <= 8; ++index)
			{
				gameManager.varList.setInteger("Lock" + std::to_string(index), reset ? 1 : 0);
			}
			CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, "playsound", [](lua_State*) { return 0; });
			CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, "sleep", [](lua_State* state)
			{
				for (const auto& object : gm->objectManager->objectList)
				{
					object->setTime(object->getTime() + static_cast<UTime>(lua_tointeger(state, 1)));
					CoreLifecycleTestAccess::update(*object);
				}
				return 0;
			});
			const int result = gameManager.script.runScript(path);
			ok = check(result == LUA_OK &&
				gameManager.varList.getInteger("Lock6") == (reset ? 0 : 1) &&
				gameManager.varList.getInteger("lock6") == 77 &&
				second->nowAction == (reset ? oaClosing : oaOpening),
				"actual tower branches start opening or closing and preserve separate variable case") && ok;
			// The actual 36-frame image lasts 2160 ms. The script's 1000/2000 ms
			// wait plus an immediately recorded dialogue does not finish it yet.
			second->setTime(second->getTime() + second->actionLastTime);
			CoreLifecycleTestAccess::update(*second);
			ok = check(second->nowAction == oaStay && second->frame ==
				(reset ? 0 : static_cast<int>(second->res.image->frame.size()) - 1),
				"tower switch animation completes independently after the script has continued") && ok;
			second->setTime(10000);
			if (reset)
			{
				second->openBox();
			}
			else
			{
				second->closeBox();
			}
			const UTime elapsed = second->actionLastTime / 2;
			second->setTime(10000 + elapsed);
			ok = check(elapsed > 0 && gameManager.objectManager->save("tower-animation.obj") &&
				gameManager.objectManager->load("tower-animation.obj"), "save/load an actual switch list halfway through animation") && ok;
			auto loaded = gameManager.objectManager->findObj(u8"通天塔开关B");
			if (!check(loaded != nullptr, "the saved switch name is restored"))
			{
				return false;
			}
			CoreLifecycleTestAccess::update(*loaded);
			ok = check(loaded->nowAction == (reset ? oaOpening : oaClosing) && loaded->actionLastTime > elapsed,
				"an actual switch list resumes its remaining animation instead of snapping on its first update") && ok;
			loaded->setTime(loaded->getTime() + loaded->actionLastTime);
			CoreLifecycleTestAccess::update(*loaded);
			ok = check(loaded->nowAction == oaStay && loaded->frame ==
				(reset ? static_cast<int>(loaded->res.image->frame.size()) - 1 : 0),
				"the real switch completes at the correct endpoint after loading") && ok;
		}
	}
	return ok;
}

bool runProductionLevelRewardTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	if (!std::filesystem::exists(assetsRoot / "yycs/game_profile.ini"))
	{
		std::cout << "SKIP: optional production level reward resources are absent\n";
		return true;
	}
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid(), "level rewards use an isolated resource root"))
	{
		return false;
	}
	bool ok = true;
	for (const char* pack : { "yycs", u8"江湖余尘", u8"江湖余尘二", u8"月眉儿外传", u8"潇湘行" })
	{
		const bool hasBook = std::string(pack) == u8"潇湘行";
		const std::string rewardFile = hasBook ? u8"技能书.ini" : u8"player-magic-魂牵梦绕.ini";
		for (const char* table : { "level-easy.ini", "level-hard.ini" })
		{
			if (!hasBook && std::string(table) == "level-hard.ini")
			{
				continue;
			}
			for (const auto& path : { std::string("ini/level/") + table,
				std::string(hasBook ? "ini/goods/" : "ini/magic/") + rewardFile })
			{
				auto sourcePath = assetsRoot / std::filesystem::u8path(pack) / std::filesystem::u8path(path);
				// Yuemeier uses its declared YYCS dependency for this magic definition.
				if (!std::filesystem::exists(sourcePath) && std::string(pack) == u8"月眉儿外传")
				{
					sourcePath = assetsRoot / "yycs" / std::filesystem::u8path(path);
				}
				std::ifstream input(sourcePath, std::ios::binary);
				const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
				if (!check(!contents.empty() && writeVirtualFile(path, contents), "copy actual level table and reward definition"))
				{
					return false;
				}
			}
			SaveFileManager::CurrentPathScope currentPath(std::string("save\\level_reward_") + pack + table);
			GameManager gameManager;
			auto player = gameManager.player;
			player->levelIni = table;
			player->loadLevel(table);
			if (!check(currentPath.valid() && player->levelList.size() > 50, "load the real level reward table"))
			{
				return false;
			}
			const auto rewardCount = [&]()
			{
				return hasBook ? gameManager.goodsManager.getItemNum(rewardFile) :
					static_cast<int>(std::count_if(gameManager.magicManager.magicList.begin(), gameManager.magicManager.magicList.end(),
						[&](const MagicInfo& entry) { return entry.iniFile == rewardFile && entry.magic != nullptr; }));
			};
			player->setLevel(50);
			ok = check(rewardCount() == 0, "direct SetLevel must not award level-table rewards") && ok;
			player->setLevel(49);
			player->exp = 0;
			const std::string source = "addexp(" + std::to_string(player->levelUpExp + 1) + ");";
			auto bytes = std::make_unique<char[]>(source.size());
			std::copy(source.begin(), source.end(), bytes.get());
			ok = check(gameManager.script.runScript(bytes, static_cast<int>(source.size())) == LUA_OK &&
				player->level == 50 && rewardCount() == 1 && gameManager.menu->magicMenu == nullptr &&
				gameManager.menu->goodsMenu == nullptr,
				"actual level 50 reward is granted once through native AddExp before menus exist") && ok;
			if (!hasBook && gameManager.magicManager.findPrimaryMagic(rewardFile) != nullptr)
			{
				gameManager.magicManager.findPrimaryMagic(rewardFile)->level = 7;
			}
			ok = check(player->save(0) && gameManager.magicManager.save(0) && gameManager.goodsManager.save(0),
				"save the earned reward using player, magic and goods persistence") && ok;
			gameManager.magicManager.clearPrimaryMagicList();
			gameManager.goodsManager.clearItem();
			ok = check(player->load(0) && gameManager.magicManager.load(0) && gameManager.goodsManager.load(0) &&
				player->level == 50 && rewardCount() == 1, "earned level rewards survive actual file save/load") && ok;
			player->addExp(0);
			ok = check(rewardCount() == 1, "loading and adding no experience do not award the same reward again") && ok;
			player->setLevel(49);
			player->exp = 0;
			player->addExp(player->levelUpExp + 1);
			const auto learned = gameManager.magicManager.findPrimaryMagic(rewardFile);
			ok = check(rewardCount() == (hasBook ? 2 : 1) && (hasBook || (learned != nullptr && learned->level == 7)),
				"another award stacks goods but preserves an already learned magic and its level") && ok;
			gameManager.magicManager.clearPrimaryMagicList();
			gameManager.goodsManager.clearItem();
			player->setLevel(49);
			player->exp = 0;
			player->addExp(player->levelList[49].levelUpExp + 1);
			ok = check(player->level == 51 && rewardCount() == (hasBook ? 1 : 0),
				"one multi-level experience batch awards only its final level as in the reference implementation") && ok;
		}
	}
	return ok;
}

bool runPlayerLevelAttributeProtectionTests()
{
	GameManager gameManager;
	gameManager.global.levelUpThresholdMode = LevelUpThresholdMode::GreaterThanOrEqual;
	auto player = gameManager.player;
	player->exp = 0;
	const auto attributes = [&]()
	{
		return std::vector<int>{player->lifeMax, player->thewMax, player->manaMax,
			player->attack, player->attack2, player->attack3,
			player->defend, player->defend2, player->defend3, player->evade};
	};
	player->levelList.resize(3);
	for (int index = 0; index < 3; ++index)
	{
		auto& detail = player->levelList[index];
		const int value = index == 0 ? 100 : (index == 1 ? 10 : 30);
		detail.lifeMax = detail.thewMax = detail.manaMax = value;
		detail.attack = detail.attack2 = detail.attack3 = value;
		detail.defend = detail.defend2 = detail.defend3 = detail.evade = value;
		detail.levelUpExp = (index + 1) * 100;
	}
	player->setLevel(1);
	player->lifeMax += 50;
	const auto before = attributes();
	gameManager.scriptAPI.addExp(100);
	bool ok = check(player->level == 2 && attributes() == before,
		"experience leveling ignores all decreasing attributes and preserves permanent bonuses");
	gameManager.setCheatModeEnabled(true);
	ok = check(static_cast<bool>(gameManager.performCheatAction(GameManager::CheatAction::IncreasePlayerLevel)),
		"native cheat leveling shares attribute protection") && ok;
	auto expected = before;
	for (auto& value : expected) value += 20;
	ok = check(player->level == 3 && attributes() == expected,
		"positive attribute growth resumes after a decreasing table row") && ok;
	player->setLevel(1);
	player->attack = INT_MAX;
	player->levelList[0].attack = INT_MIN;
	player->levelList[1].attack = INT_MAX;
	player->updateLevel();
	ok = check(player->attack == INT_MAX,
		"level attribute growth handles wide differences and saturates at INT_MAX") && ok;

	ScopedActiveResourceRoot resourceRoot;
	const auto packRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets/xjxqy";
	if (!resourceRoot.valid()) return false;
	for (const char* table : { "level-easy.ini", "level-hard.ini" })
	{
		const std::string path = std::string("ini/level/") + table;
		std::ifstream input(packRoot / path, std::ios::binary);
		const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		if (!check(!contents.empty() && writeVirtualFile(path, contents), "copy actual XJXQY level table")) return false;
		player->loadLevel(table);
		player->setLevel(60);
		player->lifeMax += 50;
		while (player->level < static_cast<int>(player->levelList.size()))
		{
			const auto previous = attributes();
			player->updateLevel();
			const auto current = attributes();
			ok = check(std::equal(previous.begin(), previous.end(), current.begin(),
				[](int lower, int higher) { return higher >= lower; }) && player->life > 0,
				"actual XJXQY levels 61 through 80 preserve all upgraded attributes") && ok;
		}
	}
	return ok;
}

bool runProductionDifficultyTableTests()
{
	const auto packRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets/jxqy2";
	if (!std::filesystem::exists(packRoot / "game_profile.ini"))
	{
		std::cout << "SKIP: optional production difficulty resources are absent\n";
		return true;
	}
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid(), "difficulty selection uses an isolated resource root"))
	{
		return false;
	}
	for (const char* table : { "level-hard.ini", "level-easy.ini" })
	{
		const std::string path = std::string("ini/level/") + table;
		std::ifstream input(packRoot / path, std::ios::binary);
		const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		if (!check(!contents.empty() && writeVirtualFile(path, contents), "copy actual JXQY2 difficulty tables"))
		{
			return false;
		}
	}
	GameManager gameManager;
	auto player = gameManager.player;
	player->loadLevel("level-hard.ini");
	gameManager.scriptAPI.setPlayerLevel(9);
	bool ok = check(player->lifeMax == 1194 && player->manaMax == 165,
		"the initial hard table supplies the expected level-nine attributes");
	gameManager.scriptAPI.setLevelFile("level-easy.ini");
	gameManager.scriptAPI.setPlayerLevel(9);
	ok = check(player->levelIni == "level-easy.ini" && player->level == 9
		&& player->lifeMax == 1791 && player->life == 1791
		&& player->manaMax == 248 && player->mana == 248
		&& player->thewMax == 228 && player->attack == 525 && player->defend == 287
		&& player->levelUpExp == 1000,
		"native SetLevelFile then SetPlayerLevel recomputes the real Player from the easy table") && ok;
	auto playerKindNPC = std::make_shared<NPC>();
	playerKindNPC->kind = nkPlayer;
	gameManager.npcManager->npcList.push_back(playerKindNPC);
	gameManager.scriptAPI.setLevelFile("level-hard.ini");
	playerKindNPC->setLevel(9);
	ok = check(playerKindNPC->npcLevelIni == "level-hard.ini"
		&& playerKindNPC->lifeMax == 1194 && playerKindNPC->manaMax == 165
		&& player->levelIni == "level-easy.ini" && player->levelList.size() >= 9
		&& player->levelList[8].lifeMax == 1791
		&& player->lifeMax == 1791 && player->manaMax == 248,
		"the player-kind NPC branch still loads its NPC table without replacing the actual Player table") && ok;
	gameManager.npcManager->npcList.clear();
	gameManager.scriptAPI.setLevelFile("level-hard.ini");
	gameManager.scriptAPI.setPlayerLevel(9);
	ok = check(player->levelIni == "level-hard.ini" && player->lifeMax == 1194
		&& player->manaMax == 165 && player->levelUpExp == 2000,
		"switching the actual Player back to hard replaces the selected difficulty table") && ok;
	return ok;
}

bool runProductionAttributeStoryTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	if (!std::filesystem::exists(assetsRoot / "yycs/game_profile.ini"))
	{
		std::cout << "SKIP: optional production attribute story resources are absent\n";
		return true;
	}
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid(), "attribute stories use an isolated resource root"))
	{
		return false;
	}
	bool ok = true;
	for (const char* pack : { "yycs", u8"江湖余尘", u8"江湖余尘二" })
	{
		const auto packRoot = assetsRoot / std::filesystem::u8path(pack);
		for (const char* path : { "ini/save/player0.ini", "ini/level/level-easy.ini", "ini/level/level-hard.ini",
			u8"script/map/map_002_凌绝峰峰顶/begin.txt" })
		{
			std::ifstream input(packRoot / std::filesystem::u8path(path), std::ios::binary);
			const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
			if (!check(!contents.empty() && writeVirtualFile(path, contents), "copy actual difficulty scripts and level/player tables"))
			{
				return false;
			}
		}
		for (int selection : { 0, 1 })
		{
			SaveFileManager::CurrentPathScope currentPath(std::string("save\\attribute_story_") + pack + std::to_string(selection));
			GameManager gameManager;
			gameManager.varList.ensureInitialized();
			auto player = gameManager.player;
			if (!check(currentPath.valid() && player->loadInitialTemplate(0), "load the production starting player"))
			{
				return false;
			}
			player->calInfo();
			const int initialLevel = player->level;
			const int initialLifeMax = player->lifeMax;
			const bool easyTargetsPlayer = selection == 1 && player->npcName == u8"杨影枫";
			auto choice = std::make_shared<RecordingChooseMenu>();
			choice->requestedSelection = selection;
			gameManager.menu->chooseMenu = choice;
			gameManager.menu->dialog = std::make_shared<RecordingDialog>();
			gameManager.varList.setInteger("level", 77);
			gameManager.mapFolderName = u8"map_002_凌绝峰峰顶";
			for (const char* command : { "fadein", "fadeout", "sleep", "playmusic", "playergoto",
				"npcspecialaction", "setnpcactionfile", "loadmap", "loadnpc", "loadobj", "saveobj" })
			{
				CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, command, [](lua_State*) { return 0; });
			}
			ok = check(gameManager.script.runScript(u8"script/map/map_002_凌绝峰峰顶/begin.txt") == LUA_OK &&
				gameManager.menu->stateMenu == nullptr && player->level == (easyTargetsPlayer ? 5 : initialLevel) &&
				player->levelIni == (selection == 1 ? "level-easy.ini" : "level-hard.ini") &&
				gameManager.varList.getInteger("Level") == selection && gameManager.varList.getInteger("level") == 77 &&
				(easyTargetsPlayer ? player->lifeMax == player->levelList[4].lifeMax && player->life == player->getLifeMax() :
					player->lifeMax == initialLifeMax),
				"actual difficulty scripts change tables and set level five only when the explicit character name matches") && ok;
			const int level = player->level;
			const int lifeMax = player->lifeMax;
			const std::string levelIni = player->levelIni;
			ok = check(player->save(0) && player->load(0) && player->level == level &&
				player->lifeMax == lifeMax && player->levelIni == levelIni && !player->levelList.empty(),
				"the selected difficulty table and level survive player-file save/load") && ok;
		}
	}
	for (const char* pack : { "yycs", u8"江湖余尘" })
	{
		const auto packRoot = assetsRoot / std::filesystem::u8path(pack);
		std::ifstream input(packRoot / std::filesystem::u8path(u8"script/map/map_016_剑气峰/事件25.txt"), std::ios::binary);
		const std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		GameManager gameManager;
		gameManager.varList.ensureInitialized();
		auto player = gameManager.player;
		player->lifeMax = player->thewMax = player->manaMax = 100;
		player->calInfo();
		player->life = player->thew = player->mana = 50;
		gameManager.menu->dialog = std::make_shared<RecordingDialog>();
		for (const char* command : { "playerchange", "loadmap", "loadnpc", "loadobj", "playergoto", "npcgoto",
			"setnpcactionfile", "fadein", "fadeout", "sleep", "playmusic", "stopmusic" })
		{
			CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, command, [](lua_State*) { return 0; });
		}
		CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, "npcspecialaction", [](lua_State*)
		{
			gm->varList.setInteger("DrainedMana", gm->player->mana);
			gm->varList.setInteger("DrainedThew", gm->player->thew);
			gm->varList.setInteger("WasManaLocked", !gm->player->canUseMana);
			return 0;
		});
		auto bytes = std::make_unique<char[]>(source.size());
		std::copy(source.begin(), source.end(), bytes.get());
		ok = check(!source.empty() && gameManager.script.runScript(bytes, static_cast<int>(source.size())) == LUA_OK &&
			gameManager.varList.getInteger("DrainedMana") == 0 && gameManager.varList.getInteger("DrainedThew") == 0 &&
			gameManager.varList.getInteger("WasManaLocked") == 1 &&
			gameManager.varList.getInteger("Event") == 250 && player->thew == 100 && player->mana == 100 && !player->canUseMana,
			"the actual seashore recovery story depletes at zero then restores both attributes without unlocking magic") && ok;
	}
	return ok;
}

bool runProductionMaskStoryTests()
{
	const auto packRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()
		/ "assets" / "jxqy2";
	ScopedActiveResourceRoot resourceRoot;
	File::setResourceFallbackRoots({ packRoot.generic_string() });
	SaveFileManager::CurrentPathScope currentPath("save/mask_story_contracts");
	GameManager gameManager;
	gameManager.varList.ensureInitialized();
	gameManager.mapFolderName = u8"临安城";
	gameManager.goodsManager.configureLayout();
	bool ok = check(resourceRoot.valid() && currentPath.valid(), "mask story uses isolated saves and production resources");
	const std::string mask = u8"goods-sj-3-面具.ini";
	for (const int stage : { 14, 15, 16, 17, 18, 19, 20, 21 })
	{
		gameManager.goodsManager.clearItem();
		gameManager.varList.setInteger("FromFengChi", stage);
		gameManager.varList.setInteger("LinAnMianJu", 0);
		gameManager.varList.setInteger("linanmianju", 71);
		ok = check(gameManager.goodsManager.addItem(mask, 2), "two actual masks load for repeated use") && ok;
		for (int use = 0; use < 2; ++use)
		{
			ok = check(gameManager.goodsManager.useItem(gameManager.goodsManager.storeBegin())
				&& gameManager.varList.getInteger("FromFengChi") == stage
				&& gameManager.varList.getInteger("LinAnMianJu") == ((stage == 15 || stage == 16) ? 1 : 0)
				&& gameManager.varList.getInteger("linanmianju") == 71,
				"actual mask use consumes the item and only enables the intended quest stages") && ok;
		}
		ok = check(!gameManager.goodsManager.goodsListExists(gameManager.goodsManager.storeBegin()),
			"each actual mask use consumes one item") && ok;
	}
	File::setResourceFallbackRoots({});
	return ok;
}

bool runProductionWudangGateTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	const auto packRoot = assetsRoot / std::filesystem::u8path(u8"江湖余尘");
	if (!std::filesystem::exists(packRoot / "game_profile.ini"))
	{
		std::cout << "SKIP: optional production Wudang dialogue pack is absent\n";
		return true;
	}
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid(), "Wudang gate uses an isolated resource root"))
	{
		return false;
	}
	for (const char* path : { u8"script/map/map_006_武当山山顶/trap03.txt",
		u8"script/map/map_006_武当山山顶/挑战武当.txt", "ini/save/traps.ini",
		"ini/save/wudangshanding.npc", "ini/save/wudangshanding1.npc", "talkindex.txt" })
	{
		const auto sourceRoot = std::string(path) == "talkindex.txt" ? assetsRoot / "yycs" : packRoot;
		std::ifstream input(sourceRoot / std::filesystem::u8path(path), std::ios::binary);
		const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		if (!check(!contents.empty() && writeVirtualFile(path, contents), "copy actual Wudang scripts, bindings and NPC lists"))
		{
			return false;
		}
	}
	bool ok = true;
	for (const int selection : { 0, 1 })
	{
		SaveFileManager::CurrentPathScope currentPath("save\\wudang_gate_" + std::to_string(selection));
		if (!check(currentPath.valid(), "Wudang branch has its own current-save generation"))
		{
			return false;
		}
		GameManager gameManager;
		gameManager.varList.ensureInitialized();
		gameManager.mapFolderName = u8"map_006_武当山山顶";
		gameManager.player->npcName = u8"杨影枫";
		gameManager.talkTextList.load();
		auto dialog = std::make_shared<RecordingDialog>();
		auto choice = std::make_shared<RecordingChooseMenu>();
		choice->requestedSelection = selection;
		gameManager.menu->dialog = dialog;
		gameManager.menu->chooseMenu = choice;
		// Keep the actual trap, Select/Talk, RunScript, NPC load/save and state
		// changes; omit motion, waits and audio/visual presentation only.
		for (const char* command : { "playergoto", "playergotoex", "npcgoto", "sleep",
			"fadeout", "fadein", "stopmusic", "playmusic" })
		{
			CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, command, [](lua_State*) { return 0; });
		}
		ok = check(gameManager.traps.loadInitialTemplate() &&
			gameManager.traps.get(gameManager.mapFolderName, 3) == "trap03.txt" &&
			gameManager.scriptAPI.loadNPC("wudangshanding.npc"), "load the production gate binding and guards") && ok;
		gameManager.varList.setInteger("Event", 39);
		gameManager.runTrapScript(3);
		ok = check(dialog->entries.empty() && choice->entries.empty() &&
			gameManager.traps.get(gameManager.mapFolderName, 3) == "trap03.txt",
			"the gate remains inactive before Event 40 without consuming its binding") && ok;
		gameManager.traps.beginMapVisit(); // A separate visit after the Washing Sword Pool fight.
		gameManager.varList.setInteger("Event", 40);
		gameManager.varList.setInteger("event", 91);
		gameManager.varList.setInteger("SelectVal", -1);
		gameManager.varList.setInteger("selectval", 92);
		gameManager.varList.setInteger("EvilVal", 100);
		gameManager.runTrapScript(3);
		const std::string innerThought = u8"杨影枫：（我扬名天下的机会终于到了，待会儿一定要好好把握。）";
		const auto thoughtCount = std::count_if(dialog->entries.begin(), dialog->entries.end(), [&](const auto& entry)
		{
			return entry.first == innerThought;
		});
		ok = check(thoughtCount == (selection == 0 ? 1 : 0),
			"the actual Wudang waiting branch shows index 287 exactly once; forcing entry does not") && ok;
		ok = check(choice->entries == std::vector<std::vector<std::string>>{
			{ u8"请选择：", u8"A：站在道观门口等待", u8"B：一怒之下杀进道观" } } &&
			gameManager.varList.getInteger("SelectVal") == selection &&
			gameManager.varList.getInteger("selectval") == 92 && gameManager.varList.getInteger("event") == 91 &&
			gameManager.varList.getInteger("Event") == (selection == 0 ? 44 : 41) &&
			gameManager.varList.getInteger("EvilVal") == (selection == 0 ? 102 : 40) &&
			gameManager.traps.get(gameManager.mapFolderName, 3).empty() && !gameManager.inEvent,
			"both production Wudang branches keep their current quest, morality and case-sensitive variable behavior") && ok;
		if (selection == 0)
		{
			const auto leaders = gameManager.npcManager->findNPC(u8"天星道长");
			ok = check(gameManager.traps.get(gameManager.mapFolderName, 8) == "trap08.txt" &&
				leaders.size() == 1 && leaders.front()->scriptFile == u8"天星道长对话.txt",
				"the real nested challenge script installs the leader interaction and battle trap") && ok;
		}
		const auto dialogueCount = dialog->entries.size();
		gameManager.runTrapScript(3);
		ok = check(dialog->entries.size() == dialogueCount && choice->entries.size() == 1,
			"the consumed gate cannot repeat the dialogue or selection") && ok;
	}
	return ok;
}

bool runProductionArenaSelfMagicAI(GameManager& gameManager, const std::shared_ptr<NPC>& boss)
{
	const auto originalOptions = boss->attackOptions;
	const bool originalDisabled = boss->isAIDisabled;
	const Point playerPosition = gameManager.player->getPosition();
	const int originalAttack = boss->getAttack();
	gameManager.player->setPosition({ 21, 31 }, false);
	boss->beginStand();
	boss->clearCombatTargetMemory();
	boss->isAIDisabled = false;
	gameManager.npcManager->scheduleBattleAction(boss);
	bool ok = check(boss->relation == nrNeutral && !boss->isAttacking(),
		"the original neutral arena boss does not acquire an automatic enemy merely because self magic is selectable");
	const auto candidates = boss->buildAttackCandidates(gameManager.player->getPosition());
	ok = check(std::count_if(candidates.begin(), candidates.end(), [](const auto& candidate)
		{
			return candidate.option.moveKind == mmkSelf && candidate.canHitNow;
		}) == 3, "the actual low-life seven-entry arena list includes healing, morph and range BUFF candidates") && ok;
	std::vector<NPCAttackOption> selfOptions;
	for (const auto& option : originalOptions)
	{
		if (option.magic->iniName != u8"wd_120_变身.ini" && option.magic->iniName != u8"wd_120_变身_BUFF.ini") continue;
		selfOptions.push_back(option);
		gameManager.effectManager->freeResource();
		boss->attackOptions = { option };
		// Set only the test combat condition; retain the production NPC file and
		// do not invent a story script that turns this neutral boss hostile.
		boss->relation = nrHostile;
		for (int cast = 0; cast < 2; ++cast)
		{
			boss->beginStand();
			boss->clearCombatTargetMemory();
			boss->isAIDisabled = false;
			boss->idledFrame = boss->idle;
			const auto beforeCount = gameManager.effectManager->effectList.size();
			const bool scheduled = gameManager.npcManager->scheduleBattleAction(boss);
			boss->isAIDisabled = true;
			if (!check(scheduled && boss->isAttacking() && boss->actionLastTime > 1,
				"the real battle scheduler starts an attack animation for an isolated production self magic"))
			{
				ok = false;
				continue;
			}
			CoreLifecycleTestAccess::advanceActorFrame(*boss, boss->actionLastTime - 1);
			ok = check(gameManager.effectManager->effectList.size() == beforeCount,
				"AI-selected self magic is not released before its attack animation ends") && ok;
			CoreLifecycleTestAccess::advanceActorFrame(*boss, 1);
			if (check(gameManager.effectManager->effectList.size() == beforeCount + 1,
				"AI-selected production self magic releases once, including a second active cast"))
			{
				const auto effect = gameManager.effectManager->effectList.back();
				ok = check(effect->magic.iniName == option.magic->iniName && effect->level == 10
					&& effect->user.lock() == boss && effect->position == boss->getPosition(),
					"self magic selected against the player is anchored to the original level-10 caster") && ok;
			}
			else ok = false;
			if (option.magic->iniName == u8"wd_120_变身.ini")
			{
				ok = check(boss->morphMilliseconds == 2500 && boss->temporaryNpcResFile == "boss001c.ini"
					&& boss->res.stand.imagePackage && boss->getAttack() == originalAttack * 150 / 100,
					"AI morph and recast use the real replacement without compounding its 50 percent attack bonus") && ok;
			}
		}
		gameManager.effectManager->freeResource();
		CoreLifecycleTestAccess::advanceActorFrame(*boss, 2500);
		ok = check(boss->morphMilliseconds == 0 && boss->getAttack() == originalAttack,
			"AI morph expiry restores the original attack before the next production skill check") && ok;
	}
	for (auto option : selfOptions)
	{
		if (option.magic->iniName != u8"wd_120_变身_BUFF.ini") continue;
		auto admissionMagic = std::make_shared<Magic>();
		admissionMagic->copy(*option.magic);
		admissionMagic->disableUse = 1;
		admissionMagic->lifeFullToUse = 1;
		option.magic = admissionMagic;
		const int originalLife = boss->life;
		boss->life = boss->getLifeMax();
		boss->attackOptions = { option };
		boss->beginStand();
		boss->beginAttack(gameManager.player->getPosition(), gameManager.player);
		ok = check(boss->isAttacking() && boss->actionLastTime > 1,
			"normal NPC attack admits a full-life production BUFF regardless of player-only DisableUse") && ok;
		if (boss->isAttacking() && boss->actionLastTime > 1)
		{
			boss->life -= 1;
			CoreLifecycleTestAccess::advanceActorFrame(*boss, boss->actionLastTime - 1);
			ok = check(gameManager.effectManager->effectList.empty(), "NPC life loss does not release its prepared BUFF early") && ok;
			CoreLifecycleTestAccess::advanceActorFrame(*boss, 1);
			ok = check(gameManager.effectManager->effectList.size() == 1
				&& gameManager.effectManager->effectList.back()->magic.iniName == admissionMagic->iniName
				&& gameManager.effectManager->effectList.back()->user.lock() == boss,
				"NPC attack releases its admitted BUFF at the actual final frame after life loss") && ok;
		}
		boss->life = originalLife;
		gameManager.effectManager->freeResource();
	}
	boss->attackOptions = selfOptions;
	bool sawFirst = false;
	bool sawSecond = false;
	for (int selection = 0; selection < 128 && selfOptions.size() == 2; ++selection)
	{
		const auto ready = boss->findReadyAttackOption(gameManager.player->getPosition());
		if (ready)
		{
			sawFirst = sawFirst || ready->magic == selfOptions[0].magic;
			sawSecond = sawSecond || ready->magic == selfOptions[1].magic;
		}
	}
	ok = check(sawFirst && sawSecond, "repeated real ready-option selection can leave the last used self magic") && ok;
	boss->attackOptions = originalOptions;
	boss->relation = nrNeutral;
	boss->isAIDisabled = originalDisabled;
	boss->beginStand();
	boss->clearCombatTargetMemory();
	gameManager.player->setPosition(playerPosition, false);
	std::cout << "Arena self magic AI checked: skills=" << selfOptions.size() << " casts=4 selections=128\n";
	return ok;
}

bool runProductionArenaAttackAdmission(GameManager& gameManager, const std::shared_ptr<NPC>& boss)
{
	const auto originalOptions = boss->attackOptions;
	const int originalLife = boss->life;
	const int originalRelation = boss->relation;
	const bool originalDisabled = boss->isAIDisabled;
	const Point playerPosition = gameManager.player->getPosition();
	auto found = std::find_if(originalOptions.begin(), originalOptions.end(), [](const auto& option)
	{
		return option.magic && option.magic->iniName == u8"wd_120_变身_BUFF.ini";
	});
	if (!check(found != originalOptions.end(), "attack admission uses the actual arena BUFF resources")) return false;
	const auto ordinaryOption = *found;
	auto fullLifeOption = ordinaryOption;
	fullLifeOption.magic = std::make_shared<Magic>();
	fullLifeOption.magic->copy(*ordinaryOption.magic);
	// Only this in-memory copy gains the flag; no production resource is edited.
	fullLifeOption.magic->lifeFullToUse = 1;
	fullLifeOption.magic->disableUse = 1;
	gameManager.player->setPosition({ 21, 31 }, false);
	boss->relation = nrHostile;
	const Point destination = gameManager.player->getPosition();
	const auto reset = [&]()
	{
		boss->isAIDisabled = true;
		boss->actionManager->restartActionIgnoringTransitions(acStand);
		boss->clearPreparedAttackMagic();
		boss->clearCombatTargetMemory();
		boss->life = boss->getLifeMax() - 1;
		gameManager.effectManager->freeResource();
	};
	bool ok = true;
	for (int entry = 0; entry < 3; ++entry)
	{
		for (int attempt = 0; attempt < 3; ++attempt)
		{
			reset();
			boss->attackOptions = { fullLifeOption };
			if (entry == 2)
			{
				boss->isAIDisabled = false;
				boss->idledFrame = boss->idle;
				gameManager.npcManager->scheduleBattleAction(boss);
				boss->isAIDisabled = true;
			}
			else boss->beginAttack(destination, entry == 0 ? nullptr : gameManager.player);
			ok = check(!boss->isAttacking() && !boss->hasPreparedAttackMagic,
				"under-full-life direct, locked and scheduled attacks reject before starting an empty animation") && ok;
			boss->life = boss->getLifeMax();
			CoreLifecycleTestAccess::advanceActorFrame(*boss, std::max<UTime>(boss->actionLastTime, 5000) + 1);
			ok = check(gameManager.effectManager->effectList.empty(),
				"recovering life after a rejected request cannot cause an animation-end late selection or release") && ok;
		}
	}
	reset();
	boss->attackOptions = { fullLifeOption, ordinaryOption };
	const auto candidates = boss->buildAttackCandidates(destination);
	ok = check(candidates.size() == 2, "full-life admission does not silently change the existing distance candidate population") && ok;
	boss->prepareImmediateAttackPlan(gameManager.player, fullLifeOption, destination);
	boss->beginAttack(destination, gameManager.player);
	ok = check(!boss->isAttacking() && !boss->hasPreparedAttackMagic,
		"an explicitly planned but inadmissible attack is rejected even when another ordinary skill exists") && ok;
	CoreLifecycleTestAccess::advanceActorFrame(*boss, std::max<UTime>(boss->actionLastTime, 5000) + 1);
	ok = check(gameManager.effectManager->effectList.empty(),
		"an empty attack cannot choose the ordinary skill at its old animation end") && ok;
	for (bool fullLife : { false, true })
	{
		reset();
		const auto selected = fullLife ? fullLifeOption : ordinaryOption;
		if (fullLife) boss->life = boss->getLifeMax();
		boss->prepareImmediateAttackPlan(gameManager.player, selected, destination);
		boss->beginAttack(destination, gameManager.player);
		ok = check(boss->isAttacking() && boss->actionLastTime > 1 && boss->preparedAttackMagic == selected.magic,
			"an admitted mixed-list attack retains exactly the originally planned skill") && ok;
		if (!boss->isAttacking() || boss->actionLastTime <= 1) continue;
		const UTime duration = boss->actionLastTime;
		boss->life = boss->getLifeMax() - 1;
		boss->beginAttack({ 40, 40 }, nullptr);
		ok = check(boss->preparedAttackMagic == selected.magic,
			"a busy request cannot replace the prepared attack after life changes") && ok;
		CoreLifecycleTestAccess::advanceActorFrame(*boss, duration - 1);
		ok = check(gameManager.effectManager->effectList.empty(), "admitted mixed-list attack waits until its actual release frame") && ok;
		CoreLifecycleTestAccess::advanceActorFrame(*boss, 1);
		ok = check(gameManager.effectManager->effectList.size() == 1 && !boss->hasPreparedAttackMagic,
			"admitted mixed-list attack releases once and clears preparation even after life loss") && ok;
	}
	reset();
	boss->attackOptions = originalOptions;
	boss->life = originalLife;
	boss->relation = originalRelation;
	boss->isAIDisabled = originalDisabled;
	gameManager.player->setPosition(playerPosition, false);
	std::cout << "Arena attack admission checked: rejected=10 admitted=2\n";
	return ok;
}

bool runProductionArenaMagicLifecycle(GameManager& gameManager, std::shared_ptr<NPC> boss, bool asynchronous)
{
	gameManager.effectManager->freeResource();
	const auto originalOptions = boss->attackOptions;
	bool ok = check(originalOptions.size() == 7 && boss->getClampedAttackLevel() == 10 && boss->relation == nrNeutral,
		"arena attack checks retain the actual seven entries, AttackLevel and neutral relation");
	ok = runProductionArenaSelfMagicAI(gameManager, boss) && ok;
	ok = runProductionArenaAttackAdmission(gameManager, boss) && ok;
	int attacks = 0;
	for (const auto& option : originalOptions)
	{
		// Isolate a real entry for deterministic release checks. AI selection and
		// the blocking full-screen presentation require separate scene coverage.
		if (!option.isTargetAttack || option.moveKind == mmkFullScreen) continue;
		gameManager.effectManager->freeResource();
		boss->attackOptions = { option };
		boss->beginStand();
		boss->beginAttack({ 21, 31 }, nullptr);
		if (!check(boss->isAttacking() && boss->actionLastTime > 1 && gameManager.effectManager->effectList.empty(),
			"a real arena attack animation starts before releasing its isolated original magic"))
		{
			ok = false;
			continue;
		}
		const UTime duration = boss->actionLastTime;
		CoreLifecycleTestAccess::advanceActorFrame(*boss, duration - 1);
		ok = check(boss->isAttacking() && gameManager.effectManager->effectList.empty(),
			"arena attack magic waits for the last animation tick") && ok;
		CoreLifecycleTestAccess::advanceActorFrame(*boss, 1);
		const auto effects = gameManager.effectManager->effectList;
		ok = check(boss->isStanding() && !effects.empty()
			&& std::all_of(effects.begin(), effects.end(), [&](const auto& effect)
			{
				return effect->magic.iniName == option.magic->iniName && effect->level == 10 && effect->user.lock() == boss;
			}), "arena animation releases its actual level-10 magic with the original caster") && ok;
		CoreLifecycleTestAccess::advanceActorFrame(*boss, 1);
		ok = check(gameManager.effectManager->effectList.size() == effects.size(),
			"an arena attack is not released twice") && ok;
		if (option.moveKind == mmkSummon && !effects.empty())
		{
			const auto effect = effects.front();
			const auto summoned = effect->summonedNPC.lock();
			ok = check(summoned && summoned->transientSummonedNPC && summoned->relation == boss->relation
				&& summoned->res.stand.imagePackage && summoned->res.death.imagePackage
				&& effect->lifeTime == 500000 && boss->summonedNpcsCount(*option.magic) == 1,
				"the real DaoTong summon inherits the caster relation, animation and 500000ms lifetime") && ok;
			if (summoned)
			{
				CoreLifecycleTestAccess::advanceActorFrame(*effect, effect->lifeTime - 1);
				ok = check(!summoned->isDying() && !effect->vanishing,
					"the production summon remains alive before its exact expiry") && ok;
				CoreLifecycleTestAccess::advanceActorFrame(*effect, 1);
				ok = check(summoned->isDying() && effect->vanishing,
					"the production summon expires through its real death action") && ok;
				CoreLifecycleTestAccess::advanceActorFrame(*summoned, summoned->actionLastTime + 1);
				gameManager.npcManager->onUpdate();
				ok = check(!gameManager.npcManager->findNPC(summoned) && boss->summonedNpcsCount(*option.magic) == 0,
					"expired summon cleanup releases its caster inventory slot") && ok;
			}
		}
		++attacks;
		std::cout << "Arena animated attack checked: " << option.magic->iniName << " effects=" << effects.size() << '\n';
	}
	boss->attackOptions = originalOptions;
	gameManager.effectManager->freeResource();
	const int beforeHealing = boss->life;
	ok = check(boss->trySelfBuff() && boss->life > beforeHealing
		&& gameManager.effectManager->effectList.size() == 1
		&& gameManager.effectManager->effectList.front()->magic.iniName == u8"magic-碧血丹心.ini",
		"the actual low-life boss can select its original healing magic through the existing self-buff logic") && ok;
	gameManager.effectManager->freeResource();
	const auto originalStandImage = boss->res.stand.imagePackage;
	const int originalAttack = boss->getAttack();
	const float originalSpeed = boss->getAdjustedWalkSpeed();
	gameManager.scriptAPI.npcUseMagic("boss001", u8"wd_120_变身.ini", 21, 31, 10);
	std::cout << "Arena morph values: remaining=" << boss->morphMilliseconds
		<< " resource=" << boss->temporaryNpcResFile << " image=" << bool(boss->res.stand.imagePackage)
		<< " replaced=" << (boss->res.stand.imagePackage != originalStandImage)
		<< " attack=" << originalAttack << "->" << boss->getAttack()
		<< " speed=" << originalSpeed << "->" << boss->getAdjustedWalkSpeed() << '\n';
	ok = check(boss->morphMilliseconds == 2500 && boss->temporaryNpcResFile == "boss001c.ini"
		&& boss->res.stand.imagePackage && boss->res.stand.imagePackage != originalStandImage
		&& boss->res.stand1.imagePackage && boss->res.walk.imagePackage && boss->res.attack.imagePackage
		&& boss->res.attack1.imagePackage && boss->res.hurt.imagePackage && boss->res.death.imagePackage
		&& boss->getAttack() == originalAttack * 150 / 100
		&& std::abs(boss->getAdjustedWalkSpeed() - originalSpeed * 1.5f) < 0.001f,
		"the actual morph uses Effect milliseconds and applies its resource, attack and movement modifiers") && ok;
	CoreLifecycleTestAccess::advanceActorFrame(*boss, 2499);
	ok = check(boss->morphMilliseconds == 1 && boss->temporaryNpcResFile == "boss001c.ini",
		"arena morph retains its replacement until the last millisecond") && ok;
	CoreLifecycleTestAccess::advanceActorFrame(*boss, 1);
	ok = check(boss->morphMilliseconds == 0 && boss->temporaryNpcResFile.empty()
		&& boss->res.stand.imagePackage == originalStandImage && boss->getAttack() == originalAttack
		&& std::abs(boss->getAdjustedWalkSpeed() - originalSpeed) < 0.001f,
		"arena morph expiry restores the original animation, attack and speed") && ok;
	gameManager.effectManager->freeResource();
	gameManager.scriptAPI.npcUseMagic("boss001", u8"wd_120_变身_BUFF.ini", 21, 31, 10);
	if (check(gameManager.effectManager->effectList.size() == 1, "the actual arena range BUFF creates one self effect"))
	{
		const auto effect = gameManager.effectManager->effectList.front();
		CoreLifecycleTestAccess::advanceActorFrame(*effect, effect->waitTime);
		const UTime duration = effect->lifeTime;
		const Point originalPosition = boss->getPosition();
		boss->setPosition({ 21, 31 }, false);
		CoreLifecycleTestAccess::advanceActorFrame(*effect, 1250);
		ok = check(duration == 10000 && effect->position == boss->getPosition() && !effect->vanishing,
			"the actual range BUFF uses 10ms LifeFrame units and follows its owner beyond one image cycle") && ok;
		if (!check(gameManager.saveGame(3), "save a complete arena slot while the actual range BUFF is active")) return false;
		const bool buffLoaded = asynchronous ? gameManager.scriptAPI.loadGameAsync(3) : gameManager.loadGame(3);
		const auto loadedBosses = gameManager.npcManager->findNPC("boss001");
		const auto loadedBuff = std::find_if(gameManager.effectManager->effectList.begin(),
			gameManager.effectManager->effectList.end(), [](const auto& candidate)
			{
				return candidate && candidate->magic.iniName == u8"wd_120_变身_BUFF.ini";
			});
		if (!check(buffLoaded && loadedBosses.size() == 1 && loadedBuff != gameManager.effectManager->effectList.end(),
			"complete sync/async reload restores the actual active range BUFF and caster")) return false;
		boss = loadedBosses.front();
		const auto restoredEffect = *loadedBuff;
		const UTime elapsed = restoredEffect->getTime() - restoredEffect->beginTime;
		ok = check(restoredEffect != effect && restoredEffect->user.lock() == boss
			&& !restoredEffect->vanishing && restoredEffect->lifeTime > elapsed
			&& restoredEffect->lifeTime - elapsed == 8750,
			"active range BUFF reload preserves the remaining 8750ms instead of restarting or truncating it") && ok;
		CoreLifecycleTestAccess::advanceActorFrame(*restoredEffect, 8749);
		ok = check(!restoredEffect->vanishing && !(restoredEffect->result & erLifeExhaust),
			"the restored range BUFF remains active immediately before expiry") && ok;
		CoreLifecycleTestAccess::advanceActorFrame(*restoredEffect, 1);
		ok = check(restoredEffect->vanishing || (restoredEffect->result & erLifeExhaust),
			"the restored range BUFF expires without gaining time from save/load") && ok;
		std::cout << "Arena range BUFF lifetime: " << duration << "ms, source LifeFrame="
			<< effect->magic.level[effect->level].lifeFrame << '\n';
		boss->setPosition(originalPosition, false);
	}
	else ok = false;
	gameManager.effectManager->freeResource();
	const auto summonMagic = gameManager.magicManager.loadAttackMagic(u8"wd_120_召唤.ini");
	const auto persistentCount = gameManager.npcManager->npcList.size();
	gameManager.scriptAPI.npcUseMagic("boss001", u8"wd_120_召唤.ini", 21, 31, 10);
	ok = check(boss->summonedNpcsCount(*summonMagic) == 1 && gameManager.saveGame(2),
		"a complete arena slot saves successfully while a real summon is active") && ok;
	const bool loaded = asynchronous ? gameManager.scriptAPI.loadGameAsync(2) : gameManager.loadGame(2);
	const auto restored = gameManager.npcManager->findNPC("boss001");
	ok = check(loaded && restored.size() == 1 && gameManager.npcManager->npcList.size() == persistentCount
		&& restored.front()->summonedNpcsCount(*summonMagic) == 0 && gameManager.effectManager->effectList.empty(),
		"complete arena reload excludes the transient summon and its effect without dropping ordinary NPCs") && ok;
	gameManager.scriptAPI.npcUseMagic("boss001", u8"wd_120_召唤.ini", 21, 31, 10);
	ok = check(restored.size() == 1 && restored.front()->summonedNpcsCount(*summonMagic) == 1,
		"a fresh arena caster can summon again after complete reload") && ok;
	ok = check(attacks == 3, "three non-self, non-fullscreen entries executed their real arena attack animations") && ok;
	std::cout << "Arena magic lifecycle checked: async=" << asynchronous << " attacks=" << attacks << '\n';
	return ok;
}

bool runProductionYuchenArenaRoutes()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	const auto packRoot = assetsRoot / std::filesystem::u8path(u8"江湖余尘");
	const char* mapOverride = std::getenv("JXQY_TEST_TMX_RESOURCE_ROOT");
	const char* dependencyOverride = std::getenv("JXQY_TEST_ARENA_DEPENDENCY_ROOT");
	const std::string mapInput = mapOverride != nullptr && mapOverride[0] != '\0' ? mapOverride : packRoot.u8string();
	const std::string dependencyInput = dependencyOverride != nullptr && dependencyOverride[0] != '\0'
		? dependencyOverride : packRoot.u8string();
	const char* convertedRoot = mapInput.c_str();
	const char* dependencyRoot = dependencyInput.c_str();
	if (!std::filesystem::exists(std::filesystem::u8path(mapInput) / std::filesystem::u8path(u8"map/map_066_比武台.tmx")))
	{
		std::cout << "SKIP: optional production arena map is absent\n";
		return true;
	}
	std::cout << "Arena input roots: map=" << mapInput << " dependencies=" << dependencyInput << std::endl;
	const std::string arenaMap = u8"map_066_比武台.tmx";
	const std::string arenaFolder = u8"map_066_比武台";
	const std::string arenaNpcs = u8"tmx_map_066_比武台.npc";
	class ArenaChooseMenu final : public ChooseMenu
	{
	public:
		int calls = 0;
		bool actionsHandled = false;
		std::string speaker;
	private:
		void onRun() override
		{
			++calls;
			speaker = CoreLifecycleTestAccess::choiceSpeaker(*this);
			actionsHandled = CoreLifecycleTestAccess::chooseWithSemanticActions(*this, 2);
			// Terminate even on a failed focus assertion; do not hang the test menu.
			logicRunning = false;
		}
	};
	struct Entrance
	{
		const char* map;
		const char* npcs;
		const char* objects;
		const char* script;
		const char* speaker;
	};
	const Entrance entrances[] =
	{
		{ u8"map_002_凌绝峰峰顶.map", "map002.npc", "map002_obj.obj", u8"多选对话.txt", u8"百晓生" },
		{ u8"map_003_武当山下.map", "wudangshanxia.npc", "map003_obj.obj", u8"酒肆老板对话.txt", u8"酒肆老板" }
	};
	const bool previousLoadAsync = Config::loadAsync;
	bool ok = true;
	for (bool asynchronous : { false, true })
	{
		Config::loadAsync = asynchronous;
		for (const auto& entrance : entrances)
		{
			ScopedActiveResourceRoot resourceRoot;
			bool prepared = resourceRoot.valid();
			// Overlay only the seven verified map artifacts, never the entire migration output.
			std::vector<std::string> mapFiles{ "map/" + arenaMap };
			for (int package = 0; package < 6; ++package)
			{
				mapFiles.push_back("map/" + arenaMap + ".tiles/00" + std::to_string(package) + ".img");
			}
			for (const auto& path : mapFiles)
			{
				std::ifstream input(std::filesystem::u8path(convertedRoot) / std::filesystem::u8path(path), std::ios::binary);
				const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
				prepared = !contents.empty() && writeVirtualFile(path, contents) && prepared;
			}
			if (!check(prepared, "arena integration copies seven converted artifacts into an isolated writable root"))
			{
				ok = false;
				continue;
			}
			std::vector<std::string> roots{ packRoot.u8string(), (assetsRoot / "yycs").u8string() };
			if (dependencyRoot != nullptr && dependencyRoot[0] != '\0') roots.push_back(dependencyRoot);
			File::setResourceFallbackRoots(roots);
			File::setUiResourceFallbackRoots(roots, true, (assetsRoot / "common").u8string());
			GameManager gameManager;
			ResourceManifest manifest;
			ok = check(manifest.loadFromFile("game_profile.ini"), "load actual Yuchen arena feature profile") && ok;
			gameManager.global.applyResourceManifestFeatures(manifest);
			gameManager.varList.ensureInitialized();
			gameManager.varList.setInteger("SelValue", -1);
			gameManager.varList.setInteger("selvalue", 91);
			gameManager.global.data.characterIndex = 0;
			auto choice = std::make_shared<ArenaChooseMenu>();
			gameManager.menu->chooseMenu = choice;
			// Only presentation timing is omitted. Menu focus, Lua, maps, NPCs, objects and saves remain real.
			for (const char* command : { "fadeout", "fadein" })
			{
				CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, command, [](lua_State*) { return 0; });
			}
			if (!check(gameManager.player->loadInitialTemplate(0) && gameManager.traps.loadInitialTemplate()
				&& gameManager.scriptAPI.loadMap(entrance.map, false)
				&& gameManager.scriptAPI.loadNPC(entrance.npcs)
				&& gameManager.scriptAPI.loadObject(entrance.objects), "load actual arena entrance player, map, NPC and object lists"))
			{
				ok = false;
				continue;
			}
			const auto actors = gameManager.npcManager->npcList;
			const auto sourceActor = std::find_if(actors.begin(), actors.end(), [&](const auto& npc)
			{
				return npc != nullptr && npc->scriptFile == entrance.script;
			});
			if (!check(sourceActor != actors.end(), "the entrance is attached to a real NPC, not invoked as an unbound test script"))
			{
				ok = false;
				continue;
			}
			const auto sourceObjectCount = gameManager.objectManager->objectList.size();
			gameManager.runNPCScript(*sourceActor);
			if (!check(choice->calls == 1 && choice->actionsHandled && choice->getSelection() == 2
				&& choice->speaker == entrance.speaker && gameManager.varList.getInteger("SelValue") == 2
				&& gameManager.varList.getInteger("selvalue") == 91
				&& gameManager.global.data.mapName == arenaMap && gameManager.mapFolderName == arenaFolder,
				"both actual choice menus enter the converted arena through production Lua and world commits"))
			{
				ok = false;
				continue;
			}
			ok = check(gameManager.map->data->head.width == 32 && gameManager.map->data->head.height == 79
				&& gameManager.npcManager->npcList.size() == 7 && gameManager.objectManager->objectList.empty()
				&& gameManager.global.data.npcName == arenaNpcs && gameManager.global.data.objName.empty()
				&& gameManager.player->getPosition().x == 8 && gameManager.player->getPosition().y == 60
				&& gameManager.player->direction == 6 && gameManager.global.data.NPCAI
				&& gameManager.camera->followPlayer && !gameManager.inEvent,
				"arena arrival replaces ordinary actors and objects and preserves the scripted spawn, AI and camera follow") && ok;
			const std::string savedEntranceNpcs = readVirtualFile(std::string("save/game/") + entrance.npcs);
			const std::string savedEntranceObjects = readVirtualFile(std::string("save/game/") + entrance.objects);
			INIReader savedObjects(std::string("save/game/") + entrance.objects);
			ok = check(!savedEntranceNpcs.empty() && !savedEntranceObjects.empty()
				&& savedObjects.GetInteger("Head", "Count", -1) == static_cast<int>(sourceObjectCount),
				"the real entrance SaveNpc and SaveObj persist the departing map before replacing the world") && ok;
			int trapCount = 0;
			for (int y = 0; y < gameManager.map->data->head.height; ++y)
			{
				for (int x = 0; x < gameManager.map->data->head.width; ++x)
				{
					if (gameManager.map->data->tile[y][x].trap == 0) continue;
					++trapCount;
					ok = check(!gameManager.map->haveTraps({ x, y }) && gameManager.map->getTrapName({ x, y }).empty(),
						"real arena trap tiles have no automatic script binding") && ok;
					gameManager.runTrapScript(gameManager.map->data->tile[y][x].trap);
				}
			}
			ok = check(trapCount == 8 && gameManager.mapFolderName == arenaFolder && !gameManager.inEvent,
				"all eight unbound trap cells leave the arena unchanged") && ok;
			const auto bosses = gameManager.npcManager->findNPC("boss001");
			if (!check(bosses.size() == 1, "actual arena boss is present")) { ok = false; continue; }
			bosses.front()->life = 1234;
			gameManager.player->life = 123;
			if (!check(gameManager.saveGame(1), "save the entered arena through complete slot publication")) { ok = false; continue; }
			gameManager.npcManager->freeResource();
			gameManager.varList.setInteger("SelValue", -5);
			gameManager.player->life = 1;
			const bool loaded = asynchronous ? gameManager.scriptAPI.loadGameAsync(1) : gameManager.loadGame(1);
			const auto restoredBosses = gameManager.npcManager->findNPC("boss001");
			ok = check(loaded && gameManager.global.data.mapName == arenaMap && gameManager.mapFolderName == arenaFolder
				&& gameManager.npcManager->npcList.size() == 7 && restoredBosses.size() == 1
				&& restoredBosses.front() != bosses.front() && restoredBosses.front()->life == 1234
				&& gameManager.player->life == 123 && gameManager.player->getPosition().x == 8 && gameManager.player->getPosition().y == 60
				&& gameManager.objectManager->objectList.empty() && gameManager.global.data.objName.empty()
				&& gameManager.varList.getInteger("SelValue") == 2 && gameManager.varList.getInteger("selvalue") == 91
				&& gameManager.traps.get(arenaFolder, 1).empty(),
				"complete sync and async slot reload restore arena map, fresh actors, player, case-sensitive variables and inactive traps") && ok;
			ok = check(readVirtualFile(std::string("save/rpg1/") + entrance.npcs) == savedEntranceNpcs
				&& readVirtualFile(std::string("save/rpg1/") + entrance.objects) == savedEntranceObjects,
				"complete arena save retains the preceding map snapshots without modifying their data") && ok;
			if (dependencyRoot != nullptr && dependencyRoot[0] != '\0' && loaded && restoredBosses.size() == 1)
			{
				gameManager.global.applyResourceManifestFeatures(manifest);
				gameManager.global.data.NPCAI = false; // Advance the selected actor/effects, not unrelated AI decisions.
				for (const char* file : { "test01.ini", "wd_90_rjhy.ini", u8"wd_120_变身.ini",
					u8"wd_120_变身_BUFF.ini", u8"wd_120_召唤.ini" })
				{
					const auto magic = gameManager.magicManager.loadAttackMagic(file);
					ok = check(magic != nullptr && magic->loadSucceeded && magic->flyImage != nullptr,
						"all five staged arena magics load their real flying animation") && ok;
				}
				gameManager.scriptAPI.npcUseMagic("boss001", "test01.ini", 21, 31, 1);
				const auto warnings = gameManager.effectManager->effectList;
				ok = check(!warnings.empty(), "actual arena boss use of published test01 creates warning markers") && ok;
				for (const auto& warning : warnings)
				{
					ok = check(warning->getMoveKind() == 999 && warning->skipsCharacterCollision() && warning->canPassThroughWall(),
						"every actual arena marker retains the published warning mode") && ok;
					if (warning->doing == ekHiding)
					{
						CoreLifecycleTestAccess::advanceActorFrame(*warning, warning->waitTime + 1);
					}
					const Point impactPosition = warning->position;
					const PointEx impactOffset = warning->offset;
					const auto beforeImpact = gameManager.effectManager->effectList.size();
					CoreLifecycleTestAccess::advanceActorFrame(*warning, warning->lifeTime + 1);
					ok = check(gameManager.effectManager->effectList.size() == beforeImpact + 1
						&& gameManager.effectManager->effectList.back()->magic.iniName == "001.ini"
						&& gameManager.effectManager->effectList.back()->position == impactPosition
						&& (gameManager.effectManager->effectList.back()->offset - impactOffset).is_zero(),
						"each published warning expires into its real 001 impact at the marked world position") && ok;
				}
				gameManager.effectManager->clearEffect();
				const auto victims = gameManager.npcManager->findNPC(u8"无忧教男弟子A");
				const auto victim = std::find_if(victims.begin(), victims.end(), [](const auto& npc)
				{
					return npc->dropIni == u8"随机掉落-测试用.ini";
				});
				if (check(victim != victims.end() && (*victim)->res.death.imagePackage != nullptr,
					"actual arena drop owner has its real death animation"))
				{
					(*victim)->life = 0;
					(*victim)->handleDeath();
					CoreLifecycleTestAccess::advanceActorFrame(**victim, (*victim)->actionLastTime + 1);
					gameManager.npcManager->onUpdate();
					ok = check(!gameManager.npcManager->findNPC(*victim),
						"real arena death animation reaches manager cleanup without removing the corpse or drop bindings") && ok;
					const auto objects = gameManager.objectManager->objectList;
					auto inventoryCount = [&]()
					{
						int count = 0;
						for (const auto& item : gameManager.goodsManager.goodsList) count += item.number;
						return count;
					};
					int rewards = 0;
					for (const auto& object : objects)
					{
						if (object->scriptFile.empty()) continue; // Keep the actual corpse.
						++rewards;
						const int beforeMoney = gameManager.player->money;
						const int beforeGoods = inventoryCount();
						gameManager.player->setPosition(object->getPosition(), false);
						gameManager.player->triggerObject(object);
						ok = check(!gameManager.objectManager->findObj(object)
							&& (gameManager.player->money > beforeMoney || inventoryCount() > beforeGoods),
							"actual arena drop scripts award and consume their generated objects") && ok;
					}
					ok = check(rewards > 0, "the restored published test drop table generates collectible rewards") && ok;
					std::cout << "Arena dependency runtime checked: warnings=" << warnings.size()
						<< " rewards=" << rewards << '\n';
				}
				else ok = false;
				ok = runProductionArenaMagicLifecycle(gameManager, restoredBosses.front(), asynchronous) && ok;
			}
			std::cout << "Arena route checked: async=" << asynchronous << " entrance=" << entrance.npcs << '\n';
		}
	}
	Config::loadAsync = previousLoadAsync;
	File::setResourceFallbackRoots({});
	File::setUiResourceFallbackRoots({});
	return ok;
}

bool runProductionHanboRouteTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	if (!std::filesystem::exists(assetsRoot / "yycs/game_profile.ini"))
	{
		std::cout << "SKIP: optional production Hanbo route pack is absent\n";
		return true;
	}
	ScopedActiveResourceRoot resourceRoot;
	std::ifstream table(assetsRoot / "yycs/talkindex.txt", std::ios::binary);
	const std::string tableText((std::istreambuf_iterator<char>(table)), std::istreambuf_iterator<char>());
	if (!check(resourceRoot.valid() && !tableText.empty() && writeVirtualFile("talkindex.txt", tableText),
		"Hanbo route tests use the actual dialogue table in an isolated root"))
	{
		return false;
	}
	GameManager gameManager;
	gameManager.varList.ensureInitialized();
	gameManager.talkTextList.load();
	gameManager.menu->dialog = std::make_shared<RecordingDialog>();
	// Inspect actual branch selection and map requests, not loaded-map traversal.
	for (const char* command : { "fadeout", "fadein", "stopmusic", "playmusic", "savenpc", "saveobj",
		"loadnpc", "loadobj", "playergoto", "playergotodir", "beginrain" })
	{
		CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, command, [](lua_State*) { return 0; });
	}
	CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, "loadmap", [](lua_State* state)
	{
		const std::string name = luaL_checkstring(state, 1);
		const int route = name == u8"map_019_寒波谷.map" ? 1 : name == u8"map_019_寒波谷(a).map" ? 2 :
			name == u8"map_019_寒波谷(b).map" ? 3 : -1;
		gm->varList.setInteger("TestHanboRoute", route);
		return 0;
	});
	struct RouteCase
	{
		const char* pack;
		const char* script;
		int event;
		int result;
		int expectedRoute;
	};
	const RouteCase cases[] = {
		{ "yycs", u8"map_018_连接地图/trap02.txt", 170, 0, 1 },
		{ "yycs", u8"map_018_连接地图/trap02.txt", 170, 1, 1 },
		{ "yycs", u8"map_018_连接地图/trap02.txt", 570, 1, 0 },
		{ "yycs", u8"map_018_连接地图/trap02.txt", 190, 0, 2 },
		{ "yycs", u8"map_018_连接地图/trap02.txt", 200, 0, 3 },
		{ "yycs", u8"map_018_连接地图/trap02.txt", 440, 0, 3 },
		{ "yycs", u8"map_018_连接地图/trap02.txt", 440, 1, 0 },
		{ "yycs", u8"map_020_樱花谷/trap01.txt", 440, 0, 0 },
		{ "yycs", u8"map_020_樱花谷/trap01.txt", 200, 0, 3 },
		{ "yycs", u8"map_059_禁地一层/trap01.txt", 440, 0, 0 },
		{ "yycs", u8"map_059_禁地一层/trap02.txt", 440, 0, 1 },
		{ u8"江湖余尘", u8"map_018_连接地图/trap02.txt", 440, 0, 1 },
		{ u8"江湖余尘", u8"map_020_樱花谷/trap01.txt", 440, 0, 1 },
		{ u8"江湖余尘二", u8"map_018_连接地图/trap02.txt", 440, 0, 1 },
		{ u8"江湖余尘二", u8"map_020_樱花谷/trap01.txt", 440, 0, 1 }
	};
	bool ok = true;
	for (const auto& test : cases)
	{
		std::ifstream input(assetsRoot / std::filesystem::u8path(test.pack) / "script/map" /
			std::filesystem::u8path(test.script), std::ios::binary);
		const std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		gameManager.varList.setInteger("Event", test.event);
		gameManager.varList.setInteger("Result", test.result);
		gameManager.varList.setInteger("TestHanboRoute", 0);
		auto bytes = std::make_unique<char[]>(source.size());
		std::copy(source.begin(), source.end(), bytes.get());
		ok = check(!source.empty() && gameManager.script.runScript(bytes, static_cast<int>(source.size())) == LUA_OK &&
			gameManager.varList.getInteger("TestHanboRoute") == test.expectedRoute,
			"the actual Hanbo entrance keeps its pack-specific Event/Result routing and sealed-dungeon behavior") && ok;
	}
	return ok;
}

bool runProductionHanboChoiceTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	const auto yycsRoot = assetsRoot / "yycs";
	if (!std::filesystem::exists(yycsRoot / "game_profile.ini"))
	{
		std::cout << "SKIP: optional production Hanbo dialogue pack is absent\n";
		return true;
	}
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid(), "Hanbo choices use an isolated resource root"))
	{
		return false;
	}
	for (const char* relativePath : { "talkindex.txt", u8"script/map/map_019_寒波谷/心魔阵战斗死亡.txt" })
	{
		std::ifstream input(yycsRoot / std::filesystem::u8path(relativePath), std::ios::binary);
		const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		if (!check(!contents.empty() && writeVirtualFile(relativePath, contents),
			"copy actual YYCS table and normal-map death script"))
		{
			return false;
		}
	}
	bool ok = true;
	for (const char* pack : { "yycs", u8"江湖余尘", u8"江湖余尘二" })
	{
		std::ifstream input(assetsRoot / std::filesystem::u8path(pack) / "ini/save/map019_heart2.npc", std::ios::binary);
		const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
		if (!check(!contents.empty() && writeVirtualFile("ini/save/map019_heart2.npc", contents),
			"copy the pack's actual heart-battle NPC list"))
		{
			return false;
		}
		for (const int selection : { 0, 1 })
		{
			GameManager gameManager;
			gameManager.varList.ensureInitialized();
			gameManager.mapFolderName = u8"map_019_寒波谷";
			gameManager.talkTextList.load();
			auto dialog = std::make_shared<RecordingDialog>();
			auto choice = std::make_shared<RecordingChooseMenu>();
			choice->requestedSelection = selection;
			gameManager.menu->dialog = dialog;
			gameManager.menu->chooseMenu = choice;
			// Observe the script contract without waiting for its cutscene or
			// replacing the synthetic world. Variable/NPC/choice APIs stay real.
			for (const char* command : { "fadeout", "fadein", "sleep", "watch", "npcattack", "npcspecialaction",
				"beginrain", "endrain", "stopmusic" })
			{
				CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, command, [](lua_State*) { return 0; });
			}
			CoreLifecycleTestAccess::registerScriptProbe(gameManager.script, "loadmap", [](lua_State* state)
			{
				gm->varList.setInteger("TestSecondLayer", std::string(luaL_checkstring(state, 1)) == u8"map_060_禁地二层.map" ? 1 : 0);
				return 0;
			});
			ok = check(gameManager.scriptAPI.loadNPC("map019_heart2.npc"), "load the actual heart-battle NPC list") && ok;
			gameManager.scriptAPI.getNpcCount(1, 1);
			gameManager.varList.setInteger("SenseVal", 100);
			gameManager.varList.setInteger("Sel", -1);
			gameManager.varList.setInteger("sel", 77);
			std::vector<std::shared_ptr<NPC>> combatants;
			for (const auto& npc : gameManager.npcManager->npcList)
			{
				if (npc->deathScript == u8"心魔阵战斗死亡.txt")
				{
					combatants.push_back(npc);
				}
			}
			ok = check(combatants.size() == 12 && gameManager.varList.getInteger("NpcCount") == 12,
				"the production list binds twelve combatants to the normal-map death script") && ok;
			for (size_t index = 0; index < combatants.size(); ++index)
			{
				gameManager.runNPCDeathScript(combatants[index], combatants[index]->deathScript, gameManager.mapFolderName);
				if (index + 1 < combatants.size())
				{
					ok = check(choice->entries.empty(), "the choice waits until the last counted death") && ok;
				}
			}
			ok = check(choice->entries == std::vector<std::vector<std::string>>{
				{ u8"杀还是不杀紫轩？", u8"A，杀紫轩", u8"B，不杀紫轩" } } &&
				gameManager.varList.getInteger("Sel") == selection && gameManager.varList.getInteger("sel") == 77 &&
				gameManager.varList.getInteger("NpcCount") == 0 && gameManager.varList.getInteger("Event") == 450 &&
				gameManager.varList.getInteger("SenseVal") == (selection == 0 ? 60 : 120) &&
				gameManager.varList.getInteger("TestSecondLayer") == 1 && gameManager.npcManager->findNPC(u8"紫轩").empty(),
				"the actual normal-map death script resolves the correct choices and advances both branches without case collisions") && ok;
		}
	}
	return ok;
}

bool runChoiceVariableContracts()
{
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid(), "choice contracts created an isolated resource root"))
	{
		return false;
	}
	GameManager gameManager;
	gameManager.setAutomationHooksEnabled(true);
	gameManager.varList.ensureInitialized();
	SaveFileManager::CurrentPathScope currentPath("save\\choice_contracts");
	if (!check(currentPath.valid(), "choice contracts selected an isolated generation"))
	{
		return false;
	}
	const auto execute = [&](const std::string& source)
	{
		auto bytes = std::make_unique<char[]>(source.size());
		std::copy(source.begin(), source.end(), bytes.get());
		return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
	};
	bool ok = check(execute(
		"assign('Quest',1); assign('quest',0); assign('result',71);"
		"assign('__automation_choose_enabled',1); assign('__automation_choose_selection',2);"
		"chooseex('Question','{$Quest >= 1}First','{$quest >= 1}Hidden',"
		"'{$Quest == 1}{$quest == 0}Third','Result');") == LUA_OK &&
		gameManager.varList.getInteger("Result") == 2 &&
		gameManager.varList.getInteger("result") == 71 &&
		gameManager.varList.getInteger("__automation_choose_visible_count") == 2,
		"ChooseEx combines conditions, preserves variable case and returns the original option index");
	ok = check(execute(
		"assign('__automation_choose_enabled',1); assign('__automation_choose_selection',1);"
		"chooseex('Question','{$Quest == 1}First','{$quest == 1}Hidden','Result');") == LUA_OK &&
		gameManager.varList.getInteger("Result") == -1 &&
		gameManager.varList.getInteger("__automation_choose_complete") == 0,
		"a hidden selection cannot become a different visible branch") && ok;
	ok = check(execute(
		"assign('__automation_choose_enabled',1); assign('__automation_choose_selection',1);"
		"chooseplus('#name',2,0,'Question','{$quest == 1}Hidden','{$Quest == 1}Second','Result');") == LUA_OK &&
		gameManager.varList.getInteger("Result") == 1 &&
		gameManager.varList.getInteger("result") == 71,
		"ChoosePlus shares case-sensitive filtering and output assignment") && ok;
	ok = check(execute(
		"assign('$pick0',77); assign('$Pick2',99);"
		"assign('__automation_choose_multiple_enabled',1); assign('__automation_choose_multiple_count',2);"
		"assign('__automation_choose_multiple_selection0',2); assign('__automation_choose_multiple_selection1',0);"
		"choosemultiple(2,2,'Pick','Question','{$Quest == 1}First','{$quest == 1}Hidden','Third');") == LUA_OK &&
		gameManager.varList.getInteger("$Pick0") == 2 &&
		gameManager.varList.getInteger("$Pick1") == 0 &&
		gameManager.varList.getInteger("$Pick2") == 99 &&
		gameManager.varList.getInteger("$pick0") == 77,
		"ChooseMultiple preserves selection order and only writes its exact-case indexed result keys") && ok;
	ok = check(gameManager.varList.save(), "choice result variables save") && ok;
	gameManager.varList.setInteger("Result", 99);
	gameManager.varList.setInteger("$Pick0", 99);
	ok = check(gameManager.varList.load() &&
		gameManager.varList.getInteger("Result") == 1 &&
		gameManager.varList.getInteger("result") == 71 &&
		gameManager.varList.getInteger("$Pick0") == 2 &&
		gameManager.varList.getInteger("$Pick1") == 0 &&
		gameManager.varList.getInteger("$pick0") == 77,
		"choice output case and multiple-selection order survive variable-file save/load") && ok;
	return ok;
}

bool runMemoGenerationCompatibilityTests()
{
	ScopedActiveResourceRoot resourceRoot;
	if (!check(
			resourceRoot.valid(),
			"memo compatibility test created an isolated resource root"))
	{
		return false;
	}

	GameManager gameManager;
	const std::string validMemo =
		"[Memo]\n"
		"Count=1\n"
		"0=loaded memo\n";
	const std::string objectMemoIni =
		"[Head]\n"
		"Count=1\n"
		"[OBJ000]\n"
		"ObjName=legacy object\n";
	const std::string invalidMemo =
		"[Memo]\n"
		"Count=1junk\n";
	bool ok = true;

	const std::string generationDirectory =
		"save\\memo_compatibility";
	SaveFileManager::CurrentPathScope currentPath(
		generationDirectory);
	if (!check(
			currentPath.valid(),
			"memo compatibility test selected an isolated generation"))
	{
		return false;
	}
	const auto execute = [&](const std::string& source)
	{
		auto bytes = std::make_unique<char[]>(source.size());
		std::copy(source.begin(), source.end(), bytes.get());
		return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
	};
	gameManager.talkTextList.list = { { 123, 0, "Indexed" } };
	ok = check(execute("memo('123'); addtomemo(123); addtomemo(999); memo(''); memo(); delmemo('');") == LUA_OK &&
		gameManager.memo.memo == std::deque<std::string>{ u8"●Indexed", u8"●123" },
		"Memo keeps numeric text literal; AddToMemo resolves an index and ignores missing/empty input") && ok;
	ok = check(execute("delmemo('123'); delmemo('Indexed');") == LUA_OK && gameManager.memo.memo.empty(),
		"DelMemo removes a final single-line entry, unlike the C# loop that excludes the last node") && ok;
	ok = check(execute("memo('-2'); memo('1e3'); memo('0x10'); memo(' 42 '); memo(123);") == LUA_OK &&
		gameManager.memo.memo == std::deque<std::string>{ u8"●123", u8"● 42 ", u8"●0x10", u8"●1e3", u8"●-2" },
		"the Memo runtime preserves signed, exponent, hexadecimal and padded numeric text literally") && ok;
	gameManager.memo.clear();
	ok = check(execute("memo('Case'); memo('case'); memo('Case'); delmemo('Case');") == LUA_OK &&
		gameManager.memo.memo == std::deque<std::string>{ u8"●case", u8"●Case" },
		"memo insertion keeps duplicates; deletion removes only the first exact-case entry") && ok;
	ok = check(execute(u8"clearmemo(); memo('一二三四五六七八九十');") == LUA_OK &&
		gameManager.memo.memo == std::deque<std::string>{ u8"●一二三四五六七八", u8"九十" } &&
		gameManager.memo.save(), "memo wraps at nine UTF-8 characters and saves all lines in order") && ok;
	const auto wrappedMemo = gameManager.memo.memo;
	gameManager.memo.clear();
	ok = check(gameManager.memo.load(false) && gameManager.memo.memo == wrappedMemo &&
		execute(u8"delmemo('一二三四五六七八九十');") == LUA_OK && gameManager.memo.memo.empty(),
		"a multiline memo remains removable by its original text after saving and loading") && ok;
	ok = check(execute("addtomemo('123');") == LUA_OK &&
		gameManager.memo.memo == std::deque<std::string>{ u8"●Indexed" },
		"characterization: AddToMemo treats numeric strings as indices, unlike literal Memo") && ok;
	gameManager.memo.memo.assign(MemoPersistence::MaximumLineCount, "old line");
	ok = check(execute("memo('Newest');") == LUA_OK &&
		gameManager.memo.memo.size() == MemoPersistence::MaximumLineCount &&
		gameManager.memo.memo.front() == u8"●Newest" && gameManager.memo.memo.back() == "old line",
		"memo line limit retains the newest entry and removes the oldest excess line") && ok;
	gameManager.memo.clear();
	ok = check(
		writeVirtualFile(
			generationDirectory + "\\memo.ini",
			objectMemoIni) &&
			writeVirtualFile(
				generationDirectory + "\\memo.txt",
				validMemo),
		"xjxqy-style object memo.ini and canonical memo.txt fixture is created") &&
		ok;
	bool memoNeedsNormalization = true;
	gameManager.memo.memo = { "old memo" };
	ok = check(
		gameManager.memo.load(true, &memoNeedsNormalization) && !memoNeedsNormalization &&
			gameManager.memo.memo.size() == 1 &&
			gameManager.memo.memo.front() ==
				"loaded memo",
		"semantic memo.txt wins while object-list memo.ini remains ordinary save data") &&
		ok;
	gameManager.memo.memo = { "saved memo" };
	ok = check(
		gameManager.memo.save() &&
			readVirtualFile(
				generationDirectory + "\\memo.ini") ==
				objectMemoIni,
		"memo save preserves the legacy object-list memo.ini") &&
		ok;
	const std::vector<std::string> savedFiles =
		File::listFiles(generationDirectory);
	ok = check(
		std::count(
			savedFiles.cbegin(),
			savedFiles.cend(),
			"memo.txt") == 1,
		"memo save retains exactly one canonical lowercase memo.txt") &&
		ok;

	ok = check(
		File::clearDirectoryFiles(
			generationDirectory) &&
			writeVirtualFile(
				generationDirectory + "\\memo.ini",
				invalidMemo) &&
			writeVirtualFile(
				generationDirectory + "\\memo.txt",
				validMemo),
		"valid canonical memo and invalid legacy alias fixture is created") &&
		ok;
	gameManager.memo.memo = { "before canonical priority" };
	ok = check(
		gameManager.memo.load(true) &&
			gameManager.memo.memo.size() == 1 &&
			gameManager.memo.memo.front() == "loaded memo",
		"valid canonical memo wins over an invalid legacy alias") &&
		ok;

	ok = check(
		File::clearDirectoryFiles(
			generationDirectory) &&
			writeVirtualFile(
				generationDirectory + "\\memo.ini",
				validMemo),
		"semantic memo.ini-only compatibility fixture is created") &&
		ok;
	gameManager.memo.memo = { "before fallback" };
	ok = check(
		gameManager.memo.load(true, &memoNeedsNormalization) && memoNeedsNormalization &&
			gameManager.memo.memo.size() == 1 &&
			gameManager.memo.memo.front() ==
				"loaded memo",
		"semantic memo.ini remains a compatible read fallback") &&
		ok;
	gameManager.memo.memo = { "migrated memo" };
	ok = check(
		gameManager.memo.save() &&
			readVirtualFile(
				generationDirectory + "\\memo.ini") ==
				validMemo &&
			File::fileExist(
				generationDirectory + "\\memo.txt"),
		"saving an imported memo.ini creates canonical memo.txt without overwriting the historical file") &&
		ok;

	ok = check(
		File::clearDirectoryFiles(
			generationDirectory) &&
			writeVirtualFile(
				generationDirectory + "\\memo.txt",
				invalidMemo),
		"invalid canonical memo fixture is created") &&
		ok;
	gameManager.memo.memo = { "preserved memo" };
	ok = check(
		!gameManager.memo.load(false) &&
			gameManager.memo.memo.size() == 1 &&
			gameManager.memo.memo.front() ==
				"preserved memo",
		"strict invalid canonical memo load fails without clearing the live memo") &&
		ok;
	ok = check(
		gameManager.memo.load(true, &memoNeedsNormalization) && memoNeedsNormalization &&
			gameManager.memo.memo.empty(),
		"compatible invalid canonical memo loads an empty optional memo") &&
		ok;
	gameManager.memo.memo = { "repaired memo" };
	ok = check(
		gameManager.memo.save() &&
			gameManager.memo.load(false) &&
			gameManager.memo.memo.size() == 1 &&
			gameManager.memo.memo.front() == "repaired memo",
		"a compatible invalid memo can be repaired by the next save") &&
		ok;

	ok = check(
		File::clearDirectoryFiles(generationDirectory) &&
			writeVirtualFile(
				generationDirectory + "\\memo.txt",
				{}),
		"zero-byte canonical memo fixture is created") &&
		ok;
	gameManager.memo.memo = { "before empty memo" };
	ok = check(
		gameManager.memo.load(true) &&
			gameManager.memo.memo.empty(),
		"a zero-byte optional memo is treated like other compatible invalid memo data") &&
		ok;
	gameManager.memo.memo = { "line one\nline two\r\nline three" };
	ok = check(
		gameManager.memo.save() &&
			gameManager.memo.load(false) &&
			gameManager.memo.memo.size() == 1 &&
			gameManager.memo.memo.front() ==
				"line one line two  line three",
		"memo serialization prevents embedded line breaks from producing an unreadable save") &&
		ok;

	ok = check(
		File::clearDirectoryFiles(
			generationDirectory),
		"missing-memo compatibility fixture is created") &&
		ok;
	gameManager.memo.memo = { "legacy live memo" };
	ok = check(
		gameManager.memo.load(true) &&
			gameManager.memo.memo.empty(),
		"a compatible generation with no semantic memo explicitly loads an empty memo") &&
		ok;
	return ok;
}

bool runOwnerWorldCommitPhaseTests()
{
	Engine* engine = Engine::getInstance();
	bool ok = true;
	const auto prepareWorld =
		[engine]()
		{
			engine->resetApplicationQuitRequest();
			auto gameManager =
				std::make_unique<GameManager>();
			gameManager->map->data =
				std::make_shared<MapData>();
			gameManager->global.data.mapName =
				"retained.map";
			return gameManager;
		};

	{
		auto gameManager = prepareWorld();
		const bool result =
			CoreLifecycleTestAccess::runOwnerWorldCommit(
				*gameManager,
				[](
					const std::function<void()>&,
					const std::function<void()>&)
				{
					return false;
				});
		ok = check(
			!result &&
				!engine->isApplicationQuitRequested() &&
				gameManager->map->data != nullptr,
			"a prepare-stage failure preserves the live world without requesting termination") &&
			ok;
	}
	{
		auto gameManager = prepareWorld();
		const bool result =
			CoreLifecycleTestAccess::runOwnerWorldCommit(
				*gameManager,
				[](
					const std::function<void()>&,
					const std::function<void()>&)
					-> bool
				{
					throw std::runtime_error(
						"prepare exception");
				});
		ok = check(
			!result &&
				!engine->isApplicationQuitRequested() &&
				gameManager->map->data != nullptr,
			"a prepare-stage exception preserves the live world without requesting termination") &&
			ok;
	}
	{
		auto gameManager = prepareWorld();
		const bool result =
			CoreLifecycleTestAccess::runOwnerWorldCommit(
				*gameManager,
				[](
					const std::function<void()>&,
					const std::function<void()>&)
				{
					return true;
				});
		ok = check(
			!result &&
				!engine->isApplicationQuitRequested() &&
				gameManager->map->data != nullptr,
			"a success result without a primary commit marker is rejected before mutation") &&
			ok;
	}
	{
		auto gameManager = prepareWorld();
		const bool result =
			CoreLifecycleTestAccess::runOwnerWorldCommit(
				*gameManager,
				[](
					const std::function<void()>& beforeMutation,
					const std::function<void()>&)
				{
					beforeMutation();
					return false;
				});
		ok = check(
			!result &&
				!engine->isApplicationQuitRequested() &&
				gameManager->map->data == nullptr,
			"a mutation-stage failure returns to title and clears the partial world without terminating the application") &&
			ok;
	}
	{
		auto gameManager = prepareWorld();
		const bool result =
			CoreLifecycleTestAccess::runOwnerWorldCommit(
				*gameManager,
				[](
					const std::function<void()>& beforeMutation,
					const std::function<void()>&)
					-> bool
				{
					beforeMutation();
					throw std::runtime_error(
						"mutation exception");
				});
		ok = check(
			!result &&
				!engine->isApplicationQuitRequested() &&
				gameManager->map->data == nullptr,
			"a mutation-stage exception returns to title and clears the partial world without terminating the application") &&
			ok;
	}
	{
		auto gameManager = prepareWorld();
		const bool result =
			CoreLifecycleTestAccess::runOwnerWorldCommit(
				*gameManager,
				[](
					const std::function<void()>& beforeMutation,
					const std::function<void()>&)
				{
					beforeMutation();
					return true;
				});
		ok = check(
			!result &&
				!engine->isApplicationQuitRequested() &&
				gameManager->map->data == nullptr,
			"a success result without a commit marker returns to title after mutation") &&
			ok;
	}
	{
		auto gameManager = prepareWorld();
		const bool result =
			CoreLifecycleTestAccess::runOwnerWorldCommit(
				*gameManager,
				[](
					const std::function<void()>& beforeMutation,
					const std::function<void()>& commitCompleted)
					-> bool
				{
					beforeMutation();
					commitCompleted();
					throw std::runtime_error(
						"auxiliary exception");
				});
		ok = check(
			result &&
				!engine->isApplicationQuitRequested() &&
				gameManager->map->data != nullptr,
			"an auxiliary exception after commit keeps the committed world and reports primary success") &&
			ok;
	}
	{
		auto gameManager = prepareWorld();
		const bool result =
			CoreLifecycleTestAccess::runOwnerWorldCommit(
				*gameManager,
				[](
					const std::function<void()>& beforeMutation,
					const std::function<void()>&)
					-> bool
				{
					beforeMutation();
					throw std::runtime_error(
						"transaction-owned mutation exception");
				},
				false);
		ok = check(
			!result &&
				!engine->isApplicationQuitRequested() &&
				gameManager->map->data != nullptr,
			"a save-load transaction defers mutation failure handling to its outer rollback") &&
			ok;
	}
	engine->resetApplicationQuitRequest();
	return ok;
}

bool runSaveWriteSharingFailureTests()
{
#if defined(_WIN32) && defined(JXQY_ENABLE_TEST_HOOKS)
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid(), "write sharing test has an isolated resource root")) return false;
	auto mapBytes = MapV3ContractFixture::build();
	std::fill(mapBytes.begin() + MapV3ContractFixture::BaseHeaderLength,
		mapBytes.begin() + MapV3ContractFixture::HeaderLength + MapV3ContractFixture::NameLength, std::uint8_t{ 0 });
	const std::string mapName = "write-sharing.map";
	if (!check(writeVirtualFile("map/" + mapName,
		std::string(reinterpret_cast<const char*>(mapBytes.data()), mapBytes.size())),
		"create the write sharing readback map")) return false;
	const auto snapshot = [](const std::string& directory)
	{
		std::vector<std::pair<std::string, std::string>> files;
		for (const auto& name : File::listFiles(directory))
		{
			files.emplace_back(name, readVirtualFile(directory + "/" + name));
		}
		std::sort(files.begin(), files.end());
		return files;
	};
	bool ok = true;
	for (bool releaseOnFailure : { false, true })
	{
		for (int slot : { 1, -1 })
		{
			for (const std::string blockedFile : { GLOBAL_INI, VARIABLE_INI, "player.ini", "player0.ini", EFFECT_INI })
			{
				for (bool asynchronous : { false, true })
				{
					const std::string slotDirectory = slot > 0 ? "save/rpg1" : "save/rpg_auto";
					if (!check(File::clearDirectoryFiles("save/game") && File::clearDirectoryFiles(slotDirectory) &&
						writeVirtualFile("save/game/game.ini", "[State]\nMap=seed.map\nNpc=\nObj=\n"),
						"seed a fresh current generation for the write sharing case")) return false;
					GameManager game;
					game.global.data.mapName = mapName;
					game.global.data.characterIndex = blockedFile == "player0.ini" ? 0 : -1;
					game.varList.ensureInitialized();
					game.traps.beginMapVisit();
					game.varList.setInteger("Event", 218);
					game.varList.setInteger("event", 7);
					game.player->lifeMax = 100;
					game.player->life = 90;
					if (!check(game.saveGame(slot), "publish a complete baseline before blocking a current-state writer")) return false;
					const auto currentBefore = snapshot("save/game");
					const auto slotBefore = snapshot(slotDirectory);
					const auto blockedBefore = readVirtualFile("save/game/" + blockedFile);
					if (!check(!currentBefore.empty() && !slotBefore.empty() && !blockedBefore.empty(),
						"baseline generations and selected writer file exist")) return false;
					game.varList.setInteger("Event", 219);
					game.varList.setInteger("event", 8);
					game.player->life = 77;
					HANDLE reader = INVALID_HANDLE_VALUE;
					bool attemptedLock = false;
					bool releasedReaderOnFailure = false;
					File::setEditorRunFileOperationTestHook([&](File::EditorRunFileOperationPhase phase)
					{
						if (releaseOnFailure && phase == File::EditorRunFileOperationPhase::AfterCheckedWriteOpenFailure && reader != INVALID_HANDLE_VALUE)
						{
							releasedReaderOnFailure = CloseHandle(reader) != 0;
							reader = INVALID_HANDLE_VALUE;
							return;
						}
						if (phase != File::EditorRunFileOperationPhase::BeforeCheckedWriteOpen || attemptedLock) return;
						std::string currentPath = SaveFileManager::CurrentPath();
						std::replace(currentPath.begin(), currentPath.end(), '\\', '/');
						if (currentPath != "save/game/") return;
						attemptedLock = true;
						const auto path = std::filesystem::u8path(File::getAssetsName("save/game/" + blockedFile));
						reader = CreateFileW(path.c_str(), GENERIC_READ,
							FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
							nullptr, OPEN_EXISTING, 0, nullptr);
					});
					const bool saved = game.saveGame(slot);
					File::setEditorRunFileOperationTestHook({});
					ok = check(attemptedLock && (releaseOnFailure ? releasedReaderOnFailure : reader != INVALID_HANDLE_VALUE) && saved == releaseOnFailure,
						"full save recovers a released reader but rejects a persistent reader") && ok;
					if (reader != INVALID_HANDLE_VALUE)
					{
						std::string retained(blockedBefore.size() + 1, '\0');
						DWORD count = 0;
						const bool read = ReadFile(reader, retained.data(), static_cast<DWORD>(retained.size()), &count, nullptr) != 0;
						retained.resize(count);
						ok = check(read && retained == blockedBefore, "failed open has not truncated or partially written the blocked file") && ok;
						ok = check(CloseHandle(reader) != 0, "release only the test-owned reader") && ok;
					}
					ok = check(releaseOnFailure || (snapshot(slotDirectory) == slotBefore),
						"failed current-state writing leaves the selected slot byte-identical") && ok;
					ok = check(game.varList.getInteger("Event") == 219 && game.varList.getInteger("event") == 8 &&
						game.player->life == 77 && !Engine::getInstance()->isApplicationQuitRequested(),
						"failed save does not roll back the live game or request exit") && ok;
					if (!releaseOnFailure && !check(game.saveGame(slot), "a new save operation after releasing the reader publishes successfully")) return false;
					game.varList.setInteger("Event", -1);
					game.varList.setInteger("event", -1);
					game.player->life = 1;
					const int loadSlot = slot;
					const bool loaded = asynchronous ? game.scriptAPI.loadGameAsync(loadSlot) : game.loadGame(loadSlot);
					ok = check(loaded && game.varList.getInteger("Event") == 219 && game.varList.getInteger("event") == 8 &&
						game.player->life == 77, "full sync/async load reads the later successful generation without mixing old fields") && ok;
					std::cout << "SaveWriteSharing slot=" << slot << " blocked=" << blockedFile << " async=" << asynchronous
						<< " release=" << releaseOnFailure << " failedSave=" << !saved << " readback=" << loaded << std::endl;
				}
			}
		}
	}
	return ok;
#else
	std::cout << "SKIP Windows save write sharing boundary tests\n";
	return true;
#endif
}

// Opt-in natural soak: one live GameManager repeatedly commits and loads full
// manual/auto generations. No artificial locks or failure retry in this loop.
bool runSaveStabilitySoakTests()
{
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid(), "natural save soak has a private resource and save root")) return false;
	auto bytes = MapV3ContractFixture::build();
	std::fill(bytes.begin() + MapV3ContractFixture::BaseHeaderLength,
		bytes.begin() + MapV3ContractFixture::HeaderLength + MapV3ContractFixture::NameLength, std::uint8_t{ 0 });
	const std::string mapName = "save-stability.map";
	if (!check(writeVirtualFile("map/" + mapName, std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size())) &&
		writeVirtualFile("save/game/game.ini", "[State]\nMap=seed.map\nNpc=\nObj=\n"), "seed the isolated soak world")) return false;
	GameManager game;
	game.global.data.mapName = mapName;
	game.varList.ensureInitialized();
	game.traps.beginMapVisit();
	game.player->lifeMax = 1000;
	const auto started = std::chrono::steady_clock::now();
	std::size_t completed = 0;
	std::cout << "SaveStability root=" << File::getAssetsName("save/game/game.ini") << std::endl;
	while (std::chrono::steady_clock::now() - started < std::chrono::minutes(10))
	{
		const int value = static_cast<int>(completed);
		const int slot = value % 2 == 0 ? 1 : -1;
		const bool asynchronous = (value / 2) % 2 != 0;
		game.varList.setInteger("Event", value);
		game.varList.setInteger("event", value + 10000);
		game.player->life = 100 + value % 500;
		if (!check(game.saveGame(slot), "natural full save succeeds without retrying a failed save operation")) return false;
		game.varList.setInteger("Event", -1);
		game.varList.setInteger("event", -1);
		game.player->life = 1;
		const bool loaded = asynchronous ? game.scriptAPI.loadGameAsync(slot) : game.loadGame(slot);
		if (!check(loaded && game.varList.getInteger("Event") == value && game.varList.getInteger("event") == value + 10000 &&
			game.player->life == 100 + value % 500 && !Engine::getInstance()->isApplicationQuitRequested(),
			"natural full sync/async load retains both case-sensitive variables and live player state")) return false;
		++completed;
		if (completed % 50 == 0) std::cout << "SaveStability completed=" << completed << std::endl;
	}
	std::cout << "SaveStability completed=" << completed << " seconds=" <<
		std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count() << std::endl;
	return check(completed >= 100, "the ten-minute soak completed at least 100 complete save/load pairs");
}

bool runSaveLoadFailureRecoveryTests()
{
	ScopedActiveResourceRoot resourceRoot;
	if (!check(
			resourceRoot.valid(),
			"save-load test created an isolated resource root"))
	{
		return false;
	}

	const std::string preparedDirectory =
		"save/game";
	Engine* engine = Engine::getInstance();
	engine->resetApplicationQuitRequest();

	bool ok = check(
		File::clearDirectoryFiles(preparedDirectory) &&
			writeVirtualFile(
				preparedDirectory + "/game.ini",
				"[State]\nMap=candidate.map\n"),
		"save-load success fixture is created");
	{
		GameManager gameManager;
		const GameLoading::LoadingTaskResult result =
			CoreLifecycleTestAccess::
				finishGameLoad(
					gameManager,
					preparedDirectory,
					[](
						const std::string&,
						const std::function<bool()>&)
					{
						return true;
					});
		ok = check(
			result.succeeded() &&
				readVirtualFile("save/game/game.ini") ==
					"[State]\nMap=candidate.map\n" &&
				!engine->isApplicationQuitRequested(),
			"the copied runtime save loads in place without publishing another directory") &&
			ok;
	}

	ok = check(
		File::clearDirectoryFiles(preparedDirectory) &&
			writeVirtualFile(
				preparedDirectory + "/game.ini",
				"[State]\nMap=candidate.map\n"),
		"save-load cancellation fixture is created") &&
		ok;
	{
		GameManager gameManager;
		CoreLifecycleTestAccess::setLogicRunning(
			gameManager,
			true);
		gameManager.map->data =
			std::make_shared<MapData>();
		gameManager.player->visible = true;
		gameManager.varList.ensureInitialized();
		gameManager.varList.setInteger(
			"partial_load_value",
			1);
		gameManager.memo.add("partial load memo");
		gameManager.traps.beginMapVisit();
		gameManager.traps.markTriggered(7);
		bool checkpointActive = true;
		const GameLoading::LoadingTaskResult result =
			CoreLifecycleTestAccess::
				finishGameLoad(
					gameManager,
					preparedDirectory,
					[&checkpointActive,
					 &gameManager](
						const std::string&,
						const std::function<bool()>&)
					{
						gameManager.map->data =
							std::make_shared<MapData>();
						checkpointActive = false;
						return false;
					},
					[&checkpointActive]()
					{
						return checkpointActive;
					});
		ok = check(
			result.status ==
				GameLoading::LoadingTaskStatus::Cancelled &&
				!CoreLifecycleTestAccess::logicRunning(
					gameManager) &&
				gameManager.map->data == nullptr &&
				!gameManager.player->visible &&
				gameManager.varList.getInteger(
					"partial_load_value") == 0 &&
				gameManager.memo.memo.empty() &&
				!gameManager.traps.hasTriggered(7) &&
				gameManager.getLastLoadFailureMessage().empty() &&
				readVirtualFile("save/game/game.ini") ==
					"[State]\nMap=candidate.map\n",
			"cancelling after world mutation discards the partial world without reporting content corruption") &&
			ok;
	}

	ok = check(
		File::clearDirectoryFiles(preparedDirectory) &&
			writeVirtualFile(
				preparedDirectory + "/game.ini",
				"[State]\nMap=candidate.map\n"),
		"save-load partial failure fixture is created") &&
		ok;
	{
		GameManager gameManager;
		CoreLifecycleTestAccess::setLogicRunning(
			gameManager,
			true);
		gameManager.map->data =
			std::make_shared<MapData>();
		auto partialNpc = std::make_shared<NPC>();
		auto partialObject = std::make_shared<Object>();
		gameManager.npcManager->npcList.push_back(
			partialNpc);
		gameManager.objectManager->objectList.push_back(
			partialObject);
		gameManager.scriptNPC = partialNpc;
		gameManager.scriptObj = partialObject;
		gameManager.global.data.mapName =
			"candidate-map";
		const GameLoading::LoadingTaskResult result =
			CoreLifecycleTestAccess::
				finishGameLoad(
					gameManager,
					preparedDirectory,
					[](
						const std::string&,
						const std::function<bool()>&)
					{
						return false;
					});
		ok = check(
			result.status ==
				GameLoading::LoadingTaskStatus::Failed &&
				!engine->isApplicationQuitRequested() &&
				!CoreLifecycleTestAccess::logicRunning(
					gameManager) &&
				gameManager.map->data == nullptr &&
				gameManager.npcManager->npcList.empty() &&
				gameManager.objectManager->objectList.empty() &&
				gameManager.scriptNPC == nullptr &&
				gameManager.scriptObj == nullptr &&
				readVirtualFile("save/game/game.ini") ==
					"[State]\nMap=candidate.map\n",
			"a partial save-load failure discards the partial world and returns to title without terminating the application") &&
			ok;
	}

	ok = check(
		File::clearDirectoryFiles(preparedDirectory) &&
			writeVirtualFile(
				"save/game/game.ini",
				"[State]\nMap=candidate.map\n") &&
			writeVirtualFile(
				preparedDirectory + "/other.ini",
				"prepared bytes"),
		"save-load exception fixture is created") &&
		ok;
	{
		GameManager gameManager;
		CoreLifecycleTestAccess::setLogicRunning(
			gameManager,
			true);
		gameManager.map->data =
			std::make_shared<MapData>();
		gameManager.npcManager->npcList.push_back(
			std::make_shared<NPC>());
		gameManager.objectManager->objectList.push_back(
			std::make_shared<Object>());
		const GameLoading::LoadingTaskResult result =
			CoreLifecycleTestAccess::
				finishGameLoad(
					gameManager,
					preparedDirectory,
					[](
						const std::string&,
						const std::function<bool()>&) -> bool
					{
						throw std::runtime_error("load failure");
					});
		ok = check(
			result.status ==
				GameLoading::LoadingTaskStatus::Failed &&
				!engine->isApplicationQuitRequested() &&
				!CoreLifecycleTestAccess::logicRunning(
					gameManager) &&
				gameManager.map->data == nullptr &&
				gameManager.npcManager->npcList.empty() &&
				gameManager.objectManager->objectList.empty() &&
				gameManager.getLastLoadFailureMessage().find(
					u8"未处理异常") !=
					std::string::npos &&
				readVirtualFile("save/game/game.ini") ==
					"[State]\nMap=candidate.map\n",
			"a load exception discards the partial world and reports the reason") &&
			ok;
	}
	engine->resetApplicationQuitRequest();
	return ok;
}

bool runMergedEntityListSaveLoadRoundTripTests()
{
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid(), "merged entity round-trip uses an isolated resource root"))
	{
		return false;
	}
	auto mapBytes = MapV3ContractFixture::build();
	std::fill(mapBytes.begin() + MapV3ContractFixture::BaseHeaderLength,
		mapBytes.begin() + MapV3ContractFixture::HeaderLength + MapV3ContractFixture::NameLength, std::uint8_t{ 0 });
	const std::string mapName = "merged-roundtrip.map";
	bool ok = check(writeVirtualFile("map/" + mapName,
		std::string(reinterpret_cast<const char*>(mapBytes.data()), mapBytes.size())),
		"write the merged entity round-trip map");
	struct MergeCase
	{
		const char* base;
		const char* merged;
		const char* explicitSave;
		bool addDirectly;
		int ordinaryCount;
	};
	const MergeCase cases[] =
	{
		{ "pilitang-guard.npc", "pilitang-night.npc", "", false, 2 },
		{ "map030_1.npc", "map030_4.npc", "", false, 2 },
		{ "temp_wudangshanxia.npc", "subevent01.npc", "temp_subevent01.npc", false, 2 },
		{ "", "unnamed-merge.npc", "", false, 1 },
		{ "", "", "", true, 1 },
		{ "", "", "", false, 0 },
	};
	for (const auto& item : cases)
	{
		for (bool asynchronous : { false, true })
		{
			const bool unnamed = item.base[0] == '\0';
			ok = check(File::clearDirectoryFiles("save/game") && File::clearDirectoryFiles("save/rpg1") &&
				writeVirtualFile("save/game/game.ini", "[State]\nMap=seed.map\nNpc=\nObj=\n"),
				"reset the merged save generation") && ok;
			const auto listBytes = [](const char* name)
			{
				return std::string("[Head]\nCount=1\n[NPC000]\nName=") + name +
					"\nKind=1\nRelation=1\nLife=31\nLifeMax=100\nMapX=0\nMapY=0\n";
			};
			if (!unnamed)
			{
				ok = check(writeVirtualFile(std::string("ini/save/") + item.base, listBytes("BaseNpc")),
					"write the base NPC template") && ok;
			}
			if (item.merged[0] != '\0')
			{
				ok = check(writeVirtualFile(std::string("ini/save/") + item.merged, listBytes("MergedNpc")),
					"write the additional NPC template") && ok;
			}
			GameManager gameManager;
			gameManager.global.data.mapName = mapName;
			gameManager.varList.ensureInitialized();
			gameManager.traps.beginMapVisit();
			gameManager.varList.setInteger("Event", 218);
			gameManager.varList.setInteger("event", 7);
			const auto execute = [&](const std::string& source)
			{
				auto bytes = std::make_unique<char[]>(source.size());
				std::copy(source.begin(), source.end(), bytes.get());
				return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
			};
			std::string source = std::string("loadnpc('") + item.base + "');";
			if (item.merged[0] != '\0')
			{
				source += std::string("mergenpc('") + item.merged + "');";
			}
			if (item.explicitSave[0] != '\0')
			{
				source += std::string("savenpc('") + item.explicitSave + "');";
			}
			ok = check(execute(source) == LUA_OK, "execute the actual LoadNpc/MergeNpc/SaveNpc API chain") && ok;
			if (item.addDirectly)
			{
				ok = check(writeVirtualFile("ini/npc/unnamed-direct.ini",
					"[Init]\nName=MergedNpc\nKind=1\nLife=31\nLifeMax=100\n") &&
					execute("addnpc('unnamed-direct.ini',0,0,0);") == LUA_OK,
					"add an ordinary NPC after an explicit clear") && ok;
			}
			const std::string expectedNamedList = item.explicitSave[0] != '\0' ? item.explicitSave : item.base;
			ok = check(gameManager.global.data.npcName == expectedNamedList,
				"MergeNpc retains the base name and explicit SaveNpc rebinds it") && ok;
			for (const auto& npc : gameManager.npcManager->findNPC("MergedNpc"))
			{
				npc->life = 19;
				npc->deathScript = "merged-death.txt";
				npc->visibleVariableName = "StoryVisible";
				npc->visibleVariableValue = 1;
				npc->updateVisibleByVariable();
			}
			auto partner = std::make_shared<NPC>();
			partner->npcName = "RoundTripPartner";
			partner->kind = nkPartner;
			partner->life = 23;
			partner->lifeMax = 100;
			gameManager.npcManager->addNPC(partner);
			if (unnamed)
			{
				ok = check(writeVirtualFile("save/game/RUNTIME-NPCS-0.NPC", "preserved-npc-file") &&
					writeVirtualFile("save/game/RUNTIME-OBJECTS-0.OBJ", "preserved-object-file"),
					"reserve case-colliding snapshot names without overwriting older map files") && ok;
				auto object = std::make_shared<Object>();
				object->objName = "RoundTripObject";
				object->kind = okBox;
				object->scriptFile = "roundtrip-object.txt";
				gameManager.objectManager->objectList.push_back(object);
			}
			if (item.ordinaryCount == 0)
			{
				for (NPCKind kind : { nkBattle, nkPlayer })
				{
					auto excluded = std::make_shared<NPC>();
					excluded->kind = kind;
					excluded->transientSummonedNPC = kind == nkBattle;
					gameManager.npcManager->addNPC(excluded);
				}
			}
			if (item.addDirectly && !asynchronous)
			{
				gameManager.traps.freeResource();
				ok = check(writeVirtualFile("save/rpg1/keep.txt", "retained-slot") && !gameManager.saveGame(1) &&
					gameManager.global.data.npcName.empty() && gameManager.global.data.objName.empty() &&
					readVirtualFile("save/game/RUNTIME-NPCS-0.NPC") == "preserved-npc-file" &&
					readVirtualFile("save/rpg1/keep.txt") == "retained-slot",
					"failed current writing retains live list names, other map state and the selected slot") && ok;
				gameManager.traps.resetToEmpty();
				int saveCheckpoints = 0;
				ok = check(!gameManager.saveGame(1, [&saveCheckpoints]()
					{
						return ++saveCheckpoints < 3;
					}) && saveCheckpoints == 3 &&
					readVirtualFile("save/game/RUNTIME-NPCS-0.NPC") == "preserved-npc-file" &&
					readVirtualFile("save/rpg1/keep.txt") == "retained-slot",
					"cancelling current writing retains other map state and the selected slot") && ok;
			}
			ok = check(asynchronous ? gameManager.scriptAPI.saveGameWithFeedback(1) : gameManager.saveGame(1),
				"save the complete merged world to a slot with both direct and feedback entry points") && ok;
			INIReader savedGlobal("save/rpg1/game.ini");
			const std::string npcName = savedGlobal.Get("State", "Npc", "");
			const std::string objectName = savedGlobal.Get("State", "Obj", "");
			const auto savedFiles = File::listFiles("save/game");
			ok = check(gameManager.global.data.npcName == npcName && gameManager.global.data.objName == objectName &&
				gameManager.saveGame(1) && gameManager.global.data.npcName == npcName &&
				gameManager.global.data.objName == objectName && File::listFiles("save/game").size() == savedFiles.size(),
				"successful publication adopts stable names and repeated saves do not accumulate snapshots") && ok;
			ok = check(!unnamed || (readVirtualFile("save/game/RUNTIME-NPCS-0.NPC") == "preserved-npc-file" &&
				readVirtualFile("save/game/RUNTIME-OBJECTS-0.OBJ") == "preserved-object-file"),
				"generated snapshot names avoid existing files case-insensitively") && ok;
			ok = check((item.ordinaryCount == 0 ? npcName.empty() : !npcName.empty()) &&
				(unnamed || npcName == expectedNamedList) && (unnamed ? !objectName.empty() : objectName.empty()),
				"full save records a usable file name for every nonempty persistent entity list") && ok;
			if (!npcName.empty())
			{
				INIReader savedNpcs(std::string("save/rpg1/") + npcName);
				ok = check(savedNpcs.GetInteger("Head", "Count", -1) == item.ordinaryCount,
					"full save writes the complete ordinary NPC collection and excludes partners") && ok;
			}
			gameManager.varList.setInteger("Event", -1);
			gameManager.npcManager->freeResource();
			const bool loaded = asynchronous ? gameManager.scriptAPI.loadGameAsync(1) : gameManager.loadGame(1);
			const auto partners = gameManager.npcManager->findNPC("RoundTripPartner");
			const auto merged = gameManager.npcManager->findNPC("MergedNpc");
			ok = check(loaded && gameManager.npcManager->npcList.size() == static_cast<size_t>(item.ordinaryCount + 1) &&
				partners.size() == 1 && partners.front()->life == 23 &&
				(item.ordinaryCount == 0 || (merged.size() == 1 && merged.front()->life == 19 &&
					merged.front()->deathScript == "merged-death.txt" && !merged.front()->isVisibleByVariable)) &&
				gameManager.varList.getInteger("Event") == 218 && gameManager.varList.getInteger("event") == 7,
				"sync and async full reload preserve merged NPC state, separate partners and case-sensitive event variables") && ok;
			ok = check(!unnamed || (gameManager.objectManager->objectList.size() == 1 &&
				gameManager.objectManager->objectList.front()->objName == "RoundTripObject" &&
				gameManager.objectManager->objectList.front()->scriptFile == "roundtrip-object.txt"),
				"full reload preserves objects created without an original list name") && ok;
		}
	}
	return ok;
}

bool runRandRunAndMapPositionContracts()
{
	ScopedActiveResourceRoot resourceRoot;
	bool ok = check(resourceRoot.valid() &&
		writeVirtualFile("script/common/RandSuccess.lua", "assign('RandBranch',1); add('RandOrder',10);") &&
		writeVirtualFile("script/common/RandFailure.lua", "assign('RandBranch',2); add('RandOrder',20);") &&
		writeVirtualFile("script/common/RandSyntax.lua", "this is not lua @") &&
		writeVirtualFile("script/map/RandMap/RandPriority.lua", "assign('RandRoute',1);") &&
		writeVirtualFile("script/goods/RandPriority.lua", "assign('RandRoute',2);") &&
		writeVirtualFile("script/common/RandPriority.lua", "assign('RandRoute',3);") &&
		writeVirtualFile("script/common/RandNpcParent.lua", "randrun('RandChance','RandNpcChild.lua',''); add('RandOrder',1);") &&
		writeVirtualFile("script/common/RandNpcChild.lua", "setnpcscript('','RandNpcBound.lua'); add('RandOrder',10);") &&
		writeVirtualFile("script/common/RandObjParent.lua", "randrun('RandChance','RandObjChild.lua',''); add('RandOrder',1);") &&
		writeVirtualFile("script/common/RandObjChild.lua", "setobjscript('','RandObjBound.lua'); add('RandOrder',10);"),
		"write isolated real RandRun branch, lookup and owner scripts");
	if (!ok) return false;
	GameManager gameManager;
	gameManager.varList.ensureInitialized();
	const auto execute = [&](const std::string& source)
	{
		auto bytes = std::make_unique<char[]>(source.size());
		std::copy(source.begin(), source.end(), bytes.get());
		return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
	};
	for (int threshold : { -100, -1, 99, 100, 1000 })
	{
		gameManager.varList.setInteger("RandChance", threshold);
		gameManager.varList.setInteger("randchance", threshold < 0 ? 99 : -1);
		gameManager.varList.setInteger("RandOrder", 0);
		ok = check(execute("randrun('RandChance','RandSuccess.lua','RandFailure.lua'); add('RandOrder',1);") == LUA_OK &&
			gameManager.varList.getInteger("RandBranch") == (threshold < 0 ? 2 : 1) &&
			gameManager.varList.getInteger("RandOrder") == (threshold < 0 ? 21 : 11),
			"RandRun uses the exact variable name and inclusive 0..99 endpoints, preserving synchronous children") && ok;
	}
	ok = check(execute("randrun('AbsentChance','RandSuccess.lua','RandSuccess.lua');") == LUA_OK &&
		gameManager.varList.getInteger("RandBranch") == 1 && gameManager.varList.getInteger("AbsentChance") == 0,
		"an absent RandRun variable uses zero and dispatches one branch without an error") && ok;
	gameManager.varList.setInteger("RandOrder", 0);
	ok = check(execute("randrun(); randrun('RandChance'); randrun('RandChance','','RandFailure.lua'); "
		"randrun('RandChance',' 0 ','RandFailure.lua'); add('RandOrder',1);") == LUA_OK &&
		gameManager.varList.getInteger("RandOrder") == 1,
		"missing arguments, empty and zero branch names are no-ops") && ok;
	CoreLifecycleTestAccess::setLastLoadFailureMessage(gameManager, "stale RandRun load error");
	ok = check(execute("randrun('RandChance','MissingRand.lua',''); assign('RandAfterMissing',1);") == LUA_OK &&
		gameManager.varList.getInteger("RandAfterMissing") == 1 && gameManager.getLastLoadFailureMessage().empty(),
		"a missing RandRun child must not inherit a stale fatal-load error") && ok;
	ok = check(execute("randrun('RandChance','RandSyntax.lua',''); assign('RandAfterSyntax',1);") == LUA_OK &&
		gameManager.varList.getInteger("RandAfterSyntax") == 1,
		"ordinary malformed child scripts retain the existing non-fatal RunScript compatibility") && ok;
	gameManager.mapFolderName = "RandMap";
	ok = check(execute("randrun('RandChance','RandPriority.lua','');") == LUA_OK &&
		gameManager.varList.getInteger("RandRoute") == 1,
		"RandRun first resolves a child in the active map folder") && ok;
	gameManager.mapFolderName = "MissingMap";
	ok = check(execute("randrun('RandChance','RandPriority.lua','');") == LUA_OK &&
		gameManager.varList.getInteger("RandRoute") == 2,
		"RandRun falls back from map to goods before common") && ok;
	gameManager.varList.setInteger("RandOrder", 0);
	auto npc = std::make_shared<NPC>();
	npc->npcName = "RandOwner";
	npc->kind = nkNormal;
	gameManager.npcManager->addNPC(npc);
	gameManager.runNPCScript(npc, "RandNpcParent.lua", false);
	ok = check(npc->scriptFile == "RandNpcBound.lua" && gameManager.varList.getInteger("RandOrder") == 11 &&
		gameManager.scriptNPC == nullptr && !gameManager.inEvent,
		"RandRun child keeps the NPC event owner and parent resumes before event cleanup") && ok;
	gameManager.varList.setInteger("RandOrder", 0);
	auto object = std::make_shared<Object>();
	object->objName = "RandObjectOwner";
	gameManager.objectManager->objectList.push_back(object);
	gameManager.runObjScript(object, "RandObjParent.lua", false);
	ok = check(object->scriptFile == "RandObjBound.lua" && gameManager.varList.getInteger("RandOrder") == 11 &&
		gameManager.scriptObj == nullptr && !gameManager.inEvent,
		"RandRun child keeps the object event owner without leaking it after return") && ok;
	for (int show : { -1, 0, 1, 8 })
	{
		ok = check(execute("setshowmappos(" + std::to_string(show) + "); setshowmappos();") == LUA_OK &&
			gameManager.global.data.scriptShowMapPos == (show > 0),
			"SetShowMapPos uses a positive numeric switch and missing arguments preserve its state") && ok;
	}
	return ok;
}

bool runScriptSpeedAndStatusTests()
{
	ScopedActiveResourceRoot resourceRoot;
	SaveFileManager::CurrentPathScope currentPath("save/speed_status_contracts");
	if (!check(resourceRoot.valid() && currentPath.valid(), "speed/status tests isolate all saved actor data"))
	{
		return false;
	}
	GameManager gameManager;
	gameManager.varList.ensureInitialized();
	gameManager.global.data.NPCAI = false;
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = gameManager.map->data->head.height = 16;
	gameManager.map->data->tile.assign(16, std::vector<MapTile>(16));
	gameManager.map->createDataMap();
	const auto execute = [&](const std::string& source)
	{
		auto bytes = std::make_unique<char[]>(source.size());
		std::copy(source.begin(), source.end(), bytes.get());
		return gameManager.script.runScript(bytes, static_cast<int>(source.size()));
	};
	auto player = gameManager.player;
	player->lifeMax = player->life = 1000;
	player->calInfo();
	player->res.walk.imagePackage = std::make_shared<IMPImage>();
	player->res.walk.imagePackage->directions = 8;
	player->res.walk.imagePackage->interval = 50;
	player->res.walk.imagePackage->frame.resize(16);
	player->walkSpeed = 2;
	player->runSpeed = 4;
	player->setPosition({ 4, 4 }, false);
	player->beginWalk({ 6, 4 });
	const UTime baselineStepTime = player->stepLastTime;
	bool ok = check(player->isWalking() && baselineStepTime > 0,
		"speed fixture starts an actual walking step");
	ok = check(execute("addmovespeedpercent(50); addmovespeedpercent(-25,999); addmovespeedpercent();") == LUA_OK &&
		player->addMoveSpeedPercent == 25 && std::abs(player->getMoveSpeedFold() - 1.25f) < 0.0001f &&
		player->getAdjustedWalkSpeed() == 2.5f && player->getAdjustedRunSpeed() == 5.0f,
		"AddMoveSpeedPercent accumulates on Player and affects both adjusted movement speeds") && ok;
	player->actionManager->resetActionIgnoringTransitions(acStand);
	player->setPosition({ 4, 4 }, false);
	player->beginWalk({ 6, 4 });
	ok = check(player->isWalking() &&
		std::abs(static_cast<double>(player->stepLastTime) - baselineStepTime / 1.25) <= 1.0,
		"a new actual walking step uses the scripted percentage with integer timing rounding") && ok;
	player->equipmentChangeMoveSpeedPercent = 15;
	ok = check(std::abs(player->getMoveSpeedFold() - 1.4f) < 0.0001f,
		"script movement percentage adds to equipment percentage") && ok;
	ok = check(execute("addmovespeedpercent(-1000);") == LUA_OK &&
		player->addMoveSpeedPercent == -975 && std::abs(player->getMoveSpeedFold() - 0.1f) < 0.0001f,
		"large negative movement percentage retains the shared minus-ninety-percent effective floor") && ok;
	player->actionManager->resetActionIgnoringTransitions(acStand);
	ok = check(player->save(0), "save the accumulated movement percentage") && ok;
	auto restoredSpeedPlayer = std::make_shared<Player>();
	ok = check(restoredSpeedPlayer->load(0) && restoredSpeedPlayer->addMoveSpeedPercent == -975,
		"a new Player restores AddMoveSpeedPercent from its own saved file") && ok;

	struct StatusCase
	{
		const char* command;
		const char* alias;
		const char* saveKey;
		bool NPC::* active;
		UTime NPC::* remaining;
	};
	const StatusCase cases[] =
	{
		{ "frozenmillisecond", "frozen", "FrozenSeconds", &NPC::frozen, &NPC::frozenLastTime },
		{ "poisonmillisecond", "poison", "PoisonSeconds", &NPC::poisoned, &NPC::poisonedLastTime },
		{ "petrifymillisecond", "petrify", "PetrifiedSeconds", &NPC::petrified, &NPC::petrifiedLastTime },
	};
	for (const auto& status : cases)
	{
		gameManager.player = std::make_shared<Player>();
		player = gameManager.player;
		player->lifeMax = player->life = 1000;
		player->calInfo();
		player->setPosition({ 4, 4 }, false);
		const std::string command(status.command);
		ok = check(execute(command + "(0); " + command + "(-1); " + command + "();") == LUA_OK &&
			!(player.get()->*status.active), "nonpositive/missing scripted status duration is a no-op") && ok;
		ok = check(execute(command + "(1250,999); " + command + "(5000); " + command +
			"(250); assign('AfterStatus',1);") == LUA_OK && player.get()->*status.active &&
			player.get()->*status.remaining == 1250 && gameManager.varList.getInteger("AfterStatus") == 1,
			"millisecond status ignores the owner/extras and does not refresh a still-active effect") && ok;
		CoreLifecycleTestAccess::advanceActorFrame(*player, 200);
		ok = check(player.get()->*status.active && player.get()->*status.remaining == 1050,
			"actual Player update deducts elapsed milliseconds from the scripted status") && ok;
		INIReader saved;
		player->saveToIni(&saved, "Init");
		ok = check(std::abs(saved.GetReal("Init", status.saveKey, -1.0) - 1.05) < 0.0001 &&
			saved.saveToFile("save/speed_status_contracts/status.ini"),
			"status persistence writes remaining seconds rather than original or elapsed duration") && ok;
		for (bool restoreAsPlayer : { false, true })
		{
			INIReader read("save/speed_status_contracts/status.ini");
			std::shared_ptr<NPC> restored = restoreAsPlayer
				? std::static_pointer_cast<NPC>(std::make_shared<Player>()) : std::make_shared<NPC>();
			restored->initFromIni(&read, "Init");
			if (auto restoredPlayer = std::dynamic_pointer_cast<Player>(restored))
			{
				restoredPlayer->calInfo();
			}
			ok = check(restored != player && restored.get()->*status.active &&
				restored.get()->*status.remaining == 1050, "new Player/NPC reads the exact remaining status duration") && ok;
			CoreLifecycleTestAccess::advanceActorFrame(*restored, 1049);
			ok = check(restored.get()->*status.active && restored.get()->*status.remaining == 1,
				"the last positive millisecond remains active") && ok;
			CoreLifecycleTestAccess::advanceActorFrame(*restored, 1);
			const std::string message = std::string(restoreAsPlayer ? "Player " : "NPC ") +
				status.command + " clears its active flag at exact expiry";
			ok = check(!(restored.get()->*status.active) && restored.get()->*status.remaining == 0,
				message.c_str()) && ok;
			if (restoreAsPlayer)
			{
				gameManager.player = std::dynamic_pointer_cast<Player>(restored);
				ok = check(execute(std::string(status.alias) + "(1250);") == LUA_OK &&
					restored.get()->*status.active && restored.get()->*status.remaining == 1250,
					"the status alias can apply again immediately after exact expiry") && ok;
			}
		}
		gameManager.player = std::make_shared<Player>();
		player = gameManager.player;
		player->nowAction = acDeath;
		ok = check(execute(command + "(1250);") == LUA_OK && !(player.get()->*status.active) &&
			player.get()->*status.remaining == 0, "a death-state Player rejects new scripted status duration") && ok;
	}
	gameManager.player = std::make_shared<Player>();
	player = gameManager.player;
	player->lifeMax = player->life = 1000;
	player->calInfo();
	player->setPosition({ 4, 4 }, false);
	ok = check(execute("frozen(500); poison(250); petrify(500); frozen(1000); poison(1000);") == LUA_OK &&
		player->petrified && player->petrifiedLastTime == 500 && !player->frozen &&
		player->poisoned && player->poisonedLastTime == 250,
		"petrify clears freeze and blocks new poison/freeze while preserving an earlier poison") && ok;
	CoreLifecycleTestAccess::advanceActorFrame(*player, 250);
	ok = check(player->life == 1000 - static_cast<int>(std::round(10 * DAMAGE_RATE)) &&
		!player->poisoned && player->poisonedLastTime == 0 &&
		player->petrified && player->petrifiedLastTime == 250,
		"exact poison expiry keeps the final damage tick without retaining an active zero-duration status") && ok;

	// SpecialKind 10 is the existing JXQY2 immobilize adaptation, not a new Lua command.
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	File::setResourceFallbackRoots({ (assetsRoot / "jxqy2").u8string(), (assetsRoot / "common").u8string() });
	auto immobilizeEffect = std::make_shared<Effect>();
	immobilizeEffect->magic.initFromIni(u8"player-magic-定身法.ini");
	immobilizeEffect->level = 1;
	immobilizeEffect->launcherKind = lkEnemy;
	ok = check(immobilizeEffect->magic.loadSucceeded &&
		immobilizeEffect->magic.level[1].specialKind == mskImmobilize &&
		immobilizeEffect->magic.getSpecialKindDurationMilliseconds(1) == 2000,
		"the actual JXQY2 immobilize magic keeps its existing kind and level-based duration") && ok;
	gameManager.player = std::make_shared<Player>();
	player = gameManager.player;
	player->npcName = "ImmobilizeTarget";
	player->lifeMax = player->life = 1000;
	player->calInfo();
	player->setPosition({ 4, 4 }, false);
	player->directHurt(immobilizeEffect);
	ok = check(player->immobilized && player->immobilizedLastTime == 2000,
		"actual magic damage applies immobilize") && ok;
	CoreLifecycleTestAccess::advanceActorFrame(*player, 750);
	player->directHurt(immobilizeEffect);
	ok = check(player->immobilizedLastTime == 1250,
		"another hit does not refresh a still-active immobilize") && ok;
	ok = check(execute("addnpcproperty('ImmobilizeTarget','ImmobilizedMilliseconds',250); "
		"addnpcproperty('ImmobilizeTarget','ImmobilizedVisualEffect',-1);") == LUA_OK &&
		player->immobilizedLastTime == 1500 && !player->immobilizedVisualEffect,
		"generic Lua properties retain additive duration and independent visual state") && ok;
	CoreLifecycleTestAccess::advanceActorFrame(*player, 250);
	INIReader savedImmobilize;
	player->saveToIni(&savedImmobilize, "Init");
	if (!check(savedImmobilize.saveToFile("save/speed_status_contracts/immobilize.ini"),
		"write the immobilize fixture before testing persisted fields"))
	{
		return false;
	}
	ok = check(std::abs(savedImmobilize.GetReal("Init", "ImmobilizedSeconds", -1.0) - 1.25) < 0.0001 &&
		!savedImmobilize.GetBoolean("Init", "IsImmobilizedVisualEffect", true),
		"immobilize saves remaining fractional seconds and the visual flag to a real file") && ok;
	for (bool restoreAsPlayer : { false, true })
	{
		INIReader read("save/speed_status_contracts/immobilize.ini");
		std::shared_ptr<NPC> restored = restoreAsPlayer
			? std::static_pointer_cast<NPC>(std::make_shared<Player>()) : std::make_shared<NPC>();
		restored->initFromIni(&read, "Init");
		if (auto restoredPlayer = std::dynamic_pointer_cast<Player>(restored))
		{
			restoredPlayer->calInfo();
		}
		restored->res.walk.imagePackage = std::make_shared<IMPImage>();
		restored->res.walk.imagePackage->directions = 8;
		restored->res.walk.imagePackage->interval = 50;
		restored->res.walk.imagePackage->frame.resize(16);
		restored->beginWalk({ 6, 4 });
		ok = check(restored != player && restored->immobilized && restored->immobilizedLastTime == 1250 &&
			!restored->immobilizedVisualEffect && !restored->isWalking(),
			"new NPC/Player restores immobilize and rejects movement while active") && ok;
		const UTime pausedTime = restored->getTime();
		CoreLifecycleTestAccess::advanceActorFrame(*restored, 1249);
		ok = check(restored->immobilized && restored->immobilizedLastTime == 1 && restored->getTime() == pausedTime,
			"immobilize pauses action time through its last positive millisecond") && ok;
		CoreLifecycleTestAccess::advanceActorFrame(*restored, 1);
		const std::string label = restoreAsPlayer ? "Player" : "NPC";
		ok = check(!restored->immobilized && restored->immobilizedLastTime == 0 && restored->immobilizedVisualEffect,
			(label + " clears immobilize and resets visuals at exact expiry").c_str()) && ok;
		restored->directHurt(immobilizeEffect);
		ok = check(restored->immobilized && restored->immobilizedLastTime == 2000,
			(label + " accepts another actual immobilize hit immediately after exact expiry").c_str()) && ok;
		CoreLifecycleTestAccess::advanceActorFrame(*restored, 2050);
		ok = check(!restored->immobilized && restored->immobilizedLastTime == 0 && restored->getTime() == pausedTime + 50,
			"a frame beyond immobilize expiry resumes only its unblocked remainder") && ok;
		restored->beginWalk({ 6, 4 });
		ok = check(restored->isWalking(), "the same NPC/Player can begin walking once immobilize expires") && ok;
	}
	return ok;
}

bool runEmptyEntityListSaveLoadRoundTripTests()
{
	ScopedActiveResourceRoot resourceRoot;
	if (!check(
			resourceRoot.valid(),
			"empty entity-list round-trip test created an isolated resource root"))
	{
		return false;
	}

	std::vector<std::uint8_t> mapBytes =
		MapV3ContractFixture::build();
	std::fill(
		mapBytes.begin() + MapV3ContractFixture::BaseHeaderLength,
		mapBytes.begin() + MapV3ContractFixture::HeaderLength,
		std::uint8_t{ 0 });
	std::fill(
		mapBytes.begin() + MapV3ContractFixture::HeaderLength,
		mapBytes.begin() + MapV3ContractFixture::HeaderLength +
			MapV3ContractFixture::NameLength,
		std::uint8_t{ 0 });
	const std::string mapName = "empty-list-roundtrip.map";
	if (!check(
			writeVirtualFile(
				"map/" + mapName,
				std::string(
					reinterpret_cast<const char*>(mapBytes.data()),
					mapBytes.size())) &&
			writeVirtualFile("ini/map/roundtrip-rain.ini",
				"[Init]\nNumber=4\nSpeed=80\nBoltProb=10000\n") &&
			writeVirtualFile("ini/goods/roundtrip_equipment.ini",
				"[Init]\nName=RoundTripEquipment\nKind=1\nPart=Head\nLifeMax=50\nThewMax=40\nManaMax=30\n") &&
			writeVirtualFile("ini/goods/stale_equipment.ini",
				"[Init]\nName=StaleEquipment\nKind=1\nPart=Head\nMagicIniWhenUse=stale_equipment_magic.ini\n") &&
			writeVirtualFile("ini/magic/stale_equipment_magic.ini",
				"[Init]\nName=StaleEquipmentMagic\nMoveKind=2\n[Level1]\nMoveKind=2\n"),
			"empty entity-list round-trip map fixture is created"))
	{
		return false;
	}

	struct EntityListCase
	{
		const char* npcName;
		const char* objectName;
		const char* description;
	};
	const EntityListCase cases[] =
	{
		{ "", "roundtrip.obj", "empty NPC list" },
		{ "roundtrip.npc", "", "empty object list" },
		{ "", "", "empty NPC and object lists" },
		{ "roundtrip.npc", "roundtrip.obj", "empty named entity lists" },
	};

	Engine* engine = Engine::getInstance();
	engine->resetApplicationQuitRequest();
	bool ok = true;
	for (const EntityListCase& entityCase : cases)
	{
		const std::string fixtureMessage =
			std::string(entityCase.description) +
			" fixture is created";
		const bool fixtureReady =
			File::clearDirectoryFiles("save/game") &&
			File::clearDirectoryFiles("save/game_build") &&
			File::clearDirectoryFiles("save/load_candidate") &&
			File::clearDirectoryFiles("save/rpg1") &&
			writeVirtualFile(
				"save/game/game.ini",
				"[State]\nMap=seed.map\nNpc=\nObj=\n");
		ok = check(fixtureReady, fixtureMessage.c_str()) && ok;
		if (!fixtureReady)
		{
			continue;
		}

		GameManager gameManager;
		gameManager.global.data.mapName = mapName;
		gameManager.global.data.npcName = entityCase.npcName;
		gameManager.global.data.objName = entityCase.objectName;
		const bool savedWeatherEnabled = entityCase.npcName[0] == '\0';
		gameManager.global.data.rainShow = savedWeatherEnabled;
		gameManager.global.data.snowShow = savedWeatherEnabled;
		gameManager.global.data.waterEffect = savedWeatherEnabled;
		gameManager.global.data.rainFile = savedWeatherEnabled ? "roundtrip-rain.ini" : "";
		gameManager.global.data.mapTime = mtDawn;
		gameManager.global.data.mpcStyle = ColorStyle::Grayscale;
		gameManager.global.data.asfStyle = 0x008096C8;
		gameManager.global.data.mainLum = 17;
		gameManager.global.data.fadeLum = 8;
		const bool savedActionsDisabled = entityCase.npcName[0] == '\0';
		const std::string dropScript = savedActionsDisabled ? "disabledrop();" : "enabeldrop();";
		auto dropScriptBytes = std::make_unique<char[]>(dropScript.size());
		std::copy(dropScript.begin(), dropScript.end(), dropScriptBytes.get());
		ok = check(gameManager.script.runScript(dropScriptBytes, static_cast<int>(dropScript.size())) == LUA_OK &&
			gameManager.global.data.dropDisabled == savedActionsDisabled,
			"Lua sets the drop permission before the complete save") && ok;
		gameManager.player->setRunDisabled(savedActionsDisabled);
		gameManager.player->setJumpDisabled(!savedActionsDisabled);
		gameManager.player->setFightDisabled(savedActionsDisabled);
		gameManager.global.data.NPCAI = !savedActionsDisabled;
		gameManager.global.data.canInput = !savedActionsDisabled;
		gameManager.varList.ensureInitialized();
		gameManager.traps.beginMapVisit();
		gameManager.player->lifeMax = 100;
		gameManager.player->thewMax = 100;
		gameManager.player->manaMax = 100;
		const auto equip = [&](const char* fileName)
		{
			auto& item = gameManager.goodsManager.goodsList[
				gameManager.goodsManager.equipIndex(0)];
			item.iniFile = fileName;
			item.number = 1;
			item.goods = std::make_shared<Goods>();
			item.goods->initFromIni(fileName);
			gameManager.player->resetEquipmentGrantedMagicSync();
			gameManager.player->calInfo();
			return item.goods->loadSucceeded;
		};
		ok = check(equip("roundtrip_equipment.ini"), "load saved character equipment") && ok;
		gameManager.player->life = 140;
		gameManager.player->thew = 130;
		gameManager.player->mana = 120;
		gameManager.player->npcName = "RoundTripStatusPlayer";
		const std::string statusScript = std::string("addmovespeedpercent(37); poisonmillisecond(1500); ") +
			(savedActionsDisabled ? "frozenmillisecond(1250); setshowmappos(1); "
				"addnpcproperty('RoundTripStatusPlayer','ImmobilizedMilliseconds',900); "
				"addnpcproperty('RoundTripStatusPlayer','ImmobilizedVisualEffect',-1);"
				: "petrifymillisecond(1250); setshowmappos(0);");
		auto statusScriptBytes = std::make_unique<char[]>(statusScript.size());
		std::copy(statusScript.begin(), statusScript.end(), statusScriptBytes.get());
		ok = check(gameManager.script.runScript(statusScriptBytes, static_cast<int>(statusScript.size())) == LUA_OK,
			"set real scripted speed and status durations before the complete save") && ok;
		const bool saved = gameManager.saveGame(1);
		INIReader savedGlobal("save/rpg1/game.ini");
		ok = check(savedGlobal.GetBoolean("Option", "ScriptShowMapPos", !savedActionsDisabled) == savedActionsDisabled,
			"complete save writes the Lua map-position switch") && ok;
		auto savedEntityListIsEmpty = [](const char* fileName)
		{
			if (fileName[0] == '\0')
			{
				return true;
			}
			INIReader savedList(
				std::string("save/rpg1/") + fileName);
			return savedList.ParseError() == 0 &&
				savedList.GetInteger("Head", "Count", -1) == 0;
		};
		const std::string saveMessage =
			std::string(entityCase.description) +
			" is saved with the original empty names";
		ok = check(
			saved && savedGlobal.ParseError() == 0 &&
				savedGlobal.Get("Save", "EngineVersion", "") ==
					JxqyBuildVersion::EngineVersion &&
				savedGlobal.Get("Save", "ResourceVersion", "") == "1.0.0" &&
				savedGlobal.Get("State", "Npc", "missing") ==
					entityCase.npcName &&
				savedGlobal.Get("State", "Obj", "missing") ==
					entityCase.objectName &&
				savedGlobal.GetBoolean("Option", "RainShow", !savedWeatherEnabled) == savedWeatherEnabled &&
				savedGlobal.Get("Option", "RainFile", "missing") == (savedWeatherEnabled ? "roundtrip-rain.ini" : "") &&
				savedGlobal.GetBoolean("Option", "NPCAI", savedActionsDisabled) == !savedActionsDisabled &&
				savedGlobal.GetBoolean("Option", "CanInput", savedActionsDisabled) == !savedActionsDisabled &&
				savedGlobal.GetBoolean("Option", "DropDisabled", !savedActionsDisabled) == savedActionsDisabled &&
				savedEntityListIsEmpty(entityCase.npcName) &&
				savedEntityListIsEmpty(entityCase.objectName),
			saveMessage.c_str()) && ok;

		gameManager.global.data.npcName = "stale.npc";
		gameManager.global.data.objName = "stale.obj";
		auto staleNpc = std::make_shared<NPC>();
		staleNpc->kind = nkNormal;
		gameManager.npcManager->npcList.push_back(staleNpc);
		gameManager.objectManager->objectList.push_back(
			std::make_shared<Object>());
		gameManager.scriptAPI.showSnow(!savedWeatherEnabled);
		gameManager.scriptAPI.showRain(!savedWeatherEnabled);
		gameManager.global.data.waterEffect = !savedWeatherEnabled;
		gameManager.global.data.bgmName = "previous-map.mp3";
		gameManager.global.data.mapTime = mtDay;
		gameManager.global.data.mpcStyle = ColorStyle::Normal;
		gameManager.global.data.asfStyle = ColorStyle::Normal;
		gameManager.global.data.mainLum = 31;
		gameManager.global.data.fadeLum = 0;
		gameManager.player->setRunDisabled(!savedActionsDisabled);
		gameManager.player->setJumpDisabled(savedActionsDisabled);
		gameManager.player->setFightDisabled(!savedActionsDisabled);
		gameManager.global.data.NPCAI = savedActionsDisabled;
		gameManager.global.data.canInput = savedActionsDisabled;
		gameManager.global.data.dropDisabled = !savedActionsDisabled;
		gameManager.global.data.scriptShowMapPos = !savedActionsDisabled;
		gameManager.player->clearPoisonedState();
		gameManager.player->clearFrozenState();
		gameManager.player->clearPetrifiedState();
		gameManager.player->immobilized = !savedActionsDisabled;
		gameManager.player->immobilizedLastTime = savedActionsDisabled ? 0 : 700;
		gameManager.player->immobilizedVisualEffect = savedActionsDisabled;
		gameManager.player->addMoveSpeedPercent = -9;
		ok = check(equip("stale_equipment.ini") &&
			gameManager.magicManager.findPrimaryMagic("stale_equipment_magic.ini") != nullptr,
			"live pre-load equipment grants a distinct magic") && ok;
		// Two cases also cover the compatible malformed-Magic fallback, one
		// through each loading path. Neither may rebuild using stale equipment.
		if (savedActionsDisabled)
		{
			ok = check(writeVirtualFile("save/rpg1/magic0.ini", "[Head\nCount=0\n"),
				"prepare malformed saved Magic for compatible empty-list fallback") && ok;
		}
		// The four cases cover both states of weather and action permissions
		// through both loading paths.
		const bool loaded = entityCase.objectName[0] == '\0'
			? gameManager.scriptAPI.loadGameAsync(1)
			: gameManager.loadGame(1);
		ok = check(loaded && gameManager.player->life == 140 &&
			gameManager.player->thew == 130 && gameManager.player->mana == 120 &&
			gameManager.player->getLifeMax() == 150 &&
			gameManager.player->getThewMax() == 140 &&
			gameManager.player->getManaMax() == 130 &&
			gameManager.magicManager.findPrimaryMagic("stale_equipment_magic.ini") == nullptr,
			"full sync/async load rebuilds attributes only after both saved lists are ready") && ok;
		ok = check(loaded && gameManager.player->addMoveSpeedPercent == 37 &&
			gameManager.player->poisoned && gameManager.player->poisonedLastTime == 1500 &&
			gameManager.player->frozen == savedActionsDisabled &&
			gameManager.player->frozenLastTime == (savedActionsDisabled ? 1250u : 0u) &&
			gameManager.player->petrified == !savedActionsDisabled &&
			gameManager.player->petrifiedLastTime == (savedActionsDisabled ? 0u : 1250u),
			"full sync/async save-load restores scripted speed and all three remaining status durations") && ok;
		ok = check(loaded && gameManager.player->immobilized == savedActionsDisabled &&
			gameManager.player->immobilizedLastTime == (savedActionsDisabled ? 900u : 0u) &&
			gameManager.player->immobilizedVisualEffect == !savedActionsDisabled,
			"full sync/async save-load restores active/inactive immobilize and its visual flag") && ok;
		ok = check(loaded
			&& gameManager.global.data.bgmName.empty()
			&& gameManager.global.data.rainShow == savedWeatherEnabled
			&& gameManager.global.data.snowShow == savedWeatherEnabled
			&& gameManager.global.data.waterEffect == savedWeatherEnabled
			&& gameManager.global.data.rainFile == (savedWeatherEnabled ? "roundtrip-rain.ini" : "")
			&& gameManager.global.data.mapTime == mtDawn
			&& gameManager.global.data.mpcStyle == ColorStyle::Grayscale
			&& gameManager.global.data.asfStyle == 0x008096C8
			&& gameManager.global.data.mainLum == 17
			&& gameManager.global.data.fadeLum == 8
			&& gameManager.weather->isRaining() == savedWeatherEnabled
			&& gameManager.weather->isSnowing() == savedWeatherEnabled
			&& (!savedWeatherEnabled || gameManager.weather->getConfiguredRainDropCount() == 4),
			"full save/load restores weather, scene colors, luminance and an empty BGM independently of the previous world") && ok;
		ok = check(loaded
			&& gameManager.player->isRunDisabled() == savedActionsDisabled
			&& gameManager.player->isJumpDisabled() == !savedActionsDisabled
			&& gameManager.player->isFightDisabled() == savedActionsDisabled
			&& gameManager.global.data.NPCAI == !savedActionsDisabled
			&& gameManager.global.data.dropDisabled == savedActionsDisabled
			&& gameManager.global.data.scriptShowMapPos == savedActionsDisabled
			&& gameManager.global.data.canInput == !savedActionsDisabled,
			"full save/load restores each player restriction and global AI/input permission after loading the map") && ok;
		const std::string loadMessage =
			std::string(entityCase.description) +
			" save reloads as empty runtime lists without leaking a source label";
		const std::vector<std::string> loadedFiles =
			File::listFiles("save/game");
		ok = check(
			loaded &&
				gameManager.global.data.npcName ==
					entityCase.npcName &&
				gameManager.global.data.objName ==
					entityCase.objectName &&
				gameManager.npcManager->npcList.empty() &&
				gameManager.objectManager->objectList.empty() &&
				std::none_of(
					loadedFiles.cbegin(),
					loadedFiles.cend(),
					[](const std::string& fileName)
					{
						return fileName.find(
							"__compatible_empty_") !=
							std::string::npos;
					}),
			loadMessage.c_str()) && ok;
		if (savedWeatherEnabled)
		{
			ok = check(gameManager.scriptAPI.loadMap(mapName, false)
				&& !gameManager.global.data.rainShow
				&& gameManager.global.data.rainFile.empty()
				&& !gameManager.weather->isRaining()
				&& gameManager.weather->isSnowing()
				&& !gameManager.player->isFightDisabled()
				&& gameManager.player->isRunDisabled()
				&& !gameManager.player->isJumpDisabled()
				&& !gameManager.global.data.NPCAI
				&& !gameManager.global.data.canInput,
				"ordinary map loading stops rain and enables fight without clearing snow or the other action permissions") && ok;
		}
	}

	struct CorruptEntityListCase
	{
		const char* fileName;
		bool npcList;
		const char* description;
		const char* bytes;
	};
	const CorruptEntityListCase corruptEntityCases[] =
	{
		{ "roundtrip.npc", true, "malformed NPC list", "[Broken\n" },
		{ "roundtrip.obj", false, "malformed object list", "[Broken\n" },
		{ "roundtrip.npc", true, "invalid NPC count", "[Head]\nCount=invalid\n" },
		{ "roundtrip.obj", false, "invalid object count", "[Head]\nCount=invalid\n" },
		{ "roundtrip.npc", true, "missing NPC section", "[Head]\nCount=1\n" },
		{ "roundtrip.obj", false, "missing object section", "[Head]\nCount=1\n" },
	};
	for (const CorruptEntityListCase& corruptCase :
		corruptEntityCases)
	{
		const std::string slotPath =
			"save/rpg1/" + std::string(corruptCase.fileName);
		const std::string originalBytes =
			readVirtualFile(slotPath);
		const bool fixtureReady =
			!originalBytes.empty() &&
			writeVirtualFile(slotPath, corruptCase.bytes);
		ok = check(
			fixtureReady,
			(std::string(corruptCase.description) +
				" fixture is created").c_str()) && ok;
		if (!fixtureReady)
		{
			continue;
		}

		for (bool asynchronous : { false, true })
		{
			GameManager tolerantLoader;
			const bool loaded = asynchronous
				? tolerantLoader.scriptAPI.loadGameAsync(1)
				: tolerantLoader.loadGame(1);
			INIReader repairedList(
				"save/game/" + std::string(corruptCase.fileName));
			const bool runtimeListEmpty = corruptCase.npcList
				? tolerantLoader.npcManager->npcList.empty()
				: tolerantLoader.objectManager->objectList.empty();
			ok = check(
				loaded &&
					tolerantLoader.getLastLoadFailureMessage().empty() &&
					runtimeListEmpty &&
					readVirtualFile(slotPath) == corruptCase.bytes &&
					repairedList.ParseError() == 0 &&
					repairedList.GetInteger("Head", "Count", -1) == 0,
				(std::string(corruptCase.description) +
					(asynchronous ? " async" : " sync") +
					" is normalized in the current generation without changing the selected slot").c_str()) && ok;
		}
		ok = check(
			writeVirtualFile(slotPath, originalBytes),
			(std::string(corruptCase.description) +
				" fixture restores the selected slot").c_str()) && ok;
	}

	INIReader reloadGlobal("save/rpg1/game.ini");
	const int characterIndex = static_cast<int>(
		reloadGlobal.GetInteger("State", "Chr", -1));
	const std::string globalVirtualPath =
		"save/rpg1/game.ini";
	const std::string validGlobalBytes =
		readVirtualFile(globalVirtualPath);
	const std::string legacySaveMessage =
		u8"抱歉，之前有严重bug，旧存档不兼容，需要重开，请谅解！";
	const std::string minimalGlobalState =
		"[State]\nMap=" + mapName +
		"\nNpc=\nObj=\nChr=" +
		std::to_string(characterIndex) + "\n";
	ok = check(
		writeVirtualFile(globalVirtualPath, minimalGlobalState),
		"legacy save fixture without an engine version is created") && ok;
	{
		GameManager legacyLoader;
		legacyLoader.map->data = std::make_shared<MapData>();
		const auto previousMap = legacyLoader.map->data;
		const bool loaded = legacyLoader.loadGame(1);
		ok = check(
			!loaded &&
				legacyLoader.map->data == previousMap &&
				legacyLoader.getLastLoadFailureMessage() ==
					legacySaveMessage,
			"a save without EngineVersion is treated as 1.0.0 and rejected before world mutation") && ok;
	}
	ok = check(
		writeVirtualFile(
			globalVirtualPath,
			minimalGlobalState +
			"[Save]\nEngineVersion=1.0.6\n"),
		"1.0.6 save fixture is created") && ok;
	{
		GameManager legacyLoader;
		const bool loaded =
			legacyLoader.scriptAPI.loadGameAsync(1);
		ok = check(
			!loaded &&
				legacyLoader.map->data == nullptr &&
				legacyLoader.getLastLoadFailureMessage() ==
					legacySaveMessage,
			"the asynchronous path rejects a 1.0.6 save with the same message") && ok;
	}
	ok = check(
		writeVirtualFile(
			globalVirtualPath,
			minimalGlobalState +
			"[Save]\nEngineVersion=invalid\n"),
		"invalid save engine version fixture is created") && ok;
	{
		GameManager invalidVersionLoader;
		const bool loaded = invalidVersionLoader.loadGame(1);
		ok = check(
			!loaded &&
				invalidVersionLoader.getLastLoadFailureMessage() ==
					u8"存档引擎版本格式错误",
			"an invalid save engine version is rejected") && ok;
	}
	ok = check(
		writeVirtualFile(
			globalVirtualPath,
			minimalGlobalState +
			"[Save]\nEngineVersion=999.0.0\n"),
		"future save engine version fixture is created") && ok;
	{
		GameManager futureVersionLoader;
		const bool loaded = futureVersionLoader.loadGame(1);
		ok = check(
			!loaded &&
				futureVersionLoader.getLastLoadFailureMessage().find(
					u8"更高版本引擎") != std::string::npos,
			"a save from a future engine version is rejected") && ok;
	}
	const std::string initialTemplatePath = "ini/save/game.ini";
	for (const char* resourceVersion : { "invalid", "999.0.0" })
	{
		ok = check(writeVirtualFile(globalVirtualPath, minimalGlobalState +
			"[Save]\nEngineVersion=" + JxqyBuildVersion::EngineVersion +
			"\nResourceVersion=" + resourceVersion + "\n"),
			"resource save version fixture is created") && ok;
		GameManager resourceVersionLoader;
		const auto previousMap = resourceVersionLoader.map->data;
		const bool loaded = resourceVersionLoader.scriptAPI.loadGameAsync(1);
		ok = check(!loaded && resourceVersionLoader.map->data == previousMap &&
			resourceVersionLoader.getLastLoadFailureMessage().find(u8"资源") != std::string::npos,
			"resource version rejection precedes asynchronous world mutation") && ok;
	}
	const std::string initialTemplateBytes =
		readVirtualFile(initialTemplatePath);
	if (!initialTemplateBytes.empty())
	{
		ok = check(
			writeVirtualFile(
				initialTemplatePath,
				minimalGlobalState),
			"old initial save template fixture is created") && ok;
		GameManager oldResourceLoader;
		const bool loaded = oldResourceLoader.loadGame(0);
		ok = check(
			!loaded &&
				oldResourceLoader.map->data == nullptr &&
				oldResourceLoader.getLastLoadFailureMessage() ==
					u8"资源包版本过旧，请更新资源包后重开游戏" &&
				writeVirtualFile(
					initialTemplatePath,
					initialTemplateBytes),
			"an old initial template reports a resource update requirement") && ok;
	}
	ok = check(
		writeVirtualFile(globalVirtualPath, validGlobalBytes),
		"save engine compatibility fixtures restore the valid save") && ok;
	struct UnsafeEntityStateCase
	{
		const char* npcName;
		const char* objectName;
		const char* description;
	};
	const UnsafeEntityStateCase unsafeEntityCases[] =
	{
		{ "../escape.npc", "roundtrip.obj", "unsafe NPC path" },
		{ "game.ini", "roundtrip.obj", "reserved NPC file name" },
		{ "shared.npc", "SHARED.NPC", "case-colliding entity file names" },
	};
	for (const UnsafeEntityStateCase& unsafeCase :
		unsafeEntityCases)
	{
		const bool fixtureReady = writeVirtualFile(
			globalVirtualPath,
			"[State]\nMap=" + mapName +
			"\nNpc=" + unsafeCase.npcName +
			"\nObj=" + unsafeCase.objectName +
			"\nChr=" + std::to_string(characterIndex) +
			"\n[Save]\nEngineVersion=" +
			JxqyBuildVersion::EngineVersion + "\n");
		ok = check(
			fixtureReady,
			(std::string(unsafeCase.description) +
				" load fixture is created").c_str()) && ok;
		if (!fixtureReady)
		{
			continue;
		}
		GameManager rejectedLoader;
		const bool loaded = rejectedLoader.loadGame(1);
		ok = check(
			!loaded &&
				rejectedLoader.map->data == nullptr &&
				!rejectedLoader.getLastLoadFailureMessage().empty(),
			(std::string(unsafeCase.description) +
				" is rejected before world mutation").c_str()) && ok;
		ok = check(
			writeVirtualFile(
				globalVirtualPath,
				validGlobalBytes),
			(std::string(unsafeCase.description) +
				" fixture restores the selected slot").c_str()) && ok;
	}
	const std::string playerFileName = characterIndex < 0
		? std::string("player.ini")
		: "player" + std::to_string(characterIndex) + ".ini";
	const std::string playerVirtualPath =
		"save/rpg1/" + playerFileName;
	const std::string validPlayerBytes =
		readVirtualFile(playerVirtualPath);
	const bool emptyPlayerFixtureReady =
		reloadGlobal.ParseError() == 0 &&
		!validPlayerBytes.empty() &&
		writeVirtualFile(
			"ini/save/" + playerFileName,
			validPlayerBytes) &&
		writeVirtualFile(playerVirtualPath, {});
	ok = check(
		emptyPlayerFixtureReady,
		"zero-byte player fixture is created from a complete save") && ok;
	if (emptyPlayerFixtureReady)
	{
		{
			GameManager tolerantLoader;
			tolerantLoader.player->npcName = "FallbackPlayer";
			tolerantLoader.player->money = 731;
			const bool loaded = tolerantLoader.loadGame(1);
			INIReader repairedPlayer(
				"save/game/" + playerFileName);
			ok = check(
				loaded &&
					tolerantLoader.player->npcName !=
						"FallbackPlayer" &&
					tolerantLoader.player->money != 731 &&
					tolerantLoader.map->isInMap(
						tolerantLoader.player->getPosition()) &&
					repairedPlayer.ParseError() == 0 &&
					repairedPlayer.HasSection("Init") &&
					File::fileExist(playerVirtualPath) &&
					readVirtualFile(playerVirtualPath).empty() &&
					tolerantLoader.getLastLoadFailureMessage().empty(),
				"a zero-byte player file falls back to the initial template and repairs the current save") &&
				ok;
		}
		{
			GameManager asyncTolerantLoader;
			asyncTolerantLoader.player->npcName =
				"AsyncFallbackPlayer";
			const bool asyncLoaded =
				asyncTolerantLoader.scriptAPI.loadGameAsync(1);
			ok = check(
				asyncLoaded &&
					asyncTolerantLoader.player->npcName !=
						"AsyncFallbackPlayer" &&
					asyncTolerantLoader.map->isInMap(
						asyncTolerantLoader.player->getPosition()) &&
					asyncTolerantLoader.
						getLastLoadFailureMessage().empty(),
				"the asynchronous path also uses the initial player template") &&
				ok;
		}
		ok = check(
			writeVirtualFile(
				playerVirtualPath,
				validPlayerBytes),
			"zero-byte player fixture restores the valid source file") &&
			ok;
	}

	const std::string missingMapName =
		"missing-load-failure.map";
	const bool fatalMapFixtureReady =
		!validGlobalBytes.empty() &&
		writeVirtualFile(
			globalVirtualPath,
			"[State]\nMap=" + missingMapName +
			"\nNpc=\nObj=\nChr=" +
			std::to_string(characterIndex) +
			"\n[Save]\nEngineVersion=" +
			JxqyBuildVersion::EngineVersion + "\n");
	ok = check(
		fatalMapFixtureReady,
		"missing-map fatal load fixture is created") && ok;
	if (fatalMapFixtureReady)
	{
		{
			GameManager failedLoader;
			failedLoader.map->data = std::make_shared<MapData>();
			const bool loaded = failedLoader.loadGame(1);
			ok = check(
				!loaded &&
					failedLoader.map->data == nullptr &&
					readVirtualFile("save/game/game.ini") == readVirtualFile(globalVirtualPath) &&
					failedLoader.getLastLoadFailureMessage().find(
						missingMapName) != std::string::npos,
				"a missing map leaves the selected save intact and exits the overwritten runtime world") &&
				ok;
		}
		{
			GameManager asyncFailedLoader;
			asyncFailedLoader.map->data = std::make_shared<MapData>();
			const bool loaded =
				asyncFailedLoader.scriptAPI.loadGameAsync(1);
			ok = check(
				!loaded &&
					asyncFailedLoader.map->data == nullptr &&
					readVirtualFile("save/game/game.ini") == readVirtualFile(globalVirtualPath) &&
					asyncFailedLoader.getLastLoadFailureMessage().find(
						missingMapName) != std::string::npos,
				"the asynchronous path reports the same fatal map reason") &&
				ok;
		}
		{
			GameManager scriptFailureLoader;
			scriptFailureLoader.varList.ensureInitialized();
			const std::string loadFailureScript =
				"loadgame(1); assign('continued_after_failed_load', 1)";
			auto loadFailureScriptBytes = std::make_unique<char[]>(
				loadFailureScript.size());
			std::copy(
				loadFailureScript.cbegin(),
				loadFailureScript.cend(),
				loadFailureScriptBytes.get());
			const int scriptResult =
				scriptFailureLoader.script.runScript(
					loadFailureScriptBytes,
					static_cast<int>(loadFailureScript.size()));
			ok = check(
				scriptResult != 0 &&
					scriptFailureLoader.varList.getInteger(
						"continued_after_failed_load") == 0 &&
					!scriptFailureLoader.getLastLoadFailureMessage().empty(),
				"a failed Lua loadgame call aborts the current script instead of running opening events against an unavailable world") &&
				ok;
		}
		{
			const std::string childScriptPath =
				"script/common/load-failure-child.txt";
			const bool childScriptReady = writeVirtualFile(
				childScriptPath,
				"loadgame(1)");
			GameManager nestedScriptFailureLoader;
			nestedScriptFailureLoader.varList.ensureInitialized();
			const std::string parentScript =
				"runscript('load-failure-child.txt'); "
				"assign('continued_after_child_failed_load', 1)";
			auto parentScriptBytes = std::make_unique<char[]>(
				parentScript.size());
			std::copy(
				parentScript.cbegin(),
				parentScript.cend(),
				parentScriptBytes.get());
			const int scriptResult = childScriptReady
				? nestedScriptFailureLoader.script.runScript(
					parentScriptBytes,
					static_cast<int>(parentScript.size()))
				: -1;
			ok = check(
				childScriptReady &&
					scriptResult != 0 &&
					nestedScriptFailureLoader.varList.getInteger(
						"continued_after_child_failed_load") == 0 &&
					!nestedScriptFailureLoader.
						getLastLoadFailureMessage().empty(),
				"a failed loadgame in a nested runscript aborts the parent script") &&
				ok;
			for (int threshold : { -1, 99 })
			{
				GameManager randomChildFailureLoader;
				randomChildFailureLoader.varList.ensureInitialized();
				randomChildFailureLoader.varList.setInteger("FatalBranchChance", threshold);
				const std::string randomParent =
					"randrun('FatalBranchChance','load-failure-child.txt','load-failure-child.txt'); "
					"assign('continued_after_rand_failed_load',1);";
				auto bytes = std::make_unique<char[]>(randomParent.size());
				std::copy(randomParent.begin(), randomParent.end(), bytes.get());
				const int result = randomChildFailureLoader.script.runScript(bytes, static_cast<int>(randomParent.size()));
				ok = check(result != LUA_OK &&
					randomChildFailureLoader.varList.getInteger("continued_after_rand_failed_load") == 0 &&
					!randomChildFailureLoader.getLastLoadFailureMessage().empty(),
					"fatal load failure in either RandRun branch aborts its parent just like RunScript") && ok;
			}
		}
		{
			GameManager missingChildLoader;
			missingChildLoader.varList.ensureInitialized();
			CoreLifecycleTestAccess::setLastLoadFailureMessage(
				missingChildLoader,
				"stale load failure");
			const std::string parentScript =
				"runscript('missing-child.txt'); "
				"assign('continued_after_missing_child', 1)";
			auto parentScriptBytes = std::make_unique<char[]>(
				parentScript.size());
			std::copy(
				parentScript.cbegin(),
				parentScript.cend(),
				parentScriptBytes.get());
			const int scriptResult =
				missingChildLoader.script.runScript(
					parentScriptBytes,
					static_cast<int>(parentScript.size()));
			ok = check(
				scriptResult == 0 &&
					missingChildLoader.varList.getInteger(
						"continued_after_missing_child") == 1 &&
					missingChildLoader.
						getLastLoadFailureMessage().empty(),
				"a missing child script does not reuse a stale load failure") &&
				ok;
		}
		ok = check(
			writeVirtualFile(
				globalVirtualPath,
				validGlobalBytes),
			"missing-map fatal fixture restores the valid game.ini") &&
			ok;
	}

	struct AuxiliaryLoadFailureCase
	{
		std::string fileName;
		const char* description;
		const char* corruptBytes;
	};
	const std::string characterSuffix = characterIndex < 0
		? std::string()
		: std::to_string(characterIndex);
	const AuxiliaryLoadFailureCase auxiliaryFailureCases[] =
	{
		{ "traps.ini", "malformed trap definitions", "[Broken\n" },
		{ "trapindexignore.ini", "malformed triggered trap indices", "[Broken\n" },
		{ "variable.ini", "malformed script variables", "[Broken\n" },
		{ "memo.txt", "malformed memo data", "[Broken\n" },
		{ "magic" + characterSuffix + ".ini", "malformed magic data", "[Broken\n" },
		{ "goods" + characterSuffix + ".ini", "malformed goods data", "[Broken\n" },
		{ "partner" + characterSuffix + ".ini", "malformed partner data", "[Broken\n" },
		{ "proj.ini", "malformed effect data", "[Broken\n" },
		{ "proj.ini", "effect data without a Head section", "[Broken]\nX=1\n" }
	};
	for (const AuxiliaryLoadFailureCase& failureCase :
		auxiliaryFailureCases)
	{
		const bool trapFailure =
			failureCase.fileName == "traps.ini" ||
			failureCase.fileName == "trapindexignore.ini";
		const std::string initialTrapTemplate =
			"[template_map]\n1=template_trap.txt\n";
		const std::string virtualPath =
			"save/rpg1/" + failureCase.fileName;
		const bool originalFileExists =
			File::fileExist(virtualPath);
		const std::string originalBytes =
			readVirtualFile(virtualPath);
		const bool corrupted = originalFileExists &&
			writeVirtualFile(
				virtualPath,
				failureCase.corruptBytes) &&
			(!trapFailure ||
				writeVirtualFile(
					"ini/save/traps.ini",
					initialTrapTemplate));
		const std::string fixtureMessage =
			std::string(failureCase.description) +
			" fixture is created from a complete save";
		ok = check(corrupted, fixtureMessage.c_str()) && ok;
		if (!corrupted)
		{
			continue;
		}

		GameManager tolerantLoader;
		tolerantLoader.player->npcName =
			"AuxiliaryStalePlayer";
		tolerantLoader.varList.ensureInitialized();
		tolerantLoader.varList.setInteger(
			"auxiliary_stale_value",
			1);
		tolerantLoader.memo.add("auxiliary stale memo");
		tolerantLoader.traps.beginMapVisit();
		tolerantLoader.traps.markTriggered(7);
		if (tolerantLoader.magicManager.magicList.empty())
		{
			tolerantLoader.magicManager.magicList.resize(1);
		}
		tolerantLoader.magicManager.magicList[0].iniFile =
			"stale-magic.ini";
		if (tolerantLoader.goodsManager.goodsList.empty())
		{
			tolerantLoader.goodsManager.goodsList.resize(1);
		}
		tolerantLoader.goodsManager.goodsList[0].iniFile =
			"stale-goods.ini";
		tolerantLoader.goodsManager.goodsList[0].number = 1;
		const bool loaded = tolerantLoader.loadGame(1);
		const std::string repairedBytes =
			readVirtualFile("save/game/" + failureCase.fileName);
		INIReader repairedTrapDefinitions("save/game/traps.ini");
		const std::string assertionMessage =
			std::string(failureCase.description) +
			(trapFailure
				? " falls back to the initial trap template"
				: " is replaced with a usable empty current state");
		ok = check(
			loaded &&
				tolerantLoader.getLastLoadFailureMessage().empty() &&
				tolerantLoader.player->npcName !=
					"AuxiliaryStalePlayer" &&
				tolerantLoader.varList.getInteger(
					"auxiliary_stale_value") == 0 &&
				tolerantLoader.memo.memo.empty() &&
				!tolerantLoader.traps.hasTriggered(7) &&
				(!trapFailure ||
					tolerantLoader.traps.get(
						"template_map",
						1) == "template_trap.txt") &&
				std::none_of(
					tolerantLoader.magicManager.magicList.cbegin(),
					tolerantLoader.magicManager.magicList.cend(),
					[](const MagicInfo& info)
					{
						return info.iniFile == "stale-magic.ini";
					}) &&
				std::none_of(
					tolerantLoader.goodsManager.goodsList.cbegin(),
					tolerantLoader.goodsManager.goodsList.cend(),
					[](const GoodsInfo& info)
					{
						return info.iniFile == "stale-goods.ini";
					}) &&
				readVirtualFile(virtualPath) ==
					failureCase.corruptBytes &&
				repairedBytes != failureCase.corruptBytes &&
				(!trapFailure ||
					repairedTrapDefinitions.Get(
						"template_map",
						"1",
						"") == "template_trap.txt"),
			assertionMessage.c_str()) && ok;
		const std::string restoreMessage =
			std::string(failureCase.description) +
			" fixture restores the valid source file";
		ok = check(
			writeVirtualFile(virtualPath, originalBytes),
			restoreMessage.c_str()) && ok;
	}

	const std::string retainedSlot =
		"[State]\n"
		"Map=retained.map\n";
	struct MapSaveGuardCase
	{
		const char* mapName;
		const char* description;
	};
	const MapSaveGuardCase mapGuardCases[] =
	{
		{ "", "an empty map name" },
		{ "../outside.map", "an unsafe map path" },
		{ "missing.map", "a missing map resource" }
	};
	for (const MapSaveGuardCase& mapGuardCase : mapGuardCases)
	{
		const bool fixtureReady =
			File::clearDirectoryFiles("save/rpg1") &&
			writeVirtualFile(
				"save/rpg1/game.ini",
				retainedSlot);
		const std::string fixtureMessage =
			std::string(mapGuardCase.description) +
			" save rejection fixture is created";
		ok = check(fixtureReady, fixtureMessage.c_str()) && ok;
		if (!fixtureReady)
		{
			continue;
		}
		GameManager gameManager;
		gameManager.global.data.mapName = mapGuardCase.mapName;
		gameManager.varList.ensureInitialized();
		gameManager.traps.beginMapVisit();
		const std::string rejectionMessage =
			std::string(mapGuardCase.description) +
			" is rejected without replacing the old slot";
		ok = check(
			!gameManager.saveGame(1) &&
				readVirtualFile("save/rpg1/game.ini") == retainedSlot,
			rejectionMessage.c_str()) && ok;
	}

	struct EntityListGuardCase
	{
		int slotIndex;
		const char* npcName;
		const char* objectName;
		const char* description;
	};
	const EntityListGuardCase guardCases[] =
	{
		{
			2,
			"game.ini",
			"ordinary.obj",
			"a reserved entity-list name"
		},
		{
			3,
			"Shared.ini",
			"shared.INI",
			"case-colliding NPC and object list names"
		}
	};
	for (const EntityListGuardCase& guardCase : guardCases)
	{
		const std::string slotDirectory =
			"save/rpg" + std::to_string(guardCase.slotIndex);
		const std::string slotGlobal =
			slotDirectory + "/game.ini";
		const bool fixtureReady =
			File::clearDirectoryFiles(slotDirectory) &&
			writeVirtualFile(slotGlobal, retainedSlot);
		const std::string fixtureMessage =
			std::string(guardCase.description) +
			" rejection fixture is created";
		ok = check(
			fixtureReady,
			fixtureMessage.c_str()) && ok;
		if (!fixtureReady)
		{
			continue;
		}

		GameManager gameManager;
		gameManager.global.data.mapName = mapName;
		gameManager.global.data.npcName = guardCase.npcName;
		gameManager.global.data.objName = guardCase.objectName;
		gameManager.varList.ensureInitialized();
		gameManager.traps.beginMapVisit();
		const std::string rejectionMessage =
			std::string(guardCase.description) +
			" is rejected without replacing the old slot";
		ok = check(
			!gameManager.saveGame(guardCase.slotIndex) &&
			readVirtualFile(slotGlobal) == retainedSlot,
			rejectionMessage.c_str()) && ok;
	}

	const std::string blockedSlotPath = "save/rpg7";
	const std::string blockedSlotMarker = "blocked-slot-destination";
	const bool publicationFailureFixtureReady =
		File::clearDirectoryFiles("save/game") &&
		File::clearDirectoryFiles("save/game_build") &&
		writeVirtualFile(
			"save/game/game.ini",
			"[State]\nMap=old-current.map\n") &&
		writeVirtualFile(
			blockedSlotPath,
			blockedSlotMarker);
	ok = check(
		publicationFailureFixtureReady,
		"current-first publication failure fixture is created") && ok;
	if (publicationFailureFixtureReady)
	{
		GameManager gameManager;
		gameManager.global.data.mapName = mapName;
		gameManager.global.data.npcName.clear();
		gameManager.global.data.objName.clear();
		gameManager.varList.ensureInitialized();
		gameManager.traps.beginMapVisit();
		auto unnamedNpc = std::make_shared<NPC>();
		unnamedNpc->kind = nkNormal;
		gameManager.npcManager->addNPC(unnamedNpc);
		gameManager.objectManager->objectList.push_back(std::make_shared<Object>());
		const bool saved = gameManager.saveGame(7);
		INIReader currentGlobal("save/game/game.ini");
		ok = check(
			!saved &&
				currentGlobal.ParseError() == 0 &&
				currentGlobal.Get("State", "Map", "") == mapName &&
				!gameManager.global.data.npcName.empty() && !gameManager.global.data.objName.empty() &&
				currentGlobal.Get("State", "Npc", "") == gameManager.global.data.npcName &&
				currentGlobal.Get("State", "Obj", "") == gameManager.global.data.objName &&
				readVirtualFile(blockedSlotPath) == blockedSlotMarker,
			"a slot publication failure retains current entity names after publication while preserving the blocked old slot") && ok;
	}
	engine->resetApplicationQuitRequest();
	return ok;
}

bool runMainThreadOwnershipTests()
{
	VirtualGamepadTest::SDLSession sdlSession;
	Engine* engine = Engine::getInstance();
	GameManager gameManager;
	std::atomic<bool> workerRecognizedAsMainThread{true};
	std::atomic<bool> workerMenuInitializationSucceeded{true};
	std::thread worker(
		[&]()
		{
			workerRecognizedAsMainThread.store(
				engine->isMainThread());
			workerMenuInitializationSucceeded.store(
				gameManager.initMenu());
		});
	worker.join();

	bool ok = check(
		engine->isMainThread(),
		"core lifecycle test owner is the SDL main thread");
	ok = check(
		!workerRecognizedAsMainThread.load(),
		"Engine rejects a worker as the SDL main thread") && ok;
	ok = check(
		!workerMenuInitializationSucceeded.load(),
		"menu initialization fails closed on a worker thread") && ok;
	return ok;
}

class CountingElement : public Element
{
public:
	int runCount = 0;
	int updateCount = 0;

protected:
	void onRun() override
	{
		runCount++;
	}

	void onUpdate() override
	{
		updateCount++;
	}
};

class CompositionLayerProbe : public Element
{
public:
	CompositionLayerProbe(
		std::vector<std::string>& drawOrder,
		std::string label,
		bool startsComposition = false) :
		drawOrder(drawOrder),
		label(std::move(label)),
		startsComposition(startsComposition)
	{
	}

	PElement childAfterComposition;

protected:
	bool onBeginDrawComposition() override
	{
		if (startsComposition)
		{
			drawOrder.push_back(label + ".composition-begin");
		}
		return startsComposition;
	}

	bool shouldDrawChildAfterComposition(
		const PElement& child) const override
	{
		return child == childAfterComposition;
	}

	void onEndDrawComposition(bool completed) override
	{
		drawOrder.push_back(label + (completed
			? ".composition-end"
			: ".composition-cancel"));
	}

	void onDraw() override
	{
		drawOrder.push_back(label + ".draw");
	}

	void onDrawEnd() override
	{
		drawOrder.push_back(label + ".draw-end");
	}

private:
	std::vector<std::string>& drawOrder;
	std::string label;
	bool startsComposition = false;
};

class ThrowingRunElement : public Element
{
protected:
	void onRun() override
	{
		throw std::runtime_error(
			"element run exception fixture");
	}
};

class QuitOnUpdateElement : public Element
{
public:
	int updateCount = 0;

protected:
	void onUpdate() override
	{
		updateCount++;
		engine->requestApplicationQuit();
	}
};

class QuitOnDrawElement : public Element
{
public:
	int drawCount = 0;

protected:
	void onDraw() override
	{
		drawCount++;
		engine->requestApplicationQuit();
	}
};

class DragCountingElement : public Element
{
public:
	int dragDrawCount = 0;

protected:
	void onDrawDrag(int, int) override
	{
		dragDrawCount++;
	}
};

class EventCountingElement : public Element
{
public:
	int eventCount = 0;
	bool requestQuit = false;

protected:
	void onEvent() override
	{
		eventCount++;
		if (requestQuit)
		{
			engine->requestApplicationQuit();
		}
	}
};

class WindowCloseElement : public CountingElement
{
public:
	bool handleWindowClose = false;
	int windowCloseCount = 0;

protected:
	bool onHandleEvent(AEvent& event) override
	{
		if (event.eventType != ET_WINDOWCLOSE)
		{
			return false;
		}
		windowCloseCount++;
		return handleWindowClose;
	}
};

class WindowResizeElement : public CountingElement
{
public:
	int resizeCount = 0;
	int lastWidth = 0;
	int lastHeight = 0;
	bool requestQuit = false;

protected:
	void onWindowResize(int width, int height) override
	{
		resizeCount++;
		lastWidth = width;
		lastHeight = height;
		if (requestQuit)
		{
			engine->requestApplicationQuit();
		}
	}
};

class QuitOnMouseOutResizeElement : public WindowResizeElement
{
public:
	int mouseOutCount = 0;

protected:
	void onMouseMoveOut() override
	{
		mouseOutCount++;
		engine->requestApplicationQuit();
	}
};

class ScopedHeadlessFramePump
{
public:
	ScopedHeadlessFramePump()
		: previousBackgroundState(
			CoreLifecycleTestAccess::setHeadlessFramePump(true))
	{
	}

	~ScopedHeadlessFramePump()
	{
		CoreLifecycleTestAccess::setHeadlessFramePump(
			previousBackgroundState);
	}

	ScopedHeadlessFramePump(const ScopedHeadlessFramePump&) = delete;
	ScopedHeadlessFramePump& operator=(const ScopedHeadlessFramePump&) = delete;

private:
	bool previousBackgroundState = false;
};

class ScopedFrameInputHandlers
{
public:
	~ScopedFrameInputHandlers()
	{
		Element::setFrameGameplayInputHandler({});
		Element::setFrameSemanticInputHandler({});
		Element::setFrameInputEventHandler({});
		Element::setFrameGlobalInputHandler({});
	}
};

bool runScriptSleepLifecycleTests()
{
	bool ok = true;
	ScopedFrameInputHandlers handlers;
	GameManager gameManager;
	gameManager.removeAllChild();
	gameManager.addChild(gameManager.weather);
	const auto originalChildren = gameManager.children;
	auto timeStop = std::make_shared<Effect>();
	timeStop->level = 1;
	timeStop->magic.level[1].moveKind = mmkTimeStop;
	timeStop->doing = ekExploding;
	timeStop->user = gameManager.player;
	gameManager.effectManager->effectList.push_back(timeStop);
	ok = check(gameManager.effectManager->hasActiveTimeStopper(),
		"sleep fixture has an active time stopper while weather updates are gated") && ok;
	for (int mode = 0; mode < 4; ++mode)
	{
		Element::resetApplicationQuitState();
		const bool previousInput = mode != 1;
		gameManager.global.data.canInput = previousInput;
		bool observedInputBlock = false;
		bool expired = false;
		bool threw = false;
		const auto start = std::chrono::steady_clock::now();
		const auto startTicks = SDL_GetTicks();
		Element::setFrameGlobalInputHandler([&](Engine*)
		{
			observedInputBlock = !gameManager.global.data.canInput;
			expired = std::chrono::steady_clock::now() - start > std::chrono::seconds(1);
			if (expired || mode == 2) Element::requestApplicationQuit();
			if (mode == 3) throw std::runtime_error("sleep frame exception fixture");
		});
		try
		{
			gameManager.scriptAPI.sleep(20);
		}
		catch (const std::runtime_error&)
		{
			threw = true;
		}
		Element::setFrameGlobalInputHandler({});
		ok = check(!expired && threw == (mode == 3) && observedInputBlock
			&& gameManager.global.data.canInput == previousInput
			&& gameManager.children == originalChildren
			&& gameManager.effectManager->hasActiveTimeStopper(),
			"sleep finishes during time stop and restores input/children on completion, quit, or exception") && ok;
		if (mode < 2)
		{
			ok = check(SDL_GetTicks() - startTicks >= 20,
				"positive sleep does not return before its requested duration") && ok;
			std::cout << "Script sleep checked: input=" << previousInput
				<< " elapsed-ms=" << SDL_GetTicks() - startTicks << " time-stop-active=1\n";
		}
		Element::resetApplicationQuitState();
	}
	return ok;
}

bool runCompositionLayeringTests()
{
	std::vector<std::string> drawOrder;
	auto root = std::make_shared<CompositionLayerProbe>(
		drawOrder, "root", true);
	auto composedChild = std::make_shared<CompositionLayerProbe>(
		drawOrder, "composed-child");
	auto overlayChild = std::make_shared<CompositionLayerProbe>(
		drawOrder, "overlay-child");
	root->addChild(composedChild);
	root->addChild(overlayChild);
	root->childAfterComposition = overlayChild;

	CoreLifecycleTestAccess::draw(*root);
	const std::vector<std::string> expectedOrder =
	{
		"root.composition-begin",
		"root.draw",
		"composed-child.draw",
		"composed-child.draw-end",
		"root.composition-end",
		"overlay-child.draw",
		"overlay-child.draw-end",
		"root.draw-end",
	};
	return check(
		drawOrder == expectedOrder,
		"a deferred modal child draws after its parent's transformed composition");
}

bool runEditorRunWindowClosePolicyTests()
{
	bool ok = true;
	{
		Element sceneRoot;
		ScopedGameInputRegistration editorRunInput(false);
		ok = check(
			CoreLifecycleTestAccess::
				hasWindowCloseConfirmationHandler() &&
				CoreLifecycleTestAccess::
					acceptsWindowCloseWithoutScenePolicy(
						sceneRoot),
			"editor-run input registration accepts window close without the interactive confirmation modal") &&
			ok;
	}
	{
		auto sceneRoot = std::make_shared<WindowCloseElement>();
		sceneRoot->setRunning(true);
		CoreLifecycleTestAccess::setRunningElements({ sceneRoot });
		ScopedGameInputRegistration ordinaryInput;
		ok = check(
			CoreLifecycleTestAccess::
				hasWindowCloseConfirmationHandler() &&
				CoreLifecycleTestAccess::
					acceptsWindowCloseWithoutScenePolicy(
						*sceneRoot),
			"ordinary game input registration accepts window close when the interactive confirmation UI is unavailable") &&
			ok;
		Engine::getInstance()->pushEvent(
			AEvent(ET_WINDOWCLOSE, 0, 0, 0));
		CoreLifecycleTestAccess::handleEvents(*sceneRoot);
		ok = check(
			(sceneRoot->result & erExit) != 0 &&
				!CoreLifecycleTestAccess::logicRunning(
					*sceneRoot),
			"unavailable close-confirmation resources cannot trap the application in the resource-selection phase") &&
			ok;
		CoreLifecycleTestAccess::clearRunningElements();
		Element::resetApplicationQuitState();
	}
	return ok;
}

bool runLoadingInputNeutralityTests()
{
	bool ok = check((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0,
		"loading input test started without SDL video");
	ok = check(
		CoreLifecycleTestAccess::
			loadingPresentationWaitMilliseconds(100, 100) == 16,
		"exclusive loading presentation uses a 16 ms frame interval") && ok;
	ok = check(
		CoreLifecycleTestAccess::
			loadingPresentationWaitMilliseconds(108, 100) == 8 &&
			CoreLifecycleTestAccess::
				loadingPresentationWaitMilliseconds(116, 100) == 0 &&
			CoreLifecycleTestAccess::
				loadingPresentationWaitMilliseconds(132, 100) == 0,
		"exclusive loading presentation waits only for remaining frame time") && ok;
	VirtualGamepadTest::SDLSession sdlSession;
	VirtualGamepadTest::VirtualGamepad gamepad(
		"JXQY Exclusive Loading Input Pad");
	Engine* engine = Engine::getInstance();
	auto& inputManager =
		const_cast<GameInput::PhysicalInputManager&>(engine->inputActions());
	HeadlessPhysicalInputTest::ScopedPhysicalInputManager inputScope(
		inputManager);
	if (!check(inputScope.isInitialized(),
		"loading input test initialized the physical input manager"))
	{
		return false;
	}
	VirtualGamepadTest::runFrame(inputManager, SDL_GetTicks());

	GameManager gameManager;
	const bool pointerStarted =
		CoreLifecycleTestAccess::beginPointerInteraction(
			gameManager, TOUCH_MOUSEID, 32, 32);
	ok = check(pointerStarted
			&& gameManager.hasPointerDownInTree(TOUCH_MOUSEID),
		"loading input fixture established a production-tree pointer transaction")
		&& ok;
	CoreLifecycleTestAccess::resetExclusiveLoadingInputState(gameManager);
	ok = check(!gameManager.hasPointerDownInTree(TOUCH_MOUSEID),
		"exclusive loading caller entry canceled the pre-existing pointer transaction")
		&& ok;

	int globalDispatchCount = 0;
	int ordinaryEventDispatchCount = 0;
	int semanticDispatchCount = 0;
	int gameplayDispatchCount = 0;
	std::atomic<bool> finishWorker{false};
	std::atomic<bool> workerExited{false};
	const std::thread::id ownerThreadId =
		std::this_thread::get_id();
	bool successFinalizerRan = false;
	bool successFinalizerRanOnOwnerThread = false;
	bool successFinalizerObservedWorkerExit = false;
	bool loadingFrameObservedOrdinaryActions = false;
	bool loadingFrameObservedGlobalToggle = false;
	bool loadingFrameConsumedGlobalToggle = false;
	auto resizeRoot =
		std::make_shared<WindowResizeElement>();
	CoreLifecycleTestAccess::setRunningElements(
		{ resizeRoot });
	ScopedFrameInputHandlers inputHandlers;
	Element::setFrameInputEventHandler(
		[&ordinaryEventDispatchCount](const AEvent& event, Engine*)
		{
			if (event.eventType == ET_KEYDOWN
				|| event.eventType == ET_MOUSEDOWN)
			{
				ordinaryEventDispatchCount++;
			}
		});
	Element::setFrameSemanticInputHandler(
		[&semanticDispatchCount](Engine*)
		{
			semanticDispatchCount++;
			return false;
		});
	Element::setFrameGameplayInputHandler(
		[&gameplayDispatchCount](Engine*)
		{
			gameplayDispatchCount++;
		});
	Element::setFrameGlobalInputHandler(
		[&](Engine* frameEngine)
		{
			globalDispatchCount++;
			if (globalDispatchCount == 1)
			{
				gamepad.setAxis(SDL_GAMEPAD_AXIS_LEFTX, 24000);
				gamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, true);

				SDL_Event ordinaryKey = {};
				ordinaryKey.type = SDL_EVENT_KEY_DOWN;
				ordinaryKey.key.scancode = SDL_SCANCODE_F;
				SDL_PushEvent(&ordinaryKey);

				SDL_Event ordinaryPointer = {};
				ordinaryPointer.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
				ordinaryPointer.button.button = SDL_BUTTON_LEFT;
				ordinaryPointer.button.x = 32.0f;
				ordinaryPointer.button.y = 32.0f;
				SDL_PushEvent(&ordinaryPointer);

				SDL_Event globalToggle = {};
				globalToggle.type = SDL_EVENT_KEY_DOWN;
				globalToggle.key.scancode = SDL_SCANCODE_H;
				globalToggle.key.mod = SDL_KMOD_CTRL | SDL_KMOD_SHIFT;
				SDL_PushEvent(&globalToggle);
				frameEngine->pushEvent(
					AEvent(
						ET_WINDOWRESIZE,
						0,
						960,
						540));
				return;
			}
			if (globalDispatchCount == 2)
			{
				loadingFrameObservedOrdinaryActions =
					inputManager.isActionDown(GameInput::InputAction::Move)
					&& inputManager.wasActionPressed(
						GameInput::InputAction::Confirm);
#ifdef __MOBILE__
				loadingFrameObservedGlobalToggle =
					inputManager.wasActionPressed(
						GameInput::InputAction::ToggleTouchControls);
				loadingFrameConsumedGlobalToggle =
					frameEngine->consumeInputAction(
						GameInput::InputAction::ToggleTouchControls);
#else
				loadingFrameObservedGlobalToggle =
					!inputManager.wasActionPressed(
						GameInput::InputAction::ToggleTouchControls);
				loadingFrameConsumedGlobalToggle =
					!frameEngine->consumeInputAction(
						GameInput::InputAction::ToggleTouchControls);
#endif
				finishWorker.store(true);
			}
		});

	{
		ScopedHeadlessFramePump headlessFramePump;
		const GameLoading::LoadingTaskResult loadingResult =
			CoreLifecycleTestAccess::runExclusiveLoadingTask(
				gameManager,
				[&finishWorker, &workerExited](
					const GameLoading::LoadingCancellationToken&
						cancellationToken)
				{
					thread_local LoadingWorkerExitSignal exitSignal(
						workerExited);
					const auto deadline =
						std::chrono::steady_clock::now() +
						ExclusiveLoadingWorkerTimeout;
					while (!finishWorker.load())
					{
						if (cancellationToken.isCancellationRequested())
						{
							return GameLoading::LoadingTaskResult::
								cancellation();
						}
						if (std::chrono::steady_clock::now() >=
							deadline)
						{
							return GameLoading::LoadingTaskResult::
								failure(
									"loading input worker timed out");
						}
						std::this_thread::yield();
					}
					return GameLoading::LoadingTaskResult::success();
				},
				[&](const std::function<bool()>&)
				{
					successFinalizerRan = true;
					successFinalizerRanOnOwnerThread =
						engine->isMainThread() &&
						std::this_thread::get_id() ==
							ownerThreadId;
					successFinalizerObservedWorkerExit =
						workerExited.load();
					return GameLoading::LoadingTaskResult::success();
				});

		ok = check(loadingResult.succeeded(),
			"exclusive loading loop returned its worker result") && ok;
		ok = check(
			successFinalizerRan &&
				successFinalizerRanOnOwnerThread &&
				successFinalizerObservedWorkerExit,
			"exclusive loading finalizer runs on the owner thread after worker exit")
			&& ok;
		ok = check(globalDispatchCount >= 2
				&& loadingFrameObservedGlobalToggle
				&& loadingFrameConsumedGlobalToggle,
			"exclusive loading loop applied the platform-specific global toggle policy")
			&& ok;
		ok = check(loadingFrameObservedOrdinaryActions
				&& ordinaryEventDispatchCount == 0
				&& semanticDispatchCount == 0
				&& gameplayDispatchCount == 0,
			"exclusive loading loop pumped ordinary input without dispatching"
			" pointer, keyboard, semantic, or gameplay handlers") && ok;
		ok = check(
			resizeRoot->resizeCount == 1 &&
				resizeRoot->lastWidth == 960 &&
				resizeRoot->lastHeight == 540,
			"exclusive loading dispatches window resize while ordinary input remains isolated") &&
			ok;
		ok = check(!inputManager.isActionDown(GameInput::InputAction::Move)
				&& !inputManager.wasActionPressed(
					GameInput::InputAction::Confirm),
			"exclusive loading caller exit released actions pressed during loading")
			&& ok;

		gamepad.setAxis(SDL_GAMEPAD_AXIS_LEFTX, 0);
		gamepad.setButton(SDL_GAMEPAD_BUTTON_SOUTH, false);
		engine->frameBegin();
		CoreLifecycleTestAccess::handleEvents(gameManager);
		ok = check(ordinaryEventDispatchCount == 0,
			"the first post-loading frame did not replay stale raw input") && ok;
	}
	CoreLifecycleTestAccess::clearRunningElements();

	int closePumpCount = 0;
	int closeConfirmationCount = 0;
	bool closeHandledAfterCleanup = false;
	std::atomic<bool> finishCloseWorker{false};
	Element::setWindowCloseConfirmationHandler(
		[&](Element&)
		{
			closeConfirmationCount++;
			closeHandledAfterCleanup =
				!gameManager.inThread.load() &&
				!engine->isMultiThreadedMode();
			return false;
		});
	Element::setFrameGlobalInputHandler(
		[&](Engine*)
		{
			closePumpCount++;
			if (closePumpCount == 1)
			{
				SDL_Event closeEvent = {};
				closeEvent.type =
					SDL_EVENT_WINDOW_CLOSE_REQUESTED;
				SDL_PushEvent(&closeEvent);
			}
			else if (closePumpCount == 2)
			{
				finishCloseWorker.store(true);
			}
		});
	{
		ScopedHeadlessFramePump headlessFramePump;
		const GameLoading::LoadingTaskResult closeResult =
			CoreLifecycleTestAccess::runExclusiveLoadingTask(
				gameManager,
				[&finishCloseWorker](
					const GameLoading::LoadingCancellationToken&
						cancellationToken)
				{
					const auto deadline =
						std::chrono::steady_clock::now() +
						ExclusiveLoadingWorkerTimeout;
					while (!finishCloseWorker.load())
					{
						if (cancellationToken.isCancellationRequested())
						{
							return GameLoading::LoadingTaskResult::
								cancellation();
						}
						if (std::chrono::steady_clock::now() >=
							deadline)
						{
							return GameLoading::LoadingTaskResult::
								failure(
									"window close worker timed out");
						}
						std::this_thread::yield();
					}
					return GameLoading::LoadingTaskResult::success();
				});
		ok = check(closeResult.succeeded(),
			"exclusive loading completed before dispatching a close request")
			&& ok;
	}
	ok = check(closeConfirmationCount == 1 &&
			closeHandledAfterCleanup,
		"exclusive loading latched close once and dispatched it after join and cleanup")
		&& ok;
	Element::setWindowCloseConfirmationHandler({});

	int terminalPumpCount = 0;
	int terminalSuccessFinalizerCount = 0;
	std::atomic<bool> finishTerminalWorker{false};
	std::atomic<bool> terminalCancellationObserved{false};
	const int terminalResizeCountBeforeQuit =
		resizeRoot->resizeCount;
	CoreLifecycleTestAccess::setRunningElements(
		{ resizeRoot });
	engine->resetApplicationQuitRequest();
	Element::resetApplicationQuitState();
	Element::setFrameGlobalInputHandler(
		[&](Engine* frameEngine)
		{
			terminalPumpCount++;
			if (terminalPumpCount == 1)
			{
				frameEngine->pushEvent(
					AEvent(
						ET_WINDOWRESIZE,
						1280,
						720,
						0));
				frameEngine->requestApplicationQuit();
			}
			else if (terminalPumpCount == 2)
			{
				finishTerminalWorker.store(true);
			}
		});
	{
		ScopedHeadlessFramePump headlessFramePump;
		const GameLoading::LoadingTaskResult terminalResult =
			CoreLifecycleTestAccess::runExclusiveLoadingTask(
				gameManager,
				[&finishTerminalWorker,
				 &terminalCancellationObserved](
					const GameLoading::LoadingCancellationToken&
						cancellationToken)
				{
					const auto deadline =
						std::chrono::steady_clock::now() +
						ExclusiveLoadingWorkerTimeout;
					while (!finishTerminalWorker.load())
					{
						if (cancellationToken.isCancellationRequested())
						{
							terminalCancellationObserved.store(true);
						}
						if (std::chrono::steady_clock::now() >=
							deadline)
						{
							return GameLoading::LoadingTaskResult::
								failure(
									"terminal quit worker timed out");
						}
						std::this_thread::yield();
					}
					if (cancellationToken.isCancellationRequested())
					{
						terminalCancellationObserved.store(true);
					}
					return GameLoading::LoadingTaskResult::success();
				},
				[&terminalSuccessFinalizerCount](
					const std::function<bool()>&)
				{
					terminalSuccessFinalizerCount++;
					return GameLoading::LoadingTaskResult::success();
				});
		ok = check(
			terminalResult.status ==
				GameLoading::LoadingTaskStatus::Cancelled &&
				terminalSuccessFinalizerCount == 0 &&
				terminalCancellationObserved.load(),
			"terminal quit converts an uncommitted worker success to Cancelled"
			", requests cancellation, and suppresses the success finalizer")
			&& ok;
	}
	ok = check(
		!gameManager.inThread.load() &&
			!engine->isMultiThreadedMode(),
		"terminal quit restores exclusive loading state after joining the worker")
		&& ok;
	ok = check(
		resizeRoot->resizeCount ==
			terminalResizeCountBeforeQuit,
		"terminal quit suppresses a deferred resize that could rebuild renderer-backed UI") &&
		ok;
	CoreLifecycleTestAccess::clearRunningElements();
	engine->resetApplicationQuitRequest();
	Element::resetApplicationQuitState();
	Element::setFrameGlobalInputHandler({});

	int finalizerCheckpointCount = 0;
	GameLoading::LoadingTaskResult
		finalizerCheckpointCancellation;
	{
		ScopedHeadlessFramePump headlessFramePump;
		finalizerCheckpointCancellation =
			CoreLifecycleTestAccess::runExclusiveLoadingTask(
				gameManager,
				[](
					const GameLoading::LoadingCancellationToken&)
				{
					return GameLoading::LoadingTaskResult::success();
				},
				[&](
					const std::function<bool()>& ownerCheckpoint)
				{
					engine->requestApplicationQuit();
					finalizerCheckpointCount++;
					return ownerCheckpoint()
						? GameLoading::LoadingTaskResult::success()
						: GameLoading::LoadingTaskResult::
							cancellation();
				});
	}
	ok = check(
		finalizerCheckpointCancellation.status ==
			GameLoading::LoadingTaskStatus::Cancelled &&
			finalizerCheckpointCount == 1 &&
			!gameManager.inThread.load() &&
			!engine->isMultiThreadedMode(),
		"owner-thread finalization checkpoint observes terminal quit and restores loading state") &&
		ok;
	engine->resetApplicationQuitRequest();
	Element::resetApplicationQuitState();

	int loadingPresentationPumpCount = 0;
	int presentationCountAtFinalizerEntry = 0;
	int presentationCountAfterFinalizerCheckpoint = 0;
	GameLoading::LoadingTaskResult
		finalizerPresentationResult;
	{
		ScopedHeadlessFramePump headlessFramePump;
		finalizerPresentationResult =
			CoreLifecycleTestAccess::runExclusiveLoadingTask(
				gameManager,
				[](
					const GameLoading::LoadingCancellationToken&)
				{
					return GameLoading::LoadingTaskResult::success();
				},
				[&](
					const std::function<bool()>& ownerCheckpoint)
				{
					presentationCountAtFinalizerEntry =
						loadingPresentationPumpCount;
					engine->delay(20);
					const bool canContinue = ownerCheckpoint();
					presentationCountAfterFinalizerCheckpoint =
						loadingPresentationPumpCount;
					return canContinue
						? GameLoading::LoadingTaskResult::success()
						: GameLoading::LoadingTaskResult::cancellation();
				},
				[&loadingPresentationPumpCount]()
				{
					++loadingPresentationPumpCount;
				});
	}
	ok = check(
		finalizerPresentationResult.succeeded() &&
			presentationCountAtFinalizerEntry > 0 &&
			presentationCountAfterFinalizerCheckpoint >
				presentationCountAtFinalizerEntry,
		"loading presentation pumping continues inside owner-thread finalization after the worker has completed") &&
		ok;

	int postFinalizerCloseCount = 0;
	bool postFinalizerCloseHandledAfterCleanup = false;
	Element::setWindowCloseConfirmationHandler(
		[&](Element&)
		{
			postFinalizerCloseCount++;
			postFinalizerCloseHandledAfterCleanup =
				!gameManager.inThread.load() &&
				!engine->isMultiThreadedMode();
			return false;
		});
	GameLoading::LoadingTaskResult postFinalizerCloseResult;
	{
		ScopedHeadlessFramePump headlessFramePump;
		postFinalizerCloseResult =
			CoreLifecycleTestAccess::runExclusiveLoadingTask(
				gameManager,
				[](
					const GameLoading::LoadingCancellationToken&)
				{
					return GameLoading::LoadingTaskResult::success();
				},
				[](const std::function<bool()>&)
				{
					SDL_Event closeEvent = {};
					closeEvent.type =
						SDL_EVENT_WINDOW_CLOSE_REQUESTED;
					SDL_PushEvent(&closeEvent);
					return GameLoading::LoadingTaskResult::success();
				});
	}
	ok = check(
		postFinalizerCloseResult.succeeded() &&
			postFinalizerCloseCount == 1 &&
			postFinalizerCloseHandledAfterCleanup,
		"the post-finalizer checkpoint preserves a completed result and"
		" dispatches a late close request after loading cleanup") && ok;
	Element::setWindowCloseConfirmationHandler({});

	Element::setWindowCloseConfirmationHandler(
		[](Element&) -> bool
		{
			throw std::runtime_error(
				"deferred close handler exception fixture");
		});
	GameLoading::LoadingTaskResult deferredCloseExceptionResult;
	{
		ScopedHeadlessFramePump headlessFramePump;
		deferredCloseExceptionResult =
			CoreLifecycleTestAccess::runExclusiveLoadingTask(
				gameManager,
				[](
					const GameLoading::LoadingCancellationToken&)
				{
					return GameLoading::LoadingTaskResult::success();
				},
				[](const std::function<bool()>&)
				{
					SDL_Event closeEvent = {};
					closeEvent.type =
						SDL_EVENT_WINDOW_CLOSE_REQUESTED;
					SDL_PushEvent(&closeEvent);
					return GameLoading::LoadingTaskResult::success();
				});
	}
	ok = check(
		deferredCloseExceptionResult.succeeded() &&
			!gameManager.inThread.load() &&
			!engine->isMultiThreadedMode(),
		"a deferred close handler exception is contained without"
		" misreporting an already committed load or leaving loading state active") && ok;
	Element::setWindowCloseConfirmationHandler({});

	GameLoading::LoadingTaskResult workerExceptionResult;
	{
		ScopedHeadlessFramePump headlessFramePump;
		workerExceptionResult =
			CoreLifecycleTestAccess::runExclusiveLoadingTask(
				gameManager,
				[](
					const GameLoading::LoadingCancellationToken&)
					-> GameLoading::LoadingTaskResult
				{
					throw std::runtime_error(
						"loading worker exception fixture");
				});
	}
	ok = check(
		workerExceptionResult.status ==
			GameLoading::LoadingTaskStatus::Failed &&
			static_cast<bool>(workerExceptionResult.exception) &&
			!gameManager.inThread.load() &&
			!engine->isMultiThreadedMode(),
		"worker exception becomes a failed result and restores loading state")
		&& ok;

	GameLoading::LoadingTaskResult finalizerExceptionResult;
	{
		ScopedHeadlessFramePump headlessFramePump;
		finalizerExceptionResult =
			CoreLifecycleTestAccess::runExclusiveLoadingTask(
				gameManager,
				[](
					const GameLoading::LoadingCancellationToken&)
				{
					return GameLoading::LoadingTaskResult::success();
				},
				[](const std::function<bool()>&)
					-> GameLoading::LoadingTaskResult
				{
					throw std::runtime_error(
						"loading finalizer exception fixture");
				});
	}
	ok = check(
		finalizerExceptionResult.status ==
			GameLoading::LoadingTaskStatus::Failed &&
			static_cast<bool>(finalizerExceptionResult.exception) &&
			!gameManager.inThread.load() &&
			!engine->isMultiThreadedMode(),
		"finalizer exception becomes a failed result and restores loading state")
		&& ok;
	ok = check((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0,
		"loading input test did not initialize SDL video") && ok;
	return ok;
}

bool runQuitLatchTests()
{
	bool ok = true;
	Element::resetApplicationQuitState();
	Element::setWindowCloseConfirmationHandler({});

	auto outer = std::make_shared<CountingElement>();
	auto modal = std::make_shared<CountingElement>();
	outer->setRunning(true);
	modal->setRunning(true);
	CoreLifecycleTestAccess::setRunningElements({ outer, modal });

	AEvent quitEvent(ET_QUIT, 0, 0, 0);
	Engine::getInstance()->pushEvent(quitEvent);
	CoreLifecycleTestAccess::handleEvents(*modal);
	ok = check((outer->result & erExit) != 0 && (modal->result & erExit) != 0,
		"central ET_QUIT marks every nested running element for application exit") && ok;
	ok = check(!CoreLifecycleTestAccess::logicRunning(*outer)
		&& !CoreLifecycleTestAccess::logicRunning(*modal),
		"central ET_QUIT terminates every nested running element") && ok;

	CoreLifecycleTestAccess::clearRunningElements();
	auto lateModal = std::make_shared<CountingElement>();
	unsigned int lateResult = lateModal->run();
	ok = check((lateResult & erExit) != 0 && lateModal->runCount == 0,
		"latched element quit prevents a later modal from starting") && ok;
	Element::resetApplicationQuitState();

	ok = check(CoreLifecycleTestAccess::sendEngineEvent(SDL_EVENT_QUIT) == 1,
		"engine leaves an SDL quit request for scene-level close handling") && ok;
	ok = check(CoreLifecycleTestAccess::sendEngineEvent(SDL_EVENT_WINDOW_CLOSE_REQUESTED) == 1,
		"engine leaves a window close request for scene-level close handling") && ok;
	ok = check(!Engine::getInstance()->isApplicationQuitRequested(),
		"user close requests do not latch terminal application quit") && ok;

	auto closeRoot = std::make_shared<WindowCloseElement>();
	auto closeModal = std::make_shared<WindowCloseElement>();
	closeRoot->handleWindowClose = true;
	closeRoot->addChild(closeModal);
	closeRoot->setRunning(true);
	closeModal->setRunning(true);
	CoreLifecycleTestAccess::setRunningElements({ closeRoot, closeModal });
	Engine::getInstance()->pushEvent(AEvent(ET_WINDOWCLOSE, 0, 0, 0));
	CoreLifecycleTestAccess::handleEvents(*closeModal);
	ok = check(closeRoot->windowCloseCount == 1 && closeModal->windowCloseCount == 0,
		"window close bypasses a nested modal and reaches the scene root") && ok;
	ok = check(!Engine::getInstance()->isApplicationQuitRequested(),
		"a handled window close request does not terminate the application") && ok;

	closeRoot->windowCloseCount = 0;
	closeModal->windowCloseCount = 0;
	int confirmationRequestCount = 0;
	Element* confirmationRoot = nullptr;
	Element::setWindowCloseConfirmationHandler(
		[&confirmationRequestCount, &confirmationRoot](Element& sceneRoot)
		{
			confirmationRequestCount++;
			confirmationRoot = &sceneRoot;
			return false;
		});
	Engine::getInstance()->pushEvent(AEvent(ET_WINDOWCLOSE, 0, 0, 0));
	CoreLifecycleTestAccess::handleEvents(*closeModal);
	ok = check(confirmationRequestCount == 1 && confirmationRoot == closeRoot.get(),
		"window close reaches one global confirmation handler from a nested modal") && ok;
	ok = check(closeRoot->windowCloseCount == 0 && closeModal->windowCloseCount == 0,
		"global confirmation bypasses scene and modal close handlers") && ok;
	ok = check(CoreLifecycleTestAccess::logicRunning(*closeRoot)
		&& CoreLifecycleTestAccess::logicRunning(*closeModal),
		"cancelled global close confirmation preserves every running layer") && ok;

	Element::setWindowCloseConfirmationHandler(
		[&confirmationRequestCount](Element&)
		{
			confirmationRequestCount++;
			return true;
		});
	Engine::getInstance()->pushEvent(AEvent(ET_WINDOWCLOSE, 0, 0, 0));
	CoreLifecycleTestAccess::handleEvents(*closeModal);
	ok = check((closeRoot->result & erExit) != 0
		&& (closeModal->result & erExit) != 0,
		"confirmed global close latches application exit across running layers") && ok;
	ok = check(!CoreLifecycleTestAccess::logicRunning(*closeRoot)
		&& !CoreLifecycleTestAccess::logicRunning(*closeModal),
		"confirmed global close terminates every running layer") && ok;
	ok = check(Engine::getInstance()->isApplicationQuitRequested()
		&& !CoreLifecycleTestAccess::canPrepareRenderFrame(),
		"confirmed global close synchronizes the terminal engine latch and closes the render gate") && ok;
	Element::setWindowCloseConfirmationHandler({});
	CoreLifecycleTestAccess::clearRunningElements();
	Element::resetApplicationQuitState();

	ok = check(CoreLifecycleTestAccess::sendEngineEvent(SDL_EVENT_TERMINATING) == 0,
		"engine accepts SDL termination as a terminal application event") && ok;
	ok = check(Engine::getInstance()->isApplicationQuitRequested(),
		"engine latches SDL termination independently of the transient event queue") && ok;
	auto engineLatchedModal = std::make_shared<CountingElement>();
	unsigned int engineLatchedResult = engineLatchedModal->run();
	ok = check((engineLatchedResult & erExit) != 0 && engineLatchedModal->runCount == 0,
		"engine termination latch prevents a later modal from starting") && ok;
	Element::resetApplicationQuitState();
	CoreLifecycleTestAccess::sendEngineEvent(
		SDL_EVENT_DID_ENTER_FOREGROUND);
	Engine::getInstance()->pumpEvents();
	return ok;
}

bool runElementRunExceptionCleanupTests()
{
	bool ok = true;
	Element::resetApplicationQuitState();
	Element::setInputContextTransitionHandler({});

	auto outer = std::make_shared<CountingElement>();
	auto throwingModal = std::make_shared<ThrowingRunElement>();
	outer->setRunning(true);
	CoreLifecycleTestAccess::setRunningElements({ outer });

	int inputContextTransitionCount = 0;
	Element::setInputContextTransitionHandler(
		[&inputContextTransitionCount]()
		{
			inputContextTransitionCount++;
		});

	bool caughtExpectedException = false;
	try
	{
		(void)throwingModal->run();
	}
	catch (const std::runtime_error&)
	{
		caughtExpectedException = true;
	}
	catch (...)
	{
	}

	ok = check(caughtExpectedException,
		"Element::run propagates an exception raised by the running element") && ok;
	ok = check(
		CoreLifecycleTestAccess::runningElementCount() == 1 &&
			Element::isCurrentRunOwner(outer.get()),
		"an exceptional nested run removes only its own running-stack entry") && ok;
	ok = check(!CoreLifecycleTestAccess::logicRunning(*throwingModal),
		"an exceptional nested run clears its logic-running state") && ok;
	ok = check(inputContextTransitionCount == 2,
		"an exceptional nested run performs both entry and exit input-context transitions") && ok;

	Element::setInputContextTransitionHandler({});
	outer->setRunning(false);
	CoreLifecycleTestAccess::clearRunningElements();
	Element::resetApplicationQuitState();

	auto eventRoot =
		std::make_shared<EventCountingElement>();
	auto quitEventChild =
		std::make_shared<EventCountingElement>();
	auto laterEventChild =
		std::make_shared<EventCountingElement>();
	quitEventChild->requestQuit = true;
	eventRoot->addChild(quitEventChild);
	eventRoot->addChild(laterEventChild);
	eventRoot->setRunning(true);
	CoreLifecycleTestAccess::setRunningElements(
		{ eventRoot });
	CoreLifecycleTestAccess::handleEvents(
		*eventRoot);
	ok = check(
		quitEventChild->eventCount == 1 &&
			laterEventChild->eventCount == 0 &&
			eventRoot->eventCount == 0 &&
			!CoreLifecycleTestAccess::logicRunning(
				*eventRoot),
		"a terminal request from a child event stops remaining siblings and the parent event in the same dispatch") &&
		ok;

	CoreLifecycleTestAccess::clearRunningElements();
	Element::resetApplicationQuitState();

	return ok;
}

bool runMidFrameTerminalQuitTests()
{
	bool ok = true;
	Engine* engine = Engine::getInstance();
	ScopedFrameInputHandlers inputHandlers;

	Element::resetApplicationQuitState();
	auto gameplayRoot = std::make_shared<CountingElement>();
	gameplayRoot->setRunning(true);
	CoreLifecycleTestAccess::setRunningElements(
		{ gameplayRoot });
	Element::setFrameGameplayInputHandler(
		[](Engine* frameEngine)
		{
			frameEngine->requestApplicationQuit();
		});
	CoreLifecycleTestAccess::frame(*gameplayRoot);
	ok = check(
		gameplayRoot->updateCount == 0 &&
			!CoreLifecycleTestAccess::logicRunning(
				*gameplayRoot),
		"an Engine-only terminal request from gameplay input stops the current frame before world update") &&
		ok;

	Element::setFrameGameplayInputHandler({});
	CoreLifecycleTestAccess::clearRunningElements();
	Element::resetApplicationQuitState();

	auto backgroundRoot =
		std::make_shared<CountingElement>();
	backgroundRoot->setRunning(true);
	CoreLifecycleTestAccess::setRunningElements(
		{ backgroundRoot });
	Element::setFrameGameplayInputHandler(
		[](Engine*)
		{
			CoreLifecycleTestAccess::sendEngineEvent(
				SDL_EVENT_WILL_ENTER_BACKGROUND);
		});
	CoreLifecycleTestAccess::frame(*backgroundRoot);
	ok = check(
		backgroundRoot->updateCount == 0 &&
			CoreLifecycleTestAccess::logicRunning(
				*backgroundRoot) &&
			!engine->isApplicationActive() &&
			!engine->isFrameReady(),
		"an asynchronous background request closes frame admission before world update without terminating the scene") &&
		ok;
	Element::setFrameGameplayInputHandler({});
	CoreLifecycleTestAccess::sendEngineEvent(
		SDL_EVENT_DID_ENTER_FOREGROUND);
	CoreLifecycleTestAccess::frame(*backgroundRoot);
	ok = check(
		engine->isApplicationActive() &&
			backgroundRoot->updateCount == 1 &&
			CoreLifecycleTestAccess::logicRunning(
				*backgroundRoot),
		"the owner frame applies foreground recovery before resuming callbacks") &&
		ok;
	CoreLifecycleTestAccess::clearRunningElements();
	Element::resetApplicationQuitState();

	auto updateRoot = std::make_shared<CountingElement>();
	auto quitChild =
		std::make_shared<QuitOnUpdateElement>();
	auto laterChild =
		std::make_shared<CountingElement>();
	updateRoot->addChild(quitChild);
	updateRoot->addChild(laterChild);
	updateRoot->setRunning(true);
	CoreLifecycleTestAccess::setRunningElements(
		{ updateRoot });
	CoreLifecycleTestAccess::frame(*updateRoot);
	ok = check(
		quitChild->updateCount == 1 &&
			laterChild->updateCount == 0 &&
			updateRoot->updateCount == 0 &&
			!CoreLifecycleTestAccess::logicRunning(
				*updateRoot),
		"a terminal request from a child update stops remaining siblings, parent update, and drawing in the same frame") &&
		ok;

	CoreLifecycleTestAccess::clearRunningElements();
	Element::resetApplicationQuitState();

	auto drawRoot = std::make_shared<QuitOnDrawElement>();
	auto dragItem = std::make_shared<DragCountingElement>();
	drawRoot->setRunning(true);
	CoreLifecycleTestAccess::setRunningElements(
		{ drawRoot });
	CoreLifecycleTestAccess::beginSyntheticDrag(
		dragItem);
	CoreLifecycleTestAccess::draw(*drawRoot);
	ok = check(
		drawRoot->drawCount == 1 &&
			dragItem->dragDrawCount == 0 &&
			!CoreLifecycleTestAccess::logicRunning(
				*drawRoot),
		"a terminal request from drawing stops the drag overlay in the same frame") &&
		ok;
	CoreLifecycleTestAccess::endSyntheticDrag();
	CoreLifecycleTestAccess::clearRunningElements();
	Element::resetApplicationQuitState();

	auto resizeRoot =
		std::make_shared<WindowResizeElement>();
	auto quitResizeChild =
		std::make_shared<WindowResizeElement>();
	auto laterResizeChild =
		std::make_shared<WindowResizeElement>();
	auto laterResizeRoot =
		std::make_shared<WindowResizeElement>();
	quitResizeChild->requestQuit = true;
	resizeRoot->addChild(quitResizeChild);
	resizeRoot->addChild(laterResizeChild);
	resizeRoot->setRunning(true);
	laterResizeRoot->setRunning(true);
	CoreLifecycleTestAccess::setRunningElements(
		{ resizeRoot, laterResizeRoot });
	const bool resizeHandled =
		Element::resizeRunningRoots(1024, 768);
	ok = check(
		resizeHandled &&
			quitResizeChild->resizeCount == 1 &&
			laterResizeChild->resizeCount == 0 &&
			resizeRoot->resizeCount == 0 &&
			laterResizeRoot->resizeCount == 0,
		"a terminal request from resize stops remaining siblings, parents, and running roots") &&
		ok;
	CoreLifecycleTestAccess::clearRunningElements();
	Element::resetApplicationQuitState();

	auto mouseOutResizeRoot =
		std::make_shared<WindowResizeElement>();
	auto mouseOutResizeChild =
		std::make_shared<QuitOnMouseOutResizeElement>();
	auto laterMouseOutResizeChild =
		std::make_shared<WindowResizeElement>();
	mouseOutResizeChild->touchingID = TOUCH_MOUSEID;
	mouseOutResizeRoot->addChild(mouseOutResizeChild);
	mouseOutResizeRoot->addChild(
		laterMouseOutResizeChild);
	mouseOutResizeRoot->setRunning(true);
	CoreLifecycleTestAccess::setRunningElements(
		{ mouseOutResizeRoot });
	(void)Element::resizeRunningRoots(1280, 720);
	ok = check(
		mouseOutResizeChild->mouseOutCount == 1 &&
			mouseOutResizeChild->resizeCount == 0 &&
			laterMouseOutResizeChild->resizeCount == 0 &&
			mouseOutResizeRoot->resizeCount == 0,
		"a terminal request from resize pointer cleanup stops resize callbacks and later nodes") &&
		ok;
	CoreLifecycleTestAccess::clearRunningElements();
	Element::resetApplicationQuitState();
	return ok;
}

bool runApplicationInactiveTests()
{
	bool ok = true;
	Element::resetApplicationQuitState();
	auto root = std::make_shared<CountingElement>();
	auto alreadyPausedRoot = std::make_shared<CountingElement>();
	root->setRunning(true);
	alreadyPausedRoot->setRunning(true);
	alreadyPausedRoot->setPaused(true);
	CoreLifecycleTestAccess::setRunningElements({ root, alreadyPausedRoot });

	ok = check(CoreLifecycleTestAccess::sendEngineEvent(SDL_EVENT_WINDOW_FOCUS_LOST) == 1,
		"desktop focus loss remains available to the regular event path") && ok;
	ok = check(Engine::getInstance()->isApplicationActive(),
		"desktop focus loss does not suspend the application") && ok;
	CoreLifecycleTestAccess::frame(*root);
	ok = check(root->updateCount == 1,
		"an unfocused desktop window continues updating game elements") && ok;
	ok = check(!CoreLifecycleTestAccess::timerPaused(*root),
		"an unfocused desktop window keeps running element timers active") && ok;
	ok = check(!CoreLifecycleTestAccess::applicationMediaPaused(),
		"an unfocused desktop window keeps application media playing") && ok;

	ok = check(CoreLifecycleTestAccess::sendEngineEvent(SDL_EVENT_WINDOW_FOCUS_GAINED) == 1,
		"desktop focus gain remains available to the regular event path") && ok;
	ok = check(Engine::getInstance()->isApplicationActive(),
		"desktop focus gain leaves the application active") && ok;

	CoreLifecycleTestAccess::sendEngineEvent(SDL_EVENT_DID_ENTER_BACKGROUND);
	ok = check(!Engine::getInstance()->isApplicationActive(),
		"lifecycle backgrounding suspends the application") && ok;
	CoreLifecycleTestAccess::frame(*root);
	ok = check(root->updateCount == 1,
		"a lifecycle-backgrounded frame does not update game elements") && ok;
	ok = check(CoreLifecycleTestAccess::timerPaused(*root),
		"lifecycle backgrounding pauses running element timers") && ok;
	ok = check(CoreLifecycleTestAccess::applicationMediaPaused(),
		"lifecycle backgrounding pauses application media") && ok;

	CoreLifecycleTestAccess::sendEngineEvent(SDL_EVENT_WILL_ENTER_FOREGROUND);
	ok = check(!Engine::getInstance()->isApplicationActive(),
		"WILL_ENTER_FOREGROUND keeps the application inactive until DID_ENTER_FOREGROUND") && ok;
	ok = check(!CoreLifecycleTestAccess::canPrepareRenderFrame(),
		"WILL_ENTER_FOREGROUND keeps renderer preparation gated until DID_ENTER_FOREGROUND") && ok;
	CoreLifecycleTestAccess::frame(*root);
	ok = check(root->updateCount == 1,
		"the WILL-to-DID foreground interval does not update game elements") && ok;

	CoreLifecycleTestAccess::sendEngineEvent(SDL_EVENT_DID_ENTER_FOREGROUND);
	ok = check(
		!Engine::getInstance()->isApplicationActive() &&
			!CoreLifecycleTestAccess::
				canPrepareRenderFrame(),
		"DID_ENTER_FOREGROUND remains gated until the owner event pump applies it") &&
		ok;
	CoreLifecycleTestAccess::frame(*root);
	ok = check(Engine::getInstance()->isApplicationActive(),
		"foreground lifecycle restoration resumes the application") && ok;
	ok = check(CoreLifecycleTestAccess::canPrepareRenderFrame(),
		"DID_ENTER_FOREGROUND reopens renderer preparation") && ok;
	ok = check(root->updateCount == 2,
		"the first lifecycle-foreground frame resumes element updates") && ok;
	ok = check(!CoreLifecycleTestAccess::timerPaused(*root),
		"lifecycle foreground restoration resumes running element timers") && ok;
	ok = check(!CoreLifecycleTestAccess::applicationMediaPaused(),
		"lifecycle foreground restoration resumes application media") && ok;
	ok = check(CoreLifecycleTestAccess::timerPaused(*alreadyPausedRoot),
		"foreground restoration preserves a root that was already paused") && ok;
	root->setRunning(false);
	alreadyPausedRoot->setPaused(false);
	alreadyPausedRoot->setRunning(false);
	CoreLifecycleTestAccess::clearRunningElements();
	Element::resetApplicationQuitState();
	return ok;
}

bool runDeferredResizeLifecycleTests()
{
	bool ok = true;
	Engine* engine = Engine::getInstance();
	Element::resetApplicationQuitState();

	int originalWidth = 0;
	int originalHeight = 0;
	engine->getWindowSize(
		originalWidth,
		originalHeight);
	auto resizeRoot =
		std::make_shared<WindowResizeElement>();
	resizeRoot->setRunning(true);
	CoreLifecycleTestAccess::setRunningElements(
		{ resizeRoot });

	int deferredWidth = originalWidth + 64;
	int deferredHeight = originalHeight + 64;
	{
		ScopedHeadlessFramePump backgroundGate;
		engine->setWindowSize(
			deferredWidth,
			deferredHeight);
		engine->getWindowSize(
			deferredWidth,
			deferredHeight);
		engine->frameBegin();
		ok = check(
			resizeRoot->resizeCount == 0,
			"a logical resize is not dispatched to renderer-backed UI while the render gate is closed") &&
			ok;
	}

	engine->frameBegin();
	CoreLifecycleTestAccess::handleEvents(
		*resizeRoot);
	ok = check(
		resizeRoot->resizeCount == 1 &&
			resizeRoot->lastWidth == deferredWidth &&
			resizeRoot->lastHeight == deferredHeight,
		"the latest deferred resize is re-emitted after renderer readiness is restored") &&
		ok;

	const bool previousPendingResizeEvent =
		CoreLifecycleTestAccess::pendingLogicalResizeEvent();
	const bool previousPendingTextureResize =
		CoreLifecycleTestAccess::
			pendingLogicalScreenTextureResize();
	int previousLogicalWidth = 0;
	int previousLogicalHeight = 0;
	CoreLifecycleTestAccess::getLogicalSize(
		previousLogicalWidth,
		previousLogicalHeight);
	const bool previousBaseBackgroundState =
		CoreLifecycleTestAccess::
			setHeadlessFramePump(false);
	CoreLifecycleTestAccess::setPendingLogicalResizeState(
		false,
		previousPendingTextureResize);
	CoreLifecycleTestAccess::setLogicalSize(910, 510);
	const std::uint32_t queuedResizeGeneration =
		CoreLifecycleTestAccess::
			recordLogicalResizeEvent();
	CoreLifecycleTestAccess::setHeadlessFramePump(true);
	CoreLifecycleTestAccess::setLogicalSize(920, 520);
	(void)CoreLifecycleTestAccess::
		recordLogicalResizeEvent();
	CoreLifecycleTestAccess::setHeadlessFramePump(false);
	CoreLifecycleTestAccess::finalizeLogicalResizeEventPump(
		true,
		queuedResizeGeneration);
	AEvent foregroundEvent;
	AEvent foregroundResizeEvent;
	int foregroundResizeCount = 0;
	while (engine->getEvent(foregroundEvent) > 0)
	{
		if (foregroundEvent.eventType ==
			ET_WINDOWRESIZE)
		{
			foregroundResizeCount++;
			foregroundResizeEvent = foregroundEvent;
		}
	}
	ok = check(
		foregroundResizeCount == 2 &&
			foregroundResizeEvent.eventType ==
				ET_WINDOWRESIZE &&
			foregroundResizeEvent.eventX == 920 &&
			foregroundResizeEvent.eventY == 520 &&
			CoreLifecycleTestAccess::
				pendingLogicalResizeEvent(),
		"foreground recovery republishes the latest resize even when an older size was queued before the background interval") &&
		ok;
	engine->acknowledgeLogicalResizeEvent(
		static_cast<std::uint32_t>(
			foregroundResizeEvent.eventData),
		foregroundResizeEvent.eventX,
		foregroundResizeEvent.eventY);
	ok = check(
		!CoreLifecycleTestAccess::
			pendingLogicalResizeEvent(),
		"the latest resize generation clears only after UI consumption acknowledgement") &&
		ok;
	CoreLifecycleTestAccess::setHeadlessFramePump(
		previousBaseBackgroundState);
	CoreLifecycleTestAccess::setLogicalSize(
		previousLogicalWidth,
		previousLogicalHeight);
	CoreLifecycleTestAccess::setPendingLogicalResizeState(
		previousPendingResizeEvent,
		previousPendingTextureResize);

	const std::uint32_t interruptedResizeGeneration =
		CoreLifecycleTestAccess::
			recordLogicalResizeEvent();
	CoreLifecycleTestAccess::sendEngineEvent(
		SDL_EVENT_WILL_ENTER_BACKGROUND);
	engine->frameBegin();
	CoreLifecycleTestAccess::sendEngineEvent(
		SDL_EVENT_DID_ENTER_FOREGROUND);
	engine->frameBegin();
	AEvent replayedResizeEvent;
	bool interruptedResizeReplayed = false;
	while (engine->getEvent(foregroundEvent) > 0)
	{
		if (foregroundEvent.eventType ==
				ET_WINDOWRESIZE &&
			static_cast<std::uint32_t>(
				foregroundEvent.eventData) ==
				interruptedResizeGeneration)
		{
			replayedResizeEvent = foregroundEvent;
			interruptedResizeReplayed = true;
		}
	}
	ok = check(
		interruptedResizeReplayed &&
			CoreLifecycleTestAccess::
				pendingLogicalResizeEvent(),
		"a resize cleared from the queue by backgrounding is replayed until the UI acknowledges it") &&
		ok;
	if (interruptedResizeReplayed)
	{
		engine->acknowledgeLogicalResizeEvent(
			static_cast<std::uint32_t>(
				replayedResizeEvent.eventData),
			replayedResizeEvent.eventX,
			replayedResizeEvent.eventY);
	}
	engine->frameEnd();
	ok = check(
		!CoreLifecycleTestAccess::
			pendingLogicalResizeEvent(),
		"the replayed resize generation is acknowledged after foreground UI consumption") &&
		ok;

	SDL_Renderer* previousRenderer =
		CoreLifecycleTestAccess::exchangeRenderer(nullptr);
	CoreLifecycleTestAccess::setPendingLogicalResizeState(
		true,
		true);
	engine->frameBegin();
	ok = check(
		CoreLifecycleTestAccess::pendingLogicalResizeEvent() &&
			CoreLifecycleTestAccess::
				pendingLogicalScreenTextureResize() &&
			!engine->isFrameReady(),
		"a deferred resize remains armed when logical texture recreation must retry") &&
		ok;
	CoreLifecycleTestAccess::exchangeRenderer(
		previousRenderer);
	CoreLifecycleTestAccess::setPendingLogicalResizeState(
		previousPendingResizeEvent,
		previousPendingTextureResize);

	engine->setWindowSize(
		originalWidth,
		originalHeight);
	engine->frameBegin();
	CoreLifecycleTestAccess::handleEvents(
		*resizeRoot);
	resizeRoot->setRunning(false);
	CoreLifecycleTestAccess::clearRunningElements();
	Element::resetApplicationQuitState();
	return ok;
}

bool runCameraViewportResizeTests()
{
	int originalWidth = 0;
	int originalHeight = 0;
	CoreLifecycleTestAccess::getLogicalSize(
		originalWidth,
		originalHeight);

	GameManager gameManager;
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = 50;
	gameManager.map->data->head.height = 100;
	gameManager.player->setPosition({ 25, 95 });
	gameManager.camera->followPlayer = true;

	CoreLifecycleTestAccess::setLogicalSize(640, 480);
	gameManager.player->setPosition({ 0, 50 });
	gameManager.camera->snapToFollowTarget();
	const PointEx leftEdgeCameraWorldPosition =
		Map::getTilePositionEx(
			gameManager.camera->position,
			{ 0, 0 },
			{ 0, 0 },
			{ 0, 0 }) +
		gameManager.camera->offset;
	const float leftEvenRowEdgeX = Map::getTilePositionEx(
		{ 0, 50 },
		gameManager.camera->position,
		{ 640 / 2, 480 / 2 },
		gameManager.camera->offset).x;
	const float leftOddRowEdgeX = Map::getTilePositionEx(
		{ 0, 51 },
		gameManager.camera->position,
		{ 640 / 2, 480 / 2 },
		gameManager.camera->offset).x;
	gameManager.player->setPosition({ 0, 51 });
	gameManager.camera->snapToFollowTarget();
	const PointEx leftOddRowCameraWorldPosition =
		Map::getTilePositionEx(
			gameManager.camera->position,
			{ 0, 0 },
			{ 0, 0 },
			{ 0, 0 }) +
		gameManager.camera->offset;
	gameManager.player->setPosition({ 49, 50 });
	gameManager.camera->snapToFollowTarget();
	const PointEx rightEdgeCameraWorldPosition =
		Map::getTilePositionEx(
			gameManager.camera->position,
			{ 0, 0 },
			{ 0, 0 },
			{ 0, 0 }) +
		gameManager.camera->offset;
	const float rightEvenRowEdgeX = Map::getTilePositionEx(
		{ 49, 50 },
		gameManager.camera->position,
		{ 640 / 2, 480 / 2 },
		gameManager.camera->offset).x;
	const float rightOddRowEdgeX = Map::getTilePositionEx(
		{ 49, 51 },
		gameManager.camera->position,
		{ 640 / 2, 480 / 2 },
		gameManager.camera->offset).x;
	gameManager.player->setPosition({ 49, 51 });
	gameManager.camera->snapToFollowTarget();
	const PointEx rightOddRowCameraWorldPosition =
		Map::getTilePositionEx(
			gameManager.camera->position,
			{ 0, 0 },
			{ 0, 0 },
			{ 0, 0 }) +
		gameManager.camera->offset;
	bool ok = check(
		std::abs(leftEdgeCameraWorldPosition.x - 352.0f) < 0.001f &&
			std::abs(leftOddRowCameraWorldPosition.x - 352.0f) < 0.001f &&
			std::abs(rightEdgeCameraWorldPosition.x - 2848.0f) < 0.001f &&
			std::abs(rightOddRowCameraWorldPosition.x - 2848.0f) < 0.001f &&
			std::abs(leftEvenRowEdgeX + 32.0f) < 0.001f &&
			std::abs(leftOddRowEdgeX) < 0.001f &&
			std::abs(rightEvenRowEdgeX - 608.0f) < 0.001f &&
			std::abs(rightOddRowEdgeX - 640.0f) < 0.001f,
		"player-follow camera insets both horizontal map edges by half a tile");
	auto snapCameraWorldX = [&gameManager](Point playerPosition)
	{
		gameManager.player->setPosition(playerPosition);
		gameManager.camera->snapToFollowTarget();
		return (Map::getTilePositionEx(
			gameManager.camera->position,
			{ 0, 0 },
			{ 0, 0 },
			{ 0, 0 }) + gameManager.camera->offset).x;
	};
	const float leftClampedEvenX = snapCameraWorldX({ 4, 50 });
	const float leftBoundaryEvenX = snapCameraWorldX({ 5, 50 });
	const float leftFollowingEvenX = snapCameraWorldX({ 6, 50 });
	const float leftClampedOddX = snapCameraWorldX({ 4, 51 });
	const float leftFollowingOddX = snapCameraWorldX({ 5, 51 });
	const float rightFollowingEvenX = snapCameraWorldX({ 43, 50 });
	const float rightClampedEvenX = snapCameraWorldX({ 45, 50 });
	const float rightBoundaryOddX = snapCameraWorldX({ 43, 51 });
	const float rightClampedOddX = snapCameraWorldX({ 44, 51 });
	ok = check(
		std::abs(leftClampedEvenX - 352.0f) < 0.001f &&
			std::abs(leftBoundaryEvenX - 352.0f) < 0.001f &&
			std::abs(leftFollowingEvenX - 384.0f) < 0.001f &&
			std::abs(leftClampedOddX - 352.0f) < 0.001f &&
			std::abs(leftFollowingOddX - 352.0f) < 0.001f &&
			std::abs(rightFollowingEvenX - 2752.0f) < 0.001f &&
			std::abs(rightClampedEvenX - 2848.0f) < 0.001f &&
			std::abs(rightBoundaryOddX - 2784.0f) < 0.001f &&
			std::abs(rightClampedOddX - 2848.0f) < 0.001f,
		"camera follow enters and leaves both parity-specific edge clamps without reversal") && ok;
	gameManager.player->setPosition({ 20, 50 });
	gameManager.camera->snapToFollowTarget();
	const PointEx interiorEvenRowCameraWorldPosition =
		Map::getTilePositionEx(
			gameManager.camera->position,
			{ 0, 0 },
			{ 0, 0 },
			{ 0, 0 }) +
		gameManager.camera->offset;
	const PointEx interiorEvenRowPlayerScreenPosition =
		Map::getTilePositionEx(
			gameManager.player->getPosition(),
			gameManager.camera->position,
			{ 640 / 2, 480 / 2 },
			gameManager.camera->offset);
	gameManager.player->setPosition({ 20, 51 });
	gameManager.camera->snapToFollowTarget();
	const PointEx interiorOddRowCameraWorldPosition =
		Map::getTilePositionEx(
			gameManager.camera->position,
			{ 0, 0 },
			{ 0, 0 },
			{ 0, 0 }) +
		gameManager.camera->offset;
	const PointEx interiorOddRowPlayerScreenPosition =
		Map::getTilePositionEx(
			gameManager.player->getPosition(),
			gameManager.camera->position,
			{ 640 / 2, 480 / 2 },
			gameManager.camera->offset);
	gameManager.map->data->head.width = 8;
	gameManager.player->setPosition({ 0, 50 });
	gameManager.camera->snapToFollowTarget();
	const PointEx centeredEvenRowCameraWorldPosition =
		Map::getTilePositionEx(
			gameManager.camera->position,
			{ 0, 0 },
			{ 0, 0 },
			{ 0, 0 }) +
		gameManager.camera->offset;
	gameManager.player->setPosition({ 0, 51 });
	gameManager.camera->snapToFollowTarget();
	const PointEx centeredOddRowCameraWorldPosition =
		Map::getTilePositionEx(
			gameManager.camera->position,
			{ 0, 0 },
			{ 0, 0 },
			{ 0, 0 }) +
		gameManager.camera->offset;
	gameManager.map->data->head.width = 13;
	CoreLifecycleTestAccess::setLogicalSize(640, 480);
	const float scrollableMapCameraX = snapCameraWorldX({ 0, 50 });
	CoreLifecycleTestAccess::setLogicalSize(768, 480);
	CoreLifecycleTestAccess::resize(*gameManager.camera, 768, 480);
	const float centeredAtThresholdCameraX =
		(Map::getTilePositionEx(
			gameManager.camera->position,
			{ 0, 0 },
			{ 0, 0 },
			{ 0, 0 }) + gameManager.camera->offset).x;
	CoreLifecycleTestAccess::setLogicalSize(640, 480);
	CoreLifecycleTestAccess::resize(*gameManager.camera, 640, 480);
	const float restoredScrollableMapCameraX =
		(Map::getTilePositionEx(
			gameManager.camera->position,
			{ 0, 0 },
			{ 0, 0 },
			{ 0, 0 }) + gameManager.camera->offset).x;
	ok = check(
		std::abs(interiorEvenRowCameraWorldPosition.x - 1280.0f) < 0.001f &&
			std::abs(interiorOddRowCameraWorldPosition.x - 1312.0f) < 0.001f &&
			std::abs(interiorEvenRowPlayerScreenPosition.x - 320.0f) < 0.001f &&
			std::abs(interiorOddRowPlayerScreenPosition.x - 320.0f) < 0.001f &&
			std::abs(centeredEvenRowCameraWorldPosition.x - 256.0f) < 0.001f &&
			std::abs(centeredOddRowCameraWorldPosition.x - 256.0f) < 0.001f &&
			std::abs(scrollableMapCameraX - 352.0f) < 0.001f &&
			std::abs(centeredAtThresholdCameraX - 416.0f) < 0.001f &&
			std::abs(restoredScrollableMapCameraX - 352.0f) < 0.001f,
		"camera follow, clamped edges, and centered maps stay continuous across row parity and viewport thresholds") && ok;
	gameManager.map->data->head.width = 50;
	gameManager.player->setPosition({ 25, 95 });
	gameManager.scriptAPI.setMapPos(10, 21);
	const PointEx referenceViewportPosition =
		Map::getTilePositionEx(
			{ 10, 21 },
			gameManager.camera->position,
			{ 640 / 2, 480 / 2 },
			gameManager.camera->offset);
	ok = check(
		std::abs(referenceViewportPosition.x - 32.0f) < 0.001f &&
			std::abs(referenceViewportPosition.y) < 0.001f &&
			gameManager.camera->position == Point{ 15, 36 } &&
			!gameManager.camera->followPlayer &&
			gameManager.camera->differencePosition.x == 0.0f &&
			gameManager.camera->differencePosition.y == 0.0f,
		"SetMapPos preserves the original 640x480 scripted composition");
	CoreLifecycleTestAccess::setLogicalSize(1122, 500);
	gameManager.scriptAPI.setMapPos(10, 21);
	const PointEx wideViewportPosition =
		Map::getTilePositionEx(
			{ 10, 21 },
			gameManager.camera->position,
			{ 1122 / 2, 500 / 2 },
			gameManager.camera->offset);
	ok = check(
		std::abs(
			wideViewportPosition.x -
			(referenceViewportPosition.x + (1122 - 640) / 2.0f)) < 0.001f &&
			std::abs(
				wideViewportPosition.y -
				(referenceViewportPosition.y + (500 - 480) / 2.0f)) < 0.001f &&
			gameManager.camera->position == Point{ 15, 36 },
		"SetMapPos centers the fixed reference composition in a larger viewport") &&
		ok;
	gameManager.camera->followPlayer = true;
	gameManager.camera->snapToFollowTarget();
	const PointEx foldedWorldPosition =
		Map::getTilePositionEx(
			gameManager.camera->position,
			{ 0, 0 },
			{ 0, 0 },
			{ 0, 0 }) +
		gameManager.camera->offset;

	gameManager.camera->setPaused(true);
	CoreLifecycleTestAccess::setLogicalSize(1122, 1082);
	CoreLifecycleTestAccess::resize(
		*gameManager.camera,
		1122,
		1082);
	const PointEx unfoldedWorldPosition =
		Map::getTilePositionEx(
			gameManager.camera->position,
			{ 0, 0 },
			{ 0, 0 },
			{ 0, 0 }) +
		gameManager.camera->offset;

	ok = check(
		unfoldedWorldPosition.y < foldedWorldPosition.y &&
			std::abs(unfoldedWorldPosition.y - 1027.0f) < 0.001f &&
			gameManager.camera->differencePosition.x == 0.0f &&
			gameManager.camera->differencePosition.y == 0.0f,
		"a paused player-follow camera reapplies map bounds immediately after a taller foldable viewport resize") &&
		ok;

	gameManager.camera->setPaused(false);
	CoreLifecycleTestAccess::setLogicalSize(
		originalWidth,
		originalHeight);
	return ok;
}

bool runGameplayPauseTests()
{
	bool ok = true;
	GameManager gameManager;
	gameManager.timerStarted = true;
	gameManager.timerSeconds = 9;
	gameManager.timerAccumulated = 500;
	ScriptTask delayedTask;
	delayedTask.scriptName = "deferred.lua";
	delayedTask.remainingMilliseconds = 250;
	gameManager.scriptTaskList.push_back(delayedTask);
	CoreLifecycleTestAccess::setFrameTime(gameManager, 40);

	auto modal = std::make_shared<CountingElement>();
	gameManager.addChild(modal);
	gameManager.controller->setPaused(true);
	gameManager.setGameplayPaused(true);
	ok = check(gameManager.isGameplayPaused(),
		"system modal enables the explicit gameplay pause state") && ok;
	ok = check(CoreLifecycleTestAccess::timerPaused(*gameManager.controller)
		&& CoreLifecycleTestAccess::timerPaused(*gameManager.menu)
		&& CoreLifecycleTestAccess::timerPaused(*gameManager.weather),
		"gameplay pause freezes controller, menu, and weather timers") && ok;
	ok = check(!CoreLifecycleTestAccess::shouldUpdateGameManagerChild(gameManager, gameManager.controller)
		&& !CoreLifecycleTestAccess::shouldUpdateGameManagerChild(gameManager, gameManager.menu)
		&& !CoreLifecycleTestAccess::shouldUpdateGameManagerChild(gameManager, gameManager.weather),
		"gameplay pause gates controller, menu, and weather updates") && ok;
	ok = check(CoreLifecycleTestAccess::shouldUpdateGameManagerChild(gameManager, modal),
		"gameplay pause leaves the active system modal interactive") && ok;

	CoreLifecycleTestAccess::updateGameManager(gameManager);
	ok = check(gameManager.timerSeconds == 9 && gameManager.timerAccumulated == 500,
		"gameplay pause does not advance the mission timer") && ok;
	ok = check(gameManager.scriptTaskList.size() == 1
		&& gameManager.scriptTaskList[0].remainingMilliseconds == 250,
		"gameplay pause does not advance delayed scripts") && ok;

	gameManager.handleSystemResult(erOK);
	ok = check(CoreLifecycleTestAccess::timerPaused(*gameManager.controller)
		&& !CoreLifecycleTestAccess::timerPaused(*gameManager.menu)
		&& !CoreLifecycleTestAccess::timerPaused(*gameManager.weather),
		"handling the system result resumes gameplay and preserves a timer that was already paused") && ok;

	gameManager.setGameplayPaused(true);
	gameManager.handleSystemResult(erLoad, -1);
	ok = check(!gameManager.isGameplayPaused()
		&& !CoreLifecycleTestAccess::timerPaused(*gameManager.weather),
		"system load handling resumes the weather timer before optional slot loading") && ok;
	gameManager.controller->setPaused(false);
	gameManager.removeChild(modal);
	return ok;
}

bool runSceneResultTests()
{
	bool ok = true;
	{
		System system;
		system.quitBtn = std::make_shared<Button>();
		system.quitBtn->result = erClick;
		system.setRunning(true);
		CoreLifecycleTestAccess::handleSystemEvent(system);
		ok = check(system.result == erReturnToTitle,
			"system quit button requests return to title instead of desktop exit") && ok;
		ok = check(!CoreLifecycleTestAccess::logicRunning(system),
			"system quit button closes the modal") && ok;
	}

	MainScene mainScene(0);
	{
		auto messageBox = std::make_shared<MsgBox>();
		mainScene.game->menu->messageBox = messageBox;
		messageBox->visible = false;
		messageBox->showed = false;
		messageBox->currentMessage.clear();

		System system;
		system.setRunning(true);
		CoreLifecycleTestAccess::handleSystemSaveFailure(system);
		ok = check(system.result == erOK
			&& !CoreLifecycleTestAccess::logicRunning(system),
			"system save failure closes the hidden modal so feedback becomes visible") && ok;
		ok = check(messageBox->visible && messageBox->showed
			&& messageBox->currentMessage == "存档失败",
			"system save failure queues the user-visible failure message") && ok;

		messageBox->visible = false;
		messageBox->showed = false;
		messageBox->currentMessage.clear();
		SaveLoad loadOnly(false, true);
		loadOnly.listBox = std::make_shared<ListBox>();
		loadOnly.listBox->index = 0;
		loadOnly.index = 0;
		loadOnly.exitBtn = std::make_shared<Button>();
		loadOnly.exitBtn->result = erClick;
		loadOnly.setRunning(true);
		CoreLifecycleTestAccess::handleSaveLoadEvent(loadOnly);
		ok = check(!CoreLifecycleTestAccess::logicRunning(loadOnly)
			&& loadOnly.result == erOK
			&& (loadOnly.result & erExit) == 0,
			"save-load return button closes only the save-load layer") && ok;

		mainScene.game->inEvent = true;
		SaveLoad saveLoad(true, false);
		saveLoad.listBox = std::make_shared<ListBox>();
		saveLoad.listBox->index = 0;
		saveLoad.index = 0;
		saveLoad.saveBtn = std::make_shared<Button>();
		saveLoad.saveBtn->result = erClick;
		saveLoad.setRunning(true);
		CoreLifecycleTestAccess::handleSaveLoadEvent(saveLoad);
		ok = check(CoreLifecycleTestAccess::logicRunning(saveLoad)
			&& saveLoad.result == erNone,
			"manual save remains open without emitting a save result during an event") && ok;
		ok = check(messageBox->visible && messageBox->showed
			&& messageBox->currentMessage == "事件进行中，暂时无法存档",
			"manual save during an event shows the save refusal message") && ok;
		mainScene.game->inEvent = false;
	}
	const unsigned int completionResults[] = { erOK, erReturnToTitle, erExit };
	for (unsigned int completionResult : completionResults)
	{
		mainScene.result = erNone;
		mainScene.setRunning(true);
		mainScene.game->result = completionResult;
		CoreLifecycleTestAccess::updateMainScene(mainScene);
		ok = check((mainScene.result & completionResult) != 0,
			"main scene propagates its game manager completion result") && ok;
		ok = check(!CoreLifecycleTestAccess::logicRunning(mainScene),
			"main scene stops after a propagated completion result") && ok;
	}

	mainScene.game->result = erNone;
	mainScene.game->setRunning(true);
	mainScene.game->handleSystemResult(erReturnToTitle);
	ok = check((mainScene.game->result & erReturnToTitle) != 0
		&& !CoreLifecycleTestAccess::logicRunning(*mainScene.game),
		"game manager propagates the system return-to-title result") && ok;
	return ok;
}

bool runNewYearPeriodTests(GameManager& gameManager)
{
	using NewYearPeriod::LocalDate;
	bool ok = true;
	ok = check(!NewYearPeriod::contains({ 2023, 12, 31 }),
		"December 31 is outside the configured January-February period") && ok;
	ok = check(NewYearPeriod::contains({ 2024, 1, 1 }),
		"January 1 starts the configured New Year period") && ok;
	ok = check(NewYearPeriod::contains({ 2024, 1, 31 }),
		"the entire month of January remains inside the configured period") && ok;
	ok = check(NewYearPeriod::contains({ 2024, 2, 1 }),
		"February 1 continues the configured New Year period") && ok;
	ok = check(NewYearPeriod::contains({ 2023, 2, 28 }),
		"February 28 remains inside the configured New Year period") && ok;
	ok = check(NewYearPeriod::contains({ 2024, 2, 29 }),
		"February 29 is accepted in a leap year") && ok;
	ok = check(!NewYearPeriod::contains({ 2023, 2, 29 }),
		"February 29 is rejected in a non-leap year") && ok;
	ok = check(!NewYearPeriod::contains({ 2024, 3, 1 }),
		"March 1 ends the configured New Year period") && ok;

	std::tm localTime = {};
	localTime.tm_year = 2024 - 1900;
	localTime.tm_mon = 1;
	localTime.tm_mday = 29;
	localTime.tm_hour = 12;
	localTime.tm_isdst = -1;
	const std::time_t localTimestamp = std::mktime(&localTime);
	LocalDate convertedDate;
	ok = check(localTimestamp != static_cast<std::time_t>(-1)
		&& NewYearPeriod::tryGetLocalDate(localTimestamp, convertedDate)
		&& convertedDate.year == 2024
		&& convertedDate.month == 2
		&& convertedDate.day == 29,
		"system timestamps are evaluated through the process local timezone") && ok;
	ok = check(NewYearPeriod::contains(std::chrono::system_clock::from_time_t(localTimestamp)),
		"time-point entry uses the same local-date New Year predicate") && ok;
	gameManager.varList.ensureInitialized();
	gameManager.scriptAPI.checkYear("new_year_boundary", { 2024, 2, 29 });
	ok = check(gameManager.varList.getInteger("new_year_boundary") == 1,
		"CheckYear writes one through the shared predicate for a leap-day date") && ok;
	gameManager.scriptAPI.checkYear("new_year_boundary", { 2024, 3, 1 });
	ok = check(gameManager.varList.getInteger("new_year_boundary") == 0,
		"CheckYear writes zero through the shared predicate after the period") && ok;
#if defined(JXQY_ENABLE_AUTOMATION_HOOKS)
	NewYearPeriod::setAutomationLocalDate({ 2026, 1, 1 });
	gameManager.scriptAPI.checkYear("new_year_boundary");
	ok = check(gameManager.varList.getInteger("new_year_boundary") == 1,
		"native CheckYear reads the process test date") && ok;
	NewYearPeriod::setAutomationLocalDate({ 2026, 3, 1 });
	gameManager.scriptAPI.checkYear("new_year_boundary");
	ok = check(gameManager.varList.getInteger("new_year_boundary") == 0,
		"native CheckYear uses the process date outside festival months") && ok;
	NewYearPeriod::clearAutomationLocalDate();
#endif
	return ok;
}
}

bool runLinanMeleeResourceContracts()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	bool ok = true;
	for (const auto& pack : { std::string("jxqy2"), std::string(u8"剑二改承合版"), std::string(u8"新月无痕") })
	{
		ScopedActiveResourceRoot resourceRoot;
		SaveFileManager::CurrentPathScope currentPath("save/linan_melee_resources");
		if (!check(resourceRoot.valid() && currentPath.valid(), "Linan melee regression isolates state writes")) return false;
		File::setResourceFallbackRoots({ (assetsRoot / std::filesystem::u8path(pack)).u8string(),
			(assetsRoot / "jxqy2").u8string(), (assetsRoot / "common").u8string() });
		GameManager gameManager;
		ResourceManifest manifest;
		if (!check(manifest.loadFromFile("game_profile.ini"), "Linan melee regression reads its real profile")) return false;
		gameManager.global.applyResourceManifestFeatures(manifest);
		gameManager.global.data.NPCAI = true;
		gameManager.varList.ensureInitialized();
		gameManager.map->data = std::make_shared<MapData>();
		gameManager.map->data->head.width = gameManager.map->data->head.height = 100;
		gameManager.map->data->tile.assign(100, std::vector<MapTile>(100));
		gameManager.player->info.lifeMax = gameManager.player->life = 100000000;
		gameManager.player->setPosition({ 41, 40 }, false);
		std::unique_ptr<char[]> bytes;
		if (!check(File::readFile("ini/save/linan.npc", bytes) > 0, "load the actual Linan NPC template")) return false;
		INIReader definition(bytes);
		for (int index : { 113, 114, 115, 116, 117, 118, 119, 120, 130, 131, 132, 133, 134, 135, 136, 137 })
		{
			gameManager.effectManager->freeResource();
			gameManager.npcManager->clearNPC(true);
			auto actor = std::make_shared<NPC>();
			actor->initFromIni(&definition, "NPC" + std::to_string(index));
			const std::string context = pack + "/NPC" + std::to_string(index);
			if (!check(actor->npcMagic != nullptr && actor->npcMagic->loadSucceeded
				&& actor->flyIni == u8"magic-两格长枪.ini", (context + " loads its real attack definition").c_str()))
			{
				ok = false;
				continue;
			}
			actor->setPosition({ 40, 40 }, false);
			gameManager.npcManager->addNPC(actor);
			gameManager.map->createDataMap();
			for (int frame = 0; frame < 100 && !actor->isAttacking(); ++frame)
			{
				CoreLifecycleTestAccess::advanceActorFrame(*actor, 50);
			}
			ok = check(actor->isAttacking(), (context + " native AI attacks an adjacent player").c_str()) && ok;
			CoreLifecycleTestAccess::advanceActorFrame(*actor, actor->actionLastTime + 1);
			ok = check(!gameManager.effectManager->effectList.empty(),
				(context + " releases the normal attack effect").c_str()) && ok;
		}
	}
	return ok;
}

bool runProductionAttackFileRuntimeTests()
{
	bool ok = runLinanMeleeResourceContracts();
	ok = runAttackSaveBoundaryContracts() && ok;
	ok = runNativeNpcAttackProtocolContracts() && ok;
	ok = runMoonlightNativeAttackObservations() && ok;
	ok = runMissedAttackMotionContracts() && ok;
	ok = runProductionExplicitAttackCombat() && ok;
	ok = runProductionAttackDistanceObservations() && ok;
	ok = runProductionExplicitAttackLists() && ok;
	ok = runProductionAttackFileContracts() && ok;
	ok = runProductionScriptAttackContracts() && ok;
	return runProductionScriptMagicContracts() && ok;
}

bool runFullAttackSaveRuntimeTests()
{
	class CollisionObservingPlayer final : public Player
	{
	public:
		int collisions = 0;
		void hurt(std::shared_ptr<Effect> effect) override
		{
			if (effect != nullptr) ++collisions;
			Player::hurt(effect);
		}
	};
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	auto& resources = ResourceManager::instance();
	if (!check(resources.initialize(assetsRoot.u8string()), "full attack saves discover actual resource profiles")) return false;
	struct SaveCase { const char* id; const char* file; const char* section; int distance = 0; };
	const SaveCase cases[] = {
		{ "YYCS", "ini/save/map006_1.npc", "NPC007" },
		{ "YYCS", "ini/save/map006_1.npc", "NPC007", 1 },
		{ "XJXQY", u8"ini/npc/npc011_方勉.ini", "Init" },
		{ "JIANGHU_YUCHEN_2", "ini/save/map016.npc", "NPC002" }
	};
	bool ok = true;
	for (const auto& fixture : cases)
	{
		if (!check(resources.setActiveResourcePackById(fixture.id), "select the actual profile used again by full reload")) return false;
		const auto packRoot = resources.getActiveResourceRoot();
		for (bool playerAttack : { false, true })
		for (bool afterRelease : { false, true })
		for (bool asynchronous : { false, true })
		{
			if (fixture.distance != 0 && playerAttack) continue;
			ScopedActiveResourceRoot resourceRoot;
			if (!check(resourceRoot.valid(), "full attack saves use the isolated platform state parent")) return false;
			std::vector<std::string> roots{ packRoot };
			if (std::string(fixture.id) == "JIANGHU_YUCHEN_2") roots.push_back((assetsRoot / "yycs").u8string());
			roots.push_back((assetsRoot / "common").u8string());
			File::setResourceFallbackRoots(roots);
			File::setUiResourceFallbackRoots(roots, true, (assetsRoot / "common").u8string());
			// A file-backed blank map exercises the real full-load map preparation.
			auto mapBytes = MapV3ContractFixture::build();
			constexpr size_t tileOffset = MapV3ContractFixture::HeaderLength
				+ MapV3ContractFixture::MpcCount * MapV3ContractFixture::InfoLength;
			mapBytes.resize(tileOffset + 96 * 96 * MapV3ContractFixture::TileLength);
			std::fill(mapBytes.begin() + MapV3ContractFixture::BaseHeaderLength, mapBytes.end(), std::uint8_t{0});
			MapV3ContractFixture::writeInt32(mapBytes, 64, 96 * 96 * MapV3ContractFixture::TileLength);
			MapV3ContractFixture::writeInt32(mapBytes, 68, 96);
			MapV3ContractFixture::writeInt32(mapBytes, 72, 96);
			if (!check(writeVirtualFile("map/attack-save.map", std::string(reinterpret_cast<const char*>(mapBytes.data()), mapBytes.size()))
				&& writeVirtualFile("save/game/game.ini", "[State]\nMap=attack-save.map\nNpc=\nObj=\n"),
				"create a file-backed empty lane and isolated current generation")) return false;
			GameManager gameManager;
			gameManager.controller->removeChild(gameManager.player);
			auto player = std::make_shared<CollisionObservingPlayer>();
			gameManager.player = player;
			gameManager.npcManager->setPlayer(player);
			gameManager.controller->addChild(player);
			std::unique_ptr<char[]> bytes;
			if (!check(player->loadInitialTemplate(0) && File::readFile(fixture.file, bytes) > 0
				&& gameManager.scriptAPI.loadMap("attack-save.map", false), "load complete production actors and the actual map loader")) return false;
			gameManager.global.applyResourceManifestFeatures(resources.getActiveManifest());
			gameManager.global.data.characterIndex = 0;
			gameManager.global.data.NPCAI = false;
			gameManager.global.data.canInput = false;
			gameManager.varList.ensureInitialized();
			gameManager.varList.setInteger("AttackSave", 73);
			gameManager.varList.setInteger("attacksave", 29);
			INIReader definition(bytes);
			auto npc = std::make_shared<NPC>();
			npc->initFromIni(&definition, fixture.section);
			npc->relation = nrHostile;
			npc->isAIDisabled = true;
			gameManager.npcManager->addNPC(npc);
			std::shared_ptr<NPC> actor = playerAttack ? std::static_pointer_cast<NPC>(player) : npc;
			std::shared_ptr<NPC> target = playerAttack ? npc : std::static_pointer_cast<NPC>(player);
			// Keep the production attacks, but make a durable stationary target.
			target->lifeMax = target->life = 1000000;
			target->defend = target->defend2 = target->defend3 = target->evade = 0;
			player->calInfo();
			actor->setPosition({ 40, 40 }, false);
			Point destination = actor->getPosition();
			const int distance = playerAttack ? 1 : fixture.distance != 0 ? fixture.distance : actor->getMaxAttackOptionDistance();
			for (int step = 0; step < distance; ++step) destination = Map::getSubPoint(destination, 0);
			target->setPosition(destination, false);
			gameManager.map->createDataMap();
			actor->beginAttack(destination, target);
			if (!check(actor->isAttacking() && actor->hasPreparedAttackMagic, "the real attack begins before a full save")) return false;
			const UTime duration = actor->actionLastTime;
			CoreLifecycleTestAccess::advanceActorFrame(*actor, afterRelease ? duration : duration / 2);
			if (afterRelease)
			{
				for (const auto& effect : gameManager.effectManager->effectList) CoreLifecycleTestAccess::advanceActorFrame(*effect, 1);
			}
			const auto originalEffects = gameManager.effectManager->effectList;
			const auto originalNpc = npc;
			const auto prepared = actor->preparedAttackMagic;
			const auto revision = actor->actionManager->getActionRevision();
			const bool native = actor->usesNativeAttackProtocol();
			if (!check(originalEffects.empty() == !afterRelease && gameManager.saveGame(1), "publish an actual slot before or after effect release")) return false;
			ok = check(gameManager.effectManager->effectList == originalEffects && actor->preparedAttackMagic == prepared
				&& actor->actionManager->getActionRevision() == revision, "full saving does not modify or replay the live attack") && ok;
			INIReader savedEffects(std::string("save/rpg1/") + EFFECT_INI);
			ok = check(savedEffects.GetInteger("Head", "Count", -1) == static_cast<int>(originalEffects.size()),
				"the published effect file contains exactly the released effects") && ok;
			INIReader savedGlobal(std::string("save/rpg1/") + GLOBAL_INI);
			ok = check(savedGlobal.Get("Save", "EngineVersion", "") == JxqyBuildVersion::EngineVersion
				&& savedGlobal.Get("Save", "ResourceVersion", "") == resources.getActiveManifest().releaseMetadata.displayVersion,
				"the same complete slot records the real engine and selected resource versions") && ok;
			const auto advanceEffects = [&]()
			{
				for (int elapsed = 0; elapsed < 6000 && !gameManager.effectManager->effectList.empty(); elapsed += 20)
				{
					CoreLifecycleTestAccess::beginElementFrame(*gameManager.effectManager);
					const auto effects = gameManager.effectManager->effectList;
					for (const auto& effect : effects) CoreLifecycleTestAccess::advanceActorFrame(*effect, 20);
					CoreLifecycleTestAccess::advanceActorFrame(*gameManager.effectManager, 20);
				}
			};
			// Run an unsaved-world control as well: firing distance alone does not
			// guarantee that a native Moonlight projectile physically reaches it.
			player->collisions = 0;
			const int savedLife = target->life;
			if (!afterRelease) CoreLifecycleTestAccess::advanceActorFrame(*actor, duration + 1);
			advanceEffects();
			const int liveCollisions = player->collisions;
			const int liveDamage = savedLife - target->life;
			// Leave a different live attack/effect set in memory for the loader to replace.
			actor->actionManager->restartActionIgnoringTransitions(acStand);
			actor->beginAttack(destination, target);
			gameManager.varList.setInteger("AttackSave", -1);
			const bool loaded = asynchronous ? gameManager.scriptAPI.loadGameAsync(1) : gameManager.loadGame(1);
			if (!check(loaded && gameManager.npcManager->npcList.size() == 1, "sync and async load replace the complete saved world")) return false;
			npc = gameManager.npcManager->npcList.front();
			actor = playerAttack ? std::static_pointer_cast<NPC>(player) : npc;
			target = playerAttack ? npc : std::static_pointer_cast<NPC>(player);
			ok = check(npc != originalNpc && actor->isStanding() && !actor->hasPreparedAttackMagic
				&& actor->usesNativeAttackProtocol() == native && gameManager.varList.getInteger("AttackSave") == 73
				&& gameManager.varList.getInteger("attacksave") == 29 && target->life == savedLife,
				"full reload clears stale attacks and restores profile, target life and case-sensitive state") && ok;
			const auto restoredEffects = gameManager.effectManager->effectList;
			ok = check(restoredEffects.size() == originalEffects.size(), "full reload restores the saved effect count without extra releases") && ok;
			for (size_t index = 0; index < std::min(originalEffects.size(), restoredEffects.size()); ++index)
			{
				const auto& effect = restoredEffects[index];
				const auto& original = originalEffects[index];
				const std::string section = "PRO" + std::to_string(index + 1);
				ok = check(effect != original && effect->user.lock() == actor
					&& effect->target.lock() == (savedEffects.GetInteger(section, "TargetReferenceKind", 0) == 0 ? nullptr : target)
					&& effect->fileName == savedEffects.Get(section, "FileName", "")
					&& effect->position.x == savedEffects.GetInteger(section, "MapX", -1)
					&& effect->position.y == savedEffects.GetInteger(section, "MapY", -1)
					&& std::abs(effect->offset.x - savedEffects.GetReal(section, "OffsetX", 0)) < 0.01f
					&& std::abs(effect->offset.y - savedEffects.GetReal(section, "OffsetY", 0)) < 0.01f
					&& effect->damage == savedEffects.GetInteger(section, "Damage", -1)
					&& effect->lifeTime == savedEffects.GetTime(section, "LifeTime", 0)
					&& effect->doing == savedEffects.GetInteger(section, "Doing", -1)
					&& effect->dest.x == savedEffects.GetInteger(section, "DestX", -1)
					&& effect->dest.y == savedEffects.GetInteger(section, "DestY", -1),
					"restored effects retain trajectory and damage and reference restored actors, not stale NPCs") && ok;
			}
			CoreLifecycleTestAccess::advanceActorFrame(*actor, duration + 1);
			ok = check(gameManager.effectManager->effectList == restoredEffects, "the restored actor does not emit its cancelled pre-load attack") && ok;
			if (!afterRelease)
			{
				actor->beginAttack(target->getPosition(), target);
				CoreLifecycleTestAccess::advanceActorFrame(*actor, actor->actionLastTime + 1);
				ok = check(!gameManager.effectManager->effectList.empty(), "a new post-load attack still releases normally") && ok;
			}
			const int initialLife = target->life;
			player->collisions = 0;
			advanceEffects();
			const bool reachable = !native || distance < actor->attackRadius;
			ok = check(playerAttack ? liveDamage > 0 && target->life < initialLife
				: (liveCollisions > 0) == (player->collisions > 0) && (!reachable || player->collisions > 0),
				"post-load collision agrees with the live control, including reachable native and explicit attacks") && ok;
			std::cout << "Full attack save:\tpack=" << fixture.id << "\tplayer=" << playerAttack << "\tafterRelease=" << afterRelease
				<< "\tasync=" << asynchronous << "\tnative=" << native << "\teffects=" << originalEffects.size()
				<< "\tdistance=" << distance << "\tdamage=" << initialLife - target->life << "\tplayerCollisions=" << player->collisions
				<< "\tliveDamage=" << liveDamage << "\tliveCollisions=" << liveCollisions << std::endl;
		}
	}
	return ok;
}

bool runEquipmentReplacementSaveRuntimeTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	auto& resources = ResourceManager::instance();
	if (!check(resources.initialize(assetsRoot.u8string()), "equipment saves discover actual resource profiles")) return false;
	const char* weapons[] = { u8"goods-w11-悲魔之刃.ini", u8"goods-w15-莫邪剑.ini", u8"goods-w18-干将剑.ini", u8"goods-w20-独孤剑.ini" };
	const std::string sourceMagic = u8"wugong普通攻击.ini";
	bool ok = true;
	for (const char* id : { "JIANGHU_YUCHEN_1_03", "XIAOXIANGXING_1_022" })
	{
		if (!check(resources.setActiveResourcePackById(id), "select the real equipment replacement resource profile")) return false;
		const auto packRoot = resources.getActiveResourceRoot();
		for (const char* weapon : weapons)
		for (bool equipped : { false, true })
		for (bool asynchronous : { false, true })
		{
			ScopedActiveResourceRoot resourceRoot;
			if (!check(resourceRoot.valid(), "equipment replacement saves isolate all mutable files")) return false;
			std::vector<std::string> roots{ packRoot, (assetsRoot / "yycs").u8string(), (assetsRoot / "common").u8string() };
			File::setResourceFallbackRoots(roots);
			File::setUiResourceFallbackRoots(roots, true, (assetsRoot / "common").u8string());
			auto mapBytes = MapV3ContractFixture::build();
			constexpr size_t tileOffset = MapV3ContractFixture::HeaderLength
				+ MapV3ContractFixture::MpcCount * MapV3ContractFixture::InfoLength;
			mapBytes.resize(tileOffset + 16 * 16 * MapV3ContractFixture::TileLength);
			std::fill(mapBytes.begin() + MapV3ContractFixture::BaseHeaderLength, mapBytes.end(), std::uint8_t{0});
			MapV3ContractFixture::writeInt32(mapBytes, 64, 16 * 16 * MapV3ContractFixture::TileLength);
			MapV3ContractFixture::writeInt32(mapBytes, 68, 16);
			MapV3ContractFixture::writeInt32(mapBytes, 72, 16);
			if (!check(writeVirtualFile("map/equipment-save.map", std::string(reinterpret_cast<const char*>(mapBytes.data()), mapBytes.size()))
				&& writeVirtualFile("save/game/game.ini", "[State]\nMap=equipment-save.map\nNpc=\nObj=\n"),
				"create a file-backed lane for complete equipment saves")) return false;
			GameManager gameManager;
			gameManager.global.applyResourceManifestFeatures(resources.getActiveManifest());
			gameManager.global.loadUiSettings();
			gameManager.goodsManager.configureLayout();
			gameManager.magicManager.configureLayout();
			if (!check(gameManager.player->loadInitialTemplate(0) && gameManager.scriptAPI.loadMap("equipment-save.map", false),
				"load the real player template and full-load map")) return false;
			gameManager.global.data.characterIndex = 0;
			gameManager.global.data.NPCAI = false;
			gameManager.global.data.canInput = false;
			gameManager.varList.ensureInitialized();
			auto player = gameManager.player;
			player->lifeMax = player->life = 5000;
			player->thewMax = player->thew = 1000;
			player->manaMax = player->mana = 1000;
			player->setPosition({ 8, 8 }, false);
			player->calInfo();
			// Yuchen's initial list does not teach this optional file. Explicitly
			// learn it to test equipment semantics, not story availability.
			gameManager.magicManager.addMagic(sourceMagic);
			if (!check(gameManager.goodsManager.addItem(weapon, 1), "add the complete production weapon through the goods loader")) return false;
			const int bagIndex = gameManager.goodsManager.storeBegin();
			const int equipmentIndex = gameManager.goodsManager.equipIndex(NPC::getEquipmentPartIndex("Hand"));
			const auto goods = gameManager.goodsManager.goodsList[bagIndex].goods;
			const std::string instanceFile = gameManager.goodsManager.goodsList[bagIndex].iniFile;
			if (!check(goods != nullptr && goods->replaceMagic == sourceMagic && !goods->useReplaceMagic.empty(),
				"the actual weapon supplies its original source and replacement filenames")) return false;
			const std::string replacement = goods->useReplaceMagic;
			const auto setEquipped = [&](bool shouldEquip)
			{
				if (gameManager.goodsManager.goodsListExists(equipmentIndex) == shouldEquip) return true;
				std::string message;
				const bool changed = shouldEquip ? gameManager.goodsManager.useItem(bagIndex)
					: gameManager.goodsManager.unequipToFirstStoreSlot(equipmentIndex, &message);
				if (!changed) std::cerr << "Equipment change failed: " << message << std::endl;
				return changed;
			};
			if (!check(setEquipped(equipped), "equip or leave the weapon in the bag through production item use")) return false;
			auto target = std::make_shared<NPC>();
			target->npcName = "EquipmentSaveTarget";
			target->kind = nkBattle;
			target->relation = nrHostile;
			target->isAIDisabled = true;
			target->lifeMax = target->life = 1000000;
			target->setPosition(Map::getSubPoint(player->getPosition(), 0), false);
			gameManager.npcManager->addNPC(target);
			gameManager.map->createDataMap();
			const auto cast = [&](const std::string& expectedFile, bool release)
			{
				gameManager.effectManager->freeResource();
				player->actionManager->restartActionIgnoringTransitions(acStand);
				gameManager.scriptAPI.useMagic(sourceMagic, target->getPosition().x, target->getPosition().y, true);
				if (!check(player->preparedMagicAction != nullptr && player->preparedMagicAction->iniName == expectedFile
					&& player->preparedMagicActionSource != nullptr && player->preparedMagicActionSource->iniName == sourceMagic,
					"real spell admission resolves equipment without changing the learned source magic")) return false;
				if (!release)
				{
					CoreLifecycleTestAccess::advanceActorFrame(*player, player->actionLastTime / 2);
					return check(gameManager.effectManager->effectList.empty(), "save during preparation before the replacement effect is released");
				}
				CoreLifecycleTestAccess::advanceActorFrame(*player, player->actionLastTime + 1);
				const auto effects = gameManager.effectManager->effectList;
				if (!check(!effects.empty() && std::all_of(effects.begin(), effects.end(), [&](const auto& effect)
					{ return effect != nullptr && effect->fileName == expectedFile && effect->user.lock() == player; }),
					"the production spell releases the resolved magic, not merely a matching preparation field")) return false;
				const int initialLife = target->life;
				const int specialKind = effects.front()->magic.level[effects.front()->level].specialKind;
				for (int elapsed = 0; elapsed < 2000 && !gameManager.effectManager->effectList.empty(); elapsed += 20)
				{
					CoreLifecycleTestAccess::beginElementFrame(*gameManager.effectManager);
					const auto activeEffects = gameManager.effectManager->effectList;
					for (const auto& effect : activeEffects) CoreLifecycleTestAccess::advanceActorFrame(*effect, 20);
					CoreLifecycleTestAccess::advanceActorFrame(*gameManager.effectManager, 20);
				}
				std::cout << "Equipment hit:\tpack=" << id << "\tweapon=" << weapon << "\tsavedEquipped=" << equipped
					<< "\tasync=" << asynchronous << "\tmagic=" << expectedFile << "\tdamage=" << initialLife - target->life
					<< "\tspecial=" << specialKind << "\tfrozen=" << target->frozen << "\tpoisoned=" << target->poisoned
					<< "\tpetrified=" << target->petrified << std::endl;
				return check(target->life < initialLife && (specialKind != 1 || target->frozen)
					&& (specialKind != 2 || target->poisoned) && (specialKind != 3 || target->petrified),
					"replacement damage and configured frozen, poison or petrified status reach the actual target");
			};
			const std::string expectedFile = equipped ? replacement : sourceMagic;
			if (!cast(expectedFile, false)) return false;
			const int savedThew = player->thew;
			const int savedLife = player->life;
			const int savedAttack = player->getAttack();
			if (!check(gameManager.saveGame(1), "publish the complete slot with learned magic, inventory and equipment")) return false;
			if (!check(setEquipped(!equipped), "change outgoing equipment before restoring the slot")) return false;
			const bool loaded = asynchronous ? gameManager.scriptAPI.loadGameAsync(1) : gameManager.loadGame(1);
			if (!check(loaded && gameManager.npcManager->npcList.size() == 1, "both full loaders restore the equipment scenario")) return false;
			const auto originalTarget = target;
			target = gameManager.npcManager->npcList.front();
			std::cout << "Equipment readback:\tpack=" << id << "\tweapon=" << weapon << "\tequipped=" << equipped
				<< "\tasync=" << asynchronous << "\tstanding=" << player->isStanding()
				<< "\tprepared=" << (player->preparedMagicAction != nullptr) << "\teffects=" << gameManager.effectManager->effectList.size()
				<< "\tlife=" << player->life << "/" << savedLife << "\tthew=" << player->thew << "/" << savedThew
				<< "\tattack=" << player->getAttack() << "/" << savedAttack << std::endl;
			ok = check(target != originalTarget && player->isStanding() && player->preparedMagicAction == nullptr
				&& gameManager.effectManager->effectList.empty() && player->thew == savedThew
				&& player->life == savedLife && player->getAttack() == savedAttack,
				"reload cancels the old cast and restores saved attributes without outgoing equipment contamination") && ok;
			const int savedIndex = equipped ? equipmentIndex : bagIndex;
			const auto& restoredGoods = gameManager.goodsManager.goodsList[savedIndex];
			const auto learned = gameManager.magicManager.findMagic(sourceMagic);
			// Random-attribute goods are saved under their generated instance file,
			// not the template name. Check the rolled values as well as the mapping.
			ok = check(restoredGoods.iniFile == instanceFile && restoredGoods.number == 1 && restoredGoods.goods != nullptr
				&& restoredGoods.goods->attack == goods->attack && restoredGoods.goods->lifeMax == goods->lifeMax
				&& restoredGoods.goods->effectType == goods->effectType && !restoredGoods.goods->hasRandomAttributes()
				&& restoredGoods.goods->replaceMagic == sourceMagic && restoredGoods.goods->useReplaceMagic == replacement
				&& learned != nullptr && learned->iniFile == sourceMagic && learned->level == 1
				&& gameManager.magicManager.findMagic(replacement) == nullptr,
				"readback keeps the weapon and original learned magic; replacement is not inserted into the magic list") && ok;
			ok = cast(expectedFile, true) && ok;
			if (!check(setEquipped(!equipped), "the restored weapon can change slots again")) return false;
			target->clearFrozenState();
			target->clearPoisonedState();
			target->clearPetrifiedState();
			ok = cast(equipped ? sourceMagic : replacement, true) && ok;
			std::cout << "Equipment replacement save:\tpack=" << id << "\tweapon=" << weapon << "\tequipped=" << equipped
				<< "\tasync=" << asynchronous << "\treplacement=" << replacement << "\tattack=" << savedAttack << std::endl;
		}
	}
	return ok;
}

bool runDynamicMagicListSaveRuntimeTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	auto& resources = ResourceManager::instance();
	if (!check(resources.initialize(assetsRoot.u8string()), "dynamic lists discover actual resource profiles")) return false;
	struct Fixture { const char* id; const char* file; const char* section; };
	const Fixture fixtures[] = {
		{ "YYCS", "ini/save/map006_1.npc", "NPC007" },
		{ "XJXQY", u8"ini/npc/npc011_方勉.ini", "Init" },
		{ "JIANGHU_YUCHEN_2", "ini/save/map016.npc", "NPC002" }
	};
	bool ok = true;
	for (const auto& fixture : fixtures)
	{
		if (!check(resources.setActiveResourcePackById(fixture.id), "select the actual dynamic-list profile")) return false;
		const auto packRoot = resources.getActiveResourceRoot();
		for (bool asynchronous : { false, true })
		{
			ScopedActiveResourceRoot resourceRoot;
			if (!check(resourceRoot.valid(), "dynamic lists isolate mutable resources and saves")) return false;
			std::vector<std::string> roots{ packRoot, (assetsRoot / "xjxqy_test_mod").u8string(),
				(assetsRoot / "yycs").u8string(), (assetsRoot / "common").u8string() };
			File::setResourceFallbackRoots(roots);
			File::setUiResourceFallbackRoots(roots, true, (assetsRoot / "common").u8string());
			auto mapBytes = MapV3ContractFixture::build();
			constexpr size_t tileOffset = MapV3ContractFixture::HeaderLength
				+ MapV3ContractFixture::MpcCount * MapV3ContractFixture::InfoLength;
			mapBytes.resize(tileOffset + 16 * 16 * MapV3ContractFixture::TileLength);
			std::fill(mapBytes.begin() + MapV3ContractFixture::BaseHeaderLength, mapBytes.end(), std::uint8_t{0});
			MapV3ContractFixture::writeInt32(mapBytes, 64, 16 * 16 * MapV3ContractFixture::TileLength);
			MapV3ContractFixture::writeInt32(mapBytes, 68, 16);
			MapV3ContractFixture::writeInt32(mapBytes, 72, 16);
			if (!check(writeVirtualFile("map/dynamic-list.map", std::string(reinterpret_cast<const char*>(mapBytes.data()), mapBytes.size()))
				&& writeVirtualFile("save/game/game.ini", "[State]\nMap=dynamic-list.map\nNpc=\nObj=\n"),
				"create the dynamic-list full-load map")) return false;
			GameManager gameManager;
			gameManager.global.applyResourceManifestFeatures(resources.getActiveManifest());
			gameManager.global.loadUiSettings();
			gameManager.goodsManager.configureLayout();
			gameManager.magicManager.configureLayout();
			std::unique_ptr<char[]> bytes;
			if (!check(gameManager.player->loadInitialTemplate(0) && File::readFile(fixture.file, bytes) > 0
				&& gameManager.scriptAPI.loadMap("dynamic-list.map", false), "load real actors and the full-load map")) return false;
			gameManager.global.data.characterIndex = 0;
			gameManager.global.data.NPCAI = false;
			gameManager.global.data.canInput = false;
			gameManager.varList.ensureInitialized();
			INIReader definition(bytes);
			auto npc = std::make_shared<NPC>();
			npc->initFromIni(&definition, fixture.section);
			npc->npcName = "DynamicListActor";
			npc->relation = nrHostile;
			npc->isAIDisabled = true;
			npc->setPosition({ 8, 8 }, false);
			gameManager.npcManager->addNPC(npc);
			auto player = gameManager.player;
			player->setPosition(Map::getSubPoint(npc->getPosition(), 0), false);
			gameManager.map->createDataMap();
			if (!check(!npc->attackOptions.empty(), "the actual NPC provides a usable attack file")) return false;
			const std::string magicFile = npc->attackOptions.front().magic->iniName;
			const std::string originalExtra = npc->flyInis;
			const auto execute = [&](const std::string& source)
			{
				auto scriptBytes = std::make_unique<char[]>(source.size());
				std::copy(source.begin(), source.end(), scriptBytes.get());
				return gameManager.script.runScript(scriptBytes, static_cast<int>(source.size()));
			};
			const auto options = [](const std::shared_ptr<NPC>& actor)
			{
				std::vector<std::string> result;
				for (const auto& option : actor->attackOptions)
				{
					result.push_back(option.magic->iniName + ":" + std::to_string(option.configuredUseDistance)
						+ ":" + std::to_string(option.useAdditionalEffect));
				}
				return result;
			};
			if (!check(execute("changeflyini('DynamicListActor','" + magicFile + "'); changeflyini2('DynamicListActor','"
				+ magicFile + "'); addflyinis('DynamicListActor','" + magicFile + "',3); addflyinis('DynamicListActor','"
				+ magicFile + "',7); addnpcmagic('DynamicListActor','" + magicFile + "');") == LUA_OK,
				"real Lua dispatch updates primary, secondary and duplicate-distance NPC entries")) return false;
			std::string expectedExtra = originalExtra;
			if (!expectedExtra.empty() && expectedExtra.back() != ';') expectedExtra += ';';
			expectedExtra += magicFile + ":3;" + magicFile + ":7;";
			ok = check(npc->flyIni == magicFile && npc->flyIni2 == magicFile && npc->flyInis == expectedExtra,
				"AddFlyInis keeps duplicates and AddNpcMagic does not duplicate an already configured magic") && ok;
			Magic npcMorph;
			npcMorph.replaceMagic = magicFile + ":1;";
			npc->applyTemporaryMorph(npcMorph, 900);
			if (!check(execute("addflyinis('DynamicListActor','" + magicFile + "',5);") == LUA_OK,
				"append a persistent NPC entry while a temporary list is active")) return false;
			expectedExtra += magicFile + ":5;";
			ok = check(npc->attackOptions.size() == 1 && npc->attackOptions.front().configuredUseDistance == 1
				&& npc->flyInis == expectedExtra, "temporary replacement masks the changed base list until expiry") && ok;

			const std::string primaryFile = player->flyIni;
			auto* primary = gameManager.magicManager.addPrimaryMagic(primaryFile, false, false);
			if (!check(primary != nullptr, "learn the actual player attack as a distinct primary-list control")) return false;
			primary->level = 4;
			primary->exp = 37;
			Magic playerMorph;
			playerMorph.initFromIni("mod_test_magic_morph_replace.ini");
			if (!check(playerMorph.loadSucceeded, "load the existing explicit MOD test transformation fixture")) return false;
			const std::string firstList = playerMorph.replaceMagic;
			const std::string secondList = "mod_test_magic_round.ini;mod_test_magic_begin_follow.ini";
			for (int index = 0; index < 2; ++index)
			{
				playerMorph.replaceMagic = index == 0 ? firstList : secondList;
				player->applyTemporaryMorph(playerMorph, 900);
				auto& info = gameManager.magicManager.magicList[gameManager.magicManager.bottomBegin()];
				if (!check(info.magic != nullptr && info.magic->loadSucceeded, "replacement toolbar uses real test magic files")) return false;
				info.level = index == 0 ? 3 : 5;
				info.exp = index == 0 ? 79 : 131;
				info.hideCount = 2;
				info.lastIndexWhenHide = 9;
				info.remainColdMilliseconds = 7300;
			}
			if (!check(gameManager.saveGame(1), "publish the full save with both cached and currently active replacement lists")) return false;
			npc->updateMagicRuntimeStateTimers(899);
			player->updateMagicRuntimeStateTimers(899);
			ok = check(npc->attackOptions.size() == 1 && gameManager.magicManager.hasActiveReplaceMagicList(),
				"neither temporary list expires a millisecond early") && ok;
			npc->updateMagicRuntimeStateTimers(1);
			player->updateMagicRuntimeStateTimers(1);
			const auto liveOptions = options(npc);
			ok = check(npc->morphMilliseconds == 0 && !gameManager.magicManager.hasActiveReplaceMagicList()
				&& liveOptions.size() >= 5 && npc->flyInis == expectedExtra,
				"expiry restores primary player skills and all current NPC entries, including changes made during morph") && ok;
			const auto oldNpc = npc;
			if (!check(asynchronous ? gameManager.scriptAPI.loadGameAsync(1) : gameManager.loadGame(1),
				"synchronous and asynchronous full loaders restore the dynamic-list slot")) return false;
			npc = gameManager.npcManager->npcList.front();
			primary = gameManager.magicManager.findPrimaryMagic(primaryFile);
			ok = check(npc != oldNpc && npc->flyIni == magicFile && npc->flyIni2 == magicFile && npc->flyInis == expectedExtra
				&& options(npc) == liveOptions && npc->morphMilliseconds == 0 && player->morphMilliseconds == 0
				&& !gameManager.magicManager.hasActiveReplaceMagicList() && primary != nullptr && primary->level == 4 && primary->exp == 37,
				"readback cancels temporary forms but retains script edits and the independent learned player list") && ok;
			for (int index = 0; index < 2; ++index)
			{
				playerMorph.replaceMagic = index == 0 ? firstList : secondList;
				player->applyTemporaryMorph(playerMorph, 900);
				const auto& info = gameManager.magicManager.magicList[gameManager.magicManager.bottomBegin()];
				std::cout << "Replacement cache readback:\tpack=" << fixture.id << "\tasync=" << asynchronous << "\tlist=" << index
					<< "\tlevel=" << info.level << "\texp=" << info.exp << "\thideCount=" << info.hideCount
					<< "\tlastIndex=" << info.lastIndexWhenHide << "\tcooldown=" << info.remainColdMilliseconds << std::endl;
				ok = check(info.level == (index == 0 ? 3 : 5) && info.exp == (index == 0 ? 79 : 131)
					&& info.hideCount == 2 && info.lastIndexWhenHide == 9 && info.remainColdMilliseconds == 0,
					"re-entering either transformation restores saved progress and resets only its transient cooldown") && ok;
			}
			player->clearMagicRuntimeStates();
			npc->beginAttack(player->getPosition(), player);
			if (!check(npc->hasPreparedAttackMagic && npc->preparedAttackMagic != nullptr,
				"the rebuilt dynamic list admits a real attack after loading")) return false;
			const auto selectedFile = npc->preparedAttackMagic->iniName;
			CoreLifecycleTestAccess::advanceActorFrame(*npc, npc->actionLastTime + 1);
			ok = check(!gameManager.effectManager->effectList.empty()
				&& gameManager.effectManager->effectList.front()->fileName == selectedFile,
				"the reloaded NPC releases its prepared dynamic-list attack") && ok;
			std::cout << "Dynamic list save:\tpack=" << fixture.id << "\tasync=" << asynchronous
				<< "\toptions=" << liveOptions.size() << "\tselected=" << selectedFile << std::endl;

			// Reuse the complete slot/real-profile fixture for two sources of one list.
			Magic firstSource = playerMorph;
			firstSource.name += "-source-one";
			firstSource.replaceMagic = firstList;
			Magic secondSource = firstSource;
			secondSource.name += "-source-two";
			const int toolbar = gameManager.magicManager.bottomBegin();
			std::shared_ptr<Magic> learnedSources[2];
			for (int source = 0; source < 2; ++source)
			{
				player->applyTemporaryMorph(source == 0 ? firstSource : secondSource, 900);
				auto& info = gameManager.magicManager.magicList[toolbar];
				if (!check(info.magic != nullptr, "same-list source fixture loads its real test magic")) return false;
				info.level = source == 0 ? 2 : 4;
				info.exp = source == 0 ? 83 : 167;
				learnedSources[source] = info.magic;
			}
			ok = check(learnedSources[0] != learnedSources[1],
				"two source forms in the full-save scene own separate learned objects") && ok;
			if (!check(gameManager.saveGame(2), "publish a full slot with two sources using the same replacement list")) return false;
			player->clearMagicRuntimeStates();
			if (!check(asynchronous ? gameManager.scriptAPI.loadGameAsync(2) : gameManager.loadGame(2),
				"both complete loaders restore source-identified replacement caches")) return false;
			ok = check(!gameManager.magicManager.hasActiveReplaceMagicList() && player->morphMilliseconds == 0,
				"source identity persistence does not restore transient transformation state") && ok;
			for (int source = 0; source < 2; ++source)
			{
				player->applyTemporaryMorph(source == 0 ? firstSource : secondSource, 900);
				const auto& info = gameManager.magicManager.magicList[toolbar];
				ok = check(info.level == (source == 0 ? 2 : 4) && info.exp == (source == 0 ? 83 : 167)
					&& info.magic != learnedSources[source],
					"complete readback reconstructs each source with only its saved progress") && ok;
			}
			player->clearMagicRuntimeStates();
			std::cout << "Replacement source full save:\tpack=" << fixture.id << "\tasync=" << asynchronous
				<< "\tsources=2" << std::endl;

			auto& manager = gameManager.magicManager;
			primary = manager.findPrimaryMagic(primaryFile);
			if (!check(primary != nullptr, "the selection fixture retains the primary learned skill")) return false;
			if (!check(primary->magic->level[primary->level].levelupExp == 0,
				"the real ordinary attack is an unlearnable experience control")) return false;
			gameManager.scriptAPI.addMagicExp(primaryFile, 25);
			ok = check(primary->exp == 62 && primary->level == 4,
				"the real zero-threshold ordinary attack accumulates experience without leveling") && ok;
			// Selection ownership needs eligible skills in both lists, including after file reload.
			const std::string selectionPrimaryFile = "selection-primary-experience.ini";
			if (!check(writeVirtualFile("ini/magic/" + selectionPrimaryFile,
				"[Init]\nName=SelectionPrimary\nMoveKind=2\nLevelupExp=1000\n")
				&& writeVirtualFile("ini/magic/selection-form-experience.ini",
					"[Init]\nName=SelectionForm\nMoveKind=2\nLevelupExp=1000\n"),
				"write isolated positive-threshold selection controls")) return false;
			primary = manager.addPrimaryMagic(selectionPrimaryFile, false, false);
			if (!check(primary != nullptr, "learn the eligible primary selection control")) return false;
			primary->level = 4;
			primary->exp = 37;
			firstSource.replaceMagic = "selection-form-experience.ini";
			const int primarySlot = static_cast<int>(primary - manager.magicList.data());
			manager.exchange(primarySlot, toolbar);
			manager.recordCurrentUseMagic(toolbar);
			player->applyTemporaryMorph(firstSource, 900);
			if (!check(manager.magicList[toolbar].magic != nullptr
				&& manager.magicList[toolbar].magic->level[1].levelupExp > 0,
				"the selected form control can actually receive experience")) return false;
			const int formExperience = manager.magicList[toolbar].exp;
			manager.addKillExp(nullptr, 40.0, 0.0f, 0.25f);
			ok = check(manager.magicList[toolbar].exp == formExperience + 10,
				"current-use callback binds the original toolbar slot to the active form") && ok;
			if (!check(gameManager.saveGame(3), "publish the complete world with a selected active-form skill")) return false;
			INIReader selectedSave("save/rpg3/magic0.ini");
			ok = check(selectedSave.GetInteger("Head", "CurrentUseMagicIndex", -1) == toolbar + 1
				&& selectedSave.Get("Head", "CurrentUseMagicFile", "") == selectionPrimaryFile,
				"the full generation projects the form selection to the saved primary toolbar slot") && ok;
			manager.addKillExp(nullptr, 40.0, 0.0f, 0.25f);
			ok = check(manager.magicList[toolbar].exp == formExperience + 20,
				"complete saving leaves the live form's selected learned object unchanged") && ok;
			if (!check(asynchronous ? gameManager.scriptAPI.loadGameAsync(3) : gameManager.loadGame(3),
				"both complete loaders restore the selected primary slot")) return false;
			manager.addKillExp(nullptr, 40.0, 0.0f, 0.25f);
			ok = check(!manager.hasActiveReplaceMagicList() && manager.magicList[toolbar].iniFile == selectionPrimaryFile
				&& manager.magicList[toolbar].exp == 47,
				"full readback cancels the form and credits the saved primary selection only") && ok;
			player->applyTemporaryMorph(firstSource, 900);
			ok = check(manager.magicList[toolbar].exp == formExperience + 10,
				"primary experience after readback leaves the independently saved form cache intact") && ok;
			player->clearMagicRuntimeStates();
			std::cout << "Replacement selection full save:\tpack=" << fixture.id << "\tasync=" << asynchronous
				<< "\tslot=" << toolbar + 1 << "\tcallbackOnly=1" << std::endl;
		}
	}
	return ok;
}

bool runProductionTalentJumpTests(ResourceManager& resources)
{
	if (!check(resources.setActiveResourcePackById("JIANGHU_YUCHEN_1_03"), "talent jumps select the real Yuchen resource chain")) return false;
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	const auto packRoot = resources.getActiveResourceRoot();
	ScopedActiveResourceRoot isolatedRoot;
	if (!check(isolatedRoot.valid(), "talent jump tests isolate definitions and saves")) return false;
	File::setResourceFallbackRoots({ packRoot, (assetsRoot / "yycs").u8string(), (assetsRoot / "common").u8string() });
	SaveFileManager::CurrentPathScope currentPath("save\\talent_jump");
	if (!check(currentPath.valid(), "talent jumps isolate their character files")) return false;
	std::ifstream input(std::filesystem::path(__FILE__).parent_path() / "fixtures" / std::filesystem::u8path(u8"mg-player-talent-轻功.ini"), std::ios::binary);
	const std::string definition((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	const std::string file = u8"mg-player-talent-轻功.ini";
	if (!check(!definition.empty() && writeVirtualFile("ini/magic/" + file, definition), "use the exact converted MG lightweight-skill definition")) return false;
	bool ok = true;
	for (bool enabled : { false, true })
	{
		GameManager game;
		ResourceManifest manifest = resources.getActiveManifest();
		manifest.features["separatetalentslots"] = enabled;
		game.global.applyResourceManifestFeatures(manifest);
		game.global.loadUiSettings();
		auto& manager = game.magicManager;
		manager.configureLayout();
		game.varList.ensureInitialized();
		game.map->data = std::make_shared<MapData>();
		game.map->data->head.width = 160;
		game.map->data->head.height = 160;
		game.map->data->tile.assign(160, std::vector<MapTile>(160));
		game.map->createDataMap();
		// Independent transcription of MG's neighbour/angle calculation; the
		// production code uses the existing C++ direction and neighbour helpers.
		const auto publishedDestination = [](Point from, Point destination, int radius)
		{
			if (radius <= 0) return destination;
			Point result = from;
			for (int i = 0; i <= radius && result != destination; ++i)
			{
				const float x = static_cast<float>(destination.x - result.x);
				const float y = static_cast<float>(destination.y - result.y);
				double angle = std::acos(y / std::sqrt(x * x + y * y));
				if (x > 0) angle = 2 * M_PI - angle;
				int direction = static_cast<int>(angle / (M_PI / 8));
				if (direction % 2 != 0) ++direction;
				direction = (direction % 16) / 2;
				const int parity = result.y % 2;
				const Point neighbours[] = {{0, 2}, {parity - 1, 1}, {-1, 0}, {parity - 1, -1},
					{0, -2}, {parity, -1}, {1, 0}, {parity, 1}};
				result.x += neighbours[direction].x;
				result.y += neighbours[direction].y;
			}
			return result;
		};
		int geometryCases = 0;
		for (Point from : { Point{64, 64}, Point{64, 65} })
		{
			for (Point delta : { Point{40, 0}, Point{-40, 0}, Point{0, 40}, Point{0, -40},
				Point{40, 40}, Point{-40, 40}, Point{40, -40}, Point{-40, -40}, Point{0, 1}, Point{1, 1}, Point{0, 0} })
			{
				const Point to{from.x + delta.x, from.y + delta.y};
				for (int value : { -5, 0, 1, 2, 5, 15, 65 })
				{
					const Point expected = publishedDestination(from, to, value);
					ok = check(game.map->getJumpPath(from, to, value) == expected,
						"odd/even row jump limits match the published neighbour calculation") && ok;
					++geometryCases;
				}
			}
		}
		ok = check(game.map->getJumpPath({64, 64}, {64, 65}, INT_MAX) == Point{64, 64}
			&& game.map->getJumpPath({64, 64}, {104, 64}, INT_MAX) == Point{104, 64},
			"an extreme radius finishes both a two-tile cycle and a reachable target without overflow") && ok;
		game.map->data->tile[20][23].obstacle = toObstacle;
		ok = check(game.map->getJumpPath({20, 20}, {120, 20}, 5) == Point{22, 20}, "radius clipping still respects solid barriers") && ok;
		game.map->data->tile[20][23].obstacle = 0;
		game.map->data->tile[20][23].trap = 2;
		game.mapFolderName = "talent-jump-test";
		game.traps.set(game.mapFolderName, 2, "jump-trap.txt");
		ok = check(game.map->getJumpPath({20, 20}, {120, 20}, 5) == Point{23, 20}, "radius clipping still stops at an active trap") && ok;
		game.traps.markTriggered(2);
		ok = check(game.map->getJumpPath({20, 20}, {120, 20}, 5) == Point{26, 20}, "a consumed trap keeps the existing pass-through behavior") && ok;
		game.map->data->tile[20][23].trap = 0;
		const auto radius = [](NPC& actor)
		{
			INIReader saved;
			actor.saveToIni(&saved, "Init");
			return saved.GetInteger("Init", "JumpRadius", 0);
		};
		const auto initializeActions = [](NPC& actor)
		{
			actor.kind = nkPlayer;
			actor.life = actor.lifeMax = actor.thew = actor.thewMax = 100;
			NPCActionRes action;
			action.imagePackage = std::make_shared<IMPImage>();
			action.imagePackage->directions = 8;
			action.imagePackage->interval = 100;
			action.imagePackage->frame.resize(24);
			actor.res.stand = actor.res.jump = action;
		};
		const auto jump = [&](NPC& actor, Point start, Point destination, Point expected)
		{
			initializeActions(actor);
			actor.setPosition(start, false);
			actor.beginStand();
			game.map->createDataMap();
			actor.beginJump(destination);
			bool passed = check(actor.isJumping() && actor.stepList.size() == 1 && actor.stepList[0] == expected,
				"talent radius reaches the real jump action's reserved landing tile");
			for (int frame = 0; frame < 200 && actor.isJumping(); ++frame)
			{
				CoreLifecycleTestAccess::advanceActorFrame(actor, 50);
			}
			return check(actor.isStanding() && actor.getPosition() == expected,
				"the real jump animation completes on the radius-limited landing tile") && passed;
		};
		game.scriptAPI.addTalent(file);
		ok = check(radius(*game.player) == (enabled ? 5 : 0), "only separate talent learning awards the first level's jump radius") && ok;
		game.scriptAPI.addTalent(file);
		game.player->calInfo();
		ok = check(radius(*game.player) == (enabled ? 5 : 0), "duplicate learning and attribute refresh do not award jump radius again") && ok;
		ok = jump(*game.player, {20, 20}, {120, 20}, {enabled ? 26 : 120, 20}) && ok;
		ok = check(game.player->thew == 90, "a talent jump retains the existing ten-point stamina charge") && ok;
		game.scriptAPI.addMagicExp(file, 300);
		ok = check(radius(*game.player) == (enabled ? 15 : 0), "reaching level two adds ten to the already learned five") && ok;
		ok = jump(*game.player, {20, 20}, {120, 20}, {enabled ? 36 : 120, 20}) && ok;
		if (!check(manager.setMagicHidden(file, true, true, false) != nullptr, "hide the learned lightweight talent")) return false;
		ok = check(radius(*game.player) == (enabled ? 15 : 0), "hiding a talent does not remove permanent gains") && ok;
		if (!check(manager.setMagicHidden(file, false, true, false) != nullptr, "show the same lightweight talent")) return false;
		manager.replaceMagicList(u8"player-magic-清心咒.ini");
		game.scriptAPI.addMagicExp(file, 700);
		ok = check(radius(*game.player) == (enabled ? 65 : 0) && manager.findPrimaryMagic(file)->level == 3,
			"continuous upgrades during a form award the primary character's new talent level") && ok;
		if (!check(game.player->save(0) && manager.save(0), "write player gains and primary talent progress while transformed")) return false;
		const auto oldPlayer = game.player;
		game.player = std::make_shared<Player>();
		manager.clearMagicList();
		if (!check(game.player->load(0) && manager.load(0), "read the actual player and talent files into fresh instances")) return false;
		ok = check(game.player != oldPlayer && radius(*game.player) == (enabled ? 65 : 0)
			&& manager.findPrimaryMagic(file)->level == 3, "fresh player file readback restores the accumulated jump radius exactly once") && ok;
		ok = jump(*game.player, {20, 20}, {120, 20}, {enabled ? 86 : 120, 20}) && ok;
		if (!check(game.player->load(0) && manager.load(0), "repeat the same component readback")) return false;
		game.scriptAPI.addMagicExp(file, INT_MAX);
		ok = check(radius(*game.player) == (enabled ? 65 : 0), "readback and terminal experience never repeat the awarded radius") && ok;
		manager.deletePrimaryMagic(file);
		ok = check(radius(*game.player) == (enabled ? 65 : 0), "deleting the skill retains its already awarded permanent gain") && ok;
		if (!check(manager.addPrimaryMagic(file, false, true) != nullptr, "learn the same definition as an ordinary skill")) return false;
		game.scriptAPI.addMagicExp(file, 1000);
		ok = check(radius(*game.player) == (enabled ? 65 : 0), "an ordinary copy of a talent definition cannot award talent-only radius") && ok;
		INIReader bare;
		bare.Set("Init", "Name", "fresh actor");
		game.player->initFromIni(&bare, "Init");
		ok = check(radius(*game.player) == 0, "a legacy actor without JumpRadius resets the previous character's gain") && ok;
		bare.SetInteger("Init", "JumpRadius", 5);
		auto npc = std::make_shared<NPC>();
		npc->initFromIni(&bare, "Init");
		game.npcManager->npcList.push_back(npc);
		game.player->setPosition({90, 90}, false);
		ok = jump(*npc, {20, 20}, {120, 20}, {26, 20}) && ok;
		ok = check(radius(*npc) == 5 && radius(*game.player) == 0, "NPC jumping uses its own saved radius and origin, not the player's") && ok;
		INIReader npcSave;
		npc->saveToIni(&npcSave, "Init");
		const std::string npcFile = SaveFileManager::CurrentPath() + "jump-npc.ini";
		if (!check(npcSave.saveToFile(npcFile), "persist the independent actor's actual saved radius")) return false;
		std::unique_ptr<char[]> npcBytes;
		int npcLength = 0;
		if (!check(File::readFile(npcFile, npcBytes, npcLength) && npcLength > 0, "read the actual actor file")) return false;
		INIReader npcRead(npcBytes);
		auto restoredNpc = std::make_shared<NPC>();
		restoredNpc->initFromIni(&npcRead, "Init");
		game.npcManager->npcList.push_back(restoredNpc);
		ok = check(radius(*restoredNpc) == 5, "a new NPC restores its own radius from disk") && ok;
		ok = jump(*restoredNpc, {20, 20}, {20, 120}, {20, 32}) && ok;
		if (enabled)
		{
			manager.clearMagicList();
			game.scriptAPI.addTalent(file);
			auto* learned = manager.findPrimaryMagic(file);
			if (!check(learned != nullptr, "learn a fresh talent for hidden in-flight experience")) return false;
			auto copied = std::make_shared<Magic>();
			copied->copy(*learned->magic);
			copied->experienceOwner = {true, copied};
			learned->magic = copied;
			ok = check(copied->level[1].jumpRadius == 5 && copied->level[2].jumpRadius == 10
				&& copied->level[3].jumpRadius == 50 && copied->level[4].jumpRadius == 0,
				"the exact three-level definition and its copy preserve per-level radius without forward inheritance") && ok;
			auto effect = std::make_shared<Effect>();
			effect->magicDispatchContext = Magic::createRootDispatchContext(copied);
			if (!check(manager.setMagicHidden(file, true, false, false) != nullptr, "hide the in-flight source after capturing its owner")) return false;
			manager.addUseExp(effect, 1000);
			ok = check(radius(*game.player) == 65, "one hidden-source award crosses both levels and adds each radius exactly once") && ok;
			if (!check(manager.setMagicHidden(file, false, false, false) != nullptr, "restore the hidden talent after its award")) return false;
			manager.clearMagicList();
			game.player->jumpRadius = INT_MAX - 1;
			game.scriptAPI.addTalent(file);
			ok = check(radius(*game.player) == INT_MAX, "talent radius accumulation saturates safely at the integer upper bound") && ok;
			game.scriptAPI.addMagicExp(file, 1000);
			ok = check(radius(*game.player) == INT_MAX, "continuous upgrades at the radius bound do not wrap negative") && ok;
			if (!check(writeVirtualFile("ini/magic/jump-defaults.ini",
				"[Init]\nName=defaults\nJumpRadius=2\n[Level1]\nJumpRadius=5\n[Level2]\nLevelupExp=1000\n[Level3]\nJumpRadius=-7\n"),
				"write explicit Init fallback and signed-radius compatibility cases")) return false;
			copied->initFromIni("jump-defaults.ini");
			ok = check(copied->level[1].jumpRadius == 5 && copied->level[2].jumpRadius == 2 && copied->level[3].jumpRadius == -7,
				"radius defaults to Init and preserves explicit signed values without rejecting the definition") && ok;
			copied->reset();
			ok = check(copied->level[1].jumpRadius == 0 && copied->level[3].jumpRadius == 0, "reusing a Magic resets all radius fields") && ok;
		}
		std::cout << "Production talent jump:\tenabled=" << enabled << "\tplayerFileRoundtrips=2\tnpcFileRoundtrips=1\tactualJumps=5\tgeometryCases="
			<< geometryCases << "\tpassed=" << ok << std::endl;
	}
	return ok;
}

bool runProductionYuchenLearningLimitTests(ResourceManager& resources)
{
	if (!check(resources.setActiveResourcePackById("JIANGHU_YUCHEN_1_03"),
		"learning review selects the actual Yuchen profile")) return false;
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	const auto packRoot = resources.getActiveResourceRoot();
	ScopedActiveResourceRoot isolatedRoot;
	if (!check(isolatedRoot.valid(), "learning review isolates all writable files")) return false;
	File::setResourceFallbackRoots({ packRoot, (assetsRoot / "yycs").u8string(), (assetsRoot / "common").u8string() });
	SaveFileManager::CurrentPathScope currentPath("save\\yuchen_learning_review");
	if (!check(currentPath.valid(), "learning review selects its private character files")) return false;
	INIReader initial("ini/save/magic0.ini");
	INIReader alternate("ini/save/magic1.ini");
	if (!check(initial.GetInteger("Head", "Count", 0) == 17 && alternate.GetInteger("Head", "Count", 0) == 17,
		"both real starting characters declare seventeen learned skills")) return false;
	std::vector<std::string> files;
	for (int index = 1; index <= 17; ++index)
	{
		const auto section = std::to_string(index);
		const auto file = initial.Get(section, "IniFile", "");
		if (!check(!file.empty() && file == alternate.Get(section, "IniFile", ""),
			"both starting lists refer to the same real skill definitions")) return false;
		files.push_back(file);
	}
	// The two additional loadable AddMagic rewards are outside the starting lists.
	files.push_back(u8"player-magic-清心咒.ini");
	files.push_back(u8"player-magic-慈航普渡.ini");
	bool ok = true;
	std::vector<std::pair<int, int>> uncappedResults;
	for (bool enabled : { false, true })
	{
		GameManager game;
		auto manifest = resources.getActiveManifest();
		if (!enabled) manifest.features["magiclevellimitfromdefinition"] = false;
		game.global.applyResourceManifestFeatures(manifest);
		if (!check(game.global.feature.magicLevelLimitFromDefinition == enabled,
			"the production profile explicitly enables the learning cap; the control changes only that flag")) return false;
		game.global.loadUiSettings();
		game.goodsManager.configureLayout();
		auto& manager = game.magicManager;
		manager.configureLayout();
		game.menu->practiceMenu = std::make_shared<PracticeMenu>();
		game.varList.ensureInitialized();
		const std::string book = u8"book01-风火雷.ini";
		if (!check(game.goodsManager.addItem(book, 1), "load the real learning book")) return false;
		auto* item = game.goodsManager.findGoods(book);
		if (!check(item != nullptr && game.goodsManager.useItem(static_cast<int>(item - game.goodsManager.goodsList.data()))
			&& manager.findPrimaryMagic(u8"player-magic-风火雷.ini") != nullptr && game.goodsManager.getItemNum(book) == 0,
			"using the actual book runs its goods script, learns the skill and consumes the book")) return false;
		std::size_t resultIndex = 0;
		int roundtrips = 0;
		for (const auto& file : files)
		{
			manager.clearMagicList();
			auto* learned = manager.addPrimaryMagic(file, false, false);
			if (!check(learned != nullptr && learned->magic->definedLearningLevelLimit == (enabled ? 10 : 0),
				"every currently loadable starting or script-reward skill defines all ten levels")) return false;
			manager.exchange(static_cast<int>(learned - manager.magicList.data()), manager.practiceIndex());
			learned = manager.findPrimaryMagic(file);
			for (int level = 1; level < 10; ++level)
			{
				const int threshold = learned->magic->level[level].levelupExp;
				if (!check(threshold > 0 && (level == 9 || threshold <= learned->magic->level[level + 1].levelupExp),
					"real learnable skills have positive nondecreasing thresholds through level nine")) return false;
				learned->level = level;
				learned->exp = threshold - 1;
				manager.addPracticeExp(1);
				const auto result = std::make_pair(learned->level, learned->exp);
				// This shipped definition has the same 40000 threshold at levels seven and eight.
				const int expectedLevel = file == u8"player-magic-云生结海.ini" && level == 7 ? 9 : level + 1;
				ok = check(result == std::make_pair(expectedLevel, threshold), "real practice preserves continuous upgrades at shared thresholds") && ok;
				if (enabled) ok = check(result == uncappedResults.at(resultIndex), "the cap does not alter any pre-terminal practice upgrade") && ok;
				else uncappedResults.push_back(result);
				++resultIndex;
			}
			learned->level = 1;
			learned->exp = 0;
			ok = check(manager.addPracticeExperienceToNextLevel() && learned->level == 2,
				"the practice shortcut remains available below the definition limit") && ok;
			const int threshold = learned->magic->level[9].levelupExp;
			learned->level = 9;
			learned->exp = threshold - 1;
			const auto previousMagic = learned->magic;
			if (!check(manager.save(0), "save the real practiced skill one point below level ten")) return false;
			manager.clearMagicList();
			if (!check(manager.load(0), "reload the real practiced skill into a fresh object")) return false;
			learned = manager.findPrimaryMagic(file);
			if (!check(learned != nullptr && learned->magic != previousMagic && learned->level == 9 && learned->exp == threshold - 1
				&& manager.magicList[manager.practiceIndex()].iniFile == file,
				"readback retains the practice slot, level and experience")) return false;
			manager.addPracticeExp(1);
			ok = check(learned->level == 10 && learned->exp == threshold, "practice still reaches level ten after file reload") && ok;
			// Terminal accumulation is the intentional difference of the explicit cap.
			manager.addPracticeExp(7);
			game.scriptAPI.addMagicExp(file, 7);
			auto effect = std::make_shared<Effect>();
			effect->magic.experienceOwnerMagicFile = file;
			manager.addUseExp(effect, 7);
			ok = check(learned->level == 10 && learned->exp == threshold + (enabled ? 0 : 21)
				&& !manager.addPracticeExperienceToNextLevel(),
				"all three experience entries preserve the intentional terminal policy without another upgrade") && ok;
			++roundtrips;
		}
		std::cout << "Yuchen learning review:\tenabled=" << enabled << "\tskills=" << files.size()
			<< "\tthresholds=" << resultIndex << "\tfileRoundtrips=" << roundtrips << "\tbookUsed=1\tpassed=" << ok << std::endl;
	}
	return ok;
}

bool runProductionDefinedMagicLevelTests(ResourceManager& resources)
{
	if (!check(resources.setActiveResourcePackById("JIANGHU_YUCHEN_1_03"), "defined levels select the real Yuchen resource chain")) return false;
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	const auto packRoot = resources.getActiveResourceRoot();
	std::ifstream input(std::filesystem::path(__FILE__).parent_path() / "fixtures" / std::filesystem::u8path(u8"mg-player-talent-轻功.ini"), std::ios::binary);
	const std::string definition((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
	if (!check(!definition.empty(), "read the published MG three-level talent converted to UTF-8")) return false;
	ScopedActiveResourceRoot isolatedRoot;
	if (!check(isolatedRoot.valid(), "defined-level tests isolate source fixtures and saves")) return false;
	File::setResourceFallbackRoots({ packRoot, (assetsRoot / "yycs").u8string(), (assetsRoot / "common").u8string() });
	SaveFileManager::CurrentPathScope currentPath("save\\defined_levels");
	if (!check(currentPath.valid(), "defined-level tests isolate character files")) return false;
	const std::string file = u8"mg-player-talent-轻功.ini";
	if (!check(writeVirtualFile("ini/magic/" + file, definition), "install the exact converted definition only in the isolated fixture")) return false;
	bool ok = true;
	for (bool definedLimit : { false, true })
	{
		GameManager game;
		ResourceManifest manifest = resources.getActiveManifest();
		manifest.features["magiclevellimitfromdefinition"] = definedLimit;
		game.global.applyResourceManifestFeatures(manifest);
		game.global.loadUiSettings();
		auto& manager = game.magicManager;
		manager.configureLayout();
		game.menu->practiceMenu = std::make_shared<PracticeMenu>();
		game.varList.ensureInitialized();
		for (int route : { 0, 1, 2 })
		{
			manager.clearMagicList();
			if (route == 0) game.scriptAPI.addTalent(file);
			else manager.addPrimaryMagic(file, false, false);
			auto* learned = manager.findPrimaryMagic(file);
			if (!check(learned != nullptr && learned->magic->name == u8"轻功", "real three-level definition loads with its correct Chinese name")) return false;
			ok = check(learned->magic->definedLearningLevelLimit == (definedLimit ? 3 : 0)
				&& learned->magic->maxLevel == 0, "learning limit comes from real sections, not target-control MaxLevel") && ok;
			if (route == 2)
			{
				manager.exchange(static_cast<int>(learned - manager.magicList.data()), manager.practiceIndex());
				learned = manager.findPrimaryMagic(file);
			}
			const auto award = [&](int amount)
			{
				if (route == 0)
				{
					const std::string command = "addmagicexp(\"" + file + "\"," + std::to_string(amount) + ");";
					auto bytes = std::make_unique<char[]>(command.size());
					std::copy(command.begin(), command.end(), bytes.get());
					return check(game.script.runScript(bytes, static_cast<int>(command.size())) == LUA_OK, "defined-level award uses the real Lua interface");
				}
				if (route == 1)
				{
					auto effect = std::make_shared<Effect>();
					effect->magic.iniName = file;
					manager.addUseExp(effect, amount);
				}
				else manager.addPracticeExp(amount);
				return true;
			};
			if (!award(1000)) return false;
			ok = check(learned->level == 3 && learned->exp == 1000, "a single award still advances continuously through both defined levels") && ok;
			if (!award(1000)) return false;
			const int expectedLevel = definedLimit ? 3 : 4;
			const int expectedExperience = definedLimit ? 1000 : 2000;
			ok = check(learned->level == expectedLevel && learned->exp == expectedExperience,
				"declared-level mode stops at the real last section while legacy mode preserves its existing fallback") && ok;
			const auto original = learned->magic;
			auto copied = std::make_shared<Magic>();
			copied->copy(*original);
			learned->magic = copied;
			if (!award(INT_MAX)) return false;
			const int expectedSaturatedExperience = definedLimit ? expectedExperience : INT_MAX;
			ok = check(learned->level == expectedLevel && learned->exp == expectedSaturatedExperience, "magic copies retain the explicit cap or ordinary saturated accumulation") && ok;
			if (!check(manager.save(0), "save the actual terminal skill to a character file")) return false;
			manager.clearMagicList();
			if (!check(manager.load(0), "reload the actual terminal skill from its character file")) return false;
			learned = manager.findPrimaryMagic(file);
			if (!check(learned != nullptr && learned->magic != original && learned->magic != copied, "readback rebuilds a fresh skill definition")) return false;
			if (!award(1)) return false;
			ok = check(learned->level == expectedLevel && learned->exp == expectedSaturatedExperience, "file readback preserves the explicit cap or ordinary accumulated experience") && ok;
			if (route == 2) ok = check(!manager.addPracticeExperienceToNextLevel(), "the practice shortcut cannot advance beyond the effective level limit") && ok;
			learned->level = 1;
			learned->exp = 0;
			if (!award(3000)) return false;
			ok = check(learned->level == expectedLevel && learned->exp == 3000,
				"one oversized award retains all experience when reaching either level boundary") && ok;
			if (!award(1)) return false;
			ok = check(learned->level == expectedLevel && learned->exp == (definedLimit ? 3000 : 3001),
				"only the explicit declared-level policy stops later experience awards") && ok;
			std::cout << "Production defined magic levels:\tdefined=" << definedLimit << "\troute=" << route
				<< "\tlevel=" << learned->level << "\texp=" << learned->exp << "\tfileRoundtrips=1" << std::endl;
		}
		if (!check(writeVirtualFile("ini/magic/defined-limit-control.ini", "[Init]\nName=control\nMaxLevel=50\n[Level1]\nLevelupExp=100\n"),
			"write a one-level control fixture with a distinct target level bound")) return false;
		Magic control;
		control.initFromIni("defined-limit-control.ini");
		ok = check(control.loadSucceeded && control.maxLevel == 50 && control.definedLearningLevelLimit == (definedLimit ? 1 : 0),
			"target-control MaxLevel remains unchanged when the learning limit is enabled") && ok;
		if (!check(writeVirtualFile("ini/magic/defined-limit-empty.ini", "[Init]\nName=empty\nLevelupExp=100\n"),
			"write an Init-only compatibility fixture")) return false;
		auto* initOnly = manager.addPrimaryMagic("defined-limit-empty.ini", false, false);
		if (!check(initOnly != nullptr, "Init-only definitions remain loadable without stricter validation")) return false;
		game.scriptAPI.addMagicExp("defined-limit-empty.ini", 100);
		ok = check(initOnly->level == (definedLimit ? 1 : 10) && initOnly->exp == (definedLimit ? 0 : 100),
			"Init-only definitions stop at their sole usable level only under the explicit declared-level policy") && ok;
	}
	return ok;
}

bool runProductionTalentSlotTests(ResourceManager& resources)
{
	if (!check(resources.setActiveResourcePackById("JIANGHU_YUCHEN_1_03"), "talent slots select the real Yuchen resources")) return false;
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	const auto packRoot = resources.getActiveResourceRoot();
	bool ok = true;
	for (const char* profile : { "JXQY2", "YYCS", "XJXQY" })
	{
		ScopedActiveResourceRoot isolatedRoot;
		if (!check(isolatedRoot.valid(), "talent tests isolate files from the actual game")) return false;
		File::setResourceFallbackRoots({ packRoot, (assetsRoot / "yycs").u8string(), (assetsRoot / "common").u8string() });
		SaveFileManager::CurrentPathScope currentPath("save\\talent_slots");
		if (!check(currentPath.valid(), "talent tests isolate their character generation")) return false;
		GameManager game;
		ResourceManifest manifest = resources.getActiveManifest();
		manifest.uiProfile = profile;
		manifest.features["separatetalentslots"] = false;
		game.global.applyResourceManifestFeatures(manifest);
		game.global.loadUiSettings();
		auto& manager = game.magicManager;
		manager.configureLayout();
		const int ordinaryLength = manager.listLength();
		const std::string ordinary = u8"player-magic-清心咒.ini";
		const std::string talent = u8"player-magic-烈火情天.ini";
		const std::string secondTalent = u8"player-magic-慈航普渡.ini";
		const std::string thirdTalent = u8"player-magic-云生结海.ini";
		game.scriptAPI.addTalent(ordinary);
		if (!check(manager.findPrimaryMagic(ordinary) != nullptr && manager.listLength() == ordinaryLength,
			"resources without the talent feature keep the existing AddTalent extension")) return false;
		manager.clearMagicList();
		manifest.features["separatetalentslots"] = true;
		game.global.applyResourceManifestFeatures(manifest);
		game.global.loadUiSettings();
		manager.configureLayout();
		if (!check(manager.listLength() == 445 && manager.storeEnd() < 415 && manager.bottomEnd() < 415,
			"talent capacity extends the list without changing ordinary menu slots")) return false;
		auto* basic = manager.addPrimaryMagic(ordinary, false, false);
		if (!check(basic != nullptr, "load a real ordinary skill for the capacity control")) return false;
		const MagicInfo occupied = *basic;
		for (int i = 0; i < manager.listLength(); ++i)
		{
			if (manager.isStoreIndex(i) || manager.isBottomIndex(i)) manager.magicList[i] = occupied;
		}
		game.varList.ensureInitialized();
		const auto script = [&](const std::string& command)
		{
			auto bytes = std::make_unique<char[]>(command.size());
			std::copy(command.begin(), command.end(), bytes.get());
			return check(game.script.runScript(bytes, static_cast<int>(command.size())) == LUA_OK,
				"talent operations run through the real Lua interface");
		};
		if (!script("addtalent(\"" + talent + "\");")) return false;
		if (!check(manager.magicList[415].iniFile == talent && manager.primaryFreeIndex() < 0,
			"a full ordinary inventory still learns into the separate first talent slot")) return false;
		const auto learned = manager.magicList[415].magic;
		if (!script("addmagicexp(\"" + talent + "\",1500);addtalent(\"" + talent + "\");getplayermagiclevel(\""
			+ talent + "\",\"TalentLevel\");")) return false;
		ok = check(manager.magicList[415].magic == learned && manager.magicList[415].level == 3
			&& manager.magicList[415].exp == 1500 && game.varList.getInteger("TalentLevel") == 3
			&& manager.magicList[416].iniFile.empty(), "duplicate talent learning preserves its object, continuous progress and slot") && ok;
		for (int i = 416; i <= 444; ++i)
		{
			manager.magicList[i] = manager.magicList[415];
			manager.magicList[i].exp = i;
		}
		game.scriptAPI.addTalent(secondTalent);
		ok = check(manager.findPrimaryMagic(secondTalent) == nullptr && manager.magicList[444].exp == 444,
			"a full talent range cannot overwrite its last entry or spill into another category") && ok;
		manager.magicList[422] = MagicInfo();
		game.scriptAPI.addTalent(secondTalent);
		ok = check(manager.magicList[422].iniFile == secondTalent && manager.magicList[444].exp == 444
			&& manager.magicList[423].exp == 423, "talent learning reuses a hole instead of deriving an occupied index from count") && ok;
		manager.clearMagicList();
		game.scriptAPI.addTalent(talent);
		game.scriptAPI.addMagicExp(talent, 1500);
		if (!check(manager.setMagicHidden(talent, true, true, false) != nullptr, "hide a talent through the existing primary-list API")) return false;
		game.scriptAPI.addTalent(secondTalent);
		if (!check(manager.save(0), "save visible and hidden talents to the real character file")) return false;
		manager.clearMagicList();
		if (!check(manager.load(0) && manager.isMagicHidden(talent) && manager.magicList[415].iniFile == secondTalent,
			"file readback restores the occupied talent slot and hidden talent independently")) return false;
		auto* shown = manager.setMagicHidden(talent, false, true, false);
		if (!check(shown != nullptr && shown == &manager.magicList[416] && shown->level == 3 && shown->exp == 1500,
			"revealing a talent with an occupied former slot stays in the talent category")) return false;
		const auto beforeReload = shown->magic;
		manager.replaceMagicList(ordinary);
		game.scriptAPI.addTalent(thirdTalent);
		game.scriptAPI.addMagicExp(talent, 1);
		ok = check(manager.findPrimaryMagic(talent)->exp == 1501 && manager.findMagic(thirdTalent) == nullptr,
			"talent grants and experience during a form target the primary character, not the temporary list") && ok;
		if (!check(manager.save(0), "save talent changes while a replacement list is active")) return false;
		manager.clearMagicList();
		if (!check(manager.load(0) && !manager.hasActiveReplaceMagicList(), "talent readback retains the existing form-ending rule")) return false;
		ok = check(manager.magicList[415].iniFile == secondTalent && manager.magicList[416].iniFile == talent
			&& manager.magicList[416].magic != beforeReload && manager.magicList[416].exp == 1501
			&& manager.magicList[417].iniFile == thirdTalent, "all primary talent positions and progress survive form-time saving") && ok;
		const int freeBeforeMissing = manager.primaryFreeIndex(true);
		game.scriptAPI.addTalent(u8"player-talent-口才.ini");
		ok = check(manager.primaryFreeIndex(true) == freeBeforeMissing && manager.findPrimaryMagic(u8"player-talent-口才.ini") == nullptr,
			"the real missing talent definition does not allocate a phantom entry") && ok;
		manager.deletePrimaryMagic(secondTalent);
		game.scriptAPI.addTalent(ordinary);
		ok = check(manager.magicList[415].iniFile == ordinary && manager.magicList[416].exp == 1501,
			"deleting and relearning a talent fills its hole without resetting its neighbour") && ok;
		if (!check(writeVirtualFile("ini/ui/ui_settings.ini",
			"[MagicInit]\nStoreIndexBegin=1\nStoreIndexEnd=420\nBottomIndexBegin=421\nBottomIndexEnd=425\nXiuLianIndex=426\n"),
			"write a valid ordinary layout that overlaps the optional talent range")) return false;
		game.global.loadUiSettings();
		manager.configureLayout();
		game.scriptAPI.addTalent(talent);
		ok = check(manager.storeEnd() == 419 && manager.bottomBegin() == 420 && manager.practiceIndex() == 425
			&& manager.findPrimaryMagic(talent) == nullptr,
			"an incompatible optional talent range preserves the ordinary layout and declines the talent grant") && ok;
		std::cout << "Production talent slots:\tprofile=" << profile << "\tcapacity=30\tfileRoundtrips=2\tpassed=" << ok << std::endl;
	}
	return ok;
}

bool runProductionMagicAdvanceTests(ResourceManager& resources)
{
	struct Fixture { const char* id; const char* list; const char* section; const char* magic; };
	const Fixture fixtures[] = {
		{ "JXQY2", "ini/save/magic.ini", "3", u8"player-magic-天意剑诀.ini" },
		{ "XJXQY", "ini/save/magic0.ini", "2", u8"magic001_衡山有雪.ini" },
		{ "YYCS", "ini/save/magic0.ini", "1", u8"player-magic-清心咒.ini" },
		{ "JIAN_ER_GAI_CHENGHE_1_041", "ini/save/magic.ini", "3", u8"0player-magic-天意剑诀.ini" },
		{ "JIANGHU_YUCHEN_1_03", "ini/save/magic0.ini", "16", u8"player-magic-云生结海.ini" },
		{ "JIANGHU_YUCHEN_2", "ini/save/magic0.ini", "1", u8"player-magic-烈火情天.ini" },
		{ "XIAOXIANGXING_1_022", "ini/save/magic0.ini", "5", u8"001春城何处不飞花.ini" },
		{ "XINYUE_WUHEN_3_0", "ini/save/magic.ini", "3", u8"player-magic-天意剑诀.ini" },
		{ "YUEMEIER_WAIZHUAN_1_053", "ini/save/magic0.ini", "1", u8"player-magic-清心咒.ini" }
	};
	bool ok = true;
	for (const auto& fixture : fixtures)
	{
		if (!check(resources.setActiveResourcePackById(fixture.id), "advancement selects the actual game profile")) return false;
		SaveFileManager::CurrentPathScope currentPath("save\\advance_policy");
		if (!check(currentPath.valid(), "advancement uses an isolated character file")) return false;
		INIReader initial(fixture.list);
		if (!check(initial.Get(fixture.section, "IniFile", "") == fixture.magic,
			"the real character list references this exact skill")) return false;
		GameManager game;
		game.global.applyResourceManifestFeatures(resources.getActiveManifest());
		auto& manager = game.magicManager;
		manager.configureLayout();
		game.menu->practiceMenu = std::make_shared<PracticeMenu>();
		game.varList.ensureInitialized();
		game.varList.setInteger("advancelevel", 71);
		for (int route : { 0, 1, 2 })
		{
			manager.clearMagicList();
			auto* learned = manager.addPrimaryMagic(fixture.magic, false, false);
			if (!check(learned != nullptr, "advancement loads the real definition through its dependency chain")) return false;
			if (route == 2)
			{
				const int slot = static_cast<int>(learned - manager.magicList.data());
				manager.exchange(slot, manager.practiceIndex());
				learned = manager.findPrimaryMagic(fixture.magic);
			}
			const int first = learned->magic->level[1].levelupExp;
			const int second = learned->magic->level[2].levelupExp;
			const int third = learned->magic->level[3].levelupExp;
			if (!check(0 < first && first < second && second + 1 < third,
				"the production fixture has distinct cumulative thresholds for two upgrades")) return false;
			const auto award = [&](int amount)
			{
				if (route == 0)
				{
					const std::string command = "addmagicexp(\"" + std::string(fixture.magic) + "\"," + std::to_string(amount)
						+ ");getplayermagiclevel(\"" + fixture.magic + "\",\"AdvanceLevel\");";
					auto bytes = std::make_unique<char[]>(command.size());
					std::copy(command.begin(), command.end(), bytes.get());
					return check(game.script.runScript(bytes, static_cast<int>(command.size())) == LUA_OK
						&& game.varList.getInteger("AdvanceLevel") == learned->level
						&& game.varList.getInteger("advancelevel") == 71,
						"real Lua awards expose the resulting level without merging variable case");
				}
				if (route == 1)
				{
					auto effect = std::make_shared<Effect>();
					effect->magic.experienceOwnerMagicFile = fixture.magic;
					manager.addUseExp(effect, amount);
				}
				else manager.addPracticeExp(amount);
				return true;
			};
			// Continuous advancement is the approved game behavior, including after file reload.
			if (!award(second)) return false;
			ok = check(learned->level == 3 && learned->exp == second,
				"one award crosses both actual thresholds without subtracting cumulative experience") && ok;
			const auto upgradedMagic = learned->magic;
			if (!check(manager.save(0), "persist the actual skill after continuous advancement")) return false;
			manager.clearMagicList();
			if (!check(manager.load(0), "reload the continuously advanced character file")) return false;
			learned = manager.findPrimaryMagic(fixture.magic);
			if (!check(learned != nullptr && learned->magic != upgradedMagic && learned->level == 3 && learned->exp == second,
				"file readback preserves the advanced level and accumulated experience in a new object")) return false;
			if (!award(1)) return false;
			ok = check(learned->level == 3 && learned->exp == second + 1,
				"a later award continues from saved experience without replaying the earlier upgrade") && ok;
			learned->level = 1;
			learned->exp = 0;
			if (!award(first - 1)) return false;
			ok = check(learned->level == 1 && learned->exp == first - 1,
				"a split award below the first actual threshold does not upgrade") && ok;
			if (!award(1)) return false;
			ok = check(learned->level == 2 && learned->exp == first,
				"an exact cumulative threshold advances the skill once") && ok;
			if (!award(second - first)) return false;
			ok = check(learned->level == 3 && learned->exp == second,
				"split and single awards reach the same real skill level and cumulative experience") && ok;
			std::cout << "Production continuous advancement:\tpack=" << fixture.id << "\troute=" << route
				<< "\tamount=" << second << "\tlevel=" << learned->level << "\tfileRoundtrips=1" << std::endl;
		}
	}
	return ok;
}

bool runFullExperienceSaveRuntimeTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	auto& resources = ResourceManager::instance();
	if (!check(resources.initialize(assetsRoot.u8string()), "experience saves discover real resource profiles")) return false;
	if (!runProductionTalentJumpTests(resources)) return false;
	if (!runProductionDefinedMagicLevelTests(resources)) return false;
	if (!runProductionYuchenLearningLimitTests(resources)) return false;
	if (!runProductionTalentSlotTests(resources)) return false;
	if (!runProductionMagicAdvanceTests(resources)) return false;
	bool thresholdsMatch = true;
	bool terminalExperienceMatches = true;
	int definitionCount = 0;
	for (const char* packId : { "JXQY2", "XJXQY", "YYCS", "JIAN_ER_GAI_CHENGHE_1_041",
		"JIANGHU_YUCHEN_1_03", "JIANGHU_YUCHEN_2", "XIAOXIANGXING_1_022", "XINYUE_WUHEN_3_0", "YUEMEIER_WAIZHUAN_1_053" })
	{
		if (!check(resources.setActiveResourcePackById(packId), "threshold checks select each actual resource profile")) return false;
		const auto magicRoot = std::filesystem::u8path(resources.getActiveResourceRoot()) / "ini/magic";
		int packCount = 0;
		int mismatchCount = 0;
		int zeroChecks = 0, terminalTransitions = 0, terminalMismatches = 0, terminalFileRoundtrips = 0;
		SaveFileManager::CurrentPathScope terminalPath("save\\terminal_experience");
		if (!check(terminalPath.valid(), "terminal experience isolates its character files")) return false;
		GameManager experienceGame;
		experienceGame.global.applyResourceManifestFeatures(resources.getActiveManifest());
		experienceGame.magicManager.configureLayout();
		for (const auto& entry : std::filesystem::recursive_directory_iterator(magicRoot))
		{
			if (!entry.is_regular_file() || entry.path().extension() != ".ini") continue;
			const auto relative = entry.path().lexically_relative(magicRoot).generic_u8string();
			std::unique_ptr<char[]> definitionBytes;
			if (!check(File::readFile("ini/magic/" + relative, definitionBytes) > 0,
				"threshold reference reads the same runtime resource path as Magic")) return false;
			INIReader definition(definitionBytes);
			if (!check(definition.ParseError() == 0, "threshold reference parses the actual definition")) return false;
			Magic magic;
			magic.initFromIni(relative, false);
			bool matches = magic.loadSucceeded;
			int expectedDefinedLimit = 0;
			if (experienceGame.global.feature.magicLevelLimitFromDefinition)
			{
				expectedDefinedLimit = 1;
				for (int candidate = 1; candidate <= MAGIC_MAX_LEVEL; ++candidate)
				{
					if (definition.HasSection("Level" + std::to_string(candidate))) expectedDefinedLimit = candidate;
				}
			}
			matches = magic.definedLearningLevelLimit == expectedDefinedLimit && matches;
			const int baseThreshold = static_cast<int>(definition.GetInteger("Init", "LevelupExp", 0));
			for (int level = 1; level <= MAGIC_MAX_LEVEL; ++level)
			{
				matches = magic.level[level].levelupExp == definition.GetInteger(
					"Level" + std::to_string(level), "LevelupExp", baseThreshold) && matches;
			}
			if (!matches)
			{
				++mismatchCount;
				std::cout << "Production threshold mismatch:\tpack=" << packId << "\tfile=" << relative << std::endl;
			}
			thresholdsMatch = matches && thresholdsMatch;
			auto& manager = experienceGame.magicManager;
			manager.clearMagicList();
			auto& learned = manager.magicList[manager.storeBegin()];
			learned.iniFile = relative;
			learned.magic = std::make_shared<Magic>();
			learned.magic->copy(magic);
			learned.hideCount = 1;
			int firstTerminal = 0;
			for (int level = 1; level <= MAGIC_MAX_LEVEL; ++level)
			{
				if (magic.level[level].levelupExp != 0) continue;
				if (firstTerminal == 0) firstTerminal = level;
				learned.level = level;
				learned.exp = 321;
				experienceGame.scriptAPI.addMagicExp(relative, 25);
				++zeroChecks;
				const bool atDefinedLimit = expectedDefinedLimit > 0 && level >= expectedDefinedLimit;
				if (learned.level != level || learned.exp != (atDefinedLimit ? 321 : 346)) ++terminalMismatches;
				if (level > 1 && magic.level[level - 1].levelupExp > 0)
				{
					const int threshold = magic.level[level - 1].levelupExp;
					learned.level = level - 1;
					learned.exp = threshold - 1;
					experienceGame.scriptAPI.addMagicExp(relative, (std::numeric_limits<int>::max)());
					++terminalTransitions;
					const bool atDefinedLimit = expectedDefinedLimit > 0 && level - 1 >= expectedDefinedLimit;
					const int expectedLevel = atDefinedLimit ? level - 1 : level;
					const int expectedExperience = atDefinedLimit ? threshold - 1 : (std::numeric_limits<int>::max)();
					if (learned.level != expectedLevel || learned.exp != expectedExperience) ++terminalMismatches;
					if (atDefinedLimit)
					{
						std::cout << "Production declared terminal cap:\tpack=" << packId << "\tfile=" << relative
							<< "\tlimit=" << expectedDefinedLimit << "\tlevel=" << learned.level << "\texp=" << learned.exp << std::endl;
					}
				}
			}
			if (firstTerminal != 0 && terminalFileRoundtrips == 0)
			{
				learned.level = firstTerminal;
				learned.exp = 321;
				const auto originalMagic = learned.magic;
				if (!check(manager.save(0), "save a real zero-threshold skill to the isolated character file")) return false;
				manager.clearMagicList();
				if (!check(manager.load(0), "rebuild a real zero-threshold skill from its character file")) return false;
				auto* reloaded = manager.findPrimaryMagic(relative);
				if (!check(reloaded != nullptr && reloaded->magic != originalMagic
					&& reloaded->level == firstTerminal && reloaded->exp == 321,
					"terminal skill file readback restores experience into a new instance")) return false;
				experienceGame.scriptAPI.addMagicExp(relative, 25);
				const bool atDefinedLimit = expectedDefinedLimit > 0 && firstTerminal >= expectedDefinedLimit;
				if (reloaded->level != firstTerminal || reloaded->exp != (atDefinedLimit ? 321 : 346)) ++terminalMismatches;
				++terminalFileRoundtrips;
			}
			++packCount;
		}
		definitionCount += packCount;
		std::cout << "Production magic thresholds:\tpack=" << packId << "\tdefinitions=" << packCount
			<< "\tmismatches=" << mismatchCount << std::endl;
		terminalExperienceMatches = terminalExperienceMatches && terminalMismatches == 0 && terminalFileRoundtrips == 1;
		std::cout << "Production terminal experience:\tpack=" << packId << "\tzero=" << zeroChecks
			<< "\ttransitions=" << terminalTransitions << "\tfileRoundtrips=" << terminalFileRoundtrips
			<< "\tmismatches=" << terminalMismatches << std::endl;
	}
	// Original 569 definitions plus five arena spells and the restored Xiaoxiang Nulei spell.
	if (!check(thresholdsMatch && definitionCount == 575,
		"all nine packs load real level thresholds from Init defaults rather than the preceding level")) return false;
	if (!check(terminalExperienceMatches,
		"real zero-threshold skills retain accumulated experience unless explicitly capped by the resource profile")) return false;
	if (!check(resources.setActiveResourcePackById("JIANGHU_YUCHEN_1_03"),
		"talent rewards select the actual Yuchen profile")) return false;
	const auto talentPackRoot = resources.getActiveResourceRoot();
	struct TalentFixture { const char* file; const char* section; const char* name; const char* map; bool right; int choices; };
	const TalentFixture talentFixtures[] = {
		{ "ini/save/wudangshanxia.npc", "NPC012", u8"李婶", u8"map_003_武当山下", false, 6 },
		{ u8"ini/save/tmx_map_066_比武台.npc", "NPC005", u8"武当山下酒肆老板", u8"map_066_比武台", true, 11 }
	};
	for (const auto& fixture : talentFixtures)
	{
		ScopedActiveResourceRoot resourceRoot;
		if (!check(resourceRoot.valid(), "talent rewards isolate all writable data")) return false;
		const std::vector<std::string> roots{ talentPackRoot, (assetsRoot / "yycs").u8string(), (assetsRoot / "common").u8string() };
		File::setResourceFallbackRoots(roots);
		File::setUiResourceFallbackRoots(roots, true, (assetsRoot / "common").u8string());
		SaveFileManager::CurrentPathScope currentPath("save\\talent_rewards");
		if (!check(currentPath.valid(), "talent rewards select an isolated file generation")) return false;
		GameManager gameManager;
		gameManager.global.applyResourceManifestFeatures(resources.getActiveManifest());
		gameManager.global.loadUiSettings();
		gameManager.goodsManager.configureLayout();
		gameManager.magicManager.configureLayout();
		gameManager.setAutomationHooksEnabled(true);
		gameManager.varList.ensureInitialized();
		gameManager.varList.setInteger("selvalue", 91);
		gameManager.mapFolderName = fixture.map;
		std::unique_ptr<char[]> bytes;
		if (!check(File::readFile(fixture.file, bytes) > 0, "read the actual talent NPC binding")) return false;
		INIReader definition(bytes);
		auto npc = std::make_shared<NPC>();
		npc->initFromIni(&definition, fixture.section);
		gameManager.npcManager->addNPC(npc);
		const std::string scriptFile = fixture.right ? npc->scriptFileRight : npc->scriptFile;
		if (!check(npc->npcName == fixture.name && scriptFile == (fixture.right ? u8"右键脚本.txt" : u8"天赋.txt")
			&& File::readFile("script/map/" + std::string(fixture.map) + "/" + scriptFile, bytes) > 0,
			"actual NPC section selects its own map-local reward script")) return false;
		const std::vector<std::string> missingTalents{
			u8"player-talent-偷取.ini", u8"player-talent-口才.ini", u8"player-talent-医术.ini", u8"player-talent-驭马.ini"
		};
		// These definitions are absent in the published MOD and current dependency
		// chain. Do not synthesize them: exercise the real partial-reward branch.
		for (const auto& file : missingTalents)
		{
			if (!check(File::readFile("ini/magic/" + file, bytes) == 0,
				"the actual runtime chain does not supply the missing talent definition")) return false;
		}
		if (fixture.right && !check(File::readFile(u8"ini/magic/player-magic-一骑当千.ini", bytes) == 0,
			"the arena's other missing reward is not supplied by fallback")) return false;
		auto& manager = gameManager.magicManager;
		const std::string rewardFile = u8"player-magic-慈航普渡.ini";
		const auto runBranch = [&](int selection)
		{
			gameManager.varList.setInteger("__automation_choose_enabled", 1);
			gameManager.varList.setInteger("__automation_choose_selection", selection);
			gameManager.varList.setInteger("__automation_choose_complete", 0);
			gameManager.runNPCScript(npc, scriptFile);
			return check(gameManager.varList.getInteger("__automation_choose_complete") == 1
				&& gameManager.varList.getInteger("__automation_choose_visible_count") == fixture.choices
				&& gameManager.varList.getInteger("SelValue") == selection
				&& gameManager.varList.getInteger("selvalue") == 91 && !gameManager.inEvent,
				"real ChoosePlus and NPC script dispatch finish the requested case-sensitive reward branch");
		};
		const auto rewardsMatch = [&]()
		{
			const auto* reward = manager.findPrimaryMagic(rewardFile);
			return check(reward != nullptr && reward->magic->loadSucceeded && reward->level == 1 && reward->exp == 73
				&& std::count_if(manager.magicList.begin(), manager.magicList.end(),
					[](const MagicInfo& info) { return info.magic != nullptr; }) == 1
				&& std::all_of(missingTalents.begin(), missingTalents.end(),
					[&](const std::string& file) { return manager.findPrimaryMagic(file) == nullptr; }),
				"missing talent rewards create no phantom slots and cannot credit or duplicate the real skill");
		};
		if (!check(runBranch(5) && manager.primaryFreeIndex() == manager.storeBegin(),
			"actual experience branch before learning leaves the empty list intact") || !runBranch(4)) return false;
		auto* reward = manager.findPrimaryMagic(rewardFile);
		if (!check(reward != nullptr && reward->level == 1 && reward->exp == 0,
			"missing definitions do not prevent the later actual Cihang reward")) return false;
		reward->exp = 73;
		const auto originalMagic = reward->magic;
		if (!runBranch(4) || !runBranch(5) || !rewardsMatch()) return false;
		if (!check(manager.findPrimaryMagic(rewardFile)->magic == originalMagic,
			"repeated real learning retains the existing learned object and progress")) return false;
		const std::string query = u8"getplayermagiclevel('player-talent-口才.ini','koucai');"
			u8"getplayermagiclevel('player-magic-慈航普渡.ini','RewardLevel');";
		auto queryBytes = std::make_unique<char[]>(query.size());
		std::copy(query.begin(), query.end(), queryBytes.get());
		gameManager.varList.setInteger("Koucai", 92);
		if (!check(gameManager.script.runScript(queryBytes, static_cast<int>(query.size())) == LUA_OK
			&& gameManager.varList.getInteger("koucai") == 0 && gameManager.varList.getInteger("Koucai") == 92
			&& gameManager.varList.getInteger("RewardLevel") == 1 && manager.save(0) && gameManager.varList.save(),
			"real level queries distinguish missing and learned skills before magic and variable files save")) return false;
		manager.clearMagicList();
		gameManager.varList.setInteger("SelValue", 99);
		gameManager.varList.setInteger("selvalue", 99);
		gameManager.varList.setInteger("koucai", 99);
		gameManager.varList.setInteger("Koucai", 99);
		if (!check(manager.load(0) && gameManager.varList.load() && rewardsMatch()
			&& manager.findPrimaryMagic(rewardFile)->magic != originalMagic
			&& gameManager.varList.getInteger("SelValue") == 5 && gameManager.varList.getInteger("selvalue") == 91
			&& gameManager.varList.getInteger("koucai") == 0 && gameManager.varList.getInteger("Koucai") == 92,
			"file roundtrip rebuilds the real skill without phantom talents and preserves exact-case results")) return false;
		if (!runBranch(4) || !runBranch(5) || !rewardsMatch()) return false;
		// Actor bindings and reward scripts are real; the arena map is not installed
		// here. This proves script/file behavior, not full-map access or UI gestures.
		std::cout << "Production talent rewards:\tmap=" << fixture.map << "\tright=" << fixture.right
			<< "\tmissing=" << (fixture.right ? 5 : 4) << "\tlearned=1\tfileRoundtrip=1\tbranches=6" << std::endl;
	}
	struct Fixture { const char* id; const char* file; const char* section; int practiceKillExperience; };
	const Fixture fixtures[] = {
		{ "YYCS", "ini/save/map006_1.npc", "NPC007", 199 },
		{ "XJXQY", u8"ini/npc/npc011_方勉.ini", "Init", 0 },
		{ "JIANGHU_YUCHEN_2", "ini/save/map016.npc", "NPC002", 199 }
	};
	const std::string sourceFile = "ownership-source.ini";
	const std::string originList = sourceFile + ";ownership-child.ini";
	const std::string controlList = "ownership-child.ini;" + sourceFile;
	bool ok = true;
	for (const auto& fixture : fixtures)
	{
		if (!check(resources.setActiveResourcePackById(fixture.id), "select the real experience profile")) return false;
		const auto packRoot = resources.getActiveResourceRoot();
		for (int originKind : { 0, 1, 2, 3 })
		for (bool asynchronous : { false, true })
		{
			ScopedActiveResourceRoot resourceRoot;
			if (!check(resourceRoot.valid(), "experience saves isolate player config and every save generation")) return false;
			std::vector<std::string> roots{ packRoot, (assetsRoot / "yycs").u8string(), (assetsRoot / "common").u8string() };
			File::setResourceFallbackRoots(roots);
			File::setUiResourceFallbackRoots(roots, true, (assetsRoot / "common").u8string());
			auto mapBytes = MapV3ContractFixture::build();
			constexpr size_t tileOffset = MapV3ContractFixture::HeaderLength
				+ MapV3ContractFixture::MpcCount * MapV3ContractFixture::InfoLength;
			mapBytes.resize(tileOffset + 16 * 16 * MapV3ContractFixture::TileLength);
			std::fill(mapBytes.begin() + MapV3ContractFixture::BaseHeaderLength, mapBytes.end(), std::uint8_t{0});
			MapV3ContractFixture::writeInt32(mapBytes, 64, 16 * 16 * MapV3ContractFixture::TileLength);
			MapV3ContractFixture::writeInt32(mapBytes, 68, 16);
			MapV3ContractFixture::writeInt32(mapBytes, 72, 16);
			// Only the lane and two simple skills are synthetic. Actor resources,
			// experience rules, layouts and complete save/load entry points are real.
			if (!check(writeVirtualFile("map/ownership-save.map", std::string(reinterpret_cast<const char*>(mapBytes.data()), mapBytes.size()))
				&& writeVirtualFile("save/game/game.ini", "[State]\nMap=ownership-save.map\nNpc=\nObj=\n")
				&& writeVirtualFile("ini/magic/ownership-source.ini",
					"[Init]\nName=OwnershipSource\nMoveKind=2\nSpeed=8\nEffect=20\nLifeFrame=100\n"
					"[Level1]\nLevelupExp=1000\n[Level2]\nLevelupExp=2000\n")
				&& writeVirtualFile("ini/magic/ownership-child.ini",
					"[Init]\nName=OwnershipChild\nMoveKind=2\nSpeed=8\nEffect=20\nLifeFrame=100\nKeepMilliseconds=10000\n"),
				"create isolated file-backed map and source/child skills")) return false;
			GameManager gameManager;
			gameManager.global.applyResourceManifestFeatures(resources.getActiveManifest());
			gameManager.global.loadUiSettings();
			gameManager.goodsManager.configureLayout();
			gameManager.magicManager.configureLayout();
			auto& manager = gameManager.magicManager;
			auto player = gameManager.player;
			std::unique_ptr<char[]> bytes;
			if (!check(player->loadInitialTemplate(0) && File::readFile(fixture.file, bytes) > 0
				&& gameManager.scriptAPI.loadMap("ownership-save.map", false), "load real actors and initialize the collision grid")) return false;
			gameManager.global.data.characterIndex = 0;
			gameManager.global.data.NPCAI = false;
			gameManager.global.data.canInput = false;
			gameManager.varList.ensureInitialized();
			gameManager.varList.setInteger("ExperienceSave", 73);
			gameManager.varList.setInteger("experiencesave", 29);
			INIReader definition(bytes);
			auto npc = std::make_shared<NPC>();
			npc->initFromIni(&definition, fixture.section);
			npc->npcName = "ExperienceTarget";
			npc->relation = nrHostile;
			npc->isAIDisabled = true;
			npc->level = 3;
			npc->lifeMax = npc->life = 1000000;
			npc->defend = npc->defend2 = npc->defend3 = npc->evade = 0;
			gameManager.npcManager->addNPC(npc);
			const Point from{ 5, 5 };
			Point to = from;
			for (int step = 0; step < 3; ++step) to = Map::getSubPoint(to, 0);
			player->setPosition(from, false);
			// A positive attacker evade against zero makes every legacy hit roll
			// succeed, without replacing collision/hurt or changing the RNG.
			player->evade = 200;
			player->calInfo();
			npc->setPosition(to, false);
			gameManager.map->createDataMap();
			if (!check(manager.addPrimaryMagic(sourceFile, false, false) != nullptr, "learn the file-backed primary entry")) return false;
			manager.findPrimaryMagic(sourceFile)->exp = originKind >= 2 ? 321 : 997;
			Magic sourceForm;
			sourceForm.name = "FullCollisionSource";
			sourceForm.replaceMagic = originList;
			Magic controlForm = sourceForm;
			controlForm.name = "FullCollisionControl";
			const std::string originKey = originKind == 3
				? player->npcName + "_" + sourceForm.name + "_" + sourceFile + "_ownership-child.ini.ini" : originList;
			if (originKind == 2) manager.replaceMagicList(originList);
			if (originKind == 3) player->applyTemporaryMorph(sourceForm, 10000);
			auto* origin = manager.findMagic(sourceFile);
			if (!check(origin != nullptr && origin->magic->loadSucceeded, "select the originating learned entry")) return false;
			origin->level = 1;
			origin->exp = 997;
			const auto originalSource = origin->magic;
			player->beginMagic(*origin, to, npc, -1);
			if (!check(player->isMagicing() && player->preparedMagicAction != nullptr,
				"the actual player animation prepares the originating skill")) return false;
			CoreLifecycleTestAccess::advanceActorFrame(*player, player->actionLastTime + 1);
			if (!check(gameManager.effectManager->effectList.size() == 1, "the animation releases exactly one live projectile")) return false;
			const auto originalEffect = gameManager.effectManager->effectList.front();
			const auto child = manager.loadAttackMagic("ownership-child.ini");
			const auto childContext = Magic::createDerivedDispatchContext(originalEffect->magicDispatchContext, child, "FlyMagic");
			gameManager.effectManager->addDelayedMagic(child, player, from, to, 1, lkSelf, npc, 200, childContext);
			gameManager.effectManager->addTrailMagic(child, player, 1, 20, player->getEvade(), lkSelf, childContext);
			ok = check(Magic::getExperienceOwner(originalEffect->magicDispatchContext).magic.lock() == originalSource
				&& gameManager.effectManager->getPendingDelayedMagicCount() == 1
				&& gameManager.effectManager->getPendingTrailMagicCount() == 1,
				"the live animation and both pending dispatches retain the cast origin") && ok;
			if (originKind == 1) manager.setMagicHidden(sourceFile, true, false, false);
			const auto selectControl = [&]()
			{
				if (originKind == 3) player->applyTemporaryMorph(controlForm, 10000);
				else manager.replaceMagicList(controlList);
			};
			selectControl();
			manager.findMagic(sourceFile)->exp = 123;
			player->jumpRadius = 65;
			npc->jumpRadius = 15;
			if (!check(gameManager.saveGame(1), "publish the entire world with active, delayed and trail origins")) return false;
			INIReader savedGlobal(std::string("save/rpg1/") + GLOBAL_INI);
			INIReader savedEffects(std::string("save/rpg1/") + EFFECT_INI);
			ok = check(savedGlobal.Get("Save", "EngineVersion", "") == JxqyBuildVersion::EngineVersion
				&& savedGlobal.Get("Save", "ResourceVersion", "") == resources.getActiveManifest().releaseMetadata.displayVersion
				&& savedEffects.GetBoolean("PRO1", "ExperienceOwnerKnown", false)
				&& savedEffects.Get("PRO1", "ExperienceOwnerList", "") == (originKind == 0 ? "primary" : originKind == 1 ? "hidden" : "replacement:" + originKey),
				"the complete published slot contains real versions and the correct origin list") && ok;
			const auto selectOrigin = [&]() -> MagicInfo*
			{
				manager.stopReplaceMagicList();
				if (originKind == 2) manager.replaceMagicList(originList);
				if (originKind == 3) player->applyTemporaryMorph(sourceForm, 10000);
				if (originKind != 1) return manager.findMagic(sourceFile);
				// Inspect via the existing reveal/hide API and leave the entry hidden.
				if (manager.setMagicHidden(sourceFile, false, false, false) == nullptr) return nullptr;
				return manager.setMagicHidden(sourceFile, true, false, false);
			};
			const auto advanceEffects = [&](int milliseconds)
			{
				for (int elapsed = 0; elapsed < milliseconds; elapsed += 20)
				{
					CoreLifecycleTestAccess::beginElementFrame(*gameManager.effectManager);
					const auto effects = gameManager.effectManager->effectList;
					for (const auto& effect : effects) CoreLifecycleTestAccess::advanceActorFrame(*effect, 20);
					CoreLifecycleTestAccess::advanceActorFrame(*gameManager.effectManager, 20);
				}
			};
			const auto collideAndCheck = [&](const char* phase)
			{
				const int initialLife = npc->life;
				advanceEffects(2000);
				const int projectileDamage = initialLife - npc->life;
				// Move off the saved trail position and put the target there. The
				// production trail dispatcher creates a fixed effect at that cell.
				player->setPosition(Map::getSubPoint(from, 2), false);
				npc->setPosition(from, false);
				gameManager.map->createDataMap();
				const int beforeTrail = npc->life;
				advanceEffects(1000);
				const int trailDamage = beforeTrail - npc->life;
				ok = check(manager.findMagic(sourceFile)->exp == 123, "another active same-file list receives no hit experience") && ok;
				auto* earned = selectOrigin();
				if (!check(earned != nullptr, "the origin survives both projectile collisions and trail dispatch")) return false;
				ok = check(projectileDamage > 0 && trailDamage > 0 && earned->exp == 1024 && earned->level == 2,
					"three actual collisions award 27 experience and upgrade only the originating entry") && ok;
				if (originKind >= 2) ok = check(manager.findPrimaryMagic(sourceFile)->exp == 321,
					"the independent primary same-file entry keeps its prior experience") && ok;
				std::cout << "Full experience save:\tpack=" << fixture.id << "\torigin=" << originKind
					<< "\tasync=" << asynchronous << "\tphase=" << phase << "\tprojectileDamage=" << projectileDamage
					<< "\ttrailDamage=" << trailDamage << "\texp=" << earned->exp << "\tlevel=" << earned->level << std::endl;
				return true;
			};
			if (!collideAndCheck("live")) return false;
			const auto originalNpc = npc;
			gameManager.varList.setInteger("ExperienceSave", -1);
			player->jumpRadius = npc->jumpRadius = 1;
			if (!check(asynchronous ? gameManager.scriptAPI.loadGameAsync(1) : gameManager.loadGame(1),
				"sync and worker-thread async restore the full pre-collision world")) return false;
			if (!check(gameManager.npcManager->npcList.size() == 1 && gameManager.effectManager->effectList.size() == 1
				&& gameManager.effectManager->getPendingDelayedMagicCount() == 1
				&& gameManager.effectManager->getPendingTrailMagicCount() == 1
				&& gameManager.effectManager->getPendingDelayedMagicUser(0) == player
				&& gameManager.effectManager->getPendingTrailMagicUser(0) == player,
				"the loader restores exactly one active, delayed and trail effect with their real caster")) return false;
			npc = gameManager.npcManager->npcList.front();
			ok = check(player->jumpRadius == 65 && npc->jumpRadius == 15,
				"complete sync/async saves independently restore player and new NPC jump radii") && ok;
			origin = selectOrigin();
			const auto restored = gameManager.effectManager->effectList.front();
			ok = check(npc != originalNpc && npc->life == 1000000 && origin != nullptr && origin->magic != originalSource
				&& origin->exp == 997 && origin->level == 1 && restored != originalEffect
				&& Magic::getExperienceOwner(restored->magicDispatchContext).magic.lock() == origin->magic
				&& restored->user.lock() == player && gameManager.varList.getInteger("ExperienceSave") == 73
				&& gameManager.varList.getInteger("experiencesave") == 29,
				"full reload restores new owner/target objects, original progress and case-sensitive variables") && ok;
			selectControl();
			if (!collideAndCheck("reloaded")) return false;
			if (!check(gameManager.saveGame(2)
				&& (asynchronous ? gameManager.scriptAPI.loadGameAsync(2) : gameManager.loadGame(2)),
				"publish and fully reload the post-collision upgraded state")) return false;
			origin = selectOrigin();
			ok = check(origin != nullptr && origin->exp == 1024 && origin->level == 2,
				"the earned experience and upgraded level survive a second complete save") && ok;
			selectControl();
			ok = check(manager.findMagic(sourceFile)->exp == 123 && manager.findMagic(sourceFile)->level == 1,
				"the second complete save still isolates the inactive same-file control") && ok;
			if (originKind == 3)
			{
				// Extend the real-profile/full-load scene with actual death settlement.
				if (!check(writeVirtualFile("ini/magic/ownership-practice.ini",
					"[Init]\nName=OwnershipPractice\nMoveKind=2\nSpeed=8\nEffect=20\nLifeFrame=100\n"
					"[Level1]\nLevelupExp=1000\n[Level2]\nLevelupExp=2000\n"),
					"write the isolated practice skill with cumulative upgrade thresholds")) return false;
				player->clearMagicRuntimeStates();
				player->level = 10;
				player->levelUpExp = INT_MAX;
				player->exp = 100;
				player->evade = 200;
				player->calInfo();
				player->setPosition(from, false);
				const auto prepareTarget = [&](int targetLife)
				{
					gameManager.effectManager->clearEffect();
					gameManager.npcManager->clearNPC();
					npc = std::make_shared<NPC>();
					npc->initFromIni(&definition, fixture.section);
					npc->npcName = "ActualKillTarget";
					npc->relation = nrHostile;
					npc->isAIDisabled = true;
					npc->level = 30;
					npc->exp = 12345;
					npc->expBonus = 0;
					npc->lifeMax = npc->life = targetLife;
					npc->defend = npc->defend2 = npc->defend3 = npc->evade = 0;
					npc->deathScript.clear();
					gameManager.npcManager->addNPC(npc);
					npc->setPosition(to, false);
					gameManager.map->createDataMap();
				};
				prepareTarget(1);
				Magic killSource = sourceForm;
				killSource.name += "-actual-kill";
				Magic killControl = killSource;
				killControl.name += "-control";
				killControl.replaceMagic += ";ownership-practice.ini";
				const int toolbar = manager.bottomBegin();
				const int practiceSlot = manager.practiceIndex();
				player->applyTemporaryMorph(killSource, 10000);
				manager.magicList[toolbar].exp = 997;
				player->beginMagic(manager.magicList[toolbar], to, npc, toolbar);
				CoreLifecycleTestAccess::advanceActorFrame(*player, player->actionLastTime + 1);
				if (!check(gameManager.effectManager->effectList.size() == 1,
					"the real player animation releases the killing projectile before switching forms")) return false;
				player->applyTemporaryMorph(killControl, 10000);
				manager.magicList[toolbar].exp = 997;
				manager.exchange(toolbar + 2, practiceSlot);
				manager.magicList[practiceSlot].exp = 997;
				gameManager.menu->practiceMenu = std::make_shared<PracticeMenu>();
				auto practiceMenu = gameManager.menu->practiceMenu;
				if (!check(practiceMenu != nullptr && practiceMenu->exp != nullptr && practiceMenu->level != nullptr,
					"the actual profile creates the practice menu's experience and level labels")) return false;
				practiceMenu->visible = true;
				practiceMenu->updateMagic();
				advanceEffects(2000);
				const int expectedPracticeExperience = 997 + fixture.practiceKillExperience;
				const int expectedPracticeLevel = fixture.practiceKillExperience > 0 ? 2 : 1;
				const std::string expectedPracticeText = std::to_string(expectedPracticeExperience)
					+ (expectedPracticeLevel == 2 ? "/2000" : "/1000");
				ok = check(npc->life == 0 && (npc->isDying() || npc->isHiding()) && player->exp == 1000
					&& manager.magicList[toolbar].exp == 1026 && manager.magicList[toolbar].level == 2
					&& manager.magicList[practiceSlot].exp == expectedPracticeExperience
					&& manager.magicList[practiceSlot].level == expectedPracticeLevel,
					"actual death awards 900 player experience, 29 to current use and the real profile's practice share") && ok;
				ok = check(practiceMenu->level->getStr() == std::to_string(expectedPracticeLevel)
					&& practiceMenu->exp->getStr() == expectedPracticeText,
					"the visible practice menu immediately reflects automatic kill experience and its new level") && ok;
				advanceEffects(1000);
				ok = check(player->exp == 1000 && manager.magicList[toolbar].exp == 1026,
					"later effect frames cannot settle the same death twice") && ok;
				player->applyTemporaryMorph(killSource, 10000);
				ok = check(manager.magicList[toolbar].exp == 1087 && manager.magicList[toolbar].level == 2
					&& manager.findPrimaryMagic(sourceFile)->exp == 321,
					"the inactive killing source receives only 90 hit experience, not the separate automatic shares") && ok;
				player->applyTemporaryMorph(killControl, 10000);
				std::cout << "Actual kill experience:\tpack=" << fixture.id << "\tasync=" << asynchronous
					<< "\tplayer=" << player->exp << "\tcurrent=" << manager.magicList[toolbar].exp
					<< "\tpractice=" << manager.magicList[practiceSlot].exp << "\tlabel=" << practiceMenu->exp->getStr() << std::endl;
				if (!check(gameManager.saveGame(3)
					&& (asynchronous ? gameManager.scriptAPI.loadGameAsync(3) : gameManager.loadGame(3)),
					"publish and fully reload actual kill rewards with the active selection and both source caches")) return false;
				ok = check(player->exp == 1000 && !manager.hasActiveReplaceMagicList()
					&& manager.findPrimaryMagic(sourceFile)->exp == 321,
					"actual player kill rewards persist while full readback cancels transient forms") && ok;
				player->applyTemporaryMorph(killControl, 10000);
				ok = check(manager.magicList[toolbar].exp == 1026 && manager.magicList[toolbar].level == 2
					&& manager.magicList[practiceSlot].exp == expectedPracticeExperience
					&& manager.magicList[practiceSlot].level == expectedPracticeLevel,
					"actual current-use and practice rewards survive full save and source-specific re-entry") && ok;
				player->applyTemporaryMorph(killSource, 10000);
				ok = check(manager.magicList[toolbar].exp == 1087 && manager.magicList[toolbar].level == 2,
					"the saved killing source retains hit experience separately from automatic kill shares") && ok;
				player->applyTemporaryMorph(killControl, 10000);
				advanceEffects(1000);
				ok = check(player->exp == 1000 && manager.magicList[toolbar].exp == 1026,
					"post-load effect frames do not award the already settled kill again") && ok;
				std::cout << "Actual kill full save:\tpack=" << fixture.id << "\tasync=" << asynchronous
					<< "\tplayer=" << player->exp << "\tcurrent=" << manager.magicList[toolbar].exp
					<< "\tpractice=" << manager.magicList[practiceSlot].exp << std::endl;
				ok = check(!practiceMenu->visible, "complete loading closes the outgoing practice panel") && ok;
				practiceMenu->visible = true;

				// Move an already released source into practice, then hit without killing.
				prepareTarget(1000000);
				player->beginMagic(manager.magicList[toolbar], to, npc, toolbar);
				CoreLifecycleTestAccess::advanceActorFrame(*player, player->actionLastTime + 1);
				if (!check(gameManager.effectManager->effectList.size() == 1,
					"release a second real projectile before moving its learned source")) return false;
				manager.exchange(toolbar, practiceSlot);
				practiceMenu->updateMagic();
				advanceEffects(2000);
				CoreLifecycleTestAccess::advanceActorFrame(*practiceMenu, 20);
				ok = check(npc->life < 1000000 && npc->life > 0 && player->exp == 1000
					&& manager.magicList[practiceSlot].exp == 1116 && manager.magicList[practiceSlot].level == 2,
					"a nonlethal in-flight hit credits the same learned object after it moves into practice") && ok;
				ok = check(practiceMenu->level->getStr() == "2" && practiceMenu->exp->getStr() == "1116/2000",
					"practice experience text refreshes after an in-flight hit even without a level-up or kill") && ok;
				std::cout << "Practice moved hit:\tpack=" << fixture.id << "\tasync=" << asynchronous
					<< "\texp=" << manager.magicList[practiceSlot].exp << "\tlabel=" << practiceMenu->exp->getStr() << std::endl;
				player->clearMagicRuntimeStates();
				auto* primaryEntry = manager.findPrimaryMagic(sourceFile);
				if (!check(primaryEntry != nullptr, "restore the primary practice reward control")) return false;
				primaryEntry->exp = 997;
				manager.exchange(static_cast<int>(primaryEntry - manager.magicList.data()), practiceSlot);
				practiceMenu->updateMagic();
				gameManager.scriptAPI.addMagicExp(sourceFile, 10);
				CoreLifecycleTestAccess::advanceActorFrame(*practiceMenu, 20);
				ok = check(manager.magicList[practiceSlot].exp == 1007 && manager.magicList[practiceSlot].level == 2
					&& practiceMenu->exp->getStr() == "1007/2000" && practiceMenu->level->getStr() == "2",
					"script-awarded experience and level changes refresh the same practice labels on the next UI frame") && ok;
				practiceMenu->visible = false;
				gameManager.scriptAPI.addMagicExp(sourceFile, 2);
				CoreLifecycleTestAccess::advanceActorFrame(*practiceMenu, 20);
				ok = check(practiceMenu->exp->getStr() == "1007/2000",
					"the hidden practice menu does not perform recurring text updates") && ok;
				practiceMenu->visible = true;
				CoreLifecycleTestAccess::advanceActorFrame(*practiceMenu, 20);
				ok = check(practiceMenu->exp->getStr() == "1009/2000",
					"reopening practice reflects experience received while the panel was hidden") && ok;
				std::cout << "Practice script refresh:\tpack=" << fixture.id << "\tasync=" << asynchronous
					<< "\texp=" << manager.magicList[practiceSlot].exp << "\tlabel=" << practiceMenu->exp->getStr() << std::endl;
			}
		}
	}
	return ok;
}

bool runCharacterCasterSaveRuntimeTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	auto& resources = ResourceManager::instance();
	if (!check(resources.initialize(assetsRoot.u8string()), "character-caster tests discover actual profiles")) return false;
	bool ok = true;
	for (const char* id : { "YYCS", "XJXQY", "JIANGHU_YUCHEN_2" })
	{
		if (!check(resources.setActiveResourcePackById(id), "select the actual character-caster profile")) return false;
		const auto packRoot = resources.getActiveResourceRoot();
		for (const auto& scenario : std::vector<std::pair<std::string, bool>>{
			{ "", false }, { "", true }, { "player1.ini", false }, { "player1.ini", true },
			{ "magic1.ini", false }, { "magic1.ini", true }, { "goods1.ini", false }, { "goods1.ini", true } })
		{
			const auto& failureFile = scenario.first;
			const bool rejected = !failureFile.empty();
			const bool asynchronous = scenario.second;
			const int expectedCharacter = rejected ? 0 : 1;
			ScopedActiveResourceRoot resourceRoot;
			if (!check(resourceRoot.valid(), "character-caster tests isolate every writable resource and save")) return false;
			std::vector<std::string> roots{ packRoot, (assetsRoot / "yycs").u8string(), (assetsRoot / "common").u8string() };
			File::setResourceFallbackRoots(roots);
			File::setUiResourceFallbackRoots(roots, true, (assetsRoot / "common").u8string());
			auto mapBytes = MapV3ContractFixture::build();
			constexpr size_t tileOffset = MapV3ContractFixture::HeaderLength
				+ MapV3ContractFixture::MpcCount * MapV3ContractFixture::InfoLength;
			mapBytes.resize(tileOffset + 16 * 16 * MapV3ContractFixture::TileLength);
			std::fill(mapBytes.begin() + MapV3ContractFixture::BaseHeaderLength, mapBytes.end(), std::uint8_t{0});
			MapV3ContractFixture::writeInt32(mapBytes, 64, 16 * 16 * MapV3ContractFixture::TileLength);
			MapV3ContractFixture::writeInt32(mapBytes, 68, 16);
			MapV3ContractFixture::writeInt32(mapBytes, 72, 16);
			if (!check(writeVirtualFile("map/character-caster.map", std::string(reinterpret_cast<const char*>(mapBytes.data()), mapBytes.size()))
				&& writeVirtualFile("save/game/game.ini", "[State]\nMap=character-caster.map\nNpc=\nObj=\n")
				&& writeVirtualFile("ini/magic/character-caster.ini",
					"[Init]\nName=CharacterCaster\nMoveKind=2\nSpeed=8\nEffect=20\nLifeFrame=100\nKeepMilliseconds=10000\nLevelupExp=1000\n"),
				"write a synthetic lane and skill for real character switching and collisions")) return false;
			GameManager gameManager;
			gameManager.global.applyResourceManifestFeatures(resources.getActiveManifest());
			gameManager.global.loadUiSettings();
			gameManager.goodsManager.configureLayout();
			gameManager.magicManager.configureLayout();
			gameManager.global.data.characterIndex = 0;
			gameManager.global.data.NPCAI = false;
			gameManager.global.data.canInput = false;
			gameManager.varList.ensureInitialized();
			auto player = gameManager.player;
			auto& manager = gameManager.magicManager;
			if (!check(player->loadInitialTemplate(0) && gameManager.scriptAPI.loadMap("character-caster.map", false)
				&& manager.addPrimaryMagic("character-caster.ini", false, false) != nullptr,
				"load the production player and prepare an independent same-file incoming skill")) return false;
			player->npcName = "IncomingCharacter";
			player->attack = 500;
			player->evade = 400;
			player->setPosition({ 11, 11 }, false);
			player->calInfo();
			manager.findMagic("character-caster.ini")->exp = 123;
			if (!check(player->save(1) && manager.save(1) && gameManager.goodsManager.save(1),
				"write the incoming character through actual character snapshot writers")) return false;
			if (rejected && !check(writeVirtualFile("save/game/" + failureFile, "[Broken\nValue=1\n"),
				"corrupt only the requested incoming player, magic or goods file")) return false;
			player->npcName = "OutgoingCharacter";
			player->attack = 20;
			player->evade = 200;
			const Point from{ 4, 4 };
			Point to = from;
			for (int step = 0; step < 3; ++step) to = Map::getSubPoint(to, 0);
			player->setPosition(from, false);
			player->calInfo();
			manager.findMagic("character-caster.ini")->exp = 497;
			auto npc = std::make_shared<NPC>();
			npc->npcName = "CharacterCasterTarget";
			npc->kind = nkBattle;
			npc->relation = nrHostile;
			npc->level = 3;
			npc->lifeMax = npc->life = 1000000;
			npc->defend = npc->evade = 0;
			npc->isAIDisabled = true;
			npc->setPosition(to, false);
			gameManager.npcManager->addNPC(npc);
			gameManager.map->createDataMap();
			auto* source = manager.findMagic("character-caster.ini");
			const auto oldMagic = source->magic;
			const int originalAttack = player->getAttack();
			const int originalEvade = player->getEvade();
			const int originalDamage = Magic::calculatePrimaryEffectAmount(oldMagic, player, 1);
			player->beginMagic(*source, to, npc, -1);
			if (!check(player->isMagicing(), "the outgoing production actor begins its real casting animation")) return false;
			CoreLifecycleTestAccess::advanceActorFrame(*player, player->actionLastTime + 1);
			if (!check(gameManager.effectManager->effectList.size() == 1, "the outgoing animation releases one active projectile")) return false;
			const auto originalEffect = gameManager.effectManager->effectList.front();
			gameManager.effectManager->addDelayedMagic(oldMagic, player, from, to, 1, lkSelf, npc, 200, originalEffect->magicDispatchContext);
			gameManager.effectManager->addTrailMagic(oldMagic, player, 1, 20, originalEvade, lkSelf, originalEffect->magicDispatchContext);
			const std::string command = "playerchange(1); assign('CharacterChangeContinued',1);";
			auto commandBytes = std::make_unique<char[]>(command.size());
			std::copy(command.begin(), command.end(), commandBytes.get());
			if (!check(gameManager.script.runScript(commandBytes, static_cast<int>(command.size())) == LUA_OK
				&& gameManager.global.data.characterIndex == expectedCharacter
				&& player->npcName == (rejected ? "OutgoingCharacter" : "IncomingCharacter")
				&& gameManager.varList.getInteger("CharacterChangeContinued") == 1,
				"actual Lua switches or rolls back the character and continues synchronously")) return false;
			if (rejected) ok = check(manager.findPrimaryMagic("character-caster.ini")->magic == oldMagic,
				"failed character switching retains the original learned object for outstanding attacks") && ok;
			const auto checkCaster = [&](const char* phase)
			{
				auto caster = std::dynamic_pointer_cast<NPC>(gameManager.effectManager->effectList.front()->user.lock());
				ok = check(caster != nullptr && caster->npcName == "OutgoingCharacter"
					&& caster->getAttack() == originalAttack && caster->getEvade() == originalEvade
					&& caster->getPosition() == from && (caster == player) == rejected
					&& gameManager.effectManager->getPendingDelayedMagicUser(0) == caster
					&& gameManager.effectManager->getPendingTrailMagicUser(0) == caster,
					"outstanding attacks retain the live caster after rollback or a detached caster after success") && ok;
				std::cout << "Character caster:\tpack=" << id << "\tasync=" << asynchronous << "\tphase=" << phase
					<< "\tfailureFile=" << (rejected ? failureFile : "none")
					<< "\tincomingUser=" << (caster == player) << "\tattack=" << (caster ? caster->getAttack() : -1)
					<< "\texpectedAttack=" << originalAttack << std::endl;
			};
			checkCaster("switched");
			const auto switchedCaster = originalEffect->user.lock();
			ok = check(gameManager.effectManager->captureCasterSnapshot(npc) == nullptr,
				"being an attack target alone does not require a detached caster snapshot") && ok;
			if (!check(gameManager.saveGame(1), "publish the switched world with the old caster's outstanding attacks")) return false;
			INIReader savedEffects(std::string("save/rpg1/") + EFFECT_INI);
			ok = check(savedEffects.GetInteger("Head", "DetachedCasterCount", -1) == (rejected ? 0 : 1),
				"failed switches keep the live caster while successful switches persist one shared detached caster") && ok;
			const auto collide = [&](const char* phase)
			{
				const int initialLife = npc->life;
				for (int elapsed = 0; elapsed < 2000; elapsed += 20)
				{
					CoreLifecycleTestAccess::beginElementFrame(*gameManager.effectManager);
					const auto effects = gameManager.effectManager->effectList;
					for (const auto& effect : effects) CoreLifecycleTestAccess::advanceActorFrame(*effect, 20);
					CoreLifecycleTestAccess::advanceActorFrame(*gameManager.effectManager, 20);
					if (elapsed == 0) ok = check(gameManager.effectManager->effectList.size() == 1,
						"changing the current player's position does not emit an old character's movement trail") && ok;
				}
				ok = check(initialLife - npc->life == originalDamage * 2,
					"live and delayed collisions both use the original caster's damage") && ok;
				const int expectedExperience = rejected ? 515 : 123;
				ok = check(manager.findPrimaryMagic("character-caster.ini")->exp == expectedExperience,
					"rollback retains both hit awards while a successful switch isolates the incoming same-file skill") && ok;
				std::cout << "Character caster collision:\tpack=" << id << "\tasync=" << asynchronous << "\tphase=" << phase
					<< "\tfailureFile=" << (rejected ? failureFile : "none")
					<< "\tdamage=" << initialLife - npc->life << "\texpectedDamage=" << originalDamage * 2
					<< "\tcurrentExp=" << manager.findPrimaryMagic("character-caster.ini")->exp
					<< "\texpectedExp=" << expectedExperience << std::endl;
			};
			collide("live");
			const auto oldNpc = npc;
			if (!check(asynchronous ? gameManager.scriptAPI.loadGameAsync(1) : gameManager.loadGame(1),
				"full sync and async readback restores the switched character and outstanding attacks")) return false;
			if (!check(gameManager.npcManager->npcList.size() == 1 && gameManager.effectManager->effectList.size() == 1,
				"the full loader restores only the saved target and active projectile")) return false;
			npc = gameManager.npcManager->npcList.front();
			ok = check(npc != oldNpc && npc->life == 1000000 && gameManager.global.data.characterIndex == expectedCharacter
				&& gameManager.effectManager->effectList.front() != originalEffect
				&& (rejected || gameManager.effectManager->effectList.front()->user.lock() != switchedCaster),
				"readback creates new target, effect and detached caster objects and retains the incoming character index") && ok;
			checkCaster("reloaded");
			collide("reloaded");
			gameManager.scriptAPI.playerChange(0);
			ok = check(gameManager.global.data.characterIndex == 0
				&& manager.findPrimaryMagic("character-caster.ini")->exp == (rejected ? 515 : 497),
				"returning to the outgoing character retains its switch-time snapshot, not another character's progress") && ok;
			if (failureFile == "goods1.ini")
			{
				auto* hidden = manager.setMagicHidden("character-caster.ini", true, false, false);
				if (!check(hidden != nullptr, "hide the primary skill before a failed transformed-character switch")) return false;
				const auto hiddenMagic = hidden->magic;
				Magic morph;
				morph.replaceMagic = "character-caster.ini;";
				player->applyTemporaryMorph(morph, 1000);
				auto* temporary = manager.findMagic("character-caster.ini");
				if (!check(temporary != nullptr, "prepare an independent same-file temporary skill")) return false;
				temporary->exp = 731;
				const auto temporaryMagic = temporary->magic;
				gameManager.scriptAPI.playerChange(1);
				ok = check(gameManager.global.data.characterIndex == 0 && player->morphMilliseconds == 0
					&& !manager.hasActiveReplaceMagicList() && manager.isMagicHidden("character-caster.ini"),
					"rollback ends the temporary form and list together while preserving the hidden primary skill") && ok;
				auto* revealed = manager.setMagicHidden("character-caster.ini", false, false, false);
				ok = check(revealed != nullptr && revealed->magic == hiddenMagic && revealed->exp == 515,
					"rollback retains the hidden learned object's identity and progress") && ok;
				player->applyTemporaryMorph(morph, 1000);
				temporary = manager.findMagic("character-caster.ini");
				ok = check(temporary != nullptr && temporary->magic == temporaryMagic && temporary->exp == 731,
					"re-entering the form after rollback reuses the cached learned object and its progress") && ok;
				player->updateMagicRuntimeStateTimers(1000);
				ok = check(!manager.hasActiveReplaceMagicList() && player->morphMilliseconds == 0,
					"the replacement still expires normally after a rolled-back character change") && ok;
			}
		}
	}
	return ok;
}

bool runMergedEntitySaveTests()
{
	return runMergedEntityListSaveLoadRoundTripTests();
}

bool runQingyuUiTests()
{
	const auto assetsRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "assets";
	ScopedActiveResourceRoot resourceRoot;
	if (!check(resourceRoot.valid(), "Qingyu isolates writable resources")) return false;
	File::setSharedApplicationRootForTests(File::getActiveResourceRoot());
	const bool initializeTtf = TTF_WasInit() == 0;
	if (!SDL_Init(0) || (initializeTtf && !TTF_Init())) return false;
	const bool previousTheme = Config::useQingyuUi;
	int previousWidth = 0, previousHeight = 0;
	CoreLifecycleTestAccess::getLogicalSize(previousWidth, previousHeight);
	Engine* engine = Engine::getInstance();
	engine->setFontName((assetsRoot / "engine/font/font.ttf").string());
	bool ok = true;
	for (const auto& selection : std::vector<std::pair<std::string, std::string>>{
		{ "jxqy2", "jxqy2" }, { "yycs", "yycs" }, { "xjxqy", "xjxqy" },
		{ u8"江湖余尘", "yycs" }, { u8"潇湘行", "yycs" },
		{ u8"江湖余尘二", "yycs" }, { u8"月眉儿外传", "yycs" },
		{ u8"剑二改承合版", "jxqy2" }, { u8"新月无痕", "jxqy2" } })
	{
		const auto& profile = selection.first;
		File::setResourceFallbackRoots({ (assetsRoot / std::filesystem::u8path(profile)).u8string(),
			(assetsRoot / "jxqy2").u8string(), (assetsRoot / "yycs").u8string() });
		const auto& uiProfile = selection.second;
		File::setUiResourceFallbackRoots({ (assetsRoot / std::filesystem::u8path(profile)).u8string(),
			(assetsRoot / uiProfile).u8string() }, true, (assetsRoot / "common").u8string());
		for (const auto viewport : { Point{ 1280, 720 }, Point{ 800, 480 }, Point{ 640, 480 } })
		{
			auto surface = make_shared_surface(SDL_CreateSurface(viewport.x, viewport.y, SDL_PIXELFORMAT_ARGB8888));
			SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface.get()) : nullptr;
			if (!check(renderer != nullptr, "create the real UI software renderer")) return false;
			auto previousRenderer = CoreLifecycleTestAccess::exchangeRenderer(renderer);
			CoreLifecycleTestAccess::setLogicalSize(viewport.x, viewport.y);
			engine->setFontName((assetsRoot / "engine/font/font.ttf").string());
			{
				GameManager game;
				ResourceManifest manifest;
				ok = check(manifest.loadFromFile("game_profile.ini"), ("read production profile for " + profile).c_str()) && ok;
				Config::useQingyuUi = false;
				game.global.applyResourceManifestFeatures(manifest);
				game.global.loadUiSettings();
				const auto goods = game.global.goodsLayout;
				const auto magic = game.global.magicLayout;
				Config::useQingyuUi = true;
				game.global.applyResourceManifestFeatures(manifest);
				game.global.loadUiSettings();
				const auto& themedGoods = game.global.goodsLayout;
				const auto& themedMagic = game.global.magicLayout;
				ok = check(game.global.feature.qingyuUi
					&& goods.storeBegin == themedGoods.storeBegin && goods.storeEnd == themedGoods.storeEnd
					&& goods.equipBegin == themedGoods.equipBegin && goods.equipEnd == themedGoods.equipEnd
					&& goods.bottomBegin == themedGoods.bottomBegin && goods.bottomEnd == themedGoods.bottomEnd
					&& magic.storeBegin == themedMagic.storeBegin && magic.storeEnd == themedMagic.storeEnd
					&& magic.bottomBegin == themedMagic.bottomBegin && magic.bottomEnd == themedMagic.bottomEnd
					&& magic.practiceIndex == themedMagic.practiceIndex && magic.talentBegin == themedMagic.talentBegin
					&& magic.talentEnd == themedMagic.talentEnd, ("theme preserves production inventory and magic indices for " + profile).c_str()) && ok;
				game.goodsManager.configureLayout();
				game.magicManager.configureLayout();
				game.menu->init();
				game.menu->toggleStateView();
				ok = check(game.menu->equipMenu->visible && game.menu->topMenu->equipBtn
					&& game.menu->topMenu->equipBtn->checked,
					"the character entry tracks the integrated equipment panel") && ok;
				game.menu->toggleStateView();
				ok = check(!game.menu->equipMenu->visible && !game.menu->topMenu->equipBtn->checked,
					"closing the character panel clears its entry selection") && ok;
				for (const char* itemFile : { u8"goods-cloth-1-书生服.ini", u8"goods-jian-1-桃木剑.ini",
					u8"goods-huwan-3-皮护腕.ini", u8"goods-toukui-10-铁盔.ini",
					u8"goods-jian-5-龙泉剑.ini", u8"book-天师符法秘笈.ini" })
				{
					game.goodsManager.addItem(itemFile, 1);
				}
				for (const char* magicFile : { u8"magic-百变掌法.ini", u8"magic-风残剑法.ini",
					u8"magic-封神十二剑.ini" })
				{
					game.magicManager.addMagic(magicFile);
				}
				game.memo.add(u8"渡口的船家正在等候。先去集市购置药品，再与同伴商议下一程。");
				game.menu->memoMenu->reset();
				game.player->npcName = u8"江湖客";
				game.player->money = 12880;
				game.player->level = 28;
				game.player->life = game.player->info.lifeMax = 860;
				game.player->mana = 520;
				game.player->info.manaMax = 680;
				game.player->thew = game.player->info.thewMax = 320;
				game.player->info.attack = 258;
				game.player->info.defend = 196;
				game.player->info.evade = 72;
				game.player->exp = 2450;
				game.player->levelUpExp = 3600;
				if (auto equipmentInfo = game.goodsManager.findGoods(u8"goods-jian-5-龙泉剑.ini"))
				{
					const int bagIndex = static_cast<int>(equipmentInfo - game.goodsManager.goodsList.data());
					ok = check(game.goodsManager.useItem(bagIndex)
						&& game.goodsManager.goodsListExists(game.goodsManager.equipIndex(4)),
						"equipping a real weapon still uses the original equipment slot") && ok;
				}
				auto equipment = game.menu->equipMenu;
				auto bag = game.menu->goodsMenu;
				auto spells = game.menu->magicMenu;
				const bool hasTalents = magic.talentBegin >= 0;
				ok = check(spells->getComponentByName("magicTab")->visible == hasTalents
					&& spells->getComponentByName("talentTab")->visible == hasTalents
					&& spells->getComponentByName<Label>("heading")->getStr() == (hasTalents ? u8"武学" : u8"武功"),
					"single-category magic pages omit redundant tabs while talent games retain both categories") && ok;
				ok = check(equipment->nineSlice == 128 && IMP::loadImageForTime(equipment->impImage, 0) != nullptr
					&& equipment->getComponentByName<Label>("labLife") != nullptr
					&& bag->item.size() == 12 && spells->item.size() == 12,
					"production theme loads its art, integrated statistics, and 12-cell grids") && ok;
				ok = check(bag->scrollbar->style == ssVertical && spells->scrollbar->style == ssVertical,
					"inventory scrollbars keep their vertical drag direction") && ok;
				ok = check(equipment->rect.x + equipment->rect.w <= bag->rect.x,
					("equipment and bag remain simultaneously usable at " + std::to_string(viewport.x)).c_str()) && ok;
				const auto goodsGroup = game.menu->bottomMenu->getComponentByName<Label>("goodsGroup");
				const auto magicGroup = game.menu->bottomMenu->getComponentByName<Label>("magicGroup");
				ok = check(goodsGroup && magicGroup && goodsGroup->getStr() == u8"物品" && magicGroup->getStr() == u8"武功"
					&& goodsGroup->rect.x + goodsGroup->rect.w <= game.menu->bottomMenu->goodsItem[0]->rect.x
					&& game.menu->bottomMenu->goodsItem[2]->rect.x + game.menu->bottomMenu->goodsItem[2]->rect.w <= magicGroup->rect.x
					&& magicGroup->rect.x + magicGroup->rect.w <= game.menu->bottomMenu->magicItem[0]->rect.x
					&& game.menu->bottomMenu->magicItem[4]->rect.x + game.menu->bottomMenu->magicItem[4]->rect.w <= viewport.x,
					"quickbar names and spacing distinguish the original three goods and five magic slots") && ok;
				ok = check(game.menu->bottomMenu->getComponentByName("key0")->getPriority()
					< game.menu->bottomMenu->getComponentByName("keyCap0")->getPriority()
					&& game.menu->bottomMenu->getComponentByName("keyCap0")->getPriority()
					< game.menu->bottomMenu->goodsItem[0]->getPriority(),
					"quick-slot key badges draw over item art without replacing slot hit targets") && ok;
				equipment->updateGoods();
				bag->updateGoods();
				bag->updateMoney();
				CoreLifecycleTestAccess::advanceActorFrame(*game.menu->columnMenu, 0);
				// Real definitions and icons stay in the selected resource chain.
				game.scriptAPI.addMagic(u8"001达摩真经.ini");
				if (auto learned = game.magicManager.findPrimaryMagic(u8"001达摩真经.ini"))
				{
					const int source = static_cast<int>(learned - game.magicManager.magicList.data());
					const auto cooldownMagic = learned->magic;
					game.magicManager.exchange(source, game.magicManager.bottomIndex(0));
					game.magicManager.finishMagicUse(cooldownMagic, 60000, true);
				}
				spells->updateMagic();
				game.menu->bottomMenu->updateMagicItem();
				const auto capture = [&](const std::string& name, const std::vector<PElement>& panels)
				{
					SDL_SetRenderDrawColor(renderer, 45, 61, 54, 255);
					SDL_RenderClear(renderer);
					for (const auto& panel : panels)
					{
						const bool wasVisible = panel->visible;
						panel->visible = true;
						CoreLifecycleTestAccess::drawSubtree(*panel);
						panel->visible = wasVisible;
					}
					if (const char* directory = std::getenv("JXQY_TEST_ARTIFACT_DIRECTORY"))
					{
						std::filesystem::create_directories(std::filesystem::u8path(directory));
						auto pixels = make_shared_surface(SDL_RenderReadPixels(renderer, nullptr));
						const auto path = std::filesystem::u8path(directory) / std::filesystem::u8path(
							profile + "-" + std::to_string(viewport.x) + "-" + name + ".png");
						ok = check(pixels && IMG_SavePNG(pixels.get(), path.u8string().c_str()), "write runtime UI screenshot") && ok;
					}
				};
				const auto captureGameplay = [&](const std::string& name, const std::vector<PElement>& panels)
				{
					// Select open pages, then use the game's actual menu hierarchy and draw order.
					// The HUD keeps its runtime visibility instead of being added by each screenshot.
					for (const auto& child : game.menu->upMenu->children)
					{
						child->visible = std::find(panels.begin(), panels.end(), child) != panels.end();
					}
					CoreLifecycleTestAccess::handleMenuEvent(*equipment);
					capture(name, { game.menu });
					auto pixels = make_shared_surface(SDL_RenderReadPixels(renderer, nullptr));
					const auto& column = game.menu->columnMenu;
					bool barsVisible = pixels && column->visible
						&& std::abs(column->columnLife->percent - static_cast<float>(game.player->life)
							/ std::max(1, game.player->info.lifeMax)) < 0.001f
						&& column->getComponentByName<Label>("valueLife")->getStr()
							== convert::formatString(u8"命  %d / %d", game.player->life, game.player->info.lifeMax);
					for (const auto& bar : std::vector<std::pair<std::shared_ptr<ColumnImage>, unsigned int>>{
						{ column->columnLife, 0x985C51 }, { column->columnThew, 0x98814B },
						{ column->columnMana, 0x508785 } })
					{
						const auto& rect = bar.first->rect;
						for (const int offset : { (rect.w - 2) / 4, (rect.w - 2) * 3 / 4 })
						{
							const unsigned int expected = offset < static_cast<int>((rect.w - 2) * bar.first->percent)
								? bar.second : 0x2A372E;
							Uint8 red = 0, green = 0, blue = 0, alpha = 0;
							barsVisible = pixels && SDL_ReadSurfacePixel(pixels.get(), rect.x + 1 + offset,
								rect.y + rect.h - 2, &red, &green, &blue, &alpha)
								&& red == ((expected >> 16) & 255) && green == ((expected >> 8) & 255)
								&& blue == (expected & 255) && alpha == 255 && barsVisible;
						}
					}
					ok = check(barsVisible, (profile + " " + std::to_string(viewport.x) + " " + name
						+ ": health, stamina and mana remain visible in the composed menu").c_str()) && ok;
					for (int key = 0; key < 8; ++key)
					{
						const auto badge = game.menu->bottomMenu->getComponentByName<Label>("key" + std::to_string(key));
						bool textVisible = false;
						for (int y = badge->rect.y; y < badge->rect.y + badge->rect.h && !textVisible; ++y)
						{
							for (int x = badge->rect.x; x < badge->rect.x + badge->rect.w; ++x)
							{
								Uint8 red = 0, green = 0, blue = 0, alpha = 0;
								textVisible = pixels && SDL_ReadSurfacePixel(pixels.get(), x, y, &red, &green, &blue, &alpha)
									&& red > 210 && green > 200 && blue > 175 && alpha == 255;
								if (textVisible) break;
							}
						}
						ok = check(textVisible, "composed quick-slot key letters remain visible above their badges") && ok;
					}
				};
				for (const int life : { 0, game.player->info.lifeMax })
				{
					game.player->life = life;
					CoreLifecycleTestAccess::advanceActorFrame(*game.menu->columnMenu, 0);
					captureGameplay(life == 0 ? "hud-empty" : "hud-full", {});
				}
				game.player->life = game.player->info.lifeMax / 2;
				game.player->thew = game.player->info.thewMax * 3 / 4;
				game.player->mana = game.player->info.manaMax / 2;
				CoreLifecycleTestAccess::advanceActorFrame(*game.menu->columnMenu, 0);
				equipment->updateGoods();
				captureGameplay("equipment", { equipment, bag });
				equipment->focusControllerEquipment();
				equipment->handleUIAction(UIAction::NavigateUp);
				equipment->handleUIAction(UIAction::NavigateRight);
				ok = check(equipment->isShowingAttributes() && !equipment->item[0]->visible
					&& !equipment->item[0]->activated && equipment->controllerFocusCandidates().size() == 2,
					"character tabs expose attributes without hidden equipment targets") && ok;
				ok = check(equipment->getComponentByName<Label>("detail0")->getStr()
					== convert::formatString(u8"攻击  %d", game.player->getAttack()),
					"character details use the active combat getter") && ok;
				captureGameplay("attributes", { equipment, bag });
				equipment->handleUIAction(UIAction::Cancel);
				equipment->handleUIAction(UIAction::NavigateDown);
				ok = check(!equipment->isShowingAttributes() && equipment->controllerFocusedElement() == equipment->item[0],
					"character details return to the equipment grid with one cancel") && ok;
				for (const bool modified : { true, false })
				{
					game.player->weakMagic = modified ? std::make_shared<Magic>() : nullptr;
					game.player->morphMagic = modified ? std::make_shared<Magic>() : nullptr;
					if (modified)
					{
						game.player->weakMagic->weakAttackPercent = 20;
						game.player->weakMagic->weakDefendPercent = 20;
						game.player->morphMagic->attackAddPercent = 50;
						game.player->morphMagic->defendAddPercent = 50;
						game.player->morphMagic->evadeAddPercent = 50;
					}
					CoreLifecycleTestAccess::handleMenuEvent(*equipment);
					equipment->showAttributes(true);
					ok = check(equipment->getComponentByName<Label>("labAttack")->getStr()
						== equipment->getComponentByName<Label>("detail0")->getStr()
						&& equipment->getComponentByName<Label>("labDefend")->getStr()
						== equipment->getComponentByName<Label>("detail1")->getStr()
						&& equipment->getComponentByName<Label>("labEvade")->getStr()
						== equipment->getComponentByName<Label>("detail6")->getStr()
						&& game.getBindValue("player.info.attack") == game.player->info.attack
						&& game.getBindValue("player.info.defend") == game.player->info.defend
						&& game.getBindValue("player.info.evade") == game.player->info.evade,
						"both character pages refresh effective attributes while original bindings keep base values") && ok;
					equipment->showAttributes(false);
				}
				if (auto goodsInfo = game.goodsManager.findGoods(u8"goods-jian-5-龙泉剑.ini"))
				{
					game.menu->toolTip->setGoods(goodsInfo->goods);
					game.menu->toolTip->placeNearElement(bag->item[0]);
					captureGameplay("tooltip", { equipment, bag, game.menu->toolTip });
				}
				{
					auto description = std::make_shared<Goods>();
					description->name = u8"长说明验收物品";
					description->attack2 = 7;
					description->defend3 = 9;
					description->intro = "<color=Red>";
					for (int repeat = 0; repeat < 40; ++repeat)
						description->intro += u8"此物来历久远，详细说明应当逐页完整可读。";
					description->intro += u8"\n<color=17,85,153,127>半透末<color=Default>末页终";
					auto tooltip = game.menu->toolTip;
					const auto pageTextInColor = [tooltip](unsigned int color)
					{
						std::string text;
						for (const auto& row : tooltip->getComponentByName<MemoText>("body")->mstr)
						{
							for (const auto& line : TextLayout::wrapColorTaggedUtf8Text(row->getStr(), 1024, row->color))
							{
								for (const auto& run : line)
								{
									if (run.color == color) text += run.text;
								}
							}
						}
						return text;
					};
					game.menu->showGoodsToolTip(bag, description, bag->item[0]);
					ok = check(pageTextInColor(0xFFFF0000U).find(u8"此物来历久远") != std::string::npos
						&& tooltip->turnPage(UIAction::PageNext)
						&& pageTextInColor(0xFFFF0000U).find(u8"此物来历久远") != std::string::npos,
						"named item colors survive wrapping and continuing onto the next tooltip page") && ok;
					for (int page = 0; page < 160; ++page) tooltip->turnPage(UIAction::PageNext);
					std::string lastPage;
					for (const auto& row : tooltip->getComponentByName<MemoText>("body")->mstr)
						lastPage += row->getStr();
					ok = check(lastPage.find(u8"末页终") != std::string::npos
						&& tooltip->rect.y >= 0 && tooltip->rect.y + tooltip->rect.h <= viewport.y - 108,
						"long item descriptions reach the final page inside the viewport above the quickbar") && ok;
					const auto lastPageColorsPreserved = [&]()
					{
						return pageTextInColor(0x7F115599U) == u8"半透末"
							&& pageTextInColor(tooltip->getComponentByName<MemoText>("body")->color) == u8"末页终";
					};
					ok = check(lastPageColorsPreserved(),
						"item tooltip pagination preserves explicit RGBA and restores the default color") && ok;
					CoreLifecycleTestAccess::resize(*tooltip, viewport.x, viewport.y);
					ok = check(lastPageColorsPreserved(),
						"resizing a paged item tooltip preserves RGBA and default colors") && ok;
					for (int page = 0; page < 160; ++page) tooltip->turnPage(UIAction::PagePrevious);
					ok = check(tooltip->intro1->getStr().find(u8"附加攻击一") != std::string::npos
						&& tooltip->intro1->getStr().find(u8"附加防御二") != std::string::npos,
						"resizing preserves item contents and descriptive additional attribute names") && ok;
					captureGameplay("goods-details", { bag, tooltip });
					auto magicDescription = std::make_shared<Magic>();
					magicDescription->intro = description->intro;
					tooltip->setMagic(magicDescription, 1);
					ok = check(pageTextInColor(0xFFFF0000U).find(u8"此物来历久远") != std::string::npos,
						"magic tooltip descriptions use the same preserved named colors") && ok;
					for (int page = 0; page < 160; ++page) tooltip->turnPage(UIAction::PageNext);
					ok = check(lastPageColorsPreserved(),
						"magic tooltip pagination preserves explicit RGBA and default colors") && ok;
					tooltip->hide();
				}
				{
					auto partner = std::make_shared<NPC>();
					partner->kind = nkPartner;
					partner->npcName = u8"同行侠客";
					partner->canEquip = 1;
					partner->level = 99;
					game.npcManager->addNPC(partner);
					ok = check(game.menu->openPartnerEquipment(partner, false), "Qingyu opens active partner equipment") && ok;
					auto panel = game.menu->partnerEquipMenu;
					auto weapon = game.goodsManager.findGoods(u8"goods-jian-1-桃木剑.ini");
					if (weapon)
					{
						auto target = panel->getComponentByName<Item>("item5");
						target->dropType = dtGoods;
						target->dropIndex = static_cast<int>(weapon - game.goodsManager.goodsList.data());
						target->result = erDropped;
						CoreLifecycleTestAccess::handleMenuEvent(*panel);
						ok = check(partner->getEquipmentFileByPartIndex(4) == u8"goods-jian-1-桃木剑.ini",
							"dropping a real bag weapon equips the intended partner body slot") && ok;
						CoreLifecycleTestAccess::resize(*panel, viewport.x, viewport.y);
						ok = check(panel->getPartner() == partner && panel->rect.x >= 0
							&& panel->rect.x + panel->rect.w <= bag->rect.x
							&& panel->rect.y + panel->rect.h <= viewport.y - 108,
							"partner resize retains selection and leaves bag and HUD regions accessible") && ok;
						captureGameplay("partner", { panel, bag });
						target = panel->getComponentByName<Item>("item5");
						target->result = erMouseRDown;
						CoreLifecycleTestAccess::handleMenuEvent(*panel);
						ok = check(partner->getEquipmentFileByPartIndex(4).empty()
							&& game.goodsManager.findGoods(u8"goods-jian-1-桃木剑.ini"),
							"partner unequip returns the same weapon to the player's bag") && ok;
					}
					else ok = check(false, "production partner test weapon exists") && ok;
					game.menu->closePartnerEquipment(true);
					game.npcManager->removeNPCOnlyFromList(partner);
				}
				{
					auto shop = game.menu->buySellMenu;
					shop->visible = true;
					shop->bsKind = bsBuy;
					shop->addGoodsItem(u8"goods-jian-1-桃木剑.ini", 2);
					shop->numberValid = true;
					shop->buyPercent = 125;
					shop->recyclePercent = 40;
					const int previousMoney = game.player->money;
					const int buyPrice = shop->goodsList[0].goods->getBuyPrice(shop->buyPercent);
					const int sellPrice = shop->goodsList[0].goods->getSellPrice(shop->recyclePercent);
					CoreLifecycleTestAccess::handleMenuEvent(*shop);
					const bool previousInEvent = game.inEvent;
					const bool previousCanInput = game.global.data.canInput;
					for (bool scriptEvent : { true, false })
					{
						game.inEvent = scriptEvent;
						game.global.data.canInput = scriptEvent;
						game.controller->processPhysicalInputFrame();
						game.controller->processPhysicalInputFrame();
						ok = check(engine->inputActions().isInputContextActive(),
							"shop hover regression exercises active physical input") && ok;
						for (bool shopSide : { true, false })
						{
							const PElement owner = shopSide ? std::static_pointer_cast<Element>(shop) : bag;
							const auto slot = shopSide ? shop->item[0] : bag->item[0];
							slot->result = erShowHint;
							CoreLifecycleTestAccess::handleMenuEvent(*owner);
							for (int frame = 0; frame < 3; ++frame)
							{
								game.controller->processPhysicalInputFrame();
							}
							ok = check(game.menu->toolTip->visible && game.menu->toolTip->parent == shop.get(),
								"shop and bag hover hints stay above the shop during script-blocked world input") && ok;
							const auto shownGoods = shopSide ? shop->goodsList[0].goods
								: game.goodsManager.goodsList[slot->dragIndex].goods;
							const int expectedPrice = shopSide ? shownGoods->getBuyPrice(shop->buyPercent)
								: shownGoods->getSellPrice(shop->recyclePercent);
							ok = check(game.menu->toolTip->cost->getStr() == (shopSide ? u8"买入价： " : u8"卖出价： ")
								+ std::to_string(expectedPrice), "trade tooltip uses the price for its source pane") && ok;
							slot->result = erHideHint;
							CoreLifecycleTestAccess::handleMenuEvent(*owner);
							ok = check(!game.menu->toolTip->visible,
								"leaving either shop pane still hides its hover hint") && ok;
						}
					}
					game.inEvent = previousInEvent;
					game.global.data.canInput = previousCanInput;
					shop->item[0]->result = erMouseRDown;
					CoreLifecycleTestAccess::handleMenuEvent(*shop);
					CoreLifecycleTestAccess::handleMenuEvent(*shop);
					ok = check(game.player->money == previousMoney - buyPrice && shop->goodsList[0].number == 1
						&& shop->getComponentByName<Label>("money")->getStr() == bag->money->getStr(),
						"Qingyu purchase updates finite stock and both displayed money totals") && ok;
					capture("shop", { game.menu, shop, bag });
					if (auto owned = game.goodsManager.findGoods(u8"goods-jian-1-桃木剑.ini"))
					{
						shop->canSellSelfGoods = false;
						game.menu->showGoodsToolTip(shop, owned->goods, bag->item[0]);
						ok = check(game.menu->toolTip->cost->getStr() == u8"当前只能购买",
							"buy-only shop explains why player goods cannot be sold") && ok;
						shop->canSellSelfGoods = true;
						shop->bsKind = bsSell;
						game.menu->showGoodsToolTip(shop, shop->goodsList[0].goods, shop->item[0]);
						ok = check(game.menu->toolTip->cost->getStr() == u8"买入价： " + std::to_string(buyPrice),
							"resale stock uses the actual buy-back price") && ok;
						shop->bsKind = bsBuy;
						game.menu->hideToolTip();
						shop->sellOneFromPlayerSlot(static_cast<int>(owned - game.goodsManager.goodsList.data()));
						CoreLifecycleTestAccess::handleMenuEvent(*shop);
						ok = check(game.player->money == previousMoney - buyPrice + sellPrice
							&& shop->goodsList[0].number == 2
							&& shop->getComponentByName<Label>("money")->getStr() == bag->money->getStr(),
							"selling restores shop stock and refreshes both money labels") && ok;
					}
					shop->handleUIAction(UIAction::Cancel);
					ok = check(!CoreLifecycleTestAccess::logicRunning(*shop), "shop cancel ends its operation loop") && ok;
					shop->visible = false;
				}
				captureGameplay("magic", { game.menu->practiceMenu, spells });
				if (auto selected = game.magicManager.findPrimaryMagic(u8"magic-风残剑法.ini"))
				{
					const int source = static_cast<int>(selected - game.magicManager.magicList.data());
					selected->remainColdMilliseconds = 1234;
					selected->exp = 125;
					const auto snapshot = game.magicManager.magicList;
					const auto selectedMagic = selected->magic;
					const auto originalCooldown = selectedMagic->coldMilliSeconds;
					selectedMagic->coldMilliSeconds = 1500;
					const int visibleIndex = source - game.magicManager.storeBegin();
					spells->item[visibleIndex]->result = erClick;
					CoreLifecycleTestAccess::handleMenuEvent(*spells);
					ok = check(spells->isShowingDetails() && !spells->item[0]->visible
						&& spells->getComponentByName<FlatTextButton>("detailPractice")->visible,
						"clicking a real spell opens actionable details") && ok;
					captureGameplay("magic-details", { equipment, spells });
					const auto originalIntro = selectedMagic->intro;
					for (int repeat = 0; repeat < 40; ++repeat) selectedMagic->intro += u8"长篇武学说明，逐页可读。";
					selectedMagic->intro += u8"末页终";
					CoreLifecycleTestAccess::advanceActorFrame(*spells, 0);
					for (int page = 0; page < 160; ++page) spells->handleUIAction(UIAction::PageNext);
					std::string lastPage;
					for (const auto& line : spells->getComponentByName<MemoText>("detailText")->mstr) lastPage += line->getStr();
					ok = check(lastPage.find(u8"末页终") != std::string::npos
						&& !spells->getComponentByName<FlatTextButton>("detailNext")->activated,
						"long non-ASCII descriptions reach their final page without clipping") && ok;
					selectedMagic->intro = originalIntro;
					spells->closeDetails();
					spells->showDetails(source);
					spells->focusControllerElement(spells->getComponentByName("detailPractice"));
					spells->handleUIAction(UIAction::Confirm);
					const auto& practiced = game.magicManager.magicList[game.magicManager.practiceIndex()];
					ok = check(!spells->isShowingDetails() && practiced.magic == selectedMagic
						&& practiced.exp == 125 && practiced.remainColdMilliseconds == 1234,
						"practice action preserves the actual spell, experience and cooldown") && ok;
					game.magicManager.magicList = snapshot;
					for (int index = game.magicManager.bottomBegin(); index <= game.magicManager.bottomEnd(); ++index)
						game.magicManager.magicList[index] = snapshot[source];
					spells->showDetails(source);
					ok = check(!spells->assignDetailedMagic(false) && game.magicManager.magicList[source].magic == selectedMagic,
						"a full quickbar leaves the selected spell in its original slot") && ok;
					game.magicManager.magicList[game.magicManager.bottomIndex(2)] = {};
					spells->focusControllerElement(spells->getComponentByName("detailQuick"));
					spells->handleUIAction(UIAction::Confirm);
					ok = check(!spells->isShowingDetails()
						&& game.magicManager.magicList[game.magicManager.bottomIndex(2)].magic == selectedMagic,
						"quickbar action uses the first available original slot") && ok;
					game.magicManager.magicList = snapshot;
					spells->showDetails(source);
					game.magicManager.magicList[source] = {};
					ok = check(!spells->assignDetailedMagic(true), "a removed detail selection cannot act on a replacement slot") && ok;
					CoreLifecycleTestAccess::advanceActorFrame(*spells, 0);
					ok = check(!spells->isShowingDetails(), "removed selections return to the list") && ok;
					game.magicManager.magicList = snapshot;
					game.magicManager.updateMenu();
					selectedMagic->coldMilliSeconds = originalCooldown;
				}
				else ok = check(false, "production sword skill is available for detail interactions") && ok;
				if (magic.talentBegin >= 0)
				{
					game.magicManager.addPrimaryMagic(u8"magic-别离心法.ini", false, false, true);
				}
				spells->showTalents(true);
				ok = check(spells->isShowingTalents() == (magic.talentBegin >= 0),
					"talent tab follows the existing game layout") && ok;
				if (spells->isShowingTalents())
				{
					for (const auto& slot : spells->item)
						ok = check(!slot->canDrag && !slot->canDrop && slot->dragIndex >= magic.talentBegin,
							"passive talent slots cannot move into ordinary spell slots") && ok;
					captureGameplay("talents", { spells });
					const auto before = game.magicManager.magicList[magic.talentBegin];
					spells->focusControllerDefault();
					spells->handleUIAction(UIAction::Secondary);
					spells->handleUIAction(UIAction::Confirm);
					ok = check(spells->isShowingDetails() && !spells->assignDetailedMagic(true)
						&& !spells->assignDetailedMagic(false)
						&& !spells->getComponentByName<FlatTextButton>("detailPractice")->visible
						&& !spells->getComponentByName<FlatTextButton>("detailQuick")->visible,
						"talent details do not expose active-spell operations") && ok;
					captureGameplay("talent-details", { spells });
					ok = check(game.magicManager.magicList[magic.talentBegin].iniFile == before.iniFile
						&& !game.menu->controllerTransfers().active(ControllerSlotKind::Magic),
						"talent confirmation and secondary actions preserve passive spell ownership") && ok;
					spells->handleUIAction(UIAction::Cancel);
					ok = check(spells->isControllerFocusActive() && !spells->isShowingDetails(),
						"returning from talent details restores active grid navigation") && ok;
					spells->handleUIAction(UIAction::NavigateUp);
					spells->handleUIAction(UIAction::NavigateLeft);
					ok = check(!spells->isShowingTalents(), "gamepad navigation reaches and switches talent tabs") && ok;
					spells->handleUIAction(UIAction::NavigateDown);
					ok = check(spells->controllerFocusedElement() == spells->item.front(),
						"gamepad returns from tabs to the first skill slot") && ok;
				}
				spells->showTalents(false);
				spells->scrollbar->setPosition(spells->scrollbar->max);
				spells->updateMagic();
				ok = check(spells->scrollbar->slideBtn->rect.y + spells->scrollbar->slideBtn->rect.h
					<= spells->scrollbar->rect.y + spells->scrollbar->rect.h,
					"last-page scrollbar thumb stays inside the scaled track") && ok;
				for (const auto& panel : { std::string("system"), std::string("option"), std::string("saveload") })
				{
					PElement menu;
					if (panel == "system") menu = std::make_shared<System>();
					if (panel == "option") menu = std::make_shared<Option>();
					if (panel == "saveload") menu = std::make_shared<SaveLoad>(true, true);
					capture(panel, { menu });
					if (panel == "option")
					{
						auto option = std::dynamic_pointer_cast<Option>(menu);
						ok = check(option->themeButton != nullptr
							&& option->focusManager.focusNode("ui-theme"), "theme preference is reachable through settings focus") && ok;
						option->handleUIAction(UIAction::Confirm);
						ok = check(!Config::useQingyuUi && game.global.feature.qingyuUi,
							"changing the saved preference leaves the active game UI coherent") && ok;
						option->handleUIAction(UIAction::Confirm);
					}
				}
				const auto dialogueLabel = game.menu->dialog->getComponentByName<TalkLabel>("label");
				const auto dialoguePages = dialogueLabel->splitTalkString(u8"甲<EnTeR>乙\n丙");
				ok = check(dialoguePages.size() == 2 && dialoguePages[0].talkChar.size() == 1
					&& dialoguePages[1].talkChar.size() == 2
					&& dialoguePages[1].talkChar[0].row == 0
					&& dialoguePages[1].talkChar[1].row == 1,
					("themed dialogue preserves forced pages and literal line breaks for " + profile).c_str()) && ok;
				game.menu->dialog->setTalkStr(u8"江湖路远，且行且看。前方便是渡口，若要继续赶路，不妨先去集市补充药品，再与同伴商议行程。");
				game.menu->dialog->getComponentByName<TalkLabel>("label")->showAllImmediately();
				capture("dialog", { game.menu->dialog });
				CoreLifecycleTestAccess::prepareRenderedChoice(*game.menu->chooseMenu, false,
					{ u8"去集市补充药品", u8"查看同行伙伴的装备", u8"继续赶路" });
				capture("choice", { game.menu->chooseMenu });
				captureGameplay("memo", { game.menu->memoMenu });
				captureGameplay("map", { game.menu->mapThumbnailMenu });
				Config::useQingyuUi = false;
				game.global.applyResourceManifestFeatures(manifest);
				bag->init();
				ok = check(!game.global.feature.qingyuUi && bag->nineSlice == 0,
					"turning off the theme reloads original menu definitions") && ok;
				auto originalOptions = std::make_shared<Option>();
				if (originalOptions->themeButton)
				{
					const auto& button = originalOptions->themeButton->rect;
					ok = check(button.x >= 0 && button.y >= 0
						&& button.x + button.w <= viewport.x && button.y + button.h <= viewport.y,
						"the theme selector is visible in the original game UI at every supported size") && ok;
					for (const auto& control : { PElement(originalOptions->rtnBtn),
						PElement(originalOptions->touchControlsButton), PElement(originalOptions->cheatSettingsButton) })
					{
						if (!control) continue;
						const auto& target = control->rect;
						ok = check(button.x + button.w <= target.x || target.x + target.w <= button.x
							|| button.y + button.h <= target.y || target.y + target.h <= button.y,
							"the theme selector preserves existing return and footer hit areas") && ok;
					}
				}
				capture("original-option", { originalOptions });
			}
			BaseComponent().tryCleanRes();
			CoreLifecycleTestAccess::exchangeRenderer(previousRenderer);
			SDL_DestroyRenderer(renderer);
		}
	}
	Config::useQingyuUi = previousTheme;
	File::setSharedApplicationRootForTests("");
	CoreLifecycleTestAccess::setLogicalSize(previousWidth, previousHeight);
	engine->setFontName("");
	if (initializeTtf) TTF_Quit();
	return ok;
}

bool runCurrentModCompatibilityTests()
{
	int previousWidth = 0, previousHeight = 0;
	CoreLifecycleTestAccess::getLogicalSize(previousWidth, previousHeight);
	// Standalone tests have no window; choice pagination still needs a real viewport.
	CoreLifecycleTestAccess::setLogicalSize(1280, 720);
	bool ok = runProductionModCooldownTests();
	ok = runProductionYuchenTalentVariableTests() && ok;
	ok = runProductionYuchenArenaRoutes() && ok;
	CoreLifecycleTestAccess::setLogicalSize(previousWidth, previousHeight);
	return ok;
}

bool runSaveWriteSharingRuntimeTests()
{
	return runSaveWriteSharingFailureTests();
}

bool runSaveStabilityTests()
{
	return runSaveStabilitySoakTests();
}

bool runXiaoxiangMissingObjectRouteTests()
{
	return runProductionXiaoxiangMissingObjectRouteTests();
}

bool runXiaoxiangTournamentRouteTests()
{
	return runProductionXiaoxiangTournamentRouteTests();
}

bool runXiaoxiangPrisonEndingTests()
{
	return runProductionXiaoxiangPrisonEndingTests();
}

bool runXiaoxiangCompanionHistoryTests(int requestedLoadMode)
{
	return runProductionXiaoxiangCompanionHistoryTests(requestedLoadMode);
}

bool runXiaoxiangLegacyEntranceTests()
{
	return runProductionXiaoxiangLegacyEntranceTests();
}

bool runMoonlightTrapRouteTests()
{
	return runProductionMoonlightTrapRouteTests();
}

bool runMoonlightDepartureTests()
{
	return runProductionMoonlightDepartureTests();
}

bool runNewSwordBoatRouteTests()
{
	return runProductionNewSwordBoatRouteTests();
}

bool runNewSwordYangYingRouteTests()
{
	return runProductionNewSwordYangYingRouteTests();
}

bool runMoonlightEndingTwoRouteTests()
{
	return runProductionMoonlightEndingTwoRouteTests();
}

bool runSwordTwoPartnerDepartureTests()
{
	return runProductionSwordTwoPartnerDepartureTests();
}

bool runBilibiliStoryFeedbackTests()
{
	bool ok = runProductionNewSwordFeedbackTests();
	ok = runProductionMoonlightRescueReturnTests() && ok;
	return runProductionHanboRouteTests() && ok;
}

bool runSwordTwoHistoricalScriptTests()
{
	return runProductionSwordTwoHistoricalScriptTests();
}

bool runScriptMovementRuntimeTests()
{
	bool ok = runScriptMovementContractTests();
	ok = runScriptBlockedMovementRetryTests() && ok;
	ok = runScriptArenaGateDetourTests() && ok;
	ok = runScriptPrisonDetourTests() && ok;
	ok = runScriptOccupiedDestinationTests() && ok;
	ok = runMovementRetargetReservationTests() && ok;
	return runScriptAsyncMovementReplacementTests() && ok;
}

bool runCoreLifecycleTests()
{
	const auto timed = [](const char* name, const auto& test)
	{
		if (const char* filter = std::getenv("JXQY_CORE_TEST_FILTER"))
		{
			if (std::string(name) != filter)
			{
				return true;
			}
		}
		const auto started = std::chrono::steady_clock::now();
		std::cout << "CoreTiming begin\t" << name << std::endl;
		const bool passed = test();
		const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - started).count();
		std::cout << "CoreTiming end\t" << name << "\tms=" << elapsed
			<< "\tpassed=" << passed << std::endl;
		return passed;
	};
	bool ok = true;
	ok = timed("runCompositionLayeringTests", runCompositionLayeringTests) && ok;
	ok = timed("runMainThreadOwnershipTests", runMainThreadOwnershipTests) && ok;
	ok = timed("runEditorRunWindowClosePolicyTests", runEditorRunWindowClosePolicyTests) && ok;
	ok = timed("runLoadingInputNeutralityTests", runLoadingInputNeutralityTests) && ok;
	ok = timed("runSaveFeedbackRenderingTests", runSaveFeedbackRenderingTests) && ok;
	ok = timed("runQuitLatchTests", runQuitLatchTests) && ok;
	ok = timed("runElementRunExceptionCleanupTests", runElementRunExceptionCleanupTests) && ok;
	ok = timed("runScriptSleepLifecycleTests", runScriptSleepLifecycleTests) && ok;
	ok = timed("runMidFrameTerminalQuitTests", runMidFrameTerminalQuitTests) && ok;
	ok = timed("runApplicationInactiveTests", runApplicationInactiveTests) && ok;
	ok = timed("runDeferredResizeLifecycleTests", runDeferredResizeLifecycleTests) && ok;
	ok = timed("runCameraViewportResizeTests", runCameraViewportResizeTests) && ok;
	ok = timed("runGameplayPauseTests", runGameplayPauseTests) && ok;
	ok = timed("runSceneResultTests", runSceneResultTests) && ok;
	ok = timed("runScriptDispatchCharacterizationTests", runScriptDispatchCharacterizationTests) && ok;
	ok = timed("runScriptMovementContractTests", runScriptMovementContractTests) && ok;
	ok = timed("runScriptInterfaceVisibilityTests", runScriptInterfaceVisibilityTests) && ok;
	ok = timed("runScriptArenaGateDetourTests", runScriptArenaGateDetourTests) && ok;
	ok = timed("runScriptPrisonDetourTests", runScriptPrisonDetourTests) && ok;
	ok = timed("runScriptOccupiedDestinationTests", runScriptOccupiedDestinationTests) && ok;
	ok = timed("runMovementRetargetReservationTests", runMovementRetargetReservationTests) && ok;
	ok = timed("runPartnerYieldMovementTests", runPartnerYieldMovementTests) && ok;
	ok = timed("runJumpMovementReservationTests", runJumpMovementReservationTests) && ok;
	ok = timed("runHurtMovementRetargetTests", runHurtMovementRetargetTests) && ok;
	ok = timed("runScriptAsyncMovementReplacementTests", runScriptAsyncMovementReplacementTests) && ok;
	ok = timed("runScriptActionMovementReplacementTests", runScriptActionMovementReplacementTests) && ok;
	ok = timed("runRandRunAndMapPositionContracts", runRandRunAndMapPositionContracts) && ok;
	ok = timed("runScriptSpeedAndStatusTests", runScriptSpeedAndStatusTests) && ok;
	ok = timed("runProductionMedicineChestTests", runProductionMedicineChestTests) && ok;
	ok = timed("runProductionXiaoxiangTrapIsolationTests", runProductionXiaoxiangTrapIsolationTests) && ok;
	ok = timed("runProductionXiaoxiangMoneyPickupTests", runProductionXiaoxiangMoneyPickupTests) && ok;
	ok = timed("runProductionXiaoxiangPrisonRouteTests", runProductionXiaoxiangPrisonRouteTests) && ok;
	ok = timed("runProductionActionResourceTests", runProductionActionResourceTests) && ok;
	ok = timed("runProductionRandomRewardContinuationTests", runProductionRandomRewardContinuationTests) && ok;
	ok = timed("runProductionZhuangDialogueTests", runProductionZhuangDialogueTests) && ok;
	ok = timed("runProductionHanboReturnBranchTests", runProductionHanboReturnBranchTests) && ok;
	ok = timed("runProductionDialogueTextTests", runProductionDialogueTextTests) && ok;
	ok = timed("runProductionLegacyTalkTests", runProductionLegacyTalkTests) && ok;
	ok = timed("runProductionCaocaoDialogueTests", runProductionCaocaoDialogueTests) && ok;
	ok = timed("runPlayerAttributeScriptContracts", runPlayerAttributeScriptContracts) && ok;
	ok = timed("runMoneyAndMessageScriptContracts", runMoneyAndMessageScriptContracts) && ok;
	ok = timed("runProductionMoneyGateTests", runProductionMoneyGateTests) && ok;
	ok = timed("runProductionYiheSystemMessageTests", runProductionYiheSystemMessageTests) && ok;
	ok = timed("runSystemMessageRenderingTests", runSystemMessageRenderingTests) && ok;
	ok = timed("runMapThumbnailLoadingTests", runMapThumbnailLoadingTests) && ok;
	ok = timed("runChooseMenuRenderingTests", runChooseMenuRenderingTests) && ok;
	ok = timed("runProductionDifficultyTableTests", runProductionDifficultyTableTests) && ok;
	ok = timed("runPlayerLevelAttributeProtectionTests", runPlayerLevelAttributeProtectionTests) && ok;
	ok = timed("runProductionAttributeStoryTests", runProductionAttributeStoryTests) && ok;
	ok = timed("runProductionPlayerNameTests", runProductionPlayerNameTests) && ok;
	ok = timed("runActorStateScriptContracts", runActorStateScriptContracts) && ok;
	ok = timed("runProductionWalkIsRunTests", runProductionWalkIsRunTests) && ok;
	ok = timed("runProductionLevelRewardTests", runProductionLevelRewardTests) && ok;
	ok = timed("runObjectAnimationScriptContracts", runObjectAnimationScriptContracts) && ok;
	ok = timed("runProductionObjectAnimationTests", runProductionObjectAnimationTests) && ok;
	ok = timed("runProductionMaskStoryTests", runProductionMaskStoryTests) && ok;
	ok = timed("runProductionWudangGateTests", runProductionWudangGateTests) && ok;
	ok = timed("runProductionHanboRouteTests", runProductionHanboRouteTests) && ok;
	ok = timed("runProductionHanboChoiceTests", runProductionHanboChoiceTests) && ok;
	ok = timed("runChoiceVariableContracts", runChoiceVariableContracts) && ok;
	ok = timed("runMemoGenerationCompatibilityTests", runMemoGenerationCompatibilityTests) && ok;
	ok = timed("runOwnerWorldCommitPhaseTests", runOwnerWorldCommitPhaseTests) && ok;
	ok = timed("runMapActorResetModeTests", runMapActorResetModeTests) && ok;
	ok = timed("runEmptyNpcLoadCancellationTests", runEmptyNpcLoadCancellationTests) && ok;
	ok = timed("runEmptyEntityListSaveLoadRoundTripTests", runEmptyEntityListSaveLoadRoundTripTests) && ok;
	ok = timed("runMergedEntityListSaveLoadRoundTripTests", runMergedEntityListSaveLoadRoundTripTests) && ok;
	ok = timed("runSaveLoadFailureRecoveryTests", runSaveLoadFailureRecoveryTests) && ok;
	ok = timed("runProductionYuchenArenaRoutes", runProductionYuchenArenaRoutes) && ok;
	GameManager gameManager;
	ok = timed("runNewYearPeriodTests", [&]() { return runNewYearPeriodTests(gameManager); }) && ok;
	return ok;
}
