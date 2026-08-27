#include "ProjectRuntimeConfiguration.h"

#include "ResourcePathValidation.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonValue>
#include <QSet>

#include <cmath>
#include <limits>

namespace
{
constexpr char RuntimeConfigurationKey[] = "runtimeConfiguration";

void setValidationError(
    ProjectRuntimeConfigurationValidationResult* result,
    ProjectRuntimeConfigurationError error,
    int sceneIndex = -1,
    const QString& fieldName = QString(),
    const QString& value = QString())
{
    if (!result)
        return;

    result->error = error;
    result->sceneIndex = sceneIndex;
    result->fieldName = fieldName;
    result->value = value;
}

bool readJsonInt32(const QJsonValue& value, int& output)
{
    if (!value.isDouble())
        return false;

    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < std::numeric_limits<int>::min() ||
        number > std::numeric_limits<int>::max())
    {
        return false;
    }
    output = static_cast<int>(number);
    return true;
}

bool readRequiredString(
    const QJsonObject& object,
    const QString& key,
    QString& output,
    ProjectRuntimeConfigurationError missingError,
    int sceneIndex,
    ProjectRuntimeConfigurationValidationResult* validationResult)
{
    const QJsonValue value = object.value(key);
    if (value.isUndefined())
    {
        setValidationError(
            validationResult, missingError, sceneIndex, key);
        return false;
    }
    if (!value.isString())
    {
        setValidationError(
            validationResult,
            ProjectRuntimeConfigurationError::InvalidFieldType,
            sceneIndex,
            key);
        return false;
    }
    output = value.toString();
    if (output.trimmed().isEmpty())
    {
        setValidationError(
            validationResult, missingError, sceneIndex, key);
        return false;
    }
    return true;
}

bool readOptionalString(
    const QJsonObject& object,
    const QString& key,
    QString& output,
    int sceneIndex,
    ProjectRuntimeConfigurationValidationResult* validationResult)
{
    const QJsonValue value = object.value(key);
    if (value.isUndefined())
    {
        output.clear();
        return true;
    }
    if (!value.isString())
    {
        setValidationError(
            validationResult,
            ProjectRuntimeConfigurationError::InvalidFieldType,
            sceneIndex,
            key);
        return false;
    }
    output = value.toString();
    return true;
}

bool normalizeResourcePath(
    QString& path,
    bool required,
    int sceneIndex,
    const QString& fieldName,
    bool& repaired,
    ProjectRuntimeConfigurationValidationResult* validationResult)
{
    if (path.isEmpty() || (required && path.trimmed().isEmpty()))
    {
        if (!required)
            return true;

        setValidationError(
            validationResult,
            ProjectRuntimeConfigurationError::MissingSceneMap,
            sceneIndex,
            fieldName);
        return false;
    }

    if (!EditorResourcePath::isSafeOptionalRelativeResourcePath(path))
    {
        setValidationError(
            validationResult,
            ProjectRuntimeConfigurationError::UnsafeResourcePath,
            sceneIndex,
            fieldName,
            path);
        return false;
    }

    QString normalized = path;
    normalized.replace('\\', '/');
    normalized = QDir::cleanPath(normalized);
    if (!EditorResourcePath::isSafeOptionalRelativeResourcePath(normalized) ||
        normalized.startsWith('/'))
    {
        setValidationError(
            validationResult,
            ProjectRuntimeConfigurationError::UnsafeResourcePath,
            sceneIndex,
            fieldName,
            path);
        return false;
    }

    for (int index = 0; index < normalized.size(); ++index)
    {
        const ushort character = normalized.at(index).unicode();
        if (character >= 'A' && character <= 'Z')
        {
            normalized[index] = QChar(
                static_cast<ushort>(character + ('a' - 'A')));
        }
    }

    if (path != normalized)
    {
        path = normalized;
        repaired = true;
    }
    return true;
}

ProjectRuntimeConfigurationValidationResult validateNormalizedConfiguration(
    const ProjectRuntimeConfiguration& configuration)
{
    ProjectRuntimeConfigurationValidationResult result;
    QSet<QString> sceneIds;
    for (int index = 0; index < configuration.scenes.size(); ++index)
    {
        const ProjectScene& scene = configuration.scenes.at(index);
        if (scene.id.trimmed().isEmpty())
        {
            setValidationError(
                &result,
                ProjectRuntimeConfigurationError::MissingSceneId,
                index,
                QStringLiteral("id"));
            return result;
        }
        if (scene.name.trimmed().isEmpty())
        {
            setValidationError(
                &result,
                ProjectRuntimeConfigurationError::MissingSceneName,
                index,
                QStringLiteral("name"));
            return result;
        }
        if (scene.mapPath.isEmpty())
        {
            setValidationError(
                &result,
                ProjectRuntimeConfigurationError::MissingSceneMap,
                index,
                QStringLiteral("map"));
            return result;
        }
        if (sceneIds.contains(scene.id))
        {
            setValidationError(
                &result,
                ProjectRuntimeConfigurationError::DuplicateSceneId,
                index,
                QStringLiteral("id"),
                scene.id);
            return result;
        }
        sceneIds.insert(scene.id);

        const QList<QPair<QString, QString>> paths = {
            {QStringLiteral("map"), scene.mapPath},
            {QStringLiteral("npc"), scene.npcPath},
            {QStringLiteral("object"), scene.objectPath},
            {QStringLiteral("entryScript"), scene.entryScriptPath}
        };
        for (const auto& path : paths)
        {
            if (!EditorResourcePath::isSafeOptionalRelativeResourcePath(
                    path.second) ||
                path.second.startsWith('/') ||
                path.second.contains('\\'))
            {
                setValidationError(
                    &result,
                    ProjectRuntimeConfigurationError::UnsafeResourcePath,
                    index,
                    path.first,
                    path.second);
                return result;
            }
        }

        for (auto variable = scene.integerVariables.cbegin();
             variable != scene.integerVariables.cend();
             ++variable)
        {
            if (variable.key().trimmed().isEmpty())
            {
                setValidationError(
                    &result,
                    ProjectRuntimeConfigurationError::InvalidVariableName,
                    index,
                    QStringLiteral("integerVariables"),
                    variable.key());
                return result;
            }
        }
    }

    if (configuration.scenes.isEmpty())
    {
        if (!configuration.defaultSceneId.isEmpty())
        {
            setValidationError(
                &result,
                ProjectRuntimeConfigurationError::InvalidDefaultSceneId,
                -1,
                QStringLiteral("defaultSceneId"),
                configuration.defaultSceneId);
        }
        return result;
    }

    if (configuration.defaultSceneId.isEmpty() ||
        !sceneIds.contains(configuration.defaultSceneId))
    {
        setValidationError(
            &result,
            ProjectRuntimeConfigurationError::InvalidDefaultSceneId,
            -1,
            QStringLiteral("defaultSceneId"),
            configuration.defaultSceneId);
    }
    return result;
}
}

bool readProjectRuntimeConfiguration(
    const QJsonObject& projectRoot,
    ProjectRuntimeConfiguration& configuration,
    bool& repaired,
    ProjectRuntimeConfigurationValidationResult* validationResult)
{
    repaired = false;
    if (validationResult)
        *validationResult = {};

    const QString key = QString::fromLatin1(RuntimeConfigurationKey);
    if (!projectRoot.contains(key))
    {
        configuration = {};
        return true;
    }

    const QJsonValue configurationValue = projectRoot.value(key);
    if (!configurationValue.isObject())
    {
        setValidationError(
            validationResult,
            ProjectRuntimeConfigurationError::InvalidConfigurationObject);
        return false;
    }

    const QJsonObject object = configurationValue.toObject();
    const QJsonValue versionValue = object.value(QStringLiteral("version"));
    if (versionValue.isUndefined())
    {
        setValidationError(
            validationResult,
            ProjectRuntimeConfigurationError::MissingVersion,
            -1,
            QStringLiteral("version"));
        return false;
    }
    int version = 0;
    if (!readJsonInt32(versionValue, version))
    {
        setValidationError(
            validationResult,
            ProjectRuntimeConfigurationError::InvalidVersion,
            -1,
            QStringLiteral("version"));
        return false;
    }
    if (version != ProjectRuntimeConfiguration::schemaVersion)
    {
        setValidationError(
            validationResult,
            ProjectRuntimeConfigurationError::UnsupportedVersion,
            -1,
            QStringLiteral("version"),
            QString::number(version));
        return false;
    }

    ProjectRuntimeConfiguration candidate;
    bool candidateNeedsRepair = false;
    candidate.preservedFields = object;
    candidate.preservedFields.remove(QStringLiteral("version"));
    candidate.preservedFields.remove(QStringLiteral("defaultSceneId"));
    candidate.preservedFields.remove(QStringLiteral("scenes"));

    const QJsonValue scenesValue = object.value(QStringLiteral("scenes"));
    if (scenesValue.isUndefined())
    {
        setValidationError(
            validationResult,
            ProjectRuntimeConfigurationError::InvalidScenesArray,
            -1,
            QStringLiteral("scenes"));
        return false;
    }
    else if (!scenesValue.isArray())
    {
        setValidationError(
            validationResult,
            ProjectRuntimeConfigurationError::InvalidScenesArray,
            -1,
            QStringLiteral("scenes"));
        return false;
    }
    else
    {
        const QJsonArray scenes = scenesValue.toArray();
        candidate.scenes.reserve(scenes.size());
        for (int index = 0; index < scenes.size(); ++index)
        {
            if (!scenes.at(index).isObject())
            {
                setValidationError(
                    validationResult,
                    ProjectRuntimeConfigurationError::InvalidSceneObject,
                    index,
                    QStringLiteral("scenes"));
                return false;
            }

            const QJsonObject sceneObject = scenes.at(index).toObject();
            ProjectScene scene;
            scene.preservedFields = sceneObject;
            scene.preservedFields.remove(QStringLiteral("id"));
            scene.preservedFields.remove(QStringLiteral("name"));
            scene.preservedFields.remove(QStringLiteral("map"));
            scene.preservedFields.remove(QStringLiteral("npc"));
            scene.preservedFields.remove(QStringLiteral("object"));
            scene.preservedFields.remove(QStringLiteral("entryScript"));
            scene.preservedFields.remove(QStringLiteral("playerPosition"));
            scene.preservedFields.remove(QStringLiteral("integerVariables"));
            if (!readRequiredString(
                    sceneObject,
                    QStringLiteral("id"),
                    scene.id,
                    ProjectRuntimeConfigurationError::MissingSceneId,
                    index,
                    validationResult) ||
                !readRequiredString(
                    sceneObject,
                    QStringLiteral("name"),
                    scene.name,
                    ProjectRuntimeConfigurationError::MissingSceneName,
                    index,
                    validationResult) ||
                !readRequiredString(
                    sceneObject,
                    QStringLiteral("map"),
                    scene.mapPath,
                    ProjectRuntimeConfigurationError::MissingSceneMap,
                    index,
                    validationResult) ||
                !readOptionalString(
                    sceneObject,
                    QStringLiteral("npc"),
                    scene.npcPath,
                    index,
                    validationResult) ||
                !readOptionalString(
                    sceneObject,
                    QStringLiteral("object"),
                    scene.objectPath,
                    index,
                    validationResult) ||
                !readOptionalString(
                    sceneObject,
                    QStringLiteral("entryScript"),
                    scene.entryScriptPath,
                    index,
                    validationResult))
            {
                return false;
            }

            const QJsonValue positionValue = sceneObject.value(
                QStringLiteral("playerPosition"));
            if (positionValue.isUndefined())
            {
                scene.playerPosition = QPoint(0, 0);
                candidateNeedsRepair = true;
            }
            else if (!positionValue.isArray())
            {
                setValidationError(
                    validationResult,
                    ProjectRuntimeConfigurationError::InvalidPlayerPosition,
                    index,
                    QStringLiteral("playerPosition"));
                return false;
            }
            else
            {
                const QJsonArray position = positionValue.toArray();
                int x = 0;
                int y = 0;
                if (position.size() != 2 ||
                    !readJsonInt32(position.at(0), x) ||
                    !readJsonInt32(position.at(1), y))
                {
                    setValidationError(
                        validationResult,
                        ProjectRuntimeConfigurationError::InvalidPlayerPosition,
                        index,
                        QStringLiteral("playerPosition"));
                    return false;
                }
                scene.playerPosition = QPoint(x, y);
            }

            const QJsonValue variablesValue = sceneObject.value(
                QStringLiteral("integerVariables"));
            if (variablesValue.isUndefined())
            {
                candidateNeedsRepair = true;
            }
            else if (!variablesValue.isObject())
            {
                setValidationError(
                    validationResult,
                    ProjectRuntimeConfigurationError::InvalidIntegerVariables,
                    index,
                    QStringLiteral("integerVariables"));
                return false;
            }
            else
            {
                const QJsonObject variables = variablesValue.toObject();
                for (auto variable = variables.begin();
                     variable != variables.end();
                     ++variable)
                {
                    int integerValue = 0;
                    if (variable.key().trimmed().isEmpty())
                    {
                        setValidationError(
                            validationResult,
                            ProjectRuntimeConfigurationError::InvalidVariableName,
                            index,
                            QStringLiteral("integerVariables"),
                            variable.key());
                        return false;
                    }
                    if (!readJsonInt32(variable.value(), integerValue))
                    {
                        setValidationError(
                            validationResult,
                            ProjectRuntimeConfigurationError::InvalidIntegerVariables,
                            index,
                            QStringLiteral("integerVariables"),
                            variable.key());
                        return false;
                    }
                    scene.integerVariables.insert(
                        variable.key(), integerValue);
                }
            }

            candidate.scenes.append(scene);
        }
    }

    const QJsonValue defaultSceneValue = object.value(
        QStringLiteral("defaultSceneId"));
    if (defaultSceneValue.isUndefined())
    {
        if (!candidate.scenes.isEmpty())
        {
            candidate.defaultSceneId = candidate.scenes.front().id;
            candidateNeedsRepair = true;
        }
    }
    else if (!defaultSceneValue.isString())
    {
        setValidationError(
            validationResult,
            ProjectRuntimeConfigurationError::InvalidFieldType,
            -1,
            QStringLiteral("defaultSceneId"));
        return false;
    }
    else
    {
        candidate.defaultSceneId = defaultSceneValue.toString();
    }

    bool normalized = false;
    if (!normalizeProjectRuntimeConfiguration(
            candidate, normalized, validationResult))
    {
        return false;
    }
    repaired = candidateNeedsRepair || normalized;
    configuration = candidate;
    return true;
}

bool normalizeProjectRuntimeConfiguration(
    ProjectRuntimeConfiguration& configuration,
    bool& repaired,
    ProjectRuntimeConfigurationValidationResult* validationResult)
{
    if (validationResult)
        *validationResult = {};

    ProjectRuntimeConfiguration candidate = configuration;
    bool changed = false;
    for (int index = 0; index < candidate.scenes.size(); ++index)
    {
        ProjectScene& scene = candidate.scenes[index];
        if (!normalizeResourcePath(
                scene.mapPath,
                true,
                index,
                QStringLiteral("map"),
                changed,
                validationResult) ||
            !normalizeResourcePath(
                scene.npcPath,
                false,
                index,
                QStringLiteral("npc"),
                changed,
                validationResult) ||
            !normalizeResourcePath(
                scene.objectPath,
                false,
                index,
                QStringLiteral("object"),
                changed,
                validationResult) ||
            !normalizeResourcePath(
                scene.entryScriptPath,
                false,
                index,
                QStringLiteral("entryScript"),
                changed,
                validationResult))
        {
            return false;
        }
    }

    const ProjectRuntimeConfigurationValidationResult result =
        validateNormalizedConfiguration(candidate);
    if (!result.isValid())
    {
        if (validationResult)
            *validationResult = result;
        return false;
    }

    configuration = candidate;
    repaired = changed;
    return true;
}

ProjectRuntimeConfigurationValidationResult validateProjectRuntimeConfiguration(
    const ProjectRuntimeConfiguration& configuration)
{
    ProjectRuntimeConfiguration candidate = configuration;
    bool repaired = false;
    ProjectRuntimeConfigurationValidationResult result;
    if (!normalizeProjectRuntimeConfiguration(candidate, repaired, &result))
        return result;
    return {};
}

QJsonObject projectRuntimeConfigurationToJson(
    const ProjectRuntimeConfiguration& configuration)
{
    QJsonObject object = configuration.preservedFields;
    object[QStringLiteral("version")] =
        ProjectRuntimeConfiguration::schemaVersion;
    if (configuration.defaultSceneId.isEmpty())
        object.remove(QStringLiteral("defaultSceneId"));
    else
        object[QStringLiteral("defaultSceneId")] =
            configuration.defaultSceneId;

    QJsonArray scenes;
    for (const ProjectScene& scene : configuration.scenes)
    {
        QJsonObject sceneObject = scene.preservedFields;
        sceneObject[QStringLiteral("id")] = scene.id;
        sceneObject[QStringLiteral("name")] = scene.name;
        sceneObject[QStringLiteral("map")] = scene.mapPath;
        if (scene.npcPath.isEmpty())
            sceneObject.remove(QStringLiteral("npc"));
        else
            sceneObject[QStringLiteral("npc")] = scene.npcPath;
        if (scene.objectPath.isEmpty())
            sceneObject.remove(QStringLiteral("object"));
        else
            sceneObject[QStringLiteral("object")] = scene.objectPath;
        if (scene.entryScriptPath.isEmpty())
            sceneObject.remove(QStringLiteral("entryScript"));
        else
            sceneObject[QStringLiteral("entryScript")] =
                scene.entryScriptPath;

        QJsonArray position;
        position.append(scene.playerPosition.x());
        position.append(scene.playerPosition.y());
        sceneObject[QStringLiteral("playerPosition")] = position;

        QJsonObject variables;
        for (auto variable = scene.integerVariables.cbegin();
             variable != scene.integerVariables.cend();
             ++variable)
        {
            variables[variable.key()] = variable.value();
        }
        sceneObject[QStringLiteral("integerVariables")] = variables;
        scenes.append(sceneObject);
    }
    object[QStringLiteral("scenes")] = scenes;
    return object;
}
