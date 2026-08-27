#include "MinimapWidget.h"
#include "MapRenderCanvas.h"
#include "MapCoordinateTransform.h"
#include "../core/MapFileEditor.h"
#include "../core/MpcImageCache.h"

#include <QPainter>
#include <QApplication>
#ifdef JXQY_EDITOR_BUILD_BENCHMARK
#include <QElapsedTimer>
#endif
#include <QPainterPath>
#include <QMouseEvent>
#ifdef JXQY_EDITOR_BUILD_BENCHMARK
#include <QVariantMap>
#endif
#include <cmath>
#include <vector>
#include <array>
#include <cstdint>
#include <algorithm>
#include <unordered_map>

// 小地图每个瓦片的宽度单位（高度一半），与主画布 2:1 一致；>=4 避免整数行步长塌缩。
static const int TILE_WIDTH = MapCoordinateTransform::CANVAS_TILE_WIDTH;
static const int TILE_HEIGHT = MapCoordinateTransform::CANVAS_TILE_HEIGHT;
static const MapCoordinateTransform minimapTransform(MinimapWidget::MINIMAP_TILE_SIZE,
                                                     MinimapWidget::MINIMAP_TILE_SIZE / 2);

MinimapWidget::MinimapWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(160, 120);
    setMaximumSize(320, 240);
    setMouseTracking(true);
}

MinimapWidget::~MinimapWidget()
{
}

void MinimapWidget::setMapFileEditor(MapFileEditor* editor)
{
    mapEditor = editor;
    minimapDirty = true;
    update();
}

void MinimapWidget::setMpcImageCache(MpcImageCache* cache)
{
    mpcCache = cache;
    minimapDirty = true;
    update();
}

void MinimapWidget::setCanvas(MapRenderCanvas* canvas)
{
    canvasWidget = canvas;
}

void MinimapWidget::refreshMinimap()
{
    minimapDirty = true;
    update();
}

QPoint MinimapWidget::tileCenterToWidgetPosition(int tileX, int tileY)
{
    ensureMinimapGeometry();
    if (!mapEditor || !mapEditor->isLoaded())
        return QPoint();

    QPoint worldCenter = minimapTransform.tileToWorldCenter(
        mapEditor->getWidth(), tileX, tileY);

    float scale = 1.0f;
    int offsetX = 0;
    int offsetY = 0;
    computeMinimapFit(scale, offsetX, offsetY);

    // Transform the logical center before rounding.  Rounding the top-left and
    // then adding a separately-truncated half tile biases very large maps by
    // several rows once a tile becomes smaller than one widget pixel.
    return QPoint(qRound(offsetX + (worldCenter.x() + minimapOriginX) * scale),
                  qRound(offsetY + (worldCenter.y() + minimapOriginY) * scale));
}

QPoint MinimapWidget::widgetPositionToTile(const QPoint& widgetPosition)
{
    ensureMinimapGeometry();
    return minimapToTile(widgetPosition);
}

QSize MinimapWidget::backingImageSize()
{
    ensureMinimapGeometry();
    return minimapImage.size();
}

void MinimapWidget::ensureMinimapGeometry()
{
    if (!mapEditor || !mapEditor->isLoaded())
        return;
    if (minimapDirty || minimapImage.isNull())
        rebuildMinimapImage(mapEditor->getWidth(), mapEditor->getHeight());
}

void MinimapWidget::rebuildMinimapImage(int mapWidth, int mapHeight)
{
#ifdef JXQY_EDITOR_BUILD_BENCHMARK
    const bool measurePerformance = qApp && qApp->property(
        "editorPerformanceBenchmark").toBool();
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
        setProperty("performanceRebuildBreakdownMs", performanceBreakdown);
    };
#else
    auto recordStage = [](const char*) {};
    auto finishMeasurement = []() {};
#endif

    // Collect all draw entries first, then compute bounds from actual frame draw rects
    // to account for MPC frame images that extend above/beyond the tile diamond.
    struct MinimapDrawEntry
    {
        int tileX, tileY, layer;
        QImage scaledFrameImage;
        int scaledOffsetX = 0;
        int scaledOffsetY = 0;
    };
    struct MinimapFrameVisual
    {
        QImage scaledFrameImage;
        int scaledOffsetX = 0;
        int scaledOffsetY = 0;
    };
    std::array<std::vector<MinimapDrawEntry>, 3> drawEntries;
    std::unordered_map<int, MinimapFrameVisual> frameVisuals;
    struct TileMarker
    {
        int tileX;
        int tileY;
        uint8_t obstacle;
        uint8_t trap;
    };
    std::vector<TileMarker> specialTileMarkers;
    const double scaleToMinimap =
        static_cast<double>(MINIMAP_TILE_SIZE) / TILE_WIDTH;

    for (int ty = 0; ty < mapHeight; ++ty)
    {
        for (int tx = 0; tx < mapWidth; ++tx)
        {
            const MapTileData& tile = mapEditor->getTile(tx, ty);
            if (tile.obstacle != 0 || tile.trap != 0)
                specialTileMarkers.push_back({tx, ty, tile.obstacle, tile.trap});

            for (int layer = 0; layer < 3; ++layer)
            {
                const MapTileLayerData& layerData = tile.layer[layer];
                if (layerData.mpc == 0)
                    continue;

                const int visualKey =
                    (static_cast<int>(layerData.mpc) << 8) |
                    static_cast<int>(layerData.frame);
                auto visualIt = frameVisuals.find(visualKey);
                if (visualIt == frameVisuals.end())
                {
                    MinimapFrameVisual visual;
                    if (mpcCache)
                    {
                        const int arrayIndex = layerData.mpc - 1;
                        if (arrayIndex >= 0 && arrayIndex < MAP_EDITOR_MPC_COUNT)
                        {
                            const std::string mpcPath =
                                mapEditor->getMpcFilePath(arrayIndex);
                            if (!mpcPath.empty())
                            {
                                const QImage frameImage = mpcCache->getFrameImage(
                                    mpcPath, layerData.frame);
                                if (!frameImage.isNull())
                                {
                                    const int scaledWidth = std::max(
                                        1,
                                        static_cast<int>(
                                            frameImage.width() * scaleToMinimap));
                                    const int scaledHeight = std::max(
                                        1,
                                        static_cast<int>(
                                            frameImage.height() * scaleToMinimap));
                                    visual.scaledFrameImage = frameImage.scaled(
                                        scaledWidth,
                                        scaledHeight,
                                        Qt::IgnoreAspectRatio,
                                        Qt::SmoothTransformation);

                                    int frameOffsetX = frameImage.width() / 2;
                                    int frameOffsetY = frameImage.height();
                                    mpcCache->getFrameOffset(
                                        mpcPath,
                                        layerData.frame,
                                        frameOffsetX,
                                        frameOffsetY);
                                    visual.scaledOffsetX = static_cast<int>(
                                        std::lround(frameOffsetX * scaleToMinimap));
                                    visual.scaledOffsetY = static_cast<int>(
                                        std::lround(frameOffsetY * scaleToMinimap));
                                }
                            }
                        }
                    }
                    visualIt = frameVisuals.emplace(
                        visualKey, std::move(visual)).first;
                }
                drawEntries[layer].push_back({
                    tx,
                    ty,
                    layer,
                    visualIt->second.scaledFrameImage,
                    visualIt->second.scaledOffsetX,
                    visualIt->second.scaledOffsetY
                });
            }
        }
    }
    recordStage("collect_tiles");

    // Start with the complete editable map footprint.  Sparse/empty maps must
    // not collapse to their non-empty art because minimap navigation addresses
    // every tile, not only tiles that already have an MPC layer.
    QRectF mapBounds = minimapTransform.mapWorldBounds(mapWidth, mapHeight);
    int worldMinX = (int)std::floor(mapBounds.left());
    int worldMinY = (int)std::floor(mapBounds.top());
    int worldMaxX = (int)std::ceil(mapBounds.right());
    int worldMaxY = (int)std::ceil(mapBounds.bottom());

    for (const auto& layerEntries : drawEntries)
    {
        for (const auto& entry : layerEntries)
        {
            QPoint topLeft = minimapTransform.tileToWorldTopLeft(
                mapWidth, entry.tileX, entry.tileY);
            int destX = topLeft.x();
            int destY = topLeft.y();

            int left, top, right, bottom;
            if (!entry.scaledFrameImage.isNull())
            {
                left = destX + MINIMAP_TILE_SIZE / 2 -
                       entry.scaledOffsetX;
                top = destY + MINIMAP_TILE_SIZE / 2 -
                      entry.scaledOffsetY;
                right = left + entry.scaledFrameImage.width();
                bottom = top + entry.scaledFrameImage.height();
            }
            else
            {
                left = destX;
                top = destY;
                right = destX + MINIMAP_TILE_SIZE;
                bottom = destY + MINIMAP_TILE_SIZE / 2;
            }

            worldMinX = std::min(worldMinX, left);
            worldMinY = std::min(worldMinY, top);
            worldMaxX = std::max(worldMaxX, right);
            worldMaxY = std::max(worldMaxY, bottom);
        }
    }
    recordStage("compute_bounds");

    // Add padding
    int padding = MINIMAP_TILE_SIZE;
    worldMinX -= padding;
    worldMinY -= padding;
    worldMaxX += padding;
    worldMaxY += padding;

    minimapOriginX = -worldMinX;
    minimapOriginY = -worldMinY;
    minimapImageWidth = std::max(1, worldMaxX - worldMinX);
    minimapImageHeight = std::max(1, worldMaxY - worldMinY);

    minimapBackingScale = std::min(
        1.0,
        std::min((double)MAX_BACKING_EDGE / minimapImageWidth,
                 (double)MAX_BACKING_EDGE / minimapImageHeight));
    int backingWidth = std::min(MAX_BACKING_EDGE, std::max(1,
        (int)std::ceil(minimapImageWidth * minimapBackingScale)));
    int backingHeight = std::min(MAX_BACKING_EDGE, std::max(1,
        (int)std::ceil(minimapImageHeight * minimapBackingScale)));
    minimapImage = QImage(backingWidth, backingHeight, QImage::Format_ARGB32);
    if (minimapImage.isNull())
    {
        minimapDirty = false;
        recordStage("allocate_backing");
        finishMeasurement();
        return;
    }
    minimapImage.fill(QColor(30, 30, 30));

    QPainter minimapPainter(&minimapImage);
    minimapPainter.scale(minimapBackingScale, minimapBackingScale);
    minimapPainter.setRenderHint(QPainter::Antialiasing, true);
    minimapPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    auto obstacleColor = [](uint8_t obstacle)
    {
        const bool hard = (obstacle & 0x80) != 0;
        const bool transparent = (obstacle & 0x40) != 0;
        const bool jumpable = (obstacle & 0x20) != 0;
        if (hard)
            return jumpable ? QColor(180, 120, 60) : QColor(180, 60, 60);
        if (transparent)
            return jumpable ? QColor(60, 180, 180) : QColor(60, 60, 180);
        return QColor(80, 140, 80);
    };

    auto drawBaseDiamond = [&](int tileX, int tileY, uint8_t obstacle, uint8_t trap)
    {
        QPoint topLeft = minimapTransform.tileToWorldTopLeft(mapWidth, tileX, tileY);
        const float halfW = MINIMAP_TILE_SIZE / 2.0f;
        const float halfH = MINIMAP_TILE_SIZE / 4.0f + 0.5f;
        float centerX = topLeft.x() + minimapOriginX + halfW;
        float centerY = topLeft.y() + minimapOriginY + halfH;

        QColor tileColor = obstacleColor(obstacle);
        tileColor.setAlpha(90);
        if (obstacle == 0 && trap != 0)
            tileColor = QColor(145, 110, 45, 130);

        QPainterPath diamond;
        diamond.moveTo(centerX, centerY - halfH);
        diamond.lineTo(centerX + halfW, centerY);
        diamond.lineTo(centerX, centerY + halfH);
        diamond.lineTo(centerX - halfW, centerY);
        diamond.closeSubpath();
        minimapPainter.fillPath(diamond, tileColor);
        minimapPainter.drawPath(diamond);
    };

    // Small maps keep the tile grid.  Once individual diamonds are below
    // useful screen resolution, paint one bounded footprint and only overlay
    // non-default obstacle/trap tiles.  This avoids hundreds of thousands of
    // QPainter path submissions for a large empty or sparse map.
    QPen basePen(QColor(95, 110, 95, 70), 1);
    basePen.setCosmetic(true);
    minimapPainter.setPen(basePen);
    const int64_t tileCount = (int64_t)mapWidth * mapHeight;
    const bool drawDetailedTileGrid = tileCount <= 16384;
    if (drawDetailedTileGrid)
    {
        for (int ty = 0; ty < mapHeight; ++ty)
        {
            for (int tx = 0; tx < mapWidth; ++tx)
            {
                const MapTileData& tile = mapEditor->getTile(tx, ty);
                drawBaseDiamond(tx, ty, tile.obstacle, tile.trap);
            }
        }
    }
    else
    {
        QRectF footprint = mapBounds.translated(minimapOriginX, minimapOriginY);
        minimapPainter.fillRect(footprint, QColor(80, 140, 80, 55));
        minimapPainter.drawRect(footprint);
        for (const TileMarker& marker : specialTileMarkers)
            drawBaseDiamond(marker.tileX, marker.tileY, marker.obstacle, marker.trap);
    }
    recordStage("draw_base");

    // 小地图只绘制瓦片层，实体由主画布/实体面板负责。
    auto drawTileEntry = [&](const MinimapDrawEntry& entry)
    {
        QPoint topLeft = minimapTransform.tileToWorldTopLeft(mapWidth, entry.tileX, entry.tileY);
        int destX = topLeft.x() + minimapOriginX;
        int destY = topLeft.y() + minimapOriginY;

        if (!entry.scaledFrameImage.isNull())
        {
            int drawX = destX + MINIMAP_TILE_SIZE / 2 -
                        entry.scaledOffsetX;
            int drawY = destY + MINIMAP_TILE_SIZE / 2 -
                        entry.scaledOffsetY;

            minimapPainter.drawImage(drawX, drawY, entry.scaledFrameImage);
        }
        else
        {
            const float halfW = MINIMAP_TILE_SIZE / 2.0f;
            const float halfH = MINIMAP_TILE_SIZE / 4.0f + 0.5f;
            float cx = destX + halfW;
            float cy = destY + halfH;

            uint8_t obstacle = mapEditor->getTileObstacle(entry.tileX, entry.tileY);
            QColor tileColor = obstacleColor(obstacle);

            QPainterPath diamond;
            diamond.moveTo(cx, cy - halfH);
            diamond.lineTo(cx + halfW, cy);
            diamond.lineTo(cx, cy + halfH);
            diamond.lineTo(cx - halfW, cy);
            diamond.closeSubpath();
            minimapPainter.fillPath(diamond, tileColor);
        }
    };

    // Layer 0
    for (const auto& entry : drawEntries[0])
        drawTileEntry(entry);

    // Layer 1
    for (const auto& entry : drawEntries[1])
        drawTileEntry(entry);

    // Layer 2
    for (const auto& entry : drawEntries[2])
        drawTileEntry(entry);

    minimapDirty = false;
    recordStage("draw_art");
    finishMeasurement();
}

void MinimapWidget::computeMinimapFit(float& scale, int& offsetX, int& offsetY) const
{
    if (!mapEditor || !mapEditor->isLoaded() || minimapImageWidth <= 0 || minimapImageHeight <= 0)
    {
        scale = 1.0f;
        offsetX = offsetY = 0;
        return;
    }

    float scaleX = (float)width() / minimapImageWidth;
    float scaleY = (float)height() / minimapImageHeight;
    scale = std::min(scaleX, scaleY);

    offsetX = (width() - (int)(minimapImageWidth * scale)) / 2;
    offsetY = (height() - (int)(minimapImageHeight * scale)) / 2;
}

QPoint MinimapWidget::minimapToTile(const QPoint& minimapPos) const
{
    if (!mapEditor || !mapEditor->isLoaded())
        return QPoint(-1, -1);

    // widget -> minimap 世界（含 origin 偏移还原），再交给统一变换做菱形命中反查。
    float scale = 1.0f;
    int offsetX = 0;
    int offsetY = 0;
    computeMinimapFit(scale, offsetX, offsetY);
    if (scale <= 0.0f)
        return QPoint(-1, -1);

    QPointF world(((minimapPos.x() - offsetX) / (double)scale) - minimapOriginX,
                  ((minimapPos.y() - offsetY) / (double)scale) - minimapOriginY);

    return minimapTransform.worldToTilePrecise(world,
        mapEditor->getWidth(), mapEditor->getHeight());
}

QPointF MinimapWidget::canvasScreenToWidget(const QPoint& canvasScreen) const
{
    if (!canvasWidget)
        return QPointF();

    // 画布世界（64 单位）-> 小地图世界（MINIMAP_TILE_SIZE 单位，再扣除 origin 偏移）-> widget。
    QPointF canvasWorld = canvasWidget->screenToWorld(canvasScreen);
    double worldScale = (double)MINIMAP_TILE_SIZE / TILE_WIDTH;

    float fitScale = 1.0f;
    int offsetX = 0;
    int offsetY = 0;
    computeMinimapFit(fitScale, offsetX, offsetY);

    return QPointF(offsetX + (canvasWorld.x() * worldScale + minimapOriginX) * fitScale,
                   offsetY + (canvasWorld.y() * worldScale + minimapOriginY) * fitScale);
}

void MinimapWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.fillRect(rect(), QColor(40, 40, 40));

    if (!mapEditor || !mapEditor->isLoaded())
    {
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter, tr("无地图"));
        return;
    }

    int mapWidth = mapEditor->getWidth();
    int mapHeight = mapEditor->getHeight();

    if (minimapDirty || minimapImage.isNull())
    {
        rebuildMinimapImage(mapWidth, mapHeight);
    }

    if (minimapImage.isNull())
    {
        painter.setPen(Qt::red);
        painter.drawText(rect(), Qt::AlignCenter, tr("小地图创建失败"));
        return;
    }

    float fitScale = 1.0f;
    int offsetX = 0;
    int offsetY = 0;
    computeMinimapFit(fitScale, offsetX, offsetY);

    int drawWidth = (int)(minimapImageWidth * fitScale);
    int drawHeight = (int)(minimapImageHeight * fitScale);

    const QRectF sourceRect(
        0.0, 0.0,
        minimapImageWidth * minimapBackingScale,
        minimapImageHeight * minimapBackingScale);
    painter.drawImage(QRect(offsetX, offsetY, drawWidth, drawHeight),
                      minimapImage, sourceRect);

    // 视口框：取画布 4 个屏幕角点投影到小地图，连成实际视野四边形。
    if (canvasWidget)
    {
        QPointF corners[4] = {
            canvasScreenToWidget(QPoint(0, 0)),
            canvasScreenToWidget(QPoint(canvasWidget->width(), 0)),
            canvasScreenToWidget(QPoint(canvasWidget->width(), canvasWidget->height())),
            canvasScreenToWidget(QPoint(0, canvasWidget->height()))
        };

        QPainterPath viewportPath;
        viewportPath.moveTo(corners[0]);
        for (int i = 1; i < 4; ++i)
            viewportPath.lineTo(corners[i]);
        viewportPath.closeSubpath();

        painter.setPen(QPen(QColor(255, 255, 0), 1));
        painter.setBrush(QColor(255, 255, 0, 30));
        painter.drawPath(viewportPath);
    }
}

void MinimapWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        isDragging = true;
        QPoint tilePos = minimapToTile(event->pos());
        if (tilePos.x() >= 0 && tilePos.y() >= 0 && canvasWidget)
        {
            canvasWidget->centerOnTile(tilePos.x(), tilePos.y());
            update();
        }
    }
}

void MinimapWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (isDragging)
    {
        QPoint tilePos = minimapToTile(event->pos());
        if (tilePos.x() >= 0 && tilePos.y() >= 0 && canvasWidget)
        {
            canvasWidget->centerOnTile(tilePos.x(), tilePos.y());
            update();
        }
    }
}

void MinimapWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        isDragging = false;
    }
}
