#include "MenuEditorWindow.h"
#include "../core/INIFileEditor.h"
#include <QComboBox>
#include <QLabel>

QSet<QString> MenuEditorWindow::passiveFields(const QString& owner) const
{
    if (owner=="menu/window") return conversationFields(owner);
    for (const auto& component:currentDefinition.components)
    {
        if (component.editorId!=owner||component.type!="Label"
            ||component.name!=(selectedPage=="message"?"label":"timeLabel")) continue;
        QSet<QString> fields={"left","top","width","height","font","color","minimumfont","centertext"};
        if(selectedPage=="timer") fields.unite({"autoshrink","autonextline"});
        return fields;
    }
    return {};
}

QStringList MenuEditorWindow::passivePreviewStates() const
{
    if(selectedPage=="message") return {tr("首选布局输入"),tr("短中文消息（示例）"),tr("长中文消息（示例）"),tr("颜色标记消息（示例）"),tr("计时可见（本包布局）"),tr("计时隐藏（示例）")};
    if(selectedPage=="timer") return {tr("首选布局输入"),tr("计时：61秒（示例）"),tr("计时：60秒（示例）"),tr("计时：零值（示例）"),tr("计时：三位分钟（示例）"),tr("计时：隐藏（示例）"),tr("计时：停止（示例）")};
    return {};
}

void MenuEditorWindow::updatePassivePreview(QList<PreviewComponent>& components,const QRect& panel,const QSize& viewport)
{
    const int state=toolTipPreviewCombo->currentIndex();
    auto* explanation=findChild<QLabel*>("conversationLayoutResult");
    explanation->setProperty("basePanel",panel);explanation->setProperty("finalPanel",panel);
    explanation->setToolTip(tr("正文、计时秒数、显示开关及事件策略由运行时控制。状态与上下文仅用于预览，不保存；Qt字形度量以运行时软件验证为准。"));
    const bool message=selectedPage=="message";
    const bool hidden=!message&&state>=5;
    const int seconds=state==2?60:state==3?0:state==4?6001:61;
    const QString time=QString("%1分%2秒").arg(seconds/60,2,10,QChar('0')).arg(seconds%60,2,10,QChar('0'));
    QString text=tr("获得物品，已放入包裹。");
    if(message&&state==2) text=QString(8,QChar(0x6c5f))+tr("：这是一段用于核对换行与缩字的长中文消息。")+QString(60,QChar(0x6e56));
    if(message&&state==3) text=tr("普通消息与")+"<color=Red>"+tr("红色文字")+"<color=Default>";
    for(auto& component:components)
    {
        if(component.name!=(message?"label":"timeLabel")) continue;
        component.text=message?text:time;
        component.passiveTextPreview=true;
        component.visible=!hidden;
        if(message) {component.rawValues["autonextline"]="true";component.rawValues["autoshrink"]="true";}
    }
    previewCanvas->setWindowPreviewVisible(!hidden);
    if(hidden) for(auto& component:components) component.visible=false;
    if(!message)
    {
        explanation->setText(tr("计时样例：%1；显示：%2。秒数与可见状态不写入配置。").arg(time,hidden?tr("否"):tr("是")));
        return;
    }
    QRect finalPanel=panel;
    QString reason=tr("计时未显示，使用基础位置。消息始终输入透传。"),contextSource;
    if(state==4&&effectiveQingyuTheme())
    {
        const auto readPanel=[&](const QString& resource,QString& source)
        {
            source=pageResources.readable(resource);INIFileEditor menu;
            if(source.isEmpty()||!menu.loadFromFile(source.toStdString())) return QRect();
            const QString window=pageResources.readable(QString::fromStdString(menu.get("menu","window")));
            ComponentIniProperties properties;
            if(window.isEmpty()) return QRect();
            readComponentIniProperties(window,properties);
            return previewPanelRectangle(properties,viewport);
        };
        QString timerSource,bottomSource;
        const QRect timer=readPanel(pageResources.resource("timer",selectedTheme),timerSource);
        const QRect bottom=readPanel(pageResources.themedResource("ini/ui/bottom/bottom.menu.ini",pageResources.qingyu(selectedTheme)),bottomSource);
        contextSource=timerSource+"\n"+bottomSource;
        if(timer.isValid()&&bottom.isValid())
        {
            if(panel.intersects(timer))
            {
                finalPanel.moveTop(bottom.y()-panel.height()-12);
                reason=tr("与可见计时相交：消息移至底部栏上方12像素；最终位置只读。");
            }
            else reason=tr("与可见计时不相交，使用基础位置。");
            for(const auto& item:QList<QPair<QString,QRect>>{{tr("计时（只读位置对照）"),timer},{tr("底部栏（只读位置对照）"),bottom}})
            {
                PreviewComponent context;context.editorId="passive-context-"+item.first;context.name=item.first;context.type="Panel";
                context.rect=item.second.translated(-finalPanel.topLeft());context.movable=context.resizable=false;
                components.append(context);
            }
        }
        else reason=tr("计时／底部布局缺失，无法推导避让，保留基础位置。");
    }
    explanation->setText(tr("基础位置：(%1,%2)，最终位置：(%3,%4)。\n%5\n上下文来源：%6").arg(panel.x()).arg(panel.y()).arg(finalPanel.x()).arg(finalPanel.y()).arg(reason,contextSource));
    explanation->setProperty("basePanel",panel);explanation->setProperty("finalPanel",finalPanel);
}
