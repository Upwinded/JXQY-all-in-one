#include "MenuEditorWindow.h"
#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <cmath>

QSet<QString> MenuEditorWindow::joystickFields(const QString& owner) const
{
    if(owner=="menu/window") return mapThumbnailFields(owner);
    for(const auto& component:currentDefinition.components)
        if(component.editorId==owner&&component.name=="joystick"&&component.type=="Joystick")
            return {"left","top","width","height","range","image","baseimage","thumbimage"};
    return {};
}

void MenuEditorWindow::applyJoystickIntegers(ComponentIniProperties& value) const
{
    const auto integer=[&](const char* key,int fallback)
    {try{return static_cast<int>(std::stol(value.rawValues.value(key).toStdString(),nullptr,0));}catch(const std::exception&){return fallback;}};
    value.left=integer("left",0);value.top=integer("top",0);
    value.width=integer("width",0);value.height=integer("height",0);
    value.alignX=integer("alignx",0);value.alignY=integer("aligny",0);value.range=integer("range",50);
}

bool MenuEditorWindow::validateJoystick(QString& error)
{
    const auto present=[&](const QString& reference)
    {
        QString local;resolveLocalResourcePath(reference,local);
        return pageResources.hasComponentData(reference)||(modifiedComponentInis.contains(local)&&modifiedComponentInis.value(local).stagedForWrite);
    };
    if(!present(currentDefinition.windowFile)) {error=tr("移动摇杆窗口配置缺失或为空。");return false;}
    const auto window=propertiesForResource(currentDefinition.windowFile);
    if(window.width<=0||window.height<=0) {error=tr("移动摇杆窗口或操作范围无效，保留原内容只读。");return false;}
    QSet<QString> names;bool joystick=false;
    for(const auto& component:currentDefinition.components)
    {
        if(component.name.isEmpty()||names.contains(component.name)||component.type.isEmpty()) {error=tr("移动摇杆组件名称重复或类型缺失。");return false;}
        names.insert(component.name);
        if(component.name!="joystick") continue;
        if(component.type!="Joystick"||!present(component.file)) {error=tr("移动摇杆固定组件缺失、类型错误或配置为空。");return false;}
        const auto properties=propertiesForResource(component.file);
        if(properties.width<=0||properties.height<=0||properties.range<=0) {error=tr("移动摇杆窗口或操作范围无效，保留原内容只读。");return false;}
        joystick=true;
    }
    if(!joystick) {error=tr("移动摇杆固定组件缺失、类型错误或配置为空。");return false;}
    const auto references=collectExternalResourceReferences();
    if(!references.complete||references.paths.contains(pageEntryResource.toCaseFolded()))
    {error=tr("移动摇杆入口还被其它菜单引用，不能拆分固定入口，已限制写入。");return false;}
    return true;
}

bool MenuEditorWindow::stageJoystickFields(const QString& owner,const ComponentIniProperties& original,ComponentIniProperties& value)
{
    const auto fields=joystickFields(owner);
    for(const auto& key:QStringList{"left","top"}) if(value.rawValues.value(key)!=original.rawValues.value(key))
    {
        const int number=value.rawValues.value(key).isEmpty()?0:value.rawValues.value(key).toInt(nullptr,0);
        if(key=="left") value.left=number;else value.top=number;
    }
    const auto integer=[&](const char* key,int before,int after)
    {if(fields.contains(key)&&before!=after)value.rawValues[key]=QString::number(after);};
    integer("left",original.left,value.left);integer("top",original.top,value.top);
    integer("width",original.width,value.width);integer("height",original.height,value.height);
    integer("range",original.range,value.range);integer("alignx",original.alignX,value.alignX);integer("aligny",original.alignY,value.alignY);
    const auto text=[&](const char* key,const QString& before,const QString& after)
    {if(fields.contains(key)&&before!=after)value.rawValues[key]=after;};
    text("image",original.image,value.image);text("bitmap",original.bitmap,value.bitmap);text("align",original.align,value.align);
    text("baseimage",original.baseImage,value.baseImage);text("thumbimage",original.thumbImage,value.thumbImage);
    if(fields.contains("stretch")&&value.stretch!=original.stretch)value.rawValues["stretch"]=value.stretch?"true":"false";
    for(const auto& key:fields)
    {
        const auto text=value.rawValues.value(key);
        if(text==original.rawValues.value(key)||text.isEmpty()) continue;
        bool valid=true,parsed=false;
        if(QStringList{"width","height","range","nineslice","nineslicewidth","windowmargin","windowverticalmargin","alignx","aligny","left","top"}.contains(key))
        {
            const int number=text.toInt(&parsed,0);
            valid=parsed&&(!QStringList{"width","height","range"}.contains(key)||number>0);
        }
        if(key=="scale") {const double number=text.toDouble(&parsed);valid=parsed&&std::isfinite(number)&&number>0;}
        if(QStringList{"keepaspect","stretch","fittowindow"}.contains(key)) valid=QStringList{"true","false","1","0","yes","no","on","off"}.contains(text.toLower());
        if(!valid) {pageContextLabel->setText(tr("移动摇杆显示字段无效，修改未应用：%1").arg(key));return false;}
    }
    if(fields.contains("keepaspect")&&value.rawValues.value("keepaspect")!=original.rawValues.value("keepaspect"))
        value.keepAspect=QStringList{"true","1","yes","on"}.contains(value.rawValues.value("keepaspect").toLower());
    return true;
}

QStringList MenuEditorWindow::joystickPreviewStates() const
{
    return {tr("未触摸（摇杆示例）"),tr("按住中心（摇杆示例）"),tr("移动（摇杆示例）"),tr("命中区域（辅助）")};
}

void MenuEditorWindow::updateJoystickPreview(QList<PreviewComponent>& components)
{
    if(selectedPage!="joystick") return;
    const auto viewport = previewWindowSize();
    const auto window=propertiesForResource(currentDefinition.windowFile);const QRect panel = previewModeCombo && previewModeCombo->currentIndex() == 0
        ? QRect(0, 0, qMax(1, window.width), qMax(1, window.height)) : previewPanelRectangle(window,viewport);
    previewInterfaceOrigin = panel.topLeft();
    float sx=window.width>0?float(panel.width())/window.width:1,sy=window.height>0?float(panel.height())/window.height:1;
    if(window.keepAspect) sx=sy=qMin(sx,sy);
    const double uniform = qMin(double(panel.width()) / qMax(1, window.width), double(panel.height()) / qMax(1, window.height));
    const QPoint offset(window.keepAspect ? (panel.width() - int(std::round(window.width * uniform))) / 2 : 0,
        window.keepAspect ? (panel.height() - int(std::round(window.height * uniform))) / 2 : 0);
    previewCanvas->setMenuRect({QPoint(),panel.size()});
    bool numeric=false;const int align=window.align.toInt(&numeric,0);
    previewCanvas->setWindowStretch(window.stretch||window.align.compare("alClient",Qt::CaseInsensitive)==0||(numeric&&align==1),window.keepAspect);
    QRect joystick;
    for(auto& component:components)
    {
        ComponentIniProperties properties;for(const auto& definition:currentDefinition.components)if(definition.editorId==component.editorId){properties=propertiesForResource(definition.file);break;}
        component.rect={int(properties.left*sx)+offset.x(),int(properties.top*sy)+offset.y(),int(properties.width*sx),int(properties.height*sy)};
        component.movable=component.resizable=false;
        if(component.type=="Joystick"&&component.name=="joystick")
        {
            joystick=component.rect.translated(panel.topLeft());
            component.rawValues["joysticksample"]=QString::number(toolTipPreviewCombo->currentIndex());
            component.rawValues["joystickrange"]=QString::number(properties.range);
        }
    }
    auto* information=findChild<QLabel*>("conversationLayoutResult");information->setVisible(true);
    information->setProperty("joystickPanel",panel);information->setProperty("joystickRect",joystick);
    information->setText(tr("窗口视口矩形：(%1,%2) %3×%4；摇杆：(%5,%6) %7×%8。\n画布为窗口局部坐标；Range是操作圆直径，与图像宽高独立。底图固定拉伸，摇杆头保持原图尺寸。样例不保存；调整窗口时游戏会重读配置并重建摇杆。")
        .arg(panel.x()).arg(panel.y()).arg(panel.width()).arg(panel.height()).arg(joystick.x()).arg(joystick.y()).arg(joystick.width()).arg(joystick.height()));
}
