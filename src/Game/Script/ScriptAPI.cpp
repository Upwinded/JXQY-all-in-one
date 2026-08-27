#include <algorithm>
#include <cerrno>
#include <climits>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>
#include "ScriptAPI.h"
#include "ScriptNpcAction.h"
#include "../GameManager/GameManager.h"
#include "../Data/ColorStyle.h"
#include "../Data/Effect.h"
#include "../Data/Goods.h"
#include "../Data/Magic.h"
#include "../Data/MediaPathResolver.h"
#include "../Data/NewYearPeriod.h"
#include "../Data/NPC.h"
#include "../Data/ObjectPersistence.h"
#include "../Data/ProjectedMovement.h"
#include "../../Engine/Engine.h"
#include "../../Engine/AudioDecodeSafety.h"
#include "../../File/File.h"
#include "../../File/INIReader.h"
#include "../../File/RootedResourceReader.h"
#include "../../File/StrictRelativeResourcePath.h"
#include "../../File/log.h"
#include "../../Launch/EditorRunRuntimeTraceWriter.h"
#include "../../libconvert/libconvert.h"
#include "../../Weather/Weather.h"
#include "../Menu/BuySellMenu.h"
#include "../../Component/VideoPlayer.h"
#include "../GameManager/SaveFileManager.h"
#include "../GameManager/RuntimeSaveGenerationPolicy.h"

namespace
{
constexpr std::size_t MaximumEditorRunScriptBytes =
	16 * 1024 * 1024;
constexpr std::size_t MaximumEditorRunNpcListBytes =
	16 * 1024 * 1024;
constexpr const char* LoadCandidateGeneration =
	"save\\load_candidate\\";
constexpr UTime LoadingPresentationFrameIntervalMilliseconds = 16;

std::string resolveLegacyMapFolderName(
	const std::string& fileName);

bool npcOrObjectListExists(const std::string& fileName)
{
	return !fileName.empty() &&
		(File::fileExist(
				SaveFileManager::CurrentPath() + fileName) ||
			File::fileExist(
				std::string(INI_SAVE_FOLDER) + fileName));
}

const std::vector<std::uint8_t>& compatibleEmptyEntityListBytes()
{
	static const std::string text =
		"[Head]\n"
		"Count=0\n";
	static const std::vector<std::uint8_t> bytes(
		text.cbegin(),
		text.cend());
	return bytes;
}

struct PreparedSaveResources
{
	Map::PreparedLoadCandidate map;
	NPCManager::PreparedLoad npc;
	PreparedObjectLoad object;
	std::string mapFolderName;

	bool isValid() const noexcept
	{
		return map.isValid() && npc.isValid() &&
			object.isPrepared();
	}
};

bool prepareSaveResources(
	GameManager* gameManager,
	const std::string& preparedDirectory,
	PreparedSaveResources& preparedResources,
	const std::function<bool()>& preparationCheckpoint)
{
	preparedResources = PreparedSaveResources{};
	if (gameManager == nullptr ||
		gameManager->map == nullptr ||
		gameManager->npcManager == nullptr ||
		gameManager->objectManager == nullptr)
	{
		return false;
	}
	SaveFileManager::CurrentPathScope generationPath(
		preparedDirectory);
	if (!generationPath.valid())
	{
		return false;
	}
	INIReader globalIni(
		SaveFileManager::CurrentPath() + GLOBAL_INI);
	if (globalIni.ParseError() != 0)
	{
		return false;
	}

	const std::string mapName =
		globalIni.Get("State", "Map", "");
	const std::string npcName =
		globalIni.Get("State", "Npc", "");
	const std::string objectName =
		globalIni.Get("State", "Obj", "");
	if (mapName.empty() ||
		(preparationCheckpoint && !preparationCheckpoint()))
	{
		return false;
	}
	preparedResources.mapFolderName =
		resolveLegacyMapFolderName(mapName);
	const std::string fallbackMpcFolder =
		preparedResources.mapFolderName.empty()
			? std::string()
			: "mpc\\map\\" +
				preparedResources.mapFolderName + "\\";
	if (!Map::prepareLoadCandidate(
			std::string(MAP_FOLDER) + mapName,
			preparedResources.map,
			preparationCheckpoint,
			fallbackMpcFolder))
	{
		return false;
	}

	const bool missingNpcList =
		!npcOrObjectListExists(npcName);
	if (missingNpcList)
	{
		GameLog::write(
			"ScriptAPI: compatible save is missing NPC list %s; preparing an empty list\n",
			npcName.c_str());
	}
	if ((preparationCheckpoint && !preparationCheckpoint()) ||
		!(missingNpcList
			? gameManager->npcManager->
				prepareExactResourceBytes(
					npcName,
					compatibleEmptyEntityListBytes(),
					preparedResources.npc)
			: gameManager->npcManager->prepareLoad(
				npcName,
				preparedResources.npc,
				true)))
	{
		return false;
	}

	const bool missingObjectList =
		!npcOrObjectListExists(objectName);
	if (missingObjectList)
	{
		GameLog::write(
			"ScriptAPI: compatible save is missing object list %s; preparing an empty list\n",
			objectName.c_str());
	}
	if ((preparationCheckpoint && !preparationCheckpoint()) ||
		!(missingObjectList
			? gameManager->objectManager->
				prepareExactResourceBytes(
					objectName,
					compatibleEmptyEntityListBytes(),
					preparedResources.object)
			: gameManager->objectManager->prepareLoad(
				objectName,
				preparedResources.object,
				true)))
	{
		return false;
	}
	return (!preparationCheckpoint || preparationCheckpoint()) &&
		preparedResources.isValid();
}

bool saveGenerationCancellationRequested(
	const SaveGenerationPreflightPolicy& policy) noexcept
{
	if (!policy.cancellationRequested)
	{
		return false;
	}
	try
	{
		return policy.cancellationRequested();
	}
	catch (...)
	{
		return true;
	}
}

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

SaveGenerationResult prepareLoadGeneration(
	int index,
	const SaveGenerationPreflightPolicy& policy,
	std::string& preparedDirectory)
{
	std::string sourceDirectory = index == 0
		? std::string(INI_SAVE_FOLDER)
		: (index < 0
			? std::string(SAVE_AUTO_FOLDER)
			: convert::formatString(SAVE_FOLDER, index));

	if (index != 0 &&
		!File::recoverDirectoryCopy(sourceDirectory))
	{
		SaveGenerationResult result;
		result.error =
			SaveGenerationError::SourceRecoveryFailed;
		result.sourceDirectory = sourceDirectory;
		result.errorPath = sourceDirectory;
		return result;
	}
	preparedDirectory = LoadCandidateGeneration;
	if (!SaveFileManager::CopySaveGenerationWithinLimits(
			sourceDirectory,
			preparedDirectory,
			policy.limits,
			{ SAVE_LIST_FILE },
			policy.cancellationRequested))
	{
		SaveGenerationResult result;
		result.error =
			saveGenerationCancellationRequested(policy)
				? SaveGenerationError::Cancelled
				: SaveGenerationError::PublicationFailed;
		result.sourceDirectory = sourceDirectory;
		result.destinationDirectory = preparedDirectory;
		result.errorPath = preparedDirectory;
		return result;
	}
	SaveGenerationResult result;
	result.sourceDirectory = sourceDirectory;
	result.destinationDirectory = preparedDirectory;
	return result;
}

GameLoading::LoadingTaskResult loadingFailure(
	const std::string& operation,
	const SaveGenerationResult& result)
{
	return GameLoading::LoadingTaskResult::failure(
		operation + ": " +
		SaveFileManager::DescribeSaveGenerationError(
			result.error) +
		(result.errorPath.empty()
			? std::string()
			: " (" + result.errorPath + ")"));
}

class FunctionScopeExit final
{
public:
	explicit FunctionScopeExit(std::function<void()> cleanup) :
		cleanup(std::move(cleanup))
	{
	}

	~FunctionScopeExit()
	{
		run();
	}

	void run() noexcept
	{
		if (!cleanup)
		{
			return;
		}
		try
		{
			cleanup();
		}
		catch (...)
		{
		}
		cleanup = {};
	}

private:
	std::function<void()> cleanup;
};

std::string lowerAscii(std::string value)
{
	for (char& character : value)
	{
		if (character >= 'A' && character <= 'Z')
		{
			character = static_cast<char>(
				character + ('a' - 'A'));
		}
	}
	return value;
}

std::optional<std::string> normalizedTraceVirtualPath(
	const std::string& path)
{
	const ResourcePathSafety::StrictRelativePathResult normalized =
		ResourcePathSafety::normalizeStrictRelativeResourcePath(path);
	if (!normalized.succeeded())
	{
		return std::nullopt;
	}
	return normalized.normalizedPath;
}

bool sameTraceVirtualPath(
	const std::string& left,
	const std::string& right)
{
#if defined(_WIN32)
	return lowerAscii(left) == lowerAscii(right);
#else
	return left == right;
#endif
}

const EditorRun::TraceContentRootIdentity*
traceIdentityForSource(
	const EditorRun::SearchRoot& root,
	const std::string& virtualPath,
	std::string* resolvedVirtualPath)
{
	if (root.kind != EditorRun::SearchRootKind::Overlay)
	{
		*resolvedVirtualPath = virtualPath;
		return root.traceContentRoot
			? &*root.traceContentRoot
			: nullptr;
	}
	const auto origin = std::find_if(
		root.traceOverlayOrigins.cbegin(),
		root.traceOverlayOrigins.cend(),
		[&virtualPath](
			const EditorRun::TraceOverlayOrigin& candidate)
		{
			return sameTraceVirtualPath(
				candidate.virtualPath,
				virtualPath);
		});
	if (origin == root.traceOverlayOrigins.cend())
	{
		return nullptr;
	}
	*resolvedVirtualPath = origin->virtualPath;
	return &origin->contentRoot;
}

bool makeResolvedTraceScriptSource(
	const EditorRun::SearchRoot& root,
	const std::string& virtualPath,
	RootedResourceReader::Result sourceBytes,
	ResolvedTraceScriptSource& source)
{
	const std::optional<std::string> normalizedPath =
		normalizedTraceVirtualPath(virtualPath);
	if (!normalizedPath || !sourceBytes.succeeded())
	{
		return false;
	}
	std::string resolvedVirtualPath;
	const EditorRun::TraceContentRootIdentity* identity =
		traceIdentityForSource(
			root,
			*normalizedPath,
			&resolvedVirtualPath);

	source = {};
	source.bytes = std::move(sourceBytes.bytes);
	source.identity.virtualPath =
		resolvedVirtualPath.empty()
			? *normalizedPath
			: resolvedVirtualPath;
	source.identity.sourceLayer =
		root.kind == EditorRun::SearchRootKind::Overlay
			? EditorRun::RuntimeTraceSourceLayer::Overlay
			: EditorRun::RuntimeTraceSourceLayer::Formal;
	if (identity != nullptr)
	{
		source.identity.rootKind = identity->kind;
		source.identity.rootOrdinal = identity->ordinal;
		source.identity.resourcePackId =
			identity->resourcePackId;
	}
	else
	{
		// Provenance is optional trace metadata. A missing or stale logical
		// origin must never prevent an otherwise valid script from running.
		source.identity.rootOrdinal =
			EditorRun::UnknownTraceContentRootOrdinal;
	}
	return true;
}

void enqueueMapChange(
	EditorRun::RuntimeTraceWriter* writer,
	const std::string& target,
	std::uint64_t executionId)
{
	if (writer == nullptr)
	{
		return;
	}
	const std::optional<std::string> normalizedPath =
		normalizedTraceVirtualPath(target);
	if (!normalizedPath)
	{
		return;
	}
	EditorRun::RuntimeTraceMapChangeEvent mapChange;
	if (executionId > 0)
	{
		mapChange.executionId = executionId;
	}
	std::string eventTarget = *normalizedPath;
	const std::string mapPrefix = "map/";
	if (eventTarget.size() > mapPrefix.size() &&
		lowerAscii(eventTarget.substr(
			0,
			mapPrefix.size())) == mapPrefix)
	{
		eventTarget.erase(0, mapPrefix.size());
	}
	mapChange.target = std::move(eventTarget);
	EditorRun::RuntimeTraceEvent event;
	event.payload = std::move(mapChange);
	(void)writer->enqueue(std::move(event));
}

RootedResourceReader::Result readExactEditorRunResource(
	const EditorRun::SearchRoot& root,
	const std::string& virtualPath,
	std::size_t maximumBytes,
	const char* resourceKind)
{
	RootedResourceReader::Result result =
		root.kind == EditorRun::SearchRootKind::Overlay
			? RootedResourceReader::
				readBoundedFileFromRoot(
					root.anchor,
					virtualPath,
					maximumBytes)
			: RootedResourceReader::
				readBoundedFileFromRoot(
					root.root,
					virtualPath,
					maximumBytes);
	if (!result.succeeded() &&
		result.status != RootedResourceReader::Status::NotFound)
	{
		GameLog::write(
			"ScriptAPI: exact-root %s read failed status=%d path=%s\n",
			resourceKind,
			static_cast<int>(result.status),
			virtualPath.c_str());
	}
	return result;
}

struct EditorRunResourceRead
{
	const EditorRun::SearchRoot* root = nullptr;
	RootedResourceReader::Result result;
};

EditorRunResourceRead readFirstEditorRunResource(
	const std::vector<EditorRun::SearchRoot>& roots,
	const std::string& virtualPath,
	std::size_t maximumBytes,
	const char* resourceKind)
{
	EditorRunResourceRead selected;
	selected.result.status = RootedResourceReader::Status::NotFound;
	for (const EditorRun::SearchRoot& root : roots)
	{
		RootedResourceReader::Result result =
			readExactEditorRunResource(
				root,
				virtualPath,
				maximumBytes,
				resourceKind);
		if (result.status == RootedResourceReader::Status::NotFound)
		{
			continue;
		}
		selected.root = &root;
		selected.result = std::move(result);
		return selected;
	}
	return selected;
}

std::string resolveLegacyMapFolderName(const std::string& fileName)
{
	std::string mapFolderName = convert::extractFileName(fileName);
	std::unique_ptr<char[]> mapNames;
	int mapNamesLength = 0;
	if (File::readFile(
			std::string(INI_MAP_FOLDER) + INI_MAP_NAME_LIST,
			mapNames,
			mapNamesLength,
			1024 * 1024) &&
		mapNamesLength > 0 &&
		mapNames != nullptr)
	{
		INIReader ini(mapNames);
		if (ini.ParseError() == 0)
		{
			mapFolderName = ini.Get(
				"Init",
				mapFolderName,
				mapFolderName);
		}
	}
	return mapFolderName;
}

bool usesTrilogyNpcRuntime(ScriptNpcRuntimeProfile profile)
{
	return profile == ScriptNpcRuntimeProfile::Trilogy;
}

std::size_t getNamedNpcTargetCount(std::size_t availableTargetCount)
{
	return std::min<std::size_t>(availableTargetCount, 1);
}

std::vector<std::shared_ptr<NPC>> findNamedNonPlayerNpcs(
	GameManager* gameManager,
	const std::string& name)
{
	if (gameManager == nullptr || gameManager->npcManager == nullptr)
	{
		return {};
	}
	auto targets = gameManager->npcManager->findNPC(name);
	targets.erase(
		std::remove(targets.begin(), targets.end(), gameManager->player),
		targets.end());
	return targets;
}

std::shared_ptr<NPC> getScriptPlayerKindCharacter(GameManager* gameManager)
{
	if (gameManager == nullptr || gameManager->player == nullptr)
	{
		return nullptr;
	}
	std::shared_ptr<NPC> character = nullptr;
	if (gameManager->npcManager != nullptr)
	{
		character = gameManager->npcManager->findPlayerNPC();
	}
	if (character == nullptr)
	{
		character = gameManager->player->getControlledCharacter();
	}
	if (character == nullptr)
	{
		character = gameManager->player;
	}
	return character;
}

void finishScriptCharacterPositionChange(GameManager* gameManager)
{
	if (gameManager == nullptr || gameManager->player == nullptr)
	{
		return;
	}

	if (gameManager->npcManager != nullptr)
	{
		const Point playerPosition = gameManager->player->getPosition();
		gameManager->npcManager->setPartnerPos(
			playerPosition.x,
			playerPosition.y,
			gameManager->player->direction);
	}

	if (gameManager->camera != nullptr)
	{
		auto cameraTarget = getScriptPlayerKindCharacter(gameManager);
		gameManager->camera->followPlayer = true;
		if (cameraTarget != nullptr && cameraTarget != gameManager->player)
		{
			gameManager->camera->followNPC = cameraTarget;
		}
		else
		{
			gameManager->camera->followNPC.reset();
		}
		gameManager->camera->snapToFollowTarget();
		gameManager->camera->differencePosition = { 0.0, 0.0 };
	}

	gameManager->player->suppressTrapAtScriptPosition();
}

void setScriptPlayerKindCharacterNonFighting(GameManager* gameManager)
{
	auto character = getScriptPlayerKindCharacter(gameManager);
	if (character != nullptr)
	{
		character->fightState.set(false);
	}
}

class ScopedScriptInputBlock final
{
public:
	explicit ScopedScriptInputBlock(GameManager* gameManager)
		: gameManager(gameManager),
		previousCanInput(gameManager != nullptr
			? gameManager->global.data.canInput
			: false)
	{
		if (gameManager != nullptr)
		{
			gameManager->global.data.canInput = false;
		}
	}

	~ScopedScriptInputBlock()
	{
		if (gameManager != nullptr)
		{
			gameManager->global.data.canInput = previousCanInput;
		}
	}

private:
	GameManager* gameManager = nullptr;
	bool previousCanInput = false;
};

void runNpcSpecialActionForTarget(const std::shared_ptr<NPC>& npc,
	const std::string& fileName,
	bool blocking)
{
	if (npc == nullptr)
	{
		return;
	}

	if (blocking)
	{
		npc->doSpecialAction(fileName);
	}
	else
	{
		npc->startScriptSpecialAction(fileName);
	}
}

void runNpcSpecialActionForTargets(GameManager* gameManager,
	const std::string& name,
	const std::string& fileName,
	bool blocking)
{
	if (gameManager == nullptr)
	{
		return;
	}

	if (name.empty())
	{
		runNpcSpecialActionForTarget(gameManager->scriptNPC, fileName, blocking);
		return;
	}

	auto npcList = gameManager->npcManager->findNPC(name);
	if (!npcList.empty())
	{
		runNpcSpecialActionForTarget(npcList.front(), fileName, blocking);
	}
}

int keepCurrentWhenLegacyWildcard(int value, int currentValue)
{
	return value < 0 ? currentValue : value;
}

int distanceOrZeroWhenLegacyWildcard(int value)
{
	return value < 0 ? 0 : value;
}

Point keepCurrentPositionWhenLegacyWildcard(int x, int y, Point currentPosition)
{
	return {
		keepCurrentWhenLegacyWildcard(x, currentPosition.x),
		keepCurrentWhenLegacyWildcard(y, currentPosition.y)
	};
}

std::string toLowerAscii(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return value;
}

std::string trimAscii(std::string value)
{
	auto isSpace = [](unsigned char c) {
		return std::isspace(c) != 0;
	};
	auto first = std::find_if_not(value.begin(), value.end(), isSpace);
	auto last = std::find_if_not(value.rbegin(), value.rend(), isSpace).base();
	if (first >= last)
	{
		return "";
	}
	return std::string(first, last);
}

bool tryParseInteger(const std::string& value, int& result)
{
	errno = 0;
	char* end = nullptr;
	long parsed = std::strtol(value.c_str(), &end, 10);
	if (end == value.c_str() || *end != '\0' || errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX)
	{
		return false;
	}
	result = static_cast<int>(parsed);
	return true;
}

std::uint32_t fnv1a32(const std::string& value)
{
	std::uint32_t hash = 2166136261u;
	for (unsigned char c : value)
	{
		hash ^= c;
		hash *= 16777619u;
	}
	return hash;
}

int snapshotIndexFromKey(const std::string& key, int defaultValue = 0)
{
	std::string normalizedKey = trimAscii(key);
	std::string lowerKey = toLowerAscii(normalizedKey);
	if (normalizedKey.empty() || lowerKey == "default")
	{
		return defaultValue;
	}

	int parsedIndex = 0;
	if (tryParseInteger(normalizedKey, parsedIndex))
	{
		return parsedIndex;
	}

	constexpr int kStringKeyBase = 100000;
	constexpr int kStringKeyRange = 900000000;
	return kStringKeyBase + static_cast<int>(fnv1a32(normalizedKey) % kStringKeyRange);
}

void addIntegerVariableAlias(const std::string& name, int value)
{
	GameManager* manager = GameManager::getInstance();
	if (manager == nullptr || name.empty())
	{
		return;
	}
	manager->varList.ensureInitialized();
	int newValue = manager->varList.getInteger(name) + value;
	manager->varList.setInteger(name, newValue);

	std::string lowerName = toLowerAscii(name);
	if (lowerName != name)
	{
		manager->varList.setInteger(lowerName, newValue);
	}
}

int readBuySellCount(INIReader& ini)
{
	int count = ini.GetInteger("Header", "Count", -1);
	if (count < 0)
	{
		count = ini.GetInteger("Head", "Count", 0);
	}
	if (count < 0)
	{
		count = 0;
	}
	if (count > BUYSELL_GOODS_COUNT)
	{
		count = BUYSELL_GOODS_COUNT;
	}
	return count;
}

std::string stripChooseExConditions(const std::string& text, std::vector<std::string>& conditions)
{
	std::string cleanText;
	for (size_t i = 0; i < text.size(); i++)
	{
		if (text[i] == '{')
		{
			size_t end = text.find('}', i + 1);
			if (end != std::string::npos)
			{
				conditions.push_back(text.substr(i + 1, end - i - 1));
				i = end;
				continue;
			}
		}
		cleanText.push_back(text[i]);
	}
	return cleanText;
}

std::string normalizeChooseExVariableName(std::string value)
{
	value = trimAscii(value);
	if (!value.empty() && value.front() == '$')
	{
		value.erase(value.begin());
	}
	return value;
}

bool parseChooseExInteger(std::string value, int& result)
{
	value = trimAscii(value);
	return !value.empty() && tryParseInteger(value, result);
}

std::string normalizeMagicListName(const std::string& value)
{
	return toLowerAscii(trimAscii(value));
}

std::string getMagicNameFromListItem(std::string item)
{
	item = trimAscii(item);
	size_t colonPosition = item.find(':');
	if (colonPosition != std::string::npos)
	{
		item = item.substr(0, colonPosition);
	}
	return trimAscii(item);
}

bool flyInisContainsMagic(std::string flyInis, const std::string& magicName)
{
	std::string normalizedMagicName = normalizeMagicListName(magicName);
	if (normalizedMagicName.empty())
	{
		return true;
	}
	convert::replaceAllString(flyInis, "\xEF\xBC\x9A", ":");
	convert::replaceAllString(flyInis, "\xEF\xBC\x9B", ";");
	auto items = convert::splitString(flyInis, ";");
	for (const auto& item : items)
	{
		if (normalizeMagicListName(getMagicNameFromListItem(item)) == normalizedMagicName)
		{
			return true;
		}
	}
	return false;
}

bool npcHasConfiguredMagic(const std::shared_ptr<NPC>& npc, const std::string& magicName)
{
	if (npc == nullptr)
	{
		return false;
	}
	std::string normalizedMagicName = normalizeMagicListName(magicName);
	return normalizeMagicListName(npc->flyIni) == normalizedMagicName ||
		normalizeMagicListName(npc->flyIni2) == normalizedMagicName ||
		flyInisContainsMagic(npc->flyInis, magicName);
}

struct NpcAttributeAssignment
{
	std::string key;
	std::string value;
};

std::vector<NpcAttributeAssignment> parseNpcAttributeAssignments(const std::string& attributes)
{
	std::vector<NpcAttributeAssignment> assignments;
	size_t start = 0;
	while (start <= attributes.size())
	{
		size_t end = attributes.find(';', start);
		std::string item = attributes.substr(start, end == std::string::npos ? std::string::npos : end - start);
		item = trimAscii(item);
		if (!item.empty())
		{
			size_t colon = item.find(':');
			if (colon != std::string::npos)
			{
				std::string key = trimAscii(item.substr(0, colon));
				std::string value = trimAscii(item.substr(colon + 1));
				if (!key.empty())
				{
					assignments.push_back({ key, value });
				}
			}
		}
		if (end == std::string::npos)
		{
			break;
		}
		start = end + 1;
	}
	return assignments;
}

int clampNpcKindValue(int value, int maxValue)
{
	if (value < 0)
	{
		return 0;
	}
	if (maxValue > 0 && value > maxValue)
	{
		return maxValue;
	}
	return value;
}

int getChooseExOperandValue(GameManager* gameManager, const std::string& operand)
{
	int literalValue = 0;
	if (parseChooseExInteger(operand, literalValue))
	{
		return literalValue;
	}
	return gameManager->varList.getInteger(normalizeChooseExVariableName(operand));
}

bool evaluateChooseExCondition(GameManager* gameManager, const std::string& condition)
{
	std::string text = trimAscii(condition);
	if (text.empty())
	{
		return true;
	}

	const std::vector<std::string> operators = { "==", "!=", "<>", ">=", "<=", ">>", "<<", ">", "<" };
	for (const std::string& op : operators)
	{
		size_t pos = text.find(op);
		if (pos == std::string::npos)
		{
			continue;
		}

		int leftValue = getChooseExOperandValue(gameManager, text.substr(0, pos));
		int rightValue = getChooseExOperandValue(gameManager, text.substr(pos + op.size()));
		if (op == "==")
		{
			return leftValue == rightValue;
		}
		if (op == "!=" || op == "<>")
		{
			return leftValue != rightValue;
		}
		if (op == ">=")
		{
			return leftValue >= rightValue;
		}
		if (op == "<=")
		{
			return leftValue <= rightValue;
		}
		if (op == ">" || op == ">>")
		{
			return leftValue > rightValue;
		}
		if (op == "<" || op == "<<")
		{
			return leftValue < rightValue;
		}
	}

	return gameManager->varList.getInteger(normalizeChooseExVariableName(text)) != 0;
}

std::string resolveChoosePlusSpeakerName(GameManager* gameManager, const std::string& speakerName)
{
	std::string trimmedSpeakerName = trimAscii(speakerName);
	if (trimmedSpeakerName == "#name" && gameManager != nullptr && gameManager->player != nullptr && !gameManager->player->npcName.empty())
	{
		return gameManager->player->npcName;
	}
	return trimmedSpeakerName;
}

struct ScriptChooseOptions
{
	std::vector<std::string> options;
	std::vector<bool> visibleOptions;
};

ScriptChooseOptions buildScriptChooseOptions(GameManager* gameManager, const std::vector<std::string>& options)
{
	ScriptChooseOptions result;
	for (const std::string& option : options)
	{
		std::vector<std::string> conditions;
		std::string cleanOption = stripChooseExConditions(option, conditions);
		bool visibleOption = true;
		for (const std::string& condition : conditions)
		{
			if (!evaluateChooseExCondition(gameManager, condition))
			{
				visibleOption = false;
				break;
			}
		}

		result.options.push_back(cleanOption);
		result.visibleOptions.push_back(visibleOption);
	}
	return result;
}

std::string makeChooseMultipleResultVariableName(std::string varName, int index)
{
	varName = trimAscii(varName);
	return "$" + varName + std::to_string(index);
}

#if defined(JXQY_ENABLE_AUTOMATION_HOOKS)
std::string makeAutomationChooseMultipleSelectionVariableName(int index)
{
	return "__automation_choose_multiple_selection" + std::to_string(index);
}

bool isScriptChooseOptionVisible(const ScriptChooseOptions& chooseOptions, int optionIndex)
{
	if (optionIndex < 0 || static_cast<size_t>(optionIndex) >= chooseOptions.options.size())
	{
		return false;
	}
	if (chooseOptions.options[optionIndex].empty())
	{
		return false;
	}
	return static_cast<size_t>(optionIndex) >= chooseOptions.visibleOptions.size() || chooseOptions.visibleOptions[optionIndex];
}

int countVisibleScriptChooseOptions(const ScriptChooseOptions& chooseOptions)
{
	int visibleCount = 0;
	for (size_t i = 0; i < chooseOptions.options.size(); i++)
	{
		if (isScriptChooseOptionVisible(chooseOptions, static_cast<int>(i)))
		{
			visibleCount++;
		}
	}
	return visibleCount;
}

bool consumeChooseAutomation(GameManager* gameManager, const ScriptChooseOptions& chooseOptions, int& selection)
{
	selection = -1;
	if (gameManager == nullptr)
	{
		return false;
	}
	if (!gameManager->areAutomationHooksEnabled())
	{
		if (gameManager->varList.getInteger(
				"__automation_choose_enabled") > 0)
		{
			GameLog::write(
				"ScriptAPI: ignored unauthorized choose automation request\n");
		}
		return false;
	}
	if (gameManager->varList.getInteger(
			"__automation_choose_enabled") <= 0)
	{
		return false;
	}

	gameManager->varList.setInteger("__automation_choose_enabled", 0);
	gameManager->varList.setInteger("__automation_choose_consumed", 1);

	int requestedSelection = gameManager->varList.getInteger("__automation_choose_selection");
	bool complete = isScriptChooseOptionVisible(chooseOptions, requestedSelection);
	if (complete)
	{
		selection = requestedSelection;
	}

	gameManager->varList.setInteger("__automation_choose_visible_count", countVisibleScriptChooseOptions(chooseOptions));
	gameManager->varList.setInteger("__automation_choose_complete", complete ? 1 : 0);
	return true;
}

bool consumeChooseMultipleAutomation(GameManager* gameManager, const ScriptChooseOptions& chooseOptions, int selectionCount, std::vector<int>& selections)
{
	selections.clear();
	if (gameManager == nullptr || selectionCount <= 0)
	{
		return false;
	}
	if (!gameManager->areAutomationHooksEnabled())
	{
		if (gameManager->varList.getInteger(
				"__automation_choose_multiple_enabled") > 0)
		{
			GameLog::write(
				"ScriptAPI: ignored unauthorized multiple-choice automation request\n");
		}
		return false;
	}
	if (gameManager->varList.getInteger("__automation_choose_multiple_enabled") <= 0)
	{
		return false;
	}

	gameManager->varList.setInteger("__automation_choose_multiple_enabled", 0);
	gameManager->varList.setInteger("__automation_choose_multiple_consumed", 1);
	int configuredSelectionCount = gameManager->varList.getInteger("__automation_choose_multiple_count");
	if (configuredSelectionCount <= 0)
	{
		configuredSelectionCount = selectionCount;
	}

	for (int i = 0; i < configuredSelectionCount && static_cast<int>(selections.size()) < selectionCount; i++)
	{
		int optionIndex = gameManager->varList.getInteger(makeAutomationChooseMultipleSelectionVariableName(i));
		if (!isScriptChooseOptionVisible(chooseOptions, optionIndex))
		{
			continue;
		}
		if (std::find(selections.begin(), selections.end(), optionIndex) != selections.end())
		{
			continue;
		}
		selections.push_back(optionIndex);
	}

	gameManager->varList.setInteger("__automation_choose_multiple_valid_count", static_cast<int>(selections.size()));
	gameManager->varList.setInteger("__automation_choose_multiple_complete", static_cast<int>(selections.size()) >= selectionCount ? 1 : 0);
	return true;
}

bool consumeGambleAutomation(GameManager* gameManager, int cost, bool& result)
{
	result = false;
	if (gameManager == nullptr)
	{
		return false;
	}
	if (!gameManager->areAutomationHooksEnabled())
	{
		if (gameManager->varList.getInteger(
				"__automation_gamble_enabled") > 0)
		{
			GameLog::write(
				"ScriptAPI: ignored unauthorized gamble automation request\n");
		}
		return false;
	}
	if (gameManager->varList.getInteger(
			"__automation_gamble_enabled") <= 0)
	{
		return false;
	}

	gameManager->varList.setInteger("__automation_gamble_enabled", 0);
	gameManager->varList.setInteger("__automation_gamble_consumed", 1);

	int playerStake = cost;
	int currentBet = 0;
	int action = gameManager->varList.getInteger("__automation_gamble_action");
	if (action == 1)
	{
		playerStake = gameManager->varList.getInteger("__automation_gamble_player_stake");
		currentBet = std::max(0, gameManager->varList.getInteger("__automation_gamble_current_bet"));
		bool roundWin = gameManager->varList.getInteger("__automation_gamble_round_win") != 0;
		playerStake += roundWin ? currentBet : -currentBet;
	}

	int moneyDelta = playerStake + currentBet - cost;
	result = moneyDelta >= 0;
	if (gameManager->player != nullptr)
	{
		gameManager->player->money = std::max(0, gameManager->player->money + moneyDelta);
	}
	if (gameManager->menu != nullptr && gameManager->menu->goodsMenu != nullptr)
	{
		gameManager->menu->goodsMenu->updateMoney();
	}

	gameManager->varList.setInteger("__automation_gamble_win", result ? 1 : 0);
	gameManager->varList.setInteger("__automation_gamble_money_delta", moneyDelta);
	gameManager->varList.setInteger("__automation_gamble_player_stake_after", playerStake);
	gameManager->varList.setInteger("__automation_gamble_current_bet_after", currentBet);
	return true;
}
#else
bool consumeChooseAutomation(
	GameManager*, const ScriptChooseOptions&, int& selection)
{
	selection = -1;
	return false;
}

bool consumeChooseMultipleAutomation(
	GameManager*, const ScriptChooseOptions&, int, std::vector<int>& selections)
{
	selections.clear();
	return false;
}

bool consumeGambleAutomation(GameManager*, int, bool& result)
{
	result = false;
	return false;
}
#endif

bool updateNpcObjAttributes(const std::string& fileName,
	const std::string& sectionPrefix,
	const std::string& nameKey,
	const std::string& name,
	const std::vector<NpcAttributeAssignment>& assignments)
{
	if (fileName.empty() || assignments.empty())
	{
		return false;
	}

	std::unique_ptr<char[]> data;
	int len = 0;
	if (!SaveFileManager::ReadNpcObjFile(fileName, data, len))
	{
		return false;
	}

	INIReader ini(data);
	if (ini.ParseError() != 0)
	{
		return false;
	}

	int count = (int)ini.GetInteger("Head", "Count", 0);
	if (count < 0)
	{
		count = 0;
	}

	bool found = false;
	for (int i = 0; i < count; i++)
	{
		const std::string section = convert::formatString("%s%03d", sectionPrefix.c_str(), i);
		if (ini.Get(section, nameKey, "") == name)
		{
			for (const auto& assignment : assignments)
			{
				ini.Set(section, assignment.key, assignment.value);
			}
			found = true;
		}
	}

	if (!found)
	{
		return false;
	}

	if (!ini.saveToFile(SaveFileManager::CurrentPath() + fileName))
	{
		return false;
	}
	SaveFileManager::AppendFile(fileName);
	return true;
}

bool updateNpcObjScriptFile(const std::string& fileName,
	const std::string& sectionPrefix,
	const std::string& nameKey,
	const std::string& name,
	const std::string& scriptKey,
	const std::string& scriptFile)
{
	return updateNpcObjAttributes(fileName, sectionPrefix, nameKey, name, { { scriptKey, scriptFile } });
}

bool isCurrentNpcFile(GameManager* gameManager, const std::string& fileName)
{
	if (gameManager == nullptr)
	{
		return false;
	}
	return toLowerAscii(trimAscii(fileName)) == toLowerAscii(trimAscii(gameManager->global.data.npcName));
}

void resetLastScriptSoundState(GameManager* gameManager)
{
	if (gameManager == nullptr)
	{
		return;
	}
	gameManager->lastScriptSoundHasPosition = 0;
	gameManager->lastScriptSoundSourceType = 0;
	gameManager->lastScriptSoundMapX = 0;
	gameManager->lastScriptSoundMapY = 0;
	gameManager->lastScriptSoundOffsetX1000 = 0;
	gameManager->lastScriptSoundOffsetY1000 = 0;
}

std::shared_ptr<GameElement> findCurrentScriptSoundSource(GameManager* gameManager, int& sourceType)
{
	sourceType = 0;
	if (gameManager == nullptr)
	{
		return nullptr;
	}
	if ((gameManager->scriptType == stNPC || gameManager->scriptType == stNPCDeath)
		&& gameManager->scriptNPC != nullptr)
	{
		sourceType = 1;
		return gameManager->scriptNPC;
	}
	if (gameManager->scriptType == stObject && gameManager->scriptObj != nullptr)
	{
		sourceType = 2;
		return gameManager->scriptObj;
	}
	if (gameManager->scriptNPC != nullptr)
	{
		sourceType = 1;
		return gameManager->scriptNPC;
	}
	if (gameManager->scriptObj != nullptr)
	{
		sourceType = 2;
		return gameManager->scriptObj;
	}
	return nullptr;
}

bool tryGetScriptSoundPosition(GameManager* gameManager,
	const std::shared_ptr<GameElement>& source,
	float& soundX,
	float& soundY)
{
	if (gameManager == nullptr || gameManager->camera == nullptr || source == nullptr)
	{
		return false;
	}
	PointEx soundOffset = gameManager->camera->offset - source->offset;
	Point pos = Map::getTilePosition(source->position, gameManager->camera->position, { 0, 0 }, soundOffset);
	soundX = static_cast<float>(pos.x) * SOUND_FACTOR / static_cast<float>(TILE_WIDTH);
	soundY = static_cast<float>(pos.y) * SOUND_FACTOR / static_cast<float>(TILE_HEIGHT);
	return true;
}

void recordLastScriptSoundState(GameManager* gameManager,
	const std::shared_ptr<GameElement>& source,
	int sourceType,
	float soundX,
	float soundY)
{
	if (gameManager == nullptr)
	{
		return;
	}
	gameManager->lastScriptSoundHasPosition = source != nullptr ? 1 : 0;
	gameManager->lastScriptSoundSourceType = sourceType;
	gameManager->lastScriptSoundMapX = source != nullptr ? source->position.x : 0;
	gameManager->lastScriptSoundMapY = source != nullptr ? source->position.y : 0;
	gameManager->lastScriptSoundOffsetX1000 = static_cast<int>(std::lround(soundX * 1000.0f));
	gameManager->lastScriptSoundOffsetY1000 = static_cast<int>(std::lround(soundY * 1000.0f));
}

std::shared_ptr<Object> findObjectScriptTarget(GameManager* gameManager, const std::string& name)
{
	if (gameManager == nullptr)
	{
		return nullptr;
	}
	if (name.empty())
	{
		return gameManager->scriptObj;
	}
	if (gameManager->objectManager == nullptr)
	{
		return nullptr;
	}
	return gameManager->objectManager->findObj(name);
}

bool getObjectRuntimePropertyValue(GameManager* gameManager,
	const std::shared_ptr<Object>& object,
	const std::string& propertyName,
	int& value)
{
	const std::string lowerName = toLowerAscii(trimAscii(propertyName));
	if (lowerName == "exists")
	{
		value = (object != nullptr
			&& gameManager != nullptr
			&& gameManager->objectManager != nullptr
			&& gameManager->objectManager->findObj(object)) ? 1 : 0;
		return true;
	}
	if (lowerName == "filename" || lowerName == "hasfilename")
	{
		value = object != nullptr && !object->objectFile.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "isremoved")
	{
		value = (object == nullptr
			|| gameManager == nullptr
			|| gameManager->objectManager == nullptr
			|| !gameManager->objectManager->findObj(object)) ? 1 : 0;
		return true;
	}
	if (object == nullptr)
	{
		return false;
	}
	if (lowerName == "kind")
	{
		value = object->kind;
		return true;
	}
	if (lowerName == "type" || lowerName == "objecttype")
	{
		value = object->objectType;
		return true;
	}
	if (lowerName == "hasobjfilemovie" || lowerName == "hasobjectfilemovie")
	{
		value = object->objectFileMovie.empty() ? 0 : 1;
		return true;
	}
	if (lowerName == "hasobjanimationresource" || lowerName == "hasobjectanimationresource")
	{
		value = object->res.animationFile.empty() ? 0 : 1;
		return true;
	}
	if (lowerName == "objanimationloaded" || lowerName == "objectanimationloaded")
	{
		value = object->res.animation != nullptr ? 1 : 0;
		return true;
	}
	if (lowerName == "isobjanimationplaying" || lowerName == "isobjectanimationplaying")
	{
		value = isPickupObjectKind(object->kind)
			&& object->nowAction == oaPlaying
			&& object->res.animation != nullptr ? 1 : 0;
		return true;
	}
	if (lowerName == "hasobjresourceimage" || lowerName == "hasobjectresourceimage")
	{
		value = object->res.imageFile.empty() ? 0 : 1;
		return true;
	}
	if (lowerName == "hasobjresourceshade" || lowerName == "hasobjectresourceshade")
	{
		value = object->res.shadowFile.empty() ? 0 : 1;
		return true;
	}
	if (lowerName == "hasobjresourcesound" || lowerName == "hasobjectresourcesound")
	{
		value = object->res.soundFile.empty() ? 0 : 1;
		return true;
	}
	if (lowerName == "objresourceimageloaded" || lowerName == "objectresourceimageloaded")
	{
		value = object->res.image != nullptr ? 1 : 0;
		return true;
	}
	if (lowerName == "objresourceshadeloaded" || lowerName == "objectresourceshadeloaded")
	{
		value = object->res.shadow != nullptr ? 1 : 0;
		return true;
	}
	if (lowerName == "dir" || lowerName == "direction")
	{
		value = object->direction;
		return true;
	}
	if (lowerName == "damage")
	{
		value = object->damage;
		return true;
	}
	if (lowerName == "damageinterval" || lowerName == "trapdamageinterval")
	{
		value = static_cast<int>(object->damageInterval);
		return true;
	}
	if (lowerName == "trapdamagecycle")
	{
		if (object->damageInterval == 0)
		{
			value = -1;
		}
		else
		{
			value = static_cast<int>(getObjectTrapDamageCycle(object->getTrapDamageElapsedMilliseconds(), object->damageInterval));
		}
		return true;
	}
	if (lowerName == "lasttrapdamagecycle")
	{
		value = object->lastTrapDamageCycle == OBJECT_TRAP_DAMAGE_CYCLE_UNSET ? -1 : static_cast<int>(object->lastTrapDamageCycle);
		return true;
	}
	if (lowerName == "frame")
	{
		value = object->frame;
		return true;
	}
	if (lowerName == "height")
	{
		value = object->height;
		return true;
	}
	if (lowerName == "lum")
	{
		value = object->lum;
		return true;
	}
	if (lowerName == "mapx")
	{
		value = object->getPosition().x;
		return true;
	}
	if (lowerName == "mapy")
	{
		value = object->getPosition().y;
		return true;
	}
	if (lowerName == "regioninworld" || lowerName == "regininworldbeginposition")
	{
		value = 1;
		return true;
	}
	if (lowerName == "regioninworldx" || lowerName == "regininworldbeginpositionx")
	{
		value = object->getPosition().x;
		return true;
	}
	if (lowerName == "regioninworldy" || lowerName == "regininworldbeginpositiony")
	{
		value = object->getPosition().y;
		return true;
	}
	if (lowerName == "offx" || lowerName == "offsetx")
	{
		value = static_cast<int>(round(object->getOffset().x));
		return true;
	}
	if (lowerName == "offy" || lowerName == "offsety")
	{
		value = static_cast<int>(round(object->getOffset().y));
		return true;
	}
	if (lowerName == "state" || lowerName == "nowaction" || lowerName == "action")
	{
		value = object->nowAction;
		return true;
	}
	if (lowerName == "caninteractdirectly")
	{
		value = object->canInteractDirectly;
		return true;
	}
	if (lowerName == "scriptfilejusttouch")
	{
		value = object->scriptFileJustTouch;
		return true;
	}
	if (lowerName == "timerscriptinterval")
	{
		value = static_cast<int>(object->timerScriptInterval);
		return true;
	}
	if (lowerName == "timerscriptelapsed")
	{
		value = static_cast<int>(object->timerScriptElapsed);
		return true;
	}
	if (lowerName == "millisecondstoremove")
	{
		value = static_cast<int>(object->millisecondsToRemove);
		return true;
	}
	if (lowerName == "isobstacle")
	{
		value = isObjectObstacleKind(object->kind) ? 1 : 0;
		return true;
	}
	if (lowerName == "isdrop")
	{
		value = isPickupObjectKind(object->kind) ? 1 : 0;
		return true;
	}
	if (lowerName == "isautoplay")
	{
		value = isObjectAutoPlayKind(object->kind) ? 1 : 0;
		return true;
	}
	if (lowerName == "isinteractive")
	{
		value = !object->scriptFile.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "hasinteractscript")
	{
		value = !object->scriptFile.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "hasinteractscriptright")
	{
		value = !object->scriptFileRight.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "hasanyinteractscript")
	{
		value = object->hasAnyInteractScript() ? 1 : 0;
		return true;
	}
	if (lowerName == "hasprimaryinteractscript" || lowerName == "haseffectiveinteractscript")
	{
		value = object->getScriptFile(object->shouldUseRightScriptForPrimaryInteraction()).empty() ? 0 : 1;
		return true;
	}
	if (lowerName == "canselectforinteraction")
	{
		value = object->canSelectForInteraction() ? 1 : 0;
		return true;
	}
	if (lowerName == "shoulduserightscriptforprimaryinteraction"
		|| lowerName == "primaryusesrightscript"
		|| lowerName == "fallbackrightscript")
	{
		value = object->shouldUseRightScriptForPrimaryInteraction() ? 1 : 0;
		return true;
	}
	if (lowerName == "istrap")
	{
		value = object->kind == okTrap ? 1 : 0;
		return true;
	}
	if (lowerName == "isbody")
	{
		value = object->kind == okBody ? 1 : 0;
		return true;
	}
	if (lowerName == "isbox")
	{
		value = object->kind == okBox ? 1 : 0;
		return true;
	}
	if (lowerName == "isdoor")
	{
		value = object->kind == okDoor ? 1 : 0;
		return true;
	}
	if (lowerName == "selecting" || lowerName == "isselected")
	{
		value = object->selecting ? 1 : 0;
		return true;
	}
	return false;
}

bool setNpcRuntimeAttribute(GameManager* gameManager, const std::shared_ptr<NPC>& npc, const std::string& key, const std::string& value)
{
	if (gameManager == nullptr || npc == nullptr)
	{
		return false;
	}

	const std::string lowerKey = toLowerAscii(trimAscii(key));
	int integerValue = 0;
	auto readInteger = [&]() {
		return parseChooseExInteger(value, integerValue);
	};

	if (lowerKey == "name")
	{
		npc->npcName = value;
		if (npc->name != "player")
		{
			npc->name = "npc-" + value;
		}
		return true;
	}
	if (lowerKey == "kind" && readInteger())
	{
		npc->kind = integerValue;
		npc->beginStand();
		return true;
	}
	if (lowerKey == "npcini")
	{
		npc->npcIni = value;
		npc->initRes(value);
		return true;
	}
	if (lowerKey == "dir" && readInteger())
	{
		npc->direction = integerValue;
		return true;
	}
	if (lowerKey == "mapx" && readInteger())
	{
		Point position = npc->getPosition();
		position.x = integerValue;
		npc->setPosition(position);
		return true;
	}
	if (lowerKey == "mapy" && readInteger())
	{
		Point position = npc->getPosition();
		position.y = integerValue;
		npc->setPosition(position);
		return true;
	}
	if ((lowerKey == "action" || lowerKey == "strollintent") && readInteger())
	{
		npc->strollIntent = integerValue;
		return true;
	}
	if ((lowerKey == "ai_type" || lowerKey == "aitype") && readInteger())
	{
		npc->aiType = integerValue;
		return true;
	}
	if (lowerKey == "group" && readInteger())
	{
		npc->group = integerValue;
		return true;
	}
	if (lowerKey == "noautoattackplayer" && readInteger())
	{
		npc->noAutoAttackPlayer = integerValue;
		return true;
	}
	if (lowerKey == "stopfindingtarget" && readInteger())
	{
		npc->stopFindingTarget = integerValue;
		return true;
	}
	if (lowerKey == "walkspeed" && readInteger())
	{
		npc->walkSpeed = integerValue <= 0 ? 1 : integerValue;
		return true;
	}
	if (lowerKey == "standspeed" && readInteger())
	{
		npc->standSpeed = integerValue;
		return true;
	}
	if (lowerKey == "attackspeed" && readInteger())
	{
		npc->attackSpeed = integerValue <= 0 ? 1 : integerValue;
		npc->hasAttackSpeedField = true;
		return true;
	}
	if (lowerKey == "runspeed" && readInteger())
	{
		npc->runSpeed = integerValue <= 0 ? 1 : integerValue;
		return true;
	}
	if (lowerKey == "pathfinder" && readInteger())
	{
		npc->pathFinder = integerValue;
		return true;
	}
	if ((lowerKey == "isaidisabled" || lowerKey == "aidisabled" || lowerKey == "islocalaidisabled" || lowerKey == "npcaidisabled") && readInteger())
	{
		npc->setAIDisabled(integerValue != 0);
		return true;
	}
	if ((lowerKey == "aienabled" || lowerKey == "npcaienabled") && readInteger())
	{
		npc->setAIDisabled(integerValue == 0);
		return true;
	}
	if (lowerKey == "dialogradius" && readInteger())
	{
		npc->dialogRadius = integerValue;
		return true;
	}
	if (lowerKey == "timerscriptinterval" && readInteger())
	{
		npc->timerScriptInterval = integerValue <= 0
			? 0
			: static_cast<UTime>(integerValue);
		return true;
	}
	if (lowerKey == "scriptfile")
	{
		npc->scriptFile = value;
		return true;
	}
	if (lowerKey == "deathscript")
	{
		npc->deathScript = value;
		return true;
	}
	if (lowerKey == "revivemilliseconds" && readInteger())
	{
		npc->reviveMilliseconds = integerValue <= 0 ? 0 : static_cast<UTime>(integerValue);
		return true;
	}
	if (lowerKey == "leftmillisecondstorevive" && readInteger())
	{
		npc->leftMillisecondsToRevive = integerValue <= 0 ? 0 : static_cast<UTime>(integerValue);
		return true;
	}
	if (lowerKey == "lifemilliseconds" && readInteger())
	{
		npc->lifeMilliseconds = integerValue <= 0 ? 0 : static_cast<UTime>(integerValue);
		return true;
	}
	if ((lowerKey == "disablemovemilliseconds" || lowerKey == "movedisabledmilliseconds") && readInteger())
	{
		npc->disableMoveMilliseconds = integerValue <= 0 ? 0 : static_cast<UTime>(integerValue);
		return true;
	}
	if ((lowerKey == "disableskillmilliseconds" || lowerKey == "disablefightmilliseconds" || lowerKey == "disablemagicmilliseconds") && readInteger())
	{
		npc->disableSkillMilliseconds = integerValue <= 0 ? 0 : static_cast<UTime>(integerValue);
		return true;
	}
	if ((lowerKey == "blindmilliseconds" || lowerKey == "blindleftmilliseconds") && readInteger())
	{
		npc->blindMilliseconds = integerValue <= 0 ? 0 : static_cast<UTime>(integerValue);
		if (npc->blindMilliseconds > 0)
		{
			npc->clearCombatTargetMemory();
		}
		return true;
	}
	if ((lowerKey == "frozenmilliseconds" || lowerKey == "frozenleftmilliseconds" || lowerKey == "frozenlasttime") && readInteger())
	{
		npc->frozenLastTime = integerValue <= 0 ? 0 : static_cast<UTime>(integerValue);
		npc->frozen = npc->frozenLastTime > 0;
		return true;
	}
	if (lowerKey == "frozenseconds" && readInteger())
	{
		npc->frozenLastTime = integerValue <= 0 ? 0 : static_cast<UTime>(integerValue) * 1000;
		npc->frozen = npc->frozenLastTime > 0;
		return true;
	}
	if ((lowerKey == "frozenvisualeffect" || lowerKey == "isfrozenvisualeffect" || lowerKey == "isfronzenvisualeffect") && readInteger())
	{
		npc->frozenVisualEffect = integerValue != 0;
		return true;
	}
	if ((lowerKey == "immobilizedmilliseconds" || lowerKey == "immobilizedleftmilliseconds" || lowerKey == "immobilizedlasttime") && readInteger())
	{
		npc->immobilizedLastTime = integerValue <= 0 ? 0 : static_cast<UTime>(integerValue);
		npc->immobilized = npc->immobilizedLastTime > 0;
		return true;
	}
	if (lowerKey == "immobilizedseconds" && readInteger())
	{
		npc->immobilizedLastTime = integerValue <= 0 ? 0 : static_cast<UTime>(integerValue) * 1000;
		npc->immobilized = npc->immobilizedLastTime > 0;
		return true;
	}
	if ((lowerKey == "immobilizedvisualeffect" || lowerKey == "isimmobilizedvisualeffect") && readInteger())
	{
		npc->immobilizedVisualEffect = integerValue != 0;
		return true;
	}
	if ((lowerKey == "poisonedmilliseconds" || lowerKey == "poisonmilliseconds" || lowerKey == "poisonedleftmilliseconds" || lowerKey == "poisonedlasttime") && readInteger())
	{
		npc->poisonedLastTime = integerValue <= 0 ? 0 : static_cast<UTime>(integerValue);
		npc->poisoned = npc->poisonedLastTime > 0;
		npc->poisonedDamageTimer = 0;
		return true;
	}
	if ((lowerKey == "poisonseconds" || lowerKey == "poisonedseconds") && readInteger())
	{
		npc->poisonedLastTime = integerValue <= 0 ? 0 : static_cast<UTime>(integerValue) * 1000;
		npc->poisoned = npc->poisonedLastTime > 0;
		npc->poisonedDamageTimer = 0;
		return true;
	}
	if ((lowerKey == "poisonedvisualeffect" || lowerKey == "ispoisonedvisualeffect" || lowerKey == "ispoisionvisualeffect" || lowerKey == "ispoisonvisualeffect") && readInteger())
	{
		npc->poisonedVisualEffect = integerValue != 0;
		return true;
	}
	if ((lowerKey == "petrifiedmilliseconds" || lowerKey == "petrifymilliseconds" || lowerKey == "petrifiedleftmilliseconds" || lowerKey == "petrifiedlasttime") && readInteger())
	{
		npc->petrifiedLastTime = integerValue <= 0 ? 0 : static_cast<UTime>(integerValue);
		npc->petrified = npc->petrifiedLastTime > 0;
		return true;
	}
	if ((lowerKey == "petrifiedseconds" || lowerKey == "petrifyseconds") && readInteger())
	{
		npc->petrifiedLastTime = integerValue <= 0 ? 0 : static_cast<UTime>(integerValue) * 1000;
		npc->petrified = npc->petrifiedLastTime > 0;
		return true;
	}
	if ((lowerKey == "petrifiedvisualeffect" || lowerKey == "ispetrifiedvisualeffect") && readInteger())
	{
		npc->petrifiedVisualEffect = integerValue != 0;
		return true;
	}
	if (lowerKey == "isbodyiniadded" && readInteger())
	{
		npc->isBodyIniAdded = integerValue;
		return true;
	}
	if ((lowerKey == "isnodaddbody" || lowerKey == "noaddbody") && readInteger())
	{
		npc->noAddBody = integerValue != 0;
		return true;
	}
	if (lowerKey == "caninteractdirectly" && readInteger())
	{
		npc->canInteractDirectly = integerValue;
		return true;
	}
	if (lowerKey == "state" && readInteger())
	{
		npc->state = integerValue;
		return true;
	}
	if (lowerKey == "relation" && readInteger())
	{
		npc->relation = integerValue;
		npc->beginStand();
		return true;
	}
	if (lowerKey == "kindvalue" && readInteger())
	{
		npc->kindValue = clampNpcKindValue(integerValue, npc->kindValueMax);
		return true;
	}
	if (lowerKey == "kindvaluemax" && readInteger())
	{
		npc->kindValueMax = integerValue;
		npc->kindValue = clampNpcKindValue(npc->kindValue, npc->kindValueMax);
		return true;
	}
	if (lowerKey == "talkcontent")
	{
		npc->talkContent = value;
		return true;
	}
	if (lowerKey == "issignalshow" && readInteger())
	{
		npc->isSignalShow = integerValue != 0;
		return true;
	}
	if (lowerKey == "signalindex" && readInteger())
	{
		npc->signalIndex = integerValue;
		npc->resetSignalImage();
		return true;
	}
	if (lowerKey == "signaltype")
	{
		npc->signalType = value;
		return true;
	}
	if (lowerKey == "life" && readInteger())
	{
		npc->life = std::clamp(integerValue, 0, std::max(0, npc->getLifeMax()));
		return true;
	}
	if (lowerKey == "lifemax" && readInteger())
	{
		npc->lifeMax = integerValue;
		if (npc->life > npc->getLifeMax())
		{
			npc->life = npc->getLifeMax();
		}
		return true;
	}
	if (lowerKey == "thew" && readInteger())
	{
		npc->thew = std::clamp(integerValue, 0, std::max(0, npc->getThewMax()));
		return true;
	}
	if (lowerKey == "thewmax" && readInteger())
	{
		npc->thewMax = integerValue;
		if (npc->thew > npc->getThewMax())
		{
			npc->thew = npc->getThewMax();
		}
		return true;
	}
	if (lowerKey == "mana" && readInteger())
	{
		npc->mana = std::clamp(integerValue, 0, std::max(0, npc->getManaMax()));
		return true;
	}
	if (lowerKey == "manamax" && readInteger())
	{
		npc->manaMax = integerValue;
		if (npc->mana > npc->getManaMax())
		{
			npc->mana = npc->getManaMax();
		}
		return true;
	}
	if (lowerKey == "attack" && readInteger())
	{
		npc->attack = integerValue;
		return true;
	}
	if (lowerKey == "attack2" && readInteger())
	{
		npc->attack2 = integerValue;
		return true;
	}
	if (lowerKey == "attack3" && readInteger())
	{
		npc->attack3 = integerValue;
		return true;
	}
	if ((lowerKey == "defend" || lowerKey == "defence") && readInteger())
	{
		npc->defend = integerValue;
		return true;
	}
	if ((lowerKey == "defend2" || lowerKey == "defence2") && readInteger())
	{
		npc->defend2 = integerValue;
		return true;
	}
	if ((lowerKey == "defend3" || lowerKey == "defence3") && readInteger())
	{
		npc->defend3 = integerValue;
		return true;
	}
	if (lowerKey == "evade" && readInteger())
	{
		npc->evade = integerValue;
		return true;
	}
	if (lowerKey == "duck" && readInteger())
	{
		npc->duck = integerValue;
		return true;
	}
	if (lowerKey == "exp" && readInteger())
	{
		npc->exp = integerValue;
		return true;
	}
	if (lowerKey == "expbonus" && readInteger())
	{
		npc->expBonus = integerValue;
		return true;
	}
	if (lowerKey == "steal" && readInteger())
	{
		npc->steal = integerValue;
		return true;
	}
	if (lowerKey == "eloquence" && readInteger())
	{
		npc->eloquence = integerValue;
		return true;
	}
	if (lowerKey == "leechcraft" && readInteger())
	{
		npc->leechcraft = integerValue;
		return true;
	}
	if (lowerKey == "levelupexp" && readInteger())
	{
		npc->levelUpExp = integerValue;
		return true;
	}
	if (lowerKey == "canlevelup" && readInteger())
	{
		npc->canLevelUp = integerValue;
		return true;
	}
	if (lowerKey == "canequip" && readInteger())
	{
		npc->canEquip = integerValue;
		return true;
	}
	if (lowerKey == "level" && readInteger())
	{
		npc->level = integerValue;
		npc->setLevel(integerValue);
		return true;
	}
	if (lowerKey == "attacklevel" && readInteger())
	{
		npc->attackLevel = integerValue;
		npc->rebuildAttackOptions();
		return true;
	}
	if (lowerKey == "magiclevel" && readInteger())
	{
		npc->magicLevel = integerValue;
		npc->rebuildAttackOptions();
		return true;
	}
	if (lowerKey == "lum" && readInteger())
	{
		npc->lum = integerValue;
		return true;
	}
	if (lowerKey == "visionradius" && readInteger())
	{
		npc->visionRadius = integerValue;
		return true;
	}
	if (lowerKey == "attackradius" && readInteger())
	{
		npc->attackRadius = integerValue;
		return true;
	}
	if (lowerKey == "bodyini")
	{
		npc->bodyIni = value;
		return true;
	}
	if (lowerKey == "flyini")
	{
		npc->flyIni = value;
		npc->npcMagic = gameManager->magicManager.loadAttackMagic(value);
		npc->rebuildAttackOptions();
		return true;
	}
	if (lowerKey == "flyini2")
	{
		npc->flyIni2 = value;
		npc->npcMagic2 = gameManager->magicManager.loadAttackMagic(value);
		npc->rebuildAttackOptions();
		return true;
	}
	if (lowerKey == "flyinis")
	{
		npc->flyInis = value;
		npc->rebuildAttackOptions();
		return true;
	}
	if (lowerKey == "magicini")
	{
		npc->magicIni = value;
		npc->rebuildAttackOptions();
		return true;
	}
	if (lowerKey == "dropini")
	{
		npc->dropIni = value;
		return true;
	}
	if (lowerKey == "magictousewhenlifelow")
	{
		npc->magicToUseWhenLifeLowFile = value;
		npc->magicToUseWhenLifeLow = gameManager->magicManager.loadAttackMagic(value);
		return true;
	}
	if (lowerKey == "lifelowpercent" && readInteger())
	{
		npc->lifeLowPercent = integerValue;
		return true;
	}
	if (lowerKey == "keepradiuswhenlifelow" && readInteger())
	{
		npc->keepRadiusWhenLifeLow = integerValue;
		return true;
	}
	if (lowerKey == "keepradiuswhenfrienddeath" && readInteger())
	{
		npc->keepRadiusWhenFriendDeath = integerValue;
		return true;
	}
	if (lowerKey == "magictousewhenbeattacked")
	{
		npc->magicToUseWhenBeAttackedFile = value;
		npc->magicToUseWhenBeAttacked = gameManager->magicManager.loadAttackMagic(value);
		return true;
	}
	if (lowerKey == "magicdirectionwhenbeattacked" && readInteger())
	{
		npc->magicDirectionWhenBeAttacked = integerValue;
		return true;
	}
	if (lowerKey == "magictousewhendeath")
	{
		npc->magicToUseWhenDeathFile = value;
		npc->magicToUseWhenDeath = gameManager->magicManager.loadAttackMagic(value);
		return true;
	}
	if (lowerKey == "magicdirectionwhendeath" && readInteger())
	{
		npc->magicDirectionWhenDeath = integerValue;
		return true;
	}
	if (lowerKey == "hurtplayerinterval" && readInteger())
	{
		npc->hurtPlayerInterval = integerValue <= 0 ? 0 : static_cast<UTime>(integerValue);
		return true;
	}
	if (lowerKey == "hurtplayerlife" && readInteger())
	{
		npc->hurtPlayerLife = integerValue;
		return true;
	}
	if (lowerKey == "hurtplayerradius" && readInteger())
	{
		npc->hurtPlayerRadius = integerValue < 0 ? 0 : integerValue;
		return true;
	}
	if (lowerKey == "nodropwhendie" && readInteger())
	{
		npc->noDropWhenDie = integerValue;
		return true;
	}
	if (lowerKey == "idle" && readInteger())
	{
		npc->idle = integerValue;
		return true;
	}
	if (lowerKey == "invincible" && readInteger())
	{
		npc->invincible = integerValue;
		return true;
	}
	if (lowerKey == "currentfixedposindex" && readInteger())
	{
		npc->currentFixedPosIndex = integerValue <= 0
			? 0
			: static_cast<std::size_t>(integerValue);
		return true;
	}
	if (lowerKey == "visiblevariablevalue" && readInteger())
	{
		npc->visibleVariableValue = integerValue;
		return true;
	}
	if (lowerKey == "destinationmapposx" && readInteger())
	{
		npc->destinationMapPosition.x = integerValue;
		return true;
	}
	if (lowerKey == "destinationmapposy" && readInteger())
	{
		npc->destinationMapPosition.y = integerValue;
		return true;
	}
	if (lowerKey == "addmovespeedpercent" && readInteger())
	{
		npc->addMoveSpeedPercent = integerValue;
		return true;
	}
	if (lowerKey == "keepattackx" && readInteger())
	{
		npc->keepAttackPosition.x = integerValue;
		return true;
	}
	if (lowerKey == "keepattacky" && readInteger())
	{
		npc->keepAttackPosition.y = integerValue;
		return true;
	}
	return false;
}

void applyNpcRuntimeAttributes(GameManager* gameManager,
	const std::string& name,
	const std::vector<NpcAttributeAssignment>& assignments)
{
	if (gameManager == nullptr || gameManager->npcManager == nullptr || assignments.empty())
	{
		return;
	}
	auto npcList = gameManager->npcManager->findNPC(name);
	for (const auto& npc : npcList)
	{
		for (const auto& assignment : assignments)
		{
			setNpcRuntimeAttribute(gameManager, npc, assignment.key, assignment.value);
		}
	}
}

std::vector<std::shared_ptr<NPC>> findNPCForStateReadback(GameManager* gameManager, const std::string& name)
{
	std::vector<std::shared_ptr<NPC>> result;
	if (gameManager == nullptr || gameManager->npcManager == nullptr)
	{
		return result;
	}
	if (gameManager->player != nullptr && gameManager->player->npcName == name)
	{
		result.push_back(gameManager->player);
	}
	for (const auto& npc : gameManager->npcManager->npcList)
	{
		if (npc != nullptr && npc->npcName == name)
		{
			result.push_back(npc);
		}
	}
	return result;
}

bool getNpcRuntimePropertyValue(const std::shared_ptr<NPC>& npc, const std::string& propertyName, int& value);

int sumMagicEffectBonusPercent(const std::unordered_map<std::string, NPCMagicEffectBonus>& bonuses)
{
	int percent = 0;
	for (const auto& item : bonuses)
	{
		percent += item.second.percent;
	}
	return percent;
}

int sumMagicEffectBonusAmount(const std::unordered_map<std::string, NPCMagicEffectBonus>& bonuses)
{
	int amount = 0;
	for (const auto& item : bonuses)
	{
		amount += item.second.amount;
	}
	return amount;
}

int countLiveSummonedNpcs(const std::shared_ptr<NPC>& npc)
{
	if (npc == nullptr)
	{
		return 0;
	}

	int count = 0;
	for (const auto& item : npc->summonedNPCsByMagic)
	{
		for (const auto& weakSummonedNpc : item.second)
		{
			auto summonedNpc = weakSummonedNpc.lock();
			if (summonedNpc != nullptr && !summonedNpc->isDying() && !summonedNpc->isHiding())
			{
				count++;
			}
		}
	}
	return count;
}

NPCEquipmentAttributes collectPlayerEquipmentAttributes()
{
	NPCEquipmentAttributes attributes;
	if (gm == nullptr)
	{
		return attributes;
	}

	auto addGoodsAttributes = [&](std::shared_ptr<Goods> goods, int count)
	{
		if (goods == nullptr || goods->kind != gkEquipment || count <= 0)
		{
			return;
		}
		for (int i = 0; i < count; i++)
		{
			attributes.lifeMax += goods->lifeMax;
			attributes.thewMax += goods->thewMax;
			attributes.manaMax += goods->manaMax;
			attributes.attack += goods->attack;
			attributes.attack2 += goods->attack2;
			attributes.attack3 += goods->attack3;
			attributes.defend += goods->defend;
			attributes.defend2 += goods->defend2;
			attributes.defend3 += goods->defend3;
			attributes.evade += goods->evade;
		}
	};

	for (int listIndex = 0; listIndex < gm->goodsManager.listLength(); listIndex++)
	{
		if (!gm->goodsManager.goodsListExists(listIndex))
		{
			continue;
		}
		auto& goodsInfo = gm->goodsManager.goodsList[listIndex];
		if (gm->goodsManager.isEquipIndex(listIndex))
		{
			addGoodsAttributes(goodsInfo.goods, 1);
		}
		else if (goodsInfo.goods != nullptr && goodsInfo.goods->kind == gkEquipment && goodsInfo.goods->noNeedToEquip > 0)
		{
			addGoodsAttributes(goodsInfo.goods, goodsInfo.number);
		}
	}
	return attributes;
}

NPCEquipmentAttributes getRuntimeEquipmentAttributes(const std::shared_ptr<NPC>& npc)
{
	if (gm != nullptr && gm->player == npc)
	{
		return collectPlayerEquipmentAttributes();
	}
	if (npc != nullptr)
	{
		return npc->equipmentAttributes;
	}
	return NPCEquipmentAttributes();
}

std::shared_ptr<NPC> lockNpcTarget(const std::weak_ptr<GameElement>& target)
{
	return std::dynamic_pointer_cast<NPC>(target.lock());
}

std::shared_ptr<NPC> findScriptMagicTarget(Point destination)
{
	if (gm == nullptr || gm->npcManager == nullptr)
	{
		return nullptr;
	}

	for (const auto& npc : gm->npcManager->findNPC(destination, 1))
	{
		if (npc != nullptr && npc->getPosition() == destination && npc->isVisibleForRuntime())
		{
			return npc;
		}
	}
	return nullptr;
}

bool shouldResolveScriptMagicTarget(const std::shared_ptr<Magic>& magic, int level)
{
	if (magic == nullptr || level < 1 || level > MAGIC_MAX_LEVEL)
	{
		return false;
	}
	return magic->level[level].moveKind == mmkControl;
}

int getRuntimeRelationForReadback(const std::shared_ptr<NPC>& npc)
{
	if (npc == nullptr)
	{
		return nrFriendly;
	}
	if (gm != nullptr && gm->player != nullptr)
	{
		if (npc == gm->player)
		{
			return nrFriendly;
		}
		auto controlled = gm->player->getControlledCharacter();
		if (controlled != nullptr && controlled == npc && npc->relation == nrHostile)
		{
			return nrFriendly;
		}
	}
	return npc->relation;
}

bool hasPendingDeathScriptForNpc(const std::shared_ptr<NPC>& npc)
{
	if (npc == nullptr || gm == nullptr)
	{
		return false;
	}
	for (const auto& eventInfo : gm->eventList)
	{
		if (eventInfo.npc == npc && !eventInfo.scriptName.empty())
		{
			return true;
		}
	}
	return false;
}

int getNpcStateValue(const std::shared_ptr<NPC>& npc, const std::string& stateName)
{
	if (npc == nullptr)
	{
		return 0;
	}
	const std::string lowerName = toLowerAscii(trimAscii(stateName));
	if (lowerName == "kindvalue")
	{
		return npc->kindValue;
	}
	if (lowerName == "kindvaluemax")
	{
		return npc->kindValueMax;
	}
	if (lowerName == "attack")
	{
		return npc->getAttack();
	}
	if (lowerName == "defend" || lowerName == "defence")
	{
		return npc->getDefend();
	}
	if (lowerName == "evade")
	{
		return npc->getEvade();
	}
	if (lowerName == "life")
	{
		return npc->life;
	}
	if (lowerName == "lifemax")
	{
		return npc->getLifeMax();
	}
	if (lowerName == "thew")
	{
		return npc->thew;
	}
	if (lowerName == "thewmax")
	{
		return npc->getThewMax();
	}
	if (lowerName == "mana")
	{
		return npc->mana;
	}
	if (lowerName == "manamax")
	{
		return npc->getManaMax();
	}
	int propertyValue = 0;
	if (getNpcRuntimePropertyValue(npc, stateName, propertyValue))
	{
		return propertyValue;
	}
	return 0;
}

bool getNpcRuntimePropertyValue(const std::shared_ptr<NPC>& npc, const std::string& propertyName, int& value)
{
	if (npc == nullptr)
	{
		return false;
	}
	const std::string lowerName = toLowerAscii(trimAscii(propertyName));
	if (lowerName == "exists")
	{
		value = 1;
		return true;
	}
	if (lowerName == "kind")
	{
		value = npc->kind;
		return true;
	}
	if (lowerName == "relation")
	{
		value = npc->relation;
		return true;
	}
	if (lowerName == "runtimerelation" || lowerName == "effectiverelation")
	{
		value = getRuntimeRelationForReadback(npc);
		return true;
	}
	if (lowerName == "dir" || lowerName == "direction")
	{
		value = npc->direction;
		return true;
	}
	if (lowerName == "isenemy")
	{
		value = npc->isEnemy() ? 1 : 0;
		return true;
	}
	if (lowerName == "isnonefighter")
	{
		value = npc->isNoneFighter() ? 1 : 0;
		return true;
	}
	if (lowerName == "isfriend")
	{
		value = npc->relation == nrFriendly ? 1 : 0;
		return true;
	}
	if (lowerName == "isrelationneutral")
	{
		value = npc->relation == nrNeutral ? 1 : 0;
		return true;
	}
	if (lowerName == "isfighterfriend")
	{
		value = npc->isFighterFriend() ? 1 : 0;
		return true;
	}
	if (lowerName == "isruntimefighterfriend" || lowerName == "iseffectivefighterfriend")
	{
		int runtimeRelation = getRuntimeRelationForReadback(npc);
		value = NPC::isFighterFriendKindRelation(npc->kind, runtimeRelation) ? 1 : 0;
		return true;
	}
	if (lowerName == "isfighter")
	{
		value = npc->isFighterLike() ? 1 : 0;
		return true;
	}
	if (lowerName == "isfighterkind")
	{
		value = npc->kind == nkBattle ? 1 : 0;
		return true;
	}
	if (lowerName == "ispartner")
	{
		value = npc->kind == nkPartner ? 1 : 0;
		return true;
	}
	if (lowerName == "isplayer")
	{
		value = NPC::isPlayerKind(npc->kind) ? 1 : 0;
		return true;
	}
	if (lowerName == "iseventcharacter")
	{
		value = npc->kind == nkEvent ? 1 : 0;
		return true;
	}
	if (lowerName == "ismagicfromcache" || lowerName == "magicfromcache")
	{
		value = NPC::isPlayerKind(npc->kind) ? 0 : 1;
		return true;
	}
	if (lowerName == "hasshowname")
	{
		value = !npc->showName.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "displaynameusesshowname" || lowerName == "isshownameused")
	{
		value = (!npc->showName.empty() && npc->getDisplayName() == npc->showName) ? 1 : 0;
		return true;
	}
	if (lowerName == "displaynameusesname" || lowerName == "displaynameusesnpcname")
	{
		value = (npc->showName.empty() && npc->getDisplayName() == npc->npcName) ? 1 : 0;
		return true;
	}
	if (lowerName == "hasinteractscript")
	{
		value = !npc->scriptFile.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "hasinteractscriptright")
	{
		value = !npc->scriptFileRight.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "hasbuyinifile")
	{
		value = !npc->buyIniFile.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "hasbuyinistring")
	{
		value = !npc->buyIniString.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "isinteractive")
	{
		value = npc->isInteractive() ? 1 : 0;
		return true;
	}
	if (lowerName == "isobstacle")
	{
		value = npc->isObstacleForCharacter() ? 1 : 0;
		return true;
	}
	if (lowerName == "isvisiblebyvariable")
	{
		value = npc->isVisibleByVariable ? 1 : 0;
		return true;
	}
	if (lowerName == "hasvisiblevariablename")
	{
		value = !npc->visibleVariableName.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "visiblevariablevalue" || lowerName == "visiblevariablethreshold")
	{
		value = npc->visibleVariableValue;
		return true;
	}
	if (lowerName == "isvisible" || lowerName == "isruntimevisible")
	{
		value = npc->isVisibleForRuntime() ? 1 : 0;
		return true;
	}
	if (lowerName == "timerscriptinterval")
	{
		value = static_cast<int>(npc->timerScriptInterval);
		return true;
	}
	if (lowerName == "issignalshow" || lowerName == "issignaltipshown" || lowerName == "hassignaltip")
	{
		value = npc->isSignalShow ? 1 : 0;
		return true;
	}
	if (lowerName == "signalindex")
	{
		value = npc->signalIndex;
		return true;
	}
	if (lowerName == "issignaltypet0" || lowerName == "signaltypet0")
	{
		value = toLowerAscii(trimAscii(npc->signalType)) == "t0" ? 1 : 0;
		return true;
	}
	if (lowerName == "issignaltypet1" || lowerName == "signaltypet1")
	{
		value = toLowerAscii(trimAscii(npc->signalType)) == "t1" ? 1 : 0;
		return true;
	}
	if (lowerName == "istransporting" || lowerName == "isintransport")
	{
		value = npc->isTransporting() ? 1 : 0;
		return true;
	}
	if (lowerName == "iscontrolledbyplayer" || lowerName == "iscontrolledcharacter")
	{
		auto controlled = (gm != nullptr && gm->player != nullptr)
			? gm->player->getControlledCharacter()
			: nullptr;
		value = (controlled != nullptr && controlled == npc) ? 1 : 0;
		return true;
	}
	if (lowerName == "controledmagicsprite" || lowerName == "controlledmagicsprite" || lowerName == "hascontrolledmagicsprite")
	{
		auto controlled = (gm != nullptr && gm->player != nullptr)
			? gm->player->getControlledCharacter()
			: nullptr;
		value = (controlled != nullptr && controlled == npc) ? 1 : 0;
		return true;
	}
	if (lowerName == "isinvisiblebymagic")
	{
		value = npc->isInvisibleByMagic() ? 1 : 0;
		return true;
	}
	if (lowerName == "invisiblemilliseconds")
	{
		value = static_cast<int>(npc->invisibleMilliseconds);
		return true;
	}
	if (lowerName == "isvisiblewhenattack")
	{
		value = npc->isVisibleWhenAttack ? 1 : 0;
		return true;
	}
	if (lowerName == "blindmilliseconds" || lowerName == "blindleftmilliseconds")
	{
		value = static_cast<int>(npc->blindMilliseconds);
		return true;
	}
	if (lowerName == "isblind" || lowerName == "isblinded")
	{
		value = npc->blindMilliseconds > 0 ? 1 : 0;
		return true;
	}
	if (lowerName == "disablemovemilliseconds" || lowerName == "movedisabledmilliseconds")
	{
		value = static_cast<int>(npc->disableMoveMilliseconds);
		return true;
	}
	if (lowerName == "disableskillmilliseconds" || lowerName == "disablefightmilliseconds" || lowerName == "disablemagicmilliseconds")
	{
		value = static_cast<int>(npc->disableSkillMilliseconds);
		return true;
	}
	if (lowerName == "ismovedisabled" || lowerName == "isrundisabled" || lowerName == "isjumpdisabled")
	{
		value = npc->disableMoveMilliseconds > 0 ? 1 : 0;
		return true;
	}
	if (lowerName == "isskilldisabled" || lowerName == "isfightdisabled" || lowerName == "ismagicdisabled")
	{
		value = npc->disableSkillMilliseconds > 0 ? 1 : 0;
		return true;
	}
	if (lowerName == "isaidisabled" || lowerName == "npcaidisabled")
	{
		value = (npc->isAIDisabled || (gm != nullptr && !gm->global.data.NPCAI)) ? 1 : 0;
		return true;
	}
	if (lowerName == "islocalaidisabled" || lowerName == "localaidisabled" || lowerName == "npcinstanceaidisabled")
	{
		value = npc->isAIDisabled ? 1 : 0;
		return true;
	}
	if (lowerName == "globalaidisabled" || lowerName == "isglobalaidisabled")
	{
		value = (gm != nullptr && !gm->global.data.NPCAI) ? 1 : 0;
		return true;
	}
	if (lowerName == "aienabled" || lowerName == "npcaienabled")
	{
		value = npc->isAIEnabled() ? 1 : 0;
		return true;
	}
	if (lowerName == "canseeplayer")
	{
		value = (gm != nullptr && gm->player != nullptr && npc->canSee(gm->player->getPosition())) ? 1 : 0;
		return true;
	}
	if (lowerName == "isdraw")
	{
		value = (!npc->isDying()
			&& !npc->isHiding()
			&& npc->isVisibleForRuntime()
			&& !npc->isHiddenByCarryMagic()) ? 1 : 0;
		return true;
	}
	if (lowerName == "group")
	{
		value = npc->group;
		return true;
	}
	if (lowerName == "mapx")
	{
		value = npc->getPosition().x;
		return true;
	}
	if (lowerName == "mapy")
	{
		value = npc->getPosition().y;
		return true;
	}
	if (lowerName == "offx" || lowerName == "offsetx")
	{
		value = static_cast<int>(round(npc->getOffset().x));
		return true;
	}
	if (lowerName == "offy" || lowerName == "offsety")
	{
		value = static_cast<int>(round(npc->getOffset().y));
		return true;
	}
	if (lowerName == "hasnonzerooffset")
	{
		PointEx offset = npc->getOffset();
		value = (std::abs(offset.x) >= 0.5f || std::abs(offset.y) >= 0.5f) ? 1 : 0;
		return true;
	}
	if (lowerName == "hasdestination")
	{
		value = npc->hasDestinationMapPosition() ? 1 : 0;
		return true;
	}
	if (lowerName == "destinationmovepositioninworld"
		|| lowerName == "destinationmovetileposition"
		|| lowerName == "destinationattacktileposition"
		|| lowerName == "destinationattackpositioninworld")
	{
		value = npc->hasDestinationMapPosition() ? 1 : 0;
		return true;
	}
	if (lowerName == "destinationmapposx")
	{
		value = npc->destinationMapPosition.x;
		return true;
	}
	if (lowerName == "destinationmapposy")
	{
		value = npc->destinationMapPosition.y;
		return true;
	}
	if (lowerName == "currentaction")
	{
		value = npc->actionManager != nullptr
			? static_cast<int>(npc->actionManager->getCurrentActionType())
			: static_cast<int>(npc->nowAction);
		return true;
	}
	if (lowerName == "nowaction")
	{
		value = static_cast<int>(npc->nowAction);
		return true;
	}
	if (lowerName == "isfighting" || lowerName == "isinfighting" || lowerName == "fightstate")
	{
		value = npc->fightState.get() ? 1 : 0;
		return true;
	}
	if (lowerName == "actionplanactive")
	{
		value = npc->actionPlan.isActive() ? 1 : 0;
		return true;
	}
	if (lowerName == "attackoptioncount")
	{
		value = static_cast<int>(npc->attackOptions.size());
		return true;
	}
	if (lowerName == "npcmagicloaded")
	{
		value = (npc->npcMagic != nullptr && npc->npcMagic->loadSucceeded) ? 1 : 0;
		return true;
	}
	if (lowerName == "npcmagic2loaded")
	{
		value = (npc->npcMagic2 != nullptr && npc->npcMagic2->loadSucceeded) ? 1 : 0;
		return true;
	}
	if (lowerName == "haslastusedattackoption")
	{
		value = npc->hasLastUsedAttackOption ? 1 : 0;
		return true;
	}
	if (lowerName == "haspreparedattackmagic")
	{
		value = npc->hasPreparedAttackMagic ? 1 : 0;
		return true;
	}
	if (lowerName == "attackadditionaleffect"
		|| lowerName == "equipmentattackadditionaleffect"
		|| lowerName == "additionaleffect")
	{
		value = npc->getAttackAdditionalEffect();
		return true;
	}
	if (lowerName == "canhitcurrentcombattarget")
	{
		auto target = lockNpcTarget(npc->currentCombatTarget);
		value = (target != nullptr && npc->canAnyAttackOptionHitTarget(target->getPosition())) ? 1 : 0;
		return true;
	}
	if (lowerName == "readyattackoptionavailable")
	{
		auto target = lockNpcTarget(npc->currentCombatTarget);
		value = (target != nullptr && npc->findReadyAttackOption(target->getPosition()).has_value()) ? 1 : 0;
		return true;
	}
	if (lowerName == "hascurrentcombattarget"
		|| lowerName == "followtarget"
		|| lowerName == "isfollowtargetfound"
		|| lowerName == "hasfollowtarget")
	{
		value = npc->currentCombatTarget.expired() ? 0 : 1;
		return true;
	}
	if (lowerName == "currentcombattargetmapx" || lowerName == "followtargetmapx" || lowerName == "followtargetx")
	{
		auto target = lockNpcTarget(npc->currentCombatTarget);
		value = target == nullptr ? 0 : target->getPosition().x;
		return true;
	}
	if (lowerName == "currentcombattargetmapy" || lowerName == "followtargetmapy" || lowerName == "followtargety")
	{
		auto target = lockNpcTarget(npc->currentCombatTarget);
		value = target == nullptr ? 0 : target->getPosition().y;
		return true;
	}
	if (lowerName == "currentcombattargetisplayer" || lowerName == "followtargetisplayer")
	{
		auto target = lockNpcTarget(npc->currentCombatTarget);
		value = (target != nullptr && NPC::isPlayerKind(target->kind)) ? 1 : 0;
		return true;
	}
	if (lowerName == "currentcombattargetkind" || lowerName == "followtargetkind")
	{
		auto target = lockNpcTarget(npc->currentCombatTarget);
		value = target == nullptr ? 0 : target->kind;
		return true;
	}
	if (lowerName == "currentcombattargetrelation" || lowerName == "followtargetrelation")
	{
		auto target = lockNpcTarget(npc->currentCombatTarget);
		value = target == nullptr ? 0 : target->relation;
		return true;
	}
	if (lowerName == "haslastcombattarget")
	{
		value = npc->lastCombatTarget.expired() ? 0 : 1;
		return true;
	}
	if (lowerName == "lastcombattargetmapx")
	{
		auto target = lockNpcTarget(npc->lastCombatTarget);
		value = target == nullptr ? 0 : target->getPosition().x;
		return true;
	}
	if (lowerName == "lastcombattargetmapy")
	{
		auto target = lockNpcTarget(npc->lastCombatTarget);
		value = target == nullptr ? 0 : target->getPosition().y;
		return true;
	}
	if (lowerName == "lastcombattargetisplayer")
	{
		auto target = lockNpcTarget(npc->lastCombatTarget);
		value = (target != nullptr && NPC::isPlayerKind(target->kind)) ? 1 : 0;
		return true;
	}
	if (lowerName == "isstanding")
	{
		value = npc->isStanding() ? 1 : 0;
		return true;
	}
	if (lowerName == "iswalking")
	{
		value = npc->isWalking() ? 1 : 0;
		return true;
	}
	if (lowerName == "isrunning")
	{
		value = npc->isRunning() ? 1 : 0;
		return true;
	}
	if (lowerName == "issitting")
	{
		value = npc->isSitting() ? 1 : 0;
		return true;
	}
	if (lowerName == "isattacking")
	{
		value = npc->isAttacking() ? 1 : 0;
		return true;
	}
	if (lowerName == "ismagicing")
	{
		value = npc->isMagicing() ? 1 : 0;
		return true;
	}
	if (lowerName == "isjumping")
	{
		value = npc->isJumping() ? 1 : 0;
		return true;
	}
	if (lowerName == "ishurting")
	{
		value = npc->isHurting() ? 1 : 0;
		return true;
	}
	if (lowerName == "ishide" || lowerName == "ishiding")
	{
		value = npc->isHiding() ? 1 : 0;
		return true;
	}
	if (lowerName == "isindeathing")
	{
		value = npc->isDying() ? 1 : 0;
		return true;
	}
	if (lowerName == "isdeath" || lowerName == "isdeathinvoked")
	{
		value = (npc->isDying() || npc->isHiding() || npc->life <= 0) ? 1 : 0;
		return true;
	}
	if (lowerName == "isdeathscriptend")
	{
		value = (npc->deathScript.empty() && !hasPendingDeathScriptForNpc(npc)) ? 1 : 0;
		return true;
	}
	if (lowerName == "isdoingspecialaction")
	{
		value = npc->isDoingSpecialAction() ? 1 : 0;
		return true;
	}
	if (lowerName == "isinspecialaction")
	{
		value = npc->isDoingSpecialAction() ? 1 : 0;
		return true;
	}
	if (lowerName == "isbouncing")
	{
		value = npc->isBouncing() ? 1 : 0;
		return true;
	}
	if (lowerName == "bouncedvelocity")
	{
		value = static_cast<int>(std::lround(npc->bounceVelocity));
		return true;
	}
	if (lowerName == "bounceddirection" || lowerName == "hasbounceddirection")
	{
		value = (std::abs(npc->bounceDirection.x) >= 0.001f || std::abs(npc->bounceDirection.y) >= 0.001f) ? 1 : 0;
		return true;
	}
	if (lowerName == "bouncevelocity")
	{
		value = static_cast<int>(std::lround(npc->bounceVelocity));
		return true;
	}
	if (lowerName == "bouncedirectionxpermille")
	{
		value = static_cast<int>(std::lround(npc->bounceDirection.x * 1000.0f));
		return true;
	}
	if (lowerName == "bouncedirectionypermille")
	{
		value = static_cast<int>(std::lround(npc->bounceDirection.y * 1000.0f));
		return true;
	}
	if (lowerName == "bouncecollisionhurt")
	{
		value = npc->bounceCollisionHurt;
		return true;
	}
	if (lowerName == "lastbounceblockedbycharacter"
		|| lowerName == "lastbounceblockedbynpc"
		|| lowerName == "bouncelastblockedbycharacter")
	{
		value = npc->lastBounceBlockedByCharacter ? 1 : 0;
		return true;
	}
	if (lowerName == "lastbounceblockedx" || lowerName == "lastbounceblockedmapx")
	{
		value = npc->lastBounceBlockedCharacterPosition.x;
		return true;
	}
	if (lowerName == "lastbounceblockedy" || lowerName == "lastbounceblockedmapy")
	{
		value = npc->lastBounceBlockedCharacterPosition.y;
		return true;
	}
	if (lowerName == "lastbounceendx" || lowerName == "lastbounceendmapx")
	{
		value = npc->lastBounceEndPosition.x;
		return true;
	}
	if (lowerName == "lastbounceendy" || lowerName == "lastbounceendmapy")
	{
		value = npc->lastBounceEndPosition.y;
		return true;
	}
	if (lowerName == "ismagicforcedmoving" || lowerName == "isforcedmoving")
	{
		value = npc->isMagicForcedMoving() ? 1 : 0;
		return true;
	}
	if (lowerName == "movedbymagicsprite" || lowerName == "boundbymagicsprite")
	{
		value = npc->isMagicForcedMoving() ? 1 : 0;
		return true;
	}
	if (lowerName == "movedbymagicspriteoffset")
	{
		value = npc->isMagicForcedMoving()
			&& (npc->magicForcedMove.destination != npc->getPosition()) ? 1 : 0;
		return true;
	}
	if (lowerName == "magicforcedmovedestinationx" || lowerName == "forcedmovedestinationx")
	{
		value = npc->magicForcedMove.destination.x;
		return true;
	}
	if (lowerName == "magicforcedmovedestinationy" || lowerName == "forcedmovedestinationy")
	{
		value = npc->magicForcedMove.destination.y;
		return true;
	}
	if (lowerName == "magicforcedmovespeed" || lowerName == "forcedmovespeed")
	{
		value = static_cast<int>(std::lround(npc->magicForcedMove.speed));
		return true;
	}
	if (lowerName == "magicforcedmoveendhurt" || lowerName == "forcedmoveendhurt")
	{
		value = npc->magicForcedMove.endHurt;
		return true;
	}
	if (lowerName == "magicforcedmovetouchhurt" || lowerName == "forcedmovetouchhurt")
	{
		value = npc->magicForcedMove.touchHurt;
		return true;
	}
	if (lowerName == "magicforcedmovetouchdistance" || lowerName == "forcedmovetouchdistance")
	{
		value = npc->magicForcedMove.touchDistance;
		return true;
	}
	if (lowerName == "magicforcedmovehastouchdirection" || lowerName == "forcedmovehastouchdirection")
	{
		value = npc->magicForcedMove.hasTouchDirection ? 1 : 0;
		return true;
	}
	if (lowerName == "magicforcedmovetouchdirectionx" || lowerName == "forcedmovetouchdirectionx")
	{
		value = static_cast<int>(std::lround(npc->magicForcedMove.touchDirection.x));
		return true;
	}
	if (lowerName == "magicforcedmovetouchdirectiony" || lowerName == "forcedmovetouchdirectiony")
	{
		value = static_cast<int>(std::lround(npc->magicForcedMove.touchDirection.y));
		return true;
	}
	if (lowerName == "magicforcedmovehasendmagic" || lowerName == "forcedmovehasendmagic")
	{
		value = (npc->magicForcedMove.endMagic != nullptr && npc->magicForcedMove.endMagic->loadSucceeded) ? 1 : 0;
		return true;
	}
	if (lowerName == "magicforcedmoveblockcharactersonpath" || lowerName == "forcedmoveblockcharactersonpath")
	{
		value = npc->magicForcedMove.blockCharactersOnPath ? 1 : 0;
		return true;
	}
	if (lowerName == "magicforcedmoveusesbezier" || lowerName == "forcedmoveusesbezier")
	{
		value = npc->isMagicForcedMoving() && npc->magicForcedMove.totalProjectedDistance > 0.0f ? 1 : 0;
		return true;
	}
	if (lowerName == "magicforcedmoveprogresspermille" || lowerName == "forcedmoveprogresspermille")
	{
		value = npc->isMagicForcedMoving()
			? getMovementProgressPermille(npc->magicForcedMove.movedProjectedDistance, npc->magicForcedMove.totalProjectedDistance)
			: 1000;
		return true;
	}
	if (lowerName == "magicforcedmovebezieroffsetx" || lowerName == "forcedmovebezieroffsetx")
	{
		value = static_cast<int>(std::lround(npc->magicForcedMove.drawOffset.x));
		return true;
	}
	if (lowerName == "magicforcedmovebezieroffsety" || lowerName == "forcedmovebezieroffsety")
	{
		value = static_cast<int>(std::lround(npc->magicForcedMove.drawOffset.y));
		return true;
	}
	if (lowerName == "magicforcedmovebezieroffsetlength" || lowerName == "forcedmovebezieroffsetlength")
	{
		value = static_cast<int>(std::lround(std::hypot(npc->magicForcedMove.drawOffset.x, npc->magicForcedMove.drawOffset.y)));
		return true;
	}
	if (lowerName == "isfrozened" || lowerName == "isfrozen")
	{
		value = npc->frozen ? 1 : 0;
		return true;
	}
	if (lowerName == "frozenmilliseconds" || lowerName == "frozenleftmilliseconds" || lowerName == "frozenlasttime")
	{
		value = static_cast<int>(npc->frozenLastTime);
		return true;
	}
	if (lowerName == "frozenvisualeffect" || lowerName == "isfrozenvisualeffect" || lowerName == "isfronzenvisualeffect")
	{
		value = npc->frozenVisualEffect ? 1 : 0;
		return true;
	}
	if (lowerName == "isimmobilized")
	{
		value = npc->immobilized ? 1 : 0;
		return true;
	}
	if (lowerName == "immobilizedmilliseconds" || lowerName == "immobilizedleftmilliseconds" || lowerName == "immobilizedlasttime")
	{
		value = static_cast<int>(npc->immobilizedLastTime);
		return true;
	}
	if (lowerName == "immobilizedvisualeffect" || lowerName == "isimmobilizedvisualeffect")
	{
		value = npc->immobilizedVisualEffect ? 1 : 0;
		return true;
	}
	if (lowerName == "ispoisoned")
	{
		value = npc->poisoned ? 1 : 0;
		return true;
	}
	if (lowerName == "poisonedmilliseconds" || lowerName == "poisonmilliseconds" || lowerName == "poisonedleftmilliseconds" || lowerName == "poisonedlasttime")
	{
		value = static_cast<int>(npc->poisonedLastTime);
		return true;
	}
	if (lowerName == "poisonedvisualeffect" || lowerName == "ispoisonedvisualeffect" || lowerName == "ispoisionvisualeffect" || lowerName == "ispoisonvisualeffect")
	{
		value = npc->poisonedVisualEffect ? 1 : 0;
		return true;
	}
	if (lowerName == "ispetrified")
	{
		value = npc->petrified ? 1 : 0;
		return true;
	}
	if (lowerName == "petrifiedmilliseconds" || lowerName == "petrifymilliseconds" || lowerName == "petrifiedleftmilliseconds" || lowerName == "petrifiedlasttime")
	{
		value = static_cast<int>(npc->petrifiedLastTime);
		return true;
	}
	if (lowerName == "petrifiedvisualeffect" || lowerName == "ispetrifiedvisualeffect")
	{
		value = npc->petrifiedVisualEffect ? 1 : 0;
		return true;
	}
	if (lowerName == "bodyfunctionwell" || lowerName == "isnormalstate")
	{
		value = (!npc->frozen && !npc->poisoned && !npc->petrified && !npc->immobilized) ? 1 : 0;
		return true;
	}
	if (lowerName == "stepstate")
	{
		value = static_cast<int>(npc->getStepState());
		return true;
	}
	if (lowerName == "isinstepmove")
	{
		value = ((npc->isWalking() || npc->isRunning())
			&& npc->stepLastTime > 0
			&& (!npc->stepList.empty() || !npc->getStepPositions().empty())) ? 1 : 0;
		return true;
	}
	if (lowerName == "isstepin")
	{
		value = npc->getStepState() == ssIn ? 1 : 0;
		return true;
	}
	if (lowerName == "isstepout")
	{
		value = npc->getStepState() == ssOut ? 1 : 0;
		return true;
	}
	if (lowerName == "steplistlength" || lowerName == "pathlength")
	{
		value = static_cast<int>(npc->stepList.size());
		return true;
	}
	if (lowerName == "path")
	{
		value = static_cast<int>(npc->stepList.size());
		return true;
	}
	if (lowerName == "steptargetx")
	{
		value = npc->stepList.empty() ? npc->getPosition().x : npc->stepList.front().x;
		return true;
	}
	if (lowerName == "steptargety")
	{
		value = npc->stepList.empty() ? npc->getPosition().y : npc->stepList.front().y;
		return true;
	}
	if (lowerName == "stepoccupiedtilecount" || lowerName == "stepoccupiedcount")
	{
		value = static_cast<int>(npc->getStepPositions().size());
		return true;
	}
	if (lowerName == "stepelapsedmilliseconds" || lowerName == "stepelapsed")
	{
		if ((npc->isWalking() || npc->isRunning()) && npc->stepLastTime > 0 && npc->getTime() >= npc->stepBeginTime)
		{
			value = static_cast<int>(std::min<UTime>(npc->getTime() - npc->stepBeginTime, npc->stepLastTime));
		}
		else
		{
			value = 0;
		}
		return true;
	}
	if (lowerName == "steplastmilliseconds" || lowerName == "steplasttime")
	{
		value = static_cast<int>(npc->stepLastTime);
		return true;
	}
	if (lowerName == "stepprogresspermille")
	{
		if ((npc->isWalking() || npc->isRunning()) && npc->stepLastTime > 0 && npc->getTime() >= npc->stepBeginTime)
		{
			UTime elapsed = std::min<UTime>(npc->getTime() - npc->stepBeginTime, npc->stepLastTime);
			value = static_cast<int>((elapsed * 1000) / npc->stepLastTime);
		}
		else
		{
			value = 0;
		}
		return true;
	}
	if (lowerName == "issmoothstepmoving")
	{
		value = ((npc->isWalking() || npc->isRunning())
			&& npc->stepLastTime > 0
			&& (!npc->stepList.empty() || !npc->getStepPositions().empty())) ? 1 : 0;
		return true;
	}
	if (lowerName == "canwalkaction")
	{
		value = (npc->canDoAction(acWalk) || npc->canDoAction(acAWalk)) ? 1 : 0;
		return true;
	}
	if (lowerName == "canfightstandaction")
	{
		value = npc->canDoAction(acAStand) ? 1 : 0;
		return true;
	}
	if (lowerName == "canfightwalkaction")
	{
		value = npc->canDoAction(acAWalk) ? 1 : 0;
		return true;
	}
	if (lowerName == "canfightrunaction")
	{
		value = npc->canDoAction(acARun) ? 1 : 0;
		return true;
	}
	if (lowerName == "canfightjumpaction")
	{
		value = npc->canDoAction(acAJump) ? 1 : 0;
		return true;
	}
	if (lowerName == "canmovedircount" || lowerName == "canmovedirectioncount")
	{
		value = npc->getMoveDirectionCount();
		return true;
	}
	if (lowerName == "canattackdircount" || lowerName == "canattackdirectioncount")
	{
		value = npc->getAttackDirectionCount();
		return true;
	}
	if (lowerName == "canusemagicdircount" || lowerName == "canusemagicdirectioncount")
	{
		value = npc->getUseMagicDirectionCount();
		return true;
	}
	if (lowerName == "canjumpdircount" || lowerName == "canjumpdirectioncount")
	{
		value = npc->getJumpDirectionCount();
		return true;
	}
	if (lowerName == "pathtype" || lowerName == "resolvedpathtype")
	{
		value = npc->resolvePathType();
		return true;
	}
	if (lowerName == "pathsearchmaxtry" || lowerName == "pathtypemaxtry")
	{
		value = NPC::getPathSearchMaxTryForPathType(npc->resolvePathType());
		return true;
	}
	if (lowerName == "hasfixedpath")
	{
		value = npc->hasFixedPath() ? 1 : 0;
		return true;
	}
	if (lowerName == "fixedpathcount" || lowerName == "fixedpathtilepositioncount")
	{
		value = static_cast<int>(npc->fixedPathTilePositions.size());
		return true;
	}
	if (lowerName == "currentfixedposindex" || lowerName == "currpos")
	{
		value = static_cast<int>(npc->currentFixedPosIndex);
		return true;
	}
	if (lowerName == "usesimplepath" || lowerName == "issimplepathtype")
	{
		value = npc->resolvePathType() == nptSimpleMaxNpcTry ? 1 : 0;
		return true;
	}
	if (lowerName == "usepathfinder" || lowerName == "usespathfinder" || lowerName == "isusingpathfinder")
	{
		value = npc->usePathFinder() ? 1 : 0;
		return true;
	}
	if (lowerName == "destinationpathtype")
	{
		value = npc->resolveDestinationPathType();
		return true;
	}
	if (lowerName == "destinationtemporarydisablerestrict" || lowerName == "destinationusestemporarydisablerestrict")
	{
		value = npc->hasDestinationMapPosition() && npc->resolveDestinationPathType() == nptPerfectMaxPlayerTry ? 1 : 0;
		return true;
	}
	if (lowerName == "destinationpathsearchmaxtry" || lowerName == "destinationpathtypemaxtry")
	{
		value = npc->hasDestinationMapPosition()
			? NPC::getPathSearchMaxTryForPathType(npc->resolveDestinationPathType(), true)
			: 0;
		return true;
	}
	if (lowerName == "destinationblockedbycharacter" || lowerName == "destinationblockedbynpc")
	{
		value = npc->isDestinationMapPositionBlockedByCharacter() ? 1 : 0;
		return true;
	}
	if (lowerName == "lastmagicforcedmoveblockedbycharacter"
		|| lowerName == "lastmagicforcedmoveblockedbynpc"
		|| lowerName == "magicforcedmovelastblockedbycharacter")
	{
		value = npc->lastMagicForcedMoveBlockedByCharacter ? 1 : 0;
		return true;
	}
	if (lowerName == "lastmagicforcedmoveblockedx" || lowerName == "lastmagicforcedmoveblockedmapx")
	{
		value = npc->lastMagicForcedMoveBlockedCharacterPosition.x;
		return true;
	}
	if (lowerName == "lastmagicforcedmoveblockedy" || lowerName == "lastmagicforcedmoveblockedmapy")
	{
		value = npc->lastMagicForcedMoveBlockedCharacterPosition.y;
		return true;
	}
	if (lowerName == "lastmagicforcedmoveendx" || lowerName == "lastmagicforcedmoveendmapx")
	{
		value = npc->lastMagicForcedMoveEndPosition.x;
		return true;
	}
	if (lowerName == "lastmagicforcedmoveendy" || lowerName == "lastmagicforcedmoveendmapy")
	{
		value = npc->lastMagicForcedMoveEndPosition.y;
		return true;
	}
	if (lowerName == "lastmagicforcedmovetouchtargetcount" || lowerName == "lastforcedmovetouchtargetcount")
	{
		value = npc->lastMagicForcedMoveTouchTargetCount;
		return true;
	}
	if (lowerName == "lastmagicforcedmovetouchhurtcount" || lowerName == "lastforcedmovetouchhurtcount")
	{
		value = npc->lastMagicForcedMoveTouchHurtCount;
		return true;
	}
	if (lowerName == "destinationpathlength")
	{
		if (gm == nullptr || gm->map == nullptr || !npc->hasDestinationMapPosition())
		{
			value = 0;
			return true;
		}
		value = static_cast<int>(npc->findPathByType(npc->destinationMapPosition, npc->resolveDestinationPathType(), true).size());
		return true;
	}
	if (lowerName == "destinationfirststepcanwalk")
	{
		if (gm == nullptr || gm->map == nullptr || !npc->hasDestinationMapPosition())
		{
			value = 0;
			return true;
		}
		auto path = npc->findPathByType(npc->destinationMapPosition, npc->resolveDestinationPathType(), true);
		if (path.empty())
		{
			value = 0;
			return true;
		}
		if (gm->map->canWalk(path[0]))
		{
			value = 1;
			return true;
		}
		auto selfStepPositions = npc->getStepPositions();
		for (const Point& stepPosition : selfStepPositions)
		{
			if (stepPosition == path[0])
			{
				value = 1;
				return true;
			}
		}
		value = 0;
		return true;
	}
	if (lowerName == "ai_type" || lowerName == "aitype")
	{
		value = npc->aiType;
		return true;
	}
	if (lowerName == "israndmoverandattack" || lowerName == "randmoverandattack")
	{
		value = npc->isRandMoveRandAttack() ? 1 : 0;
		return true;
	}
	if (lowerName == "isnotfightbackwhenbehit" || lowerName == "notfightbackwhenbehit" || lowerName == "nofightbackwhenbehit")
	{
		value = npc->isNotFightBackWhenBeHit() ? 1 : 0;
		return true;
	}
	if (lowerName == "idle" || lowerName == "attackinterval")
	{
		value = npc->idle;
		return true;
	}
	if (lowerName == "expbonus")
	{
		value = npc->expBonus;
		return true;
	}
	if (lowerName == "invincible")
	{
		value = npc->invincible;
		return true;
	}
	if (lowerName == "idledframe" || lowerName == "idleframe")
	{
		value = npc->idledFrame;
		return true;
	}
	if (lowerName == "noautoattackplayer")
	{
		value = npc->noAutoAttackPlayer;
		return true;
	}
	if (lowerName == "stopfindingtarget")
	{
		value = npc->stopFindingTarget;
		return true;
	}
	if (lowerName == "pathfinder")
	{
		value = npc->pathFinder;
		return true;
	}
	if (lowerName == "state")
	{
		value = npc->state;
		return true;
	}
	if (lowerName == "strollintent" || lowerName == "action")
	{
		value = npc->strollIntent;
		return true;
	}
	if (lowerName == "actionpathtilepositioncount" || lowerName == "randwalkpathlength" || lowerName == "randwalkcandidatecount")
	{
		value = npc->getActionPathTilePositionCount();
		return true;
	}
	if (lowerName == "ensurerandwalkpathlength" || lowerName == "ensurerandwalkcandidatecount")
	{
		npc->ensureActionPathTilePositions(npc->kind == nkFlyingAnimal);
		value = npc->getActionPathTilePositionCount();
		return true;
	}
	if (lowerName == "lum")
	{
		value = npc->lum;
		return true;
	}
	if (lowerName == "kindvalue")
	{
		value = npc->kindValue;
		return true;
	}
	if (lowerName == "kindvaluemax")
	{
		value = npc->kindValueMax;
		return true;
	}
	if (lowerName == "life")
	{
		value = npc->life;
		return true;
	}
	if (lowerName == "isfulllife")
	{
		value = npc->life >= npc->getLifeMax() ? 1 : 0;
		return true;
	}
	if (lowerName == "lifemax" || lowerName == "reallifemax" || lowerName == "effectivelifemax")
	{
		value = npc->getLifeMax();
		return true;
	}
	if (lowerName == "baselifemax" || lowerName == "rawlifemax")
	{
		value = npc->lifeMax;
		return true;
	}
	if (lowerName == "thew")
	{
		value = npc->thew;
		return true;
	}
	if (lowerName == "thewmax" || lowerName == "realthewmax" || lowerName == "effectivethewmax")
	{
		value = npc->getThewMax();
		return true;
	}
	if (lowerName == "basethewmax" || lowerName == "rawthewmax")
	{
		value = npc->thewMax;
		return true;
	}
	if (lowerName == "mana")
	{
		value = npc->mana;
		return true;
	}
	if (lowerName == "manamax" || lowerName == "realmanamax" || lowerName == "effectivemanamax")
	{
		value = npc->getManaMax();
		return true;
	}
	if (lowerName == "basemanamax" || lowerName == "rawmanamax")
	{
		value = npc->manaMax;
		return true;
	}
	if (lowerName == "attack")
	{
		value = npc->attack;
		return true;
	}
	if (lowerName == "realattack")
	{
		value = npc->getAttack();
		return true;
	}
	if (lowerName == "attack2")
	{
		value = npc->attack2;
		return true;
	}
	if (lowerName == "attack3")
	{
		value = npc->attack3;
		return true;
	}
	if (lowerName == "defend" || lowerName == "defence")
	{
		value = npc->defend;
		return true;
	}
	if (lowerName == "realdefend" || lowerName == "realdefence")
	{
		value = npc->getDefend();
		return true;
	}
	if (lowerName == "defend2" || lowerName == "defence2")
	{
		value = npc->defend2;
		return true;
	}
	if (lowerName == "defend3" || lowerName == "defence3")
	{
		value = npc->defend3;
		return true;
	}
	if (lowerName == "evade")
	{
		value = npc->evade;
		return true;
	}
	if (lowerName == "realevade")
	{
		value = npc->getEvade();
		return true;
	}
	if (lowerName == "duck")
	{
		value = npc->duck;
		return true;
	}
	if (lowerName == "dodge_beginframe" || lowerName == "dodgebeginframe")
	{
		value = npc->dodgeBeginFrame;
		return true;
	}
	if (lowerName == "hasdodge_beginframe" || lowerName == "hasdodgebeginframe")
	{
		value = npc->hasDodgeBeginFrameField ? 1 : 0;
		return true;
	}
	if (lowerName == "dodge_endframe" || lowerName == "dodgeendframe")
	{
		value = npc->dodgeEndFrame;
		return true;
	}
	if (lowerName == "hasdodge_endframe" || lowerName == "hasdodgeendframe")
	{
		value = npc->hasDodgeEndFrameField ? 1 : 0;
		return true;
	}
	if (lowerName == "exp")
	{
		value = npc->exp;
		return true;
	}
	if (lowerName == "level")
	{
		value = npc->level;
		return true;
	}
	if (lowerName == "levelupexp")
	{
		value = npc->levelUpExp;
		return true;
	}
	if (lowerName == "canlevelup")
	{
		value = npc->canLevelUp;
		return true;
	}
	if (lowerName == "canequip")
	{
		value = npc->canEquip;
		return true;
	}
	if (lowerName == "attacklevel")
	{
		value = npc->attackLevel;
		return true;
	}
	if (lowerName == "magiclevel")
	{
		value = npc->magicLevel;
		return true;
	}
	if (lowerName == "walkspeed")
	{
		value = npc->walkSpeed;
		return true;
	}
	if (lowerName == "runspeed")
	{
		value = npc->runSpeed;
		return true;
	}
	if (lowerName == "standspeed")
	{
		value = npc->standSpeed;
		return true;
	}
	if (lowerName == "attackspeed")
	{
		value = npc->attackSpeed;
		return true;
	}
	if (lowerName == "hasattackspeed" || lowerName == "hasattackspeedfield")
	{
		value = npc->hasAttackSpeedField ? 1 : 0;
		return true;
	}
	if (lowerName == "dialogradius")
	{
		value = npc->dialogRadius == 0 ? 1 : npc->dialogRadius;
		return true;
	}
	if (lowerName == "visionradius")
	{
		value = npc->visionRadius == 0 ? 9 : npc->visionRadius;
		return true;
	}
	if (lowerName == "attackradius")
	{
		value = npc->attackRadius == 0 ? 1 : npc->attackRadius;
		return true;
	}
	if (lowerName == "steal")
	{
		value = npc->steal;
		return true;
	}
	if (lowerName == "eloquence")
	{
		value = npc->eloquence;
		return true;
	}
	if (lowerName == "leechcraft")
	{
		value = npc->leechcraft;
		return true;
	}
	if (lowerName == "autorunscript")
	{
		value = npc->autoRunScript;
		return true;
	}
	if (lowerName == "hasautorunscript")
	{
		value = npc->hasAutoRunScriptField ? 1 : 0;
		return true;
	}
	if (lowerName == "arm")
	{
		value = npc->arm;
		return true;
	}
	if (lowerName == "hasarm")
	{
		value = npc->hasArmField ? 1 : 0;
		return true;
	}
	if (lowerName == "evaden")
	{
		value = npc->evadeN;
		return true;
	}
	if (lowerName == "hasevaden")
	{
		value = npc->hasEvadeNField ? 1 : 0;
		return true;
	}
	if (lowerName == "gengu")
	{
		value = npc->gengu;
		return true;
	}
	if (lowerName == "hasgengu")
	{
		value = npc->hasGenguField ? 1 : 0;
		return true;
	}
	if (lowerName == "neixi")
	{
		value = npc->neixi;
		return true;
	}
	if (lowerName == "hasneixi")
	{
		value = npc->hasNeixiField ? 1 : 0;
		return true;
	}
	if (lowerName == "physique")
	{
		value = npc->physique;
		return true;
	}
	if (lowerName == "hasphysique")
	{
		value = npc->hasPhysiqueField ? 1 : 0;
		return true;
	}
	if (lowerName == "revivemilliseconds")
	{
		value = static_cast<int>(npc->reviveMilliseconds);
		return true;
	}
	if (lowerName == "leftmillisecondstorevive")
	{
		value = static_cast<int>(npc->leftMillisecondsToRevive);
		return true;
	}
	if (lowerName == "lifemilliseconds")
	{
		value = static_cast<int>(npc->lifeMilliseconds);
		return true;
	}
	if (lowerName == "isbodyiniadded")
	{
		value = npc->isBodyIniAdded;
		return true;
	}
	if (lowerName == "isnodaddbody" || lowerName == "noaddbody")
	{
		value = npc->noAddBody ? 1 : 0;
		return true;
	}
	if (lowerName == "shouldaddbody")
	{
		value = (!npc->noAddBody && !npc->bodyIni.empty()) ? 1 : 0;
		return true;
	}
	if (lowerName == "usespecialdeath" || lowerName == "isspecialdeath")
	{
		value = npc->useSpecialDeath ? 1 : 0;
		return true;
	}
	if (lowerName == "isbodyiniok")
	{
		value = npc->bodyIni.empty() ? 0 : 1;
		return true;
	}
	if (lowerName == "caninteractdirectly")
	{
		value = npc->canInteractDirectly;
		return true;
	}
	if (lowerName == "lifelowpercent")
	{
		value = npc->lifeLowPercent;
		return true;
	}
	if (lowerName == "islifelow")
	{
		value = npc->isLifeLowForAI() ? 1 : 0;
		return true;
	}
	if (lowerName == "keepradiuswhenlifelow")
	{
		value = npc->keepRadiusWhenLifeLow;
		return true;
	}
	if (lowerName == "keepradiuswhenfrienddeath")
	{
		value = npc->keepRadiusWhenFriendDeath;
		return true;
	}
	if (lowerName == "magictousewhenlifelowloaded")
	{
		value = (npc->magicToUseWhenLifeLow != nullptr && npc->magicToUseWhenLifeLow->loadSucceeded) ? 1 : 0;
		return true;
	}
	if (lowerName == "magictousewhenbeattackedloaded")
	{
		value = (npc->magicToUseWhenBeAttacked != nullptr && npc->magicToUseWhenBeAttacked->loadSucceeded) ? 1 : 0;
		return true;
	}
	if (lowerName == "magicdirectionwhenbeattacked")
	{
		value = npc->magicDirectionWhenBeAttacked;
		return true;
	}
	if (lowerName == "magictousewhendeathloaded")
	{
		value = (npc->magicToUseWhenDeath != nullptr && npc->magicToUseWhenDeath->loadSucceeded) ? 1 : 0;
		return true;
	}
	if (lowerName == "magicdirectionwhendeath")
	{
		value = npc->magicDirectionWhenDeath;
		return true;
	}
	if (lowerName == "hurtplayerinterval")
	{
		value = static_cast<int>(npc->hurtPlayerInterval);
		return true;
	}
	if (lowerName == "hurtplayerlife")
	{
		value = npc->hurtPlayerLife;
		return true;
	}
	if (lowerName == "hurtplayerradius")
	{
		value = npc->hurtPlayerRadius;
		return true;
	}
	if (lowerName == "nodropwhendie")
	{
		value = npc->noDropWhenDie;
		return true;
	}
	if (lowerName == "issummonedbymagic" || lowerName == "hassummonowner")
	{
		value = npc->summonedByMagicEffect.expired() ? 0 : 1;
		return true;
	}
	if (lowerName == "summonownerisplayer")
	{
		auto summonEffect = npc->summonedByMagicEffect.lock();
		auto owner = summonEffect == nullptr ? nullptr : std::dynamic_pointer_cast<NPC>(summonEffect->user.lock());
		value = (owner != nullptr && NPC::isPlayerKind(owner->kind)) ? 1 : 0;
		return true;
	}
	if (lowerName == "summonownerispartner")
	{
		auto summonEffect = npc->summonedByMagicEffect.lock();
		auto owner = summonEffect == nullptr ? nullptr : std::dynamic_pointer_cast<NPC>(summonEffect->user.lock());
		value = (owner != nullptr && owner->kind == nkPartner) ? 1 : 0;
		return true;
	}
	if (lowerName == "summonownerkind")
	{
		auto summonEffect = npc->summonedByMagicEffect.lock();
		auto owner = summonEffect == nullptr ? nullptr : std::dynamic_pointer_cast<NPC>(summonEffect->user.lock());
		value = owner == nullptr ? 0 : owner->kind;
		return true;
	}
	if (lowerName == "summonownerrelation")
	{
		auto summonEffect = npc->summonedByMagicEffect.lock();
		auto owner = summonEffect == nullptr ? nullptr : std::dynamic_pointer_cast<NPC>(summonEffect->user.lock());
		value = owner == nullptr ? 0 : owner->relation;
		return true;
	}
	if (lowerName == "summonmagiclauncherkind")
	{
		auto summonEffect = npc->summonedByMagicEffect.lock();
		value = summonEffect == nullptr ? 0 : summonEffect->launcherKind;
		return true;
	}
	if (lowerName == "summoneffectalive")
	{
		auto summonEffect = npc->summonedByMagicEffect.lock();
		value = (summonEffect != nullptr && !summonEffect->vanishing && summonEffect->doing != ekHiding) ? 1 : 0;
		return true;
	}
	if (lowerName == "summonedbymagicsprite")
	{
		value = npc->summonedByMagicEffect.expired() ? 0 : 1;
		return true;
	}
	if (lowerName == "summoneffectdoing")
	{
		auto summonEffect = npc->summonedByMagicEffect.lock();
		value = summonEffect == nullptr ? 0 : summonEffect->doing;
		return true;
	}
	if (lowerName == "summonednpccount" || lowerName == "summonednpcscount"
		|| lowerName == "livesummonednpccount" || lowerName == "livesummonednpcscount")
	{
		value = countLiveSummonedNpcs(npc);
		return true;
	}
	if (lowerName == "equipmentchangemovespeedpercent")
	{
		value = npc->equipmentChangeMoveSpeedPercent;
		return true;
	}
	if (lowerName == "changemovespeedpercent")
	{
		value = npc->equipmentChangeMoveSpeedPercent;
		return true;
	}
	if (lowerName == "equipmentlifemax")
	{
		value = getRuntimeEquipmentAttributes(npc).lifeMax;
		return true;
	}
	if (lowerName == "equipmentthewmax")
	{
		value = getRuntimeEquipmentAttributes(npc).thewMax;
		return true;
	}
	if (lowerName == "equipmentmanamax")
	{
		value = getRuntimeEquipmentAttributes(npc).manaMax;
		return true;
	}
	if (lowerName == "equipmentattack")
	{
		value = getRuntimeEquipmentAttributes(npc).attack;
		return true;
	}
	if (lowerName == "equipmentattack2")
	{
		value = getRuntimeEquipmentAttributes(npc).attack2;
		return true;
	}
	if (lowerName == "equipmentattack3")
	{
		value = getRuntimeEquipmentAttributes(npc).attack3;
		return true;
	}
	if (lowerName == "equipmentdefend" || lowerName == "equipmentdefence")
	{
		value = getRuntimeEquipmentAttributes(npc).defend;
		return true;
	}
	if (lowerName == "equipmentdefend2" || lowerName == "equipmentdefence2")
	{
		value = getRuntimeEquipmentAttributes(npc).defend2;
		return true;
	}
	if (lowerName == "equipmentdefend3" || lowerName == "equipmentdefence3")
	{
		value = getRuntimeEquipmentAttributes(npc).defend3;
		return true;
	}
	if (lowerName == "equipmentevade")
	{
		value = getRuntimeEquipmentAttributes(npc).evade;
		return true;
	}
	if (lowerName == "equipmentliferestorepercent")
	{
		value = static_cast<int>(std::lround(npc->equipmentExtraLifeRestorePercent * 100.0f));
		return true;
	}
	if (lowerName == "equipmentliferestoreelapsedmilliseconds")
	{
		value = static_cast<int>(npc->equipmentLifeRestoreElapsedMilliseconds);
		return true;
	}
	if (lowerName == "addmovespeedpercent")
	{
		value = npc->addMoveSpeedPercent;
		return true;
	}
	if (lowerName == "sppedupbymagicsprite" || lowerName == "speedupbymagicsprite")
	{
		value = npc->hasActiveRangeSpeedUp() ? 1 : 0;
		return true;
	}
	if (lowerName == "movespeedfoldpermille")
	{
		value = static_cast<int>(std::lround(npc->getMoveSpeedFold() * 1000.0f));
		return true;
	}
	if (lowerName == "adjustedwalkspeedpermille")
	{
		value = static_cast<int>(std::lround(npc->getAdjustedWalkSpeed() * 1000.0f));
		return true;
	}
	if (lowerName == "adjustedrunspeedpermille")
	{
		value = static_cast<int>(std::lround(npc->getAdjustedRunSpeed() * 1000.0f));
		return true;
	}
	if (lowerName == "equipmentmagiceffectpercent")
	{
		value = npc->equipmentAddMagicEffectPercent;
		return true;
	}
	if (lowerName == "addmagiceffectpercent")
	{
		value = npc->equipmentAddMagicEffectPercent;
		return true;
	}
	if (lowerName == "equipmentmagiceffectamount")
	{
		value = npc->equipmentAddMagicEffectAmount;
		return true;
	}
	if (lowerName == "addmagiceffectamount")
	{
		value = npc->equipmentAddMagicEffectAmount;
		return true;
	}
	if (lowerName == "equipmentmagiceffectnamecount")
	{
		value = static_cast<int>(npc->equipmentAddMagicEffectByName.size());
		return true;
	}
	if (lowerName == "equipmentmagiceffecttypecount")
	{
		value = static_cast<int>(npc->equipmentAddMagicEffectByType.size());
		return true;
	}
	if (lowerName == "equipmentmagiceffectnamepercent")
	{
		value = sumMagicEffectBonusPercent(npc->equipmentAddMagicEffectByName);
		return true;
	}
	if (lowerName == "equipmentmagiceffectnameamount")
	{
		value = sumMagicEffectBonusAmount(npc->equipmentAddMagicEffectByName);
		return true;
	}
	if (lowerName == "equipmentmagiceffecttypepercent")
	{
		value = sumMagicEffectBonusPercent(npc->equipmentAddMagicEffectByType);
		return true;
	}
	if (lowerName == "equipmentmagiceffecttypeamount")
	{
		value = sumMagicEffectBonusAmount(npc->equipmentAddMagicEffectByType);
		return true;
	}
	if (lowerName == "keepattackx")
	{
		value = npc->keepAttackPosition.x;
		return true;
	}
	if (lowerName == "keepattacky")
	{
		value = npc->keepAttackPosition.y;
		return true;
	}
	return false;
}

bool addNpcRuntimeProperty(GameManager* gameManager, const std::shared_ptr<NPC>& npc, const std::string& propertyName, int addValue)
{
	if (npc == nullptr)
	{
		return false;
	}

	const std::string lowerName = toLowerAscii(trimAscii(propertyName));
	int currentValue = 0;
	if (lowerName == "lifemax")
	{
		currentValue = npc->lifeMax;
	}
	else if (lowerName == "thewmax")
	{
		currentValue = npc->thewMax;
	}
	else if (lowerName == "manamax")
	{
		currentValue = npc->manaMax;
	}
	else if (!getNpcRuntimePropertyValue(npc, propertyName, currentValue))
	{
		return false;
	}
	if (!setNpcRuntimeAttribute(gameManager, npc, propertyName, std::to_string(currentValue + addValue)))
	{
		return false;
	}

	auto player = std::dynamic_pointer_cast<Player>(npc);
	if (player != nullptr &&
		(lowerName == "attack" || lowerName == "attack2" || lowerName == "attack3" ||
		 lowerName == "defend" || lowerName == "defence" || lowerName == "defend2" ||
		 lowerName == "defence2" || lowerName == "defend3" || lowerName == "defence3" ||
		 lowerName == "evade" || lowerName == "lifemax" || lowerName == "thewmax" ||
		 lowerName == "manamax"))
	{
		player->calInfo();
		player->limitAttribute();
	}
	return true;
}

void applyStatusMillisecondsToPlayer(GameManager* gameManager, int statusKind, int milliseconds)
{
	if (gameManager == nullptr || gameManager->player == nullptr || milliseconds <= 0)
	{
		return;
	}
	UTime duration = static_cast<UTime>(milliseconds);
	auto& player = gameManager->player;
	if (statusKind == mskPetrify && !player->petrified)
	{
		player->frozen = false;
		player->frozenLastTime = 0;
		player->frozenVisualEffect = false;
		player->petrified = true;
		player->petrifiedLastTime = duration;
		player->petrifiedVisualEffect = true;
	}
	else if (statusKind == mskPoison && !player->petrified && !player->poisoned)
	{
		player->poisoned = true;
		player->poisonedLastTime = duration;
		player->poisonedDamageTimer = 0;
		player->poisonedVisualEffect = true;
		player->poisonedBy.reset();
	}
	else if (statusKind == mskFreeze && !player->petrified && !player->frozen)
	{
		player->frozen = true;
		player->frozenLastTime = duration;
		player->frozenVisualEffect = true;
	}
	else if (statusKind == mskImmobilize && !player->petrified && !player->immobilized)
	{
		player->immobilized = true;
		player->immobilizedLastTime = duration;
		player->immobilizedVisualEffect = true;
	}
}

bool isDisabledScriptName(const std::string& scriptName)
{
	std::string text = trimAscii(scriptName);
	return text.empty() || text == "0";
}

bool playerHasGoods(GameManager* gameManager, const std::string& targetName)
{
	if (gameManager == nullptr || targetName.empty())
	{
		return false;
	}
	for (const auto& goodsInfo : gameManager->goodsManager.goodsList)
	{
		if (goodsInfo.number <= 0 || goodsInfo.iniFile.empty())
		{
			continue;
		}
		if (goodsInfo.iniFile == targetName)
		{
			return true;
		}
		if (goodsInfo.goods != nullptr && goodsInfo.goods->name == targetName)
		{
			return true;
		}
	}
	return false;
}

struct StealGoodsEntry
{
	std::string rawText;
	std::string goodsIni;
	int chance = 100;
};

int parsePercentChance(const std::string& text)
{
	std::string value = trimAscii(text);
	if (value.empty())
	{
		return 100;
	}
	char* end = nullptr;
	long parsed = std::strtol(value.c_str(), &end, 10);
	if (end == value.c_str() || !trimAscii(end).empty())
	{
		return 100;
	}
	if (parsed < 0)
	{
		return 0;
	}
	if (parsed > 100)
	{
		return 100;
	}
	return static_cast<int>(parsed);
}

std::vector<StealGoodsEntry> parseStealGoodsEntries(const std::string& bagGoods)
{
	std::vector<StealGoodsEntry> entries;
	size_t start = 0;
	while (start <= bagGoods.size())
	{
		size_t end = start;
		while (end < bagGoods.size() && bagGoods[end] != ';' && bagGoods[end] != '|')
		{
			end++;
		}

		std::string token = trimAscii(bagGoods.substr(start, end - start));
		if (!token.empty())
		{
			StealGoodsEntry entry;
			entry.rawText = token;
			entry.goodsIni = token;
			size_t colon = token.rfind(':');
			if (colon != std::string::npos)
			{
				entry.goodsIni = trimAscii(token.substr(0, colon));
				entry.chance = parsePercentChance(token.substr(colon + 1));
			}
			if (!entry.goodsIni.empty())
			{
				entries.push_back(entry);
			}
		}

		if (end >= bagGoods.size())
		{
			break;
		}
		start = end + 1;
	}
	return entries;
}

std::string serializeStealGoodsEntries(const std::vector<StealGoodsEntry>& entries, int removedIndex)
{
	std::string result;
	for (size_t i = 0; i < entries.size(); i++)
	{
		if (static_cast<int>(i) == removedIndex)
		{
			continue;
		}
		if (!result.empty())
		{
			result += ";";
		}
		result += entries[i].rawText;
	}
	return result;
}

std::string stealGoodsOptionText(const StealGoodsEntry& entry)
{
	Goods goods;
	goods.initFromIni(entry.goodsIni);
	std::string name = goods.name.empty() ? entry.goodsIni : goods.name;
	return convert::formatString("%s (%d%%)", name.c_str(), entry.chance);
}

bool rollPercentChance(Engine* engine, int chance)
{
	if (chance <= 0)
	{
		return false;
	}
	if (chance >= 100 || engine == nullptr)
	{
		return true;
	}
	return engine->getRand(99) < chance;
}

int normalizeMagicStateLevel(int level)
{
	if (level <= 0)
	{
		return 0;
	}
	if (level > MAGIC_MAX_LEVEL)
	{
		return MAGIC_MAX_LEVEL;
	}
	return level;
}

bool getMagicRuntimePropertyValue(const std::shared_ptr<Magic>& magic, const std::string& stateName, int requestedLevel, int& value)
{
	if (magic == nullptr)
	{
		return false;
	}

	const std::string lowerName = toLowerAscii(trimAscii(stateName));
	const int levelIndex = normalizeMagicStateLevel(requestedLevel);
	const MagicLevel& level = magic->level[levelIndex];
	if (lowerName == "exists" || lowerName == "isok" || lowerName == "loadsucceeded")
	{
		value = magic->loadSucceeded ? 1 : 0;
		return true;
	}
	if (lowerName == "filename" || lowerName == "hasfilename" || lowerName == "inifile" || lowerName == "hasinifile")
	{
		value = !magic->iniName.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "hastype" || lowerName == "hasmagictype")
	{
		value = !magic->type.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "hasinjurytype")
	{
		value = magic->hasInjuryType ? 1 : 0;
		return true;
	}
	if (lowerName == "spritetype")
	{
		value = magic->spriteType;
		return true;
	}
	if (lowerName == "hasspritetype")
	{
		value = magic->hasSpriteType ? 1 : 0;
		return true;
	}
	if (lowerName == "attribute")
	{
		value = magic->attribute;
		return true;
	}
	if (lowerName == "hasattribute")
	{
		value = magic->hasAttribute ? 1 : 0;
		return true;
	}
	if (lowerName == "hasscriptfile")
	{
		value = magic->hasScriptFile ? 1 : 0;
		return true;
	}
	if (lowerName == "level"
		|| lowerName == "requestedlevel"
		|| lowerName == "currentlevel"
		|| lowerName == "effectlevel")
	{
		value = levelIndex;
		return true;
	}
	if (lowerName == "maxlevel")
	{
		value = magic->maxLevel;
		return true;
	}
	if (lowerName == "count")
	{
		value = level.count;
		return true;
	}
	if (lowerName == "maxcount")
	{
		value = magic->maxCount;
		return true;
	}
	if (lowerName == "movekind")
	{
		value = level.moveKind;
		return true;
	}
	if (lowerName == "specialkind")
	{
		value = level.specialKind;
		return true;
	}
	if (lowerName == "specialkindvalue")
	{
		value = level.specialKindValue;
		return true;
	}
	if (lowerName == "specialkindmilliseconds")
	{
		value = static_cast<int>(level.specialKindMilliseconds);
		return true;
	}
	if (lowerName == "region")
	{
		value = level.region;
		return true;
	}
	if (lowerName == "speed")
	{
		value = level.speed;
		return true;
	}
	if (lowerName == "waitframe")
	{
		value = level.waitFrame;
		return true;
	}
	if (lowerName == "lifeframe")
	{
		value = level.lifeFrame;
		return true;
	}
	if (lowerName == "attackradius")
	{
		value = level.attackRadius;
		return true;
	}
	if (lowerName == "effect")
	{
		value = level.effect;
		return true;
	}
	if (lowerName == "effectext")
	{
		value = level.effectExt;
		return true;
	}
	if (lowerName == "effect2")
	{
		value = level.effect2;
		return true;
	}
	if (lowerName == "effect3")
	{
		value = level.effect3;
		return true;
	}
	if (lowerName == "effectmana")
	{
		value = level.effectMana;
		return true;
	}
	if (lowerName == "leaptimes")
	{
		value = level.leapTimes;
		return true;
	}
	if (lowerName == "leapframe")
	{
		value = level.leapFrame;
		return true;
	}
	if (lowerName == "effectreducepercentage")
	{
		value = level.effectReducePercentage;
		return true;
	}
	if (lowerName == "restoretype")
	{
		value = magic->restoreType;
		return true;
	}
	if (lowerName == "restorepercent")
	{
		value = magic->restorePercent;
		return true;
	}
	if (lowerName == "restoreprobability")
	{
		value = magic->restoreProbability;
		return true;
	}
	if (lowerName == "lifecost")
	{
		value = level.lifeCost;
		return true;
	}
	if (lowerName == "thewcost")
	{
		value = level.thewCost;
		return true;
	}
	if (lowerName == "manacost")
	{
		value = level.manaCost;
		return true;
	}
	if (lowerName == "ragecost")
	{
		value = level.rageCost;
		return true;
	}
	if (lowerName == "hasragecost")
	{
		value = level.hasRageCost ? 1 : 0;
		return true;
	}
	if (lowerName == "rangeaddrage")
	{
		value = level.rangeAddRage;
		return true;
	}
	if (lowerName == "hasrangeaddrage")
	{
		value = level.hasRangeAddRage ? 1 : 0;
		return true;
	}
	if (lowerName == "critchanceaddvalue")
	{
		value = level.critChanceAddValue;
		return true;
	}
	if (lowerName == "hascritchanceaddvalue")
	{
		value = level.hasCritChanceAddValue ? 1 : 0;
		return true;
	}
	if (lowerName == "critdamageaddpercent")
	{
		value = level.critDamageAddPercent;
		return true;
	}
	if (lowerName == "hascritdamageaddpercent")
	{
		value = level.hasCritDamageAddPercent ? 1 : 0;
		return true;
	}
	if (lowerName == "rangeaddlife")
	{
		value = level.rangeAddLife;
		return true;
	}
	if (lowerName == "rangeaddmana")
	{
		value = level.rangeAddMana;
		return true;
	}
	if (lowerName == "rangeaddthew")
	{
		value = level.rangeAddThew;
		return true;
	}
	if (lowerName == "rangefreeze" || lowerName == "rangefreezemilliseconds")
	{
		value = static_cast<int>(level.rangeFreezeMilliseconds);
		return true;
	}
	if (lowerName == "rangepoison" || lowerName == "rangepoisonmilliseconds")
	{
		value = static_cast<int>(level.rangePoisonMilliseconds);
		return true;
	}
	if (lowerName == "rangepetrify" || lowerName == "rangepetrifymilliseconds")
	{
		value = static_cast<int>(level.rangePetrifyMilliseconds);
		return true;
	}
	if (lowerName == "rangedamage")
	{
		value = level.rangeDamage;
		return true;
	}
	if (lowerName == "levelupexp")
	{
		value = level.levelupExp;
		return true;
	}
	if (lowerName == "alphablend")
	{
		value = level.alphaBlend;
		return true;
	}
	if (lowerName == "flyinglum")
	{
		value = level.flyingLum;
		return true;
	}
	if (lowerName == "vanishlum")
	{
		value = level.vanishLum;
		return true;
	}
	if (lowerName == "nospecialkindeffect")
	{
		value = magic->noSpecialKindEffect;
		return true;
	}
	if (lowerName == "keepmilliseconds")
	{
		value = static_cast<int>(magic->keepMilliseconds);
		return true;
	}
	if (lowerName == "bodyradius")
	{
		value = magic->bodyRadius;
		return true;
	}
	if (lowerName == "revivebodyradius")
	{
		value = magic->reviveBodyRadius;
		return true;
	}
	if (lowerName == "revivebodymaxcount")
	{
		value = magic->reviveBodyMaxCount;
		return true;
	}
	if (lowerName == "revivebodylifemilliseconds" || lowerName == "revivebodylifemillisecond")
	{
		value = static_cast<int>(magic->reviveBodyLifeMilliseconds);
		return true;
	}
	if (lowerName == "disableuse")
	{
		value = magic->disableUse;
		return true;
	}
	if (lowerName == "lifefulltouse")
	{
		value = magic->lifeFullToUse;
		return true;
	}
	if (lowerName == "vibratingscreen")
	{
		value = magic->vibratingScreen;
		return true;
	}
	if (lowerName == "additionaleffect")
	{
		value = magic->additionalEffect;
		return true;
	}
	if (lowerName == "attackall")
	{
		value = magic->attackAll;
		return true;
	}
	if (lowerName == "rangeeffect")
	{
		value = magic->rangeEffect;
		return true;
	}
	if (lowerName == "rangeradius")
	{
		value = magic->rangeRadius;
		return true;
	}
	if (lowerName == "rangespeedup")
	{
		value = magic->rangeSpeedUp;
		return true;
	}
	if (lowerName == "rangetimeinerval"
		|| lowerName == "rangetimeinterval"
		|| lowerName == "rangetimeinervalmilliseconds"
		|| lowerName == "rangetimeintervalmilliseconds")
	{
		value = static_cast<int>(magic->rangeTimeInterval);
		return true;
	}
	if (lowerName == "bounce")
	{
		value = magic->bounce;
		return true;
	}
	if (lowerName == "bouncehurt")
	{
		value = magic->bounceHurt;
		return true;
	}
	if (lowerName == "bouncefly")
	{
		value = magic->getLinkedLevel(levelIndex).bounceFly;
		return true;
	}
	if (lowerName == "bounceflyspeed")
	{
		value = magic->getLinkedLevel(levelIndex).bounceFlySpeed;
		return true;
	}
	if (lowerName == "bounceflyendhurt")
	{
		value = magic->getLinkedLevel(levelIndex).bounceFlyEndHurt;
		return true;
	}
	if (lowerName == "bounceflytouchhurt")
	{
		value = magic->getLinkedLevel(levelIndex).bounceFlyTouchHurt;
		return true;
	}
	if (lowerName == "magicdirectionwhenbounceflyend")
	{
		value = magic->getLinkedLevel(levelIndex).magicDirectionWhenBounceFlyEnd;
		return true;
	}
	if (lowerName == "carryuser")
	{
		value = magic->carryUser;
		return true;
	}
	if (lowerName == "carryuserspriteindex")
	{
		value = magic->carryUserSpriteIndex;
		return true;
	}
	if (lowerName == "hideuserwhencarry")
	{
		value = magic->hideUserWhenCarry;
		return true;
	}
	if (lowerName == "ball")
	{
		value = magic->ball;
		return true;
	}
	if (lowerName == "sticky")
	{
		value = magic->sticky;
		return true;
	}
	if (lowerName == "solid")
	{
		value = magic->solid;
		return true;
	}
	if (lowerName == "passthrough")
	{
		value = magic->passThrough;
		return true;
	}
	if (lowerName == "passthroughwithdestroyeffect")
	{
		value = magic->passThroughWithDestroyEffect;
		return true;
	}
	if (lowerName == "passthroughwall")
	{
		value = magic->passThroughWall;
		return true;
	}
	if (lowerName == "discardoppositemagic")
	{
		value = magic->discardOppositeMagic;
		return true;
	}
	if (lowerName == "exchangeuser")
	{
		value = magic->exchangeUser;
		return true;
	}
	if (lowerName == "regionfileloaded")
	{
		value = magic->regionFileLoaded ? 1 : 0;
		return true;
	}
	if (lowerName == "hasattackfile")
	{
		value = !magic->getLinkedLevel(levelIndex).attackFile.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "hasflymagic")
	{
		value = !magic->getLinkedLevel(levelIndex).flyMagicFile.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "flyinterval" || lowerName == "flyintervalmilliseconds")
	{
		value = static_cast<int>(magic->getLinkedLevel(levelIndex).flyInterval);
		return true;
	}
	if (lowerName == "hasexplodemagic")
	{
		value = !magic->getExplodeMagicFileForLevel(levelIndex).empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "hasnpcfile")
	{
		value = !magic->npcFile.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "hasgoodsname")
	{
		value = !magic->goodsName.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "hasregionfile")
	{
		value = !magic->regionFileName.empty() ? 1 : 0;
		return true;
	}
	return false;
}

std::string normalizeEffectMagicFilter(std::string value)
{
	value = toLowerAscii(trimAscii(value));
	std::replace(value.begin(), value.end(), '\\', '/');
	return value;
}

std::string baseNameOfEffectMagicFilter(const std::string& value)
{
	size_t slash = value.find_last_of('/');
	return slash == std::string::npos ? value : value.substr(slash + 1);
}

bool effectMagicNameEquals(const std::string& candidate, const std::string& normalizedFilter, const std::string& normalizedFilterBaseName)
{
	std::string normalizedCandidate = normalizeEffectMagicFilter(candidate);
	if (normalizedCandidate.empty())
	{
		return false;
	}
	std::string normalizedCandidateBaseName = baseNameOfEffectMagicFilter(normalizedCandidate);
	return normalizedCandidate == normalizedFilter || normalizedCandidateBaseName == normalizedFilterBaseName;
}

bool matchesEffectMagicName(const std::shared_ptr<Effect>& effect, const std::string& magicName)
{
	if (effect == nullptr)
	{
		return false;
	}
	std::string normalizedFilter = normalizeEffectMagicFilter(magicName);
	if (normalizedFilter.empty() || normalizedFilter == "*" || normalizedFilter == "all")
	{
		return true;
	}
	std::string normalizedFilterBaseName = baseNameOfEffectMagicFilter(normalizedFilter);
	return effectMagicNameEquals(effect->fileName, normalizedFilter, normalizedFilterBaseName)
		|| effectMagicNameEquals(effect->magic.iniName, normalizedFilter, normalizedFilterBaseName);
}

bool isEffectLifeExhausted(const std::shared_ptr<Effect>& effect)
{
	return effect == nullptr || (effect->result & erLifeExhaust) != 0;
}

bool isEffectVisibleActive(const std::shared_ptr<Effect>& effect)
{
	return effect != nullptr && effect->doing != ekHiding && !effect->vanishing && !isEffectLifeExhausted(effect);
}

bool isEffectActiveProjectile(const std::shared_ptr<Effect>& effect)
{
	return isEffectVisibleActive(effect) && (effect->doing == ekFlying || effect->doing == ekThrowing);
}

std::vector<std::shared_ptr<Effect>> findMatchingEffects(GameManager* gameManager, const std::string& magicName)
{
	std::vector<std::shared_ptr<Effect>> effects;
	if (gameManager == nullptr || gameManager->effectManager == nullptr)
	{
		return effects;
	}
	for (const auto& effect : gameManager->effectManager->effectList)
	{
		if (matchesEffectMagicName(effect, magicName))
		{
			effects.push_back(effect);
		}
	}
	return effects;
}

std::shared_ptr<Effect> firstMatchingEffect(const std::vector<std::shared_ptr<Effect>>& effects)
{
	for (const auto& effect : effects)
	{
		if (isEffectVisibleActive(effect))
		{
			return effect;
		}
	}
	for (const auto& effect : effects)
	{
		if (effect != nullptr)
		{
			return effect;
		}
	}
	return nullptr;
}

std::shared_ptr<Effect> firstActiveProjectileEffect(const std::vector<std::shared_ptr<Effect>>& effects)
{
	for (const auto& effect : effects)
	{
		if (isEffectActiveProjectile(effect))
		{
			return effect;
		}
	}
	return nullptr;
}

int countEffectsByDoing(const std::vector<std::shared_ptr<Effect>>& effects, int doing)
{
	int count = 0;
	for (const auto& effect : effects)
	{
		if (effect != nullptr && effect->doing == doing && !isEffectLifeExhausted(effect))
		{
			count++;
		}
	}
	return count;
}

int countActiveProjectileEffects(const std::vector<std::shared_ptr<Effect>>& effects)
{
	int count = 0;
	for (const auto& effect : effects)
	{
		if (isEffectActiveProjectile(effect))
		{
			count++;
		}
	}
	return count;
}

int countVisibleActiveEffects(const std::vector<std::shared_ptr<Effect>>& effects)
{
	int count = 0;
	for (const auto& effect : effects)
	{
		if (isEffectVisibleActive(effect))
		{
			count++;
		}
	}
	return count;
}

template <typename Predicate>
int countEffectsWhere(const std::vector<std::shared_ptr<Effect>>& effects, Predicate predicate)
{
	int count = 0;
	for (const auto& effect : effects)
	{
		if (effect != nullptr && predicate(effect))
		{
			count++;
		}
	}
	return count;
}

bool isEffectParasiticActive(const std::shared_ptr<Effect>& effect)
{
	return effect != nullptr && effect->parasiticTarget.lock() != nullptr && !isEffectLifeExhausted(effect);
}

int countLiveWeakNpcTargets(const std::vector<std::weak_ptr<NPC>>& targets)
{
	int count = 0;
	for (const auto& weakTarget : targets)
	{
		if (weakTarget.lock() != nullptr)
		{
			count++;
		}
	}
	return count;
}

bool getEffectRuntimePropertyValue(GameManager* gameManager, const std::string& magicName, const std::string& stateName, int& value)
{
	auto effects = findMatchingEffects(gameManager, magicName);
	const std::string lowerName = toLowerAscii(trimAscii(stateName));
	if (lowerName == "count" || lowerName == "totalcount")
	{
		value = static_cast<int>(effects.size());
		return true;
	}
	if (lowerName == "exists")
	{
		value = effects.empty() ? 0 : 1;
		return true;
	}
	if (lowerName == "activecount" || lowerName == "livecount" || lowerName == "visibleactivecount")
	{
		value = countVisibleActiveEffects(effects);
		return true;
	}
	if (lowerName == "projectilecount" || lowerName == "activeprojectilecount" || lowerName == "flyingcount")
	{
		value = countActiveProjectileEffects(effects);
		return true;
	}
	if (lowerName == "solidobstaclecount" || lowerName == "activesolidobstaclecount" || lowerName == "solidcount")
	{
		value = countEffectsWhere(effects, [](const std::shared_ptr<Effect>& effect) {
			return effect->isSolidObstacle();
		});
		return true;
	}
	if (lowerName == "hassolidobstacle" || lowerName == "anysolidobstacle" || lowerName == "issolidobstacleactive")
	{
		value = countEffectsWhere(effects, [](const std::shared_ptr<Effect>& effect) {
			return effect->isSolidObstacle();
		}) > 0 ? 1 : 0;
		return true;
	}
	if (lowerName == "rangespeedupactivecount" || lowerName == "rangespeedupcount")
	{
		value = countEffectsWhere(effects, [](const std::shared_ptr<Effect>& effect) {
			return effect->isRangeSpeedUpActive();
		});
		return true;
	}
	if (lowerName == "hasrangespeedup" || lowerName == "rangespeedupactive")
	{
		value = countEffectsWhere(effects, [](const std::shared_ptr<Effect>& effect) {
			return effect->isRangeSpeedUpActive();
		}) > 0 ? 1 : 0;
		return true;
	}
	if (lowerName == "parasiticactivecount" || lowerName == "activeparasiticcount")
	{
		value = countEffectsWhere(effects, [](const std::shared_ptr<Effect>& effect) {
			return isEffectParasiticActive(effect);
		});
		return true;
	}
	if (lowerName == "hasactiveparasitic" || lowerName == "anyactiveparasitic")
	{
		value = countEffectsWhere(effects, [](const std::shared_ptr<Effect>& effect) {
			return isEffectParasiticActive(effect);
		}) > 0 ? 1 : 0;
		return true;
	}
	if (lowerName == "candiscardoppositemagiccount" || lowerName == "canbediscardedbyoppositemagiccount")
	{
		value = countEffectsWhere(effects, [](const std::shared_ptr<Effect>& effect) {
			return effect->canBeDiscardedByOppositeMagic();
		});
		return true;
	}
	if (lowerName == "canexchangeuserbyoppositemagiccount" || lowerName == "canexchangeusercount")
	{
		value = countEffectsWhere(effects, [](const std::shared_ptr<Effect>& effect) {
			return effect->canExchangeUserByOppositeMagic();
		});
		return true;
	}
	if (lowerName == "throwingcount")
	{
		value = countEffectsByDoing(effects, ekThrowing);
		return true;
	}
	if (lowerName == "explodingcount")
	{
		value = countEffectsByDoing(effects, ekExploding);
		return true;
	}
	if (lowerName == "hidingcount")
	{
		int count = 0;
		for (const auto& effect : effects)
		{
			if (effect != nullptr && effect->doing == ekHiding)
			{
				count++;
			}
		}
		value = count;
		return true;
	}
	if (lowerName == "lifeexhaustcount")
	{
		int count = 0;
		for (const auto& effect : effects)
		{
			if (effect != nullptr && isEffectLifeExhausted(effect))
			{
				count++;
			}
		}
		value = count;
		return true;
	}

	auto effect = firstMatchingEffect(effects);
	if (effect == nullptr)
	{
		value = 0;
		return true;
	}
	auto activeProjectile = firstActiveProjectileEffect(effects);
	if (lowerName == "activeprojectilemapx" || lowerName == "projectilemapx")
	{
		value = activeProjectile != nullptr ? activeProjectile->position.x : 0;
		return true;
	}
	if (lowerName == "activeprojectilemapy" || lowerName == "projectilemapy")
	{
		value = activeProjectile != nullptr ? activeProjectile->position.y : 0;
		return true;
	}
	if (lowerName == "activeprojectileoffsetx" || lowerName == "projectileoffsetx")
	{
		value = activeProjectile != nullptr ? static_cast<int>(std::lround(activeProjectile->offset.x)) : 0;
		return true;
	}
	if (lowerName == "activeprojectileoffsety" || lowerName == "projectileoffsety")
	{
		value = activeProjectile != nullptr ? static_cast<int>(std::lround(activeProjectile->offset.y)) : 0;
		return true;
	}
	if (lowerName == "activeprojectileoffsetlength" || lowerName == "projectileoffsetlength")
	{
		value = activeProjectile != nullptr
			? static_cast<int>(std::lround(std::hypot(activeProjectile->offset.x, activeProjectile->offset.y)))
			: 0;
		return true;
	}
	if (lowerName == "activeprojectiledirection" || lowerName == "projectiledirection")
	{
		value = activeProjectile != nullptr ? activeProjectile->direction : 0;
		return true;
	}
	if (lowerName == "activeprojectileflyingdirectionx" || lowerName == "projectileflyingdirectionx" || lowerName == "activeprojectileflyx")
	{
		value = activeProjectile != nullptr ? activeProjectile->flyingDirection.x : 0;
		return true;
	}
	if (lowerName == "activeprojectileflyingdirectiony" || lowerName == "projectileflyingdirectiony" || lowerName == "activeprojectileflyy")
	{
		value = activeProjectile != nullptr ? activeProjectile->flyingDirection.y : 0;
		return true;
	}
	if (lowerName == "activeprojectilespeed" || lowerName == "projectilespeed")
	{
		value = activeProjectile != nullptr ? activeProjectile->speed : 0;
		return true;
	}
	if (lowerName == "activeprojectilelauncherkind" || lowerName == "projectilelauncherkind")
	{
		value = activeProjectile != nullptr ? activeProjectile->launcherKind : 0;
		return true;
	}
	if (lowerName == "activeprojectileuserisplayer" || lowerName == "projectileuserisplayer")
	{
		auto projectileUser = activeProjectile != nullptr ? std::dynamic_pointer_cast<NPC>(activeProjectile->user.lock()) : nullptr;
		value = (projectileUser != nullptr && NPC::isPlayerKind(projectileUser->kind)) ? 1 : 0;
		return true;
	}
	if (lowerName == "activeprojectilecanexchangeuser" || lowerName == "projectilecanexchangeuser")
	{
		value = (activeProjectile != nullptr && activeProjectile->canExchangeUserByOppositeMagic()) ? 1 : 0;
		return true;
	}
	if (lowerName == "activeprojectilecanbediscarded" || lowerName == "projectilecanbediscarded")
	{
		value = (activeProjectile != nullptr && activeProjectile->canBeDiscardedByOppositeMagic()) ? 1 : 0;
		return true;
	}
	if (lowerName == "activeprojectilepassthroughhitcount"
		|| lowerName == "activeprojectilepassthroughhits"
		|| lowerName == "projectilepassthroughhitcount")
	{
		value = activeProjectile != nullptr ? countLiveWeakNpcTargets(activeProjectile->passThroughHitTargets) : 0;
		return true;
	}
	if (lowerName == "doing" || lowerName == "firstdoing")
	{
		value = effect->doing;
		return true;
	}
	if (lowerName == "result" || lowerName == "firstresult")
	{
		value = static_cast<int>(effect->result);
		return true;
	}
	if (lowerName == "isprojectile" || lowerName == "firstisprojectile")
	{
		value = isEffectActiveProjectile(effect) ? 1 : 0;
		return true;
	}
	if (lowerName == "ishiding" || lowerName == "firstishiding")
	{
		value = effect->doing == ekHiding ? 1 : 0;
		return true;
	}
	if (lowerName == "islifeexhausted" || lowerName == "firstislifeexhausted")
	{
		value = isEffectLifeExhausted(effect) ? 1 : 0;
		return true;
	}
	if (lowerName == "vanishing" || lowerName == "isvanishing")
	{
		value = effect->vanishing ? 1 : 0;
		return true;
	}
	if (lowerName == "passthroughhitcount" || lowerName == "firstpassthroughhitcount" || lowerName == "passthroughhits")
	{
		value = countLiveWeakNpcTargets(effect->passThroughHitTargets);
		return true;
	}
	if (lowerName == "mapx" || lowerName == "x" || lowerName == "firstmapx")
	{
		value = effect->position.x;
		return true;
	}
	if (lowerName == "mapy" || lowerName == "y" || lowerName == "firstmapy")
	{
		value = effect->position.y;
		return true;
	}
	if (lowerName == "offsetx" || lowerName == "firstoffsetx")
	{
		value = static_cast<int>(std::lround(effect->offset.x));
		return true;
	}
	if (lowerName == "offsety" || lowerName == "firstoffsety")
	{
		value = static_cast<int>(std::lround(effect->offset.y));
		return true;
	}
	if (lowerName == "offsetlength" || lowerName == "firstoffsetlength")
	{
		value = static_cast<int>(std::lround(std::hypot(effect->offset.x, effect->offset.y)));
		return true;
	}
	if (lowerName == "direction" || lowerName == "dir")
	{
		value = effect->direction;
		return true;
	}
	if (lowerName == "flyingdirectionx" || lowerName == "flyx")
	{
		value = effect->flyingDirection.x;
		return true;
	}
	if (lowerName == "flyingdirectiony" || lowerName == "flyy")
	{
		value = effect->flyingDirection.y;
		return true;
	}
	if (lowerName == "speed")
	{
		value = effect->speed;
		return true;
	}
	if (lowerName == "launcherkind" || lowerName == "launcher")
	{
		value = effect->launcherKind;
		return true;
	}
	auto user = std::dynamic_pointer_cast<NPC>(effect->user.lock());
	if (lowerName == "userisplayer" || lowerName == "ownerisplayer" || lowerName == "casterisplayer")
	{
		value = (user != nullptr && NPC::isPlayerKind(user->kind)) ? 1 : 0;
		return true;
	}
	if (lowerName == "userkind" || lowerName == "ownerkind" || lowerName == "casterkind")
	{
		value = user != nullptr ? user->kind : -1;
		return true;
	}
	if (lowerName == "carryuseractive" || lowerName == "iscarryinguser")
	{
		value = effect->carryUserActive ? 1 : 0;
		return true;
	}
	if (lowerName == "attachednpccount" || lowerName == "attachedcount")
	{
		value = effect->getAttachedNPCCount();
		return true;
	}
	if (lowerName == "movedattachednpccount" || lowerName == "attachednpcmovedcount")
	{
		value = effect->getMovedAttachedNPCCount();
		return true;
	}
	if (lowerName == "hasattachednpc" || lowerName == "hasattachednpcs")
	{
		value = effect->hasAnyAttachedNPC() ? 1 : 0;
		return true;
	}
	if (lowerName == "hasmovedattachednpc" || lowerName == "attachednpcmoved")
	{
		value = effect->hasMovedAttachedNPC() ? 1 : 0;
		return true;
	}
	if (lowerName == "hasstickytarget" || lowerName == "stickytargetattached" || lowerName == "isstickyattached")
	{
		value = effect->hasStickyTarget() ? 1 : 0;
		return true;
	}
	if (lowerName == "firstissolidobstacle" || lowerName == "issolidobstacle")
	{
		value = effect->isSolidObstacle() ? 1 : 0;
		return true;
	}
	if (lowerName == "firstcanbediscardedbyoppositemagic" || lowerName == "canbediscardedbyoppositemagic" || lowerName == "candiscardoppositemagic")
	{
		value = effect->canBeDiscardedByOppositeMagic() ? 1 : 0;
		return true;
	}
	if (lowerName == "firstcanexchangeuserbyoppositemagic" || lowerName == "canexchangeuserbyoppositemagic" || lowerName == "canexchangeuser")
	{
		value = effect->canExchangeUserByOppositeMagic() ? 1 : 0;
		return true;
	}
	if (lowerName == "firstisrangespeedupactive" || lowerName == "israngespeedupactive")
	{
		value = effect->isRangeSpeedUpActive() ? 1 : 0;
		return true;
	}
	if (lowerName == "firsthasparasitictarget" || lowerName == "hasparasitictarget" || lowerName == "isparasiticactive")
	{
		value = isEffectParasiticActive(effect) ? 1 : 0;
		return true;
	}
	if (lowerName == "parasitictotaleffect" || lowerName == "firstparasitictotaleffect")
	{
		value = effect->parasiticTotalEffect;
		return true;
	}
	if (lowerName == "parasiticelapsedmilliseconds" || lowerName == "firstparasiticelapsedmilliseconds")
	{
		value = static_cast<int>(effect->parasiticElapsedMilliseconds);
		return true;
	}
	if (lowerName == "summonednpcalive" || lowerName == "hassummonednpc")
	{
		auto summonedNPC = effect->summonedNPC.lock();
		value = (summonedNPC != nullptr && !summonedNPC->isDying() && !summonedNPC->isHiding()) ? 1 : 0;
		return true;
	}
	if (lowerName == "transportfinished")
	{
		value = effect->transportFinished ? 1 : 0;
		return true;
	}
	if (lowerName == "controlfinished")
	{
		value = effect->controlFinished ? 1 : 0;
		return true;
	}
	return false;
}

int getResolvedGoodsEffectType(const Goods& goods)
{
	const std::string part = toLowerAscii(trimAscii(goods.part));
	if (goods.kind == gkDrug)
	{
		switch (goods.effectType)
		{
		case 1:
			return 4; // ClearFrozen
		case 2:
			return 6; // ClearPoison
		case 3:
			return 8; // ClearPetrifaction
		default:
			return 0;
		}
	}
	if (goods.kind == gkEquipment)
	{
		switch (goods.effectType)
		{
		case 1:
			if (part == "foot")
			{
				return 1; // ThewNotLoseWhenRun
			}
			if (part == "neck")
			{
				return 2; // ManaRestore
			}
			if (part == "hand")
			{
				return 3; // EnemyFrozen
			}
			return 0;
		case 2:
			return part == "hand" ? 5 : 0; // EnemyPoisoned
		case 3:
			return part == "hand" ? 7 : 0; // EnemyPetrified
		default:
			return 0;
		}
	}
	return 0;
}

bool getGoodsRuntimePropertyValue(const Goods& goods, const std::string& stateName, int& value)
{
	const std::string lowerName = toLowerAscii(trimAscii(stateName));
	if (lowerName == "exists" || lowerName == "isok" || lowerName == "loadsucceeded")
	{
		value = (!goods.sourceFileName.empty() && (!goods.name.empty() || !goods.intro.empty() || !goods.icon.empty() || !goods.image.empty())) ? 1 : 0;
		return true;
	}
	if (lowerName == "filename" || lowerName == "hasfilename" || lowerName == "sourcefilename" || lowerName == "hasinifile")
	{
		value = !goods.sourceFileName.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "hasrandattr" || lowerName == "hasrandomattributes")
	{
		value = goods.hasRandomAttributes() ? 1 : 0;
		return true;
	}
	if (lowerName == "theeffecttype" || lowerName == "goodseffecttype" || lowerName == "goodeffecttype")
	{
		value = getResolvedGoodsEffectType(goods);
		return true;
	}
	if (lowerName == "effecttype" || lowerName == "raweffecttype")
	{
		value = goods.effectType;
		return true;
	}
	if (lowerName == "costraw" || lowerName == "rawcost")
	{
		value = goods.getRawCost();
		return true;
	}
	if (lowerName == "buyprice")
	{
		value = goods.getBuyPrice();
		return true;
	}
	if (lowerName == "sellpriceactual" || lowerName == "recycleprice")
	{
		value = goods.getSellPrice();
		return true;
	}
	if (lowerName == "issellpricesetted" || lowerName == "hasexplicitsellprice")
	{
		value = goods.hasExplicitSellPrice() ? 1 : 0;
		return true;
	}
	if (lowerName == "kind")
	{
		value = goods.kind;
		return true;
	}
	if (lowerName == "cost")
	{
		value = goods.cost;
		return true;
	}
	if (lowerName == "sellprice")
	{
		value = goods.sellPrice;
		return true;
	}
	if (lowerName == "specialeffect")
	{
		value = goods.specialEffect;
		return true;
	}
	if (lowerName == "specialeffectvalue")
	{
		value = goods.specialEffectValue;
		return true;
	}
	if (lowerName == "fighterfriendhasdrugeffect")
	{
		value = goods.fighterFriendHasDrugEffect;
		return true;
	}
	if (lowerName == "followpartnerhasdrugeffect")
	{
		value = goods.followPartnerHasDrugEffect;
		return true;
	}
	if (lowerName == "coldmilliseconds")
	{
		value = static_cast<int>(goods.coldMilliSeconds);
		return true;
	}
	if (lowerName == "usercount" || lowerName == "allowedusercount")
	{
		value = static_cast<int>(goods.userNames.size());
		return true;
	}
	if (lowerName == "minuserlevel")
	{
		value = goods.minUserLevel;
		return true;
	}
	if (lowerName == "sex")
	{
		value = goods.sex;
		return true;
	}
	if (lowerName == "changemovespeedpercent")
	{
		value = goods.changeMoveSpeedPercent;
		return true;
	}
	if (lowerName == "addmagiceffectpercent")
	{
		value = goods.addMagicEffectPercent;
		return true;
	}
	if (lowerName == "addmagiceffectamount")
	{
		value = goods.addMagicEffectAmount;
		return true;
	}
	if (lowerName == "noneedtoequip")
	{
		value = goods.noNeedToEquip;
		return true;
	}
	if (lowerName == "magicdirectionwhenbeattacked")
	{
		value = goods.magicDirectionWhenBeAttacked;
		return true;
	}
	if (lowerName == "lifemax")
	{
		value = goods.lifeMax;
		return true;
	}
	if (lowerName == "thewmax")
	{
		value = goods.thewMax;
		return true;
	}
	if (lowerName == "manamax")
	{
		value = goods.manaMax;
		return true;
	}
	if (lowerName == "life")
	{
		value = goods.life;
		return true;
	}
	if (lowerName == "thew")
	{
		value = goods.thew;
		return true;
	}
	if (lowerName == "mana")
	{
		value = goods.mana;
		return true;
	}
	if (lowerName == "attack")
	{
		value = goods.attack;
		return true;
	}
	if (lowerName == "attack2")
	{
		value = goods.attack2;
		return true;
	}
	if (lowerName == "attack3")
	{
		value = goods.attack3;
		return true;
	}
	if (lowerName == "defend" || lowerName == "defence")
	{
		value = goods.defend;
		return true;
	}
	if (lowerName == "defend2" || lowerName == "defence2")
	{
		value = goods.defend2;
		return true;
	}
	if (lowerName == "defend3" || lowerName == "defence3")
	{
		value = goods.defend3;
		return true;
	}
	if (lowerName == "evade")
	{
		value = goods.evade;
		return true;
	}
	if (lowerName == "hasscript")
	{
		value = !goods.script.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "haspart")
	{
		value = !goods.part.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "hasmagicname")
	{
		value = !goods.magicName.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "hasmagiciniwhenuse")
	{
		value = !goods.magicIniWhenUse.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "hasreplacemagic")
	{
		value = !goods.replaceMagic.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "hasusereplacemagic")
	{
		value = !goods.useReplaceMagic.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "hasflyini")
	{
		value = !goods.flyIni.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "hasflyini2")
	{
		value = !goods.flyIni2.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "hasmagictousewhenbeattacked")
	{
		value = !goods.magicToUseWhenBeAttacked.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "hasaddmagiceffectname")
	{
		value = !goods.addMagicEffectName.empty() ? 1 : 0;
		return true;
	}
	if (lowerName == "hasaddmagiceffecttype")
	{
		value = !goods.addMagicEffectType.empty() ? 1 : 0;
		return true;
	}
	return false;
}
}


ScriptAPI::ScriptAPI(GameManager* _gameManager) : gameManager(_gameManager)
{
	engine = Engine::getInstance();
}

ScriptAPI::~ScriptAPI()
{
}

int ScriptAPI::getVar(const std::string& varName)
{
	return gameManager->varList.getInteger(varName);
}

void ScriptAPI::assign(const std::string& varName, int value)
{
	gameManager->varList.setInteger(varName, value);
}

void ScriptAPI::add(const std::string& varName, int value)
{
	assign(varName, getVar(varName) + value);
}

void ScriptAPI::talk(const std::string& part)
{
	setScriptPlayerKindCharacterNonFighting(gameManager);
	std::string fileName = SCRIPT_FOLDER;
	fileName += "map\\";
	fileName += gameManager->mapFolderName + "\\" + TALK_FILE;
	std::unique_ptr<char[]> s;
	int len = File::readFile(fileName, s);
	if (s == nullptr || len <= 0)
	{
		return;
	}
	INIReader ini(s);
	std::vector<std::string> talkStr;
	talkStr.resize(0);

	int talkIndex = 0;
	while (true)
	{
		std::string name = convert::formatString("%d", talkIndex + 1);

		std::string str = ini.Get(part, name, "");
		if (str.empty())
		{
			break;
		}
		talkStr.push_back(str);
		talkIndex++;
	}

	if (talkIndex <= 0)
	{
		return;
	}
	auto dialog = gameManager->menu->dialog;
	std::string head1 = "";
	std::string head2 = "";

	std::string headStr = "head";
	for (int i = 0; i < talkIndex; i++)
	{
		std::string tstr = convert::formatString("%s%d", headStr.c_str(), i + 1);
		std::string thstr = ini.Get(part, tstr, "NoHeadChange");
		if (thstr != "NoHeadChange")
		{
			if (i % 2 == 1)
			{
				head1 = thstr;
			}
			else
			{
				head2 = thstr;
			}
		}
		if (i % 2 == 1)
		{
			dialog->setHead1(head1);
		}
		else
		{
			dialog->setHead2(head2);
		}
		dialog->setTalkStr(talkStr[i]);
		dialog->visible = true;
		dialog->run();
		dialog->visible = false;
	}
}

void ScriptAPI::talk(int fromIdx, int toIdx)
{
	setScriptPlayerKindCharacterNonFighting(gameManager);
	auto talkList = gameManager->talkTextList.getTextDetails(fromIdx, toIdx);
	if (talkList.empty())
	{
		return;
	}
	auto dialog = gameManager->menu->dialog;
	for (size_t i = 0; i < talkList.size(); i++)
	{
		int portraitIdx = talkList[i].portraitIndex;
		std::string headName;
		if (portraitIdx >= 0)
		{
			headName = dialog->getHeadName(portraitIdx);
		}
		if (i % 2 == 0)
		{
			dialog->setHead1(headName);
		}
		else
		{
			dialog->setHead2(headName);
		}
		dialog->setTalkStr(talkList[i].text);
		dialog->visible = true;
		dialog->run();
		dialog->visible = false;
	}
}

void ScriptAPI::say(const std::string& str, int index)
{
	setScriptPlayerKindCharacterNonFighting(gameManager);
	ScopedScriptInputBlock inputBlock(gameManager);
	auto dialog = gameManager->menu->dialog;
	if (index >= 0)
	{
		dialog->setHead1(dialog->getHeadName(index));
	}
	else
	{
		dialog->setHead1("");
	}
	dialog->setTalkStr(str);
	dialog->visible = true;
	dialog->run();
	dialog->visible = false;
}

void ScriptAPI::fadeInEx()
{
	gameManager->weather->fadeInEx();
}

void ScriptAPI::fadeIn()
{
	gameManager->weather->fadeIn();
}

void ScriptAPI::fadeOut()
{
	gameManager->weather->fadeOut();
}

void ScriptAPI::setFadeLum(int lum)
{
	gameManager->global.data.fadeLum = lum;
	gameManager->weather->setFadeLum(lum);
}

void ScriptAPI::setMainLum(int lum)
{
	gameManager->global.data.mainLum = lum;
	gameManager->weather->setLum(lum);
}

void ScriptAPI::playMusic(const std::string& fileName)
{
	gameManager->global.data.bgmName = fileName;
	if (!gameManager->global.useWav)
	{
		auto ext = convert::extractFileExt(fileName);
		if (ext.empty() || convert::lowerCase(ext) == ".wav")
		{
			gameManager->global.data.bgmName = convert::extractFilePath(fileName) + convert::extractFileName(fileName) + ".mp3";
		}
	}
	GameLog::write("play bgm %s\n", gameManager->global.data.bgmName.c_str());
	if (strcmp(gameManager->global.data.bgmName.c_str(), gameManager->bgmName.c_str()) == 0)
	{
		return;
	}
	engine->stopBGM();
	gameManager->bgmName = gameManager->global.data.bgmName;
	if (gameManager->bgmName.empty())
	{
		return;
	}
	std::unique_ptr<char[]> s;
	std::string musicPath = resolveMediaAssetPath(MUSIC_FOLDER,
		gameManager->bgmName,
		{ ".mp3", ".ogg", ".wma", ".wav" });
	int len = 0;
	if (File::readFile(musicPath, s, len,
		static_cast<int>(AudioDecodeSafety::MaxEncodedAudioBytes)) &&
		s != nullptr && len > 0)
	{
		engine->loadBGM(s, len);
		engine->playBGM();
	}
}

void ScriptAPI::playRandomMusic(const std::string& fileNameA, const std::string& fileNameB, const std::string& fileNameC)
{
	std::vector<std::string> musicFiles;
	if (!fileNameA.empty())
	{
		musicFiles.push_back(fileNameA);
	}
	if (!fileNameB.empty())
	{
		musicFiles.push_back(fileNameB);
	}
	if (!fileNameC.empty())
	{
		musicFiles.push_back(fileNameC);
	}
	if (!musicFiles.empty())
	{
		int idx = musicFiles.size() > 1 ? engine->getRand((int)musicFiles.size() - 1) : 0;
		playMusic(musicFiles[idx]);
	}
}

void ScriptAPI::stopMusic()
{
	engine->stopBGM();
	gameManager->global.data.bgmName = "";
	gameManager->bgmName = "";
}

void ScriptAPI::playSound(const std::string& fileName)
{
	resetLastScriptSoundState(gameManager);
	if (fileName.empty())
	{
		return;
	}
	std::unique_ptr<char[]> s;
	int len = 0;
	if (File::readFile(resolveSoundAssetPath(fileName), s, len,
		static_cast<int>(AudioDecodeSafety::MaxEncodedAudioBytes)) &&
		len > 0 && s != nullptr)
	{
		int sourceType = 0;
		auto source = findCurrentScriptSoundSource(gameManager, sourceType);
		float soundX = 0.0f;
		float soundY = 0.0f;
		if (tryGetScriptSoundPosition(gameManager, source, soundX, soundY))
		{
			recordLastScriptSoundState(gameManager, source, sourceType, soundX, soundY);
			engine->playSound(s, len, soundX, soundY);
			return;
		}
		recordLastScriptSoundState(gameManager, nullptr, 0, 0.0f, 0.0f);
		engine->playSound(s, len);
	}
}

void ScriptAPI::stopSound()
{
	engine->stopAllSounds();
}

void ScriptAPI::runScript(const std::string& fileName)
{
	runScript(fileName, gameManager->mapFolderName);
}

void ScriptAPI::runScript(const std::string& fileName, const std::string& mapName)
{
	runScriptWithCapturedParent(
		fileName,
		mapName,
		0,
		false);
}

void ScriptAPI::runScriptWithCapturedParent(
	const std::string& fileName,
	const std::string& mapName,
	std::uint64_t capturedParentExecutionId,
	bool parentWasCaptured)
{
	gameManager->clearSelected();
	if (gameManager->editorRunMode)
	{
		std::vector<std::string> candidates;
		if (!mapName.empty())
		{
			candidates.push_back(
				std::string(SCRIPT_MAP_FOLDER) +
				mapName + "\\" + fileName);
		}
		candidates.push_back(
			std::string(SCRIPT_GOODS_FOLDER) +
			fileName);
		candidates.push_back(
			std::string(SCRIPT_COMMON_FOLDER) +
			fileName);

		for (const std::string& candidate : candidates)
		{
			const std::optional<std::string> normalizedPath =
				normalizedTraceVirtualPath(candidate);
			if (!normalizedPath)
			{
				GameLog::write(
					"ScriptAPI: unsafe editor-run script path %s\n",
					candidate.c_str());
				return;
			}
			for (const EditorRun::SearchRoot& root :
				gameManager->editorRunSearchRoots)
			{
				RootedResourceReader::Result read =
					readExactEditorRunResource(
						root,
						*normalizedPath,
						MaximumEditorRunScriptBytes,
						"script");
				if (read.status ==
					RootedResourceReader::Status::NotFound)
				{
					continue;
				}
				if (!read.succeeded())
				{
					GameLog::write(
						"ScriptAPI: editor-run script read failed %s status=%d\n",
						normalizedPath->c_str(),
						static_cast<int>(read.status));
					return;
				}

				ResolvedTraceScriptSource source;
				if (!makeResolvedTraceScriptSource(
						root,
						*normalizedPath,
						std::move(read),
						source))
				{
					GameLog::write(
						"ScriptAPI: editor-run script source is unavailable %s\n",
						normalizedPath->c_str());
					return;
				}
				GameLog::write(
					"run script: %s\n",
					normalizedPath->c_str());
				(void)gameManager->script.
					runResolvedTraceScriptSource(
						std::move(source),
						capturedParentExecutionId,
						parentWasCaptured);
				return;
			}
		}
		GameLog::write(
			"script: %s not found in editor-run roots\n",
			fileName.c_str());
		return;
	}

	std::unique_ptr<char[]> s;
	std::string newName = fileName;

	int len = File::readFile(SCRIPT_MAP_FOLDER + mapName + "\\" + fileName, s);
	if (len <= 0 || s == nullptr)
	{
		GameLog::write("script: %s not found\n", (SCRIPT_MAP_FOLDER + mapName + "\\" + fileName).c_str());
		s = nullptr;
		int len = File::readFile(SCRIPT_GOODS_FOLDER + fileName, s);
		if (len <= 0 || s == nullptr)
		{
			GameLog::write("script: %s not found\n", (SCRIPT_GOODS_FOLDER + fileName).c_str());
			s = nullptr;
			int len = File::readFile(SCRIPT_COMMON_FOLDER + fileName, s);
			if (len <= 0 || s == nullptr)
			{
				GameLog::write("script: %s not found\n", (SCRIPT_COMMON_FOLDER + fileName).c_str());
				return;
			}
			GameLog::write("run script: %s%s\n", SCRIPT_COMMON_FOLDER, fileName.c_str());
			gameManager->script.runScript(s, len);
			return;
		}
		GameLog::write("run script: %s%s\n", SCRIPT_GOODS_FOLDER, newName.c_str());
		gameManager->script.runScript(s, len);
		return;
	}
	GameLog::write("run script: %s%s\\%s\n", SCRIPT_MAP_FOLDER, mapName.c_str(), newName.c_str());
	gameManager->script.runScript(s, len);
}

ExactScriptExecutionResult ScriptAPI::runScriptFromExactRoot(
	const EditorRun::SearchRoot& root,
	const std::string& virtualPath)
{
	RootedResourceReader::Result source =
		readExactEditorRunResource(
			root,
			virtualPath,
			MaximumEditorRunScriptBytes,
			"script");
	return runLoadedEditorRunScript(
		root, virtualPath, std::move(source));
}

ExactScriptExecutionResult ScriptAPI::runScriptFromEditorRunRoots(
	const std::vector<EditorRun::SearchRoot>& roots,
	const std::string& virtualPath)
{
	EditorRunResourceRead source = readFirstEditorRunResource(
		roots,
		virtualPath,
		MaximumEditorRunScriptBytes,
		"script");
	if (source.root == nullptr &&
		source.result.status == RootedResourceReader::Status::NotFound)
	{
		GameLog::write(
			"ScriptAPI: editor-run entry script is missing; using an empty script %s\n",
			virtualPath.c_str());
		return {};
	}
	if (source.root == nullptr)
	{
		ExactScriptExecutionResult result;
		result.status = ExactScriptExecutionStatus::LoadFailed;
		result.message =
			"Editor-run entry script could not be read";
		return result;
	}
	return runLoadedEditorRunScript(
		*source.root,
		virtualPath,
		std::move(source.result));
}

ExactScriptExecutionResult ScriptAPI::runLoadedEditorRunScript(
	const EditorRun::SearchRoot& root,
	const std::string& virtualPath,
	RootedResourceReader::Result source)
{
	if (gameManager == nullptr)
	{
		ExactScriptExecutionResult result;
		result.status = ExactScriptExecutionStatus::LoadFailed;
		result.message =
			"Editor-run script runtime is unavailable";
		return result;
	}
	if (!source.succeeded())
	{
		ExactScriptExecutionResult result;
		result.status = ExactScriptExecutionStatus::LoadFailed;
		result.message =
			"Editor-run entry script could not be read";
		return result;
	}
	gameManager->clearSelected();
	ResolvedTraceScriptSource resolved;
	if (!makeResolvedTraceScriptSource(
			root,
			virtualPath,
			std::move(source),
			resolved))
	{
		ExactScriptExecutionResult result;
		result.status = ExactScriptExecutionStatus::LoadFailed;
		result.message =
			"Editor-run entry script source is unavailable";
		return result;
	}
	return gameManager->script.runResolvedTraceScriptSource(
		std::move(resolved));
}

void ScriptAPI::runParallelScript(const std::string& fileName, int delayMilliseconds)
{
	if (isDisabledScriptName(fileName))
	{
		return;
	}
	ScriptTask task;
	task.type = stScript;
	task.scriptName = fileName;
	task.scriptMapName = gameManager->mapFolderName;
	task.remainingMilliseconds = delayMilliseconds > 0 ? static_cast<UTime>(delayMilliseconds) : 0;
	task.traceParentCaptured = true;
	task.traceParentExecutionId =
		gameManager->script.currentExecutionId();
	gameManager->addScriptTask(task);
}

void ScriptAPI::moveScreen(int direction, int distance, int speed)
{
	if (direction < 0 || distance < 0 || speed < 0)
	{
		return;
	}
	ScopedScriptInputBlock inputBlock(gameManager);
	gameManager->camera->flyTo(direction, distance, speed);
}

void ScriptAPI::moveScreenForFrameCount(
	int direction,
	int frameCount,
	int speed)
{
	if (frameCount <= 0)
	{
		return;
	}
	ScopedScriptInputBlock inputBlock(gameManager);
	gameManager->camera->moveForFrameCount(direction, frameCount, speed);
}

void ScriptAPI::sleep(int time)
{
	if (time <= 0)
	{
		return;
	}
	ScopedScriptInputBlock inputBlock(gameManager);
	gameManager->weather->sleep(static_cast<unsigned int>(time));
}

void ScriptAPI::playMovie(const std::string& fileName)
{
	if (gameManager == nullptr)
	{
		return;
	}
	if (gameManager->video != nullptr)
	{
		gameManager->video = nullptr;
	}
	const std::string previousBgmName = gameManager->global.data.bgmName;
	stopMusic();
	GameLog::write("Play Movie %s\n", fileName.c_str());
	std::string moviePath = resolveMediaAssetPath(VIDEO_FOLDER,
		fileName,
		{ ".avi", ".mp4", ".wmv", ".mpg", ".mpeg" });
	gameManager->video = std::make_shared<VideoPlayer>(moviePath);
	gameManager->video->drawFullScreen = true;
	gameManager->video->run();

	gameManager->video = nullptr;
	if (!previousBgmName.empty())
	{
		playMusic(previousBgmName);
	}
}

void ScriptAPI::stopMovie()
{
	if (engine == nullptr || gameManager == nullptr ||
		gameManager->video == nullptr || gameManager->video->v == nullptr)
	{
		return;
	}
	engine->stopVideo(gameManager->video->v);
}

bool ScriptAPI::runOwnerWorldCommit(
	const char* operationName,
	const std::function<bool(
		const std::function<void()>& beforeMutation,
		const std::function<void()>& commitCompleted)>&
			commit,
	bool failCloseOnPartialFailure)
{
	if (!commit)
	{
		return false;
	}

	enum class CommitPhase
	{
		Preparing,
		Mutating,
		Committed
	};
	CommitPhase phase = CommitPhase::Preparing;
	const std::function<void()> beforeMutation =
		[&phase]() noexcept
		{
			if (phase == CommitPhase::Preparing)
			{
				phase = CommitPhase::Mutating;
			}
		};
	const std::function<void()> commitCompleted =
		[&phase]() noexcept
		{
			phase = CommitPhase::Committed;
		};
	try
	{
		const bool succeeded = commit(
			beforeMutation,
			commitCompleted);
		if (succeeded &&
			phase != CommitPhase::Committed)
		{
			GameLog::write(
				"ScriptAPI: %s owner-thread commit reported success without completing its primary commit\n",
				operationName != nullptr
					? operationName
					: "world");
			if (phase == CommitPhase::Mutating &&
				failCloseOnPartialFailure)
			{
				recoverFromPartialWorldFailure(
					operationName);
			}
			return false;
		}
		if (!succeeded &&
			phase == CommitPhase::Committed)
		{
			GameLog::write(
				"ScriptAPI: %s auxiliary work failed after the primary world commit; keeping the committed world\n",
				operationName != nullptr
					? operationName
					: "world");
			return true;
		}
		if (!succeeded &&
			phase == CommitPhase::Mutating &&
			failCloseOnPartialFailure)
		{
			recoverFromPartialWorldFailure(operationName);
		}
		return succeeded;
	}
	catch (const std::exception& error)
	{
		GameLog::write(
			"ScriptAPI: %s owner-thread commit threw: %s\n",
			operationName != nullptr
				? operationName
				: "world",
			error.what());
	}
	catch (...)
	{
		GameLog::write(
			"ScriptAPI: %s owner-thread commit threw an unknown exception\n",
			operationName != nullptr
				? operationName
				: "world");
	}

	if (phase == CommitPhase::Committed)
	{
		GameLog::write(
			"ScriptAPI: %s auxiliary exception occurred after the primary world commit; keeping the committed world\n",
			operationName != nullptr
				? operationName
				: "world");
		return true;
	}
	if (phase == CommitPhase::Mutating &&
		failCloseOnPartialFailure)
	{
		recoverFromPartialWorldFailure(operationName);
	}
	return false;
}

void ScriptAPI::recoverFromPartialWorldFailure(
	const char* operationName) noexcept
{
	try
	{
		GameLog::write(
			"ScriptAPI: %s commit may have partially changed the live world; discarding it and returning to title\n",
			operationName != nullptr
				? operationName
				: "world");
	}
	catch (...)
	{
	}
	try
	{
		discardPartialWorldAfterFailedCommit();
	}
	catch (...)
	{
	}
	try
	{
		if (gameManager != nullptr &&
			(engine == nullptr ||
				!engine->isApplicationQuitRequested()))
		{
			returnToTitle();
		}
	}
	catch (...)
	{
	}
}

void ScriptAPI::discardPartialWorldAfterFailedCommit() noexcept
{
	if (gameManager == nullptr)
	{
		return;
	}

	try
	{
		endRain();
	}
	catch (...)
	{
	}
	try
	{
		if (gameManager->effectManager != nullptr)
		{
			gameManager->effectManager->freeResource();
		}
	}
	catch (...)
	{
	}
	try
	{
		if (gameManager->npcManager != nullptr)
		{
			gameManager->npcManager->freeResource();
		}
	}
	catch (...)
	{
	}
	try
	{
		if (gameManager->objectManager != nullptr)
		{
			gameManager->objectManager->freeResource();
		}
	}
	catch (...)
	{
	}
	try
	{
		if (gameManager->map != nullptr)
		{
			gameManager->map->freeResource();
		}
	}
	catch (...)
	{
	}

	gameManager->global.data.mapName.clear();
	gameManager->global.data.npcName.clear();
	gameManager->global.data.objName.clear();
	gameManager->mapFolderName.clear();
	gameManager->scriptObj = nullptr;
	gameManager->scriptNPC = nullptr;
	if (gameManager->camera != nullptr)
	{
		gameManager->camera->followPlayer = false;
		gameManager->camera->followNPC.reset();
	}
	try
	{
		gameManager->clearSelected();
	}
	catch (...)
	{
	}
}

unsigned int ScriptAPI::loadingPresentationWaitMilliseconds(
	UTime currentTime,
	UTime lastPresentationTime)
{
	if (currentTime <= lastPresentationTime)
	{
		return static_cast<unsigned int>(
			LoadingPresentationFrameIntervalMilliseconds);
	}
	const UTime elapsed = currentTime - lastPresentationTime;
	if (elapsed >= LoadingPresentationFrameIntervalMilliseconds)
	{
		return 0;
	}
	return static_cast<unsigned int>(
		LoadingPresentationFrameIntervalMilliseconds - elapsed);
}

void ScriptAPI::presentSynchronousLoadingStatusFrame(
	const std::string& statusText) noexcept
{
	if (engine == nullptr || !engine->isMainThread()
		|| engine->isApplicationQuitRequested())
	{
		return;
	}

	bool frameStarted = false;
	try
	{
		_shared_image loadingImage;
		if (!statusText.empty())
		{
			loadingImage = engine->createText(
				statusText, 50, 0xFFFFFFFF);
		}

		engine->frameBegin();
		frameStarted = true;
		if (engine->isApplicationActive()
			&& engine->isFrameReady()
			&& loadingImage != nullptr)
		{
			int width = 0;
			int height = 0;
			engine->getWindowSize(width, height);
			engine->drawImage(
				loadingImage,
				width - 320,
				height - 70);
		}
		engine->frameEnd();
		frameStarted = false;
	}
	catch (...)
	{
		// Loading feedback is best-effort and must never turn a valid save into
		// a failed load. Clear any frame admitted before the renderer failed.
		if (frameStarted)
		{
			try
			{
				engine->frameEnd();
			}
			catch (...)
			{
			}
		}
		GameLog::write(
			"ScriptAPI: synchronous loading display failed\n");
	}
}

GameLoading::LoadingTaskResult ScriptAPI::runExclusiveLoadingTask(
	const std::string& statusText,
	GameLoading::ExclusiveLoadingRunner::Worker worker,
	std::function<GameLoading::LoadingTaskResult(
		const std::function<bool()>& ownerCheckpoint)>
		successFinalizer,
	const std::function<void()>&
		loadingPresentationPumpObserver)
{
	if (gameManager == nullptr || engine == nullptr)
	{
		return GameLoading::LoadingTaskResult::failure(
			"Exclusive loading is not initialized.");
	}
	if (!engine->isMainThread())
	{
		return GameLoading::LoadingTaskResult::failure(
			"Exclusive loading must be started on the SDL main thread.");
	}
	if (gameManager->inThread.load())
	{
		return GameLoading::LoadingTaskResult::failure(
			"Another exclusive loading operation is already active.");
	}

	std::vector<_shared_image> loadingImage;
	try
	{
		if (!statusText.empty())
		{
			loadingImage.push_back(
				engine->createText(statusText, 50, 0xFFFFFFFF));
			loadingImage.push_back(
				engine->createText(statusText + u8".", 50, 0xFFFFFFFF));
			loadingImage.push_back(
				engine->createText(statusText + u8"..", 50, 0xFFFFFFFF));
			loadingImage.push_back(
				engine->createText(statusText + u8"...", 50, 0xFFFFFFFF));
		}
	}
	catch (...)
	{
		return GameLoading::LoadingTaskResult::failure(
			"Exclusive loading display creation failed.",
			std::current_exception());
	}

	FunctionScopeExit loadingStateCleanup(
		[this]()
		{
			gameManager->inThread.store(false);
			try
			{
				engine->setMultiThreadedMode(false);
			}
			catch (...)
			{
			}
			try
			{
				gameManager->resetExclusiveLoadingInputState();
			}
			catch (...)
			{
			}
		});

	GameLoading::ExclusiveLoadingCompletion completion;
	bool completionReceived = false;
	std::optional<AEvent> pendingResizeEvent;
	std::unique_ptr<GameLoading::ExclusiveLoadingRunner>
		loadingRunner;
	UTime animationTime = engine->getTime();
	UTime lastPresentationTime = animationTime;
	std::size_t imageIndex = 0;
	const auto finishLoadingPresentationFrame =
		[this,
		 &loadingImage,
		 &animationTime,
		 &lastPresentationTime,
		 &imageIndex](
			bool delayBeforePresentation)
		{
			if (!engine->isFrameReady())
			{
				if (delayBeforePresentation)
				{
					engine->delay(static_cast<unsigned int>(
						LoadingPresentationFrameIntervalMilliseconds));
				}
				return;
			}
			const UTime now = engine->getTime();
			if (!loadingImage.empty() &&
				now - animationTime > 100)
			{
				animationTime = now;
				imageIndex =
					(imageIndex + 1) % loadingImage.size();
			}
			if (delayBeforePresentation)
			{
				const unsigned int waitMilliseconds =
					loadingPresentationWaitMilliseconds(
						engine->getTime(),
						lastPresentationTime);
				if (waitMilliseconds > 0)
				{
					engine->delay(waitMilliseconds);
				}
			}
			if (engine->isApplicationActive() &&
				engine->isFrameReady() &&
				!engine->isApplicationQuitRequested())
			{
				if (!loadingImage.empty())
				{
					int width = 0;
					int height = 0;
					engine->getWindowSize(width, height);
					engine->drawImage(
						loadingImage[imageIndex],
						width - 320,
						height - 70);
				}
				engine->frameEnd();
				lastPresentationTime = engine->getTime();
				return;
			}
			// Clear frame readiness even if a lifecycle change invalidated the
			// renderer between frameBegin() and this presentation boundary.
			engine->frameEnd();
		};
	try
	{
		gameManager->inThread.store(true);
		gameManager->resetExclusiveLoadingInputState();
		engine->setMultiThreadedMode(true);
		loadingRunner =
			std::make_unique<
				GameLoading::ExclusiveLoadingRunner>(
				std::move(worker));
		while (loadingRunner->poll(
			[this,
			 &pendingResizeEvent,
			 &finishLoadingPresentationFrame,
			 &loadingPresentationPumpObserver](
				GameLoading::ExclusiveLoadingRunner& activeRunner)
			{
				if (loadingPresentationPumpObserver)
				{
					loadingPresentationPumpObserver();
				}
				engine->frameBegin();
				Element::dispatchFrameGlobalInput(engine);
				if (engine->isApplicationQuitRequested())
				{
					activeRunner.latchTerminalQuit();
					activeRunner.requestCancellation();
				}

				AEvent event;
				while (engine->getEvent(event) > 0)
				{
					if (event.eventType == ET_WINDOWCLOSE)
					{
						activeRunner.latchWindowClose();
						continue;
					}
					if (event.eventType == ET_QUIT)
					{
						activeRunner.latchTerminalQuit();
						activeRunner.requestCancellation();
						engine->requestApplicationQuit();
						continue;
					}
					if (event.eventType == ET_WINDOWRESIZE)
					{
						pendingResizeEvent = event;
					}
				}

				finishLoadingPresentationFrame(true);
			},
			[&completion, &completionReceived](
				const GameLoading::ExclusiveLoadingCompletion&
					loadingCompletion)
			{
				completion = loadingCompletion;
				completionReceived = true;
			},
			[this, &loadingRunner]()
			{
				return engine->isApplicationActive() ||
					loadingRunner->isTerminalQuitLatched();
			}) ==
				GameLoading::ExclusiveLoadingPollStatus::Running)
		{
		}
		loadingRunner.reset();
	}
	catch (...)
	{
		if (loadingRunner)
		{
			completion.windowCloseRequested =
				loadingRunner->isWindowCloseLatched();
			completion.terminalQuitRequested =
				loadingRunner->isTerminalQuitLatched();
			loadingRunner->requestCancellation();
			loadingRunner.reset();
		}
		completion.taskResult =
			GameLoading::LoadingTaskResult::failure(
				"Exclusive loading owner failed.",
				std::current_exception());
		completionReceived = true;
	}

	if (!completionReceived)
	{
		completion.taskResult =
			GameLoading::LoadingTaskResult::failure(
				"Exclusive loading completed without a result.");
	}
	if (completion.terminalQuitRequested &&
		completion.taskResult.succeeded())
	{
		completion.taskResult =
			GameLoading::LoadingTaskResult::cancellation(
			"Application termination was requested.");
	}
	const std::function<bool()> ownerCheckpoint =
		[this,
		 &completion,
		 &pendingResizeEvent,
		 &lastPresentationTime,
		 &finishLoadingPresentationFrame,
		 &loadingPresentationPumpObserver]()
		{
			// This callback can run from bounded-copy cancellation checks while
			// the directory transaction mutex is held. Pump and classify
			// platform events without dispatching application handlers. A
			// throttled frame uses only the prebuilt loading text and cursor so
			// renderer-owned SDL work stays on this owner thread. If a lifecycle
			// event backgrounds the renderer, wait for foreground or termination
			// before allowing owner-thread finalization to continue.
			for (;;)
			{
				const bool presentationDue =
					engine->isApplicationActive() &&
					engine->getTime() - lastPresentationTime >=
						LoadingPresentationFrameIntervalMilliseconds;
				if (presentationDue)
				{
					if (loadingPresentationPumpObserver)
					{
						loadingPresentationPumpObserver();
					}
					engine->frameBegin();
				}
				else
				{
					engine->pumpEvents();
				}
				if (engine->isApplicationQuitRequested())
				{
					completion.terminalQuitRequested = true;
				}

				AEvent event;
				while (engine->getEvent(event) > 0)
				{
					if (event.eventType == ET_WINDOWCLOSE)
					{
						completion.windowCloseRequested = true;
						continue;
					}
					if (event.eventType == ET_QUIT)
					{
						completion.terminalQuitRequested = true;
						engine->requestApplicationQuit();
						continue;
					}
					if (event.eventType == ET_WINDOWRESIZE)
					{
						pendingResizeEvent = event;
					}
				}
				if (completion.terminalQuitRequested)
				{
					if (presentationDue)
					{
						engine->frameEnd();
					}
					return false;
				}
				if (presentationDue)
				{
					finishLoadingPresentationFrame(false);
				}
				if (engine->isApplicationActive())
				{
					return true;
				}
				engine->delay(static_cast<unsigned int>(
					LoadingPresentationFrameIntervalMilliseconds));
			}
		};
	if (completion.taskResult.succeeded() &&
		!completion.terminalQuitRequested &&
		successFinalizer)
	{
		try
		{
			if (!ownerCheckpoint())
			{
				completion.taskResult =
					GameLoading::LoadingTaskResult::cancellation(
						"Application termination was requested.");
			}
			else
			{
				completion.taskResult =
					successFinalizer(ownerCheckpoint);
			}
		}
		catch (...)
		{
			completion.taskResult =
				GameLoading::LoadingTaskResult::failure(
					"Exclusive loading finalization failed.",
					std::current_exception());
		}
	}
	if (completion.terminalQuitRequested &&
		completion.taskResult.succeeded())
	{
		completion.taskResult =
			GameLoading::LoadingTaskResult::cancellation(
				"Application termination was requested.");
	}

	// Drain one final application-event checkpoint after finalization. A quit
	// observed here happened after the commit's last cancellable boundary, so
	// preserve the completed task result while still dispatching the request
	// after loading state has been cleaned up.
	try
	{
		(void)ownerCheckpoint();
	}
	catch (...)
	{
		// The primary commit has crossed its final cancellable boundary.
		// An auxiliary event-pump failure must not make callers retry a
		// loading operation whose primary result is already committed.
		GameLog::write(
			"ScriptAPI: post-finalizer application event pump failed after the primary loading commit\n");
	}

	// A close confirmation may enter a nested UI loop. End worker mode and
	// clear all loading-era input before dispatching it exactly once.
	loadingStateCleanup.run();
	auto dispatchDeferredApplicationEvent =
		[this](
			AEvent& event,
			const char* failureMessage)
		{
			try
			{
				gameManager->dispatchApplicationEvent(event);
			}
			catch (...)
			{
				// Resize/close/quit dispatch happens only after the primary
				// loading commit and loading-state cleanup. Preserve that
				// result so a caller cannot retry an already committed load.
				GameLog::write(
					"ScriptAPI: %s\n",
					failureMessage);
			}
		};
	if (completion.terminalQuitRequested)
	{
		AEvent quitEvent(ET_QUIT, 0, 0, 0);
		dispatchDeferredApplicationEvent(
			quitEvent,
			"Deferred loading quit dispatch failed.");
	}
	else
	{
		if (pendingResizeEvent)
		{
			dispatchDeferredApplicationEvent(
				*pendingResizeEvent,
				"Deferred loading resize dispatch failed.");
		}
		if (completion.windowCloseRequested)
		{
			AEvent closeEvent(ET_WINDOWCLOSE, 0, 0, 0);
			dispatchDeferredApplicationEvent(
				closeEvent,
				"Deferred loading close dispatch failed.");
		}
	}
	return completion.taskResult;
}

void ScriptAPI::loadMapAsync(const std::string& fileName)
{
	const std::string statusText = u8"读取地图中";
	Map::PreparedLoadCandidate preparedLoad;
	std::string preparedMapFolderName;
	GameLoading::ExclusiveLoadingRunner::Worker worker =
		[this,
		 fileName,
		 &preparedLoad,
		 &preparedMapFolderName](
			const GameLoading::LoadingCancellationToken& cancellationToken)
		{
			if (cancellationToken.isCancellationRequested())
			{
				return GameLoading::LoadingTaskResult::cancellation();
			}
			preparedMapFolderName =
				resolveLegacyMapFolderName(fileName);
			const std::string fallbackMpcFolder =
				preparedMapFolderName.empty()
					? std::string()
					: "mpc\\map\\" +
						preparedMapFolderName + "\\";
			const bool valid = Map::prepareLoadCandidate(
				std::string(MAP_FOLDER) + fileName,
				preparedLoad,
				{},
				fallbackMpcFolder);
			if (cancellationToken.isCancellationRequested())
			{
				return GameLoading::LoadingTaskResult::cancellation();
			}
			return valid
				? GameLoading::LoadingTaskResult::success()
				: GameLoading::LoadingTaskResult::failure(
					"Map preparation failed.");
		};
	std::function<GameLoading::LoadingTaskResult(
		const std::function<bool()>&)> successFinalizer =
		[this,
		 fileName,
		 &preparedLoad,
		 &preparedMapFolderName](
			const std::function<bool()>& ownerCheckpoint)
		{
			if (!ownerCheckpointCanContinue(
					ownerCheckpoint))
			{
				return GameLoading::LoadingTaskResult::cancellation();
			}
			if (!loadMapWithFailurePolicy(
					fileName,
					true,
					true,
					ownerCheckpoint,
					[this, &preparedLoad](
						const std::function<void()>&
							beforeMutation,
						const std::function<bool()>&
							preparationCheckpoint)
					{
						return gameManager->map->
							commitPreparedLoadCandidate(
								std::move(preparedLoad),
								beforeMutation,
								preparationCheckpoint);
					},
					true,
					preparedMapFolderName))
			{
				if (!ownerCheckpointCanContinue(
						ownerCheckpoint))
				{
					return GameLoading::LoadingTaskResult::cancellation();
				}
				return GameLoading::LoadingTaskResult::failure(
					"Map commit failed.");
			}
			return GameLoading::LoadingTaskResult::success();
		};
	const GameLoading::LoadingTaskResult result =
		runExclusiveLoadingTask(statusText, worker, successFinalizer);
	if (!result.succeeded())
	{
		GameLog::write(
			"ScriptAPI: async map load failed: %s\n",
			result.message.c_str());
	}
}

bool ScriptAPI::loadGameAsync(int index)
{
	if (engine == nullptr ||
		engine->isApplicationQuitRequested())
	{
		return false;
	}
	if (!SaveFileManager::RecoverInterruptedSaveOperations())
	{
		GameLog::write(
			"ScriptAPI: save recovery was incomplete; attempting the selected slot normally\n");
	}
	SaveFileManager::OperationScope loadOperation;
	SaveFileManager::ScratchGenerationScope candidateCleanup(
		LoadCandidateGeneration);
	if (!candidateCleanup.valid())
	{
		GameLog::write(
			"ScriptAPI: invalid load candidate cleanup path\n");
		return false;
	}
	const SaveGenerationPreflightPolicy policy =
		createRuntimeSaveGenerationPolicy(
			*gameManager,
			RuntimeSaveGenerationPolicyMode::CompatibleLoad);
	std::string preparedDirectory;
	SaveGenerationResult preparationResult;
	PreparedSaveResources preparedResources;
	const std::string statusText = u8"读取游戏中";
	GameLoading::ExclusiveLoadingRunner::Worker worker =
		[this,
		 index,
		 &policy,
		 &preparedDirectory,
		 &preparationResult,
		 &preparedResources](
			const GameLoading::LoadingCancellationToken& cancellationToken)
		{
			if (cancellationToken.isCancellationRequested())
			{
				return GameLoading::LoadingTaskResult::cancellation();
			}
			SaveGenerationPreflightPolicy workerPolicy =
				policy;
			workerPolicy.cancellationRequested =
				[cancellationToken]()
				{
					return cancellationToken.isCancellationRequested();
				};
			preparationResult = prepareLoadGeneration(
				index,
				workerPolicy,
				preparedDirectory);
			if (preparationResult.error ==
				SaveGenerationError::Cancelled)
			{
				return GameLoading::LoadingTaskResult::cancellation(
					"Save preparation was cancelled.");
			}
			if (!preparationResult.succeeded())
			{
				return loadingFailure(
					"Save preparation failed",
					preparationResult);
			}
			const std::function<bool()> preparationCheckpoint =
				[cancellationToken]()
				{
					return !cancellationToken.
						isCancellationRequested();
				};
			return prepareSaveResources(
					gameManager,
					preparedDirectory,
					preparedResources,
					preparationCheckpoint)
				? GameLoading::LoadingTaskResult::success()
				: (cancellationToken.isCancellationRequested()
					? GameLoading::LoadingTaskResult::cancellation(
						"Save resource preparation was cancelled.")
					: GameLoading::LoadingTaskResult::failure(
						"Save resource preparation failed."));
		};
	std::function<GameLoading::LoadingTaskResult(
		const std::function<bool()>&)> successFinalizer =
		[this,
		 &policy,
		 &preparedDirectory,
		 &preparedResources](
			const std::function<bool()>& ownerCheckpoint)
		{
			PreparedSaveLoadCallbacks preparedCallbacks;
			preparedCallbacks.mapFolderName =
				preparedResources.mapFolderName;
			preparedCallbacks.commitMap =
				[this, &preparedResources](
					const std::function<void()>& beforeMutation,
					const std::function<bool()>& preparationCheckpoint)
				{
					return gameManager->map->
						commitPreparedLoadCandidate(
							std::move(preparedResources.map),
							beforeMutation,
							preparationCheckpoint);
				};
			preparedCallbacks.commitNpc =
				[this, &preparedResources](
					const std::function<bool()>& preparationCheckpoint)
				{
					return gameManager->npcManager->
						commitPreparedLoad(
							preparedResources.npc,
							true,
							{},
							preparationCheckpoint);
				};
			preparedCallbacks.commitObject =
				[this, &preparedResources](
					const std::function<bool()>& preparationCheckpoint)
				{
					return gameManager->objectManager->
						commitPreparedLoad(
							std::move(preparedResources.object),
							{},
							preparationCheckpoint);
				};
			const auto generationLoad =
				[this,
				 &preparedDirectory,
				 &preparedCallbacks](
					const std::string& generationDirectory,
					const std::function<bool()>& checkpoint)
				{
					const bool usePreparedResources =
						generationDirectory == preparedDirectory;
					return loadGameFromGeneration(
						generationDirectory,
						checkpoint,
						usePreparedResources,
						usePreparedResources,
						usePreparedResources
							? preparedCallbacks
							: PreparedSaveLoadCallbacks{});
				};
			return commitPreparedSaveGeneration(
				preparedDirectory,
				policy,
				ownerCheckpoint,
				generationLoad);
		};
	const GameLoading::LoadingTaskResult result =
		runExclusiveLoadingTask(statusText, worker, successFinalizer);
	if (!result.succeeded())
	{
		GameLog::write(
			"ScriptAPI: async save load failed: %s\n",
			result.message.c_str());
	}
	return result.succeeded();
}

void ScriptAPI::setMapPos(int x, int y)
{
	gameManager->camera->followPlayer = false;
	// Original scripts compose SetMapPos scenes for a 640x480 viewport.
	// Keep that reference view centered instead of pinning it to the top-left
	// corner when the current logical viewport is wider or taller.
	gameManager->camera->position = { x + 5, y + 15 };
	gameManager->camera->offset = { 0, 0 };
	gameManager->camera->clampToMapBounds();
	gameManager->camera->differencePosition = { 0.0f, 0.0f };
}

void ScriptAPI::setMapTrap(int idx, const std::string& trapFile)
{
	if (!Traps::isValidIndex(idx))
	{
		GameLog::write("SetMapTrap ignored invalid trap index %d", idx);
		return;
	}
	if (gameManager->mapFolderName.empty())
	{
		return;
	}
	gameManager->traps.set(gameManager->mapFolderName, idx, trapFile);
	gameManager->traps.reactivate(idx);
}

void ScriptAPI::saveMapTrap()
{
	gameManager->traps.saveDefinitions();
}

void ScriptAPI::setMapTime(int time)
{
	gameManager->global.data.mapTime = time;
	gameManager->weather->setTime(time);
}

void ScriptAPI::changeASFColor(uint8_t r, uint8_t g, uint8_t b)
{
	gameManager->global.data.asfStyle = (r << 16) | (g << 8) | b;
}

void ScriptAPI::changeMapColor(uint8_t r, uint8_t g, uint8_t b)
{
	gameManager->global.data.mpcStyle = (r << 16) | (g << 8) | b;
}

void ScriptAPI::loadObjectAsync(const std::string& fileName)
{
	const std::string statusText = u8"读取资源中";
	PreparedObjectLoad preparedLoad;
	GameLoading::ExclusiveLoadingRunner::Worker worker =
		[this, fileName, &preparedLoad](
			const GameLoading::LoadingCancellationToken& cancellationToken)
		{
			if (cancellationToken.isCancellationRequested())
			{
				return GameLoading::LoadingTaskResult::cancellation();
			}
			const bool valid =
				gameManager->objectManager->prepareLoad(
					fileName,
					preparedLoad);
			if (cancellationToken.isCancellationRequested())
			{
				return GameLoading::LoadingTaskResult::cancellation();
			}
			return valid
				? GameLoading::LoadingTaskResult::success()
				: GameLoading::LoadingTaskResult::failure(
					"Object preflight failed.");
		};
	std::function<GameLoading::LoadingTaskResult(
		const std::function<bool()>&)> successFinalizer =
		[this, fileName, &preparedLoad](
			const std::function<bool()>& ownerCheckpoint)
		{
			if (!ownerCheckpointCanContinue(
					ownerCheckpoint))
			{
				return GameLoading::LoadingTaskResult::cancellation();
			}
			if (!loadObjectWithPreparationCheckpoint(
					fileName,
					ownerCheckpoint,
					[this, &preparedLoad](
						const std::function<void()>&
							beforeMutation,
						const std::function<bool()>&
							preparationCheckpoint)
					{
						return gameManager->objectManager->
							commitPreparedLoad(
								std::move(preparedLoad),
								beforeMutation,
								preparationCheckpoint);
					}))
			{
				if (!ownerCheckpointCanContinue(
						ownerCheckpoint))
				{
					return GameLoading::LoadingTaskResult::cancellation();
				}
				return GameLoading::LoadingTaskResult::failure(
					"Object commit failed.");
			}
			return GameLoading::LoadingTaskResult::success();
		};
	const GameLoading::LoadingTaskResult result =
		runExclusiveLoadingTask(statusText, worker, successFinalizer);
	if (!result.succeeded())
	{
		GameLog::write(
			"ScriptAPI: async object load failed: %s\n",
			result.message.c_str());
	}
}

void ScriptAPI::saveObject(const std::string& fileName)
{
	if (fileName.empty())
	{
		gameManager->objectManager->save(gameManager->global.data.objName);
	}
	else
	{
		gameManager->global.data.objName = fileName;
		gameManager->objectManager->save(fileName);
	}
}

void ScriptAPI::addObject(const std::string& iniName, int x, int y, int dir)
{
	Point basePosition = gameManager->player != nullptr ? gameManager->player->getPosition() : Point{ 0, 0 };
	Point position = keepCurrentPositionWhenLegacyWildcard(x, y, basePosition);
	int direction = keepCurrentWhenLegacyWildcard(
		dir, gameManager->player != nullptr ? gameManager->player->direction : 0);
	gameManager->objectManager->addObject(iniName, position.x, position.y, direction);
}

void ScriptAPI::deleteObject(const std::string& name)
{
	std::string objName = name;
	if (name.empty())
	{
		if (gameManager->scriptObj != nullptr)
		{
			gameManager->objectManager->deleteObject(gameManager->scriptObj);
		}
		return;
	}
	gameManager->objectManager->deleteObject(objName);
}

void ScriptAPI::setObjectPosition(const std::string& name, int x, int y)
{
	auto obj = findObjectScriptTarget(gameManager, name);
	if (obj != nullptr)
	{
		obj->setPosition({ x, y });
	}
}

void ScriptAPI::setObjectOffset(const std::string& name, int x, int y)
{
	auto obj = findObjectScriptTarget(gameManager, name);
	if (obj != nullptr)
	{
		obj->setOffset({ (float)x, (float)y });
	}
}

void ScriptAPI::setObjectKind(const std::string& name, int kind)
{
	auto obj = findObjectScriptTarget(gameManager, name);
	if (obj != nullptr)
	{
		obj->setKind(kind);
	}
}

void ScriptAPI::setObjectScript(const std::string& name, const std::string& scriptFile)
{
	auto obj = findObjectScriptTarget(gameManager, name);
	if (obj != nullptr)
	{
		obj->scriptFile = scriptFile;
	}
}

void ScriptAPI::setObjectScript(const std::string& name, const std::string& scriptFile, const std::string& objFileName)
{
	updateNpcObjScriptFile(objFileName, "OBJ", "ObjName", name, "ScriptFile", scriptFile);
}

void ScriptAPI::runObjectScript(const std::string& name, bool useRightScript, bool allowPrimaryFallback)
{
	auto obj = findObjectScriptTarget(gameManager, name);
	if (obj == nullptr)
	{
		return;
	}

	bool effectiveUseRightScript = useRightScript;
	if (!effectiveUseRightScript && allowPrimaryFallback && obj->shouldUseRightScriptForPrimaryInteraction())
	{
		effectiveUseRightScript = true;
	}

	std::string scriptFile = obj->getScriptFile(effectiveUseRightScript);
	if (!scriptFile.empty())
	{
		gameManager->runObjScript(obj, scriptFile);
	}
}

bool ScriptAPI::interactNearestObject(bool useRightScript, bool running, int radius)
{
	return gameManager != nullptr
		&& gameManager->queueNearestObjectInteraction(useRightScript, running, radius);
}

void ScriptAPI::clearBody()
{
	gameManager->objectManager->clearBody();
}

void ScriptAPI::openBox(const std::string& name)
{
	auto obj = findObjectScriptTarget(gameManager, name);
	if (obj != nullptr)
	{
		obj->openBox();
	}
}

void ScriptAPI::closeBox(const std::string& name)
{
	auto obj = findObjectScriptTarget(gameManager, name);
	if (obj != nullptr)
	{
		obj->closeBox();
	}
}

void ScriptAPI::getObjectState(const std::string& name, const std::string& stateName, const std::string& varName)
{
	if (varName.empty())
	{
		return;
	}
	auto obj = findObjectScriptTarget(gameManager, name);
	int value = 0;
	getObjectRuntimePropertyValue(gameManager, obj, stateName, value);
	gameManager->varList.setInteger(varName, value);
}

void ScriptAPI::loadNPCAsync(const std::string& fileName)
{
	const std::string statusText = u8"读取资源中";
	NPCManager::PreparedLoad preparedLoad;
	GameLoading::ExclusiveLoadingRunner::Worker worker =
		[this, fileName, &preparedLoad](
			const GameLoading::LoadingCancellationToken& cancellationToken)
		{
			if (cancellationToken.isCancellationRequested())
			{
				return GameLoading::LoadingTaskResult::cancellation();
			}
			const bool valid =
				gameManager->npcManager->prepareLoad(
					fileName,
					preparedLoad);
			if (cancellationToken.isCancellationRequested())
			{
				return GameLoading::LoadingTaskResult::cancellation();
			}
			return valid
				? GameLoading::LoadingTaskResult::success()
				: GameLoading::LoadingTaskResult::failure(
					"NPC preflight failed.");
		};
	std::function<GameLoading::LoadingTaskResult(
		const std::function<bool()>&)> successFinalizer =
		[this, fileName, &preparedLoad](
			const std::function<bool()>& ownerCheckpoint)
		{
			if (!ownerCheckpointCanContinue(
					ownerCheckpoint))
			{
				return GameLoading::LoadingTaskResult::cancellation();
			}
			if (!loadNPCWithPreparationCheckpoint(
					fileName,
					ownerCheckpoint,
					[this, &preparedLoad](
						const std::function<void()>&
							beforeMutation,
						const std::function<bool()>&
							preparationCheckpoint)
					{
						return gameManager->npcManager->
							commitPreparedLoad(
								preparedLoad,
								true,
								beforeMutation,
								preparationCheckpoint);
					}))
			{
				if (!ownerCheckpointCanContinue(
						ownerCheckpoint))
				{
					return GameLoading::LoadingTaskResult::cancellation();
				}
				return GameLoading::LoadingTaskResult::failure(
					"NPC commit failed.");
			}
			return GameLoading::LoadingTaskResult::success();
		};
	const GameLoading::LoadingTaskResult result =
		runExclusiveLoadingTask(statusText, worker, successFinalizer);
	if (!result.succeeded())
	{
		GameLog::write(
			"ScriptAPI: async NPC load failed: %s\n",
			result.message.c_str());
	}
}

void ScriptAPI::saveNPC(const std::string& fileName)
{
	if (fileName.empty())
	{
		gameManager->npcManager->save(gameManager->global.data.npcName);
	}
	else
	{
		gameManager->global.data.npcName = fileName;
		gameManager->npcManager->save(fileName);
	}
}

void ScriptAPI::addNPC(const std::string& iniName, int x, int y, int dir)
{
	Point basePosition = gameManager->player != nullptr ? gameManager->player->getPosition() : Point{ 0, 0 };
	Point position = keepCurrentPositionWhenLegacyWildcard(x, y, basePosition);
	int direction = keepCurrentWhenLegacyWildcard(
		dir, gameManager->player != nullptr ? gameManager->player->direction : 0);
	gameManager->npcManager->addNPC(iniName, position.x, position.y, direction);
}

void ScriptAPI::deleteNPC(const std::string& name)
{
	gameManager->npcManager->deleteNPC(name);
}

void ScriptAPI::setNPCRes(const std::string& name, const std::string& resName)
{
	if (name.empty())
	{
		std::shared_ptr<NPC> npc = gameManager->scriptNPC;
		if (npc != nullptr)
		{
			npc->npcIni = resName;
			npc->initRes(resName);
		}
	}
	else
	{
		auto npc = gameManager->npcManager->findNPC(name);
		const std::size_t targetCount = getNamedNpcTargetCount(npc.size());
		for (size_t i = 0; i < targetCount; i++)
		{
			if (npc[i] == nullptr) continue;
			npc[i]->npcIni = resName;
			npc[i]->initRes(resName);
		}
	}
}

void ScriptAPI::setNPCScript(const std::string& name, const std::string& scriptName)
{
	if (name.empty())
	{
		std::shared_ptr<NPC> npc = gameManager->scriptNPC;
		if (npc != nullptr)
		{
			npc->scriptFile = scriptName;
		}
	}
	else
	{
		auto npc = gameManager->npcManager->findNPC(name);
		const std::size_t targetCount = getNamedNpcTargetCount(npc.size());
		for (size_t i = 0; i < targetCount; i++)
		{
			if (npc[i] == nullptr) continue;
			npc[i]->scriptFile = scriptName;
		}
	}
}

void ScriptAPI::setNPCScript(const std::string& name, const std::string& scriptName, const std::string& npcFileName)
{
	updateNpcObjScriptFile(npcFileName, "NPC", "Name", name, "ScriptFile", scriptName);
}

void ScriptAPI::setNPCDeathScript(const std::string& name, const std::string& scriptName)
{
	if (name.empty())
	{
		std::shared_ptr<NPC> npc = gameManager->scriptNPC;
		if (npc != nullptr)
		{
			npc->deathScript = scriptName;
		}
	}
	else
	{
		auto npc = gameManager->npcManager->findNPC(name);
		const std::size_t targetCount = getNamedNpcTargetCount(npc.size());
		for (size_t i = 0; i < targetCount; i++)
		{
			if (npc[i] == nullptr) continue;
			npc[i]->deathScript = scriptName;
		}
	}
}

void ScriptAPI::setNPCDeathScript(const std::string& name, const std::string& scriptName, const std::string& npcFileName)
{
	updateNpcObjScriptFile(npcFileName, "NPC", "Name", name, "DeathScript", scriptName);
}

void ScriptAPI::setAllNPCScript(const std::string& name, const std::string& scriptName)
{
	if (name.empty())
	{
		setNPCScript(name, scriptName);
		return;
	}
	for (const auto& npc : findNamedNonPlayerNpcs(gameManager, name))
	{
		if (npc != nullptr)
		{
			npc->scriptFile = scriptName;
		}
	}
}

void ScriptAPI::setAllNPCDeathScript(const std::string& name, const std::string& scriptName)
{
	if (name.empty())
	{
		setNPCDeathScript(name, scriptName);
		return;
	}
	for (const auto& npc : findNamedNonPlayerNpcs(gameManager, name))
	{
		if (npc != nullptr)
		{
			npc->deathScript = scriptName;
		}
	}
}

bool ScriptAPI::interactNearestNPC(bool useRightScript, bool running, int radius)
{
	return gameManager != nullptr
		&& gameManager->queueNearestNPCInteraction(useRightScript, running, radius);
}

void ScriptAPI::goTo(const std::string& name, int x, int y)
{
	ScopedScriptInputBlock inputBlock(gameManager);
	if (name.empty())
	{
		std::shared_ptr<NPC> npc = gameManager->scriptNPC;
		if (npc != nullptr)
		{
			Point position = keepCurrentPositionWhenLegacyWildcard(x, y, npc->getPosition());
			npc->goTo(position);
		}
	}
	else
	{
		auto npc = gameManager->npcManager->findNPC(name);
		const std::size_t targetCount = getNamedNpcTargetCount(npc.size());
		for (size_t i = 0; i < targetCount; i++)
		{
			if (npc[i] == nullptr) continue;
			Point position = keepCurrentPositionWhenLegacyWildcard(x, y, npc[i]->getPosition());
			npc[i]->goTo(position);
		}
	}
}

void ScriptAPI::goToEx(const std::string& name, int x, int y)
{
	if (name.empty())
	{
		std::shared_ptr<NPC> npc = gameManager->scriptNPC;
		if (npc != nullptr)
		{
			Point position = keepCurrentPositionWhenLegacyWildcard(x, y, npc->getPosition());
			npc->goToEx(position);
		}
	}
	else
	{
		auto npc = gameManager->npcManager->findNPC(name);
		const std::size_t targetCount = getNamedNpcTargetCount(npc.size());
		for (size_t i = 0; i < targetCount; i++)
		{
			if (npc[i] == nullptr) continue;
			Point position = keepCurrentPositionWhenLegacyWildcard(x, y, npc[i]->getPosition());
			npc[i]->goToEx(position);
		}
	}
}

void ScriptAPI::goToDir(const std::string& name, int dir, int distance)
{
	ScopedScriptInputBlock inputBlock(gameManager);
	if (name.empty())
	{
		std::shared_ptr<NPC> npc = gameManager->scriptNPC;
		if (npc != nullptr)
		{
			int direction = keepCurrentWhenLegacyWildcard(dir, npc->direction);
			npc->goToDir(direction, distanceOrZeroWhenLegacyWildcard(distance));
		}
	}
	else
	{
		auto npc = gameManager->npcManager->findNPC(name);
		const std::size_t targetCount = getNamedNpcTargetCount(npc.size());
		for (size_t i = 0; i < targetCount; i++)
		{
			if (npc[i] == nullptr) continue;
			int direction = keepCurrentWhenLegacyWildcard(dir, npc[i]->direction);
			npc[i]->goToDir(direction, distanceOrZeroWhenLegacyWildcard(distance));
		}
	}
}

void ScriptAPI::setNpcDestination(const std::string& name, int x, int y)
{
	auto setDestination = [x, y](const std::shared_ptr<NPC>& npc)
	{
		if (npc == nullptr)
		{
			return;
		}
		Point position = keepCurrentPositionWhenLegacyWildcard(x, y, npc->getPosition());
		npc->setDestinationMapPosition(position);
	};

	if (name.empty())
	{
		setDestination(gameManager->scriptNPC);
		return;
	}

	auto npcList = findNamedNonPlayerNpcs(gameManager, name);
	for (auto& npc : npcList)
	{
		setDestination(npc);
	}
}

void ScriptAPI::followNPC(const std::string& follower, const std::string& leader)
{
	if (follower.empty())
	{
		if (gameManager->scriptNPC != nullptr)
		{
			gameManager->scriptNPC->followNPC = leader;
		}
		return;
	}

	auto npcList = gameManager->npcManager->findNPC(follower);
	if (!npcList.empty() && npcList.front() != nullptr)
	{
		npcList.front()->followNPC = leader;
	}
}

void ScriptAPI::followPlayer(const std::string& follower)
{
	if (gameManager->player == nullptr) return;
	auto npcList = gameManager->npcManager->findNPC(follower);
	if (!npcList.empty() && npcList.front() != nullptr)
	{
		npcList.front()->followNPC = gameManager->player->name;
	}
}

void ScriptAPI::enableNPCAI()
{
	gameManager->global.data.NPCAI = true;
}

void ScriptAPI::disableNPCAI()
{
	gameManager->global.data.NPCAI = false;
}

void ScriptAPI::setNpcAIEnabled(const std::string& name, bool enabled)
{
	if (gameManager == nullptr || gameManager->npcManager == nullptr)
	{
		return;
	}
	if (name.empty())
	{
		std::shared_ptr<NPC> npc = gameManager->scriptNPC;
		if (npc != nullptr)
		{
			npc->setAIDisabled(!enabled);
		}
		return;
	}

	auto npcList = gameManager->npcManager->findNPC(name);
	if (!npcList.empty() && npcList.front() != nullptr)
	{
		npcList.front()->setAIDisabled(!enabled);
	}
}

void ScriptAPI::enablePartnerCombat()
{
	gameManager->global.data.PartnerCombat = true;
}

void ScriptAPI::disablePartnerCombat()
{
	gameManager->global.data.PartnerCombat = false;
}

void ScriptAPI::attackTo(const std::string& name, int x, int y)
{
	if (name.empty())
	{
		std::shared_ptr<NPC> npc = gameManager->scriptNPC;
		if (npc != nullptr)
		{
			npc->beginAttack({ x, y }, nullptr);
		}
	}
	else
	{
		auto npc = gameManager->npcManager->findNPC(name);
		const std::size_t targetCount = getNamedNpcTargetCount(npc.size());
		for (size_t i = 0; i < targetCount; i++)
		{
			if (npc[i] == nullptr) continue;
			npc[i]->beginAttack({ x, y }, nullptr);
		}
	}
}

void ScriptAPI::npcUseMagic(const std::string& name, const std::string& magicFileName, int x, int y, int level)
{
	auto magic = gameManager->magicManager.loadAttackMagic(magicFileName);
	if (magic == nullptr || !magic->loadSucceeded)
	{
		return;
	}

	if (name.empty())
	{
		std::shared_ptr<NPC> npc = gameManager->scriptNPC;
		if (npc != nullptr)
		{
			npc->useMagic(magic, { x, y }, level, nullptr);
		}
	}
	else
	{
		auto npcList = gameManager->npcManager->findNPC(name);
		const std::size_t targetCount = getNamedNpcTargetCount(npcList.size());
		for (size_t i = 0; i < targetCount; i++)
		{
			if (npcList[i] == nullptr) continue;
			npcList[i]->useMagic(magic, { x, y }, level, nullptr);
		}
	}
}

void ScriptAPI::setNPCPosition(const std::string& name, int x, int y)
{
	if (name.empty())
	{
		std::shared_ptr<NPC> npc = gameManager->scriptNPC;
		if (npc != nullptr)
		{
			Point position = keepCurrentPositionWhenLegacyWildcard(x, y, npc->getPosition());
			npc->haveAsyncDest = false;
			npc->beginStand();
			npc->setPosition(position);
			npc->setOffset({ 0, 0 });
		}
	}
	else
	{
		auto npc = gameManager->npcManager->findNPC(name);
		const std::size_t targetCount = getNamedNpcTargetCount(npc.size());
		for (size_t i = 0; i < targetCount; i++)
		{
			if (npc[i] == nullptr) continue;
			Point position = keepCurrentPositionWhenLegacyWildcard(x, y, npc[i]->getPosition());
			npc[i]->beginStand();
			npc[i]->haveAsyncDest = false;
			npc[i]->setPosition(position);
			npc[i]->setOffset({ 0, 0 });
		}
	}
}

void ScriptAPI::setNPCDir(const std::string& name, int dir)
{
	const bool allowsImmediateDirectionChange = usesTrilogyNpcRuntime(
		gameManager->global.npcRuntimeProfile);
	auto setDirection = [dir, allowsImmediateDirectionChange](
		const std::shared_ptr<NPC>& npc,
		bool restartStandingAction)
	{
		if (npc == nullptr || (!allowsImmediateDirectionChange && !npc->isStanding()))
		{
			return;
		}
		if (!allowsImmediateDirectionChange)
		{
			if (restartStandingAction)
			{
				npc->beginStand();
			}
			npc->haveAsyncDest = false;
		}
		npc->direction = keepCurrentWhenLegacyWildcard(dir, npc->direction);
	};

	if (name.empty())
	{
		setDirection(gameManager->scriptNPC, true);
	}
	else
	{
		auto npc = gameManager->npcManager->findNPC(name);
		const std::size_t targetCount = getNamedNpcTargetCount(npc.size());
		for (size_t i = 0; i < targetCount; i++)
		{
			setDirection(npc[i], false);
		}
	}
}

void ScriptAPI::setNPCKind(const std::string& name, int kind)
{
	if (name.empty())
	{
		std::shared_ptr<NPC> npc = gameManager->scriptNPC;
		if (npc != nullptr)
		{
			npc->kind = kind;
		}
	}
	else
	{
		auto npc = gameManager->npcManager->findNPC(name);
		for (size_t i = 0; i < npc.size(); i++)
		{
			if (npc[i] == nullptr) continue;
			npc[i]->kind = kind;
		}
	}
}

void ScriptAPI::setNPCLevel(const std::string& name, int level)
{
	if (name.empty())
	{
		std::shared_ptr<NPC> npc = gameManager->scriptNPC;
		if (npc != nullptr)
		{
			npc->level = level;
			npc->setLevel(level);
		}
	}
	else
	{
		auto npc = gameManager->npcManager->findNPC(name);
		const std::size_t targetCount = getNamedNpcTargetCount(npc.size());
		for (size_t i = 0; i < targetCount; i++)
		{
			if (npc[i] == nullptr) continue;
			npc[i]->level = level;
			npc[i]->setLevel(level);
		}
	}
}

void ScriptAPI::setNPCAction(const std::string& name, int action, int x, int y)
{
	ScriptNpcActionKind resolvedAction = resolveScriptNpcAction(
		gameManager->global.npcActionProfile,
		action);
	if (resolvedAction == ScriptNpcActionKind::Unknown)
	{
		GameLog::write("ScriptAPI::setNPCAction: unsupported script action %d for profile %d\n",
			action, static_cast<int>(gameManager->global.npcActionProfile));
		return;
	}

	Point destination = { x, y };
	auto applyAction = [resolvedAction, destination](const std::shared_ptr<NPC>& npc)
	{
		if (npc == nullptr)
		{
			return;
		}

		if (isFightScriptNpcAction(resolvedAction))
		{
			npc->fightState.set(true);
		}

		switch (resolvedAction)
		{
		case ScriptNpcActionKind::Stand:
		case ScriptNpcActionKind::FightStand:
			npc->beginStand();
			break;
		case ScriptNpcActionKind::Walk:
		case ScriptNpcActionKind::FightWalk:
			npc->beginWalk(destination);
			break;
		case ScriptNpcActionKind::Run:
		case ScriptNpcActionKind::FightRun:
			npc->beginRun(destination);
			break;
		case ScriptNpcActionKind::Jump:
		case ScriptNpcActionKind::FightJump:
			npc->beginJump(destination);
			break;
		case ScriptNpcActionKind::Attack:
			npc->beginAttack(destination, nullptr);
			break;
		case ScriptNpcActionKind::Magic:
			if (npc->npcMagic != nullptr)
			{
				npc->setPreparedMagicAction(npc->npcMagic, destination, 1, nullptr);
				npc->beginMagic(destination, nullptr);
			}
			break;
		case ScriptNpcActionKind::Sit:
			npc->beginSit();
			break;
		case ScriptNpcActionKind::Hurt:
			npc->beginHurt();
			break;
		case ScriptNpcActionKind::Death:
			npc->beginDie();
			break;
		case ScriptNpcActionKind::Special:
			npc->beginSpecial();
			break;
		case ScriptNpcActionKind::Unknown:
			break;
		}
	};

	if (name.empty())
	{
		applyAction(gameManager->scriptNPC);
		return;
	}

	auto npcList = gameManager->npcManager->findNPC(name);
	const std::size_t targetCount = getNamedNpcTargetCount(npcList.size());
	for (std::size_t i = 0; i < targetCount; ++i)
	{
		applyAction(npcList[i]);
	}
}

void ScriptAPI::setNPCRelation(const std::string& name, int relation)
{
	const bool updatesAllMatchingNpcRelations = usesTrilogyNpcRuntime(
		gameManager->global.npcRuntimeProfile);
	auto setRelation = [relation, updatesAllMatchingNpcRelations](const std::shared_ptr<NPC>& npc)
	{
		if (npc == nullptr)
		{
			return;
		}
		const int previousRelation = npc->relation;
		npc->relation = relation;
		if (updatesAllMatchingNpcRelations)
		{
			if ((previousRelation == nrFriendly && relation == nrHostile) ||
				(previousRelation == nrHostile && relation != nrHostile))
			{
				npc->clearCombatTargetMemory();
			}
			return;
		}
		npc->beginStand();
	};

	if (name.empty())
	{
		setRelation(gameManager->scriptNPC);
	}
	else
	{
		auto npc = gameManager->npcManager->findNPC(name);
		for (size_t i = 0; i < npc.size(); i++)
		{
			setRelation(npc[i]);
		}
	}
}

void ScriptAPI::setNPCActionType(const std::string& name, int strollIntent)
{
	if (name.empty())
	{
		std::shared_ptr<NPC> npc = gameManager->scriptNPC;
		if (npc != nullptr)
		{
			npc->strollIntent = strollIntent;
		}
	}
	else
	{
		auto npc = gameManager->npcManager->findNPC(name);
		const std::size_t targetCount = getNamedNpcTargetCount(npc.size());
		for (size_t i = 0; i < targetCount; i++)
		{
			if (npc[i] == nullptr) continue;
			npc[i]->strollIntent = strollIntent;
		}
	}
}

void ScriptAPI::setNPCActionFile(const std::string& name, int action, const std::string& fileName)
{
	const int actionSlot = resolveScriptNpcActionFileSlot(
		gameManager->global.npcActionProfile,
		action);
	if (actionSlot < 0)
	{
		return;
	}
	if (name.empty())
	{
		std::shared_ptr<NPC> npc = gameManager->scriptNPC;
		if (npc != nullptr)
		{
			npc->loadActionFile(fileName, actionSlot);
		}
	}
	else
	{
		auto npc = gameManager->npcManager->findNPC(name);
		const std::size_t targetCount = getNamedNpcTargetCount(npc.size());
		for (size_t i = 0; i < targetCount; i++)
		{
			if (npc[i] == nullptr) continue;
			npc[i]->loadActionFile(fileName, actionSlot);
		}
	}
}

void ScriptAPI::npcSpecialAction(const std::string& name, const std::string& fileName)
{
	const bool usesFirstMatchingNamedNpc = usesTrilogyNpcRuntime(
		gameManager->global.npcRuntimeProfile);
	runNpcSpecialActionForTargets(gameManager,
		name,
		fileName,
		!usesFirstMatchingNamedNpc);
}

void ScriptAPI::npcSpecialActionNonBlocking(const std::string& name, const std::string& fileName)
{
	runNpcSpecialActionForTargets(gameManager,
		name,
		fileName,
		false);
}

void ScriptAPI::npcSpecialActionEx(const std::string& name, const std::string& fileName)
{
	const bool usesFirstMatchingNamedNpc = usesTrilogyNpcRuntime(
		gameManager->global.npcRuntimeProfile);
	const bool previousCanInput = gameManager->global.data.canInput;
	if (usesFirstMatchingNamedNpc)
	{
		gameManager->global.data.canInput = false;
	}
	runNpcSpecialActionForTargets(gameManager,
		name,
		fileName,
		true);
	if (usesFirstMatchingNamedNpc)
	{
		gameManager->global.data.canInput = previousCanInput;
	}
}

void ScriptAPI::changeLife(const std::string& name, int value)
{
	auto npc = gameManager->npcManager->findNPC(name);
	const std::size_t targetCount = getNamedNpcTargetCount(npc.size());
	for (size_t i = 0; i < targetCount; i++)
	{
		if (npc[i] == nullptr) continue;
		auto maxValue = npc[i]->getLifeMax();
		npc[i]->life = maxValue * value / 100;
	}
}

void ScriptAPI::changeMana(const std::string& name, int value)
{
	auto npc = gameManager->npcManager->findNPC(name);
	const std::size_t targetCount = getNamedNpcTargetCount(npc.size());
	for (size_t i = 0; i < targetCount; i++)
	{
		if (npc[i] == nullptr) continue;
		auto maxValue = npc[i]->getManaMax();
		npc[i]->mana = maxValue * value / 100;
	}
}

void ScriptAPI::changeThew(const std::string& name, int value)
{
	auto npc = gameManager->npcManager->findNPC(name);
	const std::size_t targetCount = getNamedNpcTargetCount(npc.size());
	for (size_t i = 0; i < targetCount; i++)
	{
		if (npc[i] == nullptr) continue;
		auto maxValue = npc[i]->getThewMax();
		npc[i]->thew = maxValue * value / 100;
	}
}

void ScriptAPI::getNpcState(const std::string& name, const std::string& stateName, const std::string& varName)
{
	if (varName.empty())
	{
		return;
	}
	auto npc = findNPCForStateReadback(gameManager, name);
	int value = npc.empty() ? 0 : getNpcStateValue(npc[0], stateName);
	gameManager->varList.setInteger(varName, value);
}

void ScriptAPI::addNpcProperty(const std::string& name, const std::string& propertyName, int value)
{
	auto npc = gameManager->npcManager->findNPC(name);
	for (size_t i = 0; i < npc.size(); i++)
	{
		addNpcRuntimeProperty(gameManager, npc[i], propertyName, value);
	}
}

void ScriptAPI::addKindValue(const std::string& name, int value)
{
	auto npc = gameManager->npcManager->findNPC(name);
	const std::size_t targetCount = getNamedNpcTargetCount(npc.size());
	for (size_t i = 0; i < targetCount; i++)
	{
		if (npc[i] == nullptr) continue;
		npc[i]->kindValue = clampNpcKindValue(npc[i]->kindValue + value, npc[i]->kindValueMax);
	}
}

void ScriptAPI::setMapNpcAttr(const std::string& name, const std::string& attributes, const std::string& npcFileName)
{
	auto assignments = parseNpcAttributeAssignments(attributes);
	if (assignments.empty())
	{
		return;
	}
	updateNpcObjAttributes(npcFileName, "NPC", "Name", name, assignments);
	if (isCurrentNpcFile(gameManager, npcFileName))
	{
		applyNpcRuntimeAttributes(gameManager, name, assignments);
	}
}

void ScriptAPI::setNpcTalkContent(const std::string& name, const std::string& content)
{
	auto npc = gameManager->npcManager->findNPC(name);
	const std::size_t targetCount = getNamedNpcTargetCount(npc.size());
	for (size_t i = 0; i < targetCount; i++)
	{
		if (npc[i] == nullptr) continue;
		npc[i]->talkContent = content;
	}
}

void ScriptAPI::setNpcTalkContent(const std::string& name, const std::string& content, const std::string& npcFileName)
{
	std::vector<NpcAttributeAssignment> assignments = { { "TalkContent", content } };
	updateNpcObjAttributes(npcFileName, "NPC", "Name", name, assignments);
	if (isCurrentNpcFile(gameManager, npcFileName))
	{
		applyNpcRuntimeAttributes(gameManager, name, assignments);
	}
}

void ScriptAPI::talkSelfTip(const std::string& name, const std::string& message, const std::string& appendText)
{
	std::string text = message + appendText;
	if (!name.empty())
	{
		text = name + ": " + text;
	}
	showSystemMessage(text, 3000);
}

void ScriptAPI::setAllNpcIsEnemy()
{
	for (size_t i = 0; i < gameManager->npcManager->npcList.size(); i++)
	{
		auto npc = gameManager->npcManager->npcList[i];
		if (npc == nullptr)
		{
			continue;
		}
		npc->kind = nkBattle;
		npc->relation = nrHostile;
		npc->beginStand();
	}
}

void ScriptAPI::setDropIni(const std::string& name, const std::string& fileName)
{
	auto npc = gameManager->npcManager->findNPC(name);
	const std::size_t targetCount = getNamedNpcTargetCount(npc.size());
	for (size_t i = 0; i < targetCount; i++)
	{
		if (npc[i] == nullptr) continue;
		npc[i]->dropIni = fileName;
	}
}

void ScriptAPI::enableDrop()
{
	gameManager->global.data.dropDisabled = false;
}

void ScriptAPI::disableDrop()
{
	gameManager->global.data.dropDisabled = true;
}

void ScriptAPI::changeFlyIni(const std::string& name, const std::string& magicName)
{
	auto npc = findNamedNonPlayerNpcs(gameManager, name);
	for (size_t i = 0; i < npc.size(); i++)
	{
		if (npc[i] == nullptr) continue;
		npc[i]->flyIni = magicName;
		npc[i]->npcMagic = gameManager->magicManager.loadAttackMagic(magicName);
		npc[i]->rebuildAttackOptions();
	}
}

void ScriptAPI::changeFlyIni2(const std::string& name, const std::string& magicName)
{
	auto npc = findNamedNonPlayerNpcs(gameManager, name);
	for (size_t i = 0; i < npc.size(); i++)
	{
		if (npc[i] == nullptr) continue;
		npc[i]->flyIni2 = magicName;
		npc[i]->npcMagic2 = gameManager->magicManager.loadAttackMagic(magicName);
		npc[i]->rebuildAttackOptions();
	}
}

void ScriptAPI::addFlyInis(const std::string& name, const std::string& magicName, int distance)
{
	auto npc = findNamedNonPlayerNpcs(gameManager, name);
	for (size_t i = 0; i < npc.size(); i++)
	{
		if (npc[i] == nullptr || magicName.empty()) continue;
		if (npc[i]->flyInis.empty())
		{
			npc[i]->flyInis = convert::formatString("%s:%d;", magicName.c_str(), distance);
		}
		else
		{
			if (npc[i]->flyInis.back() != ';')
			{
				npc[i]->flyInis += ";";
			}
			npc[i]->flyInis += convert::formatString("%s:%d;", magicName.c_str(), distance);
		}
		npc[i]->rebuildAttackOptions();
	}
}

void ScriptAPI::addNpcMagic(const std::string& name, const std::string& magicName)
{
	auto npcList = gameManager->npcManager->findNPC(name);
	const std::size_t targetCount = getNamedNpcTargetCount(npcList.size());
	for (size_t i = 0; i < targetCount; i++)
	{
		auto npc = npcList[i];
		if (npc == nullptr || magicName.empty() || npcHasConfiguredMagic(npc, magicName))
		{
			continue;
		}
		if (!npc->flyInis.empty() && npc->flyInis.back() != ';')
		{
			npc->flyInis += ";";
		}
		npc->flyInis += convert::formatString("%s:0;", magicName.c_str());
		npc->rebuildAttackOptions();
	}
}

void ScriptAPI::setKeepAttack(const std::string& name, int x, int y)
{
	auto npc = findNamedNonPlayerNpcs(gameManager, name);
	for (size_t i = 0; i < npc.size(); i++)
	{
		if (npc[i] == nullptr) continue;
		npc[i]->keepAttackPosition = { x, y };
	}
}

void ScriptAPI::showSignalTip(const std::string& name, int signalIndex, const std::string& signalType)
{
	auto npc = gameManager->npcManager->findNPC(name);
	const std::size_t targetCount = getNamedNpcTargetCount(npc.size());
	for (size_t i = 0; i < targetCount; i++)
	{
		if (npc[i] == nullptr) continue;
		npc[i]->isSignalShow = true;
		npc[i]->signalIndex = signalIndex;
		npc[i]->signalType = signalType;
		npc[i]->resetSignalImage();
	}
}

void ScriptAPI::setSignalTipHidden(const std::string& name)
{
	auto npc = gameManager->npcManager->findNPC(name);
	const std::size_t targetCount = getNamedNpcTargetCount(npc.size());
	for (size_t i = 0; i < targetCount; i++)
	{
		if (npc[i] == nullptr) continue;
		npc[i]->isSignalShow = false;
	}
}

void ScriptAPI::loadPlayer(int index)
{
	gameManager->player->load(index);
}

void ScriptAPI::loadPlayer(const std::string& snapshotKey)
{
	loadPlayer(snapshotIndexFromKey(snapshotKey));
}

void ScriptAPI::savePlayer(int index)
{
	gameManager->player->save(index);
}

void ScriptAPI::savePlayer(const std::string& snapshotKey)
{
	savePlayer(snapshotIndexFromKey(snapshotKey));
}

void ScriptAPI::setPlayerPosition(int x, int y)
{
	auto target = getScriptPlayerKindCharacter(gameManager);
	if (target == nullptr)
	{
		return;
	}
	Point position = keepCurrentPositionWhenLegacyWildcard(x, y, target->getPosition());
	if (target == gameManager->player)
	{
		gameManager->player->forceBeginStand();
		gameManager->player->setPosition(position, false);
	}
	else
	{
		target->beginStand();
		target->setPosition(position);
	}
	target->setOffset({ 0, 0 });
	target->haveAsyncDest = false;
	finishScriptCharacterPositionChange(gameManager);
}

void ScriptAPI::setPlayerPosition(const std::string& name, int x, int y)
{
	if (name.empty())
	{
		return;
	}
	auto targets = gameManager->npcManager->findNPC(name);
	if (targets.empty() || targets.front() == nullptr)
	{
		return;
	}
	auto& target = targets.front();
	Point position = keepCurrentPositionWhenLegacyWildcard(x, y, target->getPosition());
	if (target == gameManager->player)
	{
		gameManager->player->forceBeginStand();
		gameManager->player->setPosition(position, false);
	}
	else
	{
		target->beginStand();
		target->setPosition(position);
	}
	target->haveAsyncDest = false;
	target->setOffset({ 0, 0 });
	finishScriptCharacterPositionChange(gameManager);
}

void ScriptAPI::setPlayerDir(int dir)
{
	gameManager->player->direction = keepCurrentWhenLegacyWildcard(dir, gameManager->player->direction);
}

void ScriptAPI::setPlayerScn(bool snapCamera)
{
	gameManager->camera->followPlayer = true;
	auto target = getScriptPlayerKindCharacter(gameManager);
	if (target != nullptr && target != gameManager->player)
	{
		gameManager->camera->followNPC = target;
	}
	else
	{
		gameManager->camera->followNPC = gameManager->player;
	}
	if (snapCamera)
	{
		// Snap after switching the follow target so later script camera moves start correctly.
		gameManager->camera->snapToFollowTarget();
		gameManager->camera->differencePosition = { 0.0, 0.0 };
	}
}

void ScriptAPI::setPlayerLum(unsigned char lum)
{
	int clampedLum = std::clamp<int>(lum, 0, 31);
	gameManager->player->lum = clampedLum;
	auto npc = gameManager->npcManager->findPlayerNPC();
	if (npc != nullptr)
	{
		npc->lum = clampedLum;
	}
}

void ScriptAPI::setLevelFile(const std::string& fileName)
{
	auto target = getScriptPlayerKindCharacter(gameManager);
	if (target == nullptr)
	{
		return;
	}
	if (target == gameManager->player)
	{
		gameManager->player->levelIni = fileName;
	}
	else
	{
		target->npcLevelIni = fileName;
	}
	target->loadLevel(fileName);
}

void ScriptAPI::setMagicLevel(const std::string& magicName, int level)
{
	gameManager->magicManager.setPrimaryMagicLevel(magicName, level);
}

int ScriptAPI::getPlayerMagicLevel(const std::string& magicName)
{
	MagicInfo* m = gameManager->magicManager.findPrimaryMagic(magicName);
	return m != nullptr ? m->level : 0;
}

void ScriptAPI::getPlayerMagicLevel(const std::string& magicName, const std::string& varName)
{
	assign(varName, getPlayerMagicLevel(magicName));
}

void ScriptAPI::getMagicState(const std::string& magicName, const std::string& stateName, const std::string& varName, int level)
{
	if (varName.empty())
	{
		return;
	}
	if (gameManager == nullptr)
	{
		return;
	}

	const std::string normalizedStateName = toLowerAscii(trimAscii(stateName));
	if (normalizedStateName == "currentlevel" || normalizedStateName == "effectlevel")
	{
		MagicInfo* magicInfo = gameManager->magicManager.findPrimaryMagic(magicName);
		if (magicInfo != nullptr && magicInfo->magic != nullptr)
		{
			assign(varName, magicInfo->level);
			return;
		}
		assign(varName, normalizeMagicStateLevel(level));
		return;
	}
	if (normalizedStateName == "iteminfo" || normalizedStateName == "hasiteminfo")
	{
		MagicInfo* magicInfo = gameManager->magicManager.findPrimaryMagic(magicName);
		assign(varName, (magicInfo != nullptr && magicInfo->magic != nullptr) ? 1 : 0);
		return;
	}

	auto magic = gameManager->magicManager.loadAttackMagic(magicName);
	int value = 0;
	if (!getMagicRuntimePropertyValue(magic, stateName, level, value))
	{
		value = 0;
	}
	assign(varName, value);
}

void ScriptAPI::getEffectState(const std::string& magicName, const std::string& stateName, const std::string& varName)
{
	if (varName.empty())
	{
		return;
	}
	if (gameManager == nullptr)
	{
		assign(varName, 0);
		return;
	}

	int value = 0;
	if (!getEffectRuntimePropertyValue(gameManager, magicName, stateName, value))
	{
		value = 0;
	}
	assign(varName, value);
}

void ScriptAPI::getMapState(int x, int y, const std::string& stateName, const std::string& varName)
{
	if (varName.empty())
	{
		return;
	}
	if (gameManager == nullptr || gameManager->map == nullptr || gameManager->map->data == nullptr)
	{
		assign(varName, 0);
		return;
	}

	auto map = gameManager->map;
	Point position{ x, y };
	std::string lowerName = toLowerAscii(trimAscii(stateName));
	bool inMap = map->isInMap(position);
	int value = 0;
	if (lowerName == "hasmap")
	{
		value = 1;
	}
	else if (lowerName == "width" || lowerName == "mapwidth")
	{
		value = map->data->head.width;
	}
	else if (lowerName == "height" || lowerName == "mapheight")
	{
		value = map->data->head.height;
	}
	else if (lowerName == "isinmap" || lowerName == "exists")
	{
		value = inMap ? 1 : 0;
	}
	else if (lowerName == "canwalk" || lowerName == "walkable")
	{
		value = inMap && map->canWalk(position) ? 1 : 0;
	}
	else if (lowerName == "canfly" || lowerName == "flyable")
	{
		value = inMap && map->canFly(position) ? 1 : 0;
	}
	else if (lowerName == "canjump" || lowerName == "jumpable")
	{
		value = inMap && map->canJump(position) ? 1 : 0;
	}
	else if (lowerName == "canpass" || lowerName == "passable")
	{
		value = inMap && map->canPass(position) ? 1 : 0;
	}
	else if (lowerName == "hassolideffect"
		|| lowerName == "solideffect"
		|| lowerName == "issolideffect"
		|| lowerName == "solidobstacle")
	{
		value = inMap && gameManager->effectManager != nullptr && gameManager->effectManager->hasSolidEffectAt(position) ? 1 : 0;
	}
	else if (lowerName == "obstacle" || lowerName == "obstaclevalue")
	{
		value = inMap ? static_cast<int>(map->data->tile[y][x].obstacle) : -1;
	}
	else if (lowerName == "trap" || lowerName == "trapindex")
	{
		value = inMap ? static_cast<int>(map->data->tile[y][x].trap) : 0;
	}
	else if (lowerName == "objectcount" || lowerName == "objcount")
	{
		value = inMap && y < static_cast<int>(map->dataMap.tile.size()) && x < static_cast<int>(map->dataMap.tile[y].size())
			? static_cast<int>(map->dataMap.tile[y][x].objList.size())
			: 0;
	}
	else if (lowerName == "npccount")
	{
		value = inMap && y < static_cast<int>(map->dataMap.tile.size()) && x < static_cast<int>(map->dataMap.tile[y].size())
			? static_cast<int>(map->dataMap.tile[y][x].npcList.size())
			: 0;
	}
	else if (lowerName == "stepnpccount")
	{
		value = inMap && y < static_cast<int>(map->dataMap.tile.size()) && x < static_cast<int>(map->dataMap.tile[y].size())
			? static_cast<int>(map->dataMap.tile[y][x].stepNPCList.size())
			: 0;
	}
	assign(varName, value);
}

void ScriptAPI::getLeechcraftDifference(const std::string& npcName, const std::string& varName)
{
	if (gameManager == nullptr)
	{
		return;
	}
	if (gameManager->npcManager == nullptr)
	{
		assign(varName, 0);
		return;
	}

	auto npcList = gameManager->npcManager->findNPC(npcName);
	std::shared_ptr<NPC> targetNPC = nullptr;
	for (const auto& npc : npcList)
	{
		if (npc != nullptr)
		{
			targetNPC = npc;
			break;
		}
	}
	if (targetNPC == nullptr)
	{
		GameLog::write("GetLeechcraftDifference cannot find NPC %s.\n", npcName.c_str());
		assign(varName, 0);
		return;
	}

	int requiredLeechcraft = std::max(0, targetNPC->leechcraft);
	int playerLeechcraft = std::max(0, gameManager->varList.getInteger("yiliao"));
	int result = playerLeechcraft >= requiredLeechcraft ? -1 : requiredLeechcraft - playerLeechcraft;
	assign(varName, result);
}

void ScriptAPI::moveMagic(const std::string& magicName, int position)
{
	MagicInfo* m = gameManager->magicManager.findMagic(magicName);
	if (m != nullptr)
	{
		int fromIndex = -1;
		for (int i = 0; i < gameManager->magicManager.listLength(); i++)
		{
			if (&gameManager->magicManager.magicList[i] == m)
			{
				fromIndex = i;
				break;
			}
		}
		if (fromIndex >= 0)
		{
			int toIndex = gameManager->magicManager.bottomIndex(position - 1);
			if (gameManager->magicManager.isBottomIndex(toIndex))
			{
				gameManager->magicManager.exchange(fromIndex, toIndex);
				gameManager->magicManager.updateMenu(fromIndex);
				gameManager->magicManager.updateMenu(toIndex);
			}
		}
	}
}

void ScriptAPI::setPlayerLevel(int level)
{
	gameManager->player->setLevel(level);
}

void ScriptAPI::setPlayerState(int state)
{
	gameManager->player->fightState.set(state != 0);
}

void ScriptAPI::enableRun()
{
	gameManager->player->setRunDisabled(false);
}

void ScriptAPI::disableRun()
{
	gameManager->player->setRunDisabled(true);
}

void ScriptAPI::enableJump()
{
	gameManager->player->setJumpDisabled(false);
}

void ScriptAPI::disableJump()
{
	gameManager->player->setJumpDisabled(true);
}

void ScriptAPI::enableFight()
{
	gameManager->player->setFightDisabled(false);
}

void ScriptAPI::disableFight()
{
	gameManager->player->setFightDisabled(true);
}

void ScriptAPI::playerGoto(int x, int y)
{
	auto target = getScriptPlayerKindCharacter(gameManager);
	if (target == nullptr)
	{
		return;
	}
	ScopedScriptInputBlock inputBlock(gameManager);
	Point position = keepCurrentPositionWhenLegacyWildcard(x, y, target->getPosition());
	target->goTo(position);
}

void ScriptAPI::playerGotoEx(int x, int y)
{
	auto target = getScriptPlayerKindCharacter(gameManager);
	if (target == nullptr)
	{
		return;
	}
	Point position = keepCurrentPositionWhenLegacyWildcard(x, y, target->getPosition());
	target->goToEx(position);
}

void ScriptAPI::playerRunTo(int x, int y)
{
	auto target = getScriptPlayerKindCharacter(gameManager);
	if (target == nullptr)
	{
		return;
	}
	ScopedScriptInputBlock inputBlock(gameManager);
	Point position = keepCurrentPositionWhenLegacyWildcard(x, y, target->getPosition());
	target->runTo(position);
}

void ScriptAPI::playerRunToEx(int x, int y)
{
	auto target = getScriptPlayerKindCharacter(gameManager);
	if (target == nullptr)
	{
		return;
	}
	Point position = keepCurrentPositionWhenLegacyWildcard(x, y, target->getPosition());
	target->destGE.reset();
	if (target->getPosition() != position)
	{
		target->beginRun(position);
	}
}

void ScriptAPI::playerJumpTo(int x, int y)
{
	auto target = getScriptPlayerKindCharacter(gameManager);
	if (target == nullptr)
	{
		return;
	}
	ScopedScriptInputBlock inputBlock(gameManager);
	Point position = keepCurrentPositionWhenLegacyWildcard(x, y, target->getPosition());
	target->jumpTo(position);
}

void ScriptAPI::playerGotoDir(int dir, int distance)
{
	auto target = getScriptPlayerKindCharacter(gameManager);
	if (target == nullptr)
	{
		return;
	}
	ScopedScriptInputBlock inputBlock(gameManager);
	int direction = keepCurrentWhenLegacyWildcard(dir, target->direction);
	target->goToDir(direction, distanceOrZeroWhenLegacyWildcard(distance));
}

void ScriptAPI::setWalkIsRun(int value)
{
	gameManager->player->walkIsRun = value;
}

void ScriptAPI::addMoveSpeedPercent(int percent)
{
	if (gameManager->player == nullptr)
	{
		return;
	}
	gameManager->player->addMoveSpeedPercent += percent;
}

void ScriptAPI::useMagic(const std::string& magicName, int x, int y, bool hasDestination)
{
	if (gameManager->player == nullptr)
	{
		return;
	}
	MagicInfo* magicInfo = gameManager->magicManager.findPrimaryMagic(magicName);
	if (magicInfo == nullptr || magicInfo->magic == nullptr || magicInfo->level < 1 || magicInfo->level > MAGIC_MAX_LEVEL)
	{
		return;
	}
	if (magicInfo->remainColdMilliseconds > 0)
	{
		return;
	}
	auto magic = gameManager->player->resolveMagicReplacement(magicInfo->magic);
	if (magic == nullptr || !gameManager->player->tryConsumeMagicCost(magic, magicInfo->level, true))
	{
		return;
	}
	Point destination = hasDestination
		? Point{ x, y }
		: Map::getSubPoint(gameManager->player->getPosition(), gameManager->player->direction);
	gameManager->player->revealMagicInvisibilityOnAction();
	std::shared_ptr<GameElement> target = hasDestination && shouldResolveScriptMagicTarget(magic, magicInfo->level)
		? std::dynamic_pointer_cast<GameElement>(findScriptMagicTarget(destination))
		: nullptr;
	gameManager->player->useMagic(magic, destination, magicInfo->level, target);
	magicInfo->remainColdMilliseconds = magic->coldMilliSeconds;
}

void ScriptAPI::petrifyMillisecond(int milliseconds)
{
	applyStatusMillisecondsToPlayer(gameManager, mskPetrify, milliseconds);
}

void ScriptAPI::poisonMillisecond(int milliseconds)
{
	applyStatusMillisecondsToPlayer(gameManager, mskPoison, milliseconds);
}

void ScriptAPI::frozenMillisecond(int milliseconds)
{
	applyStatusMillisecondsToPlayer(gameManager, mskFreeze, milliseconds);
}

void ScriptAPI::addLife(int value)
{
	if (gameManager->global.addLifeMode == ScriptAddLifeMode::DirectClamp)
	{
		const long long updatedLife =
			static_cast<long long>(gameManager->player->life) + value;
		gameManager->player->life = static_cast<int>(std::clamp<long long>(
			updatedLife,
			0,
			std::max(0, gameManager->player->getLifeMax())));
		if (gameManager->menu != nullptr &&
			gameManager->menu->stateMenu != nullptr)
		{
			gameManager->menu->stateMenu->updateLabel();
		}
		if (gameManager->player->life <= 0)
		{
			gameManager->player->beginDie();
		}
		return;
	}
	gameManager->player->addLife(value);
}

void ScriptAPI::addLifeMax(int value)
{
	gameManager->player->addLifeMax(value);
}

void ScriptAPI::addThew(int value)
{
	gameManager->player->addThew(value);
}

void ScriptAPI::addThewMax(int value)
{
	gameManager->player->addThewMax(value);
}

void ScriptAPI::addMana(int value)
{
	gameManager->player->addMana(value);
}

void ScriptAPI::addManaMax(int value)
{
	gameManager->player->addManaMax(value);
}

void ScriptAPI::addAttack(int value, int type)
{
	gameManager->player->addAttack(value, type);
}

void ScriptAPI::addDefend(int value, int type)
{
	gameManager->player->addDefend(value, type);
}

void ScriptAPI::addEvade(int value)
{
	gameManager->player->addEvade(value);
}

void ScriptAPI::addExp(int value)
{
	gameManager->player->addExp(value);
}

void ScriptAPI::addMoney(int value)
{
	if (value == 0)
	{
		return;
	}
	gameManager->player->addMoney(value);
	if (value > 0)
	{
		showMessage(convert::formatString("获得%d两银子！", value));
	}
	else
	{
		const long long lostMoney = -static_cast<long long>(value);
		showMessage(convert::formatString("失去%lld两银子！", lostMoney));
	}
}

void ScriptAPI::equipGoods(int listIndex, int partIndex)
{
	int fromIndex = listIndex - 1;
	if (fromIndex >= 0 && (gameManager->goodsManager.isStoreIndex(fromIndex) || gameManager->goodsManager.isBottomIndex(fromIndex)))
	{
		auto goods = gameManager->goodsManager.goodsList[fromIndex].goods;
		bool isEquipment = goods != nullptr && goods->kind == gkEquipment;
		if (isEquipment)
		{
			int internalPartIndex = partIndex - 1;
			std::string message;
			if (!gameManager->goodsManager.canEquipGoodsAt(fromIndex, internalPartIndex, gameManager->player, &message))
			{
				if (!message.empty())
				{
					showMessage(message);
				}
				return;
			}
		}
		gameManager->goodsManager.useItem(fromIndex);
	}
}

void ScriptAPI::addRandMoney(int mMin, int mMax)
{
	int minValue = std::min(mMin, mMax);
	int maxValue = std::max(mMin, mMax);
	int value = engine->getRand(maxValue, minValue);
	addMoney(value);
}

void ScriptAPI::addGoods(const std::string& name, int count)
{
	gameManager->goodsManager.addItem(name, std::max(1, count));
}

void ScriptAPI::addRandGoods(const std::string& fileName)
{
	std::string str = BUYSELL_FOLDER;
	str += fileName;
	std::unique_ptr<char[]> s;
	int len = File::readFile(str, s);
	if (len > 0 && s != nullptr)
	{
		INIReader ini(s);
		int count = readBuySellCount(ini);
		if (count > 0)
		{
			int idx = engine->getRand(count - 1);
			std::string section = convert::formatString("%d", idx + 1);
			std::string name = ini.Get(section, "IniFile", "");
			addGoods(name);
		}
	}
}

void ScriptAPI::deleteGoods(const std::string& name)
{
	gameManager->goodsManager.deleteItem(name);
}

void ScriptAPI::deleteGoods()
{
	if (gameManager->scriptGoods == nullptr || gameManager->scriptGoods->sourceFileName.empty())
	{
		return;
	}
	deleteGoods(gameManager->scriptGoods->sourceFileName);
}

void ScriptAPI::deleteGoodsByName(const std::string& name, int count)
{
	gameManager->goodsManager.deleteItemByDisplayName(name, count);
}

void ScriptAPI::addMagic(const std::string& name)
{
	gameManager->magicManager.addPrimaryMagic(name, true, true);
}

void ScriptAPI::addTalent(const std::string& name)
{
	addMagic(name);
}

void ScriptAPI::addOneMagic(const std::string& playerName, const std::string& magicName)
{
	if (gameManager->player->npcName == playerName)
	{
		addMagic(magicName);
		return;
	}

	for (size_t i = 0; i < 5; i++)
	{
		Player tempPlayer;
		tempPlayer.load(i);
		if (tempPlayer.npcName == playerName)
		{
			MagicManager tempMagicManager;
			tempMagicManager.load(i);
			tempMagicManager.addMagic(magicName);
			tempMagicManager.save(i);
			return;
		}
	}
}

void ScriptAPI::deleteMagic(const std::string& name)
{
	gameManager->magicManager.deletePrimaryMagic(name);
}

void ScriptAPI::addMagicExp(const std::string& name, int addexp)
{
	gameManager->magicManager.addMagicExp(name, addexp);
}

void ScriptAPI::fullLife()
{
	gameManager->player->fullLife();
}

void ScriptAPI::fullThew()
{
	gameManager->player->fullThew();
}

void ScriptAPI::fullMana()
{
	gameManager->player->fullMana();
}

void ScriptAPI::updateState()
{
	gameManager->menu->equipMenu->updateGoods();
	gameManager->menu->stateMenu->updateLabel();
}

void ScriptAPI::saveGoods(int index)
{
	gameManager->goodsManager.save(index);
}

void ScriptAPI::saveGoods(const std::string& snapshotKey)
{
	saveGoods(snapshotIndexFromKey(snapshotKey));
}

void ScriptAPI::loadGoods(int index)
{
	gameManager->goodsManager.load(index);
}

void ScriptAPI::loadGoods(const std::string& snapshotKey)
{
	loadGoods(snapshotIndexFromKey(snapshotKey));
}

void ScriptAPI::clearGoods()
{
	gameManager->goodsManager.clearItem();
}

void ScriptAPI::clearMagic()
{
	gameManager->magicManager.clearPrimaryMagicList();
}

void ScriptAPI::getGoodsNum(const std::string& name)
{
	gameManager->varList.setInteger("GoodsNum", gameManager->goodsManager.getItemNum(name));
}

void ScriptAPI::getGoodsNumByName(const std::string& name)
{
	gameManager->varList.setInteger("GoodsNum", gameManager->goodsManager.getItemNumByDisplayName(name));
}

void ScriptAPI::getGoodsState(const std::string& goodsName, const std::string& stateName, const std::string& varName)
{
	if (varName.empty())
	{
		return;
	}
	Goods goods;
	goods.initFromIni(goodsName);
	int value = 0;
	if (!getGoodsRuntimePropertyValue(goods, stateName, value))
	{
		value = 0;
	}
	assign(varName, value);
}

void ScriptAPI::getExp(const std::string& varName)
{
	if (gameManager->player == nullptr)
	{
		assign(varName, 0);
		return;
	}
	assign(varName, gameManager->player->exp);
}

void ScriptAPI::clearAllVar(const std::vector<std::string>& keepNames)
{
	gameManager->varList.clearExcept(keepNames);
}

void ScriptAPI::checkFreeGoodsSpace(const std::string& varName)
{
	bool hasFreeSpace = false;
	for (int i = gameManager->goodsManager.storeBegin(); i <= gameManager->goodsManager.bottomEnd() && i < gameManager->goodsManager.listLength(); i++)
	{
		if ((gameManager->goodsManager.isStoreIndex(i) || gameManager->goodsManager.isBottomIndex(i)) && !gameManager->goodsManager.goodsListExists(i))
		{
			hasFreeSpace = true;
			break;
		}
	}
	assign(varName, hasFreeSpace ? 1 : 0);
}

void ScriptAPI::checkFreeMagicSpace(const std::string& varName)
{
	bool hasFreeSpace = gameManager->magicManager.primaryFreeIndex() >= 0;
	assign(varName, hasFreeSpace ? 1 : 0);
}

void ScriptAPI::getPlayerState(const std::string& stateName, const std::string& varName)
{
	if (gameManager->player == nullptr)
	{
		assign(varName, 0);
		return;
	}

	int value = 0;
	std::string normalizedStateName = toLowerAscii(trimAscii(stateName));
	if (normalizedStateName == "maploaded" || normalizedStateName == "ismaploaded")
	{
		value = (gameManager->map != nullptr && gameManager->map->data != nullptr) ? 1 : 0;
	}
	else if (normalizedStateName == "mapdatatilerows")
	{
		value = gameManager->map != nullptr ? static_cast<int>(gameManager->map->dataMap.tile.size()) : 0;
	}
	else if (normalizedStateName == "level")
	{
		value = gameManager->player->level;
	}
	else if (normalizedStateName == "attack")
	{
		value = gameManager->player->attack +
			gameManager->player->equipmentAttributes.attack;
		if (gameManager->player->weakMagic != nullptr)
		{
			value = value *
				(100 - gameManager->player->weakMagic->weakAttackPercent) / 100;
		}
	}
	else if (normalizedStateName == "defend")
	{
		value = gameManager->player->defend +
			gameManager->player->equipmentAttributes.defend;
		if (gameManager->player->weakMagic != nullptr)
		{
			value = value *
				(100 - gameManager->player->weakMagic->weakDefendPercent) / 100;
		}
	}
	else if (normalizedStateName == "evade")
	{
		value = gameManager->player->evade +
			gameManager->player->equipmentAttributes.evade;
	}
	else if (normalizedStateName == "life")
	{
		value = gameManager->player->life;
	}
	else if (normalizedStateName == "thew")
	{
		value = gameManager->player->thew;
	}
	else if (normalizedStateName == "mana")
	{
		value = gameManager->player->mana;
	}
	else if (normalizedStateName == "rage")
	{
		value = gameManager->player->rage;
	}
	else if (normalizedStateName == "ragemax")
	{
		value = gameManager->player->rageMax;
	}
	else if (normalizedStateName == "critchance" || normalizedStateName == "realcritchance")
	{
		value = static_cast<int>(std::round(gameManager->player->getCriticalChancePercent()));
	}
	else if (normalizedStateName == "critdamage" || normalizedStateName == "realcritdamage")
	{
		value = gameManager->player->getCriticalDamagePercent();
	}
	else if (normalizedStateName == "isrundisabled")
	{
		value = gameManager->player->isRunDisabled() ? 1 : 0;
	}
	else if (normalizedStateName == "isjumpdisabled")
	{
		value = gameManager->player->isJumpDisabled() ? 1 : 0;
	}
	else if (normalizedStateName == "isfightdisabled")
	{
		value = gameManager->player->isFightDisabled() ? 1 : 0;
	}
	else if (normalizedStateName == "canrun")
	{
		value = gameManager->player->canRun ? 1 : 0;
	}
	else if (normalizedStateName == "canjump")
	{
		value = gameManager->player->canJump ? 1 : 0;
	}
	else if (normalizedStateName == "canfight")
	{
		value = gameManager->player->canFight ? 1 : 0;
	}
	else if (normalizedStateName == "canusemana")
	{
		value = gameManager->player->canUseMana ? 1 : 0;
	}
	else if (normalizedStateName == "isinvisiblebymagic")
	{
		value = gameManager->player->isInvisibleByMagic() ? 1 : 0;
	}
	else if (normalizedStateName == "invisiblemilliseconds")
	{
		value = static_cast<int>(gameManager->player->invisibleMilliseconds);
	}
	else if (normalizedStateName == "isvisiblewhenattack")
	{
		value = gameManager->player->isVisibleWhenAttack ? 1 : 0;
	}
	else if (normalizedStateName == "walkisrun")
	{
		value = gameManager->player->walkIsRun;
	}
	else if (normalizedStateName == "isfighting" || normalizedStateName == "fightstate")
	{
		value = gameManager->player->fightState.get() ? 1 : 0;
	}
	else if (normalizedStateName == "iscontrollingcharacter"
		|| normalizedStateName == "hascontrolledcharacter"
		|| normalizedStateName == "controledmagicsprite"
		|| normalizedStateName == "controlledmagicsprite"
		|| normalizedStateName == "hascontrolledmagicsprite")
	{
		value = gameManager->player->isControllingCharacter() ? 1 : 0;
	}
	else if (normalizedStateName == "camerafollownpc"
		|| normalizedStateName == "camerahasfollownpc"
		|| normalizedStateName == "hascamerafollownpc"
		|| normalizedStateName == "camerafollowtarget")
	{
		value = (gameManager->camera != nullptr && gameManager->camera->followNPC.lock() != nullptr) ? 1 : 0;
	}
	else if (normalizedStateName == "controlledtargetkind")
	{
		auto controlled = gameManager->player->getControlledCharacter();
		value = controlled != nullptr ? controlled->kind : 0;
	}
	else if (normalizedStateName == "controlledtargetrelation" || normalizedStateName == "controlledtargetrawrelation")
	{
		auto controlled = gameManager->player->getControlledCharacter();
		value = controlled != nullptr ? controlled->relation : 0;
	}
	else if (normalizedStateName == "controlledtargetruntimerelation" || normalizedStateName == "controlledtargeteffectiverelation")
	{
		value = getRuntimeRelationForReadback(gameManager->player->getControlledCharacter());
	}
	else if (normalizedStateName == "controlledtargetmapx" || normalizedStateName == "controlledtargetx")
	{
		auto controlled = gameManager->player->getControlledCharacter();
		value = controlled != nullptr ? controlled->getPosition().x : 0;
	}
	else if (normalizedStateName == "controlledtargetmapy" || normalizedStateName == "controlledtargety")
	{
		auto controlled = gameManager->player->getControlledCharacter();
		value = controlled != nullptr ? controlled->getPosition().y : 0;
	}
	else if (normalizedStateName == "controlledtargetlevel")
	{
		auto controlled = gameManager->player->getControlledCharacter();
		value = controlled != nullptr ? controlled->level : 0;
	}
	else if (normalizedStateName == "bodyfunctionwell" || normalizedStateName == "isnormalstate")
	{
		value = (!gameManager->player->frozen
			&& !gameManager->player->poisoned
			&& !gameManager->player->petrified
			&& !gameManager->player->immobilized) ? 1 : 0;
	}
	else if (normalizedStateName == "movedbymagicsprite"
		|| normalizedStateName == "boundbymagicsprite"
		|| normalizedStateName == "sppedupbymagicsprite"
		|| normalizedStateName == "speedupbymagicsprite")
	{
		int npcValue = 0;
		value = getNpcRuntimePropertyValue(gameManager->player, stateName, npcValue) ? npcValue : 0;
	}
	else if (normalizedStateName == "hasactivereplacemagiclist" || normalizedStateName == "isinreplacemagiclist")
	{
		value = gameManager->magicManager.hasActiveReplaceMagicList() ? 1 : 0;
	}
	else if (normalizedStateName == "lastscriptsoundhasposition" || normalizedStateName == "lastscriptsoundpositioned")
	{
		value = gameManager->lastScriptSoundHasPosition;
	}
	else if (normalizedStateName == "lastscriptsoundsourcetype")
	{
		value = gameManager->lastScriptSoundSourceType;
	}
	else if (normalizedStateName == "lastscriptsoundmapx")
	{
		value = gameManager->lastScriptSoundMapX;
	}
	else if (normalizedStateName == "lastscriptsoundmapy")
	{
		value = gameManager->lastScriptSoundMapY;
	}
	else if (normalizedStateName == "lastscriptsoundoffsetx1000")
	{
		value = gameManager->lastScriptSoundOffsetX1000;
	}
	else if (normalizedStateName == "lastscriptsoundoffsety1000")
	{
		value = gameManager->lastScriptSoundOffsetY1000;
	}
	else if (normalizedStateName == "visiblemagiclistcount" || normalizedStateName == "currentmagiclistcount")
	{
		for (int i = 0; i < gameManager->magicManager.listLength(); i++)
		{
			if (gameManager->magicManager.magicListExists(i))
			{
				value++;
			}
		}
	}
	else if (normalizedStateName == "primarymagiclistcount")
	{
		for (int i = 0; i < gameManager->magicManager.listLength(); i++)
		{
			if (gameManager->magicManager.primaryMagicListExists(i))
			{
				value++;
			}
		}
	}
	else if (!getNpcRuntimePropertyValue(gameManager->player, stateName, value))
	{
		value = 0;
	}
	assign(varName, value);
}

void ScriptAPI::isEquipWeapon(const std::string& varName)
{
	bool equipped = false;
	int handSlot = NPC::getEquipmentPartIndex("Hand");
	if (handSlot >= 0)
	{
		int index = gameManager->goodsManager.equipIndex(handSlot);
		if (gameManager->goodsManager.goodsListExists(index))
		{
			auto& info = gameManager->goodsManager.goodsList[index];
			equipped = info.goods != nullptr && NPC::getEquipmentPartIndex(info.goods->part) == handSlot;
		}
	}
	assign(varName, equipped ? 1 : 0);
}

void ScriptAPI::getMoneyNum()
{
	gameManager->varList.setInteger("MoneyNum", gameManager->player->money);
}

void ScriptAPI::getMoneyNum(const std::string& varName)
{
	assign(varName.empty() ? "MoneyNum" : varName, gameManager->player->money);
}

void ScriptAPI::setMoneyNum(int value)
{
	gameManager->player->money = value;
	gameManager->menu->goodsMenu->updateMoney();
}

bool ScriptAPI::showGamble(int cost, int npcType)
{
	if (cost <= 0)
	{
		return false;
	}
	if (gameManager->player->money < cost)
	{
		gameManager->showMessage("金钱不足！");
		return false;
	}
	if (gameManager->menu == nullptr || gameManager->menu->gambleMenu == nullptr)
	{
		GameLog::write("GambleMenu is not initialized.\n");
		return false;
	}

	bool automationResult = false;
	if (consumeGambleAutomation(gameManager, cost, automationResult))
	{
		return automationResult;
	}

	return gameManager->menu->gambleMenu->open(cost, npcType);
}

void ScriptAPI::gamble(int cost, int npcType, const std::string& varName)
{
	bool result = showGamble(cost, npcType);
	assign(varName, result ? 1 : 0);
}

void ScriptAPI::showDiceGame(const std::string& npcName)
{
	if (gameManager == nullptr || gameManager->menu == nullptr || gameManager->menu->gambleMenu == nullptr)
	{
		GameLog::write("ShowDiceGame requires GambleMenu.\n");
		return;
	}
	gameManager->menu->gambleMenu->openDiceGame(npcName);
}

void ScriptAPI::showFishGame()
{
	if (gameManager == nullptr || gameManager->menu == nullptr || gameManager->menu->gambleMenu == nullptr)
	{
		GameLog::write("ShowFishGame requires GambleMenu.\n");
		return;
	}
	gameManager->menu->gambleMenu->openFishGame();
}

void ScriptAPI::showStealWin(const std::string& npcName, const std::string& successScript, const std::string& failScript)
{
	if (gameManager == nullptr || gameManager->npcManager == nullptr)
	{
		return;
	}

	auto npcList = gameManager->npcManager->findNPC(npcName);
	std::shared_ptr<NPC> targetNPC = nullptr;
	for (const auto& npc : npcList)
	{
		if (npc != nullptr)
		{
			targetNPC = npc;
			break;
		}
	}
	if (targetNPC == nullptr)
	{
		GameLog::write("ShowStealWin cannot find NPC %s.\n", npcName.c_str());
		return;
	}

	std::vector<StealGoodsEntry> entries = parseStealGoodsEntries(targetNPC->bagGoods);
	if (entries.empty())
	{
		gameManager->showMessage("没有东西可偷！");
		return;
	}
	if (gameManager->menu == nullptr || gameManager->menu->chooseMenu == nullptr)
	{
		GameLog::write("ShowStealWin requires ChooseMenu.\n");
		return;
	}

	std::vector<std::string> options;
	std::vector<bool> visibleOptions;
	for (const auto& entry : entries)
	{
		options.push_back(stealGoodsOptionText(entry));
		visibleOptions.push_back(true);
	}
	options.push_back("离开");
	visibleOptions.push_back(true);

	ScriptChooseOptions chooseOptions;
	chooseOptions.options = options;
	chooseOptions.visibleOptions = visibleOptions;

	int selection = -1;
	if (!consumeChooseAutomation(gameManager, chooseOptions, selection))
	{
		gameManager->menu->chooseMenu->chooseEx("偷取：" + targetNPC->npcName, options, visibleOptions);
		selection = gameManager->menu->chooseMenu->getSelection();
	}
	if (selection < 0 || selection >= static_cast<int>(entries.size()))
	{
		return;
	}

	int playerSteal = std::max(gameManager->varList.getInteger("touqie"), getPlayerMagicLevel("player-talent-偷取.ini"));
	int requiredSteal = std::max(0, targetNPC->steal);
	bool success = playerSteal >= requiredSteal && rollPercentChance(engine, entries[selection].chance);
	if (success)
	{
		success = gameManager->goodsManager.addItem(entries[selection].goodsIni, 1);
		if (success)
		{
			targetNPC->bagGoods = serializeStealGoodsEntries(entries, selection);
		}
		else
		{
			gameManager->showMessage("物品栏位置已满！");
		}
	}
	else if (playerSteal < requiredSteal)
	{
		gameManager->showMessage("偷窃能力不足！");
	}

	const std::string& scriptName = success ? successScript : failScript;
	if (!isDisabledScriptName(scriptName))
	{
		runScript(scriptName);
	}
}

void ScriptAPI::showGiveGoodsWin(const std::string& targetGoodsName,
	const std::string& successScript,
	const std::string& failScript)
{
	bool success = playerHasGoods(gameManager, targetGoodsName);
	const std::string& scriptName = success ? successScript : failScript;
	if (!isDisabledScriptName(scriptName))
	{
		runScript(scriptName);
	}
}

void ScriptAPI::showMessage(const std::string& str)
{
	gameManager->menu->showMessage(str);
}

void ScriptAPI::showSystemMessage(const std::string& str, int stayTime)
{
	const int duration = std::max(0, stayTime);
	gameManager->menu->showMessage(str, (UTime)duration);
}

void ScriptAPI::addToMemo(const std::string& str)
{
	gameManager->memo.add(str);
}

void ScriptAPI::deleteMemo(const std::string& str)
{
	gameManager->memo.remove(str);
}

void ScriptAPI::clearMemo()
{
	gameManager->memo.clear();
}

void ScriptAPI::buyGoods(const std::string& fileName)
{
	std::shared_ptr<NPC> owner = gameManager->scriptNPC;
	std::string resolvedFileName = fileName;
	if (resolvedFileName.empty() && owner != nullptr)
	{
		resolvedFileName = owner->buyIniFile;
	}
	if (resolvedFileName.empty() && (owner == nullptr || owner->buyIniString.empty()))
	{
		return;
	}
	gameManager->menu->buySellMenu->buy(resolvedFileName, owner, true);
}

void ScriptAPI::buyGoodsOnly(const std::string& fileName)
{
	std::shared_ptr<NPC> owner = gameManager->scriptNPC;
	std::string resolvedFileName = fileName;
	if (resolvedFileName.empty() && owner != nullptr)
	{
		resolvedFileName = owner->buyIniFile;
	}
	if (resolvedFileName.empty() && (owner == nullptr || owner->buyIniString.empty()))
	{
		return;
	}
	gameManager->menu->buySellMenu->buy(resolvedFileName, owner, false);
}

void ScriptAPI::sellGoods(const std::string& fileName)
{
	gameManager->menu->buySellMenu->sell(fileName);
}

void ScriptAPI::returnToTitle()
{
	gameManager->clearParallelScriptTasks();
	gameManager->result = erOK;
	gameManager->logicRunning = false;
}

void ScriptAPI::enableInput()
{
	gameManager->global.data.canInput = true;
}

void ScriptAPI::disableInput()
{
	if (gameManager->controller != nullptr)
	{
		gameManager->controller->cancelTouchControlInput();
	}
	gameManager->global.data.canInput = false;
}

void ScriptAPI::hideInterface()
{
	gameManager->menu->hideBottomWnd();
	gameManager->clearMenu();
}

void ScriptAPI::hideBottomWnd()
{
	gameManager->menu->hideBottomWnd();
}

void ScriptAPI::showBottomWnd()
{
	gameManager->menu->showBottomWnd();
}

void ScriptAPI::hideMouseCursor()
{
	engine->hideCursor();
}

void ScriptAPI::showMouseCursor()
{
	engine->showCursor();
}

void ScriptAPI::showSnow(int bsnow)
{
	gameManager->global.data.snowShow = bsnow != 0;
	gameManager->weather->setSnowVisible(bsnow != 0);
}

void ScriptAPI::showRandomSnow()
{
	showSnow(engine->getRand(2));
}

void ScriptAPI::showRain(int brain)
{
	gameManager->global.data.rainFile = "";
	if (brain)
	{
		gameManager->global.data.rainShow = true;
		gameManager->weather->setRainWeather(wtLightning);
	}
	else
	{
		gameManager->global.data.rainShow = false;
		gameManager->weather->setRainWeather(wtNone);
	}
}

void ScriptAPI::beginRain(const std::string& configFileName)
{
	gameManager->global.data.rainShow = true;
	gameManager->global.data.rainFile = configFileName;
	gameManager->weather->setRainWeather(wtCustomRain, configFileName);
}

void ScriptAPI::endRain()
{
	gameManager->global.data.rainShow = false;
	gameManager->global.data.rainFile = "";
	gameManager->weather->setRainWeather(wtNone);
}

void ScriptAPI::checkYear(const std::string& varName)
{
	gameManager->varList.setInteger(
		varName,
		NewYearPeriod::containsCurrentLocalDate() ? 1 : 0);
}

void ScriptAPI::checkYear(
	const std::string& varName,
	const NewYearPeriod::LocalDate& localDate)
{
	gameManager->varList.setInteger(
		varName,
		NewYearPeriod::contains(localDate) ? 1 : 0);
}

void ScriptAPI::getRandNum(const std::string& varName, int minVal, int maxVal)
{
	int minValue = std::min(minVal, maxVal);
	int maxValue = std::max(minVal, maxVal);
	int value = engine->getRand(maxValue, minValue);
	gameManager->varList.setInteger(varName, value);
}

void ScriptAPI::randRun(const std::string& varName, const std::string& successScript, const std::string& failScript)
{
	int value = gameManager->varList.getInteger(varName);
	const std::string& scriptName = engine->getRand(99) <= value ? successScript : failScript;
	if (!isDisabledScriptName(scriptName))
	{
		runScript(scriptName);
	}
}

void ScriptAPI::getPlayerLevel(const std::string& varName)
{
	gameManager->varList.setInteger(varName, gameManager->player->level);
}

void ScriptAPI::getNpcCount(int kind, int relation)
{
	if (gameManager->player->kind == kind && gameManager->player->relation == relation)
	{
		gameManager->varList.setInteger("NpcCount", 1);
		return;
	}

	int count = 0;
	for (size_t i = 0; i < gameManager->npcManager->npcList.size(); i++)
	{
		auto& npc = gameManager->npcManager->npcList[i];
		if (npc != nullptr && npc->kind == kind && npc->relation == relation)
		{
			count++;
		}
	}
	gameManager->varList.setInteger("NpcCount", count);
}

void ScriptAPI::delCurObj()
{
	deleteObject("");
}

void ScriptAPI::showInterface()
{
	gameManager->menu->showBottomWnd();
}

void ScriptAPI::drawBackground()
{
	// Historical compatibility entry. No production script or reference runtime
	// defines a safe rendering contract for this command; invoking onDraw() from
	// script execution would re-enter the renderer and disturb layer ordering.
}

void ScriptAPI::clearEffect()
{
	if (gameManager == nullptr)
	{
		return;
	}

	if (gameManager->player != nullptr)
	{
		gameManager->player->clearAbnormalState();
	}
	auto partners = gameManager->partnerManager.findPartnersFromNPCManager();
	for (const auto& partner : partners)
	{
		if (partner == nullptr)
		{
			continue;
		}
		partner->clearAbnormalState();
	}
	if (gameManager->effectManager != nullptr)
	{
		gameManager->effectManager->clearEffect();
	}
}

void ScriptAPI::saveGame()
{
	if (!gameManager->saveGame(-1))
	{
		GameLog::write("ScriptAPI: automatic save failed\n");
	}
}

void ScriptAPI::clearAllSave()
{
	SaveFileManager::ClearAllSaveData();
}

void ScriptAPI::enableSave()
{
	gameManager->global.data.saveDisabled = false;
}

void ScriptAPI::disableSave()
{
	gameManager->global.data.saveDisabled = true;
}

void ScriptAPI::limitMana(int limit)
{
	gameManager->player->canUseMana = (limit == 0);
}

void ScriptAPI::showNpc(const std::string& name, int isShow)
{
	auto npcList = gameManager->npcManager->findNPC(name);
	if (npcList.empty())
	{
		return;
	}

	std::shared_ptr<NPC> target;
	if (gameManager->player != nullptr && npcList.front() == gameManager->player)
	{
		target = gameManager->player;
	}
	else
	{
		for (auto it = npcList.rbegin(); it != npcList.rend(); ++it)
		{
			if (*it != nullptr)
			{
				target = *it;
				break;
			}
		}
	}
	if (target != nullptr)
	{
		target->setScriptHidden(isShow == 0);
	}
}

void ScriptAPI::openWaterEffect()
{
	gameManager->global.data.waterEffect = true;
}

void ScriptAPI::closeWaterEffect()
{
	gameManager->global.data.waterEffect = false;
}

void ScriptAPI::watch(const std::string& name1, const std::string& name2, int watchType)
{
	auto npc1List = gameManager->npcManager->findNPC(name1);
	auto npc2List = gameManager->npcManager->findNPC(name2);
	if (npc1List.empty() || npc2List.empty()) return;

	auto& npc1 = npc1List[0];
	auto& npc2 = npc2List[0];

	bool isC1 = false;
	bool isC2 = false;
	switch (watchType)
	{
	case 0:
		isC1 = true;
		isC2 = true;
		break;
	case 1:
		isC1 = true;
		break;
	}

	if (isC1)
	{
		npc1->direction = npc1->getDirection(npc2->getPosition());
	}
	if (isC2)
	{
		npc2->direction = npc2->getDirection(npc1->getPosition());
	}
}

void ScriptAPI::setTrap(const std::string& mapName, int idx, const std::string& trapFile)
{
	if (!Traps::isValidIndex(idx))
	{
		GameLog::write("SetTrap ignored invalid trap index %d for map %s", idx, mapName.c_str());
		return;
	}
	const bool targetsCurrentMap = mapName.empty() ||
		mapName == gameManager->mapFolderName ||
		mapName == gameManager->global.data.mapName;
	const std::string& targetMapName = targetsCurrentMap
		? gameManager->mapFolderName
		: mapName;
	if (targetMapName.empty())
	{
		return;
	}
	gameManager->traps.set(targetMapName, idx, trapFile);
	if (targetsCurrentMap)
	{
		gameManager->traps.reactivate(idx);
	}
}

void ScriptAPI::setNpcMagicFile(const std::string& name, const std::string& fileName)
{
	if (name.empty())
	{
		if (gameManager->scriptNPC != nullptr)
		{
			gameManager->scriptNPC->flyIni = fileName;
			gameManager->scriptNPC->npcMagic =
				gameManager->magicManager.loadAttackMagic(fileName);
			gameManager->scriptNPC->rebuildAttackOptions();
		}
		return;
	}

	auto npcList = gameManager->npcManager->findNPC(name);
	const std::size_t targetCount = getNamedNpcTargetCount(npcList.size());
	for (size_t i = 0; i < targetCount; i++)
	{
		if (npcList[i] == nullptr) continue;
		npcList[i]->flyIni = fileName;
		npcList[i]->npcMagic = gameManager->magicManager.loadAttackMagic(fileName);
		npcList[i]->rebuildAttackOptions();
	}
}

void ScriptAPI::setNpcMagicLevel(const std::string& name, int level)
{
	auto npcList = gameManager->npcManager->findNPC(name);
	if (!npcList.empty() && npcList.front() != nullptr)
	{
		npcList.front()->magicLevel = level;
		npcList.front()->attackLevel = level;
		npcList.front()->rebuildAttackOptions();
	}
}

void ScriptAPI::setPlayerMagicToUseWhenBeAttacked(const std::string& fileName, int direction)
{
	if (gameManager->player == nullptr)
	{
		return;
	}
	gameManager->player->magicToUseWhenBeAttackedFile = fileName;
	gameManager->player->magicDirectionWhenBeAttacked = direction;
	gameManager->player->magicToUseWhenBeAttacked = gameManager->magicManager.loadAttackMagic(fileName);
}

void ScriptAPI::setNpcMagicToUseWhenBeAttacked(const std::string& name, const std::string& fileName, int direction)
{
	auto npcList = findNamedNonPlayerNpcs(gameManager, name);
	for (size_t i = 0; i < npcList.size(); i++)
	{
		if (npcList[i] == nullptr) continue;
		npcList[i]->magicToUseWhenBeAttackedFile = fileName;
		npcList[i]->magicDirectionWhenBeAttacked = direction;
		npcList[i]->magicToUseWhenBeAttacked = gameManager->magicManager.loadAttackMagic(fileName);
	}
}

void ScriptAPI::setNpcClickScript(const std::string& name, const std::string& scriptFile)
{
	auto npcList = gameManager->npcManager->findNPC(name);
	if (!npcList.empty() && npcList.front() != nullptr)
	{
		npcList.front()->scriptFile = scriptFile;
	}
}

void ScriptAPI::setNpcPartner(const std::string& name)
{
	if (name.empty() || gameManager->player == nullptr)
	{
		return;
	}

	auto npcList = gameManager->npcManager->findNPC(name);
	if (!npcList.empty())
	{
		auto& npc = npcList.front();
		if (npc == nullptr || npc == gameManager->player)
		{
			return;
		}
		npc->kind = nkPartner;
		npc->relation = nrFriendly;
		npc->followNPC = "";
		npc->isPartnerBlockingPlayer = false;
		npc->nextFollowCheckTime = 0;
		npc->haveAsyncDest = false;
		npc->clearDestinationMapPosition();
		npc->clearCombatTargetMemory();
		npc->beginStand();
	}
}

void ScriptAPI::setPartnerLevel(int level)
{
	auto partners = gameManager->partnerManager.findPartnersFromNPCManager();
	for (size_t i = 0; i < partners.size(); i++)
	{
		if (partners[i] == nullptr) continue;
		partners[i]->level = level;
		partners[i]->setLevel(level);
	}
}

void ScriptAPI::setPartnerLevel(const std::string& name, int level)
{
	setNPCLevel(name, level);
}

void ScriptAPI::playerAddEmotion(int value)
{
	addIntegerVariableAlias("Emotion", value);
}

void ScriptAPI::playerAddJustice(int value)
{
	addIntegerVariableAlias("Justice", value);
}

void ScriptAPI::getPartnerIdx(const std::string& varName)
{
	auto partners = gameManager->partnerManager.findPartnersFromNPCManager();
	int idx = 0;
	if (!partners.empty())
	{
		idx = 1;
		std::string partnerName = partners[0]->npcName;
		std::unique_ptr<char[]> s;
		int len = File::readFile(PARTNER_IDX_INI, s);
		if (len > 0 && s != nullptr)
		{
			INIReader ini(s);
			std::string partnerIdxName;
			int tempIdx = 1;
			while (true)
			{
				partnerIdxName = ini.Get("init", std::to_string(tempIdx), "");
				if (partnerIdxName.empty())
				{
					idx = tempIdx;
					break;
				}
				if (partnerIdxName == partnerName)
				{
					idx = tempIdx;
					break;
				}
				tempIdx++;
			}
		}
	}
	gameManager->varList.setInteger(varName, idx);
}

void ScriptAPI::moveScreenEx(int x, int y, int speed)
{
	if (speed < 0)
	{
		return;
	}
	Point position = keepCurrentPositionWhenLegacyWildcard(x, y, gameManager->camera->position);
	ScopedScriptInputBlock inputBlock(gameManager);
	gameManager->camera->flyToPosition(position.x, position.y, speed);
	gameManager->camera->run();
}

void ScriptAPI::displayMessage(const std::string& text)
{
	gameManager->menu->showMessage(text);
}

void ScriptAPI::disableMapScroll()
{
	gameManager->camera->followPlayer = false;
}

void ScriptAPI::enableMapScroll()
{
	gameManager->camera->followPlayer = true;
	// Restore the camera to the player immediately after script-controlled scrolling.
	gameManager->camera->snapToFollowTarget();
	gameManager->camera->differencePosition = { 0.0, 0.0 };
}

void ScriptAPI::setShowMapPos(int show)
{
	gameManager->global.data.scriptShowMapPos = show > 0;
}

void ScriptAPI::openObj(const std::string& name)
{
	openBox(name);
}

void ScriptAPI::freeMap()
{
	if (gameManager == nullptr || gameManager->map == nullptr)
	{
		return;
	}

	// JxqyHD FreeMap releases only map rendering and tile data. NPCs, objects,
	// effects, weather, script ownership, resource names, and trap state remain
	// available until their own command or the next successful map load changes
	// them.
	gameManager->map->freeResource();
}

void ScriptAPI::openTimeLimit(int seconds)
{
	gameManager->timerSeconds = seconds;
	gameManager->timerAccumulated = 0;
	gameManager->timerStarted = true;
	gameManager->timerHidden = false;
	if (gameManager->menu != nullptr &&
		gameManager->menu->timerMenu != nullptr)
	{
		gameManager->menu->timerMenu->startTimer(seconds);
	}
}

void ScriptAPI::closeTimeLimit()
{
	gameManager->timerStarted = false;
	gameManager->timeScriptSet = false;
	if (gameManager->menu != nullptr &&
		gameManager->menu->timerMenu != nullptr)
	{
		gameManager->menu->timerMenu->stopTimer();
	}
}

void ScriptAPI::hideTimerWnd()
{
	gameManager->timerHidden = true;
	if (gameManager->menu != nullptr &&
		gameManager->menu->timerMenu != nullptr)
	{
		gameManager->menu->timerMenu->hideTimer();
	}
}

void ScriptAPI::setTimeScript(int seconds, const std::string& scriptFile)
{
	if (seconds < 0 || isDisabledScriptName(scriptFile))
	{
		return;
	}
	// Formal XJXQY scripts configure the timeout callback before opening the
	// timer, so callback state must not depend on timerStarted.
	gameManager->timeScriptSeconds = seconds;
	gameManager->timeScriptFileName = scriptFile;
	gameManager->timeScriptSet = true;
}

void ScriptAPI::choose(const std::string& message, const std::string& optionA, const std::string& optionB, const std::string& varName)
{
	gameManager->menu->chooseMenu->choose(message, optionA, optionB);
	int sel = gameManager->menu->chooseMenu->getSelection();
	assign(varName, sel);
}

void ScriptAPI::chooseEx(const std::string& message, const std::vector<std::string>& options, const std::string& varName)
{
	ScriptChooseOptions chooseOptions = buildScriptChooseOptions(gameManager, options);

	int sel = -1;
	if (!consumeChooseAutomation(gameManager, chooseOptions, sel))
	{
		gameManager->menu->chooseMenu->chooseEx(message, chooseOptions.options, chooseOptions.visibleOptions);
		sel = gameManager->menu->chooseMenu->getSelection();
	}
	assign(varName, sel);
}

void ScriptAPI::chooseMultiple(int columnCount, int selectionCount, const std::string& varName, const std::string& message, const std::vector<std::string>& options)
{
	ScriptChooseOptions chooseOptions = buildScriptChooseOptions(gameManager, options);
	std::vector<int> selections;
	if (!consumeChooseMultipleAutomation(gameManager, chooseOptions, selectionCount, selections))
	{
		gameManager->menu->chooseMenu->chooseMultiple(message, chooseOptions.options, chooseOptions.visibleOptions, columnCount, selectionCount);
		selections = gameManager->menu->chooseMenu->getMultipleSelection();
	}
	for (size_t i = 0; i < selections.size(); i++)
	{
		assign(makeChooseMultipleResultVariableName(varName, static_cast<int>(i)), selections[i]);
	}
}

void ScriptAPI::choosePlus(const std::string& speakerName, int portraitIndex, int dialogPosition, const std::string& message, const std::vector<std::string>& options, const std::string& varName)
{
	ScriptChooseOptions chooseOptions = buildScriptChooseOptions(gameManager, options);
	int selection = -1;
	if (!consumeChooseAutomation(gameManager, chooseOptions, selection))
	{
		std::string portraitFileName;
		if (portraitIndex >= 0 && gameManager->menu != nullptr && gameManager->menu->dialog != nullptr)
		{
			portraitFileName = gameManager->menu->dialog->getHeadName(portraitIndex);
		}
		gameManager->menu->chooseMenu->choosePlus(
			resolveChoosePlusSpeakerName(gameManager, speakerName),
			portraitFileName,
			dialogPosition,
			message,
			chooseOptions.options,
			chooseOptions.visibleOptions);
		selection = gameManager->menu->chooseMenu->getSelection();
	}
	assign(varName, selection);
}

void ScriptAPI::select(int messageIdx, int optionAIdx, int optionBIdx, const std::string& varName)
{
	std::string message = gameManager->talkTextList.getText(messageIdx);
	std::string optionA = gameManager->talkTextList.getText(optionAIdx);
	std::string optionB = gameManager->talkTextList.getText(optionBIdx);
	gameManager->menu->chooseMenu->choose(message, optionA, optionB);
	int sel = gameManager->menu->chooseMenu->getSelection();
	assign(varName, sel);
}

void ScriptAPI::playerChange(int index)
{
	gameManager->player->save(gameManager->global.data.characterIndex);
	gameManager->magicManager.save(gameManager->global.data.characterIndex);
	gameManager->goodsManager.save(gameManager->global.data.characterIndex);
	gameManager->memo.save();

	gameManager->global.data.characterIndex = index;

	gameManager->player->load(index);
	gameManager->magicManager.load(index);
	gameManager->goodsManager.load(index);

	gameManager->player->reloadAction();
	if (gameManager->menu != nullptr)
	{
		if (gameManager->menu->stateMenu != nullptr)
		{
			gameManager->menu->stateMenu->updateLabel();
			gameManager->menu->stateMenu->updatePanelImage();
		}
		if (gameManager->menu->equipMenu != nullptr)
		{
			gameManager->menu->equipMenu->updatePanelImage();
			gameManager->menu->equipMenu->updateGoods();
		}
		if (gameManager->menu->goodsMenu != nullptr)
		{
			gameManager->menu->goodsMenu->updateGoods();
		}
		if (gameManager->menu->bottomMenu != nullptr)
		{
			gameManager->menu->bottomMenu->updateGoodsItem();
			gameManager->menu->bottomMenu->updateMagicItem();
		}
		if (gameManager->menu->magicMenu != nullptr)
		{
			gameManager->menu->magicMenu->updateMagic();
		}
		if (gameManager->menu->practiceMenu != nullptr)
		{
			gameManager->menu->practiceMenu->updateMagic();
		}
	}
}

void ScriptAPI::mergeNpc(const std::string& fileName)
{
	if (engine == nullptr || !engine->isMainThread())
	{
		GameLog::write(
			"ScriptAPI: NPC merge commit must run on the SDL main thread\n");
		return;
	}
	(void)runOwnerWorldCommit(
		"NPC merge",
		[this, &fileName](
			const std::function<void()>& beforeMutation,
			const std::function<void()>& commitCompleted)
		{
			if (!gameManager->npcManager->load(
					fileName,
					false,
					beforeMutation))
			{
				return false;
			}
			commitCompleted();
			return true;
		});
}

bool ScriptAPI::loadMap(const std::string& fileName, bool resetCamera)
{
	return loadMapWithFailurePolicy(
		fileName,
		resetCamera,
		true);
}

bool ScriptAPI::loadMapWithFailurePolicy(
	const std::string& fileName,
	bool resetCamera,
	bool failCloseOnPartialFailure,
	const std::function<bool()>& preparationCheckpoint,
	const std::function<bool(
		const std::function<void()>& beforeMutation,
		const std::function<bool()>& preparationCheckpoint)>&
			preparedLoadCommit,
	bool rebuildDataMap,
	const std::string& preparedMapFolderName,
	MapActorResetMode actorResetMode)
{
	if (engine == nullptr || !engine->isMainThread())
	{
		GameLog::write(
			"ScriptAPI: map commit must run on the SDL main thread\n");
		return false;
	}
	return runOwnerWorldCommit(
		"map",
		[this,
		 &fileName,
		 &preparedMapFolderName,
		 resetCamera,
		 rebuildDataMap,
		 actorResetMode,
		 &preparationCheckpoint,
		 &preparedLoadCommit](
			const std::function<void()>& beforeMutation,
			const std::function<void()>& commitCompleted)
		{
			const std::string nextMapFolderName =
				preparedMapFolderName.empty()
					? resolveLegacyMapFolderName(fileName)
					: preparedMapFolderName;
			const std::string previousMapFolderName =
				gameManager->mapFolderName;
			bool mapFolderCommitted = false;
			FunctionScopeExit restoreMapFolder(
				[&]()
				{
					if (!mapFolderCommitted)
					{
						gameManager->mapFolderName =
							previousMapFolderName;
					}
				});
			gameManager->mapFolderName = nextMapFolderName;
			const bool loaded = preparedLoadCommit
				? preparedLoadCommit(
					beforeMutation,
					preparationCheckpoint)
				: gameManager->map->load(
					MAP_FOLDER + fileName,
					beforeMutation,
					preparationCheckpoint);
			if (!loaded)
			{
				GameLog::write(
					"ScriptAPI: keeping current map after load failure %s\n",
					fileName.c_str());
				return false;
			}
			mapFolderCommitted = true;
			gameManager->global.data.mapName = fileName;

			endRain();
			gameManager->effectManager->clearEffect();
			if (actorResetMode ==
				MapActorResetMode::ReplaceAllForSaveLoad)
			{
				// A complete save load replaces every actor. Drop the previous
				// action cache without reloading the old player or partners; the
				// saved player and partners are loaded immediately afterwards.
				gameManager->npcManager->freeResource();
			}
			else
			{
				gameManager->npcManager->clearNPC(false);
			}
			gameManager->objectManager->clearObj();
			gameManager->global.data.npcName.clear();
			gameManager->global.data.objName.clear();
			if (rebuildDataMap)
			{
				gameManager->map->createDataMap();
			}
			gameManager->traps.beginMapVisit();
			enableFight();
			gameManager->player->beginStand();

			// Camera snapping reads window/shared state, so async loaders defer
			// it to the main thread.
			if (resetCamera)
			{
				gameManager->camera->resetView();
			}
			else
			{
				gameManager->camera->followPlayer = true;
				gameManager->camera->followNPC.reset();
			}
			commitCompleted();
			enqueueMapChange(
				gameManager->runtimeTraceWriter,
				fileName,
				gameManager->script.currentExecutionId());
			return true;
		},
		failCloseOnPartialFailure);
}

bool ScriptAPI::loadMapFromExactRoot(
	const EditorRun::SearchRoot& root,
	const std::string& virtualPath,
	bool resetCamera)
{
	if (engine == nullptr || !engine->isMainThread())
	{
		GameLog::write(
			"ScriptAPI: exact-root map commit must run on the SDL main thread\n");
		return false;
	}
	if (virtualPath.empty() ||
		gameManager == nullptr ||
		gameManager->map == nullptr ||
		gameManager->camera == nullptr ||
		gameManager->player == nullptr ||
		gameManager->npcManager == nullptr ||
		gameManager->objectManager == nullptr ||
		gameManager->effectManager == nullptr)
	{
		return false;
	}

	RootedResourceReader::Result mapBytes =
		readExactEditorRunResource(
			root,
			virtualPath,
			MapSafety::MaximumFileBytes,
			"map");
	return mapBytes.succeeded() &&
		loadMapFromEditorRunBytes(
			virtualPath,
			std::move(mapBytes.bytes),
			resetCamera);
}

bool ScriptAPI::loadMapFromEditorRunRoots(
	const std::vector<EditorRun::SearchRoot>& roots,
	const std::string& virtualPath,
	bool resetCamera)
{
	EditorRunResourceRead mapBytes = readFirstEditorRunResource(
		roots,
		virtualPath,
		MapSafety::MaximumFileBytes,
		"map");
	return mapBytes.result.succeeded() &&
		loadMapFromEditorRunBytes(
			virtualPath,
			std::move(mapBytes.result.bytes),
			resetCamera);
}

bool ScriptAPI::loadMapFromEditorRunBytes(
	const std::string& virtualPath,
	std::vector<std::uint8_t> bytes,
	bool resetCamera)
{
	if (engine == nullptr || !engine->isMainThread() ||
		virtualPath.empty() ||
		gameManager == nullptr ||
		gameManager->map == nullptr ||
		gameManager->camera == nullptr ||
		gameManager->player == nullptr ||
		gameManager->npcManager == nullptr ||
		gameManager->objectManager == nullptr ||
		gameManager->effectManager == nullptr ||
		bytes.empty() ||
		bytes.size() > static_cast<std::size_t>(INT_MAX))
	{
		return false;
	}
	auto mapBuffer = std::make_unique<char[]>(bytes.size());
	std::memcpy(
		mapBuffer.get(),
		bytes.data(),
		bytes.size());

	return runOwnerWorldCommit(
		"exact-root map",
		[this,
		 &virtualPath,
		 resetCamera,
		 &mapBuffer,
			 mapByteCount = bytes.size()](
			const std::function<void()>& beforeMutation,
			const std::function<void()>& commitCompleted)
		{
			const std::string nextMapFolderName =
				resolveLegacyMapFolderName(virtualPath);
			const std::string previousMapFolderName =
				gameManager->mapFolderName;
			bool mapFolderCommitted = false;
			FunctionScopeExit restoreMapFolder(
				[&]()
				{
					if (!mapFolderCommitted)
					{
						gameManager->mapFolderName =
							previousMapFolderName;
					}
				});
			gameManager->mapFolderName = nextMapFolderName;
			GameLog::write(
				"ScriptAPI: load exact-root map %s\n",
				virtualPath.c_str());
			if (!gameManager->map->load(
					mapBuffer,
					static_cast<int>(mapByteCount),
					beforeMutation))
			{
				GameLog::write(
					"ScriptAPI: keeping current map after exact load failure %s\n",
					virtualPath.c_str());
				return false;
			}
			mapFolderCommitted = true;
			gameManager->global.data.mapName =
				convert::extractFullName(virtualPath);

			endRain();
			gameManager->effectManager->clearEffect();
			gameManager->npcManager->clearNPC(false);
			gameManager->objectManager->clearObj();
			gameManager->global.data.npcName.clear();
			gameManager->global.data.objName.clear();
			gameManager->map->createDataMap();
			gameManager->traps.freeResource();
			enableFight();
			gameManager->player->beginStand();

			if (resetCamera)
			{
				gameManager->camera->resetView();
			}
			else
			{
				gameManager->camera->followPlayer = true;
				gameManager->camera->followNPC.reset();
			}
			commitCompleted();
			enqueueMapChange(
				gameManager->runtimeTraceWriter,
				virtualPath,
				gameManager->script.currentExecutionId());
			return true;
		});
}

bool ScriptAPI::loadNPC(const std::string& fileName)
{
	return loadNPCWithPreparationCheckpoint(fileName, {});
}

bool ScriptAPI::loadNPCWithPreparationCheckpoint(
	const std::string& fileName,
	const std::function<bool()>& preparationCheckpoint,
	const std::function<bool(
		const std::function<void()>& beforeMutation,
		const std::function<bool()>& preparationCheckpoint)>&
			preparedLoadCommit)
{
	if (engine == nullptr || !engine->isMainThread())
	{
		GameLog::write(
			"ScriptAPI: NPC commit must run on the SDL main thread\n");
		return false;
	}
	return runOwnerWorldCommit(
		"NPC",
		[this,
		 &fileName,
		 &preparationCheckpoint,
		 &preparedLoadCommit](
			const std::function<void()>& beforeMutation,
			const std::function<void()>& commitCompleted)
		{
			const bool loaded = preparedLoadCommit
				? preparedLoadCommit(
					beforeMutation,
					preparationCheckpoint)
				: gameManager->npcManager->load(
					fileName,
					true,
					beforeMutation,
					preparationCheckpoint);
			if (!loaded)
			{
				return false;
			}
			gameManager->global.data.npcName = fileName;
			commitCompleted();
			return true;
		});
}

bool ScriptAPI::loadNPCFromExactRoot(
	const EditorRun::SearchRoot& root,
	const std::string& virtualPath)
{
	if (engine == nullptr || !engine->isMainThread())
	{
		GameLog::write(
			"ScriptAPI: exact-root NPC commit must run on the SDL main thread\n");
		return false;
	}
	if (gameManager == nullptr || gameManager->npcManager == nullptr)
	{
		return false;
	}
	RootedResourceReader::Result npcBytes =
		readExactEditorRunResource(
			root,
			virtualPath,
			MaximumEditorRunNpcListBytes,
			"NPC");
	return npcBytes.succeeded() &&
		loadNPCFromEditorRunBytes(
			virtualPath,
			std::move(npcBytes.bytes));
}

bool ScriptAPI::loadNPCFromEditorRunRoots(
	const std::vector<EditorRun::SearchRoot>& roots,
	const std::string& virtualPath)
{
	if (gameManager == nullptr || gameManager->npcManager == nullptr)
	{
		return false;
	}
	EditorRunResourceRead npcBytes = readFirstEditorRunResource(
		roots,
		virtualPath,
		MaximumEditorRunNpcListBytes,
		"NPC");
	if (npcBytes.result.status == RootedResourceReader::Status::NotFound)
	{
		GameLog::write(
			"ScriptAPI: editor-run NPC resource is missing; using an empty NPC list %s\n",
			virtualPath.c_str());
		return runOwnerWorldCommit(
			"empty editor-run NPC",
			[this](
				const std::function<void()>& beforeMutation,
				const std::function<void()>& commitCompleted)
			{
				beforeMutation();
				gameManager->npcManager->clearNPC();
				gameManager->global.data.npcName.clear();
				commitCompleted();
				return true;
			});
	}
	return npcBytes.result.succeeded() &&
		loadNPCFromEditorRunBytes(
			virtualPath,
			std::move(npcBytes.result.bytes));
}

bool ScriptAPI::loadNPCFromEditorRunBytes(
	const std::string& virtualPath,
	std::vector<std::uint8_t> bytes)
{
	if (engine == nullptr || !engine->isMainThread() ||
		gameManager == nullptr || gameManager->npcManager == nullptr)
	{
		return false;
	}
	return runOwnerWorldCommit(
		"exact-root NPC",
		[this, &virtualPath, &bytes](
			const std::function<void()>& beforeMutation,
			const std::function<void()>& commitCompleted)
		{
			if (!gameManager->npcManager->
					loadExactResourceBytes(
						virtualPath,
						bytes,
						true,
						beforeMutation))
			{
				return false;
			}
			gameManager->global.data.npcName = virtualPath;
			commitCompleted();
			return true;
		});
}

void ScriptAPI::loadOneNpc(const std::vector<std::string>& fileNames)
{
	if (fileNames.empty() ||
		engine == nullptr ||
		!engine->isMainThread())
	{
		if (!fileNames.empty())
		{
			GameLog::write(
				"ScriptAPI: NPC collection commit must run on the SDL main thread\n");
		}
		return;
	}
	(void)runOwnerWorldCommit(
		"NPC collection",
		[this, &fileNames](
			const std::function<void()>& beforeMutation,
			const std::function<void()>& commitCompleted)
		{
			bool clearCurrent = true;
			bool loadedAny = false;
			for (const auto& fileName : fileNames)
			{
				if (fileName.empty())
				{
					continue;
				}

				bool candidateMutationStarted = false;
				const std::function<void()> candidateBeforeMutation =
					[&]()
					{
						candidateMutationStarted = true;
						beforeMutation();
					};
				bool loaded = false;
				try
				{
					NPCManager::PreparedLoad preparedLoad;
					loaded = gameManager->npcManager->prepareLoad(
						fileName,
						preparedLoad) &&
						gameManager->npcManager->commitPreparedLoad(
							preparedLoad,
							clearCurrent,
							candidateBeforeMutation,
							{},
							true);
				}
				catch (const std::exception& error)
				{
					if (loadedAny ||
						candidateMutationStarted)
					{
						throw;
					}
					GameLog::write(
						"ScriptAPI: initial NPC collection candidate %s threw before mutation: %s\n",
						fileName.c_str(),
						error.what());
					continue;
				}
				catch (...)
				{
					if (loadedAny ||
						candidateMutationStarted)
					{
						throw;
					}
					GameLog::write(
						"ScriptAPI: initial NPC collection candidate %s threw an unknown exception before mutation\n",
						fileName.c_str());
					continue;
				}
				if (!loaded)
				{
					if (loadedAny ||
						candidateMutationStarted)
					{
						return false;
					}
					continue;
				}
				if (clearCurrent)
				{
					gameManager->global.data.npcName =
						fileName;
					clearCurrent = false;
				}
				loadedAny = true;
			}
			if (!loadedAny)
			{
				return false;
			}
			gameManager->global.data.npcName = "";
			commitCompleted();
			return true;
		});
}

bool ScriptAPI::loadObject(const std::string& fileName)
{
	return loadObjectWithPreparationCheckpoint(fileName, {});
}

bool ScriptAPI::loadObjectWithPreparationCheckpoint(
	const std::string& fileName,
	const std::function<bool()>& preparationCheckpoint,
	const std::function<bool(
		const std::function<void()>& beforeMutation,
		const std::function<bool()>& preparationCheckpoint)>&
			preparedLoadCommit)
{
	if (engine == nullptr || !engine->isMainThread())
	{
		GameLog::write(
			"ScriptAPI: object commit must run on the SDL main thread\n");
		return false;
	}
	return runOwnerWorldCommit(
		"object",
		[this,
		 &fileName,
		 &preparationCheckpoint,
		 &preparedLoadCommit](
			const std::function<void()>& beforeMutation,
			const std::function<void()>& commitCompleted)
		{
			const bool loaded = preparedLoadCommit
				? preparedLoadCommit(
					beforeMutation,
					preparationCheckpoint)
				: gameManager->objectManager->load(
					fileName,
					beforeMutation,
					preparationCheckpoint);
			if (!loaded)
			{
				return false;
			}
			gameManager->global.data.objName = fileName;
			commitCompleted();
			return true;
		});
}

bool ScriptAPI::loadObjectFromExactRoot(
	const EditorRun::SearchRoot& root,
	const std::string& virtualPath)
{
	if (engine == nullptr || !engine->isMainThread())
	{
		GameLog::write(
			"ScriptAPI: exact-root object commit must run on the SDL main thread\n");
		return false;
	}
	if (gameManager == nullptr || gameManager->objectManager == nullptr)
	{
		return false;
	}
	RootedResourceReader::Result objectBytes =
		readExactEditorRunResource(
			root,
			virtualPath,
			ObjectPersistence::MaximumObjectFileBytes,
			"object");
	return objectBytes.succeeded() &&
		loadObjectFromEditorRunBytes(
			virtualPath,
			std::move(objectBytes.bytes));
}

bool ScriptAPI::loadObjectFromEditorRunRoots(
	const std::vector<EditorRun::SearchRoot>& roots,
	const std::string& virtualPath)
{
	if (gameManager == nullptr || gameManager->objectManager == nullptr)
	{
		return false;
	}
	EditorRunResourceRead objectBytes = readFirstEditorRunResource(
		roots,
		virtualPath,
		ObjectPersistence::MaximumObjectFileBytes,
		"object");
	if (objectBytes.result.status == RootedResourceReader::Status::NotFound)
	{
		GameLog::write(
			"ScriptAPI: editor-run object resource is missing; using an empty object list %s\n",
			virtualPath.c_str());
		return runOwnerWorldCommit(
			"empty editor-run object",
			[this](
				const std::function<void()>& beforeMutation,
				const std::function<void()>& commitCompleted)
			{
				beforeMutation();
				gameManager->objectManager->clearObj();
				gameManager->global.data.objName.clear();
				commitCompleted();
				return true;
			});
	}
	return objectBytes.result.succeeded() &&
		loadObjectFromEditorRunBytes(
			virtualPath,
			std::move(objectBytes.result.bytes));
}

bool ScriptAPI::loadObjectFromEditorRunBytes(
	const std::string& virtualPath,
	std::vector<std::uint8_t> bytes)
{
	if (engine == nullptr || !engine->isMainThread() ||
		gameManager == nullptr || gameManager->objectManager == nullptr)
	{
		return false;
	}
	return runOwnerWorldCommit(
		"exact-root object",
		[this, &virtualPath, &bytes](
			const std::function<void()>& beforeMutation,
			const std::function<void()>& commitCompleted)
		{
			if (!gameManager->objectManager->
					loadExactResourceBytes(
						virtualPath,
						bytes,
						beforeMutation))
			{
				return false;
			}
			gameManager->global.data.objName = virtualPath;
			commitCompleted();
			return true;
		});
}

bool ScriptAPI::setEditorRunPlayerPositionAndCamera(
	std::int32_t x,
	std::int32_t y)
{
	if (engine == nullptr || !engine->isMainThread())
	{
		GameLog::write(
			"ScriptAPI: editor-run player commit must run on the SDL main thread\n");
		return false;
	}
	if (gameManager == nullptr ||
		gameManager->map == nullptr ||
		gameManager->map->data == nullptr ||
		gameManager->player == nullptr ||
		gameManager->npcManager == nullptr ||
		gameManager->camera == nullptr)
	{
		return false;
	}

	const Point position =
	{
		static_cast<int>(x),
		static_cast<int>(y)
	};
	if (!gameManager->map->isInMap(position))
	{
		GameLog::write(
			"ScriptAPI: editor-run player position is outside the map %d,%d\n",
			position.x,
			position.y);
		return false;
	}

	gameManager->player->forceBeginStand();
	gameManager->player->setPosition(position, false);
	gameManager->player->setOffset({ 0, 0 });
	gameManager->player->haveAsyncDest = false;
	gameManager->player->visible = true;
	gameManager->npcManager->setPartnerPos(
		position.x,
		position.y,
		gameManager->player->direction);
	gameManager->camera->resetView();
	gameManager->player->suppressTrapAtScriptPosition();
	return true;
}

bool ScriptAPI::loadGameFromGeneration(
	const std::string& generationDirectory,
	const std::function<bool()>& ownerCheckpoint,
	bool allowMissingNpcList,
	bool allowMissingObjectList,
	const PreparedSaveLoadCallbacks& preparedCallbacks)
{
	try
	{
		if (!ownerCheckpointCanContinue(
				ownerCheckpoint))
		{
			return false;
		}
		if (!File::recoverDirectoryCopy(
				generationDirectory))
		{
			return false;
		}
		SaveFileManager::CurrentPathScope generationPath(
			generationDirectory);
		if (!generationPath.valid() ||
			!File::fileExist(
				SaveFileManager::CurrentPath() +
				GLOBAL_INI))
		{
			return false;
		}
		return loadCurrentGame(
			ownerCheckpoint,
			allowMissingNpcList,
			allowMissingObjectList,
			preparedCallbacks);
	}
	catch (...)
	{
		GameLog::write(
			"ScriptAPI: exception while loading generation %s\n",
			generationDirectory.c_str());
		return false;
	}
}

GameLoading::LoadingTaskResult
ScriptAPI::commitPreparedSaveGeneration(
	const std::string& preparedDirectory,
	const SaveGenerationPreflightPolicy& policy,
	const std::function<bool()>& ownerCheckpoint,
	const std::function<bool(
		const std::string& generationDirectory,
		const std::function<bool()>& ownerCheckpoint)>&
			generationLoadOverride)
{
	try
	{
		if (!ownerCheckpointCanContinue(ownerCheckpoint))
		{
			return GameLoading::LoadingTaskResult::cancellation();
		}
		const bool loaded = generationLoadOverride
			? generationLoadOverride(
				preparedDirectory,
				ownerCheckpoint)
			: loadGameFromGeneration(
				preparedDirectory,
				ownerCheckpoint,
				true,
				true);
		if (!loaded)
		{
			if (!ownerCheckpointCanContinue(ownerCheckpoint))
			{
				return GameLoading::LoadingTaskResult::cancellation(
					"Save load was cancelled.");
			}
			GameLog::write(
				"ScriptAPI: save load failed; discarding the partial world and returning to title\n");
			discardPartialWorldAfterFailedCommit();
			returnToTitle();
			return GameLoading::LoadingTaskResult::failure(
				"Save load failed; returned to title.");
		}

		const SaveGenerationResult publication =
			SaveFileManager::PublishPreparedLoadCandidateToCurrent(
				policy.limits,
				policy.cancellationRequested);
		if (!publication.succeeded())
		{
			GameLog::write(
				"ScriptAPI: loaded save successfully but could not refresh save/game error=%s path=%s\n",
				SaveFileManager::DescribeSaveGenerationError(
					publication.error),
				publication.errorPath.c_str());
		}
		return GameLoading::LoadingTaskResult::success();
	}
	catch (...)
	{
		GameLog::write(
			"ScriptAPI: save load threw; discarding the partial world and returning to title\n");
		discardPartialWorldAfterFailedCommit();
		returnToTitle();
		return GameLoading::LoadingTaskResult::failure(
			"Save load failed with an exception; returned to title.",
			std::current_exception());
	}
}

bool ScriptAPI::loadGame(int index)
{
	if (engine == nullptr || !engine->isMainThread())
	{
		GameLog::write(
			"ScriptAPI: save commit must run on the SDL main thread\n");
		return false;
	}
	if (engine->isApplicationQuitRequested())
	{
		return false;
	}
	presentSynchronousLoadingStatusFrame(u8"读取游戏中");
	if (!SaveFileManager::RecoverInterruptedSaveOperations())
	{
		GameLog::write(
			"ScriptAPI: save recovery was incomplete; attempting the selected slot normally\n");
	}
	SaveFileManager::OperationScope loadOperation;
	SaveFileManager::ScratchGenerationScope candidateCleanup(
		LoadCandidateGeneration);
	if (!candidateCleanup.valid())
	{
		GameLog::write(
			"ScriptAPI: invalid load candidate cleanup path\n");
		return false;
	}
	const SaveGenerationPreflightPolicy policy =
		createRuntimeSaveGenerationPolicy(
			*gameManager,
			RuntimeSaveGenerationPolicyMode::CompatibleLoad);
	std::string preparedDirectory;
	const SaveGenerationResult preparation =
		prepareLoadGeneration(
			index,
			policy,
			preparedDirectory);
	if (!preparation.succeeded())
	{
		GameLog::write(
			"ScriptAPI: save preparation failed index=%d error=%s path=%s\n",
			index,
			SaveFileManager::DescribeSaveGenerationError(
				preparation.error),
			preparation.errorPath.c_str());
		return false;
	}
	PreparedSaveResources preparedResources;
	if (!prepareSaveResources(
			gameManager,
			preparedDirectory,
			preparedResources,
			{}))
	{
		GameLog::write(
			"ScriptAPI: save resource preparation failed index=%d\n",
			index);
		return false;
	}
	PreparedSaveLoadCallbacks preparedCallbacks;
	preparedCallbacks.mapFolderName =
		preparedResources.mapFolderName;
	preparedCallbacks.commitMap =
		[this, &preparedResources](
			const std::function<void()>& beforeMutation,
			const std::function<bool()>& preparationCheckpoint)
		{
			return gameManager->map->commitPreparedLoadCandidate(
				std::move(preparedResources.map),
				beforeMutation,
				preparationCheckpoint);
		};
	preparedCallbacks.commitNpc =
		[this, &preparedResources](
			const std::function<bool()>& preparationCheckpoint)
		{
			return gameManager->npcManager->commitPreparedLoad(
				preparedResources.npc,
				true,
				{},
				preparationCheckpoint);
		};
	preparedCallbacks.commitObject =
		[this, &preparedResources](
			const std::function<bool()>& preparationCheckpoint)
		{
			return gameManager->objectManager->commitPreparedLoad(
				std::move(preparedResources.object),
				{},
				preparationCheckpoint);
		};
	const auto generationLoad =
		[this,
		 &preparedDirectory,
		 &preparedCallbacks](
			const std::string& generationDirectory,
			const std::function<bool()>& checkpoint)
		{
			const bool usePreparedResources =
				generationDirectory == preparedDirectory;
			return loadGameFromGeneration(
				generationDirectory,
				checkpoint,
				usePreparedResources,
				usePreparedResources,
				usePreparedResources
					? preparedCallbacks
					: PreparedSaveLoadCallbacks{});
		};
	const GameLoading::LoadingTaskResult commitResult =
		commitPreparedSaveGeneration(
			preparedDirectory,
			policy,
			{},
			generationLoad);
	if (!commitResult.succeeded())
	{
		GameLog::write(
			"ScriptAPI: save commit failed index=%d message=%s\n",
			index,
			commitResult.message.c_str());
		return false;
	}
	return true;
}

bool ScriptAPI::loadCurrentGame(
	const std::function<bool()>& ownerCheckpoint,
	bool allowMissingNpcList,
	bool allowMissingObjectList,
	const PreparedSaveLoadCallbacks& preparedCallbacks)
{
	stopMusic();

	gameManager->initAllTime();
	gameManager->setPaused(true);
	FunctionScopeExit pauseCleanup(
		[this]()
		{
			gameManager->setPaused(false);
		});
	if (!ownerCheckpointCanContinue(
			ownerCheckpoint))
	{
		return false;
	}

	if (!gameManager->global.load())
	{
		return false;
	}
	gameManager->global.loadUiSettings();
	gameManager->goodsManager.configureLayout();
	gameManager->magicManager.configureLayout();
	if (!ownerCheckpointCanContinue(
			ownerCheckpoint))
	{
		return false;
	}
	std::string tempNpcName = gameManager->global.data.npcName;
	std::string tempObjName = gameManager->global.data.objName;
	if (!loadMapWithFailurePolicy(
			gameManager->global.data.mapName,
			false,
			false,
			ownerCheckpoint,
			preparedCallbacks.commitMap,
			false,
			preparedCallbacks.mapFolderName,
			MapActorResetMode::ReplaceAllForSaveLoad))
	{
		return false;
	}
	if (!ownerCheckpointCanContinue(
			ownerCheckpoint))
	{
		return false;
	}
	gameManager->global.data.npcName = tempNpcName;
	gameManager->global.data.objName = tempObjName;
	gameManager->traps.load();

	gameManager->effectManager->freeResource();

	gameManager->varList.load();
	// Older compatible saves may omit memo data and intentionally load an empty
	// memo. If either alias exists, Memo::load validates and commits it
	// atomically; failure must abort so the outer save-load transaction restores
	// the captured live generation.
	if (!gameManager->memo.load(true))
	{
		return false;
	}

	gameManager->player->load(gameManager->global.data.characterIndex);
	if (!ownerCheckpointCanContinue(
			ownerCheckpoint))
	{
		return false;
	}

	gameManager->magicManager.load(gameManager->global.data.characterIndex);

	gameManager->goodsManager.load(gameManager->global.data.characterIndex);
	if (!ownerCheckpointCanContinue(
			ownerCheckpoint))
	{
		return false;
	}

	gameManager->partnerManager.load(gameManager->global.data.characterIndex);

	const std::string& npcListName =
		gameManager->global.data.npcName;
	const bool missingNpcList =
		allowMissingNpcList &&
		!npcOrObjectListExists(npcListName);
	if (missingNpcList && !preparedCallbacks.commitNpc)
	{
		GameLog::write(
			"ScriptAPI: compatible save is missing NPC list %s; loading an empty list\n",
			npcListName.c_str());
	}
	if (!(preparedCallbacks.commitNpc
			? preparedCallbacks.commitNpc(ownerCheckpoint)
			: (missingNpcList
				? gameManager->npcManager->
					loadExactResourceBytes(
						npcListName,
						compatibleEmptyEntityListBytes(),
						true,
						{},
						ownerCheckpoint)
				: gameManager->npcManager->load(
					npcListName,
					true,
					{},
					ownerCheckpoint))))
	{
		return false;
	}

	const std::string& objectListName =
		gameManager->global.data.objName;
	const bool missingObjectList =
		allowMissingObjectList &&
		!npcOrObjectListExists(objectListName);
	if (missingObjectList && !preparedCallbacks.commitObject)
	{
		GameLog::write(
			"ScriptAPI: compatible save is missing object list %s; loading an empty list\n",
			objectListName.c_str());
	}
	if (!(preparedCallbacks.commitObject
			? preparedCallbacks.commitObject(ownerCheckpoint)
			: (missingObjectList
				? gameManager->objectManager->
					loadExactResourceBytes(
						objectListName,
						compatibleEmptyEntityListBytes(),
						{},
						ownerCheckpoint)
				: gameManager->objectManager->load(
					objectListName,
					{},
					ownerCheckpoint))))
	{
		return false;
	}
	if (!ownerCheckpointCanContinue(
			ownerCheckpoint))
	{
		return false;
	}

	gameManager->effectManager->load();

	if (gameManager->map->data != nullptr)
	{
		gameManager->map->createDataMap();
	}

	gameManager->weather->reset();

	gameManager->clearMenu();
	gameManager->loadScriptRuntimeState();
	if (!ownerCheckpointCanContinue(
			ownerCheckpoint))
	{
		return false;
	}

	gameManager->weather->setFadeLum(gameManager->global.data.fadeLum);

	pauseCleanup.run();
	playMusic(gameManager->global.data.bgmName);
	if (!ownerCheckpointCanContinue(
			ownerCheckpoint))
	{
		return false;
	}

	gameManager->weather->setSnowVisible(gameManager->global.data.snowShow);
	if (gameManager->global.data.rainShow)
	{
		if (gameManager->global.data.rainFile.empty())
		{
			gameManager->weather->setRainWeather(wtLightning);
		}
		else
		{
			gameManager->weather->setRainWeather(wtCustomRain, gameManager->global.data.rainFile);
		}
	}

	gameManager->weather->setLum(gameManager->global.data.mainLum);
	gameManager->weather->setTime(gameManager->global.data.mapTime);
	gameManager->menu->applyLayoutByGameType();
	gameManager->menu->updateAfterGameLoad();

	setPlayerScn(false);

	// All load commits run on the owner thread after any prepare worker joins.
	gameManager->camera->snapToFollowTarget();
	gameManager->camera->differencePosition = { 0.0, 0.0 };
	int viewportWidth = 0;
	int viewportHeight = 0;
	engine->getWindowSize(viewportWidth, viewportHeight);
	if (!gameManager->map->warmVisibleTextures(
			viewportWidth,
			viewportHeight,
			gameManager->camera->position,
			ownerCheckpoint,
			8))
	{
		return false;
	}

	return true;
}
