#include "../../src/Component/TextLayout.h"
#include "../../src/Game/Menu/DiceGameLayout.h"
#include "MenuPreviewCanvas.h"
#include "../../src/Game/Menu/ControllerHelpLayout.h"
#include "../core/MenuPageContext.h"

#include "../core/EditorAssetPath.h"
#include "../core/PicFileEditor.h"
#include "../core/IMPImageFile.h"
#include "../../src/Component/TextLayout.h"
#include "../core/Util.h"

#include <QMouseEvent>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QCursor>
#include <QSet>
#include <QApplication>
#include <QFontMetrics>

#include <cmath>
#include <limits>
#include "../../src/Game/Menu/SystemNoticeLayout.h"

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
    windowPreviewVisible = true;
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

void MenuPreviewCanvas::setResourceRoots(const QStringList& paths, const QStringList& contentPaths)
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
    contentResourceRoots = contentPaths.isEmpty() ? normalizedRoots : contentPaths;
    reloadImages();
    update();
}

void MenuPreviewCanvas::setBackgroundImage(const QString& imagePath)
{
    backgroundImagePath = imagePath;
    backgroundImage = loadImageFromGamePath(imagePath);
    update();
}

void MenuPreviewCanvas::setWindowStretch(bool stretch, bool keepAspect)
{
    windowStretch = stretch;
    windowKeepAspect = keepAspect;
    update();
}

void MenuPreviewCanvas::setAspectFitViewport(const QSize& viewport)
{
    aspectFitViewport = viewport;
    updateWorldBoundsAndSize();
    update();
}

QTransform MenuPreviewCanvas::compositionTransform() const
{
    QTransform transform;
    if (gameWindowViewport.isValid()) return transform;
    if (aspectFitViewport.isValid() && menuRect.width() > 0 && menuRect.height() > 0)
    {
        const double scale = qMin(double(aspectFitViewport.width()) / menuRect.width(), double(aspectFitViewport.height()) / menuRect.height());
        transform.translate((aspectFitViewport.width() - qRound(menuRect.width() * scale)) / 2,
                            (aspectFitViewport.height() - qRound(menuRect.height() * scale)) / 2);
        transform.scale(scale, scale);
    }
    return transform;
}

void MenuPreviewCanvas::setGameWindowPreview(const QSize& viewport, const QPoint& interfaceOrigin)
{
    gameWindowViewport = viewport;
    gameInterfaceOrigin = viewport.isValid() ? interfaceOrigin : QPoint();
    updateWorldBoundsAndSize();
    update();
}

QPixmap MenuPreviewCanvas::getBackgroundImage() const
{
    return backgroundImage;
}

void MenuPreviewCanvas::setScaleFactor(double factor)
{
    scaleFactor = qBound(0.01, factor, 8.0);
    updateWorldBoundsAndSize();
    update();
}

double MenuPreviewCanvas::getScaleFactor() const
{
    return scaleFactor;
}

namespace
{
void drawNineSlice(QPainter& painter, const QRect& target, const QPixmap& image, int nineSlice, int nineSliceWidth)
{
    const int source = qMin(nineSlice, qMin(image.width(), image.height()) / 2);
    const int border = qMin(nineSliceWidth, qMin(target.width(), target.height()) / 2);
    const int sx[] = {0, source, image.width() - source, image.width()};
    const int sy[] = {0, source, image.height() - source, image.height()};
    const int dx[] = {target.left(), target.left() + border, target.right() + 1 - border, target.right() + 1};
    const int dy[] = {target.top(), target.top() + border, target.bottom() + 1 - border, target.bottom() + 1};
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            painter.drawPixmap(QRect(dx[column], dy[row], dx[column+1]-dx[column], dy[row+1]-dy[row]),
                image, QRect(sx[column], sy[row], sx[column+1]-sx[column], sy[row+1]-sy[row]));
}
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
    if (gameWindowViewport.isValid())
    {
        const QRect viewport(QPoint(), gameWindowViewport);
        painter.fillRect(viewport, QColor(20, 23, 27));
        painter.setPen(QPen(QColor(125, 145, 165), 1.0 / scaleFactor));
        painter.drawRect(viewport.adjusted(0, 0, -1, -1));
        painter.setClipRect(viewport);
        painter.translate(gameInterfaceOrigin);
    }
    if (aspectFitViewport.isValid()) painter.fillRect(QRect(QPoint(), aspectFitViewport), QColor(15, 15, 15));
    painter.setTransform(compositionTransform(), true);

    if (windowPreviewVisible && (backgroundColor.isValid() || !gameWindowViewport.isValid()))
        painter.fillRect(effectiveRect, backgroundColor.isValid() ? backgroundColor : QColor(30, 30, 30));

    if (windowPreviewVisible && !backgroundImage.isNull())
    {
        if (windowStretch && nineSlice > 0 && nineSliceWidth > 0)
        {
            drawNineSlice(painter, effectiveRect, backgroundImage, nineSlice, nineSliceWidth);
        }
        else if (windowStretch && windowKeepAspect)
        {
            const QSize size = backgroundImage.size().scaled(effectiveRect.size(), Qt::KeepAspectRatio);
            const QPoint offset((effectiveRect.width() - size.width()) / 2,
                                (effectiveRect.height() - size.height()) / 2);
            painter.drawPixmap(QRect(effectiveRect.topLeft() + offset, size), backgroundImage);
        }
        else if (windowStretch)
        {
            painter.drawPixmap(effectiveRect, backgroundImage);
        }
        else
        {
            painter.drawPixmap(effectiveRect.topLeft(), backgroundImage);
        }
    }

    if (windowPreviewVisible && editingGuidesVisible && !gameWindowViewport.isValid()) drawGrid(painter, effectiveRect);

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
            if (handleHit != DragMode::None && selected->resizable && !gameWindowViewport.isValid())
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
        dragMode = hitComponent->movable && !gameWindowViewport.isValid() ? DragMode::Move : DragMode::None;
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
    update();
    const QPoint menuPos = canvasToMenu(event->pos());

    if (dragMode == DragMode::None)
    {
        if (!selectedEditorId.isEmpty())
        {
            QPoint coordinateOffset;
            PreviewComponent* selected = findComponentById(selectedEditorId, &coordinateOffset);
            if (selected && (selected->movable || selected->resizable))
            {
                const QRect selectedWorldRect = selected->rect.translated(coordinateOffset);
                DragMode handleHit = selected->resizable ? hitTestHandle(menuPos, selectedWorldRect) : DragMode::None;
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
                        if (selected->movable && selectedWorldRect.contains(menuPos))
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
            else unsetCursor();
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

void MenuPreviewCanvas::setWindowNineSlice(int sourceBorder, int displayBorder)
{
    nineSlice = sourceBorder;
    nineSliceWidth = displayBorder;
    update();
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
    const auto enabled = [&component](const QString& key)
    {
        return QStringList{"1", "true", "yes", "on"}.contains(component.rawValues.value(key).toLower());
    };
    if (!component.visible)
    {
        return;
    }

    painter.save();
    painter.translate(coordinateOffset);

    if(component.type=="ImageContainer"&&component.rawValues.contains("mapthumbnail"))
    {
        const int state=component.rawValues.value("mapthumbnail").toInt();
        painter.setClipRect(component.rect,Qt::IntersectClip);
        if(state==0)
        {
            painter.setPen(QColor(135,164,143));painter.drawRect(component.rect.adjusted(0,0,-1,-1));
        }
        else
        {
            if(state!=2)
            {
                painter.fillRect(component.rect,QColor(39,57,43));painter.setPen(QColor(66,83,61));
                for(int i=1;i<8;++i)
                {
                    const int x=component.rect.x()+component.rect.width()*i/8,y=component.rect.y()+component.rect.height()*i/8;
                    painter.drawLine(x,component.rect.y(),x,component.rect.bottom());painter.drawLine(component.rect.x(),y,component.rect.right(),y);
                }
            }
            const QList<QColor> colors={Qt::green,QColor(0,204,255),Qt::red,Qt::yellow};
            painter.setPen(Qt::NoPen);
            for(int i=0;i<colors.size();++i)
            {
                painter.setBrush(colors[i]);painter.drawEllipse(QPoint(component.rect.x()+component.rect.width()*(i+2)/7,component.rect.y()+component.rect.height()*(i%2+2)/5),3,3);
            }
            const QPoint cursor=state==3?component.rect.bottomRight():component.rect.center();
            painter.setPen(QColor(255,220,92));painter.drawLine(cursor+QPoint(-5,0),cursor+QPoint(5,0));painter.drawLine(cursor+QPoint(0,-5),cursor+QPoint(0,5));
        }
        painter.restore();return;
    }

    if(component.type=="ColumnImage"&&component.rawValues.contains("hudpercent"))
    {
        const double percent=qBound(0.0,component.rawValues.value("hudpercent").toDouble(),1.0);
        if(enabled("horizontal"))
        {
            painter.fillRect(component.rect,QColor(127,122,94));
            QRect inner=component.rect.adjusted(1,1,-1,-1);painter.fillRect(inner,QColor(42,55,46));
            inner.setWidth(int(qMax(0,inner.width())*percent));QColor color=QColor::fromRgba(component.color);color.setAlpha(255);painter.fillRect(inner,color);
        }
        else if(!component.image.isNull())
        {
            const int width=component.image.width(),height=component.image.height();
            const double lag=qBound(percent,component.rawValues.value("hudlag").toDouble(),1.0);
            if(lag>percent)
            {
                const QRect source(0,height-int(lag*height),width,int(lag*height)-int(percent*height));
                QRect destination(component.rect.x(),component.rect.y()+source.y(),width,source.height());
                // ColumnImage keeps the lag segment's source-pixel height even when stretched.
                if(component.stretch) destination={component.rect.x(),component.rect.y()+component.rect.height()-int(lag*component.rect.height()),component.rect.width(),source.height()};
                painter.setOpacity(80.0/255);painter.drawPixmap(destination,component.image,source);
            }
            const QRect source(0,height-int(percent*height),width,int(percent*height));
            QRect destination(component.rect.x(),component.rect.y()+source.y(),width,source.height());
            if(component.stretch)
            {
                const int filledHeight=int(percent*component.rect.height());
                destination={component.rect.x(),component.rect.y()+component.rect.height()-filledHeight,component.rect.width(),filledHeight};
            }
            painter.setOpacity(1);painter.drawPixmap(destination,component.image,source);
        }
        painter.restore();return;
    }
    if(component.type=="TransImage"&&component.rawValues.contains("hudalpha"))
    {
        painter.setOpacity(72.0/255);
        if(!component.image.isNull())
        {
            if(component.stretch) painter.drawPixmap(component.rect,component.image);
            else painter.drawPixmap(component.rect.topLeft(),component.image);
        }
        painter.restore();return;
    }
    if(component.type=="CheckBox"&&component.rawValues.contains("hudcheckbox")&&component.image.isNull()&&component.text.isEmpty())
    {
        if(enabled("hudcheckbox"))
        {
            const bool checked=enabled("hudchecked");painter.fillRect(component.rect,checked?QColor(47,85,65):QColor(220,211,177));
            painter.setPen(checked?QColor(248,232,188):QColor(35,79,67));painter.drawRect(component.rect.adjusted(0,0,-1,-1));
            QFont font=painter.font();font.setPixelSize(component.fontSize);painter.setFont(font);
            painter.drawText(component.rect.adjusted(4,2,-4,-2),Qt::AlignCenter,component.text);
        }
        painter.restore();return;
    }
    if(component.type=="NpcInfo")
    {
        painter.setClipRect(component.rect);
        const int state=component.rawValues.value("state").toInt();
        if(state!=6)
        {
            const int width=component.rawValues.value("width").toInt(),height=component.rawValues.value("height").toInt();
            const QRect bar(component.rect.width()/2-width/2+component.rawValues.value("alignx").toInt(),component.rawValues.value("aligny").toInt(),width,height);
            const float current=state==3?1.0f:state==4?0.0f:0.5f,lag=state==3?1.0f:state==4?0.0f:0.75f;
            const QColor fill=state==1?QColor(16,165,28,230):state==2?QColor(40,30,245,230):QColor(163,18,21,230);
            const QColor trailing=state==1?QColor(140,255,120,160):state==2?QColor(140,140,255,160):QColor(255,120,140,160);
            painter.fillRect(bar,QColor(0,0,0,180));
            painter.fillRect(QRect(bar.x()+int(width*current),bar.y(),int(width*lag)-int(width*current),height),trailing);
            painter.fillRect(QRect(bar.topLeft(),QSize(int(width*current),height)),fill);
            if(!component.text.isEmpty())
            {
                QFont font=painter.font();font.setPixelSize(12);painter.setFont(font);QFontMetrics metrics(font);
                const int textWidth=metrics.horizontalAdvance(component.text),textHeight=metrics.height();
                painter.setPen(QColor(255,255,255,208));
                painter.drawText(component.rect.width()/2-textWidth/2,bar.y()+(height-textHeight)/2+metrics.ascent(),component.text);
            }
        }
        painter.restore();return;
    }
    if(component.type=="NoticeBorder") {painter.fillRect(component.rect,QColor::fromRgba(component.color));painter.restore();return;}
    if(component.type=="NoticeText")
    {
        QFont font=painter.font();font.setPixelSize(qMax(1,component.fontSize));painter.setFont(font);const QFontMetrics metrics(font);
        const bool script=component.rawValues.value("noticescript")=="1";
        SystemNoticeLayout layout;layout.panel={0,0,component.rect.width(),component.rect.height()};layout.font=component.fontSize;layout.color=component.color;
        layout.padding=component.rawValues.value("horizontalpadding").toInt();layout.lineGap=component.rawValues.value("linegap").toInt();
        std::vector<std::string> messages;for(const auto& text:component.listBoxItems) messages.push_back(text.toStdString());
        std::vector<TextLayout::ColorTaggedTextLine> lines;const bool plain=!script&&!messages.empty()&&messages.front().find("<color=")==std::string::npos;
        if(plain) for(const auto& text:TextLayout::wrapUtf8Text(messages.front(),layout.contentWidth(),[&](const std::string& value) {return metrics.horizontalAdvance(QString::fromStdString(value));})) lines.push_back({{text,component.color}});
        else lines=layout.lines(messages,script);
        const int lineHeight=plain?metrics.height():layout.lineHeight(script);int widest=0;
        for(const auto& line:lines) {int width=0;for(const auto& run:line) width+=metrics.horizontalAdvance(QString::fromStdString(run.text));widest=qMax(widest,width);}
        if(script) widest=qMin(widest,component.rect.width());
        const int imageHeight=int(lines.size())*lineHeight,left=script?component.rect.x():component.rect.x()+(component.rect.width()-widest)/2;
        const int top=script?component.rect.y()+component.rect.height()-imageHeight:component.rect.y()+(component.rect.height()-imageHeight)/2;
        painter.setClipRect(QRect(left,top,widest,imageHeight));
        for(std::size_t row=0;row<lines.size();++row)
        {
            int width=0;for(const auto& run:lines[row]) width+=metrics.horizontalAdvance(QString::fromStdString(run.text));
            int x=left+(script||plain?0:(widest-width)/2);
            for(const auto& run:lines[row])
            {
                QColor color=QColor::fromRgba(run.color);color.setAlpha(color.alpha()*color.alpha()/255);painter.setPen(color);
                const auto text=QString::fromStdString(run.text);painter.drawText(x,top+int(row)*lineHeight+metrics.ascent(),text);x+=metrics.horizontalAdvance(text);
            }
        }
        painter.restore();return;
    }
    if (component.type == "ListBox")
    {
        const int rows = component.listBoxItems.size();
        const int rowHeight = component.fitItems && rows > 0 ? component.rect.height() / rows : component.itemHeight;
        QFont font = painter.font(); font.setPixelSize(qMax(1, component.fontSize)); painter.setFont(font);
        for (int row = 0; row < rows; ++row)
        {
            painter.setPen(QColor::fromRgba(row == component.selectedIndex ? component.selColor : component.color));
            const QRect rectangle(component.rect.x(), component.rect.y() + row * rowHeight, component.rect.width(), rowHeight);
            painter.drawText(rectangle, Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine | Qt::TextDontClip, component.listBoxItems[row]);
        }
        painter.restore();
        return;
    }

    if (component.type == "DiceGame")
    {
        DiceGameLayout layout;
        for (auto it = component.rawValues.cbegin(); it != component.rawValues.cend(); ++it)
            layout.values[it.key().toStdString()] = it.value().toStdString();
        const double scale = component.rawValues.value("diceeditview") == "true" ? 1.0 : std::min({1.0, std::max(1,component.rect.width()-16)/549.0, std::max(1,component.rect.height()-16)/300.0});
        painter.translate((component.rect.width()-qRound(549*scale))/2,(component.rect.height()-qRound(300*scale))/2);
        painter.scale(scale,scale);
        const auto rectangle = [&](const std::string& id)
        {
            const auto r = layout.rectangle(id); return QRect(r.x,r.y,r.w,r.h);
        };
        QMap<QString,QPixmap> images;
        bool loaded = true;
        for (const auto& role : DiceGameLayout::roles()) if (*role.imageKey)
        {
            const auto path = QString::fromStdString(layout.raw(role,"Image"));
            images[role.id] = loadImageFromGamePath(path); loaded = loaded && !images[role.id].isNull();
        }
        if (!loaded)
        {
            painter.fillRect(QRect(0,0,549,300),QColor(44,40,36));
            painter.setPen(Qt::white);
            painter.drawText(QRect(12,12,525,276),Qt::AlignCenter|Qt::TextWordWrap,tr("骰子图片缺失：游戏采用原回退控件，当前为设计画布示意。"));
            painter.restore(); return;
        }
        const auto image = [&](const std::string& role, const QString& key)
        { painter.drawPixmap(rectangle(role),images.value(key)); };
        for (const char* id : {"Frame","PlayerTalk","NpcTalk"}) image(id,id);
        image("PlayerTray","Nameplate"); image("NpcTray","Nameplate");
        for (const char* id : {"PlayerPortrait","NpcPortrait","Silver"}) image(id,id);
        for (int i=1; i<=3; ++i)
        {
            image("PlayerDice"+std::to_string(i),"Face"+QString::number(i));
            image("NpcDice"+std::to_string(i),"Face"+QString::number(7-i));
        }
        const QString result = component.selectedIndex==3 ? "ResultWin" : component.selectedIndex==4 ? "ResultLose" : component.selectedIndex==5 ? "ResultTie" : "Versus";
        image(result.toStdString(),result);
        const auto text = [&](const std::string& id,const QString& value)
        {
            const auto r = layout.rectangle(id); const auto& role=DiceGameLayout::role(id);
            QFont font=painter.font(); font.setPixelSize(layout.number(id,"Font",role.font)); painter.setFont(font);
            painter.setPen(QColor::fromRgba(layout.tint(id)));
            if (role.talk)
            {
                const auto lines=TextLayout::wrapUtf8Text(value.toStdString(),TextLayout::charactersPerLineForWidth(r.w,font.pixelSize()));
                const long long pitch=static_cast<long long>(font.pixelSize())+layout.number(id,"LineGap",3);
                const int count=static_cast<int>(std::min<long long>(lines.size(),std::max(1LL,(static_cast<long long>(r.h)+layout.number(id,"LineGap",3))/std::max(1LL,pitch))));
                for(int i=0;i<count;++i) painter.drawText(QPoint(r.x,r.y+static_cast<int>(i*pitch)+QFontMetrics(font).ascent()),QString::fromStdString(lines[i]));
            }
            else painter.drawText(QPoint(id=="Title"?r.x+549/2-font.pixelSize():r.x,r.y+(id=="Title"?3:0)+QFontMetrics(font).ascent()),value);
        };
        text("Title",tr("骰子"));text("CloseSymbol",QString::fromUtf8("×"));text("PlayerName",tr("玩家"));text("NpcName",tr("庄家"));
        text("PlayerText",tr("不要走，决战到天亮。"));text("NpcText",tr("谁掌控全局谁就能笑到最后。"));text("Stake","50");
        const auto button = [&](const char* id,const QString& label)
        {
            const auto r=rectangle(id);painter.fillRect(r,QColor(68,49,30,235));painter.setPen(QColor(224,184,98));painter.drawLine(r.topLeft(),r.topRight());
            QFont font=painter.font();font.setPixelSize(12);painter.setFont(font);painter.setPen(Qt::white);painter.drawText(r,Qt::AlignCenter,label);
        };
        if(component.selectedIndex==1)button("Open",tr("开盅"));
        else if(component.selectedIndex!=2){button("Start",tr("开始"));button("Bet",tr("下注50"));}
        painter.restore();return;
    }

    if(component.type=="ControllerHelp")
    {
        ControllerHelpLayout display;for(auto it=component.rawValues.cbegin();it!=component.rawValues.cend();++it)display.values[it.key().toStdString()]=it.value().toStdString();
        const auto g=display.resolve(component.rect.width(),component.rect.height());
        painter.fillRect(component.rect,QColor::fromRgba(g.background));
        painter.fillRect(QRect(g.panel.x,g.panel.y,g.panel.w,g.panel.h),QColor::fromRgba(g.panelColor));
        painter.fillRect(QRect(g.outer,g.outer,g.panel.w,2),QColor::fromRgba(g.accent));
        const auto text=[&](const QString& value,int x,int y,int size,unsigned int color)
        {
            QFont font=painter.font();font.setPixelSize(size);painter.setFont(font);painter.setPen(QColor::fromRgba(color));
            painter.drawText(QPoint(x,y+QFontMetrics(font).ascent()),value);
        };
        text(QString::fromUtf8("手柄操作说明"),g.contentLeft,g.titleTop,g.titleFont,g.title);
        text(QString::fromUtf8("游戏已暂停"),g.contentLeft,g.subtitleTop,qMax(12,g.textFont),g.muted);
        if(g.width>=1100&&!component.image.isNull())
        {
            const auto r=g.imageRectangle(component.image.width(),component.image.height());painter.drawPixmap(QRect(r.x,r.y,r.w,r.h),component.image);
        }
        else
        {
            text(QString::fromUtf8("世界场景"),g.contentLeft,g.contentTop,g.textFont+2,g.section);
            for(int i=0;i<18;++i)text(component.listBoxItems.value(i),g.contentLeft,g.contentTop+g.lineHeight*(i+1),g.textFont,g.text);
            text(QString::fromUtf8("菜单与剧情"),g.menuLeft,g.menuTop,g.textFont+2,g.section);
            for(int i=0;i<12;++i)text(component.listBoxItems.value(i+18),g.menuLeft,g.menuTop+g.lineHeight*(i+1),g.textFont,g.text);
        }
        text(QString::fromUtf8("按任意键、手柄按钮或轻触屏幕关闭说明"),g.contentLeft,g.footerTop,g.textFont,g.title);
        painter.restore();return;
    }
    if(component.rawValues.contains("skillsstate"))
    {
        const int state=component.rawValues.value("skillsstate").toInt();
        if(enabled("flat"))
        {
            painter.fillRect(component.rect,QColor(171,146,93));
            if(component.rect.width()>2&&component.rect.height()>2)painter.fillRect(component.rect.adjusted(1,1,-1,-1),QColor(44,84,70));
        }
        else if(!component.image.isNull())
        {
            if(component.stretch)painter.drawPixmap(component.rect,component.image);
            else painter.drawPixmap(component.rect.topLeft(),component.image);
        }
        if(enabled("skillsfilled"))
        {
            const QRect item(component.rect.x()+qRound(component.rect.width()*.1),component.rect.y()+qRound(component.rect.height()*.1),qRound(component.rect.width()*.8),qRound(component.rect.height()*.8));
            painter.fillRect(item,QColor(75,143,198));
            const int coldHeight=qCeil(item.height()*.6);
            painter.fillRect(QRect(item.x(),item.y()+item.height()-coldHeight,item.width(),coldHeight),QColor(0,0,0,170));
        }
        else if(component.type!="TextButton"&&!component.secondaryImage.isNull())painter.drawPixmap(component.rect,component.secondaryImage);
        else if(!component.text.isEmpty())
        {
            QFont font=painter.font();const bool round=component.type!="TextButton";
            font.setPixelSize(qMax(1,round?qRound(component.rawValues.value("skillsrange").toInt()*.8):component.fontSize));painter.setFont(font);
            painter.setPen(QColor::fromRgba(round?(state==2?0xFFFF0000:0xFF000000):component.color));
            const QFontMetrics metrics(font);const int width=metrics.horizontalAdvance(component.text),height=metrics.height();
            const int x=component.rect.x()+qRound((component.rect.width()-width)/2.0-(round?width*.05:0));
            const int y=component.rect.y()+qRound((component.rect.height()-height)/2.0)+metrics.ascent();
            painter.drawText(QPoint(x,y),component.text);
        }
        if(enabled("skillschecked"))
        {
            const int width=component.rect.width(),pad=width/4;
            painter.fillRect(component.rect.x()+pad,component.rect.y()+component.rect.height()-pad,width-pad*2,qMax(2,width/18),QColor(255,220,92,230));
        }
        if(state==6)
        {
            painter.setClipRect(component.rect);painter.setPen(QPen(QColor(100,220,255),1,Qt::DashLine));
            if(component.type=="TextButton")painter.drawRect(component.rect.adjusted(0,0,-1,-1));
            else{const int radius=component.rawValues.value("skillsrange").toInt()/2;painter.drawEllipse(QPoint(component.rect.x()+component.rect.width()/2,component.rect.y()+component.rect.height()/2),radius,radius);}
        }
        painter.restore();return;
    }
    if (enabled("flat") && (component.type == "Scrollbar" || component.type == "Button" || component.type == "DragButton"))
    {
        if (component.type == "Scrollbar") painter.fillRect(component.rect,
            QColor::fromRgba(component.rawValues.value("backgroundcolor", "0xFFB7B697").toUInt(nullptr, 0)));
        else
        {
            painter.fillRect(component.rect, QColor::fromRgba(component.rawValues.value("bordercolor", "0xFFAB925D").toUInt(nullptr, 0)));
            painter.fillRect(component.rect.adjusted(1,1,-1,-1),
                QColor::fromRgba(component.rawValues.value(component.rawValues.value("editorbuttonstate") == "2" ? "pressedbackgroundcolor"
                    : component.rawValues.value("editorbuttonstate") == "1" ? "hoverbackgroundcolor" : "backgroundcolor",
                    component.rawValues.value("backgroundcolor", "0xFF2C5446")).toUInt(nullptr, 0)));
        }
    }
    else if (component.type == "Joystick")
    {
        const bool sample=component.rawValues.contains("joysticksample");
        const int state=component.rawValues.value("joysticksample").toInt();
        if (!component.image.isNull())
        {
            // 运行时 Joystick 总是拉伸背景图：RoundButton 构造函数设置 stretch=true，
            // 且 Joystick::initFromIni 不读取 INI 中的 Stretch 字段。
            painter.drawPixmap(component.rect, component.image);
        }
        else if (!sample)
        {
            painter.fillRect(component.rect, QColor(180, 180, 180, 60));
        }

        if (!component.secondaryImage.isNull() && (!sample || state==1 || state==2))
        {
            QPoint thumbPosition(
                component.rect.center().x() - component.secondaryImage.width() / 2,
                component.rect.center().y() - component.secondaryImage.height() / 2);
            if(sample)
            {
                const int range=component.rawValues.value("joystickrange").toInt();
                const int move=state==2?qMin(component.rect.width()/2-1,qMax(1,range/4)):0;
                thumbPosition={component.rect.x()+component.rect.width()/2+move-component.secondaryImage.width()/2,
                    component.rect.y()+component.rect.height()/2-component.secondaryImage.height()/2};
            }
            painter.drawPixmap(thumbPosition, component.secondaryImage);
        }
        if(sample&&state==3)
        {
            painter.setClipRect(component.rect);painter.setPen(QPen(QColor(100,220,255),1,Qt::DashLine));
            const int radius=component.rawValues.value("joystickrange").toInt()/2;
            painter.drawEllipse(QPoint(component.rect.x()+component.rect.width()/2,component.rect.y()+component.rect.height()/2),radius,radius);
        }
    }
    else if (component.rawValues.value("optiondefaultbackground") == "true")
    {
        painter.fillRect(component.rect, QColor(18, 15, 13, 238));
    }
    else if (component.type == "CheckBox" && !component.text.isEmpty())
    {
        const bool highlighted = component.rawValues.value("editorbuttonstate").toInt() > 0 || enabled("hudchecked");
        painter.fillRect(component.rect, QColor(154, 128, 76));
        painter.fillRect(component.rect.adjusted(1, 1, -1, -1), highlighted ? QColor(36, 77, 67) : QColor(238, 227, 197));
        if (!component.image.isNull()) painter.drawPixmap(component.rect, component.image);
    }
    else if (component.type == "FlatTextButton")
    {
        const QPoint pointer = canvasToMenu(mapFromGlobal(QCursor::pos())) - coordinateOffset;
        const int previewState = component.rawValues.value("editorbuttonstate", "-1").toInt();
        const bool hovered = previewState >= 0 ? previewState > 0 : component.rect.contains(pointer);
        const QString backgroundKey = previewState == 2 || (previewState < 0 && hovered && QApplication::mouseButtons().testFlag(Qt::LeftButton))
            ? "pressedbackgroundcolor" : (hovered ? "hoverbackgroundcolor" : "backgroundcolor");
        const int visual = qBound(0, previewState >= 0 ? previewState : hovered ? 1 : 0, 2);
        // FlatTextButtonStyle paints its border before the one-pixel inset background.
        const unsigned int borders[] = {0x91B09258, 0xBEDAB66C, 0xDCEECC82};
        const unsigned int backgrounds[] = {0xE6201914, 0xE6362219, 0xE64E2C20};
        const unsigned int disabledBorders[] = {0x9B5C5C5C, 0xB98C7858, 0xD2A5875C};
        painter.fillRect(component.rect, QColor::fromRgba(enabled("optionunavailable") ? disabledBorders[visual] : borders[visual]));
        painter.fillRect(component.rect.adjusted(1, 1, -1, -1), QColor::fromRgba(
            component.rawValues.value(backgroundKey, QString::number(backgrounds[visual])).toUInt(nullptr, 0)));
        if (!component.image.isNull())
        {
            if (component.stretch) painter.drawPixmap(component.rect, component.image);
            else painter.drawPixmap(component.rect.topLeft(), component.image);
        }
    }
    else if (!component.image.isNull())
    {
        const int sourceBorder = component.rawValues.value("nineslice").toInt();
        const int targetBorder = component.rawValues.value("nineslicewidth", QString::number(sourceBorder)).toInt();
        if (component.stretch && sourceBorder > 0 && targetBorder > 0)
            drawNineSlice(painter, component.rect, component.image, sourceBorder, targetBorder);
        else if (component.stretch && enabled("keepaspect"))
        {
            const QSize size = component.image.size().scaled(component.rect.size(), Qt::KeepAspectRatio);
            painter.drawPixmap(QRect(component.rect.center() - QPoint(size.width()/2, size.height()/2), size), component.image);
        }
        else if (component.stretch)
        {
            painter.drawPixmap(component.rect, component.image);
        }
        else
        {
            QPoint drawPos(component.rect.x(), component.rect.y());
            painter.drawPixmap(drawPos, component.image);
        }
    }
    else if (component.passiveTextPreview || !editingGuidesVisible || gameWindowViewport.isValid())
    {
        // Runtime labels have no placeholder rectangle behind their text.
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

        if (!component.sampleImage && !component.name.isEmpty() && component.type != "Label" && component.type != "MemoText" && component.type != "TitleTeamText"
            && component.type != "TalkLabel" && !component.type.endsWith("TextButton"))
        {
            painter.setPen(QColor(255, 255, 255, 200));
            QFont font = painter.font();
            font.setPixelSize(qBound(10, component.rect.height() / 3, 14));
            painter.setFont(font);
            painter.drawText(component.rect, Qt::AlignCenter, component.name);
        }
    }

    if (component.type == "Item" && enabled("drawslot"))
    {
        painter.setPen(QColor(151,134,96)); painter.drawRect(component.rect.adjusted(0,0,-1,-1));
    }
    if(component.type=="Item"&&component.rawValues.contains("hudslot"))
    {
        if(enabled("hudslot")) {painter.setPen(QColor(218,191,113));painter.setBrush(QColor(63,92,76));painter.drawEllipse(component.rect.adjusted(5,5,-5,-5));}
        const double cooldown=component.rawValues.value("hudcooldown").toDouble();
        if(cooldown>0) {QRect shade=component.rect;shade.setHeight(int(shade.height()*cooldown));painter.fillRect(shade,QColor(0,0,0,150));}
    }
    if (component.type == "Item" && !component.text.isEmpty())
    {
        QFont font = painter.font(); font.setPixelSize(qMax(1, component.fontSize)); painter.setFont(font);
        painter.setPen(QColor::fromRgba(component.color));
        const bool corner = enabled("drawslot") || !component.rawValues.value("backimage1").isEmpty();
        painter.drawText(component.rect, (corner ? Qt::AlignRight | Qt::AlignBottom : Qt::AlignLeft | Qt::AlignTop) | Qt::TextSingleLine, component.text);
    }
    if (component.choiceTextPreview)
    {
        painter.setClipRect(component.rect, Qt::IntersectClip);
        QFont font=painter.font();font.setPixelSize(qMax(1,component.fontSize));painter.setFont(font);
        const QFontMetrics metrics(font);painter.setPen(QColor::fromRgba(component.color));
        const auto lines=TextLayout::wrapUtf8Text(component.text.toStdString(),component.rect.width(),
            [&](const std::string& text) {return metrics.horizontalAdvance(QString::fromStdString(text));});
        int y=component.rect.y()+metrics.ascent();
        for(const auto& line:lines) {painter.drawText(QPoint(component.rect.x(),y),QString::fromStdString(line));y+=metrics.height()+2;}
    }
    else if (component.type == "TitleTeamText")
    {
        if (component.dialogTextPreview) painter.setClipRect(component.rect, Qt::IntersectClip);
        painter.save(); painter.setClipRect(component.rect);
        QFont font = painter.font(); font.setPixelSize(qMax(1,component.fontSize)); painter.setFont(font); painter.setPen(QColor::fromRgba(component.color));
        int y = component.rect.y();
        for (const QString& line : component.text.split('\n'))
        {
            painter.drawText(QPoint(component.rect.x(),y+QFontMetrics(font).ascent()),line);
            y += component.fontSize + component.rawValues.value("linegap").toInt();
        }
        painter.restore();
    }
    else if (component.type == "FlatTextButton" && enabled("titleteamclose"))
    {
        QFont font = painter.font(); font.setPixelSize(qMax(1,component.fontSize)); painter.setFont(font);
        painter.setPen(QColor::fromRgba(component.color));
        painter.drawText(QPoint(component.rect.x()+(component.rect.width()-2*component.fontSize)/2-3,
            component.rect.y()+(component.rect.height()-component.fontSize)/2-1+QFontMetrics(font).ascent()),component.text);
    }
    else if (component.type == "MemoText")
    {
        // MemoText creates exactly five Labels; FitLines changes their height, not count.
        constexpr int rows = 5;
        const bool fit = enabled("fitlines");
        const int height = fit ? component.rect.height() / rows : component.rawValues.value("linesize", "30").toInt();
        const auto lines = component.text.split('\n');
        for (int row = 0; row < rows; ++row)
        {
            const QRect rectangle(component.rect.x(), component.rect.y() + row * height, component.rect.width(), height);
            QFont font = painter.font(); font.setPixelSize(qMax(1, component.fontSize));
            while (fit && font.pixelSize() > 12 && QFontMetrics(font).horizontalAdvance(lines.value(row)) > rectangle.width())
                font.setPixelSize(font.pixelSize() - 1);
            painter.save();painter.setClipRect(rectangle);painter.setFont(font);
            painter.setPen(QColor::fromRgba(component.color));painter.drawText(rectangle, Qt::AlignLeft | Qt::AlignTop, lines.value(row));painter.restore();
        }
    }
    else if (component.passiveTextPreview)
    {
        QFont font=painter.font();int size=qBound(1,component.fontSize,256);
        const bool wrap=enabled("autonextline"),shrink=enabled("autoshrink"),center=enabled("centertext");
        const QString minimumValue=component.rawValues.value("minimumfont");
        const int minimum=shrink?qBound(1,minimumValue.isEmpty()?7:minimumValue.toInt(nullptr,0),size):size;
        std::vector<TextLayout::ColorTaggedTextLine> lines;
        int height=1;
        for(;;)
        {
            font.setPixelSize(size);const QFontMetrics metrics(font);height=qMax(1,metrics.height());
            const auto measure=[&](const std::string& value) {return metrics.horizontalAdvance(QString::fromStdString(value));};
            lines=TextLayout::wrapColorTaggedUtf8Text(component.text.toStdString(),wrap?qMax(1,component.rect.width()):std::numeric_limits<int>::max(),component.color,measure);
            int widest=0;for(const auto& line:lines) {int width=0;for(const auto& run:line) width+=measure(run.text);widest=qMax(widest,width);}
            if(size<=minimum||((component.rect.width()<=0||widest<=component.rect.width())&&(component.rect.height()<=0||int(lines.size())*height<=component.rect.height()))) break;
            --size;
        }
        painter.setFont(font);const QFontMetrics metrics(font);
        if(wrap) painter.setClipRect(component.rect);
        int y=component.rect.y()+(center?qMax(0,(component.rect.height()-int(lines.size())*height)/2):0);
        for(const auto& line:lines)
        {
            if(wrap&&y+height>component.rect.y()+component.rect.height()) break;
            int width=0;for(const auto& run:line) width+=metrics.horizontalAdvance(QString::fromStdString(run.text));
            int x=component.rect.x()+(center?qMax(0,(component.rect.width()-width)/2):0);
            for(const auto& run:line)
            {
                painter.setPen(QColor::fromRgba(run.color));const QString text=QString::fromStdString(run.text);
                painter.drawText(x,y+metrics.ascent(),text);x+=metrics.horizontalAdvance(text);
            }
            y+=height;
        }
    }
    else if (component.type == "Label" || component.type == "TalkLabel"
        || component.type == "TextButton" || component.type == "FlatTextButton" || component.type == "CheckBox")
    {
        painter.setClipRect(component.rect);
        QFont font = painter.font(); font.setPixelSize(qMax(1, component.fontSize));
        const bool checked = component.type == "CheckBox" && (component.rawValues.value("editorbuttonstate").toInt() > 0 || enabled("hudchecked"));
        painter.setFont(font); painter.setPen(component.type == "CheckBox" ? (checked ? QColor(248, 232, 188) : QColor(35, 79, 67)) : QColor::fromRgba(component.color));
        const bool wrap = component.type == "MemoText" || enabled("autonextline");
        const bool center = component.type.endsWith("TextButton") || component.type == "CheckBox" || enabled("centertext");
        const int flags = (center ? Qt::AlignCenter : Qt::AlignLeft | Qt::AlignTop) | (wrap ? Qt::TextWordWrap : Qt::TextSingleLine);
        const QString text = component.text.isEmpty() && editingGuidesVisible && !gameWindowViewport.isValid() ? component.name : component.text;
        const QRect textRectangle = component.type == "FlatTextButton" ? component.rect.adjusted(4, 4, -4, -4)
            : component.type == "CheckBox" ? component.rect.adjusted(4, 2, -4, -2) : component.rect;
        const int minimumFont = qMax(1, component.rawValues.value("minimumfont", component.type == "FlatTextButton" ? "11" : "7").toInt());
        const bool shrink = component.type == "FlatTextButton" || component.type == "CheckBox" || enabled("autoshrink");
        while (shrink && font.pixelSize() > minimumFont)
        {
            const QRect bounds = QFontMetrics(font).boundingRect(textRectangle, flags, text);
            if (bounds.width() <= textRectangle.width() && bounds.height() <= textRectangle.height()) break;
            font.setPixelSize(font.pixelSize() - 1);
        }
        painter.setFont(font);
        painter.drawText(textRectangle, flags, text);
        painter.setClipping(false);
    }
    for (const auto& child : component.children)
    {
        drawComponent(painter, child);
    }

    painter.restore();
}

void MenuPreviewCanvas::applyButtonPreviewState(PreviewComponent& component, int state)
{
    if (!component.type.contains("Button") && component.type != "CheckBox") return;
    component.rawValues["editorbuttonstate"] = QString::number(state);
    const QString kind = component.rawValues.value("kind");
    const int up = component.rawValues.value("up", "0").toInt();
    const int down = component.rawValues.value("down", "1").toInt();
    const int hover = kind.compare("trackbtn", Qt::CaseInsensitive) == 0 ? component.rawValues.value("track", "1").toInt() : -1;
    const bool checked = component.type == "CheckBox" && (state > 0 || component.rawValues.value("hudchecked") == "1");
    const QList<int> frames = component.type == "CheckBox" ? (checked ? QList<int>{down, up} : QList<int>{up, down})
        : state == 2 ? QList<int>{down, hover, up} : state == 1 ? QList<int>{hover, up, down} : QList<int>{up, hover, down};
    if (!component.imagePath.isEmpty())
    {
        component.image = {};
        for (int frame : frames)
        {
            if (frame < 0) continue;
            component.image = loadImageFromGamePath(component.imagePath, frame);
            if (!component.image.isNull()) { component.imageFrame = frame; break; }
        }
    }
    const QString key = state == 2 ? "presscolor" : state == 1 ? "hovercolor" : "normalcolor";
    if (component.type == "FlatTextButton" && !component.rawValues.contains("color"))
        component.color = state == 0 ? 0xFFFFE7B0 : 0xFFFFFFFF;
    if (component.type == "FlatTextButton" && component.rawValues.value("optionunavailable") == "true")
        component.color = state == 0 ? 0xFFAAAAAA : state == 1 ? 0xFFD8C9B0 : 0xFFFFFFFF;
    if (component.rawValues.contains(key)) component.color = QColor::fromString(component.rawValues.value(key)).isValid()
        ? QColor::fromString(component.rawValues.value(key)).rgba() : component.rawValues.value(key).toUInt(nullptr, 0);
}

QPixmap MenuPreviewCanvas::componentThumbnail(PreviewComponent component, int state)
{
    applyButtonPreviewState(component, state);
    QPixmap image(160, 70);
    image.fill(QColor(35, 38, 42));
    if (component.rect.width() <= 0 || component.rect.height() <= 0) return image;
    QPainter painter(&image);
    const double scale = std::min({1.0, 152.0 / component.rect.width(), 62.0 / component.rect.height()});
    painter.translate((160 - component.rect.width() * scale) / 2, (70 - component.rect.height() * scale) / 2);
    painter.scale(scale, scale);
    painter.translate(-component.rect.topLeft());
    component.visible = true;
    const bool previousGuides = editingGuidesVisible;
    editingGuidesVisible = false;
    drawComponent(painter, component);
    editingGuidesVisible = previousGuides;
    return image;
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
    if (component.editorId.endsWith("/slideBtn"))
        if (const auto* parent = findComponentById(component.editorId.chopped(9))) drawSelection(painter, *parent, coordinateOffset);

    if (component.type == "Scrollbar" && !component.children.isEmpty())
    {
        const auto& slider = component.children.first();
        const bool vertical = component.rawValues.value("style", "1").toInt() == 1;
        const bool fitted = QStringList{"1", "true", "yes", "on"}.contains(component.rawValues.value("fittrack").toLower());
        const int begin = fitted ? 0 : component.rawValues.value("slidebegin", "5").toInt();
        const int end = fitted ? qMax(0, vertical ? component.rect.height() - slider.rect.height() : component.rect.width() - slider.rect.width())
            : component.rawValues.value("slideend", "145").toInt();
        painter.setPen(QPen(QColor(255, 195, 70), 1.0 / scaleFactor, Qt::DashLine));
        int endpointIndex = 0;
        for (int position : {begin, end})
        {
            QRect endpoint = slider.rect;
            if (vertical) endpoint.moveTop(component.rect.top() + position);
            else endpoint.moveLeft(component.rect.left() + position);
            painter.drawRect(endpoint);
            QFont labelFont = painter.font();
            labelFont.setPixelSize(qMax(8, qRound(12 / scaleFactor)));
            painter.setFont(labelFont);
            painter.drawText(endpoint.topRight() + QPoint(qRound(6 / scaleFactor), 0), endpointIndex++ == 0 ? tr("起点") : tr("终点"));
        }
    }

    const QList<QRect> handles = component.resizable && !gameWindowViewport.isValid() ? getHandleRects(selectRect) : QList<QRect>();

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

    const auto& roots = MenuPageContext::isUiResource(normalizedResourcePath) ? resourceRoots : contentResourceRoots;
    for (const QString& root : roots)
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

        // Ordinary images have one frame, just like IMP's common-image loader.
        if (frameIndex != 0)
            return QPixmap();
        QPixmap pixmap;
        if (pixmap.load(fullPath))
            return pixmap;
        return QPixmap();
    }

    return QPixmap();
}

void MenuPreviewCanvas::loadComponentImages(PreviewComponent& component)
{
    if (!component.rawValues.contains("partnerheadsample") && !component.sampleImage)
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
    worldBounds = gameWindowViewport.isValid() ? QRect(QPoint(), gameWindowViewport)
        : hasBounds ? bounds : QRect(0, 0, 360, 260);
    if (aspectFitViewport.isValid() && !gameWindowViewport.isValid()) worldBounds = compositionTransform().mapRect(worldBounds).united(QRect(QPoint(), aspectFitViewport));
    const int canvasWidth = qMax(
        static_cast<int>(std::ceil(worldBounds.width() * scaleFactor)) + 40, 400);
    const int canvasHeight = qMax(
        static_cast<int>(std::ceil(worldBounds.height() * scaleFactor)) + 40, 300);
    setMinimumSize(canvasWidth, canvasHeight);
    resize(canvasWidth, canvasHeight);
}

QPoint MenuPreviewCanvas::canvasToMenu(const QPoint& canvasPos) const
{
    const QPoint position(
        worldBounds.left() + static_cast<int>(std::floor((canvasPos.x() - 20) / scaleFactor)),
        worldBounds.top() + static_cast<int>(std::floor((canvasPos.y() - 20) / scaleFactor))
    );
    return compositionTransform().inverted().map(position - gameInterfaceOrigin);
}

QPoint MenuPreviewCanvas::menuToCanvas(const QPoint& menuPos) const
{
    const QPoint position = compositionTransform().map(menuPos) + gameInterfaceOrigin;
    return QPoint(
        20 + static_cast<int>(std::lround((position.x() - worldBounds.left()) * scaleFactor)),
        20 + static_cast<int>(std::lround((position.y() - worldBounds.top()) * scaleFactor))
    );
}
