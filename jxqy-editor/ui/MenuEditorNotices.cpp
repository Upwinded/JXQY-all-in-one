#include "MenuEditorWindow.h"
#include "../core/INIFileEditor.h"
#include "../core/EditorAssetPath.h"
#include "../../src/Game/Menu/SystemNoticeLayout.h"
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QToolBar>
#include <QUuid>

bool MenuEditorWindow::isNoticePage(const QString& page)
{
    return page=="systemnotice"||page=="scriptmessages";
}

QSet<QString> MenuEditorWindow::noticeFields(const QString& owner) const
{
    if(owner=="menu/window")
    {
        QSet<QString> fields={"left","top","width","height","align","alignx","aligny"};
        if(selectedPage=="systemnotice") fields.unite({"backgroundcolor","topbordercolor","bottombordercolor"});
        return fields;
    }
    for(const auto& component:currentDefinition.components) if(component.editorId==owner&&component.name=="text"&&component.type=="Label")
    {
        QSet<QString> fields={"font","color","horizontalpadding"};
        if(selectedPage=="scriptmessages") fields.insert("linegap");
        return fields;
    }
    return {};
}

ComponentIniProperties MenuEditorWindow::noticeWindowProperties()
{
    auto result=propertiesForResource(currentDefinition.windowFile);ComponentIniProperties text;
    for(const auto& component:currentDefinition.components) if(component.name=="text"&&component.type=="Label") text=propertiesForResource(component.file);
    const int font=SystemNoticeLayout::integer(text.rawValues.value("font").toStdString(),18,1,256);
    const int gap=SystemNoticeLayout::integer(text.rawValues.value("linegap").toStdString(),3,0,1024);
    const auto defaults=SystemNoticeLayout::defaults(1280,720,selectedPage=="scriptmessages",font,gap);
    result.left=SystemNoticeLayout::integer(result.rawValues.value("left").toStdString(),defaults.panel.x);
    result.top=SystemNoticeLayout::integer(result.rawValues.value("top").toStdString(),defaults.panel.y);
    result.width=SystemNoticeLayout::integer(result.rawValues.value("width").toStdString(),defaults.panel.w,1);
    result.height=SystemNoticeLayout::integer(result.rawValues.value("height").toStdString(),defaults.panel.h,1);
    return result;
}

void MenuEditorWindow::prepareNoticeDefaults()
{
    if(!isNoticePage(selectedPage)||!pageResources.valid) return;
    const QString prefix="ini/ui/"+selectedPage+"/runtime-default/";
    if(currentDefinition.windowFile.isEmpty()) currentDefinition.windowFile=prefix+"window.ini";
    bool found=false;for(const auto& component:currentDefinition.components) found|=component.name=="text";
    if(!found)
    {
        MenuComponentDefinition component;component.editorId="component/"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        component.name="text";component.type="Label";component.file=prefix+"text.ini";component.runtimeDefault=true;
        currentDefinition.components.append(component);
    }
    const auto defaults=SystemNoticeLayout::defaults(1280,720,selectedPage=="scriptmessages");
    const auto prepare=[&](const QString& owner,const QString& file,const QString& type)
    {
        QString path;if(!resolveLocalResourcePath(file,path)) return;
        auto props=propertiesForResource(file);auto& value=modifiedComponentInis[path];if(value.stagedForWrite) return;
        const auto integer=[&](const char* key,int fallback,int minimum=-100000,int maximum=100000)
        {return SystemNoticeLayout::integer(props.rawValues.value(key).toStdString(),fallback,minimum,maximum);};
        props.left=integer("left",defaults.panel.x);props.top=integer("top",defaults.panel.y);
        props.width=integer("width",defaults.panel.w,1);props.height=integer("height",defaults.panel.h,1);
        props.font=integer("font",18,1,256);props.fontSet=true;
        if(!props.rawValues.contains("color")||props.rawValues.value("color").isEmpty()) props.color=defaults.color;
        value.properties=props;value.resourcePath=file;value.sourcePath=pageResources.readable(file);value.owners.insert(owner);value.ownerComponentTypes[owner]=type;
    };
    prepare("menu/window",currentDefinition.windowFile,"MenuWindow");
    for(const auto& component:currentDefinition.components) if(component.name=="text"&&component.type=="Label") prepare(component.editorId,component.file,"Label");
}

bool MenuEditorWindow::validateNotice(QString& error)
{
    if(!currentDefinition.subMenus.isEmpty()) {error=tr("通知不使用子菜单，本页只读。");return false;}
    INIFileEditor original;original.loadFromString(originalMenuFileLines.join('\n').toStdString(),INIFileEditor::UnrecognizedLinePolicy::Preserve);
    QString declaredWindow;EditorAssetPath::normalizeResourcePath(QString::fromStdString(original.get("menu","window")),declaredWindow);
    const auto available=[&](const QString& file,bool generatedDefault)
    {
        if(file.isEmpty()) return false;
        QString target;resolveLocalResourcePath(file,target);
        const auto staged=modifiedComponentInis.constFind(target);
        return generatedDefault||pageResources.hasComponentData(file)||(staged!=modifiedComponentInis.cend()&&staged->stagedForWrite);
    };
    const QString prefix="ini/ui/"+selectedPage+"/runtime-default/";
    if(!available(currentDefinition.windowFile,currentDefinition.windowFile.startsWith(prefix)&&declaredWindow!=currentDefinition.windowFile))
    {error=tr("通知引用的配置缺失或为空：%1").arg(currentDefinition.windowFile);return false;}
    int texts=0;
    for(const auto& component:currentDefinition.components) if(component.name=="text")
    {
        ++texts;
        if(component.type!="Label"||!available(component.file,component.runtimeDefault))
        {error=tr("通知固定text角色的类型或引用无效。保留原文件，仅供读取。");return false;}
    }
    if(texts!=1) {error=tr("通知需要且仅允许一个text角色，本页只读。");return false;}
    QList<QPair<QString,QString>> owners{{"menu/window",currentDefinition.windowFile}};
    for(const auto& component:currentDefinition.components) if(component.name=="text") owners.append({component.editorId,component.file});
    for(const auto& owner:owners)
    {
        const auto props=propertiesForResource(owner.second);
        for(const auto& key:noticeFields(owner.first)) if(!validNoticeValue(key,props.rawValues.value(key)))
        {error=tr("通知显示字段无效，本页只读：%1").arg(key);return false;}
    }
    return true;
}

bool MenuEditorWindow::validNoticeValue(const QString& key,const QString& text) const
{
    if(text.isEmpty()||key=="align") return true;
    const int minimum=key=="font"||key=="width"||key=="height"?1:key=="horizontalpadding"||key=="linegap"?0:-100000;
    const int maximum=key=="font"?256:key=="linegap"?1024:100000;
    return key.endsWith("color")?displayColor(text,0x12345678)==displayColor(text,0x87654321)
        :SystemNoticeLayout::integer(text.toStdString(),-100001,minimum,maximum)!=-100001;
}

bool MenuEditorWindow::stageNoticeFields(const QString& owner,const ComponentIniProperties& original,ComponentIniProperties& value)
{
    const auto fields=noticeFields(owner);
    const auto integer=[&](const char* key,int before,int after)
    {if(fields.contains(key)&&before!=after) value.rawValues[key]=QString::number(after);};
    integer("width",original.width,value.width);integer("height",original.height,value.height);integer("font",original.font,value.font);
    integer("alignx",original.alignX,value.alignX);integer("aligny",original.alignY,value.alignY);
    if(fields.contains("align")&&original.align!=value.align) {if(value.align.isEmpty()) value.rawValues.remove("align");else value.rawValues["align"]=value.align;}
    if(fields.contains("color")&&original.color!=value.color) value.rawValues["color"]="0x"+QString::number(value.color,16);
    for(const auto& key:fields)
    {
        if(value.rawValues.value(key)==original.rawValues.value(key)) continue;
        const auto text=value.rawValues.value(key);if(text.isEmpty()||key=="align") continue;
        if(!validNoticeValue(key,text)) {pageContextLabel->setText(tr("通知显示字段无效，修改未应用：%1").arg(key));return false;}
    }
    return true;
}



QStringList MenuEditorWindow::noticePreviewStates() const
{
    return {tr("首选布局输入"),tr("短通知（示例）"),tr("长中文通知（示例）"),tr("颜色与多条消息（示例）")};
}

void MenuEditorWindow::updateNoticePreview(QList<PreviewComponent>& components)
{
    const bool active=isNoticePage(selectedPage);if(!active) return;
    auto* information=findChild<QLabel*>("conversationLayoutResult");information->setVisible(true);
    const auto viewport=previewWindowSize();const bool script=selectedPage=="scriptmessages";
    ComponentIniProperties text;
    for(const auto& component:currentDefinition.components) if(component.name=="text"&&component.type=="Label") text=propertiesForResource(component.file);
    auto window=propertiesForResource(currentDefinition.windowFile);
    const int font=SystemNoticeLayout::integer(text.rawValues.value("font").toStdString(),18,1,256),gap=script?SystemNoticeLayout::integer(text.rawValues.value("linegap").toStdString(),3,0,1024):3;
    auto layout=SystemNoticeLayout::defaults(viewport.width(),viewport.height(),script,font,gap);
    propertyEditor->setDisplayDefaults({
        {"backgroundcolor", "0x" + QString::number(layout.background, 16)},
        {"topbordercolor", "0x" + QString::number(layout.topBorder, 16)},
        {"bottombordercolor", "0x" + QString::number(layout.bottomBorder, 16)}});
    layout.padding=SystemNoticeLayout::integer(text.rawValues.value("horizontalpadding").toStdString(),14,0);
    ComponentIniProperties geometry;
    geometry.left=SystemNoticeLayout::integer(window.rawValues.value("left").toStdString(),layout.panel.x);
    geometry.top=SystemNoticeLayout::integer(window.rawValues.value("top").toStdString(),layout.panel.y);
    geometry.width=SystemNoticeLayout::integer(window.rawValues.value("width").toStdString(),layout.panel.w,1);
    geometry.height=SystemNoticeLayout::integer(window.rawValues.value("height").toStdString(),layout.panel.h,1);
    geometry.align=window.align;geometry.alignX=window.alignX;geometry.alignY=window.alignY;
    const auto panel = previewModeCombo && previewModeCombo->currentIndex() == 0
        ? QRect(0, 0, qMax(1, geometry.width), qMax(1, geometry.height)) : previewPanelRectangle(geometry,viewport);
    previewInterfaceOrigin = panel.topLeft();layout.panel={panel.x(),panel.y(),panel.width(),panel.height()};
    previewCanvas->setMenuRect({QPoint(),panel.size()});previewCanvas->setWindowStretch(false);
    previewCanvas->setBackgroundColor(script?QColor(Qt::transparent):QColor::fromRgba(displayColor(window.rawValues.value("backgroundcolor"),layout.background)));
    components.clear();
    if(!script)
    {
        for(const auto& border:QList<QPair<QString,QPair<int,unsigned int>>>{{"top",{0,displayColor(window.rawValues.value("topbordercolor"),layout.topBorder)}},{"bottom",{panel.height()-1,displayColor(window.rawValues.value("bottombordercolor"),layout.bottomBorder)}}})
        {PreviewComponent line;line.editorId="notice-"+border.first;line.type="NoticeBorder";line.rect={0,border.second.first,panel.width(),1};line.color=border.second.second;line.movable=line.resizable=false;components.append(line);}
    }
    for(const auto& definition:currentDefinition.components) if(definition.name=="text"&&definition.type=="Label")
    {
        PreviewComponent value;value.editorId=definition.editorId;value.name="text";value.type="NoticeText";value.rect={QPoint(),panel.size()};value.fontSize=font;
        value.color=displayColor(text.rawValues.value("color"),layout.color);value.movable=value.resizable=false;
        const int state=toolTipPreviewCombo->currentIndex();
        value.listBoxItems={tr("系统：这是一条通知样例。")};
        if(state==2) value.listBoxItems={QString(80,QChar(0x6c5f))+tr("，长文末行。")};
        if(state==3) value.listBoxItems={tr("<color=Red>红色消息未闭合"),tr("第二条消息恢复默认颜色。"),tr("可见末行（示例）")};
        if(!script&&state==3) value.listBoxItems={tr("系统：<color=Red>彩色提示<color=Default>默认文字")};
        value.rawValues={{"noticescript",script?"1":"0"},{"horizontalpadding",QString::number(layout.padding)},{"linegap",QString::number(gap)}};
        components.append(value);
    }
    information->setProperty("noticePanel",panel);information->setProperty("noticeLineHeight",layout.lineHeight(script));
    information->setText(tr("视口位置：(%1,%2)，尺寸：%3×%4；文字宽度：%5，行高：%6。\n正文、期限、队列及输入策略由运行时管理；Qt字形度量以软件运行时为准。").arg(panel.x()).arg(panel.y()).arg(panel.width()).arg(panel.height()).arg(layout.contentWidth()).arg(layout.lineHeight(script)));
}
