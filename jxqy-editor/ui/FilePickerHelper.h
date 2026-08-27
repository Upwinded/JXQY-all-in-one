#pragma once

#include <QString>
#include <QStringList>
#include <functional>

#include "../core/GameProfile.h"

class QLineEdit;
class QWidget;
class QFormLayout;

namespace FilePickerHelper
{

enum class EntityResourceField
{
    NpcIni,
    ObjFile,
    ScriptFile,
    BodyIni,
    FlyIni,
    FlyInis,
    MagicIni,
    WavFile,
    ReviveNpcIni,
    DropIni
};

struct EntityResourceSelection
{
    QString resourceReference;
    QString sourceFilePath;
    QString sourceRootPath;
    QString localRootPath;
    QString localOverridePath;
    QString effectiveFilePath;
    bool inherited = false;
    bool referenceResolvesToSource = false;
};

/// 按运行时字段 schema 校验并规范手工输入的实体资源引用。
/// 接受业务目录前缀或运行时相对名，输出统一使用反斜杠；脚本字段只保留文件名。
bool normalizeEntityResourceReference(EntityResourceField field,
                                      const QString& input,
                                      QString& output);

/// 返回字段 schema 声明的运行时业务目录（相对于 assets 根）。
QStringList entityResourceFolders(EntityResourceField field);

/// 校验多根候选，并计算引用在运行时业务目录顺序下实际解析到的文件。
/// referenceResolvesToSource=false 表示候选被同名资源遮蔽，只能复制为本地覆盖。
bool inspectEntityResourceSelection(
    const QString& activeAssetsPath,
    const QList<ResourceContentRoot>& orderedRoots,
    EntityResourceField field,
    const QString& selectedFilePath,
    EntityResourceSelection& selection);

/// 将候选流式、原子地复制到活动包首选业务目录。
/// overwrite=false 时拒绝覆盖已存在的本地文件。
bool copyEntityResourceToLocalOverride(const EntityResourceSelection& selection,
                                       bool overwrite,
                                       QString& errorMessage);

/// 将选中的绝对路径序列化成运行时业务目录内的相对资源名。
/// 按逻辑路径校验 assets containment；正式资源中的 junction/symlink
/// 后代会在读取时跟随，但生成的资源名仍禁止绝对路径和 ".."。
bool makeResourceReference(const QString& assetsBasePath,
                           const QString& resourceBasePath,
                           const QString& selectedFilePath,
                           QString& resourceReference,
                           bool fileNameOnly = false);

/// 在多个等价运行时业务目录中查找选中文件并生成相对资源名。
bool makeResourceReference(const QString& assetsBasePath,
                           const QStringList& resourceBasePaths,
                           const QString& selectedFilePath,
                           QString& resourceReference,
                           bool fileNameOnly = false,
                           bool alternateRootsDirectFilesOnly = false);

/// 按实体字段 schema 添加文件选择按钮，使 picker 与手工输入共用目录和格式规则。
void addEntityResourcePickerButton(QLineEdit* fileEdit, const QString& filter,
                                   std::function<QString()> assetsBasePathGetter,
                                   EntityResourceField field, QWidget* parent,
                                   std::function<QString()> defaultDirGetter = nullptr);

void addEntityResourcePickerButton(QLineEdit* fileEdit, QFormLayout* formLayout,
                                   const QString& filter,
                                   std::function<QString()> assetsBasePathGetter,
                                   EntityResourceField field, QWidget* parent,
                                   std::function<QString()> defaultDirGetter = nullptr);

/// 字段类型会随编辑对象变化时使用（如同一输入框在 NPCIni/ObjFile 间切换）。
void addEntityResourcePickerButton(
    QLineEdit* fileEdit, QFormLayout* formLayout, const QString& filter,
    std::function<QString()> assetsBasePathGetter,
    std::function<EntityResourceField()> fieldGetter, QWidget* parent,
    std::function<QString()> defaultDirGetter = nullptr);

/// 为 FlyInis 等列表字段添加 schema 驱动的追加选择按钮。
void addEntityResourceAppendPickerButton(QLineEdit* fileEdit, const QString& filter,
                                         std::function<QString()> assetsBasePathGetter,
                                         EntityResourceField field, QWidget* parent);

/// 在 QFormLayout 中为已有的 QLineEdit 添加"..."文件选择按钮。
/// 自动从 fileEdit->parentWidget()->layout() 查找 QFormLayout。
/// @param fileEdit        目标 QLineEdit（必须已添加到 QFormLayout 的 FieldRole）
/// @param filter          QFileDialog 的文件过滤器
/// @param assetsBasePathGetter 返回当前 assetsBasePath 的回调，用于延迟获取最新路径
/// @param restrictToAssets 为 true 时，选择 assets 外文件会提示并拒绝
/// @param parent          用于 QFileDialog 的父窗口
/// @param defaultDirGetter 返回文件对话框默认打开目录的回调（绝对路径），为空则使用 assetsBasePath
void addFilePickerButton(QLineEdit* fileEdit, const QString& filter,
                         std::function<QString()> assetsBasePathGetter,
                         bool restrictToAssets, QWidget* parent,
                         std::function<QString()> defaultDirGetter = nullptr,
                         std::function<QString()> resourceBasePathGetter = nullptr,
                         bool fileNameOnly = false,
                         std::function<QStringList()> resourceBasePathsGetter = nullptr,
                         bool alternateRootsDirectFilesOnly = false);

/// 在指定 QFormLayout 中为已有的 QLineEdit 添加"..."文件选择按钮。
/// 用于 QLineEdit 的父 widget layout 不是 QFormLayout 的场景（如嵌套布局）。
/// @param fileEdit        目标 QLineEdit
/// @param formLayout      QLineEdit 所在的 QFormLayout
/// @param filter          QFileDialog 的文件过滤器
/// @param assetsBasePathGetter 返回当前 assetsBasePath 的回调
/// @param restrictToAssets 为 true 时，选择 assets 外文件会提示并拒绝
/// @param parent          用于 QFileDialog 的父窗口
/// @param defaultDirGetter 返回文件对话框默认打开目录的回调（绝对路径），为空则使用 assetsBasePath
void addFilePickerButton(QLineEdit* fileEdit, QFormLayout* formLayout,
                         const QString& filter,
                         std::function<QString()> assetsBasePathGetter,
                         bool restrictToAssets, QWidget* parent,
                         std::function<QString()> defaultDirGetter = nullptr,
                         std::function<QString()> resourceBasePathGetter = nullptr,
                         bool fileNameOnly = false,
                         std::function<QStringList()> resourceBasePathsGetter = nullptr,
                         bool alternateRootsDirectFilesOnly = false);

/// 在 QFormLayout 中为已有的 QLineEdit 添加"+..."追加文件选择按钮。
/// 选择结果按 ; 追加到现有内容末尾，不覆盖。
/// @param fileEdit        目标 QLineEdit（必须已添加到 QFormLayout 的 FieldRole）
/// @param filter          QFileDialog 的文件过滤器
/// @param assetsBasePathGetter 返回当前 assetsBasePath 的回调
/// @param parent          用于 QFileDialog 的父窗口
void addAppendFilePickerButton(QLineEdit* fileEdit, const QString& filter,
                               std::function<QString()> assetsBasePathGetter,
                               QWidget* parent,
                               std::function<QString()> resourceBasePathGetter = nullptr);

} // namespace FilePickerHelper
