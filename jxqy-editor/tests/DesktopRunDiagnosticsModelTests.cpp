#include "../ui/DesktopRunDiagnosticsModel.h"

#include <QCoreApplication>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryDir>

#include <iostream>

namespace
{
const QByteArray SessionId =
    QByteArrayLiteral("11111111-2222-4333-8444-555555555555");

bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) &&
        file.write(bytes) == bytes.size() &&
        file.commit();
}

bool appendFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Append) &&
        file.write(bytes) == bytes.size() &&
        file.flush();
}

bool truncateFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
        file.write(bytes) == bytes.size() &&
        file.flush();
}

bool overwriteBytes(
    const QString& path,
    qint64 offset,
    const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::ReadWrite) &&
        file.seek(offset) &&
        file.write(bytes) == bytes.size() &&
        file.flush();
}

QByteArray eventLine(
    quint64 sequence,
    const QByteArray& severity = QByteArrayLiteral("info"),
    const QByteArray& code = QByteArrayLiteral("editor_run.test"),
    const QByteArray& message = QByteArrayLiteral("message"),
    const QByteArray& extraFields = {})
{
    return QByteArrayLiteral(
        "{\"schemaVersion\":1,\"sessionId\":\"") +
        SessionId +
        QByteArrayLiteral("\",\"sequence\":") +
        QByteArray::number(sequence) +
        QByteArrayLiteral(",\"severity\":\"") +
        severity +
        QByteArrayLiteral("\",\"code\":\"") +
        code +
        QByteArrayLiteral("\",\"message\":\"") +
        message +
        QByteArrayLiteral("\"") +
        extraFields +
        QByteArrayLiteral("}\n");
}

bool hasIssue(
    const DesktopRunDiagnosticsModel& model,
    DesktopRunDiagnosticsModel::IssueCode code)
{
    for (const DesktopRunDiagnosticsModel::Issue& issue :
         model.issues())
    {
        if (issue.code == code)
            return true;
    }
    return false;
}

bool runUnicodeAndIncrementalTests(const QString& root)
{
    bool ok = true;
    const QString path = root + QStringLiteral("/unicode.jsonl");
    const QByteArray first =
        eventLine(
            1,
            QByteArrayLiteral("warning"),
            QByteArrayLiteral("editor_run.script.runtime_failed"),
            QString::fromUtf8("入口脚本失败").toUtf8(),
            QString::fromUtf8(
                ",\"file\":\"script/章节 一/入口.txt\","
                "\"line\":27,\"column\":9,\"target\":\"序章\"")
                .toUtf8());
    const qsizetype split = first.size() / 2;
    ok = check(writeFile(path, first.left(split)),
        "write first diagnostics half") && ok;

    DesktopRunDiagnosticsModel model;
    model.setSource(path, QString::fromLatin1(SessionId));
    ok = check(model.refresh() &&
                   model.rowCount() == 0 &&
                   model.pendingByteCount() == split,
        "partial JSONL line is retained without a record") && ok;
    ok = check(appendFile(path, first.mid(split)) &&
                   model.refresh() &&
                   model.rowCount() == 1,
        "completed appended line becomes one record") && ok;

    const DesktopRunDiagnosticsModel::Record* record =
        model.recordAt(0);
    const DesktopRunDiagnosticsModel::SourceLocation source =
        model.sourceLocationAt(0);
    ok = check(record &&
                   record->sequence == 1 &&
                   record->severity ==
                       DesktopRunDiagnosticsModel::Severity::Warning &&
                   record->message ==
                       QString::fromUtf8("入口脚本失败") &&
                   record->target == QString::fromUtf8("序章") &&
                   source.file ==
                       QString::fromUtf8("script/章节 一/入口.txt") &&
                   source.line == 27 &&
                   source.column == 9,
        "Unicode and source location fields remain exact") && ok;
    ok = check(
        model.data(
            model.index(0, DesktopRunDiagnosticsModel::LocationColumn))
            .toString() ==
            QString::fromUtf8("script/章节 一/入口.txt:27:9") &&
        model.data(
            model.index(0, 0),
            DesktopRunDiagnosticsModel::SourceFileRole)
            .toString() ==
            QString::fromUtf8("script/章节 一/入口.txt"),
        "display and navigation roles expose the source location") && ok;
    return ok;
}

bool runMalformedInputTests(const QString& root)
{
    bool ok = true;
    const QString path = root + QStringLiteral("/malformed.jsonl");
    QByteArray bytes = QByteArrayLiteral("{not json}\n");
    bytes += QByteArrayLiteral("\xff\n");
    bytes += QByteArrayLiteral(
        "{\"schemaVersion\":\"1\",\"sessionId\":\"") +
        SessionId +
        QByteArrayLiteral(
            "\",\"sequence\":1,\"severity\":\"info\","
            "\"code\":\"bad.schema\",\"message\":\"bad\"}\n");
    bytes += QByteArrayLiteral(
        "{\"schemaVersion\":1,\"sessionId\":\"different\","
        "\"sequence\":1,\"severity\":\"info\","
        "\"code\":\"wrong.session\",\"message\":\"bad\"}\n");
    bytes += eventLine(1);
    ok = check(writeFile(path, bytes),
        "write malformed diagnostics fixtures") && ok;

    DesktopRunDiagnosticsModel model;
    model.setSource(path, QString::fromLatin1(SessionId));
    model.refresh();
    ok = check(model.rowCount() == 1 &&
                   hasIssue(
                       model,
                       DesktopRunDiagnosticsModel::IssueCode::InvalidJson) &&
                   hasIssue(
                       model,
                       DesktopRunDiagnosticsModel::IssueCode::InvalidUtf8) &&
                   hasIssue(
                       model,
                       DesktopRunDiagnosticsModel::IssueCode::
                           InvalidFieldType) &&
                   hasIssue(
                       model,
                       DesktopRunDiagnosticsModel::IssueCode::
                           SessionMismatch),
        "bad JSON, UTF-8, type, and session errors are observable") && ok;
    for (const DesktopRunDiagnosticsModel::Issue& issue :
         model.issues())
    {
        ok = check(issue.byteOffset >= 0 && issue.lineNumber > 0,
            "line parsing issues retain byte and line positions") && ok;
    }
    return ok;
}

bool runSchemaAndFieldBoundaryTests(const QString& root)
{
    bool ok = true;
    const QString path = root + QStringLiteral("/field-boundaries.jsonl");
    QByteArray bytes =
        QByteArrayLiteral(
            "{\"schemaVersion\":2,\"sessionId\":\"") +
        SessionId +
        QByteArrayLiteral(
            "\",\"sequence\":1,\"severity\":\"info\","
            "\"code\":\"future\",\"message\":\"future\"}\n");
    bytes +=
        QByteArrayLiteral(
            "{\"schemaVersion\":1,\"sessionId\":\"") +
        SessionId +
        QByteArrayLiteral(
            "\",\"sequence\":1,\"severity\":\"info\","
            "\"message\":\"missing code\"}\n");
    bytes +=
        QByteArrayLiteral(
            "{\"schemaVersion\":1,\"sessionId\":\"") +
        SessionId +
        QByteArrayLiteral(
            "\",\"sequence\":1.5,\"severity\":\"info\","
            "\"code\":\"fractional\",\"message\":\"bad\"}\n");
    bytes +=
        QByteArrayLiteral(
            "{\"schemaVersion\":1,\"sessionId\":\"") +
        SessionId +
        QByteArrayLiteral(
            "\",\"sequence\":1,\"severity\":\"error\","
            "\"code\":\"line.large\",\"message\":\"bad\","
            "\"file\":\"script/a.txt\",\"line\":4294967296}\n");
    bytes += eventLine(1);
    ok = check(writeFile(path, bytes),
        "write schema and field boundary fixtures") && ok;

    DesktopRunDiagnosticsModel model;
    model.setSource(path, QString::fromLatin1(SessionId));
    model.refresh();
    ok = check(
        model.rowCount() == 1 &&
        hasIssue(
            model,
            DesktopRunDiagnosticsModel::IssueCode::
                UnsupportedSchemaVersion) &&
        hasIssue(
            model,
            DesktopRunDiagnosticsModel::IssueCode::MissingField) &&
        hasIssue(
            model,
            DesktopRunDiagnosticsModel::IssueCode::InvalidFieldValue),
        "schema, missing field, fractional sequence, and 32-bit "
        "location bounds are observable") && ok;
    return ok;
}

bool runSessionSwitchTest(const QString& root)
{
    bool ok = true;
    const QString path = root + QStringLiteral("/session-switch.jsonl");
    ok = check(writeFile(path, eventLine(1)),
        "write session switch fixture") && ok;
    DesktopRunDiagnosticsModel model;
    model.setSource(path, QString::fromLatin1(SessionId));
    ok = check(model.refresh() && model.rowCount() == 1,
        "load initial session before switch") && ok;

    const QString newSession =
        QStringLiteral("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
    model.setSource(path, newSession);
    ok = check(model.rowCount() == 0 &&
                   model.issues().isEmpty() &&
                   model.nextExpectedSequence() == 1 &&
                   !model.refresh() &&
                   hasIssue(
                       model,
                       DesktopRunDiagnosticsModel::IssueCode::
                           SessionMismatch),
        "explicit session switch safely resets and rejects prior events") &&
        ok;
    return ok;
}

bool runSequenceTests(const QString& root)
{
    bool ok = true;
    const QString path = root + QStringLiteral("/sequence.jsonl");
    ok = check(writeFile(path, eventLine(1)),
        "write sequence start") && ok;

    DesktopRunDiagnosticsModel model;
    model.setSource(path, QString::fromLatin1(SessionId));
    ok = check(model.refresh() &&
                   model.rowCount() == 1 &&
                   model.nextExpectedSequence() == 2,
        "sequence starts exactly at one") && ok;
    ok = check(appendFile(path, eventLine(3)) &&
                   !model.refresh() &&
                   model.rowCount() == 1 &&
                   model.nextExpectedSequence() == 2,
        "sequence gap is rejected without advancing") && ok;
    ok = check(appendFile(path, eventLine(2)) &&
                   model.refresh() &&
                   model.rowCount() == 2 &&
                   model.nextExpectedSequence() == 3,
        "expected sequence can continue after a rejected gap") && ok;
    ok = check(appendFile(path, eventLine(2)) &&
                   !model.refresh() &&
                   model.rowCount() == 2 &&
                   hasIssue(
                       model,
                       DesktopRunDiagnosticsModel::IssueCode::
                           SequenceOutOfOrder),
        "duplicate sequence is rejected and observable") && ok;
    return ok;
}

bool runTruncationAndReplacementTests(const QString& root)
{
    bool ok = true;
    const QString truncatePath =
        root + QStringLiteral("/truncate.jsonl");
    ok = check(
        writeFile(truncatePath, eventLine(1) + eventLine(2)),
        "write truncation fixture") && ok;

    DesktopRunDiagnosticsModel truncatedModel;
    truncatedModel.setSource(
        truncatePath,
        QString::fromLatin1(SessionId));
    ok = check(truncatedModel.refresh() &&
                   truncatedModel.rowCount() == 2,
        "load pre-truncation records") && ok;
    ok = check(truncateFile(
                   truncatePath,
                   eventLine(
                       1,
                       QByteArrayLiteral("error"),
                       QByteArrayLiteral("editor_run.after_truncate"))) &&
                   !truncatedModel.refresh() &&
                   truncatedModel.rowCount() == 1 &&
                   truncatedModel.recordAt(0)->code ==
                       QStringLiteral("editor_run.after_truncate") &&
                   (hasIssue(
                        truncatedModel,
                        DesktopRunDiagnosticsModel::IssueCode::
                            FileTruncated) ||
                    hasIssue(
                        truncatedModel,
                        DesktopRunDiagnosticsModel::IssueCode::
                            FileContentChanged) ||
                    hasIssue(
                        truncatedModel,
                        DesktopRunDiagnosticsModel::IssueCode::
                            FileReplaced)),
        "truncation resets old records before rereading") && ok;

    const QString replacePath =
        root + QStringLiteral("/replace.jsonl");
    ok = check(writeFile(replacePath, eventLine(1)),
        "write replacement fixture") && ok;
    DesktopRunDiagnosticsModel replacedModel;
    replacedModel.setSource(
        replacePath,
        QString::fromLatin1(SessionId));
    ok = check(replacedModel.refresh() &&
                   replacedModel.rowCount() == 1,
        "load pre-replacement record") && ok;
    ok = check(writeFile(
                   replacePath,
                   eventLine(
                       1,
                       QByteArrayLiteral("warning"),
                       QByteArrayLiteral("editor_run.replaced"))) &&
                   !replacedModel.refresh() &&
                   replacedModel.rowCount() == 1 &&
                   replacedModel.recordAt(0)->code ==
                       QStringLiteral("editor_run.replaced") &&
                   hasIssue(
                       replacedModel,
                       DesktopRunDiagnosticsModel::IssueCode::
                           FileReplaced),
        "same-path file replacement is detected and reset") && ok;
    return ok;
}

bool runMiddleRewriteTest(const QString& root)
{
    bool ok = true;
    const QString path = root + QStringLiteral("/middle-rewrite.jsonl");
    const QByteArray first =
        eventLine(
            1,
            QByteArrayLiteral("info"),
            QByteArrayLiteral("editor_run.first"));
    const QByteArray middle =
        eventLine(
            2,
            QByteArrayLiteral("warning"),
            QByteArrayLiteral("editor_run.middle_a"));
    const QByteArray last =
        eventLine(
            3,
            QByteArrayLiteral("error"),
            QByteArrayLiteral("editor_run.last"));
    const QByteArray original = first + middle + last;
    const qint64 replacementOffset =
        original.indexOf(QByteArrayLiteral("middle_a")) +
        QByteArrayLiteral("middle_").size();
    ok = check(replacementOffset > first.size() &&
                   replacementOffset <
                       original.size() - last.size() &&
                   writeFile(path, original),
        "write middle rewrite fixture") && ok;

    DesktopRunDiagnosticsModel model;
    model.setSource(path, QString::fromLatin1(SessionId));
    ok = check(model.refresh() && model.rowCount() == 3,
        "load records before same-size middle rewrite") && ok;
    ok = check(
        overwriteBytes(path, replacementOffset, QByteArrayLiteral("b")) &&
        !model.refresh() &&
        model.rowCount() == 3 &&
        model.recordAt(1) &&
        model.recordAt(1)->code ==
            QStringLiteral("editor_run.middle_b") &&
        hasIssue(
            model,
            DesktopRunDiagnosticsModel::IssueCode::FileContentChanged),
        "full consumed-prefix digest detects same-identity same-size "
        "middle rewrite") && ok;
    return ok;
}

bool runLimitAndTailTests(const QString& root)
{
    bool ok = true;
    const QString path = root + QStringLiteral("/limit.jsonl");
    QByteArray bytes(257, 'x');
    bytes += '\n';
    bytes += eventLine(1);
    ok = check(writeFile(path, bytes),
        "write oversized line fixture") && ok;

    DesktopRunDiagnosticsModel model;
    model.setMaximumLineBytes(256);
    model.setSource(path, QString::fromLatin1(SessionId));
    model.refresh();
    ok = check(model.rowCount() == 1 &&
                   hasIssue(
                       model,
                       DesktopRunDiagnosticsModel::IssueCode::LineTooLong),
        "oversized line is discarded with an observable issue") && ok;

    const QString tailPath = root + QStringLiteral("/tail.jsonl");
    const QByteArray complete = eventLine(1);
    ok = check(writeFile(tailPath, complete.left(complete.size() - 1)),
        "write incomplete final line") && ok;
    DesktopRunDiagnosticsModel tailModel;
    tailModel.setSource(tailPath, QString::fromLatin1(SessionId));
    ok = check(tailModel.refresh() &&
                   tailModel.rowCount() == 0 &&
                   !tailModel.finalizeStream() &&
                   hasIssue(
                       tailModel,
                       DesktopRunDiagnosticsModel::IssueCode::
                           IncompleteLine),
        "producer termination exposes an incomplete retained tail") && ok;
    return ok;
}

bool runBoundedGrowthTests(const QString& root)
{
    bool ok = true;
    const QString fileLimitPath =
        root + QStringLiteral("/file-limit.jsonl");
    const QByteArray first = eventLine(1);
    const QByteArray second = eventLine(2);
    ok = check(writeFile(fileLimitPath, first),
        "write file limit fixture") && ok;

    DesktopRunDiagnosticsModel fileLimitModel;
    ok = check(
        fileLimitModel.maximumFileBytes() == 64 * 1024 * 1024 &&
        fileLimitModel.maximumRecordCount() == 100000,
        "diagnostics growth defaults are finite") && ok;
    fileLimitModel.setMaximumFileBytes(first.size());
    fileLimitModel.setSource(
        fileLimitPath,
        QString::fromLatin1(SessionId));
    ok = check(fileLimitModel.refresh() &&
                   fileLimitModel.rowCount() == 1,
        "file at its exact byte limit is accepted") && ok;
    ok = check(appendFile(fileLimitPath, second) &&
                   !fileLimitModel.refresh() &&
                   fileLimitModel.rowCount() == 1 &&
                   hasIssue(
                       fileLimitModel,
                       DesktopRunDiagnosticsModel::IssueCode::
                           FileTooLarge),
        "file byte limit stops additional records with evidence") && ok;
    const qsizetype fileLimitIssueCount =
        fileLimitModel.issues().size();
    ok = check(appendFile(fileLimitPath, eventLine(3)) &&
                   !fileLimitModel.refresh() &&
                   fileLimitModel.rowCount() == 1 &&
                   fileLimitModel.issues().size() ==
                       fileLimitIssueCount,
        "continued oversized-file growth remains bounded") && ok;

    const QString recordLimitPath =
        root + QStringLiteral("/record-limit.jsonl");
    ok = check(
        writeFile(
            recordLimitPath,
            eventLine(1) +
                eventLine(2) +
                eventLine(3) +
                eventLine(4)),
        "write record limit fixture") && ok;
    DesktopRunDiagnosticsModel recordLimitModel;
    recordLimitModel.setMaximumRecordCount(2);
    recordLimitModel.setSource(
        recordLimitPath,
        QString::fromLatin1(SessionId));
    ok = check(!recordLimitModel.refresh() &&
                   recordLimitModel.rowCount() == 2 &&
                   hasIssue(
                       recordLimitModel,
                       DesktopRunDiagnosticsModel::IssueCode::
                           RecordLimitReached),
        "record count limit stops model row growth with evidence") && ok;
    const qsizetype recordLimitIssueCount =
        recordLimitModel.issues().size();
    ok = check(
        appendFile(recordLimitPath, eventLine(5) + eventLine(6)) &&
        recordLimitModel.refresh() &&
        recordLimitModel.rowCount() == 2 &&
        recordLimitModel.issues().size() == recordLimitIssueCount,
        "records after the first count-limit evidence stay omitted") && ok;

    const QString issueLimitPath =
        root + QStringLiteral("/issue-limit.jsonl");
    QByteArray invalidLines(65, 'x');
    invalidLines += '\n';
    for (int index = 0; index < 10; ++index)
        invalidLines += QByteArrayLiteral("{\n");
    ok = check(writeFile(issueLimitPath, invalidLines),
        "write issue limit fixture") && ok;
    DesktopRunDiagnosticsModel issueLimitModel;
    issueLimitModel.setMaximumLineBytes(64);
    issueLimitModel.setMaximumIssueCount(3);
    issueLimitModel.setSource(
        issueLimitPath,
        QString::fromLatin1(SessionId));
    ok = check(!issueLimitModel.refresh() &&
                   issueLimitModel.issues().size() == 3 &&
                   hasIssue(
                       issueLimitModel,
                       DesktopRunDiagnosticsModel::IssueCode::
                           LineTooLong) &&
                   hasIssue(
                       issueLimitModel,
                       DesktopRunDiagnosticsModel::IssueCode::
                           IssueLimitReached),
        "bounded issue storage retains initial limit evidence") && ok;
    const qsizetype boundedIssueCount =
        issueLimitModel.issues().size();
    ok = check(
        appendFile(
            issueLimitPath,
            QByteArrayLiteral("{\n{\n{\n{\n")) &&
        !issueLimitModel.refresh() &&
        issueLimitModel.issues().size() == boundedIssueCount,
        "additional invalid lines cannot grow the issue collection") && ok;
    return ok;
}

bool runMissingThenCreatedTest(const QString& root)
{
    bool ok = true;
    const QString path = root + QStringLiteral("/created-later.jsonl");
    DesktopRunDiagnosticsModel model;
    model.setSource(path, QString::fromLatin1(SessionId));
    ok = check(!model.refresh() &&
                   model.rowCount() == 0 &&
                   hasIssue(
                       model,
                       DesktopRunDiagnosticsModel::IssueCode::FileNotFound),
        "missing diagnostics file is observable") && ok;
    const qsizetype missingIssueCount = model.issues().size();
    ok = check(!model.refresh() &&
                   model.issues().size() == missingIssueCount,
        "polling a still-missing file does not duplicate the issue") && ok;
    model.clearIssues();
    ok = check(!model.refresh() &&
                   model.issues().size() == 1 &&
                   hasIssue(
                       model,
                       DesktopRunDiagnosticsModel::IssueCode::FileNotFound),
        "clearing issues permits a still-current missing-file report") && ok;
    ok = check(writeFile(path, eventLine(1)) &&
                   model.refresh() &&
                   model.rowCount() == 1,
        "model reads from byte zero when the missing file is created") && ok;
    const qsizetype issuesBeforeDeletion = model.issues().size();
    ok = check(QFile::remove(path) &&
                   !model.refresh() &&
                   model.rowCount() == 0 &&
                   model.nextExpectedSequence() == 1 &&
                   model.pendingByteCount() == 0 &&
                   model.issues().size() == issuesBeforeDeletion + 1 &&
                   model.lastIssue().code ==
                       DesktopRunDiagnosticsModel::IssueCode::FileNotFound,
        "deleting a previously read file clears its records and stream") &&
        ok;
    return ok;
}
}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporaryDirectory;
    bool ok = check(temporaryDirectory.isValid(),
        "create diagnostics model temporary directory");
    if (temporaryDirectory.isValid())
    {
        ok = runUnicodeAndIncrementalTests(
                 temporaryDirectory.path()) && ok;
        ok = runMalformedInputTests(
                 temporaryDirectory.path()) && ok;
        ok = runSchemaAndFieldBoundaryTests(
                 temporaryDirectory.path()) && ok;
        ok = runSessionSwitchTest(
                 temporaryDirectory.path()) && ok;
        ok = runSequenceTests(
                 temporaryDirectory.path()) && ok;
        ok = runTruncationAndReplacementTests(
                 temporaryDirectory.path()) && ok;
        ok = runMiddleRewriteTest(
                 temporaryDirectory.path()) && ok;
        ok = runLimitAndTailTests(
                 temporaryDirectory.path()) && ok;
        ok = runBoundedGrowthTests(
                 temporaryDirectory.path()) && ok;
        ok = runMissingThenCreatedTest(
                 temporaryDirectory.path()) && ok;
    }
    if (ok)
        std::cout << "Desktop run diagnostics model tests passed\n";
    return ok ? 0 : 1;
}
