#pragma once

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QPoint>
#include <QString>

struct ProjectScene
{
    QString id;
    QString name;
    QString mapPath;
    QString npcPath;
    QString objectPath;
    QString entryScriptPath;
    QPoint playerPosition;
    QMap<QString, int> integerVariables;
    QJsonObject preservedFields;

    bool operator==(const ProjectScene& other) const
    {
        return id == other.id &&
            name == other.name &&
            mapPath == other.mapPath &&
            npcPath == other.npcPath &&
            objectPath == other.objectPath &&
            entryScriptPath == other.entryScriptPath &&
            playerPosition == other.playerPosition &&
            integerVariables == other.integerVariables &&
            preservedFields == other.preservedFields;
    }
};

struct ProjectRuntimeConfiguration
{
    static constexpr int schemaVersion = 1;

    QString defaultSceneId;
    QList<ProjectScene> scenes;
    QJsonObject preservedFields;

    bool operator==(const ProjectRuntimeConfiguration& other) const
    {
        return defaultSceneId == other.defaultSceneId &&
            scenes == other.scenes &&
            preservedFields == other.preservedFields;
    }
};

enum class ProjectRuntimeConfigurationError
{
    None,
    InvalidConfigurationObject,
    MissingVersion,
    InvalidVersion,
    UnsupportedVersion,
    InvalidScenesArray,
    InvalidSceneObject,
    InvalidFieldType,
    MissingSceneId,
    MissingSceneName,
    MissingSceneMap,
    UnsafeResourcePath,
    InvalidPlayerPosition,
    InvalidIntegerVariables,
    InvalidVariableName,
    DuplicateSceneId,
    InvalidDefaultSceneId
};

struct ProjectRuntimeConfigurationValidationResult
{
    ProjectRuntimeConfigurationError error =
        ProjectRuntimeConfigurationError::None;
    int sceneIndex = -1;
    QString fieldName;
    QString value;

    bool isValid() const
    {
        return error == ProjectRuntimeConfigurationError::None;
    }
};

// Reads the optional runtimeConfiguration section from a project root.
// A missing section is a compatible empty v1 configuration. A present section
// must have the exact supported version. The output is assigned only on success.
bool readProjectRuntimeConfiguration(
    const QJsonObject& projectRoot,
    ProjectRuntimeConfiguration& configuration,
    bool& repaired,
    ProjectRuntimeConfigurationValidationResult* validationResult = nullptr);

// Normalizes path separators and explicit repairable defaults, then validates
// the complete cross-field contract. The input is changed only on success.
bool normalizeProjectRuntimeConfiguration(
    ProjectRuntimeConfiguration& configuration,
    bool& repaired,
    ProjectRuntimeConfigurationValidationResult* validationResult = nullptr);

ProjectRuntimeConfigurationValidationResult validateProjectRuntimeConfiguration(
    const ProjectRuntimeConfiguration& configuration);

QJsonObject projectRuntimeConfigurationToJson(
    const ProjectRuntimeConfiguration& configuration);
