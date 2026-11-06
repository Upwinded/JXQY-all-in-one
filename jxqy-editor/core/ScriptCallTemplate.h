#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

enum class ScriptCallArgumentType
{
    String,
    Integer
};

struct ScriptCallArgumentDefinition
{
    QString id;
    QString label;
    QString description;
    ScriptCallArgumentType type = ScriptCallArgumentType::String;
    QString defaultValue;
    QString resourceDirectory;
    QStringList resourceFilters;
};

struct ScriptCallDefinition
{
    QString displayName;
    QString runtimeName;
    QString description;
    QList<ScriptCallArgumentDefinition> arguments;

    QString signature() const;
};

struct ScriptCallBuildResult
{
    bool success = false;
    QString call;
    QString error;
};

class ScriptCallTemplate
{
public:
    static QList<ScriptCallDefinition> definitions();
    static bool findDefinition(const QString& apiName,
                               ScriptCallDefinition& definition);
    static ScriptCallBuildResult build(
        const QString& apiName,
        const QMap<QString, QString>& argumentValues);
    static ScriptCallBuildResult build(const ScriptCallDefinition& definition,
        const QMap<QString, QString>& argumentValues);
};
