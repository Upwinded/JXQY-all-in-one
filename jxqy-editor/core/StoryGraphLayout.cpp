#include "StoryGraphLayout.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace
{
constexpr char kInvalidOptionsCode[] =
    "story_graph.layout.invalid_options";
constexpr char kIncompleteInputCode[] =
    "story_graph.layout.incomplete_input";
constexpr char kEmptyNodeIdCode[] =
    "story_graph.layout.empty_node_id";
constexpr char kDuplicateNodeIdCode[] =
    "story_graph.layout.duplicate_node_id";
constexpr char kMissingEndpointCode[] =
    "story_graph.layout.missing_endpoint";
constexpr char kCancellationCode[] =
    "story_graph.layout.cancelled";
constexpr char kInvalidCondensationCode[] =
    "story_graph.layout.invalid_condensation";

struct IndexedEdge
{
    const StoryGraphEdge* edge = nullptr;
    int inputIndex = -1;
};

struct StronglyConnectedComponent
{
    QVector<int> nodeIndexes;
    bool containsCycle = false;
};

struct TextMeasure
{
    int longestLineCharacters = 0;
    int wrappedLineCount = 0;
};

bool isPositiveFinite(qreal value)
{
    return std::isfinite(static_cast<double>(value)) &&
        value > 0.0;
}

bool isNonNegativeFinite(qreal value)
{
    return std::isfinite(static_cast<double>(value)) &&
        value >= 0.0;
}

qreal boundedValue(qreal minimum, qreal value, qreal maximum)
{
    return std::max(minimum, std::min(value, maximum));
}

void appendWarning(
    StoryGraphLayoutResult* result,
    const QString& diagnosticCode,
    const QString& message,
    const QString& relatedNodeId = QString(),
    const QString& relatedEdgeId = QString())
{
    if (!result)
    {
        return;
    }

    StoryGraphLayoutWarning warning;
    warning.diagnosticCode = diagnosticCode;
    warning.message = message;
    warning.relatedNodeId = relatedNodeId;
    warning.relatedEdgeId = relatedEdgeId;
    result->warnings.append(warning);

    if (result->status == StoryGraphLayoutStatus::Complete)
    {
        result->status = StoryGraphLayoutStatus::Partial;
    }
}

bool cancellationRequested(
    const StoryGraphLayout::CancelCallback& cancelCallback)
{
    return cancelCallback && cancelCallback();
}

StoryGraphLayoutResult cancelledResult(
    StoryGraphLayoutResult result)
{
    result.status = StoryGraphLayoutStatus::Cancelled;
    result.nodeRectangles.clear();
    result.nodePlacements.clear();
    result.edgePlacements.clear();
    result.bounds = QRectF();

    StoryGraphLayoutWarning warning;
    warning.diagnosticCode =
        QString::fromLatin1(kCancellationCode);
    warning.message =
        QCoreApplication::translate(
            "StoryGraphLayout",
            "剧情图布局已取消。");
    result.warnings.append(warning);
    return result;
}

void normalizeOptions(
    const StoryGraphLayoutOptions& options,
    StoryGraphLayoutOptions* normalized,
    StoryGraphLayoutResult* result)
{
    if (!normalized || !result)
    {
        return;
    }

    const StoryGraphLayoutOptions defaults;
    *normalized = options;

    bool adjusted = false;
    const bool fixedSizeValid =
        isPositiveFinite(options.fixedNodeSize.width()) &&
        isPositiveFinite(options.fixedNodeSize.height());
    if (!fixedSizeValid)
    {
        normalized->fixedNodeSize = defaults.fixedNodeSize;
        adjusted = true;
    }

    const bool minimumSizeValid =
        isPositiveFinite(options.minimumNodeSize.width()) &&
        isPositiveFinite(options.minimumNodeSize.height());
    if (!minimumSizeValid)
    {
        normalized->minimumNodeSize =
            defaults.minimumNodeSize;
        adjusted = true;
    }

    const bool maximumSizeValid =
        isPositiveFinite(options.maximumNodeSize.width()) &&
        isPositiveFinite(options.maximumNodeSize.height());
    if (!maximumSizeValid)
    {
        normalized->maximumNodeSize =
            defaults.maximumNodeSize;
        adjusted = true;
    }

    if (normalized->maximumNodeSize.width() <
        normalized->minimumNodeSize.width())
    {
        normalized->maximumNodeSize.setWidth(
            normalized->minimumNodeSize.width());
        adjusted = true;
    }
    if (normalized->maximumNodeSize.height() <
        normalized->minimumNodeSize.height())
    {
        normalized->maximumNodeSize.setHeight(
            normalized->minimumNodeSize.height());
        adjusted = true;
    }

    const auto normalizePositive =
        [&adjusted](qreal value, qreal fallback)
    {
        if (isPositiveFinite(value))
        {
            return value;
        }
        adjusted = true;
        return fallback;
    };

    normalized->estimatedCharacterWidth =
        normalizePositive(
            options.estimatedCharacterWidth,
            defaults.estimatedCharacterWidth);
    normalized->estimatedLineHeight =
        normalizePositive(
            options.estimatedLineHeight,
            defaults.estimatedLineHeight);
    normalized->horizontalTextPadding =
        normalizePositive(
            options.horizontalTextPadding,
            defaults.horizontalTextPadding);
    normalized->verticalTextPadding =
        normalizePositive(
            options.verticalTextPadding,
            defaults.verticalTextPadding);
    normalized->layerSpacing =
        normalizePositive(
            options.layerSpacing,
            defaults.layerSpacing);
    normalized->nodeSpacing =
        normalizePositive(
            options.nodeSpacing,
            defaults.nodeSpacing);
    normalized->componentSpacing =
        normalizePositive(
            options.componentSpacing,
            defaults.componentSpacing);
    normalized->edgeLaneSpacing =
        normalizePositive(
            options.edgeLaneSpacing,
            defaults.edgeLaneSpacing);

    if (!isNonNegativeFinite(options.outerMargin))
    {
        normalized->outerMargin = defaults.outerMargin;
        adjusted = true;
    }

    if (options.minimumEstimatedLineCharacters < 1)
    {
        normalized->minimumEstimatedLineCharacters =
            defaults.minimumEstimatedLineCharacters;
        adjusted = true;
    }
    if (options.maximumEstimatedLineCharacters < 1)
    {
        normalized->maximumEstimatedLineCharacters =
            defaults.maximumEstimatedLineCharacters;
        adjusted = true;
    }
    if (normalized->maximumEstimatedLineCharacters <
        normalized->minimumEstimatedLineCharacters)
    {
        normalized->maximumEstimatedLineCharacters =
            normalized->minimumEstimatedLineCharacters;
        adjusted = true;
    }

    if (adjusted)
    {
        appendWarning(
            result,
            QString::fromLatin1(kInvalidOptionsCode),
            QCoreApplication::translate(
                "StoryGraphLayout",
                "无效布局选项已替换为安全值。"));
    }
}

int unicodeScalarCount(const QString& text)
{
    int count = 0;
    for (qsizetype index = 0; index < text.size(); ++index)
    {
        const QChar character = text.at(index);
        if (character.isHighSurrogate() &&
            index + 1 < text.size() &&
            text.at(index + 1).isLowSurrogate())
        {
            ++index;
        }
        ++count;
    }
    return count;
}

TextMeasure measureText(
    const QString& text,
    int charactersPerLine)
{
    TextMeasure result;
    if (text.isEmpty())
    {
        return result;
    }

    const QStringList lines =
        text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    result.wrappedLineCount = 0;
    for (const QString& line : lines)
    {
        const int characterCount = unicodeScalarCount(line);
        result.longestLineCharacters =
            std::max(
                result.longestLineCharacters,
                characterCount);
        result.wrappedLineCount +=
            std::max(
                1,
                (characterCount + charactersPerLine - 1) /
                    charactersPerLine);
    }
    return result;
}

QSizeF estimatedNodeSize(
    const StoryGraphNode& node,
    const StoryGraphLayoutOptions& options)
{
    if (options.nodeSizeMode ==
        StoryGraphNodeSizeMode::Fixed)
    {
        return options.fixedNodeSize;
    }

    const TextMeasure titleMeasure =
        measureText(
            node.title,
            options.maximumEstimatedLineCharacters);
    const TextMeasure summaryMeasure =
        measureText(
            node.summary,
            options.maximumEstimatedLineCharacters);

    const int longestLine = std::max(
        titleMeasure.longestLineCharacters,
        summaryMeasure.longestLineCharacters);
    const int estimatedLineCharacters =
        std::max(
            options.minimumEstimatedLineCharacters,
            std::min(
                longestLine,
                options.maximumEstimatedLineCharacters));

    const qreal estimatedWidth =
        options.horizontalTextPadding * 2.0 +
        static_cast<qreal>(estimatedLineCharacters) *
            options.estimatedCharacterWidth;

    const int titleLines = node.title.isEmpty()
        ? 0
        : measureText(
              node.title,
              estimatedLineCharacters)
              .wrappedLineCount;
    const int summaryLines = node.summary.isEmpty()
        ? 0
        : measureText(
              node.summary,
              estimatedLineCharacters)
              .wrappedLineCount;
    const int totalLines =
        std::max(1, titleLines + summaryLines);
    const qreal textGap =
        titleLines > 0 && summaryLines > 0
        ? options.estimatedLineHeight * 0.35
        : 0.0;
    const qreal estimatedHeight =
        options.verticalTextPadding * 2.0 +
        static_cast<qreal>(totalLines) *
            options.estimatedLineHeight +
        textGap;

    return QSizeF(
        boundedValue(
            options.minimumNodeSize.width(),
            estimatedWidth,
            options.maximumNodeSize.width()),
        boundedValue(
            options.minimumNodeSize.height(),
            estimatedHeight,
            options.maximumNodeSize.height()));
}

void sortAndRemoveDuplicates(QVector<int>* values)
{
    if (!values)
    {
        return;
    }
    std::sort(values->begin(), values->end());
    values->erase(
        std::unique(values->begin(), values->end()),
        values->end());
}

bool indexedEdgeLess(
    const IndexedEdge& left,
    const IndexedEdge& right)
{
    const StoryGraphEdge& leftEdge = *left.edge;
    const StoryGraphEdge& rightEdge = *right.edge;

    if (leftEdge.id != rightEdge.id)
    {
        return leftEdge.id < rightEdge.id;
    }
    if (leftEdge.fromNodeId != rightEdge.fromNodeId)
    {
        return leftEdge.fromNodeId <
            rightEdge.fromNodeId;
    }
    if (leftEdge.toNodeId != rightEdge.toNodeId)
    {
        return leftEdge.toNodeId < rightEdge.toNodeId;
    }
    if (leftEdge.kind != rightEdge.kind)
    {
        return static_cast<int>(leftEdge.kind) <
            static_cast<int>(rightEdge.kind);
    }
    if (leftEdge.label != rightEdge.label)
    {
        return leftEdge.label < rightEdge.label;
    }
    if (leftEdge.semanticFingerprint !=
        rightEdge.semanticFingerprint)
    {
        return leftEdge.semanticFingerprint <
            rightEdge.semanticFingerprint;
    }
    if (leftEdge.structuralOccurrenceKey !=
        rightEdge.structuralOccurrenceKey)
    {
        return leftEdge.structuralOccurrenceKey <
            rightEdge.structuralOccurrenceKey;
    }
    return left.inputIndex < right.inputIndex;
}

bool computeStronglyConnectedComponents(
    const QVector<QVector<int>>& adjacency,
    const QVector<QVector<int>>& reverseAdjacency,
    const StoryGraphLayout::CancelCallback& cancelCallback,
    QVector<StronglyConnectedComponent>* components,
    QVector<int>* nodeToComponent)
{
    if (!components || !nodeToComponent)
    {
        return false;
    }

    const int nodeCount = adjacency.size();
    QVector<bool> visited(nodeCount, false);
    QVector<int> finishOrder;
    finishOrder.reserve(nodeCount);

    struct DepthFirstFrame
    {
        int nodeIndex = -1;
        int nextNeighbourIndex = 0;
    };

    for (int startIndex = 0;
         startIndex < nodeCount;
         ++startIndex)
    {
        if (visited.at(startIndex))
        {
            continue;
        }

        QVector<DepthFirstFrame> stack;
        visited[startIndex] = true;
        stack.append({startIndex, 0});

        while (!stack.isEmpty())
        {
            if (cancellationRequested(cancelCallback))
            {
                return false;
            }

            DepthFirstFrame& frame = stack.last();
            const QVector<int>& neighbours =
                adjacency.at(frame.nodeIndex);
            if (frame.nextNeighbourIndex <
                neighbours.size())
            {
                const int neighbour =
                    neighbours.at(
                        frame.nextNeighbourIndex);
                ++frame.nextNeighbourIndex;
                if (!visited.at(neighbour))
                {
                    visited[neighbour] = true;
                    stack.append({neighbour, 0});
                }
                continue;
            }

            finishOrder.append(frame.nodeIndex);
            stack.removeLast();
        }
    }

    QVector<bool> assigned(nodeCount, false);
    QVector<QVector<int>> rawComponents;
    for (int finishIndex = finishOrder.size() - 1;
         finishIndex >= 0;
         --finishIndex)
    {
        const int startIndex =
            finishOrder.at(finishIndex);
        if (assigned.at(startIndex))
        {
            continue;
        }

        QVector<int> members;
        QVector<int> stack;
        assigned[startIndex] = true;
        stack.append(startIndex);
        while (!stack.isEmpty())
        {
            if (cancellationRequested(cancelCallback))
            {
                return false;
            }

            const int nodeIndex = stack.takeLast();
            members.append(nodeIndex);

            const QVector<int>& neighbours =
                reverseAdjacency.at(nodeIndex);
            for (int neighbourOffset =
                     neighbours.size() - 1;
                 neighbourOffset >= 0;
                 --neighbourOffset)
            {
                const int neighbour =
                    neighbours.at(neighbourOffset);
                if (!assigned.at(neighbour))
                {
                    assigned[neighbour] = true;
                    stack.append(neighbour);
                }
            }
        }
        std::sort(members.begin(), members.end());
        rawComponents.append(members);
    }

    std::sort(
        rawComponents.begin(),
        rawComponents.end(),
        [](const QVector<int>& left,
           const QVector<int>& right)
        {
            return std::lexicographical_compare(
                left.begin(),
                left.end(),
                right.begin(),
                right.end());
        });

    components->clear();
    components->reserve(rawComponents.size());
    nodeToComponent->fill(-1, nodeCount);
    for (int componentIndex = 0;
         componentIndex < rawComponents.size();
         ++componentIndex)
    {
        StronglyConnectedComponent component;
        component.nodeIndexes =
            rawComponents.at(componentIndex);
        component.containsCycle =
            component.nodeIndexes.size() > 1;
        for (const int nodeIndex :
             component.nodeIndexes)
        {
            (*nodeToComponent)[nodeIndex] =
                componentIndex;
            if (!component.containsCycle &&
                adjacency.at(nodeIndex).contains(
                    nodeIndex))
            {
                component.containsCycle = true;
            }
        }
        components->append(component);
    }
    return true;
}

void appendPoint(
    QList<QPointF>* points,
    const QPointF& point)
{
    if (!points)
    {
        return;
    }
    if (points->isEmpty() ||
        points->constLast() != point)
    {
        points->append(point);
    }
}

void includePointInBounds(
    const QPointF& point,
    QRectF* bounds,
    bool* hasBounds)
{
    if (!bounds || !hasBounds)
    {
        return;
    }
    if (!*hasBounds)
    {
        *bounds = QRectF(point, QSizeF(0.0, 0.0));
        *hasBounds = true;
        return;
    }

    const qreal left = std::min(bounds->left(), point.x());
    const qreal right =
        std::max(bounds->right(), point.x());
    const qreal top = std::min(bounds->top(), point.y());
    const qreal bottom =
        std::max(bounds->bottom(), point.y());
    *bounds = QRectF(
        QPointF(left, top),
        QPointF(right, bottom));
}

void includeRectangleInBounds(
    const QRectF& rectangle,
    QRectF* bounds,
    bool* hasBounds)
{
    includePointInBounds(
        rectangle.topLeft(),
        bounds,
        hasBounds);
    includePointInBounds(
        rectangle.bottomRight(),
        bounds,
        hasBounds);
}
}

bool StoryGraphLayoutResult::isComplete() const
{
    return status == StoryGraphLayoutStatus::Complete;
}

bool StoryGraphLayoutResult::isUsable() const
{
    return status != StoryGraphLayoutStatus::Cancelled;
}

bool StoryGraphLayoutResult::wasCancelled() const
{
    return status == StoryGraphLayoutStatus::Cancelled;
}

StoryGraphLayoutResult StoryGraphLayout::layout(
    const StoryGraphResult& graph,
    const StoryGraphLayoutOptions& options,
    const CancelCallback& cancelCallback)
{
    StoryGraphLayoutResult result;
    StoryGraphLayoutOptions normalizedOptions;
    normalizeOptions(
        options,
        &normalizedOptions,
        &result);

    if (!graph.complete)
    {
        appendWarning(
            &result,
            QString::fromLatin1(kIncompleteInputCode),
            QCoreApplication::translate(
                "StoryGraphLayout",
                "输入图不完整；已对现有节点和边进行布局。"));
    }

    if (cancellationRequested(cancelCallback))
    {
        return cancelledResult(std::move(result));
    }

    QMap<QString, const StoryGraphNode*> nodesById;
    for (const StoryGraphNode& node : graph.nodes)
    {
        if (cancellationRequested(cancelCallback))
        {
            return cancelledResult(std::move(result));
        }

        if (node.id.isEmpty())
        {
            appendWarning(
                &result,
                QString::fromLatin1(kEmptyNodeIdCode),
                QCoreApplication::translate(
                    "StoryGraphLayout",
                    "已忽略 ID 为空的节点。"));
            continue;
        }
        if (nodesById.contains(node.id))
        {
            appendWarning(
                &result,
                QString::fromLatin1(kDuplicateNodeIdCode),
                QCoreApplication::translate(
                    "StoryGraphLayout",
                    "已忽略 ID 重复的节点。"),
                node.id);
            continue;
        }
        nodesById.insert(node.id, &node);
    }

    QVector<QString> nodeIds;
    QVector<const StoryGraphNode*> nodes;
    nodeIds.reserve(nodesById.size());
    nodes.reserve(nodesById.size());
    QHash<QString, int> nodeIndexById;
    for (auto iterator = nodesById.cbegin();
         iterator != nodesById.cend();
         ++iterator)
    {
        const int nodeIndex = nodeIds.size();
        nodeIds.append(iterator.key());
        nodes.append(iterator.value());
        nodeIndexById.insert(iterator.key(), nodeIndex);
    }

    QVector<IndexedEdge> indexedEdges;
    indexedEdges.reserve(graph.edges.size());
    for (int edgeIndex = 0;
         edgeIndex < graph.edges.size();
         ++edgeIndex)
    {
        indexedEdges.append(
            {&graph.edges.at(edgeIndex), edgeIndex});
    }
    std::sort(
        indexedEdges.begin(),
        indexedEdges.end(),
        indexedEdgeLess);

    QVector<QVector<int>> adjacency(nodes.size());
    QVector<QVector<int>> reverseAdjacency(nodes.size());
    for (const IndexedEdge& indexedEdge : indexedEdges)
    {
        if (cancellationRequested(cancelCallback))
        {
            return cancelledResult(std::move(result));
        }

        const StoryGraphEdge& edge = *indexedEdge.edge;
        const auto fromIterator =
            nodeIndexById.constFind(edge.fromNodeId);
        const auto toIterator =
            nodeIndexById.constFind(edge.toNodeId);
        if (fromIterator == nodeIndexById.cend() ||
            toIterator == nodeIndexById.cend())
        {
            appendWarning(
                &result,
                QString::fromLatin1(kMissingEndpointCode),
                QCoreApplication::translate(
                    "StoryGraphLayout",
                    "边的端点不存在于输入图中。"),
                fromIterator == nodeIndexById.cend()
                    ? edge.fromNodeId
                    : edge.toNodeId,
                edge.id);
            continue;
        }

        adjacency[*fromIterator].append(*toIterator);
        reverseAdjacency[*toIterator].append(
            *fromIterator);
    }
    for (QVector<int>& neighbours : adjacency)
    {
        sortAndRemoveDuplicates(&neighbours);
    }
    for (QVector<int>& neighbours : reverseAdjacency)
    {
        sortAndRemoveDuplicates(&neighbours);
    }

    QVector<StronglyConnectedComponent> components;
    QVector<int> nodeToComponent;
    if (!computeStronglyConnectedComponents(
            adjacency,
            reverseAdjacency,
            cancelCallback,
            &components,
            &nodeToComponent))
    {
        return cancelledResult(std::move(result));
    }

    QVector<QVector<int>> componentAdjacency(
        components.size());
    QVector<QVector<int>> componentUndirectedAdjacency(
        components.size());
    for (int fromNode = 0;
         fromNode < adjacency.size();
         ++fromNode)
    {
        const int fromComponent =
            nodeToComponent.at(fromNode);
        for (const int toNode : adjacency.at(fromNode))
        {
            const int toComponent =
                nodeToComponent.at(toNode);
            if (fromComponent == toComponent)
            {
                continue;
            }
            componentAdjacency[fromComponent].append(
                toComponent);
            componentUndirectedAdjacency[fromComponent]
                .append(toComponent);
            componentUndirectedAdjacency[toComponent]
                .append(fromComponent);
        }
    }
    for (QVector<int>& neighbours :
         componentAdjacency)
    {
        sortAndRemoveDuplicates(&neighbours);
    }
    for (QVector<int>& neighbours :
         componentUndirectedAdjacency)
    {
        sortAndRemoveDuplicates(&neighbours);
    }

    QVector<QVector<int>> weakComponents;
    QVector<bool> visitedComponents(
        components.size(),
        false);
    for (int startComponent = 0;
         startComponent < components.size();
         ++startComponent)
    {
        if (visitedComponents.at(startComponent))
        {
            continue;
        }

        QVector<int> weakComponent;
        QVector<int> stack;
        stack.append(startComponent);
        visitedComponents[startComponent] = true;
        while (!stack.isEmpty())
        {
            if (cancellationRequested(cancelCallback))
            {
                return cancelledResult(std::move(result));
            }

            const int componentIndex =
                stack.takeLast();
            weakComponent.append(componentIndex);
            const QVector<int>& neighbours =
                componentUndirectedAdjacency.at(
                    componentIndex);
            for (int neighbourOffset =
                     neighbours.size() - 1;
                 neighbourOffset >= 0;
                 --neighbourOffset)
            {
                const int neighbour =
                    neighbours.at(neighbourOffset);
                if (!visitedComponents.at(neighbour))
                {
                    visitedComponents[neighbour] = true;
                    stack.append(neighbour);
                }
            }
        }
        std::sort(
            weakComponent.begin(),
            weakComponent.end());
        weakComponents.append(weakComponent);
    }

    QVector<int> componentLayers(
        components.size(),
        -1);
    QVector<int> componentWeakIndexes(
        components.size(),
        -1);
    for (int weakIndex = 0;
         weakIndex < weakComponents.size();
         ++weakIndex)
    {
        const QVector<int>& weakComponent =
            weakComponents.at(weakIndex);
        QSet<int> memberSet;
        for (const int componentIndex : weakComponent)
        {
            memberSet.insert(componentIndex);
            componentWeakIndexes[componentIndex] =
                weakIndex;
        }

        QMap<int, int> indegrees;
        for (const int componentIndex : weakComponent)
        {
            indegrees.insert(componentIndex, 0);
        }
        for (const int componentIndex : weakComponent)
        {
            for (const int target :
                 componentAdjacency.at(componentIndex))
            {
                if (memberSet.contains(target))
                {
                    indegrees[target] =
                        indegrees.value(target) + 1;
                }
            }
        }

        std::set<int> ready;
        for (const int componentIndex : weakComponent)
        {
            componentLayers[componentIndex] = 0;
            if (indegrees.value(componentIndex) == 0)
            {
                ready.insert(componentIndex);
            }
        }

        int processedCount = 0;
        while (!ready.empty())
        {
            if (cancellationRequested(cancelCallback))
            {
                return cancelledResult(std::move(result));
            }

            const int componentIndex = *ready.begin();
            ready.erase(ready.begin());
            ++processedCount;
            for (const int target :
                 componentAdjacency.at(componentIndex))
            {
                if (!memberSet.contains(target))
                {
                    continue;
                }
                componentLayers[target] = std::max(
                    componentLayers.at(target),
                    componentLayers.at(componentIndex) +
                        1);
                const int newIndegree =
                    indegrees.value(target) - 1;
                indegrees[target] = newIndegree;
                if (newIndegree == 0)
                {
                    ready.insert(target);
                }
            }
        }

        if (processedCount != weakComponent.size())
        {
            appendWarning(
                &result,
                QString::fromLatin1(
                    kInvalidCondensationCode),
                QCoreApplication::translate(
                    "StoryGraphLayout",
                    "缩合图不是无环图；已使用回退层级。"));
            for (const int componentIndex :
                 weakComponent)
            {
                componentLayers[componentIndex] =
                    std::max(
                        0,
                        componentLayers.at(
                            componentIndex));
            }
        }
    }

    QVector<QSizeF> nodeSizes;
    nodeSizes.reserve(nodes.size());
    for (const StoryGraphNode* node : nodes)
    {
        if (cancellationRequested(cancelCallback))
        {
            return cancelledResult(std::move(result));
        }
        nodeSizes.append(
            estimatedNodeSize(*node, normalizedOptions));
    }

    QVector<int> nodeOrderInLayer(nodes.size(), -1);
    QVector<int> nodeWeakIndexes(nodes.size(), -1);
    qreal componentTop = normalizedOptions.outerMargin;
    for (int weakIndex = 0;
         weakIndex < weakComponents.size();
         ++weakIndex)
    {
        if (cancellationRequested(cancelCallback))
        {
            return cancelledResult(std::move(result));
        }

        QMap<int, QVector<int>> nodesByLayer;
        for (const int componentIndex :
             weakComponents.at(weakIndex))
        {
            const int layer =
                componentLayers.at(componentIndex);
            for (const int nodeIndex :
                 components.at(componentIndex)
                     .nodeIndexes)
            {
                nodesByLayer[layer].append(nodeIndex);
                nodeWeakIndexes[nodeIndex] = weakIndex;
            }
        }
        for (auto iterator = nodesByLayer.begin();
             iterator != nodesByLayer.end();
             ++iterator)
        {
            std::sort(
                iterator.value().begin(),
                iterator.value().end());
        }

        QMap<int, qreal> layerLefts;
        QMap<int, qreal> layerHeights;
        qreal layerLeft = normalizedOptions.outerMargin;
        qreal componentHeight = 0.0;
        for (auto iterator = nodesByLayer.cbegin();
             iterator != nodesByLayer.cend();
             ++iterator)
        {
            qreal maximumWidth = 0.0;
            qreal layerHeight = 0.0;
            const QVector<int>& layerNodes =
                iterator.value();
            for (int order = 0;
                 order < layerNodes.size();
                 ++order)
            {
                const int nodeIndex =
                    layerNodes.at(order);
                maximumWidth = std::max(
                    maximumWidth,
                    nodeSizes.at(nodeIndex).width());
                layerHeight +=
                    nodeSizes.at(nodeIndex).height();
                if (order + 1 < layerNodes.size())
                {
                    layerHeight +=
                        normalizedOptions.nodeSpacing;
                }
                nodeOrderInLayer[nodeIndex] = order;
            }
            layerLefts.insert(iterator.key(), layerLeft);
            layerHeights.insert(
                iterator.key(),
                layerHeight);
            componentHeight =
                std::max(componentHeight, layerHeight);
            layerLeft += maximumWidth +
                normalizedOptions.layerSpacing;
        }

        for (auto iterator = nodesByLayer.cbegin();
             iterator != nodesByLayer.cend();
             ++iterator)
        {
            qreal nodeTop = componentTop +
                (componentHeight -
                 layerHeights.value(iterator.key())) /
                    2.0;
            for (const int nodeIndex : iterator.value())
            {
                const QRectF rectangle(
                    QPointF(
                        layerLefts.value(iterator.key()),
                        nodeTop),
                    nodeSizes.at(nodeIndex));
                result.nodeRectangles.insert(
                    nodeIds.at(nodeIndex),
                    rectangle);
                nodeTop += rectangle.height() +
                    normalizedOptions.nodeSpacing;
            }
        }

        componentTop += componentHeight +
            normalizedOptions.componentSpacing;
    }

    result.nodePlacements.reserve(nodes.size());
    for (int nodeIndex = 0;
         nodeIndex < nodes.size();
         ++nodeIndex)
    {
        const int componentIndex =
            nodeToComponent.at(nodeIndex);
        StoryGraphNodePlacement placement;
        placement.nodeId = nodeIds.at(nodeIndex);
        placement.rectangle =
            result.nodeRectangles.value(placement.nodeId);
        placement.weakComponentIndex =
            nodeWeakIndexes.at(nodeIndex);
        placement.layerIndex =
            componentLayers.at(componentIndex);
        placement.orderInLayer =
            nodeOrderInLayer.at(nodeIndex);
        placement.stronglyConnectedComponentIndex =
            componentIndex;
        placement.belongsToCycle =
            components.at(componentIndex).containsCycle;
        result.nodePlacements.append(placement);
    }

    QRectF contentBounds;
    bool hasBounds = false;
    for (const StoryGraphNodePlacement& placement :
         result.nodePlacements)
    {
        includeRectangleInBounds(
            placement.rectangle,
            &contentBounds,
            &hasBounds);
    }

    QMap<int, int> nextBackLaneByWeakComponent;
    result.edgePlacements.reserve(indexedEdges.size());
    for (const IndexedEdge& indexedEdge : indexedEdges)
    {
        if (cancellationRequested(cancelCallback))
        {
            return cancelledResult(std::move(result));
        }

        const StoryGraphEdge& edge = *indexedEdge.edge;
        StoryGraphEdgePlacement placement;
        placement.edgeId = edge.id;
        placement.fromNodeId = edge.fromNodeId;
        placement.toNodeId = edge.toNodeId;

        const auto fromIterator =
            nodeIndexById.constFind(edge.fromNodeId);
        const auto toIterator =
            nodeIndexById.constFind(edge.toNodeId);
        if (fromIterator == nodeIndexById.cend() ||
            toIterator == nodeIndexById.cend())
        {
            result.edgePlacements.append(placement);
            continue;
        }

        const int fromNode = *fromIterator;
        const int toNode = *toIterator;
        const int fromComponent =
            nodeToComponent.at(fromNode);
        const int toComponent =
            nodeToComponent.at(toNode);
        const QRectF fromRectangle =
            result.nodeRectangles.value(edge.fromNodeId);
        const QRectF toRectangle =
            result.nodeRectangles.value(edge.toNodeId);

        placement.selfLoop = fromNode == toNode;
        placement.cycleEdge =
            fromComponent == toComponent &&
            components.at(fromComponent).containsCycle;
        placement.backEdge =
            componentLayers.at(toComponent) <=
            componentLayers.at(fromComponent);

        if (placement.backEdge)
        {
            const int weakIndex =
                componentWeakIndexes.at(fromComponent);
            const int laneIndex =
                nextBackLaneByWeakComponent.value(
                    weakIndex,
                    0);
            nextBackLaneByWeakComponent[weakIndex] =
                laneIndex + 1;
            const qreal laneDistance =
                normalizedOptions.edgeLaneSpacing *
                static_cast<qreal>(laneIndex + 1);

            if (placement.selfLoop)
            {
                const QPointF start(
                    fromRectangle.right(),
                    fromRectangle.center().y() -
                        fromRectangle.height() * 0.2);
                const QPointF end(
                    fromRectangle.left(),
                    fromRectangle.center().y());
                const qreal rightLane =
                    fromRectangle.right() + laneDistance;
                const qreal topLane =
                    fromRectangle.top() - laneDistance;
                const qreal leftLane =
                    fromRectangle.left() - laneDistance;
                appendPoint(
                    &placement.pathPoints,
                    start);
                appendPoint(
                    &placement.pathPoints,
                    QPointF(rightLane, start.y()));
                appendPoint(
                    &placement.pathPoints,
                    QPointF(rightLane, topLane));
                appendPoint(
                    &placement.pathPoints,
                    QPointF(leftLane, topLane));
                appendPoint(
                    &placement.pathPoints,
                    QPointF(leftLane, end.y()));
                appendPoint(
                    &placement.pathPoints,
                    end);
            }
            else
            {
                const QPointF start(
                    fromRectangle.left(),
                    fromRectangle.center().y());
                const QPointF end(
                    toRectangle.left(),
                    toRectangle.center().y());
                const qreal laneX =
                    std::min(
                        fromRectangle.left(),
                        toRectangle.left()) -
                    laneDistance;
                appendPoint(
                    &placement.pathPoints,
                    start);
                appendPoint(
                    &placement.pathPoints,
                    QPointF(laneX, start.y()));
                appendPoint(
                    &placement.pathPoints,
                    QPointF(laneX, end.y()));
                appendPoint(
                    &placement.pathPoints,
                    end);
            }
        }
        else
        {
            const QPointF start(
                fromRectangle.right(),
                fromRectangle.center().y());
            const QPointF end(
                toRectangle.left(),
                toRectangle.center().y());
            const qreal middleX =
                (start.x() + end.x()) / 2.0;
            appendPoint(
                &placement.pathPoints,
                start);
            appendPoint(
                &placement.pathPoints,
                QPointF(middleX, start.y()));
            appendPoint(
                &placement.pathPoints,
                QPointF(middleX, end.y()));
            appendPoint(
                &placement.pathPoints,
                end);
        }

        placement.routed =
            placement.pathPoints.size() >= 2;
        for (const QPointF& point :
             placement.pathPoints)
        {
            includePointInBounds(
                point,
                &contentBounds,
                &hasBounds);
        }
        result.edgePlacements.append(placement);
    }

    if (hasBounds)
    {
        result.bounds = contentBounds.adjusted(
            -normalizedOptions.outerMargin,
            -normalizedOptions.outerMargin,
            normalizedOptions.outerMargin,
            normalizedOptions.outerMargin);
    }

    return result;
}
