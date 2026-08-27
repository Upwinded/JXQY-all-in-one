#pragma once

#include <QWidget>
#include <QPainter>
#include <QPixmap>
#include <QMap>
#include <QList>
#include <QRect>
#include <QString>
#include <QStringList>

struct PreviewComponent
{
    QString editorId;
    QString type;
    QString name;
    QRect rect;
    QString imagePath;
    QPixmap image;
    int imageFrame = 0;
    QString secondaryImagePath;
    QPixmap secondaryImage;
    int secondaryImageFrame = 0;
    bool visible = true;
    bool stretch = false;
    QList<PreviewComponent> children;
    QStringList listBoxItems;
    int itemHeight = 0;
    unsigned int color = 0xFFFFFFFF;
    unsigned int selColor = 0xFFE6C864;
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

public:
    explicit MenuPreviewCanvas(QWidget* parent = nullptr);

    void setMenuRect(const QRect& rect);
    void setComponents(const QList<PreviewComponent>& components);
    void setSubMenus(const QList<PreviewSubMenu>& subMenus);
    void setBackgroundImage(const QString& imagePath);
    void setWindowStretch(bool stretch);
    QPixmap getBackgroundImage() const;
    void clear();

    void setSelectedComponent(const QString& editorId);
    QString getSelectedComponent() const;

    void setAssetsBasePath(const QString& path);
    void setResourceRoots(const QStringList& paths);

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

    static const int handleSize = 8;

    QRect menuRect;
    QList<PreviewComponent> components;
    QList<PreviewSubMenu> subMenus;
    QPixmap backgroundImage;
    QString backgroundImagePath;
    bool windowStretch = false;
    QString selectedEditorId;
    QStringList resourceRoots;
    QRect worldBounds;

    double scaleFactor = 1.0;

    DragMode dragMode = DragMode::None;
    QPoint dragStart;
    QRect dragOriginalRect;
    QString dragEditorId;
};
