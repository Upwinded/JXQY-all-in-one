#include "MenuEditorWindow.h"
#include "../core/INIFileEditor.h"
#include "../../src/Game/Menu/ChooseMenuLayout.h"
#include "../../src/Types/ComponentTypes.h"
#include <QComboBox>
#include <QDir>
#include <QFontMetrics>
#include <QLabel>
#include <QLayout>
#include <QPlainTextEdit>
#include <QToolBar>
#include <QTreeWidget>
#include <cmath>
#include <limits>
#include <cstdlib>

namespace
{
int integer(const QMap<QString,QString>& values,const QString& key,int fallback)
{
    const auto text=values.value(key).toUtf8();char* end=nullptr;const long value=std::strtol(text.constData(),&end,0);
    return end!=text.constData()&&value>=-100000&&value<=100000?static_cast<int>(value):fallback;
}
QRect portraitContentRectangle(const QPixmap& portrait, bool cropBlack)
{
    const QImage image = portrait.toImage().convertToFormat(QImage::Format_ARGB32);
    QRect content;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            const auto color = image.pixel(x, y);
            if (qAlpha(color) && (!cropBlack || qRed(color) || qGreen(color) || qBlue(color)))
                content |= QRect(x, y, 1, 1);
        }
    }
    return content;
}
int alignment(const QString& text)
{
    const QStringList names={"alnone","alclient","alleft","alright","altop","albottom","alltcorner","alrtcorner","allbcorner","alrbcorner","alcenter","alleftcenter","alrightcenter","altopcenter","albottomcenter"};
    const auto bytes=text.toUtf8();char* end=nullptr;const long value=std::strtol(bytes.constData(),&end,0);
    return end!=bytes.constData()?static_cast<int>(value):names.indexOf(text.toLower());
}
QRect panelRectangle(const ComponentIniProperties& window,const QSize& viewport)
{
    bool valid=false;double scale=window.rawValues.value("scale").toDouble(&valid);
    if(!valid||!std::isfinite(scale)||scale<=0||window.width*scale>std::numeric_limits<int>::max()||window.height*scale>std::numeric_limits<int>::max()) scale=1;
    QSize size(qMax(1,qRound(window.width*scale)),qMax(1,qRound(window.height*scale)));
    if(QStringList{"true","1","yes","on"}.contains(window.rawValues.value("fittowindow").toLower()))
    {
        const auto marginValue=[&](const QString& key,int fallback)
        {
            try { return qMax(0,static_cast<int>(std::stol(window.rawValues.value(key).toStdString(),nullptr,0))); }
            catch (const std::exception&) { return fallback; }
        };
        const int margin=marginValue("windowmargin",24);
        const int vertical=marginValue("windowverticalmargin",128);
        if(viewport.width()>margin&&viewport.height()>vertical)
            size*=qMin(1.0,qMin(double(viewport.width()-margin)/size.width(),double(viewport.height()-vertical)/size.height()));
    }
    QRect result(QPoint(window.left,window.top),size);
    const int align=alignment(window.align);
    if(align==alClient) result={QPoint(),viewport};
    if(align==alLeft||align==alLTCorner||align==alLBCorner||align==alLeftCenter) result.moveLeft(30);
    if(align==alRight||align==alRTCorner||align==alRBCorner||align==alRightCenter) result.moveLeft(viewport.width()-size.width()-30);
    if(align==alCenter||align==alTopCenter||align==alBottomCenter) result.moveLeft(viewport.width()/2-size.width()/2);
    if(align==alTop||align==alLTCorner||align==alRTCorner||align==alTopCenter) result.moveTop(0);
    if(align==alBottom||align==alLBCorner||align==alRBCorner||align==alBottomCenter) result.moveTop(viewport.height()-size.height());
    if(align==alCenter||align==alLeftCenter||align==alRightCenter) result.moveTop(viewport.height()/2-size.height()/2);
    return result.translated(window.alignX,window.alignY);
}
}

QString MenuEditorWindow::conversationPreviewPage(const QString& text, int charactersPerPage, bool lastPage)
{
    Q_ASSERT(charactersPerPage > 0);
    const auto start = lastPage && !text.isEmpty()
        ? (text.size() - 1) / charactersPerPage * charactersPerPage : 0;
    return text.mid(start, charactersPerPage);
}

QRect MenuEditorWindow::previewPanelRectangle(const ComponentIniProperties& window, const QSize& viewport)
{
    return panelRectangle(window, viewport);
}

QStringList MenuEditorWindow::conversationPreviewStates() const
{
    if(selectedPage=="dialog") return {tr("首选布局输入"),tr("无头像（示例）"),tr("左头像（示例）"),tr("右头像（示例）"),tr("长中文：首屏（示例）"),tr("长中文：末屏（示例）")};
    if(selectedPage=="choose") return {tr("首选布局输入"),tr("普通两项（示例）"),tr("隐藏项与原始索引（示例）"),tr("长中文选项：首屏（示例）"),tr("长中文选项：末屏（示例）"),tr("左侧姓名头像（示例）"),tr("右侧姓名头像（示例）"),tr("多选操作（兼容示例）")};
    return {};
}

void MenuEditorWindow::setupConversationTools()
{
    auto* result=new QLabel(this);result->setObjectName("conversationLayoutResult");result->setWordWrap(true);
    result->setMaximumHeight(64);
    result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    findChild<QTreeWidget*>("componentTree")->parentWidget()->layout()->addWidget(result);
}

bool MenuEditorWindow::conversationCanvasIsEditable() const
{
    return (!previewModeCombo || previewModeCombo->currentIndex() == 0) && toolTipPreviewCombo->currentIndex() == 0;
}

QSet<QString> MenuEditorWindow::conversationFields(const QString& owner) const
{
    if(owner=="menu/window")
    {
        if(currentDefinition.windowFile.isEmpty()) return {};
        QSet<QString> fields={"width","height","align","alignx","aligny","image","bitmap","stretch","scale","keepaspect","nineslice","nineslicewidth","fittowindow","windowmargin","windowverticalmargin"};
        if(selectedPage=="choose") fields.remove("stretch");
        const auto window=const_cast<MenuEditorWindow*>(this)->propertiesForResource(currentDefinition.windowFile);
        const int align=alignment(window.align);
        if(align<=0||align==alTop||align==alBottom) fields.insert("left");
        if(align<=0||align==alLeft||align==alRight) fields.insert("top");
        return fields;
    }
    const MenuComponentDefinition* definition=nullptr;
    bool adaptive=false,hasHint=false;
    for(const auto& component:currentDefinition.components)
    {
        if(component.editorId==owner) definition=&component;
        if(component.name=="head1")
        {
            const auto head=const_cast<MenuEditorWindow*>(this)->propertiesForResource(component.file);
            adaptive=head.cropContent&&!head.stretch;
        }
        hasHint|=component.name=="hint"&&component.type=="Label";
    }
    if(!definition) return {};
    const auto& name=definition->name;const auto& type=definition->type;
    if(selectedPage=="dialog")
    {
        if(name=="label"&&type=="TalkLabel")
        {
            QSet<QString> fields={"top","font","color","linecount","charactersperline","lineheight"};
            if(!adaptive) fields.unite({"left","width"});
            if(!adaptive||!hasHint) fields.insert("height");
            return fields;
        }
        if((name=="head1"||name=="head2")&&type=="ImageContainer")
        {
            QSet<QString> fields={"stretch","keepaspect","cropcontent","cropblack"};
            if(!adaptive)
            {
                fields.unite({"left","top"});
                if(const_cast<MenuEditorWindow*>(this)->propertiesForResource(definition->file).stretch) fields.unite({"width","height"});
            }
            return fields;
        }
        if(name=="hint"&&type=="Label")
        {
            QSet<QString> fields={"width","height","font","color","text","autoshrink","minimumfont","centertext"};
            if(!adaptive) fields.unite({"left","top"});
            return fields;
        }
    }
    if(selectedPage=="choose")
    {
        if(name=="messageLabel"&&type=="Label") return {"left","top","width","height","font","color"};
        if((name=="selectA"||name=="selectB")&&type=="ChooseTextButton")
        {
            QSet<QString> fields={"top","font","normalcolor","hovercolor","presscolor","image","bitmap","stretch","up","down","track"};
            if(name=="selectA") fields.unite({"left","width","height"});
            return fields;
        }
    }
    return {};
}

bool MenuEditorWindow::stageConversationFields(const QString& owner,const ComponentIniProperties& original,ComponentIniProperties& value)
{
    const auto fields=editableFields(owner);
    const auto retainClamped=[](int before,int& after,int minimum,int maximum)
    { if(after==qBound(minimum,before,maximum)) after=before; };
    retainClamped(original.font,value.font,1,256);
    retainClamped(original.left,value.left,-100000,100000);retainClamped(original.top,value.top,-100000,100000);
    retainClamped(original.width,value.width,0,100000);retainClamped(original.height,value.height,0,100000);
    const auto set=[&](const QString& key,const QString& before,const QString& after)
    { if(fields.contains(key)&&before!=after) { if(after.isEmpty()) value.rawValues.remove(key);else value.rawValues[key]=after; } };
#define INTEGER(key,field) set(key,QString::number(original.field),QString::number(value.field))
    INTEGER("left",left);INTEGER("top",top);INTEGER("width",width);INTEGER("height",height);INTEGER("font",font);
    INTEGER("alignx",alignX);INTEGER("aligny",alignY);INTEGER("up",up);INTEGER("down",down);INTEGER("track",track);
#undef INTEGER
#define BOOLEAN(key,field) set(key,original.field?"true":"false",value.field?"true":"false")
    BOOLEAN("stretch",stretch);BOOLEAN("keepaspect",keepAspect);BOOLEAN("cropcontent",cropContent);BOOLEAN("cropblack",cropBlack);BOOLEAN("autoshrink",autoShrink);
#undef BOOLEAN
    set("align",original.align,value.align);set("image",original.image,value.image);set("bitmap",original.bitmap,value.bitmap);set("text",original.text,value.text);
    const auto color=[](unsigned int c) { return "0x"+QString::number(c,16); };
    set("color",color(original.color),color(value.color));set("normalcolor",color(original.normalColor),color(value.normalColor));
    set("hovercolor",color(original.hoverColor),color(value.hoverColor));set("presscolor",color(original.pressColor),color(value.pressColor));
    if(owner=="menu/window"&&value.rawValues.value("keepaspect")!=original.rawValues.value("keepaspect"))
        value.keepAspect=QStringList{"true","1","yes","on"}.contains(value.rawValues.value("keepaspect").toLower());
    if(fields.contains("linecount"))
    {
        bool changed=false;for(const QString& key:QStringList{"width","height","font","linecount","charactersperline","lineheight"}) changed|=value.rawValues.value(key)!=original.rawValues.value(key);
        const auto number=[&](const char* key,std::int64_t fallback)
        {const auto text=value.rawValues.value(key).toUtf8();char* end=nullptr;const auto result=std::strtoll(text.constData(),&end,0);return end!=text.constData()?static_cast<std::int64_t>(result):fallback;};
        const auto font=number("font",18),height=number("lineheight",0)>0?number("lineheight",0):qMax<std::int64_t>(1,font);
        const auto columns=number("charactersperline",0)>0?number("charactersperline",0):font>0&&value.width>0?qMax<std::int64_t>(1,value.width/font):19;
        const auto rows=number("linecount",0)>0?number("linecount",0):qMax<std::int64_t>(1,value.height/height);
        if(changed&&(columns>std::numeric_limits<int>::max()||rows>std::numeric_limits<int>::max()||columns*rows>std::numeric_limits<int>::max()))
        {pageContextLabel->setText(tr("显示字段无效，修改未应用：%1").arg("LineCount × CharactersPerLine"));return false;}
    }
    for(const QString& key:fields)
    {
        if(value.rawValues.value(key)==original.rawValues.value(key)||value.rawValues.value(key).isEmpty()) continue;
        if(key=="keepaspect"||key=="fittowindow"||key=="autonextline"||key=="centertext")
        {
            if(QStringList{"true","false","1","0","yes","no","on","off"}.contains(value.rawValues.value(key).toLower())) continue;
            pageContextLabel->setText(tr("显示字段无效，修改未应用：%1").arg(key));return false;
        }
        const bool count=key=="linecount"||key=="charactersperline"||key=="lineheight";
        const bool geometry=QStringList{"left","top","width","height","font","alignx","aligny","minimumfont"}.contains(key);
        if(!count&&!geometry) continue;
        std::int64_t number=0;
        const int minimum=count?0:(key=="width"||key=="height"||key=="font"||key=="minimumfont"?1:-32767);
        const int maximum=count?4096:key=="font"||key=="minimumfont"?256:32767;
        if(!INIFileEditor::tryParseInt64(value.rawValues.value(key).toStdString(),number)||number<minimum||number>maximum)
        {
            pageContextLabel->setText(tr("显示字段无效，修改未应用：%1").arg(key));return false;
        }
    }
    return true;
}

void MenuEditorWindow::updateConversationPreview(QList<PreviewComponent>& components)
{
    const bool passive=selectedPage=="message"||selectedPage=="timer";
    const bool active=selectedPage=="dialog"||selectedPage=="choose"||passive;
    auto* explanation=findChild<QLabel*>("conversationLayoutResult");explanation->setVisible(active);
    if(!active) return;
    const auto viewport=previewWindowSize();
    const auto window=propertiesForResource(currentDefinition.windowFile);
    QRect panel = previewModeCombo && previewModeCombo->currentIndex() == 0
        ? QRect(0, 0, qMax(1, window.width), qMax(1, window.height)) : panelRectangle(window,viewport);
    previewInterfaceOrigin = panel.topLeft();
    previewCanvas->setMenuRect(QRect(QPoint(),panel.size()));
    previewCanvas->setWindowStretch(selectedPage=="choose"||window.stretch||alignment(window.align)==alClient,window.keepAspect);
    const int state=toolTipPreviewCombo->currentIndex();
    explanation->setText(state==0?tr("首选输入，不代表所有运行状态的最终矩形。剧情内容、索引及状态不保存。")
        :tr("运行状态示例：位置由现有布局规则计算；Qt字形度量与逐字效果以运行时软件验证为准。"));
    explanation->setToolTip(selectedPage=="dialog"
        ?tr("内容裁剪模式由head1的CropContent且非Stretch启用；正文左右边界、头像矩形和hint位置由实际头像内容派生。")
        :tr("selectA提供首选矩形与动态按钮样式；selectB的Top参与行距，其余几何不是独立运行结果。姓名和头像由脚本提供。"));
    const bool hasBase=window.width>0&&window.height>0;
    float sx=hasBase?float(panel.width())/window.width:1.0f,sy=hasBase?float(panel.height())/window.height:1.0f;
    const double uniform=qMin(double(panel.width())/qMax(1,window.width),double(panel.height())/qMax(1,window.height));
    if(window.keepAspect&&hasBase) sx=sy=static_cast<float>(uniform);
    const QPoint offset(window.keepAspect&&hasBase?(panel.width()-qRound(window.width*uniform))/2:0,window.keepAspect&&hasBase?(panel.height()-qRound(window.height*uniform))/2:0);
    QMap<QString,ComponentIniProperties> properties;
    for(auto& component:components)
    {
        for(const auto& definition:currentDefinition.components) if(definition.editorId==component.editorId) properties[component.name]=propertiesForResource(definition.file);
        component.rect={static_cast<int>(component.rect.x()*sx)+offset.x(),static_cast<int>(component.rect.y()*sy)+offset.y(),static_cast<int>(component.rect.width()*sx),static_cast<int>(component.rect.height()*sy)};
        if(state>0||!qFuzzyCompare(sx,1.0f)||!qFuzzyCompare(sy,1.0f)||!offset.isNull()) component.movable=component.resizable=false;
    }
    if(passive) {updatePassivePreview(components,panel,viewport);return;}
    if(state==0) return;
    if(selectedPage=="choose")
    {
        auto find=[&](const QString& name) { for(const auto& c:components) if(c.name==name) return c;return PreviewComponent(); };
        const auto a=find("selectA"),b=find("selectB"),message=find("messageLabel");
        ChooseMenuLayoutInput input;input.viewportWidth=viewport.width();input.viewportHeight=viewport.height();
        const auto rect=[](const QRect& r) { return ChooseMenuLayoutRectangle{r.x(),r.y(),r.width(),r.height()}; };
        input.preferredPanel=rect(panel);input.preferredMessage=rect(message.rect);input.preferredOption=rect(a.rect);
        input.messageFontSize=qMax(1,message.fontSize);input.optionFontSize=qMax(1,a.fontSize);input.speakerFontSize=input.messageFontSize;
        input.rowGap=qMax(0,b.rect.y()-a.rect.y()-a.rect.height());input.columnGap=10;
        input.measureText=[this](const std::string& text,int pixels)
        { QFont font=previewCanvas->font();font.setPixelSize(pixels);QFontMetrics metrics(font);return std::pair<int,int>{metrics.horizontalAdvance(QString::fromStdString(text)),metrics.height()}; };
        input.message=tr("请选择下一步（临时示例）").toStdString();
        input.anchorPanelToPreferredTop=input.compactDialogAlignedTwoChoice=state==1&&pageResources.manifest.uiProfile=="YYCS";
        const bool longItems=state==3||state==4;
        const auto customItems = choicePreviewText ? choicePreviewText->toPlainText().split('\n', Qt::SkipEmptyParts) : QStringList();
        const int count = customItems.isEmpty() ? (longItems ? 31 : state == 1 ? 2 : 5) : qMin(200, static_cast<int>(customItems.size()));
        for (int i = 0; i < count; ++i)
            if (state != 2 || i != 1) input.visibleItems.push_back({i, (customItems.isEmpty()
                ? tr("选项 %1：").arg(i+1) + (longItems ? QString(70,QChar(0x6c5f)) : tr("继续旅程")) : customItems[i]).toStdString()});
        if(state==4) input.requestedPageIndex=100000;
        input.showSpeaker=input.showPortrait=state==5||state==6;input.speakerRight=state==6;input.speakerName=tr("示例人物").toStdString();
        input.multipleFooter=state==7;input.columnCount=state==7?2:1;
        const auto layout=calculateChooseMenuLayout(input);previewInterfaceOrigin = {layout.panel.x, layout.panel.y};previewCanvas->setMenuRect({0,0,layout.panel.width,layout.panel.height});
        const auto local=[&](const ChooseMenuLayoutRectangle& r) { return QRect(r.x-layout.panel.x,r.y-layout.panel.y,r.width,r.height); };
        QList<PreviewComponent> result;
        auto append=[&](PreviewComponent component,const QString& name,const ChooseMenuLayoutRectangle& r,const QString& text)
        {
            component.name=name;component.rect=local(r);component.text=text;component.visible=!r.isEmpty();component.movable=component.resizable=false;
            if(component.type=="ChooseTextButton") {component.choiceTextPreview=true;component.color=displayColor(component.rawValues.value("normalcolor"),component.color);}
            result.append(component);
        };
        append(message,"messageLabel",layout.message,QString::fromStdString(input.message));
        for(size_t i=0;i<layout.pageItems.size();++i)
        {
            auto button=i==1?b:a;button.color=properties.value(i==1?"selectB":"selectA").normalColor;
            append(button,"choice-"+QString::number(layout.pageItems[i].originalIndex),layout.pageItems[i].rect,QString::fromStdString(layout.pageItems[i].text));
        }
        append(a,"previousPage",layout.previousPage,tr("上一页"));append(a,"nextPage",layout.nextPage,tr("下一页"));
        append(a,"clear",layout.clear,tr("清空"));append(a,"confirm",layout.confirm,tr("确定"));
        append(message,"speaker",layout.speaker,QString::fromStdString(input.speakerName));
        PreviewComponent portrait;portrait.type="ImageContainer";append(portrait,"portrait",layout.portrait,tr("脚本头像（示例区域）"));
        components=result;
        explanation->setText(explanation->text()+tr("\n计算结果：第%1/%2页，%3个可见原始索引。").arg(layout.pageIndex+1).arg(layout.pageCount).arg(layout.totalVisibleItems));
        return;
    }
    // Samples contain only Chinese glyphs; script markup and typing stay in TalkLabel.
    QPixmap portrait;
    INIFileEditor heads;heads.loadFromFile(pageResources.readable("ini/ui/dialog/headfile.ini").toStdString());
    for(int i=0;i<32&&portrait.isNull();++i)
    {
        const QString name=QString::fromStdString(heads.get("PORTRAIT",std::to_string(i)));
        if(!name.isEmpty()) portrait=previewCanvas->loadImageFromGamePath("asf/portrait/"+name);
    }
    if(portrait.isNull())
        for(const auto& root:pageResources.contentRoots)
            for(const QString& directory:QStringList{"asf/portrait","mpc/portrait"})
                for(const auto& name:QDir(root+"/"+directory).entryList({"*.asf","*.mpc","*.png"},QDir::Files,QDir::Name))
                    if(portrait.isNull()) portrait=previewCanvas->loadImageFromGamePath(directory+"/"+name);
    PreviewComponent *label=nullptr,*head=nullptr,*hint=nullptr;
    for(auto& component:components)
    {
        if(component.name=="label") label=&component;
        if(component.name=="hint") hint=&component;
        if(component.name=="head1"||component.name=="head2")
        {
            component.visible=state==2?component.name=="head1":state==3&&component.name=="head2";
            component.image=portrait;component.imagePath.clear();component.sampleImage=true;
            const auto own = properties.value(component.name);
            const bool nineSlice = component.stretch && integer(own.rawValues,"nineslice",0)>0
                && component.rect.width()>0 && component.rect.height()>0;
            if (own.cropContent && !nineSlice)
            {
                const QRect content = portraitContentRectangle(portrait, own.cropBlack);
                if (!content.isEmpty()) component.image = portrait.copy(content);
            }
            if(component.visible) head=&component;
        }
    }
    if(!label) return;
    const auto head1=properties.value("head1");
    if(head1.cropContent&&!head1.stretch)
    {
        const int padding=qMax(1,label->rect.y());label->rect.setX(padding);label->rect.setWidth(panel.width()-2*padding);
        if(head&&!portrait.isNull())
        {
            const QRect content=portraitContentRectangle(portrait,properties.value(head->name).cropBlack);
            if(!content.isEmpty())
            {
                head->rect={state==3?panel.width()-padding-content.width():padding,panel.height()-padding-content.height(),content.width(),content.height()};
                if(state!=3) label->rect.translate(content.width()+padding,0);
                label->rect.setWidth(label->rect.width()-content.width()-padding);
            }
        }
        if(hint)
        { hint->rect.moveTo(label->rect.x()+(label->rect.width()-hint->rect.width())/2,panel.height()-padding-hint->rect.height());label->rect.setHeight(qMax(label->fontSize,hint->rect.y()-padding-label->rect.y())); }
    }
    const auto body=properties.value("label");
    const int font=qMax(1,label->fontSize),lineHeight=qMax(1,integer(body.rawValues,"lineheight",0)>0?integer(body.rawValues,"lineheight",0):font);
    const int columns=qBound(1,integer(body.rawValues,"charactersperline",0)>0?integer(body.rawValues,"charactersperline",0):label->rect.width()/font,4096);
    const int lines=qBound(1,integer(body.rawValues,"linecount",0)>0?integer(body.rawValues,"linecount",0):label->rect.height()/lineHeight,4096);
    const QString sample=tr("对话内容示例，文字与头像由剧情提供。")+(state>=4?QString(400,QChar(0x6c5f)):QString());
    const QString page=conversationPreviewPage(sample,columns*lines,state==5);
    label->type="TitleTeamText";label->rawValues["linegap"]=QString::number(lineHeight-font);
    label->dialogTextPreview=true;
    QStringList rows;for(int i=0;i<page.size();i+=columns) rows.append(page.mid(i,columns));label->text=rows.join('\n');
}
