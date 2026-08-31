#include "Script.h"
#include "../GameManager/GameManager.h"
#include "../../Engine/Engine.h"
#include "../../File/ResourcePathSafety.h"
#include "../../Launch/EditorRunRuntimeTraceWriter.h"
#include <cctype>
#include <climits>
#include <limits>
#include <string_view>
#include <vector>

#if defined(_MSC_VER)
#define JXQY_SCRIPT_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define JXQY_SCRIPT_NOINLINE __attribute__((noinline))
#else
#define JXQY_SCRIPT_NOINLINE
#endif

namespace
{
int ApplicationQuitScriptAbortToken = 0;

void removeLuaGlobal(lua_State* luaState, const char* name)
{
	lua_pushnil(luaState);
	lua_setglobal(luaState, name);
}

void copyLuaTableFunction(
	lua_State* luaState,
	int sourceTableIndex,
	int targetTableIndex,
	const char* name)
{
	sourceTableIndex = lua_absindex(luaState, sourceTableIndex);
	targetTableIndex = lua_absindex(luaState, targetTableIndex);
	lua_getfield(luaState, sourceTableIndex, name);
	if (lua_isfunction(luaState, -1))
	{
		lua_setfield(luaState, targetTableIndex, name);
	}
	else
	{
		lua_pop(luaState, 1);
	}
}

void installEditorRunOsLibrary(lua_State* luaState)
{
	const int initialStackTop = lua_gettop(luaState);
	lua_getglobal(luaState, LUA_OSLIBNAME);
	if (!lua_istable(luaState, -1))
	{
		lua_settop(luaState, initialStackTop);
		return;
	}

	const int sourceTableIndex = lua_absindex(luaState, -1);
	lua_createtable(luaState, 0, 4);
	const int targetTableIndex = lua_absindex(luaState, -1);
	copyLuaTableFunction(luaState, sourceTableIndex, targetTableIndex, "clock");
	copyLuaTableFunction(luaState, sourceTableIndex, targetTableIndex, "date");
	copyLuaTableFunction(luaState, sourceTableIndex, targetTableIndex, "difftime");
	copyLuaTableFunction(luaState, sourceTableIndex, targetTableIndex, "time");

	lua_pushvalue(luaState, targetTableIndex);
	lua_setglobal(luaState, LUA_OSLIBNAME);

	luaL_getsubtable(luaState, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
	lua_pushvalue(luaState, targetTableIndex);
	lua_setfield(luaState, -2, LUA_OSLIBNAME);
	lua_settop(luaState, initialStackTop);
}

void openEditorRunLibraries(lua_State* luaState)
{
	constexpr int EditorRunLibraryMask =
		LUA_GLIBK |
		LUA_COLIBK |
		LUA_MATHLIBK |
		LUA_OSLIBK |
		LUA_STRLIBK |
		LUA_TABLIBK |
		LUA_UTF8LIBK;
	luaL_openselectedlibs(luaState, EditorRunLibraryMask, 0);

	removeLuaGlobal(luaState, "collectgarbage");
	removeLuaGlobal(luaState, "dofile");
	removeLuaGlobal(luaState, "load");
	removeLuaGlobal(luaState, "loadfile");
	installEditorRunOsLibrary(luaState);
}

std::string getLuaString(lua_State* l, int index)
{
	const char* value = lua_tolstring(l, index, nullptr);
	return value == nullptr ? "" : value;
}

// Child scripts may report an error through Lua's long jump. Keep the owned
// file name in a frame that has returned before lua_RunScript propagates it.
JXQY_SCRIPT_NOINLINE int runLuaChildScript(
	lua_State* luaState,
	GameManager* gameManager)
{
	if (luaState == nullptr || gameManager == nullptr)
	{
		return -1;
	}
	std::size_t length = 0;
	const char* value = lua_tolstring(luaState, 1, &length);
	if (value == nullptr)
	{
		return -1;
	}
	const std::string fileName(value, length);
	GameLog::write("lua_RunScript: %s\n", fileName.c_str());
	return gameManager->scriptAPI.runScriptForLua(fileName);
}

std::string getLuaSpeakerDialogText(lua_State* l, int speakerIndex, int textIndex)
{
	std::string speaker = getLuaString(l, speakerIndex);
	const std::string text = getLuaString(l, textIndex);
	if (speaker == "#name" && gm != nullptr && gm->player != nullptr
		&& !gm->player->npcName.empty())
	{
		speaker = gm->player->npcName;
	}
	if (speaker.empty())
	{
		return text;
	}
	return speaker + ": " + text;
}

bool getTalkTextFromLuaArgument(lua_State* l, int index, std::string& text)
{
	if (!lua_isnumber(l, index))
	{
		return false;
	}
	text = gm->talkTextList.getText((int)lua_tointeger(l, index));
	return true;
}

bool getLuaBoolean(lua_State* l, int index, bool defaultValue = true)
{
	if (lua_gettop(l) < index)
	{
		return defaultValue;
	}
	if (lua_isboolean(l, index))
	{
		return lua_toboolean(l, index) != 0;
	}
	if (lua_isnumber(l, index))
	{
		return lua_tointeger(l, index) != 0;
	}
	std::string value = convert::lowerCase(getLuaString(l, index));
	return !(value == "0" || value == "false" || value == "no" || value == "off");
}

std::string getLuaSnapshotKey(lua_State* l, int index)
{
	if (lua_gettop(l) < index || lua_isnil(l, index))
	{
		return "";
	}
	return getLuaString(l, index);
}

bool parsePositiveDecimal(
	std::string_view text,
	std::size_t& position,
	std::uint32_t& value)
{
	if (position >= text.size() ||
		!std::isdigit(static_cast<unsigned char>(text[position])))
	{
		return false;
	}

	std::uint64_t parsed = 0;
	while (position < text.size() &&
		std::isdigit(static_cast<unsigned char>(text[position])))
	{
		parsed =
			parsed * 10 +
			static_cast<std::uint64_t>(text[position] - '0');
		if (parsed > std::numeric_limits<std::uint32_t>::max())
		{
			return false;
		}
		++position;
	}
	value = static_cast<std::uint32_t>(parsed);
	return value > 0;
}

std::string_view sourceLineAt(
	const char* source,
	int length,
	std::uint32_t requestedLine)
{
	if (source == nullptr || length <= 0 || requestedLine == 0)
	{
		return {};
	}

	const std::string_view sourceView(
		source,
		static_cast<std::size_t>(length));
	std::size_t lineStart = 0;
	std::uint32_t currentLine = 1;
	while (currentLine < requestedLine)
	{
		const std::size_t lineEnd =
			sourceView.find('\n', lineStart);
		if (lineEnd == std::string_view::npos)
		{
			return {};
		}
		lineStart = lineEnd + 1;
		++currentLine;
	}

	std::size_t lineEnd = sourceView.find('\n', lineStart);
	if (lineEnd == std::string_view::npos)
	{
		lineEnd = sourceView.size();
	}
	if (lineEnd > lineStart && sourceView[lineEnd - 1] == '\r')
	{
		--lineEnd;
	}
	return sourceView.substr(lineStart, lineEnd - lineStart);
}

std::uint32_t unicodeColumnAtByteOffset(
	std::string_view sourceLine,
	std::size_t byteOffset)
{
	if (byteOffset > sourceLine.size())
	{
		return 0;
	}

	const std::string sourceLineText(sourceLine);
	if (!ResourcePathSafety::isValidUtf8(sourceLineText) ||
		(byteOffset < sourceLine.size() &&
			(static_cast<unsigned char>(sourceLine[byteOffset]) & 0xC0) ==
				0x80))
	{
		return 0;
	}

	std::uint64_t codePointCount = 0;
	for (std::size_t index = 0; index < byteOffset; ++index)
	{
		if ((static_cast<unsigned char>(sourceLine[index]) & 0xC0) !=
			0x80)
		{
			++codePointCount;
		}
	}
	if (codePointCount + 1 >
		std::numeric_limits<std::uint32_t>::max())
	{
		return 0;
	}
	return static_cast<std::uint32_t>(codePointCount + 1);
}

std::uint32_t findUnambiguousLoadErrorColumn(
	const char* source,
	int length,
	std::uint32_t line,
	std::string_view canonicalMessage)
{
	const std::string_view sourceLine =
		sourceLineAt(source, length, line);
	if (sourceLine.data() == nullptr)
	{
		return 0;
	}

	constexpr std::string_view NearQuotedToken = "near '";
	const std::size_t marker =
		canonicalMessage.rfind(NearQuotedToken);
	if (marker != std::string_view::npos)
	{
		const std::size_t tokenStart =
			marker + NearQuotedToken.size();
		const std::size_t tokenEnd =
			canonicalMessage.find('\'', tokenStart);
		if (tokenEnd != std::string_view::npos &&
			tokenEnd > tokenStart)
		{
			const std::string_view token =
				canonicalMessage.substr(
					tokenStart,
					tokenEnd - tokenStart);
			const std::size_t first = sourceLine.find(token);
			if (first != std::string_view::npos &&
				sourceLine.find(token, first + 1) ==
					std::string_view::npos)
			{
				return unicodeColumnAtByteOffset(
					sourceLine,
					first);
			}
		}
	}

	if (canonicalMessage.find("near <eof>") !=
		std::string_view::npos)
	{
		return unicodeColumnAtByteOffset(
			sourceLine,
			sourceLine.size());
	}
	return 0;
}

void populateExactLuaFailure(
	ExactScriptExecutionResult& result,
	ExactScriptExecutionStatus status,
	const std::string& rawMessage,
	const std::string& chunkName,
	const char* source,
	int length)
{
	result.status = status;
	result.line = 0;
	result.column = 0;
	result.message = rawMessage;

	const std::string sourceName =
		!chunkName.empty() && chunkName.front() == '@'
		? chunkName.substr(1)
		: chunkName;
	const std::string prefix = sourceName + ":";
	if (sourceName.empty() ||
		rawMessage.rfind(prefix, 0) != 0)
	{
		return;
	}

	std::size_t position = prefix.size();
	std::uint32_t parsedLine = 0;
	if (!parsePositiveDecimal(
			rawMessage,
			position,
			parsedLine) ||
		position >= rawMessage.size() ||
		rawMessage[position] != ':')
	{
		return;
	}
	++position;

	std::size_t messageStart = position;
	while (messageStart < rawMessage.size() &&
		std::isspace(static_cast<unsigned char>(
			rawMessage[messageStart])))
	{
		++messageStart;
	}

	result.line = parsedLine;
	result.message = rawMessage.substr(messageStart);
	if (status == ExactScriptExecutionStatus::LoadFailed)
	{
		result.column = findUnambiguousLoadErrorColumn(
			source,
			length,
			result.line,
			result.message);
	}
}
}

Script::Script(const std::string & n) :
	Script(n, ScriptLibraryProfile::Full, nullptr)
{
}

Script::Script(
	const std::string& n,
	ScriptLibraryProfile libraryProfile,
	EditorRun::RuntimeTraceWriter* writer)
{
	runtimeTraceWriter = writer;
	name = n;
	loadLib(libraryProfile);
	registerFunc();
	runScript(n);
}

Script::Script() :
	Script(ScriptLibraryProfile::Full, nullptr)
{
}

Script::Script(
	ScriptLibraryProfile libraryProfile,
	EditorRun::RuntimeTraceWriter* writer)
{
	runtimeTraceWriter = writer;
	loadLib(libraryProfile);
	registerFunc();
}

Script::~Script()
{
	lua_close(luaState);
	luaState = nullptr;
}

int Script::runScript(std::unique_ptr<char[]> &s, int len)
{
	return runScriptWithChunkName(s.get(), len, name, nullptr);
}

int Script::runScriptWithChunkName(
	const char* source,
	int len,
	const std::string& chunkName,
	bool* chunkLoaded,
	ExactScriptExecutionResult* exactResult,
	bool* applicationQuitAborted)
{
	if (chunkLoaded != nullptr)
	{
		*chunkLoaded = false;
	}
	if (exactResult != nullptr)
	{
		*exactResult = {};
	}
	if (applicationQuitAborted != nullptr)
	{
		*applicationQuitAborted = false;
	}
	if (source != nullptr && len >= 0)
	{
		ScriptRunningHolder scriptRunningHolder(&running);
		const int initialStackTop = lua_gettop(luaState);
		int loadResult = luaL_loadbuffer(
			luaState, source, len, chunkName.c_str());
		if (loadResult != LUA_OK)
		{
			const char* error = lua_tostring(luaState, -1);
			const std::string errorMessage =
				error != nullptr ? error : "";
			GameLog::write(
				"Lua load error: %s\n",
				errorMessage.c_str());
			if (exactResult != nullptr)
			{
				populateExactLuaFailure(
					*exactResult,
					ExactScriptExecutionStatus::LoadFailed,
					errorMessage,
					chunkName,
					source,
					len);
			}
			lua_settop(luaState, initialStackTop);
			return loadResult;
		}
		if (chunkLoaded != nullptr)
		{
			*chunkLoaded = true;
		}
		int callResult = lua_pcall(luaState, 0, 0, 0);
		if (callResult != LUA_OK)
		{
			const bool applicationQuitAbort = lua_islightuserdata(luaState, -1) &&
				lua_touserdata(luaState, -1) == &ApplicationQuitScriptAbortToken &&
				Engine::getInstance()->isApplicationQuitRequested();
			if (!applicationQuitAbort)
			{
				const char* error = lua_tostring(luaState, -1);
				const std::string errorMessage =
					error != nullptr ? error : "";
				GameLog::write(
					"Lua runtime error: %s\n",
					errorMessage.c_str());
				if (exactResult != nullptr)
				{
					populateExactLuaFailure(
						*exactResult,
						ExactScriptExecutionStatus::RuntimeFailed,
						errorMessage,
						chunkName,
						source,
						len);
				}
			}
			else if (exactResult != nullptr)
			{
				exactResult->status =
					ExactScriptExecutionStatus::RuntimeFailed;
			}
			if (applicationQuitAbort &&
				applicationQuitAborted != nullptr)
			{
				*applicationQuitAborted = true;
			}
		}
		lua_settop(luaState, initialStackTop);
		return callResult;
	}
	if (exactResult != nullptr)
	{
		exactResult->status =
			ExactScriptExecutionStatus::LoadFailed;
		exactResult->message =
			"Lua source is empty or unavailable";
	}
	return -1;
}

ExactScriptExecutionResult Script::runExactResourceBytes(
	const std::vector<std::uint8_t>& source,
	const std::string& virtualPath)
{
	if (virtualPath.empty() || source.empty() ||
		source.size() > static_cast<std::size_t>(INT_MAX))
	{
		ExactScriptExecutionResult result;
		result.status = ExactScriptExecutionStatus::LoadFailed;
		result.message =
			"Lua source path or payload is invalid";
		return result;
	}

	bool chunkLoaded = false;
	ExactScriptExecutionResult exactResult;
	runScriptWithChunkName(
		reinterpret_cast<const char*>(source.data()),
		static_cast<int>(source.size()),
		"@" + virtualPath,
		&chunkLoaded,
		&exactResult);
	if (!chunkLoaded)
	{
		exactResult.status =
			ExactScriptExecutionStatus::LoadFailed;
	}
	return exactResult;
}

ExactScriptExecutionResult Script::runResolvedTraceScriptSource(
	ResolvedTraceScriptSource source,
	std::uint64_t capturedParentExecutionId,
	bool parentWasCaptured)
{
	ExactScriptExecutionResult result;
	if (source.identity.virtualPath.empty())
	{
		result.status = ExactScriptExecutionStatus::LoadFailed;
		result.message = "Lua source virtual path is unavailable";
		return result;
	}

	if (nextExecutionId == 0 ||
		nextExecutionId >
			EditorRun::RuntimeTraceMaximumExactJsonInteger)
	{
		result.status = ExactScriptExecutionStatus::LoadFailed;
		result.message = "Lua trace execution ID space is exhausted";
		return result;
	}

	const std::uint64_t executionId = nextExecutionId++;
	std::optional<std::uint64_t> parentExecutionId;
	if (parentWasCaptured)
	{
		if (capturedParentExecutionId > 0)
		{
			parentExecutionId = capturedParentExecutionId;
		}
	}
	else if (!executionStack.empty())
	{
		parentExecutionId = executionStack.back();
	}

	const std::string_view sourceBytes =
		source.bytes.empty()
			? std::string_view()
			: std::string_view(
				reinterpret_cast<const char*>(
					source.bytes.data()),
				source.bytes.size());
	source.identity.contentSha256 =
		EditorRun::runtimeTraceSha256Hex(sourceBytes);

	if (runtimeTraceWriter != nullptr)
	{
		EditorRun::RuntimeTraceScriptStartEvent start;
		start.executionId = executionId;
		start.parentExecutionId = parentExecutionId;
		start.source = source.identity;
		EditorRun::RuntimeTraceEvent event;
		event.payload = std::move(start);
		(void)runtimeTraceWriter->enqueue(std::move(event));
	}

	executionStack.push_back(executionId);
	bool chunkLoaded = false;
	bool applicationQuitAborted = false;
	const bool sourceLengthValid =
		source.bytes.size() <=
			static_cast<std::size_t>(INT_MAX);
	runScriptWithChunkName(
		!sourceLengthValid
			? nullptr
			: source.bytes.empty()
			? ""
			: reinterpret_cast<const char*>(
				source.bytes.data()),
		sourceLengthValid
			? static_cast<int>(source.bytes.size())
			: -1,
		"@" + source.identity.virtualPath,
		&chunkLoaded,
		&result,
		&applicationQuitAborted);
	executionStack.pop_back();

	if (!chunkLoaded)
	{
		result.status = ExactScriptExecutionStatus::LoadFailed;
	}
	EditorRun::RuntimeTraceScriptFinishStatus finishStatus =
		EditorRun::RuntimeTraceScriptFinishStatus::Completed;
	if (applicationQuitAborted)
	{
		finishStatus =
			EditorRun::RuntimeTraceScriptFinishStatus::Aborted;
	}
	else if (result.status ==
		ExactScriptExecutionStatus::LoadFailed)
	{
		finishStatus =
			EditorRun::RuntimeTraceScriptFinishStatus::LoadError;
	}
	else if (result.status ==
		ExactScriptExecutionStatus::RuntimeFailed)
	{
		finishStatus =
			EditorRun::RuntimeTraceScriptFinishStatus::RuntimeError;
	}
	if (runtimeTraceWriter != nullptr)
	{
		EditorRun::RuntimeTraceScriptFinishEvent finish;
		finish.executionId = executionId;
		finish.status = finishStatus;
		EditorRun::RuntimeTraceEvent event;
		event.payload = finish;
		(void)runtimeTraceWriter->enqueue(std::move(event));
	}
	return result;
}

std::uint64_t Script::currentExecutionId() const noexcept
{
	return executionStack.empty() ? 0 : executionStack.back();
}

int Script::runScript(const std::string & fileName)
{
	std::unique_ptr<char[]> s;
	int len = File::readFile(fileName, s);
	if (s != nullptr && len > 0)
	{
		name = fileName;
		int ret = runScript(s, len);
		return ret;
	}
	return -1;
}

void Script::registerFunc()
{
#ifdef regFunc
#undef regFunc
#endif // regFunc

//#define regFunc(func) lua_register(luaState, #func, lua_##func);
	std::string str;
#define regFunc(func) str = #func; registerLuaFunction(convert::lowerCase(str), lua_##func);
#define regAlias(name, func) str = name; registerLuaFunction(convert::lowerCase(str), lua_##func);

	regFunc(printf);

	regFunc(Assign);
	regFunc(GetVar);
	regFunc(Add);
	regFunc(Sub);

	regFunc(Talk);
	regFunc(ShowTalk);
	regFunc(Say);

	regFunc(FadeIn);
	regFunc(FadeOut);
	regFunc(SetFadeLum);
	regFunc(SetMainLum);
	regFunc(PlayMusic);
	regFunc(PlayRandomMusic);
	regFunc(StopMusic);
#ifdef PlaySound
#undef PlaySound
#endif
	regFunc(PlaySound);
	regFunc(StopSound);
	regFunc(RunScript);
	regFunc(RunParallelScript);
	regFunc(MoveScreen);
	regFunc(Sleep);
	regFunc(PlayMovie);
	regFunc(StopMovie);

	regFunc(LoadMap);
	regFunc(LoadGame);
	regFunc(GetCurrentMapPath);
	regFunc(SetMapPos);
	regFunc(SetMapTrap);
	regFunc(SaveMapTrap);
	regFunc(SetMapTime);
	regFunc(ChangeASFColor);
	regFunc(ChangeMapColor);

	regFunc(LoadObj);
	regFunc(SaveObj);
	regFunc(AddObj);
	regFunc(DelObj);
	regFunc(SetObjPos);
	regFunc(SetObjOfs);
	regFunc(SetObjKind);
	regFunc(SetObjScript);
	regFunc(RunObjScript);
	regFunc(RunObjRightScript);
	regFunc(InteractNearestObj);
	regAlias("InteractNearestObject", InteractNearestObj);
	regFunc(ClearBody);
	regFunc(OpenBox);
	regFunc(CloseBox);
	regFunc(GetObjState);

	regFunc(LoadNpc);
	regFunc(LoadOneNpc);
	regFunc(SaveNpc);
	regFunc(AddNpc);
	regFunc(DelNpc);
	regFunc(GetNpcPos);
	regFunc(SetNpcRes);
	regFunc(SetNpcScript);
	regFunc(SetNpcDeathScript);
	regFunc(SetAllNpcScript);
	regFunc(SetAllNpcDeathScript);
	regFunc(InteractNearestNpc);
	regAlias("InteractNearestNPC", InteractNearestNpc);
	regFunc(NpcGoto);
	regFunc(NpcGotoEx);
	regFunc(NpcGotoDir);
	regFunc(SetNpcDestination);
	regFunc(FollowNpc);
	regFunc(FollowPlayer);
	regFunc(EnableNpcAI);
	regFunc(DisableNpcAI);
	regFunc(SetNpcAIEnabled);
	regFunc(EnablePartnerCombat);
	regFunc(DisablePartnerCombat);
	regFunc(NpcAttack);
	regFunc(NpcUseMagic);
	regFunc(SetNpcPos);
	regFunc(SetNpcDir);
	regFunc(SetNpcKind);
	regFunc(SetNpcLevel);
	regFunc(SetNpcState);
	regFunc(SetNpcAction);
	regFunc(SetNpcRelation);
	regFunc(SetNpcActionType);
	regFunc(SetNpcActionFile);
	regFunc(NpcSpecialAction);
	regFunc(NpcSpecialActionEx);

	regFunc(ChangeLife);
	regFunc(ChangeMana);
	regFunc(ChangeThew);
	regFunc(GetNpcState);
	regFunc(AddNpcProperty);
	regFunc(AddKindValue);
	regFunc(SetMapNpcAttr);
	regFunc(SetNpcTalkContent);
	regFunc(TalkSelfTip);
	regFunc(SetAllNpcIsEnemy);
	regFunc(SetDropIni);
	regFunc(EnableDrop);
	regFunc(DisableDrop);
	regFunc(SetDropEnabled);
	regFunc(ChangeFlyIni);
	regFunc(ChangeFlyIni2);
	regFunc(AddFlyInis);
	regFunc(AddNpcMagic);
	regFunc(SetKeepAttack);
	regFunc(ShowSignalTip);
	regFunc(SetSignalTipHidden);

	regFunc(LoadPlayer);
	regFunc(SavePlayer);
	regFunc(SetPlayerPos);
	regFunc(SetPlayerDir);
	regFunc(SetPlayerScn);
	regFunc(SetPlayerLum);
	regFunc(SetLevelFile);
	regFunc(SetMagicLevel);
	regFunc(GetPlayerMagicLevel);
	regFunc(GetMagicState);
	regFunc(GetEffectState);
	regFunc(GetMapState);
	regFunc(GetLeechcraftDifference);
	regFunc(MoveMagic);
	regFunc(SetPlayerLevel);
	regFunc(SetPlayerState);
	regFunc(ToNonFightingState);
	regFunc(EnableRun);
	regFunc(DisableRun);
	regFunc(EnableJump);
	regFunc(DisableJump);
	regFunc(EnableFight);
	regFunc(DisableFight);
	regFunc(PlayerGoto);
	regFunc(PlayerGotoEx);
	regFunc(PlayerRunTo);
	regFunc(PlayerRunToEx);
	regFunc(PlayerJumpTo);
	regFunc(PlayerGotoDir);
	regFunc(SetWalkIsRun);
	regFunc(AddMoveSpeedPercent);
	regFunc(UseMagic);
	regFunc(PetrifyMillisecond);
	regFunc(PoisonMillisecond);
	regFunc(FrozenMillisecond);

	regFunc(AddLife);
	regFunc(AddLifeMax);
	regFunc(AddThew);
	regFunc(AddThewMax);
	regFunc(AddMana);
	regFunc(AddManaMax);
	regFunc(AddAttack);
	regFunc(AddDefend);
	regFunc(AddEvade);
	regFunc(AddExp);
	regFunc(AddMoney);
	regFunc(EquipGoods);
	regFunc(AddRandMoney);
	regFunc(AddGoods);
	regFunc(AddRandGoods);
	regFunc(AddMagic);
	regFunc(AddTalent);
	regFunc(AddOneMagic);
	regFunc(DelGoods);
	regFunc(DelGoodByName);
	regFunc(DelMagic);
	regFunc(AddMagicExp);
	regFunc(FullLife);
	regFunc(FullThew);
	regFunc(FullMana);
	regFunc(UpdateState);
	regFunc(SaveGoods);
	regFunc(LoadGoods);
	regFunc(SaveGoodsSnapshot);
	regFunc(LoadGoodsSnapshot);
	regFunc(ClearGoods);
	regFunc(ClearMagic);
	regFunc(GetGoodsNum);
	regFunc(GetGoodsNumByName);
	regFunc(GetGoodsState);
	regFunc(GetExp);
	regFunc(ClearAllVar);
	regFunc(CheckFreeGoodsSpace);
	regFunc(CheckFreeMagicSpace);
	regFunc(GetGoodsCountByFile);
	regFunc(GetGoodsCountByName);
	regFunc(HasGoodsFreeSpace);
	regFunc(HasMagicFreeSpace);
	regFunc(GetMagicLevel);
	regFunc(GetMoney);
	regFunc(GetPlayerExp);
	regFunc(GetPlayerStat);
	regFunc(GetPartnerIndex);
	regFunc(GetPlayerState);
	regFunc(IsEquipWeapon);
	regFunc(GetMoneyNum);
	regFunc(SetMoneyNum);
	regFunc(Gamble);
	regFunc(ShowGamble);
	regFunc(ShowDiceGame);
	regFunc(ShowFishGame);
	regFunc(ShowStealWin);
	regFunc(ShowGiveGoodsWin);

	regFunc(ShowMessage);
	regFunc(ShowSystemMsg);
	regFunc(Memo);
	regFunc(AddToMemo);
	regFunc(DelMemo);
	regFunc(ClearMemo);
	regFunc(BuyGoods);
	regFunc(BuyGoodsOnly);
	regFunc(SellGoods);
	regFunc(ReturnToTitle);
	regFunc(EnableInput);
	regFunc(DisableInput);
	regFunc(SetInputEnabled);
	regFunc(HideInterface);
	regFunc(SetInterfaceVisible);
	regFunc(HideBottomWnd);
	regFunc(ShowBottomWnd);
	regFunc(HideMouseCursor);
	regFunc(ShowMouseCursor);
	regFunc(ShowSnow);
	regFunc(ShowRandomSnow);
	regFunc(ShowRain);
	regFunc(BeginRain);
	regFunc(EndRain);

	regFunc(CheckYear);
	regFunc(GetRandNum);
	regFunc(RandRun);
	regFunc(GetPlayerLevel);
	regFunc(GetNpcCount);
	regFunc(DelCurObj);
	regFunc(ShowInterface);
	regFunc(DrawBackground);
	regFunc(ClearEffect);
	regFunc(SaveGame);
	regFunc(ClearAllSave);
	regFunc(EnableSave);
	regFunc(DisableSave);
	regFunc(SavePlayerSnapshot);
	regFunc(LoadPlayerSnapshot);
	regFunc(SetSaveEnabled);
	regFunc(SetRunEnabled);
	regFunc(SetJumpEnabled);
	regFunc(SetFightEnabled);
	regFunc(LimitMana);
	regFunc(ShowNpc);
	regFunc(OpenWaterEffect);
	regFunc(CloseWaterEffect);
	regFunc(Watch);
	regFunc(SetTrap);
	regFunc(GetObjPos);
	regFunc(SetNpcMagicFile);
	regFunc(SetNpcMagicLevel);
	regFunc(SetPlayerMagicToUseWhenBeAttacked);
	regFunc(SetNpcMagicToUseWhenBeAttacked);
	regFunc(SetNpcClickScript);
	regFunc(SetNpcPartner);
	regFunc(SetPartnerLevel);
	regFunc(PlayerAddEmotion);
	regFunc(PlayerAddJustice);
	regFunc(GetPartnerIdx);
	regFunc(MoveScreenEx);
	regFunc(DisplayMessage);
	regFunc(DisableMapScroll);
	regFunc(EnableMapScroll);
	regFunc(SetShowMapPos);
	regFunc(OpenObj);
	regFunc(FreeMap);
	regFunc(OpenTimeLimit);
	regFunc(CloseTimeLimit);
	regFunc(HideTimerWnd);
	regFunc(SetTimeScript);
	regFunc(Choose);
	regFunc(ChooseEx);
	regFunc(ChooseMultiple);
	regFunc(ChoosePlus);
	regFunc(Select);
	regFunc(PlayerChange);
	regFunc(MergeNpc);

	regAlias("MessageBox", DisplayMessage);
	regAlias("Message", DisplayMessage);
	regAlias("ShowSystemMessage", ShowSystemMsg);
	regAlias("PlayGoto", PlayerGoto);
	regAlias("PlayerWalkTo", PlayerGoto);
	regAlias("PlayerWalkToDir", PlayerGotoDir);
	regAlias("PlayerWalkToNonBlocking", PlayerGotoEx);
	regAlias("PlayerRunToNonBlocking", PlayerRunToEx);
	regAlias("SetPlayrDir", SetPlayerDir);
	regAlias("CenterCamera", SetPlayerScn);
	regAlias("RunScirpt", RunScript);
	regAlias("LodaObj", LoadObj);
	regAlias("DeleteCurrentObj", DelCurObj);
	regAlias("DeleteObj", DelObj);
	regAlias("SetObjOffset", SetObjOfs);
	regAlias("NpcAction", SetNpcAction);
	regAlias("DeleteNpc", DelNpc);
	regAlias("LoadMapNpc", LoadNpc);
	regAlias("SaveTrap", SaveMapTrap);
	regAlias("NpcWalkTo", NpcGoto);
	regAlias("NpcWalkToDir", NpcGotoDir);
	regAlias("NpcWalkToNonBlocking", NpcGotoEx);
	regAlias("NpcSpecialActionNonBlocking", NpcSpecialActionNonBlocking);
	regAlias("NpcWatch", Watch);
	regAlias("NpcFollow", FollowNpc);
	regAlias("NpcFollowPlayer", FollowPlayer);
	regAlias("SetNpcResource", SetNpcRes);
	regAlias("SetNpcMagicWhenAttacked", SetNpcMagicToUseWhenBeAttacked);
	regAlias("SetNpcMagicToUseWhenBeatacked", SetNpcMagicToUseWhenBeAttacked);
	regAlias("ChangeNpcLife", ChangeLife);
	regAlias("ChangeNpcMana", ChangeMana);
	regAlias("ChangeNpcThew", ChangeThew);
	regAlias("ChangeNpcFlyIni", ChangeFlyIni);
	regAlias("ChangeNpcFlyIni2", ChangeFlyIni2);
	regAlias("AddNpcFlyInis", AddFlyInis);
	regAlias("SetNpcKeepAttack", SetKeepAttack);
	regAlias("GetGoodsMun", GetGoodsNum);
	regAlias("RemoveGoods", DelGoods);
	regAlias("DeleteGoodsByName", DelGoodByName);
	regAlias("AddRandomGoods", AddRandGoods);
	regAlias("DeleteMagic", DelMagic);
	regAlias("AddOneMogic", AddOneMagic);
	regAlias("SetPlayerMagicWhenAttacked", SetPlayerMagicToUseWhenBeAttacked);
	regAlias("SetPlayerMagicToUseWhenBeatacked", SetPlayerMagicToUseWhenBeAttacked);
	regAlias("Petrify", PetrifyMillisecond);
	regAlias("Poison", PoisonMillisecond);
	regAlias("Frozen", FrozenMillisecond);
	regAlias("SetMoney", SetMoneyNum);
	regAlias("SetVar", Assign);
	regAlias("Assing", Assign);
	regAlias("ClearAllVars", ClearAllVar);
	regAlias("ClearAllSaves", ClearAllSave);
	regAlias("OpenTimer", OpenTimeLimit);
	regAlias("CloseTimer", CloseTimeLimit);
	regAlias("HideTimer", HideTimerWnd);
	regAlias("SetTimerScript", SetTimeScript);
	regAlias("AddMemo", AddToMemo);
	regAlias("AddMemoById", AddToMemo);
	regAlias("DeleteMemo", DelMemo);
	regAlias("DeleteMemoById", DelMemo);
	regAlias("CameraMove", MoveScreen);
	regAlias("CameraMoveTo", MoveScreenEx);
	regAlias("SetCameraPos", SetMapPos);
	regAlias("ChangeSpriteColor", ChangeASFColor);
	regAlias("EnabelDrop", EnableDrop);
	regAlias("HideBottomWindow", HideBottomWnd);
	regAlias("ShowBottomWindow", ShowBottomWnd);

#undef regAlias
#undef regFunc
}

void Script::registerLuaFunction(const std::string& functionName, lua_CFunction function)
{
	lua_pushcfunction(luaState, function);
	lua_pushlstring(
		luaState,
		functionName.data(),
		functionName.size());
	lua_pushcclosure(luaState, callRegisteredLuaFunction, 2);
	lua_setglobal(luaState, functionName.c_str());
}

int Script::callRegisteredLuaFunction(lua_State* luaState)
{
	Script* script =
		*static_cast<Script**>(
			lua_getextraspace(luaState));
	std::size_t apiNameLength = 0;
	const char* apiName = lua_tolstring(
		luaState,
		lua_upvalueindex(2),
		&apiNameLength);
	if (script != nullptr && apiName != nullptr)
	{
		script->enqueueApiCall(
			std::string_view(apiName, apiNameLength));
	}
	const int argumentCount = lua_gettop(luaState);
	lua_pushvalue(luaState, lua_upvalueindex(1));
	lua_insert(luaState, 1);
	lua_call(luaState, argumentCount, LUA_MULTRET);
	const int resultCount = lua_gettop(luaState);
	if (Engine::getInstance()->isApplicationQuitRequested())
	{
		lua_pushlightuserdata(luaState, &ApplicationQuitScriptAbortToken);
		return lua_error(luaState);
	}
	return resultCount;
}

void Script::runtimeTraceLuaHook(
	lua_State* luaState,
	lua_Debug* debug)
{
	if (Engine::getInstance()->isApplicationQuitRequested())
	{
		lua_pushlightuserdata(
			luaState,
			&ApplicationQuitScriptAbortToken);
		lua_error(luaState);
		return;
	}
	if (debug == nullptr ||
		debug->event != LUA_HOOKLINE ||
		debug->currentline <= 0)
	{
		return;
	}
	Script* script =
		*static_cast<Script**>(
			lua_getextraspace(luaState));
	if (script != nullptr)
	{
		script->enqueueSourceLine(
			static_cast<std::uint64_t>(
				debug->currentline));
	}
}

void Script::enqueueSourceLine(std::uint64_t line)
{
	const std::uint64_t executionId =
		currentExecutionId();
	if (runtimeTraceWriter == nullptr ||
		executionId == 0 ||
		line == 0)
	{
		return;
	}
	EditorRun::RuntimeTraceSourceLineEvent sourceLine;
	sourceLine.executionId = executionId;
	sourceLine.line = line;
	EditorRun::RuntimeTraceEvent event;
	event.payload = sourceLine;
	(void)runtimeTraceWriter->enqueue(std::move(event));
}

// Lua reports errors with a long jump. Keep all trace-owned strings and
// variants outside the C++ callback frame that invokes lua_call.
JXQY_SCRIPT_NOINLINE void Script::enqueueApiCall(
	std::string_view apiName)
{
	const std::uint64_t executionId =
		currentExecutionId();
	if (runtimeTraceWriter == nullptr ||
		executionId == 0 ||
		apiName.empty())
	{
		return;
	}
	EditorRun::RuntimeTraceApiCallEvent apiCall;
	apiCall.executionId = executionId;
	apiCall.apiName.assign(apiName.data(), apiName.size());
	EditorRun::RuntimeTraceEvent event;
	event.payload = std::move(apiCall);
	(void)runtimeTraceWriter->enqueue(std::move(event));
}

#undef JXQY_SCRIPT_NOINLINE

int Script::lua_printf(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		GameLog::write("lua_printf: %s\n", getLuaString(l, 1).c_str());
	}
	else
	{
		GameLog::write("lua_printf: null str\n");
	}
	return 0;
}

int Script::lua_RunScript(lua_State * l)
{
	const int argc = lua_gettop(l);
	GameManager* const gameManager = gm;
	if (argc >= 1 && gameManager != nullptr)
	{
		const int childResult =
			runLuaChildScript(l, gameManager);
		if (childResult != 0 &&
			!gameManager->getLastLoadFailureMessage().empty())
		{
			const std::string& reason =
				gameManager->getLastLoadFailureMessage();
			lua_pushliteral(l, "runscript failed: ");
			lua_pushlstring(
				l,
				reason.data(),
				reason.size());
			lua_concat(l, 2);
			return lua_error(l);
		}
	}
	return 0;
}

int Script::lua_RunParallelScript(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		int delayMilliseconds = argc >= 2 ? (int)lua_tointeger(l, 2) : 0;
		gm->scriptAPI.runParallelScript(getLuaString(l, 1), delayMilliseconds);
	}
	return 0;
}

int Script::lua_MoveScreen(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc < 2)
	{
		return 0;
	}

	const int direction = (int)lua_tointeger(l, 1);
	const int amount = (int)lua_tointeger(l, 2);
	if (resolveMoveScreenArgumentMode(argc) ==
		MoveScreenArgumentMode::FrameCountAndSpeed)
	{
		gm->scriptAPI.moveScreenForFrameCount(
			direction,
			amount,
			(int)lua_tointeger(l, 3));
	}
	else
	{
		gm->scriptAPI.moveScreen(direction, amount, 1);
	}
	return 0;
}

int Script::lua_Sleep(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.sleep((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_PlayMovie(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.playMovie(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_StopMovie(lua_State * l)
{
	gm->scriptAPI.stopMovie();
	return 0;
}

int Script::lua_LoadMap(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		if (Config::loadAsync)
		{
			gm->scriptAPI.loadMapAsync(getLuaString(l, 1));
		}
		else
		{
			gm->scriptAPI.loadMap(getLuaString(l, 1));
		}
	}
	return 0;
}

int Script::lua_LoadGame(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		GameManager* const gameManager = gm;
		if (gameManager == nullptr)
		{
			lua_pushliteral(
				l,
				"loadgame failed: game manager is unavailable");
			return lua_error(l);
		}
		bool loaded = false;
		if (Config::loadAsync)
		{
			loaded = gameManager->scriptAPI.loadGameAsync(
				(int)lua_tointeger(l, 1));
		}
		else
		{
			loaded = gameManager->scriptAPI.loadGame(
				(int)lua_tointeger(l, 1));
		}
		if (!loaded)
		{
			const std::string& reason =
				gameManager->getLastLoadFailureMessage();
			lua_pushliteral(l, "loadgame failed: ");
			if (reason.empty())
			{
				lua_pushliteral(l, "unknown reason");
			}
			else
			{
				lua_pushlstring(
					l,
					reason.data(),
					reason.size());
			}
			lua_concat(l, 2);
			return lua_error(l);
		}
		return 0;
	}
	return 0;
}

int Script::lua_GetCurrentMapPath(lua_State* l)
{
	lua_pushstring(l, gm->global.data.mapName.c_str());
	return 1;
}

int Script::lua_SetMapPos(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.setMapPos((int)lua_tointeger(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_SetMapTrap(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.setMapTrap((int)lua_tointeger(l, 1), getLuaString(l, 2));
	}
	return 0;
}

int Script::lua_SaveMapTrap(lua_State * l)
{
	gm->scriptAPI.saveMapTrap();
	return 0;
}

int Script::lua_SetMapTime(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.setMapTime((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_ChangeASFColor(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.changeASFColor((uint8_t)lua_tointeger(l, 1), (uint8_t)lua_tointeger(l, 2), (uint8_t)lua_tointeger(l, 3));
	}
	return 0;
}

int Script::lua_ChangeMapColor(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.changeMapColor((uint8_t)lua_tointeger(l, 1), (uint8_t)lua_tointeger(l, 2), (uint8_t)lua_tointeger(l, 3));
	}
	return 0;
}

int Script::lua_LoadObj(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		if (Config::loadAsync)
		{
			gm->scriptAPI.loadObjectAsync(getLuaString(l, 1));
		}
		else
		{
			gm->scriptAPI.loadObject(getLuaString(l, 1));
		}
	}
	return 0;
}

int Script::lua_SaveObj(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.saveObject(getLuaString(l, 1));
	}
	else
	{
		gm->scriptAPI.saveObject();
	}
	return 0;
}

int Script::lua_AddObj(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 4)
	{
		gm->scriptAPI.addObject(getLuaString(l, 1), (int)lua_tointeger(l, 2), (int)lua_tointeger(l, 3), (int)lua_tointeger(l, 4));
	}
	else if(argc >= 3)
	{
		gm->scriptAPI.addObject(getLuaString(l, 1), (int)lua_tointeger(l, 2), (int)lua_tointeger(l, 3), 0);
	}
	return 0;
}

int Script::lua_DelObj(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.deleteObject(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_SetObjPos(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.setObjectPosition(getLuaString(l, 1), (int)lua_tointeger(l, 2), (int)lua_tointeger(l, 3));
	}
	return 0;
}

int Script::lua_GetObjPos(lua_State* l)
{
	int argc = lua_gettop(l);
	std::shared_ptr<Object> object = nullptr;
	if (argc >= 1 && gm->objectManager != nullptr)
	{
		std::string name = getLuaString(l, 1);
		object = name.empty() ? gm->scriptObj : gm->objectManager->findObj(name);
	}
	Point position = object != nullptr ? object->getPosition() : Point{ 0, 0 };
	lua_pushinteger(l, position.x);
	lua_pushinteger(l, position.y);
	return 2;
}

int Script::lua_SetObjOfs(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.setObjectOffset(getLuaString(l, 1), (int)lua_tointeger(l, 2), (int)lua_tointeger(l, 3));
	}
	else if (argc >= 2)
	{
		gm->scriptAPI.setObjectOffset("", (int)lua_tointeger(l, 1), (int)lua_tointeger(l, 2));
	}
	else
	{
		gm->scriptAPI.setObjectOffset("", 0, 0);
	}
	return 0;
}

int Script::lua_SetObjKind(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.setObjectKind(getLuaString(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_SetObjScript(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.setObjectScript(getLuaString(l, 1), getLuaString(l, 2), getLuaString(l, 3));
	}
	else if (argc >= 2)
	{
		gm->scriptAPI.setObjectScript(getLuaString(l, 1), getLuaString(l, 2));
	}
	return 0;
}

int Script::lua_RunObjScript(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.runObjectScript(getLuaString(l, 1), lua_tointeger(l, 2) != 0, false);
	}
	else if (argc >= 1)
	{
		gm->scriptAPI.runObjectScript(getLuaString(l, 1), false, true);
	}
	return 0;
}

int Script::lua_RunObjRightScript(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.runObjectScript(getLuaString(l, 1), true, false);
	}
	return 0;
}

int Script::lua_InteractNearestObj(lua_State* l)
{
	int argc = lua_gettop(l);
	bool useRightScript = argc >= 1 && lua_tointeger(l, 1) != 0;
	bool running = argc >= 2 && lua_tointeger(l, 2) != 0;
	int radius = argc >= 3 ? (int)lua_tointeger(l, 3) : 2;
	bool queued = gm->scriptAPI.interactNearestObject(useRightScript, running, radius);
	lua_pushinteger(l, queued ? 1 : 0);
	return 1;
}

int Script::lua_ClearBody(lua_State * l)
{
	gm->scriptAPI.clearBody();
	return 0;
}

int Script::lua_OpenBox(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.openBox(getLuaString(l, 1));
	}
	else
	{
		gm->scriptAPI.openBox();
	}
	return 0;
}

int Script::lua_CloseBox(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.closeBox(getLuaString(l, 1));
	}
	else
	{
		gm->scriptAPI.closeBox();
	}
	return 0;
}

int Script::lua_GetObjState(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.getObjectState(getLuaString(l, 1), getLuaString(l, 2), getLuaString(l, 3));
	}
	return 0;
}

int Script::lua_LoadNpc(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		// A script-level NPC list replacement is an immediate world mutation.
		// Keep it on the main thread so the following script statement observes
		// the committed list without presenting the exclusive loading screen.
		gm->scriptAPI.loadNPC(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_LoadOneNpc(lua_State* l)
{
	int argc = lua_gettop(l);
	std::vector<std::string> fileNames;
	fileNames.reserve(argc);
	for (int i = 1; i <= argc; i++)
	{
		fileNames.push_back(getLuaString(l, i));
	}
	gm->scriptAPI.loadOneNpc(fileNames);
	return 0;
}

int Script::lua_SaveNpc(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.saveNPC(getLuaString(l, 1));
	}
	else
	{
		gm->scriptAPI.saveNPC();
	}
	return 0;
}

int Script::lua_AddNpc(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 4)
	{
		gm->scriptAPI.addNPC(getLuaString(l, 1), (int)lua_tointeger(l, 2), (int)lua_tointeger(l, 3), (int)lua_tointeger(l, 4));
	}
	else if (argc >= 3)
	{
		gm->scriptAPI.addNPC(getLuaString(l, 1), (int)lua_tointeger(l, 2), (int)lua_tointeger(l, 3), 0);
	}
	return 0;
}

int Script::lua_DelNpc(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.deleteNPC(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_GetNpcPos(lua_State* l)
{
	int argc = lua_gettop(l);
	std::shared_ptr<NPC> npc = nullptr;
	if (argc >= 1 && gm->npcManager != nullptr)
	{
		std::string name = getLuaString(l, 1);
		if (name.empty())
		{
			npc = gm->player;
		}
		else
		{
			auto npcList = gm->npcManager->findNPC(name);
			for (const auto& item : npcList)
			{
				if (item != nullptr)
				{
					npc = item;
					break;
				}
			}
		}
	}
	Point position = npc != nullptr ? npc->getPosition() : Point{ 0, 0 };
	lua_pushinteger(l, position.x);
	lua_pushinteger(l, position.y);
	return 2;
}

int Script::lua_SetNpcRes(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.setNPCRes(getLuaString(l, 1), getLuaString(l, 2));
	}
	else if (argc >= 1)
	{
		gm->scriptAPI.setNPCRes("", getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_SetNpcScript(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.setNPCScript(getLuaString(l, 1), getLuaString(l, 2), getLuaString(l, 3));
	}
	else if (argc >= 2)
	{
		gm->scriptAPI.setNPCScript(getLuaString(l, 1), getLuaString(l, 2));
	}
	return 0;
}

int Script::lua_SetNpcDeathScript(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.setNPCDeathScript(getLuaString(l, 1), getLuaString(l, 2), getLuaString(l, 3));
	}
	else if (argc >= 2)
	{
		gm->scriptAPI.setNPCDeathScript(getLuaString(l, 1), getLuaString(l, 2));
	}
	return 0;
}

int Script::lua_SetAllNpcScript(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.setAllNPCScript(getLuaString(l, 1), getLuaString(l, 2));
	}
	return 0;
}

int Script::lua_SetAllNpcDeathScript(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.setAllNPCDeathScript(getLuaString(l, 1), getLuaString(l, 2));
	}
	return 0;
}

int Script::lua_InteractNearestNpc(lua_State* l)
{
	int argc = lua_gettop(l);
	bool useRightScript = argc >= 1 && lua_tointeger(l, 1) != 0;
	bool running = argc >= 2 && lua_tointeger(l, 2) != 0;
	int radius = argc >= 3 ? (int)lua_tointeger(l, 3) : 2;
	bool queued = gm->scriptAPI.interactNearestNPC(useRightScript, running, radius);
	lua_pushinteger(l, queued ? 1 : 0);
	return 1;
}

int Script::lua_NpcGoto(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.goTo(getLuaString(l, 1), (int)lua_tointeger(l, 2), (int)lua_tointeger(l, 3));
	}
	else if (argc >= 2)
	{
		gm->scriptAPI.goTo("", (int)lua_tointeger(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_NpcGotoEx(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.goToEx(getLuaString(l, 1), (int)lua_tointeger(l, 2), (int)lua_tointeger(l, 3));
	}
	else if (argc >= 2)
	{
		gm->scriptAPI.goToEx("", (int)lua_tointeger(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_NpcGotoDir(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.goToDir(getLuaString(l, 1), (int)lua_tointeger(l, 2), (int)lua_tointeger(l, 3));
	}
	else if (argc >= 2)
	{
		gm->scriptAPI.goToDir("", (int)lua_tointeger(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_SetNpcDestination(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.setNpcDestination(getLuaString(l, 1), (int)lua_tointeger(l, 2), (int)lua_tointeger(l, 3));
	}
	return 0;
}

int Script::lua_FollowNpc(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.followNPC(getLuaString(l, 1), getLuaString(l, 2));
	}
	else if (argc >= 1)
	{
		gm->scriptAPI.followNPC("", getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_FollowPlayer(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.followPlayer(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_EnableNpcAI(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1 && !lua_isnil(l, 1))
	{
		gm->scriptAPI.setNpcAIEnabled(getLuaString(l, 1), true);
	}
	else
	{
		gm->scriptAPI.enableNPCAI();
	}
	return 0;
}

int Script::lua_DisableNpcAI(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1 && !lua_isnil(l, 1))
	{
		gm->scriptAPI.setNpcAIEnabled(getLuaString(l, 1), false);
	}
	else
	{
		gm->scriptAPI.disableNPCAI();
	}
	return 0;
}

int Script::lua_SetNpcAIEnabled(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.setNpcAIEnabled(getLuaString(l, 1), getLuaBoolean(l, 2));
	}
	else if (getLuaBoolean(l, 1))
	{
		gm->scriptAPI.enableNPCAI();
	}
	else
	{
		gm->scriptAPI.disableNPCAI();
	}
	return 0;
}

int Script::lua_EnablePartnerCombat(lua_State* l)
{
	gm->scriptAPI.enablePartnerCombat();
	return 0;
}

int Script::lua_DisablePartnerCombat(lua_State* l)
{
	gm->scriptAPI.disablePartnerCombat();
	return 0;
}

int Script::lua_NpcAttack(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.attackTo(getLuaString(l, 1), (int)lua_tointeger(l, 2), (int)lua_tointeger(l, 3));
	}
	return 0;
}

int Script::lua_NpcUseMagic(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 4)
	{
		int level = argc >= 5 ? (int)lua_tointeger(l, 5) : 1;
		gm->scriptAPI.npcUseMagic(getLuaString(l, 1), getLuaString(l, 2), (int)lua_tointeger(l, 3), (int)lua_tointeger(l, 4), level);
	}
	return 0;
}

int Script::lua_SetNpcPos(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.setNPCPosition(getLuaString(l, 1), (int)lua_tointeger(l, 2), (int)lua_tointeger(l, 3));
	}
	else if (argc >= 2)
	{
		gm->scriptAPI.setNPCPosition("", (int)lua_tointeger(l, 1), (int)lua_tointeger(l, 2));
	}
	else
	{
		gm->scriptAPI.setNPCPosition("", 0, 0);
	}
	return 0;
}

int Script::lua_SetNpcDir(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.setNPCDir(getLuaString(l, 1), (int)lua_tointeger(l, 2));
	}
	else if (argc >= 1)
	{
		gm->scriptAPI.setNPCDir("", (int)lua_tointeger(l, 1));
	}
	else
	{
		gm->scriptAPI.setNPCDir("", 0);
	}
	return 0;
}

int Script::lua_SetNpcKind(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.setNPCKind(getLuaString(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_SetNpcLevel(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.setNPCLevel(getLuaString(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_SetNpcState(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2 && gm->npcManager != nullptr)
	{
		auto npcList = gm->npcManager->findNPC(getLuaString(l, 1));
		if (!npcList.empty() && npcList.front() != nullptr)
		{
			npcList.front()->state = (int)lua_tointeger(l, 2);
		}
	}
	return 0;
}

int Script::lua_SetNpcAction(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		int x = argc >= 4 ? static_cast<int>(lua_tointeger(l, 3)) : 0;
		int y = argc >= 4 ? static_cast<int>(lua_tointeger(l, 4)) : 0;
		gm->scriptAPI.setNPCAction(getLuaString(l, 1), static_cast<int>(lua_tointeger(l, 2)), x, y);
	}
	return 0;
}

int Script::lua_SetNpcRelation(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.setNPCRelation(getLuaString(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_SetNpcActionType(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.setNPCActionType(getLuaString(l, 1), (int)lua_tointeger(l, 2));
	}
	else if (argc >= 1)
	{
		gm->scriptAPI.setNPCActionType("", (int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_SetNpcActionFile(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.setNPCActionFile(getLuaString(l, 1), (int)lua_tointeger(l, 2), getLuaString(l, 3));
	}
	return 0;
}

int Script::lua_NpcSpecialAction(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.npcSpecialAction(getLuaString(l, 1), getLuaString(l, 2));
	}
	else if (argc >= 1)
	{
		gm->scriptAPI.npcSpecialAction("", getLuaString(l, 1));
	}
	else
	{
		gm->scriptAPI.npcSpecialAction("", "");
	}
	return 0;
}

int Script::lua_NpcSpecialActionNonBlocking(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.npcSpecialActionNonBlocking(getLuaString(l, 1), getLuaString(l, 2));
	}
	else if (argc >= 1)
	{
		gm->scriptAPI.npcSpecialActionNonBlocking("", getLuaString(l, 1));
	}
	else
	{
		gm->scriptAPI.npcSpecialActionNonBlocking("", "");
	}
	return 0;
}

int Script::lua_NpcSpecialActionEx(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.npcSpecialActionEx(getLuaString(l, 1), getLuaString(l, 2));
	}
	else if (argc >= 1)
	{
		gm->scriptAPI.npcSpecialActionEx("", getLuaString(l, 1));
	}
	else
	{
		gm->scriptAPI.npcSpecialActionEx("", "");
	}
	return 0;
}

int Script::lua_ChangeLife(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.changeLife(getLuaString(l, 1), lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_ChangeMana(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.changeMana(getLuaString(l, 1), lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_ChangeThew(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.changeThew(getLuaString(l, 1), lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_GetNpcState(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.getNpcState(getLuaString(l, 1), getLuaString(l, 2), getLuaString(l, 3));
	}
	return 0;
}

int Script::lua_AddNpcProperty(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.addNpcProperty(getLuaString(l, 1), getLuaString(l, 2), (int)lua_tointeger(l, 3));
	}
	return 0;
}

int Script::lua_AddKindValue(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.addKindValue(getLuaString(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_SetMapNpcAttr(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.setMapNpcAttr(getLuaString(l, 1), getLuaString(l, 2), getLuaString(l, 3));
	}
	return 0;
}

int Script::lua_SetNpcTalkContent(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.setNpcTalkContent(getLuaString(l, 1), getLuaString(l, 2), getLuaString(l, 3));
	}
	else if (argc >= 2)
	{
		gm->scriptAPI.setNpcTalkContent(getLuaString(l, 1), getLuaString(l, 2));
	}
	return 0;
}

int Script::lua_TalkSelfTip(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.talkSelfTip(getLuaString(l, 1), getLuaString(l, 2), getLuaString(l, 3));
	}
	else if (argc >= 2)
	{
		gm->scriptAPI.talkSelfTip(getLuaString(l, 1), getLuaString(l, 2));
	}
	return 0;
}

int Script::lua_SetAllNpcIsEnemy(lua_State* l)
{
	gm->scriptAPI.setAllNpcIsEnemy();
	return 0;
}

int Script::lua_SetDropIni(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.setDropIni(getLuaString(l, 1), getLuaString(l, 2));
	}
	return 0;
}

int Script::lua_EnableDrop(lua_State* l)
{
	gm->scriptAPI.enableDrop();
	return 0;
}

int Script::lua_DisableDrop(lua_State* l)
{
	gm->scriptAPI.disableDrop();
	return 0;
}

int Script::lua_SetDropEnabled(lua_State* l)
{
	if (getLuaBoolean(l, 1))
	{
		gm->scriptAPI.enableDrop();
	}
	else
	{
		gm->scriptAPI.disableDrop();
	}
	return 0;
}

int Script::lua_ChangeFlyIni(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.changeFlyIni(getLuaString(l, 1), getLuaString(l, 2));
	}
	return 0;
}

int Script::lua_ChangeFlyIni2(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.changeFlyIni2(getLuaString(l, 1), getLuaString(l, 2));
	}
	return 0;
}

int Script::lua_AddFlyInis(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.addFlyInis(getLuaString(l, 1), getLuaString(l, 2), (int)lua_tointeger(l, 3));
	}
	return 0;
}

int Script::lua_AddNpcMagic(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.addNpcMagic(getLuaString(l, 1), getLuaString(l, 2));
	}
	return 0;
}

int Script::lua_SetKeepAttack(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.setKeepAttack(getLuaString(l, 1), (int)lua_tointeger(l, 2), (int)lua_tointeger(l, 3));
	}
	return 0;
}

int Script::lua_ShowSignalTip(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.showSignalTip(getLuaString(l, 1), (int)lua_tointeger(l, 2), getLuaString(l, 3));
	}
	return 0;
}

int Script::lua_SetSignalTipHidden(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.setSignalTipHidden(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_LoadPlayer(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.loadPlayer((int)lua_tointeger(l, 1));
	}
	else
	{
		gm->scriptAPI.loadPlayer(-1);
	}
	return 0;
}

int Script::lua_SavePlayer(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.savePlayer((int)lua_tointeger(l, 1));
	}
	else
	{
		gm->scriptAPI.savePlayer(-1);
	}
	return 0;
}

int Script::lua_SetPlayerPos(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.setPlayerPosition(
			getLuaString(l, 1),
			(int)lua_tointeger(l, 2),
			(int)lua_tointeger(l, 3));
	}
	else if (argc >= 2)
	{
		gm->scriptAPI.setPlayerPosition((int)lua_tointeger(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_SetPlayerDir(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.setPlayerDir((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_SetPlayerScn(lua_State * l)
{
	gm->scriptAPI.setPlayerScn();
	return 0;
}

int Script::lua_SetPlayerLum(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.setPlayerLum((unsigned char)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_SetLevelFile(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.setLevelFile(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_SetMagicLevel(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.setMagicLevel(getLuaString(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_GetPlayerMagicLevel(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.getPlayerMagicLevel(getLuaString(l, 1), getLuaString(l, 2));
	}
	return 0;
}

int Script::lua_GetMagicState(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		int level = argc >= 4 ? (int)lua_tointeger(l, 4) : 0;
		gm->scriptAPI.getMagicState(getLuaString(l, 1), getLuaString(l, 2), getLuaString(l, 3), level);
	}
	return 0;
}

int Script::lua_GetEffectState(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.getEffectState(getLuaString(l, 1), getLuaString(l, 2), getLuaString(l, 3));
	}
	return 0;
}

int Script::lua_GetMapState(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 4)
	{
		gm->scriptAPI.getMapState((int)lua_tointeger(l, 1), (int)lua_tointeger(l, 2), getLuaString(l, 3), getLuaString(l, 4));
	}
	return 0;
}

int Script::lua_GetLeechcraftDifference(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.getLeechcraftDifference(getLuaString(l, 1), getLuaString(l, 2));
	}
	return 0;
}

int Script::lua_MoveMagic(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.moveMagic(getLuaString(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_SetPlayerLevel(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.setPlayerLevel((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_SetPlayerState(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.setPlayerState((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_ToNonFightingState(lua_State* l)
{
	if (gm->player != nullptr)
	{
		gm->player->fightState.set(false);
		auto controlled = gm->player->getControlledCharacter();
		if (controlled != nullptr)
		{
			controlled->fightState.set(false);
		}
	}
	return 0;
}

int Script::lua_EnableRun(lua_State * l)
{
	gm->scriptAPI.enableRun();
	return 0;
}

int Script::lua_DisableRun(lua_State * l)
{
	gm->scriptAPI.disableRun();
	return 0;
}

int Script::lua_EnableJump(lua_State * l)
{
	gm->scriptAPI.enableJump();
	return 0;
}

int Script::lua_DisableJump(lua_State * l)
{
	gm->scriptAPI.disableJump();
	return 0;
}

int Script::lua_EnableFight(lua_State * l)
{
	gm->scriptAPI.enableFight();
	return 0;
}

int Script::lua_DisableFight(lua_State * l)
{
	gm->scriptAPI.disableFight();
	return 0;
}

int Script::lua_PlayerGoto(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.playerGoto((int)lua_tointeger(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_PlayerGotoEx(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.playerGotoEx((int)lua_tointeger(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_PlayerRunTo(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.playerRunTo((int)lua_tointeger(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_PlayerRunToEx(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.playerRunToEx((int)lua_tointeger(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_PlayerJumpTo(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.playerJumpTo((int)lua_tointeger(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_PlayerGotoDir(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.playerGotoDir((int)lua_tointeger(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_SetWalkIsRun(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.setWalkIsRun((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_AddMoveSpeedPercent(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.addMoveSpeedPercent((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_UseMagic(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.useMagic(getLuaString(l, 1), (int)lua_tointeger(l, 2), (int)lua_tointeger(l, 3), true);
	}
	else if (argc >= 1)
	{
		gm->scriptAPI.useMagic(getLuaString(l, 1), 0, 0, false);
	}
	return 0;
}

int Script::lua_PetrifyMillisecond(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.petrifyMillisecond((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_PoisonMillisecond(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.poisonMillisecond((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_FrozenMillisecond(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.frozenMillisecond((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_AddLife(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.addLife((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_AddLifeMax(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.addLifeMax((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_AddThew(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.addThew((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_AddThewMax(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.addThewMax((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_AddMana(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.addMana((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_AddManaMax(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.addManaMax((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_AddAttack(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		const int type = argc >= 2 ? (int)lua_tointeger(l, 2) : 1;
		gm->scriptAPI.addAttack((int)lua_tointeger(l, 1), type);
	}
	return 0;
}

int Script::lua_AddDefend(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		const int type = argc >= 2 ? (int)lua_tointeger(l, 2) : 1;
		gm->scriptAPI.addDefend((int)lua_tointeger(l, 1), type);
	}
	return 0;
}

int Script::lua_AddEvade(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.addEvade((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_AddExp(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.addExp((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_AddMoney(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.addMoney((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_EquipGoods(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.equipGoods((int)lua_tointeger(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_AddRandMoney(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.addRandMoney((int)lua_tointeger(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_AddGoods(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		int count = argc >= 2 ? (int)lua_tointeger(l, 2) : 1;
		gm->scriptAPI.addGoods(getLuaString(l, 1), count);
	}
	return 0;
}

int Script::lua_AddRandGoods(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.addRandGoods(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_AddMagic(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.addMagic(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_AddTalent(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.addTalent(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_AddOneMagic(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.addOneMagic(getLuaString(l, 1), getLuaString(l, 2));
	}
	return 0;
}

int Script::lua_DelGoods(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.deleteGoods(getLuaString(l, 1));
	}
	else
	{
		gm->scriptAPI.deleteGoods();
	}
	return 0;
}

int Script::lua_DelGoodByName(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		int count = argc >= 2 ? (int)lua_tointeger(l, 2) : 0;
		gm->scriptAPI.deleteGoodsByName(getLuaString(l, 1), count);
	}
	return 0;
}

int Script::lua_DelMagic(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.deleteMagic(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_AddMagicExp(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.addMagicExp(getLuaString(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_FullLife(lua_State * l)
{
	gm->scriptAPI.fullLife();
	return 0;
}

int Script::lua_FullThew(lua_State * l)
{
	gm->scriptAPI.fullThew();
	return 0;
}

int Script::lua_FullMana(lua_State * l)
{
	gm->scriptAPI.fullMana();
	return 0;
}

int Script::lua_UpdateState(lua_State * l)
{
	gm->scriptAPI.updateState();
	return 0;
}

int Script::lua_SaveGoods(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.saveGoods((int)lua_tointeger(l, 1));
	}
	else
	{
		gm->scriptAPI.saveGoods(-1);
	}
	return 0;
}

int Script::lua_LoadGoods(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.loadGoods((int)lua_tointeger(l, 1));
	}
    else
    {
        gm->scriptAPI.loadGoods(-1);
	}
	return 0;
}

int Script::lua_SaveGoodsSnapshot(lua_State* l)
{
	gm->scriptAPI.saveGoods(getLuaSnapshotKey(l, 1));
	return 0;
}

int Script::lua_LoadGoodsSnapshot(lua_State* l)
{
	gm->scriptAPI.loadGoods(getLuaSnapshotKey(l, 1));
	return 0;
}

int Script::lua_ClearGoods(lua_State * l)
{
	gm->scriptAPI.clearGoods();
	return 0;
}

int Script::lua_ClearMagic(lua_State* l)
{
	gm->scriptAPI.clearMagic();
	return 0;
}

int Script::lua_GetGoodsNum(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.getGoodsNum(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_GetGoodsNumByName(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.getGoodsNumByName(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_GetGoodsState(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.getGoodsState(getLuaString(l, 1), getLuaString(l, 2), getLuaString(l, 3));
	}
	return 0;
}

int Script::lua_GetExp(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.getExp(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_ClearAllVar(lua_State* l)
{
	int argc = lua_gettop(l);
	std::vector<std::string> keepNames;
	keepNames.reserve(argc);
	for (int i = 1; i <= argc; i++)
	{
		keepNames.push_back(getLuaString(l, i));
	}
	gm->scriptAPI.clearAllVar(keepNames);
	return 0;
}

int Script::lua_CheckFreeGoodsSpace(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.checkFreeGoodsSpace(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_CheckFreeMagicSpace(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.checkFreeMagicSpace(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_GetGoodsCountByFile(lua_State* l)
{
	int argc = lua_gettop(l);
	int value = argc >= 1 ? gm->goodsManager.getItemNum(getLuaString(l, 1)) : 0;
	lua_pushinteger(l, value);
	return 1;
}

int Script::lua_GetGoodsCountByName(lua_State* l)
{
	int argc = lua_gettop(l);
	int value = argc >= 1 ? gm->goodsManager.getItemNumByDisplayName(getLuaString(l, 1)) : 0;
	lua_pushinteger(l, value);
	return 1;
}

int Script::lua_HasGoodsFreeSpace(lua_State* l)
{
	bool hasFreeSpace = false;
	for (int i = gm->goodsManager.storeBegin(); i <= gm->goodsManager.bottomEnd() && i < gm->goodsManager.listLength(); i++)
	{
		if ((gm->goodsManager.isStoreIndex(i) || gm->goodsManager.isBottomIndex(i)) && !gm->goodsManager.goodsListExists(i))
		{
			hasFreeSpace = true;
			break;
		}
	}
	lua_pushinteger(l, hasFreeSpace ? 1 : 0);
	return 1;
}

int Script::lua_HasMagicFreeSpace(lua_State* l)
{
	lua_pushinteger(l, gm->magicManager.primaryFreeIndex() >= 0 ? 1 : 0);
	return 1;
}

int Script::lua_GetMagicLevel(lua_State* l)
{
	int argc = lua_gettop(l);
	int value = 0;
	if (argc >= 1)
	{
		MagicInfo* magicInfo = gm->magicManager.findPrimaryMagic(getLuaString(l, 1));
		value = magicInfo != nullptr ? magicInfo->level : 0;
	}
	lua_pushinteger(l, value);
	return 1;
}

int Script::lua_GetMoney(lua_State* l)
{
	lua_pushinteger(l, gm->player != nullptr ? gm->player->money : 0);
	return 1;
}

int Script::lua_GetPlayerExp(lua_State* l)
{
	lua_pushinteger(l, gm->player != nullptr ? gm->player->exp : 0);
	return 1;
}

int Script::lua_GetPlayerStat(lua_State* l)
{
	int argc = lua_gettop(l);
	int value = 0;
	if (argc >= 1)
	{
		const std::string tempVariableName = "__script_get_player_stat_result";
		gm->scriptAPI.getPlayerState(getLuaString(l, 1), tempVariableName);
		value = gm->varList.getInteger(tempVariableName);
	}
	lua_pushinteger(l, value);
	return 1;
}

int Script::lua_GetPartnerIndex(lua_State* l)
{
	const std::string tempVariableName = "__script_get_partner_index_result";
	gm->scriptAPI.getPartnerIdx(tempVariableName);
	lua_pushinteger(l, gm->varList.getInteger(tempVariableName));
	return 1;
}

int Script::lua_GetPlayerState(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.getPlayerState(getLuaString(l, 1), getLuaString(l, 2));
	}
	return 0;
}

int Script::lua_IsEquipWeapon(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.isEquipWeapon(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_GetMoneyNum(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.getMoneyNum(getLuaString(l, 1));
	}
	else
	{
		gm->scriptAPI.getMoneyNum();
	}
	return 0;
}

int Script::lua_SetMoneyNum(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.setMoneyNum((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_Gamble(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		const char* varName = lua_tostring(l, 3);
		if (varName != nullptr)
		{
			gm->scriptAPI.gamble((int)lua_tointeger(l, 1), (int)lua_tointeger(l, 2), varName);
		}
	}
	return 0;
}

int Script::lua_ShowGamble(lua_State* l)
{
	int argc = lua_gettop(l);
	int cost = argc >= 1 ? (int)lua_tointeger(l, 1) : 0;
	int npcType = argc >= 2 ? (int)lua_tointeger(l, 2) : 0;
	bool result = gm->scriptAPI.showGamble(cost, npcType);
	lua_pushinteger(l, result ? 1 : 0);
	return 1;
}

int Script::lua_ShowDiceGame(lua_State* l)
{
	int argc = lua_gettop(l);
	gm->scriptAPI.showDiceGame(argc >= 1 ? getLuaString(l, 1) : "");
	return 0;
}

int Script::lua_ShowFishGame(lua_State* l)
{
	gm->scriptAPI.showFishGame();
	return 0;
}

int Script::lua_ShowStealWin(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.showStealWin(getLuaString(l, 1), getLuaString(l, 2), getLuaString(l, 3));
	}
	else if (argc >= 1)
	{
		gm->scriptAPI.showStealWin(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_ShowGiveGoodsWin(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.showGiveGoodsWin(getLuaString(l, 1), getLuaString(l, 2), getLuaString(l, 3));
	}
	return 0;
}

int Script::lua_ShowMessage(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		std::string text;
		if (getTalkTextFromLuaArgument(l, 1, text))
		{
			if (!text.empty())
			{
				gm->scriptAPI.showMessage(text);
			}
		}
		else
		{
			gm->scriptAPI.showMessage(getLuaString(l, 1));
		}
	}
	return 0;
}

int Script::lua_ShowSystemMsg(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		int stayTime = argc >= 2 ? (int)lua_tointeger(l, 2) : 3000;
		gm->scriptAPI.showSystemMessage(getLuaString(l, 1), stayTime);
	}
	return 0;
}

int Script::lua_Memo(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.addToMemo(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_AddToMemo(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		std::string text;
		if (getTalkTextFromLuaArgument(l, 1, text))
		{
			if (!text.empty())
			{
				gm->scriptAPI.addToMemo(text);
			}
		}
		else
		{
			gm->scriptAPI.addToMemo(getLuaString(l, 1));
		}
	}
	return 0;
}

int Script::lua_DelMemo(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.deleteMemo(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_ClearMemo(lua_State * l)
{
	gm->scriptAPI.clearMemo();
	return 0;
}

int Script::lua_BuyGoods(lua_State * l)
{
	int argc = lua_gettop(l);
	std::string fileName = argc >= 1 ? getLuaString(l, 1) : "";
	gm->scriptAPI.buyGoods(fileName);
	return 0;
}

int Script::lua_BuyGoodsOnly(lua_State * l)
{
	int argc = lua_gettop(l);
	std::string fileName = argc >= 1 ? getLuaString(l, 1) : "";
	gm->scriptAPI.buyGoodsOnly(fileName);
	return 0;
}

int Script::lua_SellGoods(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.sellGoods(getLuaString(l, 1));
	}
	else
	{
		gm->scriptAPI.sellGoods();
	}
	return 0;
}

int Script::lua_ReturnToTitle(lua_State * l)
{
	gm->scriptAPI.returnToTitle();
	return 0;
}

int Script::lua_EnableInput(lua_State * l)
{
	gm->scriptAPI.enableInput();
	return 0;
}

int Script::lua_DisableInput(lua_State * l)
{
	gm->scriptAPI.disableInput();
	return 0;
}

int Script::lua_SetInputEnabled(lua_State* l)
{
	if (getLuaBoolean(l, 1))
	{
		gm->scriptAPI.enableInput();
	}
	else
	{
		gm->scriptAPI.disableInput();
	}
	return 0;
}

int Script::lua_HideInterface(lua_State * l)
{
	gm->scriptAPI.hideInterface();
	return 0;
}

int Script::lua_SetInterfaceVisible(lua_State* l)
{
	if (getLuaBoolean(l, 1))
	{
		gm->scriptAPI.showInterface();
	}
	else
	{
		gm->scriptAPI.hideInterface();
	}
	return 0;
}

int Script::lua_HideBottomWnd(lua_State * l)
{
	gm->scriptAPI.hideBottomWnd();
	return 0;
}

int Script::lua_ShowBottomWnd(lua_State * l)
{
	gm->scriptAPI.showBottomWnd();
	return 0;
}

int Script::lua_HideMouseCursor(lua_State * l)
{
	gm->scriptAPI.hideMouseCursor();
	return 0;
}

int Script::lua_ShowMouseCursor(lua_State * l)
{
	gm->scriptAPI.showMouseCursor();
	return 0;
}

int Script::lua_ShowSnow(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.showSnow((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_ShowRandomSnow(lua_State * l)
{
	gm->scriptAPI.showRandomSnow();
	return 0;
}

int Script::lua_ShowRain(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.showRain((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_BeginRain(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.beginRain(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_EndRain(lua_State* l)
{
	gm->scriptAPI.endRain();
	return 0;
}

int Script::lua_CheckYear(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.checkYear(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_GetVar(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		int value = gm->scriptAPI.getVar(getLuaString(l, 1));
		lua_pushnumber(l, value);
	}
	else
	{
		lua_pushnumber(l, 0);
	}
	return 1;
}

int Script::lua_Assign(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.assign(getLuaString(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_Add(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.add(getLuaString(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_Sub(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.add(getLuaString(l, 1), -(int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_Talk(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.talk((int)lua_tointeger(l, 1), (int)lua_tointeger(l, 2));
	}
	else if (argc >= 1)
	{
		gm->scriptAPI.talk(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_ShowTalk(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.talk((int)lua_tointeger(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_Say(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 2 && lua_isnumber(l, 2))
	{
		gm->scriptAPI.say(getLuaString(l, 1), (int)lua_tointeger(l, 2));
	}
	else if (argc >= 3)
	{
		gm->scriptAPI.say(
			getLuaSpeakerDialogText(l, 1, 2),
			(int)lua_tointeger(l, 3));
	}
	else if (argc >= 2)
	{
		gm->scriptAPI.say(getLuaSpeakerDialogText(l, 1, 2));
	}
	else if (argc >= 1)
	{
		gm->scriptAPI.say(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_FadeIn(lua_State * l)
{
	gm->scriptAPI.fadeIn();
	return 0;
}

int Script::lua_FadeOut(lua_State * l)
{
	gm->scriptAPI.fadeOut();
	return 0;
}

int Script::lua_SetFadeLum(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.setFadeLum((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_SetMainLum(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.setMainLum((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_PlayMusic(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.playMusic(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_PlayRandomMusic(lua_State * l)
{
	int argc = lua_gettop(l);
	std::string fileA = argc >= 1 ? getLuaString(l, 1) : "";
	std::string fileB = argc >= 2 ? getLuaString(l, 2) : "";
	std::string fileC = argc >= 3 ? getLuaString(l, 3) : "";
	gm->scriptAPI.playRandomMusic(fileA, fileB, fileC);
	return 0;
}

int Script::lua_StopMusic(lua_State * l)
{
	gm->scriptAPI.stopMusic();
	return 0;
}

int Script::lua_PlaySound(lua_State * l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.playSound(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_StopSound(lua_State* l)
{
	gm->scriptAPI.stopSound();
	return 0;
}

int Script::lua_GetRandNum(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.getRandNum(getLuaString(l, 1), (int)lua_tointeger(l, 2), (int)lua_tointeger(l, 3));
	}
	return 0;
}

int Script::lua_RandRun(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.randRun(getLuaString(l, 1), getLuaString(l, 2), getLuaString(l, 3));
	}
	return 0;
}

int Script::lua_GetPlayerLevel(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.getPlayerLevel(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_GetNpcCount(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.getNpcCount((int)lua_tointeger(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_DelCurObj(lua_State* l)
{
	gm->scriptAPI.delCurObj();
	return 0;
}

int Script::lua_ShowInterface(lua_State* l)
{
	gm->scriptAPI.showInterface();
	return 0;
}

int Script::lua_DrawBackground(lua_State* l)
{
	gm->scriptAPI.drawBackground();
	return 0;
}

int Script::lua_ClearEffect(lua_State* l)
{
	gm->scriptAPI.clearEffect();
	return 0;
}

int Script::lua_SaveGame(lua_State* l)
{
	gm->scriptAPI.saveGame();
	return 0;
}

int Script::lua_ClearAllSave(lua_State* l)
{
	gm->scriptAPI.clearAllSave();
	return 0;
}

int Script::lua_EnableSave(lua_State* l)
{
	gm->scriptAPI.enableSave();
	return 0;
}

int Script::lua_DisableSave(lua_State* l)
{
	gm->scriptAPI.disableSave();
	return 0;
}

int Script::lua_SavePlayerSnapshot(lua_State* l)
{
	gm->scriptAPI.savePlayer(getLuaSnapshotKey(l, 1));
	return 0;
}

int Script::lua_LoadPlayerSnapshot(lua_State* l)
{
	gm->scriptAPI.loadPlayer(getLuaSnapshotKey(l, 1));
	return 0;
}

int Script::lua_SetSaveEnabled(lua_State* l)
{
	if (getLuaBoolean(l, 1))
	{
		gm->scriptAPI.enableSave();
	}
	else
	{
		gm->scriptAPI.disableSave();
	}
	return 0;
}

int Script::lua_SetRunEnabled(lua_State* l)
{
	if (getLuaBoolean(l, 1))
	{
		gm->scriptAPI.enableRun();
	}
	else
	{
		gm->scriptAPI.disableRun();
	}
	return 0;
}

int Script::lua_SetJumpEnabled(lua_State* l)
{
	if (getLuaBoolean(l, 1))
	{
		gm->scriptAPI.enableJump();
	}
	else
	{
		gm->scriptAPI.disableJump();
	}
	return 0;
}

int Script::lua_SetFightEnabled(lua_State* l)
{
	if (getLuaBoolean(l, 1))
	{
		gm->scriptAPI.enableFight();
	}
	else
	{
		gm->scriptAPI.disableFight();
	}
	return 0;
}

int Script::lua_LimitMana(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.limitMana((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_ShowNpc(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.showNpc(getLuaString(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_OpenWaterEffect(lua_State* l)
{
	gm->scriptAPI.openWaterEffect();
	return 0;
}

int Script::lua_CloseWaterEffect(lua_State* l)
{
	gm->scriptAPI.closeWaterEffect();
	return 0;
}

int Script::lua_Watch(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		int watchType = 0;
		if (argc >= 3)
		{
			watchType = (int)lua_tointeger(l, 3);
		}
		gm->scriptAPI.watch(getLuaString(l, 1), getLuaString(l, 2), watchType);
	}
	return 0;
}

int Script::lua_SetTrap(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		if (lua_isnumber(l, 1))
		{
			std::string mapName = getLuaString(l, 3);
			if (mapName.empty())
			{
				mapName = gm->global.data.mapName;
			}
			gm->scriptAPI.setTrap(mapName, (int)lua_tointeger(l, 1), getLuaString(l, 2));
		}
		else
		{
			gm->scriptAPI.setTrap(getLuaString(l, 1), (int)lua_tointeger(l, 2), getLuaString(l, 3));
		}
	}
	else if (argc >= 2)
	{
		gm->scriptAPI.setTrap(gm->global.data.mapName, (int)lua_tointeger(l, 1), getLuaString(l, 2));
	}
	return 0;
}

int Script::lua_SetNpcMagicFile(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.setNpcMagicFile(getLuaString(l, 1), getLuaString(l, 2));
	}
	else if (argc >= 1)
	{
		gm->scriptAPI.setNpcMagicFile("", getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_SetNpcMagicLevel(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.setNpcMagicFile(getLuaString(l, 1), getLuaString(l, 2));
		gm->scriptAPI.setNpcMagicLevel(getLuaString(l, 1), (int)lua_tointeger(l, 3));
	}
	else if (argc >= 2)
	{
		gm->scriptAPI.setNpcMagicLevel(getLuaString(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_SetPlayerMagicToUseWhenBeAttacked(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.setPlayerMagicToUseWhenBeAttacked(getLuaString(l, 1), (int)lua_tointeger(l, 2));
	}
	return 0;
}

int Script::lua_SetNpcMagicToUseWhenBeAttacked(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.setNpcMagicToUseWhenBeAttacked(getLuaString(l, 1), getLuaString(l, 2), (int)lua_tointeger(l, 3));
	}
	return 0;
}

int Script::lua_SetNpcClickScript(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.setNpcClickScript(getLuaString(l, 1), getLuaString(l, 2));
	}
	return 0;
}

int Script::lua_SetNpcPartner(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.setNpcPartner(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_SetPartnerLevel(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		const char* name = lua_tostring(l, 1);
		gm->scriptAPI.setPartnerLevel(name != nullptr ? name : "", (int)lua_tointeger(l, 2));
	}
	else if (argc >= 1)
	{
		gm->scriptAPI.setPartnerLevel((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_PlayerAddEmotion(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.playerAddEmotion((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_PlayerAddJustice(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.playerAddJustice((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_GetPartnerIdx(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.getPartnerIdx(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_MoveScreenEx(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		gm->scriptAPI.moveScreenEx((int)lua_tointeger(l, 1), (int)lua_tointeger(l, 2), (int)lua_tointeger(l, 3));
	}
	return 0;
}

int Script::lua_DisplayMessage(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.displayMessage(getLuaString(l, 1));
	}
	return 0;
}

int Script::lua_DisableMapScroll(lua_State* l)
{
	gm->scriptAPI.disableMapScroll();
	return 0;
}

int Script::lua_EnableMapScroll(lua_State* l)
{
	gm->scriptAPI.enableMapScroll();
	return 0;
}

int Script::lua_SetShowMapPos(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.setShowMapPos((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_OpenObj(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.openObj(getLuaString(l, 1));
	}
	else
	{
		gm->scriptAPI.openObj();
	}
	return 0;
}

int Script::lua_FreeMap(lua_State* l)
{
	gm->scriptAPI.freeMap();
	return 0;
}

int Script::lua_OpenTimeLimit(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.openTimeLimit((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_CloseTimeLimit(lua_State* l)
{
	gm->scriptAPI.closeTimeLimit();
	return 0;
}

int Script::lua_HideTimerWnd(lua_State* l)
{
	gm->scriptAPI.hideTimerWnd();
	return 0;
}

int Script::lua_SetTimeScript(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 2)
	{
		gm->scriptAPI.setTimeScript((int)lua_tointeger(l, 1), getLuaString(l, 2));
	}
	return 0;
}

int Script::lua_Choose(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 4)
	{
		gm->scriptAPI.choose(getLuaString(l, 1), getLuaString(l, 2), getLuaString(l, 3), getLuaString(l, 4));
	}
	return 0;
}

int Script::lua_ChooseEx(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 3)
	{
		std::vector<std::string> options;
		for (int i = 2; i < argc; i++)
		{
			options.push_back(getLuaString(l, i));
		}
		gm->scriptAPI.chooseEx(getLuaString(l, 1), options, getLuaString(l, argc));
	}
	return 0;
}

int Script::lua_ChooseMultiple(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 5)
	{
		std::vector<std::string> options;
		for (int i = 5; i <= argc; i++)
		{
			options.push_back(getLuaString(l, i));
		}
		gm->scriptAPI.chooseMultiple((int)lua_tointeger(l, 1), (int)lua_tointeger(l, 2), getLuaString(l, 3), getLuaString(l, 4), options);
	}
	return 0;
}

int Script::lua_ChoosePlus(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 6)
	{
		std::vector<std::string> options;
		for (int i = 5; i < argc; i++)
		{
			options.push_back(getLuaString(l, i));
		}
		gm->scriptAPI.choosePlus(getLuaString(l, 1), (int)lua_tointeger(l, 2), (int)lua_tointeger(l, 3), getLuaString(l, 4), options, getLuaString(l, argc));
	}
	return 0;
}

int Script::lua_Select(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 4)
	{
		gm->scriptAPI.select((int)lua_tointeger(l, 1), (int)lua_tointeger(l, 2), (int)lua_tointeger(l, 3), getLuaString(l, 4));
	}
	return 0;
}

int Script::lua_PlayerChange(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.playerChange((int)lua_tointeger(l, 1));
	}
	return 0;
}

int Script::lua_MergeNpc(lua_State* l)
{
	int argc = lua_gettop(l);
	if (argc >= 1)
	{
		gm->scriptAPI.mergeNpc(getLuaString(l, 1));
	}
	return 0;
}

void Script::loadLib(ScriptLibraryProfile libraryProfile)
{
	luaState = luaL_newstate();
	*static_cast<Script**>(
		lua_getextraspace(luaState)) = this;
	if (libraryProfile == ScriptLibraryProfile::EditorRunSafe)
	{
		openEditorRunLibraries(luaState);
	}
	else
	{
		luaL_openlibs(luaState);
	}
	// A blocking script API can pump the SDL event loop and receive application
	// quit. Abort before the chunk executes more Lua or invokes another API.
	lua_sethook(
		luaState,
		runtimeTraceLuaHook,
		LUA_MASKCOUNT |
			(runtimeTraceWriter != nullptr
				? LUA_MASKLINE
				: 0),
		100);
}
