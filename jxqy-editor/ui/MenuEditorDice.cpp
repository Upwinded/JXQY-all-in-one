#include "MenuEditorWindow.h"
#include "EditorSaveConfirmation.h"
#include "ui_MenuEditorWindow.h"
#include "../core/EditorAssetPath.h"
#include "../core/DurableFileTransaction.h"
#include "../core/INIFileEditor.h"
#include "../core/ProjectManager.h"
#include "../../src/Game/Menu/DiceGameLayout.h"
#include <QComboBox>
#include <QFile>
#include <QDir>
#include <QLabel>
#include <QMessageBox>
#include <QSignalBlocker>

namespace
{
bool validDiceImage(const QString& path)
{
    QString normalized; return path.isEmpty() || EditorAssetPath::normalizeResourcePath(path,normalized);
}
QString diceLabel(const DiceGameLayout::Role& role)
{
    return QCoreApplication::translate("MenuEditorWindow", role.label);
}
DiceGameLayout displayLayout(const ComponentIniProperties& properties)
{
    DiceGameLayout result;
    for (auto it = properties.rawValues.cbegin(); it != properties.rawValues.cend(); ++it)
        result.values[it.key().toStdString()] = it.value().toStdString();
    return result;
}
}

bool MenuEditorWindow::loadDicePage(const QString& path)
{
    if (!pageResources.valid || EditorAssetPath::logicalComparisonKey(path)
        != EditorAssetPath::logicalComparisonKey(resolvedPageSource("dicegame"))) return false;
    INIFileEditor file;
    if (!file.loadFromFile(path.toStdString())) return false;
    currentDefinition = MenuDefinition();
    currentDefinition.menuName = "DiceGame";
    currentDefinition.windowFile = DiceGameLayout::File;
    currentDefinition.windowRect = {0,0,549,300};
    documentStructureIssue.clear();
    if (file.getInteger("Window","Width",0) != 549 || file.getInteger("Window","Height",0) != 300)
        documentStructureIssue = tr("骰子配置需要549×300设计画布；原内容只读保留。");
    for (const auto& role : DiceGameLayout::roles())
    {
        MenuComponentDefinition component;
        component.editorId = "dice/" + QString(role.id);
        component.name = role.id; component.type = "DiceDisplay"; component.file = DiceGameLayout::File;
        currentDefinition.components.append(component);
    }
    return true;
}

ComponentIniProperties MenuEditorWindow::diceProperties() const
{
    ComponentIniProperties result;
    INIFileEditor file;
    if (!file.loadFromFile(pageResources.readable(DiceGameLayout::File).toStdString())) return result;
    for (const auto& role : DiceGameLayout::roles()) for (const auto& field : DiceGameLayout::fields(role))
    {
        const auto key = DiceGameLayout::key(role,field);
        const auto slash = key.find('/');
        if (file.hasKey(key.substr(0,slash),key.substr(slash+1)))
            result.rawValues[QString::fromStdString(key)] = QString::fromStdString(file.get(key.substr(0,slash),key.substr(slash+1)));
    }
    return result;
}

void MenuEditorWindow::prepareDiceProperties()
{
    if (selectedPage != "dicegame") return;
    QString target; resolveLocalResourcePath(DiceGameLayout::File,target);
    if (modifiedComponentInis.contains(target)) return;
    auto& pending = modifiedComponentInis[target];
    pending.properties = diceProperties(); pending.resourcePath = DiceGameLayout::File;
    pending.sourcePath = pageResources.readable(DiceGameLayout::File);
    pending.owners.insert("menu/window"); pending.ownerComponentTypes["menu/window"] = "MenuWindow";
}

bool MenuEditorWindow::validateDicePage(QString& error)
{
    if (currentDefinition.menuName != "DiceGame" || currentDefinition.windowFile != DiceGameLayout::File
        || !currentDefinition.subMenus.isEmpty() || currentDefinition.components.size() != static_cast<int>(DiceGameLayout::roles().size()))
    { error = tr("骰子固定角色和入口不能修改。"); return false; }
    int index = 0;
    for (const auto& role : DiceGameLayout::roles())
    {
        const auto& component = currentDefinition.components[index++];
        if (component.name != role.id || component.editorId != "dice/"+QString(role.id)
            || component.type != "DiceDisplay" || component.file != DiceGameLayout::File)
        { error = tr("骰子固定角色和入口不能修改。"); return false; }
    }
    if (!documentStructureIssue.isEmpty()) { error = documentStructureIssue; return false; }
    const auto references = collectExternalResourceReferences();
    if (!references.complete || references.paths.contains(QString(DiceGameLayout::File).toCaseFolded()))
    { error = tr("骰子配置被其它菜单共享，固定入口不能拆分，已限制写入。"); return false; }
    return true;
}

void MenuEditorWindow::updateDiceTree()
{
    const QSignalBlocker blocker(ui->componentTree);
    ui->componentTree->clear();
    QList<QTreeWidgetItem*> groups;
    for (const auto& name : {tr("背景与标题"),tr("人物与对话"),tr("骰子与结果"),tr("操作按钮")})
    {
        auto* group = new QTreeWidgetItem(ui->componentTree,QStringList{name});
        group->setExpanded(true); groups.append(group);
    }
    int index = 0;
    for (const auto& role : DiceGameLayout::roles())
    {
        auto* item = new QTreeWidgetItem(groups[role.group],QStringList{diceLabel(role)});
        item->setToolTip(0,QString(role.id)); item->setData(0,Qt::UserRole,index);
        if (index == std::max(0,selectedComponentIndex)) ui->componentTree->setCurrentItem(item);
        ++index;
    }
    selectedComponentIndex = std::max(0,selectedComponentIndex);
    selectedItemType = SelectedItemType::Component;
    updatePropertyEditor();
}

void MenuEditorWindow::updateDicePropertyEditor()
{
    if (selectedComponentIndex < 0 || selectedComponentIndex >= currentDefinition.components.size())
    { propertyEditor->clearAll(); return; }
    const auto& role = DiceGameLayout::roles()[selectedComponentIndex];
    const auto layout = displayLayout(propertiesForResource(DiceGameLayout::File));
    QMap<QString,QString> values;
    for (const auto& field : DiceGameLayout::fields(role))
        values[QString::fromStdString(field)] = QString::fromStdString(layout.raw(role,field));
    propertyEditor->setDiceDisplayProperties(role.id,values,canEditPage());
    editStatusLabel->setVisible(true);
    editStatusLabel->setText(diceLabel(role) +
        (hasUnsavedChanges ? tr("（未保存）") : QString()));
}

void MenuEditorWindow::applyDicePropertyEdit()
{
    if (selectedComponentIndex < 0 || selectedComponentIndex >= currentDefinition.components.size()) return;
    const auto& role = DiceGameLayout::roles()[selectedComponentIndex];
    const auto proposed = propertyEditor->diceDisplayProperties();
    auto properties = propertiesForResource(DiceGameLayout::File);
    const auto before = properties.rawValues;
    for (const auto& field : DiceGameLayout::fields(role))
    {
        const auto key = QString::fromStdString(DiceGameLayout::key(role,field));
        const auto text = proposed.value(QString::fromStdString(field));
        if (text == before.value(key)) continue;
        if (!DiceGameLayout::valid(field,text.toStdString())
            || (field == "Image" && !validDiceImage(text))
            || !INIFileEditor::canRoundTripValue(text.toStdString()))
        { pageContextLabel->setText(tr("骰子显示字段无效，修改未应用：%1").arg(QString::fromStdString(field))); updatePropertyEditor(); return; }
        properties.rawValues[key] = text;
    }
    if (before == properties.rawValues) return;
    QString target; resolveLocalResourcePath(DiceGameLayout::File,target);
    auto& pending = modifiedComponentInis[target];
    pending.properties = properties; pending.stagedForWrite = true;
    markModified(); updateDicePreview(); updateDicePropertyEditor();
}

bool MenuEditorWindow::saveDiceDocument(const QString& path, bool updatePath)
{
    if (!hasUnsavedChanges) return true;
    const auto expectedTarget = EditorFileState::read(path,path);
    if (!controllerHelpDiskMatches() &&
        !EditorSaveConfirmation::confirmOverwrite(this,{currentFilePath,path}))
        return false;
    INIFileEditor file;
    if (!file.loadFromFile(currentFilePath.toStdString())) return false;
    const auto properties = propertiesForResource(DiceGameLayout::File);
    for (const auto& role : DiceGameLayout::roles()) for (const auto& field : DiceGameLayout::fields(role))
    {
        const auto key = DiceGameLayout::key(role,field); const auto slash = key.find('/');
        const auto section = key.substr(0,slash), name = key.substr(slash+1);
        const auto value = properties.rawValues.value(QString::fromStdString(key)).toStdString();
        // Undo may restore an absent optional key after a later save introduced it.
        if (file.get(section,name) != value) file.set(section,name,value);
    }
    FileTransaction transaction; QSet<QString> targets; QString error;
    if (!prepareTransaction(path,transaction,targets,error))
    { QMessageBox::warning(this,tr("保存失败"),error); return false; }
    QList<FileTransaction> transactions{transaction};
    if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !file.saveToFile(transaction.temp.toStdString()))
    { cleanupTransactionTemps(transactions); QMessageBox::warning(this,tr("保存失败"),tr("骰子配置写入失败，原文件和待保存编辑保留。")); return false; }
    DurableFileTransaction publication(assetsBasePath);
    EditorFileState savedTarget;
    if (!expectedTarget.prepareWrite(publication,path,transaction.temp,savedTarget,error) || !publication.commit(error))
    { cleanupTransactionTemps(transactions); QMessageBox::warning(this,tr("保存失败"),error); return false; }
    if (!error.isEmpty()) QMessageBox::warning(this,tr("事务清理警告"),error);
    QString local; resolveLocalResourcePath(DiceGameLayout::File,local);
    modifiedComponentInis.clear(); componentIniCache.clear(); modifiedSubMenuIds.clear(); hasUnsavedChanges = false;
    if (updatePath) currentFilePath = EditorAssetPath::normalizedAbsolutePath(path);
    prepareDiceProperties(); recordControllerHelpDiskState(); establishSavePoint({local}); ProjectManager::instance().markDirty();
    updatePagePermissions(); refreshPageList(); updatePropertyEditor(); updateDicePreview();
    return true;
}

void MenuEditorWindow::updateDicePreview()
{
    const QStringList states = {tr("等待下注（示例）"),tr("摇骰（示例）"),tr("揭盅（示例）"),tr("获胜（示例）"),tr("落败（示例）"),tr("平局（示例）")};
    const QSignalBlocker blocker(toolTipPreviewCombo);
    if (toolTipPreviewCombo->count() != states.size()) { toolTipPreviewCombo->clear(); toolTipPreviewCombo->addItems(states); }
    toolTipPreviewAction->setVisible(true);
    toolTipPreviewCombo->setVisible(true);
    const bool simulation = previewModeCombo && previewModeCombo->currentIndex() == 1;
    const QSize size = simulation ? previewWindowSize() : QSize(549, 300);
    previewInterfaceOrigin = {};
    PreviewComponent sample;
    sample.editorId = "dice/preview"; sample.type = "DiceGame"; sample.rect = {QPoint(),size};
    sample.rawValues = propertiesForResource(DiceGameLayout::File).rawValues;
    if (!simulation) sample.rawValues["diceeditview"] = "true";
    sample.selectedIndex = std::max(0,toolTipPreviewCombo->currentIndex()); sample.movable = sample.resizable = false;
    previewCanvas->setMenuRect(sample.rect); previewCanvas->setBackgroundImage({}); previewCanvas->setWindowStretch(false);
    previewCanvas->setComponents({sample}); previewCanvas->setSubMenus({});
    auto* information = findChild<QLabel*>("conversationLayoutResult"); information->setVisible(true);
    information->setText(tr("549×300设计画布 · 示例不保存；图片失败时游戏使用原回退界面。"));
}
