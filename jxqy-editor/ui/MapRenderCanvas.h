#pragma once

#include <QWidget>
#include <QElapsedTimer>
#include <QCache>
#include <QImage>
#include <QPoint>
#include <QTimer>
#include <cstdint>
#include <map>
#include <set>
#include <tuple>
#include <vector>
#include "../core/MapFileEditor.h"
#include "../core/MpcImageCache.h"
#include "../core/INIFileEditor.h"

enum class MapEditTool
{
    Select,
    TilePaint,
    ObstaclePaint,
    TrapPaint,
    NpcPlace,
    ObjectPlace,
    TilePicker,
    AreaSelect,
    Pan
};

/// 区域选择的形状。菱形沿用 UPEdit 风格的等距投影选择；矩形使用
/// staggeredX = 2 * tileX + (tileY & 1) 的视觉坐标范围，不会为了每行
/// Tile 数一致而向鼠标起止 Tile 的视觉包围范围外扩展。
enum class AreaSelectionShape
{
    Diamond,
    Rectangle
};

struct MapEntityData
{
    std::string name;
    std::string iniFile;
    int mapX = 0;
    int mapY = 0;
    int direction = 0;
    int kind = 0;
    std::string scriptFile;
    bool isNpc = true;
    int action = 0;
    int relation = 0;
    int lum = 0;
    int offsetX = 0;
    int offsetY = 0;
    int frame = 0;
    int state = 0;
    int walkSpeed = 1;
    int pathFinder = 0;
    int dialogRadius = 1;
    int life = 0;
    int lifeMax = 0;
    std::string wavFile;
    int damage = 0;
    std::int64_t actionTime = 0;
    int height = 0;
    std::string scriptFileRight;
    int scriptFileJustTouch = 0;
    int canInteractDirectly = 0;
    std::string timerScriptFile;
    int timerScriptInterval = 1000;
    std::string reviveNpcIni;
    int millisecondsToRemove = 0;
    int standSpeed = 0;
    int thew = 0;
    int thewMax = 0;
    int mana = 0;
    int manaMax = 0;
    int attack = 0;
    int defend = 0;
    int evade = 0;
    int duck = 0;
    int exp = 0;
    int levelUpExp = 0;
    int level = 0;
    int attackLevel = 0;
    int magicLevel = 0;
    int visionRadius = 0;
    int attackRadius = 0;
    std::string bodyIni;
    std::string flyIni;
    std::string flyIni2;
    std::string flyInis;
    std::string magicIni;
    std::string deathScript;
    int idle = 0;
    std::string fixedPosition;
    int aiType = 0;
    int attack2 = 0;
    int attack3 = 0;
    int defend2 = 0;
    int defend3 = 0;
    int expBonus = 0;
    int invincible = 0;
    int group = 0;
    std::string dropIni;
    int noDropWhenDie = 0;
    int reviveMilliseconds = 0;
    std::string visibleVariableName;
    int visibleVariableValue = 0;
    std::map<std::string, std::string> originalProperties;

    bool operator==(const MapEntityData& other) const
    {
        return std::tie(name, iniFile, mapX, mapY, direction, kind, scriptFile,
                   isNpc, action, relation, lum, offsetX, offsetY, frame, state,
                   walkSpeed, pathFinder, dialogRadius, life, lifeMax, wavFile,
                   damage, actionTime, height, scriptFileRight,
                   scriptFileJustTouch, canInteractDirectly, timerScriptFile,
                   timerScriptInterval, reviveNpcIni, millisecondsToRemove,
                   standSpeed, thew, thewMax, mana, manaMax, attack, defend,
                   evade, duck, exp, levelUpExp, level, attackLevel, magicLevel,
                   visionRadius, attackRadius, bodyIni, flyIni, flyIni2, flyInis,
                   magicIni, deathScript, idle, fixedPosition, aiType, attack2,
                   attack3, defend2, defend3, expBonus, invincible, group,
                   dropIni, noDropWhenDie, reviveMilliseconds,
                   visibleVariableName, visibleVariableValue, originalProperties) ==
            std::tie(other.name, other.iniFile, other.mapX, other.mapY,
                   other.direction, other.kind, other.scriptFile, other.isNpc,
                   other.action, other.relation, other.lum, other.offsetX,
                   other.offsetY, other.frame, other.state, other.walkSpeed,
                   other.pathFinder, other.dialogRadius, other.life,
                   other.lifeMax, other.wavFile, other.damage, other.actionTime,
                   other.height, other.scriptFileRight,
                   other.scriptFileJustTouch, other.canInteractDirectly,
                   other.timerScriptFile, other.timerScriptInterval,
                   other.reviveNpcIni, other.millisecondsToRemove,
                   other.standSpeed, other.thew, other.thewMax, other.mana,
                   other.manaMax, other.attack, other.defend, other.evade,
                   other.duck, other.exp, other.levelUpExp, other.level,
                   other.attackLevel, other.magicLevel, other.visionRadius,
                   other.attackRadius, other.bodyIni, other.flyIni,
                   other.flyIni2, other.flyInis, other.magicIni,
                   other.deathScript, other.idle, other.fixedPosition,
                   other.aiType, other.attack2, other.attack3, other.defend2,
                   other.defend3, other.expBonus, other.invincible, other.group,
                   other.dropIni, other.noDropWhenDie,
                   other.reviveMilliseconds, other.visibleVariableName,
                   other.visibleVariableValue, other.originalProperties);
    }

    bool operator!=(const MapEntityData& other) const
    {
        return !(*this == other);
    }
};

class MapRenderCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit MapRenderCanvas(QWidget* parent = nullptr);
    ~MapRenderCanvas();

    void setMapFileEditor(MapFileEditor* editor);
    void setMpcImageCache(MpcImageCache* cache);

    /// Transactional list loads: on failure the currently displayed list,
    /// source INI metadata and selection remain unchanged.
    bool loadNpcList(const std::string& iniFileName);
    bool loadObjectList(const std::string& iniFileName);
    /// Serialize the current in-memory entity lists without touching disk or
    /// changing the source INI baselines.
    std::string serializeNpcList() const;
    std::string serializeObjectList() const;
    bool saveNpcList(const std::string& iniFileName) const;
    bool saveObjectList(const std::string& iniFileName) const;
    void clearEntities();

    void setEditTool(MapEditTool tool);
    MapEditTool getEditTool() const;

    void setPaintLayer(int layer);
    int getPaintLayer() const;

    void setPaintAllLayers(bool all);
    bool isPaintAllLayers() const;

    /// 设置多层画笔。仅保存三层数据，右键绘制时按图层分别写回。
    /// hasMultiLayer 设为 false 可清除多层画笔状态。
    void setMultiLayerPaintBrush(const MapTileData& tileData, bool hasMultiLayer);
    bool hasMultiLayerPaintBrush() const;
    const MapTileData& getMultiLayerPaintBrush() const;

    void setPaintMpcIndex(int mpcIndex);
    int getPaintMpcIndex() const;

    void setPaintFrameIndex(int frameIndex);
    int getPaintFrameIndex() const;

    void setPaintObstacle(uint8_t obstacle);
    uint8_t getPaintObstacle() const;

    void setPaintTrapIndex(uint8_t trapIndex);
    uint8_t getPaintTrapIndex() const;

    void setLayerVisible(int layer, bool visible);
    bool isLayerVisible(int layer) const;

    void setObstacleVisible(bool visible);
    bool isObstacleVisible() const;

    void setTrapVisible(bool visible);
    bool isTrapVisible() const;

    void setNpcVisible(bool visible);
    bool isNpcVisible() const;

    void setObjectVisible(bool visible);
    bool isObjectVisible() const;

    void setGridVisible(bool visible);
    bool isGridVisible() const;

    void setCoordinateVisible(bool visible);
    bool isCoordinateVisible() const;

    void zoomIn();
    void zoomOut();
    void resetZoom();
    void zoomToFit();
    float getZoomLevel() const;
    QPoint viewportScrollOffset() const;
    bool restoreViewport(float zoomLevel, const QPoint& scrollOffset);

    void zoomAtPoint(const QPoint& pos, float newZoom);

    void centerOnTile(int tileX, int tileY);
    void centerOnPixel(int pixelX, int pixelY);

    QPoint screenToTile(const QPoint& screenPos) const;
    QPoint tileToScreen(int tileX, int tileY) const;
    QPoint tileToScreenCenter(int tileX, int tileY) const;

    /// 屏幕坐标 -> 世界坐标（未缩放/未滚动的小数坐标），供小地图投影视口框复用。
    QPointF screenToWorld(const QPoint& screenPos) const;

    /// 是否有等待放置的 NPC 实体 / 物体实体（NPC/Object 工具复用）。
    bool isPlacingNpc() const;
    bool isPlacingObject() const;

    int getSelectedTileX() const;
    int getSelectedTileY() const;

    int getHoverTileX() const;
    int getHoverTileY() const;

    const std::vector<MapEntityData>& getNpcList() const;
    const std::vector<MapEntityData>& getObjectList() const;
    std::vector<MapEntityData>& getNpcListRef();
    std::vector<MapEntityData>& getObjectListRef();

    MapEntityData* getSelectedEntity();
    int getSelectedEntityIndex() const;
    bool isSelectedEntityNpc() const;

    void setPlacingEntity(const MapEntityData& entity);
    void clearPlacingEntity();

    void deleteNpc(int index);
    void deleteObject(int index);
    void clearSelection();
    void clearEntityResImageCache();

    int findEntityAtTile(int tileX, int tileY, bool isNpc) const;

    void setContinuousPlace(bool enabled);
    bool isContinuousPlace() const;

    void selectEntity(int index, bool isNpc);

    QImage generateThumbnail(int maxWidth = 320, int maxHeight = 240) const;

    void copyArea(int startX, int startY, int endX, int endY,
                  AreaSelectionShape shape = AreaSelectionShape::Diamond);
    void pasteArea(int targetX, int targetY);
    bool hasClipboardData() const;
    void clearClipboard();

    int getAreaStartX() const;
    int getAreaStartY() const;
    int getAreaEndX() const;
    int getAreaEndY() const;

    /// 设置下一次新选择的首选形状（由左侧 checkbox 驱动）。
    /// 只影响后续开始的新选择，不改变当前已完成区域、正在拖拽的选择或剪贴板内容。
    void setRectangularAreaSelect(bool enabled);
    bool isRectangularAreaSelect() const;

    AreaSelectionShape getPreferredAreaShape() const;
    AreaSelectionShape getCurrentDragShape() const;
    AreaSelectionShape getCompletedAreaShape() const;
    AreaSelectionShape getClipboardShape() const;

    // 左键范围拾取状态
    bool isPickDraggingActive() const { return isPickDragging; }
    int getPickStartX() const { return pickStartX; }
    int getPickStartY() const { return pickStartY; }
    int getPickEndX() const { return pickEndX; }
    int getPickEndY() const { return pickEndY; }

    const std::map<std::pair<int,int>, MapTileData>& getPasteOldTiles() const;
    const std::map<std::pair<int,int>, MapTileData>& getPasteNewTiles() const;

    void setAreaSelection(int startX, int startY, int endX, int endY,
                          AreaSelectionShape shape = AreaSelectionShape::Diamond);

    /// 枚举指定区域中的实际 Tile。选择预览、复制、清除和粘贴目标均复用此规则。
    std::vector<QPoint> enumerateAreaTiles(int startX, int startY, int endX, int endY,
                                          AreaSelectionShape shape) const;
    std::vector<QPoint> getSelectedAreaTiles() const;

    /// 返回当前区域选区的 tile 集合（支持 Ctrl 添加 / Alt 删除后的非连续选区）。
    /// 没有 area 选区时返回空。
    const std::set<std::pair<int,int>>& getSelectedAreaTileSet() const
    {
        return selectedAreaTiles;
    }

    /// 把当前 selectedAreaTiles 拷入剪贴板。供 Ctrl+C / 菜单复制基于最终选区集合调用，
    /// 不再仅依赖 areaStart/areaEnd 包围盒。
    void copySelectedAreaTiles();

    /// 失效边缘渲染范围和预缩放帧缓存。在 MPC 表/资源替换后调用，确保下次
    /// paintEvent 重新扫描已用 MPC 的最大帧尺寸并重建对应缩放图像。
    void invalidateRenderRangeCache()
    {
        renderRangeCacheValid = false;
        renderRangeCacheKey.clear();
        scaledMpcFrameCache.clear();
    }

    /// 剪贴板瓦片数据。保存源瓦片完整数据，以及相对锚点的偏移。
    /// 菱形使用对角坐标，矩形使用 staggered 视觉坐标；两种形状都记录
    /// 相对锚点偏移，供粘贴目标复用统一选区枚举器。
    struct ClipboardTileData
    {
        MapTileData tileData;
        // 统一选区坐标相对锚点的偏移。菱形使用对角坐标，矩形使用
        // (staggeredX, tileY) 坐标。
        int anchorOffsetCoordinateX = 0;
        int anchorOffsetCoordinateY = 0;
    };

    /// 计算粘贴目标瓦片列表。预览和实际粘贴共用此函数，确保所见即所得。
    /// 菱形用对角坐标下方角作为鼠标锚点；矩形用最上行最左实际 Tile
    /// 作为鼠标锚点。
    struct PasteTargetTile
    {
        int destX = 0;
        int destY = 0;
        const ClipboardTileData* clipTile = nullptr;
    };
    std::vector<PasteTargetTile> computePasteTargets(int targetX, int targetY) const;

    /// 返回当前绘制设置下需要粘贴的图层索引列表。
    /// 全部图层模式返回 {0,1,2}；单图层模式返回 {paintLayer}。
    std::vector<int> getPasteLayers() const;

signals:
    void tileClicked(int tileX, int tileY, Qt::MouseButton button);
    void tileHovered(int tileX, int tileY);
    /// 编辑工具发生变化（含放置完成后自动切回选择工具），主窗口据此同步 QAction 选中态与状态栏。
    void editToolChanged(MapEditTool tool);
    void entitySelected(int index, bool isNpc);
    void entityDoubleClicked(int index, bool isNpc);
    void entityMoved(int index, bool isNpc, int newTileX, int newTileY);
    void entityListChanged();
    void tileEdited(int tileX, int tileY);
    void zoomChanged(float zoomLevel);
    /// Emitted when the viewport changes (scroll/zoom/resize), for minimap to update.
    void viewportChanged();
    void tileAboutToBeEdited(int tileX, int tileY);
    void entityPlaced(int index, bool isNpc);
    void entityMoveStarted(int index, bool isNpc, int oldMapX, int oldMapY);
    void entityDeleteRequested();
    void contextMenuRequested(const QPoint& screenPos, int tileX, int tileY);
    void tilePicked(int mpcIndex, int frameIndex, int layer, int tileX, int tileY);
    /// 全部图层模式下左键拾取：携带源瓦片完整三层普通 tile 数据
    /// （layer[0..2]，不含障碍/陷阱）。主窗口据此同步多层画笔，且不切换图层下拉框。
    void tilePickedAllLayers(const MapTileData& tileData, int tileX, int tileY);
    /// 障碍工具左键拾取：从地图读取障碍值，不修改地图。主窗口据此同步障碍下拉框。
    void obstaclePicked(uint8_t obstacle, int tileX, int tileY);
    /// 陷阱工具左键拾取：从地图读取陷阱索引，不修改地图。主窗口据此同步陷阱输入框。
    void trapPicked(uint8_t trapIndex, int tileX, int tileY);
    void areaCopied(int width, int height);
    void areaPasted(int tileX, int tileY, int width, int height);
    void entityDuplicateRequested();
    void selectAllRequested();
    /// 清除选中瓦片或区域时发射，主窗口据此同步 selectedInfoTileX/Y。
    void selectionCleared();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void drawMapLayer(QPainter& painter, int layer);
    void drawMapLayerAndEntities(QPainter& painter);
    void drawObstacleOverlay(QPainter& painter);
    void drawTrapOverlay(QPainter& painter);
    void drawEntities(QPainter& painter);
    void drawEntity(QPainter& painter, const MapEntityData& entity, bool isSelected);
    void drawPlacingEntity(QPainter& painter);
    void drawGrid(QPainter& painter);
    void drawSelection(QPainter& painter);
    void drawHoverHighlight(QPainter& painter);
    void drawAreaSelection(QPainter& painter);
    void drawPickSelection(QPainter& painter);
    void drawPastePreview(QPainter& painter);
    /// 鼠标悬停时的单格画笔图像预览。仅在支持单格右键绘制的瓦片工具下，
    /// 且鼠标位于有效瓦片上、未与粘贴/拾取拖拽/实体放置冲突时显示。
    /// 单图层：预览当前 paintMpcIndex/paintFrameIndex；
    /// 全部图层+多层画笔：按 0/1/2 顺序预览三层；全部图层+标量画笔：只画一次。
    void drawBrushPreview(QPainter& painter);
    void drawCoordinateOverlay(QPainter& painter);

    void drawDiamond(QPainter& painter, const QPoint& center, float halfW, float halfH);

    void drawAreaShapeTiles(QPainter& painter, int startX, int startY, int endX, int endY,
                            AreaSelectionShape shape);
    bool hasAreaSelection() const;
    void clearAreaSelectionState();
    void clearPickSelectionState();
    void clearTileSelectionState();

    /// 绘制单个瓦片的指定图层图像（供粘贴预览复用正常地图绘制逻辑）。
    void drawTileLayerPreview(QPainter& painter, int tileX, int tileY,
                              const MapTileLayerData& layerData, int alpha);

    struct ScaledMpcFrameVisual
    {
        QImage image;
        int frameOffsetX = 0;
        int frameOffsetY = 0;
    };

    QImage getMpcFrameImage(int mpcIndex, int frameIndex) const;
    ScaledMpcFrameVisual getScaledMpcFrameVisual(
        int mpcIndex, int frameIndex);
    int resolveMpcFrameIndex(int mpcIndex, int storedFrameIndex) const;
    bool getMpcFrameOffset(int mpcIndex, int storedFrameIndex,
                           int& offsetX, int& offsetY) const;

    void getTileWorldPosition(int tileX, int tileY, int& worldX, int& worldY) const;

    void getVisibleTileRange(int& startX, int& startY, int& endX, int& endY) const;

    /// 渲染专用可见瓦片范围。MPC 帧图像常高于 64x32 tile，可从 tile 锚点向上/左右
    /// 大幅扩展；getVisibleTileRange 的固定 margin=3 只覆盖 tile 菱形可见性，会漏掉
    /// 屏幕顶部/底部边缘应伸入视口的高图 tile。本函数按当前地图已用 MPC 的最大帧
    /// 宽高推导更宽的 margin，供 drawMapLayer / drawMapLayerAndEntities 使用。
    /// 网格/坐标/选择框仍用 getVisibleTileRange，避免文字画太多。
    void getVisibleTileRenderRange(int& startX, int& startY, int& endX, int& endY) const;

    void requestAnimationRefresh() const;
    void updateAnimationTimer();
    void updateAnimation();

    MapFileEditor* mapEditor = nullptr;
    MpcImageCache* mpcCache = nullptr;
    QCache<quint64, ScaledMpcFrameVisual> scaledMpcFrameCache;

    MapEditTool editTool = MapEditTool::Select;
    int paintLayer = 0;
    bool paintAllLayers = false;
    int paintMpcIndex = 0;
    int paintFrameIndex = 0;
    uint8_t paintObstacle = 0;
    uint8_t paintTrapIndex = 0;

    // 全部图层模式下从地图拾取得到的多层画笔：保存源瓦片三层普通 tile 数据。
    // hasMultiLayerPaintBrush 为真时，右键在全部图层模式下按层分别写回，
    // 而不是把同一个 mpc/frame 写入三层。
    bool hasMultiLayerPaintBrushState = false;
    MapTileData multiLayerPaintBrush;

    bool layerVisible[3] = {true, true, true};
    bool obstacleVisible = true;
    bool trapVisible = true;
    bool npcVisible = true;
    bool objectVisible = true;
    bool gridVisible = true;
    bool coordinateVisible = true;

    float zoomLevel = 1.0f;
    int scrollX = 0;
    int scrollY = 0;

    int hoverTileX = -1;
    int hoverTileY = -1;
    int selectedTileX = -1;
    int selectedTileY = -1;

    std::vector<MapEntityData> npcList;
    std::vector<MapEntityData> objectList;
    INIFileEditor originalNpcIni;
    INIFileEditor originalObjectIni;
    int selectedEntityIndex = -1;
    bool selectedEntityIsNpc = true;

    MapEntityData placingEntity;
    bool hasPlacingEntity = false;
    bool continuousPlace = false;

    bool isDragging = false;
    bool isPanning = false;
    // 右键拖动连续绘制（障碍/陷阱工具）状态
    bool isRightDragging = false;
    QPoint lastMousePos;
    QPoint dragStartTile;
    int dragOriginalMapX = 0;
    int dragOriginalMapY = 0;

    // 左键拾取拖拽（范围拾取）状态
    bool isPickDragging = false;
    QPoint pickStartTile;
    int pickStartX = -1;
    int pickStartY = -1;
    int pickEndX = -1;
    int pickEndY = -1;

    bool isAreaSelecting = false;
    bool isTileSelectionToggleCandidate = false;
    int areaStartX = -1;
    int areaStartY = -1;
    int areaEndX = -1;
    int areaEndY = -1;

    // 显式选区 tile 集合。普通拖拽/点击替换；Ctrl+拖拽/点击 union；
    // Alt+拖拽/点击 subtract。areaStart/End 仅作为最后一次拖拽包围盒用于预览，
    // 实际选区以 selectedAreaTiles 为准。drawAreaSelection/getSelectedAreaTiles/
    // copy/clear/fill 都基于此集合。
    std::set<std::pair<int,int>> selectedAreaTiles;

    // 区域选择形状状态：分别保存下一次首选形状、当前拖拽形状、已完成区域形状、
    // 剪贴板形状。切换 checkbox 只影响 preferredAreaShape，不改变其他三个。
    AreaSelectionShape preferredAreaShape = AreaSelectionShape::Diamond;
    AreaSelectionShape currentDragShape = AreaSelectionShape::Diamond;
    AreaSelectionShape completedAreaShape = AreaSelectionShape::Diamond;
    AreaSelectionShape clipboardShape = AreaSelectionShape::Diamond;

    std::vector<ClipboardTileData> clipboardTiles;
    int clipboardWidth = 0;
    int clipboardHeight = 0;
    int clipboardMinCoordinateXOffset = 0;
    int clipboardMaxCoordinateXOffset = 0;
    int clipboardMinCoordinateYOffset = 0;
    int clipboardMaxCoordinateYOffset = 0;

    std::map<std::pair<int,int>, MapTileData> pasteOldTiles;
    std::map<std::pair<int,int>, MapTileData> pasteNewTiles;

    QTimer animationTimer;
    QElapsedTimer animationTickClock;
    uint64_t animationElapsedMilliseconds = 0;
    mutable bool collectingAnimationRequirements = false;
    mutable bool animationRequiredForCurrentPaint = false;

    std::map<std::string, std::string> entityResImageCache;

    // 渲染范围按“当前地图已用 MPC 的最大帧尺寸”外扩。该尺寸在 MPC 表/资源不变时
    // 稳定，缓存避免每次 paintEvent（200ms 动画也会触发）都扫描 255 个槽位并解码帧。
    // 缓存键：mapEditor 指针 + mpcCache 基路径 + 地图已用 MPC 槽位数。
    mutable int renderRangeMaxFrameWidth = 0;
    mutable int renderRangeMaxFrameHeight = 0;
    mutable QString renderRangeCacheKey;
    mutable bool renderRangeCacheValid = false;

    // 粘贴预览可见性。Esc 时隐藏预览但保留剪贴板数据；Ctrl+V 时重新显示。
    bool pastePreviewVisible = true;

public:
    // 测试辅助：直接设置 hover 瓦片与粘贴预览可见性，便于离屏渲染验证真实预览像素。
    // 不属于正常交互流程，仅用于回归测试。
    void setHoverTileForTest(int tileX, int tileY) { hoverTileX = tileX; hoverTileY = tileY; }
    void setPastePreviewVisibleForTest(bool visible) { pastePreviewVisible = visible; }
    bool isPastePreviewVisibleForTest() const { return pastePreviewVisible; }
    // 测试辅助：读取渲染范围。getVisibleTileRenderRange 本身是 private 实现细节，
    // 这里暴露只读访问便于回归测试验证缓存失效与边缘渲染范围刷新。
    void getVisibleTileRenderRangeForTest(int& startX, int& startY, int& endX, int& endY) const
    {
        getVisibleTileRenderRange(startX, startY, endX, endY);
    }
    // 测试辅助：读取基础可见 tile 范围（固定 margin=3，不含高图外扩）。
    void getVisibleTileRangeForTest(int& startX, int& startY, int& endX, int& endY) const
    {
        getVisibleTileRange(startX, startY, endX, endY);
    }
    // 测试辅助：查询当前是否存在区域选区（tile 集合非空时为 true）。
    bool hasAreaSelectionForTest() const { return hasAreaSelection(); }

    // 清除所有临时编辑状态：区域选择框、粘贴预览、剪贴板数据。
    // 打开/新建地图时调用，防止旧地图的剪贴板内容粘到新地图。
    void clearTransientEditState()
    {
        areaStartX = -1;
        areaStartY = -1;
        areaEndX = -1;
        areaEndY = -1;
        isAreaSelecting = false;
        isTileSelectionToggleCandidate = false;
        currentDragShape = AreaSelectionShape::Diamond;
        completedAreaShape = AreaSelectionShape::Diamond;
        clipboardShape = AreaSelectionShape::Diamond;
        pastePreviewVisible = false;
        clipboardTiles.clear();
        clipboardWidth = 0;
        clipboardHeight = 0;
        clipboardMinCoordinateXOffset = 0;
        clipboardMaxCoordinateXOffset = 0;
        clipboardMinCoordinateYOffset = 0;
        clipboardMaxCoordinateYOffset = 0;
        isPickDragging = false;
        pickStartX = -1;
        pickStartY = -1;
        pickEndX = -1;
        pickEndY = -1;
        selectedAreaTiles.clear();
    }
};
