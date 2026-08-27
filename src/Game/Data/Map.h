
#pragma once
#include <functional>
#include <deque>
#include "../GameTypes.h"
#include "../../File/File.h"
#include "../../Element/Element.h"
#include "NPC.h"
#include "Object.h"
#include "WaterEffect.h"

/*
运行时只接受结构完整的 MAP File Ver3.0。Ver2.0 只是编辑器/CLI 的迁移输入。
V3 保留 192 字节基础头；dataNil2 前 16 字节记录 headerSize、pathLen、UTF-8 flags 和
mpcCount。基础头后依次是扩展 MPC 路径、MPC 信息块和 10 字节瓦片数组；
扩展路径和 MPC 名称必须是有效 UTF-8，运行时不再猜测或转换 GBK。

以下仅记录历史 Ver2.0 布局，供迁移对照：
位置（10进制）	大小（bytes）	说明
00 - 15	16	MAP File Ver2.0
16 - 31	16	null
32 - 63	32	mpc文件路径（最多31字节 + 1个null）
64 - 67	4	地图宽 * 地图高 * 10
68 - 71	4	地图宽
72 - 75	4	地图高
76 - 79	4	推测是每个MPC的信息长度，一般为0x40(不确定)
80 - 83	4	推测是每个MPC的文件名长度，一般为0x20(不确定)
84 - 191	108	null
192 - 16511	64 * 255 = 16320	"255个mpc文件信息，一般每个64字节。这里有些地方不太确定，没验证。mpc文件名一般占32个字节，是文件名+null的格式。后面的应该是索引（1，2，3.。。）。还有：生命体设置： 00-静态 01-循环
障碍设置：00 - 空 01 - 透明 02 - 实体"
16512 - 文件末	地图宽 * 地图高 * 10	"各个地图块（10bytes）的情况，
（0，0），（1，0），（2，0）。。。。
（0，1），（1，1），（2，1）。。。。（0，2），（1，2），（2，2）。。。。
2bytes: 图层第一层[第几帧][第几个mpc文件]
2bytes：图层第二层[第几帧][第几个mpc文件]
2bytes：图层第三层[第几帧][第几个mpc文件]
1byte ：障碍： 0x80 - 障   0xA0 - 跳障（只可以跳）  0x40 - 透（只可以飞过技能） 0x60 - 跳透（可以飞过技能和跳过）
1byte ：陷阱 : 01 - 19（陷阱索引）
2bytes : 0x00 0x1F"
*/

struct DataTile
{
	std::list<std::shared_ptr<Object>> objList;
	std::list<std::shared_ptr<NPC>> npcList;
	std::list<std::shared_ptr<NPC>> stepNPCList;
};

struct DataMap
{
	std::vector<std::vector<DataTile>> tile;
};

struct LinePathPoint
{
	// 坐标
	Point pos = { 0, 0 };
	// 像素偏移
	PointEx pixelOffset = { 0, 0 };
};

struct PathTile
{
	int index = -1;
	Point from = { 0, 0 };
	float cost = 0;
};

struct PathMap
{
	int w = 0;
	int h = 0;
	std::vector<std::vector<PathTile>> map;
};

#define MAX_STEP 255

namespace MapSafety
{
inline constexpr int MaximumFileBytes = 16 * 1024 * 1024;
inline constexpr int MaximumDimension = 2048;
inline constexpr int MaximumTileCount = 1024 * 1024;
inline constexpr int MaximumMetadataBytes = 4096;
}

namespace MapThumbnailStyle
{
inline constexpr int CanvasWidth = 320;
inline constexpr int CanvasHeight = 240;
inline constexpr int FeatherPixels = 16;
}

class Map:
	public Element
{
public:
	class PreparedLoadCandidate final
	{
	public:
		PreparedLoadCandidate() = default;

		bool isValid() const noexcept
		{
			return parsedData != nullptr;
		}

	private:
		std::shared_ptr<MapData> parsedData;
		DataMap preparedDataMap;
		std::shared_ptr<MapMpc> preparedMapMpc;
		bool mapMpcPrepared = false;

		friend class Map;
	};

	Map();
	virtual ~Map();

	std::shared_ptr<MapData> data = nullptr;
	std::shared_ptr <MapMpc> mapMpc = nullptr;

	bool load(
		const std::string& fileName,
		const std::function<void()>& beforeMutation = {},
		const std::function<bool()>& preparationCheckpoint = {});
	bool load(
		std::unique_ptr<char[]>& temp_d,
		int len,
		const std::function<void()>& beforeMutation = {},
		const std::function<bool()>& preparationCheckpoint = {});
	bool validate(const std::string& fileName);
	bool validate(std::unique_ptr<char[]>& data, int length);
	static bool prepareLoadCandidate(
		const std::string& fileName,
		PreparedLoadCandidate& candidate,
		const std::function<bool()>& preparationCheckpoint = {},
		const std::string& fallbackMpcFolder = {});
	static bool prepareLoadCandidate(
		std::unique_ptr<char[]>& sourceData,
		int length,
		PreparedLoadCandidate& candidate,
		const std::function<bool()>& preparationCheckpoint = {},
		const std::string& fallbackMpcFolder = {});
	bool commitPreparedLoadCandidate(
		PreparedLoadCandidate candidate,
		const std::function<void()>& beforeMutation = {},
		const std::function<bool()>& preparationCheckpoint = {});

	static Point getElementPosition(Point pos, Point cenTile, Point cenScreen, PointEx cenTileOffset);
	static Point getMousePosition(Point mouse, Point cenTile, Point cenScreen, PointEx cenTileOffset);
	static Point getTilePosition(Point tile, Point cenTile, Point cenScreen = { 0, 0 }, PointEx cenTileOffset = { 0, 0 });
	static PointEx getTilePositionEx(Point tile, Point cenTile, Point cenScreen = { 0, 0 }, PointEx cenTileOffset = { 0, 0 });
	static Point getTileCenter(Point tile, Point cenTile, Point cenScreen, PointEx offset);
	static float getTileDistance(Point from, PointEx fromOffset, Point to, PointEx toOffset);

	void loadMapMpc();
    
    int NormalizeDirection(int direction);
    std::deque<Point> getPathAstar(Point from, Point to, int directionCount = 8, int maxTryCount = 128 * 128);
	std::deque<Point> getPathTraversal(Point from, Point to, int directionCount = 8);
	std::deque<Point> findPath(Point from, Point to, int directionCount = 8, int maxTryCount = 128 * 128);
	std::deque<Point> findSimplePath(Point from, Point to, int directionCount = 8, int maxTryCount = 100);
	std::deque<Point> getLinePath(Point from, Point to, int maxStep = 100);
	//得到距离to为radiusi数范围的点
	std::deque<Point> getRadiusPath(Point from, Point to, int radius, int directionCount = 8);
	//获得单步路径叠加
	std::deque<Point> traceTowardTarget(Point from, Point to, int stepCount, int directionCount = 8);
	//向某点方向移动一步的路径
	std::deque<Point> stepTowardTarget(Point from, Point to, int directionCount = 8);
	std::deque<Point> getPath(Point from, Point to) { return findPath(from, to); }
	std::deque<Point> getStepPath(Point from, Point to, int stepCount) { return traceTowardTarget(from, to, stepCount); }
	std::deque<Point> getStep(Point from, Point to) { return stepTowardTarget(from, to); }
	//得到起止之间的所有点
	std::deque<Point> getPassPath(Point from, Point to, Point flyDirection, Point dest);
	//得到起止之间的所有点(带偏移)
	std::deque<Point> getPassPathEx(Point from, PointEx fromOffset, Point to, PointEx toOffset, Point flyDirection);

	Point getJumpPath(Point from, Point to);
	bool canSee(Point from, Point to);

	int getTrapIndex(Point pos);
	std::string getTrapName(Point pos);
	bool haveTraps(Point pos);
	bool canWalk(Point pos);
	bool canWalkForActor(
		Point pos, const std::shared_ptr<NPC>& ignoredActor);
    bool canWalkDirectlyTo(Point pos, int dir);
	bool canJump(Point pos);
	bool canFly(Point pos);
	bool canSeeTile(Point pos);
	bool canPass(Point pos);

	static Point getSubPoint(Point from, int direction);
	static std::vector<Point> getSubPointEx(Point from, int direction);

	static int calDistance(Point from, Point to);

	DataMap dataMap;

	void drawMap();
	bool warmVisibleTextures(
		int viewportWidth,
		int viewportHeight,
		Point centerTile,
		const std::function<bool()>& checkpoint = {},
		std::size_t maximumFramesPerCheckpoint = 8,
		std::size_t* warmedFrameCount = nullptr,
		std::size_t* attemptedFrameCount = nullptr);

	void createDataMap();

	void deleteObjectFromDataMap(Point pos, std::shared_ptr<Object> obj);
	void addObjectToDataMap(Point pos, std::shared_ptr<Object> obj);

	void deleteStepFromDataMap(Point pos, std::shared_ptr<NPC> npc);
	void addStepToDataMap(Point pos, std::shared_ptr<NPC> npc);

	void deleteNPCFromDataMap(Point pos, std::shared_ptr<NPC> npc);
	void addNPCToDataMap(Point pos, std::shared_ptr<NPC> npc);

	virtual void freeResource() override;
	void freeMpc();
	void freeData();
	void drawTile(int layer, Point tile, Point cenTile, Point cenScreen, PointEx offset, uint32_t colorStyle);

	bool isInMap(Point pos);
	Point clampToWalkable(Point pos);

	void addWaterRipple(float x, float y);

	void generateThumbnail();
	_shared_imp getThumbnailImage() const { return thumbnailImage; }
	Rect getThumbnailSourceRect() const { return thumbnailSourceRect; }

private:
	std::shared_ptr<MapMpc> createMapMpc(
		const std::shared_ptr<MapData>& mapData,
		const std::function<bool()>& preparationCheckpoint = {});
	static std::shared_ptr<MapMpc> prepareMapMpc(
		const std::shared_ptr<MapData>& mapData,
		const std::string& fallbackFolder,
		const std::function<bool()>& preparationCheckpoint = {});
	static bool parsePreparedLoadCandidate(
		std::unique_ptr<char[]>& sourceData,
		int length,
		PreparedLoadCandidate& candidate,
		const std::function<bool()>& preparationCheckpoint = {},
		const std::string& fallbackMpcFolder = {},
		bool prepareMapImages = true);
	float calFlyDirection(Point flyDirection);

	bool getSlantPath(std::vector<Point>& subStep, int line, PathMap* pathMap, Point from, Point to, int stepIndex);
	bool getVHPath(std::vector<Point>& subStep, int line, PathMap* pathMap, Point from, Point to, int stepIndex);

	LinePathPoint getLineSubStepEx(Point from, PointEx fromOffset, float angle);
	std::vector<Point> getLineSubStep(Point from, Point to, float angle);
	std::vector<Point> getSubStep(PathMap * pathMap, Point from, Point to, int stepIndex);
	bool isInMap(PathMap * pathMap, Point pos);
	bool compareMapHead(MapData * md);

	WaterEffect waterEffect;

	_shared_imp thumbnailImage = nullptr;
	Rect thumbnailSourceRect = { 0, 0, 0, 0 };

};
