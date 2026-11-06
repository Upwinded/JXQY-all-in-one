#include "../../src/Game/Menu/DiceGameLayout.h"
#include "ComponentPropertyEditor.h"
#include "ui_ComponentPropertyEditor.h"
#include "FilePickerHelper.h"
#include "../core/EditorAssetPath.h"
#include "../core/INIFileEditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QColorDialog>
#include <QEvent>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <cstdlib>

namespace
{
QWidget* runtimeFormField(QWidget* edit, QWidget* group)
{
    // File pickers wrap the edit; QFormLayout visibility belongs to that wrapper.
    return edit->parentWidget() == group ? edit : edit->parentWidget();
}

const QStringList menuWindowAlignValues = {
    "alNone", "alClient", "alLeft", "alRight", "alTop", "alBottom",
    "alLTCorner", "alRTCorner", "alLBCorner", "alRBCorner", "alCenter",
    "alLeftCenter", "alRightCenter", "alTopCenter", "alBottomCenter"
};

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

QList<QPair<QString, QString>> ComponentPropertyEditor::runtimeLayoutLabels() const
{
    return {
        {"autonextline", tr("自动换行")}, {"minimumfont", tr("最小字体大小（像素）")},
        {"centertext", tr("文字居中")}, {"drawslot", tr("绘制格框")},
        {"fitlines", tr("按文本框高度分配行距")}, {"linesize", tr("正文行距")},
        {"fititems", tr("按列表高度分配行高")}, {"linegap", tr("制作名单行间距")},
        {"linecount", tr("对话行数（0自动）")}, {"charactersperline", tr("每行字符数（0自动）")}, {"lineheight", tr("对话行高（像素，0为自动）")},
        {"scale", tr("窗口缩放（百分比）")}, {"nineslice", tr("原图边框宽度（像素，0关闭）")}, {"nineslicewidth", tr("显示边框宽度（像素）")},
        {"keepaspect", tr("窗口保持宽高比")},
        {"backgroundcolor", tr("背景色（含透明度）")}, {"hoverbackgroundcolor", tr("悬停背景色")},
        {"pressedbackgroundcolor", tr("按下背景色")},
        {"bordercolor", tr("边框颜色")},
        {"left", tr("水平位置（像素）")}, {"top", tr("垂直位置（像素）")},
        {"horizontalpadding", tr("换行宽度两侧扣除")},
        {"topbordercolor", tr("顶部边线颜色")}, {"bottombordercolor", tr("底部边线颜色")},
        {"horizontal", tr("状态条方向")}, {"fillcolor", tr("水平填充色（alpha固定255）")},
        {"fittowindow", tr("小窗口自动缩小")}, {"windowmargin", tr("窗口水平预留")}, {"windowverticalmargin", tr("窗口垂直预留")},
        {"image",tr("帮助示意图（空白沿用默认）")}, {"outermargin",tr("外边距（空白动态默认）")},
        {"contentpadding",tr("内容水平内缩")}, {"titlefontsize",tr("帮助标题字号")}, {"textfontsize",tr("帮助正文字号")},
        {"linespacing",tr("帮助正文附加行距")}, {"columngap",tr("帮助两列间距")},
        {"panelcolor",tr("帮助内框颜色")}, {"accentcolor",tr("帮助顶部装饰线颜色")}, {"titlecolor",tr("帮助标题与页脚颜色")},
        {"sectioncolor",tr("帮助段标题颜色")}, {"textcolor",tr("帮助正文颜色")}, {"mutedtextcolor",tr("帮助副标题颜色")},
        {"indicate", tr("技能方向提示图片")}, {"flat", tr("使用纯色外观")}, {"fittrack", tr("滑块自动适合轨道")}
    };
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
    listBoxFontSpin = new QSpinBox(ui->listBoxGroup);
    listBoxFontSpin->setObjectName("listBoxFontSpin");
    listBoxFontSpin->setRange(1, 256);
    listBoxFontSpin->setValue(20);
    ui->listBoxFormLayout->addRow(tr("字体大小（像素）"), listBoxFontSpin);
    connect(listBoxFontSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
    ui->mainLayout->insertWidget(ui->mainLayout->indexOf(ui->labelGroup) + 1, labelAdvancedGroup);

    runtimeLayoutGroup = new QGroupBox(tr("文字与显示布局"), ui->scrollContent);
    runtimeLayoutGroup->setObjectName("runtimeLayoutGroup");
    auto* runtimeLayout = new QFormLayout(runtimeLayoutGroup);
    // These are existing runtime INI fields, not a second property format.
    const auto fields = runtimeLayoutLabels();
    for (const auto& field : fields)
    {
        const QSet<QString> booleans = {"autonextline", "centertext", "drawslot", "fitlines", "fititems", "keepaspect", "horizontal", "fittowindow", "flat", "fittrack"};
        const auto type = booleans.contains(field.first) ? DisplayPropertyEditor::ValueType::Boolean
            : field.first.contains("color") ? DisplayPropertyEditor::ValueType::Color
            : field.first == "scale" ? DisplayPropertyEditor::ValueType::Percentage
            : (field.first == "image" || field.first == "indicate") ? DisplayPropertyEditor::ValueType::Text
            : DisplayPropertyEditor::ValueType::Integer;
        auto* edit = new DisplayPropertyEditor(type, runtimeLayoutGroup);
        edit->setObjectName("runtime_" + field.first);
        if (edit->textInput()) edit->textInput()->setObjectName(edit->objectName());
        if (type == DisplayPropertyEditor::ValueType::Integer && field.first != "left" && field.first != "top")
            edit->setIntegerRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
        if (QStringList{"minimumfont", "titlefontsize", "textfontsize"}.contains(field.first)) edit->setIntegerRange(1, 256);
        if (field.first == "minimumfont") edit->setDefaultValue("7");
        if (field.first == "horizontal") edit->setBooleanLabels(tr("水平"), tr("垂直"));
        if (field.first == "scale") edit->setDefaultValue("1");
        if (field.first.startsWith("nineslice")) edit->setIntegerRange(0, std::numeric_limits<int>::max());
        if (field.first == "nineslice") edit->setDefaultValue("0");
        if (booleans.contains(field.first)) edit->setDefaultValue("false");
        if (field.first == "windowmargin") edit->setDefaultValue("24");
        if (field.first == "windowverticalmargin") edit->setDefaultValue("128");
        if (field.first == "linecount" || field.first == "charactersperline" || field.first == "lineheight") edit->setDefaultValue("0");
        edit->setPlaceholderText(tr("使用默认时不写入此属性；点击右侧恢复按钮可撤销该属性的自定义值"));
        runtimeLayoutEdits.insert(field.first, edit);
        runtimeLayout->addRow(field.second, edit);
        if (field.first == "nineslice") edit->setToolTip(tr("从原图四边各取多少像素作为边框。0表示普通拉伸；例如填8，会把原图四边各8像素作为边框。"));
        if (field.first == "nineslicewidth") edit->setToolTip(tr("边框在界面中画多宽。留空时与原图边框同宽；例如原图边框8、显示边框12，会将四边各8像素的图案画成12像素宽。"));
        connect(edit, &DisplayPropertyEditor::textChanged, this, &ComponentPropertyEditor::onAnyPropertyChanged);
    }
    imageBorderScalingHelp = new QLabel(tr("边框拉伸：保留图片四角，四边和中间随尺寸拉伸。原图边框为0时关闭；显示宽度留空时沿用原图值。仅在图片拉伸时生效。"), runtimeLayoutGroup);
    imageBorderScalingHelp->setObjectName("imageBorderScalingHelp");
    imageBorderScalingHelp->setWordWrap(true);
    runtimeLayout->addRow(imageBorderScalingHelp);
    imageBorderScalingHelp->hide();
    ui->mainLayout->insertWidget(ui->mainLayout->count() - 1, runtimeLayoutGroup);
    runtimeLayoutGroup->hide();
    diceLayoutGroup = new QGroupBox(tr("骰子显示属性"), ui->scrollContent);
    diceLayoutGroup->setObjectName("diceLayoutGroup");
    auto* diceForm = new QFormLayout(diceLayoutGroup);
    const QList<QPair<QString,QString>> diceLabels = {{"Image",tr("图片")},{"Left",tr("水平位置／标题偏移")},
        {"Top",tr("垂直位置")},{"Width",tr("宽度")},{"Height",tr("高度")},{"Font",tr("字体大小（像素）")},
        {"Color",tr("颜色（含透明度）")},{"LineGap",tr("行间距（像素）")}};
    for (const auto& field : diceLabels)
    {
        const auto type = field.first == "Image" ? DisplayPropertyEditor::ValueType::Text
            : field.first == "Color" ? DisplayPropertyEditor::ValueType::Color : DisplayPropertyEditor::ValueType::Integer;
        auto* edit = new DisplayPropertyEditor(type, diceLayoutGroup);
        edit->setObjectName("dice_" + field.first);
        if (edit->textInput()) edit->textInput()->setObjectName(edit->objectName());
        diceLayoutEdits[field.first] = edit;
        diceForm->addRow(field.second, edit);
        connect(edit, &DisplayPropertyEditor::editingFinished, this, &ComponentPropertyEditor::onAnyPropertyChanged);
    }
    ui->mainLayout->insertWidget(ui->mainLayout->count() - 1, diceLayoutGroup);
    diceLayoutGroup->hide();


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
    tagFormLabel(ui->listBoxFormLayout, listBoxFontSpin);
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
        ui->menuWindowAlignCombo->setItemData(i, menuWindowAlignValues.at(i));
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
    connect(ui->menuWindowAlignCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]()
    {
        if (!updatingFromCode)
        {
            menuWindowAlign = ui->menuWindowAlignCombo->currentData().toString();
            onAnyPropertyChanged();
        }
    });
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

    connect(ui->scrollbarDirectionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ComponentPropertyEditor::onAnyPropertyChanged);
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
    FilePickerHelper::addFilePickerButton(diceLayoutEdits.value("Image")->textInput(), imageFileFilter, getAssetsPath, true, this);
    FilePickerHelper::addFilePickerButton(runtimeLayoutEdits.value("image")->textInput(), imageFileFilter, getAssetsPath, true, this);
    FilePickerHelper::addFilePickerButton(runtimeLayoutEdits.value("indicate")->textInput(), imageFileFilter, getAssetsPath, true, this);
    FilePickerHelper::addFilePickerButton(ui->buttonSoundEdit, tr("Sound Files (*.wav *.mp3 *.ogg);;All Files (*)"), getAssetsPath, true, this);
    FilePickerHelper::addFilePickerButton(ui->listBoxSoundEdit, ui->listBoxFormLayout,
        tr("Sound Files (*.wav *.mp3 *.ogg);;All Files (*)"), getAssetsPath, true, this);

    for (auto* spin : findChildren<QAbstractSpinBox*>()) spin->setKeyboardTracking(false);
    statePreviewGroup = new QGroupBox(tr("按钮外观"), ui->scrollContent);
    statePreviewGroup->setObjectName("buttonStatePreviewGroup");
    auto* stateLayout = new QGridLayout(statePreviewGroup);
    const QStringList stateNames = {tr("正常／抬起"), tr("鼠标悬停"), tr("按下")};
    for (int index = 0; index < 3; ++index)
    {
        auto* title = new QLabel(stateNames[index], statePreviewGroup);
        title->setAlignment(Qt::AlignCenter);
        auto* image = new QLabel(statePreviewGroup);
        image->setObjectName("buttonStateImage" + QString::number(index));
        image->setMinimumSize(80, 42);
        image->setAlignment(Qt::AlignCenter);
        image->setScaledContents(false);
        stateLayout->addWidget(title, index * 2, 0);
        stateLayout->addWidget(image, index * 2 + 1, 0);
        statePreviewLabels.append(image);
    }
    ui->mainLayout->insertWidget(ui->mainLayout->count() - 1, statePreviewGroup);
    statePreviewGroup->hide();
    choiceSpacingSpin = new QSpinBox(ui->scrollContent);
    choiceSpacingSpin->setObjectName("choiceListSpacing");
    choiceSpacingSpin->setRange(0, 4096);
    choiceSpacingSpin->setSuffix(tr(" 像素"));
    choiceSpacingSpin->setKeyboardTracking(false);
    ui->labelFormLayout->addRow(tr("选项之间的间距"), choiceSpacingSpin);
    ui->labelFormLayout->setRowVisible(choiceSpacingSpin, false);
    connect(choiceSpacingSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int value)
    {
        if (!updatingFromCode) emit choiceListSpacingChanged(value);
    });
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

    for (auto* form : findChildren<QFormLayout*>())
    {
        form->setVerticalSpacing(8);
        form->setHorizontalSpacing(12);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    }
    ui->mainLayout->setSpacing(12);
    ui->slideBeginSpin->setToolTip(tr("沿滚动方向，从轨道左端或上端起算。选中滚动条后，橙色虚线框标出起点和终点。"));
    ui->slideEndSpin->setToolTip(ui->slideBeginSpin->toolTip());
    for (auto* frame : {ui->upSpin, ui->downSpin, ui->trackSpin}) frame->setToolTip(tr("图片帧号从0开始。三个状态的实际外观显示在下方，缺帧时按游戏规则回退。"));
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
        const QSignalBlocker blocker(ui->menuWindowAlignCombo);
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
    setFormLabel(ui->listBoxFormLayout, listBoxFontSpin, tr("字体大小（像素）"));

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

    runtimeLayoutGroup->setTitle(tr("文字与显示布局"));
    for (const auto& field : runtimeLayoutLabels())
    {
        auto* edit = runtimeLayoutEdits.value(field.first);
        setFormLabel(qobject_cast<QFormLayout*>(runtimeLayoutGroup->layout()), runtimeFormField(edit,runtimeLayoutGroup), field.second);
        edit->setPlaceholderText(tr("使用默认时不写入此属性；点击右侧恢复按钮可撤销该属性的自定义值"));
    }

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
    if (ui->menuWindowAlignCombo->count() > alignmentNames.size())
        ui->menuWindowAlignCombo->setItemText(alignmentNames.size(), tr("保留原值：%1")
            .arg(ui->menuWindowAlignCombo->itemData(alignmentNames.size()).toString()));

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

    const bool buttonType = type == "Button" || type == "DragButton" || type == "CheckBox" ||
        type == "TextButton" || type == "FlatTextButton" || type == "ChooseTextButton" ||
        type == "RoundButton" || type == "DragRoundButton";
    const bool roundButtonType = type == "RoundButton" || type == "DragRoundButton";
    const bool itemType = type == "Item" || type == "Label" || type == "TalkLabel";
    const bool imageContainerType = type == "ImageContainer" || type == "TransImage";

    if (type == "MenuWindow")
    {
        if(!controllerHelpDisplayMode)showGroup(ui->menuWindowGroup);
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
            if (type == "CheckBox") showGroup(ui->labelGroup);
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
        else if (type == "TextButton" || type == "FlatTextButton")
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

    const bool textType = type == "Label" || type == "TalkLabel" || type == "TextButton" || type == "FlatTextButton" || type == "CheckBox" || roundButtonType;
    auto* textLayout = roundButtonType ? ui->roundButtonFormLayout : ui->labelFormLayout;
    auto* previousLayout = qobject_cast<QFormLayout*>(ui->textEdit->parentWidget()->layout());
    if (previousLayout != textLayout)
    {
        const auto row = previousLayout->takeRow(ui->textEdit);
        delete row.labelItem;
        delete row.fieldItem;
        textLayout->insertRow(roundButtonType ? 1 : 0, ui->textLabel, ui->textEdit);
    }
    textLayout->setRowVisible(ui->textEdit, textType);
    auto* runtimeLayout = qobject_cast<QFormLayout*>(runtimeLayoutGroup->layout());
    bool anyRuntimeField = false;
    for (auto it = runtimeLayoutEdits.begin(); it != runtimeLayoutEdits.end(); ++it)
    {
        const QString& key = it.key();
        const bool visible = ((key == "autonextline" || key == "minimumfont" || key == "centertext") && (type == "Label" || type == "TalkLabel"))
            || (key == "drawslot" && type == "Item")
            || ((key == "fitlines" || key == "linesize") && type == "MemoText")
            || (key == "fititems" && type == "ListBox")
            || ((key == "scale" || key == "keepaspect") && type == "MenuWindow")
            || (key.startsWith("nineslice") && (type == "MenuWindow" || type == "ImageContainer"))
            || (key.endsWith("backgroundcolor") && type == "FlatTextButton")
            || (key == "backgroundcolor" && type == "MenuWindow") || (key == "linegap" && type == "Label")
            || (type == "ColumnImage" && (key == "horizontal" || key == "fillcolor"))
            || (noticeDisplayMode && ((type == "MenuWindow" && (key == "left" || key == "top" || key.endsWith("bordercolor")))
                || (type == "Label" && key == "horizontalpadding")));
        const bool talkField = type == "TalkLabel" && (key == "linecount" || key == "charactersperline" || key == "lineheight");
        runtimeLayout->setRowVisible(runtimeFormField(it.value(),runtimeLayoutGroup), visible || talkField);
        anyRuntimeField |= visible || talkField;
    }
    runtimeLayoutGroup->setVisible(anyRuntimeField);
    imageBorderScalingHelp->setVisible(type == "MenuWindow" || type == "ImageContainer");

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

    ui->definitionFormLayout->setRowVisible(ui->bindEdit, advanced && ui->bindEdit->isEnabled());
    ui->definitionFormLayout->setRowVisible(ui->formatEdit, advanced && ui->formatEdit->isEnabled());
    ui->commonFormLayout->setRowVisible(iniNameEdit, advanced && iniNameEdit->isEnabled());
    ui->menuWindowFormLayout->setRowVisible(
        menuWindowBitmapEdit,
        advanced);
    ui->controllerNavigationGroup->setVisible(
        advanced && (ui->controllerUpEdit->isEnabled() || ui->controllerDownEdit->isEnabled()
            || ui->controllerLeftEdit->isEnabled() || ui->controllerRightEdit->isEnabled()));
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
    runtimeLayoutEdits.value("nineslicewidth")->setDefaultValue(props.rawValues.value("nineslice", "0"));
    const bool flatText = currentType == "FlatTextButton";
    const bool flatControl = flatText || currentType == "Button" || currentType == "DragButton" || currentType == "Scrollbar";
    const QString defaultBackground = !flatControl ? QString() : currentType == "Scrollbar" ? "0xFFB7B697" : flatText ? "0xE6201914" : "0xFF2C5446";
    runtimeLayoutEdits.value("backgroundcolor")->setDefaultValue(defaultBackground);
    runtimeLayoutEdits.value("hoverbackgroundcolor")->setDefaultValue(!flatControl ? QString() : flatText ? "0xE6362219" : props.rawValues.value("backgroundcolor", defaultBackground));
    runtimeLayoutEdits.value("pressedbackgroundcolor")->setDefaultValue(!flatControl ? QString() : flatText ? "0xE64E2C20" : props.rawValues.value("backgroundcolor", defaultBackground));
    runtimeLayoutEdits.value("bordercolor")->setDefaultValue(!flatControl ? QString() : flatText ? "0x91B09258" : "0xFFAB925D");
    for (auto it = runtimeLayoutEdits.begin(); it != runtimeLayoutEdits.end(); ++it)
        it.value()->setText(props.rawValues.value(it.key()));

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
    listBoxFontSpin->setValue(props.fontSet ? props.font : 20);
    ui->labelFontSpin->setValue(props.fontSet ? props.font : (currentType == "MemoText" ? 16 : 18));
    ui->scrollbarDirectionCombo->setCurrentIndex(props.style);
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
            (currentType == "TextButton" || currentType == "FlatTextButton" || currentType == "ChooseTextButton" ||
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
        props.font = listBoxFontSpin->value();
        parsedColor = colorValueFromText(ui->listBoxColorEdit->text(), 0xFFFFFFFF, &colorOk);
        bool selectedColorOk = false;
        const unsigned int selectedColor = colorValueFromText(
            ui->listBoxSelColorEdit->text(), props.selColor, &selectedColorOk);
        if (selectedColorOk)
            props.selColor = selectedColor;
    }
    else if (currentType == "Label" || currentType == "TalkLabel" || currentType == "MemoText" ||
             currentType == "TextButton" || currentType == "FlatTextButton" || currentType == "ChooseTextButton" || currentType == "CheckBox")
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
             currentType == "TextButton" || currentType == "FlatTextButton" || currentType == "ChooseTextButton" || currentType == "CheckBox")
    {
        props.font = ui->labelFontSpin->value();
    }
    // 仅对运行时读取 font 的类型标记 fontSet，避免隐藏控件状态进入数据快照。
    // Item/Label/TalkLabel/MemoText/TextButton/ChooseTextButton 通过 typeSpecificKeys 声明 font。
    if (currentType == "Item" || currentType == "Label" || currentType == "TalkLabel" ||
        currentType == "ListBox" || currentType == "MemoText" || currentType == "TextButton" || currentType == "FlatTextButton" || currentType == "ChooseTextButton" || currentType == "CheckBox")
    {
        props.fontSet = true;
    }

    if (currentType == "Scrollbar")
    {
        props.style = ui->scrollbarDirectionCombo->currentIndex();
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
        currentType == "TextButton" || currentType == "FlatTextButton" || currentType == "RoundButton" ||
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

    for (auto it = runtimeLayoutEdits.constBegin(); it != runtimeLayoutEdits.constEnd(); ++it)
    {
        auto* layout = qobject_cast<QFormLayout*>(runtimeLayoutGroup->layout());
        if (!layout->isRowVisible(runtimeFormField(it.value(),runtimeLayoutGroup))) continue;
        const QString value = it.value()->text().trimmed();
        if (value.isEmpty()) props.rawValues.remove(it.key());
        else props.rawValues[it.key()] = value;
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
    diceLayoutGroup->hide();
    runtimeLayoutGroup->hide();
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


void ComponentPropertyEditor::setTitleTeamDisplayLimits(bool titleTeam)
{
    const QSignalBlocker blocker(ui->labelFontSpin);
    ui->labelFontSpin->setMaximum(titleTeam ? 256 : 99);
    const int limit = titleTeam ? 100000 : 9999;
    for (auto* spin : {ui->leftSpin, ui->topSpin, ui->menuWindowAlignXSpin, ui->menuWindowAlignYSpin})
    {
        const QSignalBlocker geometryBlocker(spin);
        spin->setRange(-limit, limit);
    }
    for (auto* spin : {ui->widthSpin, ui->heightSpin, ui->menuWindowWidthSpin, ui->menuWindowHeightSpin})
    {
        const QSignalBlocker geometryBlocker(spin);
        spin->setMaximum(limit);
    }
}

void ComponentPropertyEditor::setMenuEditableFields(const QSet<QString>& fields, const QString& reason)
{
    auto* runtimeLayout = qobject_cast<QFormLayout*>(runtimeLayoutGroup->layout());
    bool anyRuntimeField = false;
    for (auto it = runtimeLayoutEdits.begin(); it != runtimeLayoutEdits.end(); ++it)
    {
        const bool available = fields.contains(it.key())
            && (!(it.key() == "left" || it.key() == "top" || it.key() == "keepaspect") || currentType == "MenuWindow")
            && (!it.key().startsWith("nineslice") || currentType == "MenuWindow" || currentType == "ImageContainer")
            && (it.key() != "image" || controllerHelpDisplayMode);
        runtimeLayout->setRowVisible(runtimeFormField(it.value(), runtimeLayoutGroup), available);
        anyRuntimeField |= available;
    }
    runtimeLayoutGroup->setVisible(anyRuntimeField);
    imageBorderScalingHelp->setVisible((currentType == "MenuWindow" || currentType == "ImageContainer") && fields.contains("nineslice"));
    const QMap<QString, QString> keys = {
        {"leftSpin", "left"}, {"topSpin", "top"}, {"widthSpin", "width"}, {"heightSpin", "height"},
        {"menuWindowWidthSpin", "width"}, {"menuWindowHeightSpin", "height"},
        {"menuWindowAlignCombo", "align"}, {"menuWindowAlignXSpin", "alignx"}, {"menuWindowAlignYSpin", "aligny"},
        {"imageEdit", "image"}, {"menuWindowImageEdit", "image"},
        {"rangeSpin", "range"}, {"joystickBaseImageEdit", "baseimage"}, {"joystickThumbImageEdit", "thumbimage"},
        {"iconEdit", "icon"}, {"iconImageEdit", "iconimage"}, {"buttonKindCombo", "kind"}, {"animateCheck", "animate"},
        {"bitmapEdit", "bitmap"}, {"menuWindowBitmapEdit", "bitmap"},
        {"stretchCheck", "stretch"}, {"menuWindowStretchCheck", "stretch"},
        {"labelFontSpin", "font"}, {"itemFontSpin", "font"},
        {"listBoxFontSpin", "font"}, {"listBoxColorEdit", "color"}, {"listBoxSelColorEdit", "selcolor"},
        {"itemHeightSpin", "itemheight"}, {"listBoxEditItemButton", "items"},
        {"labelColorEdit", "color"}, {"itemColorEdit", "color"}, {"colorEdit", "color"},
        {"normalColorEdit", "normalcolor"}, {"hoverColorEdit", "hovercolor"}, {"pressColorEdit", "presscolor"},
        {"cropContentCheck", "cropcontent"}, {"cropBlackCheck", "cropblack"},
        {"backImage1Edit", "backimage1"}, {"backImage2Edit", "backimage2"},
        {"keepAspectCheck", "keepaspect"}, {"centerImageCheck", "centerimage"}, {"frameSpin", "frame"},
        {"autoShrinkCheck", "autoshrink"}, {"textEdit", "text"}, {"formatEdit", "format"},
        {"upSpin", "up"}, {"downSpin", "down"}, {"trackSpin", "track"},
        {"scrollbarDirectionCombo", "style"}, {"slideBeginSpin", "slidebegin"}, {"slideEndSpin", "slideend"}};
    for (QWidget* widget : findChildren<QWidget*>())
    {
        if (auto* field = qobject_cast<DisplayPropertyEditor*>(widget))
        {
            if (field->objectName().startsWith("runtime_"))
                field->setEnabled(fields.contains(field->objectName().mid(8))
                    && (!field->objectName().mid(8).startsWith("nineslice") || currentType == "MenuWindow" || currentType == "ImageContainer"));
            continue;
        }
        bool insideDisplayField = false;
        for (QWidget* ancestor = widget->parentWidget(); ancestor && ancestor != this; ancestor = ancestor->parentWidget())
            if (qobject_cast<DisplayPropertyEditor*>(ancestor)) { insideDisplayField = true; break; }
        if (insideDisplayField) continue;
        if (!(qobject_cast<QLineEdit*>(widget) || qobject_cast<QAbstractSpinBox*>(widget)
            || qobject_cast<QComboBox*>(widget) || qobject_cast<QAbstractButton*>(widget))) continue;
        if (widget->objectName() == "qt_spinbox_lineedit" || widget == choiceSpacingSpin) continue;
        QString name = widget->objectName();
        QString key = name.startsWith("runtime_") ? name.mid(8) : keys.value(name);
        if (name.endsWith("PickerButton")) key = keys.value(name.chopped(12));
        if (name.startsWith("runtime_") && name.endsWith("PickerButton")) key = name.mid(8).chopped(12);
        if (widget->property("jxqyColorPicker").toBool())
            if (auto* edit = widget->parentWidget()->findChild<QLineEdit*>()) key = keys.value(edit->objectName());
        const bool enabled = name == "advancedPropertiesButton" || (!key.isEmpty() && fields.contains(key));
        widget->setEnabled(enabled);
        if (!enabled) widget->setToolTip(reason);
        widget->setProperty("menuReadOnly", !enabled);
    }
    ui->definitionFormLayout->setRowVisible(ui->nameEdit, ui->nameEdit->isEnabled());
    ui->definitionFormLayout->setRowVisible(ui->fileEdit, ui->fileEdit->isEnabled());
    ui->definitionGroup->setVisible(ui->nameEdit->isEnabled() || ui->fileEdit->isEnabled() || ui->formatEdit->isEnabled());
    advancedPropertiesButton->setVisible(!(fields & QSet<QString>{"format", "bitmap", "frame", "cropcontent", "cropblack", "centerimage", "backimage1", "backimage2", "keepaspect"}).isEmpty());
    for (auto* form : findChildren<QFormLayout*>())
    {
        if (form == runtimeLayout || form == ui->definitionFormLayout) continue;
        for (int row = 0; row < form->rowCount(); ++row)
        {
            auto* item = form->itemAt(row, QFormLayout::FieldRole);
            auto* field = item ? item->widget() : nullptr;
            if (!field || field == choiceSpacingSpin || qobject_cast<DisplayPropertyEditor*>(field)) continue;
            const auto controls = field->findChildren<QWidget*>();
            const auto editable = [](QWidget* widget)
            {
                return widget->isEnabled() && (qobject_cast<QLineEdit*>(widget) || qobject_cast<QAbstractSpinBox*>(widget)
                    || qobject_cast<QComboBox*>(widget) || qobject_cast<QAbstractButton*>(widget));
            };
            const bool available = editable(field) || std::any_of(controls.begin(), controls.end(), editable);
            form->setRowVisible(field, available);
        }
    }
    updateAdvancedPropertyVisibility();
    for (auto* group : findChildren<QGroupBox*>())
    {
        auto* form = qobject_cast<QFormLayout*>(group->layout());
        if (!form || group->isHidden()) continue;
        bool hasRow = false;
        for (int row = 0; row < form->rowCount(); ++row) hasRow |= form->isRowVisible(row);
        if (!hasRow) group->hide();
    }
}

void ComponentPropertyEditor::onAnyPropertyChanged()
{
    auto* source = qobject_cast<QWidget*>(sender());
    if (!updatingFromCode && (!source || source->isEnabled()))
    {
        emit propertiesChanged();
    }
}

void ComponentPropertyEditor::setButtonStatePreviews(const QList<QPixmap>& images)
{
    statePreviewGroup->setVisible(images.size() == 3);
    for (int index = 0; index < images.size() && index < statePreviewLabels.size(); ++index)
        statePreviewLabels[index]->setPixmap(images[index]);
}

void ComponentPropertyEditor::setChoiceListSpacing(int gap, bool visible)
{
    const QSignalBlocker blocker(choiceSpacingSpin);
    choiceSpacingSpin->setValue(gap);
    ui->labelFormLayout->setRowVisible(choiceSpacingSpin, visible);
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

    menuWindowAlign = align;
    while (ui->menuWindowAlignCombo->count() > menuWindowAlignValues.size())
        ui->menuWindowAlignCombo->removeItem(menuWindowAlignValues.size());
    int alignIndex = align.isEmpty() ? 0 : -1;
    for (int i = 0; i < ui->menuWindowAlignCombo->count(); i++)
    {
        if (ui->menuWindowAlignCombo->itemData(i).toString().compare(align, Qt::CaseInsensitive) == 0)
        {
            alignIndex = i;
            break;
        }
    }
    if (alignIndex < 0)
    {
        const QByteArray text = align.toUtf8();
        char* end = nullptr;
        const long value = std::strtol(text.constData(), &end, 0);
        if (end != text.constData() && end == text.constData() + text.size() &&
            value >= 0 && value < menuWindowAlignValues.size())
            alignIndex = static_cast<int>(value);
    }
    if (alignIndex < 0)
    {
        alignIndex = ui->menuWindowAlignCombo->count();
        ui->menuWindowAlignCombo->addItem(tr("保留原值：%1").arg(align), align);
    }
    ui->menuWindowAlignCombo->setCurrentIndex(alignIndex);
    ui->menuWindowAlignXSpin->setValue(alignX);
    ui->menuWindowAlignYSpin->setValue(alignY);
    ui->menuWindowStretchCheck->setChecked(stretch);
    updatingFromCode = false;
}

void ComponentPropertyEditor::setControllerHelpDisplayMode(bool help)
{
    controllerHelpDisplayMode=help;
}

void ComponentPropertyEditor::setNoticeDisplayMode(bool notice)
{
    noticeDisplayMode=notice;
    auto* form=qobject_cast<QFormLayout*>(runtimeLayoutGroup->layout());
    if(auto* label=qobject_cast<QLabel*>(form->labelForField(runtimeLayoutEdits.value("linegap"))))
        label->setText(notice?tr("行间距（像素）"):tr("制作名单行间距"));
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
    return menuWindowAlign;
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
    if (ui->listBoxAddItemButton->property("menuReadOnly").toBool()) return;
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
    if (ui->listBoxRemoveItemButton->property("menuReadOnly").toBool()) return;
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
    if (ui->listBoxEditItemButton->property("menuReadOnly").toBool()) return;
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
            if (text != listBoxItems[currentRow] && !INIFileEditor::canRoundTripValue(text.toStdString()))
            {
                QMessageBox::warning(this, tr("无法修改列表项"),
                    tr("该文本无法按现有 INI 格式完整保存，请检查前后空白、换行及分号注释。"));
                return;
            }
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
    if (updatingFromCode || ui->itemCountSpin->property("menuReadOnly").toBool())
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
        runtimeLayoutEdits.value("image")->textInput(),
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
    button->setMinimumSize(58, 28);
    button->setMaximumWidth(68);
    button->setToolTip(tr("选择颜色"));
    button->setText(tr("选色…"));
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

void ComponentPropertyEditor::setDiceDisplayProperties(const QString& roleName, const QMap<QString,QString>& values, bool editable)
{
    diceLayoutGroup->setVisible(!roleName.isEmpty());
    advancedPropertiesButton->setVisible(roleName.isEmpty());
    if (roleName.isEmpty()) return;
    updatingFromCode = true;
    hideAllTypeSpecificGroups();
    ui->definitionGroup->hide(); ui->commonGroup->hide(); runtimeLayoutGroup->hide();
    const auto& role = DiceGameLayout::role(roleName.toStdString());
    diceLayoutGroup->setTitle(QCoreApplication::translate("MenuEditorWindow", role.label));
    auto* form = qobject_cast<QFormLayout*>(diceLayoutGroup->layout());
    const auto fields = DiceGameLayout::fields(role);
    if (auto* picker = findChild<QPushButton*>("dice_ImagePickerButton")) picker->setEnabled(editable);
    for (auto it = diceLayoutEdits.begin(); it != diceLayoutEdits.end(); ++it)
    {
        const auto key = it.key().toStdString();
        form->setRowVisible(runtimeFormField(it.value(),diceLayoutGroup), std::find(fields.begin(), fields.end(), key) != fields.end());
        it.value()->setText(values.value(it.key()));
        it.value()->setEnabled(editable);
        const auto r = role.rectangle;
        QString fallback = it.key() == "Left" ? QString::number(r.x) : it.key() == "Top" ? QString::number(r.y)
            : it.key() == "Width" ? QString::number(r.w) : it.key() == "Height" ? QString::number(r.h)
            : it.key() == "Font" ? QString::number(role.font) : it.key() == "LineGap" ? "3"
            : it.key() == "Color" ? "0x" + QString::number(role.color,16) : QString();
        it.value()->setPlaceholderText(fallback.isEmpty() ? tr("运行时必需图片；失效时使用回退界面") : tr("留空使用默认：%1").arg(fallback));
        it.value()->setDefaultValue(fallback);
    }
    updatingFromCode = false;
}

void ComponentPropertyEditor::setDisplayDefaults(const QMap<QString, QString>& defaults)
{
    for (auto it = defaults.cbegin(); it != defaults.cend(); ++it)
    {
        if (auto* field = runtimeLayoutEdits.value(it.key())) field->setDefaultValue(it.value());
    }
}

QMap<QString,QString> ComponentPropertyEditor::diceDisplayProperties() const
{
    QMap<QString,QString> result;
    for (auto it = diceLayoutEdits.cbegin(); it != diceLayoutEdits.cend(); ++it) result[it.key()] = it.value()->text();
    return result;
}
