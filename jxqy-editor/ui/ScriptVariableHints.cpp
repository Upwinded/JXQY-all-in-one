#include "ScriptVariableHints.h"
#include "../core/ProjectManager.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QLoggingCategory>

QString ScriptVariableInfo::buildTooltip() const
{
    if (name.isEmpty() && description.isEmpty() && type.isEmpty() && notes.isEmpty() && values.isEmpty())
    {
        return QString();
    }

    QString html;
    html += QStringLiteral("<b>%1</b>").arg(name.toHtmlEscaped());

    if (!type.isEmpty() || !group.isEmpty())
    {
        QStringList parts;
        if (!type.isEmpty())
            parts << type.toHtmlEscaped();
        if (!group.isEmpty())
            parts << QCoreApplication::translate("ScriptVariableHints", "分组: %1")
                .arg(group.toHtmlEscaped());
        html += QStringLiteral(" <span style='color:#888;'>[%1]</span>").arg(parts.join(" | "));
    }

    if (!description.isEmpty())
    {
        html += QStringLiteral("<br>%1").arg(description.toHtmlEscaped());
    }

    if (!values.isEmpty())
    {
        html += QCoreApplication::translate("ScriptVariableHints", "<br><span style='color:#569cd6;'>取值:</span> %1")
            .arg(values.join(QStringLiteral(", ")).toHtmlEscaped());
    }

    if (!notes.isEmpty())
    {
        html += QStringLiteral("<br><i>%1</i>").arg(notes.toHtmlEscaped());
    }

    return html;
}

QString ScriptVariableHints::globalDefaultPath()
{
    QString configPath = qApp->property("configFilePath").toString();
    if (configPath.isEmpty())
    {
        configPath = QFileInfo(qApp->applicationFilePath()).absolutePath() + "/editor_config.ini";
    }
    QString configDir = QFileInfo(configPath).absolutePath();
    return QDir(configDir).absoluteFilePath(".jxqy_editor/script_variables.default.json");
}

QString ScriptVariableHints::projectLevelPath()
{
    ProjectManager& project = ProjectManager::instance();

    QString projectFile = project.projectFilePath();
    if (!projectFile.isEmpty())
    {
        QString projectDir = QFileInfo(projectFile).absolutePath();
        return QDir(projectDir).absoluteFilePath(".jxqy_editor/script_variables.json");
    }

    QString assets = project.editableAssetsRoot();
    if (!assets.isEmpty())
    {
        return QDir(assets).absoluteFilePath(".jxqy_editor/script_variables.json");
    }

    return QString();
}

void ScriptVariableHints::mergeEntry(const ScriptVariableInfo& entry)
{
    if (entry.name.isEmpty())
        return;
    variableMap[entry.name] = entry;
}

namespace
{

/// 从单个变量描述对象构造条目。name 为空时由调用方决定是否填入键名。
ScriptVariableInfo parseVariableObject(const QJsonObject& obj)
{
    ScriptVariableInfo entry;
    entry.name = obj.value("name").toString();
    entry.type = obj.value("type").toString();
    entry.group = obj.value("group").toString();
    entry.description = obj.value("description").toString();
    entry.notes = obj.value("notes").toString();
    if (obj.value("values").isArray())
    {
        const QJsonArray valuesArray = obj.value("values").toArray();
        for (const QJsonValue& v : valuesArray)
        {
            entry.values.append(v.toString());
        }
    }
    return entry;
}

} // anonymous namespace

void ScriptVariableHints::loadSingleFile(const QString& path)
{
    QFileInfo info(path);
    if (!info.exists() || !info.isFile())
    {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning("ScriptVariableHints: 无法读取变量提示文件 %s",
                 path.toUtf8().constData());
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || (!doc.isObject() && !doc.isArray()))
    {
        qWarning("ScriptVariableHints: 变量提示文件 %s JSON 解析失败: %s",
                 path.toUtf8().constData(),
                 parseError.errorString().toUtf8().constData());
        return;
    }

    // 形态 1：顶层即为变量对象数组 [{ "name": ... }, ...]。
    if (doc.isArray())
    {
        const QJsonArray array = doc.array();
        for (const QJsonValue& value : array)
        {
            if (!value.isObject())
                continue;
            mergeEntry(parseVariableObject(value.toObject()));
        }
        return;
    }

    QJsonObject root = doc.object();

    // 形态 2：对象内 "variables" 数组。
    if (root.contains("variables") && root.value("variables").isArray())
    {
        const QJsonArray array = root.value("variables").toArray();
        for (const QJsonValue& value : array)
        {
            if (!value.isObject())
                continue;
            mergeEntry(parseVariableObject(value.toObject()));
        }
        return;
    }

    // 形态 3：对象以变量名为键。
    for (auto it = root.begin(); it != root.end(); ++it)
    {
        if (!it.value().isObject())
            continue;
        ScriptVariableInfo entry = parseVariableObject(it.value().toObject());
        if (entry.name.isEmpty())
        {
            entry.name = it.key();
        }
        mergeEntry(entry);
    }
}

void ScriptVariableHints::reload()
{
    variableMap.clear();

    // 1. 全局默认
    loadSingleFile(globalDefaultPath());

    // 2. 项目级（同名覆盖全局默认）
    QString projectPath = projectLevelPath();
    if (!projectPath.isEmpty())
    {
        loadSingleFile(projectPath);
    }
}

QVector<ScriptVariableInfo> ScriptVariableHints::variables() const
{
    QVector<ScriptVariableInfo> result;
    result.reserve(variableMap.size());
    for (auto it = variableMap.begin(); it != variableMap.end(); ++it)
    {
        result.append(it.value());
    }
    return result;
}

QStringList ScriptVariableHints::names() const
{
    QStringList result;
    result.reserve(variableMap.size());
    for (auto it = variableMap.begin(); it != variableMap.end(); ++it)
    {
        result.append(it.key());
    }
    return result;
}

const ScriptVariableInfo* ScriptVariableHints::find(const QString& name) const
{
    auto it = variableMap.find(name);
    if (it != variableMap.end())
    {
        return &it.value();
    }
    return nullptr;
}
