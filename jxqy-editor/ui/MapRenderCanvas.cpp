#include "MapRenderCanvas.h"
#include "MapCoordinateTransform.h"
#include "../core/ImageResourceCandidates.h"
#include "../core/Util.h"

#include <QPainter>
#include <QApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QKeyEvent>
#include <QPainterPath>
#ifdef JXQY_EDITOR_BUILD_BENCHMARK
#include <QVariantMap>
#endif
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <climits>
#include <cstring>
#include <cmath>
#include <limits>
#include <set>
#include <map>

// 瓦片尺寸统一来自 MapCoordinateTransform，避免与小地图/其他模块各写一套常量。
static const int TILE_WIDTH = MapCoordinateTransform::CANVAS_TILE_WIDTH;
static const int TILE_HEIGHT = MapCoordinateTransform::CANVAS_TILE_HEIGHT;
static const MapCoordinateTransform canvasTransform(TILE_WIDTH, TILE_HEIGHT);

namespace
{
constexpr int MAX_EDITOR_ENTITY_COUNT = 100000;

bool readEntityListCount(const INIFileEditor& ini, int& count)
{
    if (!ini.hasSection("Head") || !ini.hasKey("Head", "Count"))
        return false;

    const std::string text = ini.get("Head", "Count", "");
    const char* cursor = text.c_str();
    while (*cursor != '\0' && std::isspace(static_cast<unsigned char>(*cursor)))
        ++cursor;
    if (*cursor == '\0')
        return false;

    errno = 0;
    char* end = nullptr;
    long long parsed = std::strtoll(cursor, &end, 10);
    if (end == cursor || errno == ERANGE)
        return false;
    while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end)))
        ++end;
    if (*end != '\0' || parsed < 0 || parsed > MAX_EDITOR_ENTITY_COUNT)
        return false;

    count = static_cast<int>(parsed);
    return true;
}

bool isIndexedEntitySection(const std::string& section, const std::string& prefix)
{
    if (section.size() <= prefix.size())
    {
        return false;
    }

    for (size_t i = 0; i < prefix.size(); i++)
    {
        unsigned char left = static_cast<unsigned char>(section[i]);
        unsigned char right = static_cast<unsigned char>(prefix[i]);
        if (std::tolower(left) != std::tolower(right))
            return false;
    }

    return std::all_of(
        section.begin() + static_cast<std::ptrdiff_t>(prefix.size()),
        section.end(),
        [](unsigned char character) {
            return std::isdigit(character) != 0;
        });
}

std::string mapBaseFileName(const MapFileEditor* mapEditor)
{
    if (!mapEditor)
        return std::string();
    std::string fileName = mapEditor->getMapFileName();
    size_t separator = fileName.find_last_of("\\/");
    if (separator != std::string::npos)
        fileName = fileName.substr(separator + 1);
    return fileName;
}

void captureOriginalProperties(
    const INIFileEditor& ini,
    const std::string& section,
    MapEntityData& entity)
{
    for (const std::string& key : ini.getKeyNames(section))
        entity.originalProperties[key] = ini.get(section, key);
}

struct AreaDiagonalRange
{
    int minDiagonalX = 0;
    int minDiagonalY = 0;
    int maxDiagonalX = -1;
    int maxDiagonalY = -1;
};

struct AreaTileRange
{
    AreaSelectionShape shape = AreaSelectionShape::Diamond;
    int minCoordinateX = 0;
    int minCoordinateY = 0;
    int maxCoordinateX = -1;
    int maxCoordinateY = -1;
};

QPoint tileToAreaDiagonal(int tileX, int tileY)
{
    return QPoint(tileX + (tileY + 1) / 2, tileY / 2 - tileX);
}

AreaDiagonalRange makeAreaDiagonalRange(
    int startX,
    int startY,
    int endX,
    int endY,
    int mapWidth,
    int mapHeight)
{
    AreaDiagonalRange range;
    if (mapWidth <= 0 || mapHeight <= 0)
        return range;

    startX = std::clamp(startX, 0, mapWidth - 1);
    endX = std::clamp(endX, 0, mapWidth - 1);
    startY = std::clamp(startY, 0, mapHeight - 1);
    endY = std::clamp(endY, 0, mapHeight - 1);

    QPoint startDiagonal = tileToAreaDiagonal(startX, startY);
    QPoint endDiagonal = tileToAreaDiagonal(endX, endY);

    range.minDiagonalX = std::min(startDiagonal.x(), endDiagonal.x());
    range.maxDiagonalX = std::max(startDiagonal.x(), endDiagonal.x());
    range.minDiagonalY = std::min(startDiagonal.y(), endDiagonal.y());
    range.maxDiagonalY = std::max(startDiagonal.y(), endDiagonal.y());
    return range;
}

template <typename Callback>
void forEachTileInAreaDiagonalRange(
    const AreaDiagonalRange& range,
    int mapWidth,
    int mapHeight,
    Callback callback)
{
    if (mapWidth <= 0 || mapHeight <= 0 ||
        range.minDiagonalX > range.maxDiagonalX ||
        range.minDiagonalY > range.maxDiagonalY)
    {
        return;
    }

    int startY = std::max(0, range.minDiagonalX + range.minDiagonalY);
    int endY = std::min(mapHeight - 1, range.maxDiagonalX + range.maxDiagonalY);

    for (int y = startY; y <= endY; y++)
    {
        int rowFloorHalf = y / 2;
        int rowCeilHalf = (y + 1) / 2;

        int startX = std::max(
            0,
            std::max(range.minDiagonalX - rowCeilHalf,
                     rowFloorHalf - range.maxDiagonalY));
        int endX = std::min(
            mapWidth - 1,
            std::min(range.maxDiagonalX - rowCeilHalf,
                     rowFloorHalf - range.minDiagonalY));

        for (int x = startX; x <= endX; x++)
            callback(x, y);
    }
}

int tileToStaggeredX(int tileX, int tileY)
{
    return 2 * tileX + (tileY & 1);
}

AreaTileRange makeAreaTileRange(
    int startX,
    int startY,
    int endX,
    int endY,
    AreaSelectionShape shape,
    int mapWidth,
    int mapHeight)
{
    AreaTileRange range;
    range.shape = shape;
    if (mapWidth <= 0 || mapHeight <= 0)
        return range;

    startX = std::clamp(startX, 0, mapWidth - 1);
    endX = std::clamp(endX, 0, mapWidth - 1);
    startY = std::clamp(startY, 0, mapHeight - 1);
    endY = std::clamp(endY, 0, mapHeight - 1);

    if (shape == AreaSelectionShape::Diamond)
    {
        AreaDiagonalRange diagonalRange = makeAreaDiagonalRange(
            startX, startY, endX, endY, mapWidth, mapHeight);
        range.minCoordinateX = diagonalRange.minDiagonalX;
        range.minCoordinateY = diagonalRange.minDiagonalY;
        range.maxCoordinateX = diagonalRange.maxDiagonalX;
        range.maxCoordinateY = diagonalRange.maxDiagonalY;
    }
    else
    {
        int startStaggeredX = tileToStaggeredX(startX, startY);
        int endStaggeredX = tileToStaggeredX(endX, endY);
        range.minCoordinateX = std::min(startStaggeredX, endStaggeredX);
        range.maxCoordinateX = std::max(startStaggeredX, endStaggeredX);
        range.minCoordinateY = std::min(startY, endY);
        range.maxCoordinateY = std::max(startY, endY);
    }
    return range;
}

std::vector<QPoint> enumerateAreaTileRange(
    const AreaTileRange& range,
    int mapWidth,
    int mapHeight)
{
    std::vector<QPoint> tiles;
    if (mapWidth <= 0 || mapHeight <= 0 ||
        range.minCoordinateX > range.maxCoordinateX ||
        range.minCoordinateY > range.maxCoordinateY)
    {
        return tiles;
    }

    if (range.shape == AreaSelectionShape::Diamond)
    {
        AreaDiagonalRange diagonalRange;
        diagonalRange.minDiagonalX = range.minCoordinateX;
        diagonalRange.minDiagonalY = range.minCoordinateY;
        diagonalRange.maxDiagonalX = range.maxCoordinateX;
        diagonalRange.maxDiagonalY = range.maxCoordinateY;
        forEachTileInAreaDiagonalRange(
            diagonalRange,
            mapWidth,
            mapHeight,
            [&](int x, int y)
            {
                tiles.emplace_back(x, y);
            });
        return tiles;
    }

    int startY = std::max(0, range.minCoordinateY);
    int endY = std::min(mapHeight - 1, range.maxCoordinateY);
    for (int y = startY; y <= endY; y++)
    {
        int rowParity = y & 1;
        for (int x = 0; x < mapWidth; x++)
        {
            int staggeredX = 2 * x + rowParity;
            if (staggeredX >= range.minCoordinateX &&
                staggeredX <= range.maxCoordinateX)
            {
                tiles.emplace_back(x, y);
            }
        }
    }
    return tiles;
}

}

MapRenderCanvas::MapRenderCanvas(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(400, 300);

    connect(&animationTimer, &QTimer::timeout, this, &MapRenderCanvas::updateAnimation);
    animationTimer.setInterval(50);
    // QCache cost is measured in KiB. Keep scaled interaction frames bounded
    // independently from the source MPC cache.
    scaledMpcFrameCache.setMaxCost(64 * 1024);
}

MapRenderCanvas::~MapRenderCanvas()
{
}

void MapRenderCanvas::setMapFileEditor(MapFileEditor* editor)
{
    mapEditor = editor;
    invalidateRenderRangeCache();
    if (mapEditor && mapEditor->isLoaded())
    {
        int centerX = mapEditor->getWidth() / 2;
        int centerY = mapEditor->getHeight() / 2;
        centerOnTile(centerX, centerY);
    }
    update();
}

void MapRenderCanvas::setMpcImageCache(MpcImageCache* cache)
{
    mpcCache = cache;
    invalidateRenderRangeCache();
    update();
}

bool MapRenderCanvas::loadNpcList(const std::string& iniFileName)
{
    if (iniFileName.empty())
        return false;

    INIFileEditor ini;
    if (!ini.loadFromFile(iniFileName))
        return false;

    std::vector<MapEntityData> loadedNpcList;

    int count = 0;
    if (!readEntityListCount(ini, count))
        return false;
    for (int i = 0; i < count; i++)
    {
        char sectionBuffer[16];
        std::snprintf(sectionBuffer, sizeof(sectionBuffer), "NPC%03d", i);
        std::string section = sectionBuffer;
        if (!ini.hasSection(section))
            continue;

        MapEntityData entity;
        entity.isNpc = true;
        entity.name = ini.get(section, "Name", "");
        entity.iniFile = ini.get(section, "NPCIni", "");
        entity.mapX = (int)ini.getInteger(section, "MapX", 0);
        entity.mapY = (int)ini.getInteger(section, "MapY", 0);
        entity.direction = (int)ini.getInteger(section, "Dir", 0);
        entity.kind = (int)ini.getInteger(section, "Kind", 0);
        entity.scriptFile = ini.get(section, "ScriptFile", "");
        entity.action = (int)ini.getInteger(section, "Action", 0);
        entity.relation = (int)ini.getInteger(section, "Relation", 0);
        entity.lum = (int)ini.getInteger(section, "Lum", 0);
        entity.state = (int)ini.getInteger(section, "State", 0);
        entity.walkSpeed = (int)ini.getInteger(section, "WalkSpeed", 1);
        entity.pathFinder = (int)ini.getInteger(section, "PathFinder", 0);
        entity.dialogRadius = (int)ini.getInteger(section, "DialogRadius", 1);
        entity.life = (int)ini.getInteger(section, "Life", 0);
        entity.lifeMax = (int)ini.getInteger(section, "LifeMax", 0);
        entity.standSpeed = (int)ini.getInteger(section, "StandSpeed", 0);
        entity.thew = (int)ini.getInteger(section, "Thew", 0);
        entity.thewMax = (int)ini.getInteger(section, "ThewMax", 0);
        entity.mana = (int)ini.getInteger(section, "Mana", 0);
        entity.manaMax = (int)ini.getInteger(section, "ManaMax", 0);
        entity.attack = (int)ini.getInteger(section, "Attack", 0);
        entity.defend = (int)ini.getInteger(
            section, "Defend", ini.getInteger(section, "Defence", 0));
        entity.evade = (int)ini.getInteger(section, "Evade", 0);
        entity.duck = (int)ini.getInteger(section, "Duck", 0);
        entity.exp = (int)ini.getInteger(section, "Exp", 0);
        entity.levelUpExp = (int)ini.getInteger(section, "LevelUpExp", 0);
        entity.level = (int)ini.getInteger(section, "Level", 0);
        entity.attackLevel = (int)ini.getInteger(section, "AttackLevel", 0);
        entity.magicLevel = (int)ini.getInteger(section, "MagicLevel", 0);
        entity.visionRadius = (int)ini.getInteger(section, "VisionRadius", 0);
        entity.attackRadius = (int)ini.getInteger(section, "AttackRadius", 0);
        entity.bodyIni = ini.get(section, "BodyIni", "");
        entity.flyIni = ini.get(section, "FlyIni", "");
        entity.flyIni2 = ini.get(section, "FlyIni2", "");
        entity.flyInis = ini.get(section, "FlyInis", "");
        entity.magicIni = ini.get(section, "MagicIni", "");
        entity.deathScript = ini.get(section, "DeathScript", "");
        entity.idle = (int)ini.getInteger(section, "Idle", 0);
        entity.fixedPosition = ini.get(section, "FixedPos", "");
        entity.aiType = (int)ini.getInteger(
            section, "AI_TYPE", ini.getInteger(section, "AIType", 0));
        entity.scriptFileRight = ini.get(section, "ScriptFileRight", "");
        entity.timerScriptFile = ini.get(section, "TimerScriptFile", "");
        entity.timerScriptInterval = (int)ini.getInteger(section, "TimerScriptInterval", 1000);
        entity.canInteractDirectly = (int)ini.getInteger(section, "CanInteractDirectly", 0);
        entity.attack2 = (int)ini.getInteger(section, "Attack2", 0);
        entity.attack3 = (int)ini.getInteger(section, "Attack3", 0);
        entity.defend2 = (int)ini.getInteger(section, "Defend2", 0);
        entity.defend3 = (int)ini.getInteger(section, "Defend3", 0);
        entity.expBonus = (int)ini.getInteger(section, "ExpBonus", 0);
        entity.invincible = (int)ini.getInteger(section, "Invincible", 0);
        entity.group = (int)ini.getInteger(section, "Group", 0);
        entity.dropIni = ini.get(section, "DropIni", "");
        entity.noDropWhenDie = (int)ini.getInteger(section, "NoDropWhenDie", 0);
        entity.reviveMilliseconds = (int)ini.getInteger(section, "ReviveMilliseconds", 0);
        entity.visibleVariableName = ini.get(section, "VisibleVariableName", "");
        entity.visibleVariableValue = (int)ini.getInteger(section, "VisibleVariableValue", 0);
        captureOriginalProperties(ini, section, entity);
        loadedNpcList.push_back(entity);
    }
    npcList.swap(loadedNpcList);
    originalNpcIni = ini;
    if (selectedEntityIndex >= 0 && selectedEntityIsNpc)
        selectedEntityIndex = -1;
    entityResImageCache.clear();
    update();
    return true;
}

bool MapRenderCanvas::loadObjectList(const std::string& iniFileName)
{
    if (iniFileName.empty())
        return false;

    INIFileEditor ini;
    if (!ini.loadFromFile(iniFileName))
        return false;

    std::vector<MapEntityData> loadedObjectList;

    int count = 0;
    if (!readEntityListCount(ini, count))
        return false;
    for (int i = 0; i < count; i++)
    {
        char sectionBuffer[16];
        std::snprintf(sectionBuffer, sizeof(sectionBuffer), "OBJ%03d", i);
        std::string section = sectionBuffer;
        if (!ini.hasSection(section))
            continue;

        MapEntityData entity;
        entity.isNpc = false;
        entity.name = ini.get(section, "ObjName", "");
        entity.iniFile = ini.get(section, "ObjFile", "");
        entity.mapX = (int)ini.getInteger(section, "MapX", 0);
        entity.mapY = (int)ini.getInteger(section, "MapY", 0);
        entity.direction = (int)ini.getInteger(section, "Dir", 0);
        entity.kind = (int)ini.getInteger(section, "Kind", 0);
        entity.scriptFile = ini.get(section, "ScriptFile", "");
        entity.offsetX = (int)ini.getInteger(
            section, "OffsetX", ini.getInteger(section, "OffX", 0));
        entity.offsetY = (int)ini.getInteger(
            section, "OffsetY", ini.getInteger(section, "OffY", 0));
        entity.lum = (int)ini.getInteger(section, "Lum", 0);
        entity.frame = (int)ini.getInteger(section, "Frame", 0);
        entity.state = (int)ini.getInteger(section, "State", 0);
        entity.wavFile = ini.get(section, "WavFile", "");
        entity.damage = (int)ini.getInteger(section, "Damage", 0);
        if (ini.hasKey(section, "ActionTime") &&
            (!ini.tryGetInt64(section, "ActionTime", entity.actionTime) ||
             entity.actionTime < 0))
        {
            return false;
        }
        entity.height = (int)ini.getInteger(section, "Height", 0);
        entity.scriptFileRight = ini.get(section, "ScriptFileRight", "");
        entity.scriptFileJustTouch = (int)ini.getInteger(section, "ScriptFileJustTouch", 0);
        entity.canInteractDirectly = (int)ini.getInteger(section, "CanInteractDirectly", 0);
        entity.timerScriptFile = ini.get(section, "TimerScriptFile", "");
        entity.timerScriptInterval = (int)ini.getInteger(section, "TimerScriptInterval", 1000);
        entity.reviveNpcIni = ini.get(section, "ReviveNpcIni", "");
        entity.millisecondsToRemove = (int)ini.getInteger(section, "MillisecondsToRemove", 0);
        captureOriginalProperties(ini, section, entity);
        loadedObjectList.push_back(entity);
    }
    objectList.swap(loadedObjectList);
    originalObjectIni = ini;
    if (selectedEntityIndex >= 0 && !selectedEntityIsNpc)
        selectedEntityIndex = -1;
    entityResImageCache.clear();
    update();
    return true;
}

void MapRenderCanvas::clearEntities()
{
    npcList.clear();
    objectList.clear();
    originalNpcIni = INIFileEditor();
    originalObjectIni = INIFileEditor();
    selectedEntityIndex = -1;
    entityResImageCache.clear();
    update();
}

std::string MapRenderCanvas::serializeNpcList() const
{
    INIFileEditor ini = originalNpcIni;
    for (const std::string& section : ini.getSectionNames())
    {
        if (isIndexedEntitySection(section, "npc"))
            ini.removeSection(section);
    }
    ini.set("Head", "Map", mapBaseFileName(mapEditor));
    ini.setInteger("Head", "Count", (long)npcList.size());

    for (size_t i = 0; i < npcList.size(); i++)
    {
        char sectionBuffer[16];
        std::snprintf(sectionBuffer, sizeof(sectionBuffer), "NPC%03d", (int)i);
        std::string section = sectionBuffer;

        const MapEntityData& entity = npcList[i];
        for (const auto& property : entity.originalProperties)
            ini.set(section, property.first, property.second);
        ini.set(section, "Name", entity.name);
        ini.set(section, "NPCIni", entity.iniFile);
        ini.setInteger(section, "MapX", entity.mapX);
        ini.setInteger(section, "MapY", entity.mapY);
        ini.setInteger(section, "Dir", entity.direction);
        ini.setInteger(section, "Kind", entity.kind);
        ini.set(section, "ScriptFile", entity.scriptFile);
        ini.setInteger(section, "Action", entity.action);
        ini.setInteger(section, "Relation", entity.relation);
        ini.setInteger(section, "Lum", entity.lum);
        ini.setInteger(section, "State", entity.state);
        ini.setInteger(section, "WalkSpeed", entity.walkSpeed);
        ini.setInteger(section, "PathFinder", entity.pathFinder);
        ini.setInteger(section, "DialogRadius", entity.dialogRadius);
        ini.setInteger(section, "Life", entity.life);
        ini.setInteger(section, "LifeMax", entity.lifeMax);
        ini.setInteger(section, "StandSpeed", entity.standSpeed);
        ini.setInteger(section, "Thew", entity.thew);
        ini.setInteger(section, "ThewMax", entity.thewMax);
        ini.setInteger(section, "Mana", entity.mana);
        ini.setInteger(section, "ManaMax", entity.manaMax);
        ini.setInteger(section, "Attack", entity.attack);
        ini.setInteger(section, "Defend", entity.defend);
        if (ini.hasKey(section, "Defence"))
            ini.setInteger(section, "Defence", entity.defend);
        ini.setInteger(section, "Evade", entity.evade);
        ini.setInteger(section, "Duck", entity.duck);
        ini.setInteger(section, "Exp", entity.exp);
        ini.setInteger(section, "LevelUpExp", entity.levelUpExp);
        ini.setInteger(section, "Level", entity.level);
        ini.setInteger(section, "AttackLevel", entity.attackLevel);
        ini.setInteger(section, "MagicLevel", entity.magicLevel);
        ini.setInteger(section, "VisionRadius", entity.visionRadius);
        ini.setInteger(section, "AttackRadius", entity.attackRadius);
        ini.set(section, "BodyIni", entity.bodyIni);
        ini.set(section, "FlyIni", entity.flyIni);
        ini.set(section, "FlyIni2", entity.flyIni2);
        ini.set(section, "FlyInis", entity.flyInis);
        ini.set(section, "MagicIni", entity.magicIni);
        ini.set(section, "DeathScript", entity.deathScript);
        ini.setInteger(section, "Idle", entity.idle);
        ini.set(section, "FixedPos", entity.fixedPosition);
        // Runtime serialization and loading use AI_TYPE as the canonical key.
        // Keep a pre-existing AIType alias synchronized so no consumer sees a
        // stale higher/lower-priority value.
        ini.setInteger(section, "AI_TYPE", entity.aiType);
        if (ini.hasKey(section, "AIType"))
            ini.setInteger(section, "AIType", entity.aiType);
        ini.set(section, "ScriptFileRight", entity.scriptFileRight);
        ini.set(section, "TimerScriptFile", entity.timerScriptFile);
        ini.setInteger(section, "TimerScriptInterval", entity.timerScriptInterval);
        ini.setInteger(section, "CanInteractDirectly", entity.canInteractDirectly);
        ini.setInteger(section, "Attack2", entity.attack2);
        ini.setInteger(section, "Attack3", entity.attack3);
        ini.setInteger(section, "Defend2", entity.defend2);
        ini.setInteger(section, "Defend3", entity.defend3);
        ini.setInteger(section, "ExpBonus", entity.expBonus);
        ini.setInteger(section, "Invincible", entity.invincible);
        ini.setInteger(section, "Group", entity.group);
        ini.set(section, "DropIni", entity.dropIni);
        ini.setInteger(section, "NoDropWhenDie", entity.noDropWhenDie);
        ini.setInteger(section, "ReviveMilliseconds", entity.reviveMilliseconds);
        ini.set(section, "VisibleVariableName", entity.visibleVariableName);
        ini.setInteger(section, "VisibleVariableValue", entity.visibleVariableValue);
    }

    return ini.saveToString();
}

bool MapRenderCanvas::saveNpcList(const std::string& iniFileName) const
{
    if (iniFileName.empty())
        return false;

    const std::string content = serializeNpcList();
    return Util::writeFileFromBuffer(
        iniFileName, content.data(), content.size());
}

std::string MapRenderCanvas::serializeObjectList() const
{
    INIFileEditor ini = originalObjectIni;
    for (const std::string& section : ini.getSectionNames())
    {
        if (isIndexedEntitySection(section, "obj"))
            ini.removeSection(section);
    }
    ini.set("Head", "Map", mapBaseFileName(mapEditor));
    ini.setInteger("Head", "Count", (long)objectList.size());

    for (size_t i = 0; i < objectList.size(); i++)
    {
        char sectionBuffer[16];
        std::snprintf(sectionBuffer, sizeof(sectionBuffer), "OBJ%03d", (int)i);
        std::string section = sectionBuffer;

        const MapEntityData& entity = objectList[i];
        for (const auto& property : entity.originalProperties)
            ini.set(section, property.first, property.second);
        ini.set(section, "ObjName", entity.name);
        ini.set(section, "ObjFile", entity.iniFile);
        ini.setInteger(section, "MapX", entity.mapX);
        ini.setInteger(section, "MapY", entity.mapY);
        ini.setInteger(section, "Dir", entity.direction);
        ini.setInteger(section, "Kind", entity.kind);
        ini.set(section, "ScriptFile", entity.scriptFile);
        ini.setInteger(section, "OffsetX", entity.offsetX);
        ini.setInteger(section, "OffsetY", entity.offsetY);
        if (ini.hasKey(section, "OffX"))
            ini.setInteger(section, "OffX", entity.offsetX);
        if (ini.hasKey(section, "OffY"))
            ini.setInteger(section, "OffY", entity.offsetY);
        ini.setInteger(section, "Lum", entity.lum);
        ini.setInteger(section, "Frame", entity.frame);
        ini.setInteger(section, "State", entity.state);
        ini.set(section, "WavFile", entity.wavFile);
        ini.setInteger(section, "Damage", entity.damage);
        ini.setInt64(section, "ActionTime", entity.actionTime);
        ini.setInteger(section, "Height", entity.height);
        ini.set(section, "ScriptFileRight", entity.scriptFileRight);
        ini.setInteger(section, "ScriptFileJustTouch", entity.scriptFileJustTouch);
        ini.setInteger(section, "CanInteractDirectly", entity.canInteractDirectly);
        ini.set(section, "TimerScriptFile", entity.timerScriptFile);
        ini.setInteger(section, "TimerScriptInterval", entity.timerScriptInterval);
        ini.set(section, "ReviveNpcIni", entity.reviveNpcIni);
        ini.setInteger(section, "MillisecondsToRemove", entity.millisecondsToRemove);
    }

    return ini.saveToString();
}

bool MapRenderCanvas::saveObjectList(const std::string& iniFileName) const
{
    if (iniFileName.empty())
        return false;

    const std::string content = serializeObjectList();
    return Util::writeFileFromBuffer(
        iniFileName, content.data(), content.size());
}

void MapRenderCanvas::setEditTool(MapEditTool tool)
{
    editTool = tool;
    if (tool != MapEditTool::Select)
    {
        selectedEntityIndex = -1;
    }

    if (tool != MapEditTool::AreaSelect)
    {
        clearAreaSelectionState();
    }
    else
    {
        clearTileSelectionState();
        clearPickSelectionState();
        isTileSelectionToggleCandidate = false;
    }

    // 切换到障碍/陷阱工具时清除区域粘贴预览，避免右键误触发 Tile 粘贴。
    // 切换任意工具时都停止右键拖动，避免残留绘制状态。
    isRightDragging = false;
    if (tool == MapEditTool::ObstaclePaint || tool == MapEditTool::TrapPaint)
    {
        pastePreviewVisible = false;
    }

    switch (tool)
    {
    case MapEditTool::Select:
        setCursor(Qt::ArrowCursor);
        break;
    case MapEditTool::TilePaint:
        setCursor(Qt::CrossCursor);
        break;
    case MapEditTool::ObstaclePaint:
        setCursor(Qt::CrossCursor);
        break;
    case MapEditTool::TrapPaint:
        setCursor(Qt::CrossCursor);
        break;
    case MapEditTool::NpcPlace:
    case MapEditTool::ObjectPlace:
        setCursor(Qt::PointingHandCursor);
        break;
    case MapEditTool::TilePicker:
        setCursor(Qt::CrossCursor);
        break;
    case MapEditTool::AreaSelect:
        setCursor(Qt::CrossCursor);
        break;
    case MapEditTool::Pan:
        setCursor(Qt::OpenHandCursor);
        break;
    default:
        setCursor(Qt::ArrowCursor);
        break;
    }

    emit editToolChanged(tool);
    update();
}

MapEditTool MapRenderCanvas::getEditTool() const
{
    return editTool;
}

void MapRenderCanvas::setPaintLayer(int layer)
{
    paintLayer = layer;
    paintAllLayers = false;
    // 离开全部图层模式：多层画笔不再适用，清除以免后续误用。
    hasMultiLayerPaintBrushState = false;
}

int MapRenderCanvas::getPaintLayer() const
{
    return paintLayer;
}

void MapRenderCanvas::setPaintAllLayers(bool all)
{
    paintAllLayers = all;
    if (!all)
        hasMultiLayerPaintBrushState = false;
}

bool MapRenderCanvas::isPaintAllLayers() const
{
    return paintAllLayers;
}

void MapRenderCanvas::setMultiLayerPaintBrush(const MapTileData& tileData, bool hasMultiLayer)
{
    hasMultiLayerPaintBrushState = hasMultiLayer;
    if (hasMultiLayer)
        multiLayerPaintBrush = tileData;
}

bool MapRenderCanvas::hasMultiLayerPaintBrush() const
{
    return hasMultiLayerPaintBrushState;
}

const MapTileData& MapRenderCanvas::getMultiLayerPaintBrush() const
{
    return multiLayerPaintBrush;
}

void MapRenderCanvas::setPaintMpcIndex(int mpcIndex)
{
    paintMpcIndex = mpcIndex;
    // 手动修改 MPC：画笔不再是多层拾取结果，清除多层画笔状态，
    // 后续全部图层绘制回到“同一 mpc/frame 写入三层”的旧行为。
    hasMultiLayerPaintBrushState = false;
}

int MapRenderCanvas::getPaintMpcIndex() const
{
    return paintMpcIndex;
}

void MapRenderCanvas::setPaintFrameIndex(int frameIndex)
{
    paintFrameIndex = frameIndex;
    // 手动修改 frame 同样清除多层画笔状态（见 setPaintMpcIndex）。
    hasMultiLayerPaintBrushState = false;
}

int MapRenderCanvas::getPaintFrameIndex() const
{
    return paintFrameIndex;
}

void MapRenderCanvas::setPaintObstacle(uint8_t obstacle)
{
    paintObstacle = obstacle;
}

uint8_t MapRenderCanvas::getPaintObstacle() const
{
    return paintObstacle;
}

void MapRenderCanvas::setPaintTrapIndex(uint8_t trapIndex)
{
    paintTrapIndex = trapIndex;
}

uint8_t MapRenderCanvas::getPaintTrapIndex() const
{
    return paintTrapIndex;
}

void MapRenderCanvas::setLayerVisible(int layer, bool visible)
{
    if (layer >= 0 && layer < 3)
        layerVisible[layer] = visible;
    update();
}

bool MapRenderCanvas::isLayerVisible(int layer) const
{
    if (layer >= 0 && layer < 3)
        return layerVisible[layer];
    return false;
}

void MapRenderCanvas::setObstacleVisible(bool visible)
{
    obstacleVisible = visible;
    update();
}

bool MapRenderCanvas::isObstacleVisible() const
{
    return obstacleVisible;
}

void MapRenderCanvas::setTrapVisible(bool visible)
{
    trapVisible = visible;
    update();
}

bool MapRenderCanvas::isTrapVisible() const
{
    return trapVisible;
}

void MapRenderCanvas::setNpcVisible(bool visible)
{
    npcVisible = visible;
    update();
}

bool MapRenderCanvas::isNpcVisible() const
{
    return npcVisible;
}

void MapRenderCanvas::setObjectVisible(bool visible)
{
    objectVisible = visible;
    update();
}

bool MapRenderCanvas::isObjectVisible() const
{
    return objectVisible;
}

void MapRenderCanvas::setGridVisible(bool visible)
{
    gridVisible = visible;
    update();
}

bool MapRenderCanvas::isGridVisible() const
{
    return gridVisible;
}

void MapRenderCanvas::setCoordinateVisible(bool visible)
{
    coordinateVisible = visible;
    update();
}

bool MapRenderCanvas::isCoordinateVisible() const
{
    return coordinateVisible;
}

void MapRenderCanvas::zoomIn()
{
    zoomAtPoint(mapFromGlobal(QCursor::pos()), zoomLevel * 1.25f);
}

void MapRenderCanvas::zoomOut()
{
    zoomAtPoint(mapFromGlobal(QCursor::pos()), zoomLevel / 1.25f);
}

void MapRenderCanvas::resetZoom()
{
    zoomAtPoint(QPoint(width() / 2, height() / 2), 1.0f);
}

void MapRenderCanvas::zoomToFit()
{
    if (!mapEditor || !mapEditor->isLoaded())
        return;

    int mapWidth = mapEditor->getWidth();
    int mapHeight = mapEditor->getHeight();

    QRectF contentBounds = canvasTransform.mapWorldBounds(mapWidth, mapHeight);
    if (contentBounds.isEmpty())
        return;

    // Include real frame rectangles so tall/wide map art is not clipped even
    // when its anchor extends outside the editable tile footprint.
    for (int y = 0; y < mapHeight; y++)
    {
        for (int x = 0; x < mapWidth; x++)
        {
            QPoint tileTopLeft = canvasTransform.tileToWorldTopLeft(mapWidth, x, y);
            for (int layer = 0; layer < MAP_EDITOR_TILE_LAYER; layer++)
            {
                MapTileLayerData layerData = mapEditor->getTileLayer(x, y, layer);
                if (layerData.mpc == 0)
                    continue;
                QImage frameImage = getMpcFrameImage(layerData.mpc, layerData.frame);
                if (frameImage.isNull())
                    continue;
                int frameOffsetX = frameImage.width() / 2;
                int frameOffsetY = frameImage.height();
                getMpcFrameOffset(
                    layerData.mpc, layerData.frame, frameOffsetX, frameOffsetY);
                QRectF frameBounds(
                    tileTopLeft.x() + TILE_WIDTH / 2.0 - frameOffsetX,
                    tileTopLeft.y() + TILE_HEIGHT - frameOffsetY,
                    frameImage.width(), frameImage.height());
                contentBounds = contentBounds.united(frameBounds);
            }
        }
    }

    float zoomX = (float)width() / (float)contentBounds.width();
    float zoomY = (float)height() / (float)contentBounds.height();
    float newZoom = std::min(zoomX, zoomY) * 0.9f;
    // Fit must be able to show real 200x400 and very wide legacy maps in one
    // viewport.
    newZoom = std::clamp(newZoom, 0.01f, 4.0f);

    zoomLevel = newZoom;

    QPointF center = contentBounds.center();

    scrollX = (int)std::lround(center.x() * zoomLevel) - width() / 2;
    scrollY = (int)std::lround(center.y() * zoomLevel) - height() / 2;

    emit zoomChanged(zoomLevel);
    update();
    emit viewportChanged();
}

void MapRenderCanvas::zoomAtPoint(const QPoint& pos, float newZoom)
{
    // Keep interactive zoom on the same lower bound as zoom-to-fit.  Otherwise
    // the first wheel event after fitting a large legacy map jumps from e.g.
    // 0.03 straight to 0.125 instead of zooming smoothly around the cursor.
    newZoom = std::clamp(newZoom, 0.01f, 4.0f);
    if (qFuzzyCompare(newZoom, zoomLevel))
        return;

    float oldZoom = zoomLevel;
    zoomLevel = newZoom;

    int worldX = (int)((pos.x() + scrollX) / oldZoom);
    int worldY = (int)((pos.y() + scrollY) / oldZoom);

    scrollX = (int)(worldX * zoomLevel) - pos.x();
    scrollY = (int)(worldY * zoomLevel) - pos.y();

    emit zoomChanged(zoomLevel);
    update();
    emit viewportChanged();
}

float MapRenderCanvas::getZoomLevel() const
{
    return zoomLevel;
}

QPoint MapRenderCanvas::viewportScrollOffset() const
{
    return QPoint(scrollX, scrollY);
}

bool MapRenderCanvas::restoreViewport(
    float requestedZoomLevel,
    const QPoint& scrollOffset)
{
    if (!mapEditor || !mapEditor->isLoaded() ||
        !std::isfinite(requestedZoomLevel) ||
        requestedZoomLevel < 0.01f ||
        requestedZoomLevel > 4.0f)
    {
        return false;
    }

    zoomLevel = requestedZoomLevel;
    scrollX = scrollOffset.x();
    scrollY = scrollOffset.y();
    emit zoomChanged(zoomLevel);
    update();
    emit viewportChanged();
    return true;
}

void MapRenderCanvas::centerOnTile(int tileX, int tileY)
{
    // tileToScreenCenter 返回的是已扣除当前 scroll 的视口坐标，因此要居中到该
    // 瓦片必须累加偏移量：scrollX += screen.x() - width()/2。
    // 直接用赋值 scrollX = screen.x() - width()/2 会丢掉既有 scroll，导致重复
    // 点击在两个错误位置之间来回跳动。
    QPoint screen = tileToScreenCenter(tileX, tileY);
    scrollX += screen.x() - width() / 2;
    scrollY += screen.y() - height() / 2;
    update();
    emit viewportChanged();
}

void MapRenderCanvas::centerOnPixel(int pixelX, int pixelY)
{
    scrollX = pixelX - width() / 2;
    scrollY = pixelY - height() / 2;
    update();
    emit viewportChanged();
}

QPoint MapRenderCanvas::screenToTile(const QPoint& screenPos) const
{
    if (!mapEditor || !mapEditor->isLoaded())
        return QPoint(-1, -1);

    // 使用统一变换的菱形命中测试反查，避免旧闭式公式在奇偶行边界偏移到相邻瓦片。
    return canvasTransform.screenToTilePrecise(screenPos,
        mapEditor->getWidth(), mapEditor->getHeight(),
        zoomLevel, scrollX, scrollY);
}

QPointF MapRenderCanvas::screenToWorld(const QPoint& screenPos) const
{
    return MapCoordinateTransform::screenToWorld(screenPos, zoomLevel, scrollX, scrollY);
}

bool MapRenderCanvas::isPlacingNpc() const
{
    return hasPlacingEntity && placingEntity.isNpc;
}

bool MapRenderCanvas::isPlacingObject() const
{
    return hasPlacingEntity && !placingEntity.isNpc;
}

QPoint MapRenderCanvas::tileToScreen(int tileX, int tileY) const
{
    if (!mapEditor || !mapEditor->isLoaded())
        return QPoint(0, 0);

    QPointF world = canvasTransform.tileToWorldTopLeft(mapEditor->getWidth(), tileX, tileY);
    return MapCoordinateTransform::worldToScreen(world, zoomLevel, scrollX, scrollY);
}

QPoint MapRenderCanvas::tileToScreenCenter(int tileX, int tileY) const
{
    if (!mapEditor || !mapEditor->isLoaded())
        return QPoint(0, 0);

    QPointF world = canvasTransform.tileToWorldCenter(mapEditor->getWidth(), tileX, tileY);
    return MapCoordinateTransform::worldToScreen(world, zoomLevel, scrollX, scrollY);
}

int MapRenderCanvas::getSelectedTileX() const
{
    return selectedTileX;
}

int MapRenderCanvas::getSelectedTileY() const
{
    return selectedTileY;
}

int MapRenderCanvas::getHoverTileX() const
{
    return hoverTileX;
}

int MapRenderCanvas::getHoverTileY() const
{
    return hoverTileY;
}

const std::vector<MapEntityData>& MapRenderCanvas::getNpcList() const
{
    return npcList;
}

const std::vector<MapEntityData>& MapRenderCanvas::getObjectList() const
{
    return objectList;
}

std::vector<MapEntityData>& MapRenderCanvas::getNpcListRef()
{
    return npcList;
}

std::vector<MapEntityData>& MapRenderCanvas::getObjectListRef()
{
    return objectList;
}

MapEntityData* MapRenderCanvas::getSelectedEntity()
{
    if (selectedEntityIndex < 0)
        return nullptr;
    if (selectedEntityIsNpc && selectedEntityIndex < (int)npcList.size())
        return &npcList[selectedEntityIndex];
    if (!selectedEntityIsNpc && selectedEntityIndex < (int)objectList.size())
        return &objectList[selectedEntityIndex];
    return nullptr;
}

int MapRenderCanvas::getSelectedEntityIndex() const
{
    return selectedEntityIndex;
}

void MapRenderCanvas::setPlacingEntity(const MapEntityData& entity)
{
    placingEntity = entity;
    hasPlacingEntity = true;
}

void MapRenderCanvas::clearPlacingEntity()
{
    hasPlacingEntity = false;
}

void MapRenderCanvas::deleteNpc(int index)
{
    if (index >= 0 && index < (int)npcList.size())
    {
        npcList.erase(npcList.begin() + index);
        bool clearedSelection = false;
        if (selectedEntityIsNpc && selectedEntityIndex == index)
        {
            selectedEntityIndex = -1;
            selectedTileX = -1;
            selectedTileY = -1;
            clearedSelection = true;
        }
        else if (selectedEntityIsNpc && selectedEntityIndex > index)
        {
            selectedEntityIndex--;
        }
        emit entityListChanged();
        if (clearedSelection)
            emit selectionCleared();
        update();
    }
}

void MapRenderCanvas::deleteObject(int index)
{
    if (index >= 0 && index < (int)objectList.size())
    {
        objectList.erase(objectList.begin() + index);
        bool clearedSelection = false;
        if (!selectedEntityIsNpc && selectedEntityIndex == index)
        {
            selectedEntityIndex = -1;
            selectedTileX = -1;
            selectedTileY = -1;
            clearedSelection = true;
        }
        else if (!selectedEntityIsNpc && selectedEntityIndex > index)
        {
            selectedEntityIndex--;
        }
        emit entityListChanged();
        if (clearedSelection)
            emit selectionCleared();
        update();
    }
}

void MapRenderCanvas::clearSelection()
{
    clearTileSelectionState();
    clearAreaSelectionState();
    clearPickSelectionState();
}

bool MapRenderCanvas::hasAreaSelection() const
{
    return !selectedAreaTiles.empty();
}

void MapRenderCanvas::clearAreaSelectionState()
{
    areaStartX = -1;
    areaStartY = -1;
    areaEndX = -1;
    areaEndY = -1;
    isAreaSelecting = false;
    selectedAreaTiles.clear();
}

void MapRenderCanvas::clearPickSelectionState()
{
    isPickDragging = false;
    pickStartX = -1;
    pickStartY = -1;
    pickEndX = -1;
    pickEndY = -1;
}

void MapRenderCanvas::clearTileSelectionState()
{
    selectedEntityIndex = -1;
    selectedTileX = -1;
    selectedTileY = -1;
    isTileSelectionToggleCandidate = false;
}

void MapRenderCanvas::clearEntityResImageCache()
{
    entityResImageCache.clear();
    update();
}

void MapRenderCanvas::setContinuousPlace(bool enabled)
{
    continuousPlace = enabled;
}

bool MapRenderCanvas::isContinuousPlace() const
{
    return continuousPlace;
}

void MapRenderCanvas::selectEntity(int index, bool isNpc)
{
    const auto& list = isNpc ? npcList : objectList;

    // Handle out-of-range: clear selection
    if (index < 0 || index >= (int)list.size())
    {
        selectedEntityIndex = -1;
        selectedTileX = -1;
        selectedTileY = -1;
        update();
        return;
    }

    selectedEntityIndex = index;
    selectedEntityIsNpc = isNpc;
    // Set stable selection tile so the white selection diamond shows on the map
    selectedTileX = list[index].mapX;
    selectedTileY = list[index].mapY;
    emit entitySelected(index, isNpc);
    update();
}

bool MapRenderCanvas::isSelectedEntityNpc() const
{
    return selectedEntityIsNpc;
}

QImage MapRenderCanvas::generateThumbnail(int maxWidth, int maxHeight) const
{
    if (!mapEditor || !mapEditor->isLoaded())
        return QImage();

    int mapWidth = mapEditor->getWidth();
    int mapHeight = mapEditor->getHeight();

    // First pass: collect draw entries to compute actual bounds from draw rects.
    struct ThumbEntry
    {
        int x, y, layer;
        QImage image;
        int drawX, drawY;
    };
    std::vector<ThumbEntry> entries;

    int worldMinX = 0, worldMinY = 0, worldMaxX = 0, worldMaxY = 0;
    bool firstEntry = true;

    for (int layer = 0; layer < 3; layer++)
    {
        for (int y = 0; y < mapHeight; y++)
        {
            for (int x = 0; x < mapWidth; x++)
            {
                MapTileLayerData layerData = mapEditor->getTileLayer(x, y, layer);
                if (layerData.mpc == 0)
                    continue;

                QImage frameImage = getMpcFrameImage(layerData.mpc, layerData.frame);
                if (frameImage.isNull())
                    continue;

                int pixelX, pixelY;
                getTileWorldPosition(x, y, pixelX, pixelY);

                int frameOffsetX = frameImage.width() / 2;
                int frameOffsetY = frameImage.height();
                getMpcFrameOffset(
                    layerData.mpc, layerData.frame, frameOffsetX, frameOffsetY);
                int drawX = pixelX + TILE_WIDTH / 2 - frameOffsetX;
                int drawY = pixelY + TILE_HEIGHT - frameOffsetY;

                entries.push_back({x, y, layer, frameImage, drawX, drawY});

                // Expand bounds by actual draw rect
                int right = drawX + frameImage.width();
                int bottom = drawY + frameImage.height();
                if (firstEntry)
                {
                    worldMinX = drawX; worldMinY = drawY;
                    worldMaxX = right; worldMaxY = bottom;
                    firstEntry = false;
                }
                else
                {
                    worldMinX = std::min(worldMinX, drawX);
                    worldMinY = std::min(worldMinY, drawY);
                    worldMaxX = std::max(worldMaxX, right);
                    worldMaxY = std::max(worldMaxY, bottom);
                }
            }
        }
    }

    // Fallback to the complete editable map footprint if no tiles have art.
    if (firstEntry)
    {
        QRectF mapBounds = canvasTransform.mapWorldBounds(mapWidth, mapHeight);
        worldMinX = (int)std::floor(mapBounds.left());
        worldMinY = (int)std::floor(mapBounds.top());
        worldMaxX = (int)std::ceil(mapBounds.right());
        worldMaxY = (int)std::ceil(mapBounds.bottom());
    }

    // Add padding so content isn't flush with image edges
    int padding = TILE_WIDTH;
    worldMinX -= padding;
    worldMinY -= padding;
    worldMaxX += padding;
    worldMaxY += padding;

    int originOffsetX = -worldMinX;
    int originOffsetY = -worldMinY;
    int thumbWidth = worldMaxX - worldMinX;
    int thumbHeight = worldMaxY - worldMinY;

    const int maxThumbDimension = std::max({4096, maxWidth, maxHeight});
    float thumbScale = 1.0f;
    if (thumbWidth > maxThumbDimension || thumbHeight > maxThumbDimension)
    {
        thumbScale = std::min((float)maxThumbDimension / thumbWidth,
                              (float)maxThumbDimension / thumbHeight);
        thumbWidth = (int)(thumbWidth * thumbScale);
        thumbHeight = (int)(thumbHeight * thumbScale);
    }

    QImage thumbnail(thumbWidth, thumbHeight, QImage::Format_ARGB32);
    thumbnail.fill(Qt::transparent);

    QPainter painter(&thumbnail);
    if (thumbScale < 1.0f)
        painter.scale(thumbScale, thumbScale);

    // Draw in runtime order: layer 0, then layer 1, then layer 2 (tile-only)
    for (int drawLayer = 0; drawLayer < 3; drawLayer++)
    {
        for (const auto& entry : entries)
        {
            if (entry.layer != drawLayer)
                continue;
            painter.drawImage(entry.drawX + originOffsetX, entry.drawY + originOffsetY, entry.image);
        }
    }

    return thumbnail.scaled(maxWidth, maxHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void MapRenderCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

#ifdef JXQY_EDITOR_BUILD_BENCHMARK
    const bool measurePerformance = qApp && qApp->property(
        "editorPerformanceBenchmark").toBool();
    if (measurePerformance)
        setProperty("performanceScaledFrameCacheMisses", 0);
    QElapsedTimer performanceClock;
    QVariantMap performanceBreakdown;
    qint64 previousNanoseconds = 0;
    if (measurePerformance)
        performanceClock.start();
    auto recordStage = [&](const QString& stage)
    {
        if (!measurePerformance)
            return;
        const qint64 currentNanoseconds = performanceClock.nsecsElapsed();
        performanceBreakdown.insert(
            stage,
            static_cast<double>(currentNanoseconds - previousNanoseconds) /
                1000000.0);
        previousNanoseconds = currentNanoseconds;
    };
    auto finishMeasurement = [&]()
    {
        if (!measurePerformance)
            return;
        performanceBreakdown.insert(
            "total",
            static_cast<double>(performanceClock.nsecsElapsed()) / 1000000.0);
        setProperty("performancePaintBreakdownMs", performanceBreakdown);
    };
#else
    auto recordStage = [](const char*) {};
    auto finishMeasurement = []() {};
#endif

    collectingAnimationRequirements = true;
    animationRequiredForCurrentPaint = false;

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, zoomLevel < 1.0f);

    painter.fillRect(rect(), QColor(30, 30, 30));
    recordStage("background");

    if (!mapEditor || !mapEditor->isLoaded())
    {
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, "No map loaded");
        collectingAnimationRequirements = false;
        updateAnimationTimer();
        recordStage("empty_state");
        finishMeasurement();
        return;
    }

    // Layer 0 (ground)
    if (layerVisible[0])
        drawMapLayer(painter, 0);
    recordStage("layer_0");

    // Layer 1 + entities interleaved per-tile (matches runtime drawMap order)
    if (layerVisible[1] || (npcVisible && !npcList.empty()) || (objectVisible && !objectList.empty()))
        drawMapLayerAndEntities(painter);
    recordStage("layer_1_entities");

    // Layer 2 (top)
    if (layerVisible[2])
        drawMapLayer(painter, 2);
    recordStage("layer_2");

    if (obstacleVisible)
        drawObstacleOverlay(painter);
    recordStage("obstacles");

    if (trapVisible)
        drawTrapOverlay(painter);
    recordStage("traps");

    if (gridVisible)
        drawGrid(painter);
    drawSelection(painter);
    drawHoverHighlight(painter);

    if (editTool == MapEditTool::AreaSelect)
        drawAreaSelection(painter);

    // 左键拾取拖拽范围预览
    if (isPickDragging && pickStartX >= 0 && pickEndX >= 0 &&
        pickStartY >= 0 && pickEndY >= 0 &&
        (pickStartX != pickEndX || pickStartY != pickEndY))
        drawPickSelection(painter);

    // 粘贴预览：有剪贴板数据、预览可见、且鼠标悬停在有效瓦片上时显示目标区域轮廓。
    // 仅 Select/TilePaint/TilePicker 工具显示粘贴预览；其他工具（AreaSelect/Pan/
    // NpcPlace/ObjectPlace/ObstaclePaint/TrapPaint）不显示，避免误导用户以为会粘贴。
    if ((editTool == MapEditTool::Select ||
         editTool == MapEditTool::TilePaint ||
         editTool == MapEditTool::TilePicker) &&
        !isAreaSelecting && !isPickDragging &&
        pastePreviewVisible && hasClipboardData() && hoverTileX >= 0 && hoverTileY >= 0)
        drawPastePreview(painter);
    else
        drawBrushPreview(painter);

    if (hasPlacingEntity)
        drawPlacingEntity(painter);
    recordStage("interactive_overlays");

    if (coordinateVisible)
        drawCoordinateOverlay(painter);
    recordStage("coordinates");

    collectingAnimationRequirements = false;
    updateAnimationTimer();
    recordStage("animation_gate");
    finishMeasurement();
}

void MapRenderCanvas::drawMapLayer(QPainter& painter, int layer)
{
    int startX, startY, endX, endY;
    getVisibleTileRenderRange(startX, startY, endX, endY);

    QRect viewRect = rect();
    for (int y = startY; y <= endY; y++)
    {
        for (int x = startX; x <= endX; x++)
        {
            MapTileLayerData layerData = mapEditor->getTileLayer(x, y, layer);
            if (layerData.mpc == 0)
                continue;

            const ScaledMpcFrameVisual frame = getScaledMpcFrameVisual(
                layerData.mpc, layerData.frame);
            if (frame.image.isNull())
                continue;

            QPoint screenPos = tileToScreen(x, y);
            const int scaledWidth = frame.image.width();
            const int scaledHeight = frame.image.height();
            int drawX = screenPos.x() + (int)(TILE_WIDTH * zoomLevel / 2) -
                (int)std::lround(frame.frameOffsetX * zoomLevel);
            int drawY = screenPos.y() + (int)(TILE_HEIGHT * zoomLevel) -
                (int)std::lround(frame.frameOffsetY * zoomLevel);

            QRect targetRect(drawX, drawY, scaledWidth, scaledHeight);
            // 渲染范围已放宽，这里再用视口矩形剔除完全在屏幕外的图像，避免过度绘制。
            if (!targetRect.intersects(viewRect))
                continue;
            painter.drawImage(drawX, drawY, frame.image);
        }
    }
}

void MapRenderCanvas::drawMapLayerAndEntities(QPainter& painter)
{
    int startX, startY, endX, endY;
    getVisibleTileRenderRange(startX, startY, endX, endY);
    QRect viewRect = rect();

    // Build entity lookup by tile position
    std::map<std::pair<int,int>, std::vector<std::pair<const MapEntityData*, bool>>> entitiesAtTile;
    if (objectVisible)
    {
        for (size_t i = 0; i < objectList.size(); i++)
            entitiesAtTile[{objectList[i].mapX, objectList[i].mapY}].push_back({&objectList[i], false});
    }
    if (npcVisible)
    {
        for (size_t i = 0; i < npcList.size(); i++)
            entitiesAtTile[{npcList[i].mapX, npcList[i].mapY}].push_back({&npcList[i], true});
    }

    for (int y = startY; y <= endY; y++)
    {
        for (int x = startX; x <= endX; x++)
        {
            // Draw layer 1 tile
            if (layerVisible[1])
            {
                MapTileLayerData layerData = mapEditor->getTileLayer(x, y, 1);
                if (layerData.mpc != 0)
                {
                    const ScaledMpcFrameVisual frame = getScaledMpcFrameVisual(
                        layerData.mpc, layerData.frame);
                    if (!frame.image.isNull())
                    {
                        QPoint screenPos = tileToScreen(x, y);
                        const int scaledWidth = frame.image.width();
                        const int scaledHeight = frame.image.height();
                        int drawX = screenPos.x() + (int)(TILE_WIDTH * zoomLevel / 2) -
                            (int)std::lround(frame.frameOffsetX * zoomLevel);
                        int drawY = screenPos.y() + (int)(TILE_HEIGHT * zoomLevel) -
                            (int)std::lround(frame.frameOffsetY * zoomLevel);
                        QRect targetRect(drawX, drawY, scaledWidth, scaledHeight);
                        if (targetRect.intersects(viewRect))
                            painter.drawImage(drawX, drawY, frame.image);
                    }
                }
            }

            // Draw entities at this tile (objects first, then NPCs, matching runtime order)
            auto it = entitiesAtTile.find({x, y});
            if (it != entitiesAtTile.end())
            {
                for (const auto& entityPair : it->second)
                    drawEntity(painter, *entityPair.first, false);
            }
        }
    }

    // Draw selection highlight on top for the selected entity
    if (selectedEntityIndex >= 0)
    {
        const std::vector<MapEntityData>& selList = selectedEntityIsNpc ? npcList : objectList;
        if (selectedEntityIndex < (int)selList.size())
        {
            const MapEntityData& entity = selList[selectedEntityIndex];
            QPoint screenPos = tileToScreen(entity.mapX, entity.mapY);
            float halfW = TILE_WIDTH * zoomLevel / 2;
            float halfH = TILE_HEIGHT * zoomLevel / 2;

            QPainterPath diamond;
            diamond.moveTo(screenPos.x() + halfW, screenPos.y());
            diamond.lineTo(screenPos.x() + TILE_WIDTH * zoomLevel, screenPos.y() + halfH);
            diamond.lineTo(screenPos.x() + halfW, screenPos.y() + TILE_HEIGHT * zoomLevel);
            diamond.lineTo(screenPos.x(), screenPos.y() + halfH);
            diamond.closeSubpath();

            painter.setPen(QPen(Qt::white, 2));
            painter.drawPath(diamond);
        }
    }
}

void MapRenderCanvas::drawObstacleOverlay(QPainter& painter)
{
    int startX, startY, endX, endY;
    getVisibleTileRange(startX, startY, endX, endY);

    float halfW = TILE_WIDTH * zoomLevel / 2;
    float halfH = TILE_HEIGHT * zoomLevel / 2;

    for (int y = startY; y <= endY; y++)
    {
        for (int x = startX; x <= endX; x++)
        {
            uint8_t obstacle = mapEditor->getTileObstacle(x, y);
            if (obstacle == 0)
                continue;

            QColor color;
            if (obstacle == 0x80)
                color = QColor(255, 0, 0, 80);
            else if (obstacle == 0xA0)
                color = QColor(255, 165, 0, 80);
            else if (obstacle == 0x40)
                color = QColor(0, 0, 255, 80);
            else if (obstacle == 0x60)
                color = QColor(0, 255, 255, 80);
            else
                color = QColor(128, 128, 128, 60);

            QPoint screenPos = tileToScreenCenter(x, y);

            QPainterPath diamond;
            diamond.moveTo(screenPos.x(), screenPos.y() - halfH);
            diamond.lineTo(screenPos.x() + halfW, screenPos.y());
            diamond.lineTo(screenPos.x(), screenPos.y() + halfH);
            diamond.lineTo(screenPos.x() - halfW, screenPos.y());
            diamond.closeSubpath();

            painter.fillPath(diamond, color);
        }
    }
}

void MapRenderCanvas::drawTrapOverlay(QPainter& painter)
{
    int startX, startY, endX, endY;
    getVisibleTileRange(startX, startY, endX, endY);

    float halfW = TILE_WIDTH * zoomLevel / 2;
    float halfH = TILE_HEIGHT * zoomLevel / 2;

    for (int y = startY; y <= endY; y++)
    {
        for (int x = startX; x <= endX; x++)
        {
            uint8_t trap = mapEditor->getTileTrap(x, y);
            if (trap == 0)
                continue;

            QColor color = QColor(255, 255, 0, 80);

            QPoint screenPos = tileToScreenCenter(x, y);

            QPainterPath diamond;
            diamond.moveTo(screenPos.x(), screenPos.y() - halfH);
            diamond.lineTo(screenPos.x() + halfW, screenPos.y());
            diamond.lineTo(screenPos.x(), screenPos.y() + halfH);
            diamond.lineTo(screenPos.x() - halfW, screenPos.y());
            diamond.closeSubpath();

            painter.fillPath(diamond, color);

            painter.setPen(QPen(QColor(255, 255, 0, 180), 1));
            QFont font;
            font.setPixelSize(std::max(8, (int)(10 * zoomLevel)));
            painter.setFont(font);
            QRect textRect(screenPos.x() - (int)halfW, screenPos.y() - (int)halfH,
                          (int)(TILE_WIDTH * zoomLevel), (int)(TILE_HEIGHT * zoomLevel));
            painter.drawText(textRect, Qt::AlignCenter, QString::number(trap));
        }
    }
}

void MapRenderCanvas::drawEntities(QPainter& painter)
{
    struct EntityDrawEntry
    {
        const MapEntityData* entity;
        bool isSelected;
        bool isNpc;
        int index;
    };

    std::vector<EntityDrawEntry> drawList;

    if (objectVisible)
    {
        for (size_t i = 0; i < objectList.size(); i++)
        {
            bool isSelected = (selectedEntityIndex == (int)i && !selectedEntityIsNpc);
            drawList.push_back({&objectList[i], isSelected, false, (int)i});
        }
    }

    if (npcVisible)
    {
        for (size_t i = 0; i < npcList.size(); i++)
        {
            bool isSelected = (selectedEntityIndex == (int)i && selectedEntityIsNpc);
            drawList.push_back({&npcList[i], isSelected, true, (int)i});
        }
    }

    std::sort(drawList.begin(), drawList.end(),
        [](const EntityDrawEntry& left, const EntityDrawEntry& right)
        {
            if (left.entity->mapY != right.entity->mapY)
                return left.entity->mapY < right.entity->mapY;
            return left.entity->mapX < right.entity->mapX;
        });

    for (const auto& entry : drawList)
    {
        drawEntity(painter, *entry.entity, entry.isSelected);
    }
}

void MapRenderCanvas::drawEntity(QPainter& painter, const MapEntityData& entity, bool isSelected)
{
    QPoint screenPos = tileToScreen(entity.mapX, entity.mapY);
    float halfW = TILE_WIDTH * zoomLevel / 2;
    float halfH = TILE_HEIGHT * zoomLevel / 2;

    bool drewImage = false;
    int imageDrawY = screenPos.y();

    const std::string normalizedEntityIni = normalizeEditorImageResourceName(entity.iniFile);
    if (mpcCache && !normalizedEntityIni.empty())
    {
        std::string cacheKey = (entity.isNpc ? "npc|" : "object|") + normalizedEntityIni;
        std::string imagePath;

        auto cacheIt = entityResImageCache.find(cacheKey);
        if (cacheIt != entityResImageCache.end())
        {
            imagePath = cacheIt->second;
        }
        else
        {
            std::string resIniFolder = entity.isNpc ? "ini/npcres/" : "ini/objres/";
            INIFileEditor resIni;
            bool loaded = false;

            if (!mpcCache->getAssetsBasePath().empty())
            {
                loaded = resIni.loadFromFile(
                    mpcCache->getAssetsBasePath() + resIniFolder + normalizedEntityIni);
            }
            else
            {
                loaded = resIni.loadFromFile(resIniFolder + normalizedEntityIni);
            }

            if (loaded)
            {
                std::string imageName = resIni.get("stand", "Image", "");
                if (imageName.empty())
                    imageName = resIni.get("common", "Image", "");
                if (imageName.empty())
                    imageName = resIni.get("walk", "Image", "");

                for (const std::string& candidate :
                     buildEditorEntityImageCandidates(imageName, entity.isNpc))
                {
                    if (mpcCache->getFrameCount(candidate) > 0)
                    {
                        imagePath = candidate;
                        break;
                    }
                }
            }
            entityResImageCache[cacheKey] = imagePath;
            if (entityResImageCache.size() > 128)
            {
                auto it = entityResImageCache.begin();
                entityResImageCache.erase(it);
            }
        }

        if (!imagePath.empty())
        {
            int direction = entity.direction;
            int frameCount = mpcCache->getFrameCount(imagePath);
            if (frameCount > 0)
            {
                int dirCount = mpcCache->getDirection(imagePath);
                int framePerDir = dirCount > 0 ? frameCount / dirCount : frameCount;
                int interval = std::max(1, mpcCache->getInterval(imagePath));
                int animationStep = static_cast<int>(
                    animationElapsedMilliseconds / static_cast<uint64_t>(interval));
                int frameIndex = std::clamp(entity.frame, 0, frameCount - 1);

                const bool staticObject = !entity.isNpc &&
                    (entity.kind == 1 || entity.kind == 3 ||
                     entity.kind == 4 || entity.kind == 5);
                if (!staticObject)
                {
                    if (framePerDir > 1)
                        requestAnimationRefresh();
                    if (dirCount > 0 && direction >= 0)
                        frameIndex = (direction % dirCount) * framePerDir;
                    else
                        frameIndex = 0;
                    if (framePerDir > 1)
                        frameIndex += animationStep % framePerDir;
                }
                if (frameIndex >= frameCount)
                    frameIndex = 0;

                QImage frameImage = mpcCache->getFrameImage(imagePath, frameIndex);
                if (!frameImage.isNull())
                {
                    int scaledWidth = (int)(frameImage.width() * zoomLevel);
                    int scaledHeight = (int)(frameImage.height() * zoomLevel);
                    int frameOffsetX = frameImage.width() / 2;
                    int frameOffsetY = frameImage.height();
                    mpcCache->getFrameOffset(
                        imagePath, frameIndex, frameOffsetX, frameOffsetY);
                    int drawX = screenPos.x() + (int)halfW -
                                (int)std::lround(frameOffsetX * zoomLevel) +
                                (int)std::lround(entity.offsetX * zoomLevel);
                    int drawY = screenPos.y() + (int)(TILE_HEIGHT * zoomLevel) -
                                (int)std::lround(frameOffsetY * zoomLevel) +
                                (int)std::lround(entity.offsetY * zoomLevel);

                    QRect targetRect(drawX, drawY, scaledWidth, scaledHeight);
                    painter.drawImage(targetRect, frameImage);
                    drewImage = true;
                    imageDrawY = drawY;
                }
            }
        }
    }

    if (!drewImage)
    {
        // 资源缺失时绘制清晰占位：实心菱形 + 类型字母 + 描边，确保实体在地图上可见。
        QColor markerColor = entity.isNpc ? QColor(0, 200, 0, 200) : QColor(210, 180, 0, 200);
        QColor outlineColor = entity.isNpc ? QColor(120, 255, 120) : QColor(255, 230, 120);

        QPainterPath diamond;
        diamond.moveTo(screenPos.x() + halfW, screenPos.y());
        diamond.lineTo(screenPos.x() + TILE_WIDTH * zoomLevel, screenPos.y() + halfH);
        diamond.lineTo(screenPos.x() + halfW, screenPos.y() + TILE_HEIGHT * zoomLevel);
        diamond.lineTo(screenPos.x(), screenPos.y() + halfH);
        diamond.closeSubpath();

        painter.fillPath(diamond, markerColor);
        painter.setPen(QPen(outlineColor, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(diamond);

        // 在菱形中央写类型字母，便于在缩略/小地图级别也能辨认。
        if (zoomLevel >= 0.5f)
        {
            painter.setPen(QPen(Qt::white, 1));
            QFont markerFont = painter.font();
            markerFont.setPixelSize(std::max(8, (int)(TILE_HEIGHT * zoomLevel * 0.7)));
            markerFont.setBold(true);
            painter.setFont(markerFont);
            QRect letterRect(screenPos.x(), screenPos.y(),
                             (int)(TILE_WIDTH * zoomLevel), (int)(TILE_HEIGHT * zoomLevel));
            painter.drawText(letterRect, Qt::AlignCenter,
                             entity.isNpc ? tr("人") : tr("物"));
        }
    }

    if (isSelected)
    {
        QPainterPath diamond;
        diamond.moveTo(screenPos.x() + halfW, screenPos.y());
        diamond.lineTo(screenPos.x() + TILE_WIDTH * zoomLevel, screenPos.y() + halfH);
        diamond.lineTo(screenPos.x() + halfW, screenPos.y() + TILE_HEIGHT * zoomLevel);
        diamond.lineTo(screenPos.x(), screenPos.y() + halfH);
        diamond.closeSubpath();

        painter.setPen(QPen(Qt::white, 2));
        painter.drawPath(diamond);
    }

    if (!entity.name.empty())
    {
        painter.setPen(QPen(Qt::white, 1));
        QFont font;
        font.setPixelSize(std::max(8, (int)(10 * zoomLevel)));
        painter.setFont(font);
        int textWidth = (int)(TILE_WIDTH * zoomLevel) + 40;
        int nameY = drewImage ? imageDrawY - (int)(14 * zoomLevel)
                              : screenPos.y() - (int)(14 * zoomLevel);
        QRect textRect(screenPos.x() + (int)halfW - textWidth / 2,
                       nameY,
                       textWidth, (int)(12 * zoomLevel));
        painter.drawText(textRect, Qt::AlignCenter, QString::fromUtf8(entity.name.c_str()));
    }
}

void MapRenderCanvas::drawPlacingEntity(QPainter& painter)
{
    if (hoverTileX < 0 || hoverTileY < 0)
        return;

    QPoint screenPos = tileToScreen(hoverTileX, hoverTileY);
    float halfW = TILE_WIDTH * zoomLevel / 2;
    float halfH = TILE_HEIGHT * zoomLevel / 2;

    QColor color = placingEntity.isNpc ? QColor(0, 255, 0, 100) : QColor(255, 255, 0, 100);

    QPainterPath diamond;
    diamond.moveTo(screenPos.x() + halfW, screenPos.y());
    diamond.lineTo(screenPos.x() + TILE_WIDTH * zoomLevel, screenPos.y() + halfH);
    diamond.lineTo(screenPos.x() + halfW, screenPos.y() + TILE_HEIGHT * zoomLevel);
    diamond.lineTo(screenPos.x(), screenPos.y() + halfH);
    diamond.closeSubpath();

    painter.fillPath(diamond, color);
    painter.setPen(QPen(Qt::white, 1, Qt::DashLine));
    painter.drawPath(diamond);
}

void MapRenderCanvas::drawDiamond(QPainter& painter, const QPoint& center, float halfW, float halfH)
{
    QPainterPath diamond;
    diamond.moveTo(center.x(), center.y() - halfH);
    diamond.lineTo(center.x() + halfW, center.y());
    diamond.lineTo(center.x(), center.y() + halfH);
    diamond.lineTo(center.x() - halfW, center.y());
    diamond.closeSubpath();
    painter.drawPath(diamond);
}

void MapRenderCanvas::drawGrid(QPainter& painter)
{
    if (zoomLevel < 0.5f)
        return;

    int startX, startY, endX, endY;
    getVisibleTileRange(startX, startY, endX, endY);

    QPen pen(QColor(255, 255, 255, 30));
    pen.setWidth(1);
    painter.setPen(pen);

    float halfW = TILE_WIDTH * zoomLevel / 2;
    float halfH = TILE_HEIGHT * zoomLevel / 2;

    QPainterPath gridPath;
    for (int y = startY; y <= endY; y++)
    {
        for (int x = startX; x <= endX; x++)
        {
            QPoint screenPos = tileToScreen(x, y);
            QPoint center(screenPos.x() + halfW, screenPos.y() + halfH);
            gridPath.moveTo(center.x(), center.y() - halfH);
            gridPath.lineTo(center.x() + halfW, center.y());
            gridPath.lineTo(center.x(), center.y() + halfH);
            gridPath.lineTo(center.x() - halfW, center.y());
            gridPath.closeSubpath();
        }
    }
    painter.drawPath(gridPath);
}

void MapRenderCanvas::setAreaSelection(
    int startX,
    int startY,
    int endX,
    int endY,
    AreaSelectionShape shape)
{
    clearTileSelectionState();
    clearPickSelectionState();
    areaStartX = startX;
    areaStartY = startY;
    areaEndX = endX;
    areaEndY = endY;
    isAreaSelecting = false;
    isTileSelectionToggleCandidate = false;
    currentDragShape = shape;
    completedAreaShape = shape;
    pastePreviewVisible = false;
    // 同步显式选区集合，使 getSelectedAreaTiles/drawAreaSelection/copy/clear/fill
    // 都基于 selectedAreaTiles。
    selectedAreaTiles.clear();
    std::vector<QPoint> tiles = enumerateAreaTiles(startX, startY, endX, endY, shape);
    for (const QPoint& tile : tiles)
        selectedAreaTiles.insert({tile.x(), tile.y()});
    update();
}

std::vector<QPoint> MapRenderCanvas::enumerateAreaTiles(
    int startX,
    int startY,
    int endX,
    int endY,
    AreaSelectionShape shape) const
{
    if (!mapEditor || !mapEditor->isLoaded())
        return {};

    AreaTileRange range = makeAreaTileRange(
        startX,
        startY,
        endX,
        endY,
        shape,
        mapEditor->getWidth(),
        mapEditor->getHeight());
    return enumerateAreaTileRange(range, mapEditor->getWidth(), mapEditor->getHeight());
}

std::vector<QPoint> MapRenderCanvas::getSelectedAreaTiles() const
{
    std::vector<QPoint> result;
    result.reserve(selectedAreaTiles.size());
    for (const auto& tile : selectedAreaTiles)
        result.emplace_back(tile.first, tile.second);
    return result;
}

void MapRenderCanvas::drawCoordinateOverlay(QPainter& painter)
{
    if (!mapEditor || !mapEditor->isLoaded())
        return;

    int startX, startY, endX, endY;
    getVisibleTileRange(startX, startY, endX, endY);

    int step = 1;
    if (zoomLevel < 0.5f)
        step = 8;
    else if (zoomLevel < 0.75f)
        step = 4;
    else if (zoomLevel < 1.0f)
        step = 3;
    else if (zoomLevel < 1.5f)
        step = 2;

    QFont font;
    font.setPixelSize(std::max(7, (int)(9 * zoomLevel)));
    painter.setFont(font);
    painter.setPen(QPen(QColor(255, 255, 255, 120), 1));

    for (int y = startY; y <= endY; y += step)
    {
        for (int x = startX; x <= endX; x += step)
        {
            QPoint screenPos = tileToScreenCenter(x, y);
            QString coordText = QString("%1,%2").arg(x).arg(y);
            QRect textRect(screenPos.x() - (int)(TILE_WIDTH * zoomLevel / 2),
                          screenPos.y() - (int)(TILE_HEIGHT * zoomLevel / 4),
                          (int)(TILE_WIDTH * zoomLevel),
                          (int)(TILE_HEIGHT * zoomLevel / 2));
            painter.drawText(textRect, Qt::AlignCenter, coordText);
        }
    }
}

void MapRenderCanvas::drawSelection(QPainter& painter)
{
    // 持久选择框：普通瓦片选中时画黄色稳定框，实体选中时画白色粗框。
    if (selectedTileX < 0 || selectedTileY < 0)
        return;

    if (selectedTileX >= mapEditor->getWidth() || selectedTileY >= mapEditor->getHeight())
        return;

    QPoint screenPos = tileToScreen(selectedTileX, selectedTileY);
    float halfW = TILE_WIDTH * zoomLevel / 2;
    float halfH = TILE_HEIGHT * zoomLevel / 2;

    QPoint center(screenPos.x() + halfW, screenPos.y() + halfH);

    if (selectedEntityIndex >= 0)
    {
        // 实体选中：白色粗框
        painter.setPen(QPen(Qt::white, 3));
    }
    else
    {
        // 普通瓦片选中：黄色稳定框（与 hover 框区分，稍粗）
        painter.setPen(QPen(QColor(255, 200, 0), 2.5));
    }
    drawDiamond(painter, center, halfW, halfH);
}

void MapRenderCanvas::drawHoverHighlight(QPainter& painter)
{
    // 轻量 hover 提示：虚线框跟随鼠标，不与选中框重叠。
    if (hoverTileX < 0 || hoverTileY < 0)
        return;

    if (!mapEditor || !mapEditor->isLoaded())
        return;

    if (hoverTileX >= mapEditor->getWidth() || hoverTileY >= mapEditor->getHeight())
        return;

    // Pan 工具不显示 hover 高亮。
    if (editTool == MapEditTool::Pan)
        return;

    // 不与选中框重叠绘制。
    if (hoverTileX == selectedTileX && hoverTileY == selectedTileY)
        return;

    QPoint screenPos = tileToScreen(hoverTileX, hoverTileY);
    float halfW = TILE_WIDTH * zoomLevel / 2;
    float halfH = TILE_HEIGHT * zoomLevel / 2;

    QPoint center(screenPos.x() + halfW, screenPos.y() + halfH);

    painter.setPen(QPen(QColor(255, 255, 0, 128), 1, Qt::DashLine));
    drawDiamond(painter, center, halfW, halfH);
}

QImage MapRenderCanvas::getMpcFrameImage(int mpcIndex, int frameIndex) const
{
    if (!mpcCache || !mapEditor)
        return QImage();

    if (mpcIndex <= 0 || mpcIndex > MAP_EDITOR_MPC_COUNT)
        return QImage();

    int arrayIndex = mpcIndex - 1;

    std::string mpcPath = mapEditor->getMpcFilePath(arrayIndex);
    if (mpcPath.empty())
        return QImage();

    return mpcCache->getFrameImage(
        mpcPath, resolveMpcFrameIndex(mpcIndex, frameIndex));
}

MapRenderCanvas::ScaledMpcFrameVisual MapRenderCanvas::getScaledMpcFrameVisual(
    int mpcIndex, int frameIndex)
{
    ScaledMpcFrameVisual result;
    if (!mpcCache || !mapEditor || mpcIndex <= 0 ||
        mpcIndex > MAP_EDITOR_MPC_COUNT)
    {
        return result;
    }

    const int arrayIndex = mpcIndex - 1;
    const std::string mpcPath = mapEditor->getMpcFilePath(arrayIndex);
    if (mpcPath.empty())
        return result;

    const int resolvedFrameIndex = resolveMpcFrameIndex(mpcIndex, frameIndex);
    if (resolvedFrameIndex < 0 || resolvedFrameIndex > 0x00ffffff)
        return result;

    auto makeCacheKey = [&](float targetZoom)
    {
        uint32_t zoomBits = 0;
        static_assert(sizeof(zoomBits) == sizeof(targetZoom));
        std::memcpy(&zoomBits, &targetZoom, sizeof(zoomBits));
        return
            (static_cast<quint64>(static_cast<uint8_t>(mpcIndex)) << 56) |
            (static_cast<quint64>(
                static_cast<uint32_t>(resolvedFrameIndex)) << 32) |
            zoomBits;
    };

    const quint64 cacheKey = makeCacheKey(zoomLevel);
    if (const ScaledMpcFrameVisual* cached = scaledMpcFrameCache.object(cacheKey))
        return *cached;

    const QImage sourceImage = mpcCache->getFrameImage(
        mpcPath, resolvedFrameIndex);
    if (sourceImage.isNull())
        return result;

    int frameOffsetX = sourceImage.width() / 2;
    int frameOffsetY = sourceImage.height();
    mpcCache->getFrameOffset(
        mpcPath,
        resolvedFrameIndex,
        frameOffsetX,
        frameOffsetY);

    auto createVisual = [&](float targetZoom)
    {
        ScaledMpcFrameVisual visual;
        visual.frameOffsetX = frameOffsetX;
        visual.frameOffsetY = frameOffsetY;
        const int scaledWidth = static_cast<int>(
            sourceImage.width() * targetZoom);
        const int scaledHeight = static_cast<int>(
            sourceImage.height() * targetZoom);
        if (scaledWidth <= 0 || scaledHeight <= 0)
            return visual;

        if (scaledWidth == sourceImage.width() &&
            scaledHeight == sourceImage.height())
        {
            visual.image = sourceImage;
        }
        else
        {
            visual.image = sourceImage.scaled(
                scaledWidth,
                scaledHeight,
                Qt::IgnoreAspectRatio,
                targetZoom < 1.0f
                    ? Qt::SmoothTransformation
                    : Qt::FastTransformation);
        }
        if (!visual.image.isNull() &&
            visual.image.format() != QImage::Format_ARGB32_Premultiplied)
        {
            visual.image = visual.image.convertToFormat(
                QImage::Format_ARGB32_Premultiplied);
        }
        return visual;
    };

    auto insertVisual = [&](quint64 key, const ScaledMpcFrameVisual& visual)
    {
        if (visual.image.isNull())
            return;
        const qint64 imageCostKiB = std::max<qint64>(
            1, (visual.image.sizeInBytes() + 1023) / 1024);
        scaledMpcFrameCache.insert(
            key,
            new ScaledMpcFrameVisual(visual),
            static_cast<int>(std::min<qint64>(imageCostKiB, INT_MAX)));
    };

    result = createVisual(zoomLevel);
    if (result.image.isNull())
        return result;
    insertVisual(cacheKey, result);

    const float adjacentZoomLevels[] = {
        std::clamp(zoomLevel / 1.25f, 0.01f, 4.0f),
        std::clamp(zoomLevel * 1.25f, 0.01f, 4.0f)
    };
    for (float adjacentZoom : adjacentZoomLevels)
    {
        if (qFuzzyCompare(adjacentZoom, zoomLevel))
            continue;
        const quint64 adjacentKey = makeCacheKey(adjacentZoom);
        if (!scaledMpcFrameCache.object(adjacentKey))
            insertVisual(adjacentKey, createVisual(adjacentZoom));
    }

#ifdef JXQY_EDITOR_BUILD_BENCHMARK
    if (qApp && qApp->property("editorPerformanceBenchmark").toBool())
    {
        setProperty(
            "performanceScaledFrameCacheMisses",
            property("performanceScaledFrameCacheMisses").toInt() + 1);
    }
#endif

    return result;
}

int MapRenderCanvas::resolveMpcFrameIndex(int mpcIndex, int storedFrameIndex) const
{
    if (!mpcCache || !mapEditor || mpcIndex <= 0 ||
        mpcIndex > MAP_EDITOR_MPC_COUNT)
    {
        return storedFrameIndex;
    }
    int arrayIndex = mpcIndex - 1;
    std::string mpcPath = mapEditor->getMpcFilePath(arrayIndex);
    const MpcInfoData& info = mapEditor->getMpcInfo(arrayIndex);
    if (mpcPath.empty() || info.dynamic == 0)
        return storedFrameIndex;

    int frameCount = mpcCache->getFrameCount(mpcPath);
    if (frameCount <= 0)
        return storedFrameIndex;
    if (frameCount > 1)
        requestAnimationRefresh();
    int interval = std::max(1, mpcCache->getInterval(mpcPath));
    return static_cast<int>(
        (animationElapsedMilliseconds / static_cast<uint64_t>(interval)) %
        static_cast<uint64_t>(frameCount));
}

bool MapRenderCanvas::getMpcFrameOffset(int mpcIndex, int storedFrameIndex,
                                        int& offsetX, int& offsetY) const
{
    if (!mpcCache || !mapEditor || mpcIndex <= 0 ||
        mpcIndex > MAP_EDITOR_MPC_COUNT)
    {
        return false;
    }
    std::string mpcPath = mapEditor->getMpcFilePath(mpcIndex - 1);
    if (mpcPath.empty())
        return false;
    return mpcCache->getFrameOffset(
        mpcPath, resolveMpcFrameIndex(mpcIndex, storedFrameIndex),
        offsetX, offsetY);
}

void MapRenderCanvas::getTileWorldPosition(int tileX, int tileY, int& worldX, int& worldY) const
{
    if (!mapEditor || !mapEditor->isLoaded())
    {
        worldX = worldY = 0;
        return;
    }

    QPoint world = canvasTransform.tileToWorldTopLeft(mapEditor->getWidth(), tileX, tileY);
    worldX = world.x();
    worldY = world.y();
}

void MapRenderCanvas::getVisibleTileRange(int& startX, int& startY, int& endX, int& endY) const
{
    if (!mapEditor || !mapEditor->isLoaded())
    {
        startX = startY = endX = endY = 0;
        return;
    }

    int mapWidth = mapEditor->getWidth();
    int mapHeight = mapEditor->getHeight();

    // 不再依赖四个视口角的“命中 tile”——极端缩放（视口远大于地图）时四角都落在地图
    // 外，旧实现回退到固定 (0,0)-(10,10)，导致大地图极度缩小时只显示左上一小块。
    // 改为：把视口屏幕矩形换算成世界矩形，用粗估（允许越界）取四个角瓦片，取 min/max
    // 再裁剪到地图边界并外扩余量，覆盖极端缩放下的整张地图。
    QPointF worldCorners[4] = {
        QPointF(scrollX / (double)zoomLevel, scrollY / (double)zoomLevel),
        QPointF((scrollX + width()) / (double)zoomLevel, scrollY / (double)zoomLevel),
        QPointF(scrollX / (double)zoomLevel, (scrollY + height()) / (double)zoomLevel),
        QPointF((scrollX + width()) / (double)zoomLevel, (scrollY + height()) / (double)zoomLevel)
    };

    int minTileX = std::numeric_limits<int>::max();
    int minTileY = std::numeric_limits<int>::max();
    int maxTileX = std::numeric_limits<int>::min();
    int maxTileY = std::numeric_limits<int>::min();

    for (int i = 0; i < 4; i++)
    {
        QPoint estimate = canvasTransform.estimateTileFromWorld(worldCorners[i], mapWidth);
        minTileX = std::min(minTileX, estimate.x());
        minTileY = std::min(minTileY, estimate.y());
        maxTileX = std::max(maxTileX, estimate.x());
        maxTileY = std::max(maxTileY, estimate.y());
    }

    const int margin = 3;
    MapCoordinateTransform::clampVisibleTileRange(
        minTileX, minTileY, maxTileX, maxTileY, mapWidth, mapHeight, margin,
        startX, startY, endX, endY);

    // 估算范围完全在地图某一侧时，夹到最近边界后 startX > endX 表示无交集。
    // 渲染循环 (x <= endX) 自然跳过，不渲染整图——避免视口移出地图后仍显示全图。
}

void MapRenderCanvas::getVisibleTileRenderRange(int& startX, int& startY, int& endX, int& endY) const
{
    // 先取基础 tile 菱形可见范围（含固定 margin=3），再按 MPC 帧可能高于 64x32 的
    // 程度外扩。保守做法：遍历地图已使用的 MPC 槽位取最大帧宽高，换算成可向上/左右
    // 覆盖的额外 tile 行数，加到 margin 上。这样 tile 菱形可见性逻辑不变，只放宽
    // 图像渲染范围，避免顶部/底部边缘高图 tile 被裁掉。
    getVisibleTileRange(startX, startY, endX, endY);
    if (!mapEditor || !mapEditor->isLoaded() || !mpcCache)
        return;
    if (startX > endX || startY > endY)
        return;

    // 缓存“已用 MPC 最大帧尺寸”，避免每帧 paintEvent 扫描 255 个槽位并解码帧。
    // 缓存键含 mapEditor 指针、mpcCache 基路径、地图已用 MPC 槽位数；任一变化才重算。
    QString cacheKey = QString("%1|%2|%3")
        .arg(reinterpret_cast<quintptr>(mapEditor))
        .arg(QString::fromStdString(mpcCache->getAssetsBasePath()))
        .arg(mapEditor->getUsedMpcCount());
    if (!renderRangeCacheValid || renderRangeCacheKey != cacheKey)
    {
        int maxWidth = TILE_WIDTH;
        int maxHeight = TILE_HEIGHT;
        for (int slot = 0; slot < MAP_EDITOR_MPC_COUNT; slot++)
        {
            const MpcInfoData& info = mapEditor->getMpcInfo(slot);
            if (info.name.empty())
                continue;
            std::string mpcPath = mapEditor->getMpcFilePath(slot);
            int frameCount = mpcCache->getFrameCount(mpcPath);
            if (frameCount <= 0)
                continue;
            // 遍历所有帧取最大宽高：多帧 MPC 后续帧可能比第 0 帧更高/更宽，
            // 只看第 0 帧会导致顶部/底部边缘高图 tile 被裁掉。
            for (int frame = 0; frame < frameCount; frame++)
            {
                QImage frameImage = mpcCache->getFrameImage(mpcPath, frame);
                if (frameImage.isNull())
                    continue;
                maxWidth = std::max(maxWidth, frameImage.width());
                maxHeight = std::max(maxHeight, frameImage.height());
            }
        }
        renderRangeMaxFrameWidth = maxWidth;
        renderRangeMaxFrameHeight = maxHeight;
        renderRangeCacheKey = cacheKey;
        renderRangeCacheValid = true;
    }
    int maxFrameWidth = renderRangeMaxFrameWidth;
    int maxFrameHeight = renderRangeMaxFrameHeight;

    // drawMapLayer 中 drawY = screenPos.y() - scaledHeight + TILE_HEIGHT*zoom：
    // 图像从 tile 顶部锚点向上扩展 scaledHeight - TILE_HEIGHT*zoom 像素，向下一般
    // 不超过 tile 本身高度。按世界坐标换算额外需要的 tile 行数（向上/下）。
    // staggered iso 投影中每行垂直间距是 TILE_HEIGHT/2（奇偶行交错），因此纵向
    // 外扩必须按半高计算，否则高图向上覆盖 224px 时只算 7 行而实际需要约 14 行。
    float tw = TILE_WIDTH * zoomLevel;
    float th = TILE_HEIGHT * zoomLevel;
    if (tw <= 0.0f || th <= 0.0f)
        return;
    float rowSpacing = th * 0.5f;
    int extraVertical = (int)std::ceil((maxFrameHeight * zoomLevel - th) / rowSpacing);
    int extraHorizontal = (int)std::ceil((maxFrameWidth * zoomLevel - tw) / tw);
    if (extraVertical < 0)
        extraVertical = 0;
    if (extraHorizontal < 0)
        extraHorizontal = 0;
    // 至少留出基础 margin=3 已覆盖的部分，这里只在它之上再外扩。
    int mapWidth = mapEditor->getWidth();
    int mapHeight = mapEditor->getHeight();
    startX = std::max(0, startX - extraHorizontal);
    startY = std::max(0, startY - extraVertical);
    endX = std::min(mapWidth - 1, endX + extraHorizontal);
    endY = std::min(mapHeight - 1, endY + extraVertical);
}

int MapRenderCanvas::findEntityAtTile(int tileX, int tileY, bool isNpc) const
{
    const std::vector<MapEntityData>& list = isNpc ? npcList : objectList;
    for (size_t i = 0; i < list.size(); i++)
    {
        if (list[i].mapX == tileX && list[i].mapY == tileY)
            return (int)i;
    }
    return -1;
}

void MapRenderCanvas::updateAnimation()
{
    if (!isVisible() || !mapEditor || !mapEditor->isLoaded() ||
        !animationRequiredForCurrentPaint)
    {
        animationTimer.stop();
        animationTickClock.invalidate();
        return;
    }

    const qint64 elapsedMilliseconds = animationTickClock.isValid()
        ? animationTickClock.restart()
        : animationTimer.interval();
    animationElapsedMilliseconds += static_cast<uint64_t>(
        std::max<qint64>(1, elapsedMilliseconds));
    update();
}

void MapRenderCanvas::requestAnimationRefresh() const
{
    if (collectingAnimationRequirements)
        animationRequiredForCurrentPaint = true;
}

void MapRenderCanvas::updateAnimationTimer()
{
    if (animationRequiredForCurrentPaint && isVisible() &&
        mapEditor && mapEditor->isLoaded())
    {
        if (!animationTimer.isActive())
        {
            animationTickClock.start();
            animationTimer.start();
        }
        return;
    }

    animationTimer.stop();
    animationTickClock.invalidate();
}

void MapRenderCanvas::mousePressEvent(QMouseEvent* event)
{
    if (!mapEditor || !mapEditor->isLoaded())
        return;

    QPoint tilePos = screenToTile(event->pos());
    int tileX = tilePos.x();
    int tileY = tilePos.y();

    if (event->button() == Qt::MiddleButton)
    {
        isPanning = true;
        lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (event->button() == Qt::RightButton)
    {
        // 右键：绘图或区域粘贴（UPEdit 模式）
        if (tileX >= 0 && tileY >= 0 && tileX < mapEditor->getWidth() && tileY < mapEditor->getHeight())
        {
            // 实体选中：右键点中实体时仍选中实体（保持原行为）
            int npcHit = findEntityAtTile(tileX, tileY, true);
            int objHit = findEntityAtTile(tileX, tileY, false);

            // 障碍/陷阱工具下右键：绘制当前画笔值（与 Tile 工具右键绘制一致）。
            // 必须在剪贴板粘贴之前判断，避免复制区域后切到障碍/陷阱工具时
            // 右键误触发 Tile 粘贴而非绘制障碍/陷阱。
            // 设置 isRightDragging 以支持右键拖动连续绘制。
            if (editTool == MapEditTool::ObstaclePaint)
            {
                emit tileAboutToBeEdited(tileX, tileY);
                mapEditor->setTileObstacle(tileX, tileY, paintObstacle);
                emit tileEdited(tileX, tileY);
                isRightDragging = true;
                dragStartTile = QPoint(tileX, tileY);
                update();
                return;
            }
            if (editTool == MapEditTool::TrapPaint)
            {
                emit tileAboutToBeEdited(tileX, tileY);
                mapEditor->setTileTrap(tileX, tileY, paintTrapIndex);
                emit tileEdited(tileX, tileY);
                isRightDragging = true;
                dragStartTile = QPoint(tileX, tileY);
                update();
                return;
            }

            // 若剪贴板有范围数据，优先执行区域粘贴（鼠标瓦片作为区域左侧锚点）。
            // 仅对支持粘贴的工具生效（Select/TilePaint/TilePicker）。
            // AreaSelect/Pan/NpcPlace/ObjectPlace/ObstaclePaint/TrapPaint 右键不粘贴：
            // AreaSelect 只用于框选；Pan 用于平移；NpcPlace/ObjectPlace 右键应选中实体
            // 而非修改地图 tile；ObstaclePaint/TrapPaint 已在上方提前 return。
            if ((editTool == MapEditTool::Select ||
                 editTool == MapEditTool::TilePaint ||
                 editTool == MapEditTool::TilePicker) &&
                !clipboardTiles.empty() && pastePreviewVisible)
            {
                pasteArea(tileX, tileY);
                return;
            }

            // AreaSelect 工具下右键：no-op（不绘制、不粘贴、不修改地图）。
            if (editTool == MapEditTool::AreaSelect)
            {
                return;
            }

            // 实体工具下右键点中实体：选中实体（便于后续编辑/删除）
            if (npcHit >= 0)
            {
                selectedTileX = tileX;
                selectedTileY = tileY;
                selectedEntityIndex = npcHit;
                selectedEntityIsNpc = true;
                emit entitySelected(npcHit, true);
                update();
                return;
            }
            if (objHit >= 0)
            {
                selectedTileX = tileX;
                selectedTileY = tileY;
                selectedEntityIndex = objHit;
                selectedEntityIsNpc = false;
                emit entitySelected(objHit, false);
                update();
                return;
            }

            // 瓦片类工具（Select/TilePaint/TilePicker）下右键：单格绘图
            if (editTool == MapEditTool::Select || editTool == MapEditTool::TilePaint ||
                editTool == MapEditTool::TilePicker)
            {
                emit tileAboutToBeEdited(tileX, tileY);
                if (paintAllLayers)
                {
                    if (hasMultiLayerPaintBrushState)
                    {
                        // 全部图层 + 多层画笔：把拾取到的三层分别写回对应图层。
                        const MapTileData& brush = multiLayerPaintBrush;
                        mapEditor->setTileLayer(tileX, tileY, 0, brush.layer[0]);
                        mapEditor->setTileLayer(tileX, tileY, 1, brush.layer[1]);
                        mapEditor->setTileLayer(tileX, tileY, 2, brush.layer[2]);
                    }
                    else
                    {
                        // 全部图层 + 单 mpc/frame：把同一画笔写入三层（旧行为）。
                        MapTileLayerData layerData;
                        layerData.frame = (uint8_t)paintFrameIndex;
                        layerData.mpc = (uint8_t)paintMpcIndex;
                        mapEditor->setTileLayer(tileX, tileY, 0, layerData);
                        mapEditor->setTileLayer(tileX, tileY, 1, layerData);
                        mapEditor->setTileLayer(tileX, tileY, 2, layerData);
                    }
                }
                else
                {
                    MapTileLayerData layerData;
                    layerData.frame = (uint8_t)paintFrameIndex;
                    layerData.mpc = (uint8_t)paintMpcIndex;
                    mapEditor->setTileLayer(tileX, tileY, paintLayer, layerData);
                }
                emit tileEdited(tileX, tileY);
                update();
                return;
            }
        }
        return;
    }

    if (tileX < 0 || tileY < 0 || tileX >= mapEditor->getWidth() || tileY >= mapEditor->getHeight())
    {
        // 即使落在地图外，Pan 工具也应能开始拖拽（便于从边缘拉回视图）。
        if (editTool == MapEditTool::Pan && event->button() == Qt::LeftButton)
        {
            isPanning = true;
            lastMousePos = event->pos();
            setCursor(Qt::ClosedHandCursor);
        }
        return;
    }

    if (editTool == MapEditTool::Pan)
    {
        // Pan 工具左键拖拽地图：开始平移，不选择/绘制/移动实体。
        isPanning = true;
        lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (editTool == MapEditTool::Select)
    {
        // 点击普通瓦片时也设置 selectedTileX/Y 形成稳定选中框（黄色），
        // 并通过 tileClicked 信号更新右侧信息面板。实体选中则用白色框。
        int npcIndex = findEntityAtTile(tileX, tileY, true);
        int objIndex = findEntityAtTile(tileX, tileY, false);

        if (npcIndex >= 0)
        {
            selectedTileX = tileX;
            selectedTileY = tileY;
            selectedEntityIndex = npcIndex;
            selectedEntityIsNpc = true;
            isDragging = true;
            dragStartTile = QPoint(tileX, tileY);
            dragOriginalMapX = npcList[npcIndex].mapX;
            dragOriginalMapY = npcList[npcIndex].mapY;
            emit entityMoveStarted(npcIndex, true, dragOriginalMapX, dragOriginalMapY);
            emit entitySelected(npcIndex, true);
        }
        else if (objIndex >= 0)
        {
            selectedTileX = tileX;
            selectedTileY = tileY;
            selectedEntityIndex = objIndex;
            selectedEntityIsNpc = false;
            isDragging = true;
            dragStartTile = QPoint(tileX, tileY);
            dragOriginalMapX = objectList[objIndex].mapX;
            dragOriginalMapY = objectList[objIndex].mapY;
            emit entityMoveStarted(objIndex, false, dragOriginalMapX, dragOriginalMapY);
            emit entitySelected(objIndex, false);
        }
        else
        {
            // 普通瓦片：开始左键拾取拖拽（UPEdit 模式）
            // 单击松开 = 单点拾取；拖拽到另一瓦片松开 = 范围拾取
            bool isToggleCandidate = selectedEntityIndex < 0 &&
                selectedTileX == tileX && selectedTileY == tileY;
            clearAreaSelectionState();
            isPickDragging = true;
            pickStartX = tileX;
            pickStartY = tileY;
            pickEndX = tileX;
            pickEndY = tileY;
            // 拖拽开始时锁定形状，切换 checkbox 不影响本次拖拽。
            currentDragShape = preferredAreaShape;
            // 同时设置选中瓦片，便于右侧面板显示信息
            selectedTileX = tileX;
            selectedTileY = tileY;
            selectedEntityIndex = -1;
            isTileSelectionToggleCandidate = isToggleCandidate;
            pastePreviewVisible = false;
            emit tileClicked(tileX, tileY, event->button());
        }
    }
    else if (editTool == MapEditTool::TilePaint)
    {
        // 瓦片绘制工具下，左键也改为拾取（UPEdit 模式）
        bool isToggleCandidate = selectedEntityIndex < 0 &&
            selectedTileX == tileX && selectedTileY == tileY;
        clearAreaSelectionState();
        isPickDragging = true;
        pickStartX = tileX;
        pickStartY = tileY;
        pickEndX = tileX;
        pickEndY = tileY;
        currentDragShape = preferredAreaShape;
        selectedTileX = tileX;
        selectedTileY = tileY;
        selectedEntityIndex = -1;
        isTileSelectionToggleCandidate = isToggleCandidate;
        pastePreviewVisible = false;
        emit tileClicked(tileX, tileY, event->button());
    }
    else if (editTool == MapEditTool::ObstaclePaint)
    {
        // 左键拾取障碍（与 Tile 工具左键拾取一致）：读取瓦片障碍值，不修改地图。
        uint8_t picked = mapEditor->getTileObstacle(tileX, tileY);
        selectedTileX = tileX;
        selectedTileY = tileY;
        selectedEntityIndex = -1;
        emit obstaclePicked(picked, tileX, tileY);
        emit tileClicked(tileX, tileY, event->button());
    }
    else if (editTool == MapEditTool::TrapPaint)
    {
        // 左键拾取陷阱（与 Tile 工具左键拾取一致）：读取瓦片陷阱索引，不修改地图。
        uint8_t picked = mapEditor->getTileTrap(tileX, tileY);
        selectedTileX = tileX;
        selectedTileY = tileY;
        selectedEntityIndex = -1;
        emit trapPicked(picked, tileX, tileY);
        emit tileClicked(tileX, tileY, event->button());
    }
    else if (editTool == MapEditTool::NpcPlace)
    {
        if (hasPlacingEntity)
        {
            placingEntity.mapX = tileX;
            placingEntity.mapY = tileY;
            npcList.push_back(placingEntity);
            selectedEntityIndex = (int)npcList.size() - 1;
            selectedEntityIsNpc = true;
            selectedTileX = tileX;
            selectedTileY = tileY;
            emit entitySelected(selectedEntityIndex, true);
            emit entityPlaced(selectedEntityIndex, true);
            emit entityListChanged();
            if (!continuousPlace)
            {
                // 非连续放置：放置完毕后没有待放置实体，若仍停留在放置模式，下次点击会
                // 毫无反馈，用户以为工具失效。这里自动切回选择工具并通知主窗口同步 UI。
                hasPlacingEntity = false;
                setEditTool(MapEditTool::Select);
            }
        }
        emit tileClicked(tileX, tileY, event->button());
    }
    else if (editTool == MapEditTool::ObjectPlace)
    {
        if (hasPlacingEntity)
        {
            placingEntity.mapX = tileX;
            placingEntity.mapY = tileY;
            objectList.push_back(placingEntity);
            selectedEntityIndex = (int)objectList.size() - 1;
            selectedEntityIsNpc = false;
            selectedTileX = tileX;
            selectedTileY = tileY;
            emit entitySelected(selectedEntityIndex, false);
            emit entityPlaced(selectedEntityIndex, false);
            emit entityListChanged();
            if (!continuousPlace)
            {
                hasPlacingEntity = false;
                setEditTool(MapEditTool::Select);
            }
        }
        emit tileClicked(tileX, tileY, event->button());
    }
    else if (editTool == MapEditTool::TilePicker)
    {
        bool isToggleCandidate = selectedEntityIndex < 0 &&
            selectedTileX == tileX && selectedTileY == tileY;
        clearAreaSelectionState();
        // 吸管工具：左键拾取（与 Select/TilePaint 一致，支持拖拽范围拾取）
        isPickDragging = true;
        pickStartX = tileX;
        pickStartY = tileY;
        pickEndX = tileX;
        pickEndY = tileY;
        currentDragShape = preferredAreaShape;
        selectedTileX = tileX;
        selectedTileY = tileY;
        selectedEntityIndex = -1;
        isTileSelectionToggleCandidate = isToggleCandidate;
        pastePreviewVisible = false;
        emit tileClicked(tileX, tileY, event->button());
    }
    else if (editTool == MapEditTool::AreaSelect)
    {
        bool isToggleCandidate = selectedEntityIndex < 0 &&
            selectedTileX == tileX && selectedTileY == tileY;
        clearPickSelectionState();
        selectedTileX = -1;
        selectedTileY = -1;
        selectedEntityIndex = -1;
        areaStartX = tileX;
        areaStartY = tileY;
        areaEndX = tileX;
        areaEndY = tileY;
        isAreaSelecting = true;
        isTileSelectionToggleCandidate = isToggleCandidate;
        // 拖拽开始时锁定形状，切换 checkbox 不影响本次拖拽。
        currentDragShape = preferredAreaShape;
        pastePreviewVisible = false;
    }

    update();
}

void MapRenderCanvas::mouseMoveEvent(QMouseEvent* event)
{
    if (isPanning)
    {
        QPoint delta = event->pos() - lastMousePos;
        scrollX -= delta.x();
        scrollY -= delta.y();
        lastMousePos = event->pos();
        update();
        emit viewportChanged();
        return;
    }

    if (!mapEditor || !mapEditor->isLoaded())
        return;

    QPoint tilePos = screenToTile(event->pos());
    int tileX = tilePos.x();
    int tileY = tilePos.y();

    bool needUpdate = false;

    if (tileX != hoverTileX || tileY != hoverTileY)
    {
        hoverTileX = tileX;
        hoverTileY = tileY;
        emit tileHovered(tileX, tileY);
        needUpdate = true;
    }

    if (isDragging && tileX >= 0 && tileY >= 0 &&
        tileX < mapEditor->getWidth() && tileY < mapEditor->getHeight())
    {
        if (editTool == MapEditTool::Select && selectedEntityIndex >= 0)
        {
            if (tileX != dragStartTile.x() || tileY != dragStartTile.y())
            {
                std::vector<MapEntityData>& list = selectedEntityIsNpc ? npcList : objectList;
                if (selectedEntityIndex < (int)list.size())
                {
                    list[selectedEntityIndex].mapX = tileX;
                    list[selectedEntityIndex].mapY = tileY;
                    dragStartTile = QPoint(tileX, tileY);
                    needUpdate = true;
                }
            }
        }
        else if (editTool == MapEditTool::TilePaint)
        {
            if (tileX != dragStartTile.x() || tileY != dragStartTile.y())
            {
                emit tileAboutToBeEdited(tileX, tileY);
                if (paintAllLayers)
                {
                    if (hasMultiLayerPaintBrushState)
                    {
                        const MapTileData& brush = multiLayerPaintBrush;
                        mapEditor->setTileLayer(tileX, tileY, 0, brush.layer[0]);
                        mapEditor->setTileLayer(tileX, tileY, 1, brush.layer[1]);
                        mapEditor->setTileLayer(tileX, tileY, 2, brush.layer[2]);
                    }
                    else
                    {
                        MapTileLayerData layerData;
                        layerData.frame = (uint8_t)paintFrameIndex;
                        layerData.mpc = (uint8_t)paintMpcIndex;
                        mapEditor->setTileLayer(tileX, tileY, 0, layerData);
                        mapEditor->setTileLayer(tileX, tileY, 1, layerData);
                        mapEditor->setTileLayer(tileX, tileY, 2, layerData);
                    }
                }
                else
                {
                    MapTileLayerData layerData;
                    layerData.frame = (uint8_t)paintFrameIndex;
                    layerData.mpc = (uint8_t)paintMpcIndex;
                    mapEditor->setTileLayer(tileX, tileY, paintLayer, layerData);
                }
                dragStartTile = QPoint(tileX, tileY);
                emit tileEdited(tileX, tileY);
                needUpdate = true;
            }
        }
    }

    // 右键拖动连续绘制障碍/陷阱：拖到新瓦片时绘制，不重复绘制同一瓦片。
    if (isRightDragging && tileX >= 0 && tileY >= 0 &&
        tileX < mapEditor->getWidth() && tileY < mapEditor->getHeight())
    {
        if (tileX != dragStartTile.x() || tileY != dragStartTile.y())
        {
            if (editTool == MapEditTool::ObstaclePaint)
            {
                emit tileAboutToBeEdited(tileX, tileY);
                mapEditor->setTileObstacle(tileX, tileY, paintObstacle);
                emit tileEdited(tileX, tileY);
                dragStartTile = QPoint(tileX, tileY);
                needUpdate = true;
            }
            else if (editTool == MapEditTool::TrapPaint)
            {
                emit tileAboutToBeEdited(tileX, tileY);
                mapEditor->setTileTrap(tileX, tileY, paintTrapIndex);
                emit tileEdited(tileX, tileY);
                dragStartTile = QPoint(tileX, tileY);
                needUpdate = true;
            }
        }
    }

    if (isAreaSelecting && tileX >= 0 && tileY >= 0)
    {
        if (tileX != areaEndX || tileY != areaEndY)
        {
            areaEndX = tileX;
            areaEndY = tileY;
            if (tileX != areaStartX || tileY != areaStartY)
                isTileSelectionToggleCandidate = false;
            needUpdate = true;
        }
    }

    // 左键拾取拖拽：实时更新范围拾取预览
    if (isPickDragging && tileX >= 0 && tileY >= 0)
    {
        if (tileX != pickEndX || tileY != pickEndY)
        {
            pickEndX = tileX;
            pickEndY = tileY;
            if (tileX != pickStartX || tileY != pickStartY)
                isTileSelectionToggleCandidate = false;
            needUpdate = true;
        }
    }

    if (needUpdate)
        update();
}

void MapRenderCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton)
    {
        isPanning = false;
        switch (editTool)
        {
        case MapEditTool::Select:
            setCursor(Qt::ArrowCursor);
            break;
        case MapEditTool::NpcPlace:
        case MapEditTool::ObjectPlace:
            setCursor(Qt::PointingHandCursor);
            break;
        case MapEditTool::Pan:
            setCursor(Qt::OpenHandCursor);
            break;
        default:
            setCursor(Qt::CrossCursor);
            break;
        }
        return;
    }

    // Pan 工具左键松开：停止平移，恢复 OpenHand 光标。
    if (event->button() == Qt::LeftButton && editTool == MapEditTool::Pan && isPanning)
    {
        isPanning = false;
        setCursor(Qt::OpenHandCursor);
        return;
    }

    if (isDragging && editTool == MapEditTool::Select && selectedEntityIndex >= 0)
    {
        std::vector<MapEntityData>& list = selectedEntityIsNpc ? npcList : objectList;
        if (selectedEntityIndex < (int)list.size())
        {
            int currentMapX = list[selectedEntityIndex].mapX;
            int currentMapY = list[selectedEntityIndex].mapY;
            if (currentMapX != dragOriginalMapX || currentMapY != dragOriginalMapY)
            {
                emit entityMoved(selectedEntityIndex, selectedEntityIsNpc, currentMapX, currentMapY);
            }
        }
    }

    if (isAreaSelecting)
    {
        int clickedTileX = areaStartX;
        int clickedTileY = areaStartY;
        bool isSingleTileClick = areaStartX == areaEndX && areaStartY == areaEndY;
        isAreaSelecting = false;

        bool withCtrl = event->modifiers() & Qt::ControlModifier;
        bool withAlt = event->modifiers() & Qt::AltModifier;

        if (isSingleTileClick && !withCtrl && !withAlt)
        {
            // 普通单点点击：清空区域选区并设置 tile 选区（保持旧行为）。
            clearAreaSelectionState();
            if (isTileSelectionToggleCandidate)
            {
                clearTileSelectionState();
                emit selectionCleared();
            }
            else
            {
                selectedTileX = clickedTileX;
                selectedTileY = clickedTileY;
                selectedEntityIndex = -1;
                emit tileClicked(clickedTileX, clickedTileY, event->button());
            }
        }
        else if (isSingleTileClick && (withCtrl || withAlt))
        {
            // Ctrl+点击：把单个 tile union 到 selectedAreaTiles。
            // Alt+点击：从 selectedAreaTiles subtract 单个 tile；结果为空则清除区域选区。
            clearTileSelectionState();
            isTileSelectionToggleCandidate = false;
            completedAreaShape = currentDragShape;
            if (withCtrl)
            {
                selectedAreaTiles.insert({clickedTileX, clickedTileY});
            }
            else
            {
                selectedAreaTiles.erase({clickedTileX, clickedTileY});
                if (selectedAreaTiles.empty())
                {
                    areaStartX = -1;
                    areaStartY = -1;
                    areaEndX = -1;
                    areaEndY = -1;
                }
            }
        }
        else
        {
            // 拖拽完成：按 modifier 合并到 selectedAreaTiles。
            clearTileSelectionState();
            isTileSelectionToggleCandidate = false;
            completedAreaShape = currentDragShape;

            std::vector<QPoint> draggedTiles = enumerateAreaTiles(
                areaStartX, areaStartY, areaEndX, areaEndY, currentDragShape);

            if (withCtrl)
            {
                // Union：把拖拽区域并入当前选区。
                for (const QPoint& tile : draggedTiles)
                    selectedAreaTiles.insert({tile.x(), tile.y()});
            }
            else if (withAlt)
            {
                // Subtract：从当前选区移除拖拽区域；结果为空则清除区域选区。
                for (const QPoint& tile : draggedTiles)
                    selectedAreaTiles.erase({tile.x(), tile.y()});
                if (selectedAreaTiles.empty())
                {
                    areaStartX = -1;
                    areaStartY = -1;
                    areaEndX = -1;
                    areaEndY = -1;
                }
            }
            else
            {
                // 普通拖拽：替换当前选区。
                selectedAreaTiles.clear();
                for (const QPoint& tile : draggedTiles)
                    selectedAreaTiles.insert({tile.x(), tile.y()});
            }
        }
        update();
    }

    // 左键拾取拖拽完成：判断单点拾取 vs 范围拾取
    if (isPickDragging && event->button() == Qt::LeftButton)
    {
        isPickDragging = false;
        if (pickStartX >= 0 && pickStartY >= 0 && pickEndX >= 0 && pickEndY >= 0)
        {
            if (pickStartX == pickEndX && pickStartY == pickEndY)
            {
                // 单点拾取。即使本次点击会触发“取消选中”（isTileSelectionToggleCandidate），
                // 也必须先执行拾取：发出 tilePicked/tilePickedAllLayers 并更新画笔。
                // 注意：先拾取再 clear，避免 selectionCleared 在拾取后清掉画笔相关状态。
                bool wasToggleCandidate = isTileSelectionToggleCandidate;
                isTileSelectionToggleCandidate = false;

                if (paintAllLayers)
                {
                    // 全部图层模式：直接读取该格子完整三层普通 tile 数据，
                    // 保存为多层画笔，并发出 tilePickedAllLayers。
                    // 不再回退到最高非空单层，保持“全部图层”模式。
                    MapTileData sourceTile = mapEditor->getTile(pickEndX, pickEndY);
                    MapTileData brushTile;
                    brushTile.layer[0] = sourceTile.layer[0];
                    brushTile.layer[1] = sourceTile.layer[1];
                    brushTile.layer[2] = sourceTile.layer[2];
                    setMultiLayerPaintBrush(brushTile, true);
                    emit tilePickedAllLayers(brushTile, pickEndX, pickEndY);
                }
                else
                {
                    // 单图层模式：优先拾取当前绘制图层，仅当当前图层为空时，
                    // 才回退为从顶层向下查找第一个非空图层。
                    bool found = false;
                    if (paintLayer >= 0 && paintLayer <= 2)
                    {
                        MapTileLayerData layerData = mapEditor->getTileLayer(pickEndX, pickEndY, paintLayer);
                        if (layerData.mpc != 0)
                        {
                            emit tilePicked(layerData.mpc, layerData.frame, paintLayer, pickEndX, pickEndY);
                            found = true;
                        }
                    }
                    if (!found)
                    {
                        for (int layer = 2; layer >= 0; layer--)
                        {
                            MapTileLayerData layerData = mapEditor->getTileLayer(pickEndX, pickEndY, layer);
                            if (layerData.mpc != 0)
                            {
                                emit tilePicked(layerData.mpc, layerData.frame, layer, pickEndX, pickEndY);
                                found = true;
                                break;
                            }
                        }
                    }
                    if (!found)
                    {
                        // 空 tile：通知主窗口切换到"空图层"画笔
                        emit tilePicked(0, 0, paintLayer, pickEndX, pickEndY);
                    }
                }

                // 拾取完成后再处理“取消选中”行为；selectionCleared 仅清选中框，
                // 不应覆盖刚由 tilePicked/tilePickedAllLayers 同步的画笔。
                if (wasToggleCandidate)
                {
                    clearTileSelectionState();
                    emit selectionCleared();
                }
            }
            else
            {
                // 范围拾取：按拖拽开始时锁定的形状拷入剪贴板
                copyArea(pickStartX, pickStartY, pickEndX, pickEndY, currentDragShape);
            }
        }
        // 清除拾取范围预览
        pickStartX = -1;
        pickStartY = -1;
        pickEndX = -1;
        pickEndY = -1;
        isTileSelectionToggleCandidate = false;
        update();
    }

    isDragging = false;
    isRightDragging = false;
}

void MapRenderCanvas::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (!mapEditor || !mapEditor->isLoaded())
        return;

    if (event->button() != Qt::LeftButton)
        return;

    isDragging = false;
    isRightDragging = false;

    QPoint tilePos = screenToTile(event->pos());
    int tileX = tilePos.x();
    int tileY = tilePos.y();

    if (tileX < 0 || tileY < 0 || tileX >= mapEditor->getWidth() || tileY >= mapEditor->getHeight())
        return;

    int npcIndex = findEntityAtTile(tileX, tileY, true);
    if (npcIndex >= 0)
    {
        selectedEntityIndex = npcIndex;
        selectedEntityIsNpc = true;
        emit entityDoubleClicked(npcIndex, true);
        update();
        return;
    }

    int objIndex = findEntityAtTile(tileX, tileY, false);
    if (objIndex >= 0)
    {
        selectedEntityIndex = objIndex;
        selectedEntityIsNpc = false;
        emit entityDoubleClicked(objIndex, false);
        update();
        return;
    }
}

void MapRenderCanvas::wheelEvent(QWheelEvent* event)
{
    QPoint delta = event->angleDelta();
    if (delta.y() > 0)
        zoomAtPoint(event->position().toPoint(), zoomLevel * 1.25f);
    else if (delta.y() < 0)
        zoomAtPoint(event->position().toPoint(), zoomLevel / 1.25f);

    event->accept();
}

void MapRenderCanvas::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    update();
    emit viewportChanged();
}

void MapRenderCanvas::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    hoverTileX = -1;
    hoverTileY = -1;
    emit tileHovered(-1, -1);
    update();
}

void MapRenderCanvas::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
    {
        if (selectedEntityIndex >= 0)
        {
            emit entityDeleteRequested();
        }
    }
    else if (event->key() == Qt::Key_Escape)
    {
        // 无论是否在放置实体，都隐藏粘贴预览
        pastePreviewVisible = false;
        if (hasPlacingEntity)
        {
            clearPlacingEntity();
            setEditTool(MapEditTool::Select);
            update();
        }
        else
        {
            clearTileSelectionState();
            clearAreaSelectionState();
            clearPickSelectionState();
            emit selectionCleared();
            update();
        }
    }
    else if (event->modifiers() & Qt::ControlModifier)
    {
        if (event->key() == Qt::Key_C)
        {
            if (!selectedAreaTiles.empty())
            {
                // Ctrl+C 基于最终选区集合拷入剪贴板，支持非连续/挖空选区。
                copySelectedAreaTiles();
            }
        }
        else if (event->key() == Qt::Key_V)
        {
            if (hasClipboardData() && hoverTileX >= 0 && hoverTileY >= 0)
            {
                pastePreviewVisible = true;
                pasteArea(hoverTileX, hoverTileY);
            }
        }
        else if (event->key() == Qt::Key_D)
        {
            if (selectedEntityIndex >= 0)
            {
                emit entityDuplicateRequested();
            }
        }
        else if (event->key() == Qt::Key_A)
        {
            if (mapEditor && mapEditor->isLoaded())
            {
                if (editTool != MapEditTool::AreaSelect)
                    setEditTool(MapEditTool::AreaSelect);

                areaStartX = 0;
                areaStartY = 0;
                areaEndX = mapEditor->getWidth() - 1;
                areaEndY = mapEditor->getHeight() - 1;
                isAreaSelecting = false;
                isTileSelectionToggleCandidate = false;
                // 全选使用菱形（覆盖整张地图的对角范围）。
                currentDragShape = AreaSelectionShape::Diamond;
                completedAreaShape = AreaSelectionShape::Diamond;
                clearTileSelectionState();
                pastePreviewVisible = false;
                // 同步显式选区集合，使后续 copy/clear/fill 基于完整选区。
                selectedAreaTiles.clear();
                std::vector<QPoint> allTiles = enumerateAreaTiles(
                    areaStartX, areaStartY, areaEndX, areaEndY, completedAreaShape);
                for (const QPoint& tile : allTiles)
                    selectedAreaTiles.insert({tile.x(), tile.y()});
                emit selectAllRequested();
                update();
            }
        }
        else
        {
            QWidget::keyPressEvent(event);
        }
    }
    else
    {
        QWidget::keyPressEvent(event);
    }
}

void MapRenderCanvas::copyArea(int startX, int startY, int endX, int endY,
                                AreaSelectionShape shape)
{
    if (!mapEditor || !mapEditor->isLoaded())
        return;

    int mapWidth = mapEditor->getWidth();
    int mapHeight = mapEditor->getHeight();

    clipboardTiles.clear();
    clipboardWidth = 0;
    clipboardHeight = 0;
    clipboardMinCoordinateXOffset = 0;
    clipboardMaxCoordinateXOffset = 0;
    clipboardMinCoordinateYOffset = 0;
    clipboardMaxCoordinateYOffset = 0;
    clipboardShape = shape;

    AreaTileRange range = makeAreaTileRange(
        startX, startY, endX, endY, shape, mapWidth, mapHeight);
    std::vector<QPoint> selectedTiles = enumerateAreaTileRange(range, mapWidth, mapHeight);
    if (selectedTiles.empty())
        return;

    clipboardWidth = range.maxCoordinateX - range.minCoordinateX + 1;
    clipboardHeight = range.maxCoordinateY - range.minCoordinateY + 1;

    QPoint anchorCoordinate;
    if (shape == AreaSelectionShape::Diamond)
    {
        anchorCoordinate = QPoint(range.maxCoordinateX, range.maxCoordinateY);
    }
    else
    {
        const QPoint& topLeftTile = selectedTiles.front();
        anchorCoordinate = QPoint(tileToStaggeredX(topLeftTile.x(), topLeftTile.y()),
                                  topLeftTile.y());
    }

    clipboardMinCoordinateXOffset = range.minCoordinateX - anchorCoordinate.x();
    clipboardMaxCoordinateXOffset = range.maxCoordinateX - anchorCoordinate.x();
    clipboardMinCoordinateYOffset = range.minCoordinateY - anchorCoordinate.y();
    clipboardMaxCoordinateYOffset = range.maxCoordinateY - anchorCoordinate.y();

    for (const QPoint& tile : selectedTiles)
    {
        QPoint coordinate = shape == AreaSelectionShape::Diamond
            ? tileToAreaDiagonal(tile.x(), tile.y())
            : QPoint(tileToStaggeredX(tile.x(), tile.y()), tile.y());
        ClipboardTileData clipTile;
        clipTile.tileData = mapEditor->getTile(tile.x(), tile.y());
        clipTile.anchorOffsetCoordinateX = coordinate.x() - anchorCoordinate.x();
        clipTile.anchorOffsetCoordinateY = coordinate.y() - anchorCoordinate.y();
        clipboardTiles.push_back(clipTile);
    }

    // 新复制后重置粘贴预览可见性，确保后续 Ctrl+V 能显示预览
    pastePreviewVisible = true;

    emit areaCopied(clipboardWidth, clipboardHeight);
}

void MapRenderCanvas::copySelectedAreaTiles()
{
    if (!mapEditor || !mapEditor->isLoaded())
        return;
    if (selectedAreaTiles.empty())
        return;

    int mapWidth = mapEditor->getWidth();
    int mapHeight = mapEditor->getHeight();
    AreaSelectionShape shape = completedAreaShape;

    clipboardTiles.clear();
    clipboardWidth = 0;
    clipboardHeight = 0;
    clipboardMinCoordinateXOffset = 0;
    clipboardMaxCoordinateXOffset = 0;
    clipboardMinCoordinateYOffset = 0;
    clipboardMaxCoordinateYOffset = 0;
    clipboardShape = shape;

    // 计算所有选中 tile 在对应坐标系下的坐标范围，确定锚点。
    int minCoordX = std::numeric_limits<int>::max();
    int minCoordY = std::numeric_limits<int>::max();
    int maxCoordX = std::numeric_limits<int>::min();
    int maxCoordY = std::numeric_limits<int>::min();

    for (const auto& tile : selectedAreaTiles)
    {
        QPoint coordinate = (shape == AreaSelectionShape::Diamond)
            ? tileToAreaDiagonal(tile.first, tile.second)
            : QPoint(tileToStaggeredX(tile.first, tile.second), tile.second);
        minCoordX = std::min(minCoordX, coordinate.x());
        maxCoordX = std::max(maxCoordX, coordinate.x());
        minCoordY = std::min(minCoordY, coordinate.y());
        maxCoordY = std::max(maxCoordY, coordinate.y());
    }

    clipboardWidth = maxCoordX - minCoordX + 1;
    clipboardHeight = maxCoordY - minCoordY + 1;

    // 锚点：菱形用右下角（maxCoordX, maxCoordY），矩形用左上角（minCoordX, minCoordY），
    // 与 copyArea 保持一致。
    QPoint anchorCoordinate;
    if (shape == AreaSelectionShape::Diamond)
    {
        anchorCoordinate = QPoint(maxCoordX, maxCoordY);
    }
    else
    {
        anchorCoordinate = QPoint(minCoordX, minCoordY);
    }

    clipboardMinCoordinateXOffset = minCoordX - anchorCoordinate.x();
    clipboardMaxCoordinateXOffset = maxCoordX - anchorCoordinate.x();
    clipboardMinCoordinateYOffset = minCoordY - anchorCoordinate.y();
    clipboardMaxCoordinateYOffset = maxCoordY - anchorCoordinate.y();

    for (const auto& tile : selectedAreaTiles)
    {
        QPoint coordinate = (shape == AreaSelectionShape::Diamond)
            ? tileToAreaDiagonal(tile.first, tile.second)
            : QPoint(tileToStaggeredX(tile.first, tile.second), tile.second);
        ClipboardTileData clipTile;
        clipTile.tileData = mapEditor->getTile(tile.first, tile.second);
        clipTile.anchorOffsetCoordinateX = coordinate.x() - anchorCoordinate.x();
        clipTile.anchorOffsetCoordinateY = coordinate.y() - anchorCoordinate.y();
        clipboardTiles.push_back(clipTile);
    }

    pastePreviewVisible = true;
    emit areaCopied(clipboardWidth, clipboardHeight);
}

void MapRenderCanvas::pasteArea(int targetX, int targetY)
{
    if (!mapEditor || !mapEditor->isLoaded() || clipboardTiles.empty())
        return;

    int mapWidth = mapEditor->getWidth();
    int mapHeight = mapEditor->getHeight();

    std::map<std::pair<int,int>, MapTileData> oldTiles;
    std::map<std::pair<int,int>, MapTileData> newTiles;

    // 图层过滤：以粘贴时的左侧"绘制设置"为准。全部图层 -> {0,1,2}；
    // 单图层 -> {paintLayer}。障碍/陷阱不属于普通绘制图层，永远不被区域粘贴覆盖。
    std::vector<int> layers = getPasteLayers();

    auto targets = computePasteTargets(targetX, targetY);
    for (const auto& target : targets)
    {
        int destX = target.destX;
        int destY = target.destY;
        if (destX < 0 || destX >= mapWidth || destY < 0 || destY >= mapHeight)
            continue;
        if (!target.clipTile)
            continue;

        auto key = std::make_pair(destX, destY);
        if (oldTiles.count(key) == 0)
            oldTiles[key] = mapEditor->getTile(destX, destY);

        // 只覆盖当前绘制设置指定的图层，其他图层保持原值。
        MapTileData merged = mapEditor->getTile(destX, destY);
        for (int layer : layers)
            merged.layer[layer] = target.clipTile->tileData.layer[layer];
        mapEditor->setTile(destX, destY, merged);

        newTiles[key] = mapEditor->getTile(destX, destY);
    }

    pasteOldTiles = oldTiles;
    pasteNewTiles = newTiles;

    emit areaPasted(targetX, targetY, clipboardWidth, clipboardHeight);
    emit tileEdited(targetX, targetY);
    update();
}

bool MapRenderCanvas::hasClipboardData() const
{
    return !clipboardTiles.empty();
}

void MapRenderCanvas::clearClipboard()
{
    clipboardTiles.clear();
    clipboardWidth = 0;
    clipboardHeight = 0;
    clipboardMinCoordinateXOffset = 0;
    clipboardMaxCoordinateXOffset = 0;
    clipboardMinCoordinateYOffset = 0;
    clipboardMaxCoordinateYOffset = 0;
    clipboardShape = AreaSelectionShape::Diamond;
}

int MapRenderCanvas::getAreaStartX() const { return areaStartX; }
int MapRenderCanvas::getAreaStartY() const { return areaStartY; }
int MapRenderCanvas::getAreaEndX() const { return areaEndX; }
int MapRenderCanvas::getAreaEndY() const { return areaEndY; }

void MapRenderCanvas::setRectangularAreaSelect(bool enabled)
{
    preferredAreaShape = enabled ? AreaSelectionShape::Rectangle : AreaSelectionShape::Diamond;
    // 不改变 currentDragShape / completedAreaShape / clipboardShape。
}

bool MapRenderCanvas::isRectangularAreaSelect() const
{
    return preferredAreaShape == AreaSelectionShape::Rectangle;
}

AreaSelectionShape MapRenderCanvas::getPreferredAreaShape() const { return preferredAreaShape; }
AreaSelectionShape MapRenderCanvas::getCurrentDragShape() const { return currentDragShape; }
AreaSelectionShape MapRenderCanvas::getCompletedAreaShape() const { return completedAreaShape; }
AreaSelectionShape MapRenderCanvas::getClipboardShape() const { return clipboardShape; }

const std::map<std::pair<int,int>, MapTileData>& MapRenderCanvas::getPasteOldTiles() const { return pasteOldTiles; }
const std::map<std::pair<int,int>, MapTileData>& MapRenderCanvas::getPasteNewTiles() const { return pasteNewTiles; }

std::vector<int> MapRenderCanvas::getPasteLayers() const
{
    if (paintAllLayers)
        return {0, 1, 2};
    return {paintLayer};
}

std::vector<MapRenderCanvas::PasteTargetTile> MapRenderCanvas::computePasteTargets(int targetX, int targetY) const
{
    std::vector<PasteTargetTile> targets;
    if (!mapEditor || !mapEditor->isLoaded() || clipboardTiles.empty())
        return targets;

    QPoint targetAnchorCoordinate = clipboardShape == AreaSelectionShape::Diamond
        ? tileToAreaDiagonal(targetX, targetY)
        : QPoint(tileToStaggeredX(targetX, targetY), targetY);

    AreaTileRange targetRange;
    targetRange.shape = clipboardShape;
    targetRange.minCoordinateX = targetAnchorCoordinate.x() + clipboardMinCoordinateXOffset;
    targetRange.maxCoordinateX = targetAnchorCoordinate.x() + clipboardMaxCoordinateXOffset;
    targetRange.minCoordinateY = targetAnchorCoordinate.y() + clipboardMinCoordinateYOffset;
    targetRange.maxCoordinateY = targetAnchorCoordinate.y() + clipboardMaxCoordinateYOffset;

    std::map<std::pair<int, int>, const ClipboardTileData*> clipboardByOffset;
    for (const ClipboardTileData& clipTile : clipboardTiles)
    {
        clipboardByOffset[{clipTile.anchorOffsetCoordinateX,
                           clipTile.anchorOffsetCoordinateY}] = &clipTile;
    }

    std::vector<QPoint> destinationTiles = enumerateAreaTileRange(
        targetRange, mapEditor->getWidth(), mapEditor->getHeight());
    for (const QPoint& destinationTile : destinationTiles)
    {
        QPoint destinationCoordinate = clipboardShape == AreaSelectionShape::Diamond
            ? tileToAreaDiagonal(destinationTile.x(), destinationTile.y())
            : QPoint(tileToStaggeredX(destinationTile.x(), destinationTile.y()),
                     destinationTile.y());
        std::pair<int, int> offset = {
            destinationCoordinate.x() - targetAnchorCoordinate.x(),
            destinationCoordinate.y() - targetAnchorCoordinate.y()
        };
        auto clipboardIterator = clipboardByOffset.find(offset);
        if (clipboardIterator == clipboardByOffset.end())
            continue;

        PasteTargetTile target;
        target.destX = destinationTile.x();
        target.destY = destinationTile.y();
        target.clipTile = clipboardIterator->second;
        targets.push_back(target);
    }

    return targets;
}

void MapRenderCanvas::drawTileLayerPreview(QPainter& painter, int tileX, int tileY,
                                            const MapTileLayerData& layerData, int alpha)
{
    if (layerData.mpc == 0)
        return;

    QImage frameImage = getMpcFrameImage(layerData.mpc, layerData.frame);
    if (frameImage.isNull())
        return;

    QPoint screenPos = tileToScreen(tileX, tileY);
    int scaledWidth = (int)(frameImage.width() * zoomLevel);
    int scaledHeight = (int)(frameImage.height() * zoomLevel);
    int frameOffsetX = frameImage.width() / 2;
    int frameOffsetY = frameImage.height();
    getMpcFrameOffset(layerData.mpc, layerData.frame, frameOffsetX, frameOffsetY);
    int drawX = screenPos.x() + (int)(TILE_WIDTH * zoomLevel / 2) -
        (int)std::lround(frameOffsetX * zoomLevel);
    int drawY = screenPos.y() + (int)(TILE_HEIGHT * zoomLevel) -
        (int)std::lround(frameOffsetY * zoomLevel);

    QRect targetRect(drawX, drawY, scaledWidth, scaledHeight);
    painter.setOpacity(alpha / 255.0);
    painter.drawImage(targetRect, frameImage);
    painter.setOpacity(1.0);
}

void MapRenderCanvas::drawAreaShapeTiles(QPainter& painter, int startX, int startY, int endX, int endY,
                                          AreaSelectionShape shape)
{
    if (!mapEditor || !mapEditor->isLoaded())
        return;

    float halfW = TILE_WIDTH * zoomLevel * 0.5f;
    float halfH = TILE_HEIGHT * zoomLevel * 0.5f;

    std::vector<QPoint> tiles = enumerateAreaTiles(startX, startY, endX, endY, shape);
    for (const QPoint& tile : tiles)
    {
        QPoint center = tileToScreenCenter(tile.x(), tile.y());
        drawDiamond(painter, center, halfW, halfH);
    }
}

void MapRenderCanvas::drawAreaSelection(QPainter& painter)
{
    // 拖拽过程中：绘制当前拖拽预览（基于 areaStart/End + currentDragShape），
    // 以及已有 selectedAreaTiles（用于 Ctrl 添加 / Alt 删除时可见已有选区）。
    // 非拖拽：仅绘制 selectedAreaTiles。
    float halfW = TILE_WIDTH * zoomLevel * 0.5f;
    float halfH = TILE_HEIGHT * zoomLevel * 0.5f;

    if (isAreaSelecting)
    {
        // 拖拽预览：青色虚线框
        QPen dragPen(QColor(0, 200, 255, 200), 2, Qt::DashLine);
        painter.setPen(dragPen);
        painter.setBrush(QColor(0, 200, 255, 30));
        drawAreaShapeTiles(painter, areaStartX, areaStartY, areaEndX, areaEndY, currentDragShape);
    }

    if (!selectedAreaTiles.empty())
    {
        // 已完成选区：青色实线框
        QPen pen(QColor(0, 200, 255, 200), 2, Qt::SolidLine);
        painter.setPen(pen);
        painter.setBrush(QColor(0, 200, 255, 30));
        for (const auto& tile : selectedAreaTiles)
        {
            QPoint center = tileToScreenCenter(tile.first, tile.second);
            drawDiamond(painter, center, halfW, halfH);
        }
    }
}

void MapRenderCanvas::drawPickSelection(QPainter& painter)
{
    if (pickStartX < 0 || pickStartY < 0 || pickEndX < 0 || pickEndY < 0)
        return;

    // 拾取范围预览：绿色虚线框（区别于区域选择的青色）
    QPen pen(QColor(0, 255, 100, 200), 2, Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(QColor(0, 255, 100, 30));

    drawAreaShapeTiles(painter, pickStartX, pickStartY, pickEndX, pickEndY, currentDragShape);
}

void MapRenderCanvas::drawPastePreview(QPainter& painter)
{
    if (!mapEditor || !mapEditor->isLoaded() || clipboardTiles.empty())
        return;
    if (hoverTileX < 0 || hoverTileY < 0)
        return;

    int mapWidth = mapEditor->getWidth();
    int mapHeight = mapEditor->getHeight();

    // 1. 绘制半透明的实际 Tile 图像（只预览当前绘制设置会粘贴的图层）。
    //    预览坐标、图层过滤和最终 pasteArea 共用 computePasteTargets / getPasteLayers，
    //    保证所见即所得。
    std::vector<int> layers = getPasteLayers();
    auto targets = computePasteTargets(hoverTileX, hoverTileY);
    // 正常地图按完整图层绘制，而不是按单个 Tile 依次绘制 0/1/2 层。
    // 图层作为外层循环，避免后一个 Tile 的地面层盖住前一个 Tile 的空中层。
    for (int layer : layers)
    {
        for (const auto& target : targets)
        {
            int destX = target.destX;
            int destY = target.destY;
            if (destX < 0 || destX >= mapWidth || destY < 0 || destY >= mapHeight)
                continue;
            if (!target.clipTile)
                continue;

            drawTileLayerPreview(painter, destX, destY,
                                 target.clipTile->tileData.layer[layer], 160);
        }
    }

    // 2. 保留细轮廓辅助定位（不代替实际内容）。
    {
        QPen pen(QColor(0, 255, 100, 180), 2, Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(QColor(0, 255, 100, 25));
        float halfW = TILE_WIDTH * zoomLevel * 0.5f;
        float halfH = TILE_HEIGHT * zoomLevel * 0.5f;

        for (const auto& target : targets)
        {
            int destX = target.destX;
            int destY = target.destY;
            if (destX < 0 || destX >= mapWidth || destY < 0 || destY >= mapHeight)
                continue;
            QPoint center = tileToScreenCenter(destX, destY);
            drawDiamond(painter, center, halfW, halfH);
        }
    }
}

void MapRenderCanvas::drawBrushPreview(QPainter& painter)
{
    // 仅在支持单格右键绘制的瓦片工具下显示。与 mousePressEvent 右键单格绘制分支
    // 保持一致的工具集合：Select/TilePaint/TilePicker。
    // AreaSelect 右键不绘制也不粘贴，不显示画笔预览。
    if (editTool != MapEditTool::Select && editTool != MapEditTool::TilePaint &&
        editTool != MapEditTool::TilePicker)
        return;

    // 避免与其它预览/拖拽冲突：实体放置、区域选择拖拽、左键拾取拖拽、
    // 剪贴板粘贴预览可见时不显示普通画笔预览。
    if (hasPlacingEntity)
        return;
    if (isAreaSelecting)
        return;
    if (isPickDragging)
        return;
    if (!isAreaSelecting && pastePreviewVisible && hasClipboardData())
        return;

    if (hoverTileX < 0 || hoverTileY < 0)
        return;

    // 复用 drawTileLayerPreview 的锚点/缩放/透明度规则，与粘贴预览保持一致。
    const int previewAlpha = 160;

    if (paintAllLayers)
    {
        if (hasMultiLayerPaintBrushState)
        {
            // 全部图层 + 多层拾取画笔：按 0/1/2 顺序预览三层各自图像，空层跳过。
            // 顺序与 drawMapLayer 一致（地面→建筑→空中），空中层画在最上。
            for (int layer = 0; layer < 3; layer++)
                drawTileLayerPreview(painter, hoverTileX, hoverTileY,
                                     multiLayerPaintBrush.layer[layer], previewAlpha);
        }
        else
        {
            // 全部图层 + 标量画笔：只画一次当前 mpc/frame，避免叠画三层导致过暗。
            MapTileLayerData layerData;
            layerData.frame = (uint8_t)paintFrameIndex;
            layerData.mpc = (uint8_t)paintMpcIndex;
            drawTileLayerPreview(painter, hoverTileX, hoverTileY, layerData, previewAlpha);
        }
    }
    else
    {
        // 单图层：预览当前 paintMpcIndex/paintFrameIndex。
        MapTileLayerData layerData;
        layerData.frame = (uint8_t)paintFrameIndex;
        layerData.mpc = (uint8_t)paintMpcIndex;
        drawTileLayerPreview(painter, hoverTileX, hoverTileY, layerData, previewAlpha);
    }
}
