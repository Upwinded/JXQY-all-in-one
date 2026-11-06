#include "ScriptApiList.h"
#include "../core/LuaLexer.h"
#include "../core/EditorAssetPath.h"
#include "../core/ProjectManager.h"
#include "../core/ScriptConverter.h"
#include "../core/TranslationManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QRegularExpression>
#include <QStringList>
#include <climits>

namespace
{
using CommandCatalog = QMap<QString, QJsonObject>;

bool validParameters(const QJsonValue& value)
{
    if (!value.isArray()) return false;
    QSet<QString> ids;
    static const QRegularExpression identifier("^[A-Za-z_][A-Za-z0-9_]*$");
    for (const auto& item : value.toArray())
    {
        if (!item.isObject()) return false;
        const auto parameter = item.toObject();
        const QString id = parameter.value("id").toString();
        const QString type = parameter.value("type").toString();
        if (!identifier.match(id).hasMatch() || ids.contains(id) || (type != "string" && type != "integer")) return false;
        ids.insert(id);
        for (const auto& field : {"label", "description", "resourceDirectory"})
            if (parameter.contains(field) && !parameter.value(field).isString()) return false;
        if (parameter.contains("default"))
        {
            const auto value = parameter.value("default");
            if (type == "string" && !value.isString()) return false;
            if (type == "integer")
            {
                bool valid = false;
                const qlonglong number = value.toVariant().toString().toLongLong(&valid);
                if ((!value.isString() && !value.isDouble()) || !valid || number < INT_MIN || number > INT_MAX) return false;
            }
        }
        if (parameter.contains("labels"))
        {
            if (!parameter.value("labels").isObject()) return false;
            const auto labels = parameter.value("labels").toObject();
            for (auto it = labels.begin(); it != labels.end(); ++it) if (!it.value().isString()) return false;
        }
        const QString folder = parameter.value("resourceDirectory").toString();
        QString normalized;
        if (!folder.isEmpty() && (type != "string" || !EditorAssetPath::normalizeResourcePath(folder, normalized))) return false;
        if (parameter.contains("resourceFilters"))
        {
            if (!parameter.value("resourceFilters").isArray()) return false;
            for (const auto& filter : parameter.value("resourceFilters").toArray())
                if (!filter.isString()) return false;
        }
    }
    return true;
}

bool validDocumentation(const QJsonValue& value)
{
    if (value.isString())
        return true;
    if (!value.isArray())
        return false;
    for (const auto& line : value.toArray())
    {
        if (!line.isString())
            return false;
    }
    return true;
}

bool validTextFields(const QJsonObject& object)
{
    return (!object.contains("description") || object.value("description").isString()) &&
        (!object.contains("documentation") || validDocumentation(object.value("documentation")));
}

bool readCatalog(const QString& path, CommandCatalog& commands, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        error = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        error = QStringLiteral("JSON: %1 (offset %2)").arg(parseError.errorString()).arg(parseError.offset);
        return false;
    }
    const QJsonObject root = document.object();
    if (!document.isObject() || root.value("version") != QJsonValue(1) ||
        !root.value("commands").isArray())
    {
        error = QStringLiteral("version = 1; commands: array");
        return false;
    }

    CommandCatalog parsed;
    static const QRegularExpression identifier(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    const QJsonArray entries = root.value("commands").toArray();
    for (qsizetype index = 0; index < entries.size(); ++index)
    {
        const QJsonObject entry = entries[index].toObject();
        const QString name = entry.value("name").toString();
        QString invalidField;
        if (!identifier.match(name).hasMatch() || LuaLexer::isKeyword(name.toLower()))
            invalidField = "name";
        else if (parsed.contains(name.toLower()))
            invalidField = "name (duplicate)";
        else if (!validTextFields(entry))
            invalidField = "description / documentation";
        else if (entry.contains("parameters") && !validParameters(entry.value("parameters")))
            invalidField = "parameters";
        else if (entry.contains("signature"))
        {
            const QString signature = entry.value("signature").toString();
            if (signature.section('(', 0, 0).trimmed().compare(name, Qt::CaseInsensitive) != 0 ||
                !signature.contains('(') || !signature.endsWith(')'))
                invalidField = "signature";
        }
        if (invalidField.isEmpty() && entry.contains("translations"))
        {
            if (!entry.value("translations").isObject())
                invalidField = "translations";
            const QJsonObject translations = entry.value("translations").toObject();
            for (auto it = translations.begin(); it != translations.end(); ++it)
            {
                if (!it.value().isObject() || !validTextFields(it.value().toObject()))
                {
                    invalidField = "translations." + it.key();
                    break;
                }
            }
        }
        if (!invalidField.isEmpty())
        {
            error = QStringLiteral("commands[%1].%2").arg(index).arg(invalidField);
            return false;
        }
        parsed.insert(name.toLower(), entry);
    }
    commands = parsed;
    return true;
}

QJsonObject mergeCommand(QJsonObject base, const QJsonObject& override)
{
    QJsonObject translations = base.value("translations").toObject();
    // Changed base-language text must not keep a translation of the old text.
    for (const QString& field : {QStringLiteral("description"), QStringLiteral("documentation")})
    {
        if (!override.contains(field))
            continue;
        for (auto it = translations.begin(); it != translations.end(); ++it)
        {
            QJsonObject localized = it.value().toObject();
            localized.remove(field);
            it.value() = localized;
        }
    }
    const QJsonObject localizedOverrides = override.value("translations").toObject();
    for (auto it = localizedOverrides.begin(); it != localizedOverrides.end(); ++it)
    {
        QJsonObject localized = translations.value(it.key()).toObject();
        const QJsonObject fields = it.value().toObject();
        for (auto field = fields.begin(); field != fields.end(); ++field)
            localized.insert(field.key(), field.value());
        translations.insert(it.key(), localized);
    }
    for (auto it = override.begin(); it != override.end(); ++it)
    {
        if (it.key() != "translations")
            base.insert(it.key(), it.value());
    }
    base.insert("translations", translations);
    return base;
}

QString documentationText(const QJsonValue& value)
{
    if (value.isString())
        return value.toString();
    QStringList lines;
    for (const auto& line : value.toArray())
        lines.append(line.toString());
    return lines.join('\n');
}
}

QString defaultScriptApiConfigurationPath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath("config/script_api.json");
}

QString projectScriptApiConfigurationPath()
{
    const auto& project = ProjectManager::instance();
    return project.isProjectOpen()
        ? QFileInfo(project.projectFilePath()).dir().filePath(".jxqy_editor/script_api.json")
        : QString();
}

QVector<ApiInfo> loadScriptApiList(
    const QString& defaultPath, const QString& projectPath, QString* diagnostic)
{
    CommandCatalog commands;
    QString error;
    QStringList errors;
    readCatalog(QStringLiteral(":/config/script_api.json"), commands, error);
    auto readOptional = [&](const QString& path, bool merge)
    {
        if (path.isEmpty() || !QFileInfo::exists(path))
            return;
        CommandCatalog loaded;
        if (!readCatalog(path, loaded, error))
        {
            errors.append(QCoreApplication::translate("ScriptApiList",
                "指令说明未加载：%1\n%2。继续使用其余有效说明。").arg(path, error));
            return;
        }
        if (!merge)
            commands = loaded;
        else
        {
            for (auto it = loaded.begin(); it != loaded.end(); ++it)
                commands.insert(it.key(), mergeCommand(commands.value(it.key()), it.value()));
        }
    };
    readOptional(defaultPath, false);
    readOptional(projectPath, true);

    QVector<ApiInfo> apis;
    const QString locale = TranslationManager::instance().currentLocaleName();
    for (auto it = commands.begin(); it != commands.end(); ++it)
    {
        const QJsonObject entry = it.value();
        const QJsonObject localized = entry.value("translations").toObject().value(locale).toObject();
        ApiInfo info;
        info.signature = entry.value("signature").toString(it.key() + "(...)");
        info.description = localized.value("description").toString(
            entry.value("description").toString());
        info.tooltip = documentationText(localized.contains("documentation")
            ? localized.value("documentation") : entry.value("documentation"));
        if (info.tooltip.isEmpty())
            info.tooltip = info.signature + '\n' + info.description;
        info.hasParameterForm = entry.contains("parameters");
        if (info.hasParameterForm)
        {
            info.parameterForm.displayName = info.signature.section('(', 0, 0).trimmed();
            info.parameterForm.runtimeName = it.key();
            info.parameterForm.description = info.description;
            for (const auto& item : entry.value("parameters").toArray())
            {
                const auto parameter = item.toObject();
                ScriptCallArgumentDefinition argument;
                argument.id = parameter.value("id").toString();
                argument.label = parameter.value("labels").toObject().value(locale).toString(parameter.value("label").toString(argument.id));
                argument.description = parameter.value("description").toString();
                argument.type = parameter.value("type") == QJsonValue("integer") ? ScriptCallArgumentType::Integer : ScriptCallArgumentType::String;
                argument.defaultValue = parameter.contains("default") ? parameter.value("default").toVariant().toString()
                    : argument.type == ScriptCallArgumentType::Integer ? "0" : "";
                EditorAssetPath::normalizeResourcePath(parameter.value("resourceDirectory").toString(), argument.resourceDirectory);
                for (const auto& filter : parameter.value("resourceFilters").toArray()) argument.resourceFilters.append(filter.toString());
                info.parameterForm.arguments.append(argument);
            }
        }
        apis.append(info);
    }
    // Old registered commands remain discoverable even in a partial default file.
    // Configuration entries are deliberately not restricted to this compiled list.
    for (const std::string& runtimeName : ScriptConverter::runtimeApiNames())
    {
        const QString name = QString::fromStdString(runtimeName);
        if (commands.contains(name))
            continue;
        apis.append({name + "(...)",
            QCoreApplication::translate("ScriptApiList", "运行时脚本 API"),
            QCoreApplication::translate("ScriptApiList",
                "%1(...)\n该名称已由当前 C++ 运行时注册；参数和返回约定请结合脚本接口实现或现有脚本样本确认。")
                .arg(name)});
    }
    if (diagnostic)
        *diagnostic = errors.join("\n\n");
    return apis;
}

QVector<ApiInfo> buildScriptApiList(QString* diagnostic)
{
    return loadScriptApiList(defaultScriptApiConfigurationPath(),
        projectScriptApiConfigurationPath(), diagnostic);
}
