#include "DesktopRunSessionCleanup.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

namespace
{
constexpr auto MarkerFileName = "session-marker.json";
constexpr auto OverlayDirectoryName = "overlay";
constexpr auto SaveDirectoryName = "save";
constexpr auto ApplicationStateDirectoryName = "application-state";
constexpr auto DiagnosticsDirectoryName = "diagnostics";
constexpr auto DiagnosticsFileName = "diagnostics.jsonl";
constexpr auto LogFileName = "game.log";
constexpr auto RuntimeTraceFileName = "runtime-trace.jsonl";
constexpr auto ResourceRoutingContractFileName =
    "resource-routing-contract.json";
constexpr auto DescriptorFileName = "launch-descriptor.json";
constexpr qsizetype MaximumMarkerBytes = 512;

QString normalizedPath(const QString& path)
{
    return QDir::cleanPath(
        QDir::fromNativeSeparators(path));
}

QString pathComparisonKey(const QString& path)
{
    QString key = normalizedPath(path);
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}

bool samePath(const QString& left, const QString& right)
{
    return pathComparisonKey(left) == pathComparisonKey(right);
}

bool isJunctionOrLink(const QFileInfo& information)
{
    if (information.isSymLink())
        return true;
#ifdef Q_OS_WIN
    if (information.isJunction())
        return true;
#endif
    return false;
}

bool isCanonicalDescendant(
    const QString& canonicalRoot,
    const QString& canonicalCandidate)
{
    const QString rootKey = pathComparisonKey(canonicalRoot);
    const QString candidateKey =
        pathComparisonKey(canonicalCandidate);
    if (rootKey.isEmpty() || candidateKey.isEmpty())
        return false;
    return candidateKey == rootKey ||
        candidateKey.startsWith(
            rootKey + QLatin1Char('/'));
}

bool exactSessionId(const QString& sessionId)
{
    const QUuid parsed(sessionId);
    return !parsed.isNull() &&
        parsed.toString(QUuid::WithoutBraces).toLower() ==
            sessionId;
}

QByteArray expectedMarker(const QString& sessionId)
{
    return QByteArray("{\"schemaVersion\":") +
        QByteArray::number(
            DesktopRunSessionWorkspace::SchemaVersion) +
        ",\"descriptorSchemaVersion\":" +
        QByteArray::number(
            EditorRun::Descriptor::SchemaVersion) +
        ",\"sessionId\":\"" +
        sessionId.toLatin1() +
        "\"}";
}

bool readExactMarker(
    const QString& markerPath,
    const QString& sessionId,
    QString& message)
{
    const QFileInfo information(markerPath);
    if (!information.exists() ||
        !information.isFile() ||
        isJunctionOrLink(information) ||
        information.size() < 0 ||
        information.size() > MaximumMarkerBytes)
    {
        message = QStringLiteral(
            "Private session marker is unavailable or unsafe");
        return false;
    }

    QFile marker(markerPath);
    if (!marker.open(QIODevice::ReadOnly))
    {
        message = QStringLiteral(
            "Private session marker cannot be opened");
        return false;
    }
    const QByteArray bytes = marker.read(MaximumMarkerBytes + 1);
    if (bytes != expectedMarker(sessionId))
    {
        message = QStringLiteral(
            "Private session marker does not match the current session owner");
        return false;
    }
    return true;
}

bool workspacePathsMatch(
    const QString& sessionRoot,
    const DesktopRunSessionWorkspace& workspace)
{
    const QDir root(sessionRoot);
    const QString diagnosticsRoot =
        root.filePath(
            QString::fromLatin1(
                DiagnosticsDirectoryName));
    return samePath(workspace.paths.sessionRoot, sessionRoot) &&
        samePath(
            workspace.paths.overlayRoot,
            root.filePath(
                QString::fromLatin1(
                    OverlayDirectoryName))) &&
        samePath(
            workspace.paths.isolatedSaveRoot,
            root.filePath(
                QString::fromLatin1(
                    SaveDirectoryName))) &&
        samePath(
            workspace.paths.applicationStateRoot,
            root.filePath(
                QString::fromLatin1(
                    ApplicationStateDirectoryName))) &&
        samePath(
            workspace.paths.diagnosticsRoot,
            diagnosticsRoot) &&
        samePath(
            workspace.paths.diagnosticsPath,
            QDir(diagnosticsRoot).filePath(
                QString::fromLatin1(
                    DiagnosticsFileName))) &&
        samePath(
            workspace.paths.logPath,
            QDir(diagnosticsRoot).filePath(
                QString::fromLatin1(LogFileName))) &&
        samePath(
            workspace.paths.runtimeTracePath,
            QDir(diagnosticsRoot).filePath(
                QString::fromLatin1(
                    RuntimeTraceFileName))) &&
        samePath(
            workspace.paths.markerPath,
            root.filePath(
                QString::fromLatin1(MarkerFileName))) &&
        samePath(
            workspace.paths.resourceRoutingContractPath,
            root.filePath(
                QString::fromLatin1(
                    ResourceRoutingContractFileName))) &&
        samePath(
            workspace.paths.descriptorPath,
            root.filePath(
                QString::fromLatin1(
                    DescriptorFileName)));
}

void fail(
    DesktopRunSessionCleanupResult& result,
    DesktopRunSessionCleanupError error,
    const QString& problemPath,
    const QString& message)
{
    result.error = error;
    result.problemPath = problemPath;
    result.message = message;
}
}

DesktopRunSessionCleanupResult cleanupDesktopRunSession(
    const QString& trustedSessionsBaseDirectory,
    const DesktopRunSessionWorkspace& workspace,
    const DesktopRunSessionCleanupRequest& request)
{
    DesktopRunSessionCleanupResult result;
    result.sessionId = request.sessionId;
    result.sessionPath = workspace.paths.sessionRoot;

    if (request.processActive)
    {
        fail(
            result,
            DesktopRunSessionCleanupError::ProcessActive,
            workspace.paths.sessionRoot,
            QStringLiteral(
                "The current game process still owns this private session"));
        return result;
    }
    if (!exactSessionId(request.sessionId))
    {
        fail(
            result,
            DesktopRunSessionCleanupError::InvalidSessionId,
            workspace.paths.sessionRoot,
            QStringLiteral(
                "Cleanup target must be one lowercase UUID session"));
        return result;
    }
    if (workspace.sessionId != request.sessionId ||
        workspace.descriptor.sessionId !=
            request.sessionId.toStdString())
    {
        fail(
            result,
            DesktopRunSessionCleanupError::WorkspaceMismatch,
            workspace.paths.sessionRoot,
            QStringLiteral(
                "Cleanup request and current workspace do not name the same session"));
        return result;
    }

    const QFileInfo baseInformation(
        trustedSessionsBaseDirectory);
    const QString canonicalBase =
        baseInformation.canonicalFilePath();
    if (canonicalBase.isEmpty() ||
        !baseInformation.isDir() ||
        isJunctionOrLink(baseInformation))
    {
        fail(
            result,
            DesktopRunSessionCleanupError::InvalidSessionsBase,
            trustedSessionsBaseDirectory,
            QStringLiteral(
                "Trusted sessions base is unavailable or linked"));
        return result;
    }

    const QString expectedSessionRoot =
        QDir(canonicalBase).filePath(request.sessionId);
    result.sessionPath = expectedSessionRoot;
    if (!workspacePathsMatch(expectedSessionRoot, workspace))
    {
        fail(
            result,
            DesktopRunSessionCleanupError::WorkspaceMismatch,
            workspace.paths.sessionRoot,
            QStringLiteral(
                "Current workspace paths do not match the private session topology"));
        return result;
    }

    const QFileInfo sessionInformation(expectedSessionRoot);
    if (!sessionInformation.exists())
    {
        result.alreadyAbsent = true;
        result.sessionPath.clear();
        return result;
    }
    const QString canonicalSessionRoot =
        sessionInformation.canonicalFilePath();
    if (!sessionInformation.isDir() ||
        isJunctionOrLink(sessionInformation) ||
        !samePath(
            canonicalSessionRoot,
            expectedSessionRoot) ||
        !isCanonicalDescendant(
            canonicalBase,
            canonicalSessionRoot))
    {
        fail(
            result,
            DesktopRunSessionCleanupError::SessionRouteChanged,
            expectedSessionRoot,
            QStringLiteral(
                "Private session route is linked or no longer names the expected UUID child"));
        return result;
    }

    QString validationMessage;
    if (!readExactMarker(
            workspace.paths.markerPath,
            request.sessionId,
            validationMessage))
    {
        fail(
            result,
            DesktopRunSessionCleanupError::MarkerInvalid,
            workspace.paths.markerPath,
            validationMessage);
        return result;
    }
    const QFileInfo finalSessionInformation(expectedSessionRoot);
    if (isJunctionOrLink(finalSessionInformation) ||
        !samePath(
            finalSessionInformation.canonicalFilePath(),
            canonicalSessionRoot) ||
        !readExactMarker(
            workspace.paths.markerPath,
            request.sessionId,
            validationMessage))
    {
        fail(
            result,
            DesktopRunSessionCleanupError::SessionRouteChanged,
            expectedSessionRoot,
            QStringLiteral(
                "Private session changed before cleanup"));
        return result;
    }

    QDir sessionDirectory(expectedSessionRoot);
    if (!sessionDirectory.removeRecursively() ||
        QFileInfo::exists(expectedSessionRoot))
    {
        fail(
            result,
            DesktopRunSessionCleanupError::DeleteFailed,
            expectedSessionRoot,
            QStringLiteral(
                "Validated private session could not be removed"));
        return result;
    }

    result.removed = true;
    result.sessionPath.clear();
    return result;
}
