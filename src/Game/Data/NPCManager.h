#pragma once
#include "NPC.h"
#include <vector>
#include <deque>
#include "../GameManager/SaveFileManager.h"

struct ActionImage
{
	std::string name = "";
	_shared_imp image = nullptr;
};

class NPCManager:
	public Element
{
public:
	NPCManager();
	virtual ~NPCManager();
private:
    NPC * player = nullptr;
public:
    void setPlayer(NPC * nowPlayer);
	int clickIndex = 0;

	void standAll();
	int findNPCIndex(NPC * npc);
	bool findNPC(NPC * npc);
	NPC* findPlayerNPC();

	std::vector<NPC *> findNPC(const std::string & npcName);
	std::vector<NPC *> findNPC(int launcherKind);
	std::vector<NPC *> findNPC(Point pos, int radius);
	std::vector<NPC *> findNPC(int launcherKind, Point pos, int radius);
	NPC * findNearestNPC(int launcherKind, Point pos, int radius);
	NPC * findNearestViewNPC(int launcherKind, Point pos, int radius);
	NPC * findNearestScriptViewNPC(Point pos, int radius);
	std::vector<NPC *> findRadiusScriptViewNPC(Point pos, int radius);

	//更新所有npc的自动AI动作
	void npcAutoAction();

	bool drawNPCSelectedAlpha(Point cenTile, Point cenScreen, PointEx offset);
	void drawNPCAlpha(int index, Point cenTile, Point cenScreen, PointEx offset);
	void drawNPC(int index, Point cenTile, Point cenScreen, PointEx offset);

	void draw(Point tile, Point cenTile, Point cenScreen, PointEx offset);
	std::vector<NPC *> npcList;
	void setPartnerPos(int x, int y, int dir);
	//清理不含partner的NPC
	void clearNPC();
	//清理全部NPC，含partner
	void clearAllNPC();

	void clearSelected();

	void deleteNPC(std::vector<int> idx);
	void deleteNPC(int idx);
	void deleteNPC(std::string nName);

	void removeNPC(NPC * npc);
	void addNPC(std::string iniName, int x, int y, int dir);
	void addNPC(NPC * npc);

	void freeResource();

	void clearActionImageList();

	_shared_imp loadActionImage(const std::string & imageName);
	std::vector<ActionImage> actionImageList;

	virtual void load(const std::string & fileName);
	virtual void save(const std::string & fileName);
	virtual void onUpdate();
	virtual void onEvent();
};

