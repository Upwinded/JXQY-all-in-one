#pragma once

#include <QString>

enum class DesktopRunSessionBaseError
{
    None,
    DefaultLocationUnavailable,
    DefaultParentCreationFailed,
    InvalidParentPath,
    InvalidBaseDirectoryName,
    ParentUnavailable,
    ParentIsLink,
    ParentIsNotDirectory,
    BaseIsLink,
    BaseIsNotDirectory,
    BaseCreationFailed,
    BaseOwnerMismatch,
    BasePermissionsUntrusted,
    BaseRouteChanged
};

struct DesktopRunSessionBaseLocation
{
    QString parentDirectory;
    QString baseDirectoryName;

    QString baseDirectoryPath() const;
};

struct DesktopRunSessionBaseResult
{
    QString path;
    DesktopRunSessionBaseError error =
        DesktopRunSessionBaseError::None;
    QString problemPath;
    QString message;
    bool created = false;

    bool succeeded() const
    {
        return error ==
                DesktopRunSessionBaseError::None &&
            !path.isEmpty();
    }
};

// Returns the fixed direct-child location beneath
// QStandardPaths::AppLocalDataLocation without touching the filesystem.
DesktopRunSessionBaseLocation
defaultDesktopRunSessionBaseLocation();

// Creates or validates the default persistent desktop-run sessions base.
// The directory is deliberately not temporary: failed sessions remain
// available across editor restarts until the author explicitly removes them.
DesktopRunSessionBaseResult
ensureDefaultDesktopRunSessionBase();

// Creates or validates one direct child of an explicit existing parent.
// The separate parent/name form lets tests select an isolated location while
// preventing the base name itself from traversing outside that parent.
DesktopRunSessionBaseResult ensureDesktopRunSessionBase(
    const QString& parentDirectory,
    const QString& baseDirectoryName);
