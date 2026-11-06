#include "MenuEditorWindow.h"
#include "../../src/Game/Menu/OptionLayout.h"
#include "../../src/Types/ComponentTypes.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QFile>
#include "../core/INIFileEditor.h"
#include <limits>
#include <QUuid>
#include <cmath>

namespace
{
bool enabled(const QString& value)
{
    return QStringList{"1", "true", "yes", "on"}.contains(value.toLower());
}


}

bool MenuEditorWindow::optionAutomaticSaveAvailable() const
{
    const QString resource = effectiveQingyuTheme() ? "ini/ui/qingyu/saveload/listBox.ini" : "ini/ui/saveload/listbox.ini";
    QFile file(pageResources.readable(resource));
    if (!file.open(QIODevice::ReadOnly)) return false;
    INIFileEditor ini;
    ini.loadFromString(file.readAll().toStdString(), INIFileEditor::UnrecognizedLinePolicy::Preserve);
    return enabled(QString::fromStdString(ini.get("Init", "AutomaticSave")));
}

QMap<QString, ComponentIniProperties> MenuEditorWindow::optionDefaultProperties(bool controllerPresent)
{
    QMap<QString, ComponentIniProperties> values;
    QMap<QString, const MenuComponentDefinition*> definitions;
    for (const auto& component : currentDefinition.components)
    {
        definitions[component.name] = &component;
        ComponentIniProperties properties;
        QString local;
        resolveLocalResourcePath(component.file, local);
        const auto staged = modifiedComponentInis.constFind(local);
        if (staged != modifiedComponentInis.constEnd() && staged->stagedForWrite) properties = staged->properties;
        else if (!component.runtimeDefault)
        {
            const QString source = pageResources.readable(component.file);
            if (!source.isEmpty()) readComponentIniProperties(source, properties);
        }
        else if (const auto* defaults = OptionLayout::find(component.name.toStdString()))
        {
            properties.font = defaults->font;
            properties.fontSet = defaults->font > 0;
            properties.color = defaults->color;
            if (!defaults->dynamicText) properties.text = QString::fromUtf8(defaults->text);
            properties.stretch = component.type == "FlatTextButton";
            if (component.type == "Label")
            {
                properties.autoShrink = properties.autoShrinkSet = true;
                properties.rawValues["centertext"] = "true";
                properties.rawValues["minimumfont"] = component.name == "cheatPanelTitle" ? "14" : "12";
            }
        }
        values[component.name] = properties;
    }
    if (definitions.contains("experienceMultiplierButton") && definitions["experienceMultiplierButton"]->runtimeDefault
        && definitions.contains("cheatSettingsButton"))
    {
        const auto& source = values["cheatSettingsButton"];
        auto& target = values["experienceMultiplierButton"];
        target.font = source.font; target.fontSet = source.fontSet;
        target.color = source.color; target.image = source.image;
        target.stretch = source.stretch; target.stretchSet = source.stretchSet;
        target.up = source.up; target.down = source.down; target.track = source.track;
        for (const QString& key : {QString("backgroundcolor"), QString("hoverbackgroundcolor"), QString("pressedbackgroundcolor")})
            if (source.rawValues.contains(key)) target.rawValues[key] = source.rawValues.value(key);
    }
    const auto window = propertiesForResource(currentDefinition.windowFile);
    const QSize viewport = previewModeCombo && previewModeCombo->currentIndex() == 1 ? previewWindowSize() : QSize(1280, 720);
    const QRect panel = previewPanelRectangle(window, viewport);
    Rect reference{panel.x(), panel.y(), panel.width(), panel.height()};
    const double scaleX = window.width > 0 ? double(qMax(1, reference.w))/window.width : 1;
    const double scaleY = window.height > 0 ? double(qMax(1, reference.h))/window.height : 1;
    const auto global = [&](const ComponentIniProperties& p)
    {
        return Rect{reference.x + int(std::lround(p.left*scaleX)), reference.y + int(std::lround(p.top*scaleY)),
            int(std::lround(p.width*scaleX)), int(std::lround(p.height*scaleY))};
    };
    const auto assign = [&](ComponentIniProperties& p, const OptionLayout::Geometry& geometry)
    {
        p.left = int(std::lround((geometry.rectangle.x-reference.x)/scaleX));
        p.top = int(std::lround((geometry.rectangle.y-reference.y)/scaleY));
        p.width = int(std::lround(geometry.rectangle.w/scaleX)); p.height = int(std::lround(geometry.rectangle.h/scaleY));
        if (geometry.font > 0) { p.font = geometry.font; p.fontSet = true; }
    };
    const bool realCheat = definitions.contains("cheatSettingsButton") && !definitions["cheatSettingsButton"]->runtimeDefault;
    const bool legacy = realCheat && definitions["cheatSettingsButton"]->optionLayout != OptionLayout::Explicit;
    const Rect returnButton = global(values.value("rtnBtn"));
    const Rect legacyCheat = global(values.value("cheatSettingsButton"));
    const bool realTouch = definitions.contains("touchControlsButton") && !definitions["touchControlsButton"]->runtimeDefault;
    const auto footer = OptionLayout::footer(reference, definitions.contains("rtnBtn") ? &returnButton : nullptr, viewport.height(),
        realTouch || controllerPresent, definitions.contains("themeButton"), optionAutomaticSaveAvailable(), legacy ? &legacyCheat : nullptr);
    for (const auto& entry : footer.controls)
    {
        const QString name = QString::fromStdString(entry.first);
        if (definitions.contains(name) && definitions[name]->optionLayout.isEmpty()) assign(values[name], entry.second);
    }
    reference.h = footer.windowHeight;
    optionDefaultHeight = int(std::lround(reference.h/scaleY));
    Rect background = OptionLayout::cheatBounds(reference);
    if (definitions.contains("cheatPanelBackground") && !definitions["cheatPanelBackground"]->optionLayout.isEmpty())
        background = global(values["cheatPanelBackground"]);
    else assign(values["cheatPanelBackground"], {background});
    for (const auto& entry : OptionLayout::cheat(background))
    {
        const QString name = QString::fromStdString(entry.first);
        if (definitions.contains(name) && definitions[name]->optionLayout.isEmpty()) assign(values[name], entry.second);
    }
    return values;
}

void MenuEditorWindow::prepareOptionDefaults()
{
    if (selectedPage != "option" || !pageResources.valid) return;
    const auto window = propertiesForResource(currentDefinition.windowFile);
    if (window.width <= 16 || window.height <= 16)
    {
        documentStructureIssue = tr("选项窗口尺寸无效，无法推导运行时默认控件；原内容只读保留。");
        return;
    }
    const bool automatic = optionAutomaticSaveAvailable();
    for (const auto& control : OptionLayout::controls())
    {
        const QString name = QString::fromUtf8(control.name);
        bool found = false;
        for (const auto& component : currentDefinition.components) found |= component.name == name;
        if (found || (name == "automaticSaveButton" && !automatic)
            || (name == "themeButton" && !pageResources.qingyu("qingyu"))) continue;
        MenuComponentDefinition component;
        component.editorId = "component/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        component.name = name; component.type = QString::fromUtf8(control.type);
        component.file = QFileInfo(pageEntryResource).path() + "/runtime-default/" + name + ".ini";
        component.runtimeDefault = true;
        currentDefinition.components.append(component);
    }
    const auto values = optionDefaultProperties(true);
    for (const auto& component : currentDefinition.components)
    {
        if (!OptionLayout::find(component.name.toStdString())) continue;
        QString local;
        if (!resolveLocalResourcePath(component.file, local)) continue;
        auto existing = modifiedComponentInis.find(local);
        if (existing != modifiedComponentInis.end() && existing->stagedForWrite) continue;
        auto& value = modifiedComponentInis[local];
        value.properties = values.value(component.name);
        value.resourcePath = component.file;
        value.sourcePath = pageResources.readable(component.file);
        value.owners.insert(component.editorId);
        value.ownerComponentTypes[component.editorId] = component.type;
        value.stagedForWrite = false;
    }
}

void MenuEditorWindow::adjustOptionPreview(const MenuComponentDefinition& definition, PreviewComponent& component)
{
    if (selectedPage != "option") return;
    const auto* control = OptionLayout::find(definition.name.toStdString());
    const int view = optionViewCombo->currentIndex();
    if (!control) { component.visible = view == 0; return; }
    component.visible = control->cheatPanel == (view != 0);
    if (definition.name == "automaticSaveButton") component.visible &= optionAutomaticSaveAvailable();
    if (definition.name == "themeButton") component.visible &= pageResources.qingyu("qingyu");
    if (definition.name == "touchControlsButton") component.visible &= optionControllerPreview->isChecked();
    if (control->dynamicText) component.text = QString::fromUtf8(control->text);
    if (definition.name == "themeButton" && effectiveQingyuTheme()) component.text = tr("界面：青玉 · 重进游戏生效");
    if (view == 2 && definition.name == "cheatModeButton") component.text = tr("作弊模式：开");
    if (view == 2 && definition.name == "cheatResultLabel") component.text = tr("作弊模式当前已开启");
    if (definition.name == "cheatPanelBackground" && (component.imagePath.isEmpty()
        || previewCanvas->loadImageFromGamePath(component.imagePath).isNull())) component.rawValues["optiondefaultbackground"] = "true";
    if (view == 1 && QStringList{"invincibilityButton","restoreResourcesButton","increaseMagicLevelButton","increasePlayerLevelButton","addMoneyButton"}.contains(definition.name))
    {
        component.color = 0xFFAAAAAA;
        component.rawValues["optionunavailable"] = "true";
        component.rawValues["backgroundcolor"] = "0xE1232323";
        component.rawValues["hoverbackgroundcolor"] = "0xE6322D28";
        component.rawValues["pressedbackgroundcolor"] = "0xE640342A";
    }
}
