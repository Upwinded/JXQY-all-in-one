#pragma once


#ifdef __cplusplus
extern "C" {
#endif
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#ifdef __cplusplus
}
#endif

#include "../../Types/Types.h"
#include "../../Image/IMP.h"
#include "../../File/File.h"
#include "../../File/INIReader.h"
#include "../../libconvert/libconvert.h"
#include "../../Launch/EditorRunRuntimeTrace.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace EditorRun
{
class RuntimeTraceWriter;
}

class ScriptRunningHolder
{
public:
	ScriptRunningHolder(bool* running)
	{
		_running = running;
		wasRunning = *_running;
		*_running = true;
	}
	virtual ~ScriptRunningHolder() { *_running = wasRunning; }
private:
	bool * _running;
	bool wasRunning = false;
};

enum class ScriptLibraryProfile
{
	Full,
	EditorRunSafe
};

enum class MoveScreenArgumentMode
{
	Distance,
	FrameCountAndSpeed
};

constexpr MoveScreenArgumentMode resolveMoveScreenArgumentMode(
	int argumentCount)
{
	return argumentCount >= 3
		? MoveScreenArgumentMode::FrameCountAndSpeed
		: MoveScreenArgumentMode::Distance;
}

enum class ExactScriptExecutionStatus
{
	Success,
	LoadFailed,
	RuntimeFailed
};

struct ExactScriptExecutionResult
{
	ExactScriptExecutionStatus status =
		ExactScriptExecutionStatus::Success;
	std::uint32_t line = 0;
	std::uint32_t column = 0;
	std::string message;

	bool succeeded() const noexcept
	{
		return status == ExactScriptExecutionStatus::Success;
	}
};

struct ResolvedTraceScriptSource
{
	std::vector<std::uint8_t> bytes;
	EditorRun::RuntimeTraceScriptIdentity identity;
};

class Script
{
public:
	Script();
	explicit Script(
		ScriptLibraryProfile libraryProfile,
		EditorRun::RuntimeTraceWriter* runtimeTraceWriter = nullptr);
	Script(const std::string& n);
	Script(
		const std::string& n,
		ScriptLibraryProfile libraryProfile,
		EditorRun::RuntimeTraceWriter* runtimeTraceWriter = nullptr);
	virtual ~Script();

	int runScript(std::unique_ptr<char[]>& s, int len);
	int runScript(const std::string& fileName);
	ExactScriptExecutionResult runExactResourceBytes(
		const std::vector<std::uint8_t>& source,
		const std::string& virtualPath);
	ExactScriptExecutionResult runResolvedTraceScriptSource(
		ResolvedTraceScriptSource source,
		std::uint64_t capturedParentExecutionId = 0,
		bool parentWasCaptured = false);
	std::uint64_t currentExecutionId() const noexcept;
	bool running = false;
private:	
	friend class ScriptEngineRuntimeTestAccess;

	int runScriptWithChunkName(
		const char* source,
		int len,
		const std::string& chunkName,
		bool* chunkLoaded,
		ExactScriptExecutionResult* exactResult = nullptr,
		bool* applicationQuitAborted = nullptr);
	void registerFunc();
	void registerLuaFunction(const std::string& functionName, lua_CFunction function);
	static int callRegisteredLuaFunction(lua_State* luaState);
	static void runtimeTraceLuaHook(
		lua_State* luaState,
		lua_Debug* debug);
	void enqueueSourceLine(std::uint64_t line);
	void enqueueApiCall(std::string_view apiName);

	std::string name = "script";
	EditorRun::RuntimeTraceWriter* runtimeTraceWriter = nullptr;
	std::uint64_t nextExecutionId = 1;
	std::vector<std::uint64_t> executionStack;

	lua_State * luaState = nullptr;

	void loadLib(ScriptLibraryProfile libraryProfile);

	static int lua_printf(lua_State * l);

	static int lua_GetVar(lua_State * l);
	static int lua_Assign(lua_State * l);
	static int lua_Add(lua_State * l);
	static int lua_Sub(lua_State * l);
	static int lua_Talk(lua_State * l);
	static int lua_ShowTalk(lua_State* l);
	static int lua_Say(lua_State * l);
	static int lua_FadeIn(lua_State * l);
	static int lua_FadeOut(lua_State * l);
	static int lua_SetFadeLum(lua_State * l);
	static int lua_SetMainLum(lua_State * l);
	static int lua_PlayMusic(lua_State * l);
	static int lua_PlayRandomMusic(lua_State * l);
	static int lua_StopMusic(lua_State * l);
	static int lua_PlaySound(lua_State * l);
	static int lua_StopSound(lua_State* l);
	static int lua_RunScript(lua_State * l);
	static int lua_RunParallelScript(lua_State* l);
	static int lua_MoveScreen(lua_State * l);
	static int lua_Sleep(lua_State * l);
	static int lua_PlayMovie(lua_State * l);
	static int lua_StopMovie(lua_State * l);
	static int lua_LoadMap(lua_State * l);
	static int lua_LoadGame(lua_State * l);
	static int lua_GetCurrentMapPath(lua_State* l);
	static int lua_SetMapPos(lua_State * l);
	static int lua_SetMapTrap(lua_State * l);
	static int lua_SaveMapTrap(lua_State * l);
	static int lua_SetMapTime(lua_State * l);

	static int lua_ChangeASFColor(lua_State* l);
	static int lua_ChangeMapColor(lua_State* l);

	static int lua_LoadObj(lua_State * l);
	static int lua_SaveObj(lua_State * l);
	static int lua_AddObj(lua_State * l);
	static int lua_DelObj(lua_State * l);
	static int lua_SetObjPos(lua_State * l);
	static int lua_SetObjOfs(lua_State * l);
	static int lua_SetObjKind(lua_State * l);
	static int lua_SetObjScript(lua_State * l);
	static int lua_RunObjScript(lua_State * l);
	static int lua_RunObjRightScript(lua_State * l);
	static int lua_InteractNearestObj(lua_State * l);
	static int lua_ClearBody(lua_State * l);
	static int lua_OpenBox(lua_State * l);
	static int lua_CloseBox(lua_State * l);
	static int lua_GetObjState(lua_State * l);
	static int lua_LoadNpc(lua_State * l);
	static int lua_LoadOneNpc(lua_State* l);
	static int lua_SaveNpc(lua_State * l);
	static int lua_AddNpc(lua_State * l);
	static int lua_DelNpc(lua_State * l);
	static int lua_GetNpcPos(lua_State* l);
	static int lua_SetNpcRes(lua_State * l);
	static int lua_SetNpcScript(lua_State * l);
	static int lua_SetNpcDeathScript(lua_State * l);
	static int lua_SetAllNpcScript(lua_State* l);
	static int lua_SetAllNpcDeathScript(lua_State* l);
	static int lua_InteractNearestNpc(lua_State* l);
	static int lua_NpcGoto(lua_State * l);
	static int lua_NpcGotoEx(lua_State * l);
	static int lua_NpcGotoDir(lua_State * l);
	static int lua_SetNpcDestination(lua_State * l);
	static int lua_FollowNpc(lua_State * l);
	static int lua_FollowPlayer(lua_State * l);
	static int lua_EnableNpcAI(lua_State * l);
	static int lua_DisableNpcAI(lua_State * l);
	static int lua_SetNpcAIEnabled(lua_State* l);
	static int lua_EnablePartnerCombat(lua_State* l);
	static int lua_DisablePartnerCombat(lua_State* l);
	static int lua_NpcAttack(lua_State * l);
	static int lua_NpcUseMagic(lua_State* l);
	static int lua_SetNpcPos(lua_State * l);
	static int lua_SetNpcDir(lua_State * l);
	static int lua_SetNpcKind(lua_State * l);
	static int lua_SetNpcLevel(lua_State * l);
	static int lua_SetNpcState(lua_State* l);
	static int lua_SetNpcAction(lua_State * l);
	static int lua_SetNpcRelation(lua_State * l);
	static int lua_SetNpcActionType(lua_State * l);
	static int lua_SetNpcActionFile(lua_State * l);
	static int lua_NpcSpecialAction(lua_State * l);
	static int lua_NpcSpecialActionNonBlocking(lua_State * l);
	static int lua_NpcSpecialActionEx(lua_State * l);
	static int lua_ChangeLife(lua_State* l);
	static int lua_ChangeMana(lua_State* l);
	static int lua_ChangeThew(lua_State* l);
	static int lua_GetNpcState(lua_State* l);
	static int lua_AddNpcProperty(lua_State* l);
	static int lua_AddKindValue(lua_State* l);
	static int lua_SetMapNpcAttr(lua_State* l);
	static int lua_SetNpcTalkContent(lua_State* l);
	static int lua_TalkSelfTip(lua_State* l);
	static int lua_SetAllNpcIsEnemy(lua_State* l);
	static int lua_SetDropIni(lua_State* l);
	static int lua_EnableDrop(lua_State* l);
	static int lua_DisableDrop(lua_State* l);
	static int lua_SetDropEnabled(lua_State* l);
	static int lua_ChangeFlyIni(lua_State* l);
	static int lua_ChangeFlyIni2(lua_State* l);
	static int lua_AddFlyInis(lua_State* l);
	static int lua_AddNpcMagic(lua_State* l);
	static int lua_SetKeepAttack(lua_State* l);
	static int lua_ShowSignalTip(lua_State* l);
	static int lua_SetSignalTipHidden(lua_State* l);


	static int lua_LoadPlayer(lua_State * l);
	static int lua_SavePlayer(lua_State * l);
	static int lua_SetPlayerPos(lua_State * l);
	static int lua_SetPlayerDir(lua_State * l);
	static int lua_SetPlayerScn(lua_State * l);
	static int lua_SetPlayerLum(lua_State * l);
	static int lua_SetLevelFile(lua_State * l);
	static int lua_SetMagicLevel(lua_State * l);
	static int lua_GetPlayerMagicLevel(lua_State * l);
	static int lua_GetMagicState(lua_State* l);
	static int lua_GetEffectState(lua_State* l);
	static int lua_GetMapState(lua_State* l);
	static int lua_GetLeechcraftDifference(lua_State* l);
	static int lua_MoveMagic(lua_State * l);
	static int lua_SetPlayerLevel(lua_State * l);
	static int lua_SetPlayerState(lua_State * l);
	static int lua_ToNonFightingState(lua_State* l);
	static int lua_EnableRun(lua_State * l);
	static int lua_DisableRun(lua_State * l);
	static int lua_EnableJump(lua_State * l);
	static int lua_DisableJump(lua_State * l);
	static int lua_EnableFight(lua_State * l);
	static int lua_DisableFight(lua_State * l);
	static int lua_PlayerGoto(lua_State * l);
	static int lua_PlayerGotoEx(lua_State * l);
	static int lua_PlayerRunTo(lua_State * l);
	static int lua_PlayerRunToEx(lua_State* l);
	static int lua_PlayerJumpTo(lua_State * l);
	static int lua_PlayerGotoDir(lua_State * l);
	static int lua_SetWalkIsRun(lua_State * l);
	static int lua_AddMoveSpeedPercent(lua_State* l);
	static int lua_UseMagic(lua_State* l);
	static int lua_PetrifyMillisecond(lua_State* l);
	static int lua_PoisonMillisecond(lua_State* l);
	static int lua_FrozenMillisecond(lua_State* l);

	static int lua_AddLife(lua_State * l);
	static int lua_AddLifeMax(lua_State * l);
	static int lua_AddThew(lua_State * l);
	static int lua_AddThewMax(lua_State * l);
	static int lua_AddMana(lua_State * l);
	static int lua_AddManaMax(lua_State * l);
	static int lua_AddAttack(lua_State * l);
	static int lua_AddDefend(lua_State * l);
	static int lua_AddEvade(lua_State * l);
	static int lua_AddExp(lua_State * l);
	static int lua_AddMoney(lua_State * l);
	static int lua_EquipGoods(lua_State * l);
	static int lua_AddRandMoney(lua_State * l);
	static int lua_AddGoods(lua_State * l);
	static int lua_AddRandGoods(lua_State * l);
	static int lua_AddMagic(lua_State * l);
	static int lua_AddTalent(lua_State* l);
	static int lua_AddOneMagic(lua_State* l);
	static int lua_DelGoods(lua_State * l);
	static int lua_DelGoodByName(lua_State* l);
	static int lua_DelMagic(lua_State * l);
	static int lua_AddMagicExp(lua_State * l);
	static int lua_FullLife(lua_State * l);
	static int lua_FullThew(lua_State * l);
	static int lua_FullMana(lua_State * l);
	static int lua_UpdateState(lua_State * l);
	static int lua_SaveGoods(lua_State * l);
	static int lua_LoadGoods(lua_State * l);
	static int lua_SaveGoodsSnapshot(lua_State* l);
	static int lua_LoadGoodsSnapshot(lua_State* l);
	static int lua_ClearGoods(lua_State * l);
	static int lua_ClearMagic(lua_State* l);
	static int lua_GetGoodsNum(lua_State * l);
	static int lua_GetGoodsNumByName(lua_State* l);
	static int lua_GetGoodsState(lua_State* l);
	static int lua_GetExp(lua_State* l);
	static int lua_ClearAllVar(lua_State* l);
	static int lua_CheckFreeGoodsSpace(lua_State* l);
	static int lua_CheckFreeMagicSpace(lua_State* l);
	static int lua_GetGoodsCountByFile(lua_State* l);
	static int lua_GetGoodsCountByName(lua_State* l);
	static int lua_HasGoodsFreeSpace(lua_State* l);
	static int lua_HasMagicFreeSpace(lua_State* l);
	static int lua_GetMagicLevel(lua_State* l);
	static int lua_GetMoney(lua_State* l);
	static int lua_GetPlayerExp(lua_State* l);
	static int lua_GetPlayerStat(lua_State* l);
	static int lua_GetPartnerIndex(lua_State* l);
	static int lua_GetPlayerState(lua_State* l);
	static int lua_IsEquipWeapon(lua_State* l);
	static int lua_GetMoneyNum(lua_State * l);
	static int lua_SetMoneyNum(lua_State * l);
	static int lua_Gamble(lua_State * l);
	static int lua_ShowGamble(lua_State* l);
	static int lua_ShowDiceGame(lua_State* l);
	static int lua_ShowFishGame(lua_State* l);
	static int lua_ShowStealWin(lua_State* l);
	static int lua_ShowGiveGoodsWin(lua_State* l);

	static int lua_ShowMessage(lua_State * l);
	static int lua_ShowSystemMsg(lua_State * l);
	static int lua_Memo(lua_State* l);
	static int lua_AddToMemo(lua_State * l);
	static int lua_DelMemo(lua_State* l);
	static int lua_ClearMemo(lua_State * l);
	static int lua_BuyGoods(lua_State * l);
	static int lua_BuyGoodsOnly(lua_State * l);
	static int lua_SellGoods(lua_State * l);
	static int lua_ReturnToTitle(lua_State * l);
	static int lua_EnableInput(lua_State * l);
	static int lua_DisableInput(lua_State * l);
	static int lua_SetInputEnabled(lua_State* l);
	static int lua_HideInterface(lua_State * l);
	static int lua_SetInterfaceVisible(lua_State* l);
	static int lua_HideBottomWnd(lua_State * l);
	static int lua_ShowBottomWnd(lua_State * l);
	static int lua_HideMouseCursor(lua_State * l);
	static int lua_ShowMouseCursor(lua_State * l);
	static int lua_ShowSnow(lua_State * l);
	static int lua_ShowRandomSnow(lua_State * l);
	static int lua_ShowRain(lua_State * l);
	static int lua_BeginRain(lua_State* l);
	static int lua_EndRain(lua_State* l);

	static int lua_CheckYear(lua_State* l);

	static int lua_GetRandNum(lua_State* l);
	static int lua_RandRun(lua_State* l);
	static int lua_GetPlayerLevel(lua_State* l);
	static int lua_GetNpcCount(lua_State* l);
	static int lua_DelCurObj(lua_State* l);
	static int lua_ShowInterface(lua_State* l);
	static int lua_DrawBackground(lua_State* l);
	static int lua_ClearEffect(lua_State* l);
	static int lua_SaveGame(lua_State* l);
	static int lua_ClearAllSave(lua_State* l);
	static int lua_EnableSave(lua_State* l);
	static int lua_DisableSave(lua_State* l);
	static int lua_SavePlayerSnapshot(lua_State* l);
	static int lua_LoadPlayerSnapshot(lua_State* l);
	static int lua_SetSaveEnabled(lua_State* l);
	static int lua_SetRunEnabled(lua_State* l);
	static int lua_SetJumpEnabled(lua_State* l);
	static int lua_SetFightEnabled(lua_State* l);
	static int lua_LimitMana(lua_State* l);
	static int lua_ShowNpc(lua_State* l);
	static int lua_OpenWaterEffect(lua_State* l);
	static int lua_CloseWaterEffect(lua_State* l);
	static int lua_Watch(lua_State* l);
	static int lua_SetTrap(lua_State* l);
	static int lua_GetObjPos(lua_State* l);
	static int lua_SetNpcMagicFile(lua_State* l);
	static int lua_SetNpcMagicLevel(lua_State* l);
	static int lua_SetPlayerMagicToUseWhenBeAttacked(lua_State* l);
	static int lua_SetNpcMagicToUseWhenBeAttacked(lua_State* l);
	static int lua_SetNpcClickScript(lua_State* l);
	static int lua_SetNpcPartner(lua_State* l);
	static int lua_SetPartnerLevel(lua_State* l);
	static int lua_PlayerAddEmotion(lua_State* l);
	static int lua_PlayerAddJustice(lua_State* l);
	static int lua_GetPartnerIdx(lua_State* l);
	static int lua_MoveScreenEx(lua_State* l);
	static int lua_DisplayMessage(lua_State* l);
	static int lua_DisableMapScroll(lua_State* l);
	static int lua_EnableMapScroll(lua_State* l);
	static int lua_SetShowMapPos(lua_State* l);
	static int lua_OpenObj(lua_State* l);
	static int lua_FreeMap(lua_State* l);
	static int lua_OpenTimeLimit(lua_State* l);
	static int lua_CloseTimeLimit(lua_State* l);
	static int lua_HideTimerWnd(lua_State* l);
	static int lua_SetTimeScript(lua_State* l);
	static int lua_Choose(lua_State* l);
	static int lua_ChooseEx(lua_State* l);
	static int lua_ChooseMultiple(lua_State* l);
	static int lua_ChoosePlus(lua_State* l);
	static int lua_Select(lua_State* l);
	static int lua_PlayerChange(lua_State* l);
	static int lua_MergeNpc(lua_State* l);

};
