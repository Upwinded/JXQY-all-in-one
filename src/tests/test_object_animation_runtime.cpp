#include "../File/File.h"
#include "../Game/Data/Object.h"
#include "../Game/GameManager/GameManager.h"
#include "../Weather/Weather.h"
#include "TestTemporaryDirectory.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <limits>
#include <utility>
#include <vector>
#include <cstdint>
#include <cstring>

namespace
{
bool check(bool condition, const char* message)
{
	if (!condition)
	{
		std::cerr << "FAILED: " << message << '\n';
	}
	return condition;
}

std::string normalizePath(std::string path)
{
	for (char& character : path)
	{
		if (character == '\\')
		{
			character = '/';
		}
	}
	return path;
}

bool writeTextFile(const std::filesystem::path& path, const std::string& content)
{
	std::error_code errorCode;
	std::filesystem::create_directories(path.parent_path(), errorCode);
	std::ofstream output(path, std::ios::binary);
	if (!output)
	{
		return false;
	}
	output << content;
	return true;
}

bool writeMinimalWave(const std::filesystem::path& path)
{
	constexpr uint32_t sampleBytes = 1600;
	std::vector<unsigned char> wave(44 + sampleBytes, 0);
	auto write16 = [&](size_t offset, uint16_t value) {
		wave[offset] = static_cast<unsigned char>(value & 0xFF);
		wave[offset + 1] = static_cast<unsigned char>(value >> 8);
	};
	auto write32 = [&](size_t offset, uint32_t value) {
		for (size_t byteIndex = 0; byteIndex < 4; byteIndex++)
		{
			wave[offset + byteIndex] = static_cast<unsigned char>(value >> (byteIndex * 8));
		}
	};
	std::memcpy(wave.data(), "RIFF", 4);
	write32(4, 36 + sampleBytes);
	std::memcpy(wave.data() + 8, "WAVEfmt ", 8);
	write32(16, 16);
	write16(20, 1);
	write16(22, 1);
	write32(24, 8000);
	write32(28, 16000);
	write16(32, 2);
	write16(34, 16);
	std::memcpy(wave.data() + 36, "data", 4);
	write32(40, sampleBytes);
	std::error_code errorCode;
	std::filesystem::create_directories(path.parent_path(), errorCode);
	std::ofstream output(path, std::ios::binary);
	output.write(reinterpret_cast<const char*>(wave.data()), static_cast<std::streamsize>(wave.size()));
	return output.good();
}
}

bool runObjectAnimationRuntimeTests()
{
	const auto root = makeUniqueTestDirectory("jxqy_object_animation_runtime_test");
	std::error_code errorCode;
	std::filesystem::remove_all(root, errorCode);
	if (!writeTextFile(root / "ini" / "objres" / "static.ini",
		"[Common]\nImage=static.asf\nShade=static.shd\n")
		|| !writeTextFile(root / "ini" / "objres" / "legacy-movie.ini",
			"[Common]\nImage=legacy-animation.asf\nShade=legacy-animation.shd\n")
		|| !writeTextFile(root / "ini" / "objres" / "modern.ini",
			"[Common]\nImage=modern-static.asf\nAnimation=modern-animation.asf\n"))
	{
		std::cerr << "FAILED: write object animation resource fixtures\n";
		return false;
	}

	File::setAssetsCollectionRoot(root.string());
	File::setActiveResourceRoot(root.string());
	File::setResourceFallbackRoots({});
	GameManager gameManager;
	bool ok = true;

	INIReader legacyObjectIni;
	legacyObjectIni.Set("Init", "ObjName", "LEGACY_MOVIE_DROP");
	legacyObjectIni.Set("Init", "ObjFile", "static.ini");
	legacyObjectIni.Set("Init", "ObjFileMovie", "legacy-movie.ini");
	legacyObjectIni.SetInteger("Init", "Kind", okPickupLegacy);
	legacyObjectIni.SetInteger("Init", "Type", 2);
	Object legacyObject;
	legacyObject.initFromIni(&legacyObjectIni, "Init");
	ok = check(legacyObject.res.imageFile == "static.asf"
		&& legacyObject.res.shadowFile == "static.shd"
		&& legacyObject.res.animationFile == "legacy-animation.asf"
		&& legacyObject.res.animationShadowFile == "legacy-animation.shd",
		"ObjFileMovie maps its referenced object resource to the one-shot animation") && ok;
	ok = check(legacyObject.objectType == 2 && legacyObject.kind == okPickupLegacy,
		"Object Type remains independent metadata and Kind keeps runtime behavior") && ok;

	INIReader modernObjectIni;
	modernObjectIni.Set("Init", "ObjFile", "modern.ini");
	modernObjectIni.Set("Init", "ObjFileMovie", "legacy-movie.ini");
	modernObjectIni.SetInteger("Init", "Kind", okPickup);
	Object modernObject;
	modernObject.initFromIni(&modernObjectIni, "Init");
	ok = check(modernObject.res.animationFile == "modern-animation.asf"
		&& modernObject.res.animationShadowFile.empty(),
		"modern objres Animation takes precedence over the legacy ObjFileMovie fallback") && ok;

	INIReader missingStaticObjectIni;
	missingStaticObjectIni.Set("Init", "ObjFile", "missing-static.ini");
	missingStaticObjectIni.Set("Init", "ObjFileMovie", "legacy-movie.ini");
	missingStaticObjectIni.SetInteger("Init", "Kind", okPickupLegacy);
	Object missingStaticObject;
	missingStaticObject.initFromIni(&missingStaticObjectIni, "Init");
	ok = check(missingStaticObject.res.imageFile.empty()
		&& missingStaticObject.res.animationFile == "legacy-animation.asf",
		"legacy animation resource remains usable when the static object resource is missing") && ok;

	INIReader savedObjectIni;
	legacyObject.saveToIni(&savedObjectIni, "OBJ1");
	ok = check(savedObjectIni.Get("OBJ1", "ObjFileMovie", "") == "legacy-movie.ini"
		&& savedObjectIni.GetInteger("OBJ1", "Type", 0) == 2
		&& !savedObjectIni.Get("OBJ1", "State", "").empty(),
		"object save preserves legacy animation metadata and an explicit no-replay state") && ok;

	INIReader longActionIni;
	longActionIni.Set("Init", "ActionTime", "9223372036854775806");
	Object longActionObject;
	longActionObject.initFromIni(&longActionIni, "Init");
	INIReader longActionSaved;
	longActionObject.saveToIni(&longActionSaved, "OBJ0");
	ok = check(longActionSaved.GetTime("OBJ0", "ActionTime", 0) >= 9223372036854775806ULL,
		"runtime object preserves a 64-bit persisted action elapsed value") && ok;

	auto existingObject = std::make_shared<Object>();
	existingObject->objName = "PRESERVE_ON_FAILURE";
	gameManager.objectManager->objectList.push_back(existingObject);
	ok = check(
		!gameManager.objectManager->load("missing.obj") &&
			gameManager.objectManager->objectList.size() == 1 &&
			gameManager.objectManager->objectList.front() == existingObject,
		"missing object list preserves current runtime objects") && ok;
	if (!writeTextFile(root / "ini" / "save" / "bad-count.obj",
		"[Head]\nCount=999999999\n"))
	{
		std::cerr << "FAILED: write bad object count fixture\n";
		return false;
	}
	auto invalidCountExistingObject = std::make_shared<Object>();
	invalidCountExistingObject->objName = "PRESERVE_ON_INVALID_COUNT";
	gameManager.objectManager->objectList.push_back(invalidCountExistingObject);
	ok = check(
		!gameManager.objectManager->load("bad-count.obj") &&
			gameManager.objectManager->objectList.size() == 2 &&
			gameManager.objectManager->objectList[0] == existingObject &&
			gameManager.objectManager->objectList[1] ==
				invalidCountExistingObject,
		"oversized object count preserves current runtime objects") && ok;
	if (!writeTextFile(root / "ini" / "obj" / "valid.ini",
		"[Init]\nObjName=VALID_OBJECT\nKind=2\n") ||
		!writeTextFile(
			root / "ini" / "save" / "valid-list.obj",
			"[Head]\nCount=1\n"
			"[OBJ000]\n"
			"ObjName=VALID_LIST_OBJECT\n"
			"ObjFile=valid.ini\n"
			"Kind=2\n"
			"MapX=0\n"
			"MapY=0\n"))
	{
		std::cerr << "FAILED: write valid object fixture\n";
		return false;
	}
	int objectPreparationCheckpointCount = 0;
	int cancelledObjectMutationCount = 0;
	const bool cancelledObjectLoad =
		gameManager.objectManager->load(
			"valid-list.obj",
			[&cancelledObjectMutationCount]()
			{
				++cancelledObjectMutationCount;
			},
			[&objectPreparationCheckpointCount]()
			{
				return ++objectPreparationCheckpointCount < 2;
			});
	ok = check(
		!cancelledObjectLoad &&
			objectPreparationCheckpointCount == 2 &&
			cancelledObjectMutationCount == 0 &&
			gameManager.objectManager->objectList.size() == 2 &&
			gameManager.objectManager->objectList[0] == existingObject &&
			gameManager.objectManager->objectList[1] ==
				invalidCountExistingObject,
		"object preparation checkpoints can cancel after constructing a candidate but before replacing live objects") &&
		ok;
	const size_t beforeMissingAdd = gameManager.objectManager->objectList.size();
	ok = check(gameManager.objectManager->addObject("missing.ini", 1, 1, 0) == nullptr
		&& gameManager.objectManager->objectList.size() == beforeMissingAdd,
		"missing object definition does not create a blank runtime object") && ok;
	ok = check(gameManager.objectManager->addObject("valid.ini", 1, 1, 0) != nullptr,
		"valid object definition still creates a runtime object") && ok;
	auto retainedMapData = std::make_shared<MapData>();
	retainedMapData->head.width = 1;
	retainedMapData->head.height = 1;
	retainedMapData->tile.resize(1, std::vector<MapTile>(1));
	gameManager.map->data = retainedMapData;
	gameManager.global.data.mapName = "retained.map";
	gameManager.mapFolderName = "retained";
	const size_t retainedObjectCount = gameManager.objectManager->objectList.size();
	gameManager.scriptAPI.loadMap("missing.map", false);
	ok = check(gameManager.map->data == retainedMapData
		&& gameManager.global.data.mapName == "retained.map"
		&& gameManager.mapFolderName == "retained"
		&& gameManager.objectManager->objectList.size() == retainedObjectCount,
		"script map load failure preserves the current world and resource folder") && ok;

	if (!writeMinimalWave(root / "sound" / "rain-test.wav")
		|| !writeTextFile(root / "ini" / "map" / "rain-test.ini",
			"[Init]\nNumber=999999\nSpeed=0\nBoltProb=0\n"
			"[RainSound]\n1=rain-test.wav\n"))
	{
		std::cerr << "FAILED: write custom rain fixtures\n";
		return false;
	}
	gameManager.weather->setWeather(wtCustomRain, "rain-test.ini");
	ok = check(gameManager.weather->getConfiguredRainDropCount() == WeatherSafety::MaximumRainDropCount
		&& gameManager.weather->getConfiguredRainSpeed() == 1
		&& gameManager.weather->getConfiguredBoltProbability() == 1,
		"custom rain runtime clamps untrusted configuration values") && ok;
	ok = check(normalizePath(gameManager.weather->getConfiguredRainSoundName()) == "sound/rain-test.wav",
		"custom rain resolves its configured ambient sound resource") && ok;
	gameManager.weather->setWeather(wtNone);
	ok = check(gameManager.weather->getConfiguredRainSoundName().empty()
		&& !gameManager.weather->hasCustomRainSoundChannel(),
		"leaving custom rain clears and stops its ambient sound") && ok;
	gameManager.scriptAPI.showSnow(1);
	gameManager.scriptAPI.showRain(1);
	ok = check(
		gameManager.global.data.snowShow &&
		gameManager.global.data.rainShow &&
		gameManager.weather->isSnowing() &&
		gameManager.weather->isRaining(),
		"rain and snow script state can be active at the same time") &&
		ok;
	gameManager.scriptAPI.showSnow(0);
	ok = check(
		!gameManager.global.data.snowShow &&
		gameManager.global.data.rainShow &&
		!gameManager.weather->isSnowing() &&
		gameManager.weather->isRaining(),
		"ShowSnow only changes snow state") &&
		ok;
	gameManager.scriptAPI.showSnow(1);
	gameManager.scriptAPI.endRain();
	ok = check(
		gameManager.global.data.snowShow &&
		!gameManager.global.data.rainShow &&
		gameManager.weather->isSnowing() &&
		!gameManager.weather->isRaining(),
		"EndRain only changes rain state") &&
		ok;
	gameManager.scriptAPI.showSnow(0);

	PreparedObjectLoad preparedObjectLoad;
	ok = check(
		gameManager.objectManager->prepareLoad(
			"valid-list.obj",
			preparedObjectLoad) &&
			preparedObjectLoad.isPrepared() &&
			preparedObjectLoad.objectCount() == 1,
		"worker-facing object preparation parses the source once") &&
		ok;
	std::filesystem::remove(
		root / "ini" / "save" / "valid-list.obj",
		errorCode);
	int preparedObjectMutationCount = 0;
	ok = check(
		gameManager.objectManager->commitPreparedLoad(
			std::move(preparedObjectLoad),
			[&preparedObjectMutationCount]()
			{
				++preparedObjectMutationCount;
			}) &&
			preparedObjectMutationCount == 1 &&
			gameManager.objectManager->objectList.size() == 1 &&
			gameManager.objectManager->objectList.front() != nullptr &&
			gameManager.objectManager->objectList.front()->objName ==
				"VALID_LIST_OBJECT",
		"a prepared object list commits after its source is removed without rereading or reparsing it") &&
		ok;

	const std::string exactObjectList =
		"[Head]\n"
		"Count=1\n"
		"[OBJ000]\n"
		"ObjName=EXACT_PREPARED_OBJECT\n"
		"ObjFile=static.ini\n"
		"Kind=0\n"
		"MapX=0\n"
		"MapY=0\n";
	const std::vector<std::uint8_t> exactObjectBytes(
		exactObjectList.cbegin(),
		exactObjectList.cend());
	PreparedObjectLoad exactPreparedObjectLoad;
	bool exactPreparedOnWorker = false;
	const std::thread::id ownerThreadId =
		std::this_thread::get_id();
	std::thread exactPrepareWorker(
		[&]()
		{
			exactPreparedOnWorker =
				std::this_thread::get_id() != ownerThreadId &&
				gameManager.objectManager->
					prepareExactResourceBytes(
						"missing-compatible.obj",
						exactObjectBytes,
						exactPreparedObjectLoad);
		});
	exactPrepareWorker.join();
	ok = check(
		exactPreparedOnWorker &&
			exactPreparedObjectLoad.objectCount() == 1 &&
			gameManager.objectManager->commitPreparedLoad(
				std::move(exactPreparedObjectLoad)) &&
			gameManager.objectManager->objectList.size() == 1 &&
			gameManager.objectManager->objectList.front() != nullptr &&
			gameManager.objectManager->objectList.front()->objName ==
				"EXACT_PREPARED_OBJECT",
		"exact object bytes prepare on a worker and commit on the owner without reparsing") &&
		ok;

	if (!writeTextFile(
			root / "ini" / "objres" / "prepared-cache.ini",
			"[Common]\nImage=prepared-cache.asf\n") ||
		!writeTextFile(
			root / "ini" / "save" / "prepared-cache.obj",
			"[Head]\nCount=1\n"
			"[OBJ000]\n"
			"ObjName=PREPARED_CACHE_OBJECT\n"
			"ObjFile=prepared-cache.ini\n"
			"Kind=0\n"
			"MapX=0\n"
			"MapY=0\n"))
	{
		std::cerr << "FAILED: write prepared object cache fixtures\n";
		return false;
	}
	auto preparedImage = std::make_shared<IMPImage>();
	gameManager.objectManager->objectImageList["prepared-cache.asf"] =
		preparedImage;
	std::weak_ptr<IMPImage> discardedImage;
	{
		auto oldImage = std::make_shared<IMPImage>();
		discardedImage = oldImage;
		gameManager.objectManager->objectImageList["discarded-cache.asf"] =
			oldImage;
		auto oldObject = std::make_shared<Object>();
		oldObject->objName = "DISCARDED_CACHE_OBJECT";
		oldObject->res.imageFile = "discarded-cache.asf";
		oldObject->res.image = oldImage;
		gameManager.objectManager->objectList.push_back(oldObject);
		gameManager.objectManager->addChild(oldObject);
	}
	PreparedObjectLoad cachedPreparedLoad;
	ok = check(
		gameManager.objectManager->prepareLoad(
			"prepared-cache.obj",
			cachedPreparedLoad) &&
		gameManager.objectManager->commitPreparedLoad(
			std::move(cachedPreparedLoad)),
		"prepared object cache fixture commits successfully") &&
		ok;
	const auto preparedObjectCache =
		gameManager.objectManager->objectImageList.find(
			"prepared-cache.asf");
	ok = check(
		gameManager.objectManager->objectList.size() == 1 &&
		gameManager.objectManager->objectList.front()->res.image ==
			preparedImage &&
		preparedObjectCache !=
			gameManager.objectManager->objectImageList.end() &&
		preparedObjectCache->second == preparedImage &&
		gameManager.objectManager->objectImageList.find(
			"discarded-cache.asf") ==
			gameManager.objectManager->objectImageList.end() &&
		discardedImage.expired(),
		"prepared object commit retains referenced image caches and prunes discarded images") &&
		ok;

	if (!writeTextFile(
			root / "ini" / "save" / "incomplete-compatible.obj",
			"[Head]\n"
			"Count=invalid\n"
			"[OBJ000]\n"
			"ObjName=INCOMPLETE_COMPATIBLE_OBJECT\n"
			"Kind=0\n"
			"MapX=0\n"
			"MapY=0\n"))
	{
		std::cerr << "FAILED: write incomplete compatible object fixture\n";
		return false;
	}
	PreparedObjectLoad incompleteObjectLoad;
	ok = check(
		!gameManager.objectManager->prepareLoad(
			"incomplete-compatible.obj",
			incompleteObjectLoad),
		"ordinary object loading keeps rejecting an invalid declared count") &&
		ok;
	ok = check(
		gameManager.objectManager->prepareLoad(
			"incomplete-compatible.obj",
			incompleteObjectLoad,
			true) &&
			incompleteObjectLoad.objectCount() == 1 &&
			gameManager.objectManager->commitPreparedLoad(
				std::move(incompleteObjectLoad)) &&
			gameManager.objectManager->objectList.size() == 1 &&
			gameManager.objectManager->objectList.front() != nullptr &&
			gameManager.objectManager->objectList.front()->objName ==
				"INCOMPLETE_COMPATIBLE_OBJECT",
		"compatible save loading uses the contiguous object sections that can be recovered") &&
		ok;

	std::filesystem::remove_all(root, errorCode);
	return ok;
}
