#pragma once
#include "NPC.h"
#include <vector>
#include <deque>

struct ActionImage
{
	std::string name = "";
	IMPImage * image = nullptr;
};

class NPCManager:
	public Element
{
public:
	NPCManager();
	~NPCManager();

	int clickIndex = 0;

	void standAll();
	int findNPCIndex(NPC * npc);
	bool findNPC(NPC * npc);

	std::vector<NPC *> findNPC(const std::string & npcName);
	std::vector<NPC *> findNPC(int launcherKind);
	std::vector<NPC *> findNPC(int launcherKind, Point pos, int radius);
	NPC * findNearestNPC(int launcherKind, Point pos, int radius);
	NPC * findNearestViewNPC(int launcherKind, Point pos, int radius);

	//更新所有npc的自动AI动作
	void npcAutoAction();

	bool drawNPCSelectedAlpha(Point cenTile, Point cenScreen, PointEx offset);
	void drawNPCAlpha(int index, Point cenTile, Point cenScreen, PointEx offset);
	void drawNPC(int index, Point cenTile, Point cenScreen, PointEx offset);

	void draw(Point tile, Point cenTile, Point cenScreen, PointEx offset);
	std::vector<NPC *> npcList = {};
	void setPartnerPos(int x, int y, int dir);
	void clearNPC();

	void clearSelected();

	void deleteNPC(std::vector<int> idx);
	void deleteNPC(int idx);
	void deleteNPC(std::string nName);
	void addNPC(std::string iniName, int x, int y, int dir);
	void addNPC(NPC * npc);
	virtual void freeResource();

	void clearActionImageList();

	IMPImage * loadActionImage(const std::string & imageName);
	std::vector<ActionImage> actionImageList = {};

	virtual void load(const std::string & fileName);
	virtual void save(const std::string & fileName);
	virtual void onUpdate();
	virtual void onEvent();
};

