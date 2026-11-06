#include "../../src/Game/Menu/OptionLayout.h"
#include <QCheckBox>
#include "MenuEditorWindow.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <cmath>
#include <algorithm>
#include <limits>
#include "../core/AuthoringMutationGate.h"
#include "ui_MenuEditorWindow.h"
#include "../core/ProjectManager.h"
#include "../core/EditorAssetPath.h"
#include "../core/DurableFileTransaction.h"
#include "../core/GameProfile.h"
#include "../../src/Resource/ResourceCatalog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QStringConverter>
#include <QSettings>
#include <QDir>
#include <QDirIterator>
#include <QScopeGuard>
#include <QEvent>
#include <QHeaderView>
#include <QDebug>
#include <QSet>
#include <QUuid>
#include <QComboBox>
#include <QCompleter>
#include <QDataStream>
#include <QKeySequence>
#include <QCryptographicHash>

namespace
{
QString normalizedResourcePath(const QString& path)
{
    QString normalized;
    if (!EditorAssetPath::normalizeResourcePath(path, normalized))
        return {};
    return normalized;
}

unsigned int parseEditorColor(const QString& text, unsigned int defaultValue)
{
    const QString trimmed = text.trimmed();
    const QStringList parts = trimmed.split(',', Qt::KeepEmptyParts);
    if (parts.size() == 3 || parts.size() == 4)
    {
        unsigned int values[4] = {0, 0, 0, 255};
        for (int i = 0; i < parts.size(); ++i)
        {
            bool ok = false;
            const uint value = parts[i].trimmed().toUInt(&ok, 10);
            if (!ok || value > 255)
                return defaultValue;
            values[i] = value;
        }
        return (values[3] << 24) | (values[0] << 16) | (values[1] << 8) | values[2];
    }

    bool ok = false;
    const unsigned int packed = trimmed.toUInt(&ok, 0);
    if (!ok)
        return defaultValue;
    return packed <= 0xFFFFFF ? 0xFF000000U | packed : packed;
}

QString serializeEditorColor(unsigned int value)
{
    const unsigned int alpha = (value >> 24) & 0xFF;
    QString result = QString("%1,%2,%3")
        .arg((value >> 16) & 0xFF)
        .arg((value >> 8) & 0xFF)
        .arg(value & 0xFF);
    if (alpha != 0xFF)
        result += QString(",%1").arg(alpha);
    return result;
}

QString objectTitle(const QString& name, const QString& type)
{
    const QMap<QString, QString> names = {
        {"rtnBtn", QObject::tr("返回按钮")}, {"exitBtn", QObject::tr("退出按钮")}, {"closeBtn", QObject::tr("关闭按钮")},
        {"initBtn", QObject::tr("新游戏按钮")}, {"loadBtn", QObject::tr("读档按钮")}, {"teamBtn", QObject::tr("制作名单按钮")},
        {"label", QObject::tr("正文")}, {"messageLabel", QObject::tr("提示正文")}, {"hint", QObject::tr("提示文字")},
        {"heading", QObject::tr("标题文字")}, {"headingFrame", QObject::tr("标题底图")}, {"closeButton", QObject::tr("关闭按钮")},
        {"thumbnailContainer", QObject::tr("地图图像（游戏生成）")},
        {"head1", QObject::tr("左侧头像")}, {"head2", QObject::tr("右侧头像")},
        {"selectA", QObject::tr("选项列表（共用样式）")}, {"selectB", QObject::tr("选项间距参考")},
        {"music", QObject::tr("音乐音量")}, {"sound", QObject::tr("音效音量")}, {"speed", QObject::tr("游戏速度")},
        {"musicBar", QObject::tr("音乐音量滑块")}, {"soundBar", QObject::tr("音效音量滑块")}, {"speedBar", QObject::tr("游戏速度滑块")},
        {"cheatSettingsButton", QObject::tr("打开作弊设置")}, {"cheatPanelBackground", QObject::tr("作弊面板背景")},
        {"cheatPanelTitle", QObject::tr("作弊面板标题")}, {"cheatModeButton", QObject::tr("作弊开关")},
        {"invincibilityButton", QObject::tr("无敌开关")}, {"restoreResourcesButton", QObject::tr("恢复生命与内力")},
        {"increaseMagicLevelButton", QObject::tr("提升武功等级")}, {"increasePlayerLevelButton", QObject::tr("提升角色等级")},
        {"addMoneyButton", QObject::tr("增加银两")}, {"experienceMultiplierButton", QObject::tr("经验倍率")},
        {"cheatCloseButton", QObject::tr("关闭作弊设置")}, {"cheatResultLabel", QObject::tr("作弊操作结果")},
        {"touchControlsButton", QObject::tr("触控按钮开关")}, {"themeButton", QObject::tr("游戏界面主题")},
        {"automaticSaveButton", QObject::tr("自动存档开关")}, {"listBox", QObject::tr("列表")},
        {"memoText", QObject::tr("备忘正文")}, {"moneyLabel", QObject::tr("银两数值")}
    };
    if (names.contains(name)) return names[name];
    const QMap<QString, QString> types = {{"Label", QObject::tr("文字")}, {"TalkLabel", QObject::tr("对话正文")},
        {"ImageContainer", QObject::tr("图片")}, {"Button", QObject::tr("按钮")}, {"TextButton", QObject::tr("文字按钮")},
        {"FlatTextButton", QObject::tr("文字按钮")}, {"Scrollbar", QObject::tr("滚动条")}, {"Item", QObject::tr("物品格")},
        {"ColumnImage", QObject::tr("状态条")}, {"ListBox", QObject::tr("列表")}, {"Joystick", QObject::tr("移动摇杆")}};
    return types.value(type, type) + " · " + name;
}

void setPreviewComponentImages(
    const QString& componentType,
    const ComponentIniProperties& properties,
    PreviewComponent& previewComponent)
{
    previewComponent.imagePath.clear();
    previewComponent.imageFrame = 0;
    previewComponent.secondaryImagePath.clear();
    previewComponent.secondaryImageFrame = 0;

    if (componentType == "Joystick")
    {
        if (!properties.baseImage.isEmpty() || !properties.thumbImage.isEmpty())
        {
            previewComponent.imagePath = properties.baseImage;
            previewComponent.secondaryImagePath = properties.thumbImage;
        }
        else
        {
            previewComponent.imagePath = properties.image;
            previewComponent.secondaryImagePath = properties.image;
            previewComponent.secondaryImageFrame = 1;
        }
        return;
    }

    previewComponent.imagePath = properties.image.isEmpty() ? properties.bitmap : properties.image;
    if ((componentType == "Button" || componentType == "DragButton" ||
         componentType == "CheckBox" || componentType == "TextButton" || componentType == "FlatTextButton" ||
         componentType == "ChooseTextButton" || componentType == "RoundButton" ||
         componentType == "DragRoundButton") && properties.up >= 0)
    {
        previewComponent.imageFrame = properties.up;
    }
    else if (properties.frameSet && properties.frame >= 0)
    {
        previewComponent.imageFrame = properties.frame;
    }
}

// 运行时构造函数设置的 stretch 默认值。
// TextButton/ChooseTextButton/RoundButton/DragRoundButton 构造函数设置 stretch=true，
// Button/DragButton/CheckBox 构造函数不设置（继承 Button 的 stretch=false）。
// Joystick 不调用 RoundButton::initFromIni，构造函数 stretch=true 但不读取 INI。
bool runtimeStretchDefault(const QString& componentType)
{
    return componentType == "TextButton" || componentType == "FlatTextButton" ||
           componentType == "ChooseTextButton" ||
           componentType == "RoundButton" ||
           componentType == "DragRoundButton" ||
           componentType == "Joystick";
}

// 根据组件类型和 INI 设置状态，解析有效的 stretch 值。
bool resolveStretch(const ComponentIniProperties& props, const QString& componentType)
{
    if (props.stretchSet)
        return props.stretch;
    return runtimeStretchDefault(componentType);
}

bool isRuntimeIniWhitespace(QChar character)
{
    return character == QLatin1Char(' ') ||
           character == QLatin1Char('\t') ||
           character == QLatin1Char('\v') ||
           character == QLatin1Char('\f') ||
           character == QLatin1Char('\r') ||
           character == QLatin1Char('\n');
}

QString trimRuntimeIniWhitespace(const QString& value)
{
    qsizetype begin = 0;
    qsizetype end = value.size();
    while (begin < end && isRuntimeIniWhitespace(value[begin]))
        ++begin;
    while (end > begin && isRuntimeIniWhitespace(value[end - 1]))
        --end;
    return value.mid(begin, end - begin);
}

int findRuntimeIniDelimiter(const QString& value, const QString& delimiters)
{
    bool previousWasWhitespace = false;
    for (qsizetype index = 0; index < value.size(); ++index)
    {
        const QChar character = value[index];
        if (delimiters.contains(character))
            return static_cast<int>(index);
        if (character == QLatin1Char(';') && previousWasWhitespace)
            return -1;
        previousWasWhitespace = isRuntimeIniWhitespace(character);
    }
    return -1;
}

int findRuntimeIniInlineComment(const QString& value)
{
    bool previousWasWhitespace = false;
    for (qsizetype index = 0; index < value.size(); ++index)
    {
        const QChar character = value[index];
        if (character == QLatin1Char(';') && previousWasWhitespace)
            return static_cast<int>(index);
        previousWasWhitespace = isRuntimeIniWhitespace(character);
    }
    return -1;
}

bool parseRuntimeIniSection(const QString& rawLine, QString& sectionName)
{
    const QString line = trimRuntimeIniWhitespace(rawLine);
    if (!line.startsWith(QLatin1Char('[')))
        return false;
    const int sectionEnd =
        findRuntimeIniDelimiter(line.mid(1), QStringLiteral("]"));
    if (sectionEnd < 0)
        return false;
    sectionName = line.mid(1, sectionEnd);
    return true;
}

bool parseRuntimeIniEntry(
    const QString& rawLine, QString& key, QString& value,
    QString* inlineCommentSuffix = nullptr)
{
    const QString line = trimRuntimeIniWhitespace(rawLine);
    if (line.isEmpty() ||
        line.startsWith(QLatin1Char(';')) ||
        line.startsWith(QLatin1Char('#')))
    {
        return false;
    }

    const int separator =
        findRuntimeIniDelimiter(line, QStringLiteral("=:"));
    if (separator < 0)
        return false;

    key = trimRuntimeIniWhitespace(line.left(separator)).toLower();
    const QString rawValue = line.mid(separator + 1);
    const int inlineComment = findRuntimeIniInlineComment(rawValue);
    value = trimRuntimeIniWhitespace(
        inlineComment >= 0 ? rawValue.left(inlineComment) : rawValue);
    if (inlineCommentSuffix)
    {
        if (inlineComment < 0)
        {
            inlineCommentSuffix->clear();
        }
        else
        {
            int suffixBegin = inlineComment;
            while (suffixBegin > 0 &&
                   isRuntimeIniWhitespace(rawValue[suffixBegin - 1]))
            {
                --suffixBegin;
            }
            *inlineCommentSuffix = rawValue.mid(suffixBegin);
        }
    }
    return true;
}
}

MenuEditorWindow::MenuEditorWindow(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::MenuEditorWindow)
{
    ui->setupUi(this);

    setupToolBar();

    ui->outerLayout->setStretch(0, 0);
    ui->outerLayout->setStretch(1, 0);
    ui->outerLayout->setStretch(2, 0);
    ui->outerLayout->setStretch(3, 1);

    previewCanvas = new MenuPreviewCanvas();
    ui->previewScrollArea->setWidget(previewCanvas);

    propertyEditor = new ComponentPropertyEditor();
    auto propertyLayout = ui->propertyEditorPlaceholder->layout();
    editStatusLabel = new QLabel(this);
    editStatusLabel->setObjectName("menuEditStatusLabel");
    editStatusLabel->setWordWrap(true);
    editStatusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    editStatusLabel->hide();
    propertyLayout->addWidget(editStatusLabel);
    auto* functionDescription = new QLabel(this);
    functionDescription->setObjectName("menuObjectDescription");
    functionDescription->setWordWrap(true);
    functionDescription->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    propertyLayout->addWidget(functionDescription);
    propertyLayout->addWidget(propertyEditor);

    assetBrowser = new AssetBrowser();
    auto assetLayout = ui->assetPage->layout();
    auto assetRootCombo = new QComboBox(ui->assetPage);
    assetRootCombo->setObjectName("assetRootCombo");
    assetRootCombo->setToolTip(tr("选择浏览的资源根；继承根只读，保存始终写入当前资源包"));
    assetLayout->addWidget(assetRootCombo);
    assetLayout->addWidget(assetBrowser);

    ui->componentTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->componentTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->componentTree->header()->setStretchLastSection(true);

    setupPreviewControls();
    ui->mainSplitter->setSizes({220, 700, 320});
    ui->componentTree->setHeaderLabels({tr("界面对象"), tr("内部名称"), tr("配置文件")});
    ui->componentTree->setColumnHidden(1, true);
    ui->componentTree->setColumnHidden(2, true);
    ui->componentTree->setMinimumWidth(170);
    ui->mainSplitter->setStretchFactor(0, 0);
    ui->mainSplitter->setStretchFactor(1, 1);
    ui->mainSplitter->setStretchFactor(2, 0);

    connect(ui->menuNameEdit, &QLineEdit::textChanged, this, [this](const QString& text)
    {
        if (!updatingFromCode) { const QSignalBlocker blocker(ui->menuNameEdit); ui->menuNameEdit->setText(currentDefinition.menuName); }
        Q_UNUSED(text);
    });
    connect(ui->menuVisibleCheck, &QCheckBox::toggled, this, [this](bool checked)
    {
        if (!updatingFromCode) { const QSignalBlocker blocker(ui->menuVisibleCheck); ui->menuVisibleCheck->setChecked(currentDefinition.visible); }
        Q_UNUSED(checked);
    });

    connect(ui->componentTree, &QTreeWidget::currentItemChanged, this, &MenuEditorWindow::onComponentSelected);
    connect(ui->addButton, &QPushButton::clicked, this, &MenuEditorWindow::onAddComponent);
    connect(ui->addSubMenuButton, &QPushButton::clicked, this, &MenuEditorWindow::onAddSubMenu);
    connect(ui->removeButton, &QPushButton::clicked, this, &MenuEditorWindow::onRemoveComponent);
    connect(ui->moveUpButton, &QPushButton::clicked, this, &MenuEditorWindow::onComponentMovedUp);
    connect(ui->moveDownButton, &QPushButton::clicked, this, &MenuEditorWindow::onComponentMovedDown);
    connect(propertyEditor, &ComponentPropertyEditor::propertiesChanged, this, &MenuEditorWindow::onPropertyChanged);
    connect(propertyEditor, &ComponentPropertyEditor::definitionPropertiesChanged,
            this, &MenuEditorWindow::onDefinitionPropertyChanged);
    connect(propertyEditor, &ComponentPropertyEditor::choiceListSpacingChanged, this, [this](int gap)
    {
        if (selectedPage != "choose" || updatingFromCode || !canEditPage()) return;
        ComponentIniProperties first;
        for (const auto& component : currentDefinition.components)
            if (component.name == "selectA") first = propertiesForResource(component.file);
        for (const auto& component : currentDefinition.components)
            if (component.name == "selectB")
            {
                auto second = propertiesForResource(component.file);
                second.top = first.top + first.height + gap;
                if (stageComponentProperties(component.editorId, component.file, second, component.type)) { markModified(); updatePreview(); }
                break;
            }
    });
    connect(propertyEditor, &ComponentPropertyEditor::resourceFileEditingFinished,
            this, &MenuEditorWindow::onResourceFileEditingFinished);
    connect(previewCanvas, &MenuPreviewCanvas::componentSelected, this, &MenuEditorWindow::onPreviewComponentSelected);
    connect(previewCanvas, &MenuPreviewCanvas::componentMoved, this, &MenuEditorWindow::onPreviewComponentMoved);
    connect(previewCanvas, &MenuPreviewCanvas::componentResized, this, &MenuEditorWindow::onPreviewComponentResized);
    connect(previewCanvas, &MenuPreviewCanvas::componentPropertyChanged, this, &MenuEditorWindow::onPreviewComponentPropertyChanged);
    connect(assetBrowser, &AssetBrowser::fileSelected, this, &MenuEditorWindow::onAssetFileSelected);
    connect(assetBrowser, &AssetBrowser::fileDoubleClicked, this, &MenuEditorWindow::onAssetFileDoubleClicked);
    connect(assetRootCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this, assetRootCombo](int index)
        {
            const QString root = assetRootCombo->itemData(index).toString();
            if (!root.isEmpty())
                assetBrowser->setAssetsBasePath(root);
        });

    for (auto* button : {ui->addButton, ui->addSubMenuButton, ui->removeButton, ui->moveUpButton, ui->moveDownButton})
    {
        button->setEnabled(false);
        button->hide();
    }
    connect(this,&MenuEditorWindow::documentStatesChanged,this,[this]()
    {
        if (selectedPage == "dicegame") updateDicePropertyEditor();
    });
    resetHistory();
    if(selectedPage=="controllerhelp" || selectedPage=="dicegame")recordControllerHelpDiskState();
    refreshPageList();
    updatePagePermissions();
}

MenuEditorWindow::~MenuEditorWindow()
{
    delete ui;
}

void MenuEditorWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        ui->retranslateUi(this);
        retranslateDynamicUi();
    }
    QWidget::changeEvent(event);
}

void MenuEditorWindow::retranslateDynamicUi()
{
    undoAction->setText(tr("撤销"));
    redoAction->setText(tr("重做"));
    saveAction->setText(tr("保存"));
    refreshAction->setText(tr("刷新预览"));
    bagGridAction->setText(tr("背包阵列"));
    if (optionViewCombo)
    {
        const QSignalBlocker blocker(optionViewCombo);
        optionViewCombo->setItemText(0, tr("主设置"));
        optionViewCombo->setItemText(1, tr("作弊面板（关闭）"));
        optionViewCombo->setItemText(2, tr("作弊面板（开启）"));
        optionViewCombo->setToolTip(tr("切换设置或作弊面板，检查对应外观。示例不保存；游戏中的功能和显示条件保持不变。"));
        optionControllerPreview->setText(tr("控制器存在（预览）"));
    }
    if (partnerHeadPreview)
    {
        const QSignalBlocker blocker(partnerHeadPreview);
        partnerHeadPreview->setItemText(0, tr("头像模板（布局编辑）"));
        partnerHeadPreview->setItemText(1, tr("无伙伴（示例）"));
        partnerHeadPreview->setItemText(2, tr("多个伙伴（原尺寸／权限示例）"));
        partnerHeadPreview->setToolTip(tr("示例头像尺寸及等级不保存；等级位置相对头像右下角。不可查看伙伴显示为灰色示例。"));
        partnerHeadSpacing->setPrefix(tr("头像间距："));
        partnerHeadSpacing->setAccessibleName(tr("头像垂直间距"));
    }
    const int previewIndex = toolTipPreviewCombo->currentIndex();
    const QSignalBlocker previewBlocker(toolTipPreviewCombo);
    toolTipPreviewCombo->setProperty("previewContext", QVariant());
    updatePreview();
    if (previewIndex >= 0 && previewIndex < toolTipPreviewCombo->count()) toolTipPreviewCombo->setCurrentIndex(previewIndex);
    updatePreview();
    toolTipPreviewCombo->setToolTip(tr("仅切换预览；游戏仍决定实际显示状态。示例文字及分页不写入资源。"));

    if (ui->componentTree->topLevelItemCount() > 0)
    {
        ui->componentTree->topLevelItem(0)->setText(0, tr("[窗口]"));
        const int subMenuOffset = currentDefinition.components.size() + 1;
        for (int index = 0; index < currentDefinition.subMenus.size(); ++index)
        {
            if (QTreeWidgetItem* item = ui->componentTree->topLevelItem(subMenuOffset + index))
                item->setText(0, tr("[子菜单]"));
        }
    }

    if (QComboBox* rootCombo = findChild<QComboBox*>(QStringLiteral("assetRootCombo")))
    {
        rootCombo->setToolTip(tr("选择浏览的资源根；继承根只读，保存始终写入当前资源包"));
        const QString selectedRoot = rootCombo->currentData().toString();
        const bool blocked = rootCombo->blockSignals(true);
        for (int index = 0; index < rootCombo->count(); ++index)
        {
            const QString root = rootCombo->itemData(index).toString();
            const QString label = index == 0 &&
                EditorAssetPath::logicalComparisonKey(root) ==
                    EditorAssetPath::logicalComparisonKey(assetsBasePath)
                ? tr("本地 UI — %1").arg(QFileInfo(root).fileName())
                : tr("只读回退 %1 — %2").arg(index).arg(QFileInfo(root).fileName());
            rootCombo->setItemText(index, label);
        }
        const int selectedIndex = rootCombo->findData(selectedRoot);
        if (selectedIndex >= 0)
            rootCombo->setCurrentIndex(selectedIndex);
        rootCombo->blockSignals(blocked);
    }

    refreshPageList();
    themeCombo->setItemText(0, tr("原版"));
    themeCombo->setItemText(1, tr("青玉"));
    saveLoadContextCombo->setItemText(0, tr("游戏内存读档"));
    saveLoadContextCombo->setItemText(1, tr("标题存读档"));
    updatePagePermissions();
    setWindowTitle(currentFilePath.isEmpty()
        ? tr("菜单编辑器")
        : tr("菜单编辑器 - ") + QFileInfo(currentFilePath).fileName());
}


namespace
{
QStringList menuPageIds()
{
    return MenuPageContext::pages();
}

QString menuPageName(const QString& page)
{
    const QMap<QString, QString> names = {
        {"goods", MenuEditorWindow::tr("背包")}, {"equip", MenuEditorWindow::tr("装备")},
        {"state", MenuEditorWindow::tr("属性")}, {"tooltip", MenuEditorWindow::tr("提示框")},
        {"title", MenuEditorWindow::tr("标题菜单")}, {"system", MenuEditorWindow::tr("系统")}, {"yesno", MenuEditorWindow::tr("是／否确认")},
        {"option", MenuEditorWindow::tr("选项")}, {"saveload", MenuEditorWindow::tr("存读档")},
        {"buysell", MenuEditorWindow::tr("买卖")}, {"magic", MenuEditorWindow::tr("武功")},
        {"memo", MenuEditorWindow::tr("事件／备忘")}, {"xiulian", MenuEditorWindow::tr("修炼")}, {"dialog", MenuEditorWindow::tr("对话")}, {"choose", MenuEditorWindow::tr("选择")},
        {"titleteam", MenuEditorWindow::tr("制作名单")}, {"top", MenuEditorWindow::tr("顶部菜单")}, {"bottom", MenuEditorWindow::tr("底部快捷栏")}, {"column", MenuEditorWindow::tr("人物状态条")}, {"joystick", MenuEditorWindow::tr("移动摇杆")}, {"skills", MenuEditorWindow::tr("移动技能／操作按钮")}, {"dicegame",MenuEditorWindow::tr("骰子对局")}, {"controllerhelp",MenuEditorWindow::tr("游戏内手柄帮助")}, {"npcinfo", MenuEditorWindow::tr("NPC信息／目标血条")}, {"mapthumbnail", MenuEditorWindow::tr("小地图")}, {"systemnotice", MenuEditorWindow::tr("系统通知")}, {"scriptmessages", MenuEditorWindow::tr("脚本消息")}, {"message", MenuEditorWindow::tr("消息提示")}, {"timer", MenuEditorWindow::tr("计时")}, {"partnerhead", MenuEditorWindow::tr("伙伴头像")}};
    return names.value(page);
}
}

bool MenuEditorWindow::validMenuBindingFormat(QString format, const QString& binding)
{
    format.remove("%%");
    const int placeholders = format.count("%d");
    format.remove("%d");
    return !format.contains('%') && placeholders == binding.split(',').size();
}

QString MenuEditorWindow::pageResource(const QString& page) const
{
    return pageResources.resource(page, selectedTheme, saveLoadAtTitle);
}

QString MenuEditorWindow::pageDisplayName(const QString& page) const
{
    if (page.isEmpty()) return {};
    if (page != "equip") return menuPageName(page);
    QStringList roles;
    for (const QString role : {QString("equip"), QString("state"), QString("magic")})
    {
        if (pageResources.menuPageForRole(role, selectedTheme) == page) roles.append(menuPageName(role));
    }
    return roles.isEmpty() ? menuPageName(page) : roles.join(tr("／"));
}

QString MenuEditorWindow::resolvedPageSource(const QString& page) const
{
    const QString source = pageResources.source(page, selectedTheme, saveLoadAtTitle);
    if (source.isEmpty() && pageResources.valid && (page == "controllerhelp" || page == "npcinfo" || page == "partnerhead" || page == "titleteam" || isNoticePage(page)
        || (page == "yesno" && pageResources.canLoad(page, selectedTheme))))
    {
        QString target;
        resolveLocalResourcePath(pageResource(page), target);
        return target;
    }
    return source;
}

QString MenuEditorWindow::documentPage(const QString& filePath) const
{
    QStringList pages = menuPageIds();
    if (!requestedPage.isEmpty()) { pages.removeAll(requestedPage); pages.prepend(requestedPage); }
    for (const QString& page : pages)
        if (!resolvedPageSource(page).isEmpty() && EditorAssetPath::logicalComparisonKey(resolvedPageSource(page))
            == EditorAssetPath::logicalComparisonKey(filePath))
        {
            const QString menu = pageResources.menuPageForRole(page, selectedTheme);
            return menu.isEmpty() ? page : menu;
        }
    return {};
}

QString MenuEditorWindow::pageTargetPath() const
{
    QString target;
    if (!pageEntryResource.isEmpty()) resolveLocalResourcePath(pageEntryResource, target);
    return target;
}

bool MenuEditorWindow::canEditPage() const
{
    return pageRestriction.isEmpty() && !selectedPage.isEmpty() && !currentFilePath.isEmpty();
}

bool MenuEditorWindow::effectiveQingyuTheme() const
{
    return selectedPage != "title" && (selectedPage == "saveload" && saveLoadAtTitle
        ? selectedTheme == "qingyu" : pageResources.qingyu(selectedTheme));
}

QByteArray MenuEditorWindow::pageContextFingerprint() const
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << assetsBasePath << pageResources.readRoots << pageResources.contentRoots << selectedTheme << effectiveQingyuTheme() << saveLoadAtTitle
           << QString::fromStdString(pageResources.manifest.id) << QString::fromStdString(pageResources.manifest.uiProfile)
           << QString::fromStdString(pageResources.manifest.uiBaseId) << pageResources.manifest.preferLocalUi;
    for (const char* feature : {"stateequipintegratedlayout", "topbuttonslayout", "characterpanelimages", "equipplayernameimages", "partnerheadmenu", "practicemenudisabled", "magicbuttonopensintegratedequip"})
        stream << pageResources.manifest.isFeatureEnabled(feature, false);
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

void MenuEditorWindow::refreshPageList()
{
    const QSignalBlocker blocker(pageCombo);
    pageCombo->clear();
    for (const QString& page : pageResources.menuPages(selectedTheme))
    {
        QString label = pageDisplayName(page);
        if (!pageResources.canLoad(page, selectedTheme)) label += tr("（配置缺失）");
        pageCombo->addItem(label, page);
        if (page == "titleteam" && !pageResources.teamContentProblem.isEmpty())
            pageCombo->setItemData(pageCombo->count() - 1,
                pageResources.teamContentProblem, Qt::ToolTipRole);
    }
    pageCombo->setCurrentIndex(pageCombo->findData(selectedPage));
    pageCombo->setEnabled(pageResources.valid);
}

bool MenuEditorWindow::selectPage(const QString& role)
{
    const QString page = pageResources.menuPageForRole(role, selectedTheme);
    if (page.isEmpty())
    {
        pageContextLabel->setText(tr("当前布局不使用此独立菜单；请选择菜单列表中的有效入口。"));
        refreshPageList();return false;
    }
    const QString source = resolvedPageSource(page);
    if (page == "yesno" && pageResources.yesNoFallback(selectedTheme) && !pageResources.canLoad(page, selectedTheme))
    {
        QStringList unavailable;
        for (const QString& reference : {QString("ini/ui/yesno/window.ini"), QString("ini/ui/yesno/btnyes.ini"), QString("ini/ui/yesno/btnno.ini"), QString("ini/ui/yesno/label.ini")})
            if (!pageResources.hasComponentData(reference)) unavailable.append(reference);
        pageContextLabel->setText(tr("确认框固定回退文件缺失、不可读取或为空：%1").arg(unavailable.join('\n')));
        refreshPageList();
        return false;
    }
    if (source.isEmpty())
    {
        pageContextLabel->setText(tr("该页面没有可读取的有效配置，原文档保持不变。"));
        refreshPageList();
        return false;
    }
    if (documentContextFingerprint == pageContextFingerprint()
        && EditorAssetPath::logicalComparisonKey(source) == EditorAssetPath::logicalComparisonKey(currentFilePath))
    {
        selectedPage = page;
        updatePagePermissions();
        refreshPageList();
        return true;
    }
    requestedPage = page;
    const bool opened = openMenuDefinition(source);
    requestedPage.clear();
    refreshPageList();
    return opened;
}

void MenuEditorWindow::updatePagePermissions()
{
    pageRestriction.clear();
    if (!pageResources.valid) pageRestriction = tr("请先选择有效的当前资源包。");
    else if (selectedPage.isEmpty()) pageRestriction = currentFilePath.isEmpty()
        ? tr("请选择当前布局中的菜单。禁用的功能不列出，缺失的配置会明确标记。")
        : tr("未确认此文件是当前页面的有效入口，仅供读取和预览。");
    else if (!QStringList{"goods", "equip", "state", "tooltip", "option", "partnerhead", "magic", "xiulian", "memo", "title", "system", "yesno", "saveload", "buysell", "titleteam", "dialog", "choose", "message", "timer", "systemnotice", "scriptmessages", "top", "bottom", "column", "mapthumbnail", "npcinfo", "joystick", "skills", "controllerhelp", "dicegame"}.contains(selectedPage))
        pageRestriction = tr("该页面的显示字段尚未核实，本轮只读。");
    else if (selectedPage == "titleteam" && pageResources.teamInfoText.isEmpty())
        pageRestriction = pageResources.teamContentProblem.isEmpty() ? tr("纯视频制作名单：字幕属于视频内容，本页只读；不生成文字覆盖。") : pageResources.teamContentProblem;
    else if (selectedPage == "partnerhead" && !pageResources.partnerHeadsAvailable())
        pageRestriction = tr("当前资源包未启用伙伴头像功能；编辑器不会修改功能开关。");
    else if (selectedPage == "xiulian" && !pageResources.practiceAvailable())
        pageRestriction = tr("当前资源包禁用修炼入口；配置仅供预览，不修改功能开关。");
    else
    {
        QString source = resolvedPageSource(selectedPage);
        const QString target = pageTargetPath();
        for (const QString& readRoot : pageResources.rootsFor(pageEntryResource))
        {
            if (EditorAssetPath::logicalComparisonKey(readRoot) == EditorAssetPath::logicalComparisonKey(assetsBasePath)) break;
            QString shadow;
            if (EditorAssetPath::resolveLogicalResourcePath(readRoot, pageEntryResource, shadow) && QFileInfo(shadow).isFile())
            {
                pageRestriction = tr("UI 依赖优先：当前包覆盖仍会被遮蔽，已限制写入。");
                break;
            }
        }
        if (source.isEmpty() || target.isEmpty() || EditorAssetPath::logicalComparisonKey(source)
            != EditorAssetPath::logicalComparisonKey(currentFilePath))
            pageRestriction = tr("页面来源已改变，请重新选择页面；待保存编辑保留。");
        QString error;
        if (pageRestriction.isEmpty() && !validateKnownMenu(error)) pageRestriction = error;
    }
    if (pageRestriction.isEmpty())
    {
        QStringList references;
        if (!currentDefinition.windowFile.isEmpty()) references.append(currentDefinition.windowFile);
        for (const auto& component : currentDefinition.components) if (!component.file.isEmpty()) references.append(component.file);
        for (const auto& subMenu : currentDefinition.subMenus) references.append(subMenu.file);
        for (const auto& reference : references)
            if (normalizedResourcePath(reference).isEmpty()) pageRestriction = tr("存在无效或越界的固定引用，本页只读：%1").arg(reference);
    }
    if (pageRestriction.isEmpty() && !documentContextFingerprint.isEmpty()
        && documentContextFingerprint != pageContextFingerprint())
        pageRestriction = tr("资源上下文或功能设置已改变，请重新选择页面；待保存编辑保留。");
    if (pageRestriction.isEmpty() && !documentStructureIssue.isEmpty()) pageRestriction = documentStructureIssue;
    if (resourceRecoveryBlocked) pageRestriction = tr("资源事务恢复失败，保存已停用：%1").arg(resourceRecoveryErrors.join('\n'));
    if (!pageResources.problem.isEmpty()) pageRestriction = tr("资源上下文解析失败：%1").arg(pageResources.problem);
    themeCombo->setEnabled(selectedPage != "titleteam" && !isNoticePage(selectedPage));
    saveAction->setEnabled(canEditPage());
    bagGridAction->setEnabled(canEditPage() && isGoodsMenu());
    ui->menuNameEdit->setReadOnly(true);
    ui->menuVisibleCheck->setEnabled(false);
    ui->menuVisibleCheck->setToolTip(tr("显示状态由游戏控制，此处仅保留原配置，不代表运行中的可见状态。"));
    pageContextLabel->setText(pageRestriction.isEmpty()
        ? tr("%1 · %2 · 修改保存到当前资源包").arg(pageDisplayName(selectedPage), effectiveQingyuTheme() ? tr("青玉") : tr("原版"))
        : tr("%1：%2").arg(pageDisplayName(selectedPage), pageRestriction));
    updatePropertyEditor();
}

unsigned int MenuEditorWindow::displayColor(const QString& text, unsigned int fallback)
{
    return parseEditorColor(text, fallback);
}

QSet<QString> MenuEditorWindow::editableFields(const QString& owner) const
{
    if (!canEditPage()) return {};
    const QString kind = selectedPage;
    if (kind == "titleteam") return titleTeamFields(owner);
    if (isNoticePage(kind)) return noticeFields(owner);
    if (isHudPage(kind)) return hudFields(owner);
    if (kind == "joystick") return joystickFields(owner);
    if (kind == "skills") return skillsFields(owner);
    if (kind == "dicegame") return {"dice-display"};
    if (kind == "controllerhelp") return owner=="menu/window"?controllerHelpFields():QSet<QString>();
    if (kind == "npcinfo") return owner == "menu/window" ? QSet<QString>{"width","height","alignx","aligny"} : QSet<QString>();
    if (kind == "mapthumbnail") return mapThumbnailFields(owner);
    if (kind == "dialog" || kind == "choose") return conversationFields(owner);
    if (kind == "message" || kind == "timer") return passiveFields(owner);
    if ((kind == "saveload" || kind == "buysell") && owner != "menu/window") return storagePageFields(owner);
    if ((kind == "title" || kind == "option") && owner == "menu/window") return systemPageFields(owner);
    if (kind == "partnerhead" && owner == "menu/window") return {};
    if (kind == "title" || kind == "system" || kind == "yesno")
        if (owner != "menu/window") return systemPageFields(owner);
    if (kind == "option" && owner.endsWith("/slideBtn")) return systemPageFields(owner);
    if (owner == "menu/window")
    {
        if (currentDefinition.windowFile.isEmpty()) return {};
        QSet<QString> fields = {"width", "height", "image", "bitmap", "stretch", "scale", "nineslice", "nineslicewidth"};
        if (kind == "tooltip")
        {
            bool body = false;
            for (const auto& component : currentDefinition.components) body |= component.name == "body" && component.type == "MemoText";
            if (!body && (pageResources.manifest.uiProfile == "XJXQY" || effectiveQingyuTheme())) fields.remove("height");
        }
        else fields.unite({"align", "alignx", "aligny"});
        if (kind == "equip" && !effectiveQingyuTheme()
            && pageResources.manifest.isFeatureEnabled("characterpanelimages", pageResources.manifest.uiProfile == "YYCS"))
            fields.subtract({"image", "bitmap"});
        return fields;
    }
    const MenuComponentDefinition* found = nullptr;
    for (const auto& component : currentDefinition.components)
        if (component.editorId == owner) { found = &component; break; }
    if (!found || found->file.isEmpty()) return {};
    const QString& name = found->name;
    const QString& type = found->type;
    if (kind == "partnerhead")
    {
        if (name == "head" && type == "Item")
        {
            QSet<QString> fields = {"left", "top", "stretch"};
            if (const_cast<MenuEditorWindow*>(this)->propertiesForResource(found->file).stretch) fields.unite({"width", "height"});
            return fields;
        }
        if (name == "level" && type == "Label")
            return {"left", "top", "width", "height", "font", "color"};
        return {};
    }
    if (name == "characterSelector" && (kind == "equip" || kind == "state")
        && (type == "TextButton" || type == "FlatTextButton"))
    {
        QSet<QString> fields = {"left", "top", "width", "height", "font", "color", "image", "stretch", "up", "down", "track"};
        if (type == "FlatTextButton") fields.unite({"backgroundcolor", "hoverbackgroundcolor", "pressedbackgroundcolor"});
        return fields;
    }
    if (kind == "option")
    {
        const auto* control = OptionLayout::find(name.toStdString());
        if (!control) return systemPageFields(owner);
        if (type != control->type) return {};
        if (name == "automaticSaveButton" && !optionAutomaticSaveAvailable()) return {};
        if (name == "themeButton" && !pageResources.qingyu("qingyu")) return {};
        QSet<QString> fields = {"left", "top", "width", "height"};
        if (type == "ImageContainer") fields.unite({"image", "bitmap", "stretch", "keepaspect", "frame", "nineslice", "nineslicewidth"});
        else fields.unite({"font", "color"});
        if (type == "FlatTextButton") fields.unite({"image", "stretch", "backgroundcolor", "hoverbackgroundcolor", "pressedbackgroundcolor"});
        if (type == "Label") fields.unite({"autoshrink", "minimumfont", "centertext", "autonextline"});
        if (!control->dynamicText && found->bind.isEmpty() && type != "ImageContainer") fields.insert("text");
        return fields;
    }

    const int equipmentMagicIndex = name.startsWith("magic") ? name.mid(5).toInt() : 0;
    const bool equipmentMagic = equipmentMagicIndex > 0
        && name == QString("magic%1").arg(equipmentMagicIndex, 2, 10, QChar('0'));
    if (kind == "magic" || kind == "xiulian" || kind == "memo"
        || (kind == "equip" && (name == "equipmentTab" || name == "attributesTab" || name == "magicScrollbar"
            || equipmentMagic
            || QRegularExpression("^showname0[0-3]$").match(name).hasMatch())))
    {
        const bool image = type == "ImageContainer" && QStringList{"title", "image", "headingFrame", "gridTray", "footerRule",
            "detailIconFrame", "detailIcon", "detailBody", "showname00", "showname01", "showname02", "showname03"}.contains(name);
        const bool item = type == "Item" && ((kind == "magic" && QRegularExpression("^item[1-9][0-9]*$").match(name).hasMatch())
            || (kind == "xiulian" && name == "magic") || (kind == "equip" && equipmentMagic));
        const bool label = type == "Label" && (kind == "magic"
            ? QStringList{"heading", "hint", "listCaption", "detailName", "detailPage"}.contains(name)
            : (kind == "xiulian" ? QStringList{"heading", "name", "intro", "level", "exp", "levelCaption", "expCaption", "hint"}.contains(name)
                : QStringList{"heading", "hint"}.contains(name)));
        const bool memo = type == "MemoText" && ((kind == "magic" && name == "detailText") || (kind == "memo" && name == "memoText"));
        const bool scroll = type == "Scrollbar" && ((kind == "magic" || kind == "memo") ? name == "scrollbar" : name == "magicScrollbar");
        const bool tab = type == "CheckBox" && ((kind == "magic" && name == "magicTab")
            || (kind == "equip" && (name == "equipmentTab" || name == "attributesTab")));
        const bool button = type == "FlatTextButton" && kind == "magic"
            && QStringList{"detailPrevious", "detailNext", "detailBack", "detailPractice", "detailQuick"}.contains(name);
        if (!(image || item || label || memo || scroll || tab || button)) return {};
        QSet<QString> fields = {"left", "top", "width", "height"};
        if (image)
        {
            fields.unite({"stretch", "keepaspect"});
            if (name != "detailIcon" && !name.startsWith("showname")) fields.unite({"image", "bitmap", "frame", "nineslice", "nineslicewidth"});
        }
        if (item) fields.unite({"font", "color", "backimage1", "backimage2", "drawslot", "stretch", "keepaspect", "centerimage"});
        if (label || memo || button || tab) fields.insert("font");
        if (label || memo || button) fields.insert("color");
        if (label)
        {
            fields.unite({"autonextline", "centertext", "autoshrink", "minimumfont"});
            const bool dynamic = (kind == "magic" && name != "listCaption")
                || (kind == "xiulian" && QStringList{"name", "intro", "level", "exp"}.contains(name));
            if (!dynamic && found->bind.isEmpty()) fields.insert("text");
            if (kind == "xiulian" && name == "intro") fields.subtract({"autonextline", "autoshrink"});
            if (kind == "xiulian" && (name == "level" || name == "exp")) fields.remove("autoshrink");
        }
        if (memo) fields.unite({"linesize", "fitlines"});
        if (scroll) fields.unite({"image", "style", "slidebegin", "slideend"});
        if (tab) fields.unite({"text", "image", "bitmap", "stretch", "up", "down"});
        if (button)
        {
            fields.unite({"image", "stretch", "backgroundcolor", "hoverbackgroundcolor", "pressedbackgroundcolor"});
            if (name != "detailPractice" && name != "detailQuick") fields.insert("text");
        }
        return fields;
    }

    const bool slot = QRegularExpression("^item[1-9][0-9]*$").match(name).hasMatch();
    const bool tooltipLabel = kind == "tooltip" && type == "Label"
        && QStringList{"name", "cost", "intro1", "intro2", "magicIntro", "page", "hint"}.contains(name);
    const bool tooltipButton = kind == "tooltip" && type == "FlatTextButton"
        && QStringList{"previous", "next", "back"}.contains(name);
    const bool body = kind == "tooltip" && name == "body" && type == "MemoText";
    const bool dynamicLabel = type == "Label" && ((kind == "goods" && name == "money")
        || ((kind == "equip" || kind == "state")
            && (QStringList{"labLevel", "labExp", "labExpUp", "labExperience", "labLevelUpExperience", "labAttack", "labDefend", "labEvade", "labLife", "labThew", "labMana", "labRage", "playerName"}.contains(name)
                || QRegularExpression("^detail([0-9]|1[0-3])$").match(name).hasMatch())));
    const bool panelImage = type == "ImageContainer"
        && (((kind == "equip" || kind == "state") && QStringList{"image", "title", "headingFrame", "partTray", "labTray", "detailTray"}.contains(name))
            || (kind == "goods" && QStringList{"title", "image", "gold", "headingFrame", "gridTray", "footerRule"}.contains(name)));
    const bool staticLabel = type == "Label"
        && ((kind == "goods" && QStringList{"heading", "hint"}.contains(name))
            || ((kind == "equip" || kind == "state") && (QStringList{"heading", "detailHint"}.contains(name)
                || QRegularExpression("^part[0-6]$").match(name).hasMatch())));
    const bool tooltipImage = kind == "tooltip" && name == "image" && type == "ImageContainer";
    const bool scrollbar = type == "Scrollbar" && kind == "goods" && name == "scrollbar";
    if (!(tooltipLabel || tooltipButton || body || dynamicLabel || panelImage || staticLabel || tooltipImage || scrollbar
        || (slot && type == "Item" && (kind == "goods" || kind == "equip")))) return {};
    QSet<QString> fields = {"left", "top", "width", "height"};
    if (slot && kind == "goods") fields.clear();
    if (tooltipLabel || dynamicLabel || staticLabel || body || (slot && type == "Item") || tooltipButton)
        fields.unite({"font", "color"});
    if (slot) fields.unite({"backimage1", "backimage2", "drawslot"});
    if (staticLabel) fields.unite({"text", "autonextline", "minimumfont", "centertext", "autoshrink"});
    if (tooltipLabel) fields.unite({"autonextline", "minimumfont", "centertext", "autoshrink"});
    if (body) fields.unite({"linesize", "fitlines"});
    if (tooltipButton) fields.unite({"text", "backgroundcolor", "hoverbackgroundcolor", "pressedbackgroundcolor"});
    if (panelImage) fields.unite({"image", "bitmap", "stretch", "keepaspect", "centerimage", "frame"});
    if (dynamicLabel && !found->bind.isEmpty() && (kind == "equip" || (kind == "goods" && effectiveQingyuTheme()))) fields.insert("format");
    if (scrollbar) fields.unite({"style", "slidebegin", "slideend"});
    if (kind == "tooltip" && tooltipLabel)
    {
        bool bodyPresent = false;
        for (const auto& component : currentDefinition.components) bodyPresent |= component.name == "body" && component.type == "MemoText";
        if (!bodyPresent && (pageResources.manifest.uiProfile == "XJXQY" || effectiveQingyuTheme())) fields.remove("height");
        if (pageResources.manifest.uiProfile == "XJXQY" && !effectiveQingyuTheme()) fields.subtract({"color", "centertext"});
    }
    if (kind == "state" && pageResources.manifest.uiProfile == "JXQY2" && dynamicLabel && !effectiveQingyuTheme())
        fields.subtract({"width", "height", "autoshrink", "minimumfont"});
    if (panelImage && kind == "state" && name == "image" && !effectiveQingyuTheme()
        && pageResources.manifest.isFeatureEnabled("characterpanelimages", pageResources.manifest.uiProfile == "YYCS"))
        fields.subtract({"image", "bitmap"});
    return fields;
}

ComponentIniProperties MenuEditorWindow::allowedProperties(const QString& owner,
    const ComponentIniProperties& original, const ComponentIniProperties& proposed) const
{
    auto result = original;
    auto fields = editableFields(owner);
    if (applyingBagGrid) fields.unite({"left", "top", "width", "height", "linesize", "pagesize"});
#define COPY_FIELD(key, field) if (fields.contains(key)) result.field = proposed.field
    COPY_FIELD("left", left); COPY_FIELD("top", top); COPY_FIELD("width", width); COPY_FIELD("height", height);
    COPY_FIELD("align", align); COPY_FIELD("alignx", alignX); COPY_FIELD("aligny", alignY);
    COPY_FIELD("image", image); COPY_FIELD("bitmap", bitmap); COPY_FIELD("bitmap", bitmapSet);
    COPY_FIELD("icon", icon); COPY_FIELD("iconimage", iconImage); COPY_FIELD("animate", animate); COPY_FIELD("kind", kind);
    COPY_FIELD("range", range); COPY_FIELD("baseimage", baseImage); COPY_FIELD("thumbimage", thumbImage);
    COPY_FIELD("color", color); COPY_FIELD("selcolor", selColor); COPY_FIELD("itemheight", itemHeight);
    if (fields.contains("items") && proposed.items.size() == original.items.size()) result.items = proposed.items;
    COPY_FIELD("up", up); COPY_FIELD("down", down); COPY_FIELD("track", track);
    if (fields.contains("font"))
    {
        int defaultFont = 18;
        for (const auto& component : currentDefinition.components)
            if (component.editorId == owner)
            {
                if (component.type == "MemoText") defaultFont = 16;
                if (component.type == "ListBox") defaultFont = 20;
            }
        if (proposed.font != (original.fontSet ? original.font : defaultFont))
        {
            result.font = proposed.font;
            result.fontSet = true;
        }
    }
    COPY_FIELD("stretch", stretch);
    if (fields.contains("stretch") && original.stretch != proposed.stretch) result.stretchSet = true;
    COPY_FIELD("keepaspect", keepAspect); COPY_FIELD("keepaspect", keepAspectSet);
    COPY_FIELD("centerimage", centerImage); COPY_FIELD("centerimage", centerImageSet);
    COPY_FIELD("frame", frame); COPY_FIELD("frame", frameSet);
    COPY_FIELD("backimage1", backImage1); COPY_FIELD("backimage1", backImage1Set);
    COPY_FIELD("backimage2", backImage2); COPY_FIELD("backimage2", backImage2Set);
    COPY_FIELD("autoshrink", autoShrink); COPY_FIELD("autoshrink", autoShrinkSet);
    COPY_FIELD("cropcontent", cropContent); COPY_FIELD("cropcontent", cropContentSet);
    COPY_FIELD("cropblack", cropBlack); COPY_FIELD("cropblack", cropBlackSet);
    COPY_FIELD("normalcolor", normalColor); COPY_FIELD("hovercolor", hoverColor); COPY_FIELD("presscolor", pressColor);
    COPY_FIELD("text", text); COPY_FIELD("style", style); COPY_FIELD("slidebegin", slideBegin); COPY_FIELD("slideend", slideEnd);
    COPY_FIELD("linesize", lineSize); COPY_FIELD("pagesize", pageSize);
#undef COPY_FIELD
    for (const QString& key : fields)
    {
        if ((isNoticePage(selectedPage) || isHudPage(selectedPage) || selectedPage == "joystick" || selectedPage == "skills" || selectedPage == "mapthumbnail" || ((selectedPage == "message" || selectedPage == "timer") && key == "minimumfont"))
            && original.rawValues.contains(key) && original.rawValues.value(key).isEmpty()
            && !proposed.rawValues.contains(key)) continue;
        if (proposed.rawValues.contains(key)) result.rawValues[key] = proposed.rawValues.value(key);
        else result.rawValues.remove(key);
    }
    return result;
}

bool MenuEditorWindow::canEditGeometry(const QString& owner, bool resizing) const
{
    if (previewModeCombo && previewModeCombo->currentIndex() == 1) return false;
    if ((selectedPage == "dialog" || selectedPage == "choose" || selectedPage == "message" || selectedPage == "timer") && !conversationCanvasIsEditable()) return false;
    if (selectedPage == "joystick" || selectedPage == "skills" || selectedPage == "npcinfo") return false;
    if (selectedPage == "mapthumbnail" && !mapThumbnailCanvasIsEditable(owner)) return false;
    if (isHudPage(selectedPage) && previewModeCombo && previewModeCombo->currentIndex() == 1)
    {
        if (toolTipPreviewCombo->currentIndex() > 0) return false;
        const auto window = const_cast<MenuEditorWindow*>(this)->propertiesForResource(currentDefinition.windowFile);
        const auto viewport = previewWindowSize();
        if (previewPanelRectangle(window, viewport).size() != QSize(window.width, window.height)) return false;
    }
    const auto fields = editableFields(owner);
    if (selectedPage == "partnerhead")
        for (const auto& component : currentDefinition.components)
            if (component.editorId == owner && component.name == "level") return false;
    return fields.contains("left") && fields.contains("top")
        && (!resizing || (fields.contains("width") && fields.contains("height")));
}

bool MenuEditorWindow::validatePageSave(const QString& target, QString& error)
{
    rebuildResourceContext();
    if (pageResource(selectedPage) != pageEntryResource)
    {
        error = tr("页面入口或主题已改变，请重新选择页面；待保存编辑保留。");
        return false;
    }
    updatePagePermissions();
    if (!canEditPage()) { error = pageRestriction; return false; }
    if (EditorAssetPath::logicalComparisonKey(target) != EditorAssetPath::logicalComparisonKey(pageTargetPath()))
    {
        error = tr("只能保存到当前页面的有效入口，不支持任意另存。");
        return false;
    }
    const auto definitionFingerprint = [this](const MenuDefinition& definition)
    {
        EditorSnapshot snapshot; snapshot.definition = definition; return snapshotFingerprint(snapshot);
    };
    if (historyIndex < 0 || definitionFingerprint(currentDefinition) != definitionFingerprint(history[historyIndex].definition))
    {
        error = tr("功能标识或结构已改变，拒绝保存。");
        return false;
    }
    return validateKnownMenu(error);
}

bool MenuEditorWindow::saveCurrentPage()
{
    return saveDocumentToPath(pageTargetPath(), true);
}

void MenuEditorWindow::setupToolBar()
{
    undoAction = new QAction(tr("撤销"), this);
    undoAction->setObjectName(QStringLiteral("menuEditorUndoAction"));
    redoAction = new QAction(tr("重做"), this);
    redoAction->setObjectName(QStringLiteral("menuEditorRedoAction"));
    saveAction = new QAction(tr("保存"), this);
    saveAction->setObjectName(QStringLiteral("menuEditorSaveAction"));
    refreshAction = new QAction(tr("刷新预览"), this);
    refreshAction->setObjectName(QStringLiteral("menuEditorRefreshAction"));

    pageCombo = new QComboBox(this);
    pageCombo->setObjectName("menuPageCombo");
    pageCombo->setAccessibleName(tr("游戏页面"));
    pageCombo->setEditable(true);
    pageCombo->setInsertPolicy(QComboBox::NoInsert);
    pageCombo->setMaxVisibleItems(12);
    pageCombo->lineEdit()->setPlaceholderText(tr("选择或搜索界面…"));
    pageCombo->completer()->setFilterMode(Qt::MatchContains);
    pageCombo->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    ui->toolBar->addWidget(new QLabel(tr("界面"), this));
    ui->toolBar->addWidget(pageCombo);
    themeCombo = new QComboBox(this);
    themeCombo->setObjectName("menuThemeCombo");
    themeCombo->addItem(tr("原版"), "original");
    themeCombo->addItem(tr("青玉"), "qingyu");
    themeCombo->setToolTip(tr("仅选择编辑对象，不修改游戏主题开关。未提供游戏应用设置时使用运行时默认原版。"));
    ui->toolBar->addWidget(new QLabel(tr("主题"), this));
    ui->toolBar->addWidget(themeCombo);
    saveLoadContextCombo = new QComboBox(this);
    saveLoadContextCombo->setObjectName("saveLoadContextCombo");
    saveLoadContextCombo->addItems({tr("游戏内存读档"), tr("标题存读档")});
    saveLoadContextCombo->setToolTip(tr("选择存读档的实际打开场景；标题按应用主题候选选源，游戏内按活动世界主题选源，不修改游戏设置。"));
    saveLoadContextAction = ui->toolBar->addWidget(saveLoadContextCombo);
    saveLoadContextAction->setVisible(false);
    connect(saveLoadContextCombo, &QComboBox::currentIndexChanged, this, [this](int index)
    {
        const bool previous = saveLoadAtTitle;
        if ((index == 1) == previous) return;
        if (confirmSaveIfModified() == SaveConfirmResult::Cancelled)
        {
            const QSignalBlocker blocker(saveLoadContextCombo);
            saveLoadContextCombo->setCurrentIndex(previous ? 1 : 0);
            return;
        }
        pageSwitchConfirmed = true;
        const auto guard = qScopeGuard([this]() { pageSwitchConfirmed = false; });
        saveLoadAtTitle = index == 1;
        if (!selectPage("saveload"))
        {
            saveLoadAtTitle = previous;
            const QSignalBlocker blocker(saveLoadContextCombo);
            saveLoadContextCombo->setCurrentIndex(previous ? 1 : 0);
        }
        refreshPageList();
    });
    pageContextLabel = new QLabel(this);
    pageContextLabel->setObjectName("menuPageContextLabel");
    pageContextLabel->setWordWrap(true);
    pageContextLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ui->outerLayout->insertWidget(2, pageContextLabel);
    connect(pageCombo, &QComboBox::currentIndexChanged, this, [this](int index)
    {
        if (index >= 0) selectPage(pageCombo->itemData(index).toString());
    });
    connect(pageCombo->lineEdit(), &QLineEdit::editingFinished, this, [this]()
    {
        const int index = pageCombo->findData(selectedPage);
        if (index < 0 || pageCombo->currentText() == pageCombo->itemText(index)) return;
        const QSignalBlocker blocker(pageCombo);
        pageCombo->setEditText(pageCombo->itemText(index));
    });
    connect(themeCombo, &QComboBox::currentIndexChanged, this, [this](int index)
    {
        const QString theme = themeCombo->itemData(index).toString();
        if (theme == selectedTheme) return;
        const QString previous = selectedTheme;
        const QString page = selectedPage;
        if (confirmSaveIfModified() == SaveConfirmResult::Cancelled)
        {
            const QSignalBlocker blocker(themeCombo);
            themeCombo->setCurrentIndex(themeCombo->findData(previous));
            return;
        }
        pageSwitchConfirmed = true;
        const auto switchGuard = qScopeGuard([this]() { pageSwitchConfirmed = false; });
        selectedTheme = theme;
        if (!page.isEmpty() && !selectPage(page))
        {
            selectedTheme = previous;
            const QSignalBlocker blocker(themeCombo);
            themeCombo->setCurrentIndex(themeCombo->findData(previous));
        }
        refreshPageList();
    });
    ui->toolBar->addSeparator();
    ui->toolBar->addAction(undoAction);
    ui->toolBar->addAction(redoAction);
    ui->toolBar->addSeparator();
    ui->toolBar->addAction(saveAction);

    ui->toolBar->addSeparator();
    ui->toolBar->addAction(refreshAction);
    bagGridAction = ui->toolBar->addAction(tr("背包阵列"));
    bagGridAction->setObjectName("menuBagGridAction");
    connect(bagGridAction, &QAction::triggered, this, &MenuEditorWindow::editBagGrid);
    toolTipPreviewCombo = new QComboBox(this);
    toolTipPreviewCombo->setObjectName("toolTipPreviewState");
    toolTipPreviewCombo->setToolTip(tr("仅切换预览；游戏仍决定实际显示状态。示例文字及分页不写入资源。"));
    toolTipPreviewAction = ui->toolBar->addWidget(toolTipPreviewCombo);
    toolTipPreviewAction->setObjectName("toolTipPreviewAction");
    connect(toolTipPreviewCombo, &QComboBox::currentIndexChanged, this, [this]() { updatePreview(); });

    optionViewCombo = new QComboBox(this);
    optionViewCombo->setObjectName("optionEditView");
    optionViewCombo->addItems({tr("主设置"), tr("作弊面板（关闭）"), tr("作弊面板（开启）")});
    optionViewCombo->setToolTip(tr("切换设置或作弊面板，检查对应外观。示例不保存；游戏中的功能和显示条件保持不变。"));
    optionViewAction = ui->toolBar->addWidget(optionViewCombo);
    optionControllerPreview = new QCheckBox(tr("控制器存在（预览）"), this);
    optionControllerPreview->setObjectName("optionControllerPreview");
    optionControllerPreview->setChecked(true);
    optionControllerAction = ui->toolBar->addWidget(optionControllerPreview);
    connect(optionViewCombo, &QComboBox::currentIndexChanged, this, [this]() { updatePreview(); });
    connect(optionControllerPreview, &QCheckBox::toggled, this, [this]() { updatePreview(); });

    setupPartnerHeadTools();
    setupConversationTools();

    undoAction->setShortcut(QKeySequence::Undo);
    redoAction->setShortcut(QKeySequence::Redo);
    connect(undoAction, &QAction::triggered, this, &MenuEditorWindow::onUndo);
    connect(redoAction, &QAction::triggered, this, &MenuEditorWindow::onRedo);
    connect(saveAction, &QAction::triggered, this, &MenuEditorWindow::onSaveFile);
    connect(refreshAction, &QAction::triggered, this, &MenuEditorWindow::onPreviewRefresh);
}

bool MenuEditorWindow::setAssetsBasePath(const QString& path)
{
    const Decision decision = prepareAssetsPathSwitch(path);
    if (decision == Decision::Cancelled || !resolveAssetsPathSwitch(decision))
        return false;
    commitAssetsPathSwitch(path);
    return true;
}

AssetsPathSwitchParticipant::Decision MenuEditorWindow::prepareAssetsPathSwitch(
    const QString& path) const
{
    const QString normalizedPath = path.trimmed().isEmpty()
        ? QString() : EditorAssetPath::normalizedAbsolutePath(path);
    if (!configuredAssetsBasePath.isEmpty() &&
        EditorAssetPath::logicalComparisonKey(configuredAssetsBasePath) ==
            EditorAssetPath::logicalComparisonKey(normalizedPath))
    {
        // Refresh dependency/profile data without discarding the active pack
        // inferred for the current document.
        return Decision::Ready;
    }
    if (!normalizedPath.isEmpty() && !QFileInfo(normalizedPath).isDir()) return Decision::Cancelled;
    const bool changingRoot = !configuredAssetsBasePath.isEmpty() &&
        EditorAssetPath::logicalComparisonKey(configuredAssetsBasePath) !=
            EditorAssetPath::logicalComparisonKey(normalizedPath);
    if (changingRoot && !selectedPage.isEmpty() && ResourcePackScanner::hasManifest(normalizedPath))
    {
        const auto candidate = MenuPageContext::resolve(normalizedPath);
        const QString candidateMenu = candidate.menuPageForRole(selectedPage, selectedTheme);
        if (!candidate.valid || (!candidateMenu.isEmpty() && (!candidate.canLoad(candidateMenu, selectedTheme, saveLoadAtTitle)
            || !canAdoptDocumentPath(candidate.source(candidateMenu, selectedTheme, saveLoadAtTitle).isEmpty()
                ? candidate.activeRoot + "/" + candidate.resource(candidateMenu, selectedTheme, saveLoadAtTitle) : candidate.source(candidateMenu, selectedTheme, saveLoadAtTitle)))))
        {
            pageContextLabel->setText(tr("目标资源包的当前页面不可读取，原文档与编辑状态保持不变。"));
            return Decision::Cancelled;
        }
    }
    if (changingRoot && hasUnsavedChanges)
    {
        const int result = QMessageBox::question(const_cast<MenuEditorWindow*>(this),
            tr("保存更改"), tr("菜单已修改，是否保存？"),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (result == QMessageBox::Cancel)
            return Decision::Cancelled;
        return result == QMessageBox::Yes ? Decision::Save : Decision::Discard;
    }

    return Decision::Ready;
}

bool MenuEditorWindow::resolveAssetsPathSwitch(Decision decision)
{
    if (decision == Decision::Cancelled)
        return false;
    if (decision == Decision::Save)
    {
        onSaveFile();
        return !hasUnsavedChanges;
    }
    return true;
}

void MenuEditorWindow::commitAssetsPathSwitch(const QString& path)
{
    const QString previousPage = selectedPage;
    const QString normalizedPath = path.trimmed().isEmpty()
        ? QString() : EditorAssetPath::normalizedAbsolutePath(path);
    if (!configuredAssetsBasePath.isEmpty() &&
        EditorAssetPath::logicalComparisonKey(configuredAssetsBasePath) ==
            EditorAssetPath::logicalComparisonKey(normalizedPath))
    {
        // The logical root may now resolve to a different link target. Keep
        // staged writes, but discard read-only component data from target A.
        componentIniCache.clear();
        rebuildResourceContext(currentFilePath);
        updatePagePermissions();
        refreshPageList();
        updatePreview();
        return;
    }
    const bool changingRoot = !configuredAssetsBasePath.isEmpty() &&
        EditorAssetPath::logicalComparisonKey(configuredAssetsBasePath) !=
            EditorAssetPath::logicalComparisonKey(normalizedPath);
    if (changingRoot)
    {
        // A document's staged local overrides are absolute paths owned by its
        // current writable pack. Do not carry them into another resource root.
        // The transaction resolved the old document first; then start a
        // clean document in the new context.
        hasUnsavedChanges = false;
        clearPageDocument();
        selectedSubComponentName.clear();
        selectedSubComponentPath.clear();
    }

    configuredAssetsBasePath = normalizedPath;
    assetsBasePath.clear();
    resourceRecoveryBlocked = false;
    resourceRecoveryErrors.clear();
    saveAction->setEnabled(true);
    rebuildResourceContext();
    refreshPageList();
    updatePagePermissions();
    if (!previousPage.isEmpty() && pageResources.valid) selectPage(previousPage);
}

QString MenuEditorWindow::currentAssetsPath() const
{
    return configuredAssetsBasePath;
}

QList<ProjectDocumentState> MenuEditorWindow::currentProjectDocuments() const
{
    if (currentFilePath.isEmpty())
        return {};
    return {{currentFilePath, ProjectDocumentType::Menu,
             hasUnsavedChanges}};
}

bool MenuEditorWindow::canAdoptDocumentPath(
    const QString& targetPath) const
{
    return !documentPathValidator ||
        documentPathValidator(currentFilePath, targetPath);
}

void MenuEditorWindow::rebuildResourceContext(const QString& preferredFilePath)
{
    Q_UNUSED(preferredFilePath);
    pageResources = MenuPageContext::resolve(configuredAssetsBasePath);
    assetsBasePath = pageResources.activeRoot;
    resourceRecoveryErrors = pageResources.recoveryErrors;
    resourceRecoveryBlocked = !resourceRecoveryErrors.isEmpty();
    if (resourceRecoveryBlocked) saveAction->setEnabled(false);

    previewCanvas->setResourceRoots(pageResources.readRoots, pageResources.contentRoots);
    propertyEditor->setAssetsBasePath(assetsBasePath);
    propertyEditor->setAssetDropRoots(pageResources.allReadRoots());

    QComboBox* assetRootCombo = findChild<QComboBox*>("assetRootCombo");
    if (assetRootCombo)
    {
        const bool blocked = assetRootCombo->blockSignals(true);
        assetRootCombo->clear();
        for (int i = 0; i < pageResources.allReadRoots().size(); ++i)
        {
            const QString label = i == 0 &&
                EditorAssetPath::logicalComparisonKey(pageResources.allReadRoots()[i]) ==
                    EditorAssetPath::logicalComparisonKey(assetsBasePath)
                ? tr("本地 UI — %1").arg(QFileInfo(pageResources.allReadRoots()[i]).fileName())
                : tr("只读回退 %1 — %2").arg(i).arg(QFileInfo(pageResources.allReadRoots()[i]).fileName());
            assetRootCombo->addItem(label, pageResources.allReadRoots()[i]);
        }
        assetRootCombo->blockSignals(blocked);
        if (assetRootCombo->count() > 0)
            assetBrowser->setAssetsBasePath(assetRootCombo->itemData(0).toString());
    }
    else
    {
        assetBrowser->setAssetsBasePath(assetsBasePath);
    }
}

bool MenuEditorWindow::resolveLocalResourcePath(const QString& resourcePath, QString& absolutePath) const
{
    // Local component/menu overrides are editor-managed writes. Keep their
    // targets physically contained even though formal reads follow links.
    return EditorAssetPath::resolveInside(assetsBasePath, resourcePath, absolutePath);
}

bool MenuEditorWindow::resolveReadableResourcePath(const QString& resourcePath, QString& absolutePath) const
{
    absolutePath = pageResources.readable(resourcePath);
    return !absolutePath.isEmpty();
}

bool MenuEditorWindow::resourcePathForFile(const QString& filePath, QString& resourcePath) const
{
    const QString absolute = EditorAssetPath::normalizedAbsolutePath(filePath);
    for (const QString& root : pageResources.allReadRoots())
    {
        if (EditorAssetPath::makeLogicalResourceRelativePath(
                root, absolute, resourcePath))
            return true;
    }
    resourcePath.clear();
    return false;
}

ComponentIniProperties MenuEditorWindow::propertiesForResource(const QString& resourcePath)
{
    ComponentIniProperties properties;
    QString localPath;
    if (resolveLocalResourcePath(resourcePath, localPath))
    {
        auto modified = modifiedComponentInis.constFind(localPath);
        if (modified != modifiedComponentInis.constEnd())
            return modified->properties;
    }

    if (selectedPage == "dicegame") return diceProperties();
    QString sourcePath;
    if (resolveReadableResourcePath(resourcePath, sourcePath))
        readComponentIniProperties(sourcePath, properties);
    else if(selectedPage=="skills"&&normalizedResourcePath(resourcePath)=="ini/ui/mobile/skills/minimap.ini")
        properties.stretch=true;
    return properties;
}

void MenuEditorWindow::clearModifiedOwner(const QString& ownerId)
{
    if (ownerId.isEmpty())
        return;
    for (auto it = modifiedComponentInis.begin(); it != modifiedComponentInis.end(); )
    {
        it->owners.remove(ownerId);
        it->ownerComponentTypes.remove(ownerId);
        if (it->owners.isEmpty())
            it = modifiedComponentInis.erase(it);
        else
            ++it;
    }
}

bool MenuEditorWindow::canReuseComponentResource(const QString& ownerId,
                                                const QString& resourcePath,
                                                ExternalResourceReferences* externalReferences) const
{
    const auto matches = [&resourcePath](const QString& reference)
    {
        return normalizedResourcePath(reference).compare(resourcePath, Qt::CaseInsensitive) == 0;
    };
    int references = matches(currentDefinition.windowFile) ? 1 : 0;
    for (const auto& component : currentDefinition.components)
    {
        if (matches(component.file)) ++references;
        if (component.type == "Scrollbar")
        {
            const auto props = const_cast<MenuEditorWindow*>(this)->propertiesForResource(component.file);
            if (!props.slideBtn.isEmpty() && matches(normalizedResourcePath(QFileInfo(normalizedResourcePath(component.file)).path() + "/" + props.slideBtn))) ++references;
        }
    }
    for (const auto& menu : currentDefinition.subMenus)
    {
        if (matches(menu.windowFile)) ++references;
        for (const auto& component : menu.components)
            if (matches(component.file)) ++references;
    }
    if (references != 1) return false;

    QString localPath;
    if (!resolveLocalResourcePath(resourcePath, localPath)) return false;
    const auto staged = modifiedComponentInis.constFind(localPath);
    if (staged != modifiedComponentInis.constEnd() && staged->owners != QSet<QString>{ownerId})
        return false;
    if (!QFileInfo::exists(localPath))
        // A read-only history snapshot is not ownership of a new local file.
        return staged != modifiedComponentInis.constEnd() && staged->stagedForWrite;
    QString source;
    if (!resolveReadableResourcePath(resourcePath, source)
        || EditorAssetPath::comparisonKey(source) != EditorAssetPath::comparisonKey(localPath))
        return false;

    ExternalResourceReferences localReferences;
    auto& external = externalReferences ? *externalReferences : localReferences;
    if (!external.collected) external = collectExternalResourceReferences();
    if (ownerId.endsWith("/slideBtn"))
        for (const auto& parent : currentDefinition.components)
            if (parent.editorId + "/slideBtn" == ownerId)
            {
                if (!canReuseComponentResource(parent.editorId, parent.file, &external)) return false;
                QString parentPath;
                if (!resolveLocalResourcePath(parent.file, parentPath)) return false;
                auto sources = external.sources.value(resourcePath.toCaseFolded());
                sources.remove(EditorAssetPath::comparisonKey(parentPath));
                return external.complete && sources.isEmpty();
            }
    return external.complete && !external.paths.contains(resourcePath.toCaseFolded());
}

MenuEditorWindow::ExternalResourceReferences MenuEditorWindow::collectExternalResourceReferences() const
{
    ExternalResourceReferences result;
    result.collected = true;
    auto addReference = [&result](const QString& reference, const QString& source)
    {
        const QString normalized = normalizedResourcePath(reference);
        if (!normalized.isEmpty())
        {
            result.paths.insert(normalized.toCaseFolded());
            result.sources[normalized.toCaseFolded()].insert(EditorAssetPath::comparisonKey(source));
        }
    };
    // A reference snapshot belongs only to one edit or save operation.
    for (const QString& root : pageResources.allReadRoots())
    {
        QDirIterator files(root, {"*.ini"}, QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext())
        {
            const QString path = files.next();
            if (EditorAssetPath::comparisonKey(path) == EditorAssetPath::comparisonKey(currentFilePath))
                continue;
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            {
                result.complete = false;
                return result;
            }
            QTextStream stream(&file);
            stream.setEncoding(QStringConverter::Utf8);
            QString section;
            while (!stream.atEnd())
            {
                const QString line = stream.readLine();
                QString key, value;
                if (parseRuntimeIniSection(line, section))
                    section = section.toLower();
                else if (parseRuntimeIniEntry(line, key, value))
                {
                    if ((section == "menu" && key == "window")
                        || ((section.startsWith("component") || section.startsWith("submenu")) && key == "file"))
                        addReference(value, path);
                    if (section == "init" && key == "slidebtn")
                    {
                        QString relative;
                        if (EditorAssetPath::makeLogicalResourceRelativePath(root, path, relative))
                            addReference(QFileInfo(relative).path() + "/" + value, path);
                    }
                }
            }
            if (stream.status() != QTextStream::Ok)
            {
                result.complete = false;
                return result;
            }
        }
    }
    return result;
}

bool MenuEditorWindow::stageComponentProperties(const QString& ownerId,
                                                const QString& resourcePath,
                                                const ComponentIniProperties& proposed,
                                                const QString& componentType, bool forceCopy,
                                                ExternalResourceReferences* externalReferences)
{
    if (!canEditPage()) return false;
    if (selectedPage == "npcinfo") return ownerId == "menu/window" && stageNpcInfoProperties(proposed);
    const ComponentIniProperties original = isNoticePage(selectedPage) && ownerId == "menu/window" ? noticeWindowProperties() : propertiesForResource(resourcePath);
    if (selectedPage == "tooltip" && pageResources.manifest.uiProfile == "JXQY2" && !effectiveQingyuTheme()
        && proposed.font != original.font && editableFields(ownerId).contains("font"))
    {
        for (const auto& component : currentDefinition.components)
        {
            if (component.editorId != ownerId || component.type != "Label") continue;
            const int maximum = component.name == "name" || component.name == "cost" ? 18 : 16;
            if (proposed.font > maximum)
            {
                pageContextLabel->setText(tr("当前提示文字的运行时字号上限为 %1，修改未应用。").arg(maximum));
                return false;
            }
        }
    }
    if (selectedPage == "state" && componentType == "Label" && pageResources.manifest.uiProfile == "JXQY2" && !effectiveQingyuTheme()
        && editableFields(ownerId).contains("font") && proposed.font != original.font && proposed.font > 16)
    {
        pageContextLabel->setText(tr("当前提示文字的运行时字号上限为 %1，修改未应用。").arg(16));
        return false;
    }
    ComponentIniProperties hudValue = proposed;
    if (selectedPage == "joystick" && !stageJoystickFields(ownerId, original, hudValue)) return false;
    if (selectedPage == "skills" && !stageSkillsFields(ownerId, original, hudValue)) return false;
    if (isHudPage(selectedPage) && !stageFixedDisplayFields(ownerId, original, hudValue)) return false;
    if (selectedPage == "mapthumbnail" && !stageMapThumbnailFields(ownerId, original, hudValue)) return false;
    ComponentIniProperties properties = allowedProperties(ownerId, original, hudValue);
    if (selectedPage == "titleteam" && !stageTitleTeamFields(ownerId, original, properties)) return false;
    if (isNoticePage(selectedPage) && !stageNoticeFields(ownerId, original, properties)) return false;
    if ((selectedPage == "dialog" || selectedPage == "choose" || selectedPage == "message" || selectedPage == "timer") && !stageConversationFields(ownerId, original, properties)) return false;
    if (!forceCopy && !applyingBagGrid)
    {
        EditorSnapshot oldValue, newValue;
        oldValue.modifiedInis["value"].properties = original;
        newValue.modifiedInis["value"].properties = properties;
        if (snapshotFingerprint(oldValue) == snapshotFingerprint(newValue)) return true;
    }
    ExternalResourceReferences childReferences;
    MenuComponentDefinition* scrollbarParent = nullptr;
    if ((selectedPage == "option" || selectedPage == "buysell") && ownerId.endsWith("/slideBtn"))
    {
        if (!externalReferences) externalReferences = &childReferences;
        for (auto& component : currentDefinition.components)
            if (component.editorId + "/slideBtn" == ownerId) { scrollbarParent = &component; break; }
        if (!scrollbarParent || scrollbarParent->type != "Scrollbar") return false;
        if (!canReuseComponentResource(scrollbarParent->editorId, scrollbarParent->file, externalReferences)
            && !stageComponentProperties(scrollbarParent->editorId, scrollbarParent->file,
                propertiesForResource(scrollbarParent->file), "Scrollbar", true, externalReferences)) return false;
    }
    if (selectedPage == "titleteam" && resourcePath.startsWith("ini/ui/titleteam/runtime-default/")) forceCopy = true;
    if (isNoticePage(selectedPage) && resourcePath.startsWith("ini/ui/"+selectedPage+"/runtime-default/")) forceCopy = true;
    if (selectedPage == "controllerhelp" || selectedPage == "npcinfo" || selectedPage == "partnerhead" || selectedPage == "titleteam" || isNoticePage(selectedPage))
        for (auto& component : currentDefinition.components)
            if (component.editorId == ownerId)
            {
                forceCopy = forceCopy || component.runtimeDefault;
                component.runtimeDefault = false;
                break;
            }
    QString optionName;
    bool materializeCounterpart = false;
    ExternalResourceReferences optionReferences;
    if (selectedPage == "option")
    {
        if (!externalReferences) externalReferences = &optionReferences;
        for (auto& component : currentDefinition.components)
        {
            if (component.editorId != ownerId || !OptionLayout::find(component.name.toStdString())) continue;
            optionName = component.name;
            const bool legacyCheat = component.name == "cheatSettingsButton" && !component.runtimeDefault && component.optionLayout.isEmpty();
            if (component.optionLayout.isEmpty()) component.optionLayout = legacyCheat ? OptionLayout::ResourceFooter : OptionLayout::Explicit;
            materializeCounterpart = legacyCheat;
            forceCopy = forceCopy || component.runtimeDefault;
            component.runtimeDefault = false;
            break;
        }
    }
    QString normalized = normalizedResourcePath(resourcePath);
    QString localPath;
    if (ownerId.isEmpty() || normalized.isEmpty() ||
        !resolveLocalResourcePath(normalized, localPath))
    {
        return false;
    }

    QString sourcePath;
    resolveReadableResourcePath(normalized, sourcePath);
    const auto previous = modifiedComponentInis.constFind(localPath);
    if (previous != modifiedComponentInis.constEnd()) sourcePath = previous->sourcePath;
    if (sourcePath.isEmpty())
        sourcePath = localPath;

    // Known game menus use copy-on-write so another menu's shared INI is untouched.
    if ((!selectedPage.isEmpty())
        && !(selectedPage == "skills" && ownerId == "skills/minimap")
        && (forceCopy || !canReuseComponentResource(ownerId, normalized, externalReferences)))
    {
        QString* reference = nullptr;
        if (ownerId == "menu/window") reference = &currentDefinition.windowFile;
        for (auto& component : currentDefinition.components)
            if (component.editorId == ownerId) reference = &component.file;
        if (reference || scrollbarParent)
        {
            const QFileInfo file(normalized);
            const QString target = (selectedPage == "titleteam" ? QString("ini/ui/titleteam") : isNoticePage(selectedPage) ? "ini/ui/"+selectedPage : file.path()) + "/component-edit-"
                + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".ini";
            if (!resolveLocalResourcePath(target, localPath)) return false;
            normalized = target;
            if (reference) *reference = normalized;
        }
    }

    if (scrollbarParent)
    {
        QString parentPath;
        if (!resolveLocalResourcePath(scrollbarParent->file, parentPath)) return false;
        auto parentProperties = propertiesForResource(scrollbarParent->file);
        parentProperties.slideBtn = QDir(QFileInfo(parentPath).absolutePath()).relativeFilePath(localPath);
        parentProperties.rawValues["slidebtn"] = parentProperties.slideBtn;
        auto& parent = modifiedComponentInis[parentPath];
        if (parent.sourcePath.isEmpty()) parent.sourcePath = pageResources.readable(scrollbarParent->file);
        parent.properties = parentProperties;
        parent.resourcePath = scrollbarParent->file;
        parent.owners.insert(scrollbarParent->editorId);
        parent.ownerComponentTypes[scrollbarParent->editorId] = "Scrollbar";
        parent.stagedForWrite = true;
        if (ownerId == selectedOwnerId()) selectedSubComponentPath = normalized;
    }

    clearModifiedOwner(ownerId);
    ModifiedComponentIni& modified = modifiedComponentInis[localPath];
    modified.properties = properties;
    modified.resourcePath = normalized;
    modified.sourcePath = sourcePath;
    modified.owners.insert(ownerId);
    modified.ownerComponentTypes.insert(ownerId, componentType);
    modified.stagedForWrite = true;
    componentIniCache.remove(localPath);
    if (ownerId == selectedOwnerId() && (!selectedPage.isEmpty()))
    {
        if (ownerId == "menu/window")
            propertyEditor->setMenuWindowProperties(normalized, properties.width, properties.height,
                properties.image, properties.bitmap, properties.align, properties.alignX, properties.alignY, properties.stretch);
        for (const auto& component : currentDefinition.components)
            if (component.editorId == ownerId)
                propertyEditor->setDefinitionProperties(component.name, component.file, component.bind, component.format);
        if (ui->componentTree->currentItem()) ui->componentTree->currentItem()->setText(2, normalized);
    }
    if (materializeCounterpart)
    {
        for (const auto& component : currentDefinition.components)
        {
            if (component.name != "experienceMultiplierButton" || !component.optionLayout.isEmpty()) continue;
            const QString counterpartId = component.editorId, counterpartFile = component.file;
            if (!stageComponentProperties(counterpartId, counterpartFile, propertiesForResource(counterpartFile),
                "FlatTextButton", true, externalReferences)) return false;
            break;
        }
    }
    if (!optionName.isEmpty()) prepareOptionDefaults();
    return true;
}

QString MenuEditorWindow::selectedOwnerId() const
{
    if (selectedItemType == SelectedItemType::MenuWindow)
        return "menu/window";
    if (selectedItemType == SelectedItemType::Component &&
        selectedComponentIndex >= 0 && selectedComponentIndex < currentDefinition.components.size())
    {
        return currentDefinition.components[selectedComponentIndex].editorId;
    }
    if (selectedItemType == SelectedItemType::SubComponent &&
        selectedComponentIndex >= 0 && selectedComponentIndex < currentDefinition.components.size())
    {
        return currentDefinition.components[selectedComponentIndex].editorId + "/slideBtn";
    }
    if (selectedItemType == SelectedItemType::SubComponent &&
        selectedSubMenuIndex >= 0 && selectedSubMenuIndex < currentDefinition.subMenus.size() &&
        selectedSubMenuComponentIndex >= 0 &&
        selectedSubMenuComponentIndex < currentDefinition.subMenus[selectedSubMenuIndex].components.size())
    {
        return currentDefinition.subMenus[selectedSubMenuIndex]
            .components[selectedSubMenuComponentIndex].editorId + "/slideBtn";
    }
    if (selectedItemType == SelectedItemType::SubMenu &&
        selectedSubMenuIndex >= 0 && selectedSubMenuIndex < currentDefinition.subMenus.size())
    {
        return currentDefinition.subMenus[selectedSubMenuIndex].editorId + "/window";
    }
    if (selectedItemType == SelectedItemType::SubMenuComponent &&
        selectedSubMenuIndex >= 0 && selectedSubMenuIndex < currentDefinition.subMenus.size() &&
        selectedSubMenuComponentIndex >= 0 &&
        selectedSubMenuComponentIndex < currentDefinition.subMenus[selectedSubMenuIndex].components.size())
    {
        return currentDefinition.subMenus[selectedSubMenuIndex]
            .components[selectedSubMenuComponentIndex].editorId;
    }
    return {};
}

void MenuEditorWindow::markSubMenuModified(int index)
{
    if (index >= 0 && index < currentDefinition.subMenus.size())
        modifiedSubMenuIds.insert(currentDefinition.subMenus[index].editorId);
}

ClosePlan MenuEditorWindow::prepareCloseTransaction() const
{
    ClosePlan plan;
    if (!hasUnsavedChanges)
    {
        plan.decisions.append(CloseDecision::Ready);
        return plan;
    }

    const int result = QMessageBox::question(
        const_cast<MenuEditorWindow*>(this),
        tr("保存更改"),
        tr("菜单已修改，是否保存？"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (result == QMessageBox::Cancel)
        plan.decisions.append(CloseDecision::Cancelled);
    else if (result == QMessageBox::Yes)
        plan.decisions.append(CloseDecision::Save);
    else
        plan.decisions.append(CloseDecision::Discard);
    return plan;
}

bool MenuEditorWindow::resolveCloseTransaction(const ClosePlan& plan)
{
    if (plan.decisions.size() != 1 || plan.isCancelled())
        return false;
    if (plan.decisions.front() == CloseDecision::Save)
    {
        onSaveFile();
        return !hasUnsavedChanges;
    }
    return true;
}

void MenuEditorWindow::commitCloseTransaction(const ClosePlan& plan)
{
    if (plan.decisions.size() != 1 || plan.isCancelled())
        return;
    allowPreparedClose();
}

MenuEditorWindow::SaveConfirmResult MenuEditorWindow::confirmSaveIfModified()
{
    if (!hasUnsavedChanges)
        return SaveConfirmResult::Discarded;

    int result = QMessageBox::question(this,
        tr("保存更改"),
        tr("菜单已修改，是否保存？"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);

    if (result == QMessageBox::Cancel)
        return SaveConfirmResult::Cancelled;

    if (result == QMessageBox::No)
        return SaveConfirmResult::Discarded;

    onSaveFile();
    if (hasUnsavedChanges)
        return SaveConfirmResult::Cancelled;

    return SaveConfirmResult::Saved;
}

void MenuEditorWindow::closeEvent(QCloseEvent* event)
{
    if (consumePreparedClose())
    {
        emit documentClosed();
        event->accept();
        return;
    }

    if (confirmSaveIfModified() == SaveConfirmResult::Cancelled)
    {
        event->ignore();
        return;
    }
    emit documentClosed();
    event->accept();
}

bool MenuEditorWindow::openMenuDefinition(const QString& filePath)
{
    if (filePath.trimmed().isEmpty() ||
        !canAdoptDocumentPath(filePath))
    {
        return false;
    }
    if (!pageSwitchConfirmed && confirmSaveIfModified() == SaveConfirmResult::Cancelled)
        return false;

    const MenuDefinition previousDefinition = currentDefinition;
    const auto previousModified = modifiedComponentInis;
    const auto previousCache = componentIniCache;
    const auto previousSubMenus = modifiedSubMenuIds;
    const QStringList previousLines = originalMenuFileLines;
    const QString previousPath = currentFilePath;
    const QString previousTitle = windowTitle();
    const bool previousDirty = hasUnsavedChanges;
    const QString previousPage = selectedPage;
    const QString previousTheme = selectedTheme;
    const QString previousEntry = pageEntryResource;
    const QString previousIssue = documentStructureIssue;
    const QByteArray previousContext = documentContextFingerprint;
    const QString previousAssetsBasePath = assetsBasePath;

    rebuildResourceContext(filePath);
    modifiedComponentInis.clear();
    componentIniCache.clear();
    modifiedSubMenuIds.clear();
    hasUnsavedChanges = false;
    if (!loadFromMenuFile(filePath))
    {
        selectedPage = previousPage;
        selectedTheme = previousTheme;
        pageEntryResource = previousEntry;
        documentStructureIssue = previousIssue;
        documentContextFingerprint = previousContext;
        currentDefinition = previousDefinition;
        modifiedComponentInis = previousModified;
        componentIniCache = previousCache;
        modifiedSubMenuIds = previousSubMenus;
        originalMenuFileLines = previousLines;
        currentFilePath = previousPath;
        hasUnsavedChanges = previousDirty;
        setWindowTitle(previousTitle);
        assetsBasePath = previousAssetsBasePath;
        rebuildResourceContext(previousPath);
        updateComponentTree();
        updatePreview();
        return false;
    }
    currentFilePath = EditorAssetPath::normalizedAbsolutePath(filePath);
    selectedPage = documentPage(currentFilePath);
    if (selectedPage.isEmpty())
    {
        selectedTheme = selectedTheme == "qingyu" ? "original" : "qingyu";
        selectedPage = documentPage(currentFilePath);
        if (selectedPage.isEmpty()) selectedTheme = previousTheme;
        const QSignalBlocker blocker(themeCombo);
        themeCombo->setCurrentIndex(themeCombo->findData(selectedTheme));
    }
    pageEntryResource = pageResource(selectedPage);
    documentContextFingerprint = pageContextFingerprint();
    prepareOptionDefaults();
    preparePartnerHeadDefaults();
    prepareTitleTeamDefaults();
    prepareNoticeDefaults();
    prepareNpcInfoDefaults();
    prepareControllerHelpDefaults();
    prepareDiceProperties();
    prepareSkillsControls();
    updatingFromCode = true;
    ui->menuNameEdit->setText(currentDefinition.menuName);
    ui->menuVisibleCheck->setChecked(currentDefinition.visible);
    updatingFromCode = false;
    updateComponentTree();
    updatePreview();
    setWindowTitle(tr("菜单编辑器 - ") + QFileInfo(filePath).fileName());
    resetHistory();
    if(selectedPage=="controllerhelp" || selectedPage=="dicegame")recordControllerHelpDiskState();
    updatePagePermissions();
    refreshPageList();
    return true;
}


void MenuEditorWindow::clearPageDocument()
{
    if (confirmSaveIfModified() == SaveConfirmResult::Cancelled) return;

    currentDefinition = MenuDefinition();
    selectedPage.clear();
    pageEntryResource.clear();
    documentStructureIssue.clear();
    documentContextFingerprint.clear();
    currentFilePath.clear();
    modifiedComponentInis.clear();
    componentIniCache.clear();
    modifiedSubMenuIds.clear();
    originalMenuFileLines.clear();
    hasUnsavedChanges = false;
    selectedComponentIndex = -1;
    selectedSubMenuIndex = -1;
    selectedSubMenuComponentIndex = -1;
    selectedItemType = SelectedItemType::None;

    updatingFromCode = true;
    ui->menuNameEdit->clear();
    ui->menuVisibleCheck->setChecked(false);
    propertyEditor->clearAll();
    updatingFromCode = false;

    ui->componentTree->clear();
    previewCanvas->setSelectedComponent({});
    previewCanvas->clear();
    setWindowTitle(tr("菜单编辑器"));
    resetHistory();
    if(selectedPage=="controllerhelp" || selectedPage=="dicegame")recordControllerHelpDiskState();
}

void MenuEditorWindow::onAddComponent()
{
    updatePropertyEditor();
}

void MenuEditorWindow::onAddSubMenu()
{
    updatePropertyEditor();
}

void MenuEditorWindow::onRemoveComponent()
{
    updatePropertyEditor();
}

void MenuEditorWindow::onComponentSelected(QTreeWidgetItem* current, QTreeWidgetItem* previous)
{
    Q_UNUSED(previous);
    if (selectedPage == "dicegame")
    {
        selectedComponentIndex = current && current->data(0,Qt::UserRole).isValid() ? current->data(0,Qt::UserRole).toInt() : -1;
        selectedItemType = selectedComponentIndex >= 0 ? SelectedItemType::Component : SelectedItemType::None;
        updatePropertyEditor(); return;
    }

    if (current == nullptr)
    {
        selectedComponentIndex = -1;
        selectedSubMenuIndex = -1;
        selectedSubMenuComponentIndex = -1;
        selectedItemType = SelectedItemType::None;
        propertyEditor->clearAll();
        previewCanvas->setSelectedComponent("");
        return;
    }

    QTreeWidgetItem* parentItem = current->parent();
    QTreeWidgetItem* grandParentItem = parentItem ? parentItem->parent() : nullptr;
    QTreeWidgetItem* topLevelItem = grandParentItem
        ? grandParentItem : (parentItem ? parentItem : current);
    int topLevelIndex = ui->componentTree->indexOfTopLevelItem(topLevelItem);

    int componentCount = currentDefinition.components.size();

    if (grandParentItem != nullptr)
    {
        const int subMenuIndex = topLevelIndex - componentCount - 1;
        const int subMenuComponentIndex = grandParentItem->indexOfChild(parentItem);
        if (topLevelIndex >= componentCount + 1 &&
            subMenuIndex >= 0 && subMenuIndex < currentDefinition.subMenus.size() &&
            subMenuComponentIndex >= 0 &&
            subMenuComponentIndex < currentDefinition.subMenus[subMenuIndex].components.size() &&
            parentItem->indexOfChild(current) == 0 && current->text(1) == "slideBtn")
        {
            selectedItemType = SelectedItemType::SubComponent;
            selectedComponentIndex = -1;
            selectedSubMenuIndex = subMenuIndex;
            selectedSubMenuComponentIndex = subMenuComponentIndex;
            selectedSubComponentName = "slideBtn";

            const auto& parentDef = currentDefinition.subMenus[subMenuIndex]
                .components[subMenuComponentIndex];
            const ComponentIniProperties parentProps = propertiesForResource(parentDef.file);
            if (!parentProps.slideBtn.isEmpty())
            {
                const QString parentResource = normalizedResourcePath(parentDef.file);
                selectedSubComponentPath = normalizedResourcePath(QDir::cleanPath(
                    QFileInfo(parentResource).path() + "/" + parentProps.slideBtn));
            }
            else
            {
                selectedSubComponentPath.clear();
            }
        }
        else
        {
            selectedItemType = SelectedItemType::None;
            selectedComponentIndex = -1;
            selectedSubMenuIndex = -1;
            selectedSubMenuComponentIndex = -1;
            selectedSubComponentName.clear();
            selectedSubComponentPath.clear();
        }
    }
    else if (parentItem == nullptr)
    {
        if (topLevelIndex == 0)
        {
            selectedItemType = SelectedItemType::MenuWindow;
            selectedComponentIndex = -1;
            selectedSubMenuIndex = -1;
            selectedSubMenuComponentIndex = -1;
            selectedSubComponentName.clear();
            selectedSubComponentPath.clear();
        }
        else if (topLevelIndex <= componentCount)
        {
            selectedItemType = SelectedItemType::Component;
            selectedComponentIndex = topLevelIndex - 1;
            selectedSubMenuIndex = -1;
            selectedSubMenuComponentIndex = -1;
            selectedSubComponentName.clear();
            selectedSubComponentPath.clear();
        }
        else
        {
            selectedItemType = SelectedItemType::SubMenu;
            selectedComponentIndex = -1;
            selectedSubMenuIndex = topLevelIndex - componentCount - 1;
            selectedSubMenuComponentIndex = -1;
            selectedSubComponentName.clear();
            selectedSubComponentPath.clear();
        }
    }
    else
    {
        int parentTopLevelIndex = ui->componentTree->indexOfTopLevelItem(parentItem);

        if (parentTopLevelIndex >= 1 && parentTopLevelIndex <= componentCount)
        {
            int childIndex = parentItem->indexOfChild(current);
            if (childIndex == 0 && current->text(1) == "slideBtn")
            {
                selectedItemType = SelectedItemType::SubComponent;
                selectedComponentIndex = parentTopLevelIndex - 1;
                selectedSubMenuIndex = -1;
                selectedSubMenuComponentIndex = -1;
                selectedSubComponentName = "slideBtn";

                const auto& parentDef = currentDefinition.components[selectedComponentIndex];
                const ComponentIniProperties parentProps = propertiesForResource(parentDef.file);
                if (!parentProps.slideBtn.isEmpty())
                {
                    QString parentResource = normalizedResourcePath(parentDef.file);
                    QString slideResource = QDir::cleanPath(
                        QFileInfo(parentResource).path() + "/" + parentProps.slideBtn);
                    selectedSubComponentPath = normalizedResourcePath(slideResource);
                }
                else
                {
                    selectedSubComponentPath.clear();
                }
            }
            else
            {
                selectedItemType = SelectedItemType::None;
                selectedComponentIndex = -1;
                selectedSubMenuIndex = -1;
                selectedSubMenuComponentIndex = -1;
                selectedSubComponentName.clear();
                selectedSubComponentPath.clear();
            }
        }
        else if (parentTopLevelIndex >= componentCount + 1)
        {
            selectedItemType = SelectedItemType::SubMenuComponent;
            selectedComponentIndex = -1;
            selectedSubMenuIndex = parentTopLevelIndex - componentCount - 1;
            selectedSubMenuComponentIndex = parentItem->indexOfChild(current);
            selectedSubComponentName.clear();
            selectedSubComponentPath.clear();
        }
        else
        {
            selectedItemType = SelectedItemType::None;
            selectedComponentIndex = -1;
            selectedSubMenuIndex = -1;
            selectedSubMenuComponentIndex = -1;
            selectedSubComponentName.clear();
            selectedSubComponentPath.clear();
        }
    }

    updatePropertyEditor();

    QString editorId;

    if (selectedItemType == SelectedItemType::Component
        && selectedComponentIndex >= 0
        && selectedComponentIndex < currentDefinition.components.size())
    {
        editorId = currentDefinition.components[selectedComponentIndex].editorId;
    }
    else if (selectedItemType == SelectedItemType::SubComponent
        && selectedComponentIndex >= 0
        && selectedComponentIndex < currentDefinition.components.size())
    {
        editorId = currentDefinition.components[selectedComponentIndex].editorId + "/slideBtn";
    }
    else if (selectedItemType == SelectedItemType::SubComponent
        && selectedSubMenuIndex >= 0
        && selectedSubMenuIndex < currentDefinition.subMenus.size()
        && selectedSubMenuComponentIndex >= 0
        && selectedSubMenuComponentIndex < currentDefinition.subMenus[selectedSubMenuIndex].components.size())
    {
        editorId = currentDefinition.subMenus[selectedSubMenuIndex]
            .components[selectedSubMenuComponentIndex].editorId + "/slideBtn";
    }
    else if (selectedItemType == SelectedItemType::SubMenuComponent
        && selectedSubMenuIndex >= 0
        && selectedSubMenuIndex < currentDefinition.subMenus.size()
        && selectedSubMenuComponentIndex >= 0
        && selectedSubMenuComponentIndex < currentDefinition.subMenus[selectedSubMenuIndex].components.size())
    {
        editorId = currentDefinition.subMenus[selectedSubMenuIndex]
            .components[selectedSubMenuComponentIndex].editorId;
    }

    previewCanvas->setSelectedComponent(editorId);
    updatePreview();
}

void MenuEditorWindow::onPropertyChanged()
{
    if (updatingFromCode || !canEditPage() || selectedItemType == SelectedItemType::None)
        return;
    if (editableFields(selectedOwnerId()).isEmpty()) { updatePropertyEditor(); return; }

    if (selectedPage == "dicegame") { applyDicePropertyEdit(); return; }
    if(selectedPage=="controllerhelp")
    {
        const auto before=currentSnapshotFingerprint();
        if(!stageControllerHelpProperties(propertyEditor->getProperties())){updatePropertyEditor();return;}
        if(before!=currentSnapshotFingerprint())markModified();
        updatePreview();return;
    }
    if (selectedItemType == SelectedItemType::MenuWindow)
    {
        if (!currentDefinition.windowFile.isEmpty())
        {
            ComponentIniProperties windowProps = selectedPage == "npcinfo" ? npcInfoProperties() : isNoticePage(selectedPage) ? noticeWindowProperties() : propertiesForResource(currentDefinition.windowFile);
            windowProps.width = propertyEditor->getMenuWindowWidth();
            windowProps.height = propertyEditor->getMenuWindowHeight();
            windowProps.image = propertyEditor->getMenuWindowImage();
            windowProps.bitmap = propertyEditor->getMenuWindowBitmap();
            windowProps.align = propertyEditor->getMenuWindowAlign();
            windowProps.alignX = propertyEditor->getMenuWindowAlignX();
            windowProps.alignY = propertyEditor->getMenuWindowAlignY();
            windowProps.stretch = propertyEditor->getMenuWindowStretch();
            windowProps.stretchSet = true;
            windowProps.rawValues = propertyEditor->getProperties().rawValues;
            if (!stageComponentProperties("menu/window", currentDefinition.windowFile, windowProps, "MenuWindow"))
            { updatePropertyEditor(); return; }
        }
    }
    else if (selectedItemType == SelectedItemType::Component)
    {
        if (selectedComponentIndex < 0 || selectedComponentIndex >= currentDefinition.components.size())
            return;

        const auto& definition = currentDefinition.components[selectedComponentIndex];
        if (!definition.file.isEmpty())
        {
            const auto previous = propertiesForResource(definition.file);
            const auto edited = propertyEditor->getProperties();
            const auto snapshot = createSnapshot();
            if (!stageComponentProperties(definition.editorId, definition.file, edited, definition.type))
            { updatePropertyEditor(); return; }
            if (selectedPage == "choose" && definition.name == "selectA")
                for (const auto& other : currentDefinition.components)
                    if (other.name == "selectB")
                    {
                        auto second = propertiesForResource(other.file);
                        const int gap = qMax(0, second.top - previous.top - previous.height);
                        second.top = edited.top + edited.height + gap;
                        if (previous.font != edited.font) { second.font = edited.font; second.fontSet = true; }
                        if (previous.normalColor != edited.normalColor) second.normalColor = edited.normalColor;
                        if (previous.hoverColor != edited.hoverColor) second.hoverColor = edited.hoverColor;
                        if (previous.pressColor != edited.pressColor) second.pressColor = edited.pressColor;
                        if (previous.image != edited.image) second.image = edited.image;
                        if (previous.bitmap != edited.bitmap) { second.bitmap = edited.bitmap; second.bitmapSet = true; }
                        if (previous.up != edited.up) second.up = edited.up;
                        if (previous.down != edited.down) second.down = edited.down;
                        if (previous.track != edited.track) second.track = edited.track;
                        if (previous.stretch != edited.stretch) { second.stretch = edited.stretch; second.stretchSet = true; }
                        if (!stageComponentProperties(other.editorId, other.file, second, other.type)) restoreSnapshot(snapshot);
                        break;
                    }
        }
    }
    else if (selectedItemType == SelectedItemType::SubComponent)
    {
        if (!selectedSubComponentPath.isEmpty())
            stageComponentProperties(selectedOwnerId(), selectedSubComponentPath,
                                     propertyEditor->getProperties(), "DragButton");
    }
    else if (selectedItemType == SelectedItemType::SubMenu)
    {
        if (selectedSubMenuIndex < 0 || selectedSubMenuIndex >= currentDefinition.subMenus.size())
            return;

        auto& subMenu = currentDefinition.subMenus[selectedSubMenuIndex];
        subMenu.backgroundImage = propertyEditor->getSubMenuBackgroundImage();
        subMenu.windowRect = propertyEditor->getSubMenuWindowRect();
        if (!subMenu.windowFile.isEmpty())
        {
            ComponentIniProperties windowProps = propertiesForResource(subMenu.windowFile);
            windowProps.left = subMenu.windowRect.x();
            windowProps.top = subMenu.windowRect.y();
            windowProps.width = subMenu.windowRect.width();
            windowProps.height = subMenu.windowRect.height();
            if (!(windowProps.image.isEmpty() && windowProps.bitmap == subMenu.backgroundImage))
                windowProps.image = subMenu.backgroundImage;
            windowProps.stretch = subMenu.windowStretch;
            stageComponentProperties(subMenu.editorId + "/window", subMenu.windowFile,
                                     windowProps, "MenuWindow");
        }
        markSubMenuModified(selectedSubMenuIndex);
    }
    else if (selectedItemType == SelectedItemType::SubMenuComponent)
    {
        if (selectedSubMenuIndex < 0 || selectedSubMenuIndex >= currentDefinition.subMenus.size())
            return;

        auto& subMenu = currentDefinition.subMenus[selectedSubMenuIndex];
        if (selectedSubMenuComponentIndex < 0 || selectedSubMenuComponentIndex >= subMenu.components.size())
            return;

        auto& definition = subMenu.components[selectedSubMenuComponentIndex];
        if (selectedSubMenuComponentIndex < subMenu.previewComponents.size())
        {
            auto& pc = subMenu.previewComponents[selectedSubMenuComponentIndex];
            pc.name = definition.name;
            pc.type = definition.type;
            if (!definition.file.isEmpty())
            {
                ComponentIniProperties props = propertyEditor->getProperties();
                pc.rect = QRect(props.left, props.top, props.width, props.height);
                setPreviewComponentImages(definition.type, props, pc);
                pc.stretch = resolveStretch(props, definition.type);
            }
        }

        if (!definition.file.isEmpty())
            stageComponentProperties(definition.editorId, definition.file,
                                     propertyEditor->getProperties(), definition.type);
        markSubMenuModified(selectedSubMenuIndex);
    }

    markModified();
    updatePreview();
    if (selectedItemType == SelectedItemType::MenuWindow)
        propertyEditor->setProperties(selectedPage == "npcinfo" ? npcInfoProperties() : isNoticePage(selectedPage) ? noticeWindowProperties() : propertiesForResource(currentDefinition.windowFile));
    else if (selectedItemType == SelectedItemType::Component && selectedComponentIndex >= 0)
        propertyEditor->setProperties(propertiesForResource(currentDefinition.components[selectedComponentIndex].file));
    else if (selectedItemType == SelectedItemType::SubComponent)
        propertyEditor->setProperties(propertiesForResource(selectedSubComponentPath));
    // Keep the active input and its cursor intact while the value is edited.
    propertyEditor->setMenuEditableFields(editableFields(selectedOwnerId()),
        tr("此属性由游戏控制"));
}

void MenuEditorWindow::onDefinitionPropertyChanged()
{
    if (!updatingFromCode && selectedComponentIndex >= 0 && selectedComponentIndex < currentDefinition.components.size())
    {
        auto& definition = currentDefinition.components[selectedComponentIndex];
        const QString format = propertyEditor->getComponentFormat();
        if (editableFields(definition.editorId).contains("format") && format != definition.format)
        {
            if (validMenuBindingFormat(format, definition.bind))
            {
                definition.format = format;
                markModified();
                updatePreview();
            }
            else pageContextLabel->setText(tr("格式必须匹配原绑定的整数数量（%d）；绑定本身保持只读。"));
        }
    }
    updatePropertyEditor();
}

void MenuEditorWindow::onResourceFileEditingFinished()
{
    updatePropertyEditor();
}

MenuEditorWindow::EditorSnapshot MenuEditorWindow::createSnapshot()
{
    EditorSnapshot snapshot;
    snapshot.definition = currentDefinition;
    snapshot.modifiedInis = modifiedComponentInis;
    snapshot.modifiedSubMenus = modifiedSubMenuIds;

    auto addEffectiveProperties = [this, &snapshot](const QString& ownerId,
                                                    const QString& resourcePath,
                                                    const QString& componentType)
    {
        QString localPath;
        const QString normalized = normalizedResourcePath(resourcePath);
        if (ownerId.isEmpty() || normalized.isEmpty() ||
            !resolveLocalResourcePath(normalized, localPath))
        {
            return;
        }

        auto existing = snapshot.modifiedInis.find(localPath);
        if (existing != snapshot.modifiedInis.end())
        {
            existing->owners.insert(ownerId);
            existing->ownerComponentTypes.insert(ownerId, componentType);
            return;
        }

        ModifiedComponentIni effective;
        effective.properties = propertiesForResource(normalized);
        effective.resourcePath = normalized;
        if (!resolveReadableResourcePath(normalized, effective.sourcePath))
            effective.sourcePath = localPath;
        effective.owners.insert(ownerId);
        effective.ownerComponentTypes.insert(ownerId, componentType);
        effective.stagedForWrite = false;
        snapshot.modifiedInis.insert(localPath, effective);
    };

    addEffectiveProperties("menu/window", currentDefinition.windowFile, "MenuWindow");
    for (const MenuComponentDefinition& component : currentDefinition.components)
    {
        addEffectiveProperties(component.editorId, component.file, component.type);
        if (component.type == "Scrollbar" && !component.file.isEmpty())
        {
            const ComponentIniProperties properties = propertiesForResource(component.file);
            if (!properties.slideBtn.isEmpty())
            {
                const QString slideResource = normalizedResourcePath(QDir::cleanPath(
                    QFileInfo(normalizedResourcePath(component.file)).path() + "/" + properties.slideBtn));
                addEffectiveProperties(component.editorId + "/slideBtn", slideResource, "DragButton");
            }
        }
    }
    for (const SubMenuDefinition& subMenu : currentDefinition.subMenus)
    {
        addEffectiveProperties(subMenu.editorId + "/window", subMenu.windowFile, "MenuWindow");
        for (const MenuComponentDefinition& component : subMenu.components)
            addEffectiveProperties(component.editorId, component.file, component.type);
    }
    return snapshot;
}

QByteArray MenuEditorWindow::snapshotFingerprint(const EditorSnapshot& snapshot) const
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    const MenuDefinition& definition = snapshot.definition;
    stream << definition.menuName << definition.visible << definition.windowFile
           << definition.backgroundImage << definition.windowRect;

    auto writeDefinition = [&stream](const MenuComponentDefinition& component)
    {
        stream << component.originalLines << component.editorId << component.type << component.name
               << component.file << component.bind << component.format
               << component.controllerUp << component.controllerDown
               << component.controllerLeft << component.controllerRight << component.optionLayout << component.spacingY << component.runtimeDefault;
    };
    stream << definition.components.size();
    for (const MenuComponentDefinition& component : definition.components)
        writeDefinition(component);
    stream << definition.subMenus.size();
    for (const SubMenuDefinition& subMenu : definition.subMenus)
    {
        stream << subMenu.editorId << subMenu.name << subMenu.file
               << subMenu.windowFile << subMenu.windowRect
               << subMenu.backgroundImage << subMenu.windowStretch
               << subMenu.components.size();
        for (const MenuComponentDefinition& component : subMenu.components)
            writeDefinition(component);
    }

    auto writeProperties = [&stream](const ComponentIniProperties& properties)
    {
        stream << properties.name << properties.nameSet
               << properties.left << properties.top << properties.width << properties.height
               << properties.image << properties.bitmap << properties.baseImage << properties.thumbImage
               << properties.align << properties.alignX << properties.alignY
               << properties.sound << properties.kind << properties.up << properties.down << properties.track
               << properties.hoverSound << properties.animate
               << properties.color << properties.selColor << properties.font << properties.style
               << properties.min << properties.max << properties.position
               << properties.lineSize << properties.pageSize << properties.slideBegin << properties.slideEnd
               << properties.slideBtn << properties.itemHeight << properties.itemCount << properties.items
               << properties.range << properties.text << properties.icon << properties.iconImage
               << properties.indicateType << properties.indicateImage << properties.percent
               << properties.normalColor << properties.hoverColor << properties.pressColor
               << properties.stretch << properties.keepAspect << properties.centerImage
               << properties.cropContent << properties.cropBlack << properties.frame
               << properties.backImage1 << properties.backImage2 << properties.autoShrink
               << properties.stretchSet << properties.fontSet << properties.bitmapSet
               << properties.hoverSoundSet << properties.animateSet << properties.keepAspectSet
               << properties.centerImageSet << properties.cropContentSet << properties.cropBlackSet
               << properties.frameSet << properties.backImage1Set << properties.backImage2Set
               << properties.autoShrinkSet << properties.iconSet << properties.iconImageSet
               << properties.positionSet << properties.rawValues;
    };

    stream << snapshot.modifiedInis.size();
    for (auto it = snapshot.modifiedInis.constBegin(); it != snapshot.modifiedInis.constEnd(); ++it)
    {
        stream << it.key() << it->resourcePath << it->stagedForWrite;
        QStringList owners = it->owners.values();
        owners.sort();
        stream << owners << it->ownerComponentTypes;
        writeProperties(it->properties);
    }
    QStringList modifiedSubMenus = snapshot.modifiedSubMenus.values();
    modifiedSubMenus.sort();
    stream << modifiedSubMenus;
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

QByteArray MenuEditorWindow::currentSnapshotFingerprint()
{
    return snapshotFingerprint(createSnapshot());
}

void MenuEditorWindow::restoreSnapshot(const EditorSnapshot& snapshot)
{
    restoringHistory = true;
    currentDefinition = snapshot.definition;
    modifiedComponentInis = snapshot.modifiedInis;
    modifiedSubMenuIds = snapshot.modifiedSubMenus;
    if (!selectedPage.isEmpty())
    {
        auto propertyFingerprint = [this](const ComponentIniProperties& properties)
        {
            EditorSnapshot values;
            values.modifiedInis["properties"].properties = properties;
            return snapshotFingerprint(values);
        };
        // Reused files may have changed since an older history snapshot was saved.
        for (auto it = modifiedComponentInis.begin(); it != modifiedComponentInis.end(); ++it)
        {
            const auto saved = lastWrittenComponentProperties.constFind(it.key());
            if (!it->stagedForWrite && saved != lastWrittenComponentProperties.constEnd()
                && propertyFingerprint(it->properties) != propertyFingerprint(saved.value()))
                it->stagedForWrite = true;
        }
    }
    componentIniCache.clear();
    selectedComponentIndex = -1;
    selectedSubMenuIndex = -1;
    selectedSubMenuComponentIndex = -1;
    selectedSubComponentName.clear();
    selectedSubComponentPath.clear();
    selectedItemType = SelectedItemType::None;

    updatingFromCode = true;
    ui->menuNameEdit->setText(currentDefinition.menuName);
    ui->menuVisibleCheck->setChecked(currentDefinition.visible);
    propertyEditor->clearAll();
    updatingFromCode = false;
    updateComponentTree();
    updatePreview();
    restoringHistory = false;
    updatePagePermissions();
}

void MenuEditorWindow::resetHistory()
{
    history.clear();
    history.append(createSnapshot());
    historyIndex = 0;
    lastWrittenComponentProperties.clear();
    savedSnapshotFingerprint = snapshotFingerprint(history.first());
    hasUnsavedChanges = false;
    updateUndoRedoState();
    emit documentStatesChanged();
}

void MenuEditorWindow::establishSavePoint(const QSet<QString>& writtenComponentPaths)
{
    const EditorSnapshot snapshot = createSnapshot();
    if (historyIndex < 0)
    {
        history.clear();
        history.append(snapshot);
        historyIndex = 0;
    }
    else
    {
        history[historyIndex] = snapshot;
    }
    // Shrinking a grid removes references, but its retained files still hold their last saved values.
    for (const QString& path : writtenComponentPaths)
    {
        const auto written = snapshot.modifiedInis.constFind(path);
        if (written != snapshot.modifiedInis.constEnd())
            lastWrittenComponentProperties.insert(path, written->properties);
    }
    savedSnapshotFingerprint = snapshotFingerprint(snapshot);
    hasUnsavedChanges = false;
    updateUndoRedoState();
    emit documentStatesChanged();
}

void MenuEditorWindow::updateUndoRedoState()
{
    if (undoAction)
        undoAction->setEnabled(historyIndex > 0);
    if (redoAction)
        redoAction->setEnabled(historyIndex >= 0 && historyIndex + 1 < history.size());
}

void MenuEditorWindow::onUndo()
{
    if (historyIndex <= 0)
        return;
    --historyIndex;
    restoreSnapshot(history[historyIndex]);
    hasUnsavedChanges = currentSnapshotFingerprint() != savedSnapshotFingerprint;
    updateUndoRedoState();
    emit documentStatesChanged();
}

void MenuEditorWindow::onRedo()
{
    if (historyIndex < 0 || historyIndex + 1 >= history.size())
        return;
    ++historyIndex;
    restoreSnapshot(history[historyIndex]);
    hasUnsavedChanges = currentSnapshotFingerprint() != savedSnapshotFingerprint;
    updateUndoRedoState();
    emit documentStatesChanged();
}

void MenuEditorWindow::markModified()
{
    if (restoringHistory)
        return;

    const EditorSnapshot snapshot = createSnapshot();
    const QByteArray fingerprint = snapshotFingerprint(snapshot);
    if (historyIndex >= 0 && fingerprint == snapshotFingerprint(history[historyIndex]))
    {
        hasUnsavedChanges = fingerprint != savedSnapshotFingerprint;
        emit documentStatesChanged();
        return;
    }

    while (history.size() > historyIndex + 1)
        history.removeLast();
    history.append(snapshot);
    historyIndex = history.size() - 1;
    constexpr int MaximumHistoryEntries = 100;
    if (history.size() > MaximumHistoryEntries)
    {
        history.removeFirst();
        --historyIndex;
    }
    hasUnsavedChanges = fingerprint != savedSnapshotFingerprint;
    updateUndoRedoState();
    emit documentStatesChanged();
}

bool MenuEditorWindow::prepareTransaction(const QString& targetPath,
                                          FileTransaction& transaction,
                                          QSet<QString>& targetKeys,
                                          QString& error) const
{
    const QString target = EditorAssetPath::normalizedAbsolutePath(targetPath);
    if (!EditorAssetPath::isInside(assetsBasePath, target))
    {
        error = tr("写入目标越过当前资源包: %1").arg(targetPath);
        return false;
    }

    const QString targetKey = EditorAssetPath::comparisonKey(target);
    if (targetKeys.contains(targetKey))
    {
        error = tr("多个菜单对象指向同一个写入目标，已拒绝保存以避免覆盖: %1").arg(target);
        return false;
    }
    targetKeys.insert(targetKey);

    QString suffix;
    do
    {
        suffix = QUuid::createUuid().toString(QUuid::WithoutBraces);
        transaction.temp = target + ".jxqy-" + suffix + ".tmp";
    }
    while (QFileInfo::exists(transaction.temp));

    transaction.target = target;
    return true;
}

void MenuEditorWindow::cleanupTransactionTemps(const QList<FileTransaction>& transactions) const
{
    for (const FileTransaction& transaction : transactions)
    {
        if (QFileInfo::exists(transaction.temp))
            QFile::remove(transaction.temp);
    }
}

bool MenuEditorWindow::commitTransactions(QList<FileTransaction>& transactions)
{
    QSet<QString> targetKeys;
    for (const FileTransaction& tx : transactions)
    {
        const QString targetKey = EditorAssetPath::comparisonKey(tx.target);
        if (targetKeys.contains(targetKey) ||
            !EditorAssetPath::isInside(assetsBasePath, tx.target) ||
            !EditorAssetPath::isInside(assetsBasePath, tx.temp))
        {
            cleanupTransactionTemps(transactions);
            QMessageBox::warning(this, tr("保存失败"),
                tr("提交前路径复核失败，原文件未修改: %1").arg(tx.target));
            return false;
        }
        targetKeys.insert(targetKey);
    }

    DurableFileTransaction transaction(assetsBasePath);
    QString transactionError;
    for (const FileTransaction& tx : transactions)
    {
        if (!transaction.addPreparedWrite(tx.target, tx.temp, transactionError))
        {
            cleanupTransactionTemps(transactions);
            QMessageBox::warning(this, tr("保存失败"),
                tr("无法准备多文件事务：\n%1").arg(transactionError));
            return false;
        }
    }

    if (!transaction.commit(transactionError))
    {
        cleanupTransactionTemps(transactions);
        QMessageBox::warning(this, tr("保存失败"),
            tr("多文件事务提交失败：\n%1").arg(transactionError));
        return false;
    }
    if (!transactionError.isEmpty())
    {
        QMessageBox::warning(this, tr("事务清理警告"), transactionError);
    }
    return true;
}

bool MenuEditorWindow::isGoodsMenu() const
{
    return selectedPage == "goods";
}

bool MenuEditorWindow::isToolTipMenu() const
{
    return selectedPage == "tooltip";
}

bool MenuEditorWindow::readBagGrid(BagGridSettings& settings)
{
    if (!isGoodsMenu()) return false;
    QMap<int, ComponentIniProperties> gridItems;
    for (const auto& component : currentDefinition.components)
    {
        if (component.name == "scrollbar" && component.type == "Scrollbar")
            settings.columns = propertiesForResource(component.file).lineSize;
        const auto match = QRegularExpression("^item([1-9][0-9]*)$").match(component.name);
        if (match.hasMatch() && component.type == "Item")
            gridItems.insert(match.captured(1).toInt(), propertiesForResource(component.file));
    }
    if (gridItems.isEmpty() || settings.columns < 1 || gridItems.size() % settings.columns != 0) return false;
    settings.rows = gridItems.size() / settings.columns;
    const auto first = gridItems.value(1);
    settings.left = first.left; settings.top = first.top;
    settings.width = first.width; settings.height = first.height;
    settings.horizontalGapKnown = settings.columns > 1;
    settings.verticalGapKnown = settings.rows > 1;
    if (settings.horizontalGapKnown) settings.horizontalGap = gridItems.value(2).left - first.left - first.width;
    if (settings.verticalGapKnown) settings.verticalGap = gridItems.value(settings.columns + 1).top - first.top - first.height;
    for (int i = 0; i < gridItems.size(); ++i)
    {
        if (!gridItems.contains(i + 1)) return false;
        const auto slot = gridItems.value(i + 1);
        if (slot.width != first.width || slot.height != first.height
            || slot.left != first.left + (i % settings.columns) * (first.width + settings.horizontalGap)
            || slot.top != first.top + (i / settings.columns) * (first.height + settings.verticalGap)) return false;
    }
    return true;
}

bool MenuEditorWindow::applyBagGrid(const BagGridSettings& settings, QString* error)
{
    if (!canEditPage()) { if (error) *error = pageRestriction; return false; }
    applyingBagGrid = true;
    const auto gridGuard = qScopeGuard([this]() { applyingBagGrid = false; });
    ExternalResourceReferences externalReferences;
    QString reason;
    if (!isGoodsMenu() || settings.rows < 1 || settings.columns < 1
        || settings.rows > 100 || settings.columns > 100 || settings.width < 1 || settings.height < 1
        || settings.horizontalGap < 0 || settings.verticalGap < 0 || !validateKnownMenu(reason))
    {
        if (error) *error = reason.isEmpty() ? tr("背包阵列参数无效。") : reason;
        return false;
    }
    const auto before = createSnapshot();
    QMap<int, MenuComponentDefinition> gridItems;
    for (const auto& component : currentDefinition.components)
    {
        const auto match = QRegularExpression("^item([1-9][0-9]*)$").match(component.name);
        if (match.hasMatch()) gridItems.insert(match.captured(1).toInt(), component);
    }
    if (gridItems.isEmpty()) return false;
    const auto templateSlot = gridItems.first();
    const auto templateProperties = propertiesForResource(templateSlot.file);
    const int count = settings.rows * settings.columns;
    for (int number = gridItems.size() + 1; number <= count; ++number)
    {
        auto component = templateSlot;
        component.editorId = "component/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        component.originalLines.clear();
        component.name = "item" + QString::number(number);
        component.bind.clear(); component.format.clear();
        component.controllerUp.clear(); component.controllerDown.clear();
        component.controllerLeft.clear(); component.controllerRight.clear();
        currentDefinition.components.append(component);
    }
    for (int i = currentDefinition.components.size() - 1; i >= 0; --i)
    {
        const auto component = currentDefinition.components[i];
        const auto match = QRegularExpression("^item([1-9][0-9]*)$").match(component.name);
        if (!match.hasMatch()) continue;
        const int index = match.captured(1).toInt() - 1;
        if (index >= count)
        {
            clearModifiedOwner(component.editorId);
            currentDefinition.components.removeAt(i);
            continue;
        }
        auto properties = gridItems.contains(index + 1) ? propertiesForResource(component.file) : templateProperties;
        properties.left = settings.left + (index % settings.columns) * (settings.width + settings.horizontalGap);
        properties.top = settings.top + (index / settings.columns) * (settings.height + settings.verticalGap);
        properties.width = settings.width; properties.height = settings.height;
        if (!stageComponentProperties(component.editorId, component.file, properties, "Item", false, &externalReferences))
        {
            restoreSnapshot(before);
            if (error) *error = tr("无法准备当前资源包的局部覆盖。");
            return false;
        }
    }
    for (const auto& component : currentDefinition.components)
    {
        if (component.name != "scrollbar") continue;
        auto properties = propertiesForResource(component.file);
        properties.lineSize = settings.columns; properties.pageSize = count;
        if (!stageComponentProperties(component.editorId, component.file, properties, "Scrollbar", false, &externalReferences))
        {
            restoreSnapshot(before);
            return false;
        }
        break;
    }
    markModified();
    updateComponentTree(); updatePropertyEditor(); updatePreview();
    return true;
}

void MenuEditorWindow::editBagGrid()
{
    BagGridSettings settings;
    const bool regular = readBagGrid(settings);
    QDialog dialog(this);
    dialog.setWindowTitle(tr("背包阵列"));
    auto* layout = new QFormLayout(&dialog);
    auto* note = new QLabel(tr("仅调整显示，不改变背包容量。底图中画死的格线不会重绘。\n应用后只生成当前菜单引用的局部覆盖；共享原文件保留。"), &dialog);
    note->setWordWrap(true); layout->addRow(note);
    if (!regular || !settings.horizontalGapKnown || !settings.verticalGapKnown)
        layout->addRow(new QLabel(tr("不能完整识别阵列或某方向没有相邻格；请明确填写全部参数后应用。"), &dialog));
    const QStringList names = {"rows", "columns", "left", "top", "width", "height", "horizontalGap", "verticalGap"};
    const QStringList captions = {tr("行数"), tr("列数"), tr("起点 X"), tr("起点 Y"), tr("格子宽"), tr("格子高"), tr("水平间距"), tr("垂直间距")};
    const QList<int> values = {settings.rows, settings.columns, settings.left, settings.top, settings.width, settings.height, settings.horizontalGap, settings.verticalGap};
    QList<QSpinBox*> inputs;
    for (int i = 0; i < names.size(); ++i)
    {
        auto* input = new QSpinBox(&dialog); input->setObjectName("bagGrid" + names[i]);
        input->setRange(i < 2 ? 1 : (i < 4 ? -100000 : (i < 6 ? 1 : 0)), i < 2 ? 100 : 100000);
        input->setValue(values[i]); inputs.append(input); layout->addRow(captions[i], input);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttons);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    settings.rows=inputs[0]->value(); settings.columns=inputs[1]->value();
    settings.left=inputs[2]->value(); settings.top=inputs[3]->value();
    settings.width=inputs[4]->value(); settings.height=inputs[5]->value();
    settings.horizontalGap=inputs[6]->value(); settings.verticalGap=inputs[7]->value();
    QString error;
    if (!applyBagGrid(settings, &error)) QMessageBox::warning(this, tr("背包阵列"), error);
}

bool MenuEditorWindow::validateKnownMenu(QString& error)
{
    if (!QStringList{"goods", "equip", "state", "tooltip", "option", "partnerhead", "magic", "xiulian", "memo", "title", "system", "yesno", "saveload", "buysell", "titleteam", "dialog", "choose", "message", "timer", "systemnotice", "scriptmessages", "top", "bottom", "column", "mapthumbnail", "npcinfo", "joystick", "skills", "controllerhelp", "dicegame"}.contains(selectedPage)) return true;
    if (selectedPage == "titleteam") return validateTitleTeam(error);
    if (isNoticePage(selectedPage)) return validateNotice(error);
    if (isHudPage(selectedPage)) return validateHud(error);
    if (selectedPage == "joystick") return validateJoystick(error);
    if (selectedPage == "skills") return validateSkills(error);
    if (selectedPage == "dicegame") return validateDicePage(error);
    if(selectedPage=="controllerhelp")return validateControllerHelp(error);
    if (selectedPage == "npcinfo") return validateNpcInfo(error);
    if (selectedPage == "mapthumbnail") return validateMapThumbnail(error);
    if (!validateSystemPage(error) || !validateStoragePage(error)) return false;
    QMap<QString, const MenuComponentDefinition*> components;
    for (const auto& component : currentDefinition.components)
    {
        if (components.contains(component.name) || component.type.isEmpty())
        {
            error = tr("功能组件必须有唯一名称、类型和配置引用。"); return false;
        }
        components.insert(component.name, &component);
    }
    if (selectedPage == "partnerhead")
    {
        for (auto it = components.constBegin(); it != components.constEnd(); ++it)
        {
            if ((it.key() == "head" && it.value()->type != "Item") || (it.key() == "level" && it.value()->type != "Label"))
            { error = tr("伙伴头像模板类型无效：%1").arg(it.key()); return false; }
            if (it.key() == "head" && !it.value()->spacingY.isEmpty())
            {
                bool parsed = false; const int gap = it.value()->spacingY.toInt(&parsed);
                if (!parsed || gap < 0 || gap > 100000) { error = tr("头像垂直间距无效。"); return false; }
            }
            if ((it.key() == "head" || it.key() == "level"))
            {
                const auto props = propertiesForResource(it.value()->file);
                if (props.width <= 0 || props.height <= 0 || (it.key() == "level" && props.font <= 0))
                { error = tr("头像与等级模板尺寸、字号必须为正。"); return false; }
            }
        }
        return true;
    }
    if (selectedPage == "option")
    {
        for (const auto& control : OptionLayout::controls())
        {
            const QString name = QString::fromUtf8(control.name);
            const bool required = (name != "themeButton" || pageResources.qingyu("qingyu"))
                && (name != "automaticSaveButton" || optionAutomaticSaveAvailable());
            if (!components.contains(name))
            {
                if (required) { error = tr("缺少固定功能组件：%1").arg(name); return false; }
                continue;
            }
            const auto& component = *components[name];
            if (component.type != control.type || component.file.isEmpty()
                || (!component.optionLayout.isEmpty() && component.optionLayout != OptionLayout::Explicit
                    && !(name == "cheatSettingsButton" && component.optionLayout == OptionLayout::ResourceFooter)))
            { error = tr("固定功能组件的类型、引用或布局标记无效：%1").arg(name); return false; }
        }
    }
    auto validLayout = [this, &error](const QString& file, const QString& type)
    {
        const auto values = propertiesForResource(file).rawValues;
        for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        {
            const QString key = it.key();
            if ((selectedPage == "message" || selectedPage == "timer") && type == "Label"
                && key == "minimumfont" && it.value().isEmpty()) continue;
            const bool nineSliceField = key == "nineslice" || key == "nineslicewidth";
            bool valid = true;
            if (((key == "autonextline" || key == "centertext") && type == "Label")
                || (key == "drawslot" && type == "Item") || (key == "fitlines" && type == "MemoText"))
                valid = QStringList{"true","false","yes","no","on","off","1","0"}.contains(it.value().toLower());
            if ((key == "minimumfont" && type == "Label") || (key == "linesize" && type == "MemoText")
                || (type == "MenuWindow" && key == "scale")
                || ((type == "MenuWindow" || type == "ImageContainer") && nineSliceField))
            {
                bool parsed = false;
                const double number = it.value().toDouble(&parsed);
                valid = parsed && std::isfinite(number) && (nineSliceField ? number >= 0 : number > 0);
            }
            if (!valid)
            {
                error = tr("布局字段 %1 的值无效：%2").arg(key, it.value()); return false;
            }
        }
        return true;
    };
    if (!currentDefinition.windowFile.isEmpty() && !validLayout(currentDefinition.windowFile, "MenuWindow")) return false;
    for (const auto& component : currentDefinition.components) if (!validLayout(component.file, component.type)) return false;
    if (selectedPage == "title" || selectedPage == "option")
    {
        const auto window = propertiesForResource(currentDefinition.windowFile);
        if (window.width <= 16 || window.height <= 16) { error = tr("窗口设计尺寸必须大于 16。"); return false; }
    }
    const auto require = [&](const QString& name, const QString& type)
    {
        const auto* component = components.value(name);
        if (component && component->type == type) return true;
        error = tr("运行时需要组件 %1（%2），不能删除或改名／类型。").arg(name, type); return false;
    };
    if (selectedPage == "dialog" || selectedPage == "choose" || selectedPage == "message" || selectedPage == "timer")
    {
        if (selectedPage == "dialog" && (!require("label", "TalkLabel") || !require("head1", "ImageContainer")
            || !require("head2", "ImageContainer") || (components.contains("hint") && !require("hint", "Label")))) return false;
        if (selectedPage == "choose" && (!require("messageLabel", "Label") || !require("selectA", "ChooseTextButton") || !require("selectB", "ChooseTextButton"))) return false;
        if ((selectedPage == "message" && !require("label", "Label")) || (selectedPage == "timer" && !require("timeLabel", "Label"))) return false;
        QStringList references{currentDefinition.windowFile};
        const QStringList required = selectedPage == "dialog" ? QStringList{"label","head1","head2","hint"}
            : selectedPage == "choose" ? QStringList{"messageLabel","selectA","selectB"}
            : QStringList{selectedPage == "message" ? "label" : "timeLabel"};
        for (const auto& name : required)
            if (components.contains(name)) references.append(components.value(name)->file);
        for (const auto& reference : references)
        {
            QString path; resolveLocalResourcePath(reference, path);
            const auto staged = modifiedComponentInis.constFind(path);
            if (!pageResources.hasComponentData(reference) && (staged == modifiedComponentInis.cend() || !staged->stagedForWrite))
            { error = tr("固定组件配置缺失或为空，本页只读：%1").arg(reference.isEmpty() ? QStringLiteral("window") : reference); return false; }
        }
        return true;
    }
    if (isGoodsMenu())
    {
        if (!require("scrollbar", "Scrollbar") || !require("item1", "Item")) return false;
        int count = 0;
        for (const auto& component : currentDefinition.components)
            if (QRegularExpression("^item[1-9][0-9]*$").match(component.name).hasMatch()) ++count;
        for (int i = 1; i <= count; ++i) if (!require("item" + QString::number(i), "Item")) return false;
    }
    else if (pageEntryResource.endsWith("/magic/magic.menu.ini"))
    {
        int count = 0;
        for (const auto& component : currentDefinition.components)
            if (QRegularExpression("^item[1-9][0-9]*$").match(component.name).hasMatch()) ++count;
        for (int index = 1; index <= count; ++index) if (!require("item" + QString::number(index), "Item")) return false;
        if (components.contains("scrollbar") && !require("scrollbar", "Scrollbar")) return false;
        if (components.contains("detailText") && !require("detailText", "MemoText")) return false;
        const QMap<QString, QString> known = {{"magicTab","CheckBox"},{"talentTab","CheckBox"},{"detailIcon","ImageContainer"},
            {"detailName","Label"},{"detailPage","Label"},{"detailPrevious","FlatTextButton"},{"detailNext","FlatTextButton"},
            {"detailBack","FlatTextButton"},{"detailPractice","FlatTextButton"},{"detailQuick","FlatTextButton"}};
        for (auto it=known.constBegin();it!=known.constEnd();++it)
            if (components.contains(it.key()) && !require(it.key(),it.value())) return false;
    }
    else if (selectedPage == "xiulian")
    {
        const QMap<QString, QString> types = {{"magic", "Item"}, {"name", "Label"}, {"intro", "Label"}, {"level", "Label"}, {"exp", "Label"}};
        for (auto it = types.constBegin(); it != types.constEnd(); ++it)
            if (components.contains(it.key()) && !require(it.key(), it.value())) return false;
    }
    else if (selectedPage == "memo")
    {
        if (components.contains("memoText") && !require("memoText", "MemoText")) return false;
        if (components.contains("scrollbar") && !require("scrollbar", "Scrollbar")) return false;
    }
    else if (isToolTipMenu())
    {
        const QMap<QString, QString> knownTypes = {{"name", "Label"}, {"cost", "Label"}, {"intro1", "Label"},
            {"intro2", "Label"}, {"magicIntro", "Label"}, {"image", "ImageContainer"}, {"body", "MemoText"},
            {"page", "Label"}, {"hint", "Label"}, {"previous", "FlatTextButton"}, {"next", "FlatTextButton"}, {"back", "FlatTextButton"}};
        for (auto it = knownTypes.constBegin(); it != knownTypes.constEnd(); ++it)
            if (components.contains(it.key()) && !require(it.key(), it.value())) return false;
    }
    else
    {
        if (pageEntryResource.endsWith("/equip/equip.menu.ini"))
        {
            int count=0;
            for (auto it=components.constBegin();it!=components.constEnd();++it)
            {
                const int index=it.key().startsWith("magic")?it.key().mid(5).toInt():0;
                if (index>0 && it.key()==QString("magic%1").arg(index,2,10,QChar('0'))) ++count;
            }
            for (int index=1;index<=count;++index)
                if (!require(QString("magic%1").arg(index,2,10,QChar('0')), "Item")) return false;
            for (const QString& name:{QString("equipmentTab"),QString("attributesTab")})
                if (components.contains(name) && !require(name,"CheckBox")) return false;
            if (components.contains("magicScrollbar") && !require("magicScrollbar","Scrollbar")) return false;
        }
        for (auto it = components.constBegin(); it != components.constEnd(); ++it)
        {
            if (QRegularExpression("^item[1-7]$").match(it.key()).hasMatch() && !require(it.key(), "Item")) return false;
            if (QStringList{"labLevel", "labExp", "labExpUp", "labExperience", "labLevelUpExperience", "labAttack", "labDefend", "labEvade", "labLife", "labThew", "labMana", "labRage"}.contains(it.key())
                && !require(it.key(), "Label")) return false;
        }
    }
    return true;
}

bool MenuEditorWindow::validateControllerNavigationOverrides(QString& error) const
{
    auto validateScope = [this, &error](
        const QList<MenuComponentDefinition>& components,
        const QString& scopeLabel)
    {
        QMap<QString, int> nameCounts;
        for (const MenuComponentDefinition& component : components)
            nameCounts[component.name] += 1;

        for (const MenuComponentDefinition& component : components)
        {
            const bool hasOverride =
                !component.controllerUp.isEmpty() ||
                !component.controllerDown.isEmpty() ||
                !component.controllerLeft.isEmpty() ||
                !component.controllerRight.isEmpty();
            if (hasOverride && component.name.isEmpty())
            {
                error = tr("%1中存在填写了手柄方向覆盖但 name 为空的组件；"
                           "来源组件必须有非空且唯一的 name。")
                    .arg(scopeLabel);
                return false;
            }
            if (hasOverride && nameCounts.value(component.name) != 1)
            {
                error = tr("%1中的组件“%2”填写了手柄方向覆盖，但来源 name 存在重名；"
                           "来源组件 name 必须在同一菜单内唯一。")
                    .arg(scopeLabel, component.name);
                return false;
            }

            const QList<QPair<QString, QString>> overrides = {
                {tr("上"), component.controllerUp},
                {tr("下"), component.controllerDown},
                {tr("左"), component.controllerLeft},
                {tr("右"), component.controllerRight}
            };
            for (const auto& overrideEntry : overrides)
            {
                const QString& direction = overrideEntry.first;
                const QString& target = overrideEntry.second;
                if (target.isEmpty())
                    continue;
                if (target == component.name)
                {
                    error = tr("%1中的组件“%2”的%3方向目标不能指向自身。")
                        .arg(scopeLabel, component.name, direction);
                    return false;
                }
                if (!nameCounts.contains(target))
                {
                    error = tr("%1中的组件“%2”的%3方向目标“%4”不存在；"
                               "目标必须是同一菜单内 component 的 name。")
                        .arg(scopeLabel, component.name, direction, target);
                    return false;
                }
                if (nameCounts.value(target) != 1)
                {
                    error = tr("%1中的组件“%2”的%3方向目标“%4”存在重名；"
                               "目标 name 必须在同一菜单内唯一。")
                        .arg(scopeLabel, component.name, direction, target);
                    return false;
                }
            }
        }
        return true;
    };

    if (!validateScope(currentDefinition.components, tr("主菜单")))
        return false;
    for (const SubMenuDefinition& subMenu : currentDefinition.subMenus)
    {
        if (!validateScope(subMenu.components, tr("子菜单“%1”").arg(subMenu.name)))
            return false;
    }
    return true;
}

bool MenuEditorWindow::saveDocumentToPath(const QString& filePath, bool updateCurrentPath)
{
    QString error;
    if (!validatePageSave(filePath, error) || !validateStorageLabels(error))
    {
        pageContextLabel->setText(error);
        return false;
    }

    auto mutationLease =
        AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(filePath);
    if (!mutationLease)
        return false;

    if (resourceRecoveryBlocked)
    {
        QMessageBox::critical(
            this,
            tr("文件事务恢复失败"),
            tr("当前资源目录仍有未恢复事务，菜单保存功能保持停用：\n%1")
                .arg(resourceRecoveryErrors.join('\n')));
        return false;
    }
    if (filePath.trimmed().isEmpty() ||
        !canAdoptDocumentPath(filePath))
    {
        return false;
    }

    if (selectedPage == "dicegame") return saveDiceDocument(filePath, updateCurrentPath);
    if (selectedPage == "npcinfo" || selectedPage == "controllerhelp") return saveFixedWindowDocument(filePath, updateCurrentPath);
    if ((selectedPage == "joystick" || selectedPage == "skills") && !hasUnsavedChanges) return true;

    if ((selectedPage == "option" || selectedPage == "partnerhead" || selectedPage == "yesno" || selectedPage == "titleteam" || isNoticePage(selectedPage)) && !hasUnsavedChanges) return true;

    QList<FileTransaction> transactions;
    QSet<QString> targetKeys;
    const EditorSnapshot beforeSave = createSnapshot();
    bool copiedForSave = false;
    const auto restoreFailedSave = qScopeGuard([&]()
    {
        if (copiedForSave) restoreSnapshot(beforeSave);
    });
    if (!selectedPage.isEmpty())
    {
        const bool savingCopy = EditorAssetPath::comparisonKey(filePath)
            != EditorAssetPath::comparisonKey(currentFilePath);
        ExternalResourceReferences externalReferences;
        const auto pending = modifiedComponentInis;
        for (auto it = pending.constBegin(); it != pending.constEnd(); ++it)
        {
            if (!it->stagedForWrite || !QFileInfo::exists(it.key())) continue;
            for (const QString& owner : it->owners)
            {
                QString liveResource;
                if (owner == "menu/window") liveResource = currentDefinition.windowFile;
                for (const auto& component : currentDefinition.components)
                {
                    if (component.editorId == owner) liveResource = component.file;
                    if (component.editorId + "/slideBtn" == owner)
                        liveResource = QFileInfo(normalizedResourcePath(component.file)).path() + "/" + propertiesForResource(component.file).slideBtn;
                }
                // Localizing a child may already have replaced its parent's pending reference.
                if (!liveResource.isEmpty() && normalizedResourcePath(liveResource) != normalizedResourcePath(it->resourcePath)) continue;
                if (!savingCopy && canReuseComponentResource(owner, it->resourcePath, &externalReferences)) continue;
                copiedForSave = true;
                if (!stageComponentProperties(owner, it->resourcePath, it->properties,
                        it->ownerComponentTypes.value(owner), true, &externalReferences)) return false;
            }
        }
    }

    if (!validateControllerNavigationOverrides(error) || !validateKnownMenu(error))
    {
        QMessageBox::warning(this, tr("保存失败"), error);
        return false;
    }

    auto validateReference = [&error, this](const QString& reference, const QString& label)
    {
        if (reference.isEmpty())
            return true;
        QString normalized;
        if (EditorAssetPath::normalizeResourcePath(reference, normalized))
            return true;
        error = tr("%1 含无效或越界资源路径: %2").arg(label, reference);
        return false;
    };

    if (!validateReference(currentDefinition.windowFile, tr("菜单窗口")))
    {
        QMessageBox::warning(this, tr("保存失败"), error);
        return false;
    }
    for (const MenuComponentDefinition& component : currentDefinition.components)
    {
        if (!validateReference(component.file, tr("组件 %1").arg(component.name)))
        {
            QMessageBox::warning(this, tr("保存失败"), error);
            return false;
        }
    }
    for (const SubMenuDefinition& subMenu : currentDefinition.subMenus)
    {
        if (!validateReference(subMenu.file, tr("子菜单 %1").arg(subMenu.name)) ||
            !validateReference(subMenu.windowFile, tr("子菜单窗口 %1").arg(subMenu.name)))
        {
            QMessageBox::warning(this, tr("保存失败"), error);
            return false;
        }
        for (const MenuComponentDefinition& component : subMenu.components)
        {
            if (!validateReference(component.file,
                                   tr("子菜单组件 %1").arg(component.name)))
            {
                QMessageBox::warning(this, tr("保存失败"), error);
                return false;
            }
        }
    }

    QMap<QString, QList<const SubMenuDefinition*>> subMenusByTarget;
    for (const SubMenuDefinition& subMenu : currentDefinition.subMenus)
    {
        if (subMenu.file.isEmpty())
            continue;

        QString target;
        if (!resolveLocalResourcePath(subMenu.file, target))
        {
            error = tr("子菜单路径无效或越过当前资源包: %1").arg(subMenu.file);
            QMessageBox::warning(this, tr("保存失败"), error);
            return false;
        }
        subMenusByTarget[EditorAssetPath::comparisonKey(target)].append(&subMenu);
    }
    for (auto it = subMenusByTarget.constBegin(); it != subMenusByTarget.constEnd(); ++it)
    {
        const QList<const SubMenuDefinition*>& sharedSubMenus = it.value();
        if (sharedSubMenus.size() < 2)
            continue;

        bool hasModifiedSubMenu = false;
        QStringList names;
        for (const SubMenuDefinition* subMenu : sharedSubMenus)
        {
            names.append(subMenu->name);
            hasModifiedSubMenu = hasModifiedSubMenu ||
                modifiedSubMenuIds.contains(subMenu->editorId);
        }
        if (!hasModifiedSubMenu)
            continue;

        QMessageBox::warning(
            this,
            tr("保存失败"),
            tr("子菜单 %1 仍共同引用同一个文件 %2。\n"
               "其中至少一个子菜单内容已修改，请先为这些子菜单拆分为唯一的 file 路径后再保存。")
                .arg(names.join(tr("、")), sharedSubMenus.first()->file));
        return false;
    }

    FileTransaction menuTransaction;
    if (!prepareTransaction(filePath, menuTransaction, targetKeys, error))
    {
        QMessageBox::warning(this, tr("保存失败"), error);
        return false;
    }
    transactions.append(menuTransaction);

    for (auto it = modifiedComponentInis.constBegin(); it != modifiedComponentInis.constEnd(); ++it)
    {
        if (!it->stagedForWrite)
            continue;
        FileTransaction transaction;
        if (!prepareTransaction(it.key(), transaction, targetKeys, error))
        {
            QMessageBox::warning(this, tr("保存失败"), error);
            return false;
        }
        transactions.append(transaction);
    }

    QList<const SubMenuDefinition*> subMenusToWrite;
    for (const SubMenuDefinition& subMenu : currentDefinition.subMenus)
    {
        if (!modifiedSubMenuIds.contains(subMenu.editorId) || subMenu.file.isEmpty())
            continue;
        QString target;
        if (!resolveLocalResourcePath(subMenu.file, target))
        {
            error = tr("子菜单路径无效或越过当前资源包: %1").arg(subMenu.file);
            QMessageBox::warning(this, tr("保存失败"), error);
            return false;
        }
        FileTransaction transaction;
        if (!prepareTransaction(target, transaction, targetKeys, error))
        {
            QMessageBox::warning(this, tr("保存失败"), error);
            return false;
        }
        transactions.append(transaction);
        subMenusToWrite.append(&subMenu);
    }

    for (const FileTransaction& transaction : transactions)
    {
        if (!QDir().mkpath(QFileInfo(transaction.target).absolutePath()))
        {
            QMessageBox::warning(this, tr("保存失败"),
                tr("无法创建目标目录: %1").arg(QFileInfo(transaction.target).absolutePath()));
            return false;
        }
    }

    const QStringList savedMenuLines = saveToMenuFile(transactions[0].temp);
    bool allWritesSucceeded = !savedMenuLines.isEmpty();
    int transactionIndex = 1;
    if (allWritesSucceeded)
    {
        for (auto it = modifiedComponentInis.constBegin(); it != modifiedComponentInis.constEnd(); ++it)
        {
            const ModifiedComponentIni& modified = it.value();
            if (!modified.stagedForWrite)
                continue;
            QSet<QString> componentTypes;
            for (auto typeIt = modified.ownerComponentTypes.constBegin();
                 typeIt != modified.ownerComponentTypes.constEnd(); ++typeIt)
            {
                componentTypes.insert(typeIt.value());
            }
            if (!writeComponentIniProperties(transactions[transactionIndex].temp,
                                             modified.properties,
                                             componentTypes,
                                             modified.sourcePath))
            {
                allWritesSucceeded = false;
                break;
            }
            ++transactionIndex;
        }
    }
    if (allWritesSucceeded)
    {
        for (const SubMenuDefinition* subMenu : subMenusToWrite)
        {
            if (!saveSubMenuDefinition(*subMenu, transactions[transactionIndex].temp))
            {
                allWritesSucceeded = false;
                break;
            }
            ++transactionIndex;
        }
    }

    if (!allWritesSucceeded)
    {
        cleanupTransactionTemps(transactions);
        QMessageBox::warning(this, tr("保存失败"),
            tr("部分文件写入失败，原文件未修改。"));
        return false;
    }

    if (!commitTransactions(transactions))
        return false;

    copiedForSave = false;

    QSet<QString> writtenComponentPaths;
    for (auto it = modifiedComponentInis.constBegin(); it != modifiedComponentInis.constEnd(); ++it)
        if (it->stagedForWrite) writtenComponentPaths.insert(it.key());
    modifiedComponentInis.clear();
    modifiedSubMenuIds.clear();
    componentIniCache.clear();
    originalMenuFileLines = savedMenuLines;
    hasUnsavedChanges = false;
    ProjectManager::instance().markDirty();
    if (updateCurrentPath)
    {
        currentFilePath = EditorAssetPath::normalizedAbsolutePath(filePath);
        setWindowTitle(tr("菜单编辑器 - ") + QFileInfo(currentFilePath).fileName());
    }
    {
        // Save-time sharing checks can split an unselected component's reference.
        const QSignalBlocker blocker(ui->componentTree);
        if (!currentDefinition.windowFile.isEmpty() && ui->componentTree->topLevelItemCount()>0)
            ui->componentTree->topLevelItem(0)->setText(2,currentDefinition.windowFile);
        for (int index=0;index<currentDefinition.components.size();++index)
            if (auto* item=ui->componentTree->topLevelItem(index+1))
            {
                const auto& component=currentDefinition.components[index];
                item->setText(2,component.runtimeDefault && !(selectedPage == "skills" && component.editorId == "skills/minimap")?tr("运行时默认"):component.file);
                if (component.type == "Scrollbar" && item->childCount() > 0)
                    item->child(0)->setText(2, propertiesForResource(component.file).slideBtn);
            }
    }
    prepareOptionDefaults();
    preparePartnerHeadDefaults();
    prepareTitleTeamDefaults();
    prepareNoticeDefaults();
    prepareNpcInfoDefaults();
    prepareControllerHelpDefaults();
    prepareDiceProperties();
    prepareSkillsControls();
    establishSavePoint(writtenComponentPaths);
    updatePagePermissions();
    refreshPageList();
    return true;
}

void MenuEditorWindow::onSaveFile()
{
    saveCurrentPage();
}


bool MenuEditorWindow::saveMenuDefinitionAs(const QString& filePath)
{
    return saveDocumentToPath(filePath, true);
}

void MenuEditorWindow::onPreviewRefresh()
{
    componentIniCache.clear();
    updatePropertyEditor();
    updatePreview();
}

void MenuEditorWindow::onComponentMovedUp()
{
    updatePropertyEditor();
}

void MenuEditorWindow::onComponentMovedDown()
{
    updatePropertyEditor();
}

void MenuEditorWindow::onPreviewComponentSelected(const QString& editorId)
{
    for (int i = 0; i < currentDefinition.components.size(); i++)
    {
        const auto& component = currentDefinition.components[i];
        if (component.editorId == editorId)
        {
            selectComponentByIndex(i);
            return;
        }
        if (component.editorId + "/slideBtn" == editorId)
        {
            QTreeWidgetItem* parent = ui->componentTree->topLevelItem(i + 1);
            if (parent && parent->childCount() > 0)
                ui->componentTree->setCurrentItem(parent->child(0));
            return;
        }
    }

    for (int i = 0; i < currentDefinition.subMenus.size(); i++)
    {
        for (int j = 0; j < currentDefinition.subMenus[i].components.size(); j++)
        {
            const auto& component = currentDefinition.subMenus[i].components[j];
            if (component.editorId == editorId ||
                component.editorId + "/slideBtn" == editorId)
            {
                int topLevelIndex = currentDefinition.components.size() + 1 + i;
                QTreeWidgetItem* parentItem = ui->componentTree->topLevelItem(topLevelIndex);
                if (parentItem)
                {
                    QTreeWidgetItem* childItem = parentItem->child(j);
                    if (childItem)
                    {
                        if (component.editorId + "/slideBtn" == editorId &&
                            childItem->childCount() > 0)
                        {
                            ui->componentTree->setCurrentItem(childItem->child(0));
                        }
                        else
                        {
                            ui->componentTree->setCurrentItem(childItem);
                        }
                    }
                }
                return;
            }
        }
    }
}

void MenuEditorWindow::onPreviewComponentMoved(const QString& editorId, int newX, int newY)
{
    if (!canEditGeometry(editorId, false)) { updatePreview(); updatePropertyEditor(); return; }
    const auto synchronizeProperties = qScopeGuard([this, &editorId]()
    {
        if (selectedOwnerId() == editorId) updatePropertyEditor();
    });

    for (int i = 0; i < currentDefinition.components.size(); i++)
    {
        const auto& component = currentDefinition.components[i];
        if (component.editorId + "/slideBtn" == editorId && !component.file.isEmpty())
        {
            const ComponentIniProperties parentProperties = propertiesForResource(component.file);
            const QString slideResource = normalizedResourcePath(QDir::cleanPath(
                QFileInfo(normalizedResourcePath(component.file)).path() + "/" + parentProperties.slideBtn));
            if (!slideResource.isEmpty())
            {
                ComponentIniProperties properties = propertiesForResource(slideResource);
                properties.left = newX - parentProperties.left;
                properties.top = newY - parentProperties.top;
                stageComponentProperties(component.editorId + "/slideBtn", slideResource,
                                         properties, "DragButton");
                markModified();
            }
            return;
        }
        if (component.editorId == editorId)
        {
            if (!component.file.isEmpty())
            {
                ComponentIniProperties props = propertiesForResource(component.file);
                props.left = newX;
                props.top = newY;
                stageComponentProperties(component.editorId, component.file, props, component.type);
                markModified();
            }
            return;
        }
    }

    for (int i = 0; i < currentDefinition.subMenus.size(); i++)
    {
        for (int j = 0; j < currentDefinition.subMenus[i].components.size(); j++)
        {
            const auto& definition = currentDefinition.subMenus[i].components[j];
            if (definition.editorId + "/slideBtn" == editorId &&
                definition.type == "Scrollbar" && !definition.file.isEmpty())
            {
                const ComponentIniProperties parentProperties =
                    propertiesForResource(definition.file);
                const QString slideResource = normalizedResourcePath(QDir::cleanPath(
                    QFileInfo(normalizedResourcePath(definition.file)).path() +
                    "/" + parentProperties.slideBtn));
                if (!slideResource.isEmpty())
                {
                    ComponentIniProperties properties = propertiesForResource(slideResource);
                    properties.left = newX - parentProperties.left;
                    properties.top = newY - parentProperties.top;
                    stageComponentProperties(definition.editorId + "/slideBtn", slideResource,
                                             properties, "DragButton");
                    markModified();
                }
                return;
            }
            if (definition.editorId == editorId)
            {
                if (!definition.file.isEmpty())
                {
                    ComponentIniProperties props = propertiesForResource(definition.file);
                    props.left = newX;
                    props.top = newY;
                    stageComponentProperties(definition.editorId, definition.file, props, definition.type);
                    markSubMenuModified(i);
                    markModified();

                    if (j < currentDefinition.subMenus[i].previewComponents.size())
                    {
                        currentDefinition.subMenus[i].previewComponents[j].rect.moveTo(newX, newY);
                    }
                }
                return;
            }
        }
    }
}

void MenuEditorWindow::onPreviewComponentResized(const QString& editorId, int newX, int newY, int newWidth, int newHeight)
{
    if (!canEditGeometry(editorId, true)) { updatePreview(); updatePropertyEditor(); return; }
    const auto synchronizeProperties = qScopeGuard([this, &editorId]()
    {
        if (selectedOwnerId() == editorId) updatePropertyEditor();
    });

    for (int i = 0; i < currentDefinition.components.size(); i++)
    {
        const auto& component = currentDefinition.components[i];
        if (component.editorId + "/slideBtn" == editorId && !component.file.isEmpty())
        {
            const ComponentIniProperties parentProperties = propertiesForResource(component.file);
            const QString slideResource = normalizedResourcePath(QDir::cleanPath(
                QFileInfo(normalizedResourcePath(component.file)).path() + "/" + parentProperties.slideBtn));
            if (!slideResource.isEmpty())
            {
                ComponentIniProperties properties = propertiesForResource(slideResource);
                properties.left = newX - parentProperties.left;
                properties.top = newY - parentProperties.top;
                properties.width = newWidth;
                properties.height = newHeight;
                stageComponentProperties(component.editorId + "/slideBtn", slideResource,
                                         properties, "DragButton");
                markModified();
            }
            return;
        }
        if (component.editorId == editorId)
        {
            if (!component.file.isEmpty())
            {
                ComponentIniProperties props = propertiesForResource(component.file);
                props.left = newX;
                props.top = newY;
                props.width = newWidth;
                props.height = newHeight;
                stageComponentProperties(component.editorId, component.file, props, component.type);
                markModified();
            }
            return;
        }
    }

    for (int i = 0; i < currentDefinition.subMenus.size(); i++)
    {
        for (int j = 0; j < currentDefinition.subMenus[i].components.size(); j++)
        {
            const auto& definition = currentDefinition.subMenus[i].components[j];
            if (definition.editorId + "/slideBtn" == editorId &&
                definition.type == "Scrollbar" && !definition.file.isEmpty())
            {
                const ComponentIniProperties parentProperties =
                    propertiesForResource(definition.file);
                const QString slideResource = normalizedResourcePath(QDir::cleanPath(
                    QFileInfo(normalizedResourcePath(definition.file)).path() +
                    "/" + parentProperties.slideBtn));
                if (!slideResource.isEmpty())
                {
                    ComponentIniProperties properties = propertiesForResource(slideResource);
                    properties.left = newX - parentProperties.left;
                    properties.top = newY - parentProperties.top;
                    properties.width = newWidth;
                    properties.height = newHeight;
                    stageComponentProperties(definition.editorId + "/slideBtn", slideResource,
                                             properties, "DragButton");
                    markModified();
                }
                return;
            }
            if (definition.editorId == editorId)
            {
                if (!definition.file.isEmpty())
                {
                    ComponentIniProperties props = propertiesForResource(definition.file);
                    props.left = newX;
                    props.top = newY;
                    props.width = newWidth;
                    props.height = newHeight;
                    stageComponentProperties(definition.editorId, definition.file, props, definition.type);
                    markSubMenuModified(i);
                    markModified();

                    if (j < currentDefinition.subMenus[i].previewComponents.size())
                    {
                        currentDefinition.subMenus[i].previewComponents[j].rect = QRect(newX, newY, newWidth, newHeight);
                    }
                }
                return;
            }
        }
    }
}

void MenuEditorWindow::onPreviewComponentPropertyChanged(const QString& editorId, int left, int top, int width, int height)
{
    if (!canEditGeometry(editorId, true)) { updatePreview(); updatePropertyEditor(); return; }

    int propertyLeft = left;
    int propertyTop = top;
    for (const auto& component : currentDefinition.components)
    {
        if (component.editorId + "/slideBtn" == editorId)
        {
            const ComponentIniProperties parent = propertiesForResource(component.file);
            propertyLeft -= parent.left;
            propertyTop -= parent.top;
            break;
        }
    }
    for (const auto& subMenu : currentDefinition.subMenus)
    {
        bool found = false;
        for (const auto& component : subMenu.components)
        {
            if (component.editorId + "/slideBtn" == editorId)
            {
                const ComponentIniProperties parent = propertiesForResource(component.file);
                propertyLeft -= parent.left;
                propertyTop -= parent.top;
                found = true;
                break;
            }
        }
        if (found)
            break;
    }

    updatingFromCode = true;
    ComponentIniProperties props = propertyEditor->getProperties();
    props.left = propertyLeft;
    props.top = propertyTop;
    props.width = width;
    props.height = height;
    propertyEditor->setProperties(props);
    updatingFromCode = false;
}

void MenuEditorWindow::updateComponentTree()
{
    if (selectedPage == "dicegame") { updateDiceTree(); return; }
    QStringList selectedPath;
    for (auto* item = ui->componentTree->currentItem(); item; item = item->parent()) selectedPath.prepend(item->text(1));
    updatingFromCode = true;
    ui->componentTree->clear();

    QColor windowColor(255, 200, 100);
    QColor subMenuColor(100, 200, 255);
    QColor subComponentColor(180, 220, 140);

    QMap<QString, QColor> typeColors;
    typeColors["ImageContainer"] = QColor(100, 200, 200);
    typeColors["Button"] = QColor(200, 100, 100);
    typeColors["CheckBox"] = QColor(200, 130, 130);
    typeColors["Item"] = QColor(100, 100, 200);
    typeColors["Label"] = QColor(100, 200, 100);
    typeColors["TalkLabel"] = QColor(130, 200, 130);
    typeColors["Scrollbar"] = QColor(200, 200, 100);
    typeColors["ColumnImage"] = QColor(200, 160, 100);
    typeColors["ListBox"] = QColor(160, 100, 200);
    typeColors["TextButton"] = QColor(200, 150, 150);
    typeColors["RoundButton"] = QColor(200, 120, 160);
    typeColors["DragRoundButton"] = QColor(180, 100, 140);
    typeColors["DragButton"] = QColor(220, 180, 100);
    typeColors["MemoText"] = QColor(140, 200, 140);
    typeColors["FadeMask"] = QColor(150, 150, 200);
    typeColors["TransImage"] = QColor(120, 180, 180);
    typeColors["Joystick"] = QColor(200, 180, 140);
    typeColors["VideoPlayer"] = QColor(180, 140, 200);

    auto windowItem = new QTreeWidgetItem();
    windowItem->setText(0, tr("[窗口]"));
    windowItem->setText(1, currentDefinition.menuName);
    windowItem->setText(2, currentDefinition.windowFile);
    for (int col = 0; col < 3; col++)
    {
        windowItem->setForeground(col, windowColor);
    }
    ui->componentTree->addTopLevelItem(windowItem);

    for (int i = 0; i < currentDefinition.components.size(); i++)
    {
        const auto& definition = currentDefinition.components[i];
        auto item = new QTreeWidgetItem();
        item->setText(0, objectTitle(definition.name, definition.type));
        item->setToolTip(0, definition.file);
        if (selectedPage == "mapthumbnail" && definition.name == "mapNameLabel")
        {
            item->setText(0, tr("地图名称（游戏中隐藏）"));
            item->setToolTip(0, tr("游戏加载小地图时会清空并隐藏此控件，画布中不显示。"));
        }
        if (selectedPage == "choose" && definition.name == "selectA") item->setText(0, tr("选项列表（共用样式）"));
        if (selectedPage == "choose" && definition.name == "selectB") item->setHidden(true);
        item->setText(1, definition.name);
        item->setText(2, definition.runtimeDefault && !(selectedPage == "skills" && definition.editorId == "skills/minimap") ? tr("尚未自定义") : definition.file);
        QColor itemColor = typeColors.value(definition.type, QColor(212, 212, 212));
        if (selectedPage == "mapthumbnail" && definition.name == "mapNameLabel") itemColor = QColor(150, 150, 150);
        for (int col = 0; col < 3; col++)
        {
            item->setForeground(col, itemColor);
        }
        ui->componentTree->addTopLevelItem(item);
        if (selectedPage == "choose" && definition.name == "selectB") item->setHidden(true);

        if (definition.type == "Scrollbar" && !definition.file.isEmpty())
        {
            const ComponentIniProperties scrollbarProps = propertiesForResource(definition.file);

            if (!scrollbarProps.slideBtn.isEmpty())
            {
                auto childItem = new QTreeWidgetItem();
                childItem->setText(0, tr("可拖动滑块"));
                childItem->setText(1, "slideBtn");
                childItem->setText(2, scrollbarProps.slideBtn);
                for (int col = 0; col < 3; col++)
                {
                    childItem->setForeground(col, subComponentColor);
                }
                item->addChild(childItem);
                item->setExpanded(true);
            }
        }
    }

    for (int i = 0; i < currentDefinition.subMenus.size(); i++)
    {
        const auto& subMenu = currentDefinition.subMenus[i];
        auto item = new QTreeWidgetItem();
        item->setText(0, tr("[子菜单]"));
        item->setText(1, subMenu.name);
        item->setText(2, subMenu.file);
        for (int col = 0; col < 3; col++)
        {
            item->setForeground(col, subMenuColor);
        }

        for (int j = 0; j < subMenu.components.size(); j++)
        {
            const auto& comp = subMenu.components[j];
            auto child = new QTreeWidgetItem();
            child->setText(0, objectTitle(comp.name, comp.type));
            child->setText(1, comp.name);
            child->setText(2, comp.file);
            QColor childColor = typeColors.value(comp.type, QColor(212, 212, 212));
            for (int col = 0; col < 3; col++)
            {
                child->setForeground(col, childColor);
            }
            item->addChild(child);

            if (comp.type == "Scrollbar" && !comp.file.isEmpty())
            {
                const ComponentIniProperties scrollbarProps = propertiesForResource(comp.file);
                if (!scrollbarProps.slideBtn.isEmpty())
                {
                    auto slideChild = new QTreeWidgetItem();
                    slideChild->setText(0, "DragButton");
                    slideChild->setText(1, "slideBtn");
                    slideChild->setText(2, scrollbarProps.slideBtn);
                    for (int col = 0; col < 3; col++)
                        slideChild->setForeground(col, subComponentColor);
                    child->addChild(slideChild);
                    child->setExpanded(true);
                }
            }
        }

        ui->componentTree->addTopLevelItem(item);
        item->setExpanded(true);
    }

    updatingFromCode = false;
    QTreeWidgetItem* selected = nullptr;
    for (const auto& name : selectedPath)
    {
        QTreeWidgetItem* match = nullptr;
        const int count = selected ? selected->childCount() : ui->componentTree->topLevelItemCount();
        for (int index = 0; index < count; ++index)
        {
            auto* item = selected ? selected->child(index) : ui->componentTree->topLevelItem(index);
            if (item->text(1) == name && !item->isHidden()) { match = item; break; }
        }
        selected = match;
        if (!selected) break;
    }
    ui->componentTree->setCurrentItem(selected ? selected : windowItem);
}

void MenuEditorWindow::updatePropertyEditor()
{
    editStatusLabel->clear();
    editStatusLabel->hide();
    findChild<QLabel*>("menuObjectDescription")->clear();
    propertyEditor->setChoiceListSpacing(0, false);
    propertyEditor->setDiceDisplayProperties({});
    if (selectedPage == "dicegame") { updateDicePropertyEditor(); return; }
    propertyEditor->setControllerHelpDisplayMode(selectedPage=="controllerhelp");
    propertyEditor->setNoticeDisplayMode(isNoticePage(selectedPage) || isHudPage(selectedPage) || selectedPage == "mapthumbnail" || selectedPage == "joystick" || selectedPage == "skills");
    propertyEditor->setTitleTeamDisplayLimits(selectedPage == "mapthumbnail" || isHudPage(selectedPage) || isNoticePage(selectedPage) || selectedPage == "titleteam" || selectedPage == "dialog" || selectedPage == "choose" || selectedPage == "message" || selectedPage == "timer");
    for (const auto& name : QStringList{"menuWindowWidthSpin","menuWindowHeightSpin","menuWindowAlignXSpin","menuWindowAlignYSpin"})
    {
        auto* spin = propertyEditor->findChild<QSpinBox*>(name);
        if (!spin->property("npcPreviousMinimum").isValid())
            spin->setProperty("npcPreviousMinimum",spin->minimum());
        const QSignalBlocker blocker(spin);
        if (selectedPage == "npcinfo" || selectedPage == "joystick" || selectedPage == "skills")
            spin->setRange(name.contains("Align") ? std::numeric_limits<int>::min() : 1, std::numeric_limits<int>::max());
        else if (!name.contains("Align"))
            spin->setMinimum(spin->property("npcPreviousMinimum").toInt());
    }
    for (const auto& name : QStringList{"leftSpin","topSpin","widthSpin","heightSpin","rangeSpin"})
    {
        auto* spin = propertyEditor->findChild<QSpinBox*>(name);
        if (!spin->property("joystickMinimum").isValid())
        { spin->setProperty("joystickMinimum",spin->minimum());spin->setProperty("joystickMaximum",spin->maximum()); }
        const QSignalBlocker blocker(spin);
        if (selectedPage == "joystick" || selectedPage == "skills") spin->setRange(name=="leftSpin"||name=="topSpin"?std::numeric_limits<int>::min():1,std::numeric_limits<int>::max());
        else if (name=="rangeSpin") spin->setRange(spin->property("joystickMinimum").toInt(),spin->property("joystickMaximum").toInt());
        else if (name=="widthSpin"||name=="heightSpin") spin->setMinimum(spin->property("joystickMinimum").toInt());
    }
    const auto permissionGuard = qScopeGuard([this]()
    {
        propertyEditor->setMenuEditableFields(editableFields(selectedOwnerId()),
            pageRestriction.isEmpty() ? tr("内部身份、动态内容或未核实字段由游戏管理；背包格子布局请使用阵列操作。") : pageRestriction);
    });
    for(const auto& name:QStringList{"upSpin","downSpin","trackSpin"})
    {
        auto* spin=propertyEditor->findChild<QSpinBox*>(name);
        if(!spin->property("skillsPreviousMinimum").isValid())
        {spin->setProperty("skillsPreviousMinimum",spin->minimum());spin->setProperty("skillsPreviousMaximum",spin->maximum());}
        const QSignalBlocker blocker(spin);
        spin->setRange(selectedPage=="skills"?std::numeric_limits<int>::min():spin->property("skillsPreviousMinimum").toInt(),
            selectedPage=="skills"?std::numeric_limits<int>::max():spin->property("skillsPreviousMaximum").toInt());
    }
    {
        auto* font=propertyEditor->findChild<QSpinBox*>("labelFontSpin");
        if(!font->property("skillsPreviousMinimum").isValid())font->setProperty("skillsPreviousMinimum",font->minimum());
        const QSignalBlocker blocker(font);
        font->setMinimum(selectedPage=="skills"?1:font->property("skillsPreviousMinimum").toInt());
        // Other pages have just applied their own font maximum above.
        if(selectedPage=="skills")font->setMaximum(std::numeric_limits<int>::max());
    }
    QString reference = currentDefinition.windowFile;
    if (selectedItemType == SelectedItemType::Component && selectedComponentIndex >= 0
        && selectedComponentIndex < currentDefinition.components.size())
        reference = currentDefinition.components[selectedComponentIndex].file;
    if (selectedItemType == SelectedItemType::SubComponent) reference = selectedSubComponentPath;
    if (selectedSubMenuIndex >= 0 && selectedSubMenuIndex < currentDefinition.subMenus.size())
    {
        const auto& subMenu = currentDefinition.subMenus[selectedSubMenuIndex];
        if (selectedItemType == SelectedItemType::SubMenu) reference = subMenu.windowFile;
        if (selectedItemType == SelectedItemType::SubMenuComponent && selectedSubMenuComponentIndex >= 0
            && selectedSubMenuComponentIndex < subMenu.components.size()) reference = subMenu.components[selectedSubMenuComponentIndex].file;
    }
    QString source, target;
    resolveReadableResourcePath(reference, source);
    resolveLocalResourcePath(reference, target);
    if(selectedPage=="skills")
    {
        auto* kind=findChild<QComboBox*>("buttonKindCombo");
        const QString value=propertiesForResource(reference).kind;
        if(kind->findText(value)<0){const QSignalBlocker blocker(kind);kind->addItem(value);}
    }
    if (source.isEmpty())
    {
        const auto staged = modifiedComponentInis.constFind(target);
        if (staged != modifiedComponentInis.constEnd()) source = staged->sourcePath;
    }
    if(selectedPage=="skills"&&selectedOwnerId()=="skills/minimap"&&!QFileInfo(source).isFile())
    {
        editStatusLabel->setText(tr("未找到小地图按钮配置，此按钮无法编辑。"));
        editStatusLabel->show();
        for(const auto& name:QStringList{"widthSpin","heightSpin"})findChild<QSpinBox*>(name)->setMinimum(0);
    }
    if (selectedItemType == SelectedItemType::None)
    {
        propertyEditor->clearAll();
        return;
    }

    if (selectedItemType == SelectedItemType::MenuWindow)
    {
        updatingFromCode = true;
        propertyEditor->setComponentType("MenuWindow");

        int windowWidth = 0;
        int windowHeight = 0;
        QString windowImage;
        QString windowBitmap;
        QString windowAlign;
        int windowAlignX = 0;
        int windowAlignY = 0;
        bool windowStretch = false;

        if (!currentDefinition.windowFile.isEmpty())
        {
            const ComponentIniProperties windowProps = selectedPage == "npcinfo" ? npcInfoProperties() : isNoticePage(selectedPage) ? noticeWindowProperties() : propertiesForResource(currentDefinition.windowFile);
            propertyEditor->setProperties(windowProps);
            windowWidth = windowProps.width;
            windowHeight = windowProps.height;
            windowImage = windowProps.image;
            windowBitmap = windowProps.bitmap;
            windowAlign = windowProps.align;
            windowAlignX = windowProps.alignX;
            windowAlignY = windowProps.alignY;
            windowStretch = windowProps.stretch;
        }

        propertyEditor->setMenuWindowProperties(
            currentDefinition.windowFile,
            windowWidth, windowHeight,
            windowImage, windowBitmap, windowAlign,
            windowAlignX, windowAlignY,
            windowStretch);

        updatingFromCode = false;
        return;
    }

    if (selectedItemType == SelectedItemType::Component)
    {
        if (selectedComponentIndex < 0 || selectedComponentIndex >= currentDefinition.components.size())
        {
            propertyEditor->clearAll();
            return;
        }

        const auto& definition = currentDefinition.components[selectedComponentIndex];

        updatingFromCode = true;
        const auto fields = editableFields(definition.editorId);
        auto* explanation = findChild<QLabel*>("menuObjectDescription");
        if (selectedPage == "choose" && definition.name == "selectA")
            explanation->setText(tr("所有选项共用这套位置、行高和样式。实际文字与选择后的剧情由脚本提供，可在“预览游戏窗口”中输入临时选项检查效果。"));
        else if (selectedPage == "mapthumbnail" && definition.name == "mapNameLabel")
            explanation->setText(tr("游戏加载小地图时会清空并隐藏地图名称，画布中不显示。标题文字和底部提示请分别选择对应对象编辑。"));
        else if (definition.type.contains("Button") || definition.type == "CheckBox")
            explanation->setText(fields.contains("text") ? tr("功能：%1。显示文字可编辑，按钮的游戏操作保持固定。").arg(objectTitle(definition.name, definition.type))
                : definition.type == "Button" ? tr("功能：%1。文字绘制在按钮图片内；可通过图片和三个状态帧调整外观。").arg(objectTitle(definition.name, definition.type))
                : tr("功能：%1。文字随游戏设置或状态更新；此处调整外观，切换下方按钮状态可检查对应效果。").arg(objectTitle(definition.name, definition.type)));
        else if (fields.contains("text"))
            explanation->setText(tr("显示文字可在下方“文本”中修改；保存后游戏会读取该文字。"));
        else if (definition.type == "ImageContainer" || definition.type == "TransImage")
            explanation->setText(fields.contains("image") ? tr("图片对象：修改图片、位置或尺寸可调整外观；图片中绘制的文字需要修改图片素材。")
                : tr("图像内容由游戏生成；此处可调整已开放的显示属性。"));
        else if (!definition.bind.isEmpty() || !fields.contains("text"))
            explanation->setText(tr("内容由游戏或剧情提供。此处调整显示布局，预览中的示例内容不会保存为游戏数据。"));
        propertyEditor->setComponentType(definition.type);
        propertyEditor->setDefinitionProperties(definition.name, definition.file, definition.bind, definition.format);
        propertyEditor->setControllerNavigationOverrides(
            definition.controllerUp, definition.controllerDown,
            definition.controllerLeft, definition.controllerRight);

        if (!definition.file.isEmpty())
        {
            propertyEditor->setProperties(propertiesForResource(definition.file));
            if (selectedPage == "choose" && definition.name == "selectA")
                for (const auto& other : currentDefinition.components)
                    if (other.name == "selectB")
                    {
                        const auto a = propertiesForResource(definition.file), b = propertiesForResource(other.file);
                        propertyEditor->setChoiceListSpacing(qMax(0, b.top - a.top - a.height), true);
                    }
        }
        else
        {
            propertyEditor->setProperties(ComponentIniProperties());
        }

        updatingFromCode = false;
    }
    else if (selectedItemType == SelectedItemType::SubComponent)
    {
        const MenuComponentDefinition* parentDefinition = nullptr;
        if (selectedComponentIndex >= 0 &&
            selectedComponentIndex < currentDefinition.components.size())
        {
            parentDefinition = &currentDefinition.components[selectedComponentIndex];
        }
        else if (selectedSubMenuIndex >= 0 &&
                 selectedSubMenuIndex < currentDefinition.subMenus.size() &&
                 selectedSubMenuComponentIndex >= 0 &&
                 selectedSubMenuComponentIndex <
                     currentDefinition.subMenus[selectedSubMenuIndex].components.size())
        {
            parentDefinition = &currentDefinition.subMenus[selectedSubMenuIndex]
                .components[selectedSubMenuComponentIndex];
        }

        if (!parentDefinition)
        {
            propertyEditor->clearAll();
            return;
        }

        updatingFromCode = true;
        propertyEditor->setComponentType("DragButton");
        propertyEditor->setDefinitionProperties(
            selectedSubComponentName,
            parentDefinition->file + "/" + selectedSubComponentName,
            "", "");

        if (!selectedSubComponentPath.isEmpty())
        {
            propertyEditor->setProperties(propertiesForResource(selectedSubComponentPath));
        }
        else
        {
            propertyEditor->setProperties(ComponentIniProperties());
        }

        updatingFromCode = false;
    }
    else if (selectedItemType == SelectedItemType::SubMenu)
    {
        if (selectedSubMenuIndex < 0 || selectedSubMenuIndex >= currentDefinition.subMenus.size())
        {
            propertyEditor->clearAll();
            return;
        }

        const auto& subMenu = currentDefinition.subMenus[selectedSubMenuIndex];

        updatingFromCode = true;
        propertyEditor->setComponentType("SubMenu");
        propertyEditor->setDefinitionProperties(subMenu.name, subMenu.file, "", "");
        propertyEditor->setSubMenuWindowFile(subMenu.windowFile);
        propertyEditor->setSubMenuBackgroundImage(subMenu.backgroundImage);
        propertyEditor->setSubMenuWindowRect(subMenu.windowRect);

        updatingFromCode = false;
    }
    else if (selectedItemType == SelectedItemType::SubMenuComponent)
    {
        if (selectedSubMenuIndex < 0 || selectedSubMenuIndex >= currentDefinition.subMenus.size())
        {
            propertyEditor->clearAll();
            return;
        }

        const auto& subMenu = currentDefinition.subMenus[selectedSubMenuIndex];

        if (selectedSubMenuComponentIndex < 0 || selectedSubMenuComponentIndex >= subMenu.components.size())
        {
            propertyEditor->clearAll();
            return;
        }

        const auto& definition = subMenu.components[selectedSubMenuComponentIndex];

        updatingFromCode = true;
        propertyEditor->setComponentType(definition.type);
        propertyEditor->setDefinitionProperties(definition.name, definition.file, definition.bind, definition.format);
        propertyEditor->setControllerNavigationOverrides(
            definition.controllerUp, definition.controllerDown,
            definition.controllerLeft, definition.controllerRight);

        if (!definition.file.isEmpty())
        {
            propertyEditor->setProperties(propertiesForResource(definition.file));
            if (selectedPage == "choose" && definition.name == "selectA")
                for (const auto& other : currentDefinition.components)
                    if (other.name == "selectB")
                    {
                        const auto a = propertiesForResource(definition.file), b = propertiesForResource(other.file);
                        propertyEditor->setChoiceListSpacing(qMax(0, b.top - a.top - a.height), true);
                    }
        }
        else
        {
            propertyEditor->setProperties(ComponentIniProperties());
        }

        updatingFromCode = false;
    }
}

void MenuEditorWindow::updatePreview()
{
    previewInterfaceOrigin = {};
    const auto presentation = qScopeGuard([this]() { finishPreviewPresentation(); });
    if (selectedPage == "dicegame") { updateDicePreview(); return; }
    if (previewCanvas->property("dicePage").toBool())
    {
        previewCanvas->setScaleFactor(previewCanvas->property("dicePreviousScale").toDouble());
        previewCanvas->setProperty("dicePage",false);
    }
    bagGridAction->setEnabled(canEditPage() && isGoodsMenu());
    saveLoadContextAction->setVisible(selectedPage == "saveload");
    optionViewAction->setVisible(selectedPage == "option");
    optionControllerAction->setVisible(selectedPage == "option");
    const auto optionValues = selectedPage == "option" ? optionDefaultProperties(optionControllerPreview->isChecked()) : QMap<QString, ComponentIniProperties>();
    const bool tooltip = isToolTipMenu();
    const bool titlePage = selectedPage == "title";
    bool paged = false;
    for (const auto& component : currentDefinition.components)
        paged |= component.name == "body" && component.type == "MemoText";
    const bool magic = pageEntryResource.endsWith("/magic/magic.menu.ini");
    const bool equipment = pageEntryResource.endsWith("/equip/equip.menu.ini");
    bool magicDetails = false, equipmentTabs = false;
    for (const auto& component : currentDefinition.components)
    {
        magicDetails |= component.name == "detailText" && component.type == "MemoText";
        equipmentTabs |= component.name == "attributesTab" && component.type == "CheckBox";
    }
    const QStringList storageStates = storagePreviewStates();
    const QStringList conversationStates = selectedPage=="controllerhelp"?controllerHelpPreviewStates():selectedPage == "skills" ? skillsPreviewStates() : selectedPage == "joystick" ? joystickPreviewStates() : selectedPage == "npcinfo" ? npcInfoPreviewStates() : selectedPage == "mapthumbnail" ? mapThumbnailPreviewStates() : isHudPage(selectedPage) ? hudPreviewStates() : isNoticePage(selectedPage) ? noticePreviewStates() : selectedPage == "message" || selectedPage == "timer" ? passivePreviewStates() : conversationPreviewStates();
    const int count = !conversationStates.isEmpty() ? conversationStates.size() : selectedPage == "titleteam" ? 4 : !storageStates.isEmpty() ? storageStates.size() : titlePage ? 4 : (selectedPage == "yesno" ? 2 : (tooltip ? (paged ? 5 : 2) : (magic ? (magicDetails ? 5 : 2)
        : (equipment && equipmentTabs ? 3 : (selectedPage == "memo" || selectedPage == "xiulian" ? 2 : 0)))));
    const QString previewContext = selectedPage + pageEntryResource;
    if (toolTipPreviewCombo->count() != count || toolTipPreviewCombo->property("previewContext").toString() != previewContext)
    {
        const QSignalBlocker blocker(toolTipPreviewCombo);
        toolTipPreviewCombo->setProperty("previewContext", previewContext);
        toolTipPreviewCombo->clear();
        if (!conversationStates.isEmpty()) toolTipPreviewCombo->addItems(conversationStates);
        else if (selectedPage == "titleteam") toolTipPreviewCombo->addItems({tr("本包正文：首屏"), tr("本包正文：末屏"), tr("长正文（临时示例）"), tr("文字加视频（兼容示例）")});
        else if (!storageStates.isEmpty()) toolTipPreviewCombo->addItems(storageStates);
        else if (titlePage) toolTipPreviewCombo->addItems({tr("设计画布"), "640 × 480", "1280 × 720", "960 × 540"});
        else if (tooltip)
        {
            toolTipPreviewCombo->addItems({tr("布局编辑（全部组件）"), tr("悬停提示（示例）")});
            if (paged)
                toolTipPreviewCombo->addItems({tr("展开详情：第一页"), tr("展开详情：中间页"), tr("展开详情：末页")});
        }
        else if (count > 0)
        {
            toolTipPreviewCombo->addItem(tr("布局编辑（全部组件）"));
            if (magic)
            {
                toolTipPreviewCombo->addItem(tr("武功列表（示例）"));
                if (magicDetails) toolTipPreviewCombo->addItems({tr("展开详情：第一页"), tr("展开详情：中间页"), tr("展开详情：末页")});
            }
            else if (equipment) toolTipPreviewCombo->addItems({tr("装备面板（示例）"), tr("属性面板（示例）")});
            else toolTipPreviewCombo->addItem(tr("运行内容（示例）"));
        }
    }
    toolTipPreviewAction->setVisible(count > 0);
    if (previewModeCombo && previewModeCombo->currentIndex() == 1 && (selectedPage == "choose" || selectedPage == "dialog") && toolTipPreviewCombo->currentIndex() == 0)
    { const QSignalBlocker blocker(toolTipPreviewCombo); toolTipPreviewCombo->setCurrentIndex(1); }
    const int state = toolTipPreviewCombo->currentIndex();
    previewCanvas->clear();
    previewCanvas->setAspectFitViewport(titlePage && state > 0 ? QList<QSize>{QSize(640,480), QSize(1280,720), QSize(960,540)}[state-1] : QSize());

    if (!currentDefinition.windowFile.isEmpty())
    {
        const ComponentIniProperties windowProps = propertiesForResource(currentDefinition.windowFile);

        QRect menuRect(0, 0, windowProps.width, selectedPage == "option" ? optionDefaultHeight : windowProps.height);
        previewCanvas->setMenuRect(menuRect);

        const QString background = windowProps.image.isEmpty() ? windowProps.bitmap : windowProps.image;
        if (!background.isEmpty())
            previewCanvas->setBackgroundImage(background);
        previewCanvas->setWindowStretch(titlePage || windowProps.stretch, titlePage || windowProps.keepAspect);
        const int border = windowProps.rawValues.value("nineslice").toInt();
        previewCanvas->setWindowNineSlice(border, windowProps.rawValues.value("nineslicewidth", QString::number(border)).toInt());
    }

    QList<PreviewComponent> previewComponents;
    for (const auto& definition : currentDefinition.components)
    {
        PreviewComponent previewComponent;
        previewComponent.editorId = definition.editorId;
        previewComponent.movable = canEditGeometry(definition.editorId, false);
        previewComponent.resizable = canEditGeometry(definition.editorId, true);
        previewComponent.type = definition.type;
        previewComponent.name = definition.name;

        if (!definition.file.isEmpty())
        {
            const ComponentIniProperties props = selectedPage == "option" && definition.runtimeDefault
                ? optionValues.value(definition.name) : propertiesForResource(definition.file);
            previewComponent.rect = QRect(props.left, props.top, props.width, props.height);
            setPreviewComponentImages(definition.type, props, previewComponent);
            previewComponent.stretch = resolveStretch(props, definition.type);
            if (titlePage && QStringList{"initBtn", "loadBtn", "teamBtn", "exitBtn"}.contains(definition.name)) previewComponent.stretch = true;
            previewComponent.text = props.text;
            previewComponent.fontSize = props.font;
            previewComponent.color = props.color;
            previewComponent.rawValues = props.rawValues;
            previewComponent.rawValues["autoshrink"] = props.autoShrink ? "true" : "false";
            if (selectedPage == "yesno" && state > 0 && definition.name == "label") previewComponent.text = tr("确认操作？（示例，不保存）");
            for (const QString& colorKey : {QString("backgroundcolor"), QString("hoverbackgroundcolor"), QString("pressedbackgroundcolor")})
                if (props.rawValues.contains(colorKey)) previewComponent.rawValues[colorKey] = QString::number(parseEditorColor(props.rawValues.value(colorKey), 0xFFFFFFFF));
            if (tooltip && state > 0)
            {
                const bool expanded = state > 1;
                const QString& name = definition.name;
                if (paged && (name == "intro1" || name == "intro2")) previewComponent.visible = false;
                if (name == "back") previewComponent.visible = expanded;
                if (name == "previous") previewComponent.visible = expanded && state > 2;
                if (name == "next") previewComponent.visible = expanded && state < 4;
                if (name == "hint") previewComponent.visible = !expanded;
                if (name == "page") previewComponent.text = QString("%1 / 3").arg(qMax(1, state - 1));
                if (name == "name") previewComponent.text = tr("物品名称（示例）");
                if (name == "cost") previewComponent.text = tr("价格：100（示例）");
                if (name == "body" || name == "intro1" || name == "intro2")
                    previewComponent.text = tr("物品说明示例，用于检查文字区域、换行与字号。实际内容由游戏生成。\n第二行说明。");
            }
            if (state > 0 && !tooltip)
            {
                const QString& name = definition.name;
                if (magic && magicDetails)
                {
                    const bool detail = state > 1;
                    if (!name.startsWith("heading")) previewComponent.visible = name.startsWith("detail") == detail;
                    if (name == "magicTab" || name == "talentTab") previewComponent.visible = false;
                    if (name == "detailPrevious") previewComponent.visible = detail && state > 2;
                    if (name == "detailNext") previewComponent.visible = detail && state < 4;
                    if (name == "detailPractice") previewComponent.visible = detail && pageResources.practiceAvailable();
                    if (name == "heading") previewComponent.text = detail ? tr("武功详情（示例）") : tr("武功（示例）");
                    if (name == "detailName") previewComponent.text = tr("武功名称（示例）");
                    if (name == "detailPage") previewComponent.text = QString("%1 / 3").arg(state - 1);
                    if (name == "detailText") previewComponent.text = tr("武功等级：1\n升级经验：0 / 100\n消耗：气 10\n说明内容由游戏生成。\n分页示例。");
                    if (name == "detailPractice") previewComponent.text = tr("设为修炼");
                    if (name == "detailQuick") previewComponent.text = tr("放入快捷栏");
                }
                if (equipment && equipmentTabs)
                {
                    if (name.startsWith("detail")) previewComponent.visible = state == 2;
                    else if (name.startsWith("item") || name.startsWith("part") || name.startsWith("lab")) previewComponent.visible = state == 1;
                    if (definition.type == "Label" && QRegularExpression("^detail[0-9]+$").match(name).hasMatch()) previewComponent.text = tr("属性数值：100（示例）");
                }
                if (selectedPage == "xiulian")
                {
                    if (name == "name") previewComponent.text = tr("修炼武功（示例）");
                    if (name == "intro")
                    {
                        previewComponent.text = tr("修炼说明由游戏生成。此处检查换行与缩字。\n第二行示例。");
                        previewComponent.rawValues["autonextline"] = "true";
                        previewComponent.rawValues["autoshrink"] = "true";
                    }
                    if (name == "level") previewComponent.text = "1";
                    if (name == "exp") previewComponent.text = "0 / 100";
                    if (name == "level" || name == "exp") previewComponent.rawValues["autoshrink"] = "true";
                }
                if (selectedPage == "memo" && name == "memoText")
                    previewComponent.text = tr("第一条事件（示例）\n第二条事件（示例）\n第三条事件（示例）\n第四条事件（示例）\n第五条事件（示例）");
            }

            if (definition.type == "ListBox")
            {
                previewComponent.listBoxItems = props.items;
                previewComponent.itemHeight = props.itemHeight;
                previewComponent.color = props.color;
                previewComponent.selColor = props.selColor;
                previewComponent.fontSize = props.fontSet ? props.font : 20;
                previewComponent.fitItems = QStringList{"true","1","yes","on"}.contains(props.rawValues.value("fititems").toLower());
            }

            if (definition.type == "Scrollbar" && !props.slideBtn.isEmpty())
            {
                const QString slideResource = normalizedResourcePath(QDir::cleanPath(
                    QFileInfo(normalizedResourcePath(definition.file)).path() + "/" + props.slideBtn));
                const ComponentIniProperties slideBtnProps = propertiesForResource(slideResource);

                PreviewComponent slideBtnPreview;
                slideBtnPreview.editorId = definition.editorId + "/slideBtn";
                slideBtnPreview.movable = false;
                slideBtnPreview.resizable = false;
                slideBtnPreview.type = "DragButton";
                slideBtnPreview.name = definition.name + "_slideBtn";
                slideBtnPreview.rect = QRect(
                    props.left + slideBtnProps.left,
                    props.top + slideBtnProps.top,
                    slideBtnProps.width,
                    slideBtnProps.height);
                slideBtnPreview.rawValues = slideBtnProps.rawValues;
                if (selectedPage == "option" || selectedPage == "buysell")
                {
                    const bool fitted = QStringList{"true","1","yes","on"}.contains(props.rawValues.value("fittrack").toLower());
                    if (fitted)
                    {
                        if (props.style == 1) slideBtnPreview.rect.setWidth(props.width);
                        else slideBtnPreview.rect.setHeight(props.height);
                    }
                    const int begin = fitted ? 0 : props.slideBegin;
                    const int end = fitted ? qMax(0, props.style == 1 ? props.height-slideBtnPreview.rect.height() : props.width-slideBtnPreview.rect.width()) : props.slideEnd;
                    if (props.style == 1) slideBtnPreview.rect.moveTop(props.top+(begin+end)/2);
                    else slideBtnPreview.rect.moveLeft(props.left+(begin+end)/2);
                }
                setPreviewComponentImages("DragButton", slideBtnProps, slideBtnPreview);
                slideBtnPreview.stretch = resolveStretch(slideBtnProps, "DragButton");

                previewComponent.children.append(slideBtnPreview);
            }
        }

        adjustOptionPreview(definition, previewComponent);
        previewComponents.append(previewComponent);
    }

    updatePartnerHeadPreview(previewComponents);
    updateStoragePreview(previewComponents);
    updateTitleTeamPreview(previewComponents);
    updateConversationPreview(previewComponents);
    updateNoticePreview(previewComponents);
    updateHudPreview(previewComponents);
    updateMapThumbnailPreview(previewComponents);
    updateNpcInfoPreview(previewComponents);
    updateControllerHelpPreview(previewComponents);
    updateJoystickPreview(previewComponents);
    updateSkillsPreview(previewComponents);
    previewCanvas->setComponents(previewComponents);

    QList<PreviewSubMenu> previewSubMenus;
    for (const auto& subMenu : currentDefinition.subMenus)
    {
        PreviewSubMenu psm;
        psm.editorId = subMenu.editorId;
        psm.name = subMenu.name;
        psm.windowRect = subMenu.windowRect;
        psm.backgroundImage = subMenu.backgroundImage;
        for (const MenuComponentDefinition& definition : subMenu.components)
        {
            PreviewComponent component;
            component.movable = false;
            component.resizable = false;
            component.editorId = definition.editorId;
            component.type = definition.type;
            component.name = definition.name;
            if (!definition.file.isEmpty())
            {
                const ComponentIniProperties properties = propertiesForResource(definition.file);
                component.rect = QRect(properties.left, properties.top,
                                       properties.width, properties.height);
                component.rawValues = properties.rawValues;
                component.text = properties.text;
                component.fontSize = properties.font;
                component.color = properties.color;
                setPreviewComponentImages(definition.type, properties, component);
                component.stretch = resolveStretch(properties, definition.type);
                if (definition.type == "ListBox")
                {
                    component.listBoxItems = properties.items;
                    component.itemHeight = properties.itemHeight;
                    component.color = properties.color;
                    component.selColor = properties.selColor;
                    component.fontSize = properties.fontSet ? properties.font : 20;
                    component.fitItems = QStringList{"true","1","yes","on"}.contains(properties.rawValues.value("fititems").toLower());
                }
                if (definition.type == "Scrollbar" && !properties.slideBtn.isEmpty())
                {
                    const QString slideResource = normalizedResourcePath(QDir::cleanPath(
                        QFileInfo(normalizedResourcePath(definition.file)).path() + "/" + properties.slideBtn));
                    const ComponentIniProperties slide = propertiesForResource(slideResource);
                    PreviewComponent child;
                    child.movable = false;
                    child.resizable = false;
                    child.editorId = definition.editorId + "/slideBtn";
                    child.type = "DragButton";
                    child.rawValues = slide.rawValues;
                    child.name = definition.name + "_slideBtn";
                    child.rect = QRect(properties.left + slide.left,
                                       properties.top + slide.top,
                                       slide.width, slide.height);
                    setPreviewComponentImages("DragButton", slide, child);
                    child.stretch = resolveStretch(slide, "DragButton");
                    component.children.append(child);
                }
            }
            psm.components.append(component);
        }
        previewSubMenus.append(psm);
    }
    previewCanvas->setSubMenus(previewSubMenus);
}

bool MenuEditorWindow::loadFromMenuFile(const QString& filePath)
{
    if (requestedPage == "dicegame" || documentPage(filePath) == "dicegame") return loadDicePage(filePath);
    if (requestedPage == "npcinfo" || documentPage(filePath) == "npcinfo" || requestedPage=="controllerhelp" || documentPage(filePath)=="controllerhelp") return loadFixedWindowPage(filePath);
    QString menuResourcePath;
    if (!resourcePathForFile(filePath, menuResourcePath))
    {
        QMessageBox::warning(this, tr("错误"),
            tr("菜单文件不在当前资源包或其 UI 回退根中，已拒绝打开:\n%1").arg(filePath));
        return false;
    }

    if ((requestedPage == "yesno" || documentPage(filePath) == "yesno") && pageResources.yesNoFallback(selectedTheme))
        return loadYesNoFallback(filePath);
    if ((requestedPage == "partnerhead" || requestedPage == "titleteam" || isNoticePage(requestedPage)) && pageResources.valid
        && !QFileInfo::exists(filePath) && filePath == resolvedPageSource(requestedPage))
    {
        currentDefinition = MenuDefinition();
        currentDefinition.menuName = isNoticePage(requestedPage) ? (requestedPage=="systemnotice"?"SystemNotice":"ScriptMessages") : requestedPage == "titleteam" ? "TitleTeamMenu" : "PartnerHeadMenu";
        originalMenuFileLines.clear();
        documentStructureIssue.clear();
        return true;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("错误"),
            tr("无法打开文件: %1").arg(filePath));
        return false;
    }

    currentDefinition = MenuDefinition();
    originalMenuFileLines.clear();
    documentStructureIssue.clear();

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);

    QString currentSection;
    bool menuSectionSeen = false;
    QMap<int, MenuComponentDefinition> componentMap;
    QMap<int, SubMenuDefinition> subMenuMap;

    while (!in.atEnd())
    {
        QString line = in.readLine();
        originalMenuFileLines << line;

        const QString trimmed = trimRuntimeIniWhitespace(line);
        if (trimmed.isEmpty() || trimmed.startsWith(";") || trimmed.startsWith("#"))
        {
            continue;
        }

        if (trimmed.startsWith(QLatin1Char('[')))
        {
            QString sectionName;
            if (parseRuntimeIniSection(line, sectionName))
            {
                currentSection = sectionName.toLower();
                if (currentSection == "menu")
                    menuSectionSeen = true;
            }
            continue;
        }

        QString key;
        QString value;
        if (!parseRuntimeIniEntry(line, key, value))
        {
            continue;
        }

        if (currentSection == "menu")
        {
            if (key == "name") currentDefinition.menuName = value;
            else if (key == "visible") currentDefinition.visible = (value == "1" || value.toLower() == "true");
            else if (key == "window") currentDefinition.windowFile = value;
        }
        else if (currentSection.startsWith("submenu"))
        {
            bool ok = false;
            int index = currentSection.mid(7).toInt(&ok);
            if (ok)
            {
                if (key == "name") subMenuMap[index].name = value;
                else if (key == "file") subMenuMap[index].file = value;
            }
        }
        else if (currentSection.startsWith("component"))
        {
            bool ok = false;
            int index = currentSection.mid(9).toInt(&ok);
            if (ok)
            {
                if (key == "type") componentMap[index].type = value;
                else if (key == "name") componentMap[index].name = value;
                else if (key == "file") componentMap[index].file = value;
                else if (key == "bind") componentMap[index].bind = value;
                else if (key == "format") componentMap[index].format = value;
                else if (key == "controllerup") componentMap[index].controllerUp = value;
                else if (key == "controllerdown") componentMap[index].controllerDown = value;
                else if (key == "controllerleft") componentMap[index].controllerLeft = value;
                else if (key == "controllerright") componentMap[index].controllerRight = value;
                else if (key == "optionlayout") componentMap[index].optionLayout = value;
                else if (key == "spacingy") componentMap[index].spacingY = value;
            }
        }
    }

    file.close();

    if (!menuSectionSeen)
    {
        QMessageBox::warning(this, tr("错误"),
            tr("菜单文件缺少 [menu] 段，原文档保持不变: %1").arg(filePath));
        return false;
    }

    QList<int> sortedKeys = componentMap.keys();
    std::sort(sortedKeys.begin(), sortedKeys.end());
    int expectedComponent = 1;
    for (int key : sortedKeys)
    {
        if (key != expectedComponent++) documentStructureIssue = tr("组件序号不连续，游戏会在缺号处停止读取；本页只读。");
        QString section;
        for (const QString& line : originalMenuFileLines)
        {
            QString parsed;
            if (parseRuntimeIniSection(line, parsed)) section = parsed.toLower();
            if (section == "component" + QString::number(key)) componentMap[key].originalLines.append(line);
        }
        componentMap[key].editorId =
            "component/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        currentDefinition.components.append(componentMap[key]);
    }

    QList<int> sortedSubMenuKeys = subMenuMap.keys();
    std::sort(sortedSubMenuKeys.begin(), sortedSubMenuKeys.end());
    int expectedSubMenu = 1;
    for (int key : sortedSubMenuKeys)
    {
        if (key != expectedSubMenu++) documentStructureIssue = tr("子菜单序号不连续，本页只读，未自动修复。");
        SubMenuDefinition& subMenu = subMenuMap[key];
        subMenu.editorId =
            "submenu/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        loadSubMenuDefinition(subMenu);
        currentDefinition.subMenus.append(subMenu);
    }

    updatingFromCode = true;
    ui->menuNameEdit->setText(currentDefinition.menuName);
    ui->menuVisibleCheck->setChecked(currentDefinition.visible);
    updatingFromCode = false;
    return true;
}

QStringList MenuEditorWindow::saveToMenuFile(const QString& filePath)
{
    MenuDefinition savedDefinition = currentDefinition;
    if (selectedPage == "titleteam" && savedDefinition.windowFile.startsWith("ini/ui/titleteam/runtime-default/")) savedDefinition.windowFile.clear();
    if (isNoticePage(selectedPage) && savedDefinition.windowFile.startsWith("ini/ui/"+selectedPage+"/runtime-default/")) savedDefinition.windowFile.clear();
    for (int i = savedDefinition.components.size() - 1; i >= 0; --i)
        if (savedDefinition.components[i].runtimeDefault) savedDefinition.components.removeAt(i);
    QStringList outputLines;

    QSet<QString> menuKeys = {"name", "visible", "window"};
    QSet<QString> componentKeys = {
        "type", "name", "file", "bind", "format",
        "controllerup", "controllerdown", "controllerleft", "controllerright", "optionlayout"};
    if (selectedPage == "partnerhead") componentKeys.insert("spacingy");
    const QSet<QString> controllerNavigationKeys = {
        "controllerup", "controllerdown", "controllerleft", "controllerright"};
    QSet<QString> subMenuKeys = {"name", "file"};

    QMap<QString, QString> menuValues;
    menuValues["name"] = savedDefinition.menuName;
    menuValues["visible"] = savedDefinition.visible ? "true" : "false";
    menuValues["window"] = savedDefinition.windowFile;

    QMap<int, QMap<QString, QString>> componentValues;
    for (int i = 0; i < savedDefinition.components.size(); i++)
    {
        const auto& definition = savedDefinition.components[i];
        componentValues[i + 1]["type"] = definition.type;
        componentValues[i + 1]["name"] = definition.name;
        componentValues[i + 1]["file"] = definition.file;
        if (!definition.optionLayout.isEmpty())
            componentValues[i + 1]["optionlayout"] = definition.optionLayout;
        if (selectedPage == "partnerhead" && definition.name == "head" && !definition.spacingY.isEmpty()) componentValues[i + 1]["spacingy"] = definition.spacingY;
        if (!definition.bind.isEmpty())
            componentValues[i + 1]["bind"] = definition.bind;
        if (!definition.format.isEmpty() && definition.format != "%d")
            componentValues[i + 1]["format"] = definition.format;
        if (!definition.controllerUp.isEmpty())
            componentValues[i + 1]["controllerup"] = definition.controllerUp;
        if (!definition.controllerDown.isEmpty())
            componentValues[i + 1]["controllerdown"] = definition.controllerDown;
        if (!definition.controllerLeft.isEmpty())
            componentValues[i + 1]["controllerleft"] = definition.controllerLeft;
        if (!definition.controllerRight.isEmpty())
            componentValues[i + 1]["controllerright"] = definition.controllerRight;
    }

    QMap<int, QMap<QString, QString>> subMenuValues;
    for (int i = 0; i < savedDefinition.subMenus.size(); i++)
    {
        const auto& subMenu = savedDefinition.subMenus[i];
        subMenuValues[i + 1]["name"] = subMenu.name;
        subMenuValues[i + 1]["file"] = subMenu.file;
    }

    QSet<int> writtenComponents;
    QSet<int> writtenSubMenus;
    bool menuSectionWritten = false;

    if (!originalMenuFileLines.isEmpty())
    {
        QString currentSection;
        int currentComponentIndex = -1;
        int currentSubMenuIndex = -1;
        QSet<QString> writtenKeysInMenu;
        QSet<QString> writtenKeysInComponent;
        QSet<QString> writtenKeysInSubMenu;

        auto flushMissingMenuKeys = [&]() {
            for (const QString& key : menuKeys)
            {
                if (!writtenKeysInMenu.contains(key) && menuValues.contains(key))
                    outputLines << key + "=" + menuValues[key];
            }
        };

        auto flushMissingComponentKeys = [&]() {
            if (currentComponentIndex > 0 && componentValues.contains(currentComponentIndex))
            {
                const auto& values = componentValues[currentComponentIndex];
                for (const QString& key : componentKeys)
                {
                    if (!writtenKeysInComponent.contains(key) && values.contains(key))
                    {
                        outputLines << key + "=" + values[key];
                    }
                }
            }
        };

        auto flushMissingSubMenuKeys = [&]() {
            if (currentSubMenuIndex > 0 && subMenuValues.contains(currentSubMenuIndex))
            {
                const auto& values = subMenuValues[currentSubMenuIndex];
                for (const QString& key : subMenuKeys)
                {
                    if (!writtenKeysInSubMenu.contains(key) && values.contains(key))
                    {
                        outputLines << key + "=" + values[key];
                    }
                }
            }
        };

        QStringList sourceLines;
        bool componentSection = false;
        bool componentsInserted = false;
        for (const QString& line : originalMenuFileLines)
        {
            QString section;
            if (parseRuntimeIniSection(line, section))
            {
                componentSection = QRegularExpression("^component[0-9]+$", QRegularExpression::CaseInsensitiveOption).match(section).hasMatch();
                if (componentSection)
                {
                    if (!componentsInserted)
                    {
                        for (int i = 0; i < savedDefinition.components.size(); ++i)
                        {
                            auto lines = savedDefinition.components[i].originalLines;
                            if (lines.isEmpty()) continue;
                            const int begin = lines[0].indexOf('[');
                            const int end = lines[0].indexOf(']', begin + 1);
                            if (begin >= 0 && end > begin)
                                lines[0].replace(begin + 1, end - begin - 1, "component" + QString::number(i + 1));
                            sourceLines.append(lines);
                        }
                        componentsInserted = true;
                    }
                    continue;
                }
            }
            if (!componentSection) sourceLines.append(line);
        }
        for (const QString& origLine : sourceLines)
        {
            const QString trimmed = trimRuntimeIniWhitespace(origLine);
            QString parsedSectionName;

            if (parseRuntimeIniSection(origLine, parsedSectionName))
            {
                const QString sectionName = parsedSectionName.toLower();

                if (currentSection == "component")
                    flushMissingComponentKeys();
                else if (currentSection == "submenu")
                    flushMissingSubMenuKeys();
                else if (currentSection == "menu")
                    flushMissingMenuKeys();

                if (sectionName == "menu")
                {
                    currentSection = "menu";
                    menuSectionWritten = true;
                    writtenKeysInMenu.clear();
                    outputLines << origLine;
                    continue;
                }
                else if (sectionName.startsWith("component"))
                {
                    bool ok = false;
                    int index = sectionName.mid(9).toInt(&ok);
                    if (ok && index >= 1 && index <= savedDefinition.components.size())
                    {
                        currentSection = "component";
                        currentComponentIndex = index;
                        writtenComponents.insert(index);
                        writtenKeysInComponent.clear();
                        outputLines << origLine;
                        continue;
                    }
                    else if (ok)
                    {
                        currentSection = "skip_component";
                        currentComponentIndex = -1;
                        continue;
                    }
                    else
                    {
                        currentSection = sectionName;
                        currentComponentIndex = -1;
                        outputLines << origLine;
                        continue;
                    }
                }
                else if (sectionName.startsWith("submenu"))
                {
                    bool ok = false;
                    int index = sectionName.mid(7).toInt(&ok);
                    if (ok && index >= 1 && index <= savedDefinition.subMenus.size())
                    {
                        currentSection = "submenu";
                        currentSubMenuIndex = index;
                        writtenSubMenus.insert(index);
                        writtenKeysInSubMenu.clear();
                        outputLines << origLine;
                        continue;
                    }
                    else if (ok)
                    {
                        currentSection = "skip_submenu";
                        currentSubMenuIndex = -1;
                        continue;
                    }
                    else
                    {
                        currentSection = sectionName;
                        currentSubMenuIndex = -1;
                        outputLines << origLine;
                        continue;
                    }
                }
                else
                {
                    currentSection = sectionName;
                    outputLines << origLine;
                    continue;
                }
            }

            if (currentSection == "skip_component" || currentSection == "skip_submenu")
            {
                if (trimmed.isEmpty())
                    outputLines << origLine;
                continue;
            }

            if (trimmed.isEmpty())
            {
                outputLines << origLine;
                continue;
            }

            if (trimmed.startsWith(";") || trimmed.startsWith("#"))
            {
                outputLines << origLine;
                continue;
            }

            if (currentSection.isEmpty())
            {
                outputLines << origLine;
                continue;
            }

            if (currentSection == "menu")
            {
                QString key;
                QString ignoredValue;
                QString inlineCommentSuffix;
                if (!parseRuntimeIniEntry(
                        origLine, key, ignoredValue, &inlineCommentSuffix))
                {
                    outputLines << origLine;
                    continue;
                }
                writtenKeysInMenu.insert(key);
                if (menuKeys.contains(key) && menuValues.contains(key))
                {
                    outputLines << key + "=" + menuValues[key] +
                        inlineCommentSuffix;
                }
                else
                {
                    outputLines << origLine;
                }
            }
            else if (currentSection == "component" && currentComponentIndex > 0)
            {
                QString key;
                QString ignoredValue;
                QString inlineCommentSuffix;
                if (!parseRuntimeIniEntry(
                        origLine, key, ignoredValue, &inlineCommentSuffix))
                {
                    outputLines << origLine;
                    continue;
                }
                const bool duplicateControllerNavigationKey =
                    controllerNavigationKeys.contains(key) &&
                    writtenKeysInComponent.contains(key);
                writtenKeysInComponent.insert(key);
                if (duplicateControllerNavigationKey)
                    continue;
                if (componentKeys.contains(key) && (key != "spacingy"
                    || (savedDefinition.components[currentComponentIndex - 1].name == "head"
                        && componentValues[currentComponentIndex].contains(key))))
                {
                    if (componentValues[currentComponentIndex].contains(key))
                    {
                        outputLines << key + "=" +
                            componentValues[currentComponentIndex][key] +
                            inlineCommentSuffix;
                    }
                }
                else
                {
                    outputLines << origLine;
                }
            }
            else if (currentSection == "submenu" && currentSubMenuIndex > 0)
            {
                QString key;
                QString ignoredValue;
                QString inlineCommentSuffix;
                if (!parseRuntimeIniEntry(
                        origLine, key, ignoredValue, &inlineCommentSuffix))
                {
                    outputLines << origLine;
                    continue;
                }
                writtenKeysInSubMenu.insert(key);
                if (subMenuKeys.contains(key) && subMenuValues[currentSubMenuIndex].contains(key))
                {
                    outputLines << key + "=" +
                        subMenuValues[currentSubMenuIndex][key] +
                        inlineCommentSuffix;
                }
                else
                {
                    outputLines << origLine;
                }
            }
            else
            {
                outputLines << origLine;
            }
        }

        if (currentSection == "component")
            flushMissingComponentKeys();
        else if (currentSection == "submenu")
            flushMissingSubMenuKeys();
        else if (currentSection == "menu")
            flushMissingMenuKeys();
    }

    if (!menuSectionWritten)
    {
        outputLines << "[menu]";
        outputLines << "name=" + savedDefinition.menuName;
        outputLines << QString("visible=") + (savedDefinition.visible ? "true" : "false");
        outputLines << "window=" + savedDefinition.windowFile;
        outputLines << "";
    }

    for (int i = 0; i < savedDefinition.components.size(); i++)
    {
        int index = i + 1;
        if (writtenComponents.contains(index))
            continue;

        outputLines << "";
        outputLines << "[component" + QString::number(index) + "]";
        const auto& definition = savedDefinition.components[i];
        outputLines << "type=" + definition.type;
        outputLines << "name=" + definition.name;
        outputLines << "file=" + definition.file;
        if (!definition.optionLayout.isEmpty())
            outputLines << "optionlayout=" + definition.optionLayout;
        if (!definition.bind.isEmpty())
            outputLines << "bind=" + definition.bind;
        if (!definition.format.isEmpty() && definition.format != "%d")
            outputLines << "format=" + definition.format;
        if (!definition.controllerUp.isEmpty())
            outputLines << "controllerup=" + definition.controllerUp;
        if (!definition.controllerDown.isEmpty())
            outputLines << "controllerdown=" + definition.controllerDown;
        if (!definition.controllerLeft.isEmpty())
            outputLines << "controllerleft=" + definition.controllerLeft;
        if (!definition.controllerRight.isEmpty())
            outputLines << "controllerright=" + definition.controllerRight;
    }

    for (int i = 0; i < savedDefinition.subMenus.size(); i++)
    {
        int index = i + 1;
        if (writtenSubMenus.contains(index))
            continue;

        outputLines << "";
        outputLines << "[submenu" + QString::number(index) + "]";
        const auto& subMenu = savedDefinition.subMenus[i];
        outputLines << "name=" + subMenu.name;
        outputLines << "file=" + subMenu.file;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return QStringList();
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    for (const QString& line : outputLines)
    {
        out << line << "\n";
    }

    if (out.status() != QTextStream::Ok)
    {
        file.close();
        return QStringList();
    }

    file.close();

    return outputLines;
}

void MenuEditorWindow::readComponentIniProperties(const QString& iniPath, ComponentIniProperties& props)
{
    if (componentIniCache.contains(iniPath))
    {
        props = componentIniCache[iniPath];
        return;
    }

    QFile file(iniPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return;
    }

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    QString currentSection;
    QMap<int, QString> indexedItems;

    auto parseBoolean = [](const QString& value, bool defaultValue = false)
    {
        const QString normalized = value.trimmed().toLower();
        if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on")
            return true;
        if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off")
            return false;
        return defaultValue;
    };

    while (!in.atEnd())
    {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(";") || line.startsWith("#"))
        {
            continue;
        }

        QString section;
        if (parseRuntimeIniSection(line, section))
        {
            currentSection = section.toLower();
            continue;
        }

        QString key, value;
        if (!parseRuntimeIniEntry(line, key, value)) continue;

        if (currentSection == "items")
        {
            bool ok = false;
            const int index = key.toInt(&ok);
            if (ok && index > 0 && key == QString::number(index))
                indexedItems[index] = value;
            continue;
        }

        // Runtime component implementations consume known keys from [Init].
        // Equal-looking keys in extension sections must remain opaque.
        if (currentSection != "init")
            continue;

        props.rawValues[key] = value;

        if (key == "name") { props.name = value; props.nameSet = true; }
        else if (key == "left") props.left = value.toInt();
        else if (key == "top") props.top = value.toInt();
        else if (key == "width") props.width = value.toInt();
        else if (key == "height") props.height = value.toInt();
        else if (key == "image") props.image = value;
        else if (key == "bitmap") { props.bitmap = value; props.bitmapSet = true; }
        else if (key == "baseimage") props.baseImage = value;
        else if (key == "thumbimage") props.thumbImage = value;
        else if (key == "align") props.align = value;
        else if (key == "alignx") props.alignX = value.toInt();
        else if (key == "aligny") props.alignY = value.toInt();
        else if (key == "sound") props.sound = value;
        else if (key == "kind") props.kind = value;
        else if (key == "up") props.up = value.toInt();
        else if (key == "down") props.down = value.toInt();
        else if (key == "track") props.track = value.toInt();
        else if (key == "hoversound") { props.hoverSound = parseBoolean(value, true); props.hoverSoundSet = true; }
        else if (key == "animate") { props.animate = parseBoolean(value); props.animateSet = true; }
        else if (key == "color") props.color = parseEditorColor(value, props.color);
        else if (key == "selcolor") props.selColor = parseEditorColor(value, props.selColor);
        else if (key == "font") { props.font = value.toInt(); props.fontSet = true; }
        else if (key == "style") props.style = value.toInt();
        else if (key == "min") props.min = value.toInt();
        else if (key == "max") props.max = value.toInt();
        else if (key == "position") { props.position = value.toInt(); props.positionSet = true; }
        else if (key == "linesize") props.lineSize = value.toInt();
        else if (key == "pagesize") props.pageSize = value.toInt();
        else if (key == "slidebegin") props.slideBegin = value.toInt();
        else if (key == "slideend") props.slideEnd = value.toInt();
        else if (key == "slidebtn") props.slideBtn = value;
        else if (key == "itemheight") props.itemHeight = value.toInt();
        else if (key == "itemcount") props.itemCount = value.toInt();
        else if (key == "range") props.range = value.toInt();
        else if (key == "text") props.text = value;
        else if (key == "icon") { props.icon = value; props.iconSet = true; }
        else if (key == "iconimage") { props.iconImage = value; props.iconImageSet = true; }
        else if (key == "indicatetype") props.indicateType = value.toInt();
        else if (key == "indicateimage") props.indicateImage = value;
        else if (key == "percent") props.percent = value.toDouble();
        else if (key == "normalcolor") props.normalColor = parseEditorColor(value, props.normalColor);
        else if (key == "hovercolor") props.hoverColor = parseEditorColor(value, props.hoverColor);
        else if (key == "presscolor") props.pressColor = parseEditorColor(value, props.pressColor);
        else if (key == "stretch") { props.stretch = parseBoolean(value); props.stretchSet = true; }
        else if (key == "keepaspect") { props.keepAspect = parseBoolean(value); props.keepAspectSet = true; }
        else if (key == "centerimage") { props.centerImage = parseBoolean(value); props.centerImageSet = true; }
        else if (key == "cropcontent") { props.cropContent = parseBoolean(value); props.cropContentSet = true; }
        else if (key == "cropblack") { props.cropBlack = parseBoolean(value); props.cropBlackSet = true; }
        else if (key == "frame") { props.frame = value.toInt(); props.frameSet = true; }
        else if (key == "backimage1") { props.backImage1 = value; props.backImage1Set = true; }
        else if (key == "backimage2") { props.backImage2 = value; props.backImage2Set = true; }
        else if (key == "autoshrink") { props.autoShrink = parseBoolean(value); props.autoShrinkSet = true; }
    }

    file.close();

    props.items.clear();
    for (int index = 1; index <= props.itemCount; ++index) props.items.append(indexedItems.value(index));
    if (!props.rawValues.contains("normalcolor") && props.rawValues.contains("color")) props.normalColor = props.color;

    if (selectedPage == "joystick") applyJoystickIntegers(props);
    if (selectedPage == "skills") applySkillsProperties(iniPath, props);
    componentIniCache[iniPath] = props;
}

void MenuEditorWindow::loadSubMenuDefinition(SubMenuDefinition& subMenu)
{
    if (subMenu.file.isEmpty() || assetsBasePath.isEmpty())
    {
        return;
    }

    QString fullPath;
    if (!resolveReadableResourcePath(subMenu.file, fullPath))
        return;

    QFile file(fullPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return;
    }

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    QString currentSection;
    QMap<int, MenuComponentDefinition> componentMap;

    while (!in.atEnd())
    {
        const QString rawLine = in.readLine();
        const QString line = trimRuntimeIniWhitespace(rawLine);
        if (line.isEmpty() || line.startsWith(";") || line.startsWith("#"))
        {
            continue;
        }

        if (line.startsWith(QLatin1Char('[')))
        {
            QString sectionName;
            if (parseRuntimeIniSection(rawLine, sectionName))
                currentSection = sectionName.toLower();
            continue;
        }

        QString key;
        QString value;
        if (!parseRuntimeIniEntry(rawLine, key, value))
        {
            continue;
        }

        if (currentSection == "menu")
        {
            if (key == "name") subMenu.name = value;
            else if (key == "window") subMenu.windowFile = value;
        }
        else if (currentSection.startsWith("component"))
        {
            bool ok = false;
            int index = currentSection.mid(9).toInt(&ok);
            if (ok)
            {
                if (key == "type") componentMap[index].type = value;
                else if (key == "name") componentMap[index].name = value;
                else if (key == "file") componentMap[index].file = value;
                else if (key == "bind") componentMap[index].bind = value;
                else if (key == "format") componentMap[index].format = value;
                else if (key == "controllerup") componentMap[index].controllerUp = value;
                else if (key == "controllerdown") componentMap[index].controllerDown = value;
                else if (key == "controllerleft") componentMap[index].controllerLeft = value;
                else if (key == "controllerright") componentMap[index].controllerRight = value;
            }
        }
    }

    file.close();

    QList<int> sortedKeys = componentMap.keys();
    std::sort(sortedKeys.begin(), sortedKeys.end());
    for (int key : sortedKeys)
    {
        componentMap[key].editorId =
            subMenu.editorId + "/component/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        subMenu.components.append(componentMap[key]);
    }

    if (!subMenu.windowFile.isEmpty())
    {
        const ComponentIniProperties windowProps = propertiesForResource(subMenu.windowFile);
        subMenu.windowRect = QRect(windowProps.left, windowProps.top,
                                   windowProps.width, windowProps.height);
        subMenu.backgroundImage = windowProps.image.isEmpty()
            ? windowProps.bitmap : windowProps.image;
        subMenu.windowStretch = windowProps.stretch;
    }

    for (const auto& comp : subMenu.components)
    {
        PreviewComponent pc;
        pc.editorId = comp.editorId;
        pc.type = comp.type;
        pc.name = comp.name;

        if (!comp.file.isEmpty())
        {
            const ComponentIniProperties props = propertiesForResource(comp.file);
            pc.rect = QRect(props.left, props.top, props.width, props.height);
            setPreviewComponentImages(comp.type, props, pc);
            pc.stretch = resolveStretch(props, comp.type);
        }

        subMenu.previewComponents.append(pc);
    }
}

bool MenuEditorWindow::saveSubMenuDefinition(const SubMenuDefinition& subMenu, const QString& outputPath)
{
    if (subMenu.file.isEmpty() || assetsBasePath.isEmpty())
        return false;

    QString fullPath = outputPath;
    if (fullPath.isEmpty() && !resolveLocalResourcePath(subMenu.file, fullPath))
        return false;
    auto mutationLease =
        AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(fullPath);
    if (!mutationLease)
        return false;

    QString readPath;
    if (!resolveReadableResourcePath(subMenu.file, readPath))
        readPath = fullPath;
    if (!mutationLease.addResourcePath(readPath))
        return false;

    QStringList originalLines;
    QFile readFile(readPath);
    if (readFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QTextStream in(&readFile);
        in.setEncoding(QStringConverter::Utf8);
        while (!in.atEnd())
            originalLines << in.readLine();
        readFile.close();
    }

    QSet<QString> menuKeys = {"name", "visible", "window"};
    QSet<QString> componentKeys = {
        "type", "name", "file", "bind", "format",
        "controllerup", "controllerdown", "controllerleft", "controllerright"};
    const QSet<QString> controllerNavigationKeys = {
        "controllerup", "controllerdown", "controllerleft", "controllerright"};

    QMap<QString, QString> menuValues;
    menuValues["name"] = subMenu.name;
    menuValues["window"] = subMenu.windowFile;

    QMap<int, QMap<QString, QString>> componentValues;
    for (int i = 0; i < subMenu.components.size(); i++)
    {
        const auto& comp = subMenu.components[i];
        componentValues[i + 1]["type"] = comp.type;
        componentValues[i + 1]["name"] = comp.name;
        componentValues[i + 1]["file"] = comp.file;
        if (!comp.bind.isEmpty())
            componentValues[i + 1]["bind"] = comp.bind;
        if (!comp.format.isEmpty() && comp.format != "%d")
            componentValues[i + 1]["format"] = comp.format;
        if (!comp.controllerUp.isEmpty())
            componentValues[i + 1]["controllerup"] = comp.controllerUp;
        if (!comp.controllerDown.isEmpty())
            componentValues[i + 1]["controllerdown"] = comp.controllerDown;
        if (!comp.controllerLeft.isEmpty())
            componentValues[i + 1]["controllerleft"] = comp.controllerLeft;
        if (!comp.controllerRight.isEmpty())
            componentValues[i + 1]["controllerright"] = comp.controllerRight;
    }

    QStringList outputLines;
    QSet<int> writtenComponents;
    bool menuSectionWritten = false;
    QString currentSection;
    int currentComponentIndex = -1;

    if (!originalLines.isEmpty())
    {
        QSet<QString> writtenKeysInComponent;
        QSet<QString> writtenKeysInMenu;

        auto flushMissingMenuKeys = [&]() {
            for (const QString& key : menuKeys)
            {
                if (!writtenKeysInMenu.contains(key) && menuValues.contains(key))
                    outputLines << key + "=" + menuValues[key];
            }
        };

        auto flushMissingComponentKeys = [&]() {
            if (currentComponentIndex > 0 && componentValues.contains(currentComponentIndex))
            {
                const auto& values = componentValues[currentComponentIndex];
                for (const QString& key : componentKeys)
                {
                    if (!writtenKeysInComponent.contains(key) && values.contains(key))
                    {
                        outputLines << key + "=" + values[key];
                    }
                }
            }
        };

        for (const QString& origLine : originalLines)
        {
            const QString trimmed = trimRuntimeIniWhitespace(origLine);
            QString parsedSectionName;

            if (parseRuntimeIniSection(origLine, parsedSectionName))
            {
                const QString sectionName = parsedSectionName.toLower();

                if (currentSection == "component")
                    flushMissingComponentKeys();
                else if (currentSection == "menu")
                    flushMissingMenuKeys();

                if (sectionName == "menu")
                {
                    currentSection = "menu";
                    menuSectionWritten = true;
                    writtenKeysInMenu.clear();
                    outputLines << origLine;
                    continue;
                }
                else if (sectionName.startsWith("component"))
                {
                    bool ok = false;
                    int index = sectionName.mid(9).toInt(&ok);
                    if (ok && index >= 1 && index <= subMenu.components.size())
                    {
                        currentSection = "component";
                        currentComponentIndex = index;
                        writtenComponents.insert(index);
                        writtenKeysInComponent.clear();
                        outputLines << origLine;
                        continue;
                    }
                    else if (ok)
                    {
                        currentSection = "skip_component";
                        currentComponentIndex = -1;
                        continue;
                    }
                    else
                    {
                        currentSection = sectionName;
                        currentComponentIndex = -1;
                        outputLines << origLine;
                        continue;
                    }
                }
                else if (sectionName.startsWith("submenu"))
                {
                    currentSection = sectionName;
                    outputLines << origLine;
                    continue;
                }
                else
                {
                    currentSection = sectionName;
                    outputLines << origLine;
                    continue;
                }
            }

            if (currentSection == "skip_component")
            {
                if (trimmed.isEmpty())
                    outputLines << origLine;
                continue;
            }

            if (trimmed.isEmpty())
            {
                outputLines << origLine;
                continue;
            }

            if (trimmed.startsWith(";") || trimmed.startsWith("#"))
            {
                outputLines << origLine;
                continue;
            }

            if (currentSection.isEmpty())
            {
                outputLines << origLine;
                continue;
            }

            if (currentSection == "menu")
            {
                QString key;
                QString ignoredValue;
                QString inlineCommentSuffix;
                if (!parseRuntimeIniEntry(
                        origLine, key, ignoredValue, &inlineCommentSuffix))
                {
                    outputLines << origLine;
                    continue;
                }
                writtenKeysInMenu.insert(key);
                if (menuKeys.contains(key) && menuValues.contains(key))
                {
                    outputLines << key + "=" + menuValues[key] +
                        inlineCommentSuffix;
                }
                else
                    outputLines << origLine;
            }
            else if (currentSection == "component" && currentComponentIndex > 0)
            {
                QString key;
                QString ignoredValue;
                QString inlineCommentSuffix;
                if (!parseRuntimeIniEntry(
                        origLine, key, ignoredValue, &inlineCommentSuffix))
                {
                    outputLines << origLine;
                    continue;
                }
                const bool duplicateControllerNavigationKey =
                    controllerNavigationKeys.contains(key) &&
                    writtenKeysInComponent.contains(key);
                writtenKeysInComponent.insert(key);
                if (duplicateControllerNavigationKey)
                    continue;
                if (componentKeys.contains(key))
                {
                    if (componentValues[currentComponentIndex].contains(key))
                    {
                        outputLines << key + "=" +
                            componentValues[currentComponentIndex][key] +
                            inlineCommentSuffix;
                    }
                }
                else
                    outputLines << origLine;
            }
            else
            {
                outputLines << origLine;
            }
        }

        if (currentSection == "component")
            flushMissingComponentKeys();
        else if (currentSection == "menu")
            flushMissingMenuKeys();
    }

    if (!menuSectionWritten)
    {
        outputLines << "[menu]";
        outputLines << "name=" + subMenu.name;
        outputLines << "window=" + subMenu.windowFile;
        outputLines << "";
    }

    for (int i = 0; i < subMenu.components.size(); i++)
    {
        int index = i + 1;
        if (writtenComponents.contains(index))
            continue;

        outputLines << "";
        outputLines << "[component" + QString::number(index) + "]";
        const auto& comp = subMenu.components[i];
        outputLines << "type=" + comp.type;
        outputLines << "name=" + comp.name;
        outputLines << "file=" + comp.file;
        if (!comp.bind.isEmpty())
            outputLines << "bind=" + comp.bind;
        if (!comp.format.isEmpty() && comp.format != "%d")
            outputLines << "format=" + comp.format;
        if (!comp.controllerUp.isEmpty())
            outputLines << "controllerup=" + comp.controllerUp;
        if (!comp.controllerDown.isEmpty())
            outputLines << "controllerdown=" + comp.controllerDown;
        if (!comp.controllerLeft.isEmpty())
            outputLines << "controllerleft=" + comp.controllerLeft;
        if (!comp.controllerRight.isEmpty())
            outputLines << "controllerright=" + comp.controllerRight;
    }

    QFile writeFile(fullPath);
    if (!writeFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&writeFile);
    out.setEncoding(QStringConverter::Utf8);
    for (const QString& line : outputLines)
        out << line << "\n";

    if (out.status() != QTextStream::Ok)
    {
        writeFile.close();
        return false;
    }

    writeFile.close();
    return true;
}

bool MenuEditorWindow::writeComponentIniProperties(const QString& iniPath,
                                                   const ComponentIniProperties& props,
                                                   const QSet<QString>& componentTypes,
                                                   const QString& readPath)
{
    auto mutationLease =
        AuthoringMutationGate::instance().
            acquireMutationLeaseForPath(iniPath);
    if (!mutationLease)
        return false;

    QString actualReadPath = readPath.isEmpty() ? iniPath : readPath;
    if (!mutationLease.addResourcePath(actualReadPath))
        return false;
    QFile readFile(actualReadPath);
    QStringList originalLines;
    if (readFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QTextStream in(&readFile);
        in.setEncoding(QStringConverter::Utf8);
        while (!in.atEnd())
        {
            originalLines.append(in.readLine());
        }
        readFile.close();
    }

    QSet<QString> baseKeys;
    baseKeys << "name" << "left" << "top" << "width" << "height" << "image" << "bitmap";

    QMap<QString, QSet<QString>> typeSpecificKeys;
    typeSpecificKeys["Button"] = QSet<QString>{"flat", "backgroundcolor", "hoverbackgroundcolor", "pressedbackgroundcolor", "bordercolor", "kind", "up", "down", "track", "sound", "stretch", "hoversound", "animate"};
    typeSpecificKeys["CheckBox"] = QSet<QString>{"up", "down", "sound", "font", "text", "stretch"};
    typeSpecificKeys["ImageContainer"] = QSet<QString>{"stretch", "keepaspect", "cropcontent", "cropblack", "frame", "nineslice", "nineslicewidth"};
    typeSpecificKeys["Scrollbar"] = QSet<QString>{"flat", "fittrack", "backgroundcolor", "style", "min", "max", "position", "linesize", "pagesize", "slidebegin", "slideend", "slidebtn"};
    typeSpecificKeys["ColumnImage"] = QSet<QString>{"percent", "horizontal", "fillcolor", "stretch"};
    typeSpecificKeys["ListBox"] = QSet<QString>{"sound", "itemheight", "itemcount", "color", "selcolor", "font", "fititems"};
    typeSpecificKeys["Item"] = QSet<QString>{"font", "color", "stretch", "keepaspect", "centerimage", "frame", "backimage1", "backimage2"};
    typeSpecificKeys["Label"] = QSet<QString>{"font", "color", "stretch", "keepaspect", "centerimage", "frame", "backimage1", "backimage2", "autoshrink"};
    typeSpecificKeys["TalkLabel"] = QSet<QString>{"font", "color", "stretch", "keepaspect", "centerimage", "frame", "backimage1", "backimage2", "autoshrink"};
    typeSpecificKeys["TextButton"] = QSet<QString>{"kind", "up", "down", "track", "sound", "font", "color", "text", "stretch", "hoversound", "animate"};
    typeSpecificKeys["RoundButton"] = QSet<QString>{"kind", "up", "down", "track", "sound", "range", "text", "stretch", "hoversound", "animate", "icon", "iconimage"};
    typeSpecificKeys["DragRoundButton"] = QSet<QString>{"kind", "up", "down", "track", "sound", "range", "text", "indicatetype", "indicateimage", "stretch", "hoversound", "animate", "icon", "iconimage"};
    typeSpecificKeys["DragButton"] = typeSpecificKeys["Button"];
    typeSpecificKeys["MemoText"] = QSet<QString>{"font", "color"};
    typeSpecificKeys["FlatTextButton"] = typeSpecificKeys["TextButton"];
    typeSpecificKeys["FlatTextButton"].unite({"text", "backgroundcolor", "hoverbackgroundcolor", "pressedbackgroundcolor"});
    typeSpecificKeys["Item"].insert("drawslot");
    typeSpecificKeys["Label"].unite({"text", "autonextline", "minimumfont", "centertext"});
    typeSpecificKeys["TalkLabel"].unite({"text", "autonextline", "minimumfont", "centertext", "linecount", "charactersperline", "lineheight"});
    typeSpecificKeys["MemoText"].unite({"linesize", "fitlines"});
    typeSpecificKeys["FadeMask"] = QSet<QString>{};
    typeSpecificKeys["Joystick"] = QSet<QString>{"range", "baseimage", "thumbimage"};
    typeSpecificKeys["ChooseTextButton"] = QSet<QString>{"kind", "up", "down", "track", "sound", "font", "color", "normalcolor", "hovercolor", "presscolor", "stretch", "hoversound", "animate"};
    typeSpecificKeys["TransImage"] = QSet<QString>{"stretch", "keepaspect", "cropcontent", "cropblack", "frame"};
    typeSpecificKeys["VideoPlayer"] = QSet<QString>{};

    QSet<QString> windowExtraKeys;
    windowExtraKeys << "align" << "alignx" << "aligny" << "stretch" << "bitmap";
    windowExtraKeys << "scale" << "nineslice" << "nineslicewidth" << "keepaspect";

    QSet<QString> genericKeys;
    genericKeys << "align" << "alignx" << "aligny" << "sound" << "kind"
                << "up" << "down" << "track" << "color"
                << "selcolor" << "font" << "style" << "min" << "max"
                << "linesize" << "pagesize" << "slidebegin"
                << "slideend" << "slidebtn" << "itemheight" << "itemcount"
                << "range" << "text"
                << "indicatetype" << "indicateimage" << "percent"
                << "normalcolor" << "hovercolor" << "presscolor";

    QSet<QString> validKeys = baseKeys;
    if (componentTypes.isEmpty())
    {
        validKeys.unite(genericKeys);
    }
    else
    {
        for (const QString& componentType : componentTypes)
        {
            if (componentType == "MenuWindow")
            {
                validKeys.unite(windowExtraKeys);
            }
            else if (typeSpecificKeys.contains(componentType))
            {
                validKeys.unite(typeSpecificKeys[componentType]);
            }
            else
            {
                validKeys.unite(genericKeys);
            }
        }
    }

    bool hasItems = componentTypes.contains("ListBox");
    if (hasItems && !originalLines.isEmpty())
    {
        QStringList lines;
        QSet<int> written;
        QMap<int, int> lastItemLines;
        QString originalSection;
        for (int lineIndex = 0; lineIndex < originalLines.size(); ++lineIndex)
        {
            QString section, key, value;
            if (parseRuntimeIniSection(originalLines[lineIndex], section)) originalSection = section.toLower();
            if (originalSection == "items" && parseRuntimeIniEntry(originalLines[lineIndex], key, value))
            {
                bool parsed = false; const int index = key.toInt(&parsed);
                if (parsed && index > 0 && key == QString::number(index)) lastItemLines[index] = lineIndex;
            }
        }
        bool inItems = false, seenItems = false;
        const auto appendMissing = [&]()
        {
            for (int index = 1; index <= props.items.size(); ++index)
                if (!written.contains(index) && !lastItemLines.contains(index) && !props.items[index - 1].isEmpty())
                {
                    lines.append(QString::number(index) + "=" + props.items[index - 1]);
                    written.insert(index);
                }
        };
        for (int lineIndex = 0; lineIndex < originalLines.size(); ++lineIndex)
        {
            const QString& line = originalLines[lineIndex];
            QString section, key, value, suffix;
            if (parseRuntimeIniSection(line, section))
            {
                if (inItems) appendMissing();
                inItems = section.compare("Items", Qt::CaseInsensitive) == 0;
                seenItems |= inItems;
            }
            if (inItems && parseRuntimeIniEntry(line, key, value, &suffix))
            {
                bool parsed = false;
                const int index = key.toInt(&parsed);
                if (parsed && index > 0 && index <= props.items.size() && key == QString::number(index))
                {
                    written.insert(index);
                    if (lastItemLines.value(index) == lineIndex && props.items[index - 1] != value)
                    {
                        lines.append(line.left(findRuntimeIniDelimiter(line, "=:" ) + 1) + props.items[index - 1] + suffix);
                        continue;
                    }
                }
            }
            lines.append(line);
        }
        if (inItems) appendMissing();
        if (!seenItems && std::any_of(props.items.begin(), props.items.end(), [](const QString& item) { return !item.isEmpty(); }))
        {
            lines.append("[Items]");
            appendMissing();
        }
        originalLines = lines;
        hasItems = false;
    }

    QMap<QString, QString> newValues;
    const QSet<QString> layoutKeys = {"flat", "fittrack", "fittowindow", "windowmargin", "windowverticalmargin", "keepaspect", "horizontal", "fillcolor", "fititems", "autonextline", "minimumfont", "centertext", "drawslot", "fitlines", "scale", "nineslice", "nineslicewidth", "backgroundcolor", "hoverbackgroundcolor", "pressedbackgroundcolor", "bordercolor", "linecount", "charactersperline", "lineheight"};
    for (const QString& key : layoutKeys)
        if (props.rawValues.contains(key)) newValues[key] = props.rawValues.value(key);
    if (props.nameSet) newValues["name"] = props.name;
    newValues["left"] = QString::number(props.left);
    newValues["top"] = QString::number(props.top);
    newValues["width"] = QString::number(props.width);
    newValues["height"] = QString::number(props.height);
    if (!props.image.isEmpty()) newValues["image"] = props.image;
    if (props.bitmapSet && !props.bitmap.isEmpty()) newValues["bitmap"] = props.bitmap;
    if (!props.baseImage.isEmpty()) newValues["baseimage"] = props.baseImage;
    if (!props.thumbImage.isEmpty()) newValues["thumbimage"] = props.thumbImage;
    if (!props.align.isEmpty()) newValues["align"] = props.align;
    if (props.alignX != 0) newValues["alignx"] = QString::number(props.alignX);
    if (props.alignY != 0) newValues["aligny"] = QString::number(props.alignY);
    if (props.stretchSet) newValues["stretch"] = props.stretch ? "true" : "false";
    if (!props.sound.isEmpty()) newValues["sound"] = props.sound;
    if (!props.kind.isEmpty()) newValues["kind"] = props.kind;
    newValues["up"] = QString::number(props.up);
    newValues["down"] = QString::number(props.down);
    newValues["track"] = QString::number(props.track);
    if (props.hoverSoundSet) newValues["hoversound"] = props.hoverSound ? "true" : "false";
    if (props.animateSet) newValues["animate"] = props.animate ? "true" : "false";
    newValues["color"] = serializeEditorColor(props.color);
    newValues["selcolor"] = serializeEditorColor(props.selColor);
    if (props.fontSet) newValues["font"] = QString::number(props.font);
    newValues["style"] = QString::number(props.style);
    newValues["min"] = QString::number(props.min);
    newValues["max"] = QString::number(props.max);
    if (props.positionSet) newValues["position"] = QString::number(props.position);
    newValues["linesize"] = QString::number(props.lineSize);
    if (componentTypes.contains("MemoText"))
    {
        newValues.remove("linesize");
        if (props.rawValues.contains("linesize")) newValues["linesize"] = props.rawValues.value("linesize");
    }
    newValues["pagesize"] = QString::number(props.pageSize);
    newValues["slidebegin"] = QString::number(props.slideBegin);
    newValues["slideend"] = QString::number(props.slideEnd);
    if (!props.slideBtn.isEmpty()) newValues["slidebtn"] = props.slideBtn;
    newValues["itemheight"] = QString::number(props.itemHeight);
    newValues["itemcount"] = QString::number(props.itemCount);
    newValues["range"] = QString::number(props.range);
    if (!props.text.isEmpty()) newValues["text"] = props.text;
    if (props.iconSet && !props.icon.isEmpty()) newValues["icon"] = props.icon;
    if (props.iconImageSet && !props.iconImage.isEmpty()) newValues["iconimage"] = props.iconImage;
    newValues["indicatetype"] = QString::number(props.indicateType);
    if (!props.indicateImage.isEmpty()) newValues["indicateimage"] = props.indicateImage;
    newValues["percent"] = QString::number(props.percent, 'f', 2);
    newValues["normalcolor"] = serializeEditorColor(props.normalColor);
    newValues["hovercolor"] = serializeEditorColor(props.hoverColor);
    newValues["presscolor"] = serializeEditorColor(props.pressColor);
    if (props.keepAspectSet) newValues["keepaspect"] = props.keepAspect ? "true" : "false";
    if (props.centerImageSet) newValues["centerimage"] = props.centerImage ? "true" : "false";
    if (props.cropContentSet) newValues["cropcontent"] = props.cropContent ? "true" : "false";
    if (props.cropBlackSet) newValues["cropblack"] = props.cropBlack ? "true" : "false";
    if (props.frameSet) newValues["frame"] = QString::number(props.frame);
    if (props.backImage1Set && !props.backImage1.isEmpty()) newValues["backimage1"] = props.backImage1;
    if (props.backImage2Set && !props.backImage2.isEmpty()) newValues["backimage2"] = props.backImage2;
    if (props.autoShrinkSet) newValues["autoshrink"] = props.autoShrink ? "true" : "false";

    if (selectedPage == "titleteam")
    {
        validKeys = {"left", "top", "width", "height", "align", "alignx", "aligny", "font", "color", "linegap", "backgroundcolor"};
        newValues = props.rawValues;
    }
    if (selectedPage == "dialog" || selectedPage == "choose" || selectedPage == "message" || selectedPage == "timer")
    {
        validKeys.insert("keepaspect");
        newValues = props.rawValues;
    }

    if (isNoticePage(selectedPage))
    {
        newValues = props.rawValues;
        validKeys = componentTypes.contains("MenuWindow") ? QSet<QString>{"left","top","width","height","align","alignx","aligny","backgroundcolor","topbordercolor","bottombordercolor"}
            : QSet<QString>{"font","color","horizontalpadding","linegap"};
    }

    if (isHudPage(selectedPage) || selectedPage == "mapthumbnail")
    {
        newValues=props.rawValues;
        newValues.remove("percent"); validKeys.remove("percent");
        if (selectedPage == "mapthumbnail" && componentTypes.contains("MenuWindow"))
            validKeys.unite({"fittowindow", "windowmargin", "windowverticalmargin"});
    }
    if (selectedPage == "joystick")
    {
        newValues=props.rawValues;
        validKeys=componentTypes.contains("MenuWindow")?joystickFields("menu/window"):QSet<QString>{"left","top","width","height","range","image","baseimage","thumbimage"};
    }
    if (selectedPage == "skills")
    {
        newValues=props.rawValues;
        validKeys=componentTypes.contains("MenuWindow")?skillsFields("menu/window"):QSet<QString>{"left","top","width","height","image","bitmap","stretch","animate","kind","up","down","track","flat","font","range","icon","iconimage","text"};
    }
    if(selectedPage=="controllerhelp"){newValues=props.rawValues;validKeys=controllerHelpFields();}
    if (selectedPage == "npcinfo") { newValues=props.rawValues; validKeys={"width","height","alignx","aligny"}; }
    QStringList outputLines;
    QString currentSection;
    bool inItemsSection = false;
    bool itemsWritten = false;
    bool initSectionWritten = false;
    QSet<QString> writtenKeysInInit;

    if (originalLines.isEmpty())
    {
        outputLines << "[init]";
        for (auto it = newValues.constBegin(); it != newValues.constEnd(); ++it)
        {
            if (validKeys.contains(it.key()))
            {
                outputLines << it.key() + "=" + it.value();
            }
        }
        if (hasItems && !props.items.isEmpty())
        {
            outputLines << "";
            outputLines << "[Items]";
            for (int i = 0; i < props.items.size(); i++)
            {
                outputLines << QString("%1=%2").arg(i + 1).arg(props.items[i]);
            }
        }
    }
    else
    {

    for (const QString& line : originalLines)
    {
        QString trimmed = line.trimmed();

        QString sectionName;
        if (parseRuntimeIniSection(line, sectionName))
        {
            sectionName = sectionName.toLower();

            if (currentSection == "init")
            {
                for (auto it = newValues.constBegin(); it != newValues.constEnd(); ++it)
                {
                    if (validKeys.contains(it.key()) && !writtenKeysInInit.contains(it.key()))
                    {
                        outputLines << it.key() + "=" + it.value();
                    }
                }
            }

            if (inItemsSection && !itemsWritten && hasItems && !props.items.isEmpty())
            {
                outputLines << "[Items]";
                for (int i = 0; i < props.items.size(); i++)
                {
                    outputLines << QString("%1=%2").arg(i + 1).arg(props.items[i]);
                }
                itemsWritten = true;
            }

            // [Items] is editor-owned only for ListBox. Other component types
            // may use an identically named extension section, which must remain
            // represented in the line-preserving output.
            inItemsSection = hasItems && sectionName == "items";
            currentSection = sectionName;
            writtenKeysInInit.clear();

            if (sectionName == "init")
                initSectionWritten = true;

            if (inItemsSection)
            {
                continue;
            }

            outputLines << line;
            continue;
        }

        if (inItemsSection)
        {
            continue;
        }

        if (trimmed.isEmpty() || trimmed.startsWith(";") || trimmed.startsWith("#"))
        {
            outputLines << line;
            continue;
        }

        QString key, originalValue;
        const int equalPos = findRuntimeIniDelimiter(line, "=:");
        if (!parseRuntimeIniEntry(line, key, originalValue))
        {
            outputLines << line;
            continue;
        }

        if (currentSection != "init" || !validKeys.contains(key))
        {
            outputLines << line;
            continue;
        }

        if (currentSection == "init")
        {
            writtenKeysInInit.insert(key);
        }

        if (newValues.contains(key))
        {
            QString ignoredKey, value, suffix;
            parseRuntimeIniEntry(line, ignoredKey, value, &suffix);
            outputLines << (value == newValues[key] ? line : line.left(equalPos + 1) + newValues[key] + suffix);
        }
        else
        {
            // The user cleared an optional known value; omit the stale key.
        }
    }

    if (currentSection == "init")
    {
        for (auto it = newValues.constBegin(); it != newValues.constEnd(); ++it)
        {
            if (validKeys.contains(it.key()) && !writtenKeysInInit.contains(it.key()))
            {
                outputLines << it.key() + "=" + it.value();
            }
        }
    }

    if (!initSectionWritten)
    {
        if (!outputLines.isEmpty() && !outputLines.last().isEmpty())
            outputLines << "";
        outputLines << "[init]";
        for (auto it = newValues.constBegin(); it != newValues.constEnd(); ++it)
        {
            if (validKeys.contains(it.key()))
                outputLines << it.key() + "=" + it.value();
        }
    }

    if (hasItems && !props.items.isEmpty())
    {
        if (inItemsSection && !itemsWritten)
        {
            outputLines << "[Items]";
            for (int i = 0; i < props.items.size(); i++)
            {
                outputLines << QString("%1=%2").arg(i + 1).arg(props.items[i]);
            }
        }
        else if (!inItemsSection)
        {
            outputLines << "[Items]";
            for (int i = 0; i < props.items.size(); i++)
            {
                outputLines << QString("%1=%2").arg(i + 1).arg(props.items[i]);
            }
        }
    }

    }

    QFile writeFile(iniPath);
    if (!writeFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }

    QTextStream out(&writeFile);
    out.setEncoding(QStringConverter::Utf8);
    for (const QString& line : outputLines)
    {
        out << line;
        if (!line.endsWith("\n"))
        {
            out << "\n";
        }
    }

    if (out.status() != QTextStream::Ok)
    {
        writeFile.close();
        return false;
    }

    writeFile.close();
    return true;
}

void MenuEditorWindow::selectComponentByIndex(int index)
{
    if (index >= 0 && index < currentDefinition.components.size()
        && index + 1 < ui->componentTree->topLevelItemCount())
    {
        ui->componentTree->setCurrentItem(ui->componentTree->topLevelItem(index + 1));
    }
}

void MenuEditorWindow::onAssetFileSelected(const QString& relativePath)
{
}

void MenuEditorWindow::onAssetFileDoubleClicked(const QString& relativePath)
{
    if (relativePath.isEmpty())
    {
        return;
    }

    QString suffix = QFileInfo(relativePath).suffix().toLower();

    if (suffix == "ini")
    {
        if (relativePath.toLower().endsWith(".menu.ini"))
        {
            // AssetBrowser is explicitly switchable between local and
            // inherited roots. Resolve a menu double-click against the root
            // currently shown in the browser, not against runtime priority,
            // otherwise a same-name local file opens instead of the file the
            // user actually clicked.
            QString browserRoot;
            if (QComboBox* rootCombo = findChild<QComboBox*>("assetRootCombo"))
                browserRoot = rootCombo->currentData().toString();
            if (browserRoot.isEmpty() && !pageResources.readRoots.isEmpty())
                browserRoot = pageResources.readRoots.first();
            QString fullPath;
            if (EditorAssetPath::resolveLogicalResourcePath(
                    browserRoot, relativePath, fullPath) &&
                QFileInfo::exists(fullPath) && QFileInfo(fullPath).isFile())
            {
                openMenuDefinition(fullPath);
            }
        }

    }
    else if (suffix == "mpc" || suffix == "shd" || suffix == "asf" || suffix == "imp" || suffix == "img"
             || suffix == "png" || suffix == "jpg" || suffix == "bmp")
    {
        if (selectedItemType == SelectedItemType::MenuWindow)
        {
            if (!currentDefinition.windowFile.isEmpty())
            {
                ComponentIniProperties props = propertiesForResource(currentDefinition.windowFile);
                props.image = relativePath;
                stageComponentProperties("menu/window", currentDefinition.windowFile,
                                         props, "MenuWindow");
                markModified();
                updatePropertyEditor();
                updatePreview();
            }
        }
        else if (selectedComponentIndex >= 0 && selectedComponentIndex < currentDefinition.components.size())
        {
            if (!currentDefinition.components[selectedComponentIndex].file.isEmpty())
            {
                const auto& definition = currentDefinition.components[selectedComponentIndex];
                ComponentIniProperties props = propertiesForResource(definition.file);
                props.image = relativePath;
                stageComponentProperties(definition.editorId, definition.file,
                                         props, definition.type);
                markModified();
                updatePropertyEditor();
                updatePreview();
            }
        }
    }
}
