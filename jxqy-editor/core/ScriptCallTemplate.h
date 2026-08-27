#pragma once

#include <QList>
#include <QMap>
#include <QString>

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
};
