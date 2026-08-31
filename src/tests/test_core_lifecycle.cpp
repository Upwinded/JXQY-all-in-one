#include "../Component/Button.h"
#include "../Element/Element.h"
#include "../Engine/Engine.h"
#include "../File/File.h"
#include "../File/INIReader.h"
#include "../Game/Game.h"
#include "../Game/Data/MemoPersistence.h"
#include "../Game/GameManager/RuntimeSaveGenerationPolicy.h"
#include "../Game/GameManager/SaveGeneration.h"
#include "../Game/GameManager/GameManager.h"
#include "../Game/Data/NewYearPeriod.h"
#include "../Game/Menu/SaveLoad.h"
#include "../Game/Menu/System.h"
#include "../Game/Scene/MainScene.h"
#include "HeadlessPhysicalInputTestHarness.h"
#include "MapV3ContractFixture.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

class CoreLifecycleTestAccess
{
public:
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
			loadingPresentationPumpObserver = {})
	{
		return gameManager.scriptAPI.runExclusiveLoadingTask(
			{},
			std::move(worker),
			std::move(successFinalizer),
			loadingPresentationPumpObserver);
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
		commitPreparedSaveGeneration(
			GameManager& gameManager,
			const std::string& preparedDirectory,
			const SaveGenerationPreflightPolicy& policy,
			const std::function<bool(
				const std::string& generationDirectory,
					const std::function<bool()>&
						ownerCheckpoint)>&
							generationLoadOverride,
			const std::function<bool()>& ownerCheckpoint = {})
	{
		return gameManager.scriptAPI.
			commitPreparedSaveGeneration(
				preparedDirectory,
				policy,
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
	ok = check(
		writeVirtualFile(
			generationDirectory + "\\memo.ini",
			objectMemoIni) &&
			writeVirtualFile(
				generationDirectory + "\\memo.txt",
				validMemo),
		"xjxqy-style object memo.ini and canonical memo.txt fixture is created") &&
		ok;
	gameManager.memo.memo = { "old memo" };
	ok = check(
		gameManager.memo.load(true) &&
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
		gameManager.memo.load(true) &&
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
		gameManager.memo.load(true) &&
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

bool runSaveLoadFailureRecoveryTests()
{
	ScopedActiveResourceRoot resourceRoot;
	if (!check(
			resourceRoot.valid(),
			"save-load test created an isolated resource root"))
	{
		return false;
	}

	SaveGenerationPreflightPolicy policy;
	policy.limits.maximumFileCount = 32;
	policy.limits.maximumTotalBytes = 1024 * 1024;
	policy.limits.maximumSingleFileBytes = 1024 * 1024;
	const std::string preparedDirectory =
		"save/load_candidate";
	Engine* engine = Engine::getInstance();
	engine->resetApplicationQuitRequest();

	bool ok = check(
		File::clearDirectoryFiles("save/game") &&
			File::clearDirectoryFiles(preparedDirectory) &&
			writeVirtualFile(
				preparedDirectory + "/game.ini",
				"[State]\nMap=candidate.map\n"),
		"save-load success fixture is created");
	{
		GameManager gameManager;
		const GameLoading::LoadingTaskResult result =
			CoreLifecycleTestAccess::
				commitPreparedSaveGeneration(
					gameManager,
					preparedDirectory,
					policy,
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
			"a prepared save commits once and refreshes save/game without rollback snapshots") &&
			ok;
	}

	ok = check(
		File::clearDirectoryFiles("save/game") &&
			File::clearDirectoryFiles(preparedDirectory) &&
			writeVirtualFile(
				"save/game/game.ini",
				"[State]\nMap=previous.map\n") &&
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
				commitPreparedSaveGeneration(
					gameManager,
					preparedDirectory,
					policy,
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
					"[State]\nMap=previous.map\n",
			"cancelling after world mutation discards the partial world without reporting content corruption") &&
			ok;
	}

	ok = check(
		File::clearDirectoryFiles("save/game") &&
			File::clearDirectoryFiles(preparedDirectory) &&
			writeVirtualFile(
				"save/game/game.ini",
				"[State]\nMap=previous.map\n") &&
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
				commitPreparedSaveGeneration(
					gameManager,
					preparedDirectory,
					policy,
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
					"[State]\nMap=previous.map\n",
			"a partial save-load failure discards the partial world and returns to title without terminating the application") &&
			ok;
	}

	ok = check(
		File::clearDirectoryFiles("save/game") &&
			File::clearDirectoryFiles(preparedDirectory) &&
			writeVirtualFile(
				"save/game/game.ini",
				"[State]\nMap=previous.map\n") &&
			writeVirtualFile(
				preparedDirectory + "/other.ini",
				"prepared bytes"),
		"save/game refresh failure fixture is created") &&
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
				commitPreparedSaveGeneration(
					gameManager,
					preparedDirectory,
					policy,
					[](
						const std::string&,
						const std::function<bool()>&)
					{
						return true;
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
					u8"无法刷新当前存档目录") !=
					std::string::npos &&
				readVirtualFile("save/game/game.ini") ==
					"[State]\nMap=previous.map\n",
			"a current-save refresh failure discards the mismatched world, preserves the old current generation, and reports the reason") &&
			ok;
	}
	engine->resetApplicationQuitRequest();
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
					mapBytes.size())),
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
		gameManager.varList.ensureInitialized();
		gameManager.traps.beginMapVisit();
		const bool saved = gameManager.saveGame(1);
		INIReader savedGlobal("save/rpg1/game.ini");
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
				savedGlobal.Get("State", "Npc", "missing") ==
					entityCase.npcName &&
				savedGlobal.Get("State", "Obj", "missing") ==
					entityCase.objectName &&
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
		const bool loaded = gameManager.loadGame(1);
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
	}

	struct CorruptEntityListCase
	{
		const char* fileName;
		bool npcList;
		const char* description;
	};
	const CorruptEntityListCase corruptEntityCases[] =
	{
		{ "roundtrip.npc", true, "malformed NPC list" },
		{ "roundtrip.obj", false, "malformed object list" },
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
			writeVirtualFile(slotPath, "[Broken\n");
		ok = check(
			fixtureReady,
			(std::string(corruptCase.description) +
				" fixture is created").c_str()) && ok;
		if (!fixtureReady)
		{
			continue;
		}

		GameManager tolerantLoader;
		const bool loaded = tolerantLoader.loadGame(1);
		INIReader repairedList(
			"save/game/" + std::string(corruptCase.fileName));
		const bool runtimeListEmpty = corruptCase.npcList
			? tolerantLoader.npcManager->npcList.empty()
			: tolerantLoader.objectManager->objectList.empty();
		ok = check(
			loaded &&
				tolerantLoader.getLastLoadFailureMessage().empty() &&
				runtimeListEmpty &&
				readVirtualFile(slotPath) == "[Broken\n" &&
				repairedList.ParseError() == 0 &&
				repairedList.GetInteger("Head", "Count", -1) == 0,
			(std::string(corruptCase.description) +
				" is replaced with an empty current list without rejecting the save").c_str()) &&
			ok;
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
			"\n");
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
			std::to_string(characterIndex) + "\n");
	ok = check(
		fatalMapFixtureReady,
		"missing-map fatal load fixture is created") && ok;
	if (fatalMapFixtureReady)
	{
		{
			GameManager failedLoader;
			const bool loaded = failedLoader.loadGame(1);
			ok = check(
				!loaded &&
					failedLoader.getLastLoadFailureMessage().find(
						missingMapName) != std::string::npos,
				"a missing map remains a fatal load error with a concrete reason") &&
				ok;
		}
		{
			GameManager asyncFailedLoader;
			const bool loaded =
				asyncFailedLoader.scriptAPI.loadGameAsync(1);
			ok = check(
				!loaded &&
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
		const bool saved = gameManager.saveGame(7);
		INIReader currentGlobal("save/game/game.ini");
		ok = check(
			!saved &&
				currentGlobal.ParseError() == 0 &&
				currentGlobal.Get("State", "Map", "") == mapName &&
				readVirtualFile(blockedSlotPath) == blockedSlotMarker,
			"a slot publication failure reports failure after refreshing current state while preserving the blocked old slot") && ok;
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
		std::abs(leftEdgeCameraWorldPosition.x - 384.0f) < 0.001f &&
			std::abs(leftOddRowCameraWorldPosition.x - 384.0f) < 0.001f &&
			std::abs(rightEdgeCameraWorldPosition.x - 2784.0f) < 0.001f &&
			std::abs(rightOddRowCameraWorldPosition.x - 2784.0f) < 0.001f &&
			std::abs(leftEvenRowEdgeX + 64.0f) < 0.001f &&
			std::abs(leftOddRowEdgeX + 32.0f) < 0.001f &&
			std::abs(rightEvenRowEdgeX - 672.0f) < 0.001f &&
			std::abs(rightOddRowEdgeX - 704.0f) < 0.001f,
		"player-follow camera hides both staggered horizontal map edges by at least half a tile");
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
	const float leftClampedEvenX = snapCameraWorldX({ 5, 50 });
	const float leftBoundaryEvenX = snapCameraWorldX({ 6, 50 });
	const float leftFollowingEvenX = snapCameraWorldX({ 7, 50 });
	const float leftClampedOddX = snapCameraWorldX({ 5, 51 });
	const float leftFollowingOddX = snapCameraWorldX({ 6, 51 });
	const float rightFollowingEvenX = snapCameraWorldX({ 43, 50 });
	const float rightClampedEvenX = snapCameraWorldX({ 44, 50 });
	const float rightBoundaryOddX = snapCameraWorldX({ 43, 51 });
	const float rightClampedOddX = snapCameraWorldX({ 44, 51 });
	ok = check(
		std::abs(leftClampedEvenX - 384.0f) < 0.001f &&
			std::abs(leftBoundaryEvenX - 384.0f) < 0.001f &&
			std::abs(leftFollowingEvenX - 448.0f) < 0.001f &&
			std::abs(leftClampedOddX - 384.0f) < 0.001f &&
			std::abs(leftFollowingOddX - 416.0f) < 0.001f &&
			std::abs(rightFollowingEvenX - 2752.0f) < 0.001f &&
			std::abs(rightClampedEvenX - 2784.0f) < 0.001f &&
			std::abs(rightBoundaryOddX - 2784.0f) < 0.001f &&
			std::abs(rightClampedOddX - 2784.0f) < 0.001f,
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
	CoreLifecycleTestAccess::setLogicalSize(704, 480);
	CoreLifecycleTestAccess::resize(*gameManager.camera, 704, 480);
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
			std::abs(centeredEvenRowCameraWorldPosition.x - 240.0f) < 0.001f &&
			std::abs(centeredOddRowCameraWorldPosition.x - 240.0f) < 0.001f &&
			std::abs(scrollableMapCameraX - 384.0f) < 0.001f &&
			std::abs(centeredAtThresholdCameraX - 400.0f) < 0.001f &&
			std::abs(restoredScrollableMapCameraX - 384.0f) < 0.001f,
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
			std::abs(unfoldedWorldPosition.y - 1024.0f) < 0.001f &&
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
	return ok;
}
}

bool runCoreLifecycleTests()
{
	bool ok = true;
	ok = runCompositionLayeringTests() && ok;
	ok = runMainThreadOwnershipTests() && ok;
	ok = runEditorRunWindowClosePolicyTests() && ok;
	ok = runLoadingInputNeutralityTests() && ok;
	ok = runQuitLatchTests() && ok;
	ok = runElementRunExceptionCleanupTests() && ok;
	ok = runMidFrameTerminalQuitTests() && ok;
	ok = runApplicationInactiveTests() && ok;
	ok = runDeferredResizeLifecycleTests() && ok;
	ok = runCameraViewportResizeTests() && ok;
	ok = runGameplayPauseTests() && ok;
	ok = runSceneResultTests() && ok;
	ok = runMemoGenerationCompatibilityTests() && ok;
	ok = runOwnerWorldCommitPhaseTests() && ok;
	ok = runMapActorResetModeTests() && ok;
	ok = runEmptyEntityListSaveLoadRoundTripTests() && ok;
	ok = runSaveLoadFailureRecoveryTests() && ok;
	GameManager gameManager;
	ok = runNewYearPeriodTests(gameManager) && ok;
	return ok;
}
