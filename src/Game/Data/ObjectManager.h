#pragma once
#include "Object.h"
#include <vector>
#include <deque>

struct ObjectImage
{
	std::string name = "";
	_shared_imp image = nullptr;
};

class ObjectManager:
	public Element
{
public:
	ObjectManager();
	virtual ~ObjectManager();

	int clickIndex = 0;


	bool findObj(Object * object);
	Object * findObj(const std::string & name);
	Object * findNearestScriptViewObj(Point pos, int radius);
	std::vector<Object *> findRadiusScriptViewObj(Point pos, int radius);
	bool drawOBJSelectedAlpha(Point cenTile, Point cenScreen, PointEx offset);
	void drawOBJAlpha(int index, Point cenTile, Point cenScreen, PointEx offset);
	void drawOBJ(int index, Point cenTile, Point cenScreen, PointEx offset);

	std::vector<Object *> objectList;

	void removeObj(Object* obj);
	void deleteObject(std::string nName);
	void addObject(std::string iniName, int x, int y, int dir);
	void clearBody();
	void clearObj();
	void checkDamage();

	void clearSelected();

	std::vector<ObjectImage> objectImageList;
	void clearObjectImageList();
	_shared_imp loadObjectImage(const std::string & imageName);

	void freeResource();
	virtual void load(const std::string & fileName);
	virtual void save(const std::string & fileName);
	virtual void onEvent();

};

