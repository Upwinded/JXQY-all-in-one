#pragma once

#include <QString>
#include <QStringList>
#include <QMap>
#include <QVector>

/// 剧情变量提示条目。仅作为脚本编辑辅助，不参与运行时逻辑或资源完整性校验。
struct ScriptVariableInfo
{
    QString name;
    QString type;
    QString group;
    QString description;
    QStringList values;
    QString notes;

    /// 拼接用于补全/悬停提示的富文本说明。空条目返回空串。
    QString buildTooltip() const;
};

/// 剧情变量提示加载器。
///
/// 加载顺序与覆盖规则（见 jxqy-editor/requirements.md 第 7 节“脚本编辑与转换”）：
///   1. 全局默认：编辑器配置目录下 .jxqy_editor/script_variables.default.json
///   2. 项目级：项目文件所在目录的 .jxqy_editor/script_variables.json，
///      无项目文件时退化为 assets 根目录下的 .jxqy_editor/script_variables.json
/// 同名变量以项目级定义覆盖全局默认。
///
/// 文件缺失或格式错误时不抛出、不阻断脚本编辑，只记录温和日志。
class ScriptVariableHints
{
public:
    /// 重新加载并合并全局默认与项目级变量提示。可在项目打开/切换时调用。
    void reload();

    /// 当前已合并的全部变量条目（按 name 排序）。
    QVector<ScriptVariableInfo> variables() const;

    /// 变量名列表，用于自动补全。
    QStringList names() const;

    /// 按变量名查找条目；不存在返回 nullptr。
    const ScriptVariableInfo* find(const QString& name) const;

    /// 全局默认变量提示文件路径（编辑器配置目录）。
    static QString globalDefaultPath();

    /// 项目级变量提示文件路径（项目文件目录或 assets 根目录）。
    /// 没有项目文件且 assets 路径为空时返回空串。
    static QString projectLevelPath();

private:
    void loadSingleFile(const QString& path);
    void mergeEntry(const ScriptVariableInfo& entry);

    QMap<QString, ScriptVariableInfo> variableMap;
};
