#pragma once

#include "DesktopRunSessionWorkspace.h"

#include <QString>

enum class DesktopRunSessionCleanupError
{
    None,
    ProcessActive,
    InvalidSessionId,
    WorkspaceMismatch,
    InvalidSessionsBase,
    SessionRouteChanged,
    MarkerInvalid,
    DeleteFailed
};

struct DesktopRunSessionCleanupRequest
{
    QString sessionId;
    bool processActive = true;
};

struct DesktopRunSessionCleanupResult
{
    DesktopRunSessionCleanupError error =
        DesktopRunSessionCleanupError::None;
    QString sessionId;
    QString problemPath;
    QString sessionPath;
    QString message;
    bool removed = false;
    bool alreadyAbsent = false;

    bool succeeded() const
    {
        return error == DesktopRunSessionCleanupError::None;
    }
};

// Removes only the exact private workspace still owned in memory by the
// current editor-run coordinator. Root ownership is checked before deletion;
// descendants are temporary outputs from this editor/game pair and are not
// rediscovered or individually validated as retained user data.
DesktopRunSessionCleanupResult cleanupDesktopRunSession(
    const QString& trustedSessionsBaseDirectory,
    const DesktopRunSessionWorkspace& workspace,
    const DesktopRunSessionCleanupRequest& request);
