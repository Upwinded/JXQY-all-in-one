#include "MenuPreviewCanvas.h"

#include "../core/EditorAssetPath.h"
#include "../core/PicFileEditor.h"
#include "../core/IMPImageFile.h"
#include "../core/Util.h"

#include <QMouseEvent>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QCursor>
#include <QSet>

#include <cmath>

MenuPreviewCanvas::MenuPreviewCanvas(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(400, 300);
    setMouseTracking(true);
    updateWorldBoundsAndSize();
}

void MenuPreviewCanvas::setMenuRect(const QRect& rect)
{
    menuRect = rect;
    updateWorldBoundsAndSize();
    update();
}

void MenuPreviewCanvas::setComponents(const QList<PreviewComponent>& components)
{
    this->components = components;
    for (auto& component : this->components)
        loadComponentImages(component);
    updateWorldBoundsAndSize();
    update();
}

void MenuPreviewCanvas::setSubMenus(const QList<PreviewSubMenu>& subMenus)
{
    this->subMenus = subMenus;
    for (auto& subMenu : this->subMenus)
    {
        if (!subMenu.backgroundImage.isEmpty())
            subMenu.backgroundPixmap = loadImageFromGamePath(subMenu.backgroundImage);
        else
            subMenu.backgroundPixmap = QPixmap();
        for (auto& component : subMenu.components)
            loadComponentImages(component);
    }
    updateWorldBoundsAndSize();
    update();
}

void MenuPreviewCanvas::clear()
{
    menuRect = QRect();
    components.clear();
    subMenus.clear();
    backgroundImage = QPixmap();
    backgroundImagePath.clear();
    dragMode = DragMode::None;
    dragEditorId.clear();
    updateWorldBoundsAndSize();
    update();
}

void MenuPreviewCanvas::setSelectedComponent(const QString& editorId)
{
    selectedEditorId = editorId;
    update();
}

QString MenuPreviewCanvas::getSelectedComponent() const
{
    return selectedEditorId;
}

void MenuPreviewCanvas::setAssetsBasePath(const QString& path)
{
    setResourceRoots(QStringList{path});
}

void MenuPreviewCanvas::setResourceRoots(const QStringList& paths)
{
    QStringList normalizedRoots;
    QSet<QString> seenRoots;
    for (const QString& path : paths)
    {
        if (path.trimmed().isEmpty())
            continue;

        const QString normalized = EditorAssetPath::normalizedAbsolutePath(path);
        const QString key = EditorAssetPath::logicalComparisonKey(normalized);
        if (!seenRoots.contains(key))
        {
            seenRoots.insert(key);
            normalizedRoots.append(normalized);
        }
    }

    resourceRoots = normalizedRoots;
    reloadImages();
    update();
}

void MenuPreviewCanvas::setBackgroundImage(const QString& imagePath)
{
    backgroundImagePath = imagePath;
    backgroundImage = loadImageFromGamePath(imagePath);
    update();
}

void MenuPreviewCanvas::setWindowStretch(bool stretch)
{
    windowStretch = stretch;
    update();
}

QPixmap MenuPreviewCanvas::getBackgroundImage() const
{
    return backgroundImage;
}

void MenuPreviewCanvas::setScaleFactor(double factor)
{
    scaleFactor = qMax(0.05, factor);
    updateWorldBoundsAndSize();
    update();
}

double MenuPreviewCanvas::getScaleFactor() const
{
    return scaleFactor;
}

void MenuPreviewCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    painter.fillRect(rect(), QColor(48, 48, 48));

    const QRect effectiveRect = menuRect.isValid()
        ? menuRect : QRect(0, 0, 800, 600);

    painter.save();
    painter.translate(20, 20);
    painter.scale(scaleFactor, scaleFactor);
    painter.translate(-worldBounds.left(), -worldBounds.top());

    painter.fillRect(effectiveRect, QColor(30, 30, 30));

    if (!backgroundImage.isNull())
    {
        if (windowStretch)
        {
            painter.drawPixmap(effectiveRect, backgroundImage);
        }
        else
        {
            painter.drawPixmap(effectiveRect.topLeft(), backgroundImage);
        }
    }

    drawGrid(painter, effectiveRect);

    for (const auto& component : components)
    {
        drawComponent(painter, component);
    }

    for (const auto& subMenu : subMenus)
    {
        if (!subMenu.backgroundPixmap.isNull() && subMenu.windowRect.isValid())
        {
            painter.drawPixmap(subMenu.windowRect, subMenu.backgroundPixmap);
        }
        for (const auto& component : subMenu.components)
        {
            drawComponent(painter, component, subMenu.windowRect.topLeft());
        }
    }

    if (!selectedEditorId.isEmpty())
    {
        QPoint coordinateOffset;
        PreviewComponent* selected = findComponentById(selectedEditorId, &coordinateOffset);
        if (selected)
        {
            drawSelection(painter, *selected, coordinateOffset);
        }
    }

    painter.restore();
}

void MenuPreviewCanvas::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
    {
        return;
    }

    const QPoint menuPos = canvasToMenu(event->pos());

    if (!selectedEditorId.isEmpty())
    {
        QPoint coordinateOffset;
        PreviewComponent* selected = findComponentById(selectedEditorId, &coordinateOffset);
        if (selected)
        {
            const QRect selectedWorldRect = selected->rect.translated(coordinateOffset);
            DragMode handleHit = hitTestHandle(menuPos, selectedWorldRect);
            if (handleHit != DragMode::None)
            {
                dragMode = handleHit;
                dragStart = menuPos;
                dragOriginalRect = selected->rect;
                dragEditorId = componentIdentity(*selected);
                return;
            }
        }
    }

    PreviewComponent* hitComponent = findComponentAt(menuPos);
    if (hitComponent)
    {
        selectedEditorId = componentIdentity(*hitComponent);
        dragMode = DragMode::Move;
        dragStart = menuPos;
        dragOriginalRect = hitComponent->rect;
        dragEditorId = selectedEditorId;
    }
    else
    {
        selectedEditorId.clear();
        dragMode = DragMode::None;
    }

    emit componentSelected(selectedEditorId);
    update();
}

void MenuPreviewCanvas::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint menuPos = canvasToMenu(event->pos());

    if (dragMode == DragMode::None)
    {
        if (!selectedEditorId.isEmpty())
        {
            QPoint coordinateOffset;
            PreviewComponent* selected = findComponentById(selectedEditorId, &coordinateOffset);
            if (selected)
            {
                const QRect selectedWorldRect = selected->rect.translated(coordinateOffset);
                DragMode handleHit = hitTestHandle(menuPos, selectedWorldRect);
                switch (handleHit)
                {
                case DragMode::ResizeTopLeft:
                case DragMode::ResizeBottomRight:
                    setCursor(Qt::SizeFDiagCursor);
                    break;
                case DragMode::ResizeTopRight:
                case DragMode::ResizeBottomLeft:
                    setCursor(Qt::SizeBDiagCursor);
                    break;
                case DragMode::ResizeTop:
                case DragMode::ResizeBottom:
                    setCursor(Qt::SizeVerCursor);
                    break;
                case DragMode::ResizeLeft:
                case DragMode::ResizeRight:
                    setCursor(Qt::SizeHorCursor);
                    break;
                case DragMode::Move:
                    setCursor(Qt::SizeAllCursor);
                    break;
                default:
                    {
                        if (selectedWorldRect.contains(menuPos))
                        {
                            setCursor(Qt::SizeAllCursor);
                        }
                        else
                        {
                            unsetCursor();
                        }
                    }
                    break;
                }
            }
        }
        else
        {
            PreviewComponent* hit = findComponentAt(menuPos);
            if (hit)
            {
                setCursor(Qt::PointingHandCursor);
            }
            else
            {
                unsetCursor();
            }
        }
        return;
    }

    QPoint delta = menuPos - dragStart;

    PreviewComponent* dragTarget = findComponentById(dragEditorId);
    if (dragTarget)
    {
        QRect newRect = dragOriginalRect;

        switch (dragMode)
        {
        case DragMode::Move:
            newRect.moveTo(dragOriginalRect.topLeft() + delta);
            break;
        case DragMode::ResizeTopLeft:
            newRect.setTopLeft(dragOriginalRect.topLeft() + delta);
            break;
        case DragMode::ResizeTopRight:
            newRect.setTop(dragOriginalRect.top() + delta.y());
            newRect.setRight(dragOriginalRect.right() + delta.x());
            break;
        case DragMode::ResizeBottomLeft:
            newRect.setBottom(dragOriginalRect.bottom() + delta.y());
            newRect.setLeft(dragOriginalRect.left() + delta.x());
            break;
        case DragMode::ResizeBottomRight:
            newRect.setBottomRight(dragOriginalRect.bottomRight() + delta);
            break;
        case DragMode::ResizeTop:
            newRect.setTop(dragOriginalRect.top() + delta.y());
            break;
        case DragMode::ResizeBottom:
            newRect.setBottom(dragOriginalRect.bottom() + delta.y());
            break;
        case DragMode::ResizeLeft:
            newRect.setLeft(dragOriginalRect.left() + delta.x());
            break;
        case DragMode::ResizeRight:
            newRect.setRight(dragOriginalRect.right() + delta.x());
            break;
        default:
            break;
        }

        if (newRect.width() < 5)
        {
            newRect.setWidth(5);
        }
        if (newRect.height() < 5)
        {
            newRect.setHeight(5);
        }

        dragTarget->rect = newRect;

        emit componentPropertyChanged(componentIdentity(*dragTarget),
            dragTarget->rect.x(), dragTarget->rect.y(),
            dragTarget->rect.width(), dragTarget->rect.height());
    }

    update();
}

void MenuPreviewCanvas::mouseReleaseEvent(QMouseEvent* event)
{
    Q_UNUSED(event);

    if (dragMode != DragMode::None)
    {
        PreviewComponent* dragTarget = findComponentById(dragEditorId);
        if (dragTarget)
        {
            if (dragMode == DragMode::Move)
            {
                emit componentMoved(componentIdentity(*dragTarget),
                                    dragTarget->rect.x(), dragTarget->rect.y());
            }
            else
            {
                emit componentResized(componentIdentity(*dragTarget),
                    dragTarget->rect.x(), dragTarget->rect.y(),
                    dragTarget->rect.width(), dragTarget->rect.height());
            }
        }

        dragMode = DragMode::None;
        dragEditorId.clear();
        updateWorldBoundsAndSize();
        update();
    }
}

void MenuPreviewCanvas::drawGrid(QPainter& painter)
{
    drawGrid(painter, menuRect.isValid() ? menuRect : QRect(0, 0, 800, 600));
}

void MenuPreviewCanvas::drawGrid(QPainter& painter, const QRect& gridRect)
{
    painter.save();
    QPen pen(QColor(60, 60, 60), 1);
    painter.setPen(pen);

    int gridSize = 50;
    for (int x = gridRect.left(); x <= gridRect.right(); x += gridSize)
    {
        painter.drawLine(x, gridRect.top(), x, gridRect.bottom());
    }
    for (int y = gridRect.top(); y <= gridRect.bottom(); y += gridSize)
    {
        painter.drawLine(gridRect.left(), y, gridRect.right(), y);
    }

    painter.restore();
}

void MenuPreviewCanvas::drawComponent(QPainter& painter, const PreviewComponent& component,
                                      const QPoint& coordinateOffset)
{
    if (!component.visible)
    {
        return;
    }

    painter.save();
    painter.translate(coordinateOffset);

    if (component.type == "Joystick")
    {
        if (!component.image.isNull())
        {
            // 运行时 Joystick 总是拉伸背景图：RoundButton 构造函数设置 stretch=true，
            // 且 Joystick::initFromIni 不读取 INI 中的 Stretch 字段。
            painter.drawPixmap(component.rect, component.image);
        }
        else
        {
            painter.fillRect(component.rect, QColor(180, 180, 180, 60));
        }

        if (!component.secondaryImage.isNull())
        {
            QPoint thumbPosition(
                component.rect.center().x() - component.secondaryImage.width() / 2,
                component.rect.center().y() - component.secondaryImage.height() / 2);
            painter.drawPixmap(thumbPosition, component.secondaryImage);
        }
    }
    else if (!component.image.isNull())
    {
        if (component.stretch)
        {
            painter.drawPixmap(component.rect, component.image);
        }
        else
        {
            QPoint drawPos(component.rect.x(), component.rect.y());
            painter.drawPixmap(drawPos, component.image);
        }
    }
    else
    {
        QColor fillColor;
        if (component.type == "Label")
        {
            fillColor = QColor(100, 200, 100, 80);
        }
        else if (component.type == "Button" || component.type == "CheckBox")
        {
            fillColor = QColor(200, 100, 100, 80);
        }
        else if (component.type == "Item")
        {
            fillColor = QColor(100, 100, 200, 80);
        }
        else if (component.type == "Scrollbar")
        {
            fillColor = QColor(200, 200, 100, 80);
        }
        else if (component.type == "ImageContainer")
        {
            fillColor = QColor(100, 200, 200, 80);
        }
        else if (component.type == "DragButton")
        {
            fillColor = QColor(220, 180, 100, 100);
        }
        else
        {
            fillColor = QColor(180, 180, 180, 60);
        }

        painter.fillRect(component.rect, fillColor);

        QPen borderPen(QColor(200, 200, 200, 120), 1);
        painter.setPen(borderPen);
        painter.drawRect(component.rect);

        if (component.type == "ListBox" && !component.listBoxItems.isEmpty())
        {
            int itemHeight = component.itemHeight;
            if (itemHeight <= 0)
            {
                itemHeight = qMax(16, component.rect.height() / qMax(1, component.listBoxItems.size()));
            }

            QColor itemTextColor = QColor::fromRgba(component.color);
            QColor selBgColor = QColor::fromRgba(component.selColor);

            QFont font = painter.font();
            font.setPixelSize(qMax(10, itemHeight - 4));
            painter.setFont(font);

            for (int i = 0; i < component.listBoxItems.size(); i++)
            {
                int itemTop = component.rect.y() + i * itemHeight;
                if (itemTop >= component.rect.y() + component.rect.height())
                {
                    break;
                }

                QRect itemRect(component.rect.x() + 2, itemTop, component.rect.width() - 4, itemHeight);

                if (i == 0)
                {
                    painter.fillRect(itemRect, selBgColor);
                }

                painter.setPen(itemTextColor);
                painter.drawText(itemRect, Qt::AlignVCenter | Qt::AlignLeft, component.listBoxItems[i]);
            }
        }
        else if (!component.name.isEmpty())
        {
            painter.setPen(QColor(255, 255, 255, 200));
            QFont font = painter.font();
            font.setPixelSize(qMax(10, component.rect.height() / 3));
            painter.setFont(font);
            painter.drawText(component.rect, Qt::AlignCenter, component.name);
        }
    }

    for (const auto& child : component.children)
    {
        drawComponent(painter, child);
    }

    painter.restore();
}

void MenuPreviewCanvas::drawSelection(QPainter& painter, const PreviewComponent& component,
                                      const QPoint& coordinateOffset)
{
    painter.save();
    painter.translate(coordinateOffset);
    QPen pen(QColor(0, 150, 255), 2, Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    QRect selectRect = component.rect.adjusted(-2, -2, 2, 2);
    painter.drawRect(selectRect);

    QList<QRect> handles = getHandleRects(selectRect);

    painter.setBrush(QColor(0, 150, 255));
    painter.setPen(Qt::NoPen);
    for (const auto& handle : handles)
    {
        painter.drawRect(handle);
    }

    painter.restore();
}

QList<QRect> MenuPreviewCanvas::getHandleRects(const QRect& selectRect) const
{
    int hs = handleSize;
    int half = hs / 2;

    return {
        QRect(selectRect.left() - half, selectRect.top() - half, hs, hs),
        QRect(selectRect.right() - half, selectRect.top() - half, hs, hs),
        QRect(selectRect.left() - half, selectRect.bottom() - half, hs, hs),
        QRect(selectRect.right() - half, selectRect.bottom() - half, hs, hs),
        QRect(selectRect.center().x() - half, selectRect.top() - half, hs, hs),
        QRect(selectRect.center().x() - half, selectRect.bottom() - half, hs, hs),
        QRect(selectRect.left() - half, selectRect.center().y() - half, hs, hs),
        QRect(selectRect.right() - half, selectRect.center().y() - half, hs, hs)
    };
}

DragMode MenuPreviewCanvas::hitTestHandle(const QPoint& menuPos, const QRect& rect) const
{
    QRect selectRect = rect.adjusted(-2, -2, 2, 2);
    QList<QRect> handles = getHandleRects(selectRect);

    QList<DragMode> modes = {
        DragMode::ResizeTopLeft,
        DragMode::ResizeTopRight,
        DragMode::ResizeBottomLeft,
        DragMode::ResizeBottomRight,
        DragMode::ResizeTop,
        DragMode::ResizeBottom,
        DragMode::ResizeLeft,
        DragMode::ResizeRight
    };

    const int hitPadding = qMax(2, static_cast<int>(std::ceil(4.0 / scaleFactor)));
    for (int i = 0; i < handles.size() && i < modes.size(); i++)
    {
        const QRect hitRect = handles[i].adjusted(
            -hitPadding, -hitPadding, hitPadding, hitPadding);
        if (hitRect.contains(menuPos))
        {
            return modes[i];
        }
    }

    if (rect.contains(menuPos))
    {
        return DragMode::Move;
    }

    return DragMode::None;
}

QPixmap MenuPreviewCanvas::loadImageFromGamePath(const QString& gamePath, int frameIndex)
{
    QString normalizedResourcePath;
    if (resourceRoots.isEmpty() ||
        !EditorAssetPath::normalizeResourcePath(gamePath, normalizedResourcePath))
        return QPixmap();

    for (const QString& root : resourceRoots)
    {
        QString fullPath;
        if (!EditorAssetPath::resolveLogicalResourcePath(
                root, normalizedResourcePath, fullPath))
            continue;

        const QFileInfo fileInfo(fullPath);
        if (!fileInfo.exists() || !fileInfo.isFile())
            continue;

        const QString suffix = fileInfo.suffix().toLower();
        if (suffix == "mpc" || suffix == "shd" || suffix == "asf" ||
            suffix == "imp" || suffix == "img")
        {
            PicFileEditor editor;
            if (editor.loadFromFile(fullPath.toUtf8().toStdString()) &&
                frameIndex >= 0 && frameIndex < editor.getFrameCount())
            {
                const QImage image = editor.getFrameImage(frameIndex);
                if (!image.isNull())
                    return QPixmap::fromImage(image);
            }
            return QPixmap();
        }

        QPixmap pixmap;
        if (pixmap.load(fullPath))
            return pixmap;
        return QPixmap();
    }

    return QPixmap();
}

void MenuPreviewCanvas::loadComponentImages(PreviewComponent& component)
{
    component.image = component.imagePath.isEmpty()
        ? QPixmap() : loadImageFromGamePath(component.imagePath, component.imageFrame);
    component.secondaryImage = component.secondaryImagePath.isEmpty()
        ? QPixmap() : loadImageFromGamePath(
            component.secondaryImagePath, component.secondaryImageFrame);
    for (auto& child : component.children)
        loadComponentImages(child);
}

void MenuPreviewCanvas::reloadImages()
{
    backgroundImage = backgroundImagePath.isEmpty()
        ? QPixmap() : loadImageFromGamePath(backgroundImagePath);
    for (auto& component : components)
        loadComponentImages(component);
    for (auto& subMenu : subMenus)
    {
        subMenu.backgroundPixmap = subMenu.backgroundImage.isEmpty()
            ? QPixmap() : loadImageFromGamePath(subMenu.backgroundImage);
        for (auto& component : subMenu.components)
            loadComponentImages(component);
    }
}

QString MenuPreviewCanvas::componentIdentity(const PreviewComponent& component) const
{
    // Name fallback keeps older callers functional while they migrate to stable IDs.
    return component.editorId.isEmpty() ? component.name : component.editorId;
}

PreviewComponent* MenuPreviewCanvas::findComponentByIdInList(
    QList<PreviewComponent>& componentList, const QString& editorId,
    const QPoint& coordinateOffset, QPoint* foundCoordinateOffset)
{
    for (auto& component : componentList)
    {
        if (componentIdentity(component) == editorId)
        {
            if (foundCoordinateOffset)
                *foundCoordinateOffset = coordinateOffset;
            return &component;
        }

        PreviewComponent* child = findComponentByIdInList(
            component.children, editorId, coordinateOffset, foundCoordinateOffset);
        if (child)
            return child;
    }
    return nullptr;
}

PreviewComponent* MenuPreviewCanvas::findComponentById(
    const QString& editorId, QPoint* coordinateOffset)
{
    if (editorId.isEmpty())
        return nullptr;

    PreviewComponent* component = findComponentByIdInList(
        components, editorId, QPoint(), coordinateOffset);
    if (component)
        return component;

    for (auto& subMenu : subMenus)
    {
        component = findComponentByIdInList(
            subMenu.components, editorId, subMenu.windowRect.topLeft(), coordinateOffset);
        if (component)
            return component;
    }
    return nullptr;
}

PreviewComponent* MenuPreviewCanvas::findComponentAtInList(
    QList<PreviewComponent>& componentList, const QPoint& pos,
    const QPoint& coordinateOffset, QPoint* foundCoordinateOffset)
{
    for (int i = componentList.size() - 1; i >= 0; --i)
    {
        PreviewComponent& component = componentList[i];
        if (!component.visible)
            continue;

        PreviewComponent* child = findComponentAtInList(
            component.children, pos, coordinateOffset, foundCoordinateOffset);
        if (child)
            return child;

        if (component.rect.translated(coordinateOffset).contains(pos))
        {
            if (foundCoordinateOffset)
                *foundCoordinateOffset = coordinateOffset;
            return &component;
        }
    }
    return nullptr;
}

PreviewComponent* MenuPreviewCanvas::findComponentAt(
    const QPoint& pos, QPoint* coordinateOffset)
{
    // Submenus are painted after main components, so they take hit priority.
    for (int i = subMenus.size() - 1; i >= 0; --i)
    {
        PreviewComponent* component = findComponentAtInList(
            subMenus[i].components, pos, subMenus[i].windowRect.topLeft(),
            coordinateOffset);
        if (component)
            return component;
    }

    return findComponentAtInList(components, pos, QPoint(), coordinateOffset);
}

void MenuPreviewCanvas::includeComponentBounds(
    QRect& bounds, bool& hasBounds, const PreviewComponent& component,
    const QPoint& coordinateOffset) const
{
    const QRect componentBounds = component.rect.translated(coordinateOffset);
    if (componentBounds.isValid())
    {
        bounds = hasBounds ? bounds.united(componentBounds) : componentBounds;
        hasBounds = true;
    }

    for (const auto& child : component.children)
        includeComponentBounds(bounds, hasBounds, child, coordinateOffset);
}

void MenuPreviewCanvas::updateWorldBoundsAndSize()
{
    QRect bounds;
    bool hasBounds = false;
    if (menuRect.isValid())
    {
        bounds = menuRect;
        hasBounds = true;
    }

    for (const auto& component : components)
        includeComponentBounds(bounds, hasBounds, component, QPoint());

    for (const auto& subMenu : subMenus)
    {
        if (subMenu.windowRect.isValid())
        {
            bounds = hasBounds ? bounds.united(subMenu.windowRect) : subMenu.windowRect;
            hasBounds = true;
        }
        for (const auto& component : subMenu.components)
        {
            includeComponentBounds(
                bounds, hasBounds, component, subMenu.windowRect.topLeft());
        }
    }

    // Keep an empty/new document compact; real content replaces this bound.
    worldBounds = hasBounds ? bounds : QRect(0, 0, 360, 260);
    const int canvasWidth = qMax(
        static_cast<int>(std::ceil(worldBounds.width() * scaleFactor)) + 40, 400);
    const int canvasHeight = qMax(
        static_cast<int>(std::ceil(worldBounds.height() * scaleFactor)) + 40, 300);
    setMinimumSize(canvasWidth, canvasHeight);
    resize(canvasWidth, canvasHeight);
}

QPoint MenuPreviewCanvas::canvasToMenu(const QPoint& canvasPos) const
{
    return QPoint(
        worldBounds.left() + static_cast<int>(std::floor((canvasPos.x() - 20) / scaleFactor)),
        worldBounds.top() + static_cast<int>(std::floor((canvasPos.y() - 20) / scaleFactor))
    );
}

QPoint MenuPreviewCanvas::menuToCanvas(const QPoint& menuPos) const
{
    return QPoint(
        20 + static_cast<int>(std::lround((menuPos.x() - worldBounds.left()) * scaleFactor)),
        20 + static_cast<int>(std::lround((menuPos.y() - worldBounds.top()) * scaleFactor))
    );
}
