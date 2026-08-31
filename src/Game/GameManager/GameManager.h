#pragma once
#include "../../Element/Element.h"
#include "../../Component/VideoPlayer.h"
#include "../Data/Data.h"
#include "../../libconvert/libconvert.h"
#include "../Script/Script.h"
#include "../Script/ScriptAPI.h"
#include "../../Weather/Weather.h"
#include "GameController.h"
#include "MenuController.h"
#include "../Menu/Menu.h"
#include <functional>
#include <mutex>
#include <cstdint>
#include <utility>
#include <vector>
#include "SaveFileManager.h"
#include "WorldInteractionResolver.h"
#include "../../Input/TouchControlsRecoveryGesture.h"
#include "../../Input/TouchControlsVisibilityPolicy.h"
#include "../../Launch/EditorRunSceneApplication.h"

#define gm GameManager::getInstance()

struct EventInfo
{
	std::shared_ptr<NPC> npc = nullptr;
	std::string scriptMapName = "";
	std::string scriptName = "";
};


enum ScriptType
{
	stNone,
	stScript,
	stNPC,
	stNPCDeath,
	stObject,
	stTraps,
	stGoods,
};

struct ScriptTask
{
	ScriptType type = stNone;
	std::shared_ptr<NPC> npc = nullptr;
	std::shared_ptr<Object> obj = nullptr;
	std::shared_ptr<Goods> goods = nullptr;
	bool clearPlayerAction = true;
	std::string scriptName = "";
	std::string scriptMapName = "";
	int trapIndex = 0;
	UTime remainingMilliseconds = 0;
	// Runtime-only provenance for RunParallelScript. This is deliberately not
	// serialized by ScriptRuntimeState because trace execution IDs are scoped
	// to one process/session.
	bool traceParentCaptured = false;
	std::uint64_t traceParentExecutionId = 0;
};

class GameManager :
	public Element
{
	friend class ScriptAPI;
	friend class CoreLifecycleTestAccess;
	friend class EditorRunSceneRuntimeTestAccess;
	friend class MobileExternalInputRuntimeTestAccess;
	friend class ScriptEngineRuntimeTestAccess;
public:
	GameManager();
	GameManager(
		const EditorRun::SceneTarget& target,
		const EditorRun::PreparedResourcePhase& preparedResources,
		EditorRun::RuntimeTraceWriter* runtimeTraceWriter = nullptr);
	virtual ~GameManager();

	void init();

	int gameIndex = 0;
	void setStartupIntegerVariables(const std::vector<std::pair<std::string, int>>& variables);
	void setExpectedIntegerVariables(const std::vector<std::pair<std::string, int>>& variables);
	void setAutomationHooksEnabled(bool enabled);
	bool areAutomationHooksEnabled() const noexcept;
	void setExitAfterNewGameScript(bool enabled);
	void setPostNewGameAutomationWaitMilliseconds(UTime milliseconds);
	bool hasAutomationCheckFailed() const;
	bool isEditorRunMode() const noexcept;
	bool hasEditorRunSceneApplicationResult() const noexcept;
	const EditorRun::SceneApplicationResult&
		getEditorRunSceneApplicationResult() const noexcept;

	enum class CheatAction
	{
		ToggleInvincibility,
		RestorePlayerResources,
		IncreasePracticeMagicLevel,
		IncreasePlayerLevel,
		AddMoney
	};
	struct CheatOperationResult
	{
		bool succeeded = false;
		std::string message;

		explicit operator bool() const noexcept
		{
			return succeeded;
		}
	};

	bool isCheatModeEnabled() const noexcept;
	bool isCheatInvincibilityEnabled() const noexcept;
	bool shouldProtectPlayerFromCheatDamage() const noexcept;
	CheatOperationResult setCheatModeEnabled(bool enabled);
	CheatOperationResult toggleCheatMode();
	CheatOperationResult performCheatAction(CheatAction action);

	bool initMenu();

	static GameManager * this_;
	static GameManager * getInstance();

	std::string mapFolderName = "";

	std::atomic<bool> inThread;
	bool loadGame(int index);
	const std::string& getLastLoadFailureMessage() const noexcept;

	bool saveGame(int index);

	void clearMenu();
	bool menuDisplayed();
	bool blocksWorldPointerInput() const;
	void setGameplayPaused(bool paused);
	bool isGameplayPaused() const;
	void handleSystemResult(unsigned int systemResult, int selectedSaveIndex = -1);

	void freeResource();

	static bool actionCmp(const NextAction& a, const NextAction& b)
	{
		return a.distance < b.distance;
	}

	std::vector<NextAction> fastSelectingList;

	// Children
	std::shared_ptr<MenuController> menu = nullptr; // = std::make_shared<MenuController>();
	std::shared_ptr<GameController> controller = nullptr; // = std::make_shared<GameController>();
	
	std::shared_ptr<Weather> weather = nullptr; // = std::make_shared<Weather>();

	std::shared_ptr<Camera> camera = nullptr; // = std::make_shared<Camera>();

	std::shared_ptr<Map> map = nullptr; // = std::make_shared<Map>();

	std::shared_ptr<NPCManager> npcManager = nullptr; // = std::make_shared<NPCManager>();
	std::shared_ptr<ObjectManager> objectManager = nullptr; // = std::make_shared<ObjectManager>();
	std::shared_ptr<EffectManager> effectManager = nullptr; // = std::make_shared<EffectManager>();

	std::shared_ptr<VideoPlayer> video = nullptr;

	std::shared_ptr<Player> player = nullptr; // = std::make_shared<Player>();

	Global global;
	Memo memo;
	Traps traps;


	GoodsManager goodsManager;
	MagicManager magicManager;
	PartnerManager partnerManager;
	TalkTextList talkTextList;

	VariableList varList;
	Script script;
	ScriptAPI scriptAPI;

	void runScript(const std::string & fileName);
	void runScript(const std::string & fileName, const std::string & mapName);

	void playMusic(const std::string & fileName);
	void stopMusic();
	void showMessage(const std::string & str);
	void processGlobalInputFrame(bool toggleTouchControls = false);
	void requestTouchControlsToggle();

	Point getMousePoint();
	Point getMousePoint(int x, int y);
	virtual void onWindowResize(int width, int height) override;
	void loadMap(const std::string & fileName);
	void loadNPC(const std::string & fileName);
	void loadObject(const std::string & fileName);

	void returnToDesktop();

	void clearSelected();

	bool inEvent = false;
	std::shared_ptr<Object> scriptObj = nullptr;
	void runObjScript(std::shared_ptr<Object> obj, const std::string& scriptFile = "", bool clearPlayerAction = true);
	std::shared_ptr<NPC> scriptNPC = nullptr;
	void runNPCScript(std::shared_ptr<NPC> npc, const std::string& scriptFile = "", bool clearPlayerAction = true);
	int lastScriptSoundHasPosition = 0;
	int lastScriptSoundSourceType = 0; // 0 none, 1 NPC, 2 Object.
	int lastScriptSoundMapX = 0;
	int lastScriptSoundMapY = 0;
	int lastScriptSoundOffsetX1000 = 0;
	int lastScriptSoundOffsetY1000 = 0;
	bool queueObjectInteraction(std::shared_ptr<Object> obj, bool useRightScript = false, bool running = false);
	bool queueNPCInteraction(std::shared_ptr<NPC> npc, bool useRightScript = false, bool running = false);
	bool queueNearestObjectInteraction(bool useRightScript = false, bool running = false, int radius = 2);
	bool queueNearestNPCInteraction(bool useRightScript = false, bool running = false, int radius = 2);
	bool queueObjectScriptInteraction(std::shared_ptr<Object> obj,
		WorldInteractionScriptSide scriptSide = WorldInteractionScriptSide::Primary,
		bool running = false);
	bool queueNPCTalkInteraction(std::shared_ptr<NPC> npc,
		WorldInteractionScriptSide scriptSide = WorldInteractionScriptSide::Primary,
		bool running = false);
	bool queueNPCAttackInteraction(std::shared_ptr<NPC> npc, bool running = false);
	std::vector<WorldInteractionCandidate> findWorldInteractionCandidates(
		WorldInteractionIntent intent,
		int radius = 13,
		int nearRadius = 2,
		std::weak_ptr<GameElement> preferredTarget = {});
	bool queueBestWorldInteraction(
		WorldInteractionIntent intent,
		bool running = false,
		int radius = 13,
		int nearRadius = 2,
		std::weak_ptr<GameElement> preferredTarget = {});
	void runNPCDeathScript(std::shared_ptr<NPC> npc, const std::string & scriptName, const std::string & scriptMapName);
	void runEventList();
	void runScriptTaskList();
	bool addScriptTask(const ScriptTask& task);
	void clearParallelScriptTasks();
	bool saveScriptRuntimeState();
	void loadScriptRuntimeState();
	std::shared_ptr<Goods> scriptGoods = nullptr;
	void runGoodsScript(std::shared_ptr<Goods> goods);
	std::string scriptMapName = "";
	int scriptTrapIndex = 0;
	void runTrapScript(int idx);
	std::vector<EventInfo> eventList;
	std::vector<ScriptTask> scriptTaskList;
	std::mutex scriptTaskMutex;
	ScriptType scriptType = stNone;

	int getBindValue(const std::string& bindPath);

	bool timeScriptSet = false;
	int timeScriptSeconds = 0;
	std::string timeScriptFileName = "";
	int timerSeconds = 0;
	bool timerStarted = false;
	bool timerHidden = false;
	UTime timerAccumulated = 0;

private:
	explicit GameManager(
		ScriptLibraryProfile scriptLibraryProfile,
		EditorRun::RuntimeTraceWriter* runtimeTraceWriter = nullptr);
	bool initializeEditorRunPlayerBaseline(
		std::string& failureMessage,
		std::string& templateVirtualPath,
		std::string& isolatedPlayerVirtualPath,
		int& characterIndex,
		bool& resourceMissing);
	EditorRun::SceneApplicationResult applyEditorRunSceneTarget();

	std::string bgmName = "";
	std::vector<std::pair<std::string, int>> startupIntegerVariables;
	std::vector<std::pair<std::string, int>> expectedIntegerVariables;
	bool automationHooksEnabled = false;
	bool exitAfterNewGameScript = false;
	UTime postNewGameAutomationWaitMilliseconds = 0;
	UTime postNewGameAutomationWaitElapsed = 0;
	bool postNewGameAutomationWaitPending = false;
	bool automationCheckFailed = false;
	bool editorRunMode = false;
	bool editorRunSceneApplicationCompleted = false;
	std::string lastLoadFailureMessage;
	EditorRun::SceneTarget editorRunTarget;
	EditorRun::ResolvedSceneTarget editorRunPreparedTarget;
	std::vector<EditorRun::SearchRoot> editorRunSearchRoots;
	EditorRun::RuntimeTraceWriter* runtimeTraceWriter = nullptr;
	EditorRun::SceneApplicationResult editorRunSceneApplicationResult;
	bool gameplayPaused = false;
	bool controllerPausedBeforeGameplayPause = false;
	bool menuPausedBeforeGameplayPause = false;
	bool weatherPausedBeforeGameplayPause = false;

	GameInput::TouchControlsVisibilityPolicy touchControlsVisibilityPolicy;
	GameInput::TouchControlsRecoveryGesture touchControlsRecoveryGesture;
	std::string pendingExternalInputMessage;
	bool touchControlsToggleRequested = false;
	std::uint64_t observedTouchControlsInputLifecycleRevision = 0;
	bool touchControlsRecoveryAwaitingRelease = false;
	bool touchControlsRecoveryInputBlocked = false;
	bool touchControlsRecoveryEmptyFrameObserved = false;
#if defined(__MOBILE__)
	bool touchControlsCanRecoverAfterExternalInputLoss = true;
#else
	bool touchControlsCanRecoverAfterExternalInputLoss = false;
#endif
	void processGlobalInputFrameWithContacts(
		bool toggleTouchControls,
		std::vector<GameInput::TouchRecoveryContact> contacts,
		std::uint64_t nowMilliseconds);
	bool processTouchControlsRecoveryContacts(
		std::vector<GameInput::TouchRecoveryContact> contacts,
		std::uint64_t nowMilliseconds,
		bool controlsVisibleAtFrameStart,
		bool controlsVisibleAfterExternalActions,
		bool resetRecognitionForLifecycle);
	void setTouchControlsRecoveryInputBlocked(bool blocked);
	void resetTouchControlsRecoveryGesture();

	void applyStartupIntegerVariables();
	void drainImmediateScriptTasksForAutomation();
	void checkExpectedIntegerVariables();
	void finishNewGameAutomationAndExit();
	bool runPendingPlayerDeathScript();
	void showCheatNotice(const std::string& message);
	CheatOperationResult completeCheatOperation(
		bool succeeded,
		const std::string& message);
	bool applyCheatPlayerLevelIncrease();
	bool applyCheatPracticeMagicLevelIncrease();
	bool cheatMode = false;
	bool cheatInvincibilityEnabled = false;

private:
	bool writeSaveGenerationDraft(
		const std::string& generationDirectory,
		const SaveGenerationLimits& copyLimits,
		const std::function<bool()>& ownerCheckpoint = {});
	void clearLastLoadFailureMessage();
	void setLastLoadFailureMessage(std::string message);
	void resetExclusiveLoadingInputState();
private:

	virtual void onUpdate();
	virtual void onDraw();
	virtual bool onInitial();
	virtual void onRun();
	virtual void onExit();
	virtual void onEvent();
	virtual bool onHandleEvent(AEvent & e);
	virtual bool onHandleUIAction(UIAction action) override;
	virtual bool shouldUpdateChild(PElement child) override;

};
