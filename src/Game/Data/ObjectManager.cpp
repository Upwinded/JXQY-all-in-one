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

bool ObjectManager::findObj(std::shared_ptr<Object> object)
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

std::shared_ptr<Object> ObjectManager::findObj(const std::string & name)
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

std::shared_ptr<Object> ObjectManager::findNearestScriptViewObj(Point pos, int radius)
{
	int distance = radius + 1;
	int tempIdx = -1;
	for (size_t i = 0; i < objectList.size(); i++)
	{
		auto tempObj = objectList[i];
		if (tempObj != nullptr && tempObj->scriptFile != "")
		{
			auto tempDistance = gm->map->calDistance(tempObj->position, pos);
			if (tempDistance < distance && gm->map->canView(pos, tempObj->position))
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

std::vector<std::shared_ptr<Object>> ObjectManager::findRadiusScriptViewObj(Point pos, int radius)
{
	std::vector<std::shared_ptr<Object>> ret;
	for (size_t i = 0; i < objectList.size(); ++i)
	{
		if (objectList[i] == nullptr) { continue; }

		auto tempDistance = Map::calDistance(pos, objectList[i]->position);

		if (objectList[i]->scriptFile != "" && gm->map->canView(pos, objectList[i]->position) && tempDistance <= radius)
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
			objectList[i]->drawAlpha(cenTile, cenScreen, offset);
			return true;
		}
	}
	return false;
}

void ObjectManager::drawOBJ(std::shared_ptr<Object> obj, Point cenTile, Point cenScreen, PointEx offset, uint32_t colorStyle)
{
	if (obj != nullptr)
	{
		obj->draw(cenTile, cenScreen, offset, colorStyle);
	}
}

void ObjectManager::deleteObjectFromOtherPlace(std::shared_ptr<Object> obj)
{
	removeChild(obj);
	tryCleanObjectImageList();
}

void ObjectManager::deleteObject(std::string nName)
{
	std::vector<std::shared_ptr<Object>> newList;
	newList.resize(0);
	for (size_t i = 0; i < objectList.size(); i++)
	{
		if (objectList[i] == nullptr || objectList[i]->objName == nName)
		{
			deleteObjectFromOtherPlace(objectList[i]);
			objectList[i] = nullptr;
		}
		else
		{
			newList.push_back(objectList[i]);
		}
	}
	objectList = newList;
	gm->map->createDataMap();
}

void ObjectManager::addObject(std::string iniName, int x, int y, int dir)
{
	auto obj = std::make_shared<Object>();
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
	gm->map->addObjectToDataMap(obj->position, obj);
}

void ObjectManager::clearBody()
{
	std::vector<std::shared_ptr<Object>> newList;
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
				deleteObjectFromOtherPlace(objectList[i]);
				objectList[i] = nullptr;
			}
		}
	}
	objectList = newList;
	tryCleanObjectImageList();
	gm->map->createDataMap();
}

void ObjectManager::clearObj()
{
	freeResource();
	gm->map->createDataMap();
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
	for (auto iter = objectImageList.begin(); iter != objectImageList.end(); iter++)
	{
		iter->second = nullptr;
	}
	objectImageList.clear();
}

void ObjectManager::tryCleanObjectImageList()
{
	auto iter = objectImageList.begin();
	while (iter != objectImageList.end())
	{
		if (iter->second.use_count() <= 1)
		{
			iter->second = nullptr;
			iter = objectImageList.erase(iter);
		}
		else
		{
			iter++;
		}
	}
}

_shared_imp ObjectManager::loadObjectImage(const std::string & imageName)
{
	if (imageName == "")
	{
		return nullptr;
	}
	auto img = objectImageList.find(imageName);
	if (img != objectImageList.end())
	{
		return img->second;
	}

	std::string tempName = OBJECT_RES_FOLDER + imageName;
	_shared_imp objImg = IMP::createIMPImage(tempName);
	objectImageList[imageName] = objImg;
	return objImg;

}

void ObjectManager::freeResource()
{
	removeAllChild();
	for (size_t i = 0; i < objectList.size(); i++)
	{
		if (objectList[i] != nullptr)
		{
			deleteObjectFromOtherPlace(objectList[i]);
			objectList[i] = nullptr;
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
	for (int i = 0; i < count; i++)
	{
		section = convert::formatString("OBJ%03d", i);
		auto obj = std::make_shared<Object>();
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
	
	INIReader ini;

	std::string section = "Head";
	ini.Set(section, "Map", GameManager::getInstance()->global.data.mapName);
	ini.SetInteger(section, "Count", objectList.size());

	for (size_t i = 0; i < objectList.size(); i++)
	{
		section = convert::formatString("OBJ%03d", i);
		objectList[i]->saveToIni(&ini, section);
	}
	ini.saveToFile(SaveFileManager::CurrentPath() + fileName);

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

void ObjectManager::onUpdate()
{

}
