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

bool ObjectManager::findObj(Object * object)
{
	if (object == NULL)
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
		if (objectList[i] != NULL && objectList[i]->objName == name)
		{
			return objectList[i];
		}
	}
	return nullptr;
}

bool ObjectManager::drawOBJSelectedAlpha(Point cenTile, Point cenScreen, PointEx offset)
{
	for (size_t i = 0; i < objectList.size(); i++)
	{
		if (objectList[i] != NULL && objectList[i]->selected)
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
	if (objectList[i] != NULL)
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
	if (objectList[i] != NULL)
	{
		objectList[i]->draw(cenTile, cenScreen, offset);
	}
}

void ObjectManager::deleteObject(std::string nName)
{
	std::vector<Object *> newList = {};
	newList.resize(0);
	for (size_t i = 0; i < objectList.size(); i++)
	{
		if (objectList[i] == NULL || objectList[i]->objName == nName)
		{
			if (objectList[i] != NULL)
			{
				removeChild(objectList[i]);
				delete objectList[i];
			}
			objectList[i] = NULL;
		}
		else
		{
			newList.resize(newList.size() + 1);
			newList[newList.size() - 1] = objectList[i];
		}
	}
	objectList = newList;
	gm->map.createDataMap();
}

void ObjectManager::addObject(std::string iniName, int x, int y, int dir)
{
	objectList.resize(objectList.size() + 1);
	objectList[objectList.size() - 1] = new Object;
	objectList[objectList.size() - 1]->objIndex = objectList.size() - 1;
	std::string iniN = OBJECT_INI_FOLDER + iniName;
	char * s = NULL;
	int len = PakFile::readFile(iniN, &s);
	if (s != NULL && len > 0)
	{
		INIReader * ini = new INIReader(s);
		delete[] s;
		objectList[objectList.size() - 1]->initFromIni(ini, "Init");
		delete ini;
	}
	objectList[objectList.size() - 1]->position.x = x;
	objectList[objectList.size() - 1]->position.y = y;
	objectList[objectList.size() - 1]->direction = dir;
	addChild(objectList[objectList.size() - 1]);
	gm->map.addObjectToDtataMap(objectList[objectList.size() - 1]->position, objectList.size() - 1);
}

void ObjectManager::clearBody()
{
	std::vector<Object *> newList;
	for (size_t i = 0; i < objectList.size(); i++)
	{
		if (objectList[i]->kind != okBody)
		{
			newList.push_back(objectList[i]);
		}
		else
		{
			removeChild(objectList[i]);
			if (objectList[i] != NULL)
			{
				delete objectList[i];
				objectList[i] = NULL;
			}
		}
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
		if (objectList[i] != NULL && objectList[i]->selected)
		{
			objectList[i]->selected = false;
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
			delete objectImageList[i].image;
			objectImageList[i].image = nullptr;
		}
	}
	objectImageList.resize(0);
}

IMPImage * ObjectManager::loadObjectImage(const std::string & imageName)
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
	for (size_t i = 0; i < objectList.size(); i++)
	{
		if (objectList[i] != NULL)
		{
			delete objectList[i];
			objectList[i] = NULL;
		}
	}
	objectList.resize(0);
	clearObjectImageList();
}

void ObjectManager::load(const std::string & fileName)
{
	freeResource();
	std::string iniName = DEFAULT_FOLDER + fileName;
	INIReader * ini = new INIReader(iniName);
	std::string section = "Head";
	int count = ini->GetInteger(section, "Count", 0);
	if (count <= 0)
	{
		delete ini;
		ini = NULL;
		return;
	}
	objectList.resize(count);
	for (size_t i = 0; i < objectList.size(); i++)
	{
		section = convert::formatString("OBJ%03d", i);
		objectList[i] = new Object;
		objectList[i]->initFromIni(ini, section);
		addChild(objectList[i]);
	}
	delete ini;
	ini = NULL;
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
	std::string iniName = DEFAULT_FOLDER + fileName;
	ini->saveToFile(iniName);
	delete ini;
	ini = NULL;
}


void ObjectManager::onEvent()
{
	clickIndex = -1;
	for (size_t i = 0; i < objectList.size(); i++)
	{
		if (objectList[i]->selected)
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