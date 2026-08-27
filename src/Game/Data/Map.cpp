#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES 
#endif
#include <cmath>
#include "../../Engine/Engine.h"
#include <algorithm>
#include <array>
#include <climits>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "Map.h"
#include "ProjectedMovement.h"
#include "ColorStyle.h"
#include "../../Image/ImageAnimationPlayback.h"
#include "../GameManager/GameManager.h"

// 定义节点结构体
struct MapNode
{
public:
	Point pos;
	float g;
	float h;
	float f;
	float priorityScore;
	float directionBias;
	int firstDir;
	MapNode* parent;

	MapNode(Point p, float g_val, float h_val, MapNode* par, int firstDirection = -1, float bias = 0.0f)
		: pos(p), g(g_val), h(h_val), f(g_val + h_val), priorityScore(g_val + h_val + bias * 0.001f), directionBias(bias), firstDir(firstDirection), parent(par)
	{
	}

	bool operator>(const MapNode& other) const
	{
		return f > other.f;
	}
	bool operator<(const MapNode& other) const
	{
		return f < other.f;
	}
	bool operator==(const MapNode& other) const
	{
		return f == other.f;
	}

	class Compare
	{
	public:
		bool operator()(MapNode* node1, MapNode* node2)
		{
			if (node1->priorityScore != node2->priorityScore)
			{
				return node1->priorityScore > node2->priorityScore;
			}
			if (node1->directionBias != node2->directionBias)
			{
				return node1->directionBias > node2->directionBias;
			}
			if (node1->h != node2->h)
			{
				return node1->h > node2->h;
			}
			return node1->g > node2->g;
		}
	};
};

static int normalizeDirection8(int direction)
{
	return ((direction % 8) + 8) % 8;
}

static int getDirectionDistance8(int dir1, int dir2)
{
	int delta = std::abs(normalizeDirection8(dir1) - normalizeDirection8(dir2));
	return std::min(delta, 8 - delta);
}

static void drawFeatheredThumbnail(Engine* engine, const _shared_image& source,
	const Rect& sourceRect, int sourceWidth, int sourceHeight,
	int thumbnailWidth, int thumbnailHeight)
{
	if (engine == nullptr || source == nullptr || sourceWidth <= 0 || sourceHeight <= 0
		|| thumbnailWidth <= 0 || thumbnailHeight <= 0)
	{
		return;
	}

	const float feather = static_cast<float>(std::min({
		MapThumbnailStyle::FeatherPixels,
		thumbnailWidth / 2,
		thumbnailHeight / 2,
	}));
	const std::array<float, 4> positionsX = {
		0.0f,
		feather,
		static_cast<float>(thumbnailWidth) - feather,
		static_cast<float>(thumbnailWidth),
	};
	const std::array<float, 4> positionsY = {
		0.0f,
		feather,
		static_cast<float>(thumbnailHeight) - feather,
		static_cast<float>(thumbnailHeight),
	};

	std::vector<Vertex> vertices;
	vertices.reserve(16);
	for (int row = 0; row < 4; row++)
	{
		for (int column = 0; column < 4; column++)
		{
			const float x = positionsX[column];
			const float y = positionsY[row];
			const float sourceX = static_cast<float>(sourceRect.x)
				+ x / static_cast<float>(thumbnailWidth) * static_cast<float>(sourceRect.w);
			const float sourceY = static_cast<float>(sourceRect.y)
				+ y / static_cast<float>(thumbnailHeight) * static_cast<float>(sourceRect.h);

			Vertex vertex;
			vertex.position = { x, y };
			vertex.tex_coord = {
				sourceX / static_cast<float>(sourceWidth),
				sourceY / static_cast<float>(sourceHeight),
			};
			const bool atOuterEdge = row == 0 || row == 3 || column == 0 || column == 3;
			vertex.color = { 1.0f, 1.0f, 1.0f, atOuterEdge ? 0.0f : 1.0f };
			vertices.push_back(vertex);
		}
	}

	std::vector<int> indices;
	indices.reserve(54);
	for (int row = 0; row < 3; row++)
	{
		for (int column = 0; column < 3; column++)
		{
			const int topLeft = row * 4 + column;
			const int topRight = topLeft + 1;
			const int bottomLeft = topLeft + 4;
			const int bottomRight = bottomLeft + 1;
			indices.insert(indices.end(), {
				topLeft, topRight, bottomRight,
				topLeft, bottomRight, bottomLeft,
			});
		}
	}

	SDL_SetTextureBlendMode(source.get(), SDL_BLENDMODE_BLEND);
	engine->drawGeometry(source, vertices, indices);
}

template <typename NPCList>
bool hasBlockingNPC(const NPCList& npcList, const NPC* ignoredNPC = nullptr)
{
	for (const auto& npc : npcList)
	{
		if (npc != nullptr && npc.get() != ignoredNPC
			&& npc->isObstacleForCharacter())
		{
			return true;
		}
	}
	return false;
}

static std::string toLowerAsciiPath(std::string value)
{
	for (char& ch : value)
	{
		if (ch >= 'A' && ch <= 'Z')
		{
			ch = static_cast<char>(ch + ('a' - 'A'));
		}
	}
	return value;
}

static bool isMapHeader(const char* head, const char* expected)
{
	return std::memcmp(head, expected, MAP_HEADSTR_LEN) == 0;
}

static int readMapInt32(const char* data)
{
	int value = 0;
	std::memcpy(&value, data, sizeof(value));
	return value;
}

static std::string readFixedMapString(const char* data, size_t capacity)
{
	size_t length = 0;
	while (length < capacity && data[length] != '\0')
	{
		length++;
	}
	return std::string(data, length);
}

static bool isValidUtf8(const char* data, size_t length)
{
	if (data == nullptr)
	{
		return true;
	}

	size_t index = 0;
	while (index < length)
	{
		unsigned char lead = static_cast<unsigned char>(data[index]);
		if (lead < 0x80)
		{
			index++;
			continue;
		}

		size_t extraBytes = 0;
		uint32_t codePoint = 0;
		if ((lead & 0xE0) == 0xC0)
		{
			extraBytes = 1;
			codePoint = lead & 0x1F;
			if (codePoint == 0)
			{
				return false;
			}
		}
		else if ((lead & 0xF0) == 0xE0)
		{
			extraBytes = 2;
			codePoint = lead & 0x0F;
		}
		else if ((lead & 0xF8) == 0xF0)
		{
			extraBytes = 3;
			codePoint = lead & 0x07;
		}
		else
		{
			return false;
		}

		if (index + extraBytes >= length)
		{
			return false;
		}

		for (size_t offset = 1; offset <= extraBytes; offset++)
		{
			unsigned char continuation = static_cast<unsigned char>(data[index + offset]);
			if ((continuation & 0xC0) != 0x80)
			{
				return false;
			}
			codePoint = (codePoint << 6) | (continuation & 0x3F);
		}

		if ((extraBytes == 1 && codePoint < 0x80) ||
			(extraBytes == 2 && codePoint < 0x800) ||
			(extraBytes == 3 && codePoint < 0x10000) ||
			codePoint > 0x10FFFF ||
			(codePoint >= 0xD800 && codePoint <= 0xDFFF))
		{
			return false;
		}

		index += extraBytes + 1;
	}

	return true;
}

static bool readUtf8MapString(const char* data, size_t capacity, std::string& text)
{
	text = readFixedMapString(data, capacity);
	return isValidUtf8(text.data(), text.size());
}

Map::Map()
{
	setPriority(epMap);
	waterEffect.applyPresetParams();
}

Map::~Map()
{
	freeResource();
}

bool Map::load(
	const std::string& fileName,
	const std::function<void()>& beforeMutation,
	const std::function<bool()>& preparationCheckpoint)
{
	GameLog::write("load map file %s\n", fileName.c_str());
	std::unique_ptr<char[]> s;
	int len = 0;
	if (File::readFile(fileName, s, len, MapSafety::MaximumFileBytes)
		&& s != nullptr && len > 0)
	{
		return load(
			s,
			len,
			beforeMutation,
			preparationCheckpoint);
	}
	GameLog::write("map file %s doesn't exist!\n", fileName.c_str());
	return false;
}

bool Map::load(
	std::unique_ptr<char[]>& temp_d,
	int len,
	const std::function<void()>& beforeMutation,
	const std::function<bool()>& preparationCheckpoint)
{
	PreparedLoadCandidate candidate;
	if (!prepareLoadCandidate(
			temp_d,
			len,
			candidate,
			preparationCheckpoint))
	{
		return false;
	}
	return commitPreparedLoadCandidate(
		std::move(candidate),
		beforeMutation,
		preparationCheckpoint);
}

bool Map::validate(const std::string& fileName)
{
	// Save-generation validation is structural only. Reading and decoding map
	// image packages belongs to an actual load preparation, never preflight.
	std::unique_ptr<char[]> sourceData;
	int length = 0;
	PreparedLoadCandidate candidate;
	return File::readFile(
			fileName,
			sourceData,
			length,
			MapSafety::MaximumFileBytes) &&
		sourceData != nullptr &&
		length > 0 &&
		parsePreparedLoadCandidate(
			sourceData,
			length,
		candidate,
		{},
		{},
		false);
}

bool Map::validate(
	std::unique_ptr<char[]>& data,
	int length)
{
	// Keep in-memory validation parse-only for the same reason as file-backed
	// validation above.
	PreparedLoadCandidate candidate;
	return parsePreparedLoadCandidate(
		data,
		length,
		candidate,
		{},
		{},
		false);
}

bool Map::prepareLoadCandidate(
	const std::string& fileName,
	PreparedLoadCandidate& candidate,
	const std::function<bool()>& preparationCheckpoint,
	const std::string& fallbackMpcFolder)
{
	candidate.parsedData.reset();
	candidate.preparedMapMpc.reset();
	candidate.mapMpcPrepared = false;
	std::unique_ptr<char[]> sourceData;
	int length = 0;
	return File::readFile(
			fileName,
			sourceData,
			length,
			MapSafety::MaximumFileBytes) &&
		sourceData != nullptr &&
		length > 0 &&
		prepareLoadCandidate(
			sourceData,
			length,
			candidate,
			preparationCheckpoint,
			fallbackMpcFolder);
}

bool Map::prepareLoadCandidate(
	std::unique_ptr<char[]>& sourceData,
	int length,
	PreparedLoadCandidate& candidate,
	const std::function<bool()>& preparationCheckpoint,
	const std::string& fallbackMpcFolder)
{
	candidate.parsedData.reset();
	candidate.preparedMapMpc.reset();
	candidate.mapMpcPrepared = false;
	return parsePreparedLoadCandidate(
		sourceData,
		length,
		candidate,
		preparationCheckpoint,
		fallbackMpcFolder,
		true);
}

bool Map::commitPreparedLoadCandidate(
	PreparedLoadCandidate candidate,
	const std::function<void()>& beforeMutation,
	const std::function<bool()>& preparationCheckpoint)
{
	if (!candidate.isValid() ||
		engine == nullptr)
	{
		return false;
	}
	if (!engine->isMainThread())
	{
		GameLog::write(
			"Map: prepared map commit must run on the SDL main thread\n");
		return false;
	}

	std::shared_ptr<MapMpc> preparedMapMpc;
	if (candidate.mapMpcPrepared)
	{
		preparedMapMpc = std::move(candidate.preparedMapMpc);
	}
	else
	{
		preparedMapMpc = createMapMpc(
			candidate.parsedData,
			preparationCheckpoint);
	}
	if (preparedMapMpc == nullptr)
	{
		return false;
	}
	if (beforeMutation)
	{
		beforeMutation();
	}
	data = std::move(candidate.parsedData);
	mapMpc = std::move(preparedMapMpc);
	dataMap = std::move(candidate.preparedDataMap);
	initTime();
	return true;
}

bool Map::parsePreparedLoadCandidate(
	std::unique_ptr<char[]>& sourceData,
	int length,
	PreparedLoadCandidate& candidate,
	const std::function<bool()>& preparationCheckpoint,
	const std::string& fallbackMpcFolder,
	bool prepareMapImages)
{
	if (sourceData == nullptr ||
		length < MAP_HEAD_LEN ||
		length > MapSafety::MaximumFileBytes)
	{
		return false;
	}

	auto d = sourceData.get();
	auto parsedData = std::make_shared<MapData>();
#define mapReadData(_dst, _len) \
	if ((_len) < 0 || size < (int)(_len)) { return false; } \
	memcpy(_dst, d, _len);\
	d += _len;\
	size -= _len;

	int size = length;
	mapReadData(&parsedData->head, MAP_HEADSTR_LEN);

	if (!isMapHeader(parsedData->head.head, MAP_HEADSTR_V3))
	{
		GameLog::write("unsupported map format: MAP File Ver3.0 is required\n");
		return false;
	}

	mapReadData(parsedData->head.dataNil, MAP_nullptr);
	mapReadData(parsedData->head.path, MAP_PATH);
	mapReadData(&parsedData->head.dataLen, 4);
	mapReadData(&parsedData->head.width, 4);
	mapReadData(&parsedData->head.height, 4);
	mapReadData(&parsedData->head.infoLen, 4);
	mapReadData(&parsedData->head.nameLen, 4);
	mapReadData(parsedData->head.dataNil2, MAP_nullptr_2);

	int headerSize = readMapInt32(parsedData->head.dataNil2);
	int pathLen = readMapInt32(parsedData->head.dataNil2 + 4);
	int flags = readMapInt32(parsedData->head.dataNil2 + 8);
	int mpcCount = readMapInt32(parsedData->head.dataNil2 + 12);
	if (headerSize < MAP_HEAD_LEN ||
		pathLen < 0 ||
		pathLen > MapSafety::MaximumMetadataBytes ||
		static_cast<int64_t>(MAP_HEAD_LEN) + pathLen != headerSize ||
		mpcCount != MAP_MPC_COUNT ||
		(flags & MAP_V3_FLAG_UTF8) == 0)
	{
		return false;
	}
	if (pathLen > 0)
	{
		if (size < pathLen)
		{
			return false;
		}
		if (!readUtf8MapString(d, static_cast<size_t>(pathLen), parsedData->mpcPath))
		{
			return false;
		}
		d += pathLen;
		size -= pathLen;
	}
	else
	{
		if (!readUtf8MapString(parsedData->head.path, MAP_PATH, parsedData->mpcPath))
		{
			return false;
		}
	}

	const int64_t tileCount = static_cast<int64_t>(parsedData->head.width) * parsedData->head.height;
	int64_t tileBytes = (int64_t)sizeof(MapTile) * tileCount;
	int64_t mpcBytes = (int64_t)MAP_MPC_COUNT * parsedData->head.infoLen;
	if (parsedData->head.width <= 0 || parsedData->head.height <= 0 ||
		parsedData->head.width > MapSafety::MaximumDimension ||
		parsedData->head.height > MapSafety::MaximumDimension ||
		tileCount <= 0 || tileCount > MapSafety::MaximumTileCount ||
		parsedData->head.nameLen <= 0 ||
		parsedData->head.nameLen > MapSafety::MaximumMetadataBytes ||
		parsedData->head.infoLen < parsedData->head.nameLen + 16 ||
		parsedData->head.infoLen > MapSafety::MaximumMetadataBytes ||
		tileBytes < 0 ||
		mpcBytes < 0 ||
		mpcBytes + tileBytes > size)
	{
		return false;
	}

	for (size_t i = 0; i < MAP_MPC_COUNT; i++)
	{
		std::vector<char> rawName(static_cast<size_t>(parsedData->head.nameLen));
		mapReadData(rawName.data(), parsedData->head.nameLen);
		std::string utf8Name;
		if (!readUtf8MapString(rawName.data(), rawName.size(), utf8Name))
		{
			return false;
		}
		parsedData->mpc.mpc[i].name = std::make_unique<char[]>(utf8Name.size() + 1);
		std::memset(parsedData->mpc.mpc[i].name.get(), 0, utf8Name.size() + 1);
		if (!utf8Name.empty())
		{
			std::memcpy(parsedData->mpc.mpc[i].name.get(), utf8Name.data(), utf8Name.size());
		}
		mapReadData(&parsedData->mpc.mpc[i].index, 4);
		mapReadData(&parsedData->mpc.mpc[i].dynamic, 4);
		mapReadData(&parsedData->mpc.mpc[i].obstacle, 4);
		mapReadData(&parsedData->mpc.mpc[i].nil, 4);
		int reservedLen = parsedData->head.infoLen - parsedData->head.nameLen - 16;
		if (reservedLen < 0 || size < reservedLen)
		{
			return false;
		}
		d += reservedLen;
		size -= reservedLen;
	}

	//read data
	parsedData->tile.resize(parsedData->head.height);
	for (int i = 0; i < parsedData->head.height; i++)
	{
		parsedData->tile[i].resize(parsedData->head.width);
	}


	for (int i = 0; i < parsedData->head.height; i++)
	{
		if (preparationCheckpoint &&
			!preparationCheckpoint())
		{
			return false;
		}
		for (int j = 0; j < parsedData->head.width; j++)
		{
			for (size_t k = 0; k < MAP_TILE_LAYER; k++)
			{
				mapReadData(&parsedData->tile[i][j].layer[k].frame, 1);
				mapReadData(&parsedData->tile[i][j].layer[k].mpc, 1);
			}
			mapReadData(&parsedData->tile[i][j].obstacle, 1);
			mapReadData(&parsedData->tile[i][j].trap, 1);
			mapReadData(&parsedData->tile[i][j].end[0], 1);
			mapReadData(&parsedData->tile[i][j].end[1], 1);
		}
	}

	DataMap preparedDataMap;
	preparedDataMap.tile.resize(
		static_cast<std::size_t>(parsedData->head.height));
	for (auto& row : preparedDataMap.tile)
	{
		row.resize(
			static_cast<std::size_t>(parsedData->head.width));
	}

	std::shared_ptr<MapMpc> preparedMapMpc;
	const bool canPrepareMapMpc =
		prepareMapImages &&
		(!parsedData->mpcPath.empty() ||
			!fallbackMpcFolder.empty());
	if (canPrepareMapMpc)
	{
		preparedMapMpc = prepareMapMpc(
			parsedData,
			fallbackMpcFolder,
			preparationCheckpoint);
		if (preparedMapMpc == nullptr)
		{
			return false;
		}
	}

	candidate.parsedData = std::move(parsedData);
	candidate.preparedDataMap = std::move(preparedDataMap);
	candidate.preparedMapMpc = std::move(preparedMapMpc);
	candidate.mapMpcPrepared = canPrepareMapMpc;
	return true;
#undef mapReadData
}

Point Map::getElementPosition(Point pos, Point cenTile, Point cenScreen, PointEx cenTileOffset)
{
	pos.y -= TILE_HEIGHT / 2;
	return getMousePosition(pos, cenTile, cenScreen, cenTileOffset);
}

Point Map::getMousePosition(Point mouse, Point cenTile, Point cenScreen, PointEx cenTileOffset)
{
	Point point;
	int line = std::abs(cenTile.y % 2);
	cenScreen.x -= (int)round(cenTileOffset.x);
	cenScreen.y -= (int)round(cenTileOffset.y);
	float x = (float)(mouse.x - cenScreen.x);
	float y = (float)(mouse.y - cenScreen.y);
	float lx = y / TILE_HEIGHT - x / TILE_WIDTH;
	float ly = x / TILE_WIDTH + y / TILE_HEIGHT;
	int px = 0;
	int py = 0;
	if (lx < 0)
	{
		px = (int)lx;
	}
	else
	{
		px = (int)lx + 1;
	}
	if (ly < 0)
	{
		py = (int)ly;
	}
	else
	{
		py = (int)ly + 1;
	}
	point.y = px + py + cenTile.y;
	int line2 = std::abs(point.y % 2);
	if (line == line2)
	{
		point.x = (py - px) / 2 + cenTile.x;
	}
	else
	{
		if (line == 0)
		{
			if (py >= px)
			{
				point.x = (py - px) / 2 + cenTile.x;
			}
			else
			{
				point.x = (py - px) / 2 + cenTile.x - 1;
			}
		}
		else
		{
			if (py >= px)
			{
				point.x = (py - px) / 2 + cenTile.x + 1;
			}
			else
			{
				point.x = (py - px) / 2 + cenTile.x;
			}
		}
	}

	return point;
}

Point Map::getTilePosition(Point tile, Point cenTile, Point cenScreen, PointEx cenTileOffset)
{
	PointEx pointEx = getTilePositionEx(tile, cenTile, cenScreen, cenTileOffset);
	Point point;
	point.x = static_cast<int>(std::clamp<double>(std::round(pointEx.x), INT_MIN, INT_MAX));
	point.y = static_cast<int>(std::clamp<double>(std::round(pointEx.y), INT_MIN, INT_MAX));
	return point;
}

PointEx Map::getTilePositionEx(Point tile, Point cenTile, Point cenScreen, PointEx cenTileOffset)
{
	PointEx point;
	int line = std::abs(cenTile.y % 2);
	float screenX = (float)cenScreen.x - cenTileOffset.x;
	float screenY = (float)cenScreen.y - cenTileOffset.y;
	int line2 = std::abs(tile.y % 2);
	const int64_t x = static_cast<int64_t>(tile.x) - cenTile.x;
	const int64_t y = static_cast<int64_t>(tile.y) - cenTile.y;
	if (line == line2)
	{
		point.x = static_cast<float>(static_cast<double>(x) * TILE_WIDTH + screenX);
		point.y = static_cast<float>(static_cast<double>(y) * TILE_HEIGHT / 2.0 + screenY);
	}
	else
	{
		point.y = static_cast<float>(static_cast<double>(y) * TILE_HEIGHT / 2.0 + screenY);
		if (line == 0)
		{
			point.x = static_cast<float>(static_cast<double>(x) * TILE_WIDTH + TILE_WIDTH / 2.0 + screenX);
		}
		else
		{
			point.x = static_cast<float>(static_cast<double>(x) * TILE_WIDTH - TILE_WIDTH / 2.0 + screenX);
		}
	}
	return point;
}

Point Map::getTileCenter(Point tile, Point cenTile, Point cenScreen, PointEx offset)
{
	Point pos = getTilePosition(tile, cenTile, cenScreen, offset);
	pos.y -= TILE_HEIGHT / 2;
	return pos;
}

//获得距离，从斜45度矫正为平视地图比例的菱形
float Map::getTileDistance(Point from, PointEx fromOffset, Point to, PointEx toOffset)
{
	auto pos = getTilePosition(to, from);
	PointEx delta =
	{
		(float)pos.x + toOffset.x - fromOffset.x,
		(float)pos.y + toOffset.y - fromOffset.y
	};
	return getProjectedTileUnitDistance(delta);
}

void Map::loadMapMpc()
{
	std::shared_ptr<MapMpc> loadedMapMpc =
		createMapMpc(data);
	mapMpc = std::move(loadedMapMpc);
}

std::shared_ptr<MapMpc> Map::createMapMpc(
	const std::shared_ptr<MapData>& mapData,
	const std::function<bool()>& preparationCheckpoint)
{
	std::string fallbackFolder;
	if (gm != nullptr)
	{
		fallbackFolder =
			"mpc\\map\\" + gm->mapFolderName + "\\";
	}
	return prepareMapMpc(
		mapData,
		fallbackFolder,
		preparationCheckpoint);
}

std::shared_ptr<MapMpc> Map::prepareMapMpc(
	const std::shared_ptr<MapData>& mapData,
	const std::string& fallbackFolder,
	const std::function<bool()>& preparationCheckpoint)
{
	if (mapData == nullptr)
	{
		return nullptr;
	}

	auto loadedMapMpc = std::make_shared<MapMpc>();
	std::unordered_map<std::string, _shared_imp> loadedImages;
	for (size_t i = 0; i < MAP_MPC_COUNT; i++)
	{
		if (preparationCheckpoint &&
			!preparationCheckpoint())
		{
			return nullptr;
		}
		const char* slotName = mapData->mpc.mpc[i].name.get();
		if (slotName == nullptr || slotName[0] == '\0')
		{
			continue;
		}

		std::string mpcName = mapData->mpcPath;
		if (mpcName.empty())
		{
			mpcName = fallbackFolder;
		}
		if (mpcName.length() > 1 &&
			mpcName.back() != '\\' &&
			mpcName.back() != '/')
		{
			mpcName += "\\";
		}
		mpcName += slotName;
		mpcName = toLowerAsciiPath(mpcName);

		std::string cacheKey = mpcName;
		std::replace(cacheKey.begin(), cacheKey.end(), '\\', '/');
		auto loadedImage = loadedImages.find(cacheKey);
		if (loadedImage == loadedImages.end())
		{
			loadedImage = loadedImages.emplace(
				cacheKey,
				IMP::createIMPImage(mpcName, false)).first;
		}
		loadedMapMpc->mpc[i].img = loadedImage->second;
	}
	return loadedMapMpc;
}

int Map::NormalizeDirection(int direction)
{
	return ((direction % 8) + 8) % 8;
}

std::deque<Point> Map::getPathAstar(Point from, Point to, int directionCount, int maxTryCount)
{
	std::deque<Point> path;
	if (to == from)
	{
		return path;
	}
	int w, h;
	if (data == nullptr)
	{
		return path;
	}

	w = data->head.width;
	h = data->head.height;
	if (w <= 0 || h <= 0)
	{
		return path;
	}

	if (!isInMap(from) || !isInMap(to))
	{
		return path;
	}

	// 开放列表，使用优先队列
	std::vector<MapNode> nodePool;
	nodePool.reserve(static_cast<size_t>(w) * static_cast<size_t>(h));

	std::priority_queue<MapNode*, std::vector<MapNode*>, MapNode::Compare> openList;
	std::vector<std::vector<bool>> closedList(h, std::vector<bool>(w, false));
	std::vector<std::vector<MapNode*>> mapNodes(h, std::vector<MapNode*>(w, nullptr));
	int startPreferredDir = NPC::getDirection(from, to);

	nodePool.emplace_back(from, 0.0f, static_cast<float>(calDistance(from, to)), nullptr);
	MapNode* startMapNode = &nodePool.back();
	openList.push(startMapNode);
	mapNodes[from.y][from.x] = startMapNode;
	int count_times = 0;
	while (!openList.empty() && (maxTryCount < 0 || count_times++ <= maxTryCount))
	{
		MapNode* current = openList.top();
		openList.pop();

		if (current->pos == to)
		{
			MapNode* temp = current;
			while (temp->parent)
			{
				path.push_front(temp->pos);
				temp = temp->parent;
			}
			break;
		}

		closedList[current->pos.y][current->pos.x] = true;

		int preferredDir = NPC::getDirection(current->pos, to);
		int dirList[8] = {
			preferredDir,
			(preferredDir + 1) % 8, (preferredDir + 7) % 8,
			(preferredDir + 2) % 8, (preferredDir + 6) % 8,
			(preferredDir + 3) % 8, (preferredDir + 5) % 8,
			(preferredDir + 4) % 8
		};

		for (int di = 0; di < 8; di++)
		{
			int i = dirList[di];
			if (!NPC::canMoveInDirection(i, directionCount))
			{
				continue;
			}
			auto neighbor = getSubPoint(current->pos, i);
			if (!isInMap(neighbor))
			{
				continue;
			}
			if ((!canWalk(neighbor) && (neighbor != to)) || closedList[neighbor.y][neighbor.x])
			{
				continue;
			}

			if (i % 2 == 0)
			{
				if (!canPass(getSubPoint(current->pos, NormalizeDirection(i - 1))) || !canPass(getSubPoint(current->pos, NormalizeDirection(i + 1))))
				{
					continue;
				}
			}

			float temp_tentativeG = 1.0f;
			if (i % 2 == 0)
			{
				temp_tentativeG = 1.414f * temp_tentativeG;
			}
			float tentativeG = current->g + temp_tentativeG;
			int firstDir = (current->parent == nullptr) ? i : current->firstDir;
			float directionBias = (float)getDirectionDistance8(firstDir, startPreferredDir) * 8.0f +
				(float)getDirectionDistance8(i, preferredDir);
			float candidatePriorityScore = tentativeG + calDistance(neighbor, to) + directionBias * 0.001f;

			if (!mapNodes[neighbor.y][neighbor.x])
			{
				nodePool.emplace_back(neighbor, tentativeG, static_cast<float>(calDistance(neighbor, to)), current, firstDir, directionBias);
				MapNode* newNode = &nodePool.back();
				openList.push(newNode);
				mapNodes[neighbor.y][neighbor.x] = newNode;
			}
			else if (tentativeG < mapNodes[neighbor.y][neighbor.x]->g ||
				(std::abs(tentativeG - mapNodes[neighbor.y][neighbor.x]->g) < 0.0001f &&
					candidatePriorityScore < mapNodes[neighbor.y][neighbor.x]->priorityScore))
			{
				mapNodes[neighbor.y][neighbor.x]->parent = current;
				mapNodes[neighbor.y][neighbor.x]->g = tentativeG;
				mapNodes[neighbor.y][neighbor.x]->f = tentativeG + mapNodes[neighbor.y][neighbor.x]->h;
				mapNodes[neighbor.y][neighbor.x]->firstDir = firstDir;
				mapNodes[neighbor.y][neighbor.x]->directionBias = directionBias;
				mapNodes[neighbor.y][neighbor.x]->priorityScore = candidatePriorityScore;
				openList.push(mapNodes[neighbor.y][neighbor.x]);
			}
		}
	}
	return path;
}

std::deque<Point> Map::getPathTraversal(Point from, Point to, int directionCount)
{
	(void)directionCount;
	std::deque<Point> path;
	if (to == from)
	{
		return path;
	}
	int w, h;
	if (data == nullptr)
	{
		return path;
	}
	w = data->head.width;
	h = data->head.height;
	if (w <= 0 || h <= 0)
	{
		return path;
	}

	PathMap pathMap;
	pathMap.map.resize(h);
	for (int i = 0; i < h; i++)
	{
		pathMap.map[i].resize(w);
	}
	pathMap.w = w;
	pathMap.h = h;

	if (!isInMap(&pathMap, from) || !isInMap(&pathMap, to))
	{
		return path;
	}
	/*
	if (!canWalk(to))
	{
		return path;
	}
	*/
	pathMap.map[from.y][from.x].index = 0;
	std::vector<Point> stepList;
	stepList.resize(0);
	stepList.push_back(from);
	int leftStep = MAX_STEP;

	bool found = false;
	while (leftStep--)
	{
		std::vector<Point> newList;
		newList.resize(0);
		for (size_t i = 0; i < stepList.size(); i++)
		{
			std::vector<Point> tempList = getSubStep(&pathMap, stepList[i], to, MAX_STEP - leftStep);
			if (pathMap.map[to.y][to.x].index >= 0)
			{
				found = true;
			}
			for (size_t i = 0; i < tempList.size(); i++)
			{
				newList.push_back(tempList[i]);
			}
		}
		if (found)
		{
			break;
		}
		if (newList.size() == 0)
		{
			break;
		}
		stepList = newList;
	}

	if (found)
	{
		Point step = to;
		path.insert(path.begin(), step);
		while (true)
		{
			step = pathMap.map[step.y][step.x].from;
			if (step == from)
			{
				break;
			}
			else
			{
				path.insert(path.begin(), step);
			}
		}
	}

	return path;
}

std::deque<Point> Map::findPath(Point from, Point to, int directionCount, int maxTryCount)
{
	return getPathAstar(from, to, directionCount, maxTryCount);
	//    return getPathTraversal(from, to);
}

std::deque<Point> Map::findSimplePath(Point from, Point to, int directionCount, int maxTryCount)
{
	std::deque<Point> path;
	if (to == from || data == nullptr)
	{
		return path;
	}

	int w = data->head.width;
	int h = data->head.height;
	if (w <= 0 || h <= 0 || !isInMap(from) || !isInMap(to))
	{
		return path;
	}

	std::vector<MapNode> nodePool;
	nodePool.reserve(static_cast<size_t>(w) * static_cast<size_t>(h));

	std::priority_queue<MapNode*, std::vector<MapNode*>, MapNode::Compare> frontier;
	std::vector<std::vector<bool>> visited(h, std::vector<bool>(w, false));
	std::vector<std::vector<MapNode*>> mapNodes(h, std::vector<MapNode*>(w, nullptr));

	nodePool.emplace_back(from, 0.0f, static_cast<float>(calDistance(from, to)), nullptr);
	MapNode* startMapNode = &nodePool.back();
	frontier.push(startMapNode);
	mapNodes[from.y][from.x] = startMapNode;

	int tryCount = 0;
	while (!frontier.empty())
	{
		if (maxTryCount >= 0 && tryCount++ > maxTryCount)
		{
			break;
		}

		MapNode* current = frontier.top();
		frontier.pop();
		if (visited[current->pos.y][current->pos.x])
		{
			continue;
		}
		visited[current->pos.y][current->pos.x] = true;
		if (current->pos == to)
		{
			MapNode* temp = current;
			while (temp->parent)
			{
				path.push_front(temp->pos);
				temp = temp->parent;
			}
			break;
		}
		if (current->pos != from && !canWalk(current->pos))
		{
			continue;
		}

		int preferredDir = NPC::getDirection(current->pos, to);
		int dirList[8] = {
			preferredDir,
			(preferredDir + 1) % 8, (preferredDir + 7) % 8,
			(preferredDir + 2) % 8, (preferredDir + 6) % 8,
			(preferredDir + 3) % 8, (preferredDir + 5) % 8,
			(preferredDir + 4) % 8
		};

		for (int di = 0; di < 8; di++)
		{
			int direction = dirList[di];
			if (!NPC::canMoveInDirection(direction, directionCount))
			{
				continue;
			}
			Point neighbor = getSubPoint(current->pos, direction);
			if (!isInMap(neighbor) || visited[neighbor.y][neighbor.x] || mapNodes[neighbor.y][neighbor.x] != nullptr)
			{
				continue;
			}
			if (!canWalk(neighbor) && neighbor != to)
			{
				continue;
			}
			if (direction % 2 == 0
				&& (!canPass(getSubPoint(current->pos, NormalizeDirection(direction - 1)))
					|| !canPass(getSubPoint(current->pos, NormalizeDirection(direction + 1)))))
			{
				continue;
			}

			float priority = static_cast<float>(calDistance(neighbor, to));
			nodePool.emplace_back(neighbor, 0.0f, priority, current, direction);
			MapNode* newNode = &nodePool.back();
			newNode->priorityScore = priority;
			frontier.push(newNode);
			mapNodes[neighbor.y][neighbor.x] = newNode;
		}
	}

	return path;
}

std::deque<Point> Map::getLinePath(Point from, Point to, int maxStep)
{
	std::deque<Point> path;
	if (from == to || maxStep <= 0)
	{
		return path;
	}
	if (!isInMap(from) || !isInMap(to))
	{
		return path;
	}

	Point current = from;
	while (current != to && maxStep-- > 0)
	{
		int direction = NPC::getDirection(current, to);
		Point next = getSubPoint(current, direction);
		if (next == current || !isInMap(next))
		{
			break;
		}
		path.push_back(next);
		current = next;
	}
	return path;
}

std::deque<Point> Map::getRadiusPath(Point from, Point to, int radius, int directionCount)
{
	auto result = findPath(from, to, directionCount);
	if (!result.empty())
	{
		if (radius >= (int)result.size())
		{
			result.clear();
			return result;
		}
		for (int i = 0; i < radius; i++)
		{
			result.pop_back();
		}
		return result;
	}
	if (radius <= 0 || data == nullptr || calDistance(from, to) <= radius)
	{
		return result;
	}

	// The target tile can be occupied or enclosed even though a reachable tile
	// inside the requested radius exists. Search those approach tiles only when
	// the legacy exact-target path fails, keeping the common path fast.
	const int minimumX = std::max(0, to.x - radius);
	const int maximumX = std::min(data->head.width - 1, to.x + radius);
	const int minimumY = std::max(0, to.y - radius * 2);
	const int maximumY = std::min(data->head.height - 1, to.y + radius * 2);
	std::deque<Point> bestPath;
	for (int y = minimumY; y <= maximumY; ++y)
	{
		for (int x = minimumX; x <= maximumX; ++x)
		{
			const Point candidate = { x, y };
			if (calDistance(candidate, to) > radius || !canWalk(candidate))
			{
				continue;
			}
			auto candidatePath = findPath(from, candidate, directionCount);
			if (candidatePath.empty())
			{
				continue;
			}
			if (bestPath.empty() || candidatePath.size() < bestPath.size())
			{
				bestPath = std::move(candidatePath);
			}
		}
	}
	return bestPath;
}

std::deque<Point> Map::traceTowardTarget(Point from, Point to, int stepCount, int directionCount)
{
	//    std::deque<Point> ret;
	//    if (stepCount < 0) {
	//        return ret;
	//    }
	//    ret = getPath(from, to);
	//    if (ret.size() > stepCount)
	//    {
	//        ret.resize(stepCount);
	//    }
	//    return ret;

	std::deque<Point> result;
	Point pos = from;
	int stepIdx = 0;
	while (0 < stepCount--)
	{
		stepIdx++;
		if (pos == to)
		{
			return result;
		}
		int stepLength = 3;
		if (stepIdx == 1)
		{
			stepLength = 5;
		}
		for (int i = 0; i < stepLength; i++)
		{
			int d = i;
			switch (d)
			{
			case 2:
				d = -1;
				break;
			case 3:
				d = 2;
				break;
			case 4:
				d = -2;
				break;
			default:
				break;
			}
			int direction = NPC::getDirection(pos, to) + d;
			if (!NPC::canMoveInDirection(direction, directionCount))
			{
				continue;
			}
			std::vector<Point> tempSteps = gm->map->getSubPointEx(pos, direction);
			if (tempSteps.size() > 0)
			{
				bool canContinue = true;
				for (size_t j = 0; j < tempSteps.size() - 1; j++)
				{
					if (!gm->map->canPass(tempSteps[j]))
					{
						canContinue = false;
					}
				}
				if (canContinue && tempSteps.size() > 0)
				{
					if (!gm->map->canWalk(tempSteps[tempSteps.size() - 1]))
					{
						canContinue = false;
					}
				}
				if (canContinue)
				{
					pos = tempSteps[tempSteps.size() - 1];
					result.push_back(pos);
					break;
				}
			}
		}
	}
	return result;
}

std::deque<Point> Map::stepTowardTarget(Point from, Point to, int directionCount)
{
	std::deque<Point> result;
	if (from == to)
	{
		return result;
	}
	int dir = NPC::getDirection(from, to);
	if (!NPC::canMoveInDirection(dir, directionCount))
	{
		return result;
	}
	Point pos = gm->map->getSubPoint(from, dir);
	if (gm->map->canWalk(pos))
	{
		result.push_back(pos);
	}
	return result;
}

std::deque<Point> Map::getPassPath(Point from, Point to, Point flyDirection, Point dest)
{
	std::deque<Point> result;
	result.resize(0);
	result.push_back(from);

	if (flyDirection.is_zero())
	{
		return result;
	}
	if (from == to)
	{
		return result;
	}

	float angle = calFlyDirection(flyDirection);

	Point nowStep = from;
	int leftStep = calDistance(from, to);
	while (leftStep--)
	{
		bool nextStep = true;
		std::vector<Point> stepList = getLineSubStep(nowStep, dest, angle);
		for (size_t i = 0; i < stepList.size(); i++)
		{
			if (stepList[i] != to)
			{
				result.push_back(stepList[i]);
			}
			else
			{
				return result;
			}
		}
		nowStep = stepList[stepList.size() - 1];
	}
	return result;
}

std::deque<Point> Map::getPassPathEx(Point from, PointEx fromOffset, Point to, PointEx toOffset, Point flyDirection)
{
	std::deque<Point> result;
	result.resize(0);
	if (flyDirection.is_zero())
	{
		return result;
	}
	if (from == to)
	{
		return result;
	}
	float angle = calFlyDirection(flyDirection);
	int leftStep = calDistance(from, to) * 2;
	Point nowStep = from;
	PointEx nowOffset = fromOffset;
	result.push_back(nowStep);
	while ((nowStep != to) && (leftStep-- > 0))
	{
		auto nextStep = getLineSubStepEx(nowStep, nowOffset, angle);
		nowStep = nextStep.pos;
		nowOffset = nextStep.pixelOffset;
		result.push_back(nowStep);
	}
	return result;
}

Point Map::getJumpPath(Point from, Point to)
{
	if (from == to)
	{
		return from;
	}
	if (!isInMap(from) || !isInMap(to))
	{
		return from;
	}
	float angle;
	if (from.x == to.x && std::llabs(static_cast<int64_t>(from.y) - to.y) % 2 == 0)
	{
		if (from.y > to.y)
		{
			angle = M_PI / 2;
		}
		else
		{
			angle = M_PI * 3 / 2;
		}
	}
	else if (from.y == to.y)
	{
		if (from.x > to.x)
		{
			angle = M_PI;
		}
		else
		{
			angle = 0;
		}
	}
	else
	{
		Point pos = getTilePosition(to, from, { 0, 0 }, { 0, 0 });
		angle = atan2((float)-pos.y * ((float)TILE_WIDTH / TILE_HEIGHT), (float)pos.x);
		if (angle < 0)
		{
			angle += 2 * M_PI;
		}
	}
	Point lastStep = from;
	Point nowStep = from;
	while (true)
	{
		bool nextStep = true;
		std::vector<Point> stepList = getLineSubStep(nowStep, to, angle);
		if (stepList.size() == 0)
		{
			break;
		}
		for (size_t i = 0; i < stepList.size(); i++)
		{

			if (!canJump(stepList[i]))
			{
				nextStep = false;
				break;
			}
		}
		if (nextStep)
		{
			nowStep = stepList[stepList.size() - 1];

			if (canWalk(stepList[stepList.size() - 1]))
			{
				lastStep = stepList[stepList.size() - 1];
				if (haveTraps(nowStep))
				{
					break;
				}
			}
			if (stepList[stepList.size() - 1] == to)
			{
				break;
			}
		}
		else
		{
			break;
		}
	}
	return lastStep;
}

bool Map::canSee(Point from, Point to)
{
	if (from == to)
	{
		return true;
	}
	// The target occupies its own tile, so that tile must not occlude the
	// target itself. This preserves the legacy canView contract: only terrain
	// and closed doors between the viewer and target block visibility.
	if (!isInMap(from) || !isInMap(to))
	{
		return false;
	}
	float angle;
	if (from.x == to.x && std::llabs(static_cast<int64_t>(from.y) - to.y) % 2 == 0)
	{
		if (from.y > to.y)
		{
			angle = M_PI / 2;
		}
		else
		{
			angle = M_PI * 3 / 2;
		}
	}
	else if (from.y == to.y)
	{
		if (from.x > to.x)
		{
			angle = M_PI;
		}
		else
		{
			angle = 0;
		}
	}
	else
	{
		Point pos = getTilePosition(to, from, { 0, 0 }, { 0, 0 });
		angle = atan2((float)-pos.y * ((float)TILE_WIDTH / TILE_HEIGHT), (float)pos.x);
		if (angle < 0)
		{
			angle += 2 * M_PI;
		}
	}
	Point nowStep = from;
	while (true)
	{
		std::vector<Point> stepList = getLineSubStep(nowStep, to, angle);
		if (stepList.size() == 0)
		{
			return false;
		}
		bool canSeeNext = false;
		for (size_t i = 0; i < stepList.size() - 1; i++)
		{
			if (canSeeTile(stepList[i]))
			{
				canSeeNext = true;
			}
		}
		if (stepList.size() > 1 && canSeeNext == false)
		{
			return false;
		}
		if (stepList[stepList.size() - 1] == to)
		{
			return true;
		}
		if (!canSeeTile(stepList[stepList.size() - 1]))
		{
			return false;
		}
		nowStep = stepList[stepList.size() - 1];
	}
	return false;
}

bool Map::canWalk(Point pos)
{
	return canWalkForActor(pos, nullptr);
}

bool Map::canWalkForActor(
	Point pos, const std::shared_ptr<NPC>& ignoredActor)
{
    if (!isInMap(pos))
        return false;

	if (gm != nullptr && gm->effectManager != nullptr && gm->effectManager->hasSolidEffectAt(pos))
	{
		return false;
	}
    
	if (tileObstacleAllowsWalk(data->tile[pos.y][pos.x].obstacle))
	{
		const NPC* ignoredNPC = ignoredActor.get();
		if (!hasBlockingNPC(
				dataMap.tile[pos.y][pos.x].npcList, ignoredNPC)
			&& !hasBlockingNPC(
				dataMap.tile[pos.y][pos.x].stepNPCList, ignoredNPC))
		{
			if (dataMap.tile[pos.y][pos.x].objList.size() == 0)
			{
				return true;
			}
			else
			{
				for (auto iter = dataMap.tile[pos.y][pos.x].objList.begin(); iter != dataMap.tile[pos.y][pos.x].objList.end(); iter++)
				{
					int objKind = (*iter)->kind;
					if (isObjectObstacleKind(objKind))
					{
						return false;
					}
				}
				return true;
			}
		}
	}
	return false;
}

bool Map::canWalkDirectlyTo(Point pos, int dir)
{
    auto dest = getSubPoint(pos, dir);
    if (!canWalk(dest))
    {
        return false;
    }
    if (dir % 2 != 0) {
        return true;
    }
    return canPass(getSubPoint(pos, NormalizeDirection(dir - 1))) && canPass(getSubPoint(pos, NormalizeDirection(dir + 1)));
}

bool Map::canJump(Point pos)
{
    if (!isInMap(pos))
        return false;
    
	if (tileObstacleAllowsJump(data->tile[pos.y][pos.x].obstacle))
	{
		if (dataMap.tile[pos.y][pos.x].objList.size() > 0)
		{
			for (auto iter = dataMap.tile[pos.y][pos.x].objList.begin(); iter != dataMap.tile[pos.y][pos.x].objList.end(); iter++)
			{
				int objKind = (*iter)->kind;
				if (objKind == okDoor)
				{
					return false;
				}
			}
		}
		return true;
	}
	return false;
}

int Map::getTrapIndex(Point pos)
{
    if (!isInMap(pos))
        return 0;
    
	if (data->tile[pos.y][pos.x].trap != 0)
	{
		return data->tile[pos.y][pos.x].trap;
	}
	return 0;
}

std::string Map::getTrapName(Point pos)
{
    if (!isInMap(pos))
        return "";
    
	if (data->tile[pos.y][pos.x].trap != 0)
	{
		return gm->traps.get(gm->mapFolderName, data->tile[pos.y][pos.x].trap);
	}
	return "";
}

bool Map::haveTraps(Point pos)
{
	if (!isInMap(pos))
		return false;
	if (!data) { return false; }
	const int trapIndex = data->tile[pos.y][pos.x].trap;
	if (trapIndex == 0 || gm->traps.hasTriggered(trapIndex) ||
		gm->traps.get(gm->mapFolderName, trapIndex).empty())
	{
		return false;
	}
	return true;
}

bool Map::canFly(Point pos)
{
    if (!isInMap(pos))
        return false;
    
	if (tileObstacleAllowsMagic(data->tile[pos.y][pos.x].obstacle))
	{
		if (dataMap.tile[pos.y][pos.x].objList.size() == 0)
		{
			return true;
		}
		else
		{
			for (auto iter = dataMap.tile[pos.y][pos.x].objList.begin(); iter != dataMap.tile[pos.y][pos.x].objList.end(); iter++)
			{
				int objKind = (*iter)->kind;
				if (objKind == okDoor || objKind == okOrnament)
				{
					return false;
				}
			}
			return true;
		}
	}
	return false;
}

bool Map::canSeeTile(Point pos)
{
    if (!isInMap(pos))
        return false;
    
	if (tileObstacleAllowsSight(data->tile[pos.y][pos.x].obstacle))
	{
		if (dataMap.tile[pos.y][pos.x].objList.size() == 0)
		{
			return true;
		}
		else
		{
			for (auto iter = dataMap.tile[pos.y][pos.x].objList.begin(); iter != dataMap.tile[pos.y][pos.x].objList.end(); iter++)
			{
				int objKind = (*iter)->kind;
				if (objKind == okDoor)
				{
					return false;
				}
			}
			return true;
		}
	}
	return false;
}

bool Map::canPass(Point pos)
{
	return (canWalk(pos) || canFly(pos));
}

Point Map::getSubPoint(Point from, int direction)
{
	direction = ((direction % 8) + 8) % 8;
	int line = std::abs(from.y % 2);
	auto addCoordinate = [](int value, int delta) {
		const int64_t result = static_cast<int64_t>(value) + delta;
		return static_cast<int>(std::clamp<int64_t>(result, INT_MIN, INT_MAX));
	};
	Point to = from;
	switch (direction)
	{
	case 0:
		to.y = addCoordinate(to.y, 2);
		break;
	case 1:
		to.y = addCoordinate(to.y, 1);
		to.x = addCoordinate(to.x, line - 1);
		break;
	case 2:
		to.x = addCoordinate(to.x, -1);
		break;
	case 3:
		to.y = addCoordinate(to.y, -1);
		to.x = addCoordinate(to.x, line - 1);
		break;
	case 4:
		to.y = addCoordinate(to.y, -2);
		break;
	case 5:
		to.y = addCoordinate(to.y, -1);
		to.x = addCoordinate(to.x, line);
		break;
	case 6:
		to.x = addCoordinate(to.x, 1);
		break;
	case 7:
		to.y = addCoordinate(to.y, 1);
		to.x = addCoordinate(to.x, line);
		break;
	default:
		break;
	}
	return to;
}

std::vector<Point> Map::getSubPointEx(Point from, int direction)
{
	std::vector<Point> result;
	direction = ((direction % 8) + 8) % 8;
	if (direction % 2 == 0)
	{
		result.push_back(getSubPoint(from, direction - 1));
		result.push_back(getSubPoint(from, direction + 1));
	}
	result.push_back(getSubPoint(from, direction));
	return result;
}

int Map::calDistance(Point from, Point to)
{
	int line1 = std::abs(from.y % 2);
	int line2 = std::abs(to.y % 2);
	const int64_t deltaY = std::llabs(static_cast<int64_t>(to.y) - from.y) / 2;
	const int64_t deltaX = static_cast<int64_t>(to.x) - from.x;
	int64_t distance = 0;
	if (line1 == line2)
	{
		distance = deltaY + std::llabs(deltaX);
	}
	else
	{
		if (line1 == 0)
		{
			if (to.x >= from.x)
			{
				distance = deltaY + 1 + deltaX;
			}
			else
			{
				distance = deltaY - deltaX;
			}
		}
		else
		{
			if (to.x > from.x)
			{
				distance = deltaY + deltaX;

			}
			else
			{
				distance = deltaY + 1 - deltaX;
			}
		}
	}
	return static_cast<int>(std::min<int64_t>(distance, INT_MAX));
}


void Map::drawMap()
{
	int w, h;
	engine->getWindowSize(w, h);
	const bool useRainSceneTint = gm->global.data.rainShow
		&& gm->global.feature.rainSceneTint;
	const uint32_t mapColorStyle = useRainSceneTint
		? 0x00808080
		: gm->global.data.mpcStyle;
	const uint32_t actorColorStyle = useRainSceneTint
		? 0x00808080
		: gm->global.data.asfStyle;
	// Both C# ports tint maps and ordinary sprites gray in rain, while magic
	// sprites remain white so their effects do not become muddy.
	const uint32_t effectColorStyle = useRainSceneTint
		? 0x00FFFFFF
		: gm->global.data.asfStyle;

	if (gm->global.data.waterEffect)
	{
		waterEffect.setupEffectCanvas();
	}

	Point cenScreen;
	cenScreen.x = (int)w / 2;
	cenScreen.y = (int)h / 2;
	int xscal, yscal;
	xscal = cenScreen.x / TILE_WIDTH + 2 + LUM_MASK_WIDTH / TILE_WIDTH + 1;
	yscal = cenScreen.y / TILE_HEIGHT * 2 + 2 + LUM_MASK_HEIGHT / TILE_HEIGHT + 1;
	int tileHeightScal = 15;
	Point cenTile = gm->camera->position;
	PointEx offset = gm->camera->offset;

	// 画地面
	for (int i = cenTile.y - yscal; i < cenTile.y + yscal + tileHeightScal; i++)
	{
		for (int j = cenTile.x - xscal; j < cenTile.x + xscal; j++)
		{
			if (data == nullptr || j < 0 || j >= data->head.width || i < 0 || i >= data->head.height)
			{
				continue;
			}
			Point tile = { j, i };
			drawTile(0, tile, cenTile, cenScreen, offset, mapColorStyle);
		}
	}

	if (gm->weather != nullptr)
	{
		gm->weather->drawElementLum();
	}

	const EffectMap& emap = gm->effectManager->createMap(cenTile.x - xscal, cenTile.y - yscal, xscal * 2, yscal * 2 + tileHeightScal);

	for (int i = cenTile.y - yscal; i < cenTile.y + yscal + tileHeightScal; i++)
	{
		int emapRow = i - (cenTile.y - yscal);
		for (int j = cenTile.x - xscal; j < cenTile.x + xscal; j++)
		{
			if (data == nullptr || j < 0 || j >= data->head.width || i < 0 || i >= data->head.height)
			{
				continue;
			}
			int emapCol = j - (cenTile.x - xscal);
			auto& effectTileIndices = emap.tile[emapRow][emapCol].index;
			for (size_t k = 0; k < effectTileIndices.size(); k++)
			{
				auto effect = gm->effectManager->effectList[effectTileIndices[k]];
				if (effect != nullptr)
				{
					if (effect->getMoveKind() == mmkFollow)
					{
						continue;
					}
					if ((float)effect->offset.y < 0 || (float)effect->flyingDirection.y > 0)
					{
						effect->draw(cenTile, cenScreen, offset, effectColorStyle);
					}
				}
			}

			Point tile = { j, i };
			drawTile(1, tile, cenTile, cenScreen, offset, mapColorStyle);

			for (auto iter = dataMap.tile[i][j].objList.begin(); iter != dataMap.tile[i][j].objList.end(); iter++)
			{
				gm->objectManager->drawOBJ(*iter, cenTile, cenScreen, offset, actorColorStyle);
			}
			for (auto iter = dataMap.tile[i][j].npcList.begin(); iter != dataMap.tile[i][j].npcList.end(); iter++)
			{
				if (*iter == gm->player)
				{
					if (!gm->player->isJumping() || gm->player->getJumpState() != jsJumping)
					{
						gm->player->draw(cenTile, cenScreen, offset, actorColorStyle);
					}
				}
				else
				{
					gm->npcManager->drawNPC(*iter, cenTile, cenScreen, offset, actorColorStyle);
				}
			}

			for (size_t k = 0; k < effectTileIndices.size(); k++)
			{
				auto effect = gm->effectManager->effectList[effectTileIndices[k]];
				if (effect != nullptr)
				{
					if (effect->getMoveKind() == mmkFollow)
					{
						auto followTarget = effect->target.lock();
						if (followTarget == gm->player && gm->player->isJumping() && gm->player->getJumpState() == jsJumping)
						{
							continue;
						}
						effect->draw(cenTile, cenScreen, offset, effectColorStyle);
						continue;
					}
					if ((float)effect->offset.y >= 0 && effect->flyingDirection.y <= 0)
					{
						effect->draw(cenTile, cenScreen, offset, effectColorStyle);
					}
				}
			}
		}
	}
	/*
	int cline = abs(cenTile.y) % 2;
	for (int iy = cenTile.y - yscal - xscal * 2 - cline; iy < cenTile.y + yscal + tileHeightScal; iy += 2)
	{
		int line = std::abs(iy) % 2;
		int cx = cenTile.x;
		for (int ix = 0; ix < xscal * 2 + 2; ix++)
		{
			int i = iy + ix;
			int j = cx;
			cx += line - 1;
			line = 1 - line;
			if (i >= cenTile.y - yscal && i < cenTile.y + yscal + tileHeightScal && j >= cenTile.x - xscal && j < cenTile.x + xscal)
			{
				if (data == nullptr || j < 0 || j >= data->head.width || i < 0 || i >= data->head.height)
				{
					continue;
				}
				for (size_t k = 0; k < emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index.size(); k++)
				{
					if (gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]] != nullptr)
					{
						if ((float)gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->flyingDirection.x / TILE_WIDTH + (float)gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->flyingDirection.y / TILE_WIDTH > 0)
						{
							gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->draw(cenTile, cenScreen, offset);
						}

					}
				}
			}
		}
		line = std::abs(iy) % 2;
		cx = cenTile.x + line;
		line = 1 - line;
		for (int ix = 1; ix < xscal * 2 + 2; ix++)
		{
			int i = iy + ix;
			int j = cx;
			cx += line;
			line = 1 - line;
			if (i >= cenTile.y - yscal && i < cenTile.y + yscal + tileHeightScal && j >= cenTile.x - xscal && j < cenTile.x + xscal)
			{
				if (data == nullptr || j < 0 || j >= data->head.width || i < 0 || i >= data->head.height)
				{
					continue;
				}
				for (size_t k = 0; k < emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index.size(); k++)
				{
					if (gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]] != nullptr)
					{
						if (-(float)gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->flyingDirection.x / TILE_WIDTH + (float)gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->flyingDirection.y / TILE_WIDTH > 0)
						{
							gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->draw(cenTile, cenScreen, offset);
						}
					}
				}
			}
		}
		line = std::abs(iy) % 2;
		cx = cenTile.x;
		for (int ix = 0; ix < xscal * 2 + 2; ix++)
		{
			int i = iy + ix;
			int j = cx;
			cx += line - 1;
			line = 1 - line;
			if (i >= cenTile.y - yscal && i < cenTile.y + yscal + tileHeightScal && j >= cenTile.x - xscal && j < cenTile.x + xscal)
			{
				if (data == nullptr || j < 0 || j >= data->head.width || i < 0 || i >= data->head.height)
				{
					continue;
				}

				drawTile(1, { j, i }, cenTile, cenScreen, offset);

				for (size_t k = 0; k < dataMap.tile[i][j].objIndex.size(); k++)
				{
					gm->objectManager->drawOBJ(dataMap.tile[i][j].objIndex[k], cenTile, cenScreen, offset);
				}
				for (size_t k = 0; k < dataMap.tile[i][j].npcIndex.size(); k++)
				{
					if (dataMap.tile[i][j].npcIndex[k] == 0)
					{
						if (!gm->player->isJumping() || gm->player->getJumpState() != jsJumping)
						{
							gm->player->draw(cenTile, cenScreen, offset);
						}
					}
					else
					{
						gm->npcManager->drawNPC(dataMap.tile[i][j].npcIndex[k] - 1, cenTile, cenScreen, offset);
					}
				}
			}
		}
		line = std::abs(iy) % 2;
		cx = cenTile.x + line;
		line = 1 - line;
		for (int ix = 1; ix < xscal * 2 + 2; ix++)
		{
			int i = iy + ix;
			int j = cx;
			cx += line;
			line = 1 - line;

			if (i >= cenTile.y - yscal && i < cenTile.y + yscal + tileHeightScal && j >= cenTile.x - xscal && j < cenTile.x + xscal)
			{
				if (data == nullptr || j < 0 || j >= data->head.width || i < 0 || i >= data->head.height)
				{
					continue;
				}

				drawTile(1, { j, i }, cenTile, cenScreen, offset);

				for (size_t k = 0; k < dataMap.tile[i][j].objIndex.size(); k++)
				{
					gm->objectManager->drawOBJ(dataMap.tile[i][j].objIndex[k], cenTile, cenScreen, offset);
				}
				for (size_t k = 0; k < dataMap.tile[i][j].npcIndex.size(); k++)
				{
					if (dataMap.tile[i][j].npcIndex[k] == 0)
					{
						if (!gm->player->isJumping() || gm->player->getJumpState() != jsJumping)
						{
							gm->player->draw(cenTile, cenScreen, offset);
						}
					}
					else
					{
						gm->npcManager->drawNPC(dataMap.tile[i][j].npcIndex[k] - 1, cenTile, cenScreen, offset);
					}
				}
			}
		}
		line = std::abs(iy) % 2;
		cx = cenTile.x;
		for (int ix = 0; ix < xscal * 2; ix++)
		{
			int i = iy + ix;
			int j = cx;
			cx += line - 1;
			line = 1 - line;
			if (i >= cenTile.y - yscal && i < cenTile.y + yscal + tileHeightScal && j >= cenTile.x - xscal && j < cenTile.x + xscal)
			{
				if (data == nullptr || j < 0 || j >= data->head.width || i < 0 || i >= data->head.height)
				{
					continue;
				}
				for (size_t k = 0; k < emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index.size(); k++)
				{
					if (gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]] != nullptr)
					{
						if ((float)gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->flyingDirection.x / TILE_WIDTH + (float)gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->flyingDirection.y / TILE_WIDTH <= 0)
						{
							gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->draw(cenTile, cenScreen, offset);
						}

					}
				}
			}
		}
		line = std::abs(iy) % 2;
		cx = cenTile.x + line;
		line = 1 - line;
		for (int ix = 1; ix < xscal * 2; ix++)
		{
			int i = iy + ix;
			int j = cx;
			cx += line;
			line = 1 - line;
			if (i >= cenTile.y - yscal && i < cenTile.y + yscal + tileHeightScal && j >= cenTile.x - xscal && j < cenTile.x + xscal)
			{
				if (data == nullptr || j < 0 || j >= data->head.width || i < 0 || i >= data->head.height)
				{
					continue;
				}
				for (size_t k = 0; k < emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index.size(); k++)
				{
					if (gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]] != nullptr)
					{
						if (-(float)gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->flyingDirection.x / TILE_WIDTH + (float)gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->flyingDirection.y / TILE_WIDTH <= 0)
						{
							gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->draw(cenTile, cenScreen, offset);
						}
					}
				}
			}
		}

	}
	*/
	/*
	for (int i = cenTile.y - yscal; i < cenTile.y + yscal + tileHeightScal; i++)
	{
		for (int j = cenTile.x - xscal; j < cenTile.x + xscal; j++)
		{
			//npcManager->draw(tile, cenTile, cenScreen, offset);
			if (data == nullptr || j < 0 || j >= data->head.width || i < 0 || i >= data->head.height)
			{
				continue;
			}
			drawTile(1, { j, i }, cenTile, cenScreen, offset);

			for (size_t k = 0; k < dataMap.tile[i][j].objIndex.size(); k++)
			{
				gm->objectManager->drawOBJ(dataMap.tile[i][j].objIndex[k], cenTile, cenScreen, offset);
			}
			for (size_t k = 0; k < dataMap.tile[i][j].npcIndex.size(); k++)
			{
				if (dataMap.tile[i][j].npcIndex[k] == 0)
				{
					if (!gm->player->isJumping() || gm->player->getJumpState() != jsJumping)
					{
						gm->player->draw(cenTile, cenScreen, offset);
					}
				}
				else
				{
					gm->npcManager->drawNPC(dataMap.tile[i][j].npcIndex[k] - 1, cenTile, cenScreen, offset);
				}
			}
			for (size_t k = 0; k < emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index.size(); k++)
			{
				if (gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]] != nullptr)
				{
					gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->draw(cenTile, cenScreen, offset);
				}
			}
		}
		for (int j = cenTile.x - xscal; j < cenTile.x + xscal; j++)
		{
			for (size_t k = 0; k < emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index.size(); k++)
			{
				if (gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]] != nullptr)
				{
					gm->effectManager->effectList[emap.tile[i - (cenTile.y - yscal)][j - (cenTile.x - xscal)].index[k]]->draw(cenTile, cenScreen, offset);
				}
			}
		}
	}
	*/
	/*
	for (int i = 0; i < yscal * 2 + tileHeightScal; i++)
	{
		for (int j = 0; j < 2 * xscal; j++)
		{
			for (int k = 0; k < (int)emap.tile[i][j].index.size(); k++)
			{
				if (gm->effectManager->effectList[emap.tile[i][j].index[k]] != nullptr)
				{
					gm->effectManager->effectList[emap.tile[i][j].index[k]]->draw(cenTile, cenScreen, offset);
				}
			}
		}
	}
	*/

	for (int i = cenTile.y - yscal; i < cenTile.y + yscal + tileHeightScal; i++)
	{
		for (int j = cenTile.x - xscal; j < cenTile.x + xscal; j++)
		{
			if (data == nullptr || j < 0 || j >= data->head.width || i < 0 || i >= data->head.height)
			{
				continue;
			}
			Point tile = { j, i };
			drawTile(2, tile, cenTile, cenScreen, offset, mapColorStyle);
		}
	}

	if (!gm->objectManager->drawOBJSelectedAlpha(cenTile, cenScreen, offset))
	{
		gm->npcManager->drawNPCSelectedAlpha(cenTile, cenScreen, offset);
	}
	if (gm->player->isJumping() && gm->player->getJumpState() == jsJumping)
	{
		gm->player->draw(cenTile, cenScreen, offset, actorColorStyle);
		for (size_t i = 0; i < gm->effectManager->effectList.size(); i++)
		{
			auto effect = gm->effectManager->effectList[i];
			if (effect != nullptr && effect->getMoveKind() == mmkFollow)
			{
				auto followTarget = effect->target.lock();
				if (followTarget == gm->player)
				{
					effect->draw(cenTile, cenScreen, offset, effectColorStyle);
				}
			}
		}
	}
	else if (Config::playerAlpha)
	{
		gm->player->drawAlpha(cenTile, cenScreen, offset);
	}

	if (gm->global.data.waterEffect)
	{
		PointEx cameraPos;
		Point cameraPosInt = getTilePosition(gm->camera->position, {0, 0});
		cameraPos.x = static_cast<float>(cameraPosInt.x) + gm->camera->offset.x;
		cameraPos.y = static_cast<float>(cameraPosInt.y) + gm->camera->offset.y;
		waterEffect.renderEffect(getTime(), cameraPos);
	}
}

bool Map::warmVisibleTextures(
	int viewportWidth,
	int viewportHeight,
	Point centerTile,
	const std::function<bool()>& checkpoint,
	std::size_t maximumFramesPerCheckpoint,
	std::size_t* warmedFrameCount,
	std::size_t* attemptedFrameCount)
{
	if (warmedFrameCount != nullptr)
	{
		*warmedFrameCount = 0;
	}
	if (attemptedFrameCount != nullptr)
	{
		*attemptedFrameCount = 0;
	}
	if (data == nullptr || mapMpc == nullptr ||
		viewportWidth <= 0 || viewportHeight <= 0)
	{
		return true;
	}

	maximumFramesPerCheckpoint =
		std::max<std::size_t>(maximumFramesPerCheckpoint, 1);
	const Point centerScreen =
	{
		viewportWidth / 2,
		viewportHeight / 2
	};
	const int horizontalScale =
		centerScreen.x / TILE_WIDTH + 2 +
		LUM_MASK_WIDTH / TILE_WIDTH + 1;
	const int verticalScale =
		centerScreen.y / TILE_HEIGHT * 2 + 2 +
		LUM_MASK_HEIGHT / TILE_HEIGHT + 1;
	constexpr int TileHeightScale = 15;
	const UTime currentTime = getTime();
	std::unordered_set<const IMPFrame*> visitedFrames;
	std::size_t framesSinceCheckpoint = 0;
	std::size_t warmedCount = 0;
	std::size_t attemptedCount = 0;

	const auto checkpointCanContinue = [&]()
	{
		if (!checkpoint)
		{
			return true;
		}
		try
		{
			return checkpoint();
		}
		catch (...)
		{
			return false;
		}
	};
	const auto warmTile =
		[this,
		 currentTime,
		 &visitedFrames,
		 &framesSinceCheckpoint,
		 &warmedCount,
		 &attemptedCount,
		 maximumFramesPerCheckpoint,
		 &checkpointCanContinue](int layer, int x, int y)
		{
			const MapTileLayer& tileLayer = data->tile[y][x].layer[layer];
			if (tileLayer.mpc == 0)
			{
				return true;
			}
			const Mpc& resource = mapMpc->mpc[tileLayer.mpc - 1];
			const _shared_imp& image = resource.img;
			if (image == nullptr || image->frame.empty())
			{
				return true;
			}

			std::optional<std::size_t> frameIndex;
			if (data->mpc.mpc[tileLayer.mpc - 1].dynamic != 0)
			{
				frameIndex = ImageAnimationPlayback::frameIndex(
					image->frame.size(),
					1,
					0,
					currentTime,
					image->interval);
			}
			else if (static_cast<std::size_t>(tileLayer.frame) <
				image->frame.size())
			{
				frameIndex = static_cast<std::size_t>(tileLayer.frame);
			}
			if (!frameIndex.has_value())
			{
				return true;
			}

			IMPFrame& frame = image->frame[*frameIndex];
			if (!visitedFrames.insert(&frame).second || frame.image != nullptr)
			{
				return true;
			}
			if ((frame.data == nullptr || frame.dataLen <= 0) &&
				frame.pixelData.empty())
			{
				return true;
			}
			if (framesSinceCheckpoint >= maximumFramesPerCheckpoint)
			{
				if (!checkpointCanContinue())
				{
					return false;
				}
				framesSinceCheckpoint = 0;
			}
			const _shared_image loadedImage = IMP::loadImage(
				image, static_cast<int>(*frameIndex));
			++framesSinceCheckpoint;
			++attemptedCount;
			if (loadedImage != nullptr)
			{
				++warmedCount;
			}
			return true;
		};
	const auto publishCounts = [&]()
	{
		if (warmedFrameCount != nullptr)
		{
			*warmedFrameCount = warmedCount;
		}
		if (attemptedFrameCount != nullptr)
		{
			*attemptedFrameCount = attemptedCount;
		}
	};

	for (int layer = 0; layer < MAP_TILE_LAYER; ++layer)
	{
		for (int y = centerTile.y - verticalScale;
			y < centerTile.y + verticalScale + TileHeightScale;
			++y)
		{
			for (int x = centerTile.x - horizontalScale;
				x < centerTile.x + horizontalScale;
				++x)
			{
				if (x < 0 || x >= data->head.width ||
					y < 0 || y >= data->head.height)
				{
					continue;
				}
				if (!warmTile(layer, x, y))
				{
					publishCounts();
					return false;
				}
			}
		}
	}
	if (framesSinceCheckpoint > 0 && !checkpointCanContinue())
	{
		publishCounts();
		return false;
	}
	publishCounts();
	return true;
}

void Map::createDataMap()
{
	int w, h;
	if (data == nullptr)
	{
		dataMap.tile.clear();
		return;
	}
	w = data->head.width;
	h = data->head.height;
	if (w <= 0 || h <= 0)
	{
		dataMap.tile.clear();
		return;
	}
	const bool currentDimensionsMatch =
		dataMap.tile.size() ==
			static_cast<std::size_t>(h) &&
		std::all_of(
			dataMap.tile.cbegin(),
			dataMap.tile.cend(),
			[w](const std::vector<DataTile>& row)
			{
				return row.size() ==
					static_cast<std::size_t>(w);
			});
	if (currentDimensionsMatch)
	{
		for (auto& row : dataMap.tile)
		{
			for (auto& tile : row)
			{
				tile.npcList.clear();
				tile.objList.clear();
				tile.stepNPCList.clear();
			}
		}
	}
	else
	{
		dataMap.tile.clear();
		dataMap.tile.resize(static_cast<std::size_t>(h));
		for (auto& row : dataMap.tile)
		{
			row.resize(static_cast<std::size_t>(w));
		}
	}
	if (gm == nullptr || gm->npcManager == nullptr ||
		gm->objectManager == nullptr)
	{
		return;
	}

	auto addNPCToDataMapWithStep = [this, w, h](std::shared_ptr<NPC> npc, int index) {
		if (!npc) return;
		if (!npc->isVisibleByVariable) return;
		if (npc->isHiding()) return;
		Point pos = npc->getPosition();
		if (pos.x >= 0 && pos.x < w && pos.y >= 0 && pos.y < h)
		{
			dataMap.tile[pos.y][pos.x].npcList.push_back(npc);
			std::vector<Point> stepPositions = npc->getStepPositions();
			for (const Point& stepPos : stepPositions)
			{
				if (stepPos.x >= 0 && stepPos.x < w && stepPos.y >= 0 && stepPos.y < h)
				{
					addStepToDataMap(stepPos, npc);
				}
			}
		}
	};

	addNPCToDataMapWithStep(gm->player, 0);
	for (size_t i = 0; i < gm->npcManager->npcList.size(); i++)
	{
		addNPCToDataMapWithStep(gm->npcManager->npcList[i], static_cast<int>(i + 1));
	}

	for (size_t i = 0; i < gm->objectManager->objectList.size(); i++)
	{
		if (gm->objectManager->objectList[i] != nullptr && gm->objectManager->objectList[i]->position.x >= 0 && gm->objectManager->objectList[i]->position.x < w && gm->objectManager->objectList[i]->position.y >= 0 && gm->objectManager->objectList[i]->position.y < h)
		{
			dataMap.tile[gm->objectManager->objectList[i]->position.y][gm->objectManager->objectList[i]->position.x].objList.push_back(gm->objectManager->objectList[i]);
		}
	}
}

void Map::deleteObjectFromDataMap(Point pos, std::shared_ptr<Object> obj)
{
	if (isInMap(pos) && pos.y < (int)dataMap.tile.size() && pos.x < (int)dataMap.tile[pos.y].size())
	{
		dataMap.tile[pos.y][pos.x].objList.remove(obj);
	}
}

void Map::addObjectToDataMap(Point pos, std::shared_ptr<Object> obj)
{
	if (isInMap(pos) && pos.y < (int)dataMap.tile.size() && pos.x < (int)dataMap.tile[pos.y].size())
	{
		dataMap.tile[pos.y][pos.x].objList.push_back(obj);
	}
}

void Map::deleteStepFromDataMap(Point pos, std::shared_ptr<NPC> npc)
{
	if (isInMap(pos) && pos.y < (int)dataMap.tile.size() && pos.x < (int)dataMap.tile[pos.y].size())
	{
		dataMap.tile[pos.y][pos.x].stepNPCList.remove(npc);
	}
}

void Map::addStepToDataMap(Point pos, std::shared_ptr<NPC> npc)
{
	if (isInMap(pos) && pos.y < (int)dataMap.tile.size() && pos.x < (int)dataMap.tile[pos.y].size())
	{
		dataMap.tile[pos.y][pos.x].stepNPCList.push_back(npc);
	}
}

void Map::deleteNPCFromDataMap(Point pos, std::shared_ptr<NPC> npc)
{
	if (isInMap(pos) && pos.y < (int)dataMap.tile.size() && pos.x < (int)dataMap.tile[pos.y].size())
	{
		dataMap.tile[pos.y][pos.x].npcList.remove(npc);
	}
}

void Map::addNPCToDataMap(Point pos, std::shared_ptr<NPC> npc)
{
	if (isInMap(pos) && pos.y < (int)dataMap.tile.size() && pos.x < (int)dataMap.tile[pos.y].size())
	{
		dataMap.tile[pos.y][pos.x].npcList.push_back(npc);
	}
}


void Map::generateThumbnail()
{
	if (data == nullptr || mapMpc == nullptr)
	{
		thumbnailSourceRect = { 0, 0, 0, 0 };
		return;
	}

	int mapWidth = data->head.width;
	int mapHeight = data->head.height;
	if (mapWidth <= 0 || mapHeight <= 0)
	{
		thumbnailSourceRect = { 0, 0, 0, 0 };
		return;
	}

	int tilePixelWidth = mapWidth * TILE_WIDTH;
	int tilePixelHeight = (mapHeight - 1) * TILE_HEIGHT / 2;

	int paddingX = TILE_WIDTH;
	int paddingY = TILE_HEIGHT;

	int canvasWidth = tilePixelWidth + paddingX * 2;
	int canvasHeight = tilePixelHeight + paddingY * 2;

	auto canvas = engine->createCanvasImage(canvasWidth, canvasHeight);
	if (canvas == nullptr)
	{
		return;
	}
	SDL_SetTextureBlendMode(canvas.get(), SDL_BLENDMODE_BLEND);

	auto originalTarget = engine->getRenderTarget();
	if (!engine->setSharedImageAsRenderTarget(canvas))
	{
		return;
	}
	engine->renderClear(0, 0, 0, 0);

	Point cenScreen = { paddingX, paddingY };
	Point cenTile = { 0, 0 };
	PointEx offset = { 0.0, 0.0 };

	for (int i = 0; i < mapHeight; i++)
	{
		for (int j = 0; j < mapWidth; j++)
		{
			Point tile = { j, i };
			for (int layer = 0; layer < MAP_TILE_LAYER; layer++)
			{
				drawTile(layer, tile, cenTile, cenScreen, offset, 0xFFFFFF);
			}
		}
	}

	int thumbnailWidth = MapThumbnailStyle::CanvasWidth;
	int thumbnailHeight = MapThumbnailStyle::CanvasHeight;
	auto thumbnailCanvas = engine->createCanvasImage(thumbnailWidth, thumbnailHeight);
	if (thumbnailCanvas != nullptr)
	{
		if (!engine->setSharedImageAsRenderTarget(
			thumbnailCanvas))
		{
			(void)engine->
				restoreImageRenderTargetAfterAcceptedOperation(
					originalTarget,
					canvas);
			return;
		}
		engine->renderClear(0, 0, 0, 0);

		Rect srcRect = { paddingX, paddingY, tilePixelWidth - TILE_WIDTH / 2, tilePixelHeight - TILE_HEIGHT / 2 };
		thumbnailSourceRect = srcRect;
		drawFeatheredThumbnail(engine, canvas, srcRect, canvasWidth, canvasHeight,
			thumbnailWidth, thumbnailHeight);

		if (!engine->
			restoreImageRenderTargetAfterAcceptedOperation(
				originalTarget,
				thumbnailCanvas))
		{
			return;
		}
		SDL_SetTextureBlendMode(thumbnailCanvas.get(), SDL_BLENDMODE_BLEND);

		thumbnailImage = IMP::createIMPImageFromImage(thumbnailCanvas);
	}
	else
	{
		if (!engine->
			restoreImageRenderTargetAfterAcceptedOperation(
				originalTarget,
				canvas))
		{
			return;
		}
		thumbnailSourceRect = { 0, 0, canvasWidth, canvasHeight };
		thumbnailImage = IMP::createIMPImageFromImage(canvas);
	}
}

void Map::freeResource()
{
	thumbnailImage = nullptr;
	thumbnailSourceRect = { 0, 0, 0, 0 };
	dataMap.tile.clear();
	freeMpc();
	freeData();
}

void Map::freeMpc()
{
	if (mapMpc != nullptr)
	{
		for (size_t i = 0; i < MAP_MPC_COUNT; i++)
		{
			mapMpc->mpc[i].img = nullptr;
		}
		mapMpc = nullptr;
	}
}

void Map::freeData()
{
	if (data != nullptr)
	{
		data->tile.resize(0);
		for (size_t i = 0; i < MAP_MPC_COUNT; i++)
		{
			if (data->mpc.mpc[i].name.get() != nullptr)
			{
				data->mpc.mpc[i].name = std::unique_ptr<char[]>(nullptr);
			}
		}
		data = nullptr;
	}
}

void Map::drawTile(int layer, Point tile, Point cenTile, Point cenScreen, PointEx offset, uint32_t colorStyle)
{
	if (!isInMap(tile))
	{
		return;
	}
	if (data->tile[tile.y][tile.x].layer[layer].mpc == 0)
	{
		return;
	}
	int x, y;
	Point point = getTilePosition(tile, cenTile, cenScreen, offset);
	_shared_image img = nullptr;
	if (data->mpc.mpc[data->tile[tile.y][tile.x].layer[layer].mpc - 1].dynamic != 0)
	{
		img = IMP::loadImageForTime(mapMpc->mpc[data->tile[tile.y][tile.x].layer[layer].mpc - 1].img, getTime(), &x, &y);
		
	}
	else
	{
		img = IMP::loadImage(mapMpc->mpc[data->tile[tile.y][tile.x].layer[layer].mpc - 1].img, data->tile[tile.y][tile.x].layer[layer].frame, &x, &y);
	}
	if (img == nullptr)
	{
		return;
	}
	ColorStyle::drawImage(engine, img, point.x - x, point.y - y, colorStyle);
}

bool Map::isInMap(PathMap* pathMap, Point pos)
{
	if (pos.x < 0 || pos.y < 0 || pos.x >= pathMap->w || pos.y >= pathMap->h)
	{
		return false;
	}
	return true;
}

bool Map::isInMap(Point pos)
{
	if (data == nullptr)
	{
		return false;
	}
	if (pos.x < 0 || pos.y < 0 || pos.x >= data->head.width || pos.y >= data->head.height)
	{
		return false;
	}
	return true;
}

Point Map::clampToWalkable(Point pos)
{
	if (data == nullptr)
	{
		return { 0, 0 };
	}
	int width = data->head.width;
	int height = data->head.height;
	if (width <= 0 || height <= 0)
	{
		return { 0, 0 };
	}
	if (isInMap(pos) && canWalk(pos))
	{
		return pos;
	}
	int clampedX = pos.x < 0 ? 0 : (pos.x >= width ? width - 1 : pos.x);
	int clampedY = pos.y < 0 ? 0 : (pos.y >= height ? height - 1 : pos.y);
	int maxRadius = std::max(width, height);
	for (int radius = 0; radius < maxRadius; radius++)
	{
		for (int dy = -radius; dy <= radius; dy++)
		{
			for (int dx = -radius; dx <= radius; dx++)
			{
				if (std::abs(dx) != radius && std::abs(dy) != radius)
				{
					continue;
				}
				Point candidate = { clampedX + dx, clampedY + dy };
				if (isInMap(candidate) && canWalk(candidate))
				{
					return candidate;
				}
			}
		}
	}
	return { clampedX, clampedY };
}

void Map::addWaterRipple(float x, float y)
{
	auto now = getTime();
	Point cameraPosInt = getTilePosition(gm->camera->position, { 0, 0 });
	PointEx cameraPos = { static_cast<float>(cameraPosInt.x) + gm->camera->offset.x, static_cast<float>(cameraPosInt.y) + gm->camera->offset.y };
	waterEffect.addDefaultClickRipple(cameraPos.x + x, cameraPos.y + y, now);
}

float Map::calFlyDirection(Point flyDirection)
{
	float angle;
	if (flyDirection.x == 0)
	{
		if (flyDirection.y > 0)
		{
			angle = M_PI * 3 / 2;
		}
		else
		{
			angle = M_PI / 2;
		}
	}
	else if (flyDirection.y == 0)
	{
		if (flyDirection.x > 0)
		{
			angle = 0;
		}
		else
		{
			angle = M_PI;
		}
	}
	else
	{
		//angle = atan2((float)-flyDirection.y * TILE_HEIGHT / TILE_WIDTH, (float)flyDirection.x);
		angle = atan2((float)-flyDirection.y, (float)flyDirection.x);
		if (angle < 0)
		{
			angle += 2 * M_PI;
		}
	}
	return angle;
}

LinePathPoint Map::getLineSubStepEx(Point from, PointEx fromOffset, float angle)
{
	LinePathPoint result;
	result.pos = from;
	result.pixelOffset = fromOffset;
	result.pixelOffset.x /= (TILE_WIDTH / 2 / MapXRatio);
	result.pixelOffset.y /= (TILE_HEIGHT / 2);
	int dir = 0;
	if (angle <= M_PI / 4 || angle > M_PI * 7 / 4)
	{
		auto p = atan2(result.pixelOffset.y, 1.0 - result.pixelOffset.x);
		if (angle > M_PI)
		{
			angle -= 2 * M_PI;
		}
		if (p > angle)
		{
			result.pos = getSubPoint(from, 7);
			dir = 7;
		}
		else
		{
			result.pos = getSubPoint(from, 5);
			dir = 5;
		}
		if (angle < 0)
		{
			angle += 2 * M_PI;
		}
	}
	else if (angle <= M_PI * 3 / 4)
	{
		auto p = atan2(1.0 + result.pixelOffset.y, -result.pixelOffset.x);
		if (p < 0)
		{
			p += 2 * M_PI;
		}
		if (p > angle)
		{
			result.pos = getSubPoint(from, 5);
			dir = 5;
		}
		else
		{
			result.pos = getSubPoint(from, 3);
			dir = 3;
		}
	}
	else if (angle <= M_PI * 5 / 4)
	{
		auto p = atan2(result.pixelOffset.y, -1.0 - result.pixelOffset.x);
		if (p < 0)
		{
			p += 2 * M_PI;
		}
		if (p > angle)
		{
			result.pos = getSubPoint(from, 3);
			dir = 3;
		}
		else
		{
			result.pos = getSubPoint(from, 1);
			dir = 1;
		}
	}
	else
	{
		auto p = atan2(-1.0 + result.pixelOffset.y, -result.pixelOffset.x);
		if (p < 0)
		{
			p += 2 * M_PI;
		}
		if (p > angle)
		{
			result.pos = getSubPoint(from, 1);
			dir = 1;
		}
		else
		{
			result.pos = getSubPoint(from, 7);
			dir = 7;
		}
	}
	auto newpos = getTilePosition(result.pos, from);
	result.pixelOffset.x -= float(newpos.x) / (TILE_WIDTH / 2);
	result.pixelOffset.y -= float(newpos.y) / (TILE_HEIGHT / 2);
	result.pixelOffset.x *= (TILE_WIDTH / 2);
	result.pixelOffset.y *= (TILE_HEIGHT / 2);
	return result;
}

std::vector<Point> Map::getLineSubStep(Point from, Point to, float angle)
{
	std::vector<Point> result;
	result.resize(0);
	if (from == to)
	{
		result.push_back(from);
		return result;
	}
	//angle以向右为x、向上为y正方向的坐标系，与屏幕向下y增大的方向相反
	//向右时
	int line = std::abs(from.y % 2);
	if (angle == 0.0 || angle == 2 * M_PI)
	{
		Point pos;
		pos.x = from.x + line;
		pos.y = from.y - 1;
		result.push_back(pos);
		pos.x = from.x + line;
		pos.y = from.y + 1;
		result.push_back(pos);
		pos.x = from.x + 1;
		pos.y = from.y;
		result.push_back(pos);
	}
	//向上时
	else if (angle == M_PI / 2)
	{
		Point pos;
		pos.x = from.x - 1 + line;
		pos.y = from.y - 1;
		result.push_back(pos);
		pos.x = from.x + line;
		pos.y = from.y - 1;
		result.push_back(pos);
		pos.x = from.x;
		pos.y = from.y - 2;
		result.push_back(pos);
	}
	//向左时
	else if (angle == M_PI)
	{
		Point pos;
		pos.x = from.x - 1 + line;
		pos.y = from.y - 1;
		result.push_back(pos);
		pos.x = from.x - 1 + line;
		pos.y = from.y + 1;
		result.push_back(pos);
		pos.x = from.x - 1;
		pos.y = from.y;
		result.push_back(pos);
	}
	//向下时
	else if (angle == M_PI * 3 / 2)
	{
		Point pos;
		pos.x = from.x - 1 + line;
		pos.y = from.y + 1;
		result.push_back(pos);
		pos.x = from.x + line;
		pos.y = from.y + 1;
		result.push_back(pos);
		pos.x = from.x;
		pos.y = from.y + 2;
		result.push_back(pos);
	}
	else
	{
		Point newPos = getTilePosition(to, from, { 0, 0 }, { 0, 0 });
		float newAngle = atan2((float)-newPos.y * ((float)TILE_WIDTH / TILE_HEIGHT), (float)newPos.x);
		if (newAngle < 0)
		{
			newAngle += 2 * M_PI;
		}
		if (angle == atan2(1, 1))
		{
			Point pos;
			pos.x = from.x + line;
			pos.y = from.y - 1;
			result.push_back(pos);
		}
		else if (angle == atan2(1, -1))
		{
			Point pos;
			pos.x = from.x - 1 + line;
			pos.y = from.y - 1;
			result.push_back(pos);
		}
		else if (angle == atan2(-1, -1))
		{
			Point pos;
			pos.x = from.x - 1 + line;
			pos.y = from.y + 1;
			result.push_back(pos);
		}
		else if (angle == atan2(-1, 1))
		{
			Point pos;
			pos.x = from.x + line;
			pos.y = from.y + 1;
			result.push_back(pos);
		}
		else if (angle < M_PI / 4 || angle > 7 * M_PI / 4)
		{
			if (angle > M_PI)
			{
				angle -= 2 * M_PI;
			}
			if (newAngle > M_PI)
			{
				newAngle -= 2 * M_PI;
			}
			if (newAngle < angle)
			{
				Point pos;
				pos.x = from.x + line;
				pos.y = from.y + 1;
				result.push_back(pos);
			}
			else if (newAngle > angle)
			{
				Point pos;
				pos.x = from.x + line;
				pos.y = from.y - 1;
				result.push_back(pos);
			}
			else
			{
				if (angle < 0)
				{
					Point pos;
					pos.x = from.x + line;
					pos.y = from.y + 1;
					result.push_back(pos);
				}
				else
				{
					Point pos;
					pos.x = from.x + line;
					pos.y = from.y - 1;
					result.push_back(pos);
				}
			}
		}
		else if (angle > M_PI / 4 && angle < 3 * M_PI / 4)
		{
			if (newAngle < angle)
			{
				Point pos;
				pos.x = from.x + line;
				pos.y = from.y - 1;
				result.push_back(pos);
			}
			else if (newAngle > angle)
			{
				Point pos;
				pos.x = from.x - 1 + line;
				pos.y = from.y - 1;
				result.push_back(pos);
			}
			else
			{
				if (angle < M_PI / 2)
				{
					Point pos;
					pos.x = from.x + line;
					pos.y = from.y - 1;
					result.push_back(pos);
				}
				else
				{
					Point pos;
					pos.x = from.x - 1 + line;
					pos.y = from.y - 1;
					result.push_back(pos);
				}
			}
		}
		else if (angle > 3 * M_PI / 4 && angle < 5 * M_PI / 4)
		{
			if (newAngle < angle)
			{
				Point pos;
				pos.x = from.x - 1 + line;
				pos.y = from.y - 1;
				result.push_back(pos);
			}
			else if (newAngle > angle)
			{
				Point pos;
				pos.x = from.x - 1 + line;
				pos.y = from.y + 1;
				result.push_back(pos);
			}
			else
			{
				if (angle < M_PI)
				{
					Point pos;
					pos.x = from.x - 1 + line;
					pos.y = from.y - 1;
					result.push_back(pos);
				}
				else
				{
					Point pos;
					pos.x = from.x - 1 + line;
					pos.y = from.y + 1;
					result.push_back(pos);
				}
			}
		}
		else
		{
			if (newAngle < angle)
			{
				Point pos;
				pos.x = from.x - 1 + line;
				pos.y = from.y + 1;
				result.push_back(pos);
			}
			else if (newAngle > angle)
			{
				Point pos;
				pos.x = from.x + line;
				pos.y = from.y + 1;
				result.push_back(pos);
			}
			else
			{
				if (angle < M_PI * 3 / 2)
				{
					Point pos;
					pos.x = from.x - 1 + line;
					pos.y = from.y + 1;
					result.push_back(pos);
				}
				else
				{
					Point pos;
					pos.x = from.x + line;
					pos.y = from.y + 1;
					result.push_back(pos);
				}
			}
		}
	}
	return result;
}

bool Map::getSlantPath(std::vector<Point>& subStep, int line, PathMap* pathMap, Point from, Point to, int stepIndex)
{
	Point pos;
	for (int i = line - 1; i < line + 1; i++)
	{
		for (int j = -1; j < 2; j += 2)
		{
			pos.x = from.x + i;
			pos.y = from.y + j;
			if (pos == to)
			{
				pathMap->map[pos.y][pos.x].from = from;
				pathMap->map[pos.y][pos.x].index = stepIndex;
				subStep.push_back(pos);
				return true;
			}
			else if (isInMap(pathMap, pos) && canWalk(pos))
			{
				if (pathMap->map[pos.y][pos.x].index < 0)
				{
					pathMap->map[pos.y][pos.x].from = from;
					pathMap->map[pos.y][pos.x].index = stepIndex;
					auto tileCost = getTileDistance(pos, { 0, 0 }, from, { 0, 0 });
					pathMap->map[pos.y][pos.x].cost = tileCost + pathMap->map[from.y][from.x].cost;
					subStep.push_back(pos);
				}
				else if (pathMap->map[pos.y][pos.x].index == stepIndex)
				{
					auto tileCost = getTileDistance(pos, { 0, 0 }, from, { 0, 0 });
					auto totalCost = tileCost + pathMap->map[from.y][from.x].cost;
					if (pathMap->map[pos.y][pos.x].cost > totalCost)
					{
						pathMap->map[pos.y][pos.x].from = from;
						//pathMap->map[pos.y][pos.x].index = stepIndex;
						pathMap->map[pos.y][pos.x].cost = totalCost;
					}
				}
			}
		}
	}
	return false;
}

bool Map::getVHPath(std::vector<Point>& subStep, int line, PathMap* pathMap, Point from, Point to, int stepIndex)
{
	Point pos;
	for (int i = 0; i < 4; i++)
	{
		pos.x = (i - 1) % 2 + from.x;
		pos.y = ((i - 2) % 2) * 2 + from.y;
		if ((isInMap(pathMap, pos) && canWalk(pos) && (pathMap->map[pos.y][pos.x].index < 0 || pathMap->map[pos.y][pos.x].index == stepIndex)) || (pos == to))
		{
			bool canGo = false;
			if (i == 0)
			{
				Point tempPos1, tempPos2;
				tempPos1.x = from.x - 1 + line;
				tempPos1.y = from.y - 1;
				tempPos2.x = from.x - 1 + line;
				tempPos2.y = from.y + 1;
				if (canPass(tempPos1) && canPass(tempPos2))
				{
					canGo = true;
				}
			}
			else if (i == 1)
			{
				Point tempPos1, tempPos2;
				tempPos1.x = from.x - 1 + line;
				tempPos1.y = from.y - 1;
				tempPos2.x = from.x + line;
				tempPos2.y = from.y - 1;
				if (canPass(tempPos1) && canPass(tempPos2))
				{
					canGo = true;
				}
			}
			else if (i == 2)
			{
				Point tempPos1, tempPos2;
				tempPos1.x = from.x + line;
				tempPos1.y = from.y - 1;
				tempPos2.x = from.x + line;
				tempPos2.y = from.y + 1;
				if (canPass(tempPos1) && canPass(tempPos2))
				{
					canGo = true;
				}

			}
			else if (i == 3)
			{
				Point tempPos1, tempPos2;
				tempPos1.x = from.x - 1 + line;
				tempPos1.y = from.y + 1;
				tempPos2.x = from.x + line;
				tempPos2.y = from.y + 1;
				if (canPass(tempPos1) && canPass(tempPos2))
				{
					canGo = true;
				}

			}
			if (canGo)
			{
				if (pos == to)
				{
					pathMap->map[pos.y][pos.x].from = from;
					pathMap->map[pos.y][pos.x].index = stepIndex;
					subStep.push_back(pos);
					return true;
				}
				else
				{
					if (pathMap->map[pos.y][pos.x].index < 0)
					{
						pathMap->map[pos.y][pos.x].from = from;
						pathMap->map[pos.y][pos.x].index = stepIndex;
						auto tileCost = getTileDistance(pos, { 0, 0 }, from, { 0, 0 });
						pathMap->map[pos.y][pos.x].cost = tileCost + pathMap->map[from.y][from.x].cost;
						subStep.push_back(pos);
					}
					else if (pathMap->map[pos.y][pos.x].index == stepIndex)
					{
						auto tileCost = getTileDistance(pos, { 0, 0 }, from, { 0, 0 });
						auto totalCost = tileCost + pathMap->map[from.y][from.x].cost;
						if (pathMap->map[pos.y][pos.x].cost > totalCost)
						{
							pathMap->map[pos.y][pos.x].from = from;
							//pathMap->map[pos.y][pos.x].index = stepIndex;
							pathMap->map[pos.y][pos.x].cost = totalCost;
						}
					}
				}
			}
		}
	}
	return false;
}

std::vector<Point> Map::getSubStep(PathMap* pathMap, Point from, Point to, int stepIndex)
{
	std::vector<Point> subStep;
	subStep.resize(0);
	if (pathMap->map[to.y][to.x].index >= 0)
	{
		return subStep;
	}
	if (!isInMap(pathMap, from))
	{
		return subStep;
	}
	int line = std::abs(from.y % 2);

	auto dir = NPC::getDirection(from, to);
	if (dir % 2 == 0)
	{
		if (getVHPath(subStep, line, pathMap, from, to, stepIndex))
		{
			return subStep;
		}
		getSlantPath(subStep, line, pathMap, from, to, stepIndex);
	}
	else
	{
		if (getSlantPath(subStep, line, pathMap, from, to, stepIndex))
		{
			return subStep;
		}
		getVHPath(subStep, line, pathMap, from, to, stepIndex);
	}

	return subStep;

}

bool Map::compareMapHead(MapData* md)
{
	//比较文件头部数据，以确认是否为MAP文件
	if (md == nullptr)
	{
		return false;
	}

	return isMapHeader(md->head.head, MAP_HEADSTR_V3);

}
