#pragma once

#include <QAbstractTableModel>
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

#include <memory>

class QFile;
class QCryptographicHash;

class DesktopRunDiagnosticsModel final : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column
    {
        SeverityColumn = 0,
        CodeColumn,
        MessageColumn,
        LocationColumn,
        TargetColumn,
        ColumnCount
    };
    Q_ENUM(Column)

    enum Role
    {
        SeverityRole = Qt::UserRole + 1,
        SeverityNameRole,
        CodeRole,
        MessageRole,
        LocationRole,
        TargetRole,
        SourceFileRole,
        SourceLineRole,
        SourceColumnRole,
        SequenceRole
    };
    Q_ENUM(Role)

    enum class Severity
    {
        Info,
        Warning,
        Error
    };
    Q_ENUM(Severity)

    enum class IssueCode
    {
        SourceNotConfigured,
        FileNotFound,
        FileNotRegular,
        FileOpenFailed,
        FileReadFailed,
        FileReplaced,
        FileTruncated,
        FileContentChanged,
        FileTooLarge,
        InvalidUtf8,
        InvalidJson,
        InvalidRootType,
        UnsupportedSchemaVersion,
        SessionMismatch,
        MissingField,
        InvalidFieldType,
        InvalidFieldValue,
        SequenceOutOfOrder,
        LineTooLong,
        RecordLimitReached,
        IssueLimitReached,
        IncompleteLine
    };
    Q_ENUM(IssueCode)

    struct SourceLocation
    {
        QString file;
        quint32 line = 0;
        quint32 column = 0;

        bool isValid() const
        {
            return !file.isEmpty();
        }
    };

    struct Record
    {
        quint64 sequence = 0;
        Severity severity = Severity::Info;
        QString code;
        QString message;
        SourceLocation source;
        QString target;
    };

    struct Issue
    {
        IssueCode code = IssueCode::FileReadFailed;
        QString message;
        qint64 byteOffset = -1;
        quint64 lineNumber = 0;
    };

    explicit DesktopRunDiagnosticsModel(QObject* parent = nullptr);
    ~DesktopRunDiagnosticsModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(
        const QModelIndex& index,
        int role = Qt::DisplayRole) const override;
    QVariant headerData(
        int section,
        Qt::Orientation orientation,
        int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setSource(
        const QString& diagnosticsPath,
        const QString& sessionId);
    QString diagnosticsPath() const;
    QString sessionId() const;

    // Reads only bytes appended since the prior refresh. A false result means
    // that this refresh observed at least one file or stream issue.
    bool refresh();

    // Call after the producer has terminated. A retained non-empty partial
    // line is reported, but is never parsed without its terminating newline.
    bool finalizeStream();

    void clear();
    void clearIssues();

    const Record* recordAt(int row) const;
    SourceLocation sourceLocationAt(int row) const;
    const QVector<Issue>& issues() const;
    Issue lastIssue() const;

    qsizetype pendingByteCount() const;
    quint64 nextExpectedSequence() const;
    qsizetype maximumLineBytes() const;
    void setMaximumLineBytes(qsizetype maximumBytes);
    qint64 maximumFileBytes() const;
    void setMaximumFileBytes(qint64 maximumBytes);
    qsizetype maximumRecordCount() const;
    void setMaximumRecordCount(qsizetype maximumCount);
    qsizetype maximumIssueCount() const;
    void setMaximumIssueCount(qsizetype maximumCount);

signals:
    void issuesChanged();

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

    bool openOrValidateSource();
    bool readAvailableBytes();
    bool verifyConsumedDigest();
    void updateConsumedDigest();
    void consumeBytes(const QByteArray& bytes, qint64 firstByteOffset);
    void consumeCompleteLine(
        const QByteArray& line,
        qint64 byteOffset,
        quint64 lineNumber);
    bool parseRecord(
        const QByteArray& line,
        Record& record,
        Issue& issue) const;
    void appendRecord(const Record& record);
    void reportIssue(
        IssueCode code,
        const QString& message,
        qint64 byteOffset = -1,
        quint64 lineNumber = 0);
    void resetStreamState(bool resetRecords);
    void closeSource();
    QString formattedLocation(const SourceLocation& source) const;
    static QString severityName(Severity severity);
    static bool strictUtf8(const QByteArray& bytes);
    static bool isLimitEvidence(IssueCode code);
    static FileIdentity identityForFile(QFile& file);

    QString m_diagnosticsPath;
    QString m_sessionId;
    std::unique_ptr<QFile> m_file;
    FileIdentity m_fileIdentity;
    QVector<Record> m_records;
    QVector<Issue> m_issues;
    QByteArray m_pendingLine;
    QByteArray m_consumedDigest;
    std::unique_ptr<QCryptographicHash> m_consumedHasher;
    qint64 m_readOffset = 0;
    qint64 m_pendingLineOffset = 0;
    quint64 m_currentLineNumber = 1;
    quint64 m_nextExpectedSequence = 1;
    qsizetype m_maximumLineBytes = 1024 * 1024;
    qint64 m_maximumFileBytes = 64 * 1024 * 1024;
    qsizetype m_maximumRecordCount = 100000;
    qsizetype m_maximumIssueCount = 1024;
    quint64 m_reportedIssueSerial = 0;
    bool m_discardingLongLine = false;
    bool m_sourceMissingReported = false;
    bool m_incompleteLineReported = false;
    bool m_fileLimitReported = false;
    bool m_recordLimitReached = false;
    bool m_issueLimitReported = false;
};
