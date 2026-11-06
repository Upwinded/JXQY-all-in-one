#include "MenuEditorWindow.h"
#include "../../src/Types/ComponentTypes.h"
#include <QAction>
#include <QComboBox>
#include <QLabel>
#include <cmath>
#include <cstdlib>

namespace
{
bool validValue(const QString& key,const QString& value)
{
    if(value.isEmpty()) return true;
    if(QStringList{"fittowindow","stretch","keepaspect","autonextline","autoshrink","centertext"}.contains(key))
        return QStringList{"0","1","false","true","no","yes","off","on"}.contains(value.toLower());
    bool parsed=false;
    if(key=="scale")
    {
        const double number=value.toDouble(&parsed);
        return parsed&&std::isfinite(number)&&number>0&&number<=100000;
    }
    if(QStringList{"left","top","width","height","alignx","aligny","font","minimumfont","up","down","frame","nineslice","nineslicewidth","windowmargin","windowverticalmargin"}.contains(key))
    {
        const int number=value.toInt(&parsed,0);
        const bool positive=QStringList{"width","height","font","minimumfont"}.contains(key);
        const bool signedValue=QStringList{"left","top","alignx","aligny"}.contains(key);
        return parsed&&number>=(positive?1:signedValue?-100000:0)&&number<=(key=="font"||key=="minimumfont"?256:100000);
    }
    return true;
}
}

QSet<QString> MenuEditorWindow::mapThumbnailFields(const QString& owner) const
{
    QSet<QString> geometry={"left","top","width","height"};
    if(owner=="menu/window")
    {
        const auto window=const_cast<MenuEditorWindow*>(this)->propertiesForResource(currentDefinition.windowFile);
        const QString align=window.align.toLower();
        bool numeric=false;const int number=align.toInt(&numeric,0);
        const QStringList names={"alnone","alclient","alleft","alright","altop","albottom","alltcorner","alrtcorner","allbcorner","alrbcorner","alcenter","alleftcenter","alrightcenter","altopcenter","albottomcenter"};
        const int effective=numeric?number:names.indexOf(align);
        if(effective>0&&effective<names.size())
        {
            if(effective!=4&&effective!=5) geometry.remove("left");
            if(effective!=2&&effective!=3) geometry.remove("top");
        }
        geometry.unite({"align","alignx","aligny","scale","keepaspect","image","bitmap","stretch","nineslice","nineslicewidth","fittowindow","windowmargin","windowverticalmargin"});
        return geometry;
    }
    for(const auto& definition:currentDefinition.components) if(definition.editorId==owner)
    {
        if(definition.name=="thumbnailContainer"&&definition.type=="ImageContainer")
            return pageResources.mapThumbnailLayout(selectedTheme)=="yycs-derived"?QSet<QString>():geometry;
        if(definition.name=="closeButton"&&QStringList{"Button","TextButton","FlatTextButton"}.contains(definition.type))
        {
            geometry.unite({"image","bitmap","stretch","up","down"});
            if(definition.type!="Button") geometry.unite({"font","color","text"});
            if(definition.type=="FlatTextButton") geometry.unite({"backgroundcolor","hoverbackgroundcolor","pressedbackgroundcolor"});
            return geometry;
        }
        if(definition.name=="headingFrame"&&definition.type=="ImageContainer")
        {
            geometry.unite({"image","bitmap","stretch","keepaspect","frame","nineslice","nineslicewidth"});
            return geometry;
        }
        if((definition.name=="heading"||definition.name=="hint")&&definition.type=="Label")
        {
            geometry.unite({"font","color","autonextline","centertext","autoshrink","minimumfont"});
            if(definition.bind.isEmpty()) geometry.insert("text");
            return geometry;
        }
        return {};
    }
    return {};
}

bool MenuEditorWindow::validateMapThumbnail(QString& error)
{
    QSet<QString> names;
    bool thumbnail=false;
    if(currentDefinition.windowFile.isEmpty()||!pageResources.hasComponentData(currentDefinition.windowFile))
    {
        QString local;resolveLocalResourcePath(currentDefinition.windowFile,local);
        if(!modifiedComponentInis.contains(local)||!modifiedComponentInis.value(local).stagedForWrite)
        {error=tr("小地图窗口配置缺失或为空。");return false;}
    }
    const auto valid=[&](const QString& owner,const ComponentIniProperties& properties)
    {
        for(const auto& key:mapThumbnailFields(owner))
        {
            const auto text=properties.rawValues.value(key);
            if(!validValue(key,text)||(key.endsWith("color")&&!text.isEmpty()&&displayColor(text,0x12345678)!=displayColor(text,0x87654321)))
            {error=tr("小地图显示字段无效：%1").arg(key);return false;}
        }
        return true;
    };
    if(!valid("menu/window",propertiesForResource(currentDefinition.windowFile))) return false;
    for(const auto& definition:currentDefinition.components)
    {
        if(!definition.name.isEmpty()&&names.contains(definition.name)) {error=tr("小地图组件名称重复：%1").arg(definition.name);return false;}
        names.insert(definition.name);
        if((definition.name=="thumbnailContainer"&&definition.type!="ImageContainer")
            ||(definition.name=="mapNameLabel"&&definition.type!="Label")
            ||(definition.name=="closeButton"&&!QStringList{"Button","TextButton","FlatTextButton"}.contains(definition.type)))
        {error=tr("小地图固定组件类型无效：%1").arg(definition.name);return false;}
        thumbnail|=definition.name=="thumbnailContainer";
        QString local;resolveLocalResourcePath(definition.file,local);
        if(!pageResources.hasComponentData(definition.file)&&(!modifiedComponentInis.contains(local)||!modifiedComponentInis.value(local).stagedForWrite))
        {error=tr("小地图组件配置缺失或为空：%1").arg(definition.file);return false;}
        if(!valid(definition.editorId,propertiesForResource(definition.file))) return false;
    }
    if(!thumbnail) error=tr("小地图缺少固定缩略图组件。");
    return thumbnail;
}

bool MenuEditorWindow::stageMapThumbnailFields(const QString& owner,const ComponentIniProperties& original,ComponentIniProperties& value)
{
    if(!stageFixedDisplayFields(owner,original,value)) return false;
    for(const auto& key:mapThumbnailFields(owner))
    {
        const auto text=value.rawValues.value(key);
        if(!validValue(key,text)||(key.endsWith("color")&&!text.isEmpty()&&displayColor(text,0x12345678)!=displayColor(text,0x87654321)))
        {pageContextLabel->setText(tr("小地图显示字段无效，修改未应用：%1").arg(key));return false;}
    }
    return true;
}

bool MenuEditorWindow::mapThumbnailCanvasIsEditable(const QString& owner) const
{
    if(toolTipPreviewCombo->currentIndex()>0) return false;
    if(pageResources.mapThumbnailLayout(selectedTheme)=="xjxqy-expanded")
        for(const auto& definition:currentDefinition.components) if(definition.editorId==owner&&definition.name=="thumbnailContainer") return false;
    if (previewModeCombo && previewModeCombo->currentIndex() == 0) return true;
    const auto window=const_cast<MenuEditorWindow*>(this)->propertiesForResource(currentDefinition.windowFile);
    const QSize viewport=previewWindowSize();
    return previewPanelRectangle(window,viewport).size()==QSize(window.width,window.height);
}

QStringList MenuEditorWindow::mapThumbnailPreviewStates() const
{
    return {tr("布局输入（小地图）"),tr("地图与标记（临时示例）"),tr("无底图的标记（临时示例）"),tr("光标边界（临时示例）")};
}

void MenuEditorWindow::updateMapThumbnailPreview(QList<PreviewComponent>& components)
{
    if(selectedPage!="mapthumbnail") return;
    const QSize viewport=previewWindowSize();
    const auto window=propertiesForResource(currentDefinition.windowFile);
    const QRect panel = previewModeCombo && previewModeCombo->currentIndex() == 0
        ? QRect(0, 0, qMax(1, window.width), qMax(1, window.height)) : previewPanelRectangle(window,viewport);
    previewInterfaceOrigin = panel.topLeft();
    float scaleX=1,scaleY=1;
    if(window.width>0&&window.height>0)
    {
        scaleX=float(panel.width())/window.width;scaleY=float(panel.height())/window.height;
        if(window.keepAspect) scaleX=scaleY=qMin(scaleX,scaleY);
    }
    QPoint offset;
    if(window.keepAspect&&window.width>0&&window.height>0)
    {
        const double uniform=qMin(double(panel.width())/window.width,double(panel.height())/window.height);
        offset={(panel.width()-int(std::round(window.width*uniform)))/2,(panel.height()-int(std::round(window.height*uniform)))/2};
    }
    previewCanvas->setMenuRect({QPoint(),panel.size()});
    const auto alignment=window.align.toUtf8();char* alignmentEnd=nullptr;
    const long numericAlignment=std::strtol(alignment.constData(),&alignmentEnd,0);
    const bool client=window.align.compare("alClient",Qt::CaseInsensitive)==0
        ||(alignmentEnd!=alignment.constData()&&numericAlignment==alClient);
    previewCanvas->setWindowStretch(window.stretch||client,window.keepAspect);
    const auto layout=pageResources.mapThumbnailLayout(selectedTheme);
    const int state=toolTipPreviewCombo->currentIndex();
    QRect input,thumbnail,close;
    for(auto& component:components)
    {
        ComponentIniProperties properties;
        for(const auto& definition:currentDefinition.components) if(definition.editorId==component.editorId) {properties=propertiesForResource(definition.file);break;}
        component.rect={int(properties.left*scaleX)+offset.x(),int(properties.top*scaleY)+offset.y(),int(properties.width*scaleX),int(properties.height*scaleY)};
        if(!mapThumbnailCanvasIsEditable(component.editorId)) component.movable=component.resizable=false;
        if(component.name=="mapNameLabel") {component.visible=false;component.movable=component.resizable=false;}
        if(component.name=="thumbnailContainer")
        {
            input={properties.left,properties.top,properties.width,properties.height};
            QRect effective=component.rect.translated(panel.topLeft());
            if(layout=="yycs-derived") effective={panel.x()+int(std::round(160*scaleX)),panel.y()+int(std::round(120*scaleY)),qMax(1,int(std::round(320*scaleX))),qMax(1,int(std::round(240*scaleY)))};
            if(layout=="yycs-derived"||layout=="xjxqy-expanded")
            {
                const int margin=layout=="yycs-derived"?8:12;
                const int x=qMax(1,int(std::round(margin*scaleX))),y=qMax(1,int(std::round(margin*scaleY)));
                const int left=effective.x()-x,top=effective.y()-y,right=effective.x()+effective.width()+x,bottom=effective.y()+effective.height()+y;
                effective={left,top,right-left,bottom-top};
                if(panel.width()>0&&panel.height()>0)
                {
                    const int clippedLeft=qMax(left,panel.x()),clippedTop=qMax(top,panel.y());
                    effective={clippedLeft,clippedTop,qMax(1,qMin(right,panel.x()+panel.width())-clippedLeft),qMax(1,qMin(bottom,panel.y()+panel.height())-clippedTop)};
                }
            }
            thumbnail=effective;
            component.rect=effective.translated(-panel.topLeft());
            component.imagePath.clear();component.image=QPixmap();component.sampleImage=true;component.stretch=true;
            component.rawValues["mapthumbnail"]=QString::number(state);
        }
        if(component.name=="closeButton")
        {
            close=component.rect.translated(panel.topLeft());
            if(component.type=="Button") component.imageFrame=properties.up;
        }
    }
    auto* information=findChild<QLabel*>("conversationLayoutResult");information->setVisible(true);
    information->setProperty("mapThumbnailPanel",panel);information->setProperty("mapThumbnailInput",input);
    information->setProperty("mapThumbnailEffective",thumbnail);information->setProperty("mapThumbnailClose",close);
    information->setText(tr("窗口视口矩形：(%1,%2) %3×%4；缩略图输入：(%5,%6) %7×%8；实际：(%9,%10) %11×%12。\n画布使用窗口局部坐标。地图、标记和光标是临时样例；派生矩形只读，使用属性编辑原始输入。实际字体、图片和输入以运行时软件联测为准。")
        .arg(panel.x()).arg(panel.y()).arg(panel.width()).arg(panel.height()).arg(input.x()).arg(input.y()).arg(input.width()).arg(input.height())
        .arg(thumbnail.x()).arg(thumbnail.y()).arg(thumbnail.width()).arg(thumbnail.height()));
}
