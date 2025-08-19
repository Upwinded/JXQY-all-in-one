#pragma once
#include "Object.h"
#include <vector>
#include <deque>

class ObjectManager:
	public Element
{
public:
	ObjectManager();
	virtual ~ObjectManager();

	int clickIndex = 0;

	bool findObj(std::shared_ptr<Object> object);
	std::shared_ptr<Object> findObj(const std::string & name);
	std::shared_ptr<Object> findNearestScriptViewObj(Point pos, int radius);
	std::vector<std::shared_ptr<Object>> findRadiusScriptViewObj(Point pos, int radius);
	bool drawOBJSelectedAlpha(Point cenTile, Point cenScreen, PointEx offset);
	void drawOBJ(std::shared_ptr<Object> obj, Point cenTile, Point cenScreen, PointEx offset, uint32_t colorStyle);

	std::vector<std::shared_ptr<Object>> objectList;

	void deleteObject(std::string nName);
	void deleteObjectFromOtherPlace(std::shared_ptr<Object> obj);
	void addObject(std::string iniName, int x, int y, int dir);
	void clearBody();
	void clearObj();
	void checkDamage();

	
	void clearSelected();

	std::map<std::string, _shared_imp> objectImageList;
	void clearObjectImageList();
	void tryCleanObjectImageList();
	_shared_imp loadObjectImage(const std::string & imageName);

	void freeResource();
	virtual void load(const std::string & fileName);
	virtual void save(const std::string & fileName);
	virtual void onEvent();
	virtual void onUpdate();

};

