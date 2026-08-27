#pragma once

#include "ProjectRuntimeConfiguration.h"

#include <QString>
#include <QStringList>

#include <optional>

enum class DesktopRunCurrentTargetError
{
    None,
    BaselineMissing,
    BaselineAmbiguous,
    PlayerPositionOutOfBounds
};

struct DesktopRunCurrentTargetResult
{
    std::optional<ProjectScene> target;
    DesktopRunCurrentTargetError error =
        DesktopRunCurrentTargetError::None;
    QString diagnosticCode;
    QStringList matchingSceneIds;
    QStringList warningCodes;

    bool succeeded() const
    {
        return target.has_value() &&
            error == DesktopRunCurrentTargetError::None;
    }
};

// Selects the saved environment baseline for a transient current-map target.
// A matching default wins, otherwise the selected scene or one unique match
// wins. With zero exact matches the selected scene (when valid) or default
// scene supplies the environment baseline. Multiple unresolved matches remain
// ambiguous. The caller-provided MAP/NPC/OBJ paths replace every saved
// resource reference and entryScript is always cleared.
DesktopRunCurrentTargetResult selectCurrentMapTarget(
    const ProjectRuntimeConfiguration& configuration,
    const QString& mapVirtualPath,
    const QString& npcVirtualPath,
    const QString& objectVirtualPath,
    int mapWidth,
    int mapHeight,
    Qt::CaseSensitivity pathCaseSensitivity,
    const QString& preferredSceneId = QString());

// Selects the saved environment baseline for a transient current-script
// target using the same saved-environment baseline rules. The baseline map,
// NPC, OBJ, player position, and variables are retained; only entryScript is
// replaced by the current script.
DesktopRunCurrentTargetResult selectCurrentScriptTarget(
    const ProjectRuntimeConfiguration& configuration,
    const QString& scriptVirtualPath,
    Qt::CaseSensitivity pathCaseSensitivity,
    const QString& preferredSceneId = QString());
