#include "GameManager.h"
#include "../../Engine/Engine.h"
#include "RuntimeSaveGenerationPolicy.h"
#include "ScriptRuntimeState.h"
#include "../Data/TimeStopUpdateGate.h"
#include "../../File/File.h"
#include "../../File/INIReader.h"
#include "../../File/RootedResourceReader.h"
#include "../../File/StrictRelativeResourcePath.h"
#include "../../File/log.h"
#include "../../Input/PhysicalInputManager.h"
#include "../../Resource/ResourceManager.h"
#include "../Menu/SystemNotice.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <climits>
#include <cmath>
#include <cstring>

namespace
{
bool ownerCheckpointCanContinue(
	const std::function<bool()>& ownerCheckpoint) noexcept
{
	if (!ownerCheckpoint)
	{
		return true;
	}
	try
	{
		return ownerCheckpoint();
	}
	catch (...)
	{
		return false;
	}
}

bool canQueuePlayerInteraction(std::shared_ptr<Player> player)
{
	if (player == nullptr)
	{
		return false;
	}
	auto actionActor = player->getActionActor();
	return actionActor != nullptr && actionActor->nowAction != acDeath && actionActor->nowAction != acHide;
}

NPCActionType getInteractionMoveAction(std::shared_ptr<Player> player, bool running)
{
	if (running
		&& player != nullptr
		&& player->canRun
		&& (player->thew > (int)round((float)player->info.thewMax * MIN_THEW_RATE_TO_RUN)
			|| player->thew > MIN_THEW_LIMIT_TO_RUN))
	{
		return acRun;
	}
	return acWalk;
}

const EditorRun::SearchRoot* findPreparedSearchRoot(
	const EditorRun::ResolvedTargetFile& file,
	const std::vector<EditorRun::SearchRoot>& roots)
{
	return file.searchRootIndex < roots.size()
		? &roots[file.searchRootIndex]
		: nullptr;
}

constexpr std::size_t MaximumEditorRunPlayerTemplateBytes =
	1024 * 1024;
struct EditorRunPlayerTemplateCandidate
{
	const char* sourceVirtualPath;
	const char* isolatedVirtualPath;
	int characterIndex;
};

constexpr std::array<EditorRunPlayerTemplateCandidate, 2>
	EditorRunPlayerTemplateCandidates =
	{
		EditorRunPlayerTemplateCandidate
		{
			"ini/save/player0.ini",
			"save/game/player0.ini",
			0
		},
		EditorRunPlayerTemplateCandidate
		{
			"ini/save/player.ini",
			"save/game/player.ini",
			-1
		}
	};
}

int GameManager::getBindValue(const std::string& bindPath)
{
	if (bindPath == "player.level") return player->level;
	if (bindPath == "player.exp") return player->exp;
	if (bindPath == "player.levelUpExp") return player->levelUpExp;
	if (bindPath == "player.info.attack") return player->info.attack;
	if (bindPath == "player.info.defend") return player->info.defend;
	if (bindPath == "player.info.evade") return player->info.evade;
	if (bindPath == "player.life") return player->life;
	if (bindPath == "player.info.lifeMax") return player->info.lifeMax;
	if (bindPath == "player.thew") return player->thew;
	if (bindPath == "player.info.thewMax") return player->info.thewMax;
	if (bindPath == "player.mana") return player->mana;
	if (bindPath == "player.info.manaMax") return player->info.manaMax;
	if (bindPath == "player.rage") return player->rage;
	if (bindPath == "player.rageMax") return player->rageMax;
	if (bindPath == "player.money") return player->money;
	return 0;
}


GameManager * GameManager::this_ = nullptr;


GameManager::GameManager() :
	GameManager(ScriptLibraryProfile::Full, nullptr)
{
}

GameManager::GameManager(
	const EditorRun::SceneTarget& target,
	const EditorRun::PreparedResourcePhase& preparedResources,
	EditorRun::RuntimeTraceWriter* writer) :
	GameManager(ScriptLibraryProfile::EditorRunSafe, writer)
{
	editorRunMode = true;
	editorRunTarget = target;
	editorRunPreparedTarget = preparedResources.target;
	editorRunSearchRoots = preparedResources.orderedSearchRoots;
}

GameManager::GameManager(
	ScriptLibraryProfile scriptLibraryProfile,
	EditorRun::RuntimeTraceWriter* writer) :
	script(scriptLibraryProfile, writer),
	scriptAPI(this)
{
	runtimeTraceWriter = writer;
	varList.setRuntimeTraceContext(
		writer,
		[this]()
		{
			return script.currentExecutionId();
		});
	this_ = this;
	// autoFreeResourceOnExit = true;

	name = "GameManager";
    canCallBack = true;

	inThread.store(false);

	drawFullScreen = true;
	rectFullScreen = true;
	setPriority(epMap);
	result = erNone;
	init();
	
}

GameManager::~GameManager()
{
	freeResource();
	removeAllChild();
    this_ = nullptr;
}

void GameManager::init()
{
	menu = std::make_shared<MenuController>();
	controller = std::make_shared<GameController>();

	weather = std::make_shared<Weather>();

	camera = std::make_shared<Camera>();

	map = std::make_shared<Map>();

	npcManager = std::make_shared<NPCManager>();
	objectManager = std::make_shared<ObjectManager>();
	effectManager = std::make_shared<EffectManager>();
	player = std::make_shared<Player>();

	npcManager->setPlayer(player);

	talkTextList.load();

	addChild(controller);
	addChild(menu);
	addChild(weather);

	controller->addChild(camera);

	controller->addChild(player);

	controller->addChild(map);
	controller->addChild(npcManager);
	controller->addChild(objectManager);
	controller->addChild(effectManager);
}

void GameManager::setStartupIntegerVariables(const std::vector<std::pair<std::string, int>>& variables)
{
	startupIntegerVariables = variables;
}

void GameManager::setExpectedIntegerVariables(const std::vector<std::pair<std::string, int>>& variables)
{
	expectedIntegerVariables = variables;
}

void GameManager::setAutomationHooksEnabled(bool enabled)
{
	automationHooksEnabled = enabled;
}

bool GameManager::areAutomationHooksEnabled() const noexcept
{
	return automationHooksEnabled;
}

void GameManager::setExitAfterNewGameScript(bool enabled)
{
	exitAfterNewGameScript = enabled;
}

void GameManager::setPostNewGameAutomationWaitMilliseconds(UTime milliseconds)
{
	postNewGameAutomationWaitMilliseconds = milliseconds;
}

bool GameManager::hasAutomationCheckFailed() const
{
	return automationCheckFailed;
}

bool GameManager::isEditorRunMode() const noexcept
{
	return editorRunMode;
}

bool GameManager::hasEditorRunSceneApplicationResult() const noexcept
{
	return editorRunSceneApplicationCompleted;
}

const EditorRun::SceneApplicationResult&
	GameManager::getEditorRunSceneApplicationResult() const noexcept
{
	return editorRunSceneApplicationResult;
}

bool GameManager::initializeEditorRunPlayerBaseline(
	std::string& failureMessage,
	std::string& templateVirtualPath,
	std::string& isolatedPlayerVirtualPath,
	int& characterIndex,
	bool& resourceMissing)
{
	failureMessage.clear();
	templateVirtualPath.clear();
	isolatedPlayerVirtualPath.clear();
	characterIndex = -1;
	resourceMissing = false;
	if (!editorRunMode ||
		player == nullptr ||
		!File::hasEditorRunFileLayout())
	{
		failureMessage =
			"Editor-run isolated file layout or player runtime is unavailable";
		return false;
	}

	RootedResourceReader::Result templateBytes;
	bool templateSelected = false;
	for (const EditorRun::SearchRoot& searchRoot :
		editorRunSearchRoots)
	{
		for (const EditorRunPlayerTemplateCandidate& candidate :
			EditorRunPlayerTemplateCandidates)
		{
			templateBytes =
				searchRoot.kind ==
						EditorRun::SearchRootKind::Overlay
					? RootedResourceReader::
						readBoundedFileFromRoot(
							searchRoot.anchor,
							candidate.sourceVirtualPath,
							MaximumEditorRunPlayerTemplateBytes)
					: RootedResourceReader::
						readBoundedFileFromRoot(
							searchRoot.root,
							candidate.sourceVirtualPath,
							MaximumEditorRunPlayerTemplateBytes);
			if (templateBytes.status ==
				RootedResourceReader::Status::NotFound)
			{
				continue;
			}
			templateVirtualPath =
				candidate.sourceVirtualPath;
			isolatedPlayerVirtualPath =
				candidate.isolatedVirtualPath;
			characterIndex =
				candidate.characterIndex;
			if (!templateBytes.succeeded())
			{
				failureMessage =
					"Editor-run player template could not be read from its prepared resource root";
				return false;
			}
			templateSelected = true;
			break;
		}
		if (templateSelected)
		{
			break;
		}
	}

	if (!templateSelected)
	{
		resourceMissing = true;
		failureMessage =
			"Editor-run player template is missing";
		return false;
	}
	if (templateBytes.bytes.empty() ||
		templateBytes.bytes.size() >
			static_cast<std::size_t>(INT_MAX) ||
		std::find(
			templateBytes.bytes.begin(),
			templateBytes.bytes.end(),
			std::uint8_t{ 0 }) != templateBytes.bytes.end())
	{
		failureMessage =
			"Editor-run player template is invalid";
		return false;
	}

	auto templateData =
		std::make_unique<char[]>(templateBytes.bytes.size() + 1);
	std::memcpy(
		templateData.get(),
		templateBytes.bytes.data(),
		templateBytes.bytes.size());
	templateData[templateBytes.bytes.size()] = '\0';
	INIReader templateIni(templateData);
	const std::string expectedNpcIni =
		templateIni.Get("Init", "NpcIni", "");
	if (templateIni.ParseError() != 0 ||
		!templateIni.HasSection("Init") ||
		expectedNpcIni.empty() ||
		templateIni.GetInteger("Init", "Level", 0) < 1 ||
		templateIni.GetInteger("Init", "LifeMax", 0) <= 0 ||
		templateIni.GetInteger("Init", "ThewMax", 0) <= 0 ||
		templateIni.GetInteger("Init", "ManaMax", 0) <= 0)
	{
		failureMessage =
			"Editor-run player template does not contain a runnable Init baseline";
		return false;
	}

	if (!File::writeFileChecked(
			isolatedPlayerVirtualPath,
			templateBytes.bytes.data(),
			static_cast<int>(templateBytes.bytes.size())))
	{
		failureMessage =
			"Editor-run player template could not be written to isolated save state";
		return false;
	}

	std::unique_ptr<char[]> persistedTemplate;
	int persistedLength = 0;
	if (!File::readFile(
			isolatedPlayerVirtualPath,
			persistedTemplate,
			persistedLength,
			static_cast<int>(
				MaximumEditorRunPlayerTemplateBytes)) ||
		persistedTemplate == nullptr ||
		persistedLength !=
			static_cast<int>(templateBytes.bytes.size()) ||
		std::memcmp(
			persistedTemplate.get(),
			templateBytes.bytes.data(),
			templateBytes.bytes.size()) != 0)
	{
		failureMessage =
			"Editor-run isolated player template verification failed";
		return false;
	}

	global.data.characterIndex = characterIndex;
	std::string playerFailureReason;
	if (!player->load(
			characterIndex,
			&playerFailureReason))
	{
		failureMessage = playerFailureReason.empty()
			? "Editor-run player template could not be loaded"
			: playerFailureReason;
		return false;
	}
	player->calInfo();
	if (player->npcIni != expectedNpcIni ||
		player->level < 1 ||
		player->getLifeMax() <= 0 ||
		player->getThewMax() <= 0 ||
		player->getManaMax() <= 0 ||
		player->res.stand.imageFile.empty() ||
		player->res.stand.imagePackage == nullptr)
	{
		failureMessage =
			"Editor-run player template did not initialize runnable attributes and stand resources";
		return false;
	}
	return true;
}

EditorRun::SceneApplicationResult GameManager::applyEditorRunSceneTarget()
{
	enum class ApplicationStage
	{
		IntegerVariable,
		Map,
		Npc,
		Object,
		PlayerPosition,
		EntryScript
	};
	ApplicationStage applicationStage =
		ApplicationStage::IntegerVariable;
	std::string playerInitializationFailure;
	std::string playerTemplateVirtualPath;
	std::string isolatedPlayerVirtualPath;
	int playerCharacterIndex = -1;
	bool playerResourceMissing = false;
	const bool deferredResourceLookup =
		editorRunPreparedTarget.map.searchRootIndex ==
			EditorRun::SearchAllResourceRoots;
	const bool playerBaselineReady =
		initializeEditorRunPlayerBaseline(
		playerInitializationFailure,
		playerTemplateVirtualPath,
		isolatedPlayerVirtualPath,
		playerCharacterIndex,
		playerResourceMissing);
	if (!playerBaselineReady &&
		(!deferredResourceLookup || !playerResourceMissing))
	{
		EditorRun::SceneApplicationResult result;
		result.error =
			EditorRun::SceneApplicationError::
				PlayerInitializationFailed;
		result.diagnosticCode =
			"editor_run.target.player_initialization_failed";
		result.fieldPath = "target";
		result.virtualPath =
			std::move(playerTemplateVirtualPath);
		result.message =
			std::move(playerInitializationFailure);
		return result;
	}
	if (!playerBaselineReady)
	{
		GameLog::write(
			"GameManager: editor-run player resource is unavailable; continuing without a player baseline path=%s reason=%s\n",
			playerTemplateVirtualPath.c_str(),
			playerInitializationFailure.c_str());
	}

	bool editorRunWorldMutationStarted = false;
	EditorRun::SceneApplicationCallbacks callbacks;
	callbacks.setIntegerVariable =
		[this, &applicationStage](
			const std::string& name,
			std::int32_t value)
		{
			applicationStage =
				ApplicationStage::IntegerVariable;
			if (name.empty())
			{
				return false;
			}
			varList.ensureInitialized();
			varList.setInteger(name, static_cast<int>(value));
			return true;
		};
	callbacks.loadMap =
		[this,
		 &applicationStage,
		 &editorRunWorldMutationStarted](
			const EditorRun::ResolvedTargetFile& file)
		{
			applicationStage = ApplicationStage::Map;
			if (file.searchRootIndex ==
				EditorRun::SearchAllResourceRoots)
			{
				const bool loaded =
					scriptAPI.loadMapFromEditorRunRoots(
						editorRunSearchRoots,
						file.virtualPath,
						false);
				if (loaded)
				{
					editorRunWorldMutationStarted = true;
					return true;
				}
				GameLog::write(
					"GameManager: editor-run MAP resource is unavailable; using an empty map path=%s\n",
					file.virtualPath.c_str());
				map->freeResource();
				effectManager->clearEffect();
				npcManager->clearNPC();
				objectManager->clearObj();
				traps.freeResource();
				global.data.mapName.clear();
				global.data.npcName.clear();
				global.data.objName.clear();
				editorRunWorldMutationStarted = true;
				return true;
			}
			const EditorRun::SearchRoot* root =
				findPreparedSearchRoot(
					file,
					editorRunSearchRoots);
			if (root == nullptr ||
				!scriptAPI.loadMapFromExactRoot(
					*root,
					file.virtualPath,
					false))
			{
				return false;
			}
			editorRunWorldMutationStarted = true;
			return true;
		};
	callbacks.loadNpc =
		[this, &applicationStage](
			const EditorRun::ResolvedTargetFile& file)
		{
			applicationStage = ApplicationStage::Npc;
			if (file.searchRootIndex ==
				EditorRun::SearchAllResourceRoots)
			{
				if (scriptAPI.loadNPCFromEditorRunRoots(
					editorRunSearchRoots,
					file.virtualPath))
				{
					return true;
				}
				GameLog::write(
					"GameManager: editor-run NPC resource is unavailable; using an empty NPC list path=%s\n",
					file.virtualPath.c_str());
				npcManager->clearNPC();
				global.data.npcName.clear();
				return true;
			}
			const EditorRun::SearchRoot* root =
				findPreparedSearchRoot(
					file,
					editorRunSearchRoots);
			return root != nullptr &&
				scriptAPI.loadNPCFromExactRoot(
					*root,
					file.virtualPath);
		};
	callbacks.loadObject =
		[this, &applicationStage](
			const EditorRun::ResolvedTargetFile& file)
		{
			applicationStage = ApplicationStage::Object;
			if (file.searchRootIndex ==
				EditorRun::SearchAllResourceRoots)
			{
				if (scriptAPI.loadObjectFromEditorRunRoots(
					editorRunSearchRoots,
					file.virtualPath))
				{
					return true;
				}
				GameLog::write(
					"GameManager: editor-run object resource is unavailable; using an empty object list path=%s\n",
					file.virtualPath.c_str());
				objectManager->clearObj();
				global.data.objName.clear();
				return true;
			}
			const EditorRun::SearchRoot* root =
				findPreparedSearchRoot(
					file,
					editorRunSearchRoots);
			return root != nullptr &&
				scriptAPI.loadObjectFromExactRoot(
					*root,
					file.virtualPath);
		};
	callbacks.setPlayerPositionAndCamera =
		[this,
		 &applicationStage,
		 deferredResourceLookup,
		 playerBaselineReady](
			std::int32_t x,
			std::int32_t y)
		{
			applicationStage =
				ApplicationStage::PlayerPosition;
			if (!deferredResourceLookup)
			{
				return scriptAPI.setEditorRunPlayerPositionAndCamera(x, y);
			}
			if (!playerBaselineReady)
			{
				GameLog::write(
					"GameManager: editor-run player position was skipped because the player resource is empty\n");
				return true;
			}
			if (!scriptAPI.setEditorRunPlayerPositionAndCamera(x, y))
			{
				GameLog::write(
					"GameManager: editor-run player position is unavailable for the current map; continuing\n");
			}
			return true;
		};
	callbacks.runEntryScript =
		[this, &applicationStage](
			const EditorRun::ResolvedTargetFile& file)
		{
			applicationStage = ApplicationStage::EntryScript;
			if (file.searchRootIndex ==
				EditorRun::SearchAllResourceRoots)
			{
				const bool wasInEvent = inEvent;
				inEvent = true;
				ExactScriptExecutionResult scriptResult;
				try
				{
					scriptResult =
						scriptAPI.runScriptFromEditorRunRoots(
							editorRunSearchRoots,
							file.virtualPath);
				}
				catch (...)
				{
					inEvent = wasInEvent;
					throw;
				}
				inEvent = wasInEvent;
				EditorRun::EntryScriptExecutionResult result;
				result.line = scriptResult.line;
				result.column = scriptResult.column;
				result.message = scriptResult.message;
				switch (scriptResult.status)
				{
				case ExactScriptExecutionStatus::Success:
					result.status =
						EditorRun::EntryScriptExecutionStatus::Success;
					break;
				case ExactScriptExecutionStatus::LoadFailed:
					result.status =
						EditorRun::EntryScriptExecutionStatus::LoadFailed;
					break;
				case ExactScriptExecutionStatus::RuntimeFailed:
					result.status =
						EditorRun::EntryScriptExecutionStatus::RuntimeFailed;
					break;
				}
				if (result.status !=
					EditorRun::EntryScriptExecutionStatus::Success)
				{
					GameLog::write(
						"GameManager: editor-run entry script is unavailable; continuing without it path=%s line=%u column=%u message=%s\n",
						file.virtualPath.c_str(),
						result.line,
						result.column,
						result.message.c_str());
					result = {};
				}
				return result;
			}
			const EditorRun::SearchRoot* root =
				findPreparedSearchRoot(
					file,
					editorRunSearchRoots);
			if (root == nullptr)
			{
				EditorRun::EntryScriptExecutionResult result;
				result.status =
					EditorRun::EntryScriptExecutionStatus::
						LoadFailed;
				result.message =
					"Editor-run entry script prepared root is unavailable";
				return result;
			}
			const bool wasInEvent = inEvent;
			inEvent = true;
			ExactScriptExecutionResult scriptResult;
			try
			{
				scriptResult =
					scriptAPI.runScriptFromExactRoot(
						*root,
						file.virtualPath);
			}
			catch (...)
			{
				inEvent = wasInEvent;
				throw;
			}
			inEvent = wasInEvent;
			EditorRun::EntryScriptExecutionResult result;
			result.line = scriptResult.line;
			result.column = scriptResult.column;
			result.message = scriptResult.message;
			switch (scriptResult.status)
			{
			case ExactScriptExecutionStatus::Success:
				result.status =
					EditorRun::EntryScriptExecutionStatus::
						Success;
				break;
			case ExactScriptExecutionStatus::LoadFailed:
				result.status =
					EditorRun::EntryScriptExecutionStatus::
						LoadFailed;
				break;
			case ExactScriptExecutionStatus::RuntimeFailed:
			default:
				result.status =
					EditorRun::EntryScriptExecutionStatus::
						RuntimeFailed;
				break;
			}
			return result;
		};
	EditorRun::SceneApplicationResult result;
	try
	{
		result = EditorRun::applyEditorRunScene(
			editorRunTarget,
			editorRunPreparedTarget,
			callbacks);
	}
	catch (const std::exception& error)
	{
		switch (applicationStage)
		{
		case ApplicationStage::IntegerVariable:
			result.error =
				EditorRun::SceneApplicationError::
					IntegerVariableFailed;
			result.diagnosticCode =
				"editor_run.target.variable_exception";
			result.fieldPath = "variables";
			break;
		case ApplicationStage::Map:
			result.error =
				EditorRun::SceneApplicationError::MapLoadFailed;
			result.diagnosticCode =
				"editor_run.target.map_exception";
			result.fieldPath = "target.map";
			break;
		case ApplicationStage::Npc:
			result.error =
				EditorRun::SceneApplicationError::NpcLoadFailed;
			result.diagnosticCode =
				"editor_run.target.npc_exception";
			result.fieldPath = "target.npc";
			break;
		case ApplicationStage::Object:
			result.error =
				EditorRun::SceneApplicationError::
					ObjectLoadFailed;
			result.diagnosticCode =
				"editor_run.target.object_exception";
			result.fieldPath = "target.object";
			break;
		case ApplicationStage::PlayerPosition:
			result.error =
				EditorRun::SceneApplicationError::
					PlayerPositionFailed;
			result.diagnosticCode =
				"editor_run.target.position_exception";
			result.fieldPath = "target.player";
			break;
		case ApplicationStage::EntryScript:
		default:
			result.error =
				EditorRun::SceneApplicationError::
					EntryScriptRuntimeFailed;
			result.diagnosticCode =
				"editor_run.target.entry_script_exception";
			result.fieldPath = "target.entry_script";
			break;
		}
		result.message =
			std::string(
				"Editor-run scene callback threw: ") +
			error.what();
	}
	catch (...)
	{
		switch (applicationStage)
		{
		case ApplicationStage::IntegerVariable:
			result.error =
				EditorRun::SceneApplicationError::
					IntegerVariableFailed;
			result.diagnosticCode =
				"editor_run.target.variable_exception";
			result.fieldPath = "variables";
			break;
		case ApplicationStage::Map:
			result.error =
				EditorRun::SceneApplicationError::MapLoadFailed;
			result.diagnosticCode =
				"editor_run.target.map_exception";
			result.fieldPath = "target.map";
			break;
		case ApplicationStage::Npc:
			result.error =
				EditorRun::SceneApplicationError::NpcLoadFailed;
			result.diagnosticCode =
				"editor_run.target.npc_exception";
			result.fieldPath = "target.npc";
			break;
		case ApplicationStage::Object:
			result.error =
				EditorRun::SceneApplicationError::
					ObjectLoadFailed;
			result.diagnosticCode =
				"editor_run.target.object_exception";
			result.fieldPath = "target.object";
			break;
		case ApplicationStage::PlayerPosition:
			result.error =
				EditorRun::SceneApplicationError::
					PlayerPositionFailed;
			result.diagnosticCode =
				"editor_run.target.position_exception";
			result.fieldPath = "target.player";
			break;
		case ApplicationStage::EntryScript:
		default:
			result.error =
				EditorRun::SceneApplicationError::
					EntryScriptRuntimeFailed;
			result.diagnosticCode =
				"editor_run.target.entry_script_exception";
			result.fieldPath = "target.entry_script";
			break;
		}
		result.message =
			"Editor-run scene callback threw an unknown exception";
	}
	if (!result.succeeded() &&
		editorRunWorldMutationStarted &&
		engine != nullptr &&
		!engine->isApplicationQuitRequested())
	{
		scriptAPI.recoverFromPartialWorldFailure(
			"editor-run scene");
	}
	return result;
}

void GameManager::applyStartupIntegerVariables()
{
	if (startupIntegerVariables.empty())
	{
		return;
	}
	if (!automationHooksEnabled)
	{
		GameLog::write(
			"GameManager: ignored unauthorized startup integer variables\n");
		return;
	}

	varList.ensureInitialized();
	for (const auto& item : startupIntegerVariables)
	{
		if (item.first.empty())
		{
			continue;
		}
		varList.setInteger(item.first, item.second);
		GameLog::write("GameManager: startup int %s=%d\n", item.first.c_str(), item.second);
	}
}

void GameManager::drainImmediateScriptTasksForAutomation()
{
	const int maxIterations = 16;
	for (int i = 0; i < maxIterations; i++)
	{
		bool hasImmediateTask = false;
		{
			std::lock_guard<std::mutex> lock(scriptTaskMutex);
			for (const auto& task : scriptTaskList)
			{
				if (task.remainingMilliseconds <= 0)
				{
					hasImmediateTask = true;
					break;
				}
			}
		}
		if (!hasImmediateTask)
		{
			return;
		}
		runScriptTaskList();
	}
	GameLog::write("GameManager: automation script task drain reached iteration limit\n");
}

void GameManager::checkExpectedIntegerVariables()
{
	if (expectedIntegerVariables.empty())
	{
		return;
	}
	if (!automationHooksEnabled)
	{
		GameLog::write(
			"GameManager: ignored unauthorized expected integer assertions\n");
		return;
	}

	for (const auto& item : expectedIntegerVariables)
	{
		if (item.first.empty())
		{
			continue;
		}
		int actualValue = varList.getInteger(item.first);
		bool ok = (actualValue == item.second);
		GameLog::write("GameManager: expect int %s=%d actual=%d %s\n",
			item.first.c_str(), item.second, actualValue, ok ? "OK" : "FAILED");
		if (!ok)
		{
			automationCheckFailed = true;
		}
	}

	if (automationCheckFailed)
	{
		GameLog::write("GameManager: automation variable assertions failed\n");
	}
}

void GameManager::finishNewGameAutomationAndExit()
{
	drainImmediateScriptTasksForAutomation();
	checkExpectedIntegerVariables();
	GameLog::write("GameManager: exit after new game script by launch argument\n");
	stop(erOK);
}

bool GameManager::shouldUpdateChild(PElement child)
{
	if (gameplayPaused)
	{
		// System/Option/SaveLoad are temporarily attached directly to GameManager
		// and must remain interactive. World, HUD and weather stay frozen.
		return child != controller && child != menu && child != weather;
	}
	return shouldUpdateGameManagerChildDuringTimeStop(effectManager != nullptr && effectManager->hasActiveTimeStopper(),
		child == weather);
}

void GameManager::setGameplayPaused(bool paused)
{
	if (paused && controller != nullptr)
	{
		controller->cancelControllerWorldInteraction();
	}
	if (gameplayPaused == paused)
	{
		return;
	}
	gameplayPaused = paused;
	if (paused)
	{
		controllerPausedBeforeGameplayPause = controller != nullptr && controller->isPaused();
		menuPausedBeforeGameplayPause = menu != nullptr && menu->isPaused();
		weatherPausedBeforeGameplayPause = weather != nullptr && weather->isPaused();
		if (controller != nullptr)
		{
			controller->setPaused(true);
		}
		if (menu != nullptr)
		{
			menu->setPaused(true);
		}
		if (weather != nullptr)
		{
			weather->setPaused(true);
		}
		return;
	}
	if (controller != nullptr && !controllerPausedBeforeGameplayPause)
	{
		controller->setPaused(false);
	}
	if (menu != nullptr && !menuPausedBeforeGameplayPause)
	{
		menu->setPaused(false);
	}
	if (weather != nullptr && !weatherPausedBeforeGameplayPause)
	{
		weather->setPaused(false);
	}
}

bool GameManager::isGameplayPaused() const
{
	return gameplayPaused;
}

void GameManager::handleSystemResult(unsigned int systemResult, int selectedSaveIndex)
{
	// The system menu freezes controller, HUD and weather timers. Resume that
	// state before loading so map/weather transitions cannot wait on a paused
	// timer. JxqyHD also closes the save/load UI before invoking the loader.
	setGameplayPaused(false);
	if ((systemResult & erLoad) != 0 && selectedSaveIndex >= 0)
	{
		if (menu != nullptr && menu->systemNotice != nullptr)
		{
			menu->systemNotice->dismiss();
		}
		if (weather != nullptr)
		{
			weather->fadeOut();
		}
		bool loaded = false;
		if (Config::loadAsync)
		{
			loaded = scriptAPI.loadGameAsync(selectedSaveIndex + 1);
		}
		else
		{
			loaded = scriptAPI.loadGame(selectedSaveIndex + 1);
		}
		if (!loaded)
		{
			GameLog::write(
				"GameManager: in-game save load failed; restoring the current world presentation\n");
			const bool returningToTitle =
				(result & (erOK | erReturnToTitle)) != 0;
			if (!returningToTitle)
			{
				if (!engine->isApplicationQuitRequested() &&
					menu != nullptr)
				{
					menu->showSystemNotice(
						std::string(u8"读档失败：") +
							(lastLoadFailureMessage.empty()
								? std::string(u8"未提供详细原因")
								: lastLoadFailureMessage) +
							u8"。请保留日志以便进一步排查。",
						15000);
				}
				clearLastLoadFailureMessage();
			}
		}
		if (weather != nullptr && !engine->isApplicationQuitRequested())
		{
			// Fade back in for both outcomes: a successful load reveals the new
			// world, while a rejected save must restore the still-live old world.
			weather->fadeInEx();
		}
	}

	unsigned int propagatedResult = systemResult & (erExit | erReturnToTitle);
	if (propagatedResult != 0)
	{
		result |= propagatedResult;
		logicRunning = false;
	}
}

bool GameManager::initMenu()
{
	if (!engine->isMainThread())
	{
		GameLog::write(
			"GameManager: menu initialization must run on the SDL main thread\n");
		return false;
	}
	menu->init();
	controller->init();
	return true;
}

GameManager * GameManager::getInstance()
{
	return this_;
}

bool GameManager::loadGame(int index)
{
	return scriptAPI.loadGame(index);
}

const std::string& GameManager::getLastLoadFailureMessage() const noexcept
{
	return lastLoadFailureMessage;
}

void GameManager::clearLastLoadFailureMessage()
{
	lastLoadFailureMessage.clear();
}

void GameManager::setLastLoadFailureMessage(std::string message)
{
	lastLoadFailureMessage = std::move(message);
}

bool GameManager::writeSaveGenerationDraft(
	const std::string& generationDirectory,
	const SaveGenerationLimits& copyLimits,
	const std::function<bool()>& ownerCheckpoint)
{
	const ResourcePathSafety::StrictRelativePathResult mapPath =
		ResourcePathSafety::normalizeStrictRelativeResourcePath(
			global.data.mapName);
	if (!mapPath.succeeded() ||
		!File::fileExist(
			std::string(MAP_FOLDER) + global.data.mapName))
	{
		GameLog::write(
			"GameManager: refusing to publish a save with an unavailable map %s\n",
			global.data.mapName.c_str());
		return false;
	}
	if ((!global.data.npcName.empty() &&
			!SaveFileManager::IsSafeEntityListFileName(
				global.data.npcName)) ||
		(!global.data.objName.empty() &&
			!SaveFileManager::IsSafeEntityListFileName(
				global.data.objName)) ||
		!SaveFileManager::AreEntityListFileNamesDistinct(
			global.data.npcName,
			global.data.objName))
	{
		GameLog::write(
			"GameManager: refusing to publish unsafe or colliding entity list names npc=%s object=%s\n",
			global.data.npcName.c_str(),
			global.data.objName.c_str());
		return false;
	}
	if (!File::recoverDirectoryCopy(SAVE_CURRENT_FOLDER))
	{
		GameLog::write(
			"GameManager: can not recover current save generation\n");
		return false;
	}
	const bool draftReady =
		SaveFileManager::CopySaveGenerationWithinLimits(
		SAVE_CURRENT_FOLDER,
		generationDirectory,
		copyLimits,
		{ SAVE_LIST_FILE },
		[ownerCheckpoint]()
		{
			return !ownerCheckpointCanContinue(
				ownerCheckpoint);
		});
	if (!draftReady)
	{
		GameLog::write(
			"GameManager: can not prepare save draft generation %s\n",
			generationDirectory.c_str());
		return false;
	}

	bool saved = false;
	{
		SaveFileManager::CurrentPathScope draftPath(
			generationDirectory);
		if (!draftPath.valid())
		{
			GameLog::write(
				"GameManager: invalid save draft generation path %s\n",
				generationDirectory.c_str());
			return false;
		}

		saved = true;
		saved = global.save() && saved;
		saved = varList.save() && saved;
		saved = memo.save() && saved;
		saved = traps.save() && saved;
		if (!ownerCheckpointCanContinue(
				ownerCheckpoint))
		{
			return false;
		}

		saved = player->save(
			global.data.characterIndex) && saved;
		saved = partnerManager.save(
			global.data.characterIndex) && saved;
		if (!ownerCheckpointCanContinue(
				ownerCheckpoint))
		{
			return false;
		}

		saved = magicManager.save(
			global.data.characterIndex) && saved;
		saved = goodsManager.save(
			global.data.characterIndex) && saved;
		if (!ownerCheckpointCanContinue(
				ownerCheckpoint))
		{
			return false;
		}

		saved = npcManager->save(
			global.data.npcName) && saved;
		saved = objectManager->save(
			global.data.objName) && saved;
		if (!ownerCheckpointCanContinue(
				ownerCheckpoint))
		{
			return false;
		}
		saved = effectManager->save() && saved;
		saved = saveScriptRuntimeState() && saved;
	}
	return saved &&
		ownerCheckpointCanContinue(ownerCheckpoint);
}

bool GameManager::saveGame(int index)
{
	if (engine == nullptr || !engine->isMainThread())
	{
		GameLog::write(
			"GameManager: save commit must run on the SDL main thread\n");
		return false;
	}
	SaveFileManager::OperationScope saveOperation;
	const std::string draftDirectory =
		"save\\game_build\\";
	const SaveGenerationPreflightPolicy policy =
		createRuntimeSaveGenerationPolicy(
			*this,
			RuntimeSaveGenerationPolicyMode::GeneratedSave);
	if (!SaveFileManager::RecoverInterruptedSaveOperations())
	{
		GameLog::write(
			"GameManager: one or more save directories could not be recovered; continuing with unaffected slots\n");
	}
	SaveFileManager::ScratchGenerationScope draftCleanup(
		draftDirectory);
	if (!draftCleanup.valid())
	{
		GameLog::write(
			"GameManager: invalid save draft cleanup path\n");
		return false;
	}
	if (!writeSaveGenerationDraft(
			draftDirectory,
			policy.limits))
	{
		GameLog::write("GameManager: save generation failed; slot publication skipped\n");
		return false;
	}

	// Keep save/game aligned with the live world before attempting the optional
	// manual or auto slot. The two directory publications are independently
	// atomic; publishing the slot first could leave a new slot paired with an
	// old current generation when the second publication fails.
	const SaveGenerationResult currentPublication =
		SaveFileManager::PublishPreparedSaveGeneration(
			draftDirectory,
			SAVE_CURRENT_FOLDER,
			policy.limits,
			{ SAVE_LIST_FILE });
	if (!currentPublication.succeeded())
	{
		GameLog::write(
			"GameManager: current save publication failed error=%s path=%s\n",
			SaveFileManager::DescribeSaveGenerationError(
				currentPublication.error),
			currentPublication.errorPath.c_str());
		return false;
	}

	if (index != 0)
	{
		const std::string secondaryDirectory =
			index > 0
				? convert::formatString(
					SAVE_FOLDER, index)
				: std::string(SAVE_AUTO_FOLDER);
		const SaveGenerationResult slotPublication =
			SaveFileManager::PublishPreparedSaveGeneration(
				draftDirectory,
				secondaryDirectory,
				policy.limits,
				{ SAVE_LIST_FILE });
		if (!slotPublication.succeeded())
		{
			GameLog::write(
				"GameManager: slot save publication failed error=%s path=%s\n",
				SaveFileManager::DescribeSaveGenerationError(
					slotPublication.error),
				slotPublication.errorPath.c_str());
			return false;
		}
	}
	return true;
}

bool GameManager::saveScriptRuntimeState()
{
	std::string fileName =
		SaveFileManager::CurrentPath() + GLOBAL_INI;
	INIReader ini(fileName);

	ScriptRuntimeTimerState timerState;
	timerState.timerStarted = timerStarted;
	timerState.timerHidden = timerHidden;
	timerState.timerSeconds = timerSeconds;
	timerState.timerAccumulatedMilliseconds = timerAccumulated;
	timerState.timeScriptSet = timeScriptSet;
	timerState.timeScriptSeconds = timeScriptSeconds;
	timerState.timeScriptFileName = timeScriptFileName;
	writeScriptRuntimeTimerState(ini, timerState);

	std::vector<ParallelScriptRuntimeState> parallelScripts;
	{
		std::lock_guard<std::mutex> lock(scriptTaskMutex);
		for (const auto& task : scriptTaskList)
		{
			if (task.type != stScript || task.scriptName.empty())
			{
				continue;
			}
			ParallelScriptRuntimeState state;
			state.scriptName = task.scriptName;
			state.scriptMapName = task.scriptMapName;
			state.remainingMilliseconds = task.remainingMilliseconds;
			parallelScripts.push_back(state);
		}
	}
	writeParallelScriptRuntimeStates(ini, parallelScripts);

	return ini.saveToFile(fileName);
}

void GameManager::loadScriptRuntimeState()
{
	const std::string fileName =
		SaveFileManager::CurrentPath() + GLOBAL_INI;
	INIReader ini(fileName);

	ScriptRuntimeTimerState timerState = readScriptRuntimeTimerState(ini);
	timerStarted = timerState.timerStarted;
	timerHidden = timerState.timerHidden;
	timerSeconds = timerState.timerSeconds;
	timerAccumulated = timerState.timerAccumulatedMilliseconds;
	timeScriptSet = timerState.timeScriptSet;
	timeScriptSeconds = timerState.timeScriptSeconds;
	timeScriptFileName = timerState.timeScriptFileName;

	if (menu != nullptr && menu->timerMenu != nullptr)
	{
		if (timerStarted)
		{
			menu->timerMenu->startTimer(timerSeconds);
			if (timerHidden)
			{
				menu->timerMenu->hideTimer();
			}
		}
		else
		{
			menu->timerMenu->stopTimer();
		}
	}

	eventList.clear();
	std::vector<ParallelScriptRuntimeState> parallelScripts = readParallelScriptRuntimeStates(ini);
	{
		std::lock_guard<std::mutex> lock(scriptTaskMutex);
		scriptTaskList.clear();
		for (const auto& parallelScript : parallelScripts)
		{
			ScriptTask task;
			task.type = stScript;
			task.scriptName = parallelScript.scriptName;
			task.scriptMapName = parallelScript.scriptMapName;
			task.remainingMilliseconds = parallelScript.remainingMilliseconds;
			scriptTaskList.push_back(task);
		}
	}
}

void GameManager::clearMenu()
{
	menu->clearMenu();
}

bool GameManager::menuDisplayed()
{
	return menu->menuDisplayed();
}

bool GameManager::blocksWorldPointerInput() const
{
	return menu != nullptr && menu->blocksWorldPointerInput();
}

#define freeMenu(component); \
	menu->removeChild(component); \
	if (component.get() != nullptr)\
	{\
		component = nullptr; \
	}

#define freeBtmMenu(component); \
	btmWnd.removeChild(component); \
	if (component.get() != nullptr)\
	{\
		component = nullptr; \
	}
#define safeFreeResource(a) \
	if (a.get() != nullptr) \
	{\
		a->freeResource();\
	}
void GameManager::freeResource()
{
	touchControlsToggleRequested = false;
	resetTouchControlsRecoveryGesture();
	weather->reset();
	camera->followNPC.reset();
	safeFreeResource(controller);
	safeFreeResource(menu);
	safeFreeResource(effectManager);
	partnerManager.freeResource();
	safeFreeResource(npcManager);
	safeFreeResource(objectManager);
	safeFreeResource(player);
	magicManager.freeResource();
	safeFreeResource(map);
}

Point GameManager::getMousePoint(int x, int y)
{
	int w, h;
	engine->getWindowSize(w, h);
	Point cenScreen;
	cenScreen.x = (int)w / 2;
	cenScreen.y = (int)h / 2;

	Point pos = map->getMousePosition({ x, y }, camera->position, cenScreen, camera->offset);
	return pos;
}

Point GameManager::getMousePoint()
{
	int x = -1;
	int y = -1;
	engine->getMousePosition(x, y);
	return getMousePoint(x, y);
}

void GameManager::onWindowResize(int width, int height)
{
	if (controller != nullptr)
	{
		controller->init();
	}
}

void GameManager::loadMap(const std::string & fileName)
{
	scriptAPI.loadMapAsync(fileName);
}

void GameManager::loadNPC(const std::string & fileName)
{
	scriptAPI.loadNPCAsync(fileName);
}

void GameManager::loadObject(const std::string & fileName)
{
	scriptAPI.loadObjectAsync(fileName);
}

void GameManager::runScript(const std::string & fileName)
{
	scriptAPI.runScript(fileName);
}

void GameManager::runScript(const std::string & fileName, const std::string & mapName)
{
	scriptAPI.runScript(fileName, mapName);
}

void GameManager::playMusic(const std::string & fileName)
{
	scriptAPI.playMusic(fileName);
}

void GameManager::stopMusic()
{
	scriptAPI.stopMusic();
}

void GameManager::showMessage(const std::string& str)
{
	scriptAPI.showMessage(str);
}

void GameManager::requestTouchControlsToggle()
{
	// Preserve toggle parity until the next pre-pointer global input stage.
	// Option callbacks run after that stage, so changing visibility here would
	// let the rest of the current frame bypass the raw-pointer transaction.
	touchControlsToggleRequested = !touchControlsToggleRequested;
}

void GameManager::processGlobalInputFrame(bool toggleTouchControls)
{
	toggleTouchControls = toggleTouchControls != touchControlsToggleRequested;
	touchControlsToggleRequested = false;
	std::vector<GameInput::TouchRecoveryContact> contacts;
	for (const AEvent& finger : engine->getAllFingersPosition())
	{
		if (finger.eventData != TOUCH_MOUSEID)
		{
			contacts.push_back(
				{ finger.eventData, finger.eventX, finger.eventY });
		}
	}
	processGlobalInputFrameWithContacts(
		toggleTouchControls, std::move(contacts), getTime());
}

void GameManager::processGlobalInputFrameWithContacts(
	bool toggleTouchControls,
	std::vector<GameInput::TouchRecoveryContact> contacts,
	std::uint64_t nowMilliseconds)
{
	if (controller != nullptr)
	{
		// This global stage runs before pointer dispatch and GameController::onEvent.
		// Clearing lifecycle-owned touch state here prevents stale virtual input
		// from reaching the same frame's world-action queue.
		controller->synchronizeInputLifecycle();
	}
	const auto& input = engine->inputActions();
	const std::uint64_t currentInputLifecycleRevision =
		input.inputLifecycleRevision();
	const bool inputLifecycleChanged = currentInputLifecycleRevision
		!= observedTouchControlsInputLifecycleRevision;
	observedTouchControlsInputLifecycleRevision =
		currentInputLifecycleRevision;

	const bool controlsVisibleAtFrameStart =
		controller == nullptr || controller->areTouchControlsVisible();
	bool controlsVisibleAfterExternalActions = toggleTouchControls
		? !controlsVisibleAtFrameStart
		: controlsVisibleAtFrameStart;
	const auto visibilityDecision = touchControlsVisibilityPolicy.update(
		input.registeredGamepadCount(),
		input.gamepadAdditionRevision(),
		input.activeGamepadRemovalRevision(),
		controlsVisibleAfterExternalActions,
		touchControlsCanRecoverAfterExternalInputLoss);
	if (visibilityDecision.restoreTouchControls)
	{
		controlsVisibleAfterExternalActions = true;
		pendingExternalInputMessage = "手柄已断开，已恢复触控操作区";
	}

	const bool recoveryTriggered = processTouchControlsRecoveryContacts(
		std::move(contacts),
		nowMilliseconds,
		controlsVisibleAtFrameStart,
		controlsVisibleAfterExternalActions,
		inputLifecycleChanged || !input.isInputContextActive());
	const bool finalTouchControlsVisible =
		controlsVisibleAfterExternalActions || recoveryTriggered;
	if (finalTouchControlsVisible)
	{
		touchControlsCanRecoverAfterExternalInputLoss = true;
	}
	if (controller != nullptr
		&& controller->areTouchControlsVisible() != finalTouchControlsVisible)
	{
		controller->setTouchControlsVisible(finalTouchControlsVisible);
	}
	if (visibilityDecision.showGamepadConnectedMessage
		&& !visibilityDecision.restoreTouchControls
		&& touchControlsCanRecoverAfterExternalInputLoss)
	{
		pendingExternalInputMessage = finalTouchControlsVisible
			? "检测到手柄；触控操作区保持显示，可长按 Back+Start 切换"
			: "检测到手柄；触控操作区当前隐藏，可长按 Back+Start 恢复";
	}
	if (toggleTouchControls && !inThread.load() && menu != nullptr)
	{
		menu->showSystemNotice(finalTouchControlsVisible
			? "已显示触控操作区"
			: "已隐藏触控操作区");
	}
	if (!pendingExternalInputMessage.empty() && !inThread.load()
		&& menu != nullptr && menu->systemNotice != nullptr)
	{
		menu->showSystemNotice(pendingExternalInputMessage);
		pendingExternalInputMessage.clear();
	}
	if (recoveryTriggered && !inThread.load() && menu != nullptr)
	{
		menu->showSystemNotice("已通过三指长按恢复触控操作区");
	}
}

bool GameManager::onHandleUIAction(UIAction action)
{
	return menu != nullptr && menu->visible && menu->handleUIAction(action);
}

void GameManager::returnToDesktop()
{
	result = erExit;
	logicRunning = false;
}

void GameManager::clearSelected()
{
	npcManager->clearSelected();
	objectManager->clearSelected();
}

bool GameManager::queueObjectInteraction(std::shared_ptr<Object> obj, bool useRightScript, bool running)
{
	if (obj == nullptr || objectManager == nullptr || !objectManager->findObj(obj) || !canQueuePlayerInteraction(player))
	{
		return false;
	}
	if (useRightScript && obj->scriptFileRight.empty())
	{
		return false;
	}

	NextAction act;
	act.action = getInteractionMoveAction(player, running);
	act.destGE = obj;
	act.destKind = ndObj;
	act.dest = obj->position;
	act.useRightScript = useRightScript || obj->shouldUseRightScriptForPrimaryInteraction();
	if (controller != nullptr)
	{
		controller->cancelControllerWorldInteraction();
	}
	player->addNextAction(act);
	return true;
}

bool GameManager::queueNPCInteraction(std::shared_ptr<NPC> npc, bool useRightScript, bool running)
{
	if (npc == nullptr || npcManager == nullptr || !npcManager->findNPC(npc)
		|| !npc->isVisibleForRuntime() || !npc->isInteractive() || !canQueuePlayerInteraction(player))
	{
		return false;
	}
	if (useRightScript && npc->scriptFileRight.empty())
	{
		return false;
	}

	NextAction act;
	act.action = getInteractionMoveAction(player, running);
	act.destGE = npc;
	act.dest = npc->getPosition();

	bool effectiveUseRightScript = useRightScript || (npc->scriptFile.empty() && !npc->scriptFileRight.empty());
	if (effectiveUseRightScript)
	{
		act.destKind = ndTalk;
		act.useRightScript = true;
	}
	else if (npc->isEnemy() || npc->isNoneFighter())
	{
		act.destKind = ndAttack;
	}
	else
	{
		act.destKind = ndTalk;
	}

	if (controller != nullptr)
	{
		controller->cancelControllerWorldInteraction();
	}
	player->addNextAction(act);
	return true;
}

bool GameManager::queueNearestObjectInteraction(bool useRightScript, bool running, int radius)
{
	if (objectManager == nullptr || map == nullptr || !canQueuePlayerInteraction(player))
	{
		return false;
	}
	if (radius < 0)
	{
		radius = 0;
	}

	auto actionActor = player->getActionActor();
	Point playerPos = actionActor != nullptr ? actionActor->getPosition() : player->getPosition();
	auto objects = objectManager->findRadiusScriptViewObj(playerPos, radius);
	std::shared_ptr<Object> nearestObject = nullptr;
	int nearestDistance = INT_MAX;
	for (auto& object : objects)
	{
		if (object == nullptr)
		{
			continue;
		}
		if (useRightScript && object->scriptFileRight.empty())
		{
			continue;
		}
		int distance = Map::calDistance(playerPos, object->position);
		if (distance < nearestDistance)
		{
			nearestObject = object;
			nearestDistance = distance;
		}
	}
	return queueObjectInteraction(nearestObject, useRightScript, running);
}

bool GameManager::queueNearestNPCInteraction(bool useRightScript, bool running, int radius)
{
	if (npcManager == nullptr || map == nullptr || !canQueuePlayerInteraction(player))
	{
		return false;
	}
	if (radius < 0)
	{
		radius = 0;
	}

	auto actionActor = player->getActionActor();
	Point playerPos = actionActor != nullptr ? actionActor->getPosition() : player->getPosition();
	auto npcs = npcManager->findRadiusScriptViewNPC(playerPos, radius);
	std::shared_ptr<NPC> nearestNPC = nullptr;
	int nearestDistance = INT_MAX;
	for (auto& npc : npcs)
	{
		if (npc == nullptr)
		{
			continue;
		}
		if (!npc->isVisibleForRuntime() || !npc->isInteractive())
		{
			continue;
		}
		if (useRightScript && npc->scriptFileRight.empty())
		{
			continue;
		}
		int distance = Map::calDistance(playerPos, npc->getPosition());
		if (distance < nearestDistance)
		{
			nearestNPC = npc;
			nearestDistance = distance;
		}
	}
	return queueNPCInteraction(nearestNPC, useRightScript, running);
}

bool GameManager::queueObjectScriptInteraction(std::shared_ptr<Object> obj,
	WorldInteractionScriptSide scriptSide, bool running)
{
	const WorldInteractionIntent intent = scriptSide == WorldInteractionScriptSide::Alternate
		? WorldInteractionIntent::Alternate
		: WorldInteractionIntent::Primary;
	if (obj == nullptr || objectManager == nullptr || !objectManager->findObj(obj)
		|| !WorldInteractionResolver::isObjectValidForIntent(obj, intent)
		|| !canQueuePlayerInteraction(player))
	{
		return false;
	}

	bool useRightScript = scriptSide == WorldInteractionScriptSide::Alternate
		|| obj->shouldUseRightScriptForPrimaryInteraction();
	if ((useRightScript && obj->scriptFileRight.empty())
		|| (!useRightScript && obj->scriptFile.empty()))
	{
		return false;
	}

	NextAction action;
	action.action = getInteractionMoveAction(player, running);
	action.destGE = obj;
	action.destKind = ndObj;
	action.dest = obj->position;
	action.useRightScript = useRightScript;
	action.strictWorldInteraction = true;
	player->addNextAction(action);
	return true;
}

bool GameManager::queueNPCTalkInteraction(std::shared_ptr<NPC> npc,
	WorldInteractionScriptSide scriptSide, bool running)
{
	auto actionActor = player != nullptr ? player->getActionActor() : nullptr;
	const WorldInteractionIntent intent = scriptSide == WorldInteractionScriptSide::Alternate
		? WorldInteractionIntent::Alternate
		: WorldInteractionIntent::Primary;
	if (npc == nullptr || npcManager == nullptr || !npcManager->findNPC(npc)
		|| !WorldInteractionResolver::isNPCValidForIntent(npc, intent, actionActor)
		|| !canQueuePlayerInteraction(player))
	{
		return false;
	}

	bool useRightScript = scriptSide == WorldInteractionScriptSide::Alternate
		|| (npc->scriptFile.empty() && !npc->scriptFileRight.empty());
	if ((useRightScript && npc->scriptFileRight.empty())
		|| (!useRightScript && npc->scriptFile.empty()))
	{
		return false;
	}

	NextAction action;
	action.action = getInteractionMoveAction(player, running);
	action.destGE = npc;
	action.destKind = ndTalk;
	action.dest = npc->getPosition();
	action.useRightScript = useRightScript;
	action.strictWorldInteraction = true;
	player->addNextAction(action);
	return true;
}

bool GameManager::queueNPCAttackInteraction(std::shared_ptr<NPC> npc, bool running)
{
	auto actionActor = player != nullptr ? player->getActionActor() : nullptr;
	if (npc == nullptr || npcManager == nullptr || !npcManager->findNPC(npc)
		|| !WorldInteractionResolver::isNPCValidForIntent(
			npc, WorldInteractionIntent::Attack, actionActor)
		|| !canQueuePlayerInteraction(player))
	{
		return false;
	}

	NextAction action;
	action.action = getInteractionMoveAction(player, running);
	action.destGE = npc;
	action.destKind = ndAttack;
	action.dest = npc->getPosition();
	action.strictWorldInteraction = true;
	player->addNextAction(action);
	return true;
}

std::vector<WorldInteractionCandidate> GameManager::findWorldInteractionCandidates(
	WorldInteractionIntent intent, int radius, int nearRadius,
	std::weak_ptr<GameElement> preferredTarget)
{
	if (!canQueuePlayerInteraction(player) || map == nullptr)
	{
		return {};
	}

	auto actionActor = player->getActionActor();
	WorldInteractionQuery query;
	query.origin = actionActor->getPosition();
	query.facingDirection = actionActor->direction;
	query.radius = radius;
	query.nearRadius = nearRadius;
	query.preferredTarget = preferredTarget;
	return WorldInteractionResolver::findCandidates(
		intent, query, map.get(), npcManager.get(), objectManager.get(), actionActor);
}

bool GameManager::queueBestWorldInteraction(
	WorldInteractionIntent intent, bool running, int radius, int nearRadius,
	std::weak_ptr<GameElement> preferredTarget)
{
	auto candidates = findWorldInteractionCandidates(intent, radius, nearRadius, preferredTarget);
	if (candidates.empty())
	{
		return false;
	}

	const auto& candidate = candidates.front();
	if (candidate.targetType == WorldInteractionTargetType::Object)
	{
		WorldInteractionScriptSide scriptSide = intent == WorldInteractionIntent::Alternate
			? WorldInteractionScriptSide::Alternate
			: WorldInteractionScriptSide::Primary;
		return queueObjectScriptInteraction(candidate.object, scriptSide, running);
	}
	if (candidate.targetType == WorldInteractionTargetType::NPC)
	{
		if (intent == WorldInteractionIntent::Attack)
		{
			return queueNPCAttackInteraction(candidate.npc, running);
		}
		WorldInteractionScriptSide scriptSide = intent == WorldInteractionIntent::Alternate
			? WorldInteractionScriptSide::Alternate
			: WorldInteractionScriptSide::Primary;
		return queueNPCTalkInteraction(candidate.npc, scriptSide, running);
	}
	return false;
}

void GameManager::runObjScript(std::shared_ptr<Object> obj, const std::string& scriptFile, bool clearPlayerAction)
{
	if (clearPlayerAction)
	{
		player->nextAction = nullptr;
		player->nextDest = ndNone;
		player->nextDestUseRightScript = false;
		player->nextDestStrictWorldInteraction = false;
		player->nextDestRequestedRunning = false;
		player->destGE.reset();
	}
	std::string scriptFileToRun = scriptFile;
	if (scriptFileToRun.empty() && obj != nullptr)
	{
		scriptFileToRun = obj->scriptFile;
	}
	if (scriptFileToRun.empty())
	{
		return;
	}
	if (inEvent)
	{
		ScriptTask task;
		task.type = stObject;
		task.obj = obj;
		task.scriptName = scriptFileToRun;
		task.clearPlayerAction = clearPlayerAction;
		addScriptTask(task);
		return;
	}
	if (obj != nullptr && objectManager->findObj(obj))
	{
		scriptObj = obj;
		inEvent = true;
		effectManager->disableAllEffect();
		scriptType = stObject;
		runScript(scriptFileToRun);
		scriptType = stNone;
		inEvent = false;
	}
	scriptObj = nullptr;
	camera->followPlayer = true;
	runEventList();
}

void GameManager::runNPCScript(std::shared_ptr<NPC> npc, const std::string& scriptFile, bool clearPlayerAction)
{
	if (clearPlayerAction)
	{
		player->nextAction = nullptr;
		player->nextDest = ndNone;
		player->nextDestUseRightScript = false;
		player->nextDestStrictWorldInteraction = false;
		player->nextDestRequestedRunning = false;
		player->destGE.reset();
	}
	if (inEvent)
	{
		ScriptTask task;
		task.type = stNPC;
		task.npc = npc;
		task.scriptName = scriptFile;
		task.clearPlayerAction = clearPlayerAction;
		addScriptTask(task);
		return;
	}
	if (npc != nullptr && npcManager->findNPC(npc))
	{
		std::string scriptFileToRun = scriptFile.empty() ? npc->scriptFile : scriptFile;
		if (scriptFileToRun.empty())
		{
			scriptNPC = nullptr;
			camera->followPlayer = true;
			runEventList();
			return;
		}
		scriptNPC = npc;
		inEvent = true;
		effectManager->disableAllEffect();
		scriptType = stNPC;
		runScript(scriptFileToRun);
		scriptType = stNone;
		inEvent = false;
	}
	scriptNPC = nullptr;
	camera->followPlayer = true;
	runEventList();
}

void GameManager::runNPCDeathScript(std::shared_ptr<NPC> npc, const std::string & scriptName, const std::string & scriptMapName)
{
	//if (!npcManager->findNPC(npc))
	//{
	//	return;
	//}
	if (scriptName.empty())
	{
		return;
	}

	player->nextAction = nullptr;
	player->nextDest = ndNone;
	player->nextDestUseRightScript = false;
	player->nextDestStrictWorldInteraction = false;
	player->nextDestRequestedRunning = false;
	player->destGE.reset();
	if (inEvent)
	{
		if (!scriptName.empty())
		{
			EventInfo eventInfo;
			eventInfo.npc = npc;
			eventInfo.scriptName = scriptName;
			eventInfo.scriptMapName = scriptMapName;
			eventList.push_back(eventInfo);
		}
		return;
	}
	if (npc != nullptr)
	{
		if (npcManager->findNPC(npc))
		{
			scriptNPC = npc;
		}
		else
		{
			scriptNPC = nullptr;
		}
		inEvent = true;
		//effectManager->disableAllEffect();
		scriptType = stNPCDeath;
		runScript(scriptName, scriptMapName);
		scriptType = stNone;
		inEvent = false;
	}
	scriptNPC = nullptr;
	camera->followPlayer = true;
	runEventList();
}

void GameManager::runEventList()
{
	if (runPendingPlayerDeathScript())
	{
		return;
	}
	if (eventList.size() > 0)
	{
		auto deathNPC = eventList[0].npc;
		std::string tempScriptName = eventList[0].scriptName;
		std::string tempScriptMapName = eventList[0].scriptMapName;
		eventList.erase(eventList.begin());
		runNPCDeathScript(deathNPC, tempScriptName, tempScriptMapName);
	}
}

bool GameManager::addScriptTask(const ScriptTask& task)
{
	std::lock_guard<std::mutex> lock(scriptTaskMutex);
	if (task.type == stScript)
	{
		std::size_t parallelScriptCount = static_cast<std::size_t>(std::count_if(
			scriptTaskList.begin(), scriptTaskList.end(), [](const ScriptTask& queuedTask)
			{
				return queuedTask.type == stScript;
			}));
		if (parallelScriptCount >= MaxParallelScriptStates)
		{
			return false;
		}
	}
	scriptTaskList.push_back(task);
	return true;
}

void GameManager::clearParallelScriptTasks()
{
	std::lock_guard<std::mutex> lock(scriptTaskMutex);
	scriptTaskList.erase(
		std::remove_if(
			scriptTaskList.begin(),
			scriptTaskList.end(),
			[](const ScriptTask& task)
			{
				return task.type == stScript;
			}),
		scriptTaskList.end());
}

void GameManager::runScriptTaskList()
{
	if (runPendingPlayerDeathScript())
	{
		return;
	}
	std::vector<ScriptTask> tasks;
	std::vector<ScriptTask> pendingTasks;
	UTime frameTime = getFrameTime();
	{
		std::lock_guard<std::mutex> lock(scriptTaskMutex);
		if (scriptTaskList.empty())
		{
			return;
		}
		tasks = std::move(scriptTaskList);
		scriptTaskList.clear();
	}

	for (auto& task : tasks)
	{
		if (task.remainingMilliseconds > 0)
		{
			if (task.remainingMilliseconds > frameTime)
			{
				task.remainingMilliseconds -= frameTime;
				pendingTasks.push_back(task);
				continue;
			}
			task.remainingMilliseconds = 0;
		}
		switch (task.type)
		{
		case stScript:
			scriptAPI.runScriptWithCapturedParent(
				task.scriptName,
				task.scriptMapName.empty()
					? mapFolderName
					: task.scriptMapName,
				task.traceParentExecutionId,
				task.traceParentCaptured);
			break;
		case stNPC:
			runNPCScript(task.npc, task.scriptName, task.clearPlayerAction);
			break;
		case stNPCDeath:
			runNPCDeathScript(task.npc, task.scriptName, task.scriptMapName);
			break;
		case stObject:
			runObjScript(task.obj, task.scriptName, task.clearPlayerAction);
			break;
		case stTraps:
			runTrapScript(task.trapIndex);
			break;
		case stGoods:
			runGoodsScript(task.goods);
			break;
		default:
			break;
		}
		if (runPendingPlayerDeathScript())
		{
			return;
		}
	}

	if (!pendingTasks.empty())
	{
		std::lock_guard<std::mutex> lock(scriptTaskMutex);
		std::size_t parallelScriptCount = static_cast<std::size_t>(std::count_if(
			pendingTasks.begin(), pendingTasks.end(), [](const ScriptTask& task)
			{
				return task.type == stScript;
			}));
		for (const auto& task : scriptTaskList)
		{
			if (task.type == stScript)
			{
				if (parallelScriptCount >= MaxParallelScriptStates)
				{
					continue;
				}
				parallelScriptCount++;
			}
			pendingTasks.push_back(task);
		}
		scriptTaskList = std::move(pendingTasks);
	}
}

void GameManager::runGoodsScript(std::shared_ptr<Goods> goods)
{
	player->nextAction = nullptr;
	player->nextDest = ndNone;
	player->nextDestUseRightScript = false;
	player->nextDestStrictWorldInteraction = false;
	player->nextDestRequestedRunning = false;
	player->destGE.reset();
	if (!(player->isJumping() && player->getJumpState() == jsJumping))
	{
		player->beginStand();
	}
	if (inEvent)
	{
		ScriptTask task;
		task.type = stGoods;
		task.goods = goods;
		addScriptTask(task);
		return;
	}
	if (goods != nullptr)
	{
		scriptGoods = goods;
		inEvent = true;
		effectManager->disableAllEffect();
		scriptType = stGoods;
		runScript(goods->script);
		scriptType = stNone;
		inEvent = false;
	}
	scriptGoods = nullptr;
	camera->followPlayer = true;
	runEventList();
}

void GameManager::runTrapScript(int idx)
{
	if (!Traps::isValidIndex(idx))
	{
		GameLog::write("GameManager::runTrapScript ignored invalid trap index %d", idx);
		return;
	}

	player->nextAction = nullptr;
	player->nextDest = ndNone;
	player->nextDestUseRightScript = false;
	player->nextDestStrictWorldInteraction = false;
	player->nextDestRequestedRunning = false;
	player->destGE.reset();
	if (!(player->isJumping() && player->getJumpState() == jsJumping))
	{
		player->beginStand();
	}
	if (inEvent)
	{
		ScriptTask task;
		task.type = stTraps;
		task.trapIndex = idx;
		addScriptTask(task);
		return;
	}
	scriptMapName = mapFolderName;
	scriptTrapIndex = idx;
	std::string sname = traps.get(mapFolderName, idx);
	if (sname != "" && !traps.hasTriggered(idx))
	{
		inEvent = true;
		effectManager->disableAllEffect();
		traps.markTriggered(idx);
		std::string tempMapName = mapFolderName;
		scriptType = stTraps;
		runScript(sname);
		scriptType = stNone;
		inEvent = false;
	}
	scriptMapName = "";
	scriptTrapIndex = 0;
	camera->followPlayer = true;
	runEventList();
}

void GameManager::onUpdate()
{
	if (engine->consumeInputAction(GameInput::InputAction::ToggleTouchControls)
		&& controller != nullptr)
	{
		// The normal frame-global handler consumes this edge before pointer
		// dispatch. If an alternate update path reaches it here, defer the
		// visibility edge to the next global transaction instead of applying it
		// after pointer dispatch.
		requestTouchControlsToggle();
	}

	if (postNewGameAutomationWaitPending)
	{
		postNewGameAutomationWaitElapsed += getFrameTime();
		if (postNewGameAutomationWaitElapsed >= postNewGameAutomationWaitMilliseconds)
		{
			postNewGameAutomationWaitPending = false;
			finishNewGameAutomationAndExit();
			return;
		}
	}

	if (gameplayPaused)
	{
		return;
	}
	if (runPendingPlayerDeathScript())
	{
		return;
	}
	runEventList();
	if (runPendingPlayerDeathScript())
	{
		return;
	}
	runScriptTaskList();
	if (runPendingPlayerDeathScript())
	{
		return;
	}

	if (timerStarted)
	{
		int previousSeconds = timerSeconds;
		timerAccumulated += getFrameTime();
		UTime elapsedSeconds = timerAccumulated / 1000;
		timerAccumulated %= 1000;
		if (elapsedSeconds > 0)
		{
			if (elapsedSeconds >= static_cast<UTime>(timerSeconds))
			{
				timerSeconds = 0;
			}
			else
			{
				timerSeconds -= static_cast<int>(elapsedSeconds);
			}
		}

		if (timeScriptSet && shouldTriggerTimeScript(previousSeconds, timerSeconds, timeScriptSeconds))
		{
			timeScriptSet = false;
			std::string sname = timeScriptFileName;
			timeScriptFileName = "";
			runScript(sname);
		}

		if (timerSeconds <= 0)
		{
			timerSeconds = 0;
			timerStarted = false;
			timerAccumulated = 0;
			timeScriptSet = false;
			timeScriptFileName = "";
		}
	}
}

bool GameManager::runPendingPlayerDeathScript()
{
	if (player == nullptr || (player->result & erRunDeathScript) == 0)
	{
		return false;
	}

	player->result &= ~erRunDeathScript;
	player->cancelQueuedInteraction();
	// A game-over script supersedes NPC deaths, traps and interactions that
	// became ready in the same frame; keeping them would advance the story first.
	eventList.clear();
	{
		std::lock_guard<std::mutex> lock(scriptTaskMutex);
		scriptTaskList.clear();
	}
	runNPCDeathScript(
		std::dynamic_pointer_cast<NPC>(player),
		player->deathScript,
		mapFolderName);
	return true;
}

void GameManager::setTouchControlsRecoveryInputBlocked(bool blocked)
{
	if (touchControlsRecoveryInputBlocked == blocked)
	{
		return;
	}
	cancelPointerInteraction();
	Element::setRawPointerInputBlocked(blocked);
	touchControlsRecoveryInputBlocked = blocked;
}

void GameManager::resetTouchControlsRecoveryGesture()
{
	touchControlsRecoveryGesture.reset();
	touchControlsRecoveryAwaitingRelease = false;
	touchControlsRecoveryEmptyFrameObserved = false;
	setTouchControlsRecoveryInputBlocked(false);
}

bool GameManager::processTouchControlsRecoveryContacts(
	std::vector<GameInput::TouchRecoveryContact> contacts,
	std::uint64_t nowMilliseconds,
	bool controlsVisibleAtFrameStart,
	bool controlsVisibleAfterExternalActions,
	bool resetRecognitionForLifecycle)
{
	if (controller == nullptr)
	{
		resetTouchControlsRecoveryGesture();
		return false;
	}

	if (touchControlsRecoveryInputBlocked
		&& touchControlsRecoveryEmptyFrameObserved)
	{
		// The previous empty-contact frame drained every queued release. End the
		// old transaction before evaluating this frame, which may contain a new
		// touch or a new visibility transition that needs its own gate.
		resetTouchControlsRecoveryGesture();
	}

	if (touchControlsRecoveryInputBlocked && contacts.empty())
	{
		touchControlsRecoveryGesture.reset();
		touchControlsRecoveryAwaitingRelease = true;
		touchControlsRecoveryEmptyFrameObserved = true;
		return false;
	}
	if (touchControlsRecoveryInputBlocked)
	{
		touchControlsRecoveryEmptyFrameObserved = false;
	}

	const bool hasRecoveryCandidate =
		contacts.size()
			== GameInput::TouchControlsRecoveryGesture::RequiredContactCount;
	const bool visibilityChanged =
		controlsVisibleAtFrameStart != controlsVisibleAfterExternalActions;
	if (!touchControlsRecoveryInputBlocked
		&& (visibilityChanged
			|| (hasRecoveryCandidate
				&& !controlsVisibleAfterExternalActions)))
	{
		setTouchControlsRecoveryInputBlocked(true);
		// An empty SDL contact snapshot can still have a complete down/up tap
		// queued for this frame. Keep the transition frame gated, then allow the
		// next empty frame to release it.
		touchControlsRecoveryEmptyFrameObserved = contacts.empty();
	}

	if (resetRecognitionForLifecycle)
	{
		touchControlsRecoveryGesture.reset();
		if (touchControlsRecoveryInputBlocked)
		{
			touchControlsRecoveryAwaitingRelease = true;
		}
		return false;
	}

	if (controlsVisibleAfterExternalActions)
	{
		touchControlsRecoveryGesture.reset();
		if (touchControlsRecoveryInputBlocked)
		{
			touchControlsRecoveryAwaitingRelease = true;
		}
		return false;
	}

	if (touchControlsRecoveryAwaitingRelease
		|| !touchControlsRecoveryInputBlocked)
	{
		touchControlsRecoveryGesture.reset();
		return false;
	}

	if (!touchControlsRecoveryGesture.update(
		std::move(contacts), nowMilliseconds, false))
	{
		return false;
	}

	touchControlsRecoveryAwaitingRelease = true;
	return true;
}

void GameManager::onDraw()
{
	map->drawMap();
	if (global.data.scriptShowMapPos && player != nullptr)
	{
		Point position = player->getPosition();
		engine->drawText(convert::formatString("Map: %d, %d", position.x, position.y), 8, 8, 18, 0xD0FFFFFF);
	}
}

bool GameManager::onInitial()
{
	clearLastLoadFailureMessage();
	const auto& manifest = ResourceManager::instance().getActiveManifest();
	global.useWav = manifest.useWav;
	global.applyResourceManifestFeatures(manifest);
	global.loadUiSettings();
	goodsManager.configureLayout();
	magicManager.configureLayout();

	if (!SaveFileManager::RecoverInterruptedSaveOperations())
	{
		GameLog::write(
			"GameManager: startup save recovery was incomplete; unaffected slots remain available\n");
	}

	if (!initMenu())
	{
		setLastLoadFailureMessage(u8"游戏界面初始化失败");
		return false;
	}

	if (editorRunMode)
	{
		editorRunSceneApplicationResult =
			applyEditorRunSceneTarget();
		editorRunSceneApplicationCompleted = true;
		if (!editorRunSceneApplicationResult.succeeded())
		{
			GameLog::write(
				"GameManager: editor-run scene application failed "
				"code=%s field=%s path=%s line=%u column=%u message=%s\n",
				editorRunSceneApplicationResult.diagnosticCode.c_str(),
				editorRunSceneApplicationResult.fieldPath.c_str(),
				editorRunSceneApplicationResult.virtualPath.c_str(),
				editorRunSceneApplicationResult.line,
				editorRunSceneApplicationResult.column,
				editorRunSceneApplicationResult.message.c_str());
			return false;
		}
	}
	else if (gameIndex == 0)
	{
		applyStartupIntegerVariables();
		// 新游戏入口脚本：优先使用 Manifest NewGame.Script；为空时回退到 newgame.txt。
		std::string newGameScript = manifest.newGameScript;
		if (newGameScript.empty())
		{
			newGameScript = "newgame.txt";
		}
		inEvent = true;
		scriptAPI.runScript(newGameScript);
		inEvent = false;
		if (!lastLoadFailureMessage.empty())
		{
			return false;
		}
		const unsigned int sceneCompletion =
			result & (erOK | erReturnToTitle);
		if (sceneCompletion != 0)
		{
			return lastLoadFailureMessage.empty();
		}
		bool hasNewGameSave = SaveFileManager::HasSaveFile(0);
		if (map->data == nullptr && hasNewGameSave)
		{
			GameLog::write("GameManager: new game script did not load a map, fallback to save index 0\n");
			bool loaded = false;
			if (Config::loadAsync)
			{
				loaded = scriptAPI.loadGameAsync(0);
			}
			else
			{
				loaded = scriptAPI.loadGame(0);
			}
			if (!loaded)
			{
				return false;
			}
		}
		if (map == nullptr || map->data == nullptr)
		{
			setLastLoadFailureMessage(
				hasNewGameSave
					? std::string(u8"新游戏基础存档没有载入地图")
					: std::string(u8"新游戏脚本没有载入地图，且基础存档不存在"));
			return false;
		}
		if (exitAfterNewGameScript)
		{
			if (postNewGameAutomationWaitMilliseconds > 0)
			{
				postNewGameAutomationWaitElapsed = 0;
				postNewGameAutomationWaitPending = true;
				GameLog::write("GameManager: post new game automation wait %llu ms\n",
					static_cast<unsigned long long>(postNewGameAutomationWaitMilliseconds));
			}
			else
			{
				finishNewGameAutomationAndExit();
			}
		}
		else
		{
			checkExpectedIntegerVariables();
		}
	}
	else
	{
		bool loaded = false;
		if (Config::loadAsync)
		{
			loaded = scriptAPI.loadGameAsync(gameIndex);
		}
		else
		{
			loaded = scriptAPI.loadGame(gameIndex);
		}
		if (!loaded)
		{
			return false;
		}
		weather->fadeInEx();
	}	

	return true;
}

void GameManager::onRun()
{
	player->checkTrap();
}

void GameManager::onExit()
{
	freeResource();
	engine->stopBGM();
}

void GameManager::onEvent()
{
	if (!global.data.canInput)
	{
		return;
	}
}

bool GameManager::onHandleEvent(AEvent & e)
{
	if (!global.data.canInput)
	{
		return false;
	}
	if (e.eventType == ET_KEYDOWN)
	{
		const bool usesCheatShortcutModifiers =
			(engine->getKeyPress(KEY_LSHIFT) || engine->getKeyPress(KEY_RSHIFT))
			&& !engine->getKeyPress(KEY_LALT)
			&& !engine->getKeyPress(KEY_RALT)
			&& !engine->getKeyPress(KEY_LCTRL)
			&& !engine->getKeyPress(KEY_RCTRL);
		if (e.eventData == KEY_F12 && usesCheatShortcutModifiers)
		{
			toggleCheatMode();
		}
		else if (e.eventData == KEY_Q && usesCheatShortcutModifiers)
		{
			performCheatAction(CheatAction::RestorePlayerResources);
		}
		else if (e.eventData == KEY_W && usesCheatShortcutModifiers)
		{
			performCheatAction(CheatAction::IncreasePracticeMagicLevel);
		}
		else if (e.eventData == KEY_E && usesCheatShortcutModifiers)
		{
			performCheatAction(CheatAction::IncreasePlayerLevel);
		}
		else if (e.eventData == KEY_R && usesCheatShortcutModifiers)
		{
			performCheatAction(CheatAction::AddMoney);
		}
	}
	return false;
}

bool GameManager::isCheatModeEnabled() const noexcept
{
	return cheatMode;
}

bool GameManager::isCheatInvincibilityEnabled() const noexcept
{
	return cheatInvincibilityEnabled;
}

bool GameManager::shouldProtectPlayerFromCheatDamage() const noexcept
{
	return cheatMode && cheatInvincibilityEnabled;
}

GameManager::CheatOperationResult GameManager::setCheatModeEnabled(bool enabled)
{
	cheatMode = enabled;
	if (!enabled)
	{
		cheatInvincibilityEnabled = false;
	}
	return completeCheatOperation(
		true, enabled ? "作弊模式已开启" : "作弊模式已关闭");
}

GameManager::CheatOperationResult GameManager::toggleCheatMode()
{
	return setCheatModeEnabled(!cheatMode);
}

GameManager::CheatOperationResult GameManager::performCheatAction(
	CheatAction action)
{
	if (!cheatMode)
	{
		return completeCheatOperation(false, "请先开启作弊模式");
	}
	if (player == nullptr)
	{
		return completeCheatOperation(false, "角色数据不可用");
	}

	switch (action)
	{
	case CheatAction::ToggleInvincibility:
		cheatInvincibilityEnabled = !cheatInvincibilityEnabled;
		return completeCheatOperation(true,
			cheatInvincibilityEnabled ? "无敌模式已开启" : "无敌模式已关闭");
	case CheatAction::RestorePlayerResources:
		player->fullMana();
		player->fullLife();
		player->fullThew();
		return completeCheatOperation(true, "生命、内力和体力已补满");
	case CheatAction::IncreasePracticeMagicLevel:
	{
		const int practiceIndex = magicManager.practiceIndex();
		if (!magicManager.magicListExists(practiceIndex))
		{
			return completeCheatOperation(false, "当前没有正在修炼的武功");
		}
		MagicInfo& practiceMagic = magicManager.magicList[
			static_cast<std::size_t>(practiceIndex)];
		if (practiceMagic.magic == nullptr)
		{
			return completeCheatOperation(false, "当前没有正在修炼的武功");
		}
		if (practiceMagic.level >= MAGIC_MAX_LEVEL)
		{
			return completeCheatOperation(false,
				convert::formatString("%s已达到最高等级",
					practiceMagic.magic->name.c_str()));
		}
		if (!applyCheatPracticeMagicLevelIncrease())
		{
			return completeCheatOperation(false,
				"当前修炼武功无法继续提升");
		}
		return completeCheatOperation(true,
			convert::formatString("%s已提升至%d级",
				practiceMagic.magic->name.c_str(), practiceMagic.level));
	}
	case CheatAction::IncreasePlayerLevel:
		if (player->level >= static_cast<int>(player->levelList.size()))
		{
			return completeCheatOperation(false, "角色已达到最高等级");
		}
		if (!applyCheatPlayerLevelIncrease())
		{
			return completeCheatOperation(false, "当前角色无法继续提升");
		}
		return completeCheatOperation(true,
			convert::formatString("角色已提升至%d级", player->level));
	case CheatAction::AddMoney:
	{
		const int previousMoney = player->money;
		player->addMoney(100000);
		if (player->money == previousMoney)
		{
			return completeCheatOperation(false, "银两已达到上限");
		}
		return completeCheatOperation(true, convert::formatString(
			"已增加100000两银子，当前银两：%d", player->money));
	}
	default:
		return completeCheatOperation(false, "未知作弊操作");
	}
}

void GameManager::showCheatNotice(const std::string& message)
{
	if (menu != nullptr)
	{
		menu->showSystemNotice(message);
	}
}

GameManager::CheatOperationResult GameManager::completeCheatOperation(
	bool succeeded,
	const std::string& message)
{
	showCheatNotice(message);
	return { succeeded, message };
}

bool GameManager::applyCheatPlayerLevelIncrease()
{
	return player != nullptr && player->addExperienceToNextLevel();
}

bool GameManager::applyCheatPracticeMagicLevelIncrease()
{
	return magicManager.addPracticeExperienceToNextLevel();
}

void GameManager::resetExclusiveLoadingInputState()
{
	cancelPointerInteraction();
	if (engine != nullptr)
	{
		engine->releasePhysicalInputsForContextTransition();
	}
}
