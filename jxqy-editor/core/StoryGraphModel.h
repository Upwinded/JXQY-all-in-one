#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

enum class StoryGraphCertainty
{
    Certain,
    Dynamic,
    Unknown,
    Warning
};

enum class StoryGraphKind
{
    ControlFlow,
    StorySemantics
};

enum class StoryGraphNodeKind
{
    ChunkEntry,
    ChunkExit,
    FunctionEntry,
    FunctionExit,
    BasicBlock,
    Statement,
    Condition,
    Merge,
    LoopHeader,
    Label,
    Goto,
    Return,
    Break,
    Unreachable,
    Dialogue,
    Choice,
    VariableRead,
    VariableWrite,
    SerialScriptCall,
    ParallelScriptCall,
    MapLoad,
    Battle,
    RegisteredApiCall,
    DynamicCall,
    UnknownCall,
    MissingTarget,
    Warning
};

enum class StoryGraphEdgeKind
{
    Sequential,
    TrueBranch,
    FalseBranch,
    Fallthrough,
    Goto,
    LoopBody,
    LoopBack,
    Break,
    Return,
    Call,
    ParallelCall,
    Dynamic,
    Unknown
};

enum class StoryGraphWarningSeverity
{
    Information,
    Warning,
    Error
};

enum class StoryGraphWarningCategory
{
    Lexical,
    Syntax,
    ControlFlow,
    Semantic,
    Resolution,
    Budget,
    Cancellation
};

enum class StoryGraphDocumentStatus
{
    Complete,
    Partial,
    Failed,
    Cancelled
};

// This is the source snapshot used for analysis and navigation. Only
// portableRootKey and virtualPath are eligible for stable graph identity;
// the absolute path, content digest, revision, and buffer origin are
// deliberately navigation/staleness metadata.
struct StoryGraphSourceIdentity
{
    QString portableRootKey;
    QString virtualPath;
    QString canonicalAbsolutePath;
    QByteArray contentSha256;
    qint64 documentRevision = -1;
    bool fromEditorBuffer = false;

    bool operator==(const StoryGraphSourceIdentity& other) const;
    bool operator!=(const StoryGraphSourceIdentity& other) const;

    bool samePortableSourceAs(
        const StoryGraphSourceIdentity& other) const;
    bool sameAnalyzedContentAs(
        const StoryGraphSourceIdentity& other) const;
};

struct StoryGraphSourcePosition
{
    int line = 1;
    int column = 1;
    qsizetype utf16Offset = 0;
    qsizetype utf8ByteOffset = 0;

    bool operator==(const StoryGraphSourcePosition& other) const;
    bool operator!=(const StoryGraphSourcePosition& other) const;

    bool isValid() const;
};

// The end position is exclusive.
struct StoryGraphSourceRange
{
    StoryGraphSourcePosition start;
    StoryGraphSourcePosition end;

    bool operator==(const StoryGraphSourceRange& other) const;
    bool operator!=(const StoryGraphSourceRange& other) const;

    bool isValid() const;
    bool isEmpty() const;
};

struct StoryGraphNode
{
    QString id;
    StoryGraphNodeKind kind = StoryGraphNodeKind::Statement;
    StoryGraphCertainty certainty = StoryGraphCertainty::Certain;
    QString title;
    QString summary;
    StoryGraphSourceIdentity source;
    StoryGraphSourceRange sourceRange;
    QString controlFlowNodeId;
    QString scopeId;
    QString apiName;
    QString literalTarget;
    QString variableName;
    QString resolvedPortableRootKey;
    QString resolvedVirtualPath;
    QString semanticFingerprint;
    QString structuralOccurrenceKey;

    bool operator==(const StoryGraphNode& other) const;
    bool operator!=(const StoryGraphNode& other) const;
};

struct StoryGraphEdge
{
    QString id;
    QString fromNodeId;
    QString toNodeId;
    StoryGraphEdgeKind kind = StoryGraphEdgeKind::Sequential;
    StoryGraphCertainty certainty = StoryGraphCertainty::Certain;
    QString label;
    QString semanticFingerprint;
    QString structuralOccurrenceKey;

    bool operator==(const StoryGraphEdge& other) const;
    bool operator!=(const StoryGraphEdge& other) const;
};

struct StoryGraphWarning
{
    StoryGraphWarningCategory category =
        StoryGraphWarningCategory::Semantic;
    StoryGraphWarningSeverity severity =
        StoryGraphWarningSeverity::Warning;
    QString diagnosticCode;
    QString message;
    StoryGraphSourceIdentity source;
    StoryGraphSourceRange sourceRange;
    QString relatedNodeId;

    bool operator==(const StoryGraphWarning& other) const;
    bool operator!=(const StoryGraphWarning& other) const;
};

struct StoryGraphResult
{
    StoryGraphKind kind = StoryGraphKind::ControlFlow;
    QString entryNodeId;
    QString exitNodeId;
    QList<StoryGraphNode> nodes;
    QList<StoryGraphEdge> edges;
    QList<StoryGraphWarning> warnings;
    bool complete = true;

    bool operator==(const StoryGraphResult& other) const;
    bool operator!=(const StoryGraphResult& other) const;

    const StoryGraphNode* findNode(const QString& nodeId) const;
    bool containsNode(const QString& nodeId) const;
};

struct StoryGraphDocumentResult
{
    StoryGraphSourceIdentity source;
    quint64 analysisGeneration = 0;
    StoryGraphDocumentStatus status =
        StoryGraphDocumentStatus::Complete;
    StoryGraphResult controlFlowGraph;
    StoryGraphResult semanticGraph{
        StoryGraphKind::StorySemantics};
    QList<StoryGraphWarning> warnings;

    bool operator==(const StoryGraphDocumentResult& other) const;
    bool operator!=(const StoryGraphDocumentResult& other) const;

    bool hasUsableGraph() const;
    bool wasCancelled() const;
};

// This intentionally contains only fields allowed to influence a node ID.
// strictVirtualPath must already be a canonical strict relative virtual path.
// semanticFingerprint must be normalized and free of comments/whitespace
// trivia. structuralOccurrenceKey must be deterministic within scope.
struct StoryGraphStableNodeIdInput
{
    QString portableRootKey;
    QString strictVirtualPath;
    QString scopeId;
    StoryGraphNodeKind kind = StoryGraphNodeKind::Statement;
    QString semanticFingerprint;
    QString structuralOccurrenceKey;

    bool operator==(const StoryGraphStableNodeIdInput& other) const;
    bool operator!=(const StoryGraphStableNodeIdInput& other) const;
};

// Edge IDs have no source-location inputs. They are derived from stable
// endpoint IDs plus semantic and structural edge identity.
struct StoryGraphStableEdgeIdInput
{
    QString fromNodeId;
    QString toNodeId;
    StoryGraphEdgeKind kind = StoryGraphEdgeKind::Sequential;
    QString semanticFingerprint;
    QString structuralOccurrenceKey;

    bool operator==(const StoryGraphStableEdgeIdInput& other) const;
    bool operator!=(const StoryGraphStableEdgeIdInput& other) const;
};

QString storyGraphCertaintyToString(
    StoryGraphCertainty certainty);
QString storyGraphKindToString(
    StoryGraphKind kind);
QString storyGraphNodeKindToString(
    StoryGraphNodeKind kind);
QString storyGraphEdgeKindToString(
    StoryGraphEdgeKind kind);
QString storyGraphWarningSeverityToString(
    StoryGraphWarningSeverity severity);
QString storyGraphWarningCategoryToString(
    StoryGraphWarningCategory category);
QString storyGraphDocumentStatusToString(
    StoryGraphDocumentStatus status);

// IDs are self-describing lowercase SHA-256 values:
// "story-node-v1:<64 hex>" and "story-edge-v1:<64 hex>".
// The hashed preimage uses a fixed domain, schema version, field names, and
// an unsigned 64-bit big-endian byte length before every UTF-8 field value.
QString makeStoryGraphNodeId(
    const StoryGraphStableNodeIdInput& input);
QString makeStoryGraphEdgeId(
    const StoryGraphStableEdgeIdInput& input);

bool isStoryGraphNodeId(const QString& value);
bool isStoryGraphEdgeId(const QString& value);
