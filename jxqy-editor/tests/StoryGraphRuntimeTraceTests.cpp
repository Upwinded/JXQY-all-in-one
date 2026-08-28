#include "../core/StoryGraphRuntimeTrace.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>

namespace
{
const QString SessionId =
    QStringLiteral(
        "11111111-2222-4333-8444-555555555555");
const QString OtherSessionId =
    QStringLiteral(
        "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
const QString ContentDigest =
    QString(64, QLatin1Char('a'));

bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

QJsonObject record(
    quint64 sequence,
    const QString& eventType)
{
    QJsonObject object;
    object.insert(
        QStringLiteral("schemaVersion"),
        1);
    object.insert(
        QStringLiteral("sessionId"),
        SessionId);
    object.insert(
        QStringLiteral("sequence"),
        static_cast<double>(sequence));
    object.insert(
        QStringLiteral("eventType"),
        eventType);
    object.insert(
        QStringLiteral("elapsedMicroseconds"),
        static_cast<double>(sequence * 10));
    return object;
}

QByteArray jsonLine(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(
               QJsonDocument::Compact) +
        '\n';
}

bool writeBytes(
    const QString& path,
    const QByteArray& bytes)
{
    QFile file(path);
    return file.open(
               QIODevice::WriteOnly |
               QIODevice::Truncate) &&
        file.write(bytes) == bytes.size() &&
        file.flush();
}

bool appendBytes(
    const QString& path,
    const QByteArray& bytes)
{
    QFile file(path);
    return file.open(
               QIODevice::WriteOnly |
               QIODevice::Append) &&
        file.write(bytes) == bytes.size() &&
        file.flush();
}

bool rewriteAndRestoreModificationTime(
    const QString& path,
    qint64 offset,
    const QByteArray& replacement)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite))
        return false;
    const QDateTime originalModificationTime =
        file.fileTime(
            QFileDevice::FileModificationTime);
    const bool rewritten =
        originalModificationTime.isValid() &&
        file.seek(offset) &&
        file.write(replacement) ==
            replacement.size() &&
        file.flush() &&
        file.setFileTime(
            originalModificationTime,
            QFileDevice::FileModificationTime);
    file.close();
    return rewritten;
}

bool drainLiveTailer(
    StoryGraphRuntimeTraceTailer& tailer)
{
    bool succeeded = tailer.refresh();
    int passCount = 0;
    while (tailer.hasUnreadBytes() &&
           tailer.state() !=
               StoryGraphRuntimeTraceStreamState::Invalid &&
           passCount < 10000)
    {
        succeeded &= tailer.refresh();
        ++passCount;
    }
    return succeeded && passCount < 10000;
}

bool settleTerminalTailer(
    StoryGraphRuntimeTraceTailer& tailer,
    bool forcedTermination)
{
    bool succeeded =
        tailer.finalize(forcedTermination);
    int passCount = 0;
    while (tailer.state() ==
               StoryGraphRuntimeTraceStreamState::Live &&
           passCount < 10000)
    {
        succeeded &= tailer.refresh();
        ++passCount;
    }
    return succeeded && passCount < 10000;
}

bool hasIssue(
    const StoryGraphRuntimeTraceTailer& tailer,
    StoryGraphRuntimeTraceIssueCode code)
{
    for (const StoryGraphRuntimeTraceIssue& issue :
         tailer.issues())
    {
        if (issue.code == code)
            return true;
    }
    return false;
}

QJsonObject scriptStart(
    quint64 sequence,
    quint64 executionId,
    const QString& virtualPath =
        QStringLiteral("script/事件.txt"))
{
    QJsonObject object =
        record(
            sequence,
            QStringLiteral("script.start"));
    object.insert(
        QStringLiteral("executionId"),
        static_cast<double>(executionId));
    object.insert(
        QStringLiteral("virtualPath"),
        virtualPath);
    object.insert(
        QStringLiteral("contentSha256"),
        ContentDigest);
    object.insert(
        QStringLiteral("rootKind"),
        QStringLiteral("active"));
    object.insert(
        QStringLiteral("rootOrdinal"),
        0);
    object.insert(
        QStringLiteral("resourcePackId"),
        QStringLiteral("剑侠二"));
    object.insert(
        QStringLiteral("sourceLayer"),
        QStringLiteral("overlay"));
    return object;
}

QJsonObject scriptFinish(
    quint64 sequence,
    quint64 executionId)
{
    QJsonObject object =
        record(
            sequence,
            QStringLiteral("script.finish"));
    object.insert(
        QStringLiteral("executionId"),
        static_cast<double>(executionId));
    object.insert(
        QStringLiteral("status"),
        QStringLiteral("completed"));
    return object;
}

bool testIncrementalCompleteTrace()
{
    QTemporaryDir directory;
    const QString path =
        directory.filePath(
            QStringLiteral("runtime-trace.jsonl"));

    StoryGraphRuntimeTraceLimits limits;
    limits.maximumRefreshBytes = 37;
    StoryGraphRuntimeTraceTailer tailer;
    bool passed = true;
    passed &= check(
        tailer.setLimits(limits),
        "small bounded refresh limit is accepted");
    tailer.bindSource(path, SessionId);
    passed &= check(
        tailer.refresh() &&
        tailer.state() ==
            StoryGraphRuntimeTraceStreamState::
                WaitingForFile &&
        tailer.issues().isEmpty(),
        "an initially absent live trace waits without an issue");

    QByteArray bytes;
    bytes += jsonLine(
        record(
            1,
            QStringLiteral("session.start")));
    bytes += jsonLine(scriptStart(2, 1));

    QJsonObject sourceLine =
        record(
            3,
            QStringLiteral("source.line"));
    sourceLine.insert(
        QStringLiteral("executionId"),
        1);
    sourceLine.insert(
        QStringLiteral("line"),
        7);
    bytes += jsonLine(sourceLine);

    QJsonObject apiCall =
        record(
            4,
            QStringLiteral("api.call"));
    apiCall.insert(
        QStringLiteral("executionId"),
        1);
    apiCall.insert(
        QStringLiteral("apiName"),
        QStringLiteral("Äpi"));
    bytes += jsonLine(apiCall);

    QJsonObject mapChange =
        record(
            5,
            QStringLiteral("map.change"));
    mapChange.insert(
        QStringLiteral("target"),
        QStringLiteral("map/龙门镇"));
    bytes += jsonLine(mapChange);

    QJsonObject variableChange =
        record(
            6,
            QStringLiteral("variable.change"));
    variableChange.insert(
        QStringLiteral("variableName"),
        QStringLiteral("声望"));
    variableChange.insert(
        QStringLiteral("valueType"),
        QStringLiteral("real"));
    variableChange.insert(
        QStringLiteral("beforeValue"),
        QStringLiteral("-0.5"));
    variableChange.insert(
        QStringLiteral("afterValue"),
        QStringLiteral("1e-3"));
    bytes += jsonLine(variableChange);

    QJsonObject dropped =
        record(
            7,
            QStringLiteral("trace.dropped"));
    dropped.insert(
        QStringLiteral("droppedSourceLineCount"),
        9);
    bytes += jsonLine(dropped);
    bytes += jsonLine(scriptFinish(8, 1));

    QJsonObject sessionFinish =
        record(
            9,
            QStringLiteral("session.finish"));
    sessionFinish.insert(
        QStringLiteral("status"),
        QStringLiteral("completed"));
    bytes += jsonLine(sessionFinish);

    passed &= check(
        writeBytes(path, bytes),
        "complete trace fixture is written");
    passed &= check(
        drainLiveTailer(tailer),
        "bounded refresh passes consume the complete trace");
    passed &= check(
        tailer.eventCount() == 9 &&
        tailer.totalAcceptedEventCount() == 9 &&
        tailer.nextExpectedSequence() == 10,
        "all incremental events are accepted in sequence");
    passed &= check(
        tailer.events().at(1).virtualPath ==
            QStringLiteral("script/事件.txt") &&
        tailer.events().at(1).resourcePackId ==
            std::optional<QString>(
                QStringLiteral("剑侠二")) &&
        tailer.events().at(1).sourceLayer ==
            StoryGraphRuntimeTraceSourceLayer::Overlay,
        "Unicode script identity and source layer survive parsing");
    passed &= check(
        !tailer.events().at(4).executionId &&
        !tailer.events().at(5).executionId &&
        tailer.events().at(3).apiName ==
            QStringLiteral("Äpi") &&
        tailer.events().at(5).valueType ==
            QStringLiteral("real"),
        "producer-compatible API spelling, optional execution IDs, and "
        "canonical real values are accepted");
    passed &= check(
        settleTerminalTailer(
            tailer,
            false) &&
        tailer.state() ==
            StoryGraphRuntimeTraceStreamState::Complete &&
        tailer.issues().isEmpty(),
        "session.finish settles a normal terminal trace as complete");
    return passed;
}

bool invalidFixtureHasIssue(
    const QByteArray& bytes,
    StoryGraphRuntimeTraceIssueCode code)
{
    QTemporaryDir directory;
    const QString path =
        directory.filePath(
            QStringLiteral("invalid.jsonl"));
    if (!writeBytes(path, bytes))
        return false;
    StoryGraphRuntimeTraceTailer tailer;
    tailer.bindSource(path, SessionId);
    tailer.refresh();
    return tailer.state() ==
            StoryGraphRuntimeTraceStreamState::Invalid &&
        tailer.events().isEmpty() &&
        hasIssue(tailer, code);
}

bool testFailClosedProtocolAndLifecycle()
{
    bool passed = true;

    QJsonObject future =
        record(
            1,
            QStringLiteral("session.start"));
    future.insert(
        QStringLiteral("schemaVersion"),
        2);
    passed &= check(
        invalidFixtureHasIssue(
            jsonLine(future),
            StoryGraphRuntimeTraceIssueCode::
                UnsupportedSchemaVersion),
        "a future schema version fails closed");

    QJsonObject mismatch =
        record(
            1,
            QStringLiteral("session.start"));
    mismatch.insert(
        QStringLiteral("sessionId"),
        OtherSessionId);
    passed &= check(
        invalidFixtureHasIssue(
            jsonLine(mismatch),
            StoryGraphRuntimeTraceIssueCode::
                SessionMismatch),
        "a mismatched session ID fails closed");

    QByteArray sequenceGap =
        jsonLine(
            record(
                1,
                QStringLiteral("session.start")));
    sequenceGap += jsonLine(scriptStart(3, 1));
    passed &= check(
        invalidFixtureHasIssue(
            sequenceGap,
            StoryGraphRuntimeTraceIssueCode::
                SequenceOutOfOrder),
        "a sequence gap fails closed");

    QJsonObject unknownExecution =
        record(
            2,
            QStringLiteral("source.line"));
    unknownExecution.insert(
        QStringLiteral("executionId"),
        99);
    unknownExecution.insert(
        QStringLiteral("line"),
        1);
    QByteArray invalidLifecycle =
        jsonLine(
            record(
                1,
                QStringLiteral("session.start"))) +
        jsonLine(unknownExecution);
    passed &= check(
        invalidFixtureHasIssue(
            invalidLifecycle,
            StoryGraphRuntimeTraceIssueCode::
                InvalidExecutionLifecycle),
        "an unknown execution reference fails closed");

    QJsonObject invalidReal =
        record(
            2,
            QStringLiteral("variable.change"));
    invalidReal.insert(
        QStringLiteral("variableName"),
        QStringLiteral("score"));
    invalidReal.insert(
        QStringLiteral("valueType"),
        QStringLiteral("real"));
    invalidReal.insert(
        QStringLiteral("beforeValue"),
        QStringLiteral("1E3"));
    invalidReal.insert(
        QStringLiteral("afterValue"),
        QStringLiteral("-0"));
    passed &= check(
        invalidFixtureHasIssue(
            jsonLine(
                record(
                    1,
                    QStringLiteral("session.start"))) +
                jsonLine(invalidReal),
            StoryGraphRuntimeTraceIssueCode::
                InvalidFieldValue),
        "non-canonical real values fail closed");

    QJsonObject unsafeMap =
        record(
            2,
            QStringLiteral("map.change"));
    unsafeMap.insert(
        QStringLiteral("target"),
        QStringLiteral("../escape"));
    passed &= check(
        invalidFixtureHasIssue(
            jsonLine(
                record(
                    1,
                    QStringLiteral("session.start"))) +
                jsonLine(unsafeMap),
            StoryGraphRuntimeTraceIssueCode::
                InvalidFieldValue),
        "map.change requires a strict virtual target");

    unsafeMap.insert(
        QStringLiteral("target"),
        QStringLiteral("map/CON.txt"));
    passed &= check(
        invalidFixtureHasIssue(
            jsonLine(
                record(
                    1,
                    QStringLiteral("session.start"))) +
                jsonLine(unsafeMap),
            StoryGraphRuntimeTraceIssueCode::
                InvalidFieldValue),
        "consumer strict paths reject producer-reserved Windows names");

    QJsonObject nullResourcePack =
        scriptStart(2, 1);
    nullResourcePack.insert(
        QStringLiteral("resourcePackId"),
        QStringLiteral("pack") +
            QChar::Null +
            QStringLiteral("id"));
    passed &= check(
        invalidFixtureHasIssue(
            jsonLine(
                record(
                    1,
                    QStringLiteral("session.start"))) +
                jsonLine(nullResourcePack),
            StoryGraphRuntimeTraceIssueCode::
                InvalidFieldValue),
        "consumer resource-pack identifiers reject embedded nulls");

    QJsonObject uppercaseApi =
        record(
            3,
            QStringLiteral("api.call"));
    uppercaseApi.insert(
        QStringLiteral("executionId"),
        1);
    uppercaseApi.insert(
        QStringLiteral("apiName"),
        QStringLiteral("Talk"));
    passed &= check(
        invalidFixtureHasIssue(
            jsonLine(
                record(
                    1,
                    QStringLiteral("session.start"))) +
                jsonLine(scriptStart(2, 1)) +
                jsonLine(uppercaseApi),
            StoryGraphRuntimeTraceIssueCode::
                InvalidFieldValue),
        "consumer API names reject producer-forbidden ASCII uppercase");
    return passed;
}

bool testIdentityPrefixAndTruncation()
{
    bool passed = true;

    {
        QTemporaryDir directory;
        const QString path =
            directory.filePath(
                QStringLiteral("rewrite.jsonl"));
        QByteArray firstLine =
            jsonLine(
                record(
                    1,
                    QStringLiteral("session.start")));
        passed &= check(
            writeBytes(path, firstLine),
            "prefix fixture is written");
        StoryGraphRuntimeTraceTailer tailer;
        tailer.bindSource(path, SessionId);
        passed &= check(
            drainLiveTailer(tailer) &&
            tailer.eventCount() == 1,
            "prefix fixture is initially accepted");
        QFile file(path);
        const bool rewritten =
            file.open(QIODevice::ReadWrite) &&
            file.seek(0) &&
            file.write("[", 1) == 1 &&
            file.flush();
        file.close();
        passed &= check(
            rewritten &&
            !tailer.refresh() &&
            tailer.state() ==
                StoryGraphRuntimeTraceStreamState::Invalid &&
            hasIssue(
                tailer,
                StoryGraphRuntimeTraceIssueCode::
                    FileContentChanged) &&
            tailer.events().isEmpty(),
            "rewriting a consumed prefix invalidates and clears events");
    }

    {
        QTemporaryDir directory;
        const QString path =
            directory.filePath(
                QStringLiteral("truncate.jsonl"));
        const QByteArray firstLine =
            jsonLine(
                record(
                    1,
                    QStringLiteral("session.start")));
        passed &= check(
            writeBytes(path, firstLine),
            "truncate fixture is written");
        StoryGraphRuntimeTraceTailer tailer;
        tailer.bindSource(path, SessionId);
        drainLiveTailer(tailer);
        passed &= check(
            writeBytes(path, QByteArray()) &&
            !tailer.refresh() &&
            hasIssue(
                tailer,
                StoryGraphRuntimeTraceIssueCode::
                    FileTruncated),
            "truncating an established trace fails closed");
    }

    {
        QTemporaryDir directory;
        const QString path =
            directory.filePath(
                QStringLiteral("replace.jsonl"));
        const QString replacementPath =
            directory.filePath(
                QStringLiteral("replacement.jsonl"));
        const QByteArray firstLine =
            jsonLine(
                record(
                    1,
                    QStringLiteral("session.start")));
        passed &= check(
            writeBytes(path, firstLine) &&
            writeBytes(
                replacementPath,
                firstLine),
            "replacement fixtures are written");
        StoryGraphRuntimeTraceTailer tailer;
        tailer.bindSource(path, SessionId);
        drainLiveTailer(tailer);
        const bool replaced =
            QFile::remove(path) &&
            QFile::rename(
                replacementPath,
                path);
        passed &= check(
            replaced &&
            !tailer.refresh() &&
            hasIssue(
                tailer,
                StoryGraphRuntimeTraceIssueCode::
                    FileReplaced),
            "replacing the established file identity fails closed");
    }
    return passed;
}

bool testStableTerminalPrefixProof()
{
    bool passed = true;

    {
        QTemporaryDir directory;
        const QString path =
            directory.filePath(
                QStringLiteral(
                    "restored-modification-time.jsonl"));
        const QByteArray bytes =
            jsonLine(
                record(
                    1,
                    QStringLiteral("session.start")));
        StoryGraphRuntimeTraceTailer tailer;
        tailer.bindSource(path, SessionId);
        passed &= check(
            writeBytes(path, bytes) &&
            drainLiveTailer(tailer) &&
            rewriteAndRestoreModificationTime(
                path,
                0,
                QByteArray("[")),
            "same-size prefix rewrite restores the prior modification time");
        (void)tailer.refresh();
        if (tailer.state() !=
            StoryGraphRuntimeTraceStreamState::Invalid)
        {
            (void)tailer.finalize(true);
            int passCount = 0;
            while (tailer.state() ==
                       StoryGraphRuntimeTraceStreamState::Live &&
                   passCount < 10000)
            {
                (void)tailer.refresh();
                ++passCount;
            }
        }
        passed &= check(
            tailer.state() ==
                StoryGraphRuntimeTraceStreamState::Invalid &&
            hasIssue(
                tailer,
                StoryGraphRuntimeTraceIssueCode::
                    FileContentChanged) &&
            tailer.events().isEmpty(),
            "terminal prefix proof detects a same-identity rewrite "
            "even when size and modification time are restored");
    }

    {
        QTemporaryDir directory;
        const QString path =
            directory.filePath(
                QStringLiteral(
                    "between-proof-passes.jsonl"));
        QByteArray bytes =
            jsonLine(
                record(
                    1,
                    QStringLiteral("session.start"))) +
            jsonLine(scriptStart(2, 1));
        quint64 sequence = 3;
        for (int index = 0; index < 80; ++index)
        {
            QJsonObject sourceLine =
                record(
                    sequence++,
                    QStringLiteral("source.line"));
            sourceLine.insert(
                QStringLiteral("executionId"),
                1);
            sourceLine.insert(
                QStringLiteral("line"),
                index + 1);
            bytes += jsonLine(sourceLine);
        }
        bytes += jsonLine(
            scriptFinish(sequence++, 1));
        QJsonObject finish =
            record(
                sequence,
                QStringLiteral("session.finish"));
        finish.insert(
            QStringLiteral("status"),
            QStringLiteral("completed"));
        bytes += jsonLine(finish);

        StoryGraphRuntimeTraceLimits limits;
        limits.maximumRefreshBytes = 128;
        StoryGraphRuntimeTraceTailer tailer;
        passed &= check(
            tailer.setLimits(limits),
            "multi-pass proof accepts a small refresh budget");
        tailer.bindSource(path, SessionId);
        passed &= check(
            writeBytes(path, bytes) &&
            drainLiveTailer(tailer) &&
            tailer.prefixVerificationBytesReadForTests() == 0 &&
            tailer.finalize(false) &&
            tailer.state() ==
                StoryGraphRuntimeTraceStreamState::Live &&
            tailer.prefixVerificationBytesReadForTests() == 128 &&
            rewriteAndRestoreModificationTime(
                path,
                0,
                QByteArray("[")),
            "prefix is rewritten after the first bounded proof chunk");
        int passCount = 0;
        while (tailer.state() ==
                   StoryGraphRuntimeTraceStreamState::Live &&
               passCount < 10000)
        {
            (void)tailer.refresh();
            ++passCount;
        }
        passed &= check(
            passCount < 10000 &&
            tailer.state() ==
                StoryGraphRuntimeTraceStreamState::Invalid &&
            hasIssue(
                tailer,
                StoryGraphRuntimeTraceIssueCode::
                    FileContentChanged),
            "the second stable proof pass detects a rewrite of an "
            "already-verified first-pass chunk");
    }
    return passed;
}

bool testLiveAppendVerificationWork()
{
    QTemporaryDir directory;
    const QString path =
        directory.filePath(
            QStringLiteral(
                "live-append-work.jsonl"));
    StoryGraphRuntimeTraceLimits limits;
    limits.maximumRefreshBytes = 4096;
    StoryGraphRuntimeTraceTailer tailer;
    bool passed = true;
    passed &= check(
        tailer.setLimits(limits),
        "live append work fixture accepts its refresh budget");
    tailer.bindSource(path, SessionId);
    passed &= check(
        writeBytes(
            path,
            jsonLine(
                record(
                    1,
                    QStringLiteral("session.start")))) &&
        drainLiveTailer(tailer) &&
        appendBytes(
            path,
            jsonLine(scriptStart(2, 1))) &&
        drainLiveTailer(tailer),
        "live append fixture starts one script execution");

    quint64 sequence = 3;
    for (int index = 0; index < 20; ++index)
    {
        QJsonObject sourceLine =
            record(
                sequence++,
                QStringLiteral("source.line"));
        sourceLine.insert(
            QStringLiteral("executionId"),
            1);
        sourceLine.insert(
            QStringLiteral("line"),
            index + 1);
        passed &= appendBytes(
            path,
            jsonLine(sourceLine));
        passed &= drainLiveTailer(tailer);
    }
    QJsonObject sessionFinish =
        record(
            sequence + 1,
            QStringLiteral("session.finish"));
    sessionFinish.insert(
        QStringLiteral("status"),
        QStringLiteral("completed"));
    passed &= check(
        appendBytes(
            path,
            jsonLine(
                scriptFinish(sequence, 1)) +
                jsonLine(sessionFinish)) &&
        drainLiveTailer(tailer) &&
        tailer.prefixVerificationBytesReadForTests() == 0,
        "growing live appends do not rehash the consumed prefix");

    const qint64 finalBytes =
        tailer.consumedByteCount();
    passed &= check(
        tailer.finalize(false) &&
        drainLiveTailer(tailer) &&
        tailer.state() ==
            StoryGraphRuntimeTraceStreamState::Complete &&
        tailer.prefixVerificationBytesReadForTests() ==
            static_cast<quint64>(finalBytes) * 2U,
        "terminal settlement performs exactly two bounded stable "
        "full-prefix proof passes");
    return passed;
}

bool testIncompleteAndDiscardBounds()
{
    bool passed = true;

    {
        QTemporaryDir directory;
        const QString path =
            directory.filePath(
                QStringLiteral("partial.jsonl"));
        QByteArray partial =
            QJsonDocument(
                record(
                    1,
                    QStringLiteral("session.start"))).
                toJson(QJsonDocument::Compact);
        partial.chop(1);
        passed &= check(
            writeBytes(path, partial),
            "partial-line fixture is written");
        StoryGraphRuntimeTraceTailer tailer;
        tailer.bindSource(path, SessionId);
        passed &= check(
            !settleTerminalTailer(
                tailer,
                true) &&
            tailer.state() ==
                StoryGraphRuntimeTraceStreamState::Incomplete &&
            hasIssue(
                tailer,
                StoryGraphRuntimeTraceIssueCode::
                    IncompleteLine),
            "forced termination with a partial line is incomplete");
    }

    {
        QTemporaryDir directory;
        const QString path =
            directory.filePath(
                QStringLiteral("missing-finish.jsonl"));
        passed &= check(
            writeBytes(
                path,
                jsonLine(
                    record(
                        1,
                        QStringLiteral("session.start")))),
            "missing-finish fixture is written");
        StoryGraphRuntimeTraceTailer tailer;
        tailer.bindSource(path, SessionId);
        passed &= check(
            !settleTerminalTailer(
                tailer,
                false) &&
            tailer.state() ==
                StoryGraphRuntimeTraceStreamState::Incomplete &&
            hasIssue(
                tailer,
                StoryGraphRuntimeTraceIssueCode::
                    MissingSessionFinish),
            "normal termination without session.finish is incomplete");
    }

    {
        QTemporaryDir directory;
        const QString path =
            directory.filePath(
                QStringLiteral(
                    "forced-settled.jsonl"));
        const QByteArray initialBytes =
            jsonLine(
                record(
                    1,
                    QStringLiteral(
                        "session.start"))) +
            jsonLine(scriptStart(2, 1));
        StoryGraphRuntimeTraceTailer tailer;
        tailer.bindSource(path, SessionId);
        passed &= check(
            writeBytes(path, initialBytes) &&
            settleTerminalTailer(
                tailer,
                true) &&
            tailer.state() ==
                StoryGraphRuntimeTraceStreamState::
                    Incomplete &&
            tailer.eventCount() == 2 &&
            tailer.nextExpectedSequence() == 3,
            "forced termination settles a valid prefix as Incomplete");

        QJsonObject lateSessionFinish =
            record(
                4,
                QStringLiteral("session.finish"));
        lateSessionFinish.insert(
            QStringLiteral("status"),
            QStringLiteral("completed"));
        passed &= check(
            appendBytes(
                path,
                jsonLine(scriptFinish(3, 1)) +
                    jsonLine(lateSessionFinish)) &&
            tailer.refresh() &&
            tailer.refresh() &&
            tailer.state() ==
                StoryGraphRuntimeTraceStreamState::
                    Incomplete &&
            tailer.eventCount() == 2 &&
            tailer.nextExpectedSequence() == 3 &&
            !tailer.hasSessionFinish(),
            "settled Incomplete ignores late bytes and stale refresh ticks");
    }

    {
        QTemporaryDir directory;
        const QString path =
            directory.filePath(
                QStringLiteral("bounded.jsonl"));
        StoryGraphRuntimeTraceLimits limits;
        limits.maximumEventCount = 3;
        StoryGraphRuntimeTraceTailer tailer;
        passed &= check(
            tailer.setLimits(limits),
            "three-event lifetime limit is accepted");
        tailer.bindSource(path, SessionId);
        passed &= check(
            writeBytes(
                path,
                jsonLine(
                    record(
                        1,
                        QStringLiteral("session.start"))) +
                    jsonLine(scriptStart(2, 1))) &&
            drainLiveTailer(tailer),
            "first bounded event batch is accepted");
        passed &= check(
            tailer.discardEventsRetainingCursor() == 2 &&
            tailer.discardedThroughSequence() == 2 &&
            tailer.eventCount() == 0 &&
            tailer.events().capacity() == 0 &&
            tailer.nextExpectedSequence() == 3 &&
            tailer.totalAcceptedEventCount() == 2,
            "discard releases payloads but preserves cursor and lifetime count");

        QJsonObject sourceLine =
            record(
                3,
                QStringLiteral("source.line"));
        sourceLine.insert(
            QStringLiteral("executionId"),
            1);
        sourceLine.insert(
            QStringLiteral("line"),
            2);
        QJsonObject apiCall =
            record(
                4,
                QStringLiteral("api.call"));
        apiCall.insert(
            QStringLiteral("executionId"),
            1);
        apiCall.insert(
            QStringLiteral("apiName"),
            QStringLiteral("talk"));
        passed &= check(
            appendBytes(
                path,
                jsonLine(sourceLine)) &&
            drainLiveTailer(tailer) &&
            tailer.eventCount() == 1 &&
            tailer.events().constFirst().sequence == 3,
            "the first post-discard event continues at N+1");
        passed &= check(
            appendBytes(
                path,
                jsonLine(apiCall)) &&
            !drainLiveTailer(tailer) &&
            tailer.state() ==
                StoryGraphRuntimeTraceStreamState::Invalid &&
            hasIssue(
                tailer,
                StoryGraphRuntimeTraceIssueCode::
                    EventLimitReached),
            "discard cannot bypass the lifetime event bound");
        const qsizetype invalidIssueCount =
            tailer.issues().size();
        passed &= check(
            tailer.discardEventsRetainingCursor() == 3 &&
            tailer.discardedThroughSequence() == 3 &&
            tailer.eventCount() == 0 &&
            tailer.events().capacity() == 0 &&
            tailer.nextExpectedSequence() == 4 &&
            tailer.state() ==
                StoryGraphRuntimeTraceStreamState::
                    Invalid &&
            tailer.issues().size() ==
                invalidIssueCount &&
            tailer.discardEventsRetainingCursor() == 3 &&
            tailer.eventCount() == 0 &&
            tailer.nextExpectedSequence() == 4,
            "Invalid streams release published events and repeated clear remains cursor-idempotent");
    }

    passed &= check(
        storyGraphRuntimeTraceIssueCodeToString(
            StoryGraphRuntimeTraceIssueCode::
                SequenceOutOfOrder) ==
            QStringLiteral("sequence-out-of-order"),
        "issue codes have stable string identifiers");
    return passed;
}

bool testTerminalBoundedPrefixVerification()
{
    QTemporaryDir directory;
    const QString path =
        directory.filePath(
            QStringLiteral("terminal-prefix.jsonl"));
    StoryGraphRuntimeTraceLimits limits;
    limits.maximumRefreshBytes = 29;
    StoryGraphRuntimeTraceTailer tailer;
    bool passed = true;
    passed &= check(
        tailer.setLimits(limits),
        "terminal prefix fixture accepts a small refresh budget");
    tailer.bindSource(path, SessionId);

    QJsonObject commonStart =
        scriptStart(
            2,
            1,
            QStringLiteral("script/common/入口.txt"));
    commonStart.insert(
        QStringLiteral("rootKind"),
        QStringLiteral("common"));
    commonStart.remove(
        QStringLiteral("resourcePackId"));
    const QByteArray prefix =
        jsonLine(
            record(
                1,
                QStringLiteral("session.start"))) +
        jsonLine(commonStart) +
        jsonLine(scriptFinish(3, 1));
    passed &= check(
        writeBytes(path, prefix) &&
        drainLiveTailer(tailer) &&
        tailer.eventCount() == 3 &&
        !tailer.events().at(1).resourcePackId,
        "optional resourcePackId and the initial prefix are accepted");

    QJsonObject sessionFinish =
        record(
            4,
            QStringLiteral("session.finish"));
    sessionFinish.insert(
        QStringLiteral("status"),
        QStringLiteral("completed"));
    passed &= check(
        appendBytes(
            path,
            jsonLine(sessionFinish)),
        "terminal event is appended after the trusted prefix");
    passed &= check(
        tailer.finalize(false) &&
        tailer.state() ==
            StoryGraphRuntimeTraceStreamState::Live &&
        tailer.eventCount() == 3 &&
        tailer.hasUnreadBytes(),
        "finalize remains live while bounded prefix verification is incomplete");

    int passCount = 0;
    while (tailer.state() ==
               StoryGraphRuntimeTraceStreamState::Live &&
           passCount < 10000)
    {
        tailer.refresh();
        ++passCount;
    }
    passed &= check(
        passCount > 1 &&
        passCount < 10000 &&
        tailer.state() ==
            StoryGraphRuntimeTraceStreamState::Complete &&
        tailer.eventCount() == 4 &&
        tailer.hasSessionFinish() &&
        !tailer.hasUnreadBytes(),
        "terminal refreshes finish prefix verification, read session.finish, and only then complete");
    return passed;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    bool passed = true;
    passed &= testIncrementalCompleteTrace();
    passed &= testFailClosedProtocolAndLifecycle();
    passed &= testIdentityPrefixAndTruncation();
    passed &= testStableTerminalPrefixProof();
    passed &= testLiveAppendVerificationWork();
    passed &= testIncompleteAndDiscardBounds();
    passed &= testTerminalBoundedPrefixVerification();
    if (passed)
    {
        std::cout
            << "All story graph runtime trace tests passed\n";
    }
    return passed ? 0 : 1;
}
