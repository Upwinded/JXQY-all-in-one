#pragma once
#include "../../Engine/Engine.h"
#include "../../Types/Types.h"
#include "../../Image/IMP.h"
#include "../../File/PakFile.h"
#include "../../File/INIReader.h"
#include "../../../3rd/lua/lua.hpp"
#include "../../libconvert/libconvert.h"

class ScriptRunningHolder
{
public:
	ScriptRunningHolder(bool* running) { _running = running; *_running = true; }
	virtual ~ScriptRunningHolder() { *_running = false; }
private:
	bool * _running;
};

class Script
{
public:
	Script();
	Script(const std::string& n);
	virtual ~Script();

	int runScript(std::unique_ptr<char[]>& s, int len);
	int runScript(const std::string& fileName);
	bool running = false;
private:	

	void registerFunc();

	std::string name = "script";

	lua_State * luaState = nullptr;

	void loadLib();

	static int lua_printf(lua_State * l);

	static int lua_GetVar(lua_State * l);
	static int lua_Assign(lua_State * l);
	static int lua_Add(lua_State * l);
	static int lua_Talk(lua_State * l);
	static int lua_Say(lua_State * l);
	static int lua_FadeIn(lua_State * l);
	static int lua_FadeOut(lua_State * l);
	static int lua_SetFadeLum(lua_State * l);
	static int lua_SetMainLum(lua_State * l);
	static int lua_PlayMusic(lua_State * l);
	static int lua_StopMusic(lua_State * l);
	static int lua_PlaySound(lua_State * l);
	static int lua_RunScript(lua_State * l);
	static int lua_MoveScreen(lua_State * l);
	static int lua_Sleep(lua_State * l);
	static int lua_PlayMovie(lua_State * l);
	static int lua_StopMovie(lua_State * l);
	static int lua_LoadMap(lua_State * l);
	static int lua_LoadGame(lua_State * l);
	static int lua_SetMapPos(lua_State * l);
	static int lua_SetMapTrap(lua_State * l);
	static int lua_SaveMapTrap(lua_State * l);
	static int lua_SetMapTime(lua_State * l);
	static int lua_LoadObj(lua_State * l);
	static int lua_SaveObj(lua_State * l);
	static int lua_AddObj(lua_State * l);
	static int lua_DelObj(lua_State * l);
	static int lua_SetObjPos(lua_State * l);
	static int lua_SetObjKind(lua_State * l);
	static int lua_SetObjScript(lua_State * l);
	static int lua_ClearBody(lua_State * l);
	static int lua_OpenBox(lua_State * l);
	static int lua_CloseBox(lua_State * l);
	static int lua_LoadNpc(lua_State * l);
	static int lua_SaveNpc(lua_State * l);
	static int lua_AddNpc(lua_State * l);
	static int lua_DelNpc(lua_State * l);
	static int lua_SetNpcRes(lua_State * l);
	static int lua_SetNpcScript(lua_State * l);
	static int lua_SetNpcDeathScript(lua_State * l);
	static int lua_NpcGoto(lua_State * l);
	static int lua_NpcGotoEx(lua_State * l);
	static int lua_NpcGotoDir(lua_State * l);
	static int lua_FollowNpc(lua_State * l);
	static int lua_FollowPlayer(lua_State * l);
	static int lua_EnableNpcAI(lua_State * l);
	static int lua_DisableNpcAI(lua_State * l);
	static int lua_NpcAttack(lua_State * l);
	static int lua_SetNpcPos(lua_State * l);
	static int lua_SetNpcDir(lua_State * l);
	static int lua_SetNpcKind(lua_State * l);
	static int lua_SetNpcLevel(lua_State * l);
	static int lua_SetNpcAction(lua_State * l);
	static int lua_SetNpcRelation(lua_State * l);
	static int lua_SetNpcActionType(lua_State * l);
	static int lua_SetNpcActionFile(lua_State * l);
	static int lua_NpcSpecialAction(lua_State * l);
	static int lua_LoadPlayer(lua_State * l);
	static int lua_SavePlayer(lua_State * l);
	static int lua_SetPlayerPos(lua_State * l);
	static int lua_SetPlayerDir(lua_State * l);
	static int lua_SetPlayerScn(lua_State * l);
	static int lua_SetPlayerLum(lua_State * l);
	static int lua_SetLevelFile(lua_State * l);
	static int lua_SetMagicLevel(lua_State * l);
	static int lua_SetPlayerLevel(lua_State * l);
	static int lua_SetPlayerState(lua_State * l);
	static int lua_EnableRun(lua_State * l);
	static int lua_DisableRun(lua_State * l);
	static int lua_EnableJump(lua_State * l);
	static int lua_DisableJump(lua_State * l);
	static int lua_EnableFight(lua_State * l);
	static int lua_DisableFight(lua_State * l);
	static int lua_PlayerGoto(lua_State * l);
	static int lua_PlayerGotoEx(lua_State * l);
	static int lua_PlayerRunTo(lua_State * l);
	static int lua_PlayerJumpTo(lua_State * l);
	static int lua_PlayerGotoDir(lua_State * l);

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
	static int lua_AddRandMoney(lua_State * l);
	static int lua_AddGoods(lua_State * l);
	static int lua_AddRandGoods(lua_State * l);
	static int lua_AddMagic(lua_State * l);
	static int lua_DelGoods(lua_State * l);
	static int lua_DelMagic(lua_State * l);
	static int lua_AddMagicExp(lua_State * l);
	static int lua_FullLife(lua_State * l);
	static int lua_FullThew(lua_State * l);
	static int lua_FullMana(lua_State * l);
	static int lua_UpdateState(lua_State * l);
	static int lua_SaveGoods(lua_State * l);
	static int lua_LoadGoods(lua_State * l);
	static int lua_ClearGoods(lua_State * l);
	static int lua_GetGoodsNum(lua_State * l);
	static int lua_GetMoneyNum(lua_State * l);
	static int lua_SetMoneyNum(lua_State * l);

	static int lua_ShowMessage(lua_State * l);
	static int lua_AddToMemo(lua_State * l);
	static int lua_ClearMemo(lua_State * l);
	static int lua_BuyGoods(lua_State * l);
	static int lua_SellGoods(lua_State * l);
	static int lua_ReturnToTitle(lua_State * l);
	static int lua_EnableInput(lua_State * l);
	static int lua_DisableInput(lua_State * l);
	static int lua_HideInterface(lua_State * l);
	static int lua_HideBottomWnd(lua_State * l);
	static int lua_ShowBottomWnd(lua_State * l);
	static int lua_HideMouseCursor(lua_State * l);
	static int lua_ShowMouseCursor(lua_State * l);
	static int lua_ShowSnow(lua_State * l);
	static int lua_ShowRandomSnow(lua_State * l);
	static int lua_ShowRain(lua_State * l);

};

