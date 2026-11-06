#include "MenuEditorWindow.h"
#include "../core/EditorAssetPath.h"
#include "../../src/Game/Menu/ControllerHelpLayout.h"
#include "../../src/Game/Menu/ControllerHelpContent.h"
#include "../../src/File/ini.h"
#include <QAction>
#include <QComboBox>
#include <QFile>
#include <QLabel>

QSet<QString> MenuEditorWindow::controllerHelpFields() const
{
    QSet<QString> fields;for(const auto& key:ControllerHelpLayout::keys())fields.insert(QString::fromStdString(key));return fields;
}

void MenuEditorWindow::recordControllerHelpDiskState()
{
    controllerHelpDiskState.clear();
    for(const auto& path:QStringList{currentFilePath,pageTargetPath()})
    {
        QFile file(path);const bool exists=QFileInfo(path).exists();
        const auto bytes=file.open(QIODevice::ReadOnly)?file.readAll():QByteArray();
        controllerHelpDiskState[path]=qMakePair(exists,bytes);
    }
}

bool MenuEditorWindow::controllerHelpDiskMatches() const
{
    for(auto it=controllerHelpDiskState.cbegin();it!=controllerHelpDiskState.cend();++it)
    {
        QFile file(it.key());const bool exists=QFileInfo(it.key()).exists();
        if(exists!=it.value().first||(exists&&(!file.open(QIODevice::ReadOnly)||file.readAll()!=it.value().second)))return false;
    }
    return true;
}

void MenuEditorWindow::prepareControllerHelpDefaults()
{
    if(selectedPage!="controllerhelp")return;
    const auto properties=propertiesForResource(currentDefinition.windowFile);
    QString target;resolveLocalResourcePath(currentDefinition.windowFile,target);auto& modified=modifiedComponentInis[target];
    if(modified.stagedForWrite)return;
    modified.properties=properties;modified.resourcePath=currentDefinition.windowFile;
    modified.sourcePath=pageResources.readable(currentDefinition.windowFile);
    modified.owners.insert("menu/window");modified.ownerComponentTypes["menu/window"]="MenuWindow";
}

bool MenuEditorWindow::stageControllerHelpProperties(const ComponentIniProperties& proposed)
{
    const auto original=propertiesForResource(currentDefinition.windowFile);auto value=original;
    for(const auto& key:controllerHelpFields())
    {
        const auto text=proposed.rawValues.value(key);
        if(text==original.rawValues.value(key))continue;
        if(!ControllerHelpLayout::valid(key.toStdString(),text.toStdString()))
        {pageContextLabel->setText(tr("手柄帮助显示字段无效，修改未应用：%1").arg(key));return false;}
        value.rawValues[key]=text;
    }
    if(value.rawValues==original.rawValues)return true;
    QString target;resolveLocalResourcePath(currentDefinition.windowFile,target);auto& modified=modifiedComponentInis[target];
    modified.properties=value;modified.resourcePath=currentDefinition.windowFile;modified.stagedForWrite=true;
    modified.owners.insert("menu/window");modified.ownerComponentTypes["menu/window"]="MenuWindow";componentIniCache.remove(target);return true;
}

bool MenuEditorWindow::validateControllerHelp(QString& error)
{
    if(currentDefinition.menuName!="ControllerHelpOverlay"||currentDefinition.windowFile!=ControllerHelpLayout::File
        ||!currentDefinition.components.isEmpty()||!currentDefinition.subMenus.isEmpty())
    {error=tr("手柄帮助使用固定单文件，正文、动作和结构由游戏管理。");return false;}
    if(!documentStructureIssue.isEmpty()){error=documentStructureIssue;return false;}
    const auto references=collectExternalResourceReferences();
    if(!references.complete||references.paths.contains(currentDefinition.windowFile.toCaseFolded()))
    {error=tr("手柄帮助布局被其它菜单共享，固定入口不能拆分，已限制写入。");return false;}
    return true;
}

QStringList MenuEditorWindow::controllerHelpPreviewStates() const
{
    return {tr("Xbox标签（临时示例）"),tr("PlayStation标签（临时示例）"),tr("Nintendo标签（临时示例）")};
}

void MenuEditorWindow::updateControllerHelpPreview(QList<PreviewComponent>& components)
{
    if(selectedPage!="controllerhelp")return;
    const auto size = previewWindowSize();const auto properties=propertiesForResource(currentDefinition.windowFile);
    ControllerHelpLayout display;if(documentStructureIssue.isEmpty())for(auto it=properties.rawValues.cbegin();it!=properties.rawValues.cend();++it)display.values[it.key().toStdString()]=it.value().toStdString();
    const auto geometry=display.resolve(size.width(),size.height());components.clear();PreviewComponent sample;
    const auto tint = [](unsigned int value) { return "0x" + QString::number(value, 16); };
    propertyEditor->setDisplayDefaults({
        {"outermargin", QString::number(geometry.outer)}, {"contentpadding", QString::number(geometry.padding)},
        {"titlefontsize", QString::number(geometry.titleFont)}, {"textfontsize", QString::number(geometry.textFont)},
        {"linespacing", QString::number(geometry.lineHeight - geometry.textFont)}, {"columngap", QString::number(geometry.columnGap)},
        {"backgroundcolor", tint(geometry.background)}, {"panelcolor", tint(geometry.panelColor)},
        {"accentcolor", tint(geometry.accent)}, {"titlecolor", tint(geometry.title)},
        {"sectioncolor", tint(geometry.section)}, {"textcolor", tint(geometry.text)}, {"mutedtextcolor", tint(geometry.muted)}});
    sample.editorId="menu/window";sample.type="ControllerHelp";sample.name="ControllerHelpOverlay";sample.rect={QPoint(),size};sample.movable=sample.resizable=false;
    if(documentStructureIssue.isEmpty())sample.rawValues=properties.rawValues;sample.imagePath=QString::fromStdString(display.image());sample.stretch=false;
    const int theme=qBound(0,toolTipPreviewCombo->currentIndex(),2);
    for(const auto* lines:{&ControllerHelpContent::world(),&ControllerHelpContent::menu()})for(const auto& line:*lines)
        sample.listBoxItems.append(QString::fromStdString(line.previewLabels[theme].empty()?line.description:"["+line.previewLabels[theme]+"]  "+line.description));
    components.append(sample);previewCanvas->setMenuRect({QPoint(),size});previewCanvas->setBackgroundImage(QString());previewCanvas->setWindowStretch(false);
    const auto image=previewCanvas->loadImageFromGamePath(sample.imagePath);const bool diagram=size.width()>=1100&&!image.isNull();
    auto* information=findChild<QLabel*>("conversationLayoutResult");information->setVisible(true);
    const auto r=geometry.imageRectangle(image.width(),image.height());information->setProperty("controllerHelpImage",QRect(r.x,r.y,r.w,r.h));
    information->setProperty("controllerHelpPanel",QRect(geometry.panel.x,geometry.panel.y,geometry.panel.w,geometry.panel.h));
    information->setProperty("controllerHelpBranch",diagram?"image":geometry.twoColumns?"two-columns":"single-column");
    QStringList invalid;for(const auto& key:display.invalid())invalid.append(QString::fromStdString(key));
    information->setText(tr("手柄帮助：%1；标题字号%2，正文字号%3，行高%4。\n640／1100分支、18条世界说明及12条菜单说明由游戏固定。空值随视口默认；图片失败回退文字。标签样例不保存，Qt与SDL字形及小视口溢出须分别核对。%5")
        .arg(diagram?tr("图片"):geometry.twoColumns?tr("两列"):tr("单列")).arg(geometry.titleFont).arg(geometry.textFont).arg(geometry.lineHeight)
        .arg(invalid.isEmpty()?QString():tr("\n以下旧字段无效，预览采用默认，原文保留：%1").arg(invalid.join(", "))));
}
