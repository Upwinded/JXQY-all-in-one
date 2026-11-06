#include "MenuEditorWindow.h"
#include "../core/INIFileEditor.h"
#include "../../src/Game/GameManager/SaveSlots.h"
#include <QComboBox>
#include <QFile>
#include <QPainter>
#include <QRegularExpression>

namespace
{
bool enabled(const QString& value)
{
    return QStringList{"true", "1", "yes", "on"}.contains(value.toLower());
}
}

QSet<QString> MenuEditorWindow::storagePageFields(const QString& owner) const
{
    const bool slider = owner.endsWith("/slideBtn");
    const MenuComponentDefinition* definition = nullptr;
    for (const auto& component : currentDefinition.components)
        if (component.editorId == (slider ? owner.chopped(9) : owner)) definition = &component;
    if (!definition) return {};
    const QString name = definition->name;
    const QString type = definition->type;
    if (selectedPage == "buysell" && name == "scrollbar" && type == "Scrollbar") return systemPageFields(owner);
    if (slider) return {};
    QSet<QString> fields = {"left", "top", "width", "height"};
    if (selectedPage == "saveload" && name == "snap" && type == "ImageContainer") return fields;
    if (selectedPage == "saveload" && name == "listBox" && type == "ListBox")
    {
        fields.unite({"color", "selcolor", "font", "fititems", "items"});
        if (!enabled(const_cast<MenuEditorWindow*>(this)->propertiesForResource(definition->file).rawValues.value("fititems"))) fields.insert("itemheight");
        return fields;
    }
    if (selectedPage == "buysell" && QRegularExpression("^item[1-9][0-9]*$").match(name).hasMatch() && type == "Item")
    {
        fields.unite({"font", "color", "drawslot", "backimage1", "backimage2"});
        return fields;
    }
    const bool button = (selectedPage == "saveload" ? QStringList{"saveBtn", "loadBtn", "exitBtn"}.contains(name) : name == "closeBtn")
        && QStringList{"Button", "TextButton", "FlatTextButton", "RoundButton"}.contains(type);
    const bool decoration = (type == "ImageContainer" && QStringList{"title", "image", "headingFrame", "gridTray", "footerRule"}.contains(name))
        || (type == "Label" && (name == "heading" || (selectedPage == "buysell" && QStringList{"hint", "money"}.contains(name))));
    if (!button && !decoration) return {};
    if (button || type == "ImageContainer") fields.unite({"image", "bitmap", "stretch"});
    if (button)
    {
        fields.unite({"up", "down", "track"});
        const auto props = const_cast<MenuEditorWindow*>(this)->propertiesForResource(definition->file);
        if (enabled(props.rawValues.value("animate"))) fields.subtract({"up", "down", "track"});
        else if (props.rawValues.value("kind").compare("Check", Qt::CaseInsensitive) != 0) fields.remove("track");
        if (type == "Button" && enabled(props.rawValues.value("flat"))) fields.subtract({"image", "bitmap", "stretch", "up", "down", "track"});
    }
    if (type == "FlatTextButton") fields.unite({"backgroundcolor", "hoverbackgroundcolor", "pressedbackgroundcolor"});
    if (type == "Label" || type == "TextButton" || type == "FlatTextButton")
    {
        fields.unite({"font", "color"});
        if (name != "money" && definition->bind.isEmpty()) fields.insert("text");
    }
    if (type == "Label") fields.unite({"autonextline", "minimumfont", "centertext", "autoshrink"});
    if (type == "ImageContainer") fields.unite({"keepaspect", "frame", "nineslice", "nineslicewidth"});
    return fields;
}

bool MenuEditorWindow::validateStoragePage(QString& error)
{
    if (selectedPage != "saveload" && selectedPage != "buysell") return true;
    QMap<QString, QString> types;
    for (const auto& component : currentDefinition.components) types[component.name] = component.type;
    QMap<QString, QString> required = selectedPage == "saveload"
        ? QMap<QString, QString>{{"saveBtn", "Button"}, {"loadBtn", "Button"}, {"exitBtn", "Button"}, {"snap", "ImageContainer"}, {"listBox", "ListBox"}}
        : QMap<QString, QString>{{"closeBtn", "Button"}, {"scrollbar", "Scrollbar"}, {"item1", "Item"}};
    const QSet<QString> optional = selectedPage == "saveload"
        ? QSet<QString>{"saveBtn", "loadBtn", "exitBtn", "snap"} : QSet<QString>{"closeBtn"};
    if (selectedPage == "buysell")
    {
        int count = 0;
        for (const auto& component : currentDefinition.components)
            if (QRegularExpression("^item[1-9][0-9]*$").match(component.name).hasMatch()) ++count;
        for (int i = 1; i <= count; ++i) required["item" + QString::number(i)] = "Item";
    }
    for (auto it = required.constBegin(); it != required.constEnd(); ++it)
    {
        // SaveLoad guards absent action/screenshot controls; only its ListBox is mandatory.
        if (!types.contains(it.key()) && optional.contains(it.key())) continue;
        const QString type = types.value(it.key());
        if (type == it.value() || (it.value() == "Button" && QStringList{"TextButton", "FlatTextButton", "ChooseTextButton", "CheckBox", "RoundButton", "DragButton", "DragRoundButton", "Joystick"}.contains(type))) continue;
        error = tr("%1：缺少固定组件或类型不符：%2（%3）").arg(pageEntryResource, it.key(), it.value());
        return false;
    }
    for (const auto& component : currentDefinition.components)
    {
        if (component.type != "ListBox" || component.name != "listBox") continue;
        const auto props = propertiesForResource(component.file);
        if (props.itemCount <= 0 || (props.fontSet && props.font <= 0)
            || (!enabled(props.rawValues.value("fititems")) && props.itemHeight <= 0)
            || (props.rawValues.contains("fititems") && !QStringList{"true", "false", "1", "0", "yes", "no", "on", "off"}.contains(props.rawValues.value("fititems").toLower())))
        {
            error = tr("存档列表的数量、字号或行高设置无效：%1").arg(component.file);
            return false;
        }
    }
    return true;
}

bool MenuEditorWindow::validateStorageLabels(QString& error) const
{
    if (selectedPage != "saveload") return true;
    for (const auto& pending : modifiedComponentInis)
    {
        if (!pending.stagedForWrite || !pending.ownerComponentTypes.values().contains("ListBox")) continue;
        INIFileEditor original;
        QFile source(pending.sourcePath);
        if (source.open(QIODevice::ReadOnly))
            original.loadFromString(source.readAll().toStdString(), INIFileEditor::UnrecognizedLinePolicy::Preserve);
        for (int index = 0; index < pending.properties.items.size(); ++index)
        {
            const auto value = pending.properties.items[index].toStdString();
            if (value != original.get("Items", std::to_string(index + 1)) && !INIFileEditor::canRoundTripValue(value))
            {
                error = tr("列表项 %1 无法按现有 INI 格式完整保存：%2").arg(index + 1).arg(pending.resourcePath);
                return false;
            }
        }
    }
    return true;
}

QStringList MenuEditorWindow::storagePreviewStates()
{
    if (selectedPage == "buysell") return {tr("布局编辑（全部组件）"), tr("购买：首页（示例）"), tr("购买：末页（示例）"), tr("出售／回购（示例）"), tr("只买（示例）")};
    if (selectedPage != "saveload") return {};
    QStringList states = {tr("布局编辑（全部组件）"), tr("保存＋读取（示例）"), tr("仅读取（示例）"), tr("仅保存（示例）"), tr("空槽（读取无动作）")};
    for (const auto& component : currentDefinition.components)
        if (component.name == "listBox" && component.type == "ListBox")
        {
            const auto props = propertiesForResource(component.file);
            if (props.itemCount >= SaveSlots::Automatic && enabled(props.rawValues.value("automaticsave"))) states.append(tr("自动槽（禁止手动保存）"));
        }
    return states;
}

void MenuEditorWindow::updateStoragePreview(QList<PreviewComponent>& components)
{
    if (selectedPage != "saveload" && selectedPage != "buysell") return;
    const int state = toolTipPreviewCombo->currentIndex();
    for (auto& component : components)
    {
        if (selectedPage == "saveload")
        {
            if (component.name == "listBox" && component.type == "ListBox")
            {
                const bool automatic = enabled(component.rawValues.value("automaticsave"));
                component.listBoxItems = component.listBoxItems.mid(0, automatic ? SaveSlots::Automatic : SaveSlots::ManualCount);
                component.selectedIndex = state == 0 ? -1 : state == 5 ? SaveSlots::Automatic - 1
                    : state == 4 ? qMin(1, static_cast<int>(component.listBoxItems.size()) - 1) : 0;
            }
            if (state > 0 && component.name == "saveBtn") component.visible = state != 2 && state != 5;
            if (state > 0 && component.name == "loadBtn") component.visible = state != 3;
            if (component.name == "snap")
            {
                component.imagePath.clear(); component.image = QPixmap(); component.stretch = true;
                component.sampleImage = true;
                if (state > 0 && state != 4 && component.rect.width() > 0 && component.rect.height() > 0)
                {
                    QPixmap shot(320, 240); shot.fill(QColor(38, 75, 88));
                    QPainter painter(&shot); painter.fillRect(80, 45, 160, 150, QColor(162, 128, 72));
                    painter.setPen(Qt::white); painter.drawText(shot.rect(), Qt::AlignCenter, tr("存档截图（示例）")); painter.end();
                    component.image = shot;
                }
            }
        }
        else if (state > 0)
        {
            if (component.name == "scrollbar" && !component.children.isEmpty())
            {
                for (const auto& definition : currentDefinition.components)
                    if (definition.editorId == component.editorId)
                    {
                        const auto props = propertiesForResource(definition.file);
                        auto& thumb = component.children.first();
                        const bool fitted = enabled(props.rawValues.value("fittrack"));
                        const int begin = fitted ? 0 : props.slideBegin;
                        const int end = fitted ? qMax(0, props.style == 1 ? component.rect.height() - thumb.rect.height() : component.rect.width() - thumb.rect.width()) : props.slideEnd;
                        if (props.style == 1) thumb.rect.moveTop(component.rect.top() + (state == 2 ? end : begin));
                        else thumb.rect.moveLeft(component.rect.left() + (state == 2 ? end : begin));
                    }
            }
            if (component.name == "money")
                for (const auto& definition : currentDefinition.components)
                    if (definition.editorId == component.editorId && definition.bind == "player.money"
                        && validMenuBindingFormat(definition.format, definition.bind))
                        component.text = QString::asprintf(definition.format.toUtf8().constData(), 1234);
            if (component.type == "Item" && QRegularExpression("^item[1-9][0-9]*$").match(component.name).hasMatch())
            {
                const int index = component.name.mid(4).toInt();
                component.imagePath.clear(); component.sampleImage = true;
                component.image = QPixmap(24, 24); component.image.fill(index % 2 ? QColor(137, 99, 51) : QColor(51, 120, 120));
                component.stretch = false;
                component.text = state == 3 ? "2" : "1";
                if (state == 2 && index > 2) { component.image = QPixmap(); component.text.clear(); }
            }
        }
    }
}
