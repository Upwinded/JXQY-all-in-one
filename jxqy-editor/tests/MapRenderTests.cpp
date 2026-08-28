#include "TestSupport.h"
#include "WidgetTestSupport.h"

#include "../core/MapFileEditor.h"
#include "../core/MpcImageCache.h"
#include "../core/PicFileEditor.h"
#include "../core/TranslationManager.h"
#include "../core/Util.h"
#include "../ui/MapCoordinateTransform.h"
#include "../ui/MapEditorWindow.h"
#include "../ui/MapRenderCanvas.h"
#include "../ui/MinimapWidget.h"

#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QPaintEvent>
#include <QPixmap>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace
{
using namespace TestSupport;
using WidgetTestSupport::sendWidgetMouseEvent;

bool testMpcCacheBasePathChange()
{
    QTemporaryDir temporaryDirectory;
    if (!check(temporaryDirectory.isValid(), "create cache temporary directory"))
        return false;

    QDir root(temporaryDirectory.path());
    if (!check(root.mkpath("first") && root.mkpath("second") &&
               root.mkpath(QString::fromUtf8("中文缓存")),
               "create cache fixture directories"))
        return false;

    std::vector<uint8_t> first = makeSinglePixelMpc(QColor(255, 0, 0, 255));
    std::vector<uint8_t> second = makeSinglePixelMpc(QColor(0, 0, 255, 255));
    QString firstPath = root.filePath("first/shared.mpc");
    QString secondPath = root.filePath("second/shared.mpc");
    QString utf8Path = root.filePath(QString::fromUtf8("中文缓存/中文图.mpc"));
    QString outsidePath = root.filePath("outside.mpc");
    if (!check(Util::writeFileFromBuffer(
            firstPath.toUtf8().toStdString(), first.data(), first.size()),
            "write first cache fixture") ||
        !check(Util::writeFileFromBuffer(
            secondPath.toUtf8().toStdString(), second.data(), second.size()),
            "write second cache fixture") ||
        !check(Util::writeFileFromBuffer(
            utf8Path.toUtf8().toStdString(), first.data(), first.size()),
            "write UTF-8 cache fixture") ||
        !check(Util::writeFileFromBuffer(
            outsidePath.toUtf8().toStdString(), first.data(), first.size()),
            "write out-of-root cache fixture"))
    {
        return false;
    }

    MpcImageCache cache;
    cache.setAssetsBasePath(root.filePath("first").toUtf8().toStdString());
    QImage firstImage = cache.getFrameImage("shared.mpc", 0);
    QImage normalizedFirstImage = cache.getFrameImage(".\\shared.mpc", 0);
    QImage historicalLeadingSeparatorImage =
        cache.getFrameImage("\\shared.mpc", 0);
    const bool historicalLeadingSeparatorLoaded =
        cache.isLoaded("\\shared.mpc");
    QImage escapedImage = cache.getFrameImage("../outside.mpc", 0);
    QImage absoluteImage = cache.getFrameImage(outsidePath.toUtf8().toStdString(), 0);
    int normalizedCacheSize = cache.getCurrentCacheSize();
    cache.setAssetsBasePath(root.filePath("second").toUtf8().toStdString());
    QImage secondImage = cache.getFrameImage("shared.mpc", 0);
    cache.setAssetsBasePath(root.filePath(QString::fromUtf8("中文缓存")).toUtf8().toStdString());
    QImage utf8Image = cache.getFrameImage(QString::fromUtf8("中文图.mpc").toUtf8().toStdString(), 0);
    MpcImageCache rootlessCache;
    QImage rootlessAbsolute = rootlessCache.getFrameImage(
        outsidePath.toUtf8().toStdString(), 0);
    QImage rootlessTraversal = rootlessCache.getFrameImage("../outside.mpc", 0);
    QImage explicitAbsolute = rootlessCache.getFrameImageByPath(
        outsidePath.toUtf8().toStdString(), 0);

    return check(firstImage.pixelColor(0, 0) == QColor(255, 0, 0, 255),
                 "load image from first cache base path") &&
        check(normalizedFirstImage.pixelColor(0, 0) ==
                  QColor(255, 0, 0, 255) &&
              historicalLeadingSeparatorImage.pixelColor(0, 0) ==
                  QColor(255, 0, 0, 255) &&
              historicalLeadingSeparatorLoaded &&
              normalizedCacheSize == 1,
              "normalize dot and historical single-leading-separator variants to one cache entry") &&
        check(escapedImage.isNull() && absoluteImage.isNull(),
              "assets-root cache rejects traversal and absolute fallback") &&
        check(secondImage.pixelColor(0, 0) == QColor(0, 0, 255, 255),
              "invalidate cache when base path changes") &&
        check(utf8Image.pixelColor(0, 0) == QColor(255, 0, 0, 255),
              "load image through UTF-8 cache base path") &&
        check(rootlessAbsolute.isNull() && rootlessTraversal.isNull(),
              "rootless cache rejects implicit absolute and traversal paths") &&
        check(explicitAbsolute.pixelColor(0, 0) == QColor(255, 0, 0, 255),
              "explicit by-path API remains available without an assets root");
}

bool testMapEditorAssetsPathSwitchInvalidatesRenderedArt()
{
    QTemporaryDir temporaryDirectory;
    if (!check(
            temporaryDirectory.isValid(),
            "create map assets-switch temporary directory"))
    {
        return false;
    }

    QDir root(temporaryDirectory.path());
    const QString firstRootPath = root.filePath("first");
    const QString secondRootPath = root.filePath("second");
    if (!check(
            QDir().mkpath(QDir(firstRootPath).filePath("mpc/map")) &&
                QDir().mkpath(QDir(secondRootPath).filePath("mpc/map")),
            "create map assets-switch MPC directories"))
    {
        return false;
    }

    const std::vector<uint8_t> firstMpc = makeSinglePixelMpc(
        QColor(255, 0, 0, 255));
    const std::vector<uint8_t> secondMpc = makeSinglePixelMpc(
        QColor(0, 0, 255, 255));
    if (!check(
            Util::writeFileFromBuffer(
                QDir(firstRootPath).filePath("mpc/map/shared.mpc")
                    .toUtf8().toStdString(),
                firstMpc.data(),
                firstMpc.size()) &&
                Util::writeFileFromBuffer(
                    QDir(secondRootPath).filePath("mpc/map/shared.mpc")
                        .toUtf8().toStdString(),
                    secondMpc.data(),
                    secondMpc.size()),
            "write map assets-switch MPC fixtures"))
    {
        return false;
    }

    MapEditorWindow window;
    window.setAttribute(Qt::WA_DontShowOnScreen, true);
    window.resize(1000, 700);
    window.show();
    if (!check(
            window.setAssetsBasePath(firstRootPath) &&
                window.createNewMap(8, 8),
            "create map under first assets root"))
    {
        window.hide();
        return false;
    }

    MapFileEditor& editor = window.getMapEditorRef();
    editor.setMpcPath("mpc/map");
    MpcInfoData info;
    info.name = "shared.mpc";
    editor.setMpcInfo(0, info);
    MapTileData tile;
    tile.layer[0].mpc = 1;
    for (int y = 0; y < editor.getHeight(); ++y)
    {
        for (int x = 0; x < editor.getWidth(); ++x)
            editor.setTile(x, y, tile);
    }
    MapRenderCanvas* canvas = window.findChild<MapRenderCanvas*>();
    MinimapWidget* minimap = window.findChild<MinimapWidget*>();
    if (!check(
            canvas != nullptr && minimap != nullptr,
            "map assets-switch render widgets exist"))
    {
        window.hide();
        return false;
    }
    canvas->invalidateRenderRangeCache();
    canvas->update();
    minimap->refreshMinimap();
    canvas->setObstacleVisible(false);
    canvas->setTrapVisible(false);
    canvas->setGridVisible(false);
    canvas->setCoordinateVisible(false);
    canvas->centerOnTile(4, 4);
    QApplication::processEvents();

    auto containsColor = [](const QImage& image, bool blue)
    {
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                const QColor pixel = QColor::fromRgba(image.pixel(x, y));
                if (blue)
                {
                    if (pixel.blue() > 180 && pixel.red() < 80)
                        return true;
                }
                else if (pixel.red() > 180 && pixel.blue() < 80)
                {
                    return true;
                }
            }
        }
        return false;
    };
    auto grabCanvas = [&]()
    {
        QPixmap pixmap(canvas->size());
        pixmap.fill(QColor(30, 30, 30));
        canvas->render(&pixmap);
        return pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    };

    bool ok = check(
        containsColor(grabCanvas(), false),
        "map canvas renders first assets-root art");
    ok = check(
        containsColor(minimap->grab().toImage(), false),
        "minimap renders first assets-root art") && ok;

    ok = check(
        window.setAssetsBasePath(secondRootPath),
        "switch map editor to second assets root") && ok;
    QApplication::processEvents();
    ok = check(
        containsColor(grabCanvas(), true),
        "map canvas invalidates scaled art after assets-root switch") && ok;
    ok = check(
        containsColor(minimap->grab().toImage(), true),
        "minimap rebuilds art after assets-root switch") && ok;

    window.hide();
    QApplication::processEvents();
    return ok;
}

bool testMapCoordinateTransformRoundTrip()
{
    // tile->world->screen->tile 必须严格互逆：点击瓦片中心应返回该瓦片。
    // 覆盖偶数/奇数地图宽度（影响 cenTileX 整数除法）与多档 zoom/scroll。
    const MapCoordinateTransform canvasTransform(
        MapCoordinateTransform::CANVAS_TILE_WIDTH,
        MapCoordinateTransform::CANVAS_TILE_HEIGHT);

    struct ZoomScroll { float zoom; int sx; int sy; };
    const ZoomScroll cases[] = {
        {1.0f, 0, 0},
        {2.0f, 100, 50},
        {0.5f, -300, -120},
        {1.0f, 1234, 5678}
    };

    for (int mapWidth : {10, 11, 20, 21})
    {
        const int mapHeight = 16;
        for (const ZoomScroll& zs : cases)
        {
            for (int ty = 0; ty < mapHeight; ++ty)
            {
                for (int tx = 0; tx < mapWidth; ++tx)
                {
                    QPoint worldCenter = canvasTransform.tileToWorldCenter(mapWidth, tx, ty);
                    QPoint screen = MapCoordinateTransform::worldToScreen(
                        worldCenter, zs.zoom, zs.sx, zs.sy);
                    QPoint got = canvasTransform.screenToTilePrecise(
                        screen, mapWidth, mapHeight, zs.zoom, zs.sx, zs.sy);
                    if (got.x() != tx || got.y() != ty)
                    {
                        char buf[256];
                        std::snprintf(buf, sizeof(buf),
                            "screenToTile round-trip failed: mapW=%d tile=(%d,%d) got=(%d,%d)",
                            mapWidth, tx, ty, got.x(), got.y());
                        return check(false, buf);
                    }
                }
            }
        }
    }

    // 地图远外点击应返回 (-1,-1)。
    QPoint outside = canvasTransform.screenToTilePrecise(
        QPoint(-5000, -5000), 10, 16, 1.0f, 0, 0);
    if (!check(outside.x() == -1 && outside.y() == -1, "screenToTile outside map returns -1"))
        return false;

    // 小地图变换与 MinimapWidget 实际使用的 (MINIMAP_TILE_SIZE, MINIMAP_TILE_SIZE/2) = (4,2) 一致。
    // 验证：相邻行 Y 递增不塌缩、点击瓦片中心（浮点精度）返回正确瓦片、
    // 缩略图 image 边界由实际 tileToWorldTopLeft 扫描包围盒确定（最后一行紧贴边界、无大量空白）。
    const int minimapTileSize = 4;
    const MapCoordinateTransform miniTransform(minimapTileSize, minimapTileSize / 2);

    for (int mapWidth : {10, 11, 20, 21})
    {
        const int mapHeight = 16;

        // 重建与 MinimapWidget 一致的 image 包围盒：扫描所有 tile 的 topLeft + 右下角估算。
        int worldMinX = 0, worldMinY = 0, worldMaxX = 0, worldMaxY = 0;
        bool firstTile = true;
        for (int ty = 0; ty < mapHeight; ++ty)
        {
            for (int tx = 0; tx < mapWidth; ++tx)
            {
                QPoint tl = miniTransform.tileToWorldTopLeft(mapWidth, tx, ty);
                int right = tl.x() + minimapTileSize;
                int bottom = tl.y() + minimapTileSize / 2;
                if (firstTile)
                {
                    worldMinX = tl.x(); worldMinY = tl.y();
                    worldMaxX = right; worldMaxY = bottom;
                    firstTile = false;
                }
                else
                {
                    worldMinX = std::min(worldMinX, tl.x());
                    worldMinY = std::min(worldMinY, tl.y());
                    worldMaxX = std::max(worldMaxX, right);
                    worldMaxY = std::max(worldMaxY, bottom);
                }
            }
        }
        const int thumbWidth = worldMaxX - worldMinX;
        const int thumbHeight = worldMaxY - worldMinY;
        const int originX = -worldMinX;
        const int originY = -worldMinY;

        int prevY = -1;
        for (int ty = 0; ty < mapHeight; ++ty)
        {
            // 相邻行 topLeft.y() 必须严格递增（旧 (2,1) 整数除法会把相邻行压到同一 y）。
            int y = miniTransform.tileToWorldTopLeft(mapWidth, 0, ty).y();
            if (!(y > prevY))
            {
                char buf[256];
                std::snprintf(buf, sizeof(buf),
                    "minimap row y not increasing: mapW=%d ty=%d y=%d prevY=%d",
                    mapWidth, ty, y, prevY);
                return check(false, buf);
            }
            prevY = y;
        }

        // 最后一行的底部 (topLeft.y + tileHeight/2) 应接近 thumbHeight（容差 1 像素），
        // 不应留大量空白。
        QPoint lastTopLeft = miniTransform.tileToWorldTopLeft(mapWidth, 0, mapHeight - 1);
        int lastBottom = lastTopLeft.y() + minimapTileSize / 2;
        if (std::abs(lastBottom - thumbHeight) > 1)
        {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "minimap last row not near thumbHeight: mapW=%d lastBottom=%d thumbHeight=%d",
                mapWidth, lastBottom, thumbHeight);
            return check(false, buf);
        }

        for (int ty = 0; ty < mapHeight; ++ty)
        {
            for (int tx = 0; tx < mapWidth; ++tx)
            {
                QPoint topLeft = miniTransform.tileToWorldTopLeft(mapWidth, tx, ty);
                // 加 origin 偏移后必须落在 image 内。
                int ix = topLeft.x() + originX;
                int iy = topLeft.y() + originY;
                if (ix < 0 || ix >= thumbWidth || iy < 0 || iy >= thumbHeight)
                {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf),
                        "minimap tile outside image: mapW=%d tile=(%d,%d) img=(%d,%d) thumb=(%d,%d)",
                        mapWidth, tx, ty, ix, iy, thumbWidth, thumbHeight);
                    return check(false, buf);
                }

                // 点击瓦片中心（保留 QPointF 精度）应返回该瓦片。
                QPointF center = miniTransform.tileToWorldCenter(mapWidth, tx, ty);
                QPoint got = miniTransform.worldToTilePrecise(center, mapWidth, mapHeight);
                if (got.x() != tx || got.y() != ty)
                {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf),
                        "minimap worldToTilePrecise round-trip failed: mapW=%d tile=(%d,%d) got=(%d,%d)",
                        mapWidth, tx, ty, got.x(), got.y());
                    return check(false, buf);
                }
            }
        }
    }

    // 可见范围粗估：视口远大于地图时，四角粗估裁剪后应覆盖整张地图，而不是只回退左上一小块。
    {
        const int mapWidth = 200;
        const int mapHeight = 200;
        QPointF farCorners[4] = {
            QPointF(-50000, -50000),
            QPointF(50000, -50000),
            QPointF(-50000, 50000),
            QPointF(50000, 50000)
        };
        int minX = 1 << 30, minY = 1 << 30, maxX = -(1 << 30), maxY = -(1 << 30);
        for (const QPointF& c : farCorners)
        {
            QPoint est = canvasTransform.estimateTileFromWorld(c, mapWidth);
            minX = std::min(minX, est.x()); minY = std::min(minY, est.y());
            maxX = std::max(maxX, est.x()); maxY = std::max(maxY, est.y());
        }
        // 用与画布一致的 clampVisibleTileRange 裁剪，应覆盖整张地图。
        int startX, startY, endX, endY;
        MapCoordinateTransform::clampVisibleTileRange(
            minX, minY, maxX, maxY, mapWidth, mapHeight, 3, startX, startY, endX, endY);
        if (!(startX == 0 && endX == mapWidth - 1 && startY == 0 && endY == mapHeight - 1))
        {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "visible range not full map on extreme zoom: x=[%d,%d] y=[%d,%d]",
                startX, endX, startY, endY);
            return check(false, buf);
        }
    }

    // 视口完全在地图某一侧时，clampVisibleTileRange 应得到空范围（startX > endX），
    // 不能因重置为 0 而渲染整图。
    {
        const int mapWidth = 50;
        const int mapHeight = 50;
        struct Case { const char* name; int minX, minY, maxX, maxY; };
        // 估算范围全部在地图右侧 / 下侧 / 左侧 / 上侧。
        const Case cases[] = {
            { "right",  1000, 10,   1100, 20 },
            { "bottom",    10, 1000, 20, 1100 },
            { "left",   -1000, 10,  -100, 20 },
            { "top",       10,-1000, 20, -100 }
        };
        for (const Case& c : cases)
        {
            int startX, startY, endX, endY;
            MapCoordinateTransform::clampVisibleTileRange(
                c.minX, c.minY, c.maxX, c.maxY, mapWidth, mapHeight, 3,
                startX, startY, endX, endY);
            bool emptyX = startX > endX;
            bool emptyY = startY > endY;
            if (!(emptyX || emptyY))
            {
                char buf[256];
                std::snprintf(buf, sizeof(buf),
                    "visible range not empty when viewport off-map %s: x=[%d,%d] y=[%d,%d]",
                    c.name, startX, endX, startY, endY);
                return check(false, buf);
            }
        }
    }

    return check(true, "MapCoordinateTransform round-trip");
}

bool testEmptySparseMinimapAndLegacyMapZoomToFit()
{
    std::vector<uint8_t> buffer = makeEmptyMapBuffer(14, 60);
    MapFileEditor editor;
    if (!check(editor.loadFromBuffer(buffer.data(), buffer.size()),
               "load tall empty minimap fixture"))
    {
        return false;
    }

    MinimapWidget minimap;
    minimap.resize(320, 240);
    minimap.setMapFileEditor(&editor);
    minimap.refreshMinimap();
    const QPoint representativeTiles[] = {
        {0, 0}, {13, 0}, {0, 59}, {13, 59}, {7, 30}
    };
    bool ok = true;
    auto verifyMinimap = [&](const char* state)
    {
        for (const QPoint& tile : representativeTiles)
        {
            QPoint widgetPoint = minimap.tileCenterToWidgetPosition(tile.x(), tile.y());
            QPoint resolved = minimap.widgetPositionToTile(widgetPoint);
            if (!minimap.rect().adjusted(-1, -1, 1, 1).contains(widgetPoint) ||
                resolved != tile)
            {
                std::cerr << "FAILED: " << state << " minimap tile ("
                          << tile.x() << ',' << tile.y() << ") -> widget ("
                          << widgetPoint.x() << ',' << widgetPoint.y()
                          << ") -> tile (" << resolved.x() << ',' << resolved.y()
                          << ")\n";
                return false;
            }
        }
        return true;
    };
    ok = verifyMinimap("empty") && ok;

    MapTileData sparseTile = editor.getTile(7, 30);
    sparseTile.layer[0].mpc = 1; // Deliberately unresolved: exercises placeholder geometry.
    editor.setTile(7, 30, sparseTile);
    minimap.refreshMinimap();
    ok = verifyMinimap("sparse") && ok;

    QTemporaryDir minimapAssets;
    if (!check(minimapAssets.isValid(), "create minimap art assets directory"))
        return false;
    QDir minimapAssetsRoot(minimapAssets.path());
    if (!check(minimapAssetsRoot.mkpath("mpc"), "create minimap art MPC directory"))
        return false;
    const QColor minimapArtColor(220, 35, 25, 255);
    const std::vector<uint8_t> minimapMpc = makeSinglePixelMpc(minimapArtColor);
    const QString minimapMpcPath = minimapAssetsRoot.filePath("mpc/tile.mpc");
    if (!check(
            Util::writeFileFromBuffer(
                minimapMpcPath.toUtf8().toStdString(),
                minimapMpc.data(),
                minimapMpc.size()),
            "write repeated minimap art fixture"))
    {
        return false;
    }

    editor.setMpcPath("mpc");
    MpcInfoData minimapMpcInfo;
    minimapMpcInfo.name = "tile.mpc";
    editor.setMpcInfo(0, minimapMpcInfo);
    for (int y = 0; y < editor.getHeight(); ++y)
    {
        for (int x = 0; x < editor.getWidth(); ++x)
        {
            MapTileData tile = editor.getTile(x, y);
            tile.layer[0].mpc = 1;
            tile.layer[0].frame = 0;
            editor.setTile(x, y, tile);
        }
    }

    MpcImageCache minimapCache;
    minimapCache.setAssetsBasePath(
        minimapAssetsRoot.absolutePath().toUtf8().toStdString());
    minimap.setMpcImageCache(&minimapCache);
    minimap.refreshMinimap();
    const QImage renderedMinimap = minimap.grab().toImage().convertToFormat(
        QImage::Format_ARGB32);
    bool foundMinimapArt = false;
    for (int y = 0; y < renderedMinimap.height() && !foundMinimapArt; ++y)
    {
        for (int x = 0; x < renderedMinimap.width(); ++x)
        {
            const QColor pixel = QColor::fromRgba(renderedMinimap.pixel(x, y));
            if (pixel.red() > 170 && pixel.green() < 90 && pixel.blue() < 90)
            {
                foundMinimapArt = true;
                break;
            }
        }
    }
    ok = check(
        foundMinimapArt,
        "repeated minimap MPC tiles render cached resource art") && ok;

    MapRenderCanvas canvas;
    canvas.resize(800, 600);
    canvas.setMapFileEditor(&editor);
    canvas.zoomToFit();
    for (const QPoint& tile : representativeTiles)
    {
        QPoint center = canvas.tileToScreenCenter(tile.x(), tile.y());
        ok = check(canvas.rect().contains(center),
                   "zoom-to-fit keeps tall-map representative tile in viewport") && ok;
    }

    std::vector<uint8_t> wideBuffer = makeEmptyMapBuffer(512, 16);
    MapFileEditor wideEditor;
    if (!check(wideEditor.loadFromBuffer(wideBuffer.data(), wideBuffer.size()),
               "load wide legacy zoom fixture"))
    {
        return false;
    }
    canvas.setMapFileEditor(&wideEditor);
    canvas.zoomToFit();
    float fittedZoom = canvas.getZoomLevel();
    ok = check(fittedZoom < 0.125f && fittedZoom >= 0.01f,
               "wide legacy map fits below historical interactive zoom floor") && ok;
    canvas.zoomAtPoint(QPoint(400, 300), fittedZoom * 0.8f);
    ok = check(canvas.getZoomLevel() <= fittedZoom,
               "interactive zoom remains smooth after very small zoom-to-fit") && ok;

    std::vector<uint8_t> largeBuffer = makeEmptyMapBuffer(512, 512);
    MapFileEditor largeEditor;
    if (!check(largeEditor.loadFromBuffer(largeBuffer.data(), largeBuffer.size()),
               "load large empty minimap fixture"))
    {
        return false;
    }
    MinimapWidget largeMinimap;
    largeMinimap.resize(320, 240);
    largeMinimap.setMapFileEditor(&largeEditor);
    QSize backingSize = largeMinimap.backingImageSize();
    ok = check(backingSize.width() <= MinimapWidget::MAX_BACKING_EDGE &&
                   backingSize.height() <= MinimapWidget::MAX_BACKING_EDGE &&
                   (int64_t)backingSize.width() * backingSize.height() <=
                       (int64_t)MinimapWidget::MAX_BACKING_EDGE *
                           MinimapWidget::MAX_BACKING_EDGE,
               "large minimap keeps bounded backing image") && ok;

    MapTileData largeSparseTile = largeEditor.getTile(256, 256);
    largeSparseTile.obstacle = 0x81;
    largeSparseTile.trap = 7;
    largeEditor.setTile(256, 256, largeSparseTile);
    largeMinimap.refreshMinimap();
    QSize sparseBackingSize = largeMinimap.backingImageSize();
    ok = check(sparseBackingSize == backingSize,
               "large sparse edit keeps stable bounded minimap geometry") && ok;
    const QPoint largeRepresentativeTiles[] = {
        {0, 0}, {511, 0}, {0, 511}, {511, 511}, {256, 256}
    };
    for (const QPoint& tile : largeRepresentativeTiles)
    {
        QPoint widgetPoint = largeMinimap.tileCenterToWidgetPosition(tile.x(), tile.y());
        QPoint resolved = largeMinimap.widgetPositionToTile(widgetPoint);
        // At 512 rows the aspect-preserving 320x240 widget has roughly six
        // source rows per physical pixel.  The inverse must therefore select
        // the visible pixel's nearest tile; exact per-row addressing is not
        // physically representable at this size.
        if (!largeMinimap.rect().adjusted(-1, -1, 1, 1).contains(widgetPoint) ||
            std::abs(resolved.x() - tile.x()) > 1 ||
            std::abs(resolved.y() - tile.y()) > 4)
        {
            std::cerr << "FAILED: bounded minimap tile (" << tile.x() << ',' << tile.y()
                      << ") -> widget (" << widgetPoint.x() << ',' << widgetPoint.y()
                      << ") -> tile (" << resolved.x() << ',' << resolved.y() << ")\n";
            ok = false;
        }
    }
    return ok;
}

bool testMapPastePreviewRendersRealPixelsOffscreen()
{
    // 需求5：地图画布粘贴预览必须绘制真正会写入的半透明 MPC 帧，而不是只画轮廓。
    // 通过离屏渲染（QWidget::render 到 QPixmap）比较预览像素与实际粘贴像素，
    // 证明图像、图层过滤和坐标正确。
    QTemporaryDir tempDir;
    if (!check(tempDir.isValid(), "create paste preview offscreen temp assets dir"))
        return false;

    QDir root(tempDir.path());
    if (!check(root.mkpath("mpc/map"), "create paste preview mpc/map fixture dir"))
        return false;

    // 创建有显著颜色差异且会跨 Tile 重叠的 MPC 帧。
    auto makeSolidMpc = [](const QColor& color) {
        MPCFileHead head = {};
        std::memcpy(head.head, "MPC File Ver2.0", 16);
        head.picCount = 1;
        head.paletteLen = 256;
        std::vector<uint8_t> buffer;
        appendValue(buffer, head);
        std::vector<ColorARGB> palette(256);
        palette[1] = {
            static_cast<uint8_t>(color.blue()),
            static_cast<uint8_t>(color.green()),
            static_cast<uint8_t>(color.red()),
            static_cast<uint8_t>(color.alpha())
        };
        const uint8_t* paletteBytes = reinterpret_cast<const uint8_t*>(palette.data());
        buffer.insert(buffer.end(), paletteBytes, paletteBytes + palette.size() * sizeof(ColorARGB));
        int32_t frameOffset = 0;
        appendValue(buffer, frameOffset);
        const int width = 128;
        const int height = 64;
        std::vector<uint8_t> frameData;
        int remaining = width * height;
        while (remaining > 0)
        {
            int run = std::min(128, remaining);
            frameData.push_back(static_cast<uint8_t>(run));
            for (int i = 0; i < run; i++)
                frameData.push_back(1);
            remaining -= run;
        }
        MPCPicHead picHead = {};
        picHead.dataLen = static_cast<int32_t>(sizeof(MPCPicHead) + frameData.size());
        picHead.width = width;
        picHead.height = height;
        appendValue(buffer, picHead);
        buffer.insert(buffer.end(), frameData.begin(), frameData.end());
        return buffer;
    };

    QColor groundColor(255, 40, 40, 255);   // 红色（地面层）
    QColor buildingColor(40, 255, 40, 255);  // 绿色（建筑层）
    std::vector<uint8_t> groundMpc = makeSolidMpc(groundColor);
    std::vector<uint8_t> buildingMpc = makeSolidMpc(buildingColor);

    QString groundPath = root.filePath("mpc/map/ground.mpc");
    QString buildingPath = root.filePath("mpc/map/building.mpc");
    if (!check(Util::writeFileFromBuffer(groundPath.toUtf8().toStdString(), groundMpc.data(), groundMpc.size()),
               "write paste preview ground MPC fixture") ||
        !check(Util::writeFileFromBuffer(buildingPath.toUtf8().toStdString(), buildingMpc.data(), buildingMpc.size()),
               "write paste preview building MPC fixture"))
    {
        return false;
    }

    std::vector<uint8_t> buffer = makeEmptyMapBuffer(20, 20);
    MapFileEditor editor;
    if (!check(editor.loadFromBuffer(buffer.data(), buffer.size()), "load paste preview offscreen test map"))
        return false;

    // 设置 MPC 路径为 mpc/map（相对 assets 根），MPC 信息表槽位 0=ground, 1=building。
    editor.setMpcPath("mpc/map");
    MpcInfoData groundInfo;
    groundInfo.name = "ground.mpc";
    editor.setMpcInfo(0, groundInfo);
    MpcInfoData buildingInfo;
    buildingInfo.name = "building.mpc";
    editor.setMpcInfo(1, buildingInfo);

    // 源瓦片：layer[0]=ground(slot0, mpc=1)，layer[1]=building(slot1, mpc=2)。
    MapTileData src;
    src.layer[0].mpc = 1;
    src.layer[0].frame = 0;
    src.layer[1].mpc = 2;
    src.layer[1].frame = 0;
    editor.setTile(2, 2, src);

    MpcImageCache cache;
    cache.setAssetsBasePath(root.absolutePath().toUtf8().toStdString());

    MapRenderCanvas canvas;
    canvas.setAttribute(Qt::WA_DontShowOnScreen, true);
    canvas.setMapFileEditor(&editor);
    canvas.setMpcImageCache(&cache);
    canvas.setPaintAllLayers(false);
    canvas.setPaintLayer(0);  // 只预览/粘贴地面层
    QApplication::processEvents();

    // 复制源瓦片（菱形，单瓦片）。
    canvas.copyArea(2, 2, 2, 2, AreaSelectionShape::Diamond);
    // 模拟右键/鼠标移动到目标瓦片 (10,10)：预览可见且悬停在有效瓦片。
    canvas.setHoverTileForTest(10, 10);
    canvas.setPastePreviewVisibleForTest(true);
    canvas.centerOnTile(10, 10);
    canvas.show();
    QApplication::processEvents();

    // 离屏渲染：grab 把整个 widget 画到 pixmap。
    QPixmap previewPixmap(canvas.size());
    previewPixmap.fill(Qt::black);
    canvas.render(&previewPixmap);

    // 提取目标瓦片 (10,10) 屏幕中心区域的像素。
    QPoint targetCenter = canvas.tileToScreenCenter(10, 10);
    QRgb centerPixel = previewPixmap.toImage().pixel(targetCenter);
    bool ok = check(qRed(centerPixel) > 100 && qGreen(centerPixel) < 100,
                   "paste preview offscreen renders ground layer red frame at target");

    // 图层过滤：切换到建筑层，预览像素应变成绿色，不再是红色。
    canvas.setPaintLayer(1);
    QApplication::processEvents();
    QPixmap previewPixmap2(canvas.size());
    previewPixmap2.fill(Qt::black);
    canvas.render(&previewPixmap2);
    QRgb centerPixel2 = previewPixmap2.toImage().pixel(targetCenter);
    ok = check(qGreen(centerPixel2) > 100 && qRed(centerPixel2) < 100,
              "paste preview offscreen reflects current paint layer filter (building green)") && ok;

    // 实际粘贴后目标瓦片应有对应层内容，验证预览坐标与粘贴一致。
    canvas.pasteArea(10, 10);
    MapTileData pasted = editor.getTile(10, 10);
    // 注意：pasteArea 用粘贴时的绘制图层，刚切到 layer 1。
    ok = check(pasted.layer[1].mpc == 2, "paste preview target tile receives pasted layer data") && ok;

    // 全部图层、多 Tile 重叠顺序：前一个 Tile 的空中层（红）必须盖住后一个 Tile
    // 的地面层（绿）。若按 Tile 依次画 0/1/2 层，结果会错误地变成绿色。
    MapTileData skySource;
    skySource.layer[2].mpc = 1;
    editor.setTile(2, 2, skySource);
    MapTileData laterGroundSource;
    laterGroundSource.layer[0].mpc = 2;
    editor.setTile(3, 2, laterGroundSource);
    canvas.setPaintAllLayers(true);
    canvas.copyArea(2, 2, 3, 2, AreaSelectionShape::Rectangle);
    canvas.setHoverTileForTest(10, 10);
    canvas.setPastePreviewVisibleForTest(true);
    QApplication::processEvents();
    QPixmap layeredPreview(canvas.size());
    layeredPreview.fill(Qt::black);
    canvas.render(&layeredPreview);
    QPoint firstCenter = canvas.tileToScreenCenter(10, 10);
    QPoint secondCenter = canvas.tileToScreenCenter(11, 10);
    // 向上偏移，避开两个 Tile 菱形轮廓在中心高度处的共享绿色边线。
    QPoint overlapPoint((firstCenter.x() + secondCenter.x()) / 2, firstCenter.y() - 20);
    QRgb overlapPixel = layeredPreview.toImage().pixel(overlapPoint);
    ok = check(qRed(overlapPixel) > qGreen(overlapPixel),
               QString("paste preview all-layers uses global layer order across overlapping tiles (rgb=%1,%2,%3)")
                   .arg(qRed(overlapPixel)).arg(qGreen(overlapPixel)).arg(qBlue(overlapPixel))
                   .toUtf8().constData()) && ok;

    canvas.close();
    QApplication::processEvents();
    return ok;
}

bool testMinimapClickAndDragCenterCanvasOnTargetTiles()
{
    std::vector<uint8_t> buffer = makeEmptyMapBuffer(8, 8);
    MapFileEditor editor;
    if (!check(editor.loadFromBuffer(buffer.data(), buffer.size()), "load minimap click test map"))
        return false;

    for (int y = 0; y < editor.getHeight(); ++y)
    {
        for (int x = 0; x < editor.getWidth(); ++x)
        {
            MapTileData tile = editor.getTile(x, y);
            tile.layer[0].mpc = 1;
            editor.setTile(x, y, tile);
        }
    }

    MapRenderCanvas canvas;
    canvas.setAttribute(Qt::WA_DontShowOnScreen, true);
    canvas.resize(420, 320);
    canvas.setMapFileEditor(&editor);
    canvas.show();

    MinimapWidget minimap;
    minimap.setAttribute(Qt::WA_DontShowOnScreen, true);
    minimap.resize(240, 180);
    minimap.setMapFileEditor(&editor);
    minimap.setCanvas(&canvas);
    minimap.show();
    minimap.refreshMinimap();
    QApplication::processEvents();
    QPixmap forcedPaint = minimap.grab();
    Q_UNUSED(forcedPaint);

    QPoint firstPoint = minimap.tileCenterToWidgetPosition(6, 6);
    QPoint firstTile = minimap.widgetPositionToTile(firstPoint);
    bool ok = check(minimap.rect().contains(firstPoint),
                    "minimap target tile point is inside widget") &&
        check(firstTile == QPoint(6, 6),
              "minimap tile center maps back to target tile");

    canvas.centerOnTile(0, 0);
    QApplication::processEvents();
    sendWidgetMouseEvent(minimap, QEvent::MouseButtonPress, firstPoint, Qt::LeftButton, Qt::LeftButton);
    sendWidgetMouseEvent(minimap, QEvent::MouseButtonRelease, firstPoint, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();

    QPoint centeredTile = canvas.screenToTile(QPoint(canvas.width() / 2, canvas.height() / 2));
    ok = check(centeredTile == QPoint(6, 6),
               "minimap click centers canvas on clicked tile") && ok;

    QPoint secondPoint = minimap.tileCenterToWidgetPosition(2, 3);
    QPoint secondTile = minimap.widgetPositionToTile(secondPoint);
    ok = check(secondTile == QPoint(2, 3),
               "minimap second tile center maps back to target tile") && ok;

    sendWidgetMouseEvent(minimap, QEvent::MouseButtonPress, firstPoint, Qt::LeftButton, Qt::LeftButton);
    sendWidgetMouseEvent(minimap, QEvent::MouseMove, secondPoint, Qt::NoButton, Qt::LeftButton);
    sendWidgetMouseEvent(minimap, QEvent::MouseButtonRelease, secondPoint, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();

    centeredTile = canvas.screenToTile(QPoint(canvas.width() / 2, canvas.height() / 2));
    ok = check(centeredTile == QPoint(2, 3),
               "minimap drag recenters canvas on dragged tile") && ok;

    minimap.close();
    canvas.close();
    QApplication::processEvents();
    return ok;
}

bool testMapBrushPreviewRendersHoverImage()
{
    QTemporaryDir tempDir;
    if (!check(tempDir.isValid(), "create brush preview temp dir"))
        return false;
    QDir root(tempDir.path());
    if (!check(root.mkpath("mpc/map"), "create brush preview mpc/map dir"))
        return false;

    auto makeSolidMpc = [](const QColor& color, int width, int height) {
        MPCFileHead head = {};
        std::memcpy(head.head, "MPC File Ver2.0", 16);
        head.picCount = 1;
        head.paletteLen = 256;
        std::vector<uint8_t> buffer;
        appendValue(buffer, head);
        std::vector<ColorARGB> palette(256);
        palette[1] = {
            static_cast<uint8_t>(color.blue()),
            static_cast<uint8_t>(color.green()),
            static_cast<uint8_t>(color.red()),
            static_cast<uint8_t>(color.alpha())
        };
        const uint8_t* paletteBytes = reinterpret_cast<const uint8_t*>(palette.data());
        buffer.insert(buffer.end(), paletteBytes, paletteBytes + palette.size() * sizeof(ColorARGB));
        int32_t frameOffset = 0;
        appendValue(buffer, frameOffset);
        std::vector<uint8_t> frameData;
        int remaining = width * height;
        while (remaining > 0)
        {
            int run = std::min(128, remaining);
            frameData.push_back(static_cast<uint8_t>(run));
            for (int i = 0; i < run; i++) frameData.push_back(1);
            remaining -= run;
        }
        MPCPicHead picHead = {};
        picHead.dataLen = static_cast<int32_t>(sizeof(MPCPicHead) + frameData.size());
        picHead.width = width;
        picHead.height = height;
        appendValue(buffer, picHead);
        buffer.insert(buffer.end(), frameData.begin(), frameData.end());
        return buffer;
    };

    // 地面=红，建筑=绿，空中=蓝，尺寸 64x32（与 tile 相同，便于落在菱形内）。
    QColor groundColor(255, 40, 40, 255);
    QColor buildingColor(40, 255, 40, 255);
    QColor skyColor(40, 40, 255, 255);
    auto groundMpc = makeSolidMpc(groundColor, 64, 32);
    auto buildingMpc = makeSolidMpc(buildingColor, 64, 32);
    auto skyMpc = makeSolidMpc(skyColor, 64, 32);
    QString groundPath = root.filePath("mpc/map/ground.mpc");
    QString buildingPath = root.filePath("mpc/map/building.mpc");
    QString skyPath = root.filePath("mpc/map/sky.mpc");
    if (!check(Util::writeFileFromBuffer(groundPath.toUtf8().toStdString(), groundMpc.data(), groundMpc.size()),
               "write brush preview ground MPC fixture") ||
        !check(Util::writeFileFromBuffer(buildingPath.toUtf8().toStdString(), buildingMpc.data(), buildingMpc.size()),
               "write brush preview building MPC fixture") ||
        !check(Util::writeFileFromBuffer(skyPath.toUtf8().toStdString(), skyMpc.data(), skyMpc.size()),
               "write brush preview sky MPC fixture"))
    {
        return false;
    }

    std::vector<uint8_t> buffer = makeEmptyMapBuffer(12, 12);
    MapFileEditor editor;
    if (!check(editor.loadFromBuffer(buffer.data(), buffer.size()), "load brush preview test map"))
        return false;
    editor.setMpcPath("mpc/map");
    MpcInfoData groundInfo;
    groundInfo.name = "ground.mpc";
    editor.setMpcInfo(0, groundInfo);
    MpcInfoData buildingInfo;
    buildingInfo.name = "building.mpc";
    editor.setMpcInfo(1, buildingInfo);
    MpcInfoData skyInfo;
    skyInfo.name = "sky.mpc";
    editor.setMpcInfo(2, skyInfo);

    MpcImageCache cache;
    cache.setAssetsBasePath(root.absolutePath().toUtf8().toStdString());

    MapRenderCanvas canvas;
    canvas.setAttribute(Qt::WA_DontShowOnScreen, true);
    canvas.resize(640, 480);
    canvas.setMapFileEditor(&editor);
    canvas.setMpcImageCache(&cache);
    canvas.setEditTool(MapEditTool::TilePaint);
    canvas.centerOnTile(6, 6);
    canvas.show();
    QApplication::processEvents();

    bool ok = true;

    auto renderHover = [&](int tileX, int tileY) {
        canvas.setHoverTileForTest(tileX, tileY);
        // 清空剪贴板与粘贴预览，确保走 drawBrushPreview 分支。
        canvas.clearClipboard();
        canvas.setPastePreviewVisibleForTest(false);
        QApplication::processEvents();
        QPixmap pm(canvas.size());
        pm.fill(Qt::black);
        canvas.render(&pm);
        return pm.toImage();
    };

    // 1) 单图层画笔 hover：地面层=红。目标瓦片中心应渲染出红色（半透明混合后仍偏红）。
    canvas.setPaintAllLayers(false);
    canvas.setPaintLayer(0);
    canvas.setPaintMpcIndex(1);  // ground slot0 stored=1
    canvas.setPaintFrameIndex(0);
    canvas.centerOnTile(6, 6);
    QApplication::processEvents();
    QImage singleImage = renderHover(6, 6);
    QPoint singleCenter = canvas.tileToScreenCenter(6, 6);
    QRgb singlePixel = singleImage.pixel(singleCenter);
    ok = check(qRed(singlePixel) > qGreen(singlePixel) && qRed(singlePixel) > qBlue(singlePixel),
               QString("brush preview single-layer hover renders ground red (rgb=%1,%2,%3)")
                   .arg(qRed(singlePixel)).arg(qGreen(singlePixel)).arg(qBlue(singlePixel))) && ok;

    // 2) 全部图层 + 多层画笔 hover：三层都画，顺序 0/1/2，空中层(蓝)在最上。
    //    中心像素应为蓝色（不是只显示最高非空层意义上的“红”或“绿”）。
    MapTileData brushTile;
    brushTile.layer[0].mpc = 1;
    brushTile.layer[0].frame = 0;
    brushTile.layer[1].mpc = 2;
    brushTile.layer[1].frame = 0;
    brushTile.layer[2].mpc = 3;
    brushTile.layer[2].frame = 0;
    canvas.setPaintAllLayers(true);
    canvas.setMultiLayerPaintBrush(brushTile, true);
    QApplication::processEvents();
    QImage multiImage = renderHover(6, 6);
    QRgb multiPixel = multiImage.pixel(singleCenter);
    ok = check(qBlue(multiPixel) > qRed(multiPixel) && qBlue(multiPixel) > qGreen(multiPixel),
               QString("brush preview all-layers multi-layer hover renders three layers with sky on top (rgb=%1,%2,%3)")
                   .arg(qRed(multiPixel)).arg(qGreen(multiPixel)).arg(qBlue(multiPixel))) && ok;
    // 关键：必须不是只显示最高层（蓝）也不仅仅是地面层（红）——三层叠画后空中层在上。
    // 上面已验证蓝>红、蓝>绿；额外确认它确实不同于“只画地面红”的单层预览。
    ok = check(qBlue(multiPixel) != qBlue(singlePixel) || qRed(multiPixel) != qRed(singlePixel),
               "brush preview all-layers multi-layer differs from single-layer ground preview") && ok;

    // 3) 全部图层 + 标量画笔（无多层画笔）：只画一次当前 mpc/frame，不应叠画三层过暗。
    canvas.setMultiLayerPaintBrush(brushTile, false);
    canvas.setPaintAllLayers(true);
    canvas.setPaintMpcIndex(1);  // 触发清除多层状态，回到标量地面红
    canvas.setPaintFrameIndex(0);
    QApplication::processEvents();
    QImage scalarImage = renderHover(6, 6);
    QRgb scalarPixel = scalarImage.pixel(singleCenter);
    ok = check(qRed(scalarPixel) > qGreen(scalarPixel) && qRed(scalarPixel) > qBlue(scalarPixel),
               QString("brush preview all-layers scalar renders single frame not triple-stacked (rgb=%1,%2,%3)")
                   .arg(qRed(scalarPixel)).arg(qGreen(scalarPixel)).arg(qBlue(scalarPixel))) && ok;

    canvas.close();
    QApplication::processEvents();
    return ok;
}

bool testMapEdgeRenderShowsTallImageAtViewportEdge()
{
    QTemporaryDir tempDir;
    if (!check(tempDir.isValid(), "create edge render temp dir"))
        return false;
    QDir root(tempDir.path());
    if (!check(root.mkpath("mpc/map"), "create edge render mpc/map dir"))
        return false;

    // 极高图：宽 64，高 256（远超 tile 高度 32）。从 tile 锚点向上扩展很多。
    auto makeTallMpc = [](const QColor& color) {
        MPCFileHead head = {};
        std::memcpy(head.head, "MPC File Ver2.0", 16);
        head.picCount = 1;
        head.paletteLen = 256;
        std::vector<uint8_t> buffer;
        appendValue(buffer, head);
        std::vector<ColorARGB> palette(256);
        palette[1] = {
            static_cast<uint8_t>(color.blue()),
            static_cast<uint8_t>(color.green()),
            static_cast<uint8_t>(color.red()),
            static_cast<uint8_t>(color.alpha())
        };
        const uint8_t* paletteBytes = reinterpret_cast<const uint8_t*>(palette.data());
        buffer.insert(buffer.end(), paletteBytes, paletteBytes + palette.size() * sizeof(ColorARGB));
        int32_t frameOffset = 0;
        appendValue(buffer, frameOffset);
        const int width = 64;
        const int height = 256;
        std::vector<uint8_t> frameData;
        int remaining = width * height;
        while (remaining > 0)
        {
            int run = std::min(128, remaining);
            frameData.push_back(static_cast<uint8_t>(run));
            for (int i = 0; i < run; i++) frameData.push_back(1);
            remaining -= run;
        }
        MPCPicHead picHead = {};
        picHead.dataLen = static_cast<int32_t>(sizeof(MPCPicHead) + frameData.size());
        picHead.width = width;
        picHead.height = height;
        appendValue(buffer, picHead);
        buffer.insert(buffer.end(), frameData.begin(), frameData.end());
        return buffer;
    };

    QColor tallColor(255, 200, 0, 255);
    auto tallMpc = makeTallMpc(tallColor);
    QString tallPath = root.filePath("mpc/map/tall.mpc");
    if (!check(Util::writeFileFromBuffer(tallPath.toUtf8().toStdString(), tallMpc.data(), tallMpc.size()),
               "write edge render tall MPC fixture"))
    {
        return false;
    }

    std::vector<uint8_t> buffer = makeEmptyMapBuffer(20, 20);
    MapFileEditor editor;
    if (!check(editor.loadFromBuffer(buffer.data(), buffer.size()), "load edge render test map"))
        return false;
    editor.setMpcPath("mpc/map");
    MpcInfoData tallInfo;
    tallInfo.name = "tall.mpc";
    editor.setMpcInfo(0, tallInfo);

    MpcImageCache cache;
    cache.setAssetsBasePath(root.absolutePath().toUtf8().toStdString());

    MapRenderCanvas canvas;
    canvas.setAttribute(Qt::WA_DontShowOnScreen, true);
    canvas.resize(640, 480);
    canvas.setMapFileEditor(&editor);
    canvas.setMpcImageCache(&cache);
    canvas.show();
    QApplication::processEvents();

    bool ok = true;

    // 在多个 tile 上放极高图，确保至少有一个落在靠近视口顶部的位置。
    MapTileData tallTile;
    tallTile.layer[0].mpc = 1;
    tallTile.layer[0].frame = 0;
    for (int x = 0; x < 20; x++)
        for (int y = 0; y < 20; y++)
            editor.setTile(x, y, tallTile);

    // 居中到地图中部，极高图从各 tile 锚点向上扩展 224 像素。在 zoom=1 下，
    // 屏幕顶部一行的 tile 的图像顶端应伸入屏幕外但接近顶部边缘的 tile 图像应可见。
    canvas.centerOnTile(10, 10);
    canvas.zoomAtPoint(canvas.rect().center(), 1.0f);
    QApplication::processEvents();

    // 离屏渲染整张画布。
    QPixmap pm(canvas.size());
    pm.fill(QColor(30, 30, 30));
    canvas.render(&pm);
    QImage img = pm.toImage();

    // 统计画面上接近 tallColor 的像素数量。极高图覆盖大量像素，
    // 若边缘裁剪过窄，可见的高图像素会显著偏少。这里用阈值断言：
    // 在整张 640x480 视口内，黄色像素应超过一定数量（高图未被整体裁掉）。
    int tallPixelCount = 0;
    int sampleStep = 4;  // 4 像素步长采样，降低开销
    for (int y = 0; y < img.height(); y += sampleStep)
    {
        for (int x = 0; x < img.width(); x += sampleStep)
        {
            QRgb px = img.pixel(x, y);
            if (qRed(px) > 180 && qGreen(px) > 140 && qBlue(px) < 80)
                tallPixelCount++;
        }
    }
    // 640x480 / (4x4) = 19200 个采样点；极高图在视口中应覆盖相当比例。
    // 旧实现 margin=3 会漏掉顶部/底部边缘高图，可见像素大幅减少。
    ok = check(tallPixelCount > 200,
               QString("edge render shows tall image pixels across viewport (sampled=%1)")
                   .arg(tallPixelCount)) && ok;

    // 更直接：断言视口顶部 1/4 区域内也能采到高图像素（证明顶部边缘未被裁掉）。
    int topQuarterCount = 0;
    int topBandHeight = img.height() / 4;
    for (int y = 0; y < topBandHeight; y += sampleStep)
    {
        for (int x = 0; x < img.width(); x += sampleStep)
        {
            QRgb px = img.pixel(x, y);
            if (qRed(px) > 180 && qGreen(px) > 140 && qBlue(px) < 80)
                topQuarterCount++;
        }
    }
    ok = check(topQuarterCount > 0,
               QString("edge render shows tall image pixels in top quarter band (sampled=%1)")
                   .arg(topQuarterCount)) && ok;

#ifdef JXQY_EDITOR_BUILD_BENCHMARK
    const QVariant previousPerformanceMode = qApp->property(
        "editorPerformanceBenchmark");
    qApp->setProperty("editorPerformanceBenchmark", true);
    canvas.invalidateRenderRangeCache();
    QPixmap initialScalePaint(canvas.size());
    initialScalePaint.fill(QColor(30, 30, 30));
    canvas.render(&initialScalePaint);
    ok = check(
        canvas.property("performanceScaledFrameCacheMisses").toInt() == 1,
        "repeated map tiles build one visual and prewarm adjacent zooms") && ok;

    canvas.zoomAtPoint(canvas.rect().center(), 0.8f);
    QPixmap zoomedOutPaint(canvas.size());
    zoomedOutPaint.fill(QColor(30, 30, 30));
    canvas.render(&zoomedOutPaint);
    ok = check(
        canvas.property("performanceScaledFrameCacheMisses").toInt() == 0,
        "wheel zoom-out reuses the prewarmed scaled frame") && ok;

    canvas.zoomAtPoint(canvas.rect().center(), 1.0f);
    canvas.zoomAtPoint(canvas.rect().center(), 1.25f);
    QPixmap zoomedInPaint(canvas.size());
    zoomedInPaint.fill(QColor(30, 30, 30));
    canvas.render(&zoomedInPaint);
    ok = check(
        canvas.property("performanceScaledFrameCacheMisses").toInt() == 0,
        "wheel zoom-in reuses the prewarmed scaled frame") && ok;
    qApp->setProperty("editorPerformanceBenchmark", previousPerformanceMode);
#endif

    canvas.close();
    QApplication::processEvents();
    return ok;
}

bool testMapRenderRangeCacheInvalidation()
{
    std::vector<uint8_t> buffer = makeEmptyMapBuffer(8, 8);
    MapFileEditor editor;
    if (!check(editor.loadFromBuffer(buffer.data(), buffer.size()),
               "load render range cache test map"))
        return false;

    MapRenderCanvas canvas;
    canvas.setAttribute(Qt::WA_DontShowOnScreen, true);
    canvas.resize(600, 500);
    canvas.setMapFileEditor(&editor);
    canvas.show();
    QApplication::processEvents();

    bool ok = true;

    // setMapFileEditor 后缓存应失效，getVisibleTileRenderRange 应正常返回。
    int startX, startY, endX, endY;
    canvas.getVisibleTileRenderRangeForTest(startX, startY, endX, endY);
    ok = check(startX <= endX && startY <= endY,
               "render range valid after setMapFileEditor") && ok;

    // 手动失效缓存后再次调用应正常。
    canvas.invalidateRenderRangeCache();
    canvas.getVisibleTileRenderRangeForTest(startX, startY, endX, endY);
    ok = check(startX <= endX && startY <= endY,
               "render range valid after invalidateRenderRangeCache") && ok;

    // setMpcImageCache 后缓存应失效。
    MpcImageCache cache;
    canvas.setMpcImageCache(&cache);
    QApplication::processEvents();
    canvas.getVisibleTileRenderRangeForTest(startX, startY, endX, endY);
    ok = check(startX <= endX && startY <= endY,
               "render range valid after setMpcImageCache") && ok;

    canvas.hide();
    return ok;
}

class PaintCountingMapRenderCanvas : public MapRenderCanvas
{
public:
    using MapRenderCanvas::MapRenderCanvas;

    int paintCount() const
    {
        return paintEventCount;
    }

    void resetPaintCount()
    {
        paintEventCount = 0;
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        ++paintEventCount;
        MapRenderCanvas::paintEvent(event);
    }

private:
    int paintEventCount = 0;
};

bool testMapAnimationRefreshIsDemandDriven()
{
    QTemporaryDir tempDir;
    if (!check(tempDir.isValid(), "create demand-driven animation temp dir"))
        return false;
    QDir root(tempDir.path());
    if (!check(root.mkpath("mpc/map"), "create demand-driven animation mpc/map dir"))
        return false;

    std::vector<QImage> frames;
    frames.emplace_back(64, 32, QImage::Format_ARGB32);
    frames[0].fill(qRgba(255, 30, 30, 255));
    frames.emplace_back(64, 32, QImage::Format_ARGB32);
    frames[1].fill(qRgba(30, 255, 30, 255));
    if (!check(writeMpcFileFromImages(root.filePath("mpc/map/animated.mpc"), frames, 0),
               "write demand-driven animation MPC fixture"))
    {
        return false;
    }

    std::vector<uint8_t> buffer = makeEmptyMapBuffer(8, 8);
    MapFileEditor editor;
    if (!check(editor.loadFromBuffer(buffer.data(), buffer.size()),
               "load demand-driven animation map"))
    {
        return false;
    }
    editor.setMpcPath("mpc/map");
    MpcInfoData info;
    info.name = "animated.mpc";
    info.dynamic = 0;
    editor.setMpcInfo(0, info);
    MapTileData tile;
    tile.layer[0].mpc = 1;
    editor.setTile(4, 4, tile);

    MpcImageCache cache;
    cache.setAssetsBasePath(root.absolutePath().toUtf8().toStdString());

    PaintCountingMapRenderCanvas canvas;
    canvas.setAttribute(Qt::WA_DontShowOnScreen, true);
    canvas.resize(640, 480);
    canvas.setMapFileEditor(&editor);
    canvas.setMpcImageCache(&cache);
    canvas.centerOnTile(4, 4);
    canvas.show();
    QApplication::processEvents();

    auto processEventsFor = [](int milliseconds)
    {
        QEventLoop eventLoop;
        QTimer::singleShot(milliseconds, &eventLoop, &QEventLoop::quit);
        eventLoop.exec();
    };

    canvas.resetPaintCount();
    processEventsFor(220);
    bool ok = check(canvas.paintCount() == 0,
                    QString("static multi-frame tile stays idle: paints=%1")
                        .arg(canvas.paintCount()));

    info.dynamic = 1;
    editor.setMpcInfo(0, info);
    canvas.setMapFileEditor(&editor);
    QApplication::processEvents();
    canvas.resetPaintCount();
    processEventsFor(220);
    ok = check(canvas.paintCount() >= 2,
               QString("dynamic tile keeps scheduled animation paints: paints=%1")
                   .arg(canvas.paintCount())) && ok;

    canvas.hide();
    QApplication::processEvents();
    return ok;
}

bool testMapRenderRangeMultiFrameAndRowSpacing()
{
    // 验证两个修复：
    // 1. 多帧 MPC 后续帧比第 0 帧更高时，render range 应按所有帧的最大尺寸外扩，
    //    而非只看第 0 帧。
    // 2. 纵向外扩按 staggered iso 行距 TILE_HEIGHT/2 计算，而非 TILE_HEIGHT。
    //    高图 256px 在 zoom=1 下：旧公式 ceil((256-32)/32)=7 行，
    //    新公式 ceil((256-32)/16)=14 行。
    QTemporaryDir tempDir;
    if (!check(tempDir.isValid(), "create multi-frame render range temp dir"))
        return false;
    QDir root(tempDir.path());
    if (!check(root.mkpath("mpc/map"), "create multi-frame mpc/map dir"))
        return false;

    // 构造 2 帧 MPC：frame 0 = 64x32（普通 tile），frame 1 = 64x256（极高图）。
    std::vector<QImage> frames;
    frames.emplace_back(64, 32, QImage::Format_ARGB32);
    frames[0].fill(qRgba(255, 0, 0, 255));
    frames.emplace_back(64, 256, QImage::Format_ARGB32);
    frames[1].fill(qRgba(255, 200, 0, 255));

    QString mpcPath = root.filePath("mpc/map/multiframe.mpc");
    if (!check(writeMpcFileFromImages(mpcPath, frames, 0),
               "write multi-frame MPC fixture"))
        return false;

    // 创建一个足够大的地图，使 viewport 居中后上下都有足够 tile 可外扩。
    // 40x40 在 640x480 视口下基础可见范围已接近地图边界，会被 clamp 掉大部分外扩。
    constexpr int renderRangeMapSize = 120;
    constexpr int renderRangeCenterTile = 60;
    std::vector<uint8_t> buffer = makeEmptyMapBuffer(renderRangeMapSize, renderRangeMapSize);
    MapFileEditor editor;
    if (!check(editor.loadFromBuffer(buffer.data(), buffer.size()),
               "load multi-frame render range test map"))
        return false;
    editor.setMpcPath("mpc/map");
    MpcInfoData info;
    info.name = "multiframe.mpc";
    editor.setMpcInfo(0, info);

    MpcImageCache cache;
    cache.setAssetsBasePath(root.absolutePath().toUtf8().toStdString());

    MapRenderCanvas canvas;
    canvas.setAttribute(Qt::WA_DontShowOnScreen, true);
    canvas.resize(640, 480);
    canvas.setMapFileEditor(&editor);
    canvas.setMpcImageCache(&cache);
    canvas.show();
    QApplication::processEvents();

    // 居中到地图中部，zoom=1.0。
    canvas.centerOnTile(renderRangeCenterTile, renderRangeCenterTile);
    canvas.zoomAtPoint(canvas.rect().center(), 1.0f);
    QApplication::processEvents();

    bool ok = true;

    // 取基础可见范围（无 MPC 缓存时，只有固定 margin=3）。
    // 先不设 mpcCache，取基础范围。
    MapRenderCanvas baseCanvas;
    baseCanvas.setAttribute(Qt::WA_DontShowOnScreen, true);
    baseCanvas.resize(640, 480);
    baseCanvas.setMapFileEditor(&editor);
    baseCanvas.show();
    QApplication::processEvents();
    baseCanvas.centerOnTile(renderRangeCenterTile, renderRangeCenterTile);
    baseCanvas.zoomAtPoint(baseCanvas.rect().center(), 1.0f);
    QApplication::processEvents();

    int baseStartX, baseStartY, baseEndX, baseEndY;
    baseCanvas.getVisibleTileRangeForTest(baseStartX, baseStartY, baseEndX, baseEndY);
    int baseHeight = baseEndY - baseStartY + 1;

    // 取带高图 MPC 的渲染范围。
    int renderStartX, renderStartY, renderEndX, renderEndY;
    canvas.getVisibleTileRenderRangeForTest(renderStartX, renderStartY, renderEndX, renderEndY);
    int renderHeight = renderEndY - renderStartY + 1;

    // 高图 256px，TILE_HEIGHT=32，zoom=1.0：
    // extraVertical = ceil((256 - 32) / 16) = ceil(224/16) = ceil(14) = 14
    // 旧公式（bug）：ceil(224/32) = 7
    // 渲染范围应比基础范围上下各多出至少 14 行（共 28 行）。
    // 用 renderHeight - baseHeight >= 28 断言新公式生效。
    // 由于 baseCanvas 和 canvas 的 viewport/zoom 设置相同，基础范围应一致。
    int verticalExpansion = renderHeight - baseHeight;
    ok = check(verticalExpansion >= 28,
               QString("render range uses TILE_HEIGHT/2 row spacing: expansion=%1 (need >=28)")
                   .arg(verticalExpansion)) && ok;

    // 旧公式只会产生 expansion=14（上下各 7），如果仍为 7 则说明行距 bug 未修复。
    ok = check(verticalExpansion > 14,
               QString("render range expansion exceeds old TILE_HEIGHT formula: expansion=%1 (>14)")
                   .arg(verticalExpansion)) && ok;

    // 验证多帧：frame 1 是 256px 高，frame 0 只有 32px。
    // 如果只看 frame 0，maxFrameHeight=32，extraVertical=0，renderHeight==baseHeight。
    // 修复后应遍历所有帧，maxFrameHeight=256，extraVertical=14。
    ok = check(renderHeight > baseHeight,
               QString("render range accounts for non-zero frame: render=%1 base=%2")
                   .arg(renderHeight).arg(baseHeight)) && ok;

    baseCanvas.hide();
    canvas.hide();
    return ok;
}

bool testMapRenderRangeCacheInvalidatesOnMpcSlotReplace()
{
    QTemporaryDir tempDir;
    if (!check(tempDir.isValid(), "create slot-replace render range temp dir"))
        return false;
    QDir root(tempDir.path());
    if (!check(root.mkpath("mpc/map"), "create slot-replace mpc/map dir"))
        return false;

    // 短图 MPC：64x32（普通 tile 高度）。
    {
        std::vector<QImage> frames;
        frames.emplace_back(64, 32, QImage::Format_ARGB32);
        frames[0].fill(qRgba(0, 0, 255, 255));
        if (!check(writeMpcFileFromImages(root.filePath("mpc/map/short.mpc"), frames, 0),
                   "write short MPC fixture"))
            return false;
    }
    // 高图 MPC：64x256（远高于 tile），同样占一个槽位。
    {
        std::vector<QImage> frames;
        frames.emplace_back(64, 256, QImage::Format_ARGB32);
        frames[0].fill(qRgba(255, 200, 0, 255));
        if (!check(writeMpcFileFromImages(root.filePath("mpc/map/tall.mpc"), frames, 0),
                   "write tall MPC fixture"))
            return false;
    }

    // 足够大的地图，使居中后上下都有足够 tile 可外扩。
    constexpr int slotReplaceMapSize = 120;
    constexpr int slotReplaceCenterTile = 60;
    std::vector<uint8_t> buffer = makeEmptyMapBuffer(slotReplaceMapSize, slotReplaceMapSize);
    MapFileEditor editor;
    if (!check(editor.loadFromBuffer(buffer.data(), buffer.size()),
               "load slot-replace render range test map"))
        return false;
    editor.setMpcPath("mpc/map");

    // 槽位 0 = 短图。
    MpcInfoData info;
    info.name = "short.mpc";
    editor.setMpcInfo(0, info);

    MpcImageCache cache;
    cache.setAssetsBasePath(root.absolutePath().toUtf8().toStdString());

    MapRenderCanvas canvas;
    canvas.setAttribute(Qt::WA_DontShowOnScreen, true);
    canvas.resize(640, 480);
    canvas.setMapFileEditor(&editor);
    canvas.setMpcImageCache(&cache);
    canvas.show();
    QApplication::processEvents();
    canvas.centerOnTile(slotReplaceCenterTile, slotReplaceCenterTile);
    canvas.zoomAtPoint(canvas.rect().center(), 1.0f);
    QApplication::processEvents();

    bool ok = true;

    // 取基础可见范围（固定 margin=3，无高图外扩）。
    int baseStartY, baseEndYDummy;
    int baseStartXDummy, baseEndXDummy;
    canvas.getVisibleTileRangeForTest(baseStartXDummy, baseStartY, baseEndXDummy, baseEndYDummy);
    int baseHeight = baseEndYDummy - baseStartY + 1;

    // 短图（64x32）下渲染范围与基础范围一致：maxFrameHeight=TILE_HEIGHT，extraVertical=0。
    int shortStartY, shortEndY;
    int shortStartX, shortEndX;
    canvas.getVisibleTileRenderRangeForTest(shortStartX, shortStartY, shortEndX, shortEndY);
    int shortHeight = shortEndY - shortStartY + 1;
    ok = check(shortHeight == baseHeight,
               QString("short MPC render range equals base range: short=%1 base=%2")
                   .arg(shortHeight).arg(baseHeight)) && ok;

    // 替换槽位 0 为高图 MPC：已用槽位数仍为 1，mpcCache 基路径不变。
    // commitMpcTableChange / refreshMpcUi 路径会显式 invalidateRenderRangeCache。
    MpcInfoData tallInfo;
    tallInfo.name = "tall.mpc";
    editor.setMpcInfo(0, tallInfo);

    // 先不失效：验证缓存键不变会导致 stale 结果（旧 maxFrameHeight 仍为 32）。
    // 这条断言锁住“必须显式失效”的设计意图。
    int staleStartY, staleEndY;
    int staleStartX, staleEndX;
    canvas.getVisibleTileRenderRangeForTest(staleStartX, staleStartY, staleEndX, staleEndY);
    int staleHeight = staleEndY - staleStartY + 1;
    ok = check(staleHeight == shortHeight,
               QString("stale cache returns old range before invalidation: stale=%1 short=%2")
                   .arg(staleHeight).arg(shortHeight)) && ok;

    // 显式失效（模拟 MPC 表变更后的刷新）。
    canvas.invalidateRenderRangeCache();
    QApplication::processEvents();

    int tallStartY, tallEndY;
    int tallStartX, tallEndX;
    canvas.getVisibleTileRenderRangeForTest(tallStartX, tallStartY, tallEndX, tallEndY);
    int tallHeight = tallEndY - tallStartY + 1;

    // 高图 256px 在 zoom=1 下：extraVertical = ceil((256-32)/16) = 14，上下各 14 → +28。
    ok = check(tallHeight - shortHeight >= 28,
               QString("tall MPC expands render range after invalidation: tall=%1 short=%2 (diff=%3, need >=28)")
                   .arg(tallHeight).arg(shortHeight).arg(tallHeight - shortHeight)) && ok;
    ok = check(tallHeight > shortHeight,
               "render range refreshes when slot replaced with taller image") && ok;

    canvas.hide();
    return ok;
}

}

int main(int argc, char* argv[])
{
#if !defined(Q_OS_WIN)
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") &&
        qEnvironmentVariableIsEmpty("DISPLAY"))
    {
        qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    }
#endif

    QApplication application(argc, argv);
    QTemporaryDir isolatedWorkingDirectory;
    QTemporaryDir isolatedSettingsDirectory;
    if (!isolatedWorkingDirectory.isValid() ||
        !isolatedSettingsDirectory.isValid() ||
        !QDir::setCurrent(isolatedWorkingDirectory.path()))
    {
        return 1;
    }
    application.setProperty(
        "configFilePath",
        isolatedSettingsDirectory.filePath("editor_config.ini"));
    TranslationManager::instance().initialize(application);

    bool ok = true;
    ok = testMpcCacheBasePathChange() && ok;
    ok = testMapEditorAssetsPathSwitchInvalidatesRenderedArt() && ok;
    ok = testMapCoordinateTransformRoundTrip() && ok;
    ok = testEmptySparseMinimapAndLegacyMapZoomToFit() && ok;
    ok = testMapPastePreviewRendersRealPixelsOffscreen() && ok;
    ok = testMinimapClickAndDragCenterCanvasOnTargetTiles() && ok;
    ok = testMapBrushPreviewRendersHoverImage() && ok;
    ok = testMapEdgeRenderShowsTallImageAtViewportEdge() && ok;
    ok = testMapRenderRangeCacheInvalidation() && ok;
    ok = testMapAnimationRefreshIsDemandDriven() && ok;
    ok = testMapRenderRangeMultiFrameAndRowSpacing() && ok;
    ok = testMapRenderRangeCacheInvalidatesOnMpcSlotReplace() && ok;
    return ok ? 0 : 1;
}
