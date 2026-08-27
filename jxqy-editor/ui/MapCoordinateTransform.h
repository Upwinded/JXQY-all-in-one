#pragma once

#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <algorithm>
#include <cmath>

/// 等距（菱形）地图坐标变换的统一实现。
///
/// 历史上 MapRenderCanvas、MinimapWidget 各自维护一套 tile<->像素 公式，
/// 且 screenToTile 的闭式反查在奇偶行边界会偏移到相邻瓦片。本类把正向映射
/// 收敛到 tileToWorldTopLeft / tileToWorldCenter 一处，反查改用菱形命中测试，
/// 保证 tile->screen 与 screen->tile 严格互逆，供主画布、小地图等统一复用。
///
/// 瓦片布局：cenTileY 固定为 0，奇数行（tileY 为奇）在世界 X 上额外偏移半个
/// 瓦片宽（staggered iso）。tileToWorldTopLeft 返回的是瓦片包围盒左上角，
/// 与原 getTileWorldPosition / tileToScreen 的整数运算完全一致。
class MapCoordinateTransform
{
public:
    /// 主画布的标准菱形瓦片尺寸（宽:高 = 2:1）。
    static constexpr int CANVAS_TILE_WIDTH = 64;
    static constexpr int CANVAS_TILE_HEIGHT = 32;

    constexpr MapCoordinateTransform(int tileWidth, int tileHeight)
        : tileWidth(tileWidth)
        , tileHeight(tileHeight)
    {
    }

    int getTileWidth() const { return tileWidth; }
    int getTileHeight() const { return tileHeight; }

    /// 瓦片包围盒左上角在世界坐标（未缩放、未滚动）下的位置。
    QPoint tileToWorldTopLeft(int mapWidth, int tileX, int tileY) const
    {
        int cenTileX = mapWidth / 2;
        int cenTileY = 0;
        int line = std::abs(tileY % 2);
        int cenLine = std::abs(cenTileY % 2);
        int dx = tileX - cenTileX;
        int dy = tileY - cenTileY;

        int worldX = 0;
        int worldY = dy * tileHeight / 2;
        if (cenLine == line)
        {
            worldX = dx * tileWidth + mapWidth * tileWidth / 2;
        }
        else
        {
            if (cenLine == 0)
                worldX = dx * tileWidth + tileWidth / 2 + mapWidth * tileWidth / 2;
            else
                worldX = dx * tileWidth - tileWidth / 2 + mapWidth * tileWidth / 2;
        }
        return QPoint(worldX, worldY);
    }

    /// 瓦片菱形中心在世界坐标下的位置。
    QPoint tileToWorldCenter(int mapWidth, int tileX, int tileY) const
    {
        QPoint topLeft = tileToWorldTopLeft(mapWidth, tileX, tileY);
        return QPoint(topLeft.x() + tileWidth / 2, topLeft.y() + tileHeight / 2);
    }

    /// Full editable map footprint in world coordinates.  This is independent
    /// of which tile layers currently contain images, so empty and sparse maps
    /// retain stable zoom/minimap geometry.
    QRectF mapWorldBounds(int mapWidth, int mapHeight) const
    {
        if (mapWidth <= 0 || mapHeight <= 0)
            return QRectF();

        QRectF bounds;
        bool first = true;
        auto includeTile = [&](int tileX, int tileY)
        {
            QPoint topLeft = tileToWorldTopLeft(mapWidth, tileX, tileY);
            QRectF tileBounds(topLeft.x(), topLeft.y(), tileWidth, tileHeight);
            bounds = first ? tileBounds : bounds.united(tileBounds);
            first = false;
        };

        // X extent depends only on row parity; Y extent is determined by the
        // first/last rows.  These representatives cover both without scanning
        // every tile in very large maps.
        includeTile(0, 0);
        includeTile(mapWidth - 1, 0);
        if (mapHeight > 1)
        {
            includeTile(0, 1);
            includeTile(mapWidth - 1, 1);
            includeTile(0, mapHeight - 1);
            includeTile(mapWidth - 1, mapHeight - 1);
        }
        return bounds;
    }

    /// 世界坐标 -> 屏幕坐标（视口）。zoom 为缩放，scroll 为视口左上角对应的世界偏移。
    static QPoint worldToScreen(const QPointF& world, float zoom, int scrollX, int scrollY)
    {
        return QPoint((int)(world.x() * zoom) - scrollX, (int)(world.y() * zoom) - scrollY);
    }

    /// 屏幕坐标 -> 世界坐标。
    static QPointF screenToWorld(const QPoint& screen, float zoom, int scrollX, int scrollY)
    {
        return QPointF((screen.x() + scrollX) / (double)zoom,
                       (screen.y() + scrollY) / (double)zoom);
    }

    /// 世界点是否落在指定瓦片的菱形内（含容差，用于边界判定）。
    bool worldPointInTileDiamond(const QPointF& world, int mapWidth, int tileX, int tileY) const
    {
        QPoint center = tileToWorldCenter(mapWidth, tileX, tileY);
        double halfW = tileWidth / 2.0;
        double halfH = tileHeight / 2.0;
        if (halfW <= 0.0 || halfH <= 0.0)
            return false;
        double dx = std::abs(world.x() - center.x()) / halfW;
        double dy = std::abs(world.y() - center.y()) / halfH;
        return (dx + dy) <= 1.0 + 1e-3;
    }

    /// 菱形命中测试反查：世界坐标（QPointF，保留小数精度）-> 瓦片。先粗估候选瓦片，
    /// 再对其 3x3 邻域做菱形命中测试，取距离中心最近者；范围外或未命中返回 (-1,-1)。
    /// 小地图等小瓦片场景必须传入未截断的 QPointF，避免 int 截断导致点击偏移。
    QPoint worldToTilePrecise(const QPointF& world, int mapWidth, int mapHeight) const
    {
        if (mapWidth <= 0 || mapHeight <= 0)
            return QPoint(-1, -1);

        double wx = world.x();
        double wy = world.y();

        double halfW = tileWidth / 2.0;
        double halfH = tileHeight / 2.0;

        // 粗估行：中心 Y = tileY*tileHeight/2 + tileHeight/2。
        int tyEst = (int)std::floor(2.0 * wy / tileHeight);

        int bestTileX = -1;
        int bestTileY = -1;
        double bestScore = 1e18;

        for (int ty = tyEst - 1; ty <= tyEst + 1; ++ty)
        {
            if (ty < 0 || ty >= mapHeight)
                continue;

            // 该行的奇偶偏移决定了列估算基准。
            int parity = (std::abs(ty % 2) == 1) ? (tileWidth / 2) : 0;
            int term = (mapWidth / 2) * tileWidth - mapWidth * tileWidth / 2;
            double txReal = (wx + term - tileWidth / 2.0 - parity) / (double)tileWidth;
            int txEst = (int)std::floor(txReal + 0.5);

            for (int tx = txEst - 1; tx <= txEst + 1; ++tx)
            {
                if (tx < 0 || tx >= mapWidth)
                    continue;

                QPoint center = tileToWorldCenter(mapWidth, tx, ty);
                double dx = std::abs(wx - center.x()) / halfW;
                double dy = std::abs(wy - center.y()) / halfH;
                double score = dx + dy;
                if (score <= 1.0 + 1e-3 && score < bestScore)
                {
                    bestScore = score;
                    bestTileX = tx;
                    bestTileY = ty;
                }
            }
        }

        if (bestTileX < 0)
            return QPoint(-1, -1);
        return QPoint(bestTileX, bestTileY);
    }

    /// 菱形命中测试反查：屏幕坐标 -> 瓦片。内部走 worldToTilePrecise（保留精度）。
    QPoint screenToTilePrecise(const QPoint& screen, int mapWidth, int mapHeight,
                               float zoom, int scrollX, int scrollY) const
    {
        QPointF world = screenToWorld(screen, zoom, scrollX, scrollY);
        return worldToTilePrecise(world, mapWidth, mapHeight);
    }

    /// 由世界坐标粗估瓦片（不做边界裁剪，可能返回负数或超出 mapWidth 的索引），
    /// 仅供可见范围推导使用：取视口世界矩形的若干角点粗估后取 min/max 并裁剪到地图边界，
    /// 即可覆盖极端缩放（视口远大于地图）的情况，避免回退到固定左上角小块。
    QPoint estimateTileFromWorld(const QPointF& world, int mapWidth) const
    {
        int ty = (int)std::floor(2.0 * world.y() / tileHeight);
        int parity = (std::abs(ty % 2) == 1) ? (tileWidth / 2) : 0;
        int term = (mapWidth / 2) * tileWidth - mapWidth * tileWidth / 2;
        double txReal = (world.x() + term - tileWidth / 2.0 - parity) / (double)tileWidth;
        int tx = (int)std::floor(txReal + 0.5);
        return QPoint(tx, ty);
    }

    /// 把粗估得到的瓦片 min/max 范围裁剪到地图边界并外扩余量。
    /// 估算范围完全在地图某一侧（无交集）时返回 startX>endX / startY>endY，
    /// 渲染循环自然跳过，不会因重置为 0 而渲染整图。可供画布与单元测试共用。
    static void clampVisibleTileRange(int minTileX, int minTileY,
                                      int maxTileX, int maxTileY,
                                      int mapWidth, int mapHeight, int margin,
                                      int& startX, int& startY, int& endX, int& endY)
    {
        startX = std::max(0, minTileX - margin);
        startY = std::max(0, minTileY - margin);
        endX = std::min(mapWidth - 1, maxTileX + margin);
        endY = std::min(mapHeight - 1, maxTileY + margin);
    }


private:
    int tileWidth;
    int tileHeight;
};
