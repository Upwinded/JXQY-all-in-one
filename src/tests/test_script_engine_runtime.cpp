#include "../Game/Script/Script.h"
#include "../Game/Script/ScriptNpcAction.h"
#include "../Game/Data/ColorStyle.h"
#include "../Game/Data/Goods.h"
#include "../Game/Data/Magic.h"
#include "../Game/Data/Map.h"
#include "../Game/Data/NPC.h"
#include "../Game/Data/NPCManager.h"
#include "../Game/Data/NPCAction/NPCActionManager.h"
#include "../Game/GameManager/GameManager.h"
#include "../Game/GameManager/ScriptRuntimeState.h"
#include "../Element/Element.h"
#include "../Engine/Engine.h"
#include "../Image/IMP.h"
#include "../Launch/EditorRunRuntimeTraceWriter.h"
#include "../Resource/ResourceManifest.h"

#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class ScriptEngineRuntimeTestAccess
{
public:
	static int execute(Script& script, const std::string& source)
	{
		auto data = std::make_unique<char[]>(source.size());
		std::memcpy(data.get(), source.data(), source.size());
		return script.runScript(data, static_cast<int>(source.size()));
	}

	static int stackTop(const Script& script)
	{
		return lua_gettop(script.luaState);
	}

	static int globalType(const Script& script, const char* name)
	{
		const int initialStackTop = lua_gettop(script.luaState);
		const int type = lua_getglobal(script.luaState, name);
		lua_settop(script.luaState, initialStackTop);
		return type;
	}

	static int globalTableFieldType(
		const Script& script,
		const char* tableName,
		const char* fieldName)
	{
		const int initialStackTop = lua_gettop(script.luaState);
		int type = lua_getglobal(script.luaState, tableName);
		if (type == LUA_TTABLE)
		{
			type = lua_getfield(script.luaState, -1, fieldName);
		}
		lua_settop(script.luaState, initialStackTop);
		return type;
	}

	static int registryTableFieldType(
		const Script& script,
		const char* tableName,
		const char* fieldName)
	{
		const int initialStackTop = lua_gettop(script.luaState);
		int type = lua_getfield(script.luaState, LUA_REGISTRYINDEX, tableName);
		if (type == LUA_TTABLE)
		{
			type = lua_getfield(script.luaState, -1, fieldName);
		}
		lua_settop(script.luaState, initialStackTop);
		return type;
	}

	static bool globalMatchesRegistryTableField(
		const Script& script,
		const char* globalName,
		const char* tableName,
		const char* fieldName)
	{
		const int initialStackTop = lua_gettop(script.luaState);
		lua_getglobal(script.luaState, globalName);
		const int globalIndex = lua_absindex(script.luaState, -1);
		bool matches = false;
		if (lua_getfield(script.luaState, LUA_REGISTRYINDEX, tableName) ==
			LUA_TTABLE)
		{
			lua_getfield(script.luaState, -1, fieldName);
			matches = lua_rawequal(script.luaState, globalIndex, -1) != 0;
		}
		lua_settop(script.luaState, initialStackTop);
		return matches;
	}

	static int globalTableEntryCount(
		const Script& script,
		const char* tableName)
	{
		const int initialStackTop = lua_gettop(script.luaState);
		int count = -1;
		if (lua_getglobal(script.luaState, tableName) == LUA_TTABLE)
		{
			count = 0;
			const int tableIndex = lua_absindex(script.luaState, -1);
			lua_pushnil(script.luaState);
			while (lua_next(script.luaState, tableIndex) != 0)
			{
				++count;
				lua_pop(script.luaState, 1);
			}
		}
		lua_settop(script.luaState, initialStackTop);
		return count;
	}

	static void registerNestedRunner(Script& script)
	{
		lua_pushlightuserdata(script.luaState, &script);
		lua_pushcclosure(script.luaState, runNested, 1);
		lua_setglobal(script.luaState, "test_run_nested");
	}

	static void registerQuitRequester(Script& script)
	{
		script.registerLuaFunction("test_request_application_quit", requestApplicationQuit);
	}

	static void registerTraceNestedRunner(Script& script)
	{
		lua_pushlightuserdata(script.luaState, &script);
		lua_pushcclosure(
			script.luaState,
			runTraceNested,
			1);
		lua_setglobal(
			script.luaState,
			"test_run_trace_nested");
	}

	static bool nestedStackWasPreserved()
	{
		return nestedStackPreserved;
	}

	static bool nestedTraceRunningWasPreserved()
	{
		return nestedTraceRunningPreserved;
	}

	static void setCurrentBgmName(GameManager& gameManager, const std::string& name)
	{
		gameManager.bgmName = name;
	}

	static const std::string& currentBgmName(const GameManager& gameManager)
	{
		return gameManager.bgmName;
	}

	static bool isLogicRunning(const GameManager& gameManager)
	{
		return gameManager.logicRunning;
	}

	static bool runPendingPlayerDeathScript(GameManager& gameManager)
	{
		return gameManager.runPendingPlayerDeathScript();
	}

private:
	static int runNested(lua_State* luaState)
	{
		auto* script = static_cast<Script*>(lua_touserdata(luaState, lua_upvalueindex(1)));
		const int initialStackTop = lua_gettop(luaState);
		const int result = execute(*script, "return 'nested-result'");
		nestedStackPreserved = result == LUA_OK && lua_gettop(luaState) == initialStackTop;
		lua_pushinteger(luaState, initialStackTop);
		return 1;
	}

	static int runTraceNested(lua_State* luaState)
	{
		auto* script = static_cast<Script*>(
			lua_touserdata(
				luaState,
				lua_upvalueindex(1)));
		const bool runningBefore = script->running;
		ResolvedTraceScriptSource source;
		const std::string nestedSource =
			"printf('nested trace')\n";
		source.bytes.assign(
			nestedSource.begin(),
			nestedSource.end());
		source.identity.virtualPath =
			"script/common/nested.lua";
		source.identity.rootKind =
			EditorRun::RuntimeTraceRootKind::Active;
		source.identity.rootOrdinal = 0;
		source.identity.resourcePackId = "test-pack";
		source.identity.sourceLayer =
			EditorRun::RuntimeTraceSourceLayer::Formal;
		const ExactScriptExecutionResult result =
			script->runResolvedTraceScriptSource(
				std::move(source));
		nestedTraceRunningPreserved =
			runningBefore &&
			script->running &&
			result.succeeded();
		return 0;
	}

	static int requestApplicationQuit(lua_State*)
	{
		Engine::getInstance()->requestApplicationQuit();
		return 0;
	}

	inline static bool nestedStackPreserved = false;
	inline static bool nestedTraceRunningPreserved =
		false;
};

namespace
{
enum class RecordedNpcAction
{
	None,
	Stand,
	Walk,
	Run,
	Jump,
	Attack,
	Magic,
	Sit,
	Hurt,
	Death,
	Special,
};

class RecordingNPC : public NPC
{
public:
	void resetRecording()
	{
		recordedAction = RecordedNpcAction::None;
		recordedDestination = {};
		specialActionFile.clear();
		specialActionStartCount = 0;
		eventRunCount = 0;
		goToCount = 0;
		goToExCount = 0;
		goToDirCount = 0;
		recordedDirection = -1;
		recordedDistance = -1;
		initResCount = 0;
		setLevelCount = 0;
		loadActionFileCount = 0;
		loadedActionFile.clear();
		loadedAction = -1;
		specialActionStartSucceeds = true;
		inputEnabledDuringEventRun = true;
		inputEnabledDuringBlockingMove = true;
		fightState.set(false);
	}

	void beginStand() override
	{
		recordedAction = RecordedNpcAction::Stand;
	}

	void beginWalk(Point destination) override
	{
		recordedAction = RecordedNpcAction::Walk;
		recordedDestination = destination;
	}

	void beginRun(Point destination) override
	{
		recordedAction = RecordedNpcAction::Run;
		recordedDestination = destination;
	}

	void beginJump(Point destination) override
	{
		recordedAction = RecordedNpcAction::Jump;
		recordedDestination = destination;
	}

	void beginAttack(Point destination, std::shared_ptr<GameElement>) override
	{
		recordedAction = RecordedNpcAction::Attack;
		recordedDestination = destination;
	}

	void beginMagic(Point destination, std::shared_ptr<GameElement>) override
	{
		recordedAction = RecordedNpcAction::Magic;
		recordedDestination = destination;
	}

	void beginSit() override
	{
		recordedAction = RecordedNpcAction::Sit;
	}

	void beginHurt() override
	{
		recordedAction = RecordedNpcAction::Hurt;
	}

	void beginDie() override
	{
		recordedAction = RecordedNpcAction::Death;
	}

	void beginSpecial() override
	{
		recordedAction = RecordedNpcAction::Special;
	}

	void goTo(Point destination) override
	{
		recordedDestination = destination;
		inputEnabledDuringBlockingMove =
			gm == nullptr || gm->global.data.canInput;
		++goToCount;
	}

	void goToEx(Point destination) override
	{
		recordedDestination = destination;
		++goToExCount;
	}

	void goToDir(int direction, int distance) override
	{
		recordedDirection = direction;
		recordedDistance = distance;
		inputEnabledDuringBlockingMove =
			gm == nullptr || gm->global.data.canInput;
		++goToDirCount;
	}

	void initRes(const std::string&) override
	{
		++initResCount;
	}

	void setLevel(int newLevel) override
	{
		level = newLevel;
		++setLevelCount;
	}

	void loadActionFile(const std::string& fileName, int action) override
	{
		loadedActionFile = fileName;
		loadedAction = action;
		++loadActionFileCount;
	}

	bool startScriptSpecialAction(const std::string& fileName) override
	{
		specialActionFile = fileName;
		++specialActionStartCount;
		if (specialActionStartSucceeds)
		{
			recordedAction = RecordedNpcAction::Special;
		}
		return specialActionStartSucceeds;
	}

	unsigned int eventRun() override
	{
		++eventRunCount;
		inputEnabledDuringEventRun = gm == nullptr || gm->global.data.canInput;
		return 0;
	}

	RecordedNpcAction recordedAction = RecordedNpcAction::None;
	Point recordedDestination = {};
	std::string specialActionFile;
	int specialActionStartCount = 0;
	int eventRunCount = 0;
	int goToCount = 0;
	int goToExCount = 0;
	int goToDirCount = 0;
	int recordedDirection = -1;
	int recordedDistance = -1;
	int initResCount = 0;
	int setLevelCount = 0;
	int loadActionFileCount = 0;
	std::string loadedActionFile;
	int loadedAction = -1;
	bool specialActionStartSucceeds = true;
	bool inputEnabledDuringEventRun = true;
	bool inputEnabledDuringBlockingMove = true;
};

class InMemorySpecialActionNPC : public NPC
{
public:
	void loadSpecialAction(const std::string& fileName) override
	{
		loadedFileName = fileName;
		++loadCount;
		res.special.imageFile = fileName;
		res.special.imagePackage = nullptr;
		if (provideResource)
		{
			res.special.imagePackage = std::make_shared<IMPImage>();
			res.special.imagePackage->directions = 1;
			res.special.imagePackage->interval = 50;
			res.special.imagePackage->frame.resize(1);
		}
	}

	unsigned int eventRun() override
	{
		++eventRunCount;
		return 0;
	}

	void advanceFrameForTest(UTime elapsedMilliseconds)
	{
		setTime(getTime() + elapsedMilliseconds);
		frameTime = elapsedMilliseconds;
		onUpdate();
	}

	std::string loadedFileName;
	int loadCount = 0;
	int eventRunCount = 0;
	bool provideResource = true;
};

class BlockingInMemorySpecialActionNPC : public NPC
{
public:
	void loadSpecialAction(const std::string& fileName) override
	{
		res.special.imageFile = fileName;
		res.special.imagePackage = std::make_shared<IMPImage>();
		res.special.imagePackage->directions = 1;
		res.special.imagePackage->interval = 1;
		res.special.imagePackage->frame.resize(1);
	}
};

bool check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}

ResourceManifest explicitOriginalBehavior(int originalType)
{
	ResourceManifest manifest;
	manifest.type = originalType;
	manifest.typeDefined = true;
	const bool trilogy = originalType == GAME_YYCS ||
		originalType == GAME_XJXQY;
	manifest.levelUpThresholdMode = trilogy
		? LevelUpThresholdMode::GreaterThan
		: LevelUpThresholdMode::GreaterThanOrEqual;
	manifest.levelUpThresholdModeDefined = true;
	manifest.partnerFollowRadius = trilogy ? 2 : 1;
	manifest.partnerFollowRadiusDefined = true;
	manifest.partnerFollowRunRadius = 5;
	manifest.partnerFollowRunRadiusDefined = true;
	manifest.npcActionProfile = originalType == GAME_YYCS
		? ScriptNpcActionProfile::Yycs
		: (originalType == GAME_XJXQY
			? ScriptNpcActionProfile::Xjxqy
			: ScriptNpcActionProfile::Legacy);
	manifest.npcActionProfileDefined = true;
	manifest.npcRuntimeProfile = trilogy
		? ScriptNpcRuntimeProfile::Trilogy
		: ScriptNpcRuntimeProfile::Legacy;
	manifest.npcRuntimeProfileDefined = true;
	manifest.specialActionMode = trilogy
		? ScriptSpecialActionMode::Overlay
		: ScriptSpecialActionMode::Replace;
	manifest.specialActionModeDefined = true;
	manifest.addLifeMode = trilogy
		? ScriptAddLifeMode::DirectClamp
		: ScriptAddLifeMode::PlayerRules;
	manifest.addLifeModeDefined = true;
	return manifest;
}

void applyOriginalBehavior(Global& global, int originalType)
{
	global.applyResourceManifestFeatures(
		explicitOriginalBehavior(originalType));
}

struct LuaTypeExpectation
{
	const char* name;
	int type;
};

bool checkGlobalTypes(
	Script& script,
	const std::vector<LuaTypeExpectation>& expectations,
	const char* profileName)
{
	bool ok = true;
	for (const auto& expectation : expectations)
	{
		const int actualType =
			ScriptEngineRuntimeTestAccess::globalType(script, expectation.name);
		const std::string message =
			std::string(profileName) + " global '" + expectation.name +
			"' has the expected type";
		ok = check(actualType == expectation.type, message.c_str()) && ok;
	}
	return ok;
}

bool checkGlobalTableFieldTypes(
	Script& script,
	const char* tableName,
	const std::vector<LuaTypeExpectation>& expectations,
	const char* profileName)
{
	bool ok = true;
	for (const auto& expectation : expectations)
	{
		const int actualType =
			ScriptEngineRuntimeTestAccess::globalTableFieldType(
				script, tableName, expectation.name);
		const std::string message =
			std::string(profileName) + " table '" + tableName + "." +
			expectation.name + "' has the expected type";
		ok = check(actualType == expectation.type, message.c_str()) && ok;
	}
	return ok;
}

bool runScriptLibraryProfileTests()
{
	bool ok = true;
	Script fullBefore;
	Script editorRunSafe(ScriptLibraryProfile::EditorRunSafe);
	Script fullAfter;

	const std::vector<LuaTypeExpectation> fullGlobals = {
		{ "collectgarbage", LUA_TFUNCTION },
		{ "dofile", LUA_TFUNCTION },
		{ "load", LUA_TFUNCTION },
		{ "loadfile", LUA_TFUNCTION },
		{ "require", LUA_TFUNCTION },
		{ "package", LUA_TTABLE },
		{ "io", LUA_TTABLE },
		{ "debug", LUA_TTABLE },
		{ "os", LUA_TTABLE },
	};
	ok = checkGlobalTypes(fullBefore, fullGlobals, "full-before") && ok;
	ok = checkGlobalTypes(fullAfter, fullGlobals, "full-after") && ok;

	const std::vector<LuaTypeExpectation> fullOsFields = {
		{ "clock", LUA_TFUNCTION },
		{ "date", LUA_TFUNCTION },
		{ "difftime", LUA_TFUNCTION },
		{ "execute", LUA_TFUNCTION },
		{ "exit", LUA_TFUNCTION },
		{ "getenv", LUA_TFUNCTION },
		{ "remove", LUA_TFUNCTION },
		{ "rename", LUA_TFUNCTION },
		{ "setlocale", LUA_TFUNCTION },
		{ "time", LUA_TFUNCTION },
		{ "tmpname", LUA_TFUNCTION },
	};
	ok = checkGlobalTableFieldTypes(
		fullBefore, "os", fullOsFields, "full-before") && ok;
	ok = checkGlobalTableFieldTypes(
		fullAfter, "os", fullOsFields, "full-after") && ok;

	const std::vector<LuaTypeExpectation> safeAllowedGlobals = {
		{ "_G", LUA_TTABLE },
		{ "_VERSION", LUA_TSTRING },
		{ "assert", LUA_TFUNCTION },
		{ "pairs", LUA_TFUNCTION },
		{ "pcall", LUA_TFUNCTION },
		{ "tostring", LUA_TFUNCTION },
		{ "type", LUA_TFUNCTION },
		{ "coroutine", LUA_TTABLE },
		{ "math", LUA_TTABLE },
		{ "os", LUA_TTABLE },
		{ "string", LUA_TTABLE },
		{ "table", LUA_TTABLE },
		{ "utf8", LUA_TTABLE },
		{ "assign", LUA_TFUNCTION },
		{ "runscript", LUA_TFUNCTION },
		{ "printf", LUA_TFUNCTION },
	};
	ok = checkGlobalTypes(
		editorRunSafe, safeAllowedGlobals, "editor-run-safe") && ok;

	const std::vector<LuaTypeExpectation> safeBlockedGlobals = {
		{ "collectgarbage", LUA_TNIL },
		{ "dofile", LUA_TNIL },
		{ "load", LUA_TNIL },
		{ "loadfile", LUA_TNIL },
		{ "require", LUA_TNIL },
		{ "package", LUA_TNIL },
		{ "io", LUA_TNIL },
		{ "debug", LUA_TNIL },
	};
	ok = checkGlobalTypes(
		editorRunSafe, safeBlockedGlobals, "editor-run-safe") && ok;

	const std::vector<LuaTypeExpectation> safeOsFields = {
		{ "clock", LUA_TFUNCTION },
		{ "date", LUA_TFUNCTION },
		{ "difftime", LUA_TFUNCTION },
		{ "time", LUA_TFUNCTION },
		{ "execute", LUA_TNIL },
		{ "exit", LUA_TNIL },
		{ "getenv", LUA_TNIL },
		{ "remove", LUA_TNIL },
		{ "rename", LUA_TNIL },
		{ "setlocale", LUA_TNIL },
		{ "tmpname", LUA_TNIL },
	};
	ok = checkGlobalTableFieldTypes(
		editorRunSafe, "os", safeOsFields, "editor-run-safe") && ok;
	ok = check(
		ScriptEngineRuntimeTestAccess::globalTableEntryCount(
			editorRunSafe, "os") == 4,
		"editor-run-safe os table contains exactly four approved functions") &&
		ok;

	const std::vector<LuaTypeExpectation> blockedLoadedModules = {
		{ LUA_LOADLIBNAME, LUA_TNIL },
		{ LUA_IOLIBNAME, LUA_TNIL },
		{ LUA_DBLIBNAME, LUA_TNIL },
	};
	for (const auto& expectation : blockedLoadedModules)
	{
		const int actualType =
			ScriptEngineRuntimeTestAccess::registryTableFieldType(
				editorRunSafe, LUA_LOADED_TABLE, expectation.name);
		const std::string message =
			std::string("editor-run-safe loaded registry omits '") +
			expectation.name + "'";
		ok = check(actualType == expectation.type, message.c_str()) && ok;
	}

	const std::vector<const char*> blockedPreloads = {
		LUA_LOADLIBNAME,
		LUA_IOLIBNAME,
		LUA_OSLIBNAME,
		LUA_DBLIBNAME,
	};
	for (const char* name : blockedPreloads)
	{
		const int actualType =
			ScriptEngineRuntimeTestAccess::registryTableFieldType(
				editorRunSafe, LUA_PRELOAD_TABLE, name);
		const std::string message =
			std::string("editor-run-safe preload registry omits '") +
			name + "'";
		ok = check(actualType == LUA_TNIL, message.c_str()) && ok;
	}

	ok = check(
		ScriptEngineRuntimeTestAccess::globalMatchesRegistryTableField(
			editorRunSafe, LUA_GNAME, LUA_LOADED_TABLE, LUA_GNAME),
		"editor-run-safe loaded global table is the sanitized global table") &&
		ok;
	ok = check(
		ScriptEngineRuntimeTestAccess::globalMatchesRegistryTableField(
			editorRunSafe, LUA_OSLIBNAME, LUA_LOADED_TABLE, LUA_OSLIBNAME),
		"editor-run-safe loaded os module is the restricted global os table") &&
		ok;

	const int safeLibraryResult = ScriptEngineRuntimeTestAccess::execute(
		editorRunSafe,
		"assert(math.abs(-4) == 4); "
		"assert(string.upper('a') == 'A'); "
		"local values = {}; table.insert(values, 3); "
		"assert(values[1] == 3); "
		"local thread = coroutine.create(function() return 7 end); "
		"local resumed, value = coroutine.resume(thread); "
		"assert(resumed and value == 7); "
		"assert(utf8.len('A') == 1); "
		"assert(os.difftime(9, 4) == 5)");
	ok = check(
		safeLibraryResult == LUA_OK &&
			ScriptEngineRuntimeTestAccess::stackTop(editorRunSafe) == 0,
		"editor-run-safe approved libraries execute and restore the stack") &&
		ok;

	const int blockedLibraryResult = ScriptEngineRuntimeTestAccess::execute(
		editorRunSafe, "return io.open('editor-run-escape.txt', 'w')");
	ok = check(
		blockedLibraryResult != LUA_OK &&
			ScriptEngineRuntimeTestAccess::stackTop(editorRunSafe) == 0,
		"editor-run-safe blocked library errors restore the stack") && ok;

	return ok;
}

std::vector<std::uint8_t> scriptBytes(
	const std::string& source)
{
	return std::vector<std::uint8_t>(
		source.begin(),
		source.end());
}

bool runExactScriptDiagnosticTests()
{
	bool ok = true;
	Script script(ScriptLibraryProfile::EditorRunSafe);
	const std::string virtualPath =
		u8"script/map/嵌套/入口.lua";

	ExactScriptExecutionResult result =
		script.runExactResourceBytes(
			scriptBytes("local value = 1\nreturn value\n"),
			virtualPath);
	ok = check(
		result.succeeded() &&
			result.line == 0 &&
			result.column == 0 &&
			result.message.empty(),
		"exact Lua success has no synthetic source location") && ok;

	result = script.runExactResourceBytes(
		scriptBytes(
			"local value = 1\n"
			"local broken = )\n"),
		virtualPath);
	ok = check(
		result.status ==
			ExactScriptExecutionStatus::LoadFailed &&
			result.line == 2 &&
			result.column == 16 &&
			result.message ==
				"unexpected symbol near ')'",
		"exact Lua load failure preserves path line and an unambiguous true token column") &&
		ok;

	result = script.runExactResourceBytes(
		scriptBytes(
			u8"local text = '中文'; local broken = )\n"),
		virtualPath);
	ok = check(
		result.status ==
			ExactScriptExecutionStatus::LoadFailed &&
			result.line == 1 &&
			result.column == 35 &&
			result.message ==
				"unexpected symbol near ')'",
		"exact Lua load column is one-based Unicode code points rather than UTF-8 bytes") &&
		ok;

	std::string invalidUtf8Source = "local text = '";
	invalidUtf8Source.push_back(static_cast<char>(0xFF));
	invalidUtf8Source += "'; local broken = )\n";
	result = script.runExactResourceBytes(
		scriptBytes(invalidUtf8Source),
		virtualPath);
	ok = check(
		result.status ==
			ExactScriptExecutionStatus::LoadFailed &&
			result.line == 1 &&
			result.column == 0 &&
			result.message ==
				"unexpected symbol near ')'",
		"exact Lua load column stays unknown when the source line is not valid UTF-8") &&
		ok;

	result = script.runExactResourceBytes(
		scriptBytes("local broken = ) + )\n"),
		virtualPath);
	ok = check(
		result.status ==
			ExactScriptExecutionStatus::LoadFailed &&
			result.line == 1 &&
			result.column == 0,
		"exact Lua load failure leaves an ambiguous token column unknown") &&
		ok;

	result = script.runExactResourceBytes(
		scriptBytes(
			"local value = 1\n"
			"error('12: runtime marker')\n"),
		virtualPath);
	ok = check(
		result.status ==
			ExactScriptExecutionStatus::RuntimeFailed &&
			result.line == 2 &&
			result.column == 0 &&
			result.message == "12: runtime marker",
		"exact Lua runtime message numeric prefix is not misread as a Lua column") &&
		ok;
	ok = check(
		ScriptEngineRuntimeTestAccess::stackTop(script) == 0,
		"exact Lua diagnostics restore the shared stack") && ok;
	return ok;
}

ResolvedTraceScriptSource traceSource(
	const std::string& virtualPath,
	const std::string& contents)
{
	ResolvedTraceScriptSource source;
	source.bytes.assign(
		contents.begin(),
		contents.end());
	source.identity.virtualPath = virtualPath;
	source.identity.rootKind =
		EditorRun::RuntimeTraceRootKind::Active;
	source.identity.rootOrdinal = 0;
	source.identity.resourcePackId = "test-pack";
	source.identity.sourceLayer =
		EditorRun::RuntimeTraceSourceLayer::Formal;
	return source;
}

std::size_t substringCount(
	const std::string& text,
	const std::string& needle)
{
	std::size_t count = 0;
	for (std::size_t offset = 0;
		(offset = text.find(needle, offset)) !=
			std::string::npos;
		offset += needle.size())
	{
		++count;
	}
	return count;
}

bool runRuntimeTraceInstrumentationTests()
{
	bool ok = true;
	std::mutex traceMutex;
	std::string trace;
	std::unique_ptr<EditorRun::RuntimeTraceWriter> writer =
		EditorRun::RuntimeTraceWriter::create(
			"123e4567-e89b-12d3-a456-426614174000",
			[&traceMutex, &trace](
				std::string_view batch)
			{
				std::lock_guard<std::mutex> lock(
					traceMutex);
				trace.append(
					batch.data(),
					batch.size());
				return true;
			});
	ok = check(
		writer != nullptr && writer->valid(),
		"runtime trace writer starts for production Script instrumentation") &&
		ok;
	if (writer == nullptr || !writer->valid())
	{
		return false;
	}

	Script script(
		ScriptLibraryProfile::EditorRunSafe,
		writer.get());
	ScriptEngineRuntimeTestAccess::
		registerTraceNestedRunner(script);
	ScriptEngineRuntimeTestAccess::
		registerQuitRequester(script);

	const ExactScriptExecutionResult outer =
		script.runResolvedTraceScriptSource(
			traceSource(
				"script/common/entry.lua",
				"printf('outer trace')\n"
				"test_run_trace_nested()\n"));
	ok = check(
		outer.succeeded() &&
			ScriptEngineRuntimeTestAccess::
				nestedTraceRunningWasPreserved() &&
			!script.running &&
			script.currentExecutionId() == 0,
		"nested traced execution preserves the outer running state and unwinds its execution stack") &&
		ok;

	const ExactScriptExecutionResult empty =
		script.runResolvedTraceScriptSource(
			traceSource(
				"script/common/empty.lua",
				""));
	ok = check(
		empty.succeeded(),
		"zero-byte resolved editor-run Lua is a completed empty chunk") &&
		ok;

	const ExactScriptExecutionResult loadError =
		script.runResolvedTraceScriptSource(
			traceSource(
				"script/common/loaderror.lua",
				"local broken = )\n"));
	ok = check(
		loadError.status ==
			ExactScriptExecutionStatus::LoadFailed,
		"resolved traced Lua records load failures") &&
		ok;

	const ExactScriptExecutionResult runtimeError =
		script.runResolvedTraceScriptSource(
			traceSource(
				"script/common/runtimeerror.lua",
				"error('trace runtime failure')\n"));
	ok = check(
		runtimeError.status ==
			ExactScriptExecutionStatus::RuntimeFailed,
		"resolved traced Lua records runtime failures") &&
		ok;

	const ExactScriptExecutionResult aborted =
		script.runResolvedTraceScriptSource(
			traceSource(
				"script/common/quit.lua",
				"test_request_application_quit()\n"
				"printf('must not run')\n"));
	ok = check(
		aborted.status ==
			ExactScriptExecutionStatus::RuntimeFailed,
		"application quit aborts resolved traced Lua") &&
		ok;
	Element::resetApplicationQuitState();

	ok = check(
		writer->finish(
			EditorRun::RuntimeTraceSessionFinishStatus::
				Completed),
		"runtime trace writer finishes after Script instrumentation") &&
		ok;
	{
		std::lock_guard<std::mutex> lock(traceMutex);
		ok = check(
			trace.find(
				"\"virtualPath\":\"script/common/entry.lua\"") !=
				std::string::npos,
			"script.start preserves canonical lowercase virtual-path spelling") &&
			ok;
		ok = check(
			trace.find(
				"\"eventType\":\"script.start\",\"elapsedMicroseconds\":") !=
				std::string::npos &&
			trace.find(
				"\"executionId\":2,\"parentExecutionId\":1") !=
				std::string::npos,
			"nested script.start captures the active parent execution") &&
			ok;
		ok = check(
			trace.find(
				"\"eventType\":\"source.line\"") !=
				std::string::npos &&
			substringCount(
				trace,
				"\"eventType\":\"api.call\"") >= 3 &&
			substringCount(
				trace,
				"\"apiName\":\"printf\"") == 2,
			"line hooks and precise registered API names are emitted") &&
			ok;
		ok = check(
			trace.find(
				"\"executionId\":3,\"status\":\"completed\"") !=
				std::string::npos &&
			trace.find(
				"\"executionId\":4,\"status\":\"load-error\"") !=
				std::string::npos &&
			trace.find(
				"\"executionId\":5,\"status\":\"runtime-error\"") !=
				std::string::npos &&
			trace.find(
				"\"executionId\":6,\"status\":\"aborted\"") !=
				std::string::npos,
			"script.finish distinguishes empty success, load error, runtime error, and quit abort") &&
			ok;
	}

	Script noWriter(
		ScriptLibraryProfile::EditorRunSafe);
	ok = check(
		noWriter.runResolvedTraceScriptSource(
			traceSource(
				"script/common/nowriter.lua",
				"printf('ordinary no writer')\n"))
			.succeeded() &&
			!noWriter.running &&
			noWriter.currentExecutionId() == 0,
		"resolved execution remains functional without a trace writer") &&
		ok;
	return ok;
}
}

bool runScriptEngineRuntimeTests()
{
	bool ok = true;
	ok = check(
		resolveMoveScreenArgumentMode(2) ==
			MoveScreenArgumentMode::Distance,
		"two-argument MoveScreen uses the original JXQY2 distance protocol") && ok;
	ok = check(
		resolveMoveScreenArgumentMode(3) ==
			MoveScreenArgumentMode::FrameCountAndSpeed &&
		resolveMoveScreenArgumentMode(4) ==
			MoveScreenArgumentMode::FrameCountAndSpeed,
		"three-or-more-argument MoveScreen uses the JxqyHD frame-count protocol") && ok;
	ok = runScriptLibraryProfileTests() && ok;
	ok = runExactScriptDiagnosticTests() && ok;
	ok = runRuntimeTraceInstrumentationTests() && ok;
	Script script;

	for (int index = 0; index < 4096; index++)
	{
		const int result = ScriptEngineRuntimeTestAccess::execute(script, "return 123");
		ok = check(result == LUA_OK && ScriptEngineRuntimeTestAccess::stackTop(script) == 0,
			"repeated top-level script returns do not accumulate on the shared Lua stack") && ok;
		if (!ok)
		{
			break;
		}
	}

	const int syntaxResult = ScriptEngineRuntimeTestAccess::execute(script, "local =");
	ok = check(syntaxResult != LUA_OK && ScriptEngineRuntimeTestAccess::stackTop(script) == 0,
		"syntax errors restore the shared Lua stack") && ok;

	const int runtimeResult = ScriptEngineRuntimeTestAccess::execute(script, "error('expected failure')");
	ok = check(runtimeResult != LUA_OK && ScriptEngineRuntimeTestAccess::stackTop(script) == 0,
		"runtime errors restore the shared Lua stack") && ok;

	const int malformedStringResult = ScriptEngineRuntimeTestAccess::execute(script,
		"printf(nil); printf({}); return true");
	ok = check(malformedStringResult == LUA_OK && ScriptEngineRuntimeTestAccess::stackTop(script) == 0,
		"non-string Lua arguments are converted without null string construction") && ok;

	ScriptEngineRuntimeTestAccess::registerNestedRunner(script);
	const int nestedResult = ScriptEngineRuntimeTestAccess::execute(script,
		"assert(test_run_nested('first', 'second') == 2); return 'outer-result'");
	ok = check(nestedResult == LUA_OK && ScriptEngineRuntimeTestAccess::nestedStackWasPreserved()
		&& ScriptEngineRuntimeTestAccess::stackTop(script) == 0,
		"nested script execution preserves the caller stack and discards unused chunk returns") && ok;

	GameManager gameManager;
	gameManager.varList.ensureInitialized();
	gameManager.map->data = std::make_shared<MapData>();
	gameManager.map->data->head.width = 8;
	gameManager.map->data->head.height = 8;
	gameManager.map->data->tile.assign(8, std::vector<MapTile>(8));
	gameManager.map->createDataMap();
	gameManager.memo.clear();
	const int goodsQueryIndex = gameManager.goodsManager.storeBegin();
	const GoodsInfo savedGoodsQueryInfo =
		gameManager.goodsManager.goodsList[goodsQueryIndex];
	gameManager.goodsManager.goodsList[goodsQueryIndex].goods =
		std::make_shared<Goods>();
	gameManager.goodsManager.goodsList[goodsQueryIndex].goods->name =
		"CasePotion";
	gameManager.goodsManager.goodsList[goodsQueryIndex].number = 3;
	ScriptEngineRuntimeTestAccess::execute(
		script, "getgoodsnumbyname('casepotion')");
	ok = check(gameManager.varList.getInteger("GoodsNum") == 3,
		"GetGoodsNumByName preserves the YYCS/XJXQY case-insensitive display-name lookup") && ok;
	gameManager.goodsManager.goodsList[goodsQueryIndex] = savedGoodsQueryInfo;
	ScriptEngineRuntimeTestAccess::execute(
		script, "memo('123'); delmemo('123')");
	ok = check(gameManager.memo.memo.empty(),
		"DelMemo removes literal numeric text instead of treating it as a talk-text index") && ok;
	gameManager.varList.setInteger("after_negative_sleep", 0);
	gameManager.global.data.canInput = true;
	ScriptEngineRuntimeTestAccess::execute(
		script, "sleep(-1); assign('after_negative_sleep', 1)");
	ok = check(gameManager.varList.getInteger("after_negative_sleep") == 1
		&& gameManager.global.data.canInput,
		"non-positive Sleep completes immediately without unsigned wraparound") && ok;
	const int preconfiguredTimerResult = ScriptEngineRuntimeTestAccess::execute(
		script,
		"settimescript(0, 'production-timeout.lua'); "
		"opentimelimit(60); hidetimerwnd()");
	ok = check(preconfiguredTimerResult == LUA_OK
		&& gameManager.timerStarted
		&& gameManager.timerHidden
		&& gameManager.timerSeconds == 60
		&& gameManager.timeScriptSet
		&& gameManager.timeScriptSeconds == 0
		&& gameManager.timeScriptFileName == "production-timeout.lua",
		"SetTimeScript before OpenTimeLimit preserves the XJXQY production timeout callback") && ok;
	gameManager.scriptAPI.closeTimeLimit();
	ok = check(!gameManager.timerStarted && !gameManager.timeScriptSet,
		"CloseTimeLimit clears the active timer and its callback") && ok;
	const int signedRandomResult = ScriptEngineRuntimeTestAccess::execute(
		script,
		"for i = 1, 200 do "
		"  getrandnum('signed_random', -5, -3); "
		"  local value = getvar('signed_random'); "
		"  assert(value >= -5 and value <= -3); "
		"end");
	ok = check(signedRandomResult == LUA_OK,
		"GetRandNum preserves the YYCS/XJXQY signed inclusive range") && ok;
	ScriptEngineRuntimeTestAccess::registerQuitRequester(script);
	gameManager.varList.setInteger("after_generic_quit", 0);
	const int genericQuitResult = ScriptEngineRuntimeTestAccess::execute(script,
		"test_request_application_quit(); assign('after_generic_quit', 1)");
	ok = check(genericQuitResult != LUA_OK &&
		gameManager.varList.getInteger("after_generic_quit") == 0 &&
		ScriptEngineRuntimeTestAccess::stackTop(script) == 0,
		"application quit from any blocking Lua API aborts the remaining chunk") && ok;
	Element::resetApplicationQuitState();

	gameManager.varList.setInteger("after_movie_quit", 0);
	gameManager.global.data.bgmName = "existing-theme.mp3";
	ScriptEngineRuntimeTestAccess::setCurrentBgmName(
		gameManager,
		"existing-theme.mp3");
	Engine::getInstance()->requestApplicationQuit();
	const int movieQuitResult = ScriptEngineRuntimeTestAccess::execute(script,
		"playmovie('missing.avi'); assign('after_movie_quit', 1)");
	ok = check(movieQuitResult != LUA_OK &&
		gameManager.varList.getInteger("after_movie_quit") == 0 &&
		gameManager.global.data.bgmName == "existing-theme.mp3" &&
		ScriptEngineRuntimeTestAccess::currentBgmName(gameManager) ==
			"existing-theme.mp3" &&
		ScriptEngineRuntimeTestAccess::stackTop(script) == 0,
		"PlayMovie preserves BGM state and application quit aborts the remaining Lua chunk") && ok;
	Element::resetApplicationQuitState();

	gameManager.global.data.asfStyle = ColorStyle::Grayscale;
	gameManager.global.data.mpcStyle = ColorStyle::Grayscale;
	ScriptEngineRuntimeTestAccess::execute(script,
		"changeasfcolor(0, 0, 0); changemapcolor(0, 0, 0)");
	ok = check(gameManager.global.data.asfStyle == 0
		&& gameManager.global.data.mpcStyle == 0,
		"ChangeASFColor and ChangeMapColor preserve explicit black instead of converting it to grayscale") && ok;

	const int savedAttack = gameManager.player->attack;
	const int savedAttack2 = gameManager.player->attack2;
	const int savedAttack3 = gameManager.player->attack3;
	const int savedDefend = gameManager.player->defend;
	const int savedDefend2 = gameManager.player->defend2;
	const int savedDefend3 = gameManager.player->defend3;
	gameManager.player->attack = 1;
	gameManager.player->attack2 = 2;
	gameManager.player->attack3 = 3;
	gameManager.player->defend = 4;
	gameManager.player->defend2 = 5;
	gameManager.player->defend3 = 6;
	gameManager.player->calInfo();
	ScriptEngineRuntimeTestAccess::execute(script,
		"addattack(7, 2); addattack(4); addattack(99, 4); "
		"adddefend(-9, 3); adddefend(3, 2); adddefend(99, 4)");
	ok = check(gameManager.player->attack == 5
		&& gameManager.player->attack2 == 9
		&& gameManager.player->attack3 == 3
		&& gameManager.player->defend == 4
		&& gameManager.player->defend2 == 8
		&& gameManager.player->defend3 == 0,
		"AddAttack and AddDefend preserve the YYCS/XJXQY type selector and defend clamp") && ok;
	gameManager.player->attack = savedAttack;
	gameManager.player->attack2 = savedAttack2;
	gameManager.player->attack3 = savedAttack3;
	gameManager.player->defend = savedDefend;
	gameManager.player->defend2 = savedDefend2;
	gameManager.player->defend3 = savedDefend3;

	const int savedStateEvade = gameManager.player->evade;
	const NPCEquipmentAttributes savedEquipmentAttributes =
		gameManager.player->equipmentAttributes;
	const auto savedWeakMagic = gameManager.player->weakMagic;
	const auto savedMorphMagic = gameManager.player->morphMagic;
	gameManager.player->attack = 100;
	gameManager.player->defend = 80;
	gameManager.player->evade = 60;
	gameManager.player->equipmentAttributes.attack = 20;
	gameManager.player->equipmentAttributes.defend = 20;
	gameManager.player->equipmentAttributes.evade = 20;
	gameManager.player->weakMagic = std::make_shared<Magic>();
	gameManager.player->weakMagic->weakAttackPercent = 25;
	gameManager.player->weakMagic->weakDefendPercent = 10;
	gameManager.player->morphMagic = std::make_shared<Magic>();
	gameManager.player->morphMagic->attackAddPercent = 100;
	gameManager.player->morphMagic->defendAddPercent = 100;
	gameManager.player->morphMagic->evadeAddPercent = 100;
	ScriptEngineRuntimeTestAccess::execute(
		script,
		"getplayerstate('Attack', 'script_attack'); "
		"getplayerstate('Defend', 'script_defend'); "
		"getplayerstate('Evade', 'script_evade')");
	ok = check(
		gameManager.varList.getInteger("script_attack") == 90 &&
		gameManager.varList.getInteger("script_defend") == 90 &&
		gameManager.varList.getInteger("script_evade") == 80,
		"GetPlayerState matches YYCS/XJXQY base attributes: weak applies to attack/defend and morph is excluded") && ok;
	gameManager.player->attack = savedAttack;
	gameManager.player->defend = savedDefend;
	gameManager.player->evade = savedStateEvade;
	gameManager.player->equipmentAttributes = savedEquipmentAttributes;
	gameManager.player->weakMagic = savedWeakMagic;
	gameManager.player->morphMagic = savedMorphMagic;
	gameManager.player->calInfo();

	const int savedPlayerExp = gameManager.player->exp;
	const int savedPlayerLevelUpExp = gameManager.player->levelUpExp;
	const int savedPlayerLevel = gameManager.player->level;
	const std::vector<LevelInfo> savedPlayerLevelList =
		gameManager.player->levelList;
	const int savedThresholdTestLife = gameManager.player->life;
	const int savedThresholdTestThew = gameManager.player->thew;
	const int savedThresholdTestMana = gameManager.player->mana;
	gameManager.player->exp = 35;
	gameManager.player->levelUpExp = 0;
	ScriptEngineRuntimeTestAccess::execute(script, "addexp(100)");
	ok = check(gameManager.player->exp == 35,
		"AddExp follows the configured trilogy behavior and ignores additions when no level threshold is configured") && ok;
	LevelInfo thresholdLevel;
	thresholdLevel.levelUpExp = 100;
	thresholdLevel.attack = gameManager.player->attack;
	thresholdLevel.attack2 = gameManager.player->attack2;
	thresholdLevel.attack3 = gameManager.player->attack3;
	thresholdLevel.defend = gameManager.player->defend;
	thresholdLevel.defend2 = gameManager.player->defend2;
	thresholdLevel.defend3 = gameManager.player->defend3;
	thresholdLevel.evade = gameManager.player->evade;
	thresholdLevel.lifeMax = gameManager.player->lifeMax;
	thresholdLevel.thewMax = gameManager.player->thewMax;
	thresholdLevel.manaMax = gameManager.player->manaMax;
	LevelInfo nextThresholdLevel = thresholdLevel;
	nextThresholdLevel.levelUpExp = 200;
	gameManager.player->levelList = { thresholdLevel, nextThresholdLevel };
	gameManager.player->level = 1;
	gameManager.player->exp = 0;
	gameManager.player->levelUpExp = 100;
	applyOriginalBehavior(gameManager.global, GAME_YYCS);
	ScriptEngineRuntimeTestAccess::execute(script, "addexp(100)");
	ok = check(gameManager.player->level == 1
		&& gameManager.player->exp == 100,
		"YYCS AddExp preserves the strict-greater-than level threshold") && ok;
	applyOriginalBehavior(gameManager.global, GAME_JXQY2);
	gameManager.player->exp = savedPlayerExp;
	gameManager.player->levelUpExp = savedPlayerLevelUpExp;
	gameManager.player->level = savedPlayerLevel;
	gameManager.player->levelList = savedPlayerLevelList;
	gameManager.player->life = savedThresholdTestLife;
	gameManager.player->thew = savedThresholdTestThew;
	gameManager.player->mana = savedThresholdTestMana;

	const int savedScriptLife = gameManager.player->life;
	const int savedScriptLifeMax = gameManager.player->lifeMax;
	const int savedScriptInvincible = gameManager.player->invincible;
	applyOriginalBehavior(gameManager.global, GAME_YYCS);
	gameManager.player->lifeMax = 2000;
	gameManager.player->life = 1000;
	gameManager.player->invincible = 1;
	ScriptEngineRuntimeTestAccess::execute(script, "addlife(-600)");
	ok = check(gameManager.player->life == 400,
		"YYCS AddLife applies the exact script delta without damage scaling or invincibility") && ok;
	gameManager.player->life = savedScriptLife;
	gameManager.player->lifeMax = savedScriptLifeMax;
	gameManager.player->invincible = savedScriptInvincible;
	applyOriginalBehavior(gameManager.global, GAME_JXQY2);
	gameManager.player->calInfo();

	const int savedLifeMax = gameManager.player->lifeMax;
	const int savedManaMax = gameManager.player->manaMax;
	const int savedThewMax = gameManager.player->thewMax;
	const int savedLife = gameManager.player->life;
	const int savedMana = gameManager.player->mana;
	const int savedThew = gameManager.player->thew;
	const int savedEvade = gameManager.player->evade;
	gameManager.player->lifeMax = 100;
	gameManager.player->manaMax = 100;
	gameManager.player->thewMax = 100;
	gameManager.player->life = 40;
	gameManager.player->mana = 35;
	gameManager.player->thew = 30;
	gameManager.player->evade = 5;
	gameManager.player->calInfo();
	ScriptEngineRuntimeTestAccess::execute(script,
		"addlifemax(20); addmanamax(-500); addthewmax(30); addevade(-500)");
	ok = check(gameManager.player->lifeMax == 120
		&& gameManager.player->manaMax == 1
		&& gameManager.player->thewMax == 130
		&& gameManager.player->life == 40
		&& gameManager.player->thew == 30
		&& gameManager.player->mana <= gameManager.player->getManaMax()
		&& gameManager.player->evade == 0,
		"maximum-stat additions clamp their bases without refilling current values") && ok;
	gameManager.player->lifeMax = savedLifeMax;
	gameManager.player->manaMax = savedManaMax;
	gameManager.player->thewMax = savedThewMax;
	gameManager.player->life = savedLife;
	gameManager.player->mana = savedMana;
	gameManager.player->thew = savedThew;
	gameManager.player->evade = savedEvade;
	gameManager.player->calInfo();

	gameManager.player->frozen = true;
	gameManager.player->frozenLastTime = 1000;
	gameManager.player->frozenVisualEffect = true;
	gameManager.player->petrified = false;
	gameManager.player->petrifiedLastTime = 0;
	gameManager.player->petrifiedVisualEffect = false;
	ScriptEngineRuntimeTestAccess::execute(script, "petrifymillisecond(2500)");
	ok = check(gameManager.player->petrified
		&& gameManager.player->petrifiedLastTime == 2500
		&& gameManager.player->petrifiedVisualEffect
		&& !gameManager.player->frozen
		&& gameManager.player->frozenLastTime == 0
		&& !gameManager.player->frozenVisualEffect,
		"script petrify clears an existing frozen state in the YYCS/XJXQY runtime profile") && ok;
	gameManager.player->petrified = false;
	gameManager.player->petrifiedLastTime = 0;
	gameManager.player->petrifiedVisualEffect = false;

	const NPCActionType savedFullLifeAction =
		gameManager.player->actionManager->getCurrentActionType();
	const unsigned int savedFullLifeResult = gameManager.player->result;
	gameManager.player->life = 0;
	gameManager.player->result |= erLifeExhaust;
	gameManager.player->actionManager->resetActionIgnoringTransitions(acDeath);
	ScriptEngineRuntimeTestAccess::execute(script, "fulllife()");
	ok = check(gameManager.player->isStanding()
		&& gameManager.player->life == gameManager.player->getLifeMax()
		&& (gameManager.player->result & erLifeExhaust) == 0,
		"FullLife exits the active death flow using the YYCS/XJXQY death-state reset") && ok;
	gameManager.player->result = savedFullLifeResult;
	gameManager.player->life = savedLife;
	if (gameManager.player->actionManager->getCurrentActionType() !=
		savedFullLifeAction)
	{
		gameManager.player->actionManager->resetActionIgnoringTransitions(
			savedFullLifeAction);
	}

	auto recordingNPC = std::make_shared<RecordingNPC>();
	recordingNPC->npcMagic = std::make_shared<Magic>();
	gameManager.scriptNPC = recordingNPC;

	applyOriginalBehavior(gameManager.global, GAME_JXQY2);
	recordingNPC->resetRecording();
	ScriptEngineRuntimeTestAccess::execute(script, "npcgoto(29, 70)");
	ScriptEngineRuntimeTestAccess::execute(script, "npcgotoex(31, 71)");
	ScriptEngineRuntimeTestAccess::execute(script, "npcgotodir(6, 4)");
	ScriptEngineRuntimeTestAccess::execute(script, "follownpc('current-leader')");
	ScriptEngineRuntimeTestAccess::execute(script, "setnpcactiontype(9)");
	ScriptEngineRuntimeTestAccess::execute(script,
		"setnpcmagicfile('missing-current-magic.ini')");
	ok = check(recordingNPC->goToCount == 1
		&& recordingNPC->goToExCount == 1
		&& recordingNPC->goToDirCount == 1
		&& recordingNPC->recordedDestination == Point{ 31, 71 }
		&& recordingNPC->recordedDirection == 6
		&& recordingNPC->recordedDistance == 4
		&& recordingNPC->followNPC == "current-leader"
		&& recordingNPC->strollIntent == 9
		&& recordingNPC->flyIni == "missing-current-magic.ini"
		&& !recordingNPC->inputEnabledDuringBlockingMove
		&& gameManager.global.data.canInput,
		"current-script NPC overloads are available consistently across game profiles") && ok;

	recordingNPC->resetRecording();
	int legacyHurtResult = ScriptEngineRuntimeTestAccess::execute(script, "setnpcaction('', 9)");
	ok = check(legacyHurtResult == LUA_OK && recordingNPC->recordedAction == RecordedNpcAction::Hurt,
		"JXQY2 script action 9 keeps legacy hurt numbering") && ok;
	recordingNPC->resetRecording();
	ScriptEngineRuntimeTestAccess::execute(script, "setnpcaction('', 10)");
	ok = check(recordingNPC->recordedAction == RecordedNpcAction::Sit,
		"JXQY2 script action 10 keeps legacy sit numbering") && ok;
	recordingNPC->resetRecording();
	ScriptEngineRuntimeTestAccess::execute(script, "setnpcaction('', 12)");
	ok = check(recordingNPC->recordedAction == RecordedNpcAction::Special,
		"JXQY2 script action 12 keeps the legacy special action") && ok;

	applyOriginalBehavior(gameManager.global, GAME_YYCS);
	recordingNPC->resetRecording();
	ScriptEngineRuntimeTestAccess::execute(script, "setnpcaction('', 9)");
	ok = check(recordingNPC->recordedAction == RecordedNpcAction::Sit,
		"YYCS script action 9 follows the YYCS sit numbering") && ok;
	recordingNPC->resetRecording();
	ScriptEngineRuntimeTestAccess::execute(script, "setnpcaction('', 10)");
	ok = check(recordingNPC->recordedAction == RecordedNpcAction::Hurt,
		"YYCS script action 10 follows the YYCS hurt numbering") && ok;
	recordingNPC->resetRecording();
	ScriptEngineRuntimeTestAccess::execute(script, "setnpcaction('', 13, 17, 29)");
	ok = check(recordingNPC->recordedAction == RecordedNpcAction::Walk
		&& recordingNPC->recordedDestination.x == 17 && recordingNPC->recordedDestination.y == 29
		&& recordingNPC->fightState.get(),
		"YYCS fight-walk action maps profile numbering and forwards optional coordinates") && ok;
	recordingNPC->resetRecording();
	ScriptEngineRuntimeTestAccess::execute(script, "setnpcaction('', 14, 31, 47)");
	ok = check(recordingNPC->recordedAction == RecordedNpcAction::Run
		&& recordingNPC->recordedDestination.x == 31 && recordingNPC->recordedDestination.y == 47
		&& recordingNPC->fightState.get(),
		"YYCS fight-run action maps profile numbering and forwards coordinates") && ok;
	ok = check(
		resolveScriptNpcActionFileSlot(ScriptNpcActionProfile::Yycs, 9) == 10 &&
		resolveScriptNpcActionFileSlot(ScriptNpcActionProfile::Yycs, 10) == 9 &&
		resolveScriptNpcActionFileSlot(ScriptNpcActionProfile::Yycs, 12) == 20 &&
		resolveScriptNpcActionFileSlot(ScriptNpcActionProfile::Yycs, 15) == 23 &&
		resolveScriptNpcActionFileSlot(ScriptNpcActionProfile::Legacy, 9) == 9,
		"SetNpcActionFile translates YYCS/XJXQY action slots without changing the JXQY2 numbering") && ok;

	applyOriginalBehavior(gameManager.global, GAME_XJXQY);
	recordingNPC->resetRecording();
	ScriptEngineRuntimeTestAccess::execute(script, "setnpcaction('', -1, '')");
	ok = check(recordingNPC->recordedAction == RecordedNpcAction::Stand,
		"XJXQY omitted legacy action maps to standing") && ok;
	recordingNPC->resetRecording();
	ScriptEngineRuntimeTestAccess::execute(script, "setnpcaction('', 13, '')");
	ok = check(recordingNPC->recordedAction == RecordedNpcAction::Stand,
		"XJXQY real action 13 call returns the character to standing") && ok;
	recordingNPC->resetRecording();
	ScriptEngineRuntimeTestAccess::execute(script, "setnpcaction('', 8, 53, 71)");
	ok = check(recordingNPC->recordedAction == RecordedNpcAction::Magic
		&& recordingNPC->recordedDestination.x == 53 && recordingNPC->recordedDestination.y == 71,
		"script magic action prepares the default NPC magic at the optional destination") && ok;

	recordingNPC->resetRecording();
	int unknownResult = ScriptEngineRuntimeTestAccess::execute(script, "setnpcaction('', 999, 1, 2)");
	ok = check(unknownResult == LUA_OK && recordingNPC->recordedAction == RecordedNpcAction::None
		&& resolveScriptNpcAction(ScriptNpcActionProfile::Xjxqy, 999) == ScriptNpcActionKind::Unknown,
		"unknown profile action is diagnosed and does not silently select an internal enum") && ok;

	recordingNPC->resetRecording();
	applyOriginalBehavior(gameManager.global, GAME_YYCS);
	const int yycsSpecialResult = ScriptEngineRuntimeTestAccess::execute(
		script, "npcspecialaction('yycs-normal.asf')");
	ok = check(yycsSpecialResult == LUA_OK
		&& recordingNPC->specialActionStartCount == 1
		&& recordingNPC->eventRunCount == 0
		&& recordingNPC->specialActionFile == "yycs-normal.asf",
		"YYCS ordinary NpcSpecialAction follows its non-blocking semantics") && ok;

	recordingNPC->resetRecording();
	gameManager.global.data.canInput = true;
	const int yycsSpecialExResult = ScriptEngineRuntimeTestAccess::execute(
		script, "npcspecialactionex('yycs-blocking.asf')");
	ok = check(yycsSpecialExResult == LUA_OK
		&& recordingNPC->specialActionStartCount == 1
		&& recordingNPC->eventRunCount == 1
		&& !recordingNPC->inputEnabledDuringEventRun
		&& gameManager.global.data.canInput
		&& recordingNPC->specialActionFile == "yycs-blocking.asf",
		"YYCS NpcSpecialActionEx waits with input disabled, then restores input") && ok;

	recordingNPC->resetRecording();
	recordingNPC->specialActionStartSucceeds = false;
	ScriptEngineRuntimeTestAccess::execute(script, "npcspecialactionex('missing-special.asf')");
	ok = check(recordingNPC->specialActionStartCount == 1
		&& recordingNPC->eventRunCount == 0
		&& gameManager.global.data.canInput,
		"NpcSpecialActionEx does not enter a nested wait when the action cannot start") && ok;
	recordingNPC->specialActionStartSucceeds = true;

	auto firstNamedNPC = std::make_shared<RecordingNPC>();
	auto secondNamedNPC = std::make_shared<RecordingNPC>();
	firstNamedNPC->npcName = "duplicate-special-target";
	secondNamedNPC->npcName = "duplicate-special-target";
	auto savedNpcList = gameManager.npcManager->npcList;
	gameManager.npcManager->npcList.clear();
	gameManager.npcManager->npcList.push_back(firstNamedNPC);
	gameManager.npcManager->npcList.push_back(secondNamedNPC);
	const int savedFirstNamedKind = firstNamedNPC->kind;
	const Point savedPlayerKindPlayerPosition = gameManager.player->getPosition();
	const Point savedPlayerKindNpcPosition = firstNamedNPC->getPosition();
	const bool savedPlayerKindPlayerFight = gameManager.player->fightState.get();
	const bool savedPlayerKindNpcFight = firstNamedNPC->fightState.get();
	const bool savedPlayerKindCanInput = gameManager.global.data.canInput;
	const bool savedPlayerKindCameraFollowPlayer = gameManager.camera->followPlayer;
	auto savedPlayerKindCameraTarget = gameManager.camera->followNPC;
	firstNamedNPC->kind = nkPlayer;
	firstNamedNPC->fightState.set(true);
	gameManager.player->fightState.set(true);
	gameManager.scriptAPI.talk("__missing_player_kind_talk_section__");
	ok = check(!firstNamedNPC->fightState.get()
		&& gameManager.player->fightState.get(),
		"Talk exits the YYCS/XJXQY player-kind character from fighting state") && ok;
	firstNamedNPC->setPosition({ 4, 5 }, false);
	gameManager.player->setPosition({ 1, 1 }, false);
	gameManager.scriptAPI.setPlayerPosition(20, 21);
	ok = check(firstNamedNPC->getPosition() == Point{ 20, 21 }
		&& gameManager.player->getPosition() == Point{ 1, 1 },
		"two-argument SetPlayerPos targets the YYCS/XJXQY player-kind character") && ok;
	const std::string savedPlayerKindActualPlayerName = gameManager.player->npcName;
	gameManager.player->npcName = "actual-player-position-target";
	gameManager.camera->followPlayer = false;
	gameManager.scriptAPI.setPlayerPosition(
		"actual-player-position-target", 30, 31);
	ok = check(gameManager.player->getPosition() == Point{ 30, 31 }
		&& firstNamedNPC->getPosition() == Point{ 20, 21 }
		&& gameManager.camera->followPlayer
		&& gameManager.camera->followNPC.lock() == firstNamedNPC,
		"three-argument SetPlayerPos moves the named player and recenters on the player-kind character") && ok;
	gameManager.player->npcName = savedPlayerKindActualPlayerName;
	firstNamedNPC->resetRecording();
	gameManager.global.data.canInput = true;
	gameManager.scriptAPI.playerGoto(22, 23);
	gameManager.scriptAPI.playerGotoEx(24, 25);
	gameManager.scriptAPI.playerRunToEx(26, 27);
	gameManager.scriptAPI.playerJumpTo(28, 29);
	gameManager.scriptAPI.playerGotoDir(6, 7);
	ok = check(firstNamedNPC->goToCount == 1
		&& firstNamedNPC->goToExCount == 1
		&& firstNamedNPC->goToDirCount == 1
		&& firstNamedNPC->recordedDirection == 6
		&& firstNamedNPC->recordedDistance == 7
		&& !firstNamedNPC->inputEnabledDuringBlockingMove
		&& firstNamedNPC->recordedAction == RecordedNpcAction::Jump
		&& gameManager.global.data.canInput,
		"Player movement APIs target the player-kind character and restore input") && ok;
	gameManager.scriptAPI.setLevelFile("player-kind-level.ini");
	gameManager.scriptAPI.setPlayerScn(false);
	ok = check(firstNamedNPC->npcLevelIni == "player-kind-level.ini"
		&& gameManager.camera->followNPC.lock() == firstNamedNPC,
		"SetLevelFile and SetPlayerScn target the YYCS/XJXQY player-kind character") && ok;
	firstNamedNPC->kind = savedFirstNamedKind;
	firstNamedNPC->setPosition(savedPlayerKindNpcPosition, false);
	firstNamedNPC->fightState.set(savedPlayerKindNpcFight);
	gameManager.player->setPosition(savedPlayerKindPlayerPosition, false);
	gameManager.player->fightState.set(savedPlayerKindPlayerFight);
	gameManager.global.data.canInput = savedPlayerKindCanInput;
	gameManager.camera->followPlayer = savedPlayerKindCameraFollowPlayer;
	gameManager.camera->followNPC = savedPlayerKindCameraTarget;
	firstNamedNPC->resetRecording();
	const int savedPlayerKind = gameManager.player->kind;
	const int savedPlayerRelation = gameManager.player->relation;
	gameManager.player->kind = 9;
	gameManager.player->relation = 9;
	firstNamedNPC->kind = 9;
	firstNamedNPC->relation = 9;
	secondNamedNPC->kind = 9;
	secondNamedNPC->relation = 9;
	gameManager.scriptAPI.getNpcCount(9, 9);
	ok = check(gameManager.varList.getInteger("NpcCount") == 1,
		"GetNpcCount preserves the YYCS/XJXQY player-match short circuit") && ok;
	gameManager.player->kind = 8;
	gameManager.scriptAPI.getNpcCount(9, 9);
	ok = check(gameManager.varList.getInteger("NpcCount") == 2,
		"GetNpcCount counts matching NPCs when the player does not match") && ok;
	gameManager.player->kind = savedPlayerKind;
	gameManager.player->relation = savedPlayerRelation;
	firstNamedNPC->resetRecording();
	secondNamedNPC->resetRecording();
	firstNamedNPC->setPosition({ 2, 2 }, false);
	secondNamedNPC->setPosition({ 6, 2 }, false);
	const Point playerPositionBeforeNamedSetPlayerPos =
		gameManager.player->getPosition();
	ScriptEngineRuntimeTestAccess::execute(script,
		"setplayerpos('duplicate-special-target', 9, 10); "
		"setplayerpos(16, 6, 0)");
	ok = check(firstNamedNPC->getPosition() == Point{ 9, 10 }
		&& secondNamedNPC->getPosition() == Point{ 6, 2 }
		&& gameManager.player->getPosition() ==
			playerPositionBeforeNamedSetPlayerPos,
		"three-argument SetPlayerPos resolves the first named character and ignores a missing target") && ok;
	firstNamedNPC->resetRecording();
	secondNamedNPC->resetRecording();
	firstNamedNPC->haveAsyncDest = true;
	secondNamedNPC->haveAsyncDest = true;
	secondNamedNPC->npcName = "watch-second";
	gameManager.scriptAPI.watch(
		"duplicate-special-target",
		"watch-second",
		0);
	ok = check(firstNamedNPC->recordedAction == RecordedNpcAction::None
		&& secondNamedNPC->recordedAction == RecordedNpcAction::None
		&& firstNamedNPC->haveAsyncDest
		&& secondNamedNPC->haveAsyncDest,
		"Watch changes facing without cancelling the current action or movement") && ok;
	secondNamedNPC->npcName = "duplicate-special-target";

	applyOriginalBehavior(gameManager.global, GAME_YYCS);
	firstNamedNPC->scriptFile = "first-old.lua";
	secondNamedNPC->scriptFile = "second-old.lua";
	gameManager.scriptAPI.setNPCScript(
		"duplicate-special-target", "first-only.lua");
	ok = check(firstNamedNPC->scriptFile == "first-only.lua"
		&& secondNamedNPC->scriptFile == "second-old.lua",
		"YYCS named SetNpcScript targets only the first matching NPC") && ok;
	gameManager.scriptAPI.setAllNPCScript(
		"duplicate-special-target", "all.lua");
	ok = check(firstNamedNPC->scriptFile == "all.lua"
		&& secondNamedNPC->scriptFile == "all.lua",
		"YYCS SetAllNpcScript preserves the explicit broadcast contract") && ok;
	const std::string savedBatchPlayerName = gameManager.player->npcName;
	const std::string savedBatchPlayerScript = gameManager.player->scriptFile;
	const std::string savedBatchPlayerDeathScript = gameManager.player->deathScript;
	const std::string savedBatchPlayerFlyIni = gameManager.player->flyIni;
	const std::string savedBatchPlayerFlyIni2 = gameManager.player->flyIni2;
	const std::string savedBatchPlayerFlyInis = gameManager.player->flyInis;
	const std::string savedBatchPlayerAttackedMagic =
		gameManager.player->magicToUseWhenBeAttackedFile;
	const int savedBatchPlayerAttackedMagicDirection =
		gameManager.player->magicDirectionWhenBeAttacked;
	const Point savedBatchPlayerKeepAttack = gameManager.player->keepAttackPosition;
	const Point savedBatchPlayerDestination = gameManager.player->destinationMapPosition;
	gameManager.player->npcName = "duplicate-special-target";
	gameManager.player->scriptFile = "player-script.lua";
	gameManager.player->deathScript = "player-death.lua";
	gameManager.player->flyIni = "player-fly.ini";
	gameManager.player->flyIni2 = "player-fly2.ini";
	gameManager.player->flyInis = "player-fly-list.ini:1;";
	gameManager.player->magicToUseWhenBeAttackedFile = "player-attacked.ini";
	gameManager.player->magicDirectionWhenBeAttacked = 7;
	gameManager.player->keepAttackPosition = { 1, 2 };
	gameManager.player->destinationMapPosition = { 3, 4 };
	gameManager.scriptAPI.setAllNPCScript(
		"duplicate-special-target", "npc-batch-script.lua");
	gameManager.scriptAPI.setAllNPCDeathScript(
		"duplicate-special-target", "npc-batch-death.lua");
	gameManager.scriptAPI.changeFlyIni(
		"duplicate-special-target", "npc-batch-fly.ini");
	gameManager.scriptAPI.changeFlyIni2(
		"duplicate-special-target", "npc-batch-fly2.ini");
	gameManager.scriptAPI.addFlyInis(
		"duplicate-special-target", "npc-batch-extra.ini", 5);
	gameManager.scriptAPI.setNpcMagicToUseWhenBeAttacked(
		"duplicate-special-target", "npc-batch-attacked.ini", 6);
	gameManager.scriptAPI.setKeepAttack(
		"duplicate-special-target", 8, 9);
	gameManager.scriptAPI.setNpcDestination(
		"duplicate-special-target", 10, 11);
	const bool batchPlayerUnchanged =
		gameManager.player->scriptFile == "player-script.lua"
		&& gameManager.player->deathScript == "player-death.lua"
		&& gameManager.player->flyIni == "player-fly.ini"
		&& gameManager.player->flyIni2 == "player-fly2.ini"
		&& gameManager.player->flyInis == "player-fly-list.ini:1;"
		&& gameManager.player->magicToUseWhenBeAttackedFile == "player-attacked.ini"
		&& gameManager.player->magicDirectionWhenBeAttacked == 7
		&& gameManager.player->keepAttackPosition == Point{ 1, 2 }
		&& gameManager.player->destinationMapPosition == Point{ 3, 4 };
	const bool batchNpcScriptsUpdated =
		firstNamedNPC->scriptFile == "npc-batch-script.lua"
		&& secondNamedNPC->scriptFile == "npc-batch-script.lua"
		&& firstNamedNPC->deathScript == "npc-batch-death.lua"
		&& secondNamedNPC->deathScript == "npc-batch-death.lua";
	const bool batchNpcMagicUpdated =
		firstNamedNPC->flyIni == "npc-batch-fly.ini"
		&& secondNamedNPC->flyIni == "npc-batch-fly.ini"
		&& firstNamedNPC->flyIni2 == "npc-batch-fly2.ini"
		&& secondNamedNPC->flyIni2 == "npc-batch-fly2.ini"
		&& firstNamedNPC->flyInis.find("npc-batch-extra.ini:5;") != std::string::npos
		&& secondNamedNPC->flyInis.find("npc-batch-extra.ini:5;") != std::string::npos
		&& firstNamedNPC->magicToUseWhenBeAttackedFile == "npc-batch-attacked.ini"
		&& secondNamedNPC->magicToUseWhenBeAttackedFile == "npc-batch-attacked.ini"
		&& firstNamedNPC->magicDirectionWhenBeAttacked == 6
		&& secondNamedNPC->magicDirectionWhenBeAttacked == 6;
	const bool batchNpcPositionsUpdated =
		firstNamedNPC->keepAttackPosition == Point{ 8, 9 }
		&& secondNamedNPC->keepAttackPosition == Point{ 8, 9 }
		&& firstNamedNPC->destinationMapPosition == Point{ 10, 11 }
		&& secondNamedNPC->destinationMapPosition == Point{ 10, 11 };
	ok = check(batchPlayerUnchanged,
		"explicit NPC batch APIs leave an identically named player unchanged") && ok;
	ok = check(batchNpcScriptsUpdated,
		"explicit NPC script batch APIs update every matching NPC") && ok;
	ok = check(batchNpcMagicUpdated,
		"explicit NPC magic batch APIs update every matching NPC") && ok;
	ok = check(batchNpcPositionsUpdated,
		"explicit NPC movement batch APIs update every matching NPC") && ok;
	ok = check(batchPlayerUnchanged && batchNpcScriptsUpdated
		&& batchNpcMagicUpdated && batchNpcPositionsUpdated,
		"explicit NPC batch APIs exclude an identically named player") && ok;
	const int savedBatchPlayerAttack = gameManager.player->attack;
	const int savedBatchPlayerLife = gameManager.player->life;
	const int savedBatchPlayerLifeMax = gameManager.player->lifeMax;
	const int savedBatchPlayerExpBonus = gameManager.player->expBonus;
	const UTime savedBatchPlayerTimerScriptInterval =
		gameManager.player->timerScriptInterval;
	const int savedBatchPlayerVisionRadius = gameManager.player->visionRadius;
	const int savedBatchPlayerLifeLowPercent = gameManager.player->lifeLowPercent;
	firstNamedNPC->attack = 10;
	secondNamedNPC->attack = 20;
	gameManager.player->attack = 30;
	firstNamedNPC->lifeMax = 100;
	secondNamedNPC->lifeMax = 200;
	gameManager.player->lifeMax = 300;
	firstNamedNPC->life = 50;
	secondNamedNPC->life = 75;
	gameManager.player->life = 100;
	firstNamedNPC->expBonus = 1;
	secondNamedNPC->expBonus = 2;
	gameManager.player->expBonus = 3;
	firstNamedNPC->timerScriptInterval = 100;
	secondNamedNPC->timerScriptInterval = 200;
	gameManager.player->timerScriptInterval = 300;
	firstNamedNPC->visionRadius = 0;
	secondNamedNPC->visionRadius = 0;
	gameManager.player->visionRadius = 0;
	firstNamedNPC->lifeLowPercent = 0;
	secondNamedNPC->lifeLowPercent = 0;
	gameManager.player->lifeLowPercent = 0;
	gameManager.player->calInfo();
	const int playerEffectiveAttackBeforeAddNpcProperty =
		gameManager.player->getAttack();
	gameManager.scriptAPI.addNpcProperty(
		"duplicate-special-target", "Attack", 4);
	gameManager.scriptAPI.addNpcProperty(
		"duplicate-special-target", "LifeMax", 10);
	gameManager.scriptAPI.addNpcProperty(
		"duplicate-special-target", "Life", -1000);
	gameManager.scriptAPI.addNpcProperty(
		"duplicate-special-target", "ExpBonus", 5);
	gameManager.scriptAPI.addNpcProperty(
		"duplicate-special-target", "TimerScriptInterval", 25);
	gameManager.scriptAPI.addNpcProperty(
		"duplicate-special-target", "VisionRadius", 1);
	gameManager.scriptAPI.addNpcProperty(
		"duplicate-special-target", "LifeLowPercent", -5);
	ok = check(firstNamedNPC->attack == 14
		&& secondNamedNPC->attack == 24
		&& gameManager.player->attack == 34
		&& gameManager.player->getAttack() ==
			playerEffectiveAttackBeforeAddNpcProperty + 4
		&& firstNamedNPC->lifeMax == 110
		&& secondNamedNPC->lifeMax == 210
		&& gameManager.player->lifeMax == 310
		&& firstNamedNPC->life == 0
		&& secondNamedNPC->life == 0
		&& gameManager.player->life == 0
		&& firstNamedNPC->expBonus == 6
		&& secondNamedNPC->expBonus == 7
		&& gameManager.player->expBonus == 8
		&& firstNamedNPC->timerScriptInterval == 125
		&& secondNamedNPC->timerScriptInterval == 225
		&& gameManager.player->timerScriptInterval == 325
		&& firstNamedNPC->visionRadius == 10
		&& secondNamedNPC->visionRadius == 10
		&& gameManager.player->visionRadius == 10
		&& firstNamedNPC->lifeLowPercent == -5
		&& secondNamedNPC->lifeLowPercent == -5
		&& gameManager.player->lifeLowPercent == -5,
		"AddNpcProperty updates every same-name character with YYCS/XJXQY property semantics") && ok;
	gameManager.player->attack = savedBatchPlayerAttack;
	gameManager.player->life = savedBatchPlayerLife;
	gameManager.player->lifeMax = savedBatchPlayerLifeMax;
	gameManager.player->expBonus = savedBatchPlayerExpBonus;
	gameManager.player->timerScriptInterval =
		savedBatchPlayerTimerScriptInterval;
	gameManager.player->visionRadius = savedBatchPlayerVisionRadius;
	gameManager.player->lifeLowPercent = savedBatchPlayerLifeLowPercent;
	gameManager.player->calInfo();
	gameManager.player->npcName = savedBatchPlayerName;
	gameManager.player->scriptFile = savedBatchPlayerScript;
	gameManager.player->deathScript = savedBatchPlayerDeathScript;
	gameManager.player->flyIni = savedBatchPlayerFlyIni;
	gameManager.player->flyIni2 = savedBatchPlayerFlyIni2;
	gameManager.player->flyInis = savedBatchPlayerFlyInis;
	gameManager.player->magicToUseWhenBeAttackedFile = savedBatchPlayerAttackedMagic;
	gameManager.player->magicDirectionWhenBeAttacked =
		savedBatchPlayerAttackedMagicDirection;
	gameManager.player->keepAttackPosition = savedBatchPlayerKeepAttack;
	gameManager.player->destinationMapPosition = savedBatchPlayerDestination;
	firstNamedNPC->resetRecording();
	secondNamedNPC->resetRecording();
	firstNamedNPC->actionManager->resetActionIgnoringTransitions(acWalk);
	secondNamedNPC->actionManager->resetActionIgnoringTransitions(acWalk);
	firstNamedNPC->nowAction = acWalk;
	secondNamedNPC->nowAction = acWalk;
	firstNamedNPC->direction = 1;
	secondNamedNPC->direction = 2;
	gameManager.scriptAPI.setNPCDir("duplicate-special-target", 6);
	ok = check(firstNamedNPC->direction == 6
		&& secondNamedNPC->direction == 2,
		"YYCS named SetNpcDir updates the first matching NPC even during an active action") && ok;
	firstNamedNPC->actionManager->resetActionIgnoringTransitions(acStand);
	secondNamedNPC->actionManager->resetActionIgnoringTransitions(acStand);
	firstNamedNPC->nowAction = acStand;
	secondNamedNPC->nowAction = acStand;
	firstNamedNPC->goToCount = 0;
	secondNamedNPC->goToCount = 0;
	gameManager.scriptAPI.goTo("duplicate-special-target", 15, 104);
	ok = check(firstNamedNPC->goToCount == 1
		&& firstNamedNPC->recordedDestination == Point{ 15, 104 }
		&& secondNamedNPC->goToCount == 0,
		"YYCS named NpcGoto waits for only the first matching NPC") && ok;

	firstNamedNPC->resetRecording();
	secondNamedNPC->resetRecording();
	firstNamedNPC->npcIni = "first-old.ini";
	secondNamedNPC->npcIni = "second-old.ini";
	firstNamedNPC->level = 1;
	secondNamedNPC->level = 2;
	firstNamedNPC->strollIntent = 3;
	secondNamedNPC->strollIntent = 4;
	firstNamedNPC->dropIni = "first-old-drop.ini";
	secondNamedNPC->dropIni = "second-old-drop.ini";
	firstNamedNPC->flyIni = "first-old-magic.ini";
	secondNamedNPC->flyIni = "second-old-magic.ini";
	gameManager.scriptAPI.setNPCRes(
		"duplicate-special-target", "first-only-res.ini");
	gameManager.scriptAPI.setNPCLevel("duplicate-special-target", 12);
	gameManager.scriptAPI.setNPCActionType("duplicate-special-target", 8);
	gameManager.scriptAPI.setNPCActionFile(
		"duplicate-special-target", 3, "first-only-action.asf");
	gameManager.scriptAPI.setDropIni(
		"duplicate-special-target", "first-only-drop.ini");
	gameManager.scriptAPI.setNpcMagicFile(
		"duplicate-special-target", "first-only-magic.ini");
	ok = check(firstNamedNPC->npcIni == "first-only-res.ini"
		&& firstNamedNPC->initResCount == 1
		&& firstNamedNPC->level == 12
		&& firstNamedNPC->setLevelCount == 1
		&& firstNamedNPC->strollIntent == 8
		&& firstNamedNPC->loadedActionFile == "first-only-action.asf"
		&& firstNamedNPC->loadedAction == 3
		&& firstNamedNPC->dropIni == "first-only-drop.ini"
		&& firstNamedNPC->flyIni == "first-only-magic.ini"
		&& secondNamedNPC->npcIni == "second-old.ini"
		&& secondNamedNPC->initResCount == 0
		&& secondNamedNPC->level == 2
		&& secondNamedNPC->setLevelCount == 0
		&& secondNamedNPC->strollIntent == 4
		&& secondNamedNPC->loadActionFileCount == 0
		&& secondNamedNPC->dropIni == "second-old-drop.ini"
		&& secondNamedNPC->flyIni == "second-old-magic.ini",
		"YYCS named NPC resource and property mutators target only the first match") && ok;

	firstNamedNPC->relation = nrFriendly;
	secondNamedNPC->relation = nrFriendly;
	firstNamedNPC->fightState.set(true);
	secondNamedNPC->fightState.set(true);
	firstNamedNPC->actionManager->resetActionIgnoringTransitions(acRun);
	secondNamedNPC->actionManager->resetActionIgnoringTransitions(acRun);
	gameManager.scriptAPI.setNPCRelation(
		"duplicate-special-target", nrHostile);
	ok = check(
		firstNamedNPC->relation == nrHostile &&
		secondNamedNPC->relation == nrHostile &&
		!firstNamedNPC->fightState.get() &&
		!secondNamedNPC->fightState.get() &&
		firstNamedNPC->actionManager->getCurrentActionType() == acRun &&
		secondNamedNPC->actionManager->getCurrentActionType() == acRun,
		"YYCS SetNpcRelation keeps the explicit all-match contract without cancelling the current action") && ok;

	firstNamedNPC->resetRecording();
	secondNamedNPC->resetRecording();
	gameManager.scriptAPI.setNPCPosition(
		"duplicate-special-target", 0, 0);
	gameManager.scriptAPI.setNPCAction(
		"duplicate-special-target", 0, 0, 0);
	gameManager.scriptAPI.attackTo(
		"duplicate-special-target", 7, 9);
	ok = check(firstNamedNPC->recordedAction == RecordedNpcAction::Attack
		&& firstNamedNPC->recordedDestination == Point{ 7, 9 }
		&& firstNamedNPC->eventRunCount == 0
		&& secondNamedNPC->recordedAction == RecordedNpcAction::None,
		"YYCS named NPC position, action, and non-blocking attack target only the first match") && ok;

	ScriptEngineRuntimeTestAccess::execute(
		script, "npcspecialaction('duplicate-special-target', 'yycs-first.asf')");
	ok = check(firstNamedNPC->specialActionStartCount == 1
		&& firstNamedNPC->eventRunCount == 0
		&& secondNamedNPC->specialActionStartCount == 0,
		"YYCS named NpcSpecialAction targets only the first matching NPC") && ok;
	const std::string savedPlayerName = gameManager.player->npcName;
	const NPCActionRes savedPlayerSpecialAction = gameManager.player->res.special;
	const NPCActionRes savedPlayerSpecialOverlayResource = gameManager.player->scriptSpecialActionOverlayResource;
	const NPCActionType savedPlayerAction = gameManager.player->actionManager->getCurrentActionType();
	const int savedPlayerDirection = gameManager.player->direction;
	const bool savedPlayerSpecialOverlayActive = gameManager.player->scriptSpecialActionOverlayActive;
	const UTime savedPlayerSpecialOverlayElapsed = gameManager.player->scriptSpecialActionOverlayElapsed;
	const UTime savedPlayerSpecialOverlayDuration = gameManager.player->scriptSpecialActionOverlayDuration;
	const int savedPlayerSpecialOverlayDirection = gameManager.player->scriptSpecialActionOverlayDirection;
	auto playerSpecialImage = std::make_shared<IMPImage>();
	playerSpecialImage->directions = 1;
	playerSpecialImage->interval = 1;
	playerSpecialImage->frame.resize(1);
	gameManager.npcManager->actionImageList["player-first.asf"] = playerSpecialImage;
	gameManager.npcManager->actionImageList["player-ex.asf"] = playerSpecialImage;
	gameManager.player->npcName = "duplicate-special-target";
	firstNamedNPC->resetRecording();
	secondNamedNPC->resetRecording();
	const int playerFirstSpecialResult = ScriptEngineRuntimeTestAccess::execute(
		script, "npcspecialaction('duplicate-special-target', 'player-first.asf')");
	ok = check(playerFirstSpecialResult == LUA_OK
		&& gameManager.player->scriptSpecialActionOverlayResource.imageFile == "player-first.asf"
		&& gameManager.player->scriptSpecialActionOverlayActive
		&& firstNamedNPC->specialActionStartCount == 0
		&& secondNamedNPC->specialActionStartCount == 0,
		"YYCS/XJXQY named special actions prefer the player over same-name NPCs") && ok;
	const int playerSpecialExResult = ScriptEngineRuntimeTestAccess::execute(
		script, "npcspecialactionex('duplicate-special-target', 'player-ex.asf')");
	ok = check(playerSpecialExResult == LUA_OK
		&& gameManager.player->scriptSpecialActionOverlayResource.imageFile == "player-ex.asf"
		&& !gameManager.player->scriptSpecialActionOverlayActive
		&& firstNamedNPC->specialActionStartCount == 0
		&& secondNamedNPC->specialActionStartCount == 0,
		"YYCS/XJXQY player NpcSpecialActionEx advances and completes its overlay") && ok;
	gameManager.npcManager->actionImageList.erase("player-first.asf");
	gameManager.npcManager->actionImageList.erase("player-ex.asf");
	gameManager.player->npcName = savedPlayerName;
	gameManager.player->res.special = savedPlayerSpecialAction;
	gameManager.player->scriptSpecialActionOverlayResource = savedPlayerSpecialOverlayResource;
	gameManager.player->scriptSpecialActionOverlayActive = savedPlayerSpecialOverlayActive;
	gameManager.player->scriptSpecialActionOverlayElapsed = savedPlayerSpecialOverlayElapsed;
	gameManager.player->scriptSpecialActionOverlayDuration = savedPlayerSpecialOverlayDuration;
	gameManager.player->scriptSpecialActionOverlayDirection = savedPlayerSpecialOverlayDirection;
	if (gameManager.player->actionManager->getCurrentActionType() != savedPlayerAction)
	{
		gameManager.player->actionManager->resetActionIgnoringTransitions(savedPlayerAction);
	}
	gameManager.player->direction = savedPlayerDirection;

	firstNamedNPC->resetRecording();
	secondNamedNPC->resetRecording();
	applyOriginalBehavior(gameManager.global, GAME_XJXQY);
	const int xjxqySpecialResult = ScriptEngineRuntimeTestAccess::execute(
		script, "npcspecialaction('duplicate-special-target', 'xjxqy-normal.asf')");
	ok = check(xjxqySpecialResult == LUA_OK
		&& firstNamedNPC->specialActionStartCount == 1
		&& firstNamedNPC->eventRunCount == 0
		&& secondNamedNPC->specialActionStartCount == 0,
		"XJXQY ordinary NpcSpecialAction uses the first-target non-blocking contract") && ok;

	firstNamedNPC->resetRecording();
	secondNamedNPC->resetRecording();
	gameManager.global.data.canInput = true;
	const int xjxqySpecialExResult = ScriptEngineRuntimeTestAccess::execute(
		script, "npcspecialactionex('duplicate-special-target', 'xjxqy-first.asf')");
	ok = check(xjxqySpecialExResult == LUA_OK
		&& firstNamedNPC->specialActionStartCount == 1
		&& firstNamedNPC->eventRunCount == 1
		&& !firstNamedNPC->inputEnabledDuringEventRun
		&& gameManager.global.data.canInput
		&& secondNamedNPC->specialActionStartCount == 0,
		"XJXQY NpcSpecialActionEx uses the first-target and input contract") && ok;

	firstNamedNPC->resetRecording();
	secondNamedNPC->resetRecording();
	applyOriginalBehavior(gameManager.global, GAME_JXQY2);
	firstNamedNPC->scriptFile = "first-old.lua";
	secondNamedNPC->scriptFile = "second-old.lua";
	gameManager.scriptAPI.setNPCScript(
		"duplicate-special-target", "jxqy2-first.lua");
	ok = check(firstNamedNPC->scriptFile == "jxqy2-first.lua"
		&& secondNamedNPC->scriptFile == "second-old.lua",
		"JXQY2 named SetNpcScript uses the shared first-target contract") && ok;
	gameManager.scriptAPI.setAllNPCScript(
		"duplicate-special-target", "jxqy2-all.lua");
	ok = check(firstNamedNPC->scriptFile == "jxqy2-all.lua"
		&& secondNamedNPC->scriptFile == "jxqy2-all.lua",
		"JXQY2 SetAllNpcScript keeps the explicit broadcast contract") && ok;
	firstNamedNPC->actionManager->resetActionIgnoringTransitions(acWalk);
	secondNamedNPC->actionManager->resetActionIgnoringTransitions(acWalk);
	firstNamedNPC->nowAction = acWalk;
	secondNamedNPC->nowAction = acWalk;
	firstNamedNPC->direction = 1;
	secondNamedNPC->direction = 2;
	gameManager.scriptAPI.setNPCDir("duplicate-special-target", 7);
	ok = check(firstNamedNPC->direction == 1
		&& secondNamedNPC->direction == 2,
		"JXQY2 SetNpcDir preserves main behavior by ignoring moving NPCs") && ok;
	firstNamedNPC->actionManager->resetActionIgnoringTransitions(acStand);
	secondNamedNPC->actionManager->resetActionIgnoringTransitions(acStand);
	firstNamedNPC->nowAction = acStand;
	secondNamedNPC->nowAction = acStand;
	gameManager.scriptAPI.setNPCDir("duplicate-special-target", 7);
	ok = check(firstNamedNPC->direction == 7
		&& secondNamedNPC->direction == 2,
		"JXQY2 named SetNpcDir uses the shared first-target contract") && ok;
	firstNamedNPC->goToCount = 0;
	secondNamedNPC->goToCount = 0;
	gameManager.scriptAPI.goTo("duplicate-special-target", 15, 104);
	ok = check(firstNamedNPC->goToCount == 1
		&& secondNamedNPC->goToCount == 0,
		"JXQY2 named NpcGoto uses the shared first-target contract") && ok;

	firstNamedNPC->resetRecording();
	secondNamedNPC->resetRecording();
	firstNamedNPC->npcIni = "first-old.ini";
	secondNamedNPC->npcIni = "second-old.ini";
	firstNamedNPC->level = 1;
	secondNamedNPC->level = 2;
	firstNamedNPC->strollIntent = 3;
	secondNamedNPC->strollIntent = 4;
	firstNamedNPC->dropIni = "first-old-drop.ini";
	secondNamedNPC->dropIni = "second-old-drop.ini";
	firstNamedNPC->flyIni = "first-old-magic.ini";
	secondNamedNPC->flyIni = "second-old-magic.ini";
	gameManager.scriptAPI.setNPCRes(
		"duplicate-special-target", "jxqy2-first-res.ini");
	gameManager.scriptAPI.setNPCLevel("duplicate-special-target", 20);
	gameManager.scriptAPI.setNPCActionType("duplicate-special-target", 6);
	gameManager.scriptAPI.setNPCActionFile(
		"duplicate-special-target", 4, "jxqy2-first-action.asf");
	gameManager.scriptAPI.setDropIni(
		"duplicate-special-target", "jxqy2-first-drop.ini");
	gameManager.scriptAPI.setNpcMagicFile(
		"duplicate-special-target", "jxqy2-first-magic.ini");
	gameManager.scriptAPI.setNPCPosition(
		"duplicate-special-target", 0, 0);
	gameManager.scriptAPI.setNPCAction(
		"duplicate-special-target", 0, 0, 0);
	gameManager.scriptAPI.attackTo(
		"duplicate-special-target", 11, 13);
	ok = check(firstNamedNPC->npcIni == "jxqy2-first-res.ini"
		&& firstNamedNPC->initResCount == 1
		&& secondNamedNPC->npcIni == "second-old.ini"
		&& secondNamedNPC->initResCount == 0
		&& firstNamedNPC->level == 20
		&& firstNamedNPC->setLevelCount == 1
		&& secondNamedNPC->level == 2
		&& secondNamedNPC->setLevelCount == 0
		&& firstNamedNPC->strollIntent == 6
		&& secondNamedNPC->strollIntent == 4
		&& firstNamedNPC->loadedActionFile == "jxqy2-first-action.asf"
		&& secondNamedNPC->loadActionFileCount == 0
		&& firstNamedNPC->dropIni == "jxqy2-first-drop.ini"
		&& secondNamedNPC->dropIni == "second-old-drop.ini"
		&& firstNamedNPC->flyIni == "jxqy2-first-magic.ini"
		&& secondNamedNPC->flyIni == "second-old-magic.ini"
		&& firstNamedNPC->recordedAction == RecordedNpcAction::Attack
		&& secondNamedNPC->recordedAction == RecordedNpcAction::None
		&& firstNamedNPC->recordedDestination == Point{ 11, 13 }
		&& firstNamedNPC->eventRunCount == 0
		&& secondNamedNPC->eventRunCount == 0,
		"JXQY2 named NPC mutators use the shared first-target contract") && ok;

	firstNamedNPC->state = 1;
	secondNamedNPC->state = 2;
	firstNamedNPC->isAIDisabled = false;
	secondNamedNPC->isAIDisabled = false;
	firstNamedNPC->magicLevel = 1;
	secondNamedNPC->magicLevel = 2;
	firstNamedNPC->attackLevel = 1;
	secondNamedNPC->attackLevel = 2;
	firstNamedNPC->scriptFile = "first-old-click.lua";
	secondNamedNPC->scriptFile = "second-old-click.lua";
	firstNamedNPC->kind = nkNormal;
	secondNamedNPC->kind = nkNormal;
	firstNamedNPC->relation = nrNeutral;
	secondNamedNPC->relation = nrNeutral;
	firstNamedNPC->actionManager->resetActionIgnoringTransitions(acStand);
	secondNamedNPC->actionManager->resetActionIgnoringTransitions(acStand);
	ScriptEngineRuntimeTestAccess::execute(
		script, "setnpcstate('duplicate-special-target', 9)");
	gameManager.scriptAPI.setNpcAIEnabled("duplicate-special-target", false);
	gameManager.scriptAPI.showNpc("duplicate-special-target", 0);
	gameManager.scriptAPI.setNpcMagicLevel("duplicate-special-target", 7);
	gameManager.scriptAPI.setNpcClickScript(
		"duplicate-special-target", "first-click.lua");
	gameManager.scriptAPI.setNpcPartner("duplicate-special-target");
	ok = check(firstNamedNPC->state == 9
		&& secondNamedNPC->state == 2
		&& firstNamedNPC->isAIDisabled
		&& !secondNamedNPC->isAIDisabled
		&& !firstNamedNPC->scriptHidden
		&& firstNamedNPC->isVisibleForRuntime()
		&& secondNamedNPC->scriptHidden
		&& !secondNamedNPC->isVisibleForRuntime()
		&& firstNamedNPC->actionManager->getCurrentActionType() == acStand
		&& secondNamedNPC->actionManager->getCurrentActionType() == acStand
		&& firstNamedNPC->magicLevel == 7
		&& firstNamedNPC->attackLevel == 7
		&& secondNamedNPC->magicLevel == 2
		&& secondNamedNPC->attackLevel == 2
		&& firstNamedNPC->scriptFile == "first-click.lua"
		&& secondNamedNPC->scriptFile == "second-old-click.lua"
		&& firstNamedNPC->kind == nkPartner
		&& firstNamedNPC->relation == nrFriendly
		&& secondNamedNPC->kind == nkNormal
		&& secondNamedNPC->relation == nrNeutral,
		"JXQY2 singular named NPC extensions keep their command-specific target contract") && ok;
	gameManager.scriptAPI.showNpc("duplicate-special-target", 1);
	ok = check(!secondNamedNPC->scriptHidden
		&& secondNamedNPC->isVisibleForRuntime()
		&& secondNamedNPC->actionManager->getCurrentActionType() == acStand,
		"ShowNpc restores the last matching NPC without replacing its current action") && ok;

	auto duplicatePlayerNameNPC = std::make_shared<RecordingNPC>();
	duplicatePlayerNameNPC->npcName = gameManager.player->npcName;
	gameManager.npcManager->npcList.push_back(duplicatePlayerNameNPC);
	gameManager.scriptAPI.showNpc(gameManager.player->npcName, 0);
	ok = check(gameManager.player->scriptHidden
		&& !duplicatePlayerNameNPC->scriptHidden,
		"ShowNpc gives the player priority over same-name map NPCs") && ok;
	gameManager.scriptAPI.showNpc(gameManager.player->npcName, 1);
	ok = check(!gameManager.player->scriptHidden,
		"ShowNpc restores player visibility through the player-priority path") && ok;
	gameManager.npcManager->npcList.pop_back();

	firstNamedNPC->lifeMax = 100;
	secondNamedNPC->lifeMax = 200;
	firstNamedNPC->life = 100;
	secondNamedNPC->life = 200;
	firstNamedNPC->manaMax = 120;
	secondNamedNPC->manaMax = 220;
	firstNamedNPC->mana = 120;
	secondNamedNPC->mana = 220;
	firstNamedNPC->thewMax = 140;
	secondNamedNPC->thewMax = 240;
	firstNamedNPC->thew = 140;
	secondNamedNPC->thew = 240;
	firstNamedNPC->kindValue = 10;
	secondNamedNPC->kindValue = 20;
	firstNamedNPC->kindValueMax = 100;
	secondNamedNPC->kindValueMax = 100;
	firstNamedNPC->talkContent = "first-old-talk";
	secondNamedNPC->talkContent = "second-old-talk";
	firstNamedNPC->flyInis.clear();
	secondNamedNPC->flyInis.clear();
	firstNamedNPC->isSignalShow = false;
	secondNamedNPC->isSignalShow = true;
	firstNamedNPC->signalIndex = 0;
	secondNamedNPC->signalIndex = 5;
	firstNamedNPC->signalType = "first-old-signal";
	secondNamedNPC->signalType = "second-old-signal";
	gameManager.scriptAPI.changeLife("duplicate-special-target", 25);
	gameManager.scriptAPI.changeMana("duplicate-special-target", 50);
	gameManager.scriptAPI.changeThew("duplicate-special-target", 75);
	gameManager.scriptAPI.addKindValue("duplicate-special-target", 7);
	gameManager.scriptAPI.setNpcTalkContent(
		"duplicate-special-target", "first-talk");
	gameManager.scriptAPI.addNpcMagic(
		"duplicate-special-target", "first-extra-magic.ini");
	gameManager.scriptAPI.showSignalTip(
		"duplicate-special-target", 3, "quest");
	gameManager.scriptAPI.setSignalTipHidden("duplicate-special-target");
	ok = check(firstNamedNPC->life == 25
		&& secondNamedNPC->life == 200
		&& firstNamedNPC->mana == 60
		&& secondNamedNPC->mana == 220
		&& firstNamedNPC->thew == 105
		&& secondNamedNPC->thew == 240
		&& firstNamedNPC->kindValue == 17
		&& secondNamedNPC->kindValue == 20
		&& firstNamedNPC->talkContent == "first-talk"
		&& secondNamedNPC->talkContent == "second-old-talk"
		&& firstNamedNPC->flyInis == "first-extra-magic.ini:0;"
		&& secondNamedNPC->flyInis.empty()
		&& !firstNamedNPC->isSignalShow
		&& secondNamedNPC->isSignalShow
		&& firstNamedNPC->signalIndex == 3
		&& secondNamedNPC->signalIndex == 5
		&& firstNamedNPC->signalType == "quest"
		&& secondNamedNPC->signalType == "second-old-signal",
		"JXQY2 named extension mutators share the first-target contract") && ok;

	ScriptEngineRuntimeTestAccess::execute(
		script, "npcspecialaction('duplicate-special-target', 'jxqy2-blocking.asf')");
	ok = check(firstNamedNPC->specialActionStartCount == 1
		&& firstNamedNPC->eventRunCount == 1
		&& secondNamedNPC->specialActionStartCount == 0
		&& secondNamedNPC->eventRunCount == 0,
		"JXQY2 ordinary NpcSpecialAction keeps blocking but uses the shared first target") && ok;

	firstNamedNPC->resetRecording();
	secondNamedNPC->resetRecording();
	ScriptEngineRuntimeTestAccess::execute(
		script, "npcspecialactionnonblocking('duplicate-special-target', 'jxqy2-non-blocking.asf')");
	ok = check(firstNamedNPC->specialActionStartCount == 1
		&& firstNamedNPC->eventRunCount == 0
		&& secondNamedNPC->specialActionStartCount == 0
		&& secondNamedNPC->eventRunCount == 0,
		"JXQY2 explicit NpcSpecialActionNonBlocking uses the shared first target") && ok;
	firstNamedNPC->kind = nkNormal;
	secondNamedNPC->kind = nkNormal;
	auto sameNamePartner = std::make_shared<RecordingNPC>();
	sameNamePartner->npcName = "duplicate-special-target";
	sameNamePartner->kind = nkPartner;
	gameManager.npcManager->npcList.push_back(sameNamePartner);
	gameManager.scriptAPI.deleteNPC("duplicate-special-target");
	ok = check(gameManager.npcManager->npcList.size() == 1
		&& gameManager.npcManager->npcList.front() == sameNamePartner,
		"DelNpc removes every matching ordinary NPC but preserves a same-name partner") && ok;
	gameManager.npcManager->npcList = std::move(savedNpcList);

	auto forcedSpecialNPC = std::make_shared<InMemorySpecialActionNPC>();
	forcedSpecialNPC->actionManager->resetActionIgnoringTransitions(acHide);
	forcedSpecialNPC->immobilized = true;
	forcedSpecialNPC->petrified = true;
	forcedSpecialNPC->petrifiedLastTime = 100;
	const bool forcedSpecialStarted = forcedSpecialNPC->startScriptSpecialAction("forced-special.asf");
	ok = check(forcedSpecialStarted
		&& forcedSpecialNPC->actionManager->getCurrentActionType() == acSpecial,
		"script special actions bypass ordinary transition, immobilize, and petrify gates") && ok;
	forcedSpecialNPC->setOffset({ 7.0f, 9.0f });
	forcedSpecialNPC->stepList.push_back({ 2, 3 });
	forcedSpecialNPC->actionBeginTime = std::numeric_limits<UTime>::max();
	const bool restartedSpecial = forcedSpecialNPC->startScriptSpecialAction("restarted-special.asf");
	ok = check(restartedSpecial
		&& forcedSpecialNPC->loadedFileName == "restarted-special.asf"
		&& forcedSpecialNPC->getOffset().x == 0.0f
		&& forcedSpecialNPC->getOffset().y == 0.0f
		&& forcedSpecialNPC->stepList.empty()
		&& forcedSpecialNPC->actionBeginTime != std::numeric_limits<UTime>::max(),
		"a second scripted special action restarts the active one-shot animation") && ok;
	forcedSpecialNPC->advanceFrameForTest(forcedSpecialNPC->actionLastTime);
	ok = check(forcedSpecialNPC->actionManager->getCurrentActionType() == acSpecial
		&& forcedSpecialNPC->petrified,
		"JXQY2 petrify pauses a scripted special action like main") && ok;
	forcedSpecialNPC->clearPetrifiedState();
	forcedSpecialNPC->advanceFrameForTest(forcedSpecialNPC->actionLastTime);
	ok = check(forcedSpecialNPC->actionManager->getCurrentActionType() == acStand,
		"JXQY2 scripted special actions resume and return to stand after petrify clears") && ok;

	for (int yycsOrXjxqyGameType : { GAME_YYCS, GAME_XJXQY })
	{
		applyOriginalBehavior(
			gameManager.global, yycsOrXjxqyGameType);
		auto overlaySpecialNPC = std::make_shared<InMemorySpecialActionNPC>();
		overlaySpecialNPC->res.special.imageFile = "persistent-special.asf";
		overlaySpecialNPC->actionManager->resetActionIgnoringTransitions(acHide);
		overlaySpecialNPC->immobilized = true;
		overlaySpecialNPC->petrified = true;
		overlaySpecialNPC->direction = 2;
		overlaySpecialNPC->setOffset({ 7.0f, 9.0f });
		overlaySpecialNPC->stepList.push_back({ 2, 3 });
		const bool firstOverlayStarted = overlaySpecialNPC->startScriptSpecialAction("first-overlay.asf");
		const UTime firstOverlayDuration = overlaySpecialNPC->scriptSpecialActionOverlayDuration;
		overlaySpecialNPC->direction = 6;
		const bool secondOverlayStarted = overlaySpecialNPC->startScriptSpecialAction("second-overlay.asf");
		overlaySpecialNPC->provideResource = false;
		const bool missingOverlayRestarted = overlaySpecialNPC->startScriptSpecialAction("missing-overlay.asf");
		overlaySpecialNPC->provideResource = true;
		const bool failedRestartPreservedOverlay = !missingOverlayRestarted
			&& overlaySpecialNPC->scriptSpecialActionOverlayResource.imageFile == "second-overlay.asf"
			&& overlaySpecialNPC->scriptSpecialActionOverlayActive;
		overlaySpecialNPC->direction = 7;
		const UTime actionBeginTimeBeforeHiddenOverlay = overlaySpecialNPC->actionBeginTime;
		overlaySpecialNPC->visibleVariableName = "$hidden-special-overlay";
		overlaySpecialNPC->visibleVariableValue = 1;
		overlaySpecialNPC->advanceFrameForTest(25);
		const bool hiddenOverlayPaused = overlaySpecialNPC->scriptSpecialActionOverlayActive
			&& overlaySpecialNPC->scriptSpecialActionOverlayElapsed == 0
			&& overlaySpecialNPC->actionBeginTime == actionBeginTimeBeforeHiddenOverlay + 25;
		overlaySpecialNPC->visibleVariableName.clear();
		overlaySpecialNPC->advanceFrameForTest(overlaySpecialNPC->scriptSpecialActionOverlayDuration);
		ok = check(firstOverlayStarted
			&& secondOverlayStarted
			&& failedRestartPreservedOverlay
			&& firstOverlayDuration > 0
			&& hiddenOverlayPaused
			&& !overlaySpecialNPC->scriptSpecialActionOverlayActive
			&& overlaySpecialNPC->scriptSpecialActionOverlayElapsed == overlaySpecialNPC->scriptSpecialActionOverlayDuration
			&& overlaySpecialNPC->actionManager->getCurrentActionType() == acHide
			&& overlaySpecialNPC->direction == 6
			&& overlaySpecialNPC->getOffset().x == 7.0f
			&& overlaySpecialNPC->getOffset().y == 9.0f
			&& overlaySpecialNPC->stepList.size() == 1
			&& overlaySpecialNPC->stepList.front() == Point{ 2, 3 }
			&& overlaySpecialNPC->res.special.imageFile == "persistent-special.asf",
			"YYCS/XJXQY special overlays restart without destroying the underlying action or path") && ok;
	}
	applyOriginalBehavior(gameManager.global, GAME_XJXQY);
	auto interruptedOverlayNPC = std::make_shared<InMemorySpecialActionNPC>();
	interruptedOverlayNPC->res.hurt.imagePackage = std::make_shared<IMPImage>();
	interruptedOverlayNPC->res.hurt.imagePackage->directions = 1;
	interruptedOverlayNPC->res.hurt.imagePackage->interval = 100;
	interruptedOverlayNPC->res.hurt.imagePackage->frame.resize(1);
	interruptedOverlayNPC->direction = 2;
	interruptedOverlayNPC->startScriptSpecialAction("interrupted-overlay.asf");
	interruptedOverlayNPC->direction = 7;
	interruptedOverlayNPC->actionManager->resetActionIgnoringTransitions(acHurt);
	interruptedOverlayNPC->advanceFrameForTest(50);
	ok = check(interruptedOverlayNPC->scriptSpecialActionOverlayActive
		&& interruptedOverlayNPC->scriptSpecialActionOverlaySupersededByAction
		&& interruptedOverlayNPC->actionManager->getCurrentActionType() == acHurt
		&& interruptedOverlayNPC->direction == 7,
		"a new one-shot action visually supersedes a running YYCS/XJXQY special overlay") && ok;
	interruptedOverlayNPC->advanceFrameForTest(50);
	ok = check(!interruptedOverlayNPC->scriptSpecialActionOverlayActive
		&& interruptedOverlayNPC->actionManager->getCurrentActionType() == acHurt
		&& interruptedOverlayNPC->direction == 2,
		"the replacement one-shot ends the YYCS/XJXQY special lifecycle and restores direction") && ok;
	interruptedOverlayNPC->advanceFrameForTest(1);
	ok = check(interruptedOverlayNPC->actionManager->getCurrentActionType() == acStand,
		"the replacement one-shot completes through its normal action logic on the next frame") && ok;

	auto attackingOverlayNPC = std::make_shared<InMemorySpecialActionNPC>();
	attackingOverlayNPC->res.attack.imagePackage = std::make_shared<IMPImage>();
	attackingOverlayNPC->res.attack.imagePackage->directions = 8;
	attackingOverlayNPC->res.attack.imagePackage->interval = 100;
	attackingOverlayNPC->res.attack.imagePackage->frame.resize(1);
	attackingOverlayNPC->setPosition({ 3, 4 });
	attackingOverlayNPC->direction = 2;
	attackingOverlayNPC->startScriptSpecialAction("attack-interrupted-overlay.asf");
	attackingOverlayNPC->beginAttack({ 4, 4 });
	attackingOverlayNPC->advanceFrameForTest(50);
	ok = check(attackingOverlayNPC->scriptSpecialActionOverlayActive
		&& attackingOverlayNPC->scriptSpecialActionOverlaySupersededByAction
		&& attackingOverlayNPC->actionManager->getCurrentActionType() == acAttack,
		"a scripted attack takes over a running YYCS/XJXQY special overlay") && ok;
	attackingOverlayNPC->advanceFrameForTest(50);
	ok = check(!attackingOverlayNPC->scriptSpecialActionOverlayActive
		&& attackingOverlayNPC->actionManager->getCurrentActionType() == acAttack
		&& attackingOverlayNPC->direction == 2,
		"the attack animation ends the special lifecycle before normal attack completion") && ok;
	attackingOverlayNPC->advanceFrameForTest(1);
	ok = check(attackingOverlayNPC->actionManager->getCurrentActionType() == acStand,
		"the takeover attack releases and returns to stand on its normal completion frame") && ok;

	applyOriginalBehavior(gameManager.global, GAME_YYCS);
	auto blockingSpecialNPC = std::make_shared<BlockingInMemorySpecialActionNPC>();
	blockingSpecialNPC->res.hurt.imagePackage = std::make_shared<IMPImage>();
	blockingSpecialNPC->res.hurt.imagePackage->directions = 1;
	blockingSpecialNPC->res.hurt.imagePackage->interval = 5000;
	blockingSpecialNPC->res.hurt.imagePackage->frame.resize(1);
	blockingSpecialNPC->actionManager->resetActionIgnoringTransitions(acHurt);
	blockingSpecialNPC->doSpecialAction("yycs-blocking-special.asf");
	ok = check(blockingSpecialNPC->actionManager->getCurrentActionType() == acHurt
		&& !blockingSpecialNPC->scriptSpecialActionOverlayActive
		&& blockingSpecialNPC->scriptSpecialActionOverlayDuration > 0
		&& blockingSpecialNPC->scriptSpecialActionOverlayElapsed == blockingSpecialNPC->scriptSpecialActionOverlayDuration
		&& !blockingSpecialNPC->eventRunUntilScriptSpecialActionEnds,
		"blocking special actions return when the special animation ends, before a restored action ends") && ok;
	applyOriginalBehavior(gameManager.global, GAME_JXQY2);
	auto legacyBlockingSpecialNPC = std::make_shared<BlockingInMemorySpecialActionNPC>();
	legacyBlockingSpecialNPC->actionManager->resetActionIgnoringTransitions(acHide);
	legacyBlockingSpecialNPC->visibleVariableName = "$hidden-blocking-special";
	legacyBlockingSpecialNPC->visibleVariableValue = 1;
	legacyBlockingSpecialNPC->doSpecialAction("jxqy2-blocking-special.asf");
	ok = check(legacyBlockingSpecialNPC->actionManager->getCurrentActionType() == acStand
		&& !legacyBlockingSpecialNPC->isVisibleByVariable,
		"JXQY2 hidden blocking special actions preserve main's return-to-stand behavior") && ok;

	auto missingSpecialNPC = std::make_shared<InMemorySpecialActionNPC>();
	missingSpecialNPC->provideResource = false;
	missingSpecialNPC->doSpecialAction("missing-special.asf");
	ok = check(missingSpecialNPC->loadCount == 1
		&& missingSpecialNPC->loadedFileName == "missing-special.asf"
		&& missingSpecialNPC->eventRunCount == 0
		&& missingSpecialNPC->actionManager->getCurrentActionType() == acStand,
		"missing special-action resources fail without waiting on the previous action") && ok;

	gameManager.mapFolderName = "script-position-trap-test";
	gameManager.global.data.mapName = "script-position-trap-test.map";
	gameManager.map->data->tile[4][3].trap = 1;
	gameManager.map->data->tile[4][5].trap = 2;
	gameManager.traps.set(gameManager.mapFolderName, 1, "script-position-trap-1.lua");
	gameManager.traps.set(gameManager.mapFolderName, 2, "script-position-trap-2.lua");
	gameManager.camera->followPlayer = false;
	gameManager.inEvent = true;
	gameManager.scriptTaskList.clear();
	const int setPlayerPositionResult = ScriptEngineRuntimeTestAccess::execute(
		script, "setplayerpos(3, 4)");
	ok = check(setPlayerPositionResult == LUA_OK
		&& gameManager.player->getPosition() == Point{ 3, 4 }
		&& gameManager.scriptTaskList.empty(),
		"script SetPlayerPos places the player without queuing its landing trap") && ok;
	gameManager.player->checkTrap();
	ok = check(gameManager.scriptTaskList.empty(),
		"frame trap checks keep the script placement tile suppressed") && ok;
	gameManager.player->setPosition({ 5, 4 }, false);
	gameManager.player->checkTrap();
	ok = check(gameManager.scriptTaskList.size() == 1
		&& gameManager.scriptTaskList[0].type == stTraps
		&& gameManager.scriptTaskList[0].trapIndex == 2,
		"a non-script teleport to another tile still queues its landing trap") && ok;
	gameManager.scriptTaskList.clear();
	gameManager.inEvent = false;

	gameManager.traps.markTriggered(1);
	const int rearmCurrentTrapResult =
		ScriptEngineRuntimeTestAccess::execute(
			script,
			"setmaptrap(1, \"rearmed-current-trap.lua\")");
	ok = check(rearmCurrentTrapResult == LUA_OK
		&& !gameManager.traps.hasTriggered(1)
		&& gameManager.traps.get(
			gameManager.mapFolderName,
			1) == "rearmed-current-trap.lua",
		"SetMapTrap replaces and rearms the current map trap index") && ok;

	const int setMaximumTrapResult =
		ScriptEngineRuntimeTestAccess::execute(
			script,
			"setmaptrap(255, \"maximum-trap.lua\")");
	ok = check(setMaximumTrapResult == LUA_OK
		&& gameManager.traps.get(
			gameManager.mapFolderName,
			255) == "maximum-trap.lua",
		"SetMapTrap accepts the full byte-sized map trap range") && ok;
	gameManager.traps.markTriggered(255);
	const int removeMaximumTrapResult =
		ScriptEngineRuntimeTestAccess::execute(
			script,
			"setmaptrap(255, \"\")");
	ok = check(removeMaximumTrapResult == LUA_OK
		&& !gameManager.traps.hasTriggered(255)
		&& gameManager.traps.get(
			gameManager.mapFolderName,
			255).empty(),
		"SetMapTrap removes an empty mapping and rearms its current-map index") && ok;

	const int largeMapTimeResult = ScriptEngineRuntimeTestAccess::execute(
		script, "setmaptime(300)");
	ok = check(largeMapTimeResult == LUA_OK
		&& gameManager.global.data.mapTime == 300,
		"SetMapTime preserves values above the byte range") && ok;
	const int negativeMapTimeResult = ScriptEngineRuntimeTestAccess::execute(
		script, "setmaptime(-1)");
	ok = check(negativeMapTimeResult == LUA_OK
		&& gameManager.global.data.mapTime == -1,
		"SetMapTime preserves negative integer state like the reference runtime") && ok;
	gameManager.scriptAPI.setMapTime(mtDay);

	gameManager.traps.markTriggered(1);
	const int rearmNamedCurrentTrapResult =
		ScriptEngineRuntimeTestAccess::execute(
			script,
			"settrap(\"script-position-trap-test\", 1, \"rearmed-named-trap.lua\")");
	ok = check(rearmNamedCurrentTrapResult == LUA_OK
		&& !gameManager.traps.hasTriggered(1)
		&& gameManager.traps.get(
			gameManager.mapFolderName,
			1) == "rearmed-named-trap.lua",
		"SetTrap rearms an explicitly named current map") && ok;

	gameManager.traps.markTriggered(1);
	const int editOtherMapTrapResult =
		ScriptEngineRuntimeTestAccess::execute(
			script,
			"settrap(\"another-map\", 1, \"another-map-trap.lua\")");
	ok = check(editOtherMapTrapResult == LUA_OK
		&& gameManager.traps.hasTriggered(1)
		&& gameManager.traps.get(
			"another-map",
			1) == "another-map-trap.lua",
		"SetTrap edits another map without rearming the current map index") && ok;

	GameManager freeMapManager;
	freeMapManager.mapFolderName = "free-map-trap-test";
	freeMapManager.traps.set(
		freeMapManager.mapFolderName,
		1,
		"retained-after-free-map.lua");
	freeMapManager.traps.markTriggered(1);
	freeMapManager.global.data.mapName = "free-map-trap-test.map";
	freeMapManager.global.data.npcName = "retained.npc";
	freeMapManager.global.data.objName = "retained.obj";
	freeMapManager.global.data.rainFile = "retained-rain.ini";
	freeMapManager.global.data.rainShow = true;
	auto retainedFreeMapNpc = std::make_shared<NPC>();
	auto retainedFreeMapObject = std::make_shared<Object>();
	freeMapManager.npcManager->npcList.push_back(nullptr);
	freeMapManager.objectManager->objectList.push_back(nullptr);
	freeMapManager.scriptNPC = retainedFreeMapNpc;
	freeMapManager.scriptObj = retainedFreeMapObject;
	freeMapManager.camera->followPlayer = true;
	freeMapManager.scriptAPI.freeMap();
	ok = check(freeMapManager.traps.get(
			"free-map-trap-test",
			1) == "retained-after-free-map.lua"
		&& freeMapManager.traps.hasTriggered(1)
		&& freeMapManager.map->data == nullptr
		&& freeMapManager.mapFolderName == "free-map-trap-test"
		&& freeMapManager.global.data.mapName == "free-map-trap-test.map"
		&& freeMapManager.global.data.npcName == "retained.npc"
		&& freeMapManager.global.data.objName == "retained.obj"
		&& freeMapManager.global.data.rainFile == "retained-rain.ini"
		&& freeMapManager.global.data.rainShow
		&& freeMapManager.npcManager->npcList.size() == 1
		&& freeMapManager.npcManager->npcList.front() == nullptr
		&& freeMapManager.objectManager->objectList.size() == 1
		&& freeMapManager.objectManager->objectList.front() == nullptr
		&& freeMapManager.scriptNPC == retainedFreeMapNpc
		&& freeMapManager.scriptObj == retainedFreeMapObject
		&& freeMapManager.camera->followPlayer,
		"FreeMap releases only map data and preserves the reference runtime world state") && ok;

	GameManager noCurrentMapManager;
	noCurrentMapManager.scriptAPI.setMapTrap(1, "must-not-create-an-empty-map.lua");
	noCurrentMapManager.scriptAPI.setTrap("", 2, "must-not-create-an-empty-map.lua");
	ok = check(noCurrentMapManager.traps.get("", 1).empty()
		&& noCurrentMapManager.traps.get("", 2).empty(),
		"SetMapTrap and an unnamed SetTrap do nothing when no current map exists") && ok;

	ScriptTask parallelTask;
	parallelTask.type = stScript;
	parallelTask.scriptName = "bounded-parallel.lua";
	for (std::size_t index = 0; index < MaxParallelScriptStates; index++)
	{
		ok = check(gameManager.addScriptTask(parallelTask),
			"parallel script tasks are accepted within the runtime limit") && ok;
	}
	ok = check(!gameManager.addScriptTask(parallelTask)
		&& gameManager.scriptTaskList.size() == MaxParallelScriptStates,
		"parallel script task scheduling is bounded before persistence") && ok;
	ScriptTask interactionTask;
	interactionTask.type = stTraps;
	ok = check(gameManager.addScriptTask(interactionTask)
		&& gameManager.scriptTaskList.size() == MaxParallelScriptStates + 1,
		"parallel script limits do not discard non-script interaction tasks") && ok;

	const std::string savedPlayerDeathScript = gameManager.player->deathScript;
	const unsigned int savedPlayerResult = gameManager.player->result;
	gameManager.player->deathScript.clear();
	gameManager.player->result = erRunDeathScript;
	gameManager.eventList.push_back(EventInfo{});
	const bool playerDeathHandled =
		ScriptEngineRuntimeTestAccess::runPendingPlayerDeathScript(gameManager);
	ok = check(playerDeathHandled
		&& (gameManager.player->result & erRunDeathScript) == 0
		&& gameManager.eventList.empty()
		&& gameManager.scriptTaskList.empty(),
		"player death takes priority and discards same-frame NPC events and queued scripts") && ok;
	ok = check(!ScriptEngineRuntimeTestAccess::runPendingPlayerDeathScript(gameManager),
		"a consumed player death script is not dispatched twice") && ok;
	gameManager.player->deathScript = savedPlayerDeathScript;
	gameManager.player->result = savedPlayerResult;

	for (std::size_t index = 0; index < MaxParallelScriptStates; index++)
	{
		ok = check(gameManager.addScriptTask(parallelTask),
			"parallel script tasks can be queued again after player-death cleanup") && ok;
	}
	ok = check(gameManager.addScriptTask(interactionTask),
		"interaction tasks can be queued again after player-death cleanup") && ok;
	gameManager.scriptAPI.returnToTitle();
	const std::size_t remainingParallelTasks = static_cast<std::size_t>(
		std::count_if(
			gameManager.scriptTaskList.begin(),
			gameManager.scriptTaskList.end(),
			[](const ScriptTask& task)
			{
				return task.type == stScript;
			}));
	ok = check(remainingParallelTasks == 0
		&& gameManager.scriptTaskList.size() == 1
		&& gameManager.scriptTaskList.front().type == stTraps
		&& gameManager.result == erOK
		&& !ScriptEngineRuntimeTestAccess::isLogicRunning(gameManager),
		"ReturnToTitle clears YYCS/XJXQY parallel scripts before leaving the game") && ok;

	return ok;
}
