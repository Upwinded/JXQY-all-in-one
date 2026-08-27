#include "StoryGraphRuntimeTrace.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#include <Windows.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
constexpr qint64 ReadChunkBytes = 64 * 1024;
constexpr quint64 MaximumExactJsonInteger =
    9'007'199'254'740'991ULL;
constexpr qsizetype MaximumVirtualPathBytes =
    64 * 1024;
constexpr qsizetype MaximumResourcePackIdBytes =
    1024;
constexpr qsizetype MaximumApiNameBytes =
    1024;
constexpr qsizetype MaximumVariableNameBytes =
    4096;
constexpr qsizetype MaximumVariableValueBytes =
    64 * 1024;

qsizetype fieldStringLimit(
    qsizetype configuredLimit,
    qsizetype protocolLimit)
{
    return (std::min)(
        configuredLimit,
        protocolLimit);
}

bool jsonUnsignedInteger(
    const QJsonValue& value,
    quint64 minimum,
    quint64 maximum,
    quint64& output)
{
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) ||
        std::floor(number) != number ||
        number < static_cast<double>(minimum) ||
        number > static_cast<double>(maximum))
    {
        return false;
    }
    output = static_cast<quint64>(number);
    return true;
}

void assignIssue(
    StoryGraphRuntimeTraceIssue& issue,
    StoryGraphRuntimeTraceIssueCode code,
    const QString& message)
{
    issue.code = code;
    issue.message = message;
}

bool requiredString(
    const QJsonObject& object,
    const QString& name,
    qsizetype maximumBytes,
    QString& output,
    StoryGraphRuntimeTraceIssue& issue,
    bool allowNull = false)
{
    if (!object.contains(name))
    {
        assignIssue(
            issue,
            StoryGraphRuntimeTraceIssueCode::MissingField,
            QStringLiteral("Required field \"%1\" is missing.")
                .arg(name));
        return false;
    }
    const QJsonValue value = object.value(name);
    if (!value.isString())
    {
        assignIssue(
            issue,
            StoryGraphRuntimeTraceIssueCode::InvalidFieldType,
            QStringLiteral("Field \"%1\" must be a string.")
                .arg(name));
        return false;
    }
    output = value.toString();
    if (output.toUtf8().size() > maximumBytes ||
        (!allowNull &&
         output.contains(QChar::Null)))
    {
        assignIssue(
            issue,
            StoryGraphRuntimeTraceIssueCode::InvalidFieldValue,
            QStringLiteral(
                "Field \"%1\" contains a null character or exceeds "
                "the configured UTF-8 byte limit.")
                .arg(name));
        return false;
    }
    return true;
}

bool optionalString(
    const QJsonObject& object,
    const QString& name,
    qsizetype maximumBytes,
    std::optional<QString>& output,
    StoryGraphRuntimeTraceIssue& issue)
{
    output.reset();
    if (!object.contains(name))
        return true;
    QString value;
    if (!requiredString(
            object,
            name,
            maximumBytes,
            value,
            issue))
    {
        return false;
    }
    output = std::move(value);
    return true;
}

bool lowercaseAsciiName(
    const QString& value)
{
    if (value.isEmpty())
        return false;
    for (const QChar character : value)
    {
        const ushort codePoint =
            character.unicode();
        if (codePoint >=
                static_cast<ushort>('A') &&
            codePoint <=
                static_cast<ushort>('Z'))
        {
            return false;
        }
    }
    return true;
}

bool optionalUnsignedInteger(
    const QJsonObject& object,
    const QString& name,
    quint64 minimum,
    quint64 maximum,
    std::optional<quint64>& output,
    StoryGraphRuntimeTraceIssue& issue)
{
    output.reset();
    if (!object.contains(name))
        return true;
    quint64 value = 0;
    if (!jsonUnsignedInteger(
            object.value(name),
            minimum,
            maximum,
            value))
    {
        assignIssue(
            issue,
            StoryGraphRuntimeTraceIssueCode::InvalidFieldValue,
            QStringLiteral(
                "Field \"%1\" must be an exact JSON integer in "
                "the supported range.")
                .arg(name));
        return false;
    }
    output = value;
    return true;
}

bool requiredUnsignedInteger(
    const QJsonObject& object,
    const QString& name,
    quint64 minimum,
    quint64 maximum,
    quint64& output,
    StoryGraphRuntimeTraceIssue& issue)
{
    if (!object.contains(name))
    {
        assignIssue(
            issue,
            StoryGraphRuntimeTraceIssueCode::MissingField,
            QStringLiteral("Required field \"%1\" is missing.")
                .arg(name));
        return false;
    }
    if (!jsonUnsignedInteger(
            object.value(name),
            minimum,
            maximum,
            output))
    {
        assignIssue(
            issue,
            StoryGraphRuntimeTraceIssueCode::InvalidFieldValue,
            QStringLiteral(
                "Field \"%1\" must be an exact JSON integer in "
                "the supported range.")
                .arg(name));
        return false;
    }
    return true;
}

bool parseEventType(
    const QString& value,
    StoryGraphRuntimeTraceEventType& output)
{
    if (value == QStringLiteral("session.start"))
        output = StoryGraphRuntimeTraceEventType::SessionStart;
    else if (value == QStringLiteral("session.finish"))
        output = StoryGraphRuntimeTraceEventType::SessionFinish;
    else if (value == QStringLiteral("script.start"))
        output = StoryGraphRuntimeTraceEventType::ScriptStart;
    else if (value == QStringLiteral("script.finish"))
        output = StoryGraphRuntimeTraceEventType::ScriptFinish;
    else if (value == QStringLiteral("source.line"))
        output = StoryGraphRuntimeTraceEventType::SourceLine;
    else if (value == QStringLiteral("api.call"))
        output = StoryGraphRuntimeTraceEventType::ApiCall;
    else if (value == QStringLiteral("map.change"))
        output = StoryGraphRuntimeTraceEventType::MapChange;
    else if (value == QStringLiteral("variable.change"))
        output = StoryGraphRuntimeTraceEventType::VariableChange;
    else if (value == QStringLiteral("trace.dropped"))
        output = StoryGraphRuntimeTraceEventType::TraceDropped;
    else
        return false;
    return true;
}

bool parseRootKind(
    const QString& value,
    StoryGraphContentRootKind& output)
{
    if (value == QStringLiteral("active"))
        output = StoryGraphContentRootKind::Active;
    else if (value == QStringLiteral("dependency-id"))
        output = StoryGraphContentRootKind::DependencyId;
    else if (value == QStringLiteral("common"))
        output = StoryGraphContentRootKind::Common;
    else
        return false;
    return true;
}

bool parseSourceLayer(
    const QString& value,
    StoryGraphRuntimeTraceSourceLayer& output)
{
    if (value == QStringLiteral("formal"))
        output = StoryGraphRuntimeTraceSourceLayer::Formal;
    else if (value == QStringLiteral("overlay"))
        output = StoryGraphRuntimeTraceSourceLayer::Overlay;
    else
        return false;
    return true;
}

bool validSessionFinishStatus(const QString& value)
{
    return value == QStringLiteral("completed") ||
        value == QStringLiteral("resource-failure") ||
        value == QStringLiteral("engine-failure") ||
        value == QStringLiteral("scene-failure") ||
        value == QStringLiteral("orchestration-failure");
}

bool validScriptFinishStatus(const QString& value)
{
    return value == QStringLiteral("completed") ||
        value == QStringLiteral("load-error") ||
        value == QStringLiteral("runtime-error") ||
        value == QStringLiteral("aborted");
}

bool canonicalInteger(const QString& value)
{
    if (value == QStringLiteral("0"))
        return true;
    qsizetype index = 0;
    if (value.startsWith(QLatin1Char('-')))
    {
        if (value.size() == 1)
            return false;
        index = 1;
    }
    if (value.at(index) < QLatin1Char('1') ||
        value.at(index) > QLatin1Char('9'))
    {
        return false;
    }
    for (++index; index < value.size(); ++index)
    {
        if (value.at(index) < QLatin1Char('0') ||
            value.at(index) > QLatin1Char('9'))
        {
            return false;
        }
    }
    return true;
}

bool canonicalReal(const QString& value)
{
    if (value.isEmpty() ||
        value.startsWith(QLatin1Char('+')) ||
        value == QStringLiteral("nan") ||
        value == QStringLiteral("inf") ||
        value == QStringLiteral("-inf"))
    {
        return false;
    }

    qsizetype offset =
        value.startsWith(QLatin1Char('-')) ? 1 : 0;
    if (offset == value.size())
        return false;

    if (value.at(offset) == QLatin1Char('0'))
    {
        ++offset;
        if (offset < value.size() &&
            value.at(offset) >= QLatin1Char('0') &&
            value.at(offset) <= QLatin1Char('9'))
        {
            return false;
        }
    }
    else
    {
        if (value.at(offset) < QLatin1Char('1') ||
            value.at(offset) > QLatin1Char('9'))
        {
            return false;
        }
        while (offset < value.size() &&
               value.at(offset) >= QLatin1Char('0') &&
               value.at(offset) <= QLatin1Char('9'))
        {
            ++offset;
        }
    }

    if (offset < value.size() &&
        value.at(offset) == QLatin1Char('.'))
    {
        ++offset;
        const qsizetype fractionStart = offset;
        while (offset < value.size() &&
               value.at(offset) >= QLatin1Char('0') &&
               value.at(offset) <= QLatin1Char('9'))
        {
            ++offset;
        }
        if (offset == fractionStart)
            return false;
    }

    if (offset < value.size() &&
        value.at(offset) == QLatin1Char('e'))
    {
        ++offset;
        if (offset < value.size() &&
            value.at(offset) == QLatin1Char('-'))
        {
            ++offset;
        }
        const qsizetype exponentStart = offset;
        while (offset < value.size() &&
               value.at(offset) >= QLatin1Char('0') &&
               value.at(offset) <= QLatin1Char('9'))
        {
            ++offset;
        }
        if (offset == exponentStart ||
            (value.at(exponentStart) == QLatin1Char('0') &&
             offset - exponentStart > 1))
        {
            return false;
        }
    }

    return offset == value.size() &&
        value != QStringLiteral("-0");
}

bool canonicalValue(
    const QString& type,
    const QString& value)
{
    if (type == QStringLiteral("integer"))
        return canonicalInteger(value);
    if (type == QStringLiteral("real"))
        return canonicalReal(value);
    if (type == QStringLiteral("string"))
        return true;
    if (type == QStringLiteral("boolean"))
    {
        return value == QStringLiteral("true") ||
            value == QStringLiteral("false");
    }
    if (type == QStringLiteral("nil"))
        return value.isEmpty();
    return false;
}
}

bool StoryGraphRuntimeTraceLimits::isValid() const
{
    return maximumLineBytes > 0 &&
        maximumFileBytes > 0 &&
        maximumEventCount > 0 &&
        maximumExecutionCount > 0 &&
        maximumIssueCount > 0 &&
        maximumStringBytes > 0 &&
        maximumRefreshBytes > 0 &&
        maximumLineBytes <= maximumFileBytes &&
        maximumRefreshBytes <= maximumFileBytes;
}

StoryGraphRuntimeTraceTailer::
StoryGraphRuntimeTraceTailer()
    : consumedHasher(
          std::make_unique<QCryptographicHash>(
              QCryptographicHash::Sha256))
    , verificationHasher(
          std::make_unique<QCryptographicHash>(
              QCryptographicHash::Sha256))
{
}

StoryGraphRuntimeTraceTailer::
~StoryGraphRuntimeTraceTailer() = default;

bool StoryGraphRuntimeTraceTailer::setLimits(
    const StoryGraphRuntimeTraceLimits& limits)
{
    if (!limits.isValid())
    {
        if (currentPath.isEmpty())
        {
            reportIssue(
                StoryGraphRuntimeTraceIssueCode::InvalidLimits,
                QStringLiteral(
                    "Runtime trace limits must all be positive and "
                    "must fit within the file limit."));
            currentState =
                StoryGraphRuntimeTraceStreamState::Invalid;
        }
        return false;
    }
    if (!currentPath.isEmpty())
        return false;
    currentLimits = limits;
    return true;
}

StoryGraphRuntimeTraceLimits
StoryGraphRuntimeTraceTailer::limits() const
{
    return currentLimits;
}

void StoryGraphRuntimeTraceTailer::bindSource(
    const QString& runtimeTracePath,
    const QString& sessionId)
{
    closeSource();
    currentPath = runtimeTracePath;
    currentSessionId = sessionId;
    parsedEvents.clear();
    parsedEvents.squeeze();
    streamIssues.clear();
    executions.clear();
    resetStreamState();
    issueLimitReported = false;

    if (!currentLimits.isValid())
    {
        invalidate(
            StoryGraphRuntimeTraceIssueCode::InvalidLimits,
            QStringLiteral(
                "Runtime trace limits are invalid."));
        return;
    }
    if (currentPath.isEmpty() ||
        currentSessionId.isEmpty())
    {
        invalidate(
            StoryGraphRuntimeTraceIssueCode::SourceNotConfigured,
            QStringLiteral(
                "The runtime trace path and session ID must both "
                "be configured."));
        return;
    }
    if (!exactLowercaseUuid(currentSessionId))
    {
        invalidate(
            StoryGraphRuntimeTraceIssueCode::InvalidFieldValue,
            QStringLiteral(
                "The configured runtime trace session ID must be "
                "an exact lowercase UUID."));
        return;
    }
    currentState =
        StoryGraphRuntimeTraceStreamState::WaitingForFile;
}

void StoryGraphRuntimeTraceTailer::clear()
{
    closeSource();
    currentPath.clear();
    currentSessionId.clear();
    parsedEvents.clear();
    parsedEvents.squeeze();
    streamIssues.clear();
    executions.clear();
    resetStreamState();
    issueLimitReported = false;
    currentState =
        StoryGraphRuntimeTraceStreamState::Unbound;
}

bool StoryGraphRuntimeTraceTailer::refresh()
{
    if (currentState ==
            StoryGraphRuntimeTraceStreamState::Complete ||
        currentState ==
            StoryGraphRuntimeTraceStreamState::Incomplete)
    {
        return true;
    }
    if (currentState ==
        StoryGraphRuntimeTraceStreamState::Invalid)
    {
        return false;
    }
    if (currentPath.isEmpty() ||
        currentSessionId.isEmpty())
    {
        invalidate(
            StoryGraphRuntimeTraceIssueCode::SourceNotConfigured,
            QStringLiteral(
                "The runtime trace source is not configured."));
        return false;
    }

    const qsizetype issueCountBefore =
        streamIssues.size();
    if (!openOrValidateSource())
    {
        if (currentState ==
            StoryGraphRuntimeTraceStreamState::WaitingForFile)
        {
            if (producerIsTerminal)
                updateTerminalState();
            return streamIssues.size() == issueCountBefore;
        }
        return false;
    }

    latestObservedFileBytes = file->size();
    const FileRevision currentRevision =
        revisionForFile(*file);
    if (latestObservedFileBytes < 0)
    {
        invalidate(
            StoryGraphRuntimeTraceIssueCode::FileReadFailed,
            QStringLiteral(
                "Cannot determine the runtime trace file size."));
    }
    else if (latestObservedFileBytes >
             currentLimits.maximumFileBytes)
    {
        invalidate(
            StoryGraphRuntimeTraceIssueCode::FileTooLarge,
            QStringLiteral(
                "Runtime trace file size %1 exceeds the %2-byte limit.")
                .arg(latestObservedFileBytes)
                .arg(currentLimits.maximumFileBytes));
    }
    else if (latestObservedFileBytes < readOffset)
    {
        invalidate(
            StoryGraphRuntimeTraceIssueCode::FileTruncated,
            QStringLiteral(
                "The runtime trace file was truncated after bytes "
                "had been consumed."));
    }
    else if (!currentRevision.valid)
    {
        invalidate(
            StoryGraphRuntimeTraceIssueCode::
                FileIdentityUnavailable,
            QStringLiteral(
                "Cannot read the runtime trace file revision."));
    }
    else
    {
        qint64 remainingByteBudget =
            currentLimits.maximumRefreshBytes;
        const bool verificationInProgress =
            verificationTargetOffset >= 0;
        const bool suspiciousSameSizeChange =
            trustedRevision.valid &&
            latestObservedFileBytes ==
                trustedObservedFileBytes &&
            currentRevision != trustedRevision &&
            readOffset > 0;
        const bool terminalNeedsVerification =
            producerIsTerminal &&
            readOffset ==
                latestObservedFileBytes &&
            terminalVerifiedOffset != readOffset;
        if (verificationInProgress ||
            suspiciousSameSizeChange ||
            terminalNeedsVerification)
        {
            bool prefixVerified = false;
            if (!verifyConsumedPrefix(
                    remainingByteBudget,
                    currentRevision,
                    latestObservedFileBytes,
                    prefixVerified))
            {
                prefixVerified = false;
            }
            if (prefixVerified &&
                currentState !=
                    StoryGraphRuntimeTraceStreamState::Invalid)
            {
                trustedRevision = currentRevision;
                trustedObservedFileBytes =
                    latestObservedFileBytes;
                if (producerIsTerminal)
                    terminalVerifiedOffset = readOffset;
                resetPrefixVerification();
            }
        }
        else if (readBoundedBytes(
                     remainingByteBudget))
        {
            // A growing live file is provisionally treated as append-only.
            // The complete consumed prefix is independently re-read twice
            // after the producer becomes terminal.
            trustedRevision = currentRevision;
            trustedObservedFileBytes =
                latestObservedFileBytes;
        }
    }

    closeSource();
    if (currentState !=
        StoryGraphRuntimeTraceStreamState::Invalid)
    {
        if (!producerIsTerminal)
        {
            currentState =
                StoryGraphRuntimeTraceStreamState::Live;
        }
        else
        {
            updateTerminalState();
        }
    }
    return currentState !=
            StoryGraphRuntimeTraceStreamState::Invalid &&
        streamIssues.size() == issueCountBefore;
}

bool StoryGraphRuntimeTraceTailer::finalize(
    bool forcedTermination)
{
    producerIsTerminal = true;
    producerWasForced = forcedTermination;
    const qsizetype issueCountBefore =
        streamIssues.size();
    const bool refreshed = refresh();
    if (currentState ==
        StoryGraphRuntimeTraceStreamState::WaitingForFile)
    {
        reportIssue(
            StoryGraphRuntimeTraceIssueCode::FileNotFound,
            QStringLiteral(
                "The terminal runtime trace file does not exist: %1")
                .arg(currentPath));
        currentState = forcedTermination
            ? StoryGraphRuntimeTraceStreamState::Incomplete
            : StoryGraphRuntimeTraceStreamState::Invalid;
    }
    else if (currentState !=
             StoryGraphRuntimeTraceStreamState::Invalid)
    {
        updateTerminalState();
    }
    return refreshed &&
        currentState !=
            StoryGraphRuntimeTraceStreamState::Invalid &&
        streamIssues.size() == issueCountBefore;
}

quint64 StoryGraphRuntimeTraceTailer::
discardEventsRetainingCursor()
{
    if (currentState ==
            StoryGraphRuntimeTraceStreamState::Unbound)
    {
        return discardedSequence;
    }
    if (expectedSequence > 1)
        discardedSequence = expectedSequence - 1;
    parsedEvents.clear();
    parsedEvents.squeeze();
    return discardedSequence;
}

QString StoryGraphRuntimeTraceTailer::
runtimeTracePath() const
{
    return currentPath;
}

QString StoryGraphRuntimeTraceTailer::sessionId() const
{
    return currentSessionId;
}

StoryGraphRuntimeTraceStreamState
StoryGraphRuntimeTraceTailer::state() const
{
    return currentState;
}

const QVector<StoryGraphRuntimeTraceEvent>&
StoryGraphRuntimeTraceTailer::events() const
{
    return parsedEvents;
}

const QVector<StoryGraphRuntimeTraceIssue>&
StoryGraphRuntimeTraceTailer::issues() const
{
    return streamIssues;
}

qsizetype StoryGraphRuntimeTraceTailer::
eventCount() const
{
    return parsedEvents.size();
}

qsizetype StoryGraphRuntimeTraceTailer::
pendingByteCount() const
{
    return pendingLine.size();
}

qint64 StoryGraphRuntimeTraceTailer::
consumedByteCount() const
{
    return readOffset;
}

qint64 StoryGraphRuntimeTraceTailer::
observedFileBytes() const
{
    return latestObservedFileBytes;
}

quint64 StoryGraphRuntimeTraceTailer::
nextExpectedSequence() const
{
    return expectedSequence;
}

quint64 StoryGraphRuntimeTraceTailer::
discardedThroughSequence() const
{
    return discardedSequence;
}

quint64 StoryGraphRuntimeTraceTailer::
totalAcceptedEventCount() const
{
    return acceptedEventCount;
}

quint64 StoryGraphRuntimeTraceTailer::
prefixVerificationBytesReadForTests() const
{
    return prefixVerificationBytesRead;
}

bool StoryGraphRuntimeTraceTailer::
hasSessionStart() const
{
    return sessionStartSeen;
}

bool StoryGraphRuntimeTraceTailer::
hasSessionFinish() const
{
    return sessionFinishSeen;
}

bool StoryGraphRuntimeTraceTailer::
hasUnreadBytes() const
{
    return latestObservedFileBytes > readOffset ||
        (verificationTargetOffset >= 0 &&
         verificationOffset <
             verificationTargetOffset) ||
        (producerIsTerminal &&
         sourceWasEstablished &&
         terminalVerifiedOffset != readOffset);
}

bool StoryGraphRuntimeTraceTailer::
openOrValidateSource()
{
    QFileInfo information(currentPath);
    if (!information.exists())
    {
        if (!sourceWasEstablished)
        {
            currentState =
                StoryGraphRuntimeTraceStreamState::
                    WaitingForFile;
            return false;
        }
        invalidate(
            StoryGraphRuntimeTraceIssueCode::FileNotFound,
            QStringLiteral(
                "An established runtime trace file disappeared: %1")
                .arg(currentPath));
        return false;
    }
    if (!information.isFile() ||
        information.isSymLink())
    {
        invalidate(
            StoryGraphRuntimeTraceIssueCode::FileNotRegular,
            QStringLiteral(
                "The runtime trace path is not a direct regular file: %1")
                .arg(currentPath));
        return false;
    }

    std::unique_ptr<QFile> currentFile =
        std::make_unique<QFile>(currentPath);
    if (!currentFile->open(QIODevice::ReadOnly))
    {
        invalidate(
            StoryGraphRuntimeTraceIssueCode::FileOpenFailed,
            QStringLiteral(
                "Cannot open runtime trace \"%1\": %2")
                .arg(
                    currentPath,
                    currentFile->errorString()));
        return false;
    }
    const FileIdentity identity =
        identityForFile(*currentFile);
    if (!identity.valid)
    {
        invalidate(
            StoryGraphRuntimeTraceIssueCode::
                FileIdentityUnavailable,
            QStringLiteral(
                "Cannot identify runtime trace file: %1")
                .arg(currentPath));
        return false;
    }
    if (!fileIdentity.valid)
    {
        fileIdentity = identity;
        sourceWasEstablished = true;
    }
    else if (identity != fileIdentity)
    {
        invalidate(
            StoryGraphRuntimeTraceIssueCode::FileReplaced,
            QStringLiteral(
                "The runtime trace file identity was replaced."));
        return false;
    }
    file = std::move(currentFile);
    return true;
}

bool StoryGraphRuntimeTraceTailer::
verifyConsumedPrefix(
    qint64& remainingByteBudget,
    const FileRevision& currentRevision,
    qint64 currentObservedFileBytes,
    bool& complete)
{
    complete = false;
    if (!file)
        return false;
    if (readOffset == 0)
    {
        complete = true;
        return true;
    }

    if (verificationTargetOffset < 0)
    {
        verificationTargetOffset = readOffset;
        verificationRevision = currentRevision;
        verificationObservedFileBytes =
            currentObservedFileBytes;
        verificationOffset = 0;
        verificationPass = 1;
        verificationHasher->reset();
    }
    else if (verificationTargetOffset != readOffset ||
             currentRevision != verificationRevision ||
             currentObservedFileBytes !=
                 verificationObservedFileBytes)
    {
        invalidate(
            StoryGraphRuntimeTraceIssueCode::FileContentChanged,
            QStringLiteral(
                "The runtime trace changed while its consumed prefix "
                "was being verified."));
        return false;
    }

    while (verificationPass <= 2)
    {
        if (!file->seek(verificationOffset))
        {
            invalidate(
                StoryGraphRuntimeTraceIssueCode::FileReadFailed,
                QStringLiteral(
                    "Cannot seek to verify consumed runtime trace bytes."));
            return false;
        }
        while (verificationOffset <
                   verificationTargetOffset &&
               remainingByteBudget > 0)
        {
            const qint64 request =
                (std::min)(
                    ReadChunkBytes,
                    (std::min)(
                        verificationTargetOffset -
                            verificationOffset,
                        remainingByteBudget));
            const QByteArray bytes =
                file->read(request);
            if (bytes.size() != request)
            {
                invalidate(
                    StoryGraphRuntimeTraceIssueCode::
                        FileReadFailed,
                    QStringLiteral(
                        "Cannot read all consumed runtime trace bytes "
                        "for prefix verification."));
                return false;
            }
            verificationHasher->addData(bytes);
            verificationOffset += bytes.size();
            remainingByteBudget -= bytes.size();
            prefixVerificationBytesRead +=
                static_cast<quint64>(bytes.size());
        }
        if (verificationOffset <
            verificationTargetOffset)
        {
            return true;
        }
        if (verificationHasher->result() !=
            consumedDigest)
        {
            invalidate(
                StoryGraphRuntimeTraceIssueCode::
                    FileContentChanged,
                QStringLiteral(
                    "Previously consumed runtime trace bytes changed."));
            return false;
        }

        const FileRevision endRevision =
            revisionForFile(*file);
        const qint64 endObservedFileBytes =
            file->size();
        if (!endRevision.valid ||
            endRevision != verificationRevision ||
            endObservedFileBytes !=
                verificationObservedFileBytes)
        {
            invalidate(
                StoryGraphRuntimeTraceIssueCode::
                    FileContentChanged,
                QStringLiteral(
                    "The runtime trace changed during prefix "
                    "verification."));
            return false;
        }
        if (verificationPass == 2)
        {
            complete = true;
            return true;
        }
        ++verificationPass;
        verificationOffset = 0;
        verificationHasher->reset();
        if (remainingByteBudget == 0)
            return true;
    }
    return true;
}

void StoryGraphRuntimeTraceTailer::
resetPrefixVerification()
{
    verificationRevision = FileRevision();
    verificationObservedFileBytes = -1;
    verificationTargetOffset = -1;
    verificationOffset = 0;
    verificationPass = 0;
    verificationHasher->reset();
}

bool StoryGraphRuntimeTraceTailer::readBoundedBytes(
    qint64 byteBudget)
{
    if (!file)
        return false;
    const qint64 targetOffset =
        (std::min)(
            latestObservedFileBytes,
            readOffset +
                byteBudget);
    if (targetOffset > readOffset)
        terminalVerifiedOffset = -1;
    if (!file->seek(readOffset))
    {
        invalidate(
            StoryGraphRuntimeTraceIssueCode::FileReadFailed,
            QStringLiteral(
                "Cannot seek to byte %1 in the runtime trace.")
                .arg(readOffset));
        return false;
    }

    while (readOffset < targetOffset &&
           currentState !=
               StoryGraphRuntimeTraceStreamState::Invalid)
    {
        const qint64 request =
            (std::min)(
                ReadChunkBytes,
                targetOffset - readOffset);
        const QByteArray bytes = file->read(request);
        if (bytes.isEmpty())
        {
            invalidate(
                StoryGraphRuntimeTraceIssueCode::FileReadFailed,
                QStringLiteral(
                    "Runtime trace ended before bounded target byte %1.")
                    .arg(targetOffset),
                readOffset,
                currentLineNumber);
            return false;
        }
        const qint64 firstByteOffset = readOffset;
        readOffset += bytes.size();
        consumedHasher->addData(bytes);
        consumeBytes(bytes, firstByteOffset);
    }
    if (currentState ==
        StoryGraphRuntimeTraceStreamState::Invalid)
    {
        return false;
    }
    updateConsumedDigest();
    return true;
}

void StoryGraphRuntimeTraceTailer::consumeBytes(
    const QByteArray& bytes,
    qint64 firstByteOffset)
{
    for (qsizetype index = 0;
         index < bytes.size() &&
         currentState !=
             StoryGraphRuntimeTraceStreamState::Invalid;
         ++index)
    {
        const char byte = bytes.at(index);
        const qint64 byteOffset =
            firstByteOffset + index;
        if (byte == '\n')
        {
            QByteArray completeLine = pendingLine;
            if (completeLine.endsWith('\r'))
                completeLine.chop(1);
            consumeCompleteLine(
                completeLine,
                pendingLineOffset,
                currentLineNumber);
            pendingLine.clear();
            ++currentLineNumber;
            pendingLineOffset = byteOffset + 1;
            continue;
        }
        if (pendingLine.size() >=
            currentLimits.maximumLineBytes)
        {
            discardingLongLine = true;
            invalidate(
                StoryGraphRuntimeTraceIssueCode::LineTooLong,
                QStringLiteral(
                    "A runtime trace line exceeded the %1-byte limit.")
                    .arg(
                        currentLimits.maximumLineBytes),
                pendingLineOffset,
                currentLineNumber);
            return;
        }
        pendingLine.append(byte);
    }
}

void StoryGraphRuntimeTraceTailer::
consumeCompleteLine(
    const QByteArray& line,
    qint64 byteOffset,
    quint64 lineNumber)
{
    StoryGraphRuntimeTraceEvent event;
    StoryGraphRuntimeTraceIssue issue;
    if (!parseEvent(line, event, issue))
    {
        invalidate(
            issue.code,
            issue.message,
            byteOffset,
            lineNumber);
        return;
    }
    validateAndAppendEvent(
        event,
        byteOffset,
        lineNumber);
}

bool StoryGraphRuntimeTraceTailer::parseEvent(
    const QByteArray& line,
    StoryGraphRuntimeTraceEvent& event,
    StoryGraphRuntimeTraceIssue& issue) const
{
    if (line.isEmpty())
    {
        assignIssue(
            issue,
            StoryGraphRuntimeTraceIssueCode::InvalidJson,
            QStringLiteral(
                "A runtime trace line must not be empty."));
        return false;
    }
    if (!strictUtf8(line))
    {
        assignIssue(
            issue,
            StoryGraphRuntimeTraceIssueCode::InvalidUtf8,
            QStringLiteral(
                "Runtime trace line is not valid strict UTF-8."));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(line, &parseError);
    if (parseError.error !=
        QJsonParseError::NoError)
    {
        assignIssue(
            issue,
            StoryGraphRuntimeTraceIssueCode::InvalidJson,
            QStringLiteral("Invalid runtime trace JSON: %1")
                .arg(parseError.errorString()));
        return false;
    }
    if (!document.isObject())
    {
        assignIssue(
            issue,
            StoryGraphRuntimeTraceIssueCode::InvalidRootType,
            QStringLiteral(
                "A runtime trace line must contain one JSON object."));
        return false;
    }
    const QJsonObject object = document.object();

    quint64 schemaVersion = 0;
    if (!requiredUnsignedInteger(
            object,
            QStringLiteral("schemaVersion"),
            1,
            MaximumExactJsonInteger,
            schemaVersion,
            issue))
    {
        return false;
    }
    if (schemaVersion != 1)
    {
        assignIssue(
            issue,
            StoryGraphRuntimeTraceIssueCode::
                UnsupportedSchemaVersion,
            QStringLiteral(
                "Unsupported runtime trace schema version: %1")
                .arg(schemaVersion));
        return false;
    }

    QString eventSessionId;
    if (!requiredString(
            object,
            QStringLiteral("sessionId"),
            currentLimits.maximumStringBytes,
            eventSessionId,
            issue))
    {
        return false;
    }
    if (eventSessionId != currentSessionId)
    {
        assignIssue(
            issue,
            StoryGraphRuntimeTraceIssueCode::SessionMismatch,
            QStringLiteral(
                "Runtime trace session \"%1\" does not match \"%2\".")
                .arg(
                    eventSessionId,
                    currentSessionId));
        return false;
    }
    if (!requiredUnsignedInteger(
            object,
            QStringLiteral("sequence"),
            1,
            MaximumExactJsonInteger,
            event.sequence,
            issue))
    {
        return false;
    }
    if (!optionalUnsignedInteger(
            object,
            QStringLiteral("elapsedMicroseconds"),
            0,
            MaximumExactJsonInteger,
            event.elapsedMicroseconds,
            issue))
    {
        return false;
    }

    QString eventType;
    if (!requiredString(
            object,
            QStringLiteral("eventType"),
            currentLimits.maximumStringBytes,
            eventType,
            issue))
    {
        return false;
    }
    if (!parseEventType(eventType, event.type))
    {
        assignIssue(
            issue,
            StoryGraphRuntimeTraceIssueCode::InvalidFieldValue,
            QStringLiteral(
                "Unsupported runtime trace eventType: %1")
                .arg(eventType));
        return false;
    }

    const auto readExecutionId =
        [&object, &event, &issue]()
        {
            quint64 executionId = 0;
            if (!requiredUnsignedInteger(
                object,
                QStringLiteral("executionId"),
                1,
                MaximumExactJsonInteger,
                executionId,
                issue))
            {
                return false;
            }
            event.executionId = executionId;
            return true;
        };

    switch (event.type)
    {
    case StoryGraphRuntimeTraceEventType::SessionStart:
        return true;

    case StoryGraphRuntimeTraceEventType::SessionFinish:
        if (!requiredString(
                object,
                QStringLiteral("status"),
                currentLimits.maximumStringBytes,
                event.status,
                issue))
        {
            return false;
        }
        if (!validSessionFinishStatus(event.status))
        {
            assignIssue(
                issue,
                StoryGraphRuntimeTraceIssueCode::InvalidFieldValue,
                QStringLiteral(
                    "session.finish status is not a supported value."));
            return false;
        }
        return true;

    case StoryGraphRuntimeTraceEventType::ScriptStart:
    {
        if (!readExecutionId() ||
            !optionalUnsignedInteger(
                object,
                QStringLiteral("parentExecutionId"),
                1,
                MaximumExactJsonInteger,
                event.parentExecutionId,
                issue) ||
            !requiredString(
                object,
                QStringLiteral("virtualPath"),
                fieldStringLimit(
                    currentLimits.maximumStringBytes,
                    MaximumVirtualPathBytes),
                event.virtualPath,
                issue))
        {
            return false;
        }
        QString pathRejection;
        if (!StoryGraphProjectResolver::
                isStrictRelativeVirtualPath(
                    event.virtualPath,
                    &pathRejection))
        {
            assignIssue(
                issue,
                StoryGraphRuntimeTraceIssueCode::InvalidFieldValue,
                QStringLiteral(
                    "script.start virtualPath is not strict: %1")
                    .arg(pathRejection));
            return false;
        }
        QString digest;
        if (!requiredString(
                object,
                QStringLiteral("contentSha256"),
                currentLimits.maximumStringBytes,
                digest,
                issue) ||
            !exactLowercaseSha256(
                digest,
                event.contentSha256))
        {
            if (issue.message.isEmpty())
            {
                assignIssue(
                    issue,
                    StoryGraphRuntimeTraceIssueCode::
                        InvalidFieldValue,
                    QStringLiteral(
                        "script.start contentSha256 must be "
                        "64 lowercase hexadecimal digits."));
            }
            return false;
        }
        QString rootKind;
        quint64 rootOrdinal = 0;
        QString sourceLayer;
        if (!requiredString(
                object,
                QStringLiteral("rootKind"),
                currentLimits.maximumStringBytes,
                rootKind,
                issue) ||
            !parseRootKind(rootKind, event.rootKind) ||
            !requiredUnsignedInteger(
                object,
                QStringLiteral("rootOrdinal"),
                0,
                MaximumExactJsonInteger,
                rootOrdinal,
                issue) ||
            !optionalString(
                object,
                QStringLiteral("resourcePackId"),
                fieldStringLimit(
                    currentLimits.maximumStringBytes,
                    MaximumResourcePackIdBytes),
                event.resourcePackId,
                issue) ||
            !requiredString(
                object,
                QStringLiteral("sourceLayer"),
                currentLimits.maximumStringBytes,
                sourceLayer,
                issue) ||
            !parseSourceLayer(
                sourceLayer,
                event.sourceLayer))
        {
            if (issue.message.isEmpty())
            {
                assignIssue(
                    issue,
                    StoryGraphRuntimeTraceIssueCode::
                        InvalidFieldValue,
                    QStringLiteral(
                        "script.start rootKind or sourceLayer is "
                        "not supported."));
            }
            return false;
        }
        if (event.resourcePackId &&
            event.resourcePackId->isEmpty())
        {
            assignIssue(
                issue,
                StoryGraphRuntimeTraceIssueCode::
                    InvalidFieldValue,
                QStringLiteral(
                    "script.start resourcePackId must be omitted "
                    "or non-empty."));
            return false;
        }
        event.rootOrdinal = rootOrdinal;
        return true;
    }

    case StoryGraphRuntimeTraceEventType::ScriptFinish:
        if (!readExecutionId() ||
            !requiredString(
                object,
                QStringLiteral("status"),
                currentLimits.maximumStringBytes,
                event.status,
                issue))
        {
            return false;
        }
        if (!validScriptFinishStatus(event.status))
        {
            assignIssue(
                issue,
                StoryGraphRuntimeTraceIssueCode::InvalidFieldValue,
                QStringLiteral(
                    "script.finish status is not a supported value."));
            return false;
        }
        return true;

    case StoryGraphRuntimeTraceEventType::SourceLine:
    {
        quint64 lineNumber = 0;
        if (!readExecutionId() ||
            !requiredUnsignedInteger(
                object,
                QStringLiteral("line"),
                1,
                MaximumExactJsonInteger,
                lineNumber,
                issue))
        {
            return false;
        }
        event.line = lineNumber;
        return true;
    }

    case StoryGraphRuntimeTraceEventType::ApiCall:
        if (!readExecutionId() ||
            !requiredString(
                object,
                QStringLiteral("apiName"),
                fieldStringLimit(
                    currentLimits.maximumStringBytes,
                    MaximumApiNameBytes),
                event.apiName,
                issue))
        {
            return false;
        }
        if (!lowercaseAsciiName(
                event.apiName))
        {
            assignIssue(
                issue,
                StoryGraphRuntimeTraceIssueCode::InvalidFieldValue,
                QStringLiteral(
                    "api.call apiName must be non-empty and must not "
                    "contain ASCII uppercase letters."));
            return false;
        }
        return true;

    case StoryGraphRuntimeTraceEventType::MapChange:
        if (!optionalUnsignedInteger(
                object,
                QStringLiteral("executionId"),
                1,
                MaximumExactJsonInteger,
                event.executionId,
                issue) ||
            !requiredString(
                object,
                QStringLiteral("target"),
                fieldStringLimit(
                    currentLimits.maximumStringBytes,
                    MaximumVirtualPathBytes),
                event.target,
                issue))
        {
            return false;
        }
        if (event.target.isEmpty())
        {
            assignIssue(
                issue,
                StoryGraphRuntimeTraceIssueCode::InvalidFieldValue,
                QStringLiteral(
                    "map.change target must not be empty."));
            return false;
        }
        {
            QString pathRejection;
            if (!StoryGraphProjectResolver::
                    isStrictRelativeVirtualPath(
                        event.target,
                        &pathRejection))
            {
                assignIssue(
                    issue,
                    StoryGraphRuntimeTraceIssueCode::
                        InvalidFieldValue,
                    QStringLiteral(
                        "map.change target is not strict: %1")
                        .arg(pathRejection));
                return false;
            }
        }
        return true;

    case StoryGraphRuntimeTraceEventType::VariableChange:
        if (!optionalUnsignedInteger(
                object,
                QStringLiteral("executionId"),
                1,
                MaximumExactJsonInteger,
                event.executionId,
                issue) ||
            !requiredString(
                object,
                QStringLiteral("variableName"),
                fieldStringLimit(
                    currentLimits.maximumStringBytes,
                    MaximumVariableNameBytes),
                event.variableName,
                issue) ||
            !requiredString(
                object,
                QStringLiteral("valueType"),
                currentLimits.maximumStringBytes,
                event.valueType,
                issue) ||
            !requiredString(
                object,
                QStringLiteral("beforeValue"),
                fieldStringLimit(
                    currentLimits.maximumStringBytes,
                    MaximumVariableValueBytes),
                event.beforeValue,
                issue,
                true) ||
            !requiredString(
                object,
                QStringLiteral("afterValue"),
                fieldStringLimit(
                    currentLimits.maximumStringBytes,
                    MaximumVariableValueBytes),
                event.afterValue,
                issue,
                true))
        {
            return false;
        }
        if (event.variableName.isEmpty() ||
            !canonicalValue(
                event.valueType,
                event.beforeValue) ||
            !canonicalValue(
                event.valueType,
                event.afterValue))
        {
            assignIssue(
                issue,
                StoryGraphRuntimeTraceIssueCode::InvalidFieldValue,
                QStringLiteral(
                    "variable.change name, valueType, or canonical "
                    "before/after value is invalid."));
            return false;
        }
        return true;

    case StoryGraphRuntimeTraceEventType::TraceDropped:
        return requiredUnsignedInteger(
            object,
            QStringLiteral("droppedSourceLineCount"),
            1,
            MaximumExactJsonInteger,
            event.droppedSourceLineCount,
            issue);
    }
    return false;
}

bool StoryGraphRuntimeTraceTailer::
validateAndAppendEvent(
    const StoryGraphRuntimeTraceEvent& event,
    qint64 byteOffset,
    quint64 lineNumber)
{
    const auto reject =
        [this, byteOffset, lineNumber](
            StoryGraphRuntimeTraceIssueCode code,
            const QString& message)
        {
            invalidate(
                code,
                message,
                byteOffset,
                lineNumber);
            return false;
        };

    if (event.sequence != expectedSequence)
    {
        return reject(
            StoryGraphRuntimeTraceIssueCode::SequenceOutOfOrder,
            QStringLiteral(
                "Expected runtime trace sequence %1 but received %2.")
                .arg(expectedSequence)
                .arg(event.sequence));
    }
    if (sessionFinishSeen)
    {
        return reject(
            StoryGraphRuntimeTraceIssueCode::
                EventAfterSessionFinish,
            QStringLiteral(
                "No runtime trace event may follow session.finish."));
    }
    if (event.type ==
        StoryGraphRuntimeTraceEventType::SessionStart)
    {
        if (sessionStartSeen ||
            event.sequence != 1)
        {
            return reject(
                StoryGraphRuntimeTraceIssueCode::
                    InvalidExecutionLifecycle,
                QStringLiteral(
                    "session.start must be the unique first event."));
        }
        sessionStartSeen = true;
    }
    else if (!sessionStartSeen)
    {
        return reject(
            StoryGraphRuntimeTraceIssueCode::MissingSessionStart,
            QStringLiteral(
                "Runtime trace events require a preceding session.start."));
    }

    switch (event.type)
    {
    case StoryGraphRuntimeTraceEventType::ScriptStart:
        if (!event.executionId)
        {
            return reject(
                StoryGraphRuntimeTraceIssueCode::
                    InvalidExecutionLifecycle,
                QStringLiteral(
                    "script.start requires executionId."));
        }
        if (executions.contains(*event.executionId))
        {
            return reject(
                StoryGraphRuntimeTraceIssueCode::
                    InvalidExecutionLifecycle,
                QStringLiteral(
                    "script.start reused executionId %1.")
                    .arg(*event.executionId));
        }
        if (executions.size() >=
            currentLimits.maximumExecutionCount)
        {
            return reject(
                StoryGraphRuntimeTraceIssueCode::
                    ExecutionLimitReached,
                QStringLiteral(
                    "Runtime trace exceeded the %1-execution limit.")
                    .arg(
                        currentLimits.
                            maximumExecutionCount));
        }
        if (event.parentExecutionId &&
            !executions.contains(
                *event.parentExecutionId))
        {
            return reject(
                StoryGraphRuntimeTraceIssueCode::
                    InvalidExecutionLifecycle,
                QStringLiteral(
                    "script.start parentExecutionId %1 is unknown.")
                    .arg(*event.parentExecutionId));
        }
        executions.insert(
            *event.executionId,
            ExecutionState());
        break;

    case StoryGraphRuntimeTraceEventType::ScriptFinish:
    {
        if (!event.executionId)
        {
            return reject(
                StoryGraphRuntimeTraceIssueCode::
                    InvalidExecutionLifecycle,
                QStringLiteral(
                    "script.finish requires executionId."));
        }
        auto execution =
            executions.find(*event.executionId);
        if (execution == executions.end() ||
            !execution->active)
        {
            return reject(
                StoryGraphRuntimeTraceIssueCode::
                    InvalidExecutionLifecycle,
                QStringLiteral(
                    "script.finish executionId %1 is unknown or "
                    "already finished.")
                    .arg(*event.executionId));
        }
        execution->active = false;
        break;
    }

    case StoryGraphRuntimeTraceEventType::SourceLine:
    case StoryGraphRuntimeTraceEventType::ApiCall:
    {
        if (!event.executionId)
        {
            return reject(
                StoryGraphRuntimeTraceIssueCode::
                    InvalidExecutionLifecycle,
                QStringLiteral(
                    "The runtime event requires executionId."));
        }
        const auto execution =
            executions.constFind(*event.executionId);
        if (execution == executions.cend() ||
            !execution->active)
        {
            return reject(
                StoryGraphRuntimeTraceIssueCode::
                    InvalidExecutionLifecycle,
                QStringLiteral(
                    "Runtime event references inactive or unknown "
                    "executionId %1.")
                    .arg(*event.executionId));
        }
        break;
    }

    case StoryGraphRuntimeTraceEventType::MapChange:
    case StoryGraphRuntimeTraceEventType::VariableChange:
        if (event.executionId)
        {
            const auto execution =
                executions.constFind(*event.executionId);
            if (execution == executions.cend() ||
                !execution->active)
            {
                return reject(
                    StoryGraphRuntimeTraceIssueCode::
                        InvalidExecutionLifecycle,
                    QStringLiteral(
                        "Runtime event references inactive or unknown "
                        "executionId %1.")
                        .arg(*event.executionId));
            }
        }
        break;

    case StoryGraphRuntimeTraceEventType::SessionFinish:
        if (std::any_of(
                executions.cbegin(),
                executions.cend(),
                [](const ExecutionState& execution)
                {
                    return execution.active;
                }))
        {
            return reject(
                StoryGraphRuntimeTraceIssueCode::
                    InvalidExecutionLifecycle,
                QStringLiteral(
                    "session.finish requires every script execution "
                    "to be finished."));
        }
        sessionFinishSeen = true;
        break;

    case StoryGraphRuntimeTraceEventType::SessionStart:
    case StoryGraphRuntimeTraceEventType::TraceDropped:
        break;
    }

    if (acceptedEventCount >=
        static_cast<quint64>(
            currentLimits.maximumEventCount))
    {
        return reject(
            StoryGraphRuntimeTraceIssueCode::EventLimitReached,
            QStringLiteral(
                "Runtime trace exceeded the %1-event limit.")
                .arg(currentLimits.maximumEventCount));
    }
    parsedEvents.append(event);
    ++acceptedEventCount;
    ++expectedSequence;
    return true;
}

void StoryGraphRuntimeTraceTailer::
updateConsumedDigest()
{
    consumedDigest = readOffset == 0
        ? QByteArray()
        : consumedHasher->result();
}

void StoryGraphRuntimeTraceTailer::
updateTerminalState()
{
    if (currentState ==
            StoryGraphRuntimeTraceStreamState::Invalid ||
        currentState ==
            StoryGraphRuntimeTraceStreamState::Complete ||
        currentState ==
            StoryGraphRuntimeTraceStreamState::Incomplete ||
        !producerIsTerminal)
    {
        return;
    }
    if (!sourceWasEstablished)
    {
        currentState =
            StoryGraphRuntimeTraceStreamState::
                WaitingForFile;
        return;
    }
    if (hasUnreadBytes())
    {
        currentState =
            StoryGraphRuntimeTraceStreamState::Live;
        return;
    }
    if (!pendingLine.isEmpty() ||
        discardingLongLine)
    {
        reportIssue(
            StoryGraphRuntimeTraceIssueCode::IncompleteLine,
            QStringLiteral(
                "Runtime trace ended with an incomplete JSONL line."),
            pendingLineOffset,
            currentLineNumber);
        currentState =
            StoryGraphRuntimeTraceStreamState::Incomplete;
        return;
    }
    if (!sessionStartSeen)
    {
        reportIssue(
            StoryGraphRuntimeTraceIssueCode::MissingSessionStart,
            QStringLiteral(
                "Terminal runtime trace has no session.start event."));
        currentState = producerWasForced
            ? StoryGraphRuntimeTraceStreamState::Incomplete
            : StoryGraphRuntimeTraceStreamState::Invalid;
        return;
    }
    if (!sessionFinishSeen)
    {
        if (!producerWasForced)
        {
            reportIssue(
                StoryGraphRuntimeTraceIssueCode::MissingSessionFinish,
                QStringLiteral(
                    "Runtime trace ended without session.finish."));
        }
        currentState =
            StoryGraphRuntimeTraceStreamState::Incomplete;
        return;
    }
    currentState =
        StoryGraphRuntimeTraceStreamState::Complete;
}

void StoryGraphRuntimeTraceTailer::reportIssue(
    StoryGraphRuntimeTraceIssueCode code,
    const QString& message,
    qint64 byteOffset,
    quint64 lineNumber)
{
    StoryGraphRuntimeTraceIssue issue;
    issue.code = code;
    issue.message = message;
    issue.byteOffset = byteOffset;
    issue.lineNumber = lineNumber;
    if (streamIssues.size() <
        currentLimits.maximumIssueCount)
    {
        streamIssues.append(std::move(issue));
        return;
    }
    if (issueLimitReported)
        return;
    issueLimitReported = true;
    StoryGraphRuntimeTraceIssue limitIssue;
    limitIssue.code =
        StoryGraphRuntimeTraceIssueCode::IssueLimitReached;
    limitIssue.message =
        QStringLiteral(
            "Additional runtime trace issues were omitted after "
            "the %1-entry limit.")
            .arg(currentLimits.maximumIssueCount);
    limitIssue.byteOffset = byteOffset;
    limitIssue.lineNumber = lineNumber;
    if (!streamIssues.isEmpty())
        streamIssues.last() = std::move(limitIssue);
}

void StoryGraphRuntimeTraceTailer::invalidate(
    StoryGraphRuntimeTraceIssueCode code,
    const QString& message,
    qint64 byteOffset,
    quint64 lineNumber)
{
    if (currentState ==
        StoryGraphRuntimeTraceStreamState::Invalid)
    {
        return;
    }
    reportIssue(
        code,
        message,
        byteOffset,
        lineNumber);
    parsedEvents.clear();
    parsedEvents.squeeze();
    executions.clear();
    closeSource();
    currentState =
        StoryGraphRuntimeTraceStreamState::Invalid;
}

void StoryGraphRuntimeTraceTailer::
resetStreamState()
{
    fileIdentity = FileIdentity();
    pendingLine.clear();
    consumedDigest.clear();
    consumedHasher->reset();
    trustedRevision = FileRevision();
    trustedObservedFileBytes = -1;
    resetPrefixVerification();
    terminalVerifiedOffset = -1;
    readOffset = 0;
    pendingLineOffset = 0;
    latestObservedFileBytes = 0;
    currentLineNumber = 1;
    expectedSequence = 1;
    discardedSequence = 0;
    acceptedEventCount = 0;
    prefixVerificationBytesRead = 0;
    discardingLongLine = false;
    sourceWasEstablished = false;
    producerIsTerminal = false;
    producerWasForced = false;
    sessionStartSeen = false;
    sessionFinishSeen = false;
}

void StoryGraphRuntimeTraceTailer::closeSource()
{
    if (file)
        file->close();
    file.reset();
}

bool StoryGraphRuntimeTraceTailer::strictUtf8(
    const QByteArray& bytes)
{
    for (qsizetype offset = 0;
         offset < bytes.size();)
    {
        const unsigned char first =
            static_cast<unsigned char>(
                bytes.at(offset));
        if (first < 0x80U)
        {
            ++offset;
            continue;
        }

        qsizetype length = 0;
        quint32 codePoint = 0;
        quint32 minimumCodePoint = 0;
        if ((first & 0xE0U) == 0xC0U)
        {
            length = 2;
            codePoint = first & 0x1FU;
            minimumCodePoint = 0x80U;
        }
        else if ((first & 0xF0U) == 0xE0U)
        {
            length = 3;
            codePoint = first & 0x0FU;
            minimumCodePoint = 0x800U;
        }
        else if ((first & 0xF8U) == 0xF0U)
        {
            length = 4;
            codePoint = first & 0x07U;
            minimumCodePoint = 0x10000U;
        }
        else
        {
            return false;
        }
        if (offset + length > bytes.size())
            return false;

        for (qsizetype index = 1;
             index < length;
             ++index)
        {
            const unsigned char continuation =
                static_cast<unsigned char>(
                    bytes.at(offset + index));
            if ((continuation & 0xC0U) != 0x80U)
                return false;
            codePoint =
                (codePoint << 6U) |
                static_cast<quint32>(
                    continuation & 0x3FU);
        }
        if (codePoint < minimumCodePoint ||
            codePoint > 0x10FFFFU ||
            (codePoint >= 0xD800U &&
             codePoint <= 0xDFFFU))
        {
            return false;
        }
        offset += length;
    }
    return true;
}

bool StoryGraphRuntimeTraceTailer::
exactLowercaseUuid(const QString& value)
{
    if (value.size() != 36)
        return false;
    for (int index = 0; index < value.size(); ++index)
    {
        if (index == 8 ||
            index == 13 ||
            index == 18 ||
            index == 23)
        {
            if (value.at(index) != QLatin1Char('-'))
                return false;
            continue;
        }
        const QChar character = value.at(index);
        if (!((character >= QLatin1Char('0') &&
               character <= QLatin1Char('9')) ||
              (character >= QLatin1Char('a') &&
               character <= QLatin1Char('f'))))
        {
            return false;
        }
    }
    return true;
}

bool StoryGraphRuntimeTraceTailer::
exactLowercaseSha256(
    const QString& value,
    QByteArray& bytes)
{
    if (value.size() != 64)
        return false;
    for (const QChar character : value)
    {
        if (!((character >= QLatin1Char('0') &&
               character <= QLatin1Char('9')) ||
              (character >= QLatin1Char('a') &&
               character <= QLatin1Char('f'))))
        {
            return false;
        }
    }
    bytes = QByteArray::fromHex(value.toLatin1());
    return bytes.size() == 32;
}

StoryGraphRuntimeTraceTailer::FileIdentity
StoryGraphRuntimeTraceTailer::identityForFile(
    QFile& file)
{
    FileIdentity identity;
    const qintptr descriptor = file.handle();
    if (descriptor < 0)
        return identity;

#ifdef Q_OS_WIN
    const intptr_t nativeHandleValue =
        _get_osfhandle(
            static_cast<int>(descriptor));
    if (nativeHandleValue == -1)
        return identity;
    BY_HANDLE_FILE_INFORMATION information = {};
    if (!GetFileInformationByHandle(
            reinterpret_cast<HANDLE>(
                nativeHandleValue),
            &information))
    {
        return identity;
    }
    identity.first =
        (static_cast<quint64>(
             information.dwVolumeSerialNumber)
         << 32U) |
        information.nFileIndexHigh;
    identity.second =
        information.nFileIndexLow;
#else
    struct stat information = {};
    if (fstat(
            static_cast<int>(descriptor),
            &information) != 0 ||
        !S_ISREG(information.st_mode))
    {
        return identity;
    }
    identity.first =
        static_cast<quint64>(
            information.st_dev);
    identity.second =
        static_cast<quint64>(
            information.st_ino);
#endif
    identity.valid = true;
    return identity;
}

StoryGraphRuntimeTraceTailer::FileRevision
StoryGraphRuntimeTraceTailer::revisionForFile(
    QFile& file)
{
    FileRevision revision;
    const qintptr descriptor = file.handle();
    if (descriptor < 0)
        return revision;

#ifdef Q_OS_WIN
    const intptr_t nativeHandleValue =
        _get_osfhandle(
            static_cast<int>(descriptor));
    if (nativeHandleValue == -1)
        return revision;
    BY_HANDLE_FILE_INFORMATION information = {};
    if (!GetFileInformationByHandle(
            reinterpret_cast<HANDLE>(
                nativeHandleValue),
            &information))
    {
        return revision;
    }
    revision.first =
        (static_cast<quint64>(
             information.ftLastWriteTime.
                 dwHighDateTime)
         << 32U) |
        information.ftLastWriteTime.
            dwLowDateTime;
    revision.second =
        (static_cast<quint64>(
             information.nFileSizeHigh)
         << 32U) |
        information.nFileSizeLow;
#else
    struct stat information = {};
    if (fstat(
            static_cast<int>(descriptor),
            &information) != 0 ||
        !S_ISREG(information.st_mode))
    {
        return revision;
    }
#if defined(__APPLE__)
    revision.first =
        static_cast<quint64>(
            information.st_mtimespec.tv_sec);
    revision.second =
        static_cast<quint64>(
            information.st_mtimespec.tv_nsec);
#else
    revision.first =
        static_cast<quint64>(
            information.st_mtim.tv_sec);
    revision.second =
        static_cast<quint64>(
            information.st_mtim.tv_nsec);
#endif
#endif
    revision.valid = true;
    return revision;
}

QString storyGraphRuntimeTraceEventTypeToString(
    StoryGraphRuntimeTraceEventType type)
{
    switch (type)
    {
    case StoryGraphRuntimeTraceEventType::SessionStart:
        return QStringLiteral("session.start");
    case StoryGraphRuntimeTraceEventType::SessionFinish:
        return QStringLiteral("session.finish");
    case StoryGraphRuntimeTraceEventType::ScriptStart:
        return QStringLiteral("script.start");
    case StoryGraphRuntimeTraceEventType::ScriptFinish:
        return QStringLiteral("script.finish");
    case StoryGraphRuntimeTraceEventType::SourceLine:
        return QStringLiteral("source.line");
    case StoryGraphRuntimeTraceEventType::ApiCall:
        return QStringLiteral("api.call");
    case StoryGraphRuntimeTraceEventType::MapChange:
        return QStringLiteral("map.change");
    case StoryGraphRuntimeTraceEventType::VariableChange:
        return QStringLiteral("variable.change");
    case StoryGraphRuntimeTraceEventType::TraceDropped:
        return QStringLiteral("trace.dropped");
    }
    return QStringLiteral("invalid");
}

QString storyGraphRuntimeTraceSourceLayerToString(
    StoryGraphRuntimeTraceSourceLayer sourceLayer)
{
    switch (sourceLayer)
    {
    case StoryGraphRuntimeTraceSourceLayer::Formal:
        return QStringLiteral("formal");
    case StoryGraphRuntimeTraceSourceLayer::Overlay:
        return QStringLiteral("overlay");
    }
    return QStringLiteral("invalid");
}

QString storyGraphRuntimeTraceIssueCodeToString(
    StoryGraphRuntimeTraceIssueCode code)
{
    switch (code)
    {
    case StoryGraphRuntimeTraceIssueCode::InvalidLimits:
        return QStringLiteral("invalid-limits");
    case StoryGraphRuntimeTraceIssueCode::SourceNotConfigured:
        return QStringLiteral("source-not-configured");
    case StoryGraphRuntimeTraceIssueCode::FileNotFound:
        return QStringLiteral("file-not-found");
    case StoryGraphRuntimeTraceIssueCode::FileNotRegular:
        return QStringLiteral("file-not-regular");
    case StoryGraphRuntimeTraceIssueCode::FileOpenFailed:
        return QStringLiteral("file-open-failed");
    case StoryGraphRuntimeTraceIssueCode::FileIdentityUnavailable:
        return QStringLiteral("file-identity-unavailable");
    case StoryGraphRuntimeTraceIssueCode::FileReplaced:
        return QStringLiteral("file-replaced");
    case StoryGraphRuntimeTraceIssueCode::FileTruncated:
        return QStringLiteral("file-truncated");
    case StoryGraphRuntimeTraceIssueCode::FileContentChanged:
        return QStringLiteral("file-content-changed");
    case StoryGraphRuntimeTraceIssueCode::FileTooLarge:
        return QStringLiteral("file-too-large");
    case StoryGraphRuntimeTraceIssueCode::FileReadFailed:
        return QStringLiteral("file-read-failed");
    case StoryGraphRuntimeTraceIssueCode::LineTooLong:
        return QStringLiteral("line-too-long");
    case StoryGraphRuntimeTraceIssueCode::InvalidUtf8:
        return QStringLiteral("invalid-utf8");
    case StoryGraphRuntimeTraceIssueCode::InvalidJson:
        return QStringLiteral("invalid-json");
    case StoryGraphRuntimeTraceIssueCode::InvalidRootType:
        return QStringLiteral("invalid-root-type");
    case StoryGraphRuntimeTraceIssueCode::UnsupportedSchemaVersion:
        return QStringLiteral("unsupported-schema-version");
    case StoryGraphRuntimeTraceIssueCode::SessionMismatch:
        return QStringLiteral("session-mismatch");
    case StoryGraphRuntimeTraceIssueCode::MissingField:
        return QStringLiteral("missing-field");
    case StoryGraphRuntimeTraceIssueCode::InvalidFieldType:
        return QStringLiteral("invalid-field-type");
    case StoryGraphRuntimeTraceIssueCode::InvalidFieldValue:
        return QStringLiteral("invalid-field-value");
    case StoryGraphRuntimeTraceIssueCode::SequenceOutOfOrder:
        return QStringLiteral("sequence-out-of-order");
    case StoryGraphRuntimeTraceIssueCode::EventAfterSessionFinish:
        return QStringLiteral("event-after-session-finish");
    case StoryGraphRuntimeTraceIssueCode::InvalidExecutionLifecycle:
        return QStringLiteral("invalid-execution-lifecycle");
    case StoryGraphRuntimeTraceIssueCode::EventLimitReached:
        return QStringLiteral("event-limit-reached");
    case StoryGraphRuntimeTraceIssueCode::ExecutionLimitReached:
        return QStringLiteral("execution-limit-reached");
    case StoryGraphRuntimeTraceIssueCode::IssueLimitReached:
        return QStringLiteral("issue-limit-reached");
    case StoryGraphRuntimeTraceIssueCode::IncompleteLine:
        return QStringLiteral("incomplete-line");
    case StoryGraphRuntimeTraceIssueCode::MissingSessionStart:
        return QStringLiteral("missing-session-start");
    case StoryGraphRuntimeTraceIssueCode::MissingSessionFinish:
        return QStringLiteral("missing-session-finish");
    }
    return QStringLiteral("invalid");
}
