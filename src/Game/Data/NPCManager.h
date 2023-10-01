#pragma once
#include "NPC.h"
#include <vector>
#include <deque>
#include "../GameManager/SaveFileManager.h"

class NPCManager:
	public Element
{
public:
	NPCManager();
	virtual ~NPCManager();
private:
    std::shared_ptr<NPC> player = nullptr;
public:
    void setPlayer(std::shared_ptr<NPC> nowPlayer);
	int clickIndex = 0;

	void standAll();
	int findNPCIndex(std::shared_ptr<NPC> npc);
	bool findNPC(std::shared_ptr<NPC> npc);
	std::shared_ptr<NPC> findPlayerNPC();

	std::vector<std::shared_ptr<NPC>> findNPC(const std::string & npcName);
	std::vector<std::shared_ptr<NPC>> findNPC(int launcherKind);
	std::vector<std::shared_ptr<NPC>> findNPC(Point pos, int radius);
	std::vector<std::shared_ptr<NPC>> findNPC(int launcherKind, Point pos, int radius);
	std::shared_ptr<NPC> findNearestNPC(int launcherKind, Point pos, int radius);
	std::shared_ptr<NPC> findNearestViewNPC(int launcherKind, Point pos, int radius);
	std::shared_ptr<NPC> findNearestScriptViewNPC(Point pos, int radius);
	std::vector<std::shared_ptr<NPC>> findRadiusScriptViewNPC(Point pos, int radius);



	// 更新所有npc的自动AI动作
	void npcAutoAction();

	bool drawNPCSelectedAlpha(Point cenTile, Point cenScreen, PointEx offset);
	void drawNPCAlpha(int index, Point cenTile, Point cenScreen, PointEx offset);
	void drawNPC(int index, Point cenTile, Point cenScreen, PointEx offset);

	void draw(Point tile, Point cenTile, Point cenScreen, PointEx offset);
	std::vector<std::shared_ptr<NPC>> npcList;
	void setPartnerPos(int x, int y, int dir);
	//清理不含partner的NPC
	void clearNPC();
	//清理全部NPC，含partner
	void clearAllNPC();

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

	_shared_imp loadActionImage(const std::string & imageName);
	std::map<std::string, _shared_imp> actionImageList;

	virtual void load(const std::string & fileName);
	virtual void save(const std::string & fileName);
	virtual void onUpdate();
	virtual void onEvent();
};

