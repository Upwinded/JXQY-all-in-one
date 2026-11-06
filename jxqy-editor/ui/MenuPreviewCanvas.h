#pragma once

#include <QWidget>
#include <QPainter>
#include <QPixmap>
#include <QMap>
#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QTransform>

struct PreviewComponent
{
    QString editorId;
    QString type;
    QString name;
    QRect rect;
    QString imagePath;
    QPixmap image;
    bool sampleImage = false;
    int imageFrame = 0;
    QString secondaryImagePath;
    QPixmap secondaryImage;
    int secondaryImageFrame = 0;
    bool visible = true;
    bool stretch = false;
    QList<PreviewComponent> children;
    QStringList listBoxItems;
    int itemHeight = 0;
    bool fitItems = false;
    int selectedIndex = -1;
    unsigned int color = 0xFFFFFFFF;
    unsigned int selColor = 0xFFE6C864;
    QString text;
    int fontSize = 18;
    QMap<QString, QString> rawValues;
    bool movable = true;
    bool resizable = true;
    bool dialogTextPreview = false;
    bool choiceTextPreview = false;
    bool passiveTextPreview = false;
};

struct PreviewSubMenu
{
    QString editorId;
    QString name;
    QRect windowRect;
    QString backgroundImage;
    QPixmap backgroundPixmap;
    QList<PreviewComponent> components;
};

enum class DragMode
{
    None,
    Move,
    ResizeTopLeft,
    ResizeTopRight,
    ResizeBottomLeft,
    ResizeBottomRight,
    ResizeTop,
    ResizeBottom,
    ResizeLeft,
    ResizeRight
};

class MenuPreviewCanvas : public QWidget
{
    Q_OBJECT
    friend class MenuEditorWindow;

public:
    explicit MenuPreviewCanvas(QWidget* parent = nullptr);

    void setBackgroundColor(const QColor& color) { backgroundColor = color; update(); }
    void setMenuRect(const QRect& rect);
    void setComponents(const QList<PreviewComponent>& components);
    void setSubMenus(const QList<PreviewSubMenu>& subMenus);
    void setBackgroundImage(const QString& imagePath);
    void setWindowStretch(bool stretch, bool keepAspect = false);
    void setWindowPreviewVisible(bool visible) { windowPreviewVisible = visible; update(); }
    void setAspectFitViewport(const QSize& viewport);
    void setGameWindowPreview(const QSize& viewport, const QPoint& interfaceOrigin = {});
    void setEditingGuidesVisible(bool visible) { editingGuidesVisible = visible; update(); }
    void setWindowNineSlice(int sourceBorder, int displayBorder);
    QPixmap getBackgroundImage() const;
    void applyButtonPreviewState(PreviewComponent& component, int state);
    QPixmap componentThumbnail(PreviewComponent component, int state);
    void clear();

    void setSelectedComponent(const QString& editorId);
    QString getSelectedComponent() const;

    void setAssetsBasePath(const QString& path);
    void setResourceRoots(const QStringList& paths, const QStringList& contentPaths = {});

    void setScaleFactor(double factor);
    double getScaleFactor() const;

signals:
    void componentSelected(const QString& editorId);
    void componentMoved(const QString& editorId, int newX, int newY);
    void componentResized(const QString& editorId, int newX, int newY, int newWidth, int newHeight);
    void componentPropertyChanged(const QString& editorId, int left, int top, int width, int height);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void drawGrid(QPainter& painter);
    void drawGrid(QPainter& painter, const QRect& gridRect);
    void drawComponent(QPainter& painter, const PreviewComponent& component,
                       const QPoint& coordinateOffset = QPoint());
    void drawSelection(QPainter& painter, const PreviewComponent& component,
                       const QPoint& coordinateOffset = QPoint());
    QPixmap loadImageFromGamePath(const QString& gamePath, int frameIndex = 0);
    void loadComponentImages(PreviewComponent& component);
    void reloadImages();
    PreviewComponent* findComponentAt(const QPoint& pos, QPoint* coordinateOffset = nullptr);
    PreviewComponent* findComponentById(const QString& editorId,
                                        QPoint* coordinateOffset = nullptr);
    PreviewComponent* findComponentAtInList(QList<PreviewComponent>& componentList,
                                            const QPoint& pos,
                                            const QPoint& coordinateOffset,
                                            QPoint* foundCoordinateOffset);
    PreviewComponent* findComponentByIdInList(QList<PreviewComponent>& componentList,
                                              const QString& editorId,
                                              const QPoint& coordinateOffset,
                                              QPoint* foundCoordinateOffset);
    QString componentIdentity(const PreviewComponent& component) const;
    void updateWorldBoundsAndSize();
    void includeComponentBounds(QRect& bounds, bool& hasBounds,
                                const PreviewComponent& component,
                                const QPoint& coordinateOffset) const;
    DragMode hitTestHandle(const QPoint& menuPos, const QRect& rect) const;
    QList<QRect> getHandleRects(const QRect& selectRect) const;
    QPoint canvasToMenu(const QPoint& canvasPos) const;
    QPoint menuToCanvas(const QPoint& menuPos) const;
    QTransform compositionTransform() const;

    static const int handleSize = 8;

    QRect menuRect;
    QSize aspectFitViewport;
    QSize gameWindowViewport;
    bool editingGuidesVisible = true;
    QPoint gameInterfaceOrigin;
    QList<PreviewComponent> components;
    QList<PreviewSubMenu> subMenus;
    QColor backgroundColor;
    QPixmap backgroundImage;
    QString backgroundImagePath;
    bool windowStretch = false;
    bool windowPreviewVisible = true;
    bool windowKeepAspect = false;
    int nineSlice = 0;
    int nineSliceWidth = 0;
    QString selectedEditorId;
    QStringList resourceRoots;
    QStringList contentResourceRoots;
    QRect worldBounds;

    double scaleFactor = 1.0;

    DragMode dragMode = DragMode::None;
    QPoint dragStart;
    QRect dragOriginalRect;
    QString dragEditorId;
};
