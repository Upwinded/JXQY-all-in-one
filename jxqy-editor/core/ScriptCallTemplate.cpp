#include "ScriptCallTemplate.h"

#include <QCoreApplication>
#include <QStringList>
#include <QRegularExpression>

#include <limits>

namespace
{
QString translated(const char* sourceText)
{
    return QCoreApplication::translate("ScriptCallTemplate", sourceText);
}

ScriptCallArgumentDefinition stringArgument(
    const QString& id, const char* label, const char* description)
{
    ScriptCallArgumentDefinition argument;
    argument.id = id;
    argument.label = translated(label);
    argument.description = translated(description);
    argument.type = ScriptCallArgumentType::String;
    return argument;
}

ScriptCallArgumentDefinition integerArgument(
    const QString& id, const char* label, const char* description,
    const QString& defaultValue)
{
    ScriptCallArgumentDefinition argument;
    argument.id = id;
    argument.label = translated(label);
    argument.description = translated(description);
    argument.type = ScriptCallArgumentType::Integer;
    argument.defaultValue = defaultValue;
    return argument;
}

bool hasValidUtf16(const QString& value)
{
    for (qsizetype index = 0; index < value.size(); ++index)
    {
        const QChar character = value[index];
        if (character.isHighSurrogate())
        {
            if (index + 1 >= value.size() ||
                !value[index + 1].isLowSurrogate())
            {
                return false;
            }
            ++index;
        }
        else if (character.isLowSurrogate())
        {
            return false;
        }
    }
    return true;
}

QString escapeLuaString(const QString& value)
{
    QString escaped;
    escaped.reserve(value.size() + 2);
    escaped.append('"');
    for (const QChar character : value)
    {
        const ushort code = character.unicode();
        switch (code)
        {
        case '\\': escaped.append(QStringLiteral("\\\\")); break;
        case '"': escaped.append(QStringLiteral("\\\"")); break;
        case '\a': escaped.append(QStringLiteral("\\a")); break;
        case '\b': escaped.append(QStringLiteral("\\b")); break;
        case '\t': escaped.append(QStringLiteral("\\t")); break;
        case '\n': escaped.append(QStringLiteral("\\n")); break;
        case '\v': escaped.append(QStringLiteral("\\v")); break;
        case '\f': escaped.append(QStringLiteral("\\f")); break;
        case '\r': escaped.append(QStringLiteral("\\r")); break;
        default:
            if (code < 0x20 || code == 0x7F)
            {
                escaped.append(QStringLiteral("\\%1")
                    .arg(code, 3, 10, QChar('0')));
            }
            else
            {
                escaped.append(character);
            }
            break;
        }
    }
    escaped.append('"');
    return escaped;
}
}

QString ScriptCallDefinition::signature() const
{
    QStringList argumentNames;
    argumentNames.reserve(arguments.size());
    for (const ScriptCallArgumentDefinition& argument : arguments)
        argumentNames.append(argument.id);
    return displayName + '(' + argumentNames.join(QStringLiteral(", ")) + ')';
}

QList<ScriptCallDefinition> ScriptCallTemplate::definitions()
{
    QList<ScriptCallDefinition> result;

    ScriptCallDefinition assign;
    assign.displayName = QStringLiteral("Assign");
    assign.runtimeName = QStringLiteral("assign");
    assign.description = translated(QT_TRANSLATE_NOOP(
        "ScriptCallTemplate", "给剧情变量赋整数值"));
    assign.arguments = {
        stringArgument(QStringLiteral("varName"),
            QT_TRANSLATE_NOOP("ScriptCallTemplate", "变量名"),
            QT_TRANSLATE_NOOP("ScriptCallTemplate",
                "运行时剧情变量名，按字符串字面量插入")),
        integerArgument(QStringLiteral("value"),
            QT_TRANSLATE_NOOP("ScriptCallTemplate", "整数值"),
            QT_TRANSLATE_NOOP("ScriptCallTemplate",
                "写入变量的十进制 32 位整数"), QStringLiteral("0"))};
    result.append(assign);

    ScriptCallDefinition add;
    add.displayName = QStringLiteral("Add");
    add.runtimeName = QStringLiteral("add");
    add.description = translated(QT_TRANSLATE_NOOP(
        "ScriptCallTemplate", "增加剧情变量的整数值"));
    add.arguments = {
        stringArgument(QStringLiteral("varName"),
            QT_TRANSLATE_NOOP("ScriptCallTemplate", "变量名"),
            QT_TRANSLATE_NOOP("ScriptCallTemplate",
                "运行时剧情变量名，按字符串字面量插入")),
        integerArgument(QStringLiteral("value"),
            QT_TRANSLATE_NOOP("ScriptCallTemplate", "增加值"),
            QT_TRANSLATE_NOOP("ScriptCallTemplate",
                "可为负数的十进制 32 位整数"), QStringLiteral("1"))};
    result.append(add);

    ScriptCallDefinition say;
    say.displayName = QStringLiteral("Say");
    say.runtimeName = QStringLiteral("say");
    say.description = translated(QT_TRANSLATE_NOOP(
        "ScriptCallTemplate", "显示一段直接对话"));
    say.arguments = {
        stringArgument(QStringLiteral("text"),
            QT_TRANSLATE_NOOP("ScriptCallTemplate", "对话文本"),
            QT_TRANSLATE_NOOP("ScriptCallTemplate",
                "显示的 UTF-8 文本，按字符串字面量插入")),
        integerArgument(QStringLiteral("portraitIndex"),
            QT_TRANSLATE_NOOP("ScriptCallTemplate", "头像索引"),
            QT_TRANSLATE_NOOP("ScriptCallTemplate",
                "头像索引，-1 表示不显示头像"), QStringLiteral("-1"))};
    result.append(say);

    ScriptCallDefinition playSound;
    playSound.displayName = QStringLiteral("PlaySound");
    playSound.runtimeName = QStringLiteral("playsound");
    playSound.description = translated(QT_TRANSLATE_NOOP(
        "ScriptCallTemplate", "播放一个运行时音效"));
    playSound.arguments = {
        stringArgument(QStringLiteral("fileName"),
            QT_TRANSLATE_NOOP("ScriptCallTemplate", "音效文件名"),
            QT_TRANSLATE_NOOP("ScriptCallTemplate",
                "运行时 sound 目录下的文件名或相对路径"))};
    result.append(playSound);

    ScriptCallDefinition runScript;
    runScript.displayName = QStringLiteral("RunScript");
    runScript.runtimeName = QStringLiteral("runscript");
    runScript.description = translated(QT_TRANSLATE_NOOP(
        "ScriptCallTemplate", "运行另一个脚本文件"));
    runScript.arguments = {
        stringArgument(QStringLiteral("fileName"),
            QT_TRANSLATE_NOOP("ScriptCallTemplate", "脚本文件名"),
            QT_TRANSLATE_NOOP("ScriptCallTemplate",
                "运行时按当前地图、goods 和 common 顺序查找的文件名"))};
    result.append(runScript);

    return result;
}

bool ScriptCallTemplate::findDefinition(
    const QString& apiName, ScriptCallDefinition& definition)
{
    const QString requestedName = apiName.trimmed();
    for (const ScriptCallDefinition& candidate : definitions())
    {
        if (candidate.displayName.compare(
                requestedName, Qt::CaseInsensitive) == 0 ||
            candidate.runtimeName.compare(
                requestedName, Qt::CaseInsensitive) == 0)
        {
            definition = candidate;
            return true;
        }
    }
    return false;
}

ScriptCallBuildResult ScriptCallTemplate::build(
    const QString& apiName,
    const QMap<QString, QString>& argumentValues)
{
    ScriptCallBuildResult result;
    ScriptCallDefinition definition;
    if (!findDefinition(apiName, definition))
    {
        result.error = translated(QT_TRANSLATE_NOOP(
            "ScriptCallTemplate", "不支持的参数化 API：%1")).arg(apiName);
        return result;
    }
    if (argumentValues.size() != definition.arguments.size())
    {
        result.error = translated(QT_TRANSLATE_NOOP(
            "ScriptCallTemplate", "参数数量与固定调用定义不一致"));
        return result;
    }

    static const QRegularExpression decimalPattern(
        QStringLiteral("^[+-]?[0-9]+$"));
    QStringList generatedArguments;
    generatedArguments.reserve(definition.arguments.size());
    for (const ScriptCallArgumentDefinition& argument : definition.arguments)
    {
        if (!argumentValues.contains(argument.id))
        {
            result.error = translated(QT_TRANSLATE_NOOP(
                "ScriptCallTemplate", "缺少参数：%1")).arg(argument.label);
            return result;
        }
        const QString value = argumentValues.value(argument.id);
        if (argument.type == ScriptCallArgumentType::String)
        {
            if (value.trimmed().isEmpty())
            {
                result.error = translated(QT_TRANSLATE_NOOP(
                    "ScriptCallTemplate", "参数不能为空：%1"))
                    .arg(argument.label);
                return result;
            }
            if (!hasValidUtf16(value))
            {
                result.error = translated(QT_TRANSLATE_NOOP(
                    "ScriptCallTemplate", "参数包含无效 Unicode：%1"))
                    .arg(argument.label);
                return result;
            }
            generatedArguments.append(escapeLuaString(value));
            continue;
        }

        const QString trimmed = value.trimmed();
        bool integerOk = false;
        const qlonglong integerValue = trimmed.toLongLong(&integerOk, 10);
        if (!decimalPattern.match(trimmed).hasMatch() || !integerOk ||
            integerValue < std::numeric_limits<qint32>::min() ||
            integerValue > std::numeric_limits<qint32>::max())
        {
            result.error = translated(QT_TRANSLATE_NOOP(
                "ScriptCallTemplate", "参数必须是十进制 32 位整数：%1"))
                .arg(argument.label);
            return result;
        }
        generatedArguments.append(QString::number(integerValue));
    }

    result.success = true;
    result.call = definition.runtimeName + '(' +
        generatedArguments.join(QStringLiteral(", ")) + QStringLiteral(");");
    return result;
}
