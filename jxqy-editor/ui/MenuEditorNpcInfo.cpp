#include "MenuEditorWindow.h"
#include "EditorSaveConfirmation.h"
#include "../core/EditorAssetPath.h"
#include "../core/DurableFileTransaction.h"
#include "../core/ProjectManager.h"
#include "../../src/File/ini.h"
#include <QAction>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QMessageBox>
#include <QDir>
#include <QTextStream>
#include <limits>

namespace
{
int npcInteger(const QMap<QString,QString>& values,const char* key,int fallback)
{
    try { return static_cast<int>(std::stol(values.value(key).toStdString(),nullptr,0)); }
    catch(const std::exception&) { return fallback; }
}
int npcParseError(const QByteArray& bytes)
{
    return ini_parse_string(bytes.constData(),[](void*,const char*,const char*,const char*) {return 1;},nullptr);
}
}

bool MenuEditorWindow::loadFixedWindowPage(const QString& path)
{
    const bool help=requestedPage=="controllerhelp"||documentPage(path)=="controllerhelp";
    if(!pageResources.valid||EditorAssetPath::logicalComparisonKey(path)!=EditorAssetPath::logicalComparisonKey(resolvedPageSource(help?"controllerhelp":"npcinfo")))return false;
    currentDefinition=MenuDefinition();currentDefinition.menuName=help?"ControllerHelpOverlay":"NpcInfoPanel";
    currentDefinition.windowFile=help?"ini/ui/controller_help/window.ini":"ini/ui/npcinfo/window.ini";originalMenuFileLines.clear();documentStructureIssue.clear();
    QFile file(path);
    if(file.exists()&&!file.open(QIODevice::ReadOnly)) return false;
    const auto bytes=file.isOpen()?file.readAll():QByteArray();
    const int error=npcParseError(bytes);
    if(error)documentStructureIssue=(help?tr("手柄帮助布局解析失败（行%1），运行时采用默认，本页只读。"):tr("NPC布局配置解析失败（行%1），运行时使用整组默认，本页只读。")).arg(error);
    if(!bytes.isEmpty()) originalMenuFileLines=QString::fromUtf8(bytes).split('\n');
    return true;
}

ComponentIniProperties MenuEditorWindow::npcInfoProperties()
{
    auto value=propertiesForResource(currentDefinition.windowFile);
    int width=npcInteger(value.rawValues,"width",140),height=npcInteger(value.rawValues,"height",18);
    if(width<=0||height<=0||!documentStructureIssue.isEmpty())
    {value.width=140;value.height=18;value.alignX=0;value.alignY=50;}
    else
    {
        value.width=width;value.height=height;
        value.alignX=npcInteger(value.rawValues,"alignx",0);value.alignY=npcInteger(value.rawValues,"aligny",50);
    }
    return value;
}

void MenuEditorWindow::prepareNpcInfoDefaults()
{
    if(selectedPage!="npcinfo") return;
    QString target;resolveLocalResourcePath(currentDefinition.windowFile,target);
    const auto properties=npcInfoProperties();auto& modified=modifiedComponentInis[target];
    if(modified.stagedForWrite) return;
    modified.properties=properties;modified.resourcePath=currentDefinition.windowFile;
    modified.sourcePath=pageResources.readable(currentDefinition.windowFile);
    modified.owners.insert("menu/window");modified.ownerComponentTypes["menu/window"]="MenuWindow";
}

bool MenuEditorWindow::stageNpcInfoProperties(const ComponentIniProperties& proposed)
{
    auto value=npcInfoProperties();const auto original=value;
    if(proposed.width<=0||proposed.height<=0) return false;
    const auto change=[&](const char* key,int before,int after) {if(before!=after) value.rawValues[key]=QString::number(after);};
    change("width",value.width,proposed.width);change("height",value.height,proposed.height);
    change("alignx",value.alignX,proposed.alignX);change("aligny",value.alignY,proposed.alignY);
    if(value.rawValues==original.rawValues) return true;
    QString target;resolveLocalResourcePath(currentDefinition.windowFile,target);
    auto& modified=modifiedComponentInis[target];
    modified.properties=value;modified.stagedForWrite=true;
    modified.resourcePath=currentDefinition.windowFile;
    modified.owners.insert("menu/window");modified.ownerComponentTypes["menu/window"]="MenuWindow";
    modified.properties=npcInfoProperties();
    componentIniCache.remove(target);
    updatePropertyEditor();
    return true;
}

bool MenuEditorWindow::validateNpcInfo(QString& error)
{
    if(currentDefinition.menuName!="NpcInfoPanel"||currentDefinition.windowFile!="ini/ui/npcinfo/window.ini"
        ||!currentDefinition.components.isEmpty()||!currentDefinition.subMenus.isEmpty())
    {error=tr("NPC信息使用固定单文件，不能修改结构或引用。");return false;}
    if(!documentStructureIssue.isEmpty()) {error=documentStructureIssue;return false;}
    const auto references=collectExternalResourceReferences();
    if(!references.complete||references.paths.contains(currentDefinition.windowFile.toCaseFolded()))
    {error=tr("NPC布局文件还被其它菜单引用；固定入口不能拆分，已限制写入。");return false;}
    return true;
}

bool MenuEditorWindow::saveFixedWindowDocument(const QString& path,bool updatePath)
{
    if(!hasUnsavedChanges)return true;
    const auto expectedTarget=EditorFileState::read(path,path);
    if(selectedPage=="controllerhelp"&&!controllerHelpDiskMatches()&&
        !EditorSaveConfirmation::confirmOverwrite(this,{currentFilePath,path}))
        return false;
    FileTransaction transaction;QSet<QString> targets;QString error;
    if(!prepareTransaction(path,transaction,targets,error)) {QMessageBox::warning(this,tr("保存失败"),error);return false;}
    QList<FileTransaction> transactions{transaction};
    QString local;resolveLocalResourcePath(currentDefinition.windowFile,local);
    const auto pending=modifiedComponentInis.value(local);
    if(!QDir().mkpath(QFileInfo(path).absolutePath())||!writeComponentIniProperties(transaction.temp,pending.properties,{"MenuWindow"},pending.sourcePath.isEmpty()?currentFilePath:pending.sourcePath))
    {cleanupTransactionTemps(transactions);QMessageBox::warning(this,tr("保存失败"),selectedPage=="controllerhelp"?tr("手柄帮助布局写入失败，原文件和待保存编辑保留。"):tr("NPC布局写入失败，原文件和待保存编辑保留。"));return false;}
    DurableFileTransaction publication(assetsBasePath);EditorFileState savedTarget;
    if(!expectedTarget.prepareWrite(publication,path,transaction.temp,savedTarget,error)||!publication.commit(error))
    {cleanupTransactionTemps(transactions);QMessageBox::warning(this,tr("保存失败"),error);return false;}
    if(!error.isEmpty())QMessageBox::warning(this,tr("事务清理警告"),error);
    modifiedComponentInis.clear();componentIniCache.clear();modifiedSubMenuIds.clear();hasUnsavedChanges=false;
    if(updatePath) currentFilePath=EditorAssetPath::normalizedAbsolutePath(path);
    if(selectedPage=="controllerhelp"){prepareControllerHelpDefaults();recordControllerHelpDiskState();}else prepareNpcInfoDefaults();establishSavePoint({local});ProjectManager::instance().markDirty();
    updatePagePermissions();refreshPageList();updatePreview();
    return true;
}

QStringList MenuEditorWindow::npcInfoPreviewStates() const
{
    return {tr("敌对：半血与滞后（示例）"),tr("友方（示例）"),tr("中立（示例）"),tr("满血（示例）"),tr("零血（示例）"),tr("空名称（示例）"),tr("最大生命为零（示例）"),tr("无有效目标（示例）")};
}

void MenuEditorWindow::updateNpcInfoPreview(QList<PreviewComponent>& components)
{
    if(selectedPage!="npcinfo") return;
    const auto viewport=previewWindowSize();const auto value=npcInfoProperties();
    previewCanvas->setMenuRect({QPoint(),viewport});previewCanvas->setBackgroundImage(QString());previewCanvas->setWindowStretch(false);
    components.clear();PreviewComponent sample;sample.editorId="menu/window";sample.name="NpcInfoPanel";sample.type="NpcInfo";
    sample.rect={QPoint(),viewport};sample.movable=sample.resizable=false;sample.fontSize=12;
    const int state=toolTipPreviewCombo->currentIndex();sample.visible=state!=7;
    sample.text=state==5?QString():tr("目标名称（示例）");
    sample.rawValues={{"width",QString::number(value.width)},{"height",QString::number(value.height)},{"alignx",QString::number(value.alignX)},{"aligny",QString::number(value.alignY)},{"state",QString::number(state)}};
    components.append(sample);
    const QRect bar(viewport.width()/2-value.width/2+value.alignX,value.alignY,value.width,value.height);
    auto* information=findChild<QLabel*>("conversationLayoutResult");information->setVisible(true);information->setProperty("npcInfoBar",bar);
    information->setText(tr("血条视口矩形：(%1,%2) %3×%4。名称始终按视口居中，AlignX仅偏移血条。\n默认140×18、偏移0/50；旧无效尺寸整组回退，原文保留。字体12和颜色固定；名称、生命与目标是临时样例，实际字形和输入以软件联测为准。")
        .arg(bar.x()).arg(bar.y()).arg(bar.width()).arg(bar.height()));
}
