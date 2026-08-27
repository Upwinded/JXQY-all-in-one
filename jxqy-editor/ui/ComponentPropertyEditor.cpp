#include "ComponentPropertyEditor.h"
#include "ui_ComponentPropertyEditor.h"
#include "FilePickerHelper.h"
#include "../core/EditorAssetPath.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QEvent>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QToolButton>

namespace
{
QString colorValueText(unsigned int value)
{
    return QStringLiteral("0x") + QString("%1").arg(value, 8, 16, QChar('0')).toUpper();
}

unsigned int colorValueFromText(const QString& text, unsigned int fallback, bool* parsed = nullptr)
{
    QString valueText = text.trimmed();
    if (valueText.startsWith('#'))
    {
        const QString hexadecimal = valueText.mid(1);
        if (hexadecimal.size() == 6)
        {
            valueText = QStringLiteral("0xFF") + hexadecimal;
        }
        else if (hexadecimal.size() == 8)
        {
            valueText = QStringLiteral("0x") + hexadecimal;
        }
        else
        {
            if (parsed)
                *parsed = false;
            return fallback;
        }
    }

    bool ok = false;
    const unsigned int value = valueText.toUInt(&ok, 0);
    if (parsed)
    {
        *parsed = ok;
    }
    return ok ? value : fallback;
}
}

ComponentPropertyEditor::ComponentPropertyEditor(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::ComponentPropertyEditor)
{
    ui->setupUi(this);

    advancedPropertiesButton = new QToolButton(ui->scrollContent);
    advancedPropertiesButton->setObjectName(
        QStringLiteral("advancedPropertiesButton"));
    advancedPropertiesButton->setText(tr("高级属性"));
    advancedPropertiesButton->setCheckable(true);
    advancedPropertiesButton->setArrowType(Qt::RightArrow);
    advancedPropertiesButton->setToolButtonStyle(
        Qt::ToolButtonTextBesideIcon);
    ui->mainLayout->insertWidget(0, advancedPropertiesButton);
    connect(
        advancedPropertiesButton,
        &QToolButton::toggled,
        this,
        [this](bool)
        {
            updateAdvancedPropertyVisibility();
        });

    iniNameEdit = new QLineEdit(ui->commonGroup);
    iniNameEdit->setObjectName(QStringLiteral("iniNameEdit"));
    ui->commonFormLayout->insertRow(0, tr("内部名称"), iniNameEdit);

    menuWindowBitmapEdit = new QLineEdit(ui->menuWindowGroup);
    menuWindowBitmapEdit->setObjectName(QStringLiteral("menuWindowBitmapEdit"));
    ui->menuWindowFormLayout->insertRow(4, tr("备用图片"), menuWindowBitmapEdit);

    scrollbarPositionSpin = new QSpinBox(ui->scrollbarGroup);
    scrollbarPositionSpin->setObjectName(QStringLiteral("scrollbarPositionSpin"));
    scrollbarPositionSpin->setRange(-999999, 999999);
    ui->scrollbarFormLayout->insertRow(3, tr("当前位置"), scrollbarPositionSpin);

    imageOptionsGroup = new QGroupBox(tr("图像高级属性"), ui->scrollContent);
    imageOptionsGroup->setObjectName(QStringLiteral("imageOptionsGroup"));
    auto imageOptionsLayout = new QFormLayout(imageOptionsGroup);
    bitmapEdit = new QLineEdit(imageOptionsGroup);
    bitmapEdit->setObjectName(QStringLiteral("bitmapEdit"));
    frameSpin = new QSpinBox(imageOptionsGroup);
    frameSpin->setObjectName(QStringLiteral("frameSpin"));
    frameSpin->setRange(-1, 999999);
    keepAspectCheck = new QCheckBox(imageOptionsGroup);
    keepAspectCheck->setObjectName(QStringLiteral("keepAspectCheck"));
    centerImageCheck = new QCheckBox(imageOptionsGroup);
    centerImageCheck->setObjectName(QStringLiteral("centerImageCheck"));
    cropContentCheck = new QCheckBox(imageOptionsGroup);
    cropContentCheck->setObjectName(QStringLiteral("cropContentCheck"));
    cropBlackCheck = new QCheckBox(imageOptionsGroup);
    cropBlackCheck->setObjectName(QStringLiteral("cropBlackCheck"));
    backImage1Edit = new QLineEdit(imageOptionsGroup);
    backImage1Edit->setObjectName(QStringLiteral("backImage1Edit"));
    backImage2Edit = new QLineEdit(imageOptionsGroup);
    backImage2Edit->setObjectName(QStringLiteral("backImage2Edit"));
    imageOptionsLayout->addRow(tr("备用图片"), bitmapEdit);
    imageOptionsLayout->addRow(tr("帧"), frameSpin);
    imageOptionsLayout->addRow(tr("保持宽高比"), keepAspectCheck);
    imageOptionsLayout->addRow(tr("图片居中"), centerImageCheck);
    imageOptionsLayout->addRow(tr("裁剪内容"), cropContentCheck);
    imageOptionsLayout->addRow(tr("裁剪黑边"), cropBlackCheck);
    imageOptionsLayout->addRow(tr("背景图片 1"), backImage1Edit);
    imageOptionsLayout->addRow(tr("背景图片 2"), backImage2Edit);
    ui->mainLayout->insertWidget(ui->mainLayout->indexOf(ui->commonGroup) + 1, imageOptionsGroup);

    buttonAdvancedGroup = new QGroupBox(tr("按钮高级属性"), ui->scrollContent);
    buttonAdvancedGroup->setObjectName(QStringLiteral("buttonAdvancedGroup"));
    auto buttonAdvancedLayout = new QFormLayout(buttonAdvancedGroup);
    hoverSoundCheck = new QCheckBox(buttonAdvancedGroup);
    hoverSoundCheck->setObjectName(QStringLiteral("hoverSoundCheck"));
    animateCheck = new QCheckBox(buttonAdvancedGroup);
    animateCheck->setObjectName(QStringLiteral("animateCheck"));
    buttonAdvancedLayout->addRow(tr("悬停音效"), hoverSoundCheck);
    buttonAdvancedLayout->addRow(tr("动画帧"), animateCheck);
    ui->mainLayout->insertWidget(ui->mainLayout->indexOf(ui->buttonGroup) + 1, buttonAdvancedGroup);

    labelAdvancedGroup = new QGroupBox(tr("标签高级属性"), ui->scrollContent);
    labelAdvancedGroup->setObjectName(QStringLiteral("labelAdvancedGroup"));
    auto labelAdvancedLayout = new QFormLayout(labelAdvancedGroup);
    autoShrinkCheck = new QCheckBox(labelAdvancedGroup);
    autoShrinkCheck->setObjectName(QStringLiteral("autoShrinkCheck"));
    labelAdvancedLayout->addRow(tr("自动缩小"), autoShrinkCheck);
    ui->mainLayout->insertWidget(ui->mainLayout->indexOf(ui->labelGroup) + 1, labelAdvancedGroup);

    roundIconGroup = new QGroupBox(tr("圆形按钮图标"), ui->scrollContent);
    roundIconGroup->setObjectName(QStringLiteral("roundIconGroup"));
    auto roundIconLayout = new QFormLayout(roundIconGroup);
    iconEdit = new QLineEdit(roundIconGroup);
    iconEdit->setObjectName(QStringLiteral("iconEdit"));
    iconImageEdit = new QLineEdit(roundIconGroup);
    iconImageEdit->setObjectName(QStringLiteral("iconImageEdit"));
    roundIconLayout->addRow(tr("图标名称"), iconEdit);
    roundIconLayout->addRow(tr("图标图片"), iconImageEdit);
    ui->mainLayout->insertWidget(ui->mainLayout->indexOf(ui->roundButtonGroup) + 1, roundIconGroup);

    auto tagFormLabel = [](QFormLayout* layout, QWidget* field)
    {
        if (QWidget* label = layout->labelForField(field))
            label->setProperty("jxqyFormField", field->objectName());
    };
    tagFormLabel(ui->commonFormLayout, iniNameEdit);
    tagFormLabel(ui->menuWindowFormLayout, menuWindowBitmapEdit);
    tagFormLabel(ui->scrollbarFormLayout, scrollbarPositionSpin);
    const QList<QWidget*> imageOptionFields = {
        bitmapEdit, frameSpin, keepAspectCheck, centerImageCheck,
        cropContentCheck, cropBlackCheck, backImage1Edit, backImage2Edit};
    for (QWidget* field : imageOptionFields)
    {
        tagFormLabel(imageOptionsLayout, field);
    }
    tagFormLabel(buttonAdvancedLayout, hoverSoundCheck);
    tagFormLabel(buttonAdvancedLayout, animateCheck);
    tagFormLabel(labelAdvancedLayout, autoShrinkCheck);
    tagFormLabel(roundIconLayout, iconEdit);
    tagFormLabel(roundIconLayout, iconImageEdit);

    for (int i = 0; i < ui->menuWindowAlignCombo->count(); i++)
    {
        ui->menuWindowAlignCombo->setItemData(i, i);
    }

    auto emitDefinitionChanged = [this](const QString&)
    {
        if (!updatingFromCode)
        {
            emit definitionPropertiesChanged();
        }
    };
    auto emitResourceEditingFinished = [this]()
    {
        if (!updatingFromCode)
        {
            emit resourceFileEditingFinished();
        }
    };

    connect(ui->menuWindowFileEdit, &QLineEdit::editingFinished, this, emitResourceEditingFinished);
    connect(ui->menuWindowWidthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->menuWindowHeightSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->menuWindowImageEdit, &QLineEdit::textChanged, this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(menuWindowBitmapEdit, &QLineEdit::textChanged, this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->menuWindowAlignCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->menuWindowAlignXSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->menuWindowAlignYSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->menuWindowStretchCheck, &QCheckBox::toggled, this, &ComponentPropertyEditor::onAnyPropertyChanged);

    connect(ui->nameEdit, &QLineEdit::textChanged, this, emitDefinitionChanged);
    connect(ui->bindEdit, &QLineEdit::textChanged, this, emitDefinitionChanged);
    connect(ui->formatEdit, &QLineEdit::textChanged, this, emitDefinitionChanged);
    connect(ui->controllerUpEdit, &QLineEdit::textChanged, this, emitDefinitionChanged);
    connect(ui->controllerDownEdit, &QLineEdit::textChanged, this, emitDefinitionChanged);
    connect(ui->controllerLeftEdit, &QLineEdit::textChanged, this, emitDefinitionChanged);
    connect(ui->controllerRightEdit, &QLineEdit::textChanged, this, emitDefinitionChanged);
    connect(ui->fileEdit, &QLineEdit::editingFinished, this, emitResourceEditingFinished);

    connect(iniNameEdit, &QLineEdit::textChanged, this, [this](const QString&)
    {
        if (!updatingFromCode)
        {
            currentProperties.nameSet = true;
            emit propertiesChanged();
        }
    });

    connect(ui->leftSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->topSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->widthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->heightSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->imageEdit, &QLineEdit::textChanged, this, &ComponentPropertyEditor::onAnyPropertyChanged);

    connect(ui->buttonSoundEdit, &QLineEdit::textChanged, this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->upSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->downSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->trackSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);

    connect(ui->itemFontSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);

    connect(ui->labelFontSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);

    connect(ui->scrollbarStyleSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->minSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->maxSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->lineSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->pageSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->slideBeginSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->slideEndSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->slideBtnEdit, &QLineEdit::textChanged, this, &ComponentPropertyEditor::onAnyPropertyChanged);

    connect(ui->listBoxSoundEdit, &QLineEdit::textChanged, this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->itemHeightSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->itemCountSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onItemCountChanged);

    connect(ui->listBoxAddItemButton, &QPushButton::clicked, this, &ComponentPropertyEditor::onListBoxAddItem);
    connect(ui->listBoxRemoveItemButton, &QPushButton::clicked, this, &ComponentPropertyEditor::onListBoxRemoveItem);
    connect(ui->listBoxEditItemButton, &QPushButton::clicked, this, &ComponentPropertyEditor::onListBoxEditItem);

    connect(ui->rangeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->textEdit, &QLineEdit::textChanged, this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->joystickBaseImageEdit, &QLineEdit::textChanged, this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->joystickThumbImageEdit, &QLineEdit::textChanged, this, &ComponentPropertyEditor::onAnyPropertyChanged);

    connect(ui->indicateTypeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->indicateImageEdit, &QLineEdit::textChanged, this, &ComponentPropertyEditor::onAnyPropertyChanged);

    connect(ui->buttonKindCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->buttonKindCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ComponentPropertyEditor::updateTrackVisibility);
    connect(ui->colorEdit, &QLineEdit::textChanged, this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->stretchCheck, &QCheckBox::toggled, this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->itemColorEdit, &QLineEdit::textChanged, this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->labelColorEdit, &QLineEdit::textChanged, this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->listBoxColorEdit, &QLineEdit::textChanged, this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->listBoxSelColorEdit, &QLineEdit::textChanged, this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->normalColorEdit, &QLineEdit::textChanged, this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->hoverColorEdit, &QLineEdit::textChanged, this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->pressColorEdit, &QLineEdit::textChanged, this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->percentSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);

    connect(ui->subMenuWindowFileEdit, &QLineEdit::editingFinished, this, emitResourceEditingFinished);
    connect(ui->subMenuBackgroundImageEdit, &QLineEdit::textChanged, this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->subMenuRectLeftSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->subMenuRectTopSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->subMenuRectWidthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    connect(ui->subMenuRectHeightSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);

    connect(bitmapEdit, &QLineEdit::textChanged, this, [this](const QString&)
    {
        if (!updatingFromCode)
        {
            currentProperties.bitmapSet = true;
            emit propertiesChanged();
        }
    });
    connect(frameSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int)
    {
        if (!updatingFromCode)
        {
            currentProperties.frameSet = true;
            emit propertiesChanged();
        }
    });
    connect(keepAspectCheck, &QCheckBox::toggled, this, [this](bool)
    {
        if (!updatingFromCode)
        {
            currentProperties.keepAspectSet = true;
            emit propertiesChanged();
        }
    });
    connect(centerImageCheck, &QCheckBox::toggled, this, [this](bool)
    {
        if (!updatingFromCode)
        {
            currentProperties.centerImageSet = true;
            emit propertiesChanged();
        }
    });
    connect(cropContentCheck, &QCheckBox::toggled, this, [this](bool)
    {
        if (!updatingFromCode)
        {
            currentProperties.cropContentSet = true;
            emit propertiesChanged();
        }
    });
    connect(cropBlackCheck, &QCheckBox::toggled, this, [this](bool)
    {
        if (!updatingFromCode)
        {
            currentProperties.cropBlackSet = true;
            emit propertiesChanged();
        }
    });
    connect(backImage1Edit, &QLineEdit::textChanged, this, [this](const QString&)
    {
        if (!updatingFromCode)
        {
            currentProperties.backImage1Set = true;
            emit propertiesChanged();
        }
    });
    connect(backImage2Edit, &QLineEdit::textChanged, this, [this](const QString&)
    {
        if (!updatingFromCode)
        {
            currentProperties.backImage2Set = true;
            emit propertiesChanged();
        }
    });
    connect(hoverSoundCheck, &QCheckBox::toggled, this, [this](bool)
    {
        if (!updatingFromCode)
        {
            currentProperties.hoverSoundSet = true;
            emit propertiesChanged();
        }
    });
    connect(animateCheck, &QCheckBox::toggled, this, [this](bool)
    {
        if (!updatingFromCode)
        {
            currentProperties.animateSet = true;
            emit propertiesChanged();
        }
    });
    connect(iconEdit, &QLineEdit::textChanged, this, [this](const QString&)
    {
        if (!updatingFromCode)
        {
            currentProperties.iconSet = true;
            emit propertiesChanged();
        }
    });
    connect(iconImageEdit, &QLineEdit::textChanged, this, [this](const QString&)
    {
        if (!updatingFromCode)
        {
            currentProperties.iconImageSet = true;
            emit propertiesChanged();
        }
    });
    connect(autoShrinkCheck, &QCheckBox::toggled, this, [this](bool)
    {
        if (!updatingFromCode)
        {
            currentProperties.autoShrinkSet = true;
            emit propertiesChanged();
        }
    });
    connect(scrollbarPositionSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int)
    {
        if (!updatingFromCode)
        {
            currentProperties.positionSet = true;
            emit propertiesChanged();
        }
    });

    hideAllTypeSpecificGroups();

    addColorPickerButton(ui->colorEdit);
    addColorPickerButton(ui->itemColorEdit);
    addColorPickerButton(ui->labelColorEdit);
    addColorPickerButton(ui->listBoxColorEdit);
    addColorPickerButton(ui->listBoxSelColorEdit);
    addColorPickerButton(ui->normalColorEdit);
    addColorPickerButton(ui->hoverColorEdit);
    addColorPickerButton(ui->pressColorEdit);

    const QString imageFileFilter =
        tr("Image Files (*.mpc *.shd *.asf *.imp *.img *.pic *.png *.jpg *.jpeg *.bmp *.gif *.webp *.tga);;All Files (*)");

    auto getAssetsPath = [this]() -> QString { return assetsBasePath; };

    FilePickerHelper::addFilePickerButton(ui->imageEdit, imageFileFilter, getAssetsPath, true, this);
    FilePickerHelper::addFilePickerButton(ui->joystickBaseImageEdit, imageFileFilter, getAssetsPath, true, this);
    FilePickerHelper::addFilePickerButton(ui->joystickThumbImageEdit, imageFileFilter, getAssetsPath, true, this);
    FilePickerHelper::addFilePickerButton(ui->menuWindowImageEdit, imageFileFilter, getAssetsPath, true, this);
    FilePickerHelper::addFilePickerButton(menuWindowBitmapEdit, imageFileFilter, getAssetsPath, true, this);
    FilePickerHelper::addFilePickerButton(ui->indicateImageEdit, imageFileFilter, getAssetsPath, true, this);
    FilePickerHelper::addFilePickerButton(ui->subMenuBackgroundImageEdit, imageFileFilter, getAssetsPath, true, this);
    FilePickerHelper::addFilePickerButton(bitmapEdit, imageFileFilter, getAssetsPath, true, this);
    FilePickerHelper::addFilePickerButton(backImage1Edit, imageFileFilter, getAssetsPath, true, this);
    FilePickerHelper::addFilePickerButton(backImage2Edit, imageFileFilter, getAssetsPath, true, this);
    FilePickerHelper::addFilePickerButton(iconImageEdit, imageFileFilter, getAssetsPath, true, this);
    FilePickerHelper::addFilePickerButton(ui->buttonSoundEdit, tr("Sound Files (*.wav *.mp3 *.ogg);;All Files (*)"), getAssetsPath, true, this);
    FilePickerHelper::addFilePickerButton(ui->listBoxSoundEdit, ui->listBoxFormLayout,
        tr("Sound Files (*.wav *.mp3 *.ogg);;All Files (*)"), getAssetsPath, true, this);

    setupAssetDropTargets();

    const QString iniFilter = tr("INI Files (*.ini);;All Files (*)");
    FilePickerHelper::addFilePickerButton(ui->fileEdit, iniFilter, getAssetsPath, true, this);
    FilePickerHelper::addFilePickerButton(ui->menuWindowFileEdit, iniFilter, getAssetsPath, true, this);
    FilePickerHelper::addFilePickerButton(ui->subMenuWindowFileEdit, iniFilter, getAssetsPath, true, this);
    FilePickerHelper::addFilePickerButton(ui->slideBtnEdit, iniFilter, getAssetsPath, true, this);

    // Clicking the picker moves focus out of the line edit before the dialog
    // writes the selected path. Emit once more after the picker's synchronous
    // handler returns so callers reload the newly selected resource, not the
    // previous path.
    for (QLineEdit* resourceEdit : {ui->fileEdit, ui->menuWindowFileEdit, ui->subMenuWindowFileEdit})
    {
        QWidget* wrapper = resourceEdit->parentWidget();
        if (!wrapper)
        {
            continue;
        }
        const auto pickerButtons = wrapper->findChildren<QPushButton*>(QString(), Qt::FindDirectChildrenOnly);
        for (QPushButton* pickerButton : pickerButtons)
        {
            connect(pickerButton, &QPushButton::clicked, this, emitResourceEditingFinished);
        }
    }

    retranslateDynamicUi();
    updateAdvancedPropertyVisibility();
}

ComponentPropertyEditor::~ComponentPropertyEditor()
{
    delete ui;
}

void ComponentPropertyEditor::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        ui->retranslateUi(this);
        retranslateDynamicUi();
    }
    QWidget::changeEvent(event);
}

void ComponentPropertyEditor::retranslateDynamicUi()
{
    advancedPropertiesButton->setText(tr("高级属性"));

    auto setFormLabel = [](QFormLayout* layout, QWidget* field, const QString& text)
    {
        QWidget* label = layout ? layout->labelForField(field) : nullptr;
        if (!label && layout && layout->parentWidget())
        {
            const auto labels = layout->parentWidget()->findChildren<QLabel*>();
            for (QLabel* candidate : labels)
            {
                if (candidate->buddy() == field ||
                    candidate->property("jxqyFormField").toString() == field->objectName())
                {
                    label = candidate;
                    break;
                }
            }
        }
        if (label)
        {
            if (auto* textLabel = qobject_cast<QLabel*>(label))
                textLabel->setText(text);
        }
    };

    setFormLabel(ui->commonFormLayout, iniNameEdit, tr("内部名称"));
    setFormLabel(ui->menuWindowFormLayout, menuWindowBitmapEdit, tr("备用图片"));
    setFormLabel(ui->scrollbarFormLayout, scrollbarPositionSpin, tr("当前位置"));

    imageOptionsGroup->setTitle(tr("图像高级属性"));
    auto* imageLayout = qobject_cast<QFormLayout*>(imageOptionsGroup->layout());
    setFormLabel(imageLayout, bitmapEdit, tr("备用图片"));
    setFormLabel(imageLayout, frameSpin, tr("帧"));
    setFormLabel(imageLayout, keepAspectCheck, tr("保持宽高比"));
    setFormLabel(imageLayout, centerImageCheck, tr("图片居中"));
    setFormLabel(imageLayout, cropContentCheck, tr("裁剪内容"));
    setFormLabel(imageLayout, cropBlackCheck, tr("裁剪黑边"));
    setFormLabel(imageLayout, backImage1Edit, tr("背景图片 1"));
    setFormLabel(imageLayout, backImage2Edit, tr("背景图片 2"));

    buttonAdvancedGroup->setTitle(tr("按钮高级属性"));
    auto* buttonLayout = qobject_cast<QFormLayout*>(buttonAdvancedGroup->layout());
    setFormLabel(buttonLayout, hoverSoundCheck, tr("悬停音效"));
    setFormLabel(buttonLayout, animateCheck, tr("动画帧"));

    labelAdvancedGroup->setTitle(tr("标签高级属性"));
    setFormLabel(qobject_cast<QFormLayout*>(labelAdvancedGroup->layout()),
        autoShrinkCheck, tr("自动缩小"));

    roundIconGroup->setTitle(tr("圆形按钮图标"));
    auto* iconLayout = qobject_cast<QFormLayout*>(roundIconGroup->layout());
    setFormLabel(iconLayout, iconEdit, tr("图标名称"));
    setFormLabel(iconLayout, iconImageEdit, tr("图标图片"));

    const QStringList alignmentNames = {
        tr("不自动对齐"),
        tr("填满窗口"),
        tr("靠左"),
        tr("靠右"),
        tr("靠上"),
        tr("靠下"),
        tr("左上角"),
        tr("右上角"),
        tr("左下角"),
        tr("右下角"),
        tr("居中"),
        tr("左侧居中"),
        tr("右侧居中"),
        tr("顶部居中"),
        tr("底部居中")
    };
    for (int i = 0;
         i < alignmentNames.size() &&
         i < ui->menuWindowAlignCombo->count();
         ++i)
    {
        ui->menuWindowAlignCombo->setItemText(
            i,
            alignmentNames.at(i));
    }

    const auto colorButtons = findChildren<QPushButton*>();
    for (QPushButton* button : colorButtons)
    {
        if (button->property("jxqyColorPicker").toBool())
            button->setToolTip(tr("选择颜色"));
    }
}

void ComponentPropertyEditor::setComponentType(const QString& type)
{
    currentType = type;
    hideAllTypeSpecificGroups();

    const bool buttonType = type == "Button" || type == "DragButton" ||
        type == "TextButton" || type == "ChooseTextButton" ||
        type == "RoundButton" || type == "DragRoundButton";
    const bool roundButtonType = type == "RoundButton" || type == "DragRoundButton";
    const bool itemType = type == "Item" || type == "Label" || type == "TalkLabel";
    const bool imageContainerType = type == "ImageContainer" || type == "TransImage";

    if (type == "MenuWindow")
    {
        showGroup(ui->menuWindowGroup);
        ui->definitionGroup->hide();
        ui->commonGroup->hide();
    }
    else
    {
        ui->definitionGroup->show();
        ui->commonGroup->show();

        if (type == "Button" || type == "DragButton" || type == "CheckBox")
        {
            showGroup(ui->buttonGroup);
        }
        else if (type == "Item")
        {
            showGroup(ui->itemGroup);
        }
        else if (type == "Label" || type == "TalkLabel")
        {
            showGroup(ui->labelGroup);
        }
        else if (type == "MemoText")
        {
            showGroup(ui->labelGroup);
        }
        else if (type == "Scrollbar")
        {
            showGroup(ui->scrollbarGroup);
        }
        else if (type == "ColumnImage")
        {
            showGroup(ui->columnImageGroup);
        }
        else if (type == "ListBox")
        {
            showGroup(ui->listBoxGroup);
        }
        else if (type == "RoundButton")
        {
            showGroup(ui->buttonGroup);
            showGroup(ui->roundButtonGroup);
        }
        else if (type == "DragRoundButton")
        {
            showGroup(ui->buttonGroup);
            showGroup(ui->roundButtonGroup);
            showGroup(ui->dragRoundButtonGroup);
        }
        else if (type == "TextButton")
        {
            showGroup(ui->buttonGroup);
            showGroup(ui->labelGroup);
        }
        else if (type == "ChooseTextButton")
        {
            showGroup(ui->buttonGroup);
            showGroup(ui->labelGroup);
            showGroup(ui->chooseTextButtonGroup);
        }
        else if (type == "ImageContainer" || type == "TransImage")
        {
        }
        else if (type == "Joystick")
        {
            showGroup(ui->roundButtonGroup);
            showGroup(ui->joystickGroup);
        }
        else if (type == "FadeMask")
        {
        }
        else if (type == "SubMenu")
        {
            showGroup(ui->subMenuGroup);
        }
    }

    ui->textLabel->setVisible(roundButtonType);
    ui->textEdit->setVisible(roundButtonType);

    const bool bitmapVisible = buttonType || type == "CheckBox" || itemType || imageContainerType;
    const bool frameVisible = itemType || imageContainerType;
    const bool keepAspectVisible = itemType || imageContainerType;
    const bool centerImageVisible = itemType;
    const bool cropVisible = imageContainerType;
    const bool backImageVisible = itemType;
    auto imageOptionsLayout = qobject_cast<QFormLayout*>(imageOptionsGroup->layout());
    imageOptionsLayout->setRowVisible(bitmapEdit, bitmapVisible);
    imageOptionsLayout->setRowVisible(frameSpin, frameVisible);
    imageOptionsLayout->setRowVisible(keepAspectCheck, keepAspectVisible);
    imageOptionsLayout->setRowVisible(centerImageCheck, centerImageVisible);
    imageOptionsLayout->setRowVisible(cropContentCheck, cropVisible);
    imageOptionsLayout->setRowVisible(cropBlackCheck, cropVisible);
    imageOptionsLayout->setRowVisible(backImage1Edit, backImageVisible);
    imageOptionsLayout->setRowVisible(backImage2Edit, backImageVisible);
    imageAdvancedAvailable = bitmapVisible || frameVisible ||
        keepAspectVisible || centerImageVisible || cropVisible ||
        backImageVisible;
    buttonAdvancedAvailable = buttonType;
    labelAdvancedAvailable = type == "Label" || type == "TalkLabel";

    roundIconGroup->setVisible(roundButtonType);

    updateTrackVisibility();

    // Button 派生类、ImageContainer 派生类和 Item 派生类都会读取 Stretch。
    const bool readsStretch = buttonType || imageContainerType || itemType;
    ui->stretchCheck->setVisible(readsStretch);
    ui->stretchLabel->setVisible(readsStretch);
    updateAdvancedPropertyVisibility();
}

void ComponentPropertyEditor::updateAdvancedPropertyVisibility()
{
    const bool advanced = advancedPropertiesButton &&
        advancedPropertiesButton->isChecked();
    if (advancedPropertiesButton)
    {
        advancedPropertiesButton->setEnabled(!currentType.isEmpty());
        advancedPropertiesButton->setArrowType(
            advanced ? Qt::DownArrow : Qt::RightArrow);
    }

    ui->definitionFormLayout->setRowVisible(ui->bindEdit, advanced);
    ui->definitionFormLayout->setRowVisible(ui->formatEdit, advanced);
    ui->commonFormLayout->setRowVisible(iniNameEdit, advanced);
    ui->menuWindowFormLayout->setRowVisible(
        menuWindowBitmapEdit,
        advanced);
    ui->controllerNavigationGroup->setVisible(
        advanced && !currentType.isEmpty() &&
        currentType != QStringLiteral("MenuWindow"));
    imageOptionsGroup->setVisible(
        advanced && imageAdvancedAvailable);
    buttonAdvancedGroup->setVisible(
        advanced && buttonAdvancedAvailable);
    labelAdvancedGroup->setVisible(
        advanced && labelAdvancedAvailable);
}

void ComponentPropertyEditor::setProperties(const ComponentIniProperties& props)
{
    updatingFromCode = true;
    currentProperties = props;

    iniNameEdit->setText(props.name);
    ui->leftSpin->setValue(props.left);
    ui->topSpin->setValue(props.top);
    ui->widthSpin->setValue(props.width);
    ui->heightSpin->setValue(props.height);
    ui->imageEdit->setText(props.image);
    bitmapEdit->setText(props.bitmap);
    ui->joystickBaseImageEdit->setText(props.baseImage);
    ui->joystickThumbImageEdit->setText(props.thumbImage);

    ui->buttonSoundEdit->setText(props.sound);
    ui->listBoxSoundEdit->setText(props.sound);

    int kindIndex = 0;
    QString kindLower = props.kind.toLower();
    for (int i = 0; i < ui->buttonKindCombo->count(); i++)
    {
        if (ui->buttonKindCombo->itemText(i).toLower() == kindLower)
        {
            kindIndex = i;
            break;
        }
    }
    ui->buttonKindCombo->setCurrentIndex(kindIndex);

    ui->upSpin->setValue(props.up);
    ui->downSpin->setValue(props.down);
    ui->trackSpin->setValue(props.track);
    hoverSoundCheck->setChecked(props.hoverSound);
    animateCheck->setChecked(props.animate);

    ui->colorEdit->setText(colorValueText(props.color));
    ui->itemColorEdit->setText(colorValueText(props.color));
    ui->labelColorEdit->setText(colorValueText(props.color));
    ui->listBoxColorEdit->setText(colorValueText(props.color));
    ui->listBoxSelColorEdit->setText(colorValueText(props.selColor));

    ui->itemFontSpin->setValue(props.fontSet ? props.font : (currentType == "MemoText" ? 16 : 18));
    ui->labelFontSpin->setValue(props.fontSet ? props.font : (currentType == "MemoText" ? 16 : 18));
    ui->scrollbarStyleSpin->setValue(props.style);
    ui->minSpin->setValue(props.min);
    ui->maxSpin->setValue(props.max);
    scrollbarPositionSpin->setValue(props.position);
    ui->lineSizeSpin->setValue(props.lineSize);
    ui->pageSizeSpin->setValue(props.pageSize);
    ui->slideBeginSpin->setValue(props.slideBegin);
    ui->slideEndSpin->setValue(props.slideEnd);
    ui->slideBtnEdit->setText(props.slideBtn);
    ui->itemHeightSpin->setValue(props.itemHeight);
    listBoxItems = props.items;
    while (listBoxItems.size() < props.itemCount)
    {
        listBoxItems.append("");
    }
    ui->itemCountSpin->setValue(qMax(props.itemCount, static_cast<int>(listBoxItems.size())));
    updateListBoxItemsList();
    ui->rangeSpin->setValue(props.range);
    ui->textEdit->setText(props.text);
    ui->indicateTypeSpin->setValue(props.indicateType);
    ui->indicateImageEdit->setText(props.indicateImage);
    ui->percentSpin->setValue(props.percent);
    {
        bool stretchValue = props.stretchSet ? props.stretch :
            (currentType == "TextButton" || currentType == "ChooseTextButton" ||
             currentType == "RoundButton" || currentType == "DragRoundButton" ||
             currentType == "Joystick");
        ui->stretchCheck->setChecked(stretchValue);
    }

    frameSpin->setValue(props.frame);
    keepAspectCheck->setChecked(props.keepAspect);
    centerImageCheck->setChecked(props.centerImage);
    cropContentCheck->setChecked(props.cropContent);
    cropBlackCheck->setChecked(props.cropBlack);
    backImage1Edit->setText(props.backImage1);
    backImage2Edit->setText(props.backImage2);
    iconEdit->setText(props.icon);
    iconImageEdit->setText(props.iconImage);
    autoShrinkCheck->setChecked(props.autoShrink);

    ui->normalColorEdit->setText(colorValueText(props.normalColor));
    ui->hoverColorEdit->setText(colorValueText(props.hoverColor));
    ui->pressColorEdit->setText(colorValueText(props.pressColor));

    updatingFromCode = false;
}

ComponentIniProperties ComponentPropertyEditor::getProperties() const
{
    // Start from the loaded snapshot so that unknown keys and explicit-presence
    // flags survive edits to an unrelated visible field.
    ComponentIniProperties props = currentProperties;
    props.name = iniNameEdit->text();
    props.left = ui->leftSpin->value();
    props.top = ui->topSpin->value();
    props.width = ui->widthSpin->value();
    props.height = ui->heightSpin->value();
    props.image = ui->imageEdit->text();
    props.bitmap = bitmapEdit->text();
    props.baseImage = ui->joystickBaseImageEdit->text();
    props.thumbImage = ui->joystickThumbImageEdit->text();

    if (currentType == "ListBox")
    {
        props.sound = ui->listBoxSoundEdit->text();
    }
    else
    {
        props.sound = ui->buttonSoundEdit->text();
    }

    props.kind = ui->buttonKindCombo->currentText();

    props.up = ui->upSpin->value();
    props.down = ui->downSpin->value();
    props.track = ui->trackSpin->value();
    props.hoverSound = hoverSoundCheck->isChecked();
    props.animate = animateCheck->isChecked();

    bool colorOk = false;
    unsigned int parsedColor = 0;
    if (currentType == "Item")
    {
        parsedColor = colorValueFromText(ui->itemColorEdit->text(), 0xFFFFFFFF, &colorOk);
        props.font = ui->itemFontSpin->value();
    }
    else if (currentType == "ListBox")
    {
        parsedColor = colorValueFromText(ui->listBoxColorEdit->text(), 0xFFFFFFFF, &colorOk);
        bool selectedColorOk = false;
        const unsigned int selectedColor = colorValueFromText(
            ui->listBoxSelColorEdit->text(), props.selColor, &selectedColorOk);
        if (selectedColorOk)
            props.selColor = selectedColor;
    }
    else if (currentType == "Label" || currentType == "TalkLabel" || currentType == "MemoText" ||
             currentType == "TextButton" || currentType == "ChooseTextButton")
    {
        parsedColor = colorValueFromText(ui->labelColorEdit->text(), 0xFFFFFFFF, &colorOk);
        props.font = ui->labelFontSpin->value();
    }
    else
    {
        parsedColor = colorValueFromText(ui->colorEdit->text(), 0xFFFFFFFF, &colorOk);
    }
    // A partially typed/invalid color must not silently replace the loaded
    // value with white. The text edit may emit on every keystroke, so retain
    // the loaded snapshot until the text parses successfully.
    if (colorOk)
        props.color = parsedColor;

    if (currentType == "Item")
    {
        props.font = ui->itemFontSpin->value();
    }
    else if (currentType == "Label" || currentType == "TalkLabel" || currentType == "MemoText" ||
             currentType == "TextButton" || currentType == "ChooseTextButton")
    {
        props.font = ui->labelFontSpin->value();
    }
    // 仅对运行时读取 font 的类型标记 fontSet，避免隐藏控件状态进入数据快照。
    // Item/Label/TalkLabel/MemoText/TextButton/ChooseTextButton 通过 typeSpecificKeys 声明 font。
    if (currentType == "Item" || currentType == "Label" || currentType == "TalkLabel" ||
        currentType == "MemoText" || currentType == "TextButton" || currentType == "ChooseTextButton")
    {
        props.fontSet = true;
    }

    if (currentType == "Scrollbar")
    {
        props.style = ui->scrollbarStyleSpin->value();
    }

    props.min = ui->minSpin->value();
    props.max = ui->maxSpin->value();
    props.position = scrollbarPositionSpin->value();
    props.lineSize = ui->lineSizeSpin->value();
    props.pageSize = ui->pageSizeSpin->value();
    props.slideBegin = ui->slideBeginSpin->value();
    props.slideEnd = ui->slideEndSpin->value();
    props.slideBtn = ui->slideBtnEdit->text();
    props.itemHeight = ui->itemHeightSpin->value();
    props.items = listBoxItems;
    props.itemCount = qMax(ui->itemCountSpin->value(), static_cast<int>(props.items.size()));
    while (props.items.size() < props.itemCount)
    {
        props.items.append("");
    }
    props.range = ui->rangeSpin->value();
    props.text = ui->textEdit->text();
    props.indicateType = ui->indicateTypeSpin->value();
    props.indicateImage = ui->indicateImageEdit->text();
    props.percent = ui->percentSpin->value();
    props.stretch = ui->stretchCheck->isChecked();
    // Only types that consume Stretch may turn the visible value into an
    // explicit property. Other types retain the loaded presence flag.
    if (currentType == "Button" || currentType == "DragButton" ||
        currentType == "TextButton" || currentType == "RoundButton" ||
        currentType == "DragRoundButton" || currentType == "ChooseTextButton" ||
        currentType == "ImageContainer" || currentType == "TransImage" ||
        currentType == "Item" || currentType == "Label" || currentType == "TalkLabel")
    {
        props.stretchSet = true;
    }

    props.frame = frameSpin->value();
    props.keepAspect = keepAspectCheck->isChecked();
    props.centerImage = centerImageCheck->isChecked();
    props.cropContent = cropContentCheck->isChecked();
    props.cropBlack = cropBlackCheck->isChecked();
    props.backImage1 = backImage1Edit->text();
    props.backImage2 = backImage2Edit->text();
    props.icon = iconEdit->text();
    props.iconImage = iconImageEdit->text();
    props.autoShrink = autoShrinkCheck->isChecked();

    if (currentType == "ChooseTextButton")
    {
        bool normalColorOk = false;
        bool hoverColorOk = false;
        bool pressColorOk = false;
        const unsigned int normalColor = colorValueFromText(
            ui->normalColorEdit->text(), props.normalColor, &normalColorOk);
        const unsigned int hoverColor = colorValueFromText(
            ui->hoverColorEdit->text(), props.hoverColor, &hoverColorOk);
        const unsigned int pressColor = colorValueFromText(
            ui->pressColorEdit->text(), props.pressColor, &pressColorOk);
        if (normalColorOk)
            props.normalColor = normalColor;
        if (hoverColorOk)
            props.hoverColor = hoverColor;
        if (pressColorOk)
            props.pressColor = pressColor;
    }

    return props;
}

void ComponentPropertyEditor::setDefinitionProperties(const QString& name, const QString& file,
                                                       const QString& bind, const QString& format)
{
    updatingFromCode = true;
    ui->nameEdit->setText(name);
    ui->fileEdit->setText(file);
    ui->bindEdit->setText(bind);
    ui->formatEdit->setText(format);
    updatingFromCode = false;
}

QString ComponentPropertyEditor::getComponentName() const
{
    return ui->nameEdit->text();
}

QString ComponentPropertyEditor::getComponentFile() const
{
    return ui->fileEdit->text();
}

QString ComponentPropertyEditor::getComponentBind() const
{
    return ui->bindEdit->text();
}

QString ComponentPropertyEditor::getComponentFormat() const
{
    return ui->formatEdit->text();
}

void ComponentPropertyEditor::setControllerNavigationOverrides(
    const QString& up, const QString& down,
    const QString& left, const QString& right)
{
    updatingFromCode = true;
    ui->controllerUpEdit->setText(up);
    ui->controllerDownEdit->setText(down);
    ui->controllerLeftEdit->setText(left);
    ui->controllerRightEdit->setText(right);
    updatingFromCode = false;
    updateAdvancedPropertyVisibility();
}

QString ComponentPropertyEditor::getControllerUp() const
{
    return ui->controllerUpEdit->text().trimmed();
}

QString ComponentPropertyEditor::getControllerDown() const
{
    return ui->controllerDownEdit->text().trimmed();
}

QString ComponentPropertyEditor::getControllerLeft() const
{
    return ui->controllerLeftEdit->text().trimmed();
}

QString ComponentPropertyEditor::getControllerRight() const
{
    return ui->controllerRightEdit->text().trimmed();
}

void ComponentPropertyEditor::clearAll()
{
    const ComponentIniProperties defaults;
    setProperties(defaults);

    updatingFromCode = true;
    ui->menuWindowFileEdit->clear();
    ui->menuWindowWidthSpin->setValue(0);
    ui->menuWindowHeightSpin->setValue(0);
    ui->menuWindowImageEdit->clear();
    menuWindowBitmapEdit->clear();
    ui->menuWindowAlignCombo->setCurrentIndex(0);
    ui->menuWindowAlignXSpin->setValue(0);
    ui->menuWindowAlignYSpin->setValue(0);
    ui->menuWindowStretchCheck->setChecked(false);
    ui->nameEdit->clear();
    ui->fileEdit->clear();
    ui->bindEdit->clear();
    ui->formatEdit->clear();
    ui->controllerUpEdit->clear();
    ui->controllerDownEdit->clear();
    ui->controllerLeftEdit->clear();
    ui->controllerRightEdit->clear();
    ui->stretchCheck->setChecked(false);
    ui->subMenuWindowFileEdit->clear();
    ui->subMenuBackgroundImageEdit->clear();
    ui->subMenuRectLeftSpin->setValue(0);
    ui->subMenuRectTopSpin->setValue(0);
    ui->subMenuRectWidthSpin->setValue(0);
    ui->subMenuRectHeightSpin->setValue(0);
    currentProperties = defaults;
    hideAllTypeSpecificGroups();
    currentType.clear();
    imageAdvancedAvailable = false;
    buttonAdvancedAvailable = false;
    labelAdvancedAvailable = false;
    advancedPropertiesButton->setChecked(false);
    updateAdvancedPropertyVisibility();
    updatingFromCode = false;
}

void ComponentPropertyEditor::setSubMenuWindowFile(const QString& windowFile)
{
    updatingFromCode = true;
    ui->subMenuWindowFileEdit->setText(windowFile);
    updatingFromCode = false;
}

QString ComponentPropertyEditor::getSubMenuWindowFile() const
{
    return ui->subMenuWindowFileEdit->text();
}

void ComponentPropertyEditor::setSubMenuBackgroundImage(const QString& backgroundImage)
{
    updatingFromCode = true;
    ui->subMenuBackgroundImageEdit->setText(backgroundImage);
    updatingFromCode = false;
}

QString ComponentPropertyEditor::getSubMenuBackgroundImage() const
{
    return ui->subMenuBackgroundImageEdit->text();
}

void ComponentPropertyEditor::setSubMenuWindowRect(const QRect& rect)
{
    updatingFromCode = true;
    ui->subMenuRectLeftSpin->setValue(rect.x());
    ui->subMenuRectTopSpin->setValue(rect.y());
    ui->subMenuRectWidthSpin->setValue(rect.width());
    ui->subMenuRectHeightSpin->setValue(rect.height());
    updatingFromCode = false;
}

QRect ComponentPropertyEditor::getSubMenuWindowRect() const
{
    return QRect(ui->subMenuRectLeftSpin->value(),
                 ui->subMenuRectTopSpin->value(),
                 ui->subMenuRectWidthSpin->value(),
                 ui->subMenuRectHeightSpin->value());
}

void ComponentPropertyEditor::onAnyPropertyChanged()
{
    if (!updatingFromCode)
    {
        emit propertiesChanged();
    }
}

void ComponentPropertyEditor::showGroup(QWidget* group)
{
    if (group)
    {
        group->show();
    }
}

void ComponentPropertyEditor::hideAllTypeSpecificGroups()
{
    ui->menuWindowGroup->hide();
    ui->buttonGroup->hide();
    ui->itemGroup->hide();
    ui->labelGroup->hide();
    ui->scrollbarGroup->hide();
    ui->columnImageGroup->hide();
    ui->listBoxGroup->hide();
    ui->roundButtonGroup->hide();
    ui->joystickGroup->hide();
    ui->dragRoundButtonGroup->hide();
    ui->chooseTextButtonGroup->hide();
    ui->subMenuGroup->hide();
    ui->controllerNavigationGroup->hide();
    if (imageOptionsGroup)
    {
        imageOptionsGroup->hide();
    }
    if (buttonAdvancedGroup)
    {
        buttonAdvancedGroup->hide();
    }
    if (roundIconGroup)
    {
        roundIconGroup->hide();
    }
    if (labelAdvancedGroup)
    {
        labelAdvancedGroup->hide();
    }
}

void ComponentPropertyEditor::updateTrackVisibility()
{
    bool isTrackBtn = (ui->buttonKindCombo->currentText().toLower() == "trackbtn");
    ui->trackLabel->setVisible(isTrackBtn);
    ui->trackSpin->setVisible(isTrackBtn);
}

void ComponentPropertyEditor::setMenuWindowProperties(const QString& windowFile, int width, int height,
                                                        const QString& image, const QString& bitmap,
                                                        const QString& align,
                                                        int alignX, int alignY, bool stretch)
{
    updatingFromCode = true;
    ui->menuWindowFileEdit->setText(windowFile);
    ui->menuWindowWidthSpin->setValue(width);
    ui->menuWindowHeightSpin->setValue(height);
    ui->menuWindowImageEdit->setText(image);
    menuWindowBitmapEdit->setText(bitmap);

    int alignIndex = 0;
    for (int i = 0; i < ui->menuWindowAlignCombo->count(); i++)
    {
        if (ui->menuWindowAlignCombo->itemText(i).compare(align, Qt::CaseInsensitive) == 0)
        {
            alignIndex = i;
            break;
        }
    }
    ui->menuWindowAlignCombo->setCurrentIndex(alignIndex);
    ui->menuWindowAlignXSpin->setValue(alignX);
    ui->menuWindowAlignYSpin->setValue(alignY);
    ui->menuWindowStretchCheck->setChecked(stretch);
    updatingFromCode = false;
}

QString ComponentPropertyEditor::getMenuWindowFile() const
{
    return ui->menuWindowFileEdit->text();
}

int ComponentPropertyEditor::getMenuWindowWidth() const
{
    return ui->menuWindowWidthSpin->value();
}

int ComponentPropertyEditor::getMenuWindowHeight() const
{
    return ui->menuWindowHeightSpin->value();
}

QString ComponentPropertyEditor::getMenuWindowImage() const
{
    return ui->menuWindowImageEdit->text();
}

QString ComponentPropertyEditor::getMenuWindowBitmap() const
{
    return menuWindowBitmapEdit->text();
}

QString ComponentPropertyEditor::getMenuWindowAlign() const
{
    return ui->menuWindowAlignCombo->currentText();
}

int ComponentPropertyEditor::getMenuWindowAlignX() const
{
    return ui->menuWindowAlignXSpin->value();
}

int ComponentPropertyEditor::getMenuWindowAlignY() const
{
    return ui->menuWindowAlignYSpin->value();
}

bool ComponentPropertyEditor::getMenuWindowStretch() const
{
    return ui->menuWindowStretchCheck->isChecked();
}

void ComponentPropertyEditor::setListBoxItems(const QStringList& items)
{
    updatingFromCode = true;
    listBoxItems = items;
    ui->itemCountSpin->setValue(items.size());
    updateListBoxItemsList();
    updatingFromCode = false;
}

QStringList ComponentPropertyEditor::getListBoxItems() const
{
    return listBoxItems;
}

void ComponentPropertyEditor::onListBoxAddItem()
{
    bool ok = false;
    QString text = QInputDialog::getText(this,
        tr("添加列表项"),
        tr("项文本:"),
        QLineEdit::Normal, "", &ok);

    if (ok)
    {
        listBoxItems.append(text);
        updatingFromCode = true;
        ui->itemCountSpin->setValue(listBoxItems.size());
        updateListBoxItemsList();
        updatingFromCode = false;
        emit propertiesChanged();
    }
}

void ComponentPropertyEditor::onListBoxRemoveItem()
{
    int currentRow = ui->listBoxItemsList->currentRow();
    if (currentRow >= 0 && currentRow < listBoxItems.size())
    {
        listBoxItems.removeAt(currentRow);
        updatingFromCode = true;
        ui->itemCountSpin->setValue(listBoxItems.size());
        updateListBoxItemsList();
        updatingFromCode = false;
        emit propertiesChanged();
    }
}

void ComponentPropertyEditor::onListBoxEditItem()
{
    int currentRow = ui->listBoxItemsList->currentRow();
    if (currentRow >= 0 && currentRow < listBoxItems.size())
    {
        bool ok = false;
        QString text = QInputDialog::getText(this,
            tr("编辑列表项"),
            tr("项文本:"),
            QLineEdit::Normal, listBoxItems[currentRow], &ok);

        if (ok)
        {
            listBoxItems[currentRow] = text;
            updatingFromCode = true;
            updateListBoxItemsList();
            ui->listBoxItemsList->setCurrentRow(currentRow);
            updatingFromCode = false;
            emit propertiesChanged();
        }
    }
}

void ComponentPropertyEditor::onItemCountChanged(int count)
{
    if (updatingFromCode)
    {
        return;
    }

    while (listBoxItems.size() < count)
    {
        listBoxItems.append("");
    }
    while (listBoxItems.size() > count)
    {
        listBoxItems.removeLast();
    }

    updatingFromCode = true;
    updateListBoxItemsList();
    updatingFromCode = false;
    emit propertiesChanged();
}

void ComponentPropertyEditor::updateListBoxItemsList()
{
    ui->listBoxItemsList->clear();
    for (int i = 0; i < listBoxItems.size(); i++)
    {
        QString displayText = QString("%1: %2").arg(i + 1).arg(listBoxItems[i]);
        ui->listBoxItemsList->addItem(displayText);
    }
}

void ComponentPropertyEditor::setAssetsBasePath(const QString& path)
{
    assetsBasePath = path;
    setAssetDropRoots(path.trimmed().isEmpty()
        ? QStringList() : QStringList{path});
}

void ComponentPropertyEditor::setAssetDropRoots(const QStringList& roots)
{
    assetDropRoots.clear();
    QSet<QString> seenRoots;
    for (const QString& root : roots)
    {
        if (root.trimmed().isEmpty())
            continue;
        const QString normalized =
            EditorAssetPath::normalizedAbsolutePath(root);
        const QString key =
            EditorAssetPath::logicalComparisonKey(normalized);
        if (!seenRoots.contains(key))
        {
            seenRoots.insert(key);
            assetDropRoots.append(normalized);
        }
    }
}

void ComponentPropertyEditor::setupAssetDropTargets()
{
    const QList<QLineEdit*> imageEdits = {
        ui->menuWindowImageEdit,
        menuWindowBitmapEdit,
        ui->imageEdit,
        bitmapEdit,
        backImage1Edit,
        backImage2Edit,
        ui->joystickBaseImageEdit,
        ui->joystickThumbImageEdit,
        ui->indicateImageEdit,
        iconImageEdit,
        ui->subMenuBackgroundImageEdit
    };
    for (QLineEdit* imageEdit : imageEdits)
    {
        AssetDragDrop::installLineEditTarget(imageEdit,
            [this](const AssetDragDrop::Payload& payload)
            {
                return evaluateAssetDrop(payload, false);
            });
    }

    for (QLineEdit* soundEdit :
         {ui->buttonSoundEdit, ui->listBoxSoundEdit})
    {
        AssetDragDrop::installLineEditTarget(soundEdit,
            [this](const AssetDragDrop::Payload& payload)
            {
                return evaluateAssetDrop(payload, true);
            });
    }
}

AssetDragDrop::DropResult ComponentPropertyEditor::evaluateAssetDrop(
    const AssetDragDrop::Payload& payload, bool sound) const
{
    bool readableRoot = false;
    for (const QString& root : assetDropRoots)
    {
        if (EditorAssetPath::logicalComparisonKey(root) ==
            EditorAssetPath::logicalComparisonKey(payload.sourceRoot))
        {
            readableRoot = true;
            break;
        }
    }
    if (!readableRoot)
    {
        return {false, {},
            tr("该文件不属于当前菜单可读取的资源目录。")};
    }

    static const QSet<QString> imageSuffixes = {
        QStringLiteral("mpc"), QStringLiteral("shd"),
        QStringLiteral("asf"), QStringLiteral("imp"),
        QStringLiteral("img"), QStringLiteral("pic"),
        QStringLiteral("png"), QStringLiteral("jpg"),
        QStringLiteral("jpeg"), QStringLiteral("bmp"),
        QStringLiteral("gif"), QStringLiteral("webp"),
        QStringLiteral("tga")
    };
    static const QSet<QString> soundSuffixes = {
        QStringLiteral("wav"), QStringLiteral("mp3"),
        QStringLiteral("ogg")
    };

    const QString suffix = QFileInfo(payload.relativePath).suffix().toLower();
    if (sound)
    {
        if (!soundSuffixes.contains(suffix))
        {
            return {false, {},
                tr("该字段只接受 WAV、MP3 或 OGG 音效。")};
        }
        if (!payload.relativePath.startsWith(
                QStringLiteral("sound/"), Qt::CaseInsensitive))
        {
            return {false, {},
                tr("菜单音效必须位于资源目录的 sound 文件夹内。")};
        }
    }
    else if (!imageSuffixes.contains(suffix))
    {
        return {false, {}, tr("该字段只接受菜单图片资源。")};
    }

    QString reference = payload.relativePath;
    reference.replace('/', '\\');
    return {true, reference, {}};
}

void ComponentPropertyEditor::addColorPickerButton(QLineEdit* colorEdit)
{
    QWidget* parentWidget = colorEdit->parentWidget();
    if (!parentWidget) return;

    QFormLayout* formLayout = qobject_cast<QFormLayout*>(parentWidget->layout());
    if (!formLayout) return;

    int targetRow = -1;
    for (int i = 0; i < formLayout->rowCount(); i++)
    {
        QLayoutItem* item = formLayout->itemAt(i, QFormLayout::FieldRole);
        if (item && item->widget() == colorEdit)
        {
            targetRow = i;
            break;
        }
    }

    if (targetRow < 0) return;

    auto wrapper = new QWidget(parentWidget);
    auto hLayout = new QHBoxLayout(wrapper);
    hLayout->setContentsMargins(0, 0, 0, 0);
    hLayout->setSpacing(2);

    formLayout->removeWidget(colorEdit);
    colorEdit->setParent(wrapper);
    hLayout->addWidget(colorEdit, 1);

    auto button = new QPushButton(wrapper);
    button->setProperty("jxqyColorPicker", true);
    button->setFixedSize(24, 24);
    button->setToolTip(tr("选择颜色"));
    button->setText("");
    hLayout->addWidget(button);

    formLayout->setWidget(targetRow, QFormLayout::FieldRole, wrapper);

    updateColorButtonStyle(button, colorEdit->text());

    connect(button, &QPushButton::clicked, this, [this, colorEdit, button]()
    {
        const unsigned int currentValue = colorValueFromText(colorEdit->text(), 0xFFFFFFFF);

        QColor currentColor = QColor::fromRgba(currentValue);
        QColor newColor = QColorDialog::getColor(currentColor, this,
            tr("选择颜色"), QColorDialog::ShowAlphaChannel);
        if (newColor.isValid())
        {
            unsigned int argb = newColor.rgba();
            colorEdit->setText(colorValueText(argb));
        }
    });

    connect(colorEdit, &QLineEdit::textChanged, this, [button, this](const QString& text)
    {
        updateColorButtonStyle(button, text);
    });
}

void ComponentPropertyEditor::updateColorButtonStyle(QPushButton* button, const QString& colorText)
{
    bool ok = false;
    const unsigned int value = colorValueFromText(colorText, 0xFFFFFFFF, &ok);
    if (ok)
    {
        QColor color = QColor::fromRgba(value);
        button->setStyleSheet(QString("background-color: %1; border: 1px solid #888; border-radius: 2px;")
            .arg(color.name(QColor::HexArgb)));
    }
    else
    {
        button->setStyleSheet("background-color: #FFFFFF; border: 1px solid #888; border-radius: 2px;");
    }
}
