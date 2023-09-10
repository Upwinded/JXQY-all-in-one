#include "ObjectManager.h"
#include "../GameManager/GameManager.h"


ObjectManager::ObjectManager()
{
	priority = epGameManager;
	objectList.resize(0);
	needArrangeChild = false;
	canDraw = false;
}

ObjectManager::~ObjectManager()
{
	freeResource();
}

void ObjectManager::removeObj(Object * obj)
{
	if (obj == nullptr) { return; }
	for (int i = 0; i < objectList.size(); ++i) {
		if (objectList[i] == obj)
		{
			objectList.erase(objectList.begin() + i);
			removeChild(obj);
			break;
		}
	}
}

bool ObjectManager::findObj(Object * object)
{
	if (object == nullptr)
	{
		return false;
	}
	for (size_t i = 0; i < objectList.size(); i++)
	{
		if (objectList[i] == object)
		{
			return true;
		}
	}
	return false;
}

Object * ObjectManager::findObj(const std::string & name)
{
	for (size_t i = 0; i < objectList.size(); i++)
	{
		if (objectList[i] != nullptr && objectList[i]->objName == name)
		{
			return objectList[i];
		}
	}
	return nullptr;
}

Object * ObjectManager::findNearestScriptViewObj(Point pos, int radius)
{
	int distance = radius + 1;
	int tempIdx = -1;
	for (size_t i = 0; i < objectList.size(); i++)
	{
		auto tempObj = objectList[i];
		if (tempObj != nullptr && tempObj->scriptFile != "")
		{
			auto tempDistance = gm->map.calDistance(tempObj->position, pos);
			if (tempDistance < distance && gm->map.canView(pos, tempObj->position))
			{
				distance = tempDistance;
				tempIdx = i;
			}
		}
	}
	if (tempIdx >= 0)
	{
		return objectList[tempIdx];
	}
	else
	{
		return nullptr;
	}
}

std::vector<Object *> ObjectManager::findRadiusScriptViewObj(Point pos, int radius)
{
	std::vector<Object *> ret;
	for (int i = 0; i < objectList.size(); ++i)
	{
		if (objectList[i] == nullptr) { continue; }

		int tempDistance = Map::calDistance(pos, objectList[i]->position);

		if (objectList[i]->scriptFile != "" && gm->map.canView(pos, objectList[i]->position) && tempDistance <= radius)
		{
			ret.push_back(objectList[i]);
		}
	}
	return ret;
}

bool ObjectManager::drawOBJSelectedAlpha(Point cenTile, Point cenScreen, PointEx offset)
{
	for (size_t i = 0; i < objectList.size(); i++)
	{
		if (objectList[i] != nullptr && objectList[i]->selecting)
		{
			drawOBJAlpha(i, cenTile, cenScreen, offset);
			return true;
		}
	}
	return false;
}

void ObjectManager::drawOBJAlpha(int index, Point cenTile, Point cenScreen, PointEx offset)
{
	if (index < 0 || index >= (int)objectList.size())
	{
		return;
	}
	int i = index;
	if (objectList[i] != nullptr)
	{
		objectList[i]->drawAlpha(cenTile, cenScreen, offset);
	}
}

void ObjectManager::drawOBJ(int index, Point cenTile, Point cenScreen, PointEx offset)
{
	if (index < 0 || index >= (int)objectList.size())
	{
		return;
	}
	int i = index;
	if (objectList[i] != nullptr)
	{
		objectList[i]->draw(cenTile, cenScreen, offset);
	}
}

void ObjectManager::deleteObject(std::string nName)
{
	std::vector<Object*> newList, deleteList;
	newList.resize(0);
	for (size_t i = 0; i < objectList.size(); i++)
	{
		if (objectList[i] == nullptr || objectList[i]->objName == nName)
		{
			if (objectList[i] != nullptr)
			{
				deleteList.push_back(objectList[i]);
			}
			objectList[i] = nullptr;
		}
		else
		{
			newList.push_back(objectList[i]);
		}
	}
	for (size_t i = 0; i < deleteList.size(); i++)
	{
		delete deleteList[i];
	}
	objectList = newList;
	gm->map.createDataMap();
}

void ObjectManager::addObject(std::string iniName, int x, int y, int dir)
{
	auto obj = new Object;
	obj->objIndex = objectList.size();
	std::string iniN = OBJECT_INI_FOLDER + iniName;
	std::unique_ptr<char[]> s;
	int len = PakFile::readFile(iniN, s);
	if (s != nullptr && len > 0)
	{
		INIReader ini(s);
		obj->initFromIni(&ini, "Init");
	}
	obj->position.x = x;
	obj->position.y = y;
	obj->direction = dir;
    objectList.push_back(obj);
	addChild(obj);
	gm->map.addObjectToDataMap(obj->position, obj->objIndex);
}

void ObjectManager::clearBody()
{
	std::vector<Object *> newList, deleteList;
	for (size_t i = 0; i < objectList.size(); i++)
	{
		if (objectList[i] != nullptr)
		{
			if (objectList[i]->kind != okBody)
			{
				newList.push_back(objectList[i]);
			}
			else
			{
				deleteList.push_back(objectList[i]);
			}
		}
	}
	for (size_t i = 0; i < deleteList.size(); i++)
	{
		delete deleteList[i];
	}
	objectList = newList;
	gm->map.createDataMap();
}

void ObjectManager::clearObj()
{
	freeResource();
	gm->map.createDataMap();
}

void ObjectManager::checkDamage()
{
	if (gm->inEvent)
	{
		return;
	}
}

void ObjectManager::clearSelected()
{
	for (size_t i = 0; i < objectList.size(); i++)
	{
		if (objectList[i] != nullptr && objectList[i]->selecting)
		{
			objectList[i]->selecting = false;
		}
	}
}

void ObjectManager::clearObjectImageList()
{
	for (size_t i = 0; i < objectImageList.size(); i++)
	{
		if (objectImageList[i].image != nullptr)
		{
			IMP::clearIMPImage(objectImageList[i].image);
			//delete objectImageList[i].image;
			objectImageList[i].image = nullptr;
		}
	}
	objectImageList.resize(0);
}

_shared_imp ObjectManager::loadObjectImage(const std::string & imageName)
{
	if (imageName == "")
	{
		return nullptr;
	}
	for (size_t i = 0; i < objectImageList.size(); i++)
	{
		if (objectImageList[i].name == imageName)
		{
			return objectImageList[i].image;
		}
	}
	ObjectImage objImg;
	objImg.name = imageName;
	std::string tempName = OBJECT_RES_FOLDER + imageName;
	objImg.image = IMP::createIMPImage(tempName);
	objectImageList.push_back(objImg);
	return objImg.image;
}

void ObjectManager::freeResource()
{
	removeAllChild();
	auto deleteList = objectList;
	for (size_t i = 0; i < deleteList.size(); i++)
	{
		if (deleteList[i] != nullptr)
		{
			delete deleteList[i];
		}
	}
	objectList.resize(0);
	clearObjectImageList();
}

void ObjectManager::load(const std::string & fileName)
{
	freeResource();
	std::string iniName = SAVE_CURRENT_FOLDER + fileName;
	INIReader ini(iniName);
	std::string section = "Head";
	int count = ini.GetInteger(section, "Count", 0);
	if (count <= 0)
	{
		return;
	}

	for (size_t i = 0; i < count; i++)
	{
		section = convert::formatString("OBJ%03d", i);
		auto obj = new Object;
		obj->initFromIni(&ini, section);
		addChild(obj);
		objectList.push_back(obj);
	}
}

void ObjectManager::save(const std::string & fileName)
{
	if (fileName == "")
	{
		return;
	}
	
	INIReader * ini = new INIReader();

	std::string section = "Head";
	ini->Set(section, "Map", GameManager::getInstance()->global.data.mapName);
	ini->SetInteger(section, "Count", objectList.size());

	for (size_t i = 0; i < objectList.size(); i++)
	{
		section = convert::formatString("OBJ%03d", i);
		objectList[i]->saveToIni(ini, section);
	}
	std::string iniName = SAVE_CURRENT_FOLDER + fileName;
	ini->saveToFile(iniName);
	delete ini;
	ini = nullptr;
	SaveFileManager::AppendFile(fileName);
}


void ObjectManager::onEvent()
{
	clickIndex = -1;
	for (size_t i = 0; i < objectList.size(); i++)
	{
		if (objectList[i]->selecting)
		{
			clickIndex = i;
			break;
		}
		/*unsigned int ret = objectList[i]->getResult();
		if (ret && erMouseLDown)
		{
			result = erMouseLDown;
			clickIndex = i;
			break;
		}
		else if (ret && erMouseRDown)
		{
			result = erMouseRDown;
			clickIndex = i;
			break;
		}*/
	}
}