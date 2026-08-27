#include "FilePickerHelper.h"
#include "../core/AuthoringMutationGate.h"
#include "../core/EditorAssetPath.h"

#include <QCoreApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QSaveFile>
#include <QVBoxLayout>
#include <utility>

namespace FilePickerHelper
{

namespace
{

struct EntityResourceRule
{
    QStringList allowedFolders;
    bool fileNameOnly = false;
    bool alternateFoldersDirectFilesOnly = false;
    bool list = false;
};

EntityResourceRule entityResourceRule(EntityResourceField field)
{
    switch (field)
    {
    case EntityResourceField::NpcIni:
        return {{QStringLiteral("ini/npcres"), QStringLiteral("ini/npc")},
                false, true, false};
    case EntityResourceField::ObjFile:
        return {{QStringLiteral("ini/objres")}, false, false, false};
    case EntityResourceField::ScriptFile:
        return {{QStringLiteral("script")}, true, false, false};
    case EntityResourceField::BodyIni:
    case EntityResourceField::DropIni:
        return {{QStringLiteral("ini/obj")}, false, false, false};
    case EntityResourceField::FlyIni:
    case EntityResourceField::MagicIni:
        return {{QStringLiteral("ini/magic")}, false, false, false};
    case EntityResourceField::FlyInis:
        return {{QStringLiteral("ini/magic")}, false, false, true};
    case EntityResourceField::WavFile:
        return {{QStringLiteral("sound")}, false, false, false};
    case EntityResourceField::ReviveNpcIni:
        return {{QStringLiteral("ini/npc")}, false, false, false};
    }
    return {};
}

QStringList entityResourceBasePaths(const QString& assetsBasePath,
                                    EntityResourceField field)
{
    QStringList paths;
    for (const QString& folder : entityResourceRule(field).allowedFolders)
        paths.append(QDir(assetsBasePath).filePath(folder));
    return paths;
}

QString entityResourceDefaultPath(const QString& assetsBasePath,
                                  EntityResourceField field)
{
    const QStringList paths = entityResourceBasePaths(assetsBasePath, field);
    for (const QString& path : paths)
    {
        if (QDir(path).exists())
            return path;
    }
    return paths.isEmpty() ? QString() : paths.front();
}

QString resourceRootLabel(const ResourceContentRoot& root, int readOnlyIndex)
{
    const QString identity = !root.name.trimmed().isEmpty()
        ? root.name.trimmed()
        : (!root.id.trimmed().isEmpty() ? root.id.trimmed()
                                        : QFileInfo(root.rootPath).fileName());
    switch (root.kind)
    {
    case ResourceContentRoot::Kind::Local:
        return QCoreApplication::translate("FilePickerHelper", "本地资源 — %1")
            .arg(identity);
    case ResourceContentRoot::Kind::Common:
        return QCoreApplication::translate("FilePickerHelper", "只读共享资源 — %1")
            .arg(identity);
    case ResourceContentRoot::Kind::DependencyId:
        return QCoreApplication::translate("FilePickerHelper", "只读依赖 %1 — %2")
            .arg(readOnlyIndex).arg(identity);
    }
    return identity;
}

int chooseEntityResourceRoot(QWidget* parent,
                             const ResourceContentRootResolution& resolution)
{
    QList<int> availableIndices;
    for (int i = 0; i < resolution.roots.size(); ++i)
    {
        if (resolution.roots[i].available)
            availableIndices.append(i);
    }
    if (availableIndices.isEmpty())
        return -1;
    if (availableIndices.size() == 1 && resolution.missingDependencyIds.isEmpty() &&
        resolution.missingPaths.isEmpty())
    {
        return availableIndices.front();
    }

    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("entityResourceRootDialog"));
    dialog.setWindowTitle(QCoreApplication::translate(
        "FilePickerHelper", "选择资源来源"));
    auto layout = new QVBoxLayout(&dialog);
    auto explanation = new QLabel(QCoreApplication::translate(
        "FilePickerHelper",
        "本地资源可直接引用；依赖和共享资源为只读来源，选中文件后可仅保存引用或复制为本地覆盖。"),
        &dialog);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    auto rootCombo = new QComboBox(&dialog);
    rootCombo->setObjectName(QStringLiteral("entityResourceRootCombo"));
    int readOnlyIndex = 0;
    for (int index : availableIndices)
    {
        const ResourceContentRoot& root = resolution.roots[index];
        if (root.kind != ResourceContentRoot::Kind::Local)
            ++readOnlyIndex;
        rootCombo->addItem(resourceRootLabel(root, readOnlyIndex), index);
    }
    layout->addWidget(rootCombo);

    auto pathLabel = new QLabel(&dialog);
    pathLabel->setObjectName(QStringLiteral("entityResourceRootPathLabel"));
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pathLabel->setWordWrap(true);
    layout->addWidget(pathLabel);
    auto updatePath = [rootCombo, pathLabel, &resolution]()
    {
        const int index = rootCombo->currentData().toInt();
        if (index >= 0 && index < resolution.roots.size())
            pathLabel->setText(resolution.roots[index].rootPath);
    };
    QObject::connect(rootCombo, &QComboBox::currentIndexChanged, &dialog,
        [updatePath](int) { updatePath(); });
    updatePath();

    QStringList warnings;
    if (!resolution.missingDependencyIds.isEmpty())
    {
        warnings.append(QCoreApplication::translate(
            "FilePickerHelper", "缺失依赖 ID：%1")
            .arg(resolution.missingDependencyIds.join(QStringLiteral(", "))));
    }
    if (!resolution.missingPaths.isEmpty())
    {
        warnings.append(QCoreApplication::translate(
            "FilePickerHelper", "不可用依赖路径：%1")
            .arg(resolution.missingPaths.join(QStringLiteral("; "))));
    }
    if (!warnings.isEmpty())
    {
        auto warningLabel = new QLabel(warnings.join(QLatin1Char('\n')), &dialog);
        warningLabel->setObjectName(QStringLiteral("entityResourceRootWarningLabel"));
        warningLabel->setWordWrap(true);
        layout->addWidget(warningLabel);
    }

    auto buttons = new QDialogButtonBox(QDialogButtonBox::Open |
                                         QDialogButtonBox::Cancel, &dialog);
    buttons->setObjectName(QStringLiteral("entityResourceRootButtons"));
    buttons->button(QDialogButtonBox::Open)->setText(QCoreApplication::translate(
        "FilePickerHelper", "继续选择文件"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.resize(560, dialog.sizeHint().height());
    if (dialog.exec() != QDialog::Accepted)
        return -1;
    return rootCombo->currentData().toInt();
}

bool normalizeSingleEntityResourceReference(const QString& input,
                                            const EntityResourceRule& rule,
                                            QString& output)
{
    output.clear();
    if (input.trimmed().isEmpty())
        return true;

    QString normalized;
    if (!EditorAssetPath::normalizeResourcePath(input, normalized))
        return false;

    bool removedKnownFolder = false;
    for (int folderIndex = 0; folderIndex < rule.allowedFolders.size(); ++folderIndex)
    {
        QString folder = rule.allowedFolders[folderIndex];
        folder.replace('\\', '/');
        while (folder.endsWith('/'))
            folder.chop(1);
        if (normalized.startsWith(folder + '/', Qt::CaseInsensitive))
        {
            normalized = normalized.mid(folder.size() + 1);
            if (rule.alternateFoldersDirectFilesOnly && folderIndex > 0 &&
                normalized.contains('/'))
            {
                return false;
            }
            removedKnownFolder = true;
            break;
        }
    }

    const QString topFolder = normalized.section('/', 0, 0).toLower();
    if (!removedKnownFolder &&
        (topFolder == QStringLiteral("ini") ||
         topFolder == QStringLiteral("script") ||
         topFolder == QStringLiteral("sound")))
    {
        return false;
    }

    QString validated;
    if (!EditorAssetPath::normalizeResourcePath(normalized, validated))
        return false;
    if (rule.fileNameOnly)
        validated = validated.section('/', -1);
    validated.replace('/', '\\');
    output = validated;
    return true;
}

bool normalizeEntityResourceList(const QString& input,
                                 const EntityResourceRule& rule,
                                 QString& output)
{
    QString canonicalInput = input;
    canonicalInput.replace(QChar(0xFF1B), QChar(';'));

    QStringList normalizedItems;
    const QStringList items = canonicalInput.split(';', Qt::KeepEmptyParts);
    EntityResourceRule itemRule = rule;
    itemRule.list = false;
    for (QString item : items)
    {
        item = item.trimmed();
        if (item.isEmpty())
        {
            normalizedItems.append(QString());
            continue;
        }

        item.replace(QChar(0xFF1A), QChar(':'));
        QString fileName = item;
        QString distanceSuffix;
        const int colon = item.lastIndexOf(':');
        if (colon >= 0)
        {
            if (item.indexOf(':') != colon)
                return false;
            bool distanceOk = false;
            const QString distance = item.mid(colon + 1).trimmed();
            distance.toInt(&distanceOk);
            if (!distanceOk)
                return false;
            fileName = item.left(colon).trimmed();
            if (fileName.isEmpty())
                return false;
            distanceSuffix = QStringLiteral(":") + distance;
        }

        QString normalizedFile;
        if (!normalizeSingleEntityResourceReference(fileName, itemRule, normalizedFile))
            return false;
        normalizedItems.append(normalizedFile + distanceSuffix);
    }
    output = normalizedItems.join(';');
    return true;
}

bool confirmAndCreateLocalOverride(QWidget* parent,
                                   const EntityResourceSelection& selection)
{
    bool overwrite = false;
    if (QFileInfo::exists(selection.localOverridePath) &&
        EditorAssetPath::logicalComparisonKey(selection.localOverridePath) !=
            EditorAssetPath::logicalComparisonKey(selection.sourceFilePath))
    {
        const QMessageBox::StandardButton answer = QMessageBox::question(parent,
            QCoreApplication::translate("FilePickerHelper", "覆盖本地资源"),
            QCoreApplication::translate("FilePickerHelper",
                "本地覆盖文件已存在，是否用所选来源替换？\n\n%1")
                .arg(selection.localOverridePath),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return false;
        overwrite = true;
    }

    QString errorMessage;
    if (!copyEntityResourceToLocalOverride(selection, overwrite, errorMessage))
    {
        QMessageBox::warning(parent,
            QCoreApplication::translate("FilePickerHelper", "复制失败"),
            QCoreApplication::translate("FilePickerHelper",
                "无法创建本地覆盖：%1").arg(errorMessage));
        return false;
    }
    return true;
}

bool resolveInheritedSelectionAction(QWidget* parent,
                                     const EntityResourceSelection& selection)
{
    QMessageBox dialog(parent);
    dialog.setObjectName(QStringLiteral("entityResourceInheritanceDialog"));
    dialog.setWindowTitle(QCoreApplication::translate(
        "FilePickerHelper", "继承资源处理"));
    dialog.setIcon(selection.referenceResolvesToSource
        ? QMessageBox::Question : QMessageBox::Warning);

    QPushButton* referenceButton = nullptr;
    if (selection.referenceResolvesToSource)
    {
        dialog.setText(QCoreApplication::translate("FilePickerHelper",
            "所选文件来自只读资源根。仅保存引用不会复制文件；运行时会继续从该继承根读取。"));
        referenceButton = dialog.addButton(QCoreApplication::translate(
            "FilePickerHelper", "仅保存引用"), QMessageBox::AcceptRole);
        referenceButton->setObjectName(QStringLiteral("saveInheritedReferenceButton"));
    }
    else
    {
        if (selection.effectiveFilePath.isEmpty())
        {
            dialog.setText(QCoreApplication::translate("FilePickerHelper",
                "该相对引用无法解析到所选文件，请复制为本地覆盖。"));
        }
        else
        {
            dialog.setText(QCoreApplication::translate("FilePickerHelper",
                "该相对引用会优先解析到另一个同名文件，不能直接引用所选来源。请复制为本地覆盖。"));
            dialog.setInformativeText(QCoreApplication::translate(
                "FilePickerHelper", "当前优先文件：%1")
                .arg(selection.effectiveFilePath));
        }
    }
    QPushButton* copyButton = dialog.addButton(QCoreApplication::translate(
        "FilePickerHelper", "复制为本地覆盖"), QMessageBox::ActionRole);
    copyButton->setObjectName(QStringLiteral("copyLocalOverrideButton"));
    QPushButton* cancelButton = dialog.addButton(QMessageBox::Cancel);
    cancelButton->setObjectName(QStringLiteral("cancelInheritedResourceButton"));
    dialog.exec();

    if (dialog.clickedButton() == referenceButton)
        return true;
    if (dialog.clickedButton() != copyButton)
        return false;
    return confirmAndCreateLocalOverride(parent, selection);
}

bool pickEntityResourceReference(QWidget* parent, const QString& filter,
                                 const QString& title,
                                 const QString& activeAssetsPath,
                                 EntityResourceField field,
                                 const std::function<QString()>& localDefaultDirGetter,
                                 QString& resourceReference)
{
    resourceReference.clear();
    if (activeAssetsPath.isEmpty() || !QDir(activeAssetsPath).exists())
    {
        QMessageBox::warning(parent,
            QCoreApplication::translate("FilePickerHelper", "路径未配置"),
            QCoreApplication::translate("FilePickerHelper",
                "assets 根目录未设置或不存在，无法选择文件。\n请先打开项目以设置 assets 路径。"));
        return false;
    }

    const ResourceContentRootResolution resolution =
        ResourcePackScanner::resolveContentRoots(activeAssetsPath);
    if (!resolution.recoveryErrors.isEmpty())
    {
        QMessageBox::critical(parent,
            QCoreApplication::translate(
                "FilePickerHelper", "文件事务恢复失败"),
            QCoreApplication::translate(
                "FilePickerHelper",
                "资源目录中存在未能安全恢复的保存事务，无法选择资源：\n%1")
                .arg(resolution.recoveryErrors.join('\n')));
        return false;
    }
    const int rootIndex = chooseEntityResourceRoot(parent, resolution);
    if (rootIndex < 0 || rootIndex >= resolution.roots.size())
        return false;
    const ResourceContentRoot& selectedRoot = resolution.roots[rootIndex];

    QString startDir;
    if (selectedRoot.kind == ResourceContentRoot::Kind::Local && localDefaultDirGetter)
    {
        const QString candidate = localDefaultDirGetter();
        if (!candidate.isEmpty() && QDir(candidate).exists() &&
            EditorAssetPath::isLexicallyInside(
                selectedRoot.rootPath, candidate))
        {
            startDir = candidate;
        }
    }
    if (startDir.isEmpty())
        startDir = entityResourceDefaultPath(selectedRoot.rootPath, field);
    if (!QDir(startDir).exists())
        startDir = selectedRoot.rootPath;

    const QString filePath = QFileDialog::getOpenFileName(
        parent, title, startDir, filter, nullptr,
        QFileDialog::DontResolveSymlinks);
    if (filePath.isEmpty())
        return false;
    QString selectedRootRelativePath;
    if (!EditorAssetPath::makeLogicalResourceRelativePath(
            selectedRoot.rootPath, filePath, selectedRootRelativePath))
    {
        QMessageBox::warning(parent,
            QCoreApplication::translate("FilePickerHelper", "文件范围限制"),
            QCoreApplication::translate("FilePickerHelper",
                "所选文件不在当前选择的资源根的逻辑路径范围内，或无法生成安全的相对资源路径。\n\n"
                "资源根：%1\n文件路径：%2")
                .arg(selectedRoot.rootPath, filePath));
        return false;
    }

    EntityResourceSelection selection;
    if (!inspectEntityResourceSelection(activeAssetsPath, resolution.roots,
            field, filePath, selection) ||
        EditorAssetPath::logicalComparisonKey(selection.sourceRootPath) !=
            EditorAssetPath::logicalComparisonKey(selectedRoot.rootPath))
    {
        QMessageBox::warning(parent,
            QCoreApplication::translate("FilePickerHelper", "文件范围限制"),
            QCoreApplication::translate("FilePickerHelper",
                "所选路径不是可读取的文件、不在该字段对应的逻辑资源目录内，或无法生成安全的相对资源路径。"));
        return false;
    }

    if ((!selection.inherited && selection.referenceResolvesToSource) ||
        resolveInheritedSelectionAction(parent, selection))
    {
        resourceReference = selection.resourceReference;
        return true;
    }
    return false;
}

/// 在指定 QFormLayout 中查找 fileEdit 所在行号，找不到返回 -1。
int findFormLayoutRow(QFormLayout* formLayout, QLineEdit* fileEdit)
{
    for (int i = 0; i < formLayout->rowCount(); i++)
    {
        QLayoutItem* item = formLayout->itemAt(i, QFormLayout::FieldRole);
        if (item && item->widget() == fileEdit)
        {
            return i;
        }
    }
    return -1;
}

/// 沿 layout 的父对象链向上查找第一个 QWidget，用于嵌套 addLayout 的场景。
/// commonForm 这类通过 addLayout 嵌入的子 layout，其 parentWidget() 在部分 Qt
/// 版本中可能为空，需要向上遍历到真正挂载的 widget。
QWidget* resolveLayoutOwningWidget(QFormLayout* formLayout, QLineEdit* fileEdit,
                                   QWidget* explicitParent)
{
    if (formLayout)
    {
        if (QWidget* widget = formLayout->parentWidget())
        {
            return widget;
        }
        // 子 layout 的 parentWidget() 可能为空，沿 QObject parent 向上找 widget。
        for (QObject* obj = formLayout->parent(); obj; obj = obj->parent())
        {
            if (obj->isWidgetType())
            {
                return static_cast<QWidget*>(obj);
            }
        }
    }
    if (fileEdit)
    {
        if (QWidget* widget = fileEdit->parentWidget())
        {
            return widget;
        }
    }
    return explicitParent;
}

/// 为 fileEdit 包装"输入框 + 按钮"并插入 formLayout。
/// @param buttonLabel    按钮文字（"..." 或 "+..."）
/// @param buttonWidth    按钮宽度
/// @param buttonTip      按钮提示
/// @param onClicked      按钮点击回调
/// @param explicitParent 当 formLayout/fileEdit 的 parentWidget 为空时的备用父控件
void wrapFileEditWithButton(QLineEdit* fileEdit, QFormLayout* formLayout,
                            int targetRow, const QString& buttonLabel,
                            int buttonWidth, const QString& buttonTip,
                            std::function<void()> onClicked,
                            QWidget* explicitParent)
{
    QWidget* parentWidget = resolveLayoutOwningWidget(formLayout, fileEdit, explicitParent);
    if (!parentWidget) return;

    auto wrapper = new QWidget(parentWidget);
    auto hLayout = new QHBoxLayout(wrapper);
    hLayout->setContentsMargins(0, 0, 0, 0);
    hLayout->setSpacing(2);

    formLayout->removeWidget(fileEdit);
    fileEdit->setParent(wrapper);
    hLayout->addWidget(fileEdit, 1);

    auto button = new QPushButton(buttonLabel, wrapper);
    if (fileEdit && !fileEdit->objectName().isEmpty())
    {
        button->setObjectName(fileEdit->objectName() +
            (buttonLabel.startsWith(QLatin1Char('+'))
                ? QStringLiteral("AppendPickerButton")
                : QStringLiteral("PickerButton")));
    }
    button->setFixedSize(buttonWidth, fileEdit->sizeHint().height());
    button->setToolTip(buttonTip);
    hLayout->addWidget(button);

    formLayout->setWidget(targetRow, QFormLayout::FieldRole, wrapper);

    QObject::connect(button, &QPushButton::clicked, parentWidget, [onClicked]() { onClicked(); });
}

/// 构建文件选择并写入 fileEdit 的通用逻辑。
/// @param restrictToAssets 为 true 且 assets 路径无效时，提示并拒绝写入
/// @param defaultDirGetter 返回文件对话框默认打开目录的回调（绝对路径），为空则使用 assetsBasePath
void pickFileAndSetResult(QLineEdit* fileEdit, QWidget* dialogParent,
                          const QString& filter, const QString& title,
                          std::function<QString()> assetsBasePathGetter,
                          bool restrictToAssets,
                          std::function<QString()> defaultDirGetter,
                          std::function<QString()> resourceBasePathGetter,
                          bool fileNameOnly,
                          std::function<QStringList()> resourceBasePathsGetter,
                          bool alternateRootsDirectFilesOnly)
{
    QString assetsBasePath = assetsBasePathGetter();

    if (restrictToAssets && (assetsBasePath.isEmpty() || !QDir(assetsBasePath).exists()))
    {
        QMessageBox::warning(dialogParent, QCoreApplication::translate("FilePickerHelper", "路径未配置"),
            QCoreApplication::translate("FilePickerHelper", "assets 根目录未设置或不存在，无法选择文件。\n请先打开项目以设置 assets 路径。"));
        return;
    }

    QString startDir;
    if (defaultDirGetter)
    {
        QString candidateDir = defaultDirGetter();
        if (!candidateDir.isEmpty() && QDir(candidateDir).exists())
        {
            startDir = candidateDir;
        }
    }
    if (startDir.isEmpty())
    {
        startDir = assetsBasePath;
        if (startDir.isEmpty())
        {
            startDir = QDir::currentPath();
        }
    }

    QString filePath = QFileDialog::getOpenFileName(
        dialogParent, title, startDir, filter, nullptr,
        QFileDialog::DontResolveSymlinks);

    if (filePath.isEmpty()) return;

    QStringList resourceBasePaths;
    if (resourceBasePathsGetter)
        resourceBasePaths = resourceBasePathsGetter();
    else
        resourceBasePaths.append(resourceBasePathGetter ? resourceBasePathGetter() : assetsBasePath);
    resourceBasePaths.removeAll(QString());
    QString relativePath;
    if (!makeResourceReference(
            assetsBasePath, resourceBasePaths, filePath, relativePath, fileNameOnly,
            alternateRootsDirectFilesOnly))
    {
        QMessageBox::warning(dialogParent, QCoreApplication::translate("FilePickerHelper", "文件范围限制"),
            QCoreApplication::translate("FilePickerHelper",
                "所选路径不是可读取的文件、不在该字段对应的逻辑资源目录内，或无法生成安全的相对资源路径。\n\n"
                "字段资源目录: %1\n文件路径: %2")
                .arg(resourceBasePaths.join(QStringLiteral("; ")), filePath));
        return;
    }

    fileEdit->setText(relativePath);
}

} // anonymous namespace

bool normalizeEntityResourceReference(EntityResourceField field,
                                      const QString& input,
                                      QString& output)
{
    const EntityResourceRule rule = entityResourceRule(field);
    if (rule.allowedFolders.isEmpty())
    {
        output.clear();
        return false;
    }
    return rule.list
        ? normalizeEntityResourceList(input, rule, output)
        : normalizeSingleEntityResourceReference(input, rule, output);
}

QStringList entityResourceFolders(EntityResourceField field)
{
    return entityResourceRule(field).allowedFolders;
}

bool inspectEntityResourceSelection(
    const QString& activeAssetsPath,
    const QList<ResourceContentRoot>& orderedRoots,
    EntityResourceField field,
    const QString& selectedFilePath,
    EntityResourceSelection& selection)
{
    selection = {};
    const EntityResourceRule rule = entityResourceRule(field);
    if (activeAssetsPath.isEmpty() || orderedRoots.isEmpty() ||
        rule.allowedFolders.isEmpty() || selectedFilePath.isEmpty())
    {
        return false;
    }

    const QString localRoot = EditorAssetPath::normalizedAbsolutePath(activeAssetsPath);
    const QString selectedPath = EditorAssetPath::normalizedAbsolutePath(selectedFilePath);
    if (localRoot.isEmpty() || selectedPath.isEmpty() ||
        !QFileInfo(selectedPath).isFile())
    {
        return false;
    }

    int sourceRootIndex = -1;
    int sourceFolderIndex = -1;
    QString reference;
    for (int rootIndex = 0; rootIndex < orderedRoots.size() && sourceRootIndex < 0;
         ++rootIndex)
    {
        const ResourceContentRoot& root = orderedRoots[rootIndex];
        QString rootRelativePath;
        if (!root.available ||
            !EditorAssetPath::makeLogicalResourceRelativePath(
                root.rootPath, selectedPath, rootRelativePath))
        {
            continue;
        }
        for (int folderIndex = 0; folderIndex < rule.allowedFolders.size(); ++folderIndex)
        {
            const QString basePath = QDir(root.rootPath).filePath(
                rule.allowedFolders[folderIndex]);
            QString normalized;
            if (!EditorAssetPath::makeLogicalResourceRelativePath(
                    basePath, selectedPath, normalized))
                continue;
            if (rule.alternateFoldersDirectFilesOnly && folderIndex > 0 &&
                normalized.contains('/'))
            {
                continue;
            }
            if (rule.fileNameOnly)
                normalized = normalized.section('/', -1);
            normalized.replace('/', '\\');
            sourceRootIndex = rootIndex;
            sourceFolderIndex = folderIndex;
            reference = normalized;
            break;
        }
    }
    if (sourceRootIndex < 0 || sourceFolderIndex < 0 || reference.isEmpty())
        return false;

    QString normalizedReference = reference;
    normalizedReference.replace('\\', '/');
    QString effectivePath;
    // 业务目录候选在调用端按声明顺序尝试；每个候选再按内容根顺序回退。
    // 因此 NpcIni 的 npcres 优先级高于所有根中的 legacy ini/npc。
    for (int folderIndex = 0;
         folderIndex < rule.allowedFolders.size() && effectivePath.isEmpty();
         ++folderIndex)
    {
        if (rule.alternateFoldersDirectFilesOnly && folderIndex > 0 &&
            normalizedReference.contains('/'))
        {
            continue;
        }
        for (const ResourceContentRoot& root : orderedRoots)
        {
            if (!root.available)
                continue;
            const QString relativeCandidate =
                rule.allowedFolders[folderIndex] + QLatin1Char('/') +
                normalizedReference;
            QString candidate;
            if (EditorAssetPath::resolveLogicalResourcePath(
                    root.rootPath, relativeCandidate, candidate) &&
                QFileInfo(candidate).isFile())
            {
                effectivePath = candidate;
                break;
            }
        }
    }

    const QString localOverrideRelativePath =
        rule.allowedFolders.front() + QLatin1Char('/') + normalizedReference;
    QString targetPath;
    if (!EditorAssetPath::resolveLogicalResourcePath(
            localRoot, localOverrideRelativePath, targetPath))
        return false;

    selection.resourceReference = reference;
    selection.sourceFilePath = selectedPath;
    selection.sourceRootPath = EditorAssetPath::normalizedAbsolutePath(
        orderedRoots[sourceRootIndex].rootPath);
    selection.localRootPath = localRoot;
    selection.localOverridePath = targetPath;
    selection.effectiveFilePath = effectivePath;
    selection.inherited =
        EditorAssetPath::logicalComparisonKey(selection.sourceRootPath) !=
        EditorAssetPath::logicalComparisonKey(localRoot);
    selection.referenceResolvesToSource = !effectivePath.isEmpty() &&
        EditorAssetPath::logicalComparisonKey(effectivePath) ==
            EditorAssetPath::logicalComparisonKey(selectedPath);
    return true;
}

bool copyEntityResourceToLocalOverride(const EntityResourceSelection& selection,
                                       bool overwrite,
                                       QString& errorMessage)
{
    errorMessage.clear();
    QString sourceRelativePath;
    if (selection.sourceFilePath.isEmpty() || selection.sourceRootPath.isEmpty() ||
        selection.localRootPath.isEmpty() || selection.localOverridePath.isEmpty() ||
        !EditorAssetPath::makeLogicalResourceRelativePath(
            selection.sourceRootPath, selection.sourceFilePath,
            sourceRelativePath) ||
        !EditorAssetPath::isInside(selection.localRootPath, selection.localOverridePath))
    {
        errorMessage = QCoreApplication::translate(
            "FilePickerHelper", "来源或目标路径无效。");
        return false;
    }
    if (EditorAssetPath::logicalComparisonKey(selection.sourceFilePath) ==
        EditorAssetPath::logicalComparisonKey(selection.localOverridePath))
    {
        return true;
    }
    AuthoringMutationGate::Lease mutationLease =
        AuthoringMutationGate::instance().acquireMutationLeaseForPath(
            selection.localOverridePath);
    if (!mutationLease ||
        !mutationLease.addResourcePath(selection.sourceFilePath))
    {
        errorMessage = QCoreApplication::translate(
            "FilePickerHelper", "资源包正在更新或进行其他写入");
        return false;
    }
    if (!QFileInfo(selection.sourceFilePath).isFile())
    {
        errorMessage = QCoreApplication::translate(
            "FilePickerHelper", "来源文件不存在。");
        return false;
    }
    if (QFileInfo::exists(selection.localOverridePath) && !overwrite)
    {
        errorMessage = QCoreApplication::translate(
            "FilePickerHelper", "本地覆盖文件已存在。");
        return false;
    }
    if (!QDir().mkpath(QFileInfo(selection.localOverridePath).dir().absolutePath()))
    {
        errorMessage = QCoreApplication::translate(
            "FilePickerHelper", "无法创建目标目录。");
        return false;
    }

    QFile source(selection.sourceFilePath);
    if (!source.open(QIODevice::ReadOnly))
    {
        errorMessage = source.errorString();
        return false;
    }
    QSaveFile target(selection.localOverridePath);
    if (!target.open(QIODevice::WriteOnly))
    {
        errorMessage = target.errorString();
        return false;
    }
    QByteArray buffer;
    buffer.resize(64 * 1024);
    while (true)
    {
        const qint64 readCount = source.read(buffer.data(), buffer.size());
        if (readCount < 0)
        {
            errorMessage = source.errorString();
            target.cancelWriting();
            return false;
        }
        if (readCount == 0)
            break;
        if (target.write(buffer.constData(), readCount) != readCount)
        {
            errorMessage = target.errorString();
            target.cancelWriting();
            return false;
        }
    }
    if (!target.commit())
    {
        errorMessage = target.errorString();
        return false;
    }
    return true;
}

bool makeResourceReference(const QString& assetsBasePath,
                           const QString& resourceBasePath,
                           const QString& selectedFilePath,
                           QString& resourceReference,
                           bool fileNameOnly)
{
    return makeResourceReference(assetsBasePath, QStringList{resourceBasePath},
        selectedFilePath, resourceReference, fileNameOnly, false);
}

bool makeResourceReference(const QString& assetsBasePath,
                           const QStringList& resourceBasePaths,
                           const QString& selectedFilePath,
                           QString& resourceReference,
                           bool fileNameOnly,
                           bool alternateRootsDirectFilesOnly)
{
    resourceReference.clear();
    QString selectedAssetsRelativePath;
    if (assetsBasePath.isEmpty() || resourceBasePaths.isEmpty() ||
        selectedFilePath.isEmpty() ||
        !EditorAssetPath::makeLogicalResourceRelativePath(
            assetsBasePath, selectedFilePath, selectedAssetsRelativePath))
    {
        return false;
    }

    for (int rootIndex = 0; rootIndex < resourceBasePaths.size(); ++rootIndex)
    {
        const QString& resourceBasePath = resourceBasePaths[rootIndex];
        if (resourceBasePath.isEmpty() ||
            !EditorAssetPath::isLexicallyInside(
                assetsBasePath, resourceBasePath))
        {
            continue;
        }

        QString normalized;
        if (!EditorAssetPath::makeLogicalResourceRelativePath(
                resourceBasePath, selectedFilePath, normalized))
            continue;
        if (alternateRootsDirectFilesOnly && rootIndex > 0 && normalized.contains('/'))
            continue;
        if (fileNameOnly)
            normalized = normalized.section('/', -1);
        normalized.replace('/', '\\');
        resourceReference = normalized;
        return true;
    }
    return false;
}

void addEntityResourcePickerButton(QLineEdit* fileEdit, const QString& filter,
                                   std::function<QString()> assetsBasePathGetter,
                                   EntityResourceField field, QWidget* parent,
                                   std::function<QString()> defaultDirGetter)
{
    QWidget* parentWidget = fileEdit ? fileEdit->parentWidget() : nullptr;
    QFormLayout* formLayout = parentWidget
        ? qobject_cast<QFormLayout*>(parentWidget->layout()) : nullptr;
    addEntityResourcePickerButton(fileEdit, formLayout, filter,
        std::move(assetsBasePathGetter), field, parent, std::move(defaultDirGetter));
}

void addEntityResourcePickerButton(QLineEdit* fileEdit, QFormLayout* formLayout,
                                   const QString& filter,
                                   std::function<QString()> assetsBasePathGetter,
                                   EntityResourceField field, QWidget* parent,
                                   std::function<QString()> defaultDirGetter)
{
    if (!formLayout || !fileEdit)
        return;
    const int targetRow = findFormLayoutRow(formLayout, fileEdit);
    if (targetRow < 0)
        return;
    wrapFileEditWithButton(fileEdit, formLayout, targetRow,
        QCoreApplication::translate("FilePickerHelper", "..."), 28,
        QCoreApplication::translate("FilePickerHelper", "选择文件"),
        [parent, fileEdit, filter, assetsBasePathGetter, field, defaultDirGetter]()
        {
            QString reference;
            if (pickEntityResourceReference(parent, filter,
                    QCoreApplication::translate("FilePickerHelper", "选择文件"),
                    assetsBasePathGetter(), field, defaultDirGetter, reference))
            {
                fileEdit->setText(reference);
            }
        }, parent);
}

void addEntityResourcePickerButton(
    QLineEdit* fileEdit, QFormLayout* formLayout, const QString& filter,
    std::function<QString()> assetsBasePathGetter,
    std::function<EntityResourceField()> fieldGetter, QWidget* parent,
    std::function<QString()> defaultDirGetter)
{
    if (!formLayout)
        return;
    const int targetRow = findFormLayoutRow(formLayout, fileEdit);
    if (targetRow < 0)
        return;

    wrapFileEditWithButton(fileEdit, formLayout, targetRow,
        QCoreApplication::translate("FilePickerHelper", "..."), 28,
        QCoreApplication::translate("FilePickerHelper", "选择文件"),
        [parent, fileEdit, filter, assetsBasePathGetter, fieldGetter,
         defaultDirGetter]()
        {
            const EntityResourceField field = fieldGetter();
            QString reference;
            if (pickEntityResourceReference(parent, filter,
                    QCoreApplication::translate("FilePickerHelper", "选择文件"),
                    assetsBasePathGetter(), field, defaultDirGetter, reference))
            {
                fileEdit->setText(reference);
            }
        },
        parent);
}

void addEntityResourceAppendPickerButton(QLineEdit* fileEdit, const QString& filter,
                                         std::function<QString()> assetsBasePathGetter,
                                         EntityResourceField field, QWidget* parent)
{
    QWidget* parentWidget = fileEdit ? fileEdit->parentWidget() : nullptr;
    QFormLayout* formLayout = parentWidget
        ? qobject_cast<QFormLayout*>(parentWidget->layout()) : nullptr;
    if (!formLayout)
        return;
    const int targetRow = findFormLayoutRow(formLayout, fileEdit);
    if (targetRow < 0)
        return;
    wrapFileEditWithButton(fileEdit, formLayout, targetRow,
        QCoreApplication::translate("FilePickerHelper", "+..."), 36,
        QCoreApplication::translate("FilePickerHelper", "追加选择文件"),
        [parent, fileEdit, filter, assetsBasePathGetter, field]()
        {
            QString reference;
            if (!pickEntityResourceReference(parent, filter,
                    QCoreApplication::translate("FilePickerHelper", "追加选择文件"),
                    assetsBasePathGetter(), field, {}, reference))
            {
                return;
            }
            QString currentText = fileEdit->text().trimmed();
            if (!currentText.isEmpty())
                currentText += QLatin1Char(';');
            fileEdit->setText(currentText + reference);
        }, parent);
}

// ========== 自动查找 QFormLayout 的重载 ==========

void addFilePickerButton(QLineEdit* fileEdit, const QString& filter,
                         std::function<QString()> assetsBasePathGetter,
                         bool restrictToAssets, QWidget* parent,
                         std::function<QString()> defaultDirGetter,
                         std::function<QString()> resourceBasePathGetter,
                         bool fileNameOnly,
                         std::function<QStringList()> resourceBasePathsGetter,
                         bool alternateRootsDirectFilesOnly)
{
    QWidget* parentWidget = fileEdit->parentWidget();
    if (!parentWidget) return;

    QFormLayout* formLayout = qobject_cast<QFormLayout*>(parentWidget->layout());
    if (!formLayout) return;

    addFilePickerButton(fileEdit, formLayout, filter, assetsBasePathGetter,
        restrictToAssets, parent, defaultDirGetter, resourceBasePathGetter,
        fileNameOnly, resourceBasePathsGetter, alternateRootsDirectFilesOnly);
}

// ========== 显式传入 QFormLayout 的重载 ==========

void addFilePickerButton(QLineEdit* fileEdit, QFormLayout* formLayout,
                         const QString& filter,
                         std::function<QString()> assetsBasePathGetter,
                         bool restrictToAssets, QWidget* parent,
                         std::function<QString()> defaultDirGetter,
                         std::function<QString()> resourceBasePathGetter,
                         bool fileNameOnly,
                         std::function<QStringList()> resourceBasePathsGetter,
                         bool alternateRootsDirectFilesOnly)
{
    if (!formLayout) return;

    int targetRow = findFormLayoutRow(formLayout, fileEdit);
    if (targetRow < 0) return;

    wrapFileEditWithButton(fileEdit, formLayout, targetRow,
        QCoreApplication::translate("FilePickerHelper", "..."), 28, QCoreApplication::translate("FilePickerHelper", "选择文件"),
        [parent, fileEdit, filter, assetsBasePathGetter, restrictToAssets,
         defaultDirGetter, resourceBasePathGetter, fileNameOnly, resourceBasePathsGetter,
         alternateRootsDirectFilesOnly]()
    {
        pickFileAndSetResult(fileEdit, parent, filter,
            QCoreApplication::translate("FilePickerHelper", "选择文件"), assetsBasePathGetter, restrictToAssets,
            defaultDirGetter, resourceBasePathGetter, fileNameOnly, resourceBasePathsGetter,
            alternateRootsDirectFilesOnly);
    },
        parent);
}

// ========== 追加文件选择按钮 ==========

void addAppendFilePickerButton(QLineEdit* fileEdit, const QString& filter,
                               std::function<QString()> assetsBasePathGetter,
                               QWidget* parent,
                               std::function<QString()> resourceBasePathGetter)
{
    QWidget* parentWidget = fileEdit->parentWidget();
    if (!parentWidget) return;

    QFormLayout* formLayout = qobject_cast<QFormLayout*>(parentWidget->layout());
    if (!formLayout) return;

    int targetRow = findFormLayoutRow(formLayout, fileEdit);
    if (targetRow < 0) return;

    wrapFileEditWithButton(fileEdit, formLayout, targetRow,
        QCoreApplication::translate("FilePickerHelper", "+..."), 36, QCoreApplication::translate("FilePickerHelper", "追加选择文件"),
        [parent, fileEdit, filter, assetsBasePathGetter, resourceBasePathGetter]()
    {
        QString assetsBasePath = assetsBasePathGetter();

        if (assetsBasePath.isEmpty() || !QDir(assetsBasePath).exists())
        {
            QMessageBox::warning(parent, QCoreApplication::translate("FilePickerHelper", "路径未配置"),
                QCoreApplication::translate("FilePickerHelper", "assets 根目录未设置或不存在，无法选择文件。\n请先打开项目以设置 assets 路径。"));
            return;
        }

        const QString configuredResourceBasePath = resourceBasePathGetter
            ? resourceBasePathGetter()
            : assetsBasePath;
        QString startDir = QDir(configuredResourceBasePath).exists()
            ? configuredResourceBasePath
            : assetsBasePath;

        QString filePath = QFileDialog::getOpenFileName(
            parent,
            QCoreApplication::translate("FilePickerHelper", "追加选择文件"),
            startDir, filter, nullptr,
            QFileDialog::DontResolveSymlinks);

        if (filePath.isEmpty()) return;

        const QString resourceBasePath = resourceBasePathGetter
            ? resourceBasePathGetter()
            : assetsBasePath;
        QString relativePath;
        if (!makeResourceReference(assetsBasePath, resourceBasePath, filePath, relativePath))
        {
            QMessageBox::warning(parent, QCoreApplication::translate("FilePickerHelper", "文件范围限制"),
                QCoreApplication::translate("FilePickerHelper",
                    "所选路径不是可读取的文件、不在该字段对应的逻辑资源目录内，或无法生成安全的相对资源路径。\n\n"
                    "字段资源目录: %1\n文件路径: %2")
                    .arg(resourceBasePath, filePath));
            return;
        }

        QString currentText = fileEdit->text().trimmed();
        if (!currentText.isEmpty())
        {
            // FlyInis 格式: file:distance;file:distance;...
            // 追加时只追加文件名，用户需要手动补充 :distance
            currentText += ";";
        }
        currentText += relativePath;
        fileEdit->setText(currentText);
    },
        parent);
}

} // namespace FilePickerHelper
