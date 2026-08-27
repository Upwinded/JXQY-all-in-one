#include "DesktopRunDiagnosticsModel.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace
{
constexpr qint64 ReadChunkBytes = 64 * 1024;
constexpr quint64 MaximumExactJsonInteger = 9007199254740991ULL;

bool jsonUnsignedInteger(
    const QJsonValue& value,
    quint64 minimum,
    quint64 maximum,
    quint64& result)
{
    if (!value.isDouble())
        return false;

    const double number = value.toDouble();
    const double effectiveMaximum = static_cast<double>(
        std::min(maximum, MaximumExactJsonInteger));
    if (!std::isfinite(number) ||
        number < static_cast<double>(minimum) ||
        number > effectiveMaximum ||
        std::floor(number) != number)
    {
        return false;
    }

    result = static_cast<quint64>(number);
    return true;
}

bool requiredString(
    const QJsonObject& object,
    const QString& name,
    QString& value,
    DesktopRunDiagnosticsModel::Issue& issue)
{
    if (!object.contains(name))
    {
        issue.code =
            DesktopRunDiagnosticsModel::IssueCode::MissingField;
        issue.message =
            DesktopRunDiagnosticsModel::tr(
                "Required field \"%1\" is missing.")
                .arg(name);
        return false;
    }
    const QJsonValue candidate = object.value(name);
    if (!candidate.isString())
    {
        issue.code =
            DesktopRunDiagnosticsModel::IssueCode::InvalidFieldType;
        issue.message =
            DesktopRunDiagnosticsModel::tr(
                "Field \"%1\" must be a string.")
                .arg(name);
        return false;
    }
    value = candidate.toString();
    return true;
}

bool optionalString(
    const QJsonObject& object,
    const QString& name,
    QString& value,
    DesktopRunDiagnosticsModel::Issue& issue)
{
    if (!object.contains(name))
        return true;
    const QJsonValue candidate = object.value(name);
    if (!candidate.isString())
    {
        issue.code =
            DesktopRunDiagnosticsModel::IssueCode::InvalidFieldType;
        issue.message =
            DesktopRunDiagnosticsModel::tr(
                "Field \"%1\" must be a string when present.")
                .arg(name);
        return false;
    }
    value = candidate.toString();
    return true;
}

bool optionalPositiveUint32(
    const QJsonObject& object,
    const QString& name,
    quint32& value,
    DesktopRunDiagnosticsModel::Issue& issue)
{
    if (!object.contains(name))
        return true;

    quint64 parsedValue = 0;
    if (!jsonUnsignedInteger(
            object.value(name),
            1,
            std::numeric_limits<quint32>::max(),
            parsedValue))
    {
        issue.code =
            DesktopRunDiagnosticsModel::IssueCode::InvalidFieldValue;
        issue.message =
            DesktopRunDiagnosticsModel::tr(
                "Field \"%1\" must be a positive 32-bit integer.")
                .arg(name);
        return false;
    }
    value = static_cast<quint32>(parsedValue);
    return true;
}
}

DesktopRunDiagnosticsModel::DesktopRunDiagnosticsModel(QObject* parent)
    : QAbstractTableModel(parent),
      m_consumedHasher(std::make_unique<QCryptographicHash>(
          QCryptographicHash::Sha256))
{
}

DesktopRunDiagnosticsModel::~DesktopRunDiagnosticsModel() = default;

int DesktopRunDiagnosticsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_records.size();
}

int DesktopRunDiagnosticsModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant DesktopRunDiagnosticsModel::data(
    const QModelIndex& index,
    int role) const
{
    if (!index.isValid() ||
        index.row() < 0 ||
        index.row() >= m_records.size() ||
        index.column() < 0 ||
        index.column() >= ColumnCount)
    {
        return QVariant();
    }

    const Record& record = m_records.at(index.row());
    if (role == Qt::DisplayRole)
    {
        switch (index.column())
        {
        case SeverityColumn:
            return severityName(record.severity);
        case CodeColumn:
            return record.code;
        case MessageColumn:
            return record.message;
        case LocationColumn:
            return formattedLocation(record.source);
        case TargetColumn:
            return record.target;
        default:
            return QVariant();
        }
    }

    switch (role)
    {
    case SeverityRole:
        return static_cast<int>(record.severity);
    case SeverityNameRole:
        return severityName(record.severity);
    case CodeRole:
        return record.code;
    case MessageRole:
        return record.message;
    case LocationRole:
    {
        QVariantMap location;
        location.insert(QStringLiteral("file"), record.source.file);
        if (record.source.line > 0)
        {
            location.insert(
                QStringLiteral("line"),
                record.source.line);
        }
        if (record.source.column > 0)
        {
            location.insert(
                QStringLiteral("column"),
                record.source.column);
        }
        return location;
    }
    case TargetRole:
        return record.target;
    case SourceFileRole:
        return record.source.file;
    case SourceLineRole:
        return record.source.line;
    case SourceColumnRole:
        return record.source.column;
    case SequenceRole:
        return QVariant::fromValue(record.sequence);
    default:
        return QVariant();
    }
}

QVariant DesktopRunDiagnosticsModel::headerData(
    int section,
    Qt::Orientation orientation,
    int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();

    switch (section)
    {
    case SeverityColumn:
        return tr("Severity");
    case CodeColumn:
        return tr("Code");
    case MessageColumn:
        return tr("Message");
    case LocationColumn:
        return tr("Location");
    case TargetColumn:
        return tr("Target");
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> DesktopRunDiagnosticsModel::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractTableModel::roleNames();
    roles.insert(SeverityRole, QByteArrayLiteral("severity"));
    roles.insert(SeverityNameRole, QByteArrayLiteral("severityName"));
    roles.insert(CodeRole, QByteArrayLiteral("code"));
    roles.insert(MessageRole, QByteArrayLiteral("message"));
    roles.insert(LocationRole, QByteArrayLiteral("location"));
    roles.insert(TargetRole, QByteArrayLiteral("target"));
    roles.insert(SourceFileRole, QByteArrayLiteral("sourceFile"));
    roles.insert(SourceLineRole, QByteArrayLiteral("sourceLine"));
    roles.insert(SourceColumnRole, QByteArrayLiteral("sourceColumn"));
    roles.insert(SequenceRole, QByteArrayLiteral("sequence"));
    return roles;
}

void DesktopRunDiagnosticsModel::setSource(
    const QString& diagnosticsPath,
    const QString& sessionId)
{
    if (m_diagnosticsPath == diagnosticsPath &&
        m_sessionId == sessionId)
    {
        return;
    }

    beginResetModel();
    m_records.clear();
    endResetModel();
    closeSource();
    resetStreamState(false);
    m_diagnosticsPath = diagnosticsPath;
    m_sessionId = sessionId;
    m_issues.clear();
    m_sourceMissingReported = false;
    m_issueLimitReported = false;
    emit issuesChanged();
}

QString DesktopRunDiagnosticsModel::diagnosticsPath() const
{
    return m_diagnosticsPath;
}

QString DesktopRunDiagnosticsModel::sessionId() const
{
    return m_sessionId;
}

bool DesktopRunDiagnosticsModel::refresh()
{
    const quint64 issueSerialBefore = m_reportedIssueSerial;
    if (m_diagnosticsPath.isEmpty() || m_sessionId.isEmpty())
    {
        reportIssue(
            IssueCode::SourceNotConfigured,
            tr("The diagnostics path and session ID must both be set."));
        return false;
    }

    if (!openOrValidateSource())
        return false;
    const qint64 sourceSize = m_file->size();
    if (sourceSize < 0)
    {
        reportIssue(
            IssueCode::FileReadFailed,
            tr("Cannot determine the diagnostics file size."));
        m_file->close();
        m_file.reset();
        return false;
    }
    if (!verifyConsumedDigest())
    {
        m_file->close();
        m_file.reset();
        return false;
    }
    if (sourceSize > m_maximumFileBytes)
    {
        if (!m_fileLimitReported)
        {
            reportIssue(
                IssueCode::FileTooLarge,
                tr("Diagnostics file size %1 exceeds the %2-byte limit.")
                    .arg(sourceSize)
                    .arg(m_maximumFileBytes));
            m_fileLimitReported = true;
        }
        m_file->close();
        m_file.reset();
        return false;
    }
    m_fileLimitReported = false;
    const bool refreshed = readAvailableBytes();
    if (m_file)
        m_file->close();
    m_file.reset();
    return refreshed && m_reportedIssueSerial == issueSerialBefore;
}

bool DesktopRunDiagnosticsModel::finalizeStream()
{
    const quint64 issueSerialBefore = m_reportedIssueSerial;
    refresh();
    if ((!m_pendingLine.isEmpty() || m_discardingLongLine) &&
        !m_incompleteLineReported)
    {
        reportIssue(
            IssueCode::IncompleteLine,
            tr("The diagnostics stream ended with an incomplete line."),
            m_pendingLineOffset,
            m_currentLineNumber);
        m_incompleteLineReported = true;
    }
    return m_reportedIssueSerial == issueSerialBefore;
}

void DesktopRunDiagnosticsModel::clear()
{
    beginResetModel();
    m_records.clear();
    endResetModel();
    closeSource();
    resetStreamState(false);
    m_issues.clear();
    m_sourceMissingReported = false;
    m_issueLimitReported = false;
    emit issuesChanged();
}

void DesktopRunDiagnosticsModel::clearIssues()
{
    if (m_issues.isEmpty())
        return;
    m_issues.clear();
    m_sourceMissingReported = false;
    m_issueLimitReported = false;
    emit issuesChanged();
}

const DesktopRunDiagnosticsModel::Record*
DesktopRunDiagnosticsModel::recordAt(int row) const
{
    if (row < 0 || row >= m_records.size())
        return nullptr;
    return &m_records.at(row);
}

DesktopRunDiagnosticsModel::SourceLocation
DesktopRunDiagnosticsModel::sourceLocationAt(int row) const
{
    const Record* record = recordAt(row);
    return record ? record->source : SourceLocation();
}

const QVector<DesktopRunDiagnosticsModel::Issue>&
DesktopRunDiagnosticsModel::issues() const
{
    return m_issues;
}

DesktopRunDiagnosticsModel::Issue
DesktopRunDiagnosticsModel::lastIssue() const
{
    return m_issues.isEmpty() ? Issue() : m_issues.constLast();
}

qsizetype DesktopRunDiagnosticsModel::pendingByteCount() const
{
    return m_pendingLine.size();
}

quint64 DesktopRunDiagnosticsModel::nextExpectedSequence() const
{
    return m_nextExpectedSequence;
}

qsizetype DesktopRunDiagnosticsModel::maximumLineBytes() const
{
    return m_maximumLineBytes;
}

void DesktopRunDiagnosticsModel::setMaximumLineBytes(
    qsizetype maximumBytes)
{
    if (maximumBytes <= 0 || maximumBytes == m_maximumLineBytes)
        return;

    m_maximumLineBytes = maximumBytes;
    if (m_pendingLine.size() > m_maximumLineBytes &&
        !m_discardingLongLine)
    {
        reportIssue(
            IssueCode::LineTooLong,
            tr("A diagnostics line exceeded the %1-byte limit.")
                .arg(m_maximumLineBytes),
            m_pendingLineOffset,
            m_currentLineNumber);
        m_pendingLine.clear();
        m_discardingLongLine = true;
    }
}

qint64 DesktopRunDiagnosticsModel::maximumFileBytes() const
{
    return m_maximumFileBytes;
}

void DesktopRunDiagnosticsModel::setMaximumFileBytes(
    qint64 maximumBytes)
{
    if (maximumBytes <= 0)
        return;
    m_maximumFileBytes = maximumBytes;
    m_fileLimitReported = false;
}

qsizetype DesktopRunDiagnosticsModel::maximumRecordCount() const
{
    return m_maximumRecordCount;
}

void DesktopRunDiagnosticsModel::setMaximumRecordCount(
    qsizetype maximumCount)
{
    if (maximumCount <= 0 ||
        maximumCount == m_maximumRecordCount)
    {
        return;
    }

    m_maximumRecordCount = maximumCount;
    resetStreamState(true);
}

qsizetype DesktopRunDiagnosticsModel::maximumIssueCount() const
{
    return m_maximumIssueCount;
}

void DesktopRunDiagnosticsModel::setMaximumIssueCount(
    qsizetype maximumCount)
{
    if (maximumCount <= 0 ||
        maximumCount == m_maximumIssueCount)
    {
        return;
    }

    m_maximumIssueCount = maximumCount;
    if (m_issues.size() <= maximumCount)
        return;

    Issue firstLimitIssue;
    bool hasFirstLimitIssue = false;
    for (const Issue& issue : std::as_const(m_issues))
    {
        if (isLimitEvidence(issue.code) &&
            issue.code != IssueCode::IssueLimitReached)
        {
            firstLimitIssue = issue;
            hasFirstLimitIssue = true;
            break;
        }
    }
    m_issues.resize(maximumCount);
    Issue limitIssue;
    limitIssue.code = IssueCode::IssueLimitReached;
    limitIssue.message =
        tr("Additional diagnostics model issues were omitted after "
           "reaching the %1-entry limit.")
            .arg(maximumCount);
    if (hasFirstLimitIssue)
    {
        m_issues[maximumCount - 1] = firstLimitIssue;
        if (maximumCount > 1)
            m_issues[maximumCount - 2] = limitIssue;
    }
    else
    {
        m_issues[maximumCount - 1] = limitIssue;
    }
    m_issueLimitReported = true;
    ++m_reportedIssueSerial;
    emit issuesChanged();
}

bool DesktopRunDiagnosticsModel::openOrValidateSource()
{
    QFileInfo information(m_diagnosticsPath);
    if (!information.exists())
    {
        const bool hadEstablishedSource =
            m_fileIdentity.valid ||
            m_readOffset > 0 ||
            !m_records.isEmpty() ||
            !m_pendingLine.isEmpty() ||
            m_discardingLongLine;
        if (!m_sourceMissingReported)
        {
            reportIssue(
                IssueCode::FileNotFound,
                tr("Diagnostics file does not exist: %1")
                    .arg(m_diagnosticsPath));
            m_sourceMissingReported = true;
        }
        if (hadEstablishedSource)
        {
            beginResetModel();
            m_records.clear();
            endResetModel();
            closeSource();
            resetStreamState(false);
        }
        return false;
    }
    m_sourceMissingReported = false;

    if (!information.isFile() || information.isSymLink())
    {
        reportIssue(
            IssueCode::FileNotRegular,
            tr("Diagnostics path is not a regular file: %1")
                .arg(m_diagnosticsPath));
        return false;
    }

    std::unique_ptr<QFile> currentFile =
        std::make_unique<QFile>(m_diagnosticsPath);
    if (!currentFile->open(QIODevice::ReadOnly))
    {
        reportIssue(
            IssueCode::FileOpenFailed,
            tr("Cannot open diagnostics file \"%1\": %2")
                .arg(m_diagnosticsPath, currentFile->errorString()));
        return false;
    }

    const FileIdentity currentIdentity =
        identityForFile(*currentFile);
    if (!currentIdentity.valid)
    {
        reportIssue(
            IssueCode::FileOpenFailed,
            tr("Cannot identify diagnostics file: %1")
                .arg(m_diagnosticsPath));
        return false;
    }

    if (!m_fileIdentity.valid)
    {
        m_file = std::move(currentFile);
        m_fileIdentity = currentIdentity;
        return true;
    }

    if (currentIdentity != m_fileIdentity)
    {
        reportIssue(
            IssueCode::FileReplaced,
            tr("Diagnostics file was replaced; prior events were reset."));
        beginResetModel();
        m_records.clear();
        endResetModel();
        resetStreamState(false);
        m_fileIdentity = currentIdentity;
    }
    m_file = std::move(currentFile);
    return true;
}

bool DesktopRunDiagnosticsModel::readAvailableBytes()
{
    if (!m_file)
        return false;

    const qint64 availableSize = m_file->size();
    if (availableSize < 0)
    {
        reportIssue(
            IssueCode::FileReadFailed,
            tr("Cannot determine the diagnostics file size."));
        return false;
    }
    if (availableSize < m_readOffset)
    {
        reportIssue(
            IssueCode::FileTruncated,
            tr("Diagnostics file was truncated; prior events were reset."));
        beginResetModel();
        m_records.clear();
        endResetModel();
        resetStreamState(false);
    }

    const qint64 targetOffset = m_file->size();
    if (targetOffset < 0)
    {
        reportIssue(
            IssueCode::FileReadFailed,
            tr("Cannot determine the diagnostics file size after reset."));
        return false;
    }
    if (targetOffset > m_maximumFileBytes)
    {
        if (!m_fileLimitReported)
        {
            reportIssue(
                IssueCode::FileTooLarge,
                tr("Diagnostics file size %1 exceeds the %2-byte limit.")
                    .arg(targetOffset)
                    .arg(m_maximumFileBytes));
            m_fileLimitReported = true;
        }
        return false;
    }
    if (!m_file->seek(m_readOffset))
    {
        reportIssue(
            IssueCode::FileReadFailed,
            tr("Cannot seek to byte %1 in the diagnostics file.")
                .arg(m_readOffset));
        return false;
    }

    while (m_readOffset < targetOffset)
    {
        const qint64 requestedBytes =
            std::min(ReadChunkBytes, targetOffset - m_readOffset);
        const QByteArray bytes = m_file->read(requestedBytes);
        if (bytes.isEmpty())
        {
            reportIssue(
                IssueCode::FileReadFailed,
                tr("Diagnostics file ended before byte %1.")
                    .arg(targetOffset),
                m_readOffset,
                m_currentLineNumber);
            return false;
        }
        const qint64 firstByteOffset = m_readOffset;
        m_readOffset += bytes.size();
        m_consumedHasher->addData(bytes);
        consumeBytes(bytes, firstByteOffset);
    }

    updateConsumedDigest();
    return true;
}

bool DesktopRunDiagnosticsModel::verifyConsumedDigest()
{
    if (!m_file || m_readOffset == 0)
        return true;

    const qint64 currentSize = m_file->size();
    if (currentSize < m_readOffset)
        return true;

    QCryptographicHash verifier(QCryptographicHash::Sha256);
    const qint64 savedPosition = m_file->pos();
    if (!m_file->seek(0))
    {
        reportIssue(
            IssueCode::FileReadFailed,
            tr("Cannot seek to verify consumed diagnostics bytes."));
        return false;
    }

    qint64 remainingBytes = m_readOffset;
    while (remainingBytes > 0)
    {
        const qint64 requestedBytes =
            std::min(ReadChunkBytes, remainingBytes);
        const QByteArray bytes = m_file->read(requestedBytes);
        if (bytes.size() != requestedBytes)
        {
            reportIssue(
                IssueCode::FileReadFailed,
                tr("Cannot read all previously consumed diagnostics "
                   "bytes for verification."));
            return false;
        }
        verifier.addData(bytes);
        remainingBytes -= bytes.size();
    }

    if (!m_file->seek(savedPosition))
    {
        reportIssue(
            IssueCode::FileReadFailed,
            tr("Cannot restore the diagnostics read position."));
        return false;
    }
    if (verifier.result() == m_consumedDigest)
        return true;

    reportIssue(
        IssueCode::FileContentChanged,
        tr("Previously consumed diagnostics bytes changed; prior events "
           "were reset."));
    beginResetModel();
    m_records.clear();
    endResetModel();
    resetStreamState(false);
    return true;
}

void DesktopRunDiagnosticsModel::updateConsumedDigest()
{
    if (m_readOffset == 0)
    {
        m_consumedDigest.clear();
        return;
    }
    m_consumedDigest = m_consumedHasher->result();
}

void DesktopRunDiagnosticsModel::consumeBytes(
    const QByteArray& bytes,
    qint64 firstByteOffset)
{
    for (qsizetype index = 0; index < bytes.size(); ++index)
    {
        const char byte = bytes.at(index);
        const qint64 byteOffset = firstByteOffset + index;
        if (m_discardingLongLine)
        {
            if (byte == '\n')
            {
                m_discardingLongLine = false;
                ++m_currentLineNumber;
                m_pendingLineOffset = byteOffset + 1;
                m_incompleteLineReported = false;
            }
            continue;
        }

        if (byte == '\n')
        {
            QByteArray completeLine = m_pendingLine;
            if (completeLine.endsWith('\r'))
                completeLine.chop(1);
            consumeCompleteLine(
                completeLine,
                m_pendingLineOffset,
                m_currentLineNumber);
            m_pendingLine.clear();
            ++m_currentLineNumber;
            m_pendingLineOffset = byteOffset + 1;
            m_incompleteLineReported = false;
            continue;
        }

        if (m_pendingLine.size() >= m_maximumLineBytes)
        {
            reportIssue(
                IssueCode::LineTooLong,
                tr("A diagnostics line exceeded the %1-byte limit.")
                    .arg(m_maximumLineBytes),
                m_pendingLineOffset,
                m_currentLineNumber);
            m_pendingLine.clear();
            m_discardingLongLine = true;
            continue;
        }
        m_pendingLine.append(byte);
    }
}

void DesktopRunDiagnosticsModel::consumeCompleteLine(
    const QByteArray& line,
    qint64 byteOffset,
    quint64 lineNumber)
{
    if (m_recordLimitReached)
        return;

    Record record;
    Issue issue;
    if (!parseRecord(line, record, issue))
    {
        reportIssue(
            issue.code,
            issue.message,
            byteOffset,
            lineNumber);
        return;
    }

    if (record.sequence != m_nextExpectedSequence)
    {
        reportIssue(
            IssueCode::SequenceOutOfOrder,
            tr("Expected diagnostics sequence %1 but received %2.")
                .arg(m_nextExpectedSequence)
                .arg(record.sequence),
            byteOffset,
            lineNumber);
        return;
    }

    if (m_records.size() >= m_maximumRecordCount)
    {
        reportIssue(
            IssueCode::RecordLimitReached,
            tr("Additional diagnostics records were omitted after "
               "reaching the %1-record limit.")
                .arg(m_maximumRecordCount),
            byteOffset,
            lineNumber);
        m_recordLimitReached = true;
        return;
    }

    appendRecord(record);
    ++m_nextExpectedSequence;
}

bool DesktopRunDiagnosticsModel::parseRecord(
    const QByteArray& line,
    Record& record,
    Issue& issue) const
{
    if (!strictUtf8(line))
    {
        issue.code = IssueCode::InvalidUtf8;
        issue.message =
            tr("Diagnostics line is not valid UTF-8.");
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        issue.code = IssueCode::InvalidJson;
        issue.message =
            tr("Invalid diagnostics JSON: %1")
                .arg(parseError.errorString());
        return false;
    }
    if (!document.isObject())
    {
        issue.code = IssueCode::InvalidRootType;
        issue.message =
            tr("A diagnostics line must contain one JSON object.");
        return false;
    }

    const QJsonObject object = document.object();
    quint64 schemaVersion = 0;
    if (!object.contains(QStringLiteral("schemaVersion")))
    {
        issue.code = IssueCode::MissingField;
        issue.message =
            tr("Required field \"schemaVersion\" is missing.");
        return false;
    }
    if (!jsonUnsignedInteger(
            object.value(QStringLiteral("schemaVersion")),
            1,
            MaximumExactJsonInteger,
            schemaVersion))
    {
        issue.code = IssueCode::InvalidFieldType;
        issue.message =
            tr("Field \"schemaVersion\" must be a positive integer.");
        return false;
    }
    if (schemaVersion != 1)
    {
        issue.code = IssueCode::UnsupportedSchemaVersion;
        issue.message =
            tr("Unsupported diagnostics schema version: %1")
                .arg(schemaVersion);
        return false;
    }

    QString eventSessionId;
    if (!requiredString(
            object,
            QStringLiteral("sessionId"),
            eventSessionId,
            issue))
    {
        return false;
    }
    if (eventSessionId != m_sessionId)
    {
        issue.code = IssueCode::SessionMismatch;
        issue.message =
            tr("Diagnostics session \"%1\" does not match \"%2\".")
                .arg(eventSessionId, m_sessionId);
        return false;
    }

    if (!object.contains(QStringLiteral("sequence")))
    {
        issue.code = IssueCode::MissingField;
        issue.message =
            tr("Required field \"sequence\" is missing.");
        return false;
    }
    if (!jsonUnsignedInteger(
            object.value(QStringLiteral("sequence")),
            1,
            MaximumExactJsonInteger,
            record.sequence))
    {
        issue.code = IssueCode::InvalidFieldValue;
        issue.message =
            tr("Field \"sequence\" must be a positive exact integer.");
        return false;
    }

    QString severity;
    if (!requiredString(
            object,
            QStringLiteral("severity"),
            severity,
            issue))
    {
        return false;
    }
    if (severity == QStringLiteral("info"))
        record.severity = Severity::Info;
    else if (severity == QStringLiteral("warning"))
        record.severity = Severity::Warning;
    else if (severity == QStringLiteral("error"))
        record.severity = Severity::Error;
    else
    {
        issue.code = IssueCode::InvalidFieldValue;
        issue.message =
            tr("Field \"severity\" must be info, warning, or error.");
        return false;
    }

    if (!requiredString(
            object,
            QStringLiteral("code"),
            record.code,
            issue) ||
        !requiredString(
            object,
            QStringLiteral("message"),
            record.message,
            issue))
    {
        return false;
    }
    if (record.code.isEmpty())
    {
        issue.code = IssueCode::InvalidFieldValue;
        issue.message =
            tr("Field \"code\" must not be empty.");
        return false;
    }

    if (!optionalString(
            object,
            QStringLiteral("file"),
            record.source.file,
            issue) ||
        !optionalString(
            object,
            QStringLiteral("target"),
            record.target,
            issue) ||
        !optionalPositiveUint32(
            object,
            QStringLiteral("line"),
            record.source.line,
            issue) ||
        !optionalPositiveUint32(
            object,
            QStringLiteral("column"),
            record.source.column,
            issue))
    {
        return false;
    }
    if ((record.source.line > 0 || record.source.column > 0) &&
        record.source.file.isEmpty())
    {
        issue.code = IssueCode::InvalidFieldValue;
        issue.message =
            tr("Fields \"line\" and \"column\" require a non-empty "
               "\"file\" field.");
        return false;
    }
    return true;
}

void DesktopRunDiagnosticsModel::appendRecord(const Record& record)
{
    const int row = m_records.size();
    beginInsertRows(QModelIndex(), row, row);
    m_records.append(record);
    endInsertRows();
}

void DesktopRunDiagnosticsModel::reportIssue(
    IssueCode code,
    const QString& message,
    qint64 byteOffset,
    quint64 lineNumber)
{
    Issue issue;
    issue.code = code;
    issue.message = message;
    issue.byteOffset = byteOffset;
    issue.lineNumber = lineNumber;
    ++m_reportedIssueSerial;

    if (m_issues.size() < m_maximumIssueCount)
    {
        m_issues.append(issue);
        emit issuesChanged();
        return;
    }

    bool issuesChangedNow = false;
    const bool alreadyHasLimitEvidence = std::any_of(
        m_issues.cbegin(),
        m_issues.cend(),
        [](const Issue& existingIssue)
        {
            return isLimitEvidence(existingIssue.code);
        });
    if (isLimitEvidence(code) && !alreadyHasLimitEvidence)
    {
        for (qsizetype index = m_issues.size(); index > 0; --index)
        {
            if (!isLimitEvidence(m_issues.at(index - 1).code))
            {
                m_issues[index - 1] = issue;
                issuesChangedNow = true;
                break;
            }
        }
    }

    if (!m_issueLimitReported)
    {
        m_issueLimitReported = true;
        Issue limitIssue;
        limitIssue.code = IssueCode::IssueLimitReached;
        limitIssue.message =
            tr("Additional diagnostics model issues were omitted after "
               "reaching the %1-entry limit.")
                .arg(m_maximumIssueCount);
        limitIssue.byteOffset = byteOffset;
        limitIssue.lineNumber = lineNumber;
        for (qsizetype index = m_issues.size(); index > 0; --index)
        {
            if (!isLimitEvidence(m_issues.at(index - 1).code))
            {
                m_issues[index - 1] = limitIssue;
                issuesChangedNow = true;
                break;
            }
        }
    }

    if (issuesChangedNow)
        emit issuesChanged();
}

void DesktopRunDiagnosticsModel::resetStreamState(bool resetRecords)
{
    if (resetRecords)
    {
        beginResetModel();
        m_records.clear();
        endResetModel();
    }
    m_pendingLine.clear();
    m_consumedDigest.clear();
    m_consumedHasher->reset();
    m_readOffset = 0;
    m_pendingLineOffset = 0;
    m_currentLineNumber = 1;
    m_nextExpectedSequence = 1;
    m_discardingLongLine = false;
    m_incompleteLineReported = false;
    m_fileLimitReported = false;
    m_recordLimitReached = false;
}

void DesktopRunDiagnosticsModel::closeSource()
{
    if (m_file)
        m_file->close();
    m_file.reset();
    m_fileIdentity = FileIdentity();
}

QString DesktopRunDiagnosticsModel::formattedLocation(
    const SourceLocation& source) const
{
    if (source.file.isEmpty())
        return QString();

    QString location = source.file;
    if (source.line > 0)
        location += QStringLiteral(":") + QString::number(source.line);
    if (source.column > 0)
    {
        if (source.line == 0)
            location += QStringLiteral(":0");
        location +=
            QStringLiteral(":") + QString::number(source.column);
    }
    return location;
}

QString DesktopRunDiagnosticsModel::severityName(Severity severity)
{
    switch (severity)
    {
    case Severity::Info:
        return QStringLiteral("info");
    case Severity::Warning:
        return QStringLiteral("warning");
    case Severity::Error:
        return QStringLiteral("error");
    }
    return QStringLiteral("error");
}

bool DesktopRunDiagnosticsModel::strictUtf8(const QByteArray& bytes)
{
    for (qsizetype offset = 0; offset < bytes.size();)
    {
        const unsigned char first =
            static_cast<unsigned char>(bytes.at(offset));
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

        for (qsizetype index = 1; index < length; ++index)
        {
            const unsigned char continuation =
                static_cast<unsigned char>(
                    bytes.at(offset + index));
            if ((continuation & 0xC0U) != 0x80U)
                return false;
            codePoint =
                (codePoint << 6U) |
                static_cast<quint32>(continuation & 0x3FU);
        }
        if (codePoint < minimumCodePoint ||
            codePoint > 0x10FFFFU ||
            (codePoint >= 0xD800U && codePoint <= 0xDFFFU))
        {
            return false;
        }
        offset += length;
    }
    return true;
}

bool DesktopRunDiagnosticsModel::isLimitEvidence(IssueCode code)
{
    return code == IssueCode::FileTooLarge ||
        code == IssueCode::LineTooLong ||
        code == IssueCode::RecordLimitReached ||
        code == IssueCode::IssueLimitReached;
}

DesktopRunDiagnosticsModel::FileIdentity
DesktopRunDiagnosticsModel::identityForFile(QFile& file)
{
    FileIdentity identity;
    const qintptr descriptor = file.handle();
    if (descriptor < 0)
        return identity;

#ifdef Q_OS_WIN
    const intptr_t nativeHandleValue =
        _get_osfhandle(static_cast<int>(descriptor));
    if (nativeHandleValue == -1)
        return identity;
    BY_HANDLE_FILE_INFORMATION information = {};
    if (!GetFileInformationByHandle(
            reinterpret_cast<HANDLE>(nativeHandleValue),
            &information))
    {
        return identity;
    }
    identity.first =
        (static_cast<quint64>(information.dwVolumeSerialNumber) << 32U) |
        information.nFileIndexHigh;
    identity.second = information.nFileIndexLow;
#else
    struct stat information = {};
    if (fstat(static_cast<int>(descriptor), &information) != 0 ||
        !S_ISREG(information.st_mode))
    {
        return identity;
    }
    identity.first = static_cast<quint64>(information.st_dev);
    identity.second = static_cast<quint64>(information.st_ino);
#endif
    identity.valid = true;
    return identity;
}
