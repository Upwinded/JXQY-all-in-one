#include "ObjectManager.h"
#include "../../Engine/Engine.h"
#include "ObjectPersistence.h"
#include "Map.h"
#include "TimeStopUpdateGate.h"
#include "../GameManager/GameManager.h"
#include "../../File/File.h"

#include <algorithm>
#include <cstring>
#include <utility>


bool PreparedObjectLoad::isPrepared() const noexcept
{
	return reader != nullptr;
}

std::size_t PreparedObjectLoad::objectCount() const noexcept
{
	return reader != nullptr && count > 0
		? static_cast<std::size_t>(count)
		: 0;
}

ObjectManager::ObjectManager()
{
	setPriority(epGameManager);
	objectList.resize(0);
	needArrangeChild = false;
	canDraw = false;
}

ObjectManager::~ObjectManager()
{
	freeResource();
}

bool ObjectManager::shouldUpdateChild(PElement child)
{
	(void)child;
	const bool hasActiveTimeStopper = gm != nullptr
		&& gm->effectManager != nullptr
		&& gm->effectManager->hasActiveTimeStopper();
	return shouldUpdateObjectManagerChildDuringTimeStop(hasActiveTimeStopper);
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
		if (tempObj != nullptr && tempObj->canSelectForInteraction())
		{
			auto tempDistance = gm->map->calDistance(tempObj->position, pos);
			if (tempDistance < distance && gm->map->canSee(pos, tempObj->position))
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

		if (objectList[i]->canSelectForInteraction() && gm->map->canSee(pos, objectList[i]->position) && tempDistance <= radius)
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
	if (obj == nullptr)
	{
		return;
	}
	obj->removeFromDataMap();
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
}

void ObjectManager::deleteObject(std::shared_ptr<Object> object)
{
	if (object == nullptr)
	{
		return;
	}

	auto iter = std::remove(objectList.begin(), objectList.end(), object);
	if (iter == objectList.end())
	{
		return;
	}

	deleteObjectFromOtherPlace(object);
	objectList.erase(iter, objectList.end());
}

std::shared_ptr<Object> ObjectManager::addObject(std::string iniName, int x, int y, int dir, PointEx objOffset)
{
	if (iniName.empty() || objectList.size() >= ObjectPersistence::MaximumRuntimeObjectCount)
	{
		return nullptr;
	}

	auto obj = std::make_shared<Object>();
	std::string iniN = OBJECT_INI_FOLDER + iniName;
	std::unique_ptr<char[]> s;
	int len = 0;
	if (!File::readFile(iniN, s, len, ObjectPersistence::MaximumObjectFileBytes)
		|| s == nullptr || len <= 0)
	{
		GameLog::write("ObjectManager: object ini missing or too large %s\n", iniN.c_str());
		return nullptr;
	}
	INIReader ini(s);
	if (ini.ParseError() != 0)
	{
		GameLog::write("ObjectManager: invalid object ini %s\n", iniN.c_str());
		return nullptr;
	}
	obj->initFromIni(&ini, "Init");
	obj->setPosition({ x, y });
	obj->direction = dir;
	obj->setOffset(objOffset);
    objectList.push_back(obj);
	addChild(obj);
	return obj;
}

std::vector<std::shared_ptr<Object>> ObjectManager::takeBodiesInRadius(Point pos, int radius)
{
	std::vector<std::shared_ptr<Object>> ret;
	std::vector<std::shared_ptr<Object>> newList;
	for (size_t i = 0; i < objectList.size(); i++)
	{
		auto obj = objectList[i];
		if (obj == nullptr)
		{
			continue;
		}
		if (obj->kind == okBody && Map::calDistance(pos, obj->getPosition()) <= radius)
		{
			ret.push_back(obj);
			deleteObjectFromOtherPlace(obj);
			objectList[i] = nullptr;
		}
		else
		{
			newList.push_back(obj);
		}
	}
	objectList = newList;
	tryCleanObjectImageList();
	return ret;
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
}

void ObjectManager::clearObj()
{
	freeResource();
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
	if (imageName.empty())
	{
		return nullptr;
	}
	auto img = objectImageList.find(imageName);
	if (img != objectImageList.end())
	{
		return img->second;
	}

	_shared_imp objImg = nullptr;
	for (const auto& candidate : buildObjectImageResourceCandidates(imageName))
	{
		objImg = IMP::createIMPImage(candidate);
		if (objImg != nullptr)
		{
			break;
		}
	}
	objectImageList[imageName] = objImg;
	return objImg;

}

void ObjectManager::freeResource()
{
	releaseManagedObjects(true);
}

void ObjectManager::releaseManagedObjects(bool clearObjectImages)
{
	std::map<std::string, _shared_imp> retainedObjectImages;
	retainedObjectImages.swap(objectImageList);
	for (size_t i = 0; i < objectList.size(); i++)
	{
		if (objectList[i] != nullptr)
		{
			deleteObjectFromOtherPlace(objectList[i]);
			objectList[i] = nullptr;
		}
	}
	removeAllChild();
	objectList.resize(0);
	if (!clearObjectImages)
	{
		objectImageList.swap(retainedObjectImages);
	}
}

bool ObjectManager::validate(const std::string& fileName)
{
	std::unique_ptr<char[]> data;
	int length = 0;
	std::string loadedPath;
	if (!SaveFileManager::ReadNpcObjFile(
			fileName,
			data,
			length,
			&loadedPath,
			ObjectPersistence::MaximumObjectFileBytes))
	{
		GameLog::write(
			"ObjectManager: object preflight read failed %s\n",
			fileName.c_str());
		return false;
	}

	INIReader ini(data);
	if (ini.ParseError() != 0)
	{
		GameLog::write(
			"ObjectManager: object preflight parse failed %s\n",
			loadedPath.c_str());
		return false;
	}
	int count = 0;
	if (!ObjectPersistence::readCount(ini, count))
	{
		return false;
	}
	for (int index = 0; index < count; ++index)
	{
		if (!ini.HasSection(
				convert::formatString("OBJ%03d", index)))
		{
			return false;
		}
	}
	return true;
}

bool ObjectManager::load(
	const std::string& fileName,
	const std::function<void()>& beforeMutation,
	const std::function<bool()>& preparationCheckpoint)
{
	PreparedObjectLoad preparedLoad;
	if (!prepareLoad(
			fileName,
			preparedLoad))
	{
		return false;
	}
	return commitPreparedLoad(
		std::move(preparedLoad),
		beforeMutation,
		preparationCheckpoint);
}

bool ObjectManager::prepareLoad(
	const std::string& fileName,
	PreparedObjectLoad& preparedLoad,
	bool allowIncompleteSectionList) const
{
	preparedLoad = PreparedObjectLoad();

	std::unique_ptr<char[]> data;
	int len = 0;
	std::string loadedPath;
	if (!SaveFileManager::ReadNpcObjFile(fileName, data, len, &loadedPath,
		ObjectPersistence::MaximumObjectFileBytes))
	{
		GameLog::write("ObjectManager: unable to read object list %s\n", fileName.c_str());
		return false;
	}

	auto reader = std::make_shared<INIReader>(data);
	if (reader->ParseError() != 0)
	{
		GameLog::write("ObjectManager: invalid object list %s\n", loadedPath.c_str());
		return false;
	}

	int declaredCount = 0;
	const bool declaredCountIsValid =
		ObjectPersistence::readCount(*reader, declaredCount);
	if (!declaredCountIsValid &&
		!allowIncompleteSectionList)
	{
		GameLog::write("ObjectManager: invalid object count in %s\n", loadedPath.c_str());
		return false;
	}

	const int sectionLimit = declaredCountIsValid
		? declaredCount
		: ObjectPersistence::MaximumObjectCount;
	int count = 0;
	for (int index = 0; index < sectionLimit; ++index)
	{
		const std::string section =
			convert::formatString("OBJ%03d", index);
		if (!reader->HasSection(section))
		{
			if (allowIncompleteSectionList)
			{
				break;
			}
			GameLog::write("ObjectManager: missing section %s in %s\n",
				section.c_str(), loadedPath.c_str());
			return false;
		}
		count = index + 1;
	}
	if (allowIncompleteSectionList &&
		(!declaredCountIsValid || count != declaredCount))
	{
		GameLog::write(
			"ObjectManager: compatible object list %s loads %d contiguous sections\n",
			loadedPath.c_str(),
			count);
	}

	preparedLoad.reader = std::move(reader);
	preparedLoad.count = count;
	return true;
}

bool ObjectManager::prepareExactResourceBytes(
	const std::string& virtualPath,
	const std::vector<std::uint8_t>& bytes,
	PreparedObjectLoad& preparedLoad) const
{
	preparedLoad = PreparedObjectLoad();
	if (virtualPath.empty() ||
		bytes.empty() ||
		bytes.size() > static_cast<std::size_t>(
			ObjectPersistence::MaximumObjectFileBytes) ||
		std::find(bytes.begin(), bytes.end(), std::uint8_t{ 0 }) !=
			bytes.end())
	{
		return false;
	}

	auto data = std::make_unique<char[]>(bytes.size() + 1);
	std::memcpy(data.get(), bytes.data(), bytes.size());
	data[bytes.size()] = '\0';
	auto reader = std::make_shared<INIReader>(data);
	if (reader->ParseError() != 0)
	{
		GameLog::write(
			"ObjectManager: invalid exact object list %s\n",
			virtualPath.c_str());
		return false;
	}

	int count = 0;
	if (!ObjectPersistence::readCount(*reader, count))
	{
		GameLog::write(
			"ObjectManager: invalid exact object count in %s\n",
			virtualPath.c_str());
		return false;
	}
	for (int index = 0; index < count; ++index)
	{
		const std::string section =
			convert::formatString("OBJ%03d", index);
		if (!reader->HasSection(section))
		{
			GameLog::write(
				"ObjectManager: missing section %s in exact object list %s\n",
				section.c_str(),
				virtualPath.c_str());
			return false;
		}
	}

	preparedLoad.reader = std::move(reader);
	preparedLoad.count = count;
	return true;
}

bool ObjectManager::commitPreparedLoad(
	PreparedObjectLoad&& preparedLoad,
	const std::function<void()>& beforeMutation,
	const std::function<bool()>& preparationCheckpoint)
{
	if (!preparedLoad.isPrepared() ||
		engine == nullptr)
	{
		return false;
	}
	if (!engine->isMainThread())
	{
		GameLog::write(
			"ObjectManager: prepared object commit must run on the SDL main thread\n");
		return false;
	}
	std::shared_ptr<INIReader> reader =
		std::move(preparedLoad.reader);
	const int count = preparedLoad.count;
	preparedLoad.count = 0;

	std::vector<std::shared_ptr<Object>> loadedObjects;
	loadedObjects.reserve(static_cast<std::size_t>(count));
	for (int i = 0; i < count; ++i)
	{
		if (preparationCheckpoint &&
			!preparationCheckpoint())
		{
			return false;
		}
		const std::string section =
			convert::formatString("OBJ%03d", i);
		auto object = std::make_shared<Object>();
		object->initFromIni(reader.get(), section);
		loadedObjects.push_back(std::move(object));
	}
	if (preparationCheckpoint &&
		!preparationCheckpoint())
	{
		return false;
	}

	if (beforeMutation)
	{
		beforeMutation();
	}
	releaseManagedObjects(false);
	for (const auto& obj : loadedObjects)
	{
		addChild(obj);
		objectList.push_back(obj);
		if (gm != nullptr && gm->map != nullptr)
		{
			gm->map->addObjectToDataMap(obj->getPosition(), obj);
		}
	}
	tryCleanObjectImageList();
	return true;
}

bool ObjectManager::loadExactResourceBytes(
	const std::string& virtualPath,
	const std::vector<std::uint8_t>& bytes,
	const std::function<void()>& beforeMutation,
	const std::function<bool()>& preparationCheckpoint)
{
	if (gm == nullptr ||
		gm->map == nullptr)
	{
		return false;
	}
	PreparedObjectLoad preparedLoad;
	return prepareExactResourceBytes(
			virtualPath,
			bytes,
			preparedLoad) &&
		commitPreparedLoad(
			std::move(preparedLoad),
			beforeMutation,
			preparationCheckpoint);
}

bool ObjectManager::save(const std::string & fileName)
{
	if (fileName.empty())
	{
		return true;
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
	const bool saved = ini.saveToFile(SaveFileManager::CurrentPath() + fileName);

	SaveFileManager::AppendFile(fileName);
	return saved;
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
