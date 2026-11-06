#include "MenuEditorWindow.h"
#include "../core/EditorAssetPath.h"
#include <QFile>
#include <QFileInfo>
#include <QUuid>

bool MenuEditorWindow::validateSystemPage(QString& error) const
{
    QMap<QString, QString> types;
    if (selectedPage == "title") for (const QString name : {QString("initBtn"), QString("loadBtn"), QString("teamBtn"), QString("exitBtn")}) types[name] = "Button";
    if (selectedPage == "system")
        types = {{"title","ImageContainer"},{"headingFrame","ImageContainer"},{"heading","Label"},{"returnBtn","Button"},{"saveloadBtn","Button"},{"optionBtn","Button"},{"quitBtn","Button"}};
    if (selectedPage == "yesno") types = {{"yes","Button"},{"no","Button"},{"label","Label"}};
    if (selectedPage == "option")
        types = {{"music","Scrollbar"},{"sound","Scrollbar"},{"speed","Scrollbar"},{"musicCB","CheckBox"},{"soundCB","CheckBox"},{"speedCB","CheckBox"},
            {"playerAlpha","CheckBox"},{"shadow","CheckBox"},{"dyLoad","CheckBox"},{"playerBg","ImageContainer"},{"shadowBg","ImageContainer"},{"dyLoadBg","ImageContainer"},{"rtnBtn","Button"}};
    const QStringList buttons = {"Button", "TextButton", "FlatTextButton", "ChooseTextButton", "CheckBox", "RoundButton", "DragButton", "DragRoundButton", "Joystick"};
    for (const auto& component : currentDefinition.components)
        if (types.contains(component.name) && (types.value(component.name) == "Button" ? !buttons.contains(component.type) : types.value(component.name) != component.type))
        { error = tr("固定组件类型与运行时不符：%1").arg(component.name); return false; }
    return true;
}

bool MenuEditorWindow::loadYesNoFallback(const QString& filePath)
{
    if (!pageResources.canLoad("yesno", selectedTheme)) return false;
    QFile file(filePath);
    QStringList preserved;
    if (file.open(QIODevice::ReadOnly))
    {
        // The runtime discarded this incomplete definition; retain it as inactive source text.
        preserved.append("; " + tr("原聚合定义未被运行时采用，以下原文仅作保留："));
        for (const auto& line : QString::fromUtf8(file.readAll()).split('\n'))
            preserved.append("; " + QString(line).remove('\r'));
    }
    currentDefinition = MenuDefinition();
    currentDefinition.menuName = "yesnoMenu";
    currentDefinition.windowFile = "ini/ui/yesno/window.ini";
    for (const auto& entry : QList<QStringList>{{"yes", "Button", "btnyes"}, {"no", "Button", "btnno"}, {"label", "Label", "label"}})
    {
        MenuComponentDefinition component;
        component.editorId = "component/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        component.name = entry[0]; component.type = entry[1]; component.file = "ini/ui/yesno/" + entry[2] + ".ini";
        currentDefinition.components.append(component);
    }
    originalMenuFileLines = preserved;
    documentStructureIssue.clear();
    return true;
}

QSet<QString> MenuEditorWindow::systemPageFields(const QString& owner) const
{
    if (owner == "menu/window")
    {
        if (selectedPage == "title") return {"width", "height", "image", "bitmap"};
        if (selectedPage == "option") return {"width", "image", "bitmap", "stretch", "align", "alignx", "aligny"};
    }
    const MenuComponentDefinition* definition = nullptr;
    const bool slider = owner.endsWith("/slideBtn");
    for (const auto& component : currentDefinition.components)
        if (component.editorId == (slider ? owner.chopped(9) : owner)) { definition = &component; break; }
    if (!definition) return {};
    const auto& name = definition->name;
    const QString type = slider ? "DragButton" : definition->type;
    const QStringList buttons = {"Button", "TextButton", "FlatTextButton", "CheckBox", "RoundButton", "DragButton", "DragRoundButton"};
    bool known = false, dynamic = false;
    if (selectedPage == "title") known = !slider && buttons.contains(type) && QStringList{"initBtn", "loadBtn", "teamBtn", "exitBtn"}.contains(name);
    if (selectedPage == "system")
        known = !slider && ((buttons.contains(type) && QStringList{"returnBtn", "saveloadBtn", "optionBtn", "quitBtn"}.contains(name))
            || (type == "ImageContainer" && QStringList{"title", "headingFrame"}.contains(name)) || (type == "Label" && name == "heading"));
    if (selectedPage == "yesno")
    {
        known = !slider && ((buttons.contains(type) && (name == "yes" || name == "no")) || (type == "Label" && name == "label"));
        dynamic = name == "label";
    }
    if (selectedPage == "option")
    {
        known = (QStringList{"music", "sound", "speed"}.contains(name) && definition->type == "Scrollbar")
            || (!slider && ((buttons.contains(type) && QStringList{"rtnBtn", "musicCB", "soundCB", "speedCB", "playerAlpha", "shadow", "dyLoad"}.contains(name))
                || (type == "ImageContainer" && QStringList{"playerBg", "shadowBg", "dyLoadBg", "headingFrame"}.contains(name))
                || (type == "Label" && QStringList{"heading", "musicCaption", "soundCaption", "speedCaption"}.contains(name))));
    }
    if (selectedPage == "buysell" && name == "scrollbar" && definition->type == "Scrollbar") known = true;
    if (!known) return {};
    QSet<QString> fields = {"left", "top", "width", "height"};
    // The slider's longitudinal position is recomputed from its value every frame.
    if (slider)
    {
        const auto properties = const_cast<MenuEditorWindow*>(this)->propertiesForResource(definition->file);
        fields.remove(properties.style == 1 ? "top" : "left");
        if (QStringList{"true", "1", "yes", "on"}.contains(properties.rawValues.value("fittrack").toLower()))
            fields.remove(properties.style == 1 ? "width" : "height");
    }
    if (type == "Scrollbar")
    {
        const auto properties = const_cast<MenuEditorWindow*>(this)->propertiesForResource(definition->file);
        fields.unite({"style", "flat", "fittrack"});
        if (QStringList{"true", "1", "yes", "on"}.contains(properties.rawValues.value("flat").toLower())) fields.insert("backgroundcolor");
        else fields.insert("image");
        if (!QStringList{"true", "1", "yes", "on"}.contains(properties.rawValues.value("fittrack").toLower())) fields.unite({"slidebegin", "slideend"});
    }
    else if (buttons.contains(type))
    {
        fields.unite({"image", "bitmap", "up", "down", "track"});
        if (selectedPage != "title") fields.insert("stretch");
        if (type == "TextButton" || type == "FlatTextButton" || type == "CheckBox")
            fields.unite({"text", "font", "color"});
        if (type == "FlatTextButton") fields.unite({"backgroundcolor", "hoverbackgroundcolor", "pressedbackgroundcolor"});
        const auto properties = const_cast<MenuEditorWindow*>(this)->propertiesForResource(slider
            ? QFileInfo(QString(definition->file).replace('\\', '/')).path() + "/" + const_cast<MenuEditorWindow*>(this)->propertiesForResource(definition->file).slideBtn : definition->file);
        if (type == "CheckBox") fields.subtract({"color", "track"});
        else
        {
            if (QStringList{"true", "1", "yes", "on"}.contains(properties.rawValues.value("animate").toLower())) fields.subtract({"up", "down", "track"});
            else if (properties.kind.compare("trackbtn", Qt::CaseInsensitive) != 0) fields.remove("track");
            if ((type == "Button" || type == "DragButton") && QStringList{"true", "1", "yes", "on"}.contains(properties.rawValues.value("flat").toLower()))
            {
                fields.subtract({"image", "bitmap", "up", "down", "track", "stretch"});
                fields.unite({"backgroundcolor", "hoverbackgroundcolor", "pressedbackgroundcolor", "bordercolor"});
            }
        }
    }
    else if (type == "ImageContainer") fields.unite({"image", "bitmap", "stretch", "keepaspect", "frame", "nineslice", "nineslicewidth"});
    else if (type == "Label")
    {
        fields.unite({"font", "color", "autonextline", "minimumfont", "centertext", "autoshrink"});
        if (!dynamic && definition->bind.isEmpty()) fields.insert("text");
    }
    return fields;
}
