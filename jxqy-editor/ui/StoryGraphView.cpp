#include "StoryGraphView.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QTimer>
#include <QWheelEvent>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace
{
constexpr int PresentationBatchSize = 180;
constexpr int MaximumPresentationItemsPerBatch = 250;
constexpr int MaximumItemsPerPresentedEdge = 3;
constexpr qreal MinimumZoomFactor = 0.15;
constexpr qreal MaximumZoomFactor = 5.0;
constexpr qreal ZoomStep = 1.18;

QColor nodeFillColor(StoryGraphNodeKind kind)
{
    switch (kind)
    {
    case StoryGraphNodeKind::ChunkEntry:
    case StoryGraphNodeKind::FunctionEntry:
        return QColor(47, 125, 90);
    case StoryGraphNodeKind::ChunkExit:
    case StoryGraphNodeKind::FunctionExit:
    case StoryGraphNodeKind::Return:
        return QColor(103, 89, 151);
    case StoryGraphNodeKind::Condition:
    case StoryGraphNodeKind::LoopHeader:
    case StoryGraphNodeKind::Choice:
        return QColor(158, 111, 35);
    case StoryGraphNodeKind::Dialogue:
        return QColor(43, 111, 148);
    case StoryGraphNodeKind::SerialScriptCall:
    case StoryGraphNodeKind::ParallelScriptCall:
    case StoryGraphNodeKind::MapLoad:
        return QColor(35, 125, 127);
    case StoryGraphNodeKind::VariableRead:
    case StoryGraphNodeKind::VariableWrite:
        return QColor(111, 91, 43);
    case StoryGraphNodeKind::Battle:
        return QColor(150, 67, 58);
    case StoryGraphNodeKind::DynamicCall:
    case StoryGraphNodeKind::UnknownCall:
    case StoryGraphNodeKind::MissingTarget:
    case StoryGraphNodeKind::Warning:
        return QColor(135, 73, 73);
    case StoryGraphNodeKind::BasicBlock:
    case StoryGraphNodeKind::Statement:
    case StoryGraphNodeKind::Merge:
    case StoryGraphNodeKind::Label:
    case StoryGraphNodeKind::Goto:
    case StoryGraphNodeKind::Break:
    case StoryGraphNodeKind::Unreachable:
    case StoryGraphNodeKind::RegisteredApiCall:
        return QColor(75, 87, 105);
    }
    return QColor(75, 87, 105);
}

QColor edgeColor(
    StoryGraphEdgeKind kind,
    StoryGraphCertainty certainty)
{
    if (certainty == StoryGraphCertainty::Warning ||
        certainty == StoryGraphCertainty::Unknown)
    {
        return QColor(196, 92, 82);
    }
    if (certainty == StoryGraphCertainty::Dynamic)
        return QColor(204, 139, 54);

    switch (kind)
    {
    case StoryGraphEdgeKind::TrueBranch:
        return QColor(58, 154, 91);
    case StoryGraphEdgeKind::FalseBranch:
        return QColor(196, 81, 81);
    case StoryGraphEdgeKind::Goto:
    case StoryGraphEdgeKind::LoopBack:
        return QColor(135, 91, 181);
    case StoryGraphEdgeKind::Call:
    case StoryGraphEdgeKind::ParallelCall:
        return QColor(52, 143, 166);
    case StoryGraphEdgeKind::Dynamic:
    case StoryGraphEdgeKind::Unknown:
        return QColor(204, 139, 54);
    case StoryGraphEdgeKind::Sequential:
    case StoryGraphEdgeKind::Fallthrough:
    case StoryGraphEdgeKind::LoopBody:
    case StoryGraphEdgeKind::Break:
    case StoryGraphEdgeKind::Return:
        return QColor(122, 132, 145);
    }
    return QColor(122, 132, 145);
}

QString certaintyDisplayText(
    StoryGraphCertainty certainty)
{
    switch (certainty)
    {
    case StoryGraphCertainty::Certain:
        return QApplication::translate(
            "StoryGraphView", "确定");
    case StoryGraphCertainty::Dynamic:
        return QApplication::translate(
            "StoryGraphView", "动态");
    case StoryGraphCertainty::Unknown:
        return QApplication::translate(
            "StoryGraphView", "未知");
    case StoryGraphCertainty::Warning:
        return QApplication::translate(
            "StoryGraphView", "警告");
    }
    return QApplication::translate(
        "StoryGraphView", "未知");
}

QPainterPath pathFromPoints(const QList<QPointF>& points)
{
    QPainterPath path;
    if (points.isEmpty())
        return path;

    path.moveTo(points.constFirst());
    for (int index = 1; index < points.size(); ++index)
        path.lineTo(points[index]);
    return path;
}

QPolygonF arrowPolygon(const QList<QPointF>& points)
{
    if (points.size() < 2)
        return {};

    QPointF tip = points.constLast();
    QPointF previous;
    for (int index = points.size() - 2; index >= 0; --index)
    {
        previous = points[index];
        if (QLineF(previous, tip).length() > 0.01)
            break;
    }

    const QLineF direction(previous, tip);
    if (direction.length() <= 0.01)
        return {};

    const qreal angle = std::atan2(
        direction.dy(), direction.dx());
    constexpr qreal arrowLength = 11.0;
    constexpr qreal arrowHalfAngle = 0.48;
    return {
        tip,
        tip - QPointF(
                  std::cos(angle - arrowHalfAngle) * arrowLength,
                  std::sin(angle - arrowHalfAngle) * arrowLength),
        tip - QPointF(
                  std::cos(angle + arrowHalfAngle) * arrowLength,
                  std::sin(angle + arrowHalfAngle) * arrowLength)
    };
}

QString nodeToolTip(const StoryGraphNode& node)
{
    QStringList lines;
    lines.append(node.title);
    if (!node.summary.isEmpty())
        lines.append(node.summary);
    if (!node.apiName.isEmpty())
        lines.append(QObject::tr("API：%1").arg(node.apiName));
    if (!node.literalTarget.isEmpty())
    {
        lines.append(
            QObject::tr("目标：%1").arg(node.literalTarget));
    }
    if (!node.source.virtualPath.isEmpty())
    {
        lines.append(
            QObject::tr("来源：%1:%2:%3")
                .arg(node.source.virtualPath)
                .arg(node.sourceRange.start.line)
                .arg(node.sourceRange.start.column));
    }
    lines.append(
        QObject::tr("确定性：%1")
            .arg(
                certaintyDisplayText(
                    node.certainty)));
    return lines.join(QLatin1Char('\n'));
}
}

class StoryGraphNodeGraphicsItem final : public QGraphicsItem
{
public:
    StoryGraphNodeGraphicsItem(
        const StoryGraphNode& node,
        const QRectF& sceneRectangle,
        std::function<void(const StoryGraphNode&)> activate)
        : graphNode(node)
        , localRectangle(
              0.0,
              0.0,
              sceneRectangle.width(),
              sceneRectangle.height())
        , activate(std::move(activate))
    {
        setPos(sceneRectangle.topLeft());
        setFlag(QGraphicsItem::ItemIsSelectable);
        setAcceptHoverEvents(true);
        setToolTip(nodeToolTip(node));
        setZValue(10.0);
    }

    QRectF boundingRect() const override
    {
        return localRectangle.adjusted(-2.0, -2.0, 2.0, 2.0);
    }

    void setSearchMatch(bool match)
    {
        if (searchMatch == match)
            return;
        searchMatch = match;
        update();
    }

    bool setExecuted(bool value)
    {
        if (executed == value)
            return false;
        executed = value;
        update();
        return true;
    }

    const StoryGraphNode& node() const
    {
        return graphNode;
    }

    void paint(
        QPainter* painter,
        const QStyleOptionGraphicsItem*,
        QWidget*) override
    {
        painter->setRenderHint(QPainter::Antialiasing);

        QColor fill = nodeFillColor(graphNode.kind);
        if (!isSelected())
            fill.setAlpha(218);
        else
            fill = fill.lighter(122);

        QColor borderColor(232, 237, 243);
        qreal borderWidth = 1.4;
        if (executed)
        {
            fill = fill.lighter(118);
            borderColor = QColor(61, 222, 166);
            borderWidth = 3.2;
        }
        if (searchMatch)
        {
            borderColor = QColor(255, 202, 61);
            borderWidth = executed ? 4.0 : 3.0;
        }
        QPen border(borderColor, borderWidth);
        if (graphNode.certainty != StoryGraphCertainty::Certain)
            border.setStyle(Qt::DashLine);

        painter->setPen(border);
        painter->setBrush(fill);
        painter->drawRoundedRect(
            localRectangle, 8.0, 8.0);

        const QRectF textRectangle =
            localRectangle.adjusted(12.0, 8.0, -12.0, -8.0);
        QFont titleFont = QApplication::font();
        titleFont.setBold(true);
        painter->setFont(titleFont);
        painter->setPen(Qt::white);

        const QFontMetricsF titleMetrics(titleFont);
        const QString elidedTitle =
            titleMetrics.elidedText(
                graphNode.title,
                Qt::ElideRight,
                static_cast<int>(textRectangle.width()));
        painter->drawText(
            textRectangle,
            Qt::AlignLeft | Qt::AlignTop,
            elidedTitle);

        QFont summaryFont = QApplication::font();
        summaryFont.setPointSizeF(
            std::max(7.0, summaryFont.pointSizeF() - 1.0));
        painter->setFont(summaryFont);
        QColor secondary = QColor(Qt::white);
        secondary.setAlpha(220);
        painter->setPen(secondary);

        const qreal summaryTop =
            textRectangle.top() +
            titleMetrics.height() + 5.0;
        const QRectF summaryRectangle(
            textRectangle.left(),
            summaryTop,
            textRectangle.width(),
            std::max(
                0.0,
                textRectangle.bottom() - summaryTop));
        painter->drawText(
            summaryRectangle,
            Qt::AlignLeft | Qt::AlignTop |
                Qt::TextWordWrap,
            graphNode.summary);
    }

protected:
    void mouseDoubleClickEvent(
        QGraphicsSceneMouseEvent* event) override
    {
        if (activate)
            activate(graphNode);
        event->accept();
    }

private:
    StoryGraphNode graphNode;
    QRectF localRectangle;
    std::function<void(const StoryGraphNode&)> activate;
    bool searchMatch = false;
    bool executed = false;
};

StoryGraphView::StoryGraphView(QWidget* parent)
    : QGraphicsView(parent)
{
    auto* graphScene = new QGraphicsScene(this);
    setScene(graphScene);
    setObjectName(QStringLiteral("storyGraphView"));
    setRenderHints(
        QPainter::Antialiasing |
        QPainter::TextAntialiasing |
        QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(
        QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(
        QGraphicsView::AnchorViewCenter);
    setViewportUpdateMode(
        QGraphicsView::BoundingRectViewportUpdate);
    setBackgroundBrush(
        QApplication::palette().brush(QPalette::Base));
}

void StoryGraphView::setGraph(
    const StoryGraphResult& graph,
    const StoryGraphLayoutResult& layout,
    quint64 generation)
{
    ++presentationToken;
    retireCurrentScene();
    currentGraph = graph;
    currentLayout = layout;
    currentGeneration = generation;
    if (traceOverlayGeneration != generation)
    {
        traceOverlayGeneration = 0;
        currentTraceOverlayEpoch = 0;
        currentTraceOverlayRevision = 0;
        traceExecutedNodeIds.clear();
        traceExecutedEdgeIds.clear();
    }
    pendingEdgePlacementIndex = 0;
    pendingEdgeIndex = 0;
    pendingNodeIndex = 0;
    batchScheduled = false;
    completePresentation = false;
    currentVisibleNodeCount = 0;
    currentExecutedNodeCount = 0;
    currentExecutedEdgeCount = 0;
    nodeItems.clear();
    edgePlacementIndexes.clear();
    edgeItemIndexes.clear();
    edgeItems.clear();
    searchMatchNodeIds.clear();
    nextSearchMatchIndex = 0;

    QRectF sceneRectangle = layout.bounds;
    if (!sceneRectangle.isValid() || sceneRectangle.isEmpty())
        sceneRectangle = QRectF(-50.0, -50.0, 100.0, 100.0);
    scene()->setSceneRect(
        sceneRectangle.adjusted(-32.0, -32.0, 32.0, 32.0));

    emit presentationProgressChanged(
        0, currentGraph.nodes.size(), false);
    emit visibleNodeCountChanged(
        0, currentGraph.nodes.size());
    emit searchMatchCountChanged(0);
    scheduleNextPresentationBatch(presentationToken);
}

void StoryGraphView::cancelPendingPresentation()
{
    ++presentationToken;
    batchScheduled = false;
    completePresentation = true;
}

void StoryGraphView::clearGraph()
{
    ++presentationToken;
    retireCurrentScene();
    currentGraph = StoryGraphResult();
    currentLayout = StoryGraphLayoutResult();
    currentGeneration = 0;
    traceOverlayGeneration = 0;
    currentTraceOverlayEpoch = 0;
    currentTraceOverlayRevision = 0;
    traceExecutedNodeIds.clear();
    traceExecutedEdgeIds.clear();
    pendingEdgePlacementIndex = 0;
    pendingEdgeIndex = 0;
    pendingNodeIndex = 0;
    batchScheduled = false;
    completePresentation = true;
    currentVisibleNodeCount = 0;
    currentExecutedNodeCount = 0;
    currentExecutedEdgeCount = 0;
    nodeItems.clear();
    edgePlacementIndexes.clear();
    edgeItemIndexes.clear();
    edgeItems.clear();
    searchMatchNodeIds.clear();
    nextSearchMatchIndex = 0;
    emit presentationProgressChanged(0, 0, true);
    emit visibleNodeCountChanged(0, 0);
    emit searchMatchCountChanged(0);
}

void StoryGraphView::setTraceOverlay(
    const StoryGraphTraceGraphOverlay& overlay,
    quint64 generation)
{
    if (generation == 0 ||
        generation != currentGeneration)
    {
        clearTraceOverlay();
        return;
    }

    if (traceOverlayGeneration == generation &&
        currentTraceOverlayEpoch == overlay.epoch &&
        currentTraceOverlayRevision ==
            overlay.revision)
    {
        return;
    }

    if (traceOverlayGeneration == generation &&
        currentTraceOverlayEpoch == overlay.epoch &&
        currentTraceOverlayRevision <
            overlay.revision &&
        applyTraceOverlayDelta(overlay))
    {
        currentTraceOverlayRevision =
            overlay.revision;
        return;
    }

    traceOverlayGeneration = generation;
    currentTraceOverlayEpoch = overlay.epoch;
    currentTraceOverlayRevision =
        overlay.revision;
    rebuildTraceOverlay(overlay);
}

void StoryGraphView::clearTraceOverlay()
{
    if (traceOverlayGeneration == 0 &&
        currentTraceOverlayEpoch == 0 &&
        currentTraceOverlayRevision == 0 &&
        traceExecutedNodeIds.isEmpty() &&
        traceExecutedEdgeIds.isEmpty())
    {
        return;
    }

    for (const QString& nodeId :
         std::as_const(traceExecutedNodeIds))
    {
        ++currentTraceOverlayWorkItemCount;
        setNodeExecuted(nodeId, false);
    }
    for (const QString& edgeId :
         std::as_const(traceExecutedEdgeIds))
    {
        ++currentTraceOverlayWorkItemCount;
        setEdgeExecuted(edgeId, false);
    }
    traceOverlayGeneration = 0;
    currentTraceOverlayEpoch = 0;
    currentTraceOverlayRevision = 0;
    traceExecutedNodeIds.clear();
    traceExecutedEdgeIds.clear();
    ++currentTraceOverlayRebuildCount;
    viewport()->update();
}

void StoryGraphView::setAllowedNodeKinds(
    const QSet<StoryGraphNodeKind>& kinds)
{
    if (allowedKinds == kinds)
        return;
    allowedKinds = kinds;
    applyVisibilityAndSearch();
}

QSet<StoryGraphNodeKind>
StoryGraphView::allowedNodeKinds() const
{
    return allowedKinds;
}

void StoryGraphView::setSearchText(const QString& text)
{
    const QString normalized = text.trimmed();
    if (normalizedSearchText == normalized)
        return;
    normalizedSearchText = normalized;
    nextSearchMatchIndex = 0;
    applyVisibilityAndSearch();
}

QString StoryGraphView::searchText() const
{
    return normalizedSearchText;
}

bool StoryGraphView::focusNextSearchMatch()
{
    if (searchMatchNodeIds.isEmpty())
        return false;

    if (nextSearchMatchIndex >= searchMatchNodeIds.size())
        nextSearchMatchIndex = 0;
    const QString nodeId =
        searchMatchNodeIds[nextSearchMatchIndex++];
    StoryGraphNodeGraphicsItem* item =
        nodeItems.value(nodeId, nullptr);
    if (!item || !item->isVisible())
        return false;

    scene()->clearSelection();
    item->setSelected(true);
    centerOn(item);
    return true;
}

void StoryGraphView::zoomIn()
{
    applyZoomFactor(currentZoomFactor * ZoomStep);
}

void StoryGraphView::zoomOut()
{
    applyZoomFactor(currentZoomFactor / ZoomStep);
}

void StoryGraphView::resetZoom()
{
    applyZoomFactor(1.0);
}

void StoryGraphView::fitGraphInView()
{
    QRectF visibleBounds;
    for (StoryGraphNodeGraphicsItem* item :
         std::as_const(nodeItems))
    {
        if (item && item->isVisible())
        {
            visibleBounds = visibleBounds.united(
                item->sceneBoundingRect());
        }
    }
    if (!visibleBounds.isValid() || visibleBounds.isEmpty())
        visibleBounds = scene()->itemsBoundingRect();
    if (!visibleBounds.isValid() || visibleBounds.isEmpty())
        return;

    fitInView(
        visibleBounds.adjusted(
            -24.0, -24.0, 24.0, 24.0),
        Qt::KeepAspectRatio);
    currentZoomFactor =
        std::sqrt(
            std::abs(
                transform().m11() *
                transform().m22()));
    emit zoomFactorChanged(currentZoomFactor);
}

quint64 StoryGraphView::generation() const
{
    return currentGeneration;
}

int StoryGraphView::presentedNodeCount() const
{
    return nodeItems.size();
}

int StoryGraphView::visibleNodeCount() const
{
    return currentVisibleNodeCount;
}

int StoryGraphView::searchMatchCount() const
{
    return searchMatchNodeIds.size();
}

int StoryGraphView::executedNodeCount() const
{
    return currentExecutedNodeCount;
}

int StoryGraphView::executedEdgeCount() const
{
    return currentExecutedEdgeCount;
}

quint64 StoryGraphView::traceOverlayEpoch() const
{
    return currentTraceOverlayEpoch;
}

quint64 StoryGraphView::traceOverlayRevision() const
{
    return currentTraceOverlayRevision;
}

quint64 StoryGraphView::
traceOverlayRebuildCount() const
{
    return currentTraceOverlayRebuildCount;
}

quint64 StoryGraphView::
traceOverlayWorkItemCount() const
{
    return currentTraceOverlayWorkItemCount;
}

bool StoryGraphView::presentationComplete() const
{
    return completePresentation;
}

qreal StoryGraphView::zoomFactor() const
{
    return currentZoomFactor;
}

void StoryGraphView::wheelEvent(QWheelEvent* event)
{
    if (event->angleDelta().y() == 0)
    {
        QGraphicsView::wheelEvent(event);
        return;
    }

    if (event->angleDelta().y() > 0)
        zoomIn();
    else
        zoomOut();
    event->accept();
}

void StoryGraphView::scheduleNextPresentationBatch(
    quint64 token)
{
    if (batchScheduled)
        return;
    batchScheduled = true;
    QTimer::singleShot(
        0,
        this,
        [this, token]()
        {
            if (token != presentationToken)
                return;
            batchScheduled = false;
            appendPresentationBatch(token);
        });
}

void StoryGraphView::appendPresentationBatch(
    quint64 token)
{
    if (token != presentationToken)
        return;

    int remainingWork = PresentationBatchSize;
    int remainingItems =
        MaximumPresentationItemsPerBatch;
    while (remainingWork > 0 &&
           pendingNodeIndex < currentGraph.nodes.size())
    {
        remainingItems -=
            addNodeItem(
                currentGraph.nodes[pendingNodeIndex]);
        ++pendingNodeIndex;
        --remainingWork;
    }
    while (remainingWork > 0 &&
           pendingEdgePlacementIndex <
               currentLayout.edgePlacements.size())
    {
        edgePlacementIndexes.insert(
            currentLayout.
                edgePlacements[
                    pendingEdgePlacementIndex].edgeId,
            pendingEdgePlacementIndex);
        ++pendingEdgePlacementIndex;
        --remainingWork;
    }
    while (remainingWork > 0 &&
           remainingItems >=
               MaximumItemsPerPresentedEdge &&
           pendingEdgeIndex < currentGraph.edges.size())
    {
        remainingItems -=
            addEdgeItem(
                currentGraph.edges[pendingEdgeIndex]);
        ++pendingEdgeIndex;
        --remainingWork;
    }

    const bool finished =
        pendingEdgePlacementIndex >=
            currentLayout.edgePlacements.size() &&
        pendingEdgeIndex >= currentGraph.edges.size() &&
        pendingNodeIndex >= currentGraph.nodes.size();
    completePresentation = finished;
    if (finished)
    {
        std::sort(
            searchMatchNodeIds.begin(),
            searchMatchNodeIds.end());
        if (nextSearchMatchIndex >=
            searchMatchNodeIds.size())
        {
            nextSearchMatchIndex = 0;
        }
    }
    emit visibleNodeCountChanged(
        currentVisibleNodeCount,
        currentGraph.nodes.size());
    emit searchMatchCountChanged(
        searchMatchNodeIds.size());
    emit presentationProgressChanged(
        nodeItems.size(),
        currentGraph.nodes.size(),
        finished);
    emit presentationBatchProcessed(
        PresentationBatchSize -
            remainingWork);
    if (!finished)
        scheduleNextPresentationBatch(token);
}

int StoryGraphView::addNodeItem(
    const StoryGraphNode& node)
{
    const auto rectangle =
        currentLayout.nodeRectangles.constFind(node.id);
    if (rectangle == currentLayout.nodeRectangles.constEnd())
        return 0;

    auto* item = new StoryGraphNodeGraphicsItem(
        node,
        rectangle.value(),
        [this](const StoryGraphNode& activatedNode)
        {
            emit nodeActivated(activatedNode);
        });
    scene()->addItem(item);
    nodeItems.insert(node.id, item);

    const bool visible =
        nodeKindIsAllowed(node.kind);
    item->setVisible(visible);
    if (visible)
        ++currentVisibleNodeCount;

    const bool match =
        visible && nodeMatchesSearch(node);
    item->setSearchMatch(match);
    if (item->setExecuted(
            traceOverlayGeneration ==
                currentGeneration &&
            traceExecutedNodeIds.contains(
                node.id)))
    {
        ++currentExecutedNodeCount;
    }
    if (match)
        searchMatchNodeIds.append(node.id);
    return 1;
}

int StoryGraphView::addEdgeItem(
    const StoryGraphEdge& edge)
{
    const auto placementIndex =
        edgePlacementIndexes.constFind(edge.id);
    if (placementIndex ==
            edgePlacementIndexes.constEnd() ||
        placementIndex.value() < 0 ||
        placementIndex.value() >=
            currentLayout.edgePlacements.size())
    {
        return 0;
    }
    const StoryGraphEdgePlacement& placement =
        currentLayout.edgePlacements[
            placementIndex.value()];
    if (placement.pathPoints.size() < 2)
        return 0;

    const QColor color =
        edgeColor(edge.kind, edge.certainty);
    QPen pen(color, 1.8);
    if (edge.certainty != StoryGraphCertainty::Certain ||
        edge.kind == StoryGraphEdgeKind::Dynamic ||
        edge.kind == StoryGraphEdgeKind::Unknown)
    {
        pen.setStyle(Qt::DashLine);
    }

    auto* pathItem =
        scene()->addPath(
            pathFromPoints(placement.pathPoints),
            pen);
    pathItem->setZValue(0.0);

    QGraphicsPolygonItem* arrowItem = nullptr;
    const QPolygonF arrow =
        arrowPolygon(placement.pathPoints);
    if (!arrow.isEmpty())
    {
        arrowItem =
            scene()->addPolygon(
                arrow,
                QPen(color),
                QBrush(color));
        arrowItem->setZValue(1.0);
    }

    QGraphicsSimpleTextItem* labelItem = nullptr;
    if (!edge.label.isEmpty())
    {
        labelItem =
            scene()->addSimpleText(edge.label);
        labelItem->setBrush(color);
        labelItem->setZValue(2.0);
        const QPointF middle =
            placement.pathPoints[
                placement.pathPoints.size() / 2];
        labelItem->setPos(
            middle +
            QPointF(4.0, -labelItem->boundingRect().height()));
    }

    edgeItems.append({
        edge.id,
        edge.fromNodeId,
        edge.toNodeId,
        pathItem,
        arrowItem,
        labelItem,
        pen,
        color,
        false
    });
    edgeItemIndexes.insert(
        edge.id,
        edgeItems.size() - 1);
    const StoryGraphNodeGraphicsItem* from =
        nodeItems.value(edge.fromNodeId, nullptr);
    const StoryGraphNodeGraphicsItem* to =
        nodeItems.value(edge.toNodeId, nullptr);
    const bool visible =
        from && to &&
        from->isVisible() && to->isVisible();
    pathItem->setVisible(visible);
    if (arrowItem)
        arrowItem->setVisible(visible);
    if (labelItem)
        labelItem->setVisible(visible);
    if (traceOverlayGeneration ==
            currentGeneration &&
        traceExecutedEdgeIds.contains(edge.id))
    {
        setEdgeExecuted(edge.id, true);
    }
    return 1 +
        (arrowItem ? 1 : 0) +
        (labelItem ? 1 : 0);
}

void StoryGraphView::applyVisibilityAndSearch()
{
    searchMatchNodeIds.clear();
    int visibleCount = 0;

    for (auto iterator = nodeItems.cbegin();
         iterator != nodeItems.cend();
         ++iterator)
    {
        StoryGraphNodeGraphicsItem* item = iterator.value();
        if (!item)
            continue;

        const bool visible =
            nodeKindIsAllowed(item->node().kind);
        item->setVisible(visible);
        if (visible)
            ++visibleCount;

        const bool match =
            visible && nodeMatchesSearch(item->node());
        item->setSearchMatch(match);
        if (match)
            searchMatchNodeIds.append(iterator.key());
    }
    std::sort(
        searchMatchNodeIds.begin(),
        searchMatchNodeIds.end());
    if (nextSearchMatchIndex >= searchMatchNodeIds.size())
        nextSearchMatchIndex = 0;

    for (EdgePresentation& edge : edgeItems)
    {
        const StoryGraphNodeGraphicsItem* from =
            nodeItems.value(edge.fromNodeId, nullptr);
        const StoryGraphNodeGraphicsItem* to =
            nodeItems.value(edge.toNodeId, nullptr);
        const bool visible =
            from && to &&
            from->isVisible() && to->isVisible();
        if (edge.pathItem)
            edge.pathItem->setVisible(visible);
        if (edge.arrowItem)
            edge.arrowItem->setVisible(visible);
        if (edge.labelItem)
            edge.labelItem->setVisible(visible);
    }

    currentVisibleNodeCount = visibleCount;
    emit visibleNodeCountChanged(
        currentVisibleNodeCount,
        currentGraph.nodes.size());
    emit searchMatchCountChanged(
        searchMatchNodeIds.size());
}

void StoryGraphView::rebuildTraceOverlay(
    const StoryGraphTraceGraphOverlay& overlay)
{
    for (const QString& nodeId :
         std::as_const(traceExecutedNodeIds))
    {
        if (!overlay.executedNodeIds.contains(
                nodeId))
        {
            ++currentTraceOverlayWorkItemCount;
            setNodeExecuted(nodeId, false);
        }
    }
    for (const QString& edgeId :
         std::as_const(traceExecutedEdgeIds))
    {
        if (!overlay.executedEdgeIds.contains(
                edgeId))
        {
            ++currentTraceOverlayWorkItemCount;
            setEdgeExecuted(edgeId, false);
        }
    }
    for (const QString& nodeId :
         overlay.executedNodeIds)
    {
        if (!traceExecutedNodeIds.contains(nodeId))
        {
            ++currentTraceOverlayWorkItemCount;
            setNodeExecuted(nodeId, true);
        }
    }
    for (const QString& edgeId :
         overlay.executedEdgeIds)
    {
        if (!traceExecutedEdgeIds.contains(edgeId))
        {
            ++currentTraceOverlayWorkItemCount;
            setEdgeExecuted(edgeId, true);
        }
    }
    traceExecutedNodeIds =
        overlay.executedNodeIds;
    traceExecutedEdgeIds =
        overlay.executedEdgeIds;
    traceExecutedNodeIds.detach();
    traceExecutedEdgeIds.detach();
    ++currentTraceOverlayRebuildCount;
    viewport()->update();
}

bool StoryGraphView::applyTraceOverlayDelta(
    const StoryGraphTraceGraphOverlay& overlay)
{
    if (currentTraceOverlayRevision >
            static_cast<quint64>(
                overlay.deltas.size()) ||
        overlay.revision >
            static_cast<quint64>(
                overlay.deltas.size()))
    {
        return false;
    }

    const qsizetype firstDeltaIndex =
        static_cast<qsizetype>(
            currentTraceOverlayRevision);
    const qsizetype endDeltaIndex =
        static_cast<qsizetype>(
            overlay.revision);
    for (qsizetype index = firstDeltaIndex;
         index < endDeltaIndex;
         ++index)
    {
        if (overlay.deltas.at(index).revision !=
            static_cast<quint64>(index) + 1)
        {
            return false;
        }
    }

    bool changedPresentedItem = false;
    for (qsizetype index = firstDeltaIndex;
         index < endDeltaIndex;
         ++index)
    {
        const StoryGraphTraceGraphOverlay::Delta&
            delta = overlay.deltas.at(index);
        for (const QString& nodeId :
             delta.addedNodeIds)
        {
            ++currentTraceOverlayWorkItemCount;
            traceExecutedNodeIds.insert(nodeId);
            changedPresentedItem =
                setNodeExecuted(nodeId, true) ||
                changedPresentedItem;
        }
        for (const QString& edgeId :
             delta.addedEdgeIds)
        {
            ++currentTraceOverlayWorkItemCount;
            traceExecutedEdgeIds.insert(edgeId);
            changedPresentedItem =
                setEdgeExecuted(edgeId, true) ||
                changedPresentedItem;
        }
    }
    if (traceExecutedNodeIds.size() !=
            overlay.executedNodeIds.size() ||
        traceExecutedEdgeIds.size() !=
            overlay.executedEdgeIds.size())
    {
        return false;
    }
    if (changedPresentedItem)
        viewport()->update();
    return true;
}

bool StoryGraphView::setNodeExecuted(
    const QString& nodeId,
    bool executed)
{
    StoryGraphNodeGraphicsItem* item =
        nodeItems.value(nodeId, nullptr);
    if (!item || !item->setExecuted(executed))
        return false;
    currentExecutedNodeCount +=
        executed ? 1 : -1;
    return true;
}

bool StoryGraphView::setEdgeExecuted(
    const QString& edgeId,
    bool executed)
{
    const auto index =
        edgeItemIndexes.constFind(edgeId);
    if (index == edgeItemIndexes.cend() ||
        *index < 0 ||
        *index >= edgeItems.size())
    {
        return false;
    }
    EdgePresentation& edge =
        edgeItems[*index];
    if (edge.executed == executed)
        return false;
    edge.executed = executed;
    currentExecutedEdgeCount +=
        executed ? 1 : -1;
    if (edge.pathItem)
    {
        edge.pathItem->setPen(
            executed
                ? QPen(
                      QColor(61, 222, 166),
                      3.6)
                : edge.basePen);
    }
    if (edge.arrowItem)
    {
        const QColor color =
            executed
                ? QColor(61, 222, 166)
                : edge.baseColor;
        edge.arrowItem->setPen(QPen(color));
        edge.arrowItem->setBrush(color);
    }
    if (edge.labelItem)
    {
        edge.labelItem->setBrush(
            executed
                ? QColor(61, 222, 166)
                : edge.baseColor);
    }
    return true;
}

void StoryGraphView::retireCurrentScene()
{
    QGraphicsScene* previousScene = scene();
    auto* replacementScene =
        new QGraphicsScene(this);
    setScene(replacementScene);
    if (!previousScene)
        return;

    RetiredPresentation retired;
    retired.scene = previousScene;
    retired.items.reserve(
        nodeItems.size() +
        edgeItems.size() * 3);
    for (StoryGraphNodeGraphicsItem* item :
         std::as_const(nodeItems))
    {
        if (item)
            retired.items.append(item);
    }
    for (const EdgePresentation& edge :
         std::as_const(edgeItems))
    {
        if (edge.pathItem)
            retired.items.append(edge.pathItem);
        if (edge.arrowItem)
            retired.items.append(edge.arrowItem);
        if (edge.labelItem)
            retired.items.append(edge.labelItem);
    }
    if (retired.items.isEmpty())
    {
        previousScene->deleteLater();
        return;
    }
    retiredPresentations.append(
        std::move(retired));
    scheduleRetiredPresentationRelease();
}

void StoryGraphView::
    scheduleRetiredPresentationRelease()
{
    if (retiredReleaseScheduled ||
        retiredPresentations.isEmpty())
    {
        return;
    }
    retiredReleaseScheduled = true;
    QTimer::singleShot(
        0,
        this,
        [this]()
        {
            retiredReleaseScheduled = false;
            releaseRetiredPresentationBatch();
        });
}

void StoryGraphView::
    releaseRetiredPresentationBatch()
{
    int remaining = PresentationBatchSize;
    while (remaining > 0 &&
           !retiredPresentations.isEmpty())
    {
        RetiredPresentation& retired =
            retiredPresentations.first();
        while (remaining > 0 &&
               !retired.items.isEmpty())
        {
            delete retired.items.takeLast();
            --remaining;
        }
        if (!retired.items.isEmpty())
            break;

        if (retired.scene)
            retired.scene->deleteLater();
        retiredPresentations.removeFirst();
    }
    if (!retiredPresentations.isEmpty())
        scheduleRetiredPresentationRelease();
}

bool StoryGraphView::nodeMatchesSearch(
    const StoryGraphNode& node) const
{
    if (normalizedSearchText.isEmpty())
        return false;

    const QString searchable =
        node.title + QLatin1Char('\n') +
        node.summary + QLatin1Char('\n') +
        node.apiName + QLatin1Char('\n') +
        node.literalTarget + QLatin1Char('\n') +
        node.variableName + QLatin1Char('\n') +
        node.source.virtualPath;
    return searchable.contains(
        normalizedSearchText,
        Qt::CaseInsensitive);
}

bool StoryGraphView::nodeKindIsAllowed(
    StoryGraphNodeKind kind) const
{
    return allowedKinds.isEmpty() ||
        allowedKinds.contains(kind);
}

void StoryGraphView::applyZoomFactor(qreal factor)
{
    factor = std::clamp(
        factor,
        MinimumZoomFactor,
        MaximumZoomFactor);
    if (qFuzzyCompare(currentZoomFactor, factor))
        return;

    const qreal relative =
        factor / currentZoomFactor;
    scale(relative, relative);
    currentZoomFactor = factor;
    emit zoomFactorChanged(currentZoomFactor);
}
