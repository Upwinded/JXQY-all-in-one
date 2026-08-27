#pragma once

#include "../core/StoryGraphLayout.h"
#include "../core/StoryGraphModel.h"
#include "../core/StoryGraphTraceMatcher.h"

#include <QGraphicsView>
#include <QColor>
#include <QHash>
#include <QList>
#include <QPen>
#include <QSet>
#include <QString>

class StoryGraphNodeGraphicsItem;
class QGraphicsScene;
class QGraphicsPathItem;
class QGraphicsPolygonItem;
class QGraphicsSimpleTextItem;

class StoryGraphView : public QGraphicsView
{
    Q_OBJECT

public:
    explicit StoryGraphView(QWidget* parent = nullptr);

    void setGraph(
        const StoryGraphResult& graph,
        const StoryGraphLayoutResult& layout,
        quint64 generation);
    void cancelPendingPresentation();
    void clearGraph();
    void setTraceOverlay(
        const StoryGraphTraceGraphOverlay& overlay,
        quint64 generation);
    void clearTraceOverlay();

    void setAllowedNodeKinds(
        const QSet<StoryGraphNodeKind>& allowedKinds);
    QSet<StoryGraphNodeKind> allowedNodeKinds() const;

    void setSearchText(const QString& text);
    QString searchText() const;
    bool focusNextSearchMatch();

    void zoomIn();
    void zoomOut();
    void resetZoom();
    void fitGraphInView();

    quint64 generation() const;
    int presentedNodeCount() const;
    int visibleNodeCount() const;
    int searchMatchCount() const;
    int executedNodeCount() const;
    int executedEdgeCount() const;
    // Monotonic observability counters used by deterministic workload tests.
    // A same-epoch, same-revision update changes none of them.
    quint64 traceOverlayEpoch() const;
    quint64 traceOverlayRevision() const;
    quint64 traceOverlayRebuildCount() const;
    quint64 traceOverlayWorkItemCount() const;
    bool presentationComplete() const;
    qreal zoomFactor() const;

signals:
    void nodeActivated(const StoryGraphNode& node);
    void presentationProgressChanged(
        int presentedNodes,
        int totalNodes,
        bool complete);
    void visibleNodeCountChanged(
        int visibleNodes,
        int totalNodes);
    void searchMatchCountChanged(int matchCount);
    void zoomFactorChanged(qreal factor);
    void presentationBatchProcessed(int workItemCount);

protected:
    void wheelEvent(QWheelEvent* event) override;

private:
    struct EdgePresentation
    {
        QString edgeId;
        QString fromNodeId;
        QString toNodeId;
        QGraphicsPathItem* pathItem = nullptr;
        QGraphicsPolygonItem* arrowItem = nullptr;
        QGraphicsSimpleTextItem* labelItem = nullptr;
        QPen basePen;
        QColor baseColor;
        bool executed = false;
    };

    struct RetiredPresentation
    {
        QGraphicsScene* scene = nullptr;
        QList<QGraphicsItem*> items;
    };

    void scheduleNextPresentationBatch(quint64 token);
    void appendPresentationBatch(quint64 token);
    int addNodeItem(const StoryGraphNode& node);
    int addEdgeItem(const StoryGraphEdge& edge);
    void applyVisibilityAndSearch();
    void rebuildTraceOverlay(
        const StoryGraphTraceGraphOverlay& overlay);
    bool applyTraceOverlayDelta(
        const StoryGraphTraceGraphOverlay& overlay);
    bool setNodeExecuted(
        const QString& nodeId,
        bool executed);
    bool setEdgeExecuted(
        const QString& edgeId,
        bool executed);
    void retireCurrentScene();
    void scheduleRetiredPresentationRelease();
    void releaseRetiredPresentationBatch();
    bool nodeMatchesSearch(const StoryGraphNode& node) const;
    bool nodeKindIsAllowed(StoryGraphNodeKind kind) const;
    void applyZoomFactor(qreal factor);

    StoryGraphResult currentGraph;
    StoryGraphLayoutResult currentLayout;
    quint64 currentGeneration = 0;
    quint64 presentationToken = 0;
    int pendingEdgePlacementIndex = 0;
    int pendingEdgeIndex = 0;
    int pendingNodeIndex = 0;
    bool batchScheduled = false;
    bool retiredReleaseScheduled = false;
    bool completePresentation = true;
    qreal currentZoomFactor = 1.0;
    int currentVisibleNodeCount = 0;
    QSet<StoryGraphNodeKind> allowedKinds;
    QString normalizedSearchText;
    QList<QString> searchMatchNodeIds;
    int nextSearchMatchIndex = 0;
    quint64 traceOverlayGeneration = 0;
    quint64 currentTraceOverlayEpoch = 0;
    quint64 currentTraceOverlayRevision = 0;
    quint64 currentTraceOverlayRebuildCount = 0;
    quint64 currentTraceOverlayWorkItemCount = 0;
    int currentExecutedNodeCount = 0;
    int currentExecutedEdgeCount = 0;
    QSet<QString> traceExecutedNodeIds;
    QSet<QString> traceExecutedEdgeIds;
    QHash<QString, StoryGraphNodeGraphicsItem*> nodeItems;
    QHash<QString, int> edgePlacementIndexes;
    QHash<QString, int> edgeItemIndexes;
    QList<EdgePresentation> edgeItems;
    QList<RetiredPresentation> retiredPresentations;
};
