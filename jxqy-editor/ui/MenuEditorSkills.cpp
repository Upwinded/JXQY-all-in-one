#include "MenuEditorWindow.h"
#include "../core/EditorAssetPath.h"
#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <cmath>

namespace
{
const QString minimapResource="ini/ui/mobile/skills/minimap.ini";
const QString minimapOwner="skills/minimap";
QMap<QString,QString> skillsRoles()
{
    QMap<QString,QString> roles{{"attackBtn","RoundButton"},{"sitBtn","RoundButton"},{"rightJumpBtn","DragRoundButton"}};
    for(int i=1;i<=5;++i)roles["skillBtn"+QString::number(i)]="DragRoundButton";
    for(int i=1;i<=4;++i)roles["fastBtn"+QString::number(i)]="TextButton";
    return roles;
}
}

void MenuEditorWindow::prepareSkillsControls()
{
    if(selectedPage!="skills")return;
    for(const auto& existing:currentDefinition.components)if(existing.editorId==minimapOwner)return;
    MenuComponentDefinition component;
    component.editorId=minimapOwner;component.name="minimapButton";component.type="RoundButton";
    component.file=minimapResource;component.runtimeDefault=true;
    currentDefinition.components.append(component);
}

QSet<QString> MenuEditorWindow::skillsFields(const QString& owner) const
{
    if(owner=="menu/window")
    {
        auto fields=mapThumbnailFields(owner);fields.insert("indicate");return fields;
    }
    const auto roles=skillsRoles();
    for(const auto& component:currentDefinition.components)if(component.editorId==owner)
    {
        if(owner==minimapOwner)
        {
            if(!pageResources.hasComponentData(minimapResource))return {};
        }
        else if(roles.value(component.name)!=component.type)return {};
        QSet<QString> fields{"left","top","width","height","flat"};
        const auto props=const_cast<MenuEditorWindow*>(this)->propertiesForResource(component.file);
        const bool flat=QStringList{"1","true","yes","on"}.contains(props.rawValues.value("flat").toLower());
        if(!flat)
        {
            fields.unite({"image","bitmap","stretch","animate"});
            if(!props.animate)
            {
                fields.unite({"kind","up","down"});
                if(props.kind.compare("trackbtn",Qt::CaseInsensitive)==0)fields.insert("track");
            }
        }
        if(component.type=="TextButton")fields.insert("font");
        else fields.unite({"range","text","icon","iconimage"});
        return fields;
    }
    return {};
}

void MenuEditorWindow::applySkillsProperties(const QString& resource,ComponentIniProperties& props) const
{
    applyJoystickIntegers(props);
    const auto integer=[&](const char* key,int fallback)
    {
        try {return static_cast<int>(std::stol(props.rawValues.value(key).toStdString(),nullptr,0));}
        catch(const std::exception&){return fallback;}
    };
    props.up=integer("up",0);props.down=integer("down",1);props.track=integer("track",1);props.font=integer("font",18);
    QString localWindow;resolveLocalResourcePath(currentDefinition.windowFile,localWindow);
    const bool window=EditorAssetPath::comparisonKey(resource)==EditorAssetPath::comparisonKey(localWindow)
        ||EditorAssetPath::comparisonKey(resource)==EditorAssetPath::comparisonKey(pageResources.readable(currentDefinition.windowFile));
    if(!window&&(!props.stretchSet||props.rawValues.value("stretch").isEmpty()))props.stretch=true;
}

bool MenuEditorWindow::validateSkills(QString& error)
{
    const auto present=[&](const QString& reference)
    {
        QString local;resolveLocalResourcePath(reference,local);
        return pageResources.hasComponentData(reference)||(modifiedComponentInis.contains(local)&&modifiedComponentInis.value(local).stagedForWrite);
    };
    if(!present(currentDefinition.windowFile)){error=tr("移动操作窗口配置缺失或为空。");return false;}
    auto roles=skillsRoles();QSet<QString> names;bool minimap=false;
    for(const auto& component:currentDefinition.components)
    {
        if(component.name.isEmpty()||names.contains(component.name)||component.type.isEmpty())
        {error=tr("移动操作组件身份重复或无效。");return false;}
        names.insert(component.name);
        if(component.editorId==minimapOwner)
        {
            minimap=component.name=="minimapButton"&&component.type=="RoundButton"&&component.file==minimapResource&&component.runtimeDefault;
            continue;
        }
        if(component.name=="minimapButton") {error=tr("小地图按钮由代码创建，不能追加到聚合。");return false;}
        if(!roles.contains(component.name))continue;
        if(roles.take(component.name)!=component.type||!present(component.file))
        {error=tr("移动操作固定组件缺失、类型错误或配置为空：%1").arg(component.name);return false;}
        const auto props=propertiesForResource(component.file);
        if(props.width<=0||props.height<=0||(component.type!="TextButton"&&props.range<=0))
        {error=tr("移动操作组件尺寸或范围无效：%1").arg(component.name);return false;}
    }
    const auto window=propertiesForResource(currentDefinition.windowFile);
    if(!roles.isEmpty()||!minimap||window.width<=0||window.height<=0)
    {error=tr("移动操作固定结构不完整，保留原内容只读。");return false;}
    const auto external=collectExternalResourceReferences();
    if(!external.complete||external.paths.contains(pageEntryResource.toCaseFolded())||external.paths.contains(minimapResource))
    {error=tr("移动操作固定入口或小地图文件被其它菜单共享，已限制写入。");return false;}
    for(const auto& root:pageResources.readRoots)
    {
        if(EditorAssetPath::logicalComparisonKey(root)==EditorAssetPath::logicalComparisonKey(assetsBasePath))break;
        QString path;
        if(EditorAssetPath::resolveLogicalResourcePath(root,minimapResource,path)&&QFileInfo(path).isFile())
        {error=tr("UI依赖优先会遮蔽固定小地图覆盖，已限制写入。");return false;}
    }
    return true;
}

bool MenuEditorWindow::stageSkillsFields(const QString& owner,const ComponentIniProperties& original,ComponentIniProperties& value)
{
    if(owner==minimapOwner)
    {
        QString error;if(!validateSkills(error)){pageContextLabel->setText(error);return false;}
    }
    const auto fields=skillsFields(owner);
    for(const auto& key:QStringList{"left","top"})if(value.rawValues.value(key)!=original.rawValues.value(key))
    {
        const int number=value.rawValues.value(key).isEmpty()?0:value.rawValues.value(key).toInt(nullptr,0);
        if(key=="left")value.left=number;else value.top=number;
    }
    const auto number=[&](const char* key,int before,int after)
    {if(fields.contains(key)&&before!=after)value.rawValues[key]=QString::number(after);};
    number("left",original.left,value.left);number("top",original.top,value.top);number("width",original.width,value.width);number("height",original.height,value.height);
    number("range",original.range,value.range);number("font",original.font,value.font);number("alignx",original.alignX,value.alignX);number("aligny",original.alignY,value.alignY);
    number("up",original.up,value.up);number("down",original.down,value.down);number("track",original.track,value.track);
    const auto text=[&](const char* key,const QString& before,const QString& after)
    {if(fields.contains(key)&&before!=after)value.rawValues[key]=after;};
    text("image",original.image,value.image);text("bitmap",original.bitmap,value.bitmap);text("align",original.align,value.align);
    text("kind",original.kind,value.kind);text("text",original.text,value.text);text("icon",original.icon,value.icon);text("iconimage",original.iconImage,value.iconImage);
    if(fields.contains("stretch")&&value.stretch!=original.stretch)value.rawValues["stretch"]=value.stretch?"true":"false";
    if(fields.contains("animate")&&value.animate!=original.animate)value.rawValues["animate"]=value.animate?"true":"false";
    for(const auto& key:fields)
    {
        const auto raw=value.rawValues.value(key);if(raw==original.rawValues.value(key)||raw.isEmpty())continue;
        bool parsed=false,valid=true;
        if(QStringList{"left","top","width","height","range","font","up","down","track","alignx","aligny","nineslice","nineslicewidth","windowmargin","windowverticalmargin"}.contains(key))
        {
            const int n=raw.toInt(&parsed,0);valid=parsed&&(!QStringList{"width","height","range","font"}.contains(key)||n>0);
        }
        if(key=="scale"){const double n=raw.toDouble(&parsed);valid=parsed&&std::isfinite(n)&&n>0;}
        if(QStringList{"flat","stretch","animate","keepaspect","fittowindow"}.contains(key))valid=QStringList{"1","0","true","false","yes","no","on","off"}.contains(raw.toLower());
        if(!valid){pageContextLabel->setText(tr("移动操作显示字段无效，修改未应用：%1").arg(key));return false;}
    }
    if(fields.contains("keepaspect")&&value.rawValues.value("keepaspect")!=original.rawValues.value("keepaspect"))
        value.keepAspect=QStringList{"1","true","yes","on"}.contains(value.rawValues.value("keepaspect").toLower());
    return true;
}

QStringList MenuEditorWindow::skillsPreviewStates() const
{
    return {tr("布局编辑（全部操作控件）"),tr("空技能槽（示例）"),tr("按住（按钮示例）"),tr("技能与冷却（示例色块）"),tr("快捷目标与小地图开启（示例）"),tr("方向图片（示例）"),tr("操作范围（辅助）")};
}

void MenuEditorWindow::updateSkillsPreview(QList<PreviewComponent>& components)
{
    if(selectedPage!="skills")return;
    const auto viewport = previewWindowSize();
    const auto window=propertiesForResource(currentDefinition.windowFile);
    if(window.width<=0||window.height<=0)return;
    const QRect panel = previewModeCombo && previewModeCombo->currentIndex() == 0
        ? QRect(0, 0, qMax(1, window.width), qMax(1, window.height)) : previewPanelRectangle(window,viewport);
    previewInterfaceOrigin = panel.topLeft();float sx=float(panel.width())/window.width,sy=float(panel.height())/window.height;
    if(window.keepAspect)sx=sy=qMin(sx,sy);
    const double uniform = qMin(double(panel.width()) / window.width, double(panel.height()) / window.height);
    const QPoint offset(window.keepAspect ? (panel.width() - int(std::round(window.width * uniform))) / 2 : 0,
        window.keepAspect ? (panel.height() - int(std::round(window.height * uniform))) / 2 : 0);
    previewCanvas->setMenuRect({QPoint(),panel.size()});bool numeric=false;const int align=window.align.toInt(&numeric,0);
    previewCanvas->setWindowStretch(window.stretch||window.align.compare("alClient",Qt::CaseInsensitive)==0||(numeric&&align==1),window.keepAspect);
    const int state=toolTipPreviewCombo->currentIndex();QVariantMap rectangles;
    for(auto& component:components)
    {
        ComponentIniProperties props;for(const auto& definition:currentDefinition.components)if(definition.editorId==component.editorId){props=propertiesForResource(definition.file);break;}
        component.rect={int(props.left*sx)+offset.x(),int(props.top*sy)+offset.y(),int(props.width*sx),int(props.height*sy)};
        component.movable=component.resizable=false;component.rawValues["skillsstate"]=QString::number(state);component.rawValues["skillsrange"]=QString::number(props.range);
        component.sampleImage=true;component.imagePath.clear();component.image=QPixmap();
        const auto path=props.image.isEmpty()?props.bitmap:props.image;
        const int up=props.up,down=props.down,track=props.kind.compare("trackbtn",Qt::CaseInsensitive)==0?props.track:-1;
        const QList<int> order=props.animate?QList<int>{0}:state==2?QList<int>{down,track,up}:QList<int>{up,track,down};
        for(int frame:order)if(frame>=0){component.image=previewCanvas->loadImageFromGamePath(path,frame);if(!component.image.isNull())break;}
        component.secondaryImagePath=props.iconImage;
        if(component.secondaryImagePath.isEmpty()&&!props.icon.isEmpty())
        {
            auto name=props.icon.toLower();if(name=="minimap")name="map";
            component.secondaryImagePath=name.contains('/')||name.contains('\\')||name.contains('.')?props.icon:"image/ui/mobile/icon_"+name+".png";
        }
        component.secondaryImageFrame=0;component.text=props.text;component.stretch=props.stretch;
        if(component.type=="TextButton")
        {
            component.visible=state==0||state==4;component.text=tr("目标（临时示例）");component.color=component.name.endsWith('2')?0xFFFF0000:0xFF000000;
        }
        if(component.name.startsWith("skillBtn")&&state==3)component.rawValues["skillsfilled"]="true";
        if(component.editorId==minimapOwner)component.rawValues["skillschecked"]=state==4?"true":"false";
        rectangles[component.name]=component.rect.translated(panel.topLeft());
    }
    if(state==5)
    {
        PreviewComponent indication;indication.editorId="skills/indicate-sample";indication.type="ImageContainer";indication.name="indicateSample";
        indication.imagePath=window.rawValues.value("indicate");const auto image=previewCanvas->loadImageFromGamePath(indication.imagePath);
        indication.rect={panel.width()/2-image.width()/2,panel.height()/2-image.height(),image.width(),image.height()};indication.movable=indication.resizable=false;components.append(indication);
    }
    auto* info=findChild<QLabel*>("conversationLayoutResult");info->setVisible(true);info->setProperty("skillsPanel",panel);info->setProperty("skillsControls",rectangles);
    info->setText(tr("移动操作窗口：(%1,%2) %3×%4；12个聚合控件＋代码创建的小地图按钮。\n画布为父窗口局部坐标；圆形Range独立，图标覆盖背景，已有技能与冷却覆盖图标／文字。样例、目标文字、选中标记及方向图不保存；动画取代表帧，Qt字形和方向世界投影以运行时软件联测为准。")
        .arg(panel.x()).arg(panel.y()).arg(panel.width()).arg(panel.height()));
}
