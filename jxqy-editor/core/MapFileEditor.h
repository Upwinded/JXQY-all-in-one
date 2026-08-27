#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <array>

#define MAP_EDITOR_HEADSTR_LEN 16
#define MAP_EDITOR_HEADSTR_V2 "MAP File Ver2.0"
#define MAP_EDITOR_HEADSTR_V3 "MAP File Ver3.0"
#define MAP_EDITOR_NULL_LEN 16
#define MAP_EDITOR_PATH_LEN 32
#define MAP_EDITOR_NULL2_LEN 108
#define MAP_EDITOR_HEAD_LEN (MAP_EDITOR_HEADSTR_LEN + MAP_EDITOR_NULL_LEN + MAP_EDITOR_PATH_LEN + 20 + MAP_EDITOR_NULL2_LEN)
#define MAP_EDITOR_MPC_COUNT 255
#define MAP_EDITOR_TILE_LAYER 3
#define MAP_EDITOR_TILE_WIDTH 64
#define MAP_EDITOR_TILE_HEIGHT 32
#define MAP_EDITOR_V2_NAME_LEN 0x20
#define MAP_EDITOR_V2_INFO_LEN 0x40
#define MAP_EDITOR_V3_PATH_LEN 256
#define MAP_EDITOR_V3_NAME_LEN 128
#define MAP_EDITOR_V3_INFO_EXTRA_LEN 32
#define MAP_EDITOR_V3_FLAG_UTF8 0x01

enum class TileObstacle
{
    Passable = 0x00,
    Trans = 0x40,
    JumpTrans = 0x60,
    Obstacle = 0x80,
    JumpOpaque = 0xA0
};

struct MapEditorHead
{
    char head[MAP_EDITOR_HEADSTR_LEN] = {0};
    char dataNil[MAP_EDITOR_NULL_LEN] = {0};
    char path[MAP_EDITOR_PATH_LEN] = {0};
    int32_t dataLen = 0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t infoLen = 0x40;
    int32_t nameLen = 0x20;
    char dataNil2[MAP_EDITOR_NULL2_LEN] = {0};
};

static_assert(sizeof(MapEditorHead) == MAP_EDITOR_HEAD_LEN, "MapEditorHead size mismatch");

struct MpcInfoData
{
    std::string name;
    int32_t index = 0;
    int32_t dynamic = 0;
    int32_t obstacle = 0;
    int32_t nil = 0;
    // Ver2.0/Ver3.0 MPC entries both reserve 16 opaque bytes after the four
    // known integer fields.  They are ignored by the current runtime, but
    // real trilogy maps contain non-zero data here, so editing must preserve
    // them instead of silently zeroing them on first save.
    std::array<uint8_t, MAP_EDITOR_V3_INFO_EXTRA_LEN - 16> opaqueTail = {};
};

struct MapTileLayerData
{
    uint8_t frame = 0;
    uint8_t mpc = 0;
};

struct MapTileData
{
    MapTileLayerData layer[MAP_EDITOR_TILE_LAYER];
    uint8_t obstacle = 0;
    uint8_t trap = 0;
    uint8_t end[2] = {0x00, 0x1F};
};

struct MapDataFull
{
    MapEditorHead head;
    std::string mpcPath;
    MpcInfoData mpc[MAP_EDITOR_MPC_COUNT];
    std::vector<std::vector<MapTileData>> tile;
};

class MapFileEditor
{
public:
    MapFileEditor();
    ~MapFileEditor();

    bool loadFromFile(const std::string& fileName);
    bool loadFromBuffer(const uint8_t* data, size_t length, bool forceLegacyGbkStrings = false);

    bool saveToFile(const std::string& fileName) const;
    std::vector<uint8_t> saveToBuffer() const;

    void clear();

    bool isLoaded() const;

    int32_t getWidth() const;
    int32_t getHeight() const;
    std::string getMpcPath() const;
    void setMpcPath(const std::string& path);

    /// 调整地图尺寸。新区域用 emptyFill 填充；超出新边界的瓦片被裁剪。
    /// 返回 true 表示尺寸已改变（或未变但无错误），false 表示参数无效。
    bool resizeMap(int32_t newWidth, int32_t newHeight, const MapTileData& emptyFill);

    const MpcInfoData& getMpcInfo(int index) const;
    void setMpcInfo(int index, const MpcInfoData& info);
    int findMpcIndexByName(const std::string& name) const;
    int getUsedMpcCount() const;

    const MapTileData& getTile(int x, int y) const;
    MapTileData& getTileRef(int x, int y);
    void setTile(int x, int y, const MapTileData& tile);

    /// Bulk access: returns a const reference to the internal 2D tile array ([y][x]).
    const std::vector<std::vector<MapTileData>>& getTileData() const;
    /// Replaces the entire tile grid and updates width/height accordingly.
    void setTileDataAndSize(const std::vector<std::vector<MapTileData>>& tileData,
                            int32_t width, int32_t height);

    uint8_t getTileObstacle(int x, int y) const;
    void setTileObstacle(int x, int y, uint8_t obstacle);

    uint8_t getTileTrap(int x, int y) const;
    void setTileTrap(int x, int y, uint8_t trapIndex);

    MapTileLayerData getTileLayer(int x, int y, int layer) const;
    void setTileLayer(int x, int y, int layer, const MapTileLayerData& layerData);

    static bool isMapFile(const std::string& fileName);
    static bool isMapData(const uint8_t* data, size_t length);

    std::string getLastError() const;

    void setAssetsBasePath(const std::string& path);
    const std::string& getAssetsBasePath() const;

    void setMapFileName(const std::string& fileName);
    const std::string& getMapFileName() const;

    std::string getMpcFilePath(int mpcIndex) const;

private:
    bool parseMapData(const uint8_t* data, size_t length, bool forceLegacyGbkStrings);

    MapDataFull mapData;
    bool loaded = false;
    std::string lastError;
    std::string assetsBasePath;
    std::string mapFileName;
};
