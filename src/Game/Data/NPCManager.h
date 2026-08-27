#pragma once
#include "NPC.h"
#include <cstddef>
#include <cstdint>
#include <climits>
#include <functional>
#include <memory>
#include <vector>
#include <deque>
#include "../GameManager/SaveFileManager.h"

class INIReader;

class NPCManager:
	public Element
{
public:
	class PreparedLoad final
	{
	public:
		bool isValid() const noexcept;
		std::size_t npcCount() const noexcept;
		const std::string& sourcePath() const noexcept;

	private:
		friend class NPCManager;
		std::shared_ptr<INIReader> parsedIni;
		std::size_t preparedNpcCount = 0;
		std::string resolvedSourcePath;
	};

	NPCManager();
	virtual ~NPCManager();
private:
    std::shared_ptr<NPC> player = nullptr;
	struct DeadNPCInfo
	{
		Point position = { 0, 0 };
		int kind = nkNormal;
		int relation = nrNeutral;
		std::weak_ptr<NPC> attacker;
		int framesToKeep = 0;
	};
	std::deque<DeadNPCInfo> deadNPCInfos;
    bool beginFallbackApproach(std::shared_ptr<NPC> npc, std::shared_ptr<NPC> target);
	void addDefeatedNPCDrop(std::shared_ptr<NPC> npc);
	void advanceDeadNPCInfos();
	void releaseManagedNPCs(bool clearActionImages);
	bool prepareParsedLoad(
		const std::unique_ptr<char[]>& data,
		const std::string& sourcePath,
		bool exactResource,
		bool allowIncompleteSectionList,
		PreparedLoad& preparedLoad) const;
public:
    void setPlayer(std::shared_ptr<NPC> nowPlayer);
	int clickIndex = -1;

	void standAll();
	int findNPCIndex(std::shared_ptr<NPC> npc);
	bool findNPC(std::shared_ptr<NPC> npc);
	static bool isManagedEffectCaster(const std::shared_ptr<GameElement>& actor);
	static bool shouldPersistNPC(const std::shared_ptr<NPC>& npc);
	std::shared_ptr<NPC> findPlayerNPC();

	std::vector<std::shared_ptr<NPC>> findNPC(const std::string & npcName);
	std::vector<std::shared_ptr<NPC>> findNPC(int launcherKind);
	std::vector<std::shared_ptr<NPC>> findNPC(Point pos, int radius);
	std::vector<std::shared_ptr<NPC>> findNPC(int launcherKind, Point pos, int radius);
	std::shared_ptr<NPC> findNearestNPC(int launcherKind, Point pos, int radius);
	std::shared_ptr<NPC> findNearestViewNPC(int launcherKind, Point pos, int radius);
	std::shared_ptr<NPC> findNearestScriptViewNPC(Point pos, int radius);
	std::vector<std::shared_ptr<NPC>> findRadiusScriptViewNPC(Point pos, int radius);
	std::vector<std::shared_ptr<NPC>> findRadiusFastSelectionNPC(Point pos, int radius);
	std::vector<std::shared_ptr<NPC>> findFriendFighters();
	void clearCombatTargetIfEqual(std::shared_ptr<GameElement> target);


	// 判断两个阵营关系是否为敌对
	static bool isEnemyOf(int npcRelation, int otherRelation)
	{
		if (npcRelation == nrNone)
		{
			return otherRelation != nrNone;
		}
		if (otherRelation == nrNone)
		{
			return npcRelation == nrFriendly || npcRelation == nrHostile;
		}
		if (npcRelation == nrFriendly && otherRelation == nrHostile)
		{
			return true;
		}
		if (npcRelation == nrHostile && otherRelation == nrFriendly)
		{
			return true;
		}
		return false;
	}
	static int getRelationOf(std::shared_ptr<GameElement> element, int fallbackLauncher = lkSelf);
	static bool canLauncherHitRelation(int launcherKind, int targetRelation)
	{
		if (launcherKind == lkNeutral)
		{
			return false;
		}
		switch (targetRelation)
		{
		case nrFriendly:
			return launcherKind == lkEnemy;
		case nrHostile:
			return launcherKind == lkSelf || launcherKind == lkFriend;
		case nrNone:
			return launcherKind == lkSelf || launcherKind == lkFriend || launcherKind == lkEnemy;
		default:
			return false;
		}
	}
	static bool canLauncherHitNPC(int launcherKind, std::shared_ptr<NPC> target);
	static int getAutomaticTargetPriority(int attackerKind, int attackerRelation,
		int attackerGroup, int attackerNoAutoAttackPlayer, int candidateKind,
		int candidateRelation, int candidateGroup, bool candidateIsPlayer)
	{
		if (NPC::isEnemyKindRelation(attackerKind, attackerRelation))
		{
			if (NPC::isEnemyKindRelation(candidateKind, candidateRelation))
			{
				return attackerGroup != candidateGroup ? 0 : INT_MAX;
			}
			if (attackerNoAutoAttackPlayer > 0)
			{
				return INT_MAX;
			}
			return candidateIsPlayer
				|| NPC::isFighterFriendKindRelation(candidateKind, candidateRelation)
				|| NPC::isNoneFighterKindRelation(candidateKind, candidateRelation)
				? 1 : INT_MAX;
		}

		if (NPC::isFighterFriendKindRelation(attackerKind, attackerRelation))
		{
			return NPC::isEnemyKindRelation(candidateKind, candidateRelation)
				|| NPC::isNoneFighterKindRelation(candidateKind, candidateRelation)
				? 0 : INT_MAX;
		}

		if (NPC::isNoneFighterKindRelation(attackerKind, attackerRelation))
		{
			return candidateIsPlayer
				|| (NPC::isFighterLikeKind(candidateKind)
					&& !NPC::isNoneFighterKindRelation(candidateKind, candidateRelation))
				? 0 : INT_MAX;
		}

		return INT_MAX;
	}
	static int getLauncherHitPriority(int launcherRelation, int launcherGroup, int targetRelation, int targetGroup)
	{
		if (targetRelation == nrFriendly)
		{
			return (launcherRelation == nrHostile || launcherRelation == nrNone) ? 0 : INT_MAX;
		}
		if (targetRelation == nrHostile)
		{
			if (launcherRelation == nrFriendly || launcherRelation == nrNone)
			{
				return 0;
			}
			return (launcherRelation == nrHostile && launcherGroup != targetGroup) ? 1 : INT_MAX;
		}
		if (targetRelation == nrNeutral)
		{
			return launcherRelation == nrNone ? 0 : INT_MAX;
		}
		if (targetRelation == nrNone)
		{
			return (launcherRelation == nrFriendly || launcherRelation == nrHostile) ? 0 : INT_MAX;
		}
		if (launcherRelation == nrNone)
		{
			return (targetRelation == nrFriendly || targetRelation == nrHostile || targetRelation == nrNeutral) ? 0 : INT_MAX;
		}
		return INT_MAX;
	}
	static int getLauncherHitPriority(int launcherKind, std::shared_ptr<NPC> target, std::shared_ptr<GameElement> launcher);
	static bool canLauncherHitNPC(int launcherKind, std::shared_ptr<NPC> target, std::shared_ptr<GameElement> launcher);
	static bool isFriendDeathRelationMatch(int finderKind, int finderRelation, int deadKind, int deadRelation)
	{
		const bool finderIsEnemy = finderKind == nkBattle && finderRelation == nrHostile;
		const bool deadIsEnemy = deadKind == nkBattle && deadRelation == nrHostile;
		const bool finderIsFighterFriend = (finderKind == nkBattle || finderKind == nkPartner) && finderRelation == nrFriendly;
		const bool deadIsFighterFriend = (deadKind == nkBattle || deadKind == nkPartner) && deadRelation == nrFriendly;
		const bool finderIsNoneFighter = finderKind == nkBattle && finderRelation == nrNone;
		const bool deadIsNoneFighter = deadKind == nkBattle && deadRelation == nrNone;
		return (finderIsEnemy && deadIsEnemy) || (finderIsFighterFriend && deadIsFighterFriend) || (finderIsNoneFighter && deadIsNoneFighter);
	}
	void addDeadNPC(std::shared_ptr<NPC> npc);
	std::shared_ptr<NPC> findFriendDeathAttacker(std::shared_ptr<NPC> finder, int maxTileDistance);

	// 更新所有npc的自动AI动作
	void npcAutoAction();

	// 对正在移动中的战斗NPC重新调度攻击决策，每步更新坐标后调用
	// 返回true表示动作已处理（攻击/后退/站立），调用方应return
	// 返回false表示需要继续移动（stepList已更新），调用方应继续走步逻辑
	bool scheduleBattleAction(std::shared_ptr<NPC> npc);

	bool drawNPCSelectedAlpha(Point cenTile, Point cenScreen, PointEx offset);
	void drawNPC(std::shared_ptr<NPC> npc, Point cenTile, Point cenScreen, PointEx offset, uint32_t colorStyle);
	void sortChildrenByY();

	//void draw(Point tile, Point cenTile, Point cenScreen, PointEx offset, uint32_t colorStyle);
	std::vector<std::shared_ptr<NPC>> npcList;
	void setPartnerPos(int x, int y, int dir);
	//清理不含partner的NPC
	void clearNPC(bool rebuildDataMap = true);
	//清理全部NPC，含partner
	void clearAllNPC(bool rebuildDataMap = true);

	void clearSelected();

	void deleteNPC(std::vector<int> idx);
	void deleteNPC(int idx);
	void deleteNPC(std::string nName);
	void deleteNPCFromOtherPlace(std::shared_ptr<NPC> npc);

	// 只从列表中移除
	void removeNPCOnlyFromList(std::shared_ptr<NPC> npc);

	void addNPC(std::string iniName, int x, int y, int dir);
	void addNPC(std::shared_ptr<NPC> npc);

	void freeResource();

	void clearActionImageList();
	void tryCleanActionImageList();

	_shared_imp loadActionImage(const std::string & imageName);
	_shared_imp loadActionImageDirect(const std::string & fullPath);
	std::map<std::string, _shared_imp> actionImageList;

	bool prepareLoad(
		const std::string& fileName,
		PreparedLoad& preparedLoad,
		bool allowIncompleteSectionList = false) const;
	bool prepareExactResourceBytes(
		const std::string& virtualPath,
		const std::vector<std::uint8_t>& bytes,
		PreparedLoad& preparedLoad) const;
	bool commitPreparedLoad(
		const PreparedLoad& preparedLoad,
		bool clearCurrent = true,
		const std::function<void()>& beforeMutation = {},
		const std::function<bool()>& preparationCheckpoint = {},
		bool randomOne = false);
	virtual bool load(
		const std::string& fileName,
		bool clearCurrent = true,
		const std::function<void()>& beforeMutation = {},
		const std::function<bool()>& preparationCheckpoint = {});
	bool validate(const std::string& fileName);
	bool loadExactResourceBytes(
		const std::string& virtualPath,
		const std::vector<std::uint8_t>& bytes,
		bool clearCurrent = true,
		const std::function<void()>& beforeMutation = {},
		const std::function<bool()>& preparationCheckpoint = {});
	virtual bool save(const std::string & fileName);
	virtual void onUpdate();
	virtual void onEvent();

protected:
	virtual bool shouldUpdateChild(PElement child) override;
};
