#include "MenuEditorWindow.h"
#include "../../src/Game/Menu/PartnerHeadLayout.h"
#include <QComboBox>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QFileInfo>
#include <QUuid>
#include <QToolBar>

void MenuEditorWindow::preparePartnerHeadDefaults()
{
    if (selectedPage != "partnerhead" || !pageResources.valid) return;
    const PartnerHeadLayout defaults;
    for (const QString& name : {QString("head"), QString("level")})
    {
        bool found = false;
        for (const auto& component : currentDefinition.components) found |= component.name == name;
        if (found) continue;
        MenuComponentDefinition component;
        component.editorId = "component/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        component.name = name;
        component.type = name == "head" ? "Item" : "Label";
        component.file = QFileInfo(pageEntryResource).path() + "/runtime-default/" + name + ".ini";
        component.runtimeDefault = true;
        currentDefinition.components.append(component);
    }
    for (const auto& component : currentDefinition.components)
    {
        if (!component.runtimeDefault) continue;
        QString target;
        if (!resolveLocalResourcePath(component.file, target)) continue;
        auto& value = modifiedComponentInis[target];
        if (value.stagedForWrite) continue;
        const Rect rectangle = component.name == "head" ? defaults.head : defaults.level;
        value.properties.left = rectangle.x; value.properties.top = rectangle.y;
        value.properties.width = rectangle.w; value.properties.height = rectangle.h;
        if (component.name == "level") { value.properties.font = defaults.font; value.properties.fontSet = true; }
        value.properties.color = defaults.color;
        value.properties.stretch = defaults.stretch; value.properties.stretchSet = true;
        value.resourcePath = component.file;
        value.owners.insert(component.editorId);
        value.ownerComponentTypes[component.editorId] = component.type;
    }
}

void MenuEditorWindow::setupPartnerHeadTools()
{
    auto* toolbar = findChild<QToolBar*>("toolBar");
    partnerHeadPreview = new QComboBox(this);
    partnerHeadPreview->setObjectName("partnerHeadPreviewState");
    partnerHeadPreview->addItems({tr("头像模板（布局编辑）"), tr("无伙伴（示例）"), tr("多个伙伴（原尺寸／权限示例）")});
    partnerHeadPreview->setToolTip(tr("示例头像尺寸及等级不保存；等级位置相对头像右下角。不可查看伙伴显示为灰色示例。"));
    partnerHeadPreviewAction = toolbar->addWidget(partnerHeadPreview);
    connect(partnerHeadPreview, &QComboBox::currentIndexChanged, this, [this]() { updatePreview(); });
    partnerHeadSpacing = new QSpinBox(this);
    partnerHeadSpacing->setObjectName("partnerHeadSpacingY");
    partnerHeadSpacing->setRange(0, 100000);
    partnerHeadSpacing->setPrefix(tr("头像间距："));
    partnerHeadSpacing->setAccessibleName(tr("头像垂直间距"));
    partnerHeadSpacingAction = toolbar->addWidget(partnerHeadSpacing);
    connect(partnerHeadSpacing, &QSpinBox::valueChanged, this, [this](int value)
    {
        if (updatingFromCode || selectedPage != "partnerhead" || !canEditPage()) return;
        QString owner, file; bool materialize = false;
        for (const auto& component : currentDefinition.components)
            if (component.name == "head") { owner = component.editorId; file = component.file; materialize = component.runtimeDefault; break; }
        if (owner.isEmpty()) return;
        if (materialize && !stageComponentProperties(owner, file, propertiesForResource(file), "Item", true)) return;
        for (auto& component : currentDefinition.components)
            if (component.editorId == owner) component.spacingY = QString::number(value);
        markModified();
        updatePreview();
        updatePagePermissions();
    });
}

void MenuEditorWindow::updatePartnerHeadPreview(QList<PreviewComponent>& components)
{
    const bool active = selectedPage == "partnerhead";
    partnerHeadPreviewAction->setVisible(active);
    partnerHeadSpacingAction->setVisible(active);
    if (!active) return;
    PartnerHeadLayout layout;
    for (const auto& component : currentDefinition.components)
    {
        const auto p = propertiesForResource(component.file);
        if (component.name == "head")
        {
            layout.head = {p.left, p.top, p.width, p.height}; layout.stretch = p.stretch;
            if (!component.spacingY.isEmpty()) layout.spacingY = component.spacingY.toInt();
        }
        if (component.name == "level")
        {
            layout.level = {p.left, p.top, p.width, p.height}; layout.font = p.font; layout.color = p.color;
        }
    }
    const QSignalBlocker blocker(partnerHeadSpacing);
    partnerHeadSpacing->setValue(layout.spacingY);
    partnerHeadSpacing->setEnabled(canEditPage());
    const int state = partnerHeadPreview->currentIndex();
    auto templates = components;
    components.clear();
    int y = layout.head.y;
    const int count = state == 1 ? 0 : (state == 2 ? 3 : 1);
    const QSize nativeSizes[] = {{32, 48}, {42, 24}, {24, 64}};
    for (int index = 0; index < count; ++index)
    {
        const auto native = state == 0 ? QSize(32,32) : nativeSizes[index];
        const auto headRect = layout.headRectangle(native.width(), native.height(), y);
        const auto levelRect = layout.levelRectangle(headRect);
        for (auto sample : templates)
        {
            if (sample.name != "head" && sample.name != "level") { if (index == 0) components.append(sample); continue; }
            const bool head = sample.name == "head";
            const Rect rectangle = head ? headRect : levelRect;
            sample.rect = {rectangle.x, rectangle.y, rectangle.w, rectangle.h};
            sample.movable = state == 0 && head && canEditGeometry(sample.editorId, false);
            sample.resizable = sample.movable && layout.stretch;
            if (head)
            {
                sample.rawValues["partnerheadsample"] = "true";
                sample.image = QPixmap(native);
                sample.image.fill(index == 2 ? QColor(100,100,100) : QColor(85 + index*40, 135, 110));
                sample.stretch = layout.stretch;
            }
            else
            {
                sample.text = "LV" + QString::number(10 + index*5);
                sample.visible = index != 1;
                sample.fontSize = layout.font; sample.color = layout.color;
            }
            if (state != 0) { sample.editorId.clear(); sample.movable = sample.resizable = false; }
            components.append(sample);
        }
        y += headRect.h + layout.spacingY;
    }
    previewCanvas->setMenuRect({0,0, qMax(240,layout.head.x + layout.head.w + layout.level.x + layout.level.w), qMax(180,y+layout.spacingY)});
}
