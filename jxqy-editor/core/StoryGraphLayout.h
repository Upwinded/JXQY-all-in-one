#pragma once

#include "StoryGraphModel.h"

#include <QList>
#include <QMap>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>

#include <functional>

enum class StoryGraphNodeSizeMode
{
    Fixed,
    EstimatedFromText
};

enum class StoryGraphLayoutStatus
{
    Complete,
    Partial,
    Cancelled
};

struct StoryGraphLayoutOptions
{
    StoryGraphNodeSizeMode nodeSizeMode =
        StoryGraphNodeSizeMode::EstimatedFromText;
    QSizeF fixedNodeSize = QSizeF(224.0, 80.0);
    QSizeF minimumNodeSize = QSizeF(160.0, 68.0);
    QSizeF maximumNodeSize = QSizeF(360.0, 200.0);
    qreal estimatedCharacterWidth = 7.5;
    qreal estimatedLineHeight = 18.0;
    int minimumEstimatedLineCharacters = 16;
    int maximumEstimatedLineCharacters = 44;
    qreal horizontalTextPadding = 24.0;
    qreal verticalTextPadding = 16.0;
    qreal layerSpacing = 96.0;
    qreal nodeSpacing = 32.0;
    qreal componentSpacing = 112.0;
    qreal edgeLaneSpacing = 18.0;
    qreal outerMargin = 24.0;
};

struct StoryGraphNodePlacement
{
    QString nodeId;
    QRectF rectangle;
    int weakComponentIndex = -1;
    int layerIndex = -1;
    int orderInLayer = -1;
    int stronglyConnectedComponentIndex = -1;
    bool belongsToCycle = false;
};

struct StoryGraphEdgePlacement
{
    QString edgeId;
    QString fromNodeId;
    QString toNodeId;
    QList<QPointF> pathPoints;
    bool routed = false;
    bool selfLoop = false;
    bool cycleEdge = false;
    bool backEdge = false;
};

struct StoryGraphLayoutWarning
{
    QString diagnosticCode;
    QString message;
    QString relatedNodeId;
    QString relatedEdgeId;
};

struct StoryGraphLayoutResult
{
    StoryGraphLayoutStatus status =
        StoryGraphLayoutStatus::Complete;
    QMap<QString, QRectF> nodeRectangles;
    QList<StoryGraphNodePlacement> nodePlacements;
    QList<StoryGraphEdgePlacement> edgePlacements;
    QRectF bounds;
    QList<StoryGraphLayoutWarning> warnings;

    bool isComplete() const;
    bool isUsable() const;
    bool wasCancelled() const;
};

class StoryGraphLayout
{
public:
    using CancelCallback = std::function<bool()>;

    static StoryGraphLayoutResult layout(
        const StoryGraphResult& graph,
        const StoryGraphLayoutOptions& options =
            StoryGraphLayoutOptions(),
        const CancelCallback& cancelCallback =
            CancelCallback());
};
