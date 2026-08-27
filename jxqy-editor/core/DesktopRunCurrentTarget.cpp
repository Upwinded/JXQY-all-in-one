#include "DesktopRunCurrentTarget.h"

namespace
{
QString normalizedVirtualPath(QString path)
{
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return path;
}

bool sameVirtualPath(
    const QString& left,
    const QString& right,
    Qt::CaseSensitivity caseSensitivity)
{
    return normalizedVirtualPath(left).compare(
               normalizedVirtualPath(right),
               caseSensitivity) == 0;
}

const ProjectScene* findDefaultScene(
    const ProjectRuntimeConfiguration& configuration)
{
    if (configuration.defaultSceneId.trimmed().isEmpty())
        return nullptr;
    for (const ProjectScene& scene : configuration.scenes)
    {
        if (scene.id == configuration.defaultSceneId)
            return &scene;
    }
    return nullptr;
}

const ProjectScene* findScene(
    const ProjectRuntimeConfiguration& configuration,
    const QString& sceneId)
{
    if (sceneId.trimmed().isEmpty())
        return nullptr;
    for (const ProjectScene& scene : configuration.scenes)
    {
        if (scene.id == sceneId)
            return &scene;
    }
    return nullptr;
}

template<typename PathAccessor>
DesktopRunCurrentTargetResult selectBaseline(
    const ProjectRuntimeConfiguration& configuration,
    const QString& targetVirtualPath,
    Qt::CaseSensitivity caseSensitivity,
    const QString& preferredSceneId,
    PathAccessor pathAccessor)
{
    DesktopRunCurrentTargetResult result;
    const ProjectScene* defaultScene =
        findDefaultScene(configuration);
    if (!defaultScene)
    {
        result.error =
            DesktopRunCurrentTargetError::BaselineMissing;
        result.diagnosticCode =
            QStringLiteral(
                "editor_run.current_target.baseline_missing");
        return result;
    }

    QList<const ProjectScene*> matches;
    for (const ProjectScene& scene : configuration.scenes)
    {
        if (sameVirtualPath(
                pathAccessor(scene),
                targetVirtualPath,
                caseSensitivity))
        {
            matches.append(&scene);
            result.matchingSceneIds.append(scene.id);
        }
    }

    const ProjectScene* selected = nullptr;
    for (const ProjectScene* match : matches)
    {
        if (match->id == defaultScene->id)
        {
            selected = match;
            break;
        }
    }
    if (!selected && !preferredSceneId.trimmed().isEmpty())
    {
        for (const ProjectScene* match : matches)
        {
            if (match->id == preferredSceneId)
            {
                selected = match;
                break;
            }
        }
    }
    if (!selected && matches.size() == 1)
        selected = matches.constFirst();
    if (!selected && matches.size() > 1)
    {
        result.error =
            DesktopRunCurrentTargetError::BaselineAmbiguous;
        result.diagnosticCode =
            QStringLiteral(
                "editor_run.current_target.baseline_ambiguous");
        return result;
    }
    if (!selected)
    {
        selected = findScene(
            configuration,
            preferredSceneId);
        if (!selected)
            selected = defaultScene;
    }

    result.target = *selected;
    return result;
}
}

DesktopRunCurrentTargetResult selectCurrentMapTarget(
    const ProjectRuntimeConfiguration& configuration,
    const QString& mapVirtualPath,
    const QString& npcVirtualPath,
    const QString& objectVirtualPath,
    int mapWidth,
    int mapHeight,
    Qt::CaseSensitivity pathCaseSensitivity,
    const QString& preferredSceneId)
{
    DesktopRunCurrentTargetResult result =
        selectBaseline(
            configuration,
            mapVirtualPath,
            pathCaseSensitivity,
            preferredSceneId,
            [](const ProjectScene& scene)
            {
                return scene.mapPath;
            });
    if (!result.succeeded())
        return result;

    const QPoint position =
        result.target->playerPosition;
    if (mapWidth <= 0 ||
        mapHeight <= 0)
    {
        result.target.reset();
        result.error =
            DesktopRunCurrentTargetError::
                PlayerPositionOutOfBounds;
        result.diagnosticCode =
            QStringLiteral(
                "editor_run.current_map.invalid_dimensions");
        return result;
    }
    if (position.x() < 0 ||
        position.y() < 0 ||
        position.x() >= mapWidth ||
        position.y() >= mapHeight)
    {
        result.target->playerPosition = QPoint(0, 0);
        result.warningCodes.append(
            QStringLiteral(
                "editor_run.current_map.player_position_fallback"));
    }

    result.target->mapPath =
        normalizedVirtualPath(mapVirtualPath);
    result.target->npcPath =
        normalizedVirtualPath(npcVirtualPath);
    result.target->objectPath =
        normalizedVirtualPath(objectVirtualPath);
    result.target->entryScriptPath.clear();
    return result;
}

DesktopRunCurrentTargetResult selectCurrentScriptTarget(
    const ProjectRuntimeConfiguration& configuration,
    const QString& scriptVirtualPath,
    Qt::CaseSensitivity pathCaseSensitivity,
    const QString& preferredSceneId)
{
    DesktopRunCurrentTargetResult result =
        selectBaseline(
            configuration,
            scriptVirtualPath,
            pathCaseSensitivity,
            preferredSceneId,
            [](const ProjectScene& scene)
            {
                return scene.entryScriptPath;
            });
    if (!result.succeeded())
        return result;

    result.target->entryScriptPath =
        normalizedVirtualPath(scriptVirtualPath);
    return result;
}
