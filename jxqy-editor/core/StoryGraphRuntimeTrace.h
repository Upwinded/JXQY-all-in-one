#pragma once

#include "StoryGraphProjectResolver.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

#include <memory>
#include <optional>

class QFile;
class QCryptographicHash;
class QJsonObject;

enum class StoryGraphRuntimeTraceEventType
{
    SessionStart,
    SessionFinish,
    ScriptStart,
    ScriptFinish,
    SourceLine,
    ApiCall,
    MapChange,
    VariableChange,
    TraceDropped
};

enum class StoryGraphRuntimeTraceSourceLayer
{
    Formal,
    Overlay
};

enum class StoryGraphRuntimeTraceStreamState
{
    Unbound,
    WaitingForFile,
    Live,
    Complete,
    Incomplete,
    Invalid
};

enum class StoryGraphRuntimeTraceIssueCode
{
    InvalidLimits,
    SourceNotConfigured,
    FileNotFound,
    FileNotRegular,
    FileOpenFailed,
    FileIdentityUnavailable,
    FileReplaced,
    FileTruncated,
    FileContentChanged,
    FileTooLarge,
    FileReadFailed,
    LineTooLong,
    InvalidUtf8,
    InvalidJson,
    InvalidRootType,
    UnsupportedSchemaVersion,
    SessionMismatch,
    MissingField,
    InvalidFieldType,
    InvalidFieldValue,
    SequenceOutOfOrder,
    EventAfterSessionFinish,
    InvalidExecutionLifecycle,
    EventLimitReached,
    ExecutionLimitReached,
    IssueLimitReached,
    IncompleteLine,
    MissingSessionStart,
    MissingSessionFinish
};

struct StoryGraphRuntimeTraceEvent
{
    quint64 sequence = 0;
    StoryGraphRuntimeTraceEventType type =
        StoryGraphRuntimeTraceEventType::SessionStart;
    std::optional<quint64> elapsedMicroseconds;

    std::optional<quint64> executionId;
    std::optional<quint64> parentExecutionId;
    QString virtualPath;
    QByteArray contentSha256;
    StoryGraphContentRootKind rootKind =
        StoryGraphContentRootKind::Active;
    quint64 rootOrdinal = 0;
    std::optional<QString> resourcePackId;
    StoryGraphRuntimeTraceSourceLayer sourceLayer =
        StoryGraphRuntimeTraceSourceLayer::Formal;

    quint64 line = 0;
    QString apiName;
    QString target;
    QString variableName;
    QString valueType;
    QString beforeValue;
    QString afterValue;
    quint64 droppedSourceLineCount = 0;
    QString status;
};

struct StoryGraphRuntimeTraceIssue
{
    StoryGraphRuntimeTraceIssueCode code =
        StoryGraphRuntimeTraceIssueCode::FileReadFailed;
    QString message;
    qint64 byteOffset = -1;
    quint64 lineNumber = 0;
};

struct StoryGraphRuntimeTraceLimits
{
    static constexpr qsizetype DefaultMaximumLineBytes =
        1024 * 1024;
    static constexpr qint64 DefaultMaximumFileBytes =
        256LL * 1024LL * 1024LL;
    static constexpr qsizetype DefaultMaximumEventCount =
        1'000'000;
    static constexpr qsizetype DefaultMaximumExecutionCount =
        100'000;
    static constexpr qsizetype DefaultMaximumIssueCount =
        1024;
    static constexpr qsizetype DefaultMaximumStringBytes =
        64 * 1024;
    static constexpr qint64 DefaultMaximumRefreshBytes =
        1024 * 1024;

    qsizetype maximumLineBytes =
        DefaultMaximumLineBytes;
    qint64 maximumFileBytes =
        DefaultMaximumFileBytes;
    qsizetype maximumEventCount =
        DefaultMaximumEventCount;
    qsizetype maximumExecutionCount =
        DefaultMaximumExecutionCount;
    qsizetype maximumIssueCount =
        DefaultMaximumIssueCount;
    qsizetype maximumStringBytes =
        DefaultMaximumStringBytes;
    qint64 maximumRefreshBytes =
        DefaultMaximumRefreshBytes;

    bool isValid() const;
};

// Incrementally tails one trusted desktop-run trace path. The caller owns
// scheduling: each refresh consumes at most maximumRefreshBytes. Once an
// established file is replaced or truncated, the reader fails closed. Growing
// live files are provisionally treated as append-only so refresh work stays
// bounded; after the producer becomes terminal, the final consumed prefix is
// re-read twice under one stable file revision before Complete or Incomplete is
// published. A changed prefix clears parsed events and requires a new binding.
class StoryGraphRuntimeTraceTailer
{
public:
    StoryGraphRuntimeTraceTailer();
    ~StoryGraphRuntimeTraceTailer();

    bool setLimits(
        const StoryGraphRuntimeTraceLimits& limits);
    StoryGraphRuntimeTraceLimits limits() const;

    void bindSource(
        const QString& runtimeTracePath,
        const QString& sessionId);
    void clear();

    // Returns false if this pass observed a stream issue. Waiting for a live
    // producer to create its first file is not an issue.
    bool refresh();

    // Marks the producer terminal and consumes one bounded pass. Call refresh()
    // again while state() is Live and unread bytes remain. A forced termination
    // may legitimately settle as Incomplete without a session.finish event.
    bool finalize(bool forcedTermination);

    // Releases already published event payloads without changing the trusted
    // file identity, consumed-prefix digest, sequence, or execution-lifecycle
    // validator. Events appended after this call remain available normally.
    quint64 discardEventsRetainingCursor();

    QString runtimeTracePath() const;
    QString sessionId() const;
    StoryGraphRuntimeTraceStreamState state() const;
    const QVector<StoryGraphRuntimeTraceEvent>& events() const;
    const QVector<StoryGraphRuntimeTraceIssue>& issues() const;
    qsizetype eventCount() const;
    qsizetype pendingByteCount() const;
    qint64 consumedByteCount() const;
    qint64 observedFileBytes() const;
    quint64 nextExpectedSequence() const;
    quint64 discardedThroughSequence() const;
    quint64 totalAcceptedEventCount() const;
    quint64 prefixVerificationBytesReadForTests() const;
    bool hasSessionStart() const;
    bool hasSessionFinish() const;
    bool hasUnreadBytes() const;

private:
    struct FileIdentity
    {
        quint64 first = 0;
        quint64 second = 0;
        bool valid = false;

        bool operator==(const FileIdentity& other) const
        {
            return valid && other.valid &&
                first == other.first &&
                second == other.second;
        }

        bool operator!=(const FileIdentity& other) const
        {
            return !(*this == other);
        }
    };

    struct ExecutionState
    {
        bool active = true;
    };

    struct FileRevision
    {
        quint64 first = 0;
        quint64 second = 0;
        bool valid = false;

        bool operator==(const FileRevision& other) const
        {
            return valid && other.valid &&
                first == other.first &&
                second == other.second;
        }

        bool operator!=(const FileRevision& other) const
        {
            return !(*this == other);
        }
    };

    bool openOrValidateSource();
    bool verifyConsumedPrefix(
        qint64& remainingByteBudget,
        const FileRevision& currentRevision,
        qint64 currentObservedFileBytes,
        bool& complete);
    void resetPrefixVerification();
    bool readBoundedBytes(qint64 byteBudget);
    void consumeBytes(
        const QByteArray& bytes,
        qint64 firstByteOffset);
    void consumeCompleteLine(
        const QByteArray& line,
        qint64 byteOffset,
        quint64 lineNumber);
    bool parseEvent(
        const QByteArray& line,
        StoryGraphRuntimeTraceEvent& event,
        StoryGraphRuntimeTraceIssue& issue) const;
    bool validateAndAppendEvent(
        const StoryGraphRuntimeTraceEvent& event,
        qint64 byteOffset,
        quint64 lineNumber);
    void updateConsumedDigest();
    void updateTerminalState();
    void reportIssue(
        StoryGraphRuntimeTraceIssueCode code,
        const QString& message,
        qint64 byteOffset = -1,
        quint64 lineNumber = 0);
    void invalidate(
        StoryGraphRuntimeTraceIssueCode code,
        const QString& message,
        qint64 byteOffset = -1,
        quint64 lineNumber = 0);
    void resetStreamState();
    void closeSource();

    static bool strictUtf8(const QByteArray& bytes);
    static bool exactLowercaseUuid(const QString& value);
    static bool exactLowercaseSha256(
        const QString& value,
        QByteArray& bytes);
    static FileIdentity identityForFile(QFile& file);
    static FileRevision revisionForFile(QFile& file);

    StoryGraphRuntimeTraceLimits currentLimits;
    QString currentPath;
    QString currentSessionId;
    std::unique_ptr<QFile> file;
    FileIdentity fileIdentity;
    QVector<StoryGraphRuntimeTraceEvent> parsedEvents;
    QVector<StoryGraphRuntimeTraceIssue> streamIssues;
    QHash<quint64, ExecutionState> executions;
    QByteArray pendingLine;
    QByteArray consumedDigest;
    std::unique_ptr<QCryptographicHash> consumedHasher;
    std::unique_ptr<QCryptographicHash> verificationHasher;
    FileRevision trustedRevision;
    FileRevision verificationRevision;
    qint64 trustedObservedFileBytes = -1;
    qint64 verificationObservedFileBytes = -1;
    qint64 verificationTargetOffset = -1;
    qint64 verificationOffset = 0;
    qint64 terminalVerifiedOffset = -1;
    int verificationPass = 0;
    qint64 readOffset = 0;
    qint64 pendingLineOffset = 0;
    qint64 latestObservedFileBytes = 0;
    quint64 currentLineNumber = 1;
    quint64 expectedSequence = 1;
    quint64 discardedSequence = 0;
    quint64 acceptedEventCount = 0;
    quint64 prefixVerificationBytesRead = 0;
    bool discardingLongLine = false;
    bool sourceWasEstablished = false;
    bool producerIsTerminal = false;
    bool producerWasForced = false;
    bool sessionStartSeen = false;
    bool sessionFinishSeen = false;
    bool issueLimitReported = false;
    StoryGraphRuntimeTraceStreamState currentState =
        StoryGraphRuntimeTraceStreamState::Unbound;
};

QString storyGraphRuntimeTraceEventTypeToString(
    StoryGraphRuntimeTraceEventType type);
QString storyGraphRuntimeTraceSourceLayerToString(
    StoryGraphRuntimeTraceSourceLayer sourceLayer);
QString storyGraphRuntimeTraceIssueCodeToString(
    StoryGraphRuntimeTraceIssueCode code);
