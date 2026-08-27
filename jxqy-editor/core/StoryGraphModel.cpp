#include "StoryGraphModel.h"

#include <QByteArrayView>
#include <QCryptographicHash>

namespace
{
constexpr auto NodeIdentifierPrefix = "story-node-v1:";
constexpr auto EdgeIdentifierPrefix = "story-edge-v1:";
constexpr auto NodeIdentifierDomain =
    "jxqy-editor.story-graph.node-id";
constexpr auto EdgeIdentifierDomain =
    "jxqy-editor.story-graph.edge-id";
constexpr auto IdentifierSchemaVersion = "1";

void addLengthPrefixedBytes(
    QCryptographicHash& hash,
    QByteArrayView bytes)
{
    quint64 length = static_cast<quint64>(bytes.size());
    char encodedLength[sizeof(length)] = {};
    for (int index = static_cast<int>(sizeof(length)) - 1;
         index >= 0;
         --index)
    {
        encodedLength[index] =
            static_cast<char>(length & 0xffU);
        length >>= 8U;
    }

    hash.addData(
        QByteArrayView(encodedLength, sizeof(encodedLength)));
    hash.addData(bytes);
}

void addUtf8Field(
    QCryptographicHash& hash,
    QByteArrayView fieldName,
    const QString& value)
{
    addLengthPrefixedBytes(hash, fieldName);
    const QByteArray utf8 = value.toUtf8();
    addLengthPrefixedBytes(hash, QByteArrayView(utf8));
}

void addAsciiField(
    QCryptographicHash& hash,
    QByteArrayView fieldName,
    QByteArrayView value)
{
    addLengthPrefixedBytes(hash, fieldName);
    addLengthPrefixedBytes(hash, value);
}

QString finishIdentifier(
    QCryptographicHash& hash,
    QByteArrayView prefix)
{
    const QByteArray digest = hash.result().toHex();
    return QString::fromLatin1(prefix) +
        QString::fromLatin1(digest);
}

bool isLowerHex(QByteArrayView value)
{
    for (const char character : value)
    {
        const bool digit =
            character >= '0' && character <= '9';
        const bool lowerHex =
            character >= 'a' && character <= 'f';
        if (!digit && !lowerHex)
            return false;
    }
    return true;
}

bool hasIdentifierShape(
    const QString& value,
    QByteArrayView prefix)
{
    const QByteArray latin1 = value.toLatin1();
    if (latin1.size() !=
        prefix.size() +
            QCryptographicHash::hashLength(
                QCryptographicHash::Sha256) *
                2)
    {
        return false;
    }
    if (!QByteArrayView(latin1).startsWith(prefix))
        return false;
    return isLowerHex(
        QByteArrayView(latin1).sliced(prefix.size()));
}
}

bool StoryGraphSourceIdentity::operator==(
    const StoryGraphSourceIdentity& other) const
{
    return portableRootKey == other.portableRootKey &&
        virtualPath == other.virtualPath &&
        canonicalAbsolutePath == other.canonicalAbsolutePath &&
        contentSha256 == other.contentSha256 &&
        documentRevision == other.documentRevision &&
        fromEditorBuffer == other.fromEditorBuffer;
}

bool StoryGraphSourceIdentity::operator!=(
    const StoryGraphSourceIdentity& other) const
{
    return !(*this == other);
}

bool StoryGraphSourceIdentity::samePortableSourceAs(
    const StoryGraphSourceIdentity& other) const
{
    return portableRootKey == other.portableRootKey &&
        virtualPath == other.virtualPath;
}

bool StoryGraphSourceIdentity::sameAnalyzedContentAs(
    const StoryGraphSourceIdentity& other) const
{
    if (!samePortableSourceAs(other))
        return false;
    if (!contentSha256.isEmpty() ||
        !other.contentSha256.isEmpty())
    {
        return contentSha256 == other.contentSha256;
    }
    if (documentRevision >= 0 ||
        other.documentRevision >= 0)
    {
        return documentRevision == other.documentRevision;
    }
    return false;
}

bool StoryGraphSourcePosition::operator==(
    const StoryGraphSourcePosition& other) const
{
    return line == other.line &&
        column == other.column &&
        utf16Offset == other.utf16Offset &&
        utf8ByteOffset == other.utf8ByteOffset;
}

bool StoryGraphSourcePosition::operator!=(
    const StoryGraphSourcePosition& other) const
{
    return !(*this == other);
}

bool StoryGraphSourcePosition::isValid() const
{
    return line >= 1 &&
        column >= 1 &&
        utf16Offset >= 0 &&
        utf8ByteOffset >= 0;
}

bool StoryGraphSourceRange::operator==(
    const StoryGraphSourceRange& other) const
{
    return start == other.start &&
        end == other.end;
}

bool StoryGraphSourceRange::operator!=(
    const StoryGraphSourceRange& other) const
{
    return !(*this == other);
}

bool StoryGraphSourceRange::isValid() const
{
    if (!start.isValid() || !end.isValid())
        return false;
    if (end.utf16Offset < start.utf16Offset ||
        end.utf8ByteOffset < start.utf8ByteOffset ||
        end.line < start.line)
    {
        return false;
    }
    return end.line != start.line ||
        end.column >= start.column;
}

bool StoryGraphSourceRange::isEmpty() const
{
    return isValid() &&
        start.utf16Offset == end.utf16Offset &&
        start.utf8ByteOffset == end.utf8ByteOffset;
}

bool StoryGraphNode::operator==(
    const StoryGraphNode& other) const
{
    return id == other.id &&
        kind == other.kind &&
        certainty == other.certainty &&
        title == other.title &&
        summary == other.summary &&
        source == other.source &&
        sourceRange == other.sourceRange &&
        controlFlowNodeId ==
            other.controlFlowNodeId &&
        scopeId == other.scopeId &&
        apiName == other.apiName &&
        literalTarget == other.literalTarget &&
        variableName == other.variableName &&
        resolvedPortableRootKey ==
            other.resolvedPortableRootKey &&
        resolvedVirtualPath == other.resolvedVirtualPath &&
        semanticFingerprint ==
            other.semanticFingerprint &&
        structuralOccurrenceKey ==
            other.structuralOccurrenceKey;
}

bool StoryGraphNode::operator!=(
    const StoryGraphNode& other) const
{
    return !(*this == other);
}

bool StoryGraphEdge::operator==(
    const StoryGraphEdge& other) const
{
    return id == other.id &&
        fromNodeId == other.fromNodeId &&
        toNodeId == other.toNodeId &&
        kind == other.kind &&
        certainty == other.certainty &&
        label == other.label &&
        semanticFingerprint ==
            other.semanticFingerprint &&
        structuralOccurrenceKey ==
            other.structuralOccurrenceKey;
}

bool StoryGraphEdge::operator!=(
    const StoryGraphEdge& other) const
{
    return !(*this == other);
}

bool StoryGraphWarning::operator==(
    const StoryGraphWarning& other) const
{
    return category == other.category &&
        severity == other.severity &&
        diagnosticCode == other.diagnosticCode &&
        message == other.message &&
        source == other.source &&
        sourceRange == other.sourceRange &&
        relatedNodeId == other.relatedNodeId;
}

bool StoryGraphWarning::operator!=(
    const StoryGraphWarning& other) const
{
    return !(*this == other);
}

bool StoryGraphResult::operator==(
    const StoryGraphResult& other) const
{
    return kind == other.kind &&
        entryNodeId == other.entryNodeId &&
        exitNodeId == other.exitNodeId &&
        nodes == other.nodes &&
        edges == other.edges &&
        warnings == other.warnings &&
        complete == other.complete;
}

bool StoryGraphResult::operator!=(
    const StoryGraphResult& other) const
{
    return !(*this == other);
}

const StoryGraphNode* StoryGraphResult::findNode(
    const QString& nodeId) const
{
    for (const StoryGraphNode& node : nodes)
    {
        if (node.id == nodeId)
            return &node;
    }
    return nullptr;
}

bool StoryGraphResult::containsNode(
    const QString& nodeId) const
{
    return findNode(nodeId) != nullptr;
}

bool StoryGraphDocumentResult::operator==(
    const StoryGraphDocumentResult& other) const
{
    return source == other.source &&
        analysisGeneration == other.analysisGeneration &&
        status == other.status &&
        controlFlowGraph == other.controlFlowGraph &&
        semanticGraph == other.semanticGraph &&
        warnings == other.warnings;
}

bool StoryGraphDocumentResult::operator!=(
    const StoryGraphDocumentResult& other) const
{
    return !(*this == other);
}

bool StoryGraphDocumentResult::hasUsableGraph() const
{
    return status == StoryGraphDocumentStatus::Complete ||
        status == StoryGraphDocumentStatus::Partial;
}

bool StoryGraphDocumentResult::wasCancelled() const
{
    return status == StoryGraphDocumentStatus::Cancelled;
}

bool StoryGraphStableNodeIdInput::operator==(
    const StoryGraphStableNodeIdInput& other) const
{
    return portableRootKey == other.portableRootKey &&
        strictVirtualPath == other.strictVirtualPath &&
        scopeId == other.scopeId &&
        kind == other.kind &&
        semanticFingerprint ==
            other.semanticFingerprint &&
        structuralOccurrenceKey ==
            other.structuralOccurrenceKey;
}

bool StoryGraphStableNodeIdInput::operator!=(
    const StoryGraphStableNodeIdInput& other) const
{
    return !(*this == other);
}

bool StoryGraphStableEdgeIdInput::operator==(
    const StoryGraphStableEdgeIdInput& other) const
{
    return fromNodeId == other.fromNodeId &&
        toNodeId == other.toNodeId &&
        kind == other.kind &&
        semanticFingerprint ==
            other.semanticFingerprint &&
        structuralOccurrenceKey ==
            other.structuralOccurrenceKey;
}

bool StoryGraphStableEdgeIdInput::operator!=(
    const StoryGraphStableEdgeIdInput& other) const
{
    return !(*this == other);
}

QString storyGraphCertaintyToString(
    StoryGraphCertainty certainty)
{
    switch (certainty)
    {
    case StoryGraphCertainty::Certain:
        return QStringLiteral("certain");
    case StoryGraphCertainty::Dynamic:
        return QStringLiteral("dynamic");
    case StoryGraphCertainty::Unknown:
        return QStringLiteral("unknown");
    case StoryGraphCertainty::Warning:
        return QStringLiteral("warning");
    }
    return QStringLiteral("invalid");
}

QString storyGraphKindToString(
    StoryGraphKind kind)
{
    switch (kind)
    {
    case StoryGraphKind::ControlFlow:
        return QStringLiteral("control-flow");
    case StoryGraphKind::StorySemantics:
        return QStringLiteral("story-semantics");
    }
    return QStringLiteral("invalid");
}

QString storyGraphNodeKindToString(
    StoryGraphNodeKind kind)
{
    switch (kind)
    {
    case StoryGraphNodeKind::ChunkEntry:
        return QStringLiteral("chunk-entry");
    case StoryGraphNodeKind::ChunkExit:
        return QStringLiteral("chunk-exit");
    case StoryGraphNodeKind::FunctionEntry:
        return QStringLiteral("function-entry");
    case StoryGraphNodeKind::FunctionExit:
        return QStringLiteral("function-exit");
    case StoryGraphNodeKind::BasicBlock:
        return QStringLiteral("basic-block");
    case StoryGraphNodeKind::Statement:
        return QStringLiteral("statement");
    case StoryGraphNodeKind::Condition:
        return QStringLiteral("condition");
    case StoryGraphNodeKind::Merge:
        return QStringLiteral("merge");
    case StoryGraphNodeKind::LoopHeader:
        return QStringLiteral("loop-header");
    case StoryGraphNodeKind::Label:
        return QStringLiteral("label");
    case StoryGraphNodeKind::Goto:
        return QStringLiteral("goto");
    case StoryGraphNodeKind::Return:
        return QStringLiteral("return");
    case StoryGraphNodeKind::Break:
        return QStringLiteral("break");
    case StoryGraphNodeKind::Unreachable:
        return QStringLiteral("unreachable");
    case StoryGraphNodeKind::Dialogue:
        return QStringLiteral("dialogue");
    case StoryGraphNodeKind::Choice:
        return QStringLiteral("choice");
    case StoryGraphNodeKind::VariableRead:
        return QStringLiteral("variable-read");
    case StoryGraphNodeKind::VariableWrite:
        return QStringLiteral("variable-write");
    case StoryGraphNodeKind::SerialScriptCall:
        return QStringLiteral("serial-script-call");
    case StoryGraphNodeKind::ParallelScriptCall:
        return QStringLiteral("parallel-script-call");
    case StoryGraphNodeKind::MapLoad:
        return QStringLiteral("map-load");
    case StoryGraphNodeKind::Battle:
        return QStringLiteral("battle");
    case StoryGraphNodeKind::RegisteredApiCall:
        return QStringLiteral("registered-api-call");
    case StoryGraphNodeKind::DynamicCall:
        return QStringLiteral("dynamic-call");
    case StoryGraphNodeKind::UnknownCall:
        return QStringLiteral("unknown-call");
    case StoryGraphNodeKind::MissingTarget:
        return QStringLiteral("missing-target");
    case StoryGraphNodeKind::Warning:
        return QStringLiteral("warning");
    }
    return QStringLiteral("invalid");
}

QString storyGraphEdgeKindToString(
    StoryGraphEdgeKind kind)
{
    switch (kind)
    {
    case StoryGraphEdgeKind::Sequential:
        return QStringLiteral("sequential");
    case StoryGraphEdgeKind::TrueBranch:
        return QStringLiteral("true-branch");
    case StoryGraphEdgeKind::FalseBranch:
        return QStringLiteral("false-branch");
    case StoryGraphEdgeKind::Fallthrough:
        return QStringLiteral("fallthrough");
    case StoryGraphEdgeKind::Goto:
        return QStringLiteral("goto");
    case StoryGraphEdgeKind::LoopBody:
        return QStringLiteral("loop-body");
    case StoryGraphEdgeKind::LoopBack:
        return QStringLiteral("loop-back");
    case StoryGraphEdgeKind::Break:
        return QStringLiteral("break");
    case StoryGraphEdgeKind::Return:
        return QStringLiteral("return");
    case StoryGraphEdgeKind::Call:
        return QStringLiteral("call");
    case StoryGraphEdgeKind::ParallelCall:
        return QStringLiteral("parallel-call");
    case StoryGraphEdgeKind::Dynamic:
        return QStringLiteral("dynamic");
    case StoryGraphEdgeKind::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("invalid");
}

QString storyGraphWarningSeverityToString(
    StoryGraphWarningSeverity severity)
{
    switch (severity)
    {
    case StoryGraphWarningSeverity::Information:
        return QStringLiteral("information");
    case StoryGraphWarningSeverity::Warning:
        return QStringLiteral("warning");
    case StoryGraphWarningSeverity::Error:
        return QStringLiteral("error");
    }
    return QStringLiteral("invalid");
}

QString storyGraphWarningCategoryToString(
    StoryGraphWarningCategory category)
{
    switch (category)
    {
    case StoryGraphWarningCategory::Lexical:
        return QStringLiteral("lexical");
    case StoryGraphWarningCategory::Syntax:
        return QStringLiteral("syntax");
    case StoryGraphWarningCategory::ControlFlow:
        return QStringLiteral("control-flow");
    case StoryGraphWarningCategory::Semantic:
        return QStringLiteral("semantic");
    case StoryGraphWarningCategory::Resolution:
        return QStringLiteral("resolution");
    case StoryGraphWarningCategory::Budget:
        return QStringLiteral("budget");
    case StoryGraphWarningCategory::Cancellation:
        return QStringLiteral("cancellation");
    }
    return QStringLiteral("invalid");
}

QString storyGraphDocumentStatusToString(
    StoryGraphDocumentStatus status)
{
    switch (status)
    {
    case StoryGraphDocumentStatus::Complete:
        return QStringLiteral("complete");
    case StoryGraphDocumentStatus::Partial:
        return QStringLiteral("partial");
    case StoryGraphDocumentStatus::Failed:
        return QStringLiteral("failed");
    case StoryGraphDocumentStatus::Cancelled:
        return QStringLiteral("cancelled");
    }
    return QStringLiteral("invalid");
}

QString makeStoryGraphNodeId(
    const StoryGraphStableNodeIdInput& input)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    addAsciiField(
        hash, "domain", NodeIdentifierDomain);
    addAsciiField(
        hash, "schema-version", IdentifierSchemaVersion);
    addUtf8Field(
        hash, "portable-root-key", input.portableRootKey);
    addUtf8Field(
        hash, "strict-virtual-path", input.strictVirtualPath);
    addUtf8Field(
        hash, "scope-id", input.scopeId);
    addUtf8Field(
        hash, "node-kind",
        storyGraphNodeKindToString(input.kind));
    addUtf8Field(
        hash, "semantic-fingerprint",
        input.semanticFingerprint);
    addUtf8Field(
        hash, "structural-occurrence-key",
        input.structuralOccurrenceKey);
    return finishIdentifier(hash, NodeIdentifierPrefix);
}

QString makeStoryGraphEdgeId(
    const StoryGraphStableEdgeIdInput& input)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    addAsciiField(
        hash, "domain", EdgeIdentifierDomain);
    addAsciiField(
        hash, "schema-version", IdentifierSchemaVersion);
    addUtf8Field(
        hash, "from-node-id", input.fromNodeId);
    addUtf8Field(
        hash, "to-node-id", input.toNodeId);
    addUtf8Field(
        hash, "edge-kind",
        storyGraphEdgeKindToString(input.kind));
    addUtf8Field(
        hash, "semantic-fingerprint",
        input.semanticFingerprint);
    addUtf8Field(
        hash, "structural-occurrence-key",
        input.structuralOccurrenceKey);
    return finishIdentifier(hash, EdgeIdentifierPrefix);
}

bool isStoryGraphNodeId(const QString& value)
{
    return hasIdentifierShape(
        value, NodeIdentifierPrefix);
}

bool isStoryGraphEdgeId(const QString& value)
{
    return hasIdentifierShape(
        value, EdgeIdentifierPrefix);
}
