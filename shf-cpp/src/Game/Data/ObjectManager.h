#pragma once
#include "Object.h"
#include <vector>
#include <deque>

struct ObjectImage
{
	std::string name = "";
	IMPImage * image = nullptr;
};

class ObjectManager:
	public Element
{
public:
	ObjectManager();
	~ObjectManager();

	int clickIndex = 0;

	bool findObj(Object * object);
	Object * findObj(const std::string & name);
	bool drawOBJSelectedAlpha(Point cenTile, Point cenScreen, PointEx offset);
	void drawOBJAlpha(int index, Point cenTile, Point cenScreen, PointEx offset);
	void drawOBJ(int index, Point cenTile, Point cenScreen, PointEx offset);

	std::vector<Object *> objectList = {};

	void deleteObject(std::string nName);
	void addObject(std::string iniName, int x, int y, int dir);
	void clearBody();
	void clearObj();
	void checkDamage();

	void clearSelected();

	std::vector<ObjectImage> objectImageList = {};
	void clearObjectImageList();
	IMPImage * loadObjectImage(const std::string & imageName);

	virtual void freeResource();
	virtual void load(const std::string & fileName);
	virtual void save(const std::string & fileName);
	virtual void onEvent();

};

