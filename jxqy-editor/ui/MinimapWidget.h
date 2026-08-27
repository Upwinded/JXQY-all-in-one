#pragma once

#include <QWidget>
#include <QImage>

class MapFileEditor;
class MpcImageCache;
class MapRenderCanvas;

class MinimapWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MinimapWidget(QWidget* parent = nullptr);
    ~MinimapWidget();

    void setMapFileEditor(MapFileEditor* editor);
    void setMpcImageCache(MpcImageCache* cache);
    void setCanvas(MapRenderCanvas* canvas);

    void refreshMinimap();
    QPoint tileCenterToWidgetPosition(int tileX, int tileY);
    QPoint widgetPositionToTile(const QPoint& widgetPosition);
    QSize backingImageSize();

    /// 小地图上每个瓦片的宽度单位（高度为其一半，保持 2:1，与主画布一致）。
    /// 必须为偶数且 >= 4：tileHeight = MINIMAP_TILE_SIZE/2，整数行步长 dy*tileHeight/2
    /// 取 16 让缩略图先绘制较大图，再由 paintEvent 缩放到 widget，避免两级极度缩小。
    static const int MINIMAP_TILE_SIZE = 16;
    /// The widget is at most 320x240, so a larger raster only consumes memory
    /// without adding visible detail.  Logical minimap coordinates remain at
    /// MINIMAP_TILE_SIZE precision and are independent of this raster cap.
    static constexpr int MAX_BACKING_EDGE = 1024;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QPoint minimapToTile(const QPoint& minimapPos) const;

    /// 计算 minimapImage 缩放到当前 widget 的等比缩放与居中偏移。
    void computeMinimapFit(float& scale, int& offsetX, int& offsetY) const;
    /// 把画布屏幕坐标（视口角点）投影到小地图 widget 坐标，用于绘制真实视口框。
    QPointF canvasScreenToWidget(const QPoint& canvasScreen) const;
    /// 按 MapCoordinateTransform 实际输出重建缩略图，缓存 image 尺寸与世界原点偏移。
    void rebuildMinimapImage(int mapWidth, int mapHeight);
    void ensureMinimapGeometry();

    MapFileEditor* mapEditor = nullptr;
    MpcImageCache* mpcCache = nullptr;
    MapRenderCanvas* canvasWidget = nullptr;

    QImage minimapImage;
    bool minimapDirty = true;
    bool isDragging = false;

    // 缩略图几何缓存：minimapWorld 原点偏移（把负坐标平移到 image 内）与 image 尺寸，
    // 使点击/视口框投影与缩略图绘制严格一致。
    int minimapOriginX = 0;
    int minimapOriginY = 0;
    int minimapImageWidth = 0;
    int minimapImageHeight = 0;
    double minimapBackingScale = 1.0;
};
