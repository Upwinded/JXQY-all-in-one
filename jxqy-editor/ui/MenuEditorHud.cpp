#include "MenuEditorWindow.h"
#include "../core/INIFileEditor.h"
#include <QComboBox>
#include <QLabel>
#include <QRegularExpression>
#include <QToolBar>
#include <cmath>

namespace
{
bool validHudValue(const QString& key,const QString& value)
{
    if(value.isEmpty()) return true;
    if(QStringList{"horizontal","keepaspect","stretch","drawslot","centerimage","autonextline","autoshrink","centertext"}.contains(key))
        return QStringList{"true","false","1","0","yes","no","on","off"}.contains(value.toLower());
    bool parsed=false;
    if(key=="scale") {const double number=value.toDouble(&parsed);return parsed&&std::isfinite(number)&&number>0&&number<=100000;}
    if(QStringList{"left","top","width","height","font","minimumfont","nineslice","nineslicewidth","alignx","aligny","up","down"}.contains(key))
    {
        const int number=value.toInt(&parsed,0),minimum=QStringList{"font","minimumfont","width","height"}.contains(key)?1:QStringList{"nineslice","nineslicewidth","up","down"}.contains(key)?0:-100000;
        return parsed&&number>=minimum&&number<=(key=="font"||key=="minimumfont"?256:100000);
    }
    return true;
}
}

bool MenuEditorWindow::isHudPage(const QString& page)
{
    return page == "top" || page == "bottom" || page == "column";
}

QSet<QString> MenuEditorWindow::hudFields(const QString& owner) const
{
    QSet<QString> geometry={"left","top","width","height"};
    if(owner=="menu/window")
    {
        geometry.unite({"align","alignx","aligny","image","bitmap","stretch","scale","keepaspect","nineslice","nineslicewidth"});
        return geometry;
    }
    for(const auto& component:currentDefinition.components) if(component.editorId==owner)
    {
        const auto name=component.name,type=component.type;
        const bool button=QStringList{"equipBtn","goodsBtn","magicBtn","notesBtn","optionBtn","stateBtn","xiulianBtn"}.contains(name);
        if((selectedPage=="top"||selectedPage=="bottom")&&button&&type=="CheckBox")
        {
            if(name=="xiulianBtn"&&!pageResources.practiceAvailable()) return {};
            geometry.unite({"image","bitmap","stretch","up","down","font","text"});return geometry;
        }
        if(selectedPage=="bottom")
        {
            if(type=="Item"&&QRegularExpression("^(goodsItem[1-3]|magicItem[1-5])$").match(name).hasMatch())
            {geometry.unite({"font","color","stretch","keepaspect","centerimage","drawslot","backimage1","backimage2"});return geometry;}
            if(type=="ImageContainer"&&(QStringList{"goodsSeal","magicSeal"}.contains(name)||QRegularExpression("^keyCap[0-7]$").match(name).hasMatch()))
            {geometry.unite({"image","bitmap","stretch","keepaspect","frame","nineslice","nineslicewidth"});return geometry;}
            if(type=="Label"&&(QStringList{"goodsGroup","magicGroup"}.contains(name)||QRegularExpression("^key[0-7]$").match(name).hasMatch()))
            {geometry.unite({"font","color","text","autonextline","centertext","autoshrink","minimumfont"});return geometry;}
        }
        if(selectedPage=="column")
        {
            if(type=="ColumnImage"&&QStringList{"columnLife","columnThew","columnMana","columnRage"}.contains(name))
            {
                if(name=="columnRage"&&!pageResources.manifest.isFeatureEnabled("ragesystem",false)) return {};
                geometry.insert("horizontal");
                const auto props=const_cast<MenuEditorWindow*>(this)->propertiesForResource(component.file);
                if(QStringList{"1","true","yes","on"}.contains(props.rawValues.value("horizontal").toLower())) geometry.insert("fillcolor");
                else geometry.unite({"image","bitmap","stretch"});
                return geometry;
            }
            if(type=="TransImage"&&name=="image") {geometry.unite({"image","bitmap","stretch"});return geometry;}
            if(type=="Label"&&QStringList{"valueLife","valueThew","valueMana","valueRage"}.contains(name)&&!component.bind.isEmpty())
            {geometry.unite({"font","color","autonextline","centertext","autoshrink","minimumfont"});return geometry;}
        }
        break;
    }
    return {};
}

bool MenuEditorWindow::validateHud(QString& error)
{
    if(!pageResources.menuPages(selectedTheme).contains(selectedPage))
    {error=tr("当前功能组合未构造此HUD实例；配置仅供读取。");return false;}
    QMap<QString,QString> types;
    if(selectedPage=="bottom")
    {
        for(int i=1;i<=3;++i) types["goodsItem"+QString::number(i)]="Item";
        for(int i=1;i<=5;++i) types["magicItem"+QString::number(i)]="Item";
    }
    if(selectedPage=="column") for(const auto& name:QStringList{"columnLife","columnThew","columnMana"}) types[name]="ColumnImage";
    if(selectedPage=="top") for(const auto& name:QStringList{"equipBtn","goodsBtn","notesBtn","optionBtn"}) types[name]="CheckBox";
    QSet<QString> names;
    for(const auto& component:currentDefinition.components)
    {
        if(names.contains(component.name)||component.type.isEmpty()) {error=tr("HUD功能组件名称或类型重复／无效。");return false;}
        names.insert(component.name);
        if(types.contains(component.name))
        {
            if(types.take(component.name)!=component.type) {error=tr("HUD固定组件类型无效：%1").arg(component.name);return false;}
        }
        QString local;resolveLocalResourcePath(component.file,local);
        if(!pageResources.hasComponentData(component.file)&&(!modifiedComponentInis.contains(local)||!modifiedComponentInis.value(local).stagedForWrite))
        {error=tr("HUD组件配置缺失或为空：%1").arg(component.file);return false;}
        const auto props=propertiesForResource(component.file);
        if(!hudFields(component.editorId).isEmpty()&&(props.width<=0||props.height<=0)) {error=tr("HUD显示组件尺寸必须为正。");return false;}
        for(const auto& key:hudFields(component.editorId)) if(!validHudValue(key,props.rawValues.value(key))) {error=tr("HUD显示字段无效：%1").arg(key);return false;}
    }
    if(!types.isEmpty()) {error=tr("HUD缺少固定组件：%1").arg(types.firstKey());return false;}
    QString window;resolveLocalResourcePath(currentDefinition.windowFile,window);
    if(!pageResources.hasComponentData(currentDefinition.windowFile)&&(!modifiedComponentInis.contains(window)||!modifiedComponentInis.value(window).stagedForWrite))
    {error=tr("HUD窗口配置缺失或为空。");return false;}
    const auto windowProperties=propertiesForResource(currentDefinition.windowFile);
    for(const auto& key:hudFields("menu/window")) if(!validHudValue(key,windowProperties.rawValues.value(key)))
    {error=tr("HUD显示字段无效：%1").arg(key);return false;}
    return true;
}

bool MenuEditorWindow::stageFixedDisplayFields(const QString& owner,const ComponentIniProperties& original,ComponentIniProperties& value)
{
    const auto fields=editableFields(owner);
    const auto integer=[&](const char* key,int before,int after)
    {
        const bool cleared=QStringList{"left","top"}.contains(key)&&original.rawValues.value(key)!=value.rawValues.value(key)&&value.rawValues.value(key).isEmpty();
        if(fields.contains(key)&&before!=after&&!cleared) value.rawValues[key]=QString::number(after);
    };
    const auto text=[&](const char* key,const QString& before,const QString& after)
    {if(fields.contains(key)&&before!=after) {if(after.isEmpty()) value.rawValues.remove(key);else value.rawValues[key]=after;}};
    for(const auto& key:QStringList{"left","top"}) if(fields.contains(key)&&value.rawValues.value(key)!=original.rawValues.value(key))
    {
        if(key=="left") value.left=value.rawValues.value(key).isEmpty()?0:value.rawValues.value(key).toInt(nullptr,0);
        else value.top=value.rawValues.value(key).isEmpty()?0:value.rawValues.value(key).toInt(nullptr,0);
    }
    integer("left",original.left,value.left);integer("top",original.top,value.top);integer("width",original.width,value.width);integer("height",original.height,value.height);
    integer("font",original.font,value.font);integer("up",original.up,value.up);integer("down",original.down,value.down);integer("frame",original.frame,value.frame);
    integer("alignx",original.alignX,value.alignX);integer("aligny",original.alignY,value.alignY);
    text("align",original.align,value.align);text("image",original.image,value.image);text("bitmap",original.bitmap,value.bitmap);
    text("text",original.text,value.text);text("backimage1",original.backImage1,value.backImage1);text("backimage2",original.backImage2,value.backImage2);
    if(fields.contains("color")&&original.color!=value.color) value.rawValues["color"]="0x"+QString::number(value.color,16);
    if(fields.contains("stretch")&&original.stretch!=value.stretch) value.rawValues["stretch"]=value.stretch?"true":"false";
    if(fields.contains("centerimage")&&original.centerImage!=value.centerImage) value.rawValues["centerimage"]=value.centerImage?"true":"false";
    if((fields.contains("width")&&value.width<=0)||(fields.contains("height")&&value.height<=0)
        ||(fields.contains("font")&&(value.font<=0||value.font>256))) return false;
    for(const auto& key:fields) if(value.rawValues.value(key)!=original.rawValues.value(key)&&!validHudValue(key,value.rawValues.value(key)))
    {pageContextLabel->setText(tr("HUD显示字段无效，修改未应用：%1").arg(key));return false;}
    if(fields.contains("keepaspect")&&value.rawValues.value("keepaspect")!=original.rawValues.value("keepaspect"))
    {
        value.keepAspect=QStringList{"true","1","yes","on"}.contains(value.rawValues.value("keepaspect").toLower());
        value.keepAspectSet=!value.rawValues.value("keepaspect").isEmpty();
    }
    else if(fields.contains("keepaspect")&&original.keepAspect!=value.keepAspect) value.rawValues["keepaspect"]=value.keepAspect?"true":"false";
    for(const auto& key:QStringList{"horizontal","fillcolor"}) if(fields.contains(key)&&value.rawValues.value(key)!=original.rawValues.value(key))
    {
        const auto text=value.rawValues.value(key).trimmed();
        const bool valid=text.isEmpty()||(key=="horizontal"?QStringList{"true","false","1","0","yes","no","on","off"}.contains(text.toLower())
            :displayColor(text,0x12345678)==displayColor(text,0x87654321));
        if(!valid) {pageContextLabel->setText(tr("HUD显示字段无效，修改未应用：%1").arg(key));return false;}
    }
    return true;
}

QStringList MenuEditorWindow::hudPreviewStates() const
{
    return {tr("布局输入（HUD）"),tr("空快捷位／空状态条（示例）"),tr("占用与半满（示例）"),tr("数量／冷却与全满（示例）")};
}



void MenuEditorWindow::updateHudPreview(QList<PreviewComponent>& components)
{
    const bool active=isHudPage(selectedPage);if(!active) return;
    const auto viewport=previewWindowSize();
    const auto window=propertiesForResource(currentDefinition.windowFile);const QRect panel = previewModeCombo && previewModeCombo->currentIndex() == 0
        ? QRect(0, 0, qMax(1, window.width), qMax(1, window.height)) : previewPanelRectangle(window,viewport);
    previewInterfaceOrigin = panel.topLeft();
    const int state=toolTipPreviewCombo->currentIndex();
    const float sx = float(panel.width()) / qMax(1, window.width), sy = float(panel.height()) / qMax(1, window.height);
    const float scaleX = window.keepAspect ? qMin(sx, sy) : sx, scaleY = window.keepAspect ? qMin(sx, sy) : sy;
    const double uniform = qMin(double(panel.width()) / qMax(1, window.width), double(panel.height()) / qMax(1, window.height));
    const QPoint offset(window.keepAspect ? (panel.width() - int(std::round(window.width * uniform))) / 2 : 0,
                        window.keepAspect ? (panel.height() - int(std::round(window.height * uniform))) / 2 : 0);
    bool numericAlignment = false;
    const int alignment = window.align.toInt(&numericAlignment, 0);
    previewCanvas->setMenuRect({QPoint(), panel.size()});
    previewCanvas->setWindowStretch(window.stretch || window.align.compare("alClient", Qt::CaseInsensitive) == 0
        || (numericAlignment && alignment == 1), window.keepAspect);
    auto* information=findChild<QLabel*>("conversationLayoutResult");information->setVisible(true);information->setProperty("hudPanel",panel);
    information->setText(tr("HUD视口矩形：(%1,%2) %3×%4；状态条宿主：%5。\n数量、占用标记、冷却和数值仅为示例；布局变换后的画布只读，请用属性编辑输入。字体／实际图标以运行时软件联测为准。")
        .arg(panel.x()).arg(panel.y()).arg(panel.width()).arg(panel.height()).arg(pageResources.hudColumnHost(selectedTheme)));
    for(auto& component:components)
    {
        ComponentIniProperties props;MenuComponentDefinition definition;
        for(const auto& candidate:currentDefinition.components) if(candidate.editorId==component.editorId) {definition=candidate;props=propertiesForResource(candidate.file);break;}
        component.rect={int(props.left*scaleX)+offset.x(),int(props.top*scaleY)+offset.y(),int(props.width*scaleX),int(props.height*scaleY)};
        if(state>0||!qFuzzyCompare(scaleX,1.0f)||!qFuzzyCompare(scaleY,1.0f)||!offset.isNull()) component.movable=component.resizable=false;
        if(component.type=="ColumnImage")
        {
            component.imageFrame=0;
            component.color=displayColor(props.rawValues.value("fillcolor"),0xFF527D6A);
            component.rawValues["hudpercent"]=state==1?"0":state==2?"0.5":"1";
            component.rawValues["hudlag"]=state==2?"0.75":component.rawValues["hudpercent"];
            if(component.name=="columnRage") component.visible=pageResources.manifest.isFeatureEnabled("ragesystem",false);
        }
        if(component.type=="TransImage") {component.imageFrame=0;component.rawValues["hudalpha"]="72";}
        if(component.type=="CheckBox")
        {
            component.rawValues["hudcheckbox"]=effectiveQingyuTheme()?"1":"0";
            component.imageFrame=state==2||state==3?props.down:props.up;component.rawValues["hudchecked"]=state==2||state==3?"1":"0";
            if(component.name=="xiulianBtn"&&!pageResources.practiceAvailable()) component.visible=false;
        }
        if(component.type=="Item")
        {
            component.imagePath=props.backImage1;component.imageFrame=0;
            component.rawValues["hudslot"]=state>1?"1":"0";component.rawValues["hudcooldown"]=state==3&&component.name.startsWith("magic")?"0.5":"0";
            component.text=state==3&&component.name.startsWith("goods")?"99":"";
        }
        if(component.type=="Label"&&!definition.bind.isEmpty())
        {
            component.text=definition.format;const int first=component.text.indexOf("%d");if(first>=0) component.text.replace(first,2,state==1?"0":state==2?"50":"100");component.text.replace("%d","100");component.passiveTextPreview=true;
        }
    }
}
