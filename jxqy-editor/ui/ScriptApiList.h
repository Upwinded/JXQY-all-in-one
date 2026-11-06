#pragma once

#include "../core/ScriptCallTemplate.h"

#include <QString>
#include <QVector>

struct ApiInfo
{
    QString signature;
    QString description;
    QString tooltip;
    bool hasParameterForm = false;
    ScriptCallDefinition parameterForm;
};

QString defaultScriptApiConfigurationPath();
QString projectScriptApiConfigurationPath();
QVector<ApiInfo> loadScriptApiList(
    const QString& defaultPath,
    const QString& projectPath,
    QString* diagnostic = nullptr);
QVector<ApiInfo> buildScriptApiList(QString* diagnostic = nullptr);
