#include "MenuEditorWindow.h"
#include "../core/INIFileEditor.h"
#include "../../src/Game/Scene/TitleTeamLayout.h"
#include "../../src/Types/ComponentTypes.h"
#include "../../src/Component/TextLayout.h"
#include <cstdlib>
#include <QComboBox>
#include <QFileInfo>
#include <QLabel>
#include <QFontMetrics>
#include <QToolBar>
#include <QUuid>

namespace
{
const QMap<QString, QString> roles = {{"body", "Label"}, {"scrollHint", "Label"}, {"closeBtn", "FlatTextButton"}};
int displayValue(const QMap<QString, QString>& values, const QString& key, int fallback, int minimum = -100000, int maximum = 100000)
{
    std::int64_t value = 0;
    return INIFileEditor::tryParseInt64(values.value(key).toStdString(), value) && value >= minimum && value <= maximum
        ? static_cast<int>(value) : fallback;
}
}

QSet<QString> MenuEditorWindow::titleTeamFields(const QString& owner) const
{
    if (owner == "menu/window") return {"width", "height", "align", "alignx", "aligny", "backgroundcolor"};
    for (const auto& component : currentDefinition.components)
    {
        if (component.editorId != owner || roles.value(component.name) != component.type) continue;
        QSet<QString> fields = {"left", "top", "font", "color"};
        if (component.name != "scrollHint") fields.unite({"width", "height"});
        if (component.name == "body") fields.insert("linegap");
        if (component.name == "closeBtn") fields.insert("backgroundcolor");
        return fields;
    }
    return {};
}

void MenuEditorWindow::prepareTitleTeamDefaults()
{
    if (selectedPage != "titleteam" || !pageResources.valid || pageResources.teamInfoText.isEmpty()) return;
    if (currentDefinition.windowFile.isEmpty()) currentDefinition.windowFile = "ini/ui/titleteam/runtime-default/window.ini";
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
    {
        bool found = false;
        for (const auto& component : currentDefinition.components) found |= component.name == it.key();
        if (found) continue;
        MenuComponentDefinition component;
        component.editorId = "component/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        component.name = it.key(); component.type = it.value();
        component.file = "ini/ui/titleteam/runtime-default/" + it.key() + ".ini";
        component.runtimeDefault = true;
        currentDefinition.components.append(component);
    }
    const auto defaults = TitleTeamLayout::defaults(1280, 720, !pageResources.teamVideoPath.isEmpty());
    auto prepare = [this](const QString& owner, const QString& file, const QString& type, const Rect& rectangle, int font, unsigned int color)
    {
        QString target;
        if (!resolveLocalResourcePath(file, target)) return;
        auto properties = propertiesForResource(file);
        auto& value = modifiedComponentInis[target];
        if (value.stagedForWrite) return;
        const auto integer = [&properties](const QString& key, int& field, int fallback)
        { field = displayValue(properties.rawValues, key, fallback, key == "font" || key == "width" || key == "height" ? 1 : -100000, key == "font" ? 256 : 100000); };
        integer("left", properties.left, rectangle.x); integer("top", properties.top, rectangle.y);
        integer("alignx", properties.alignX, 0); integer("aligny", properties.alignY, 0);
        integer("width", properties.width, rectangle.w); integer("height", properties.height, rectangle.h);
        integer("font", properties.font, font);
        properties.fontSet = true;
        if (!properties.rawValues.contains("color")) properties.color = color;
        value.properties = properties;
        value.resourcePath = file;
        value.sourcePath = pageResources.readable(file);
        value.owners.insert(owner); value.ownerComponentTypes[owner] = type;
    };
    prepare("menu/window", currentDefinition.windowFile, "MenuWindow", defaults.panel, 26, defaults.color);
    for (const auto& component : currentDefinition.components)
        if (roles.value(component.name) == component.type)
            prepare(component.editorId, component.file, component.type,
                component.name == "body" ? defaults.body : component.name == "closeBtn" ? defaults.close : Rect{defaults.hint.x, defaults.hint.y, 220, 24},
                component.name == "body" ? defaults.font : component.name == "closeBtn" ? defaults.closeFont : defaults.hintFont, defaults.color);
}

bool MenuEditorWindow::validateTitleTeam(QString& error)
{
    if (!currentDefinition.subMenus.isEmpty()) { error = tr("制作名单不使用子菜单；保留原文件，仅供读取。"); return false; }
    QSet<QString> names;
    for (const auto& component : currentDefinition.components)
    {
        if (names.contains(component.name) || (roles.contains(component.name) && (roles.value(component.name) != component.type || component.file.isEmpty())))
        { error = tr("制作名单固定角色重复或类型不符：%1").arg(component.name); return false; }
        names.insert(component.name);
    }
    return true;
}

bool MenuEditorWindow::stageTitleTeamFields(const QString& owner, const ComponentIniProperties& original, ComponentIniProperties& properties)
{
    const auto fields = titleTeamFields(owner);
    const auto integer = [&](const char* key, int before, int after)
    { if (before != after && fields.contains(key)) properties.rawValues[key] = QString::number(after); };
    integer("left", original.left, properties.left); integer("top", original.top, properties.top);
    integer("width", original.width, properties.width); integer("height", original.height, properties.height);
    integer("font", original.font, properties.font);
    integer("alignx", original.alignX, properties.alignX); integer("aligny", original.alignY, properties.alignY);
    if (fields.contains("color") && original.color != properties.color)
        properties.rawValues["color"] = QString::number((properties.color >> 16) & 255) + "," + QString::number((properties.color >> 8) & 255)
            + "," + QString::number(properties.color & 255) + "," + QString::number(properties.color >> 24);
    if (fields.contains("align") && original.align != properties.align)
    {
        if (properties.align.isEmpty()) properties.rawValues.remove("align");
        else properties.rawValues["align"] = properties.align;
    }
    // Validate only changed values, retaining historical unknown/invalid source fields.
    for (const auto& key : fields)
    {
        if (properties.rawValues.value(key) == original.rawValues.value(key)) continue;
        const QString text = properties.rawValues.value(key);
        if (text.isEmpty() || key == "align") continue;
        bool valid = true;
        if (key.endsWith("color"))
        {
            valid = displayColor(text, 0x12345678) == displayColor(text, 0x87654321);
        }
        else
        {
            std::int64_t value = 0;
            valid = INIFileEditor::tryParseInt64(text.toStdString(), value);
            const int minimum = key == "font" || key == "width" || key == "height" ? 1 : key == "linegap" ? 0 : -100000;
            const int maximum = key == "font" ? 256 : key == "linegap" ? 1024 : 100000;
            valid &= value >= minimum && value <= maximum;
        }
        if (!valid)
        {
            pageContextLabel->setText(tr("制作名单显示值无效，修改未应用：%1").arg(key));
            return false;
        }
    }
    return true;
}



void MenuEditorWindow::updateTitleTeamPreview(QList<PreviewComponent>& components)
{
    const bool active = selectedPage == "titleteam";
    previewCanvas->setBackgroundColor(QColor());
    if (!active) return;
    components.clear();
    const QSize viewport = previewWindowSize();
    const bool video = !pageResources.teamVideoPath.isEmpty() || toolTipPreviewCombo->currentIndex() == 3;
    auto layout = TitleTeamLayout::defaults(viewport.width(), viewport.height(), video);
    const auto window = propertiesForResource(currentDefinition.windowFile);
    layout.panel.w = displayValue(window.rawValues, "width", layout.panel.w, 1);
    layout.panel.h = displayValue(window.rawValues, "height", layout.panel.h, 1);
    const QString alignment = window.rawValues.value("align");
    const QByteArray alignmentBytes = alignment.toUtf8();
    char* end = nullptr;
    const long numericAlignment = std::strtol(alignmentBytes.constData(), &end, 0);
    // Panel::setAlign expands alClient before calculating its local child layout.
    if (alignment.compare("alClient", Qt::CaseInsensitive) == 0 ||
        (end != alignmentBytes.constData() && numericAlignment == alClient))
    {
        layout.panel.w = viewport.width();
        layout.panel.h = viewport.height();
    }
    layout = TitleTeamLayout::defaults(viewport.width(), viewport.height(), video, &layout.panel);
    previewInterfaceOrigin = {layout.panel.x, layout.panel.y};
    previewCanvas->setMenuRect({0,0,layout.panel.w,layout.panel.h});
    const auto color = [](const QString& text, unsigned int fallback)
    { return displayColor(text, fallback); };
    previewCanvas->setBackgroundColor(QColor::fromRgba(color(window.rawValues.value("backgroundcolor"), layout.panelColor)));
    propertyEditor->setDisplayDefaults({{"backgroundcolor", "0x" + QString::number(layout.panelColor, 16)}});
    if (pageResources.teamInfoText.isEmpty()) return;
    QString text = pageResources.teamInfoText;
    const int state = toolTipPreviewCombo->currentIndex();
    if (state == 2) text = QString(80, QChar('\n')).replace("\n", tr("制作名单长文本示例（不保存）\n"));
    const auto apply = [](const ComponentIniProperties& p, Rect rectangle)
    {
        rectangle.x = displayValue(p.rawValues,"left",rectangle.x);
        rectangle.y = displayValue(p.rawValues,"top",rectangle.y);
        rectangle.w = displayValue(p.rawValues,"width",rectangle.w,1);
        rectangle.h = displayValue(p.rawValues,"height",rectangle.h,1);
        return rectangle;
    };
    ComponentIniProperties body, close, hint;
    for (const auto& definition : currentDefinition.components)
    {
        if (definition.name == "body") body = propertiesForResource(definition.file);
        if (definition.name == "closeBtn") close = propertiesForResource(definition.file);
        if (definition.name == "scrollHint") hint = propertiesForResource(definition.file);
    }
    layout.body = apply(body,layout.body); layout.close = apply(close,layout.close);
    const int font = displayValue(body.rawValues,"font",layout.font,1,256);
    const int gap = displayValue(body.rawValues,"linegap",qMax(4,font/4),0,1024);
    const auto lines = TextLayout::wrapUtf8Text(text.toStdString(), TextLayout::charactersPerLineForWidth(layout.body.w,font));
    const int visible = TextLayout::visibleWrappedLineCount(static_cast<int>(lines.size()),layout.body.h,font,gap);
    QStringList shown;
    const int first = state == 1 ? qMax(0,static_cast<int>(lines.size())-visible) : 0;
    for (int i=first;i<qMin(static_cast<int>(lines.size()),first+visible);++i) shown.append(QString::fromStdString(lines[i]));
    for (const auto& definition : currentDefinition.components)
    {
        if (roles.value(definition.name) != definition.type) continue;
        auto p = propertiesForResource(definition.file);
        PreviewComponent component;
        component.editorId = definition.editorId; component.name = definition.name; component.type = definition.type;
        component.movable = canEditGeometry(definition.editorId,false); component.resizable = canEditGeometry(definition.editorId,true);
        Rect rectangle = definition.name == "body" ? layout.body : definition.name == "closeBtn" ? layout.close
            : apply(p,Rect{layout.body.x,layout.close.y+7,220,24});
        component.rect = {rectangle.x,rectangle.y,rectangle.w,rectangle.h};
        component.color = p.rawValues.contains("color") ? p.color : layout.color;
        component.fontSize = displayValue(p.rawValues,"font",definition.name == "body" ? font : definition.name == "closeBtn" ? layout.closeFont : layout.hintFont,1,256);
        if (definition.name == "body")
        {
            component.type = "TitleTeamText"; component.text = shown.join('\n'); component.rawValues["linegap"] = QString::number(gap);
        }
        else if (definition.name == "scrollHint")
        {
            component.text = tr("↑/↓ 或滚轮滚动"); component.visible = static_cast<int>(lines.size()) > visible;
            QFont font=previewCanvas->font();font.setPixelSize(qMax(1,component.fontSize));const QFontMetrics metrics(font);
            component.rect.setSize(QSize(metrics.horizontalAdvance(component.text)+2,metrics.height()));
        }
        else
        { component.text = tr("返回"); component.rawValues["titleteamclose"] = "true"; component.rawValues["backgroundcolor"] = QString::number(color(p.rawValues.value("backgroundcolor"), layout.closeBackground)); }
        components.append(component);
    }
}
