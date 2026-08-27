#include "MenuEditorWindow.h"
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
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QStringConverter>
#include <QSettings>
#include <QDir>
#include <QEvent>
#include <QInputDialog>
#include <QHeaderView>
#include <QDebug>
#include <QSet>
#include <QUuid>
#include <QComboBox>
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
         componentType == "CheckBox" || componentType == "TextButton" ||
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
    return componentType == "TextButton" ||
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
    ui->outerLayout->setStretch(2, 1);

    previewCanvas = new MenuPreviewCanvas();
    ui->previewScrollArea->setWidget(previewCanvas);

    propertyEditor = new ComponentPropertyEditor();
    auto propertyLayout = ui->propertyEditorPlaceholder->layout();
    propertyLayout->addWidget(propertyEditor);

    assetBrowser = new AssetBrowser();
    auto assetLayout = ui->assetPage->layout();
    auto assetRootCombo = new QComboBox(ui->assetPage);
    assetRootCombo->setObjectName("assetRootCombo");
    assetRootCombo->setToolTip(tr("选择浏览的 UI 资源根；继承根只读，保存始终写入当前资源包"));
    assetLayout->addWidget(assetRootCombo);
    assetLayout->addWidget(assetBrowser);

    ui->componentTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->componentTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->componentTree->header()->setStretchLastSection(true);

    ui->mainSplitter->setSizes({250, 550, 400});

    connect(ui->menuNameEdit, &QLineEdit::textChanged, this, [this](const QString& text)
    {
        if (!updatingFromCode) { currentDefinition.menuName = text; markModified(); }
    });
    connect(ui->menuVisibleCheck, &QCheckBox::toggled, this, [this](bool checked)
    {
        if (!updatingFromCode) { currentDefinition.visible = checked; markModified(); }
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

    resetHistory();
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
    newAction->setText(tr("新建"));
    openAction->setText(tr("打开"));
    undoAction->setText(tr("撤销"));
    redoAction->setText(tr("重做"));
    saveAction->setText(tr("保存"));
    saveAsAction->setText(tr("另存为"));
    refreshAction->setText(tr("刷新预览"));

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
        rootCombo->setToolTip(tr("选择浏览的 UI 资源根；继承根只读，保存始终写入当前资源包"));
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

    setWindowTitle(currentFilePath.isEmpty()
        ? tr("菜单编辑器 - 新建")
        : tr("菜单编辑器 - ") + QFileInfo(currentFilePath).fileName());
}

void MenuEditorWindow::setupToolBar()
{
    newAction = new QAction(tr("新建"), this);
    newAction->setObjectName(QStringLiteral("menuEditorNewAction"));
    openAction = new QAction(tr("打开"), this);
    openAction->setObjectName(QStringLiteral("menuEditorOpenAction"));
    undoAction = new QAction(tr("撤销"), this);
    undoAction->setObjectName(QStringLiteral("menuEditorUndoAction"));
    redoAction = new QAction(tr("重做"), this);
    redoAction->setObjectName(QStringLiteral("menuEditorRedoAction"));
    saveAction = new QAction(tr("保存"), this);
    saveAction->setObjectName(QStringLiteral("menuEditorSaveAction"));
    saveAsAction = new QAction(tr("另存为"), this);
    saveAsAction->setObjectName(QStringLiteral("menuEditorSaveAsAction"));
    refreshAction = new QAction(tr("刷新预览"), this);
    refreshAction->setObjectName(QStringLiteral("menuEditorRefreshAction"));

    ui->toolBar->addAction(newAction);
    ui->toolBar->addAction(openAction);
    ui->toolBar->addSeparator();
    ui->toolBar->addAction(undoAction);
    ui->toolBar->addAction(redoAction);
    ui->toolBar->addSeparator();
    ui->toolBar->addAction(saveAction);
    ui->toolBar->addAction(saveAsAction);
    ui->toolBar->addSeparator();
    ui->toolBar->addAction(refreshAction);

    connect(newAction, &QAction::triggered, this, &MenuEditorWindow::newMenuDefinition);
    undoAction->setShortcut(QKeySequence::Undo);
    redoAction->setShortcut(QKeySequence::Redo);
    connect(undoAction, &QAction::triggered, this, &MenuEditorWindow::onUndo);
    connect(redoAction, &QAction::triggered, this, &MenuEditorWindow::onRedo);
    connect(openAction, &QAction::triggered, this, [this]()
    {
        QString filePath = QFileDialog::getOpenFileName(this,
            tr("打开菜单定义文件"), assetsBasePath,
            "Menu INI Files (*.menu.ini);;INI Files (*.ini);;All Files (*)",
            nullptr, QFileDialog::DontResolveSymlinks);
        if (!filePath.isEmpty())
        {
            openMenuDefinition(filePath);
        }
    });
    connect(saveAction, &QAction::triggered, this, &MenuEditorWindow::onSaveFile);
    connect(saveAsAction, &QAction::triggered, this, &MenuEditorWindow::onSaveAsFile);
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
    const bool changingRoot = !configuredAssetsBasePath.isEmpty() &&
        EditorAssetPath::logicalComparisonKey(configuredAssetsBasePath) !=
            EditorAssetPath::logicalComparisonKey(normalizedPath);
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
        updatePropertyEditor();
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
        newMenuDefinition();
        selectedSubComponentName.clear();
        selectedSubComponentPath.clear();
    }

    configuredAssetsBasePath = normalizedPath;
    assetsBasePath.clear();
    activeWriteRootLocked = false;
    resourceRecoveryBlocked = false;
    resourceRecoveryErrors.clear();
    saveAction->setEnabled(true);
    saveAsAction->setEnabled(true);
    rebuildResourceContext();
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
    if (configuredAssetsBasePath.isEmpty())
        return;

    const ResourceContentRootResolution resolution =
        ResourcePackScanner::resolveContentRoots(
            configuredAssetsBasePath);
    resourceRecoveryBlocked =
        !resolution.recoveryErrors.isEmpty();
    resourceRecoveryErrors = resolution.recoveryErrors;
    if (resourceRecoveryBlocked)
    {
        assetsCollectionPath.clear();
        assetsBasePath.clear();
        uiReadRoots.clear();
        activeWriteRootLocked = true;
        saveAction->setEnabled(false);
        saveAsAction->setEnabled(false);
        QMessageBox::critical(
            this,
            tr("文件事务恢复失败"),
            tr("资源目录中存在未能安全恢复的保存事务。为避免覆盖可恢复数据，"
               "菜单保存功能已停用：\n%1")
                .arg(resourceRecoveryErrors.join('\n')));
        return;
    }
    saveAction->setEnabled(true);
    saveAsAction->setEnabled(true);

    assetsCollectionPath = resolution.collectionRoot.isEmpty()
        ? configuredAssetsBasePath
        : resolution.collectionRoot;
    const QList<ResourcePackInfo> packs =
        resolution.availablePacks;
    QString activeRoot = activeWriteRootLocked && !assetsBasePath.isEmpty()
        ? assetsBasePath : configuredAssetsBasePath;
    if (!activeWriteRootLocked &&
        !ResourcePackScanner::hasManifest(configuredAssetsBasePath) &&
        !preferredFilePath.isEmpty())
    {
        int bestLength = -1;
        for (const ResourcePackInfo& pack : packs)
        {
            if (EditorAssetPath::isLexicallyInside(
                    pack.rootPath, preferredFilePath) &&
                pack.rootPath.size() > bestLength)
            {
                activeRoot = pack.rootPath;
                bestLength = pack.rootPath.size();
            }
        }
        if (bestLength >= 0)
            activeWriteRootLocked = true;
    }
    else if (ResourcePackScanner::hasManifest(configuredAssetsBasePath))
    {
        activeWriteRootLocked = true;
    }
    assetsBasePath = EditorAssetPath::normalizedAbsolutePath(activeRoot);

    int activeIndex = -1;
    for (int i = 0; i < packs.size(); ++i)
    {
        if (EditorAssetPath::logicalComparisonKey(packs[i].rootPath) ==
            EditorAssetPath::logicalComparisonKey(assetsBasePath))
        {
            activeIndex = i;
            break;
        }
    }

    // 统一使用共享 ResourceCatalog 的精确选择结果构建 UI 读取根，
    // 与游戏运行、编辑器运行预检解析到同一组文件。不再在菜单编辑器中
    // 自行做 ID 查询、依赖递归、CommonPath 推断或 PreferLocal 排序。
    uiReadRoots.clear();
    auto appendReadRoot = [this](const QString& root)
    {
        const QString normalized = EditorAssetPath::normalizedAbsolutePath(root);
        if (normalized.isEmpty())
            return;
        const QString key = EditorAssetPath::logicalComparisonKey(normalized);
        for (const QString& existing : uiReadRoots)
        {
            if (EditorAssetPath::logicalComparisonKey(existing) == key)
                return;
        }
        uiReadRoots.append(normalized);
    };

    bool selectionResolved = false;
    bool preferLocal = true;
    QString selectionActiveRoot = assetsBasePath;
    QString commonRoot;
    QStringList uiFallbackRoots;

    if (activeIndex >= 0 && !packs[activeIndex].stableEntryKey.isEmpty())
    {
        const RuntimeResource::ExactSelectionResult exact =
            RuntimeResource::resolveResourceCatalogEntrySelection(
                std::filesystem::u8path(
                    assetsCollectionPath.toUtf8().constData()),
                packs[activeIndex].stableEntryKey.toStdString());
        if (exact.succeeded())
        {
            selectionResolved = true;
            preferLocal = exact.selection.preferLocalUi;
            if (!exact.selection.activeResourceRoot.empty())
            {
                selectionActiveRoot = EditorAssetPath::normalizedAbsolutePath(
                    QString::fromStdString(
                        exact.selection.activeResourceRoot.u8string()));
            }
            if (!exact.selection.commonResourceRoot.empty())
            {
                commonRoot = EditorAssetPath::normalizedAbsolutePath(
                    QString::fromStdString(
                        exact.selection.commonResourceRoot.u8string()));
            }
            // UI 回退根（不含活动根与 Common）按共享顺序保留，稍后依据
            // preferLocal 决定相对活动根的位置，与运行时 File 层顺序一致。
            for (const std::filesystem::path& fallback :
                 exact.selection.orderedUiFallbackRoots)
            {
                uiFallbackRoots.append(
                    QString::fromStdString(fallback.u8string()));
            }
        }
    }

    // PreferLocal 决定活动根相对 UI 回退根的位置；Common 始终最后追加。
    // 与运行时 File::setUiResourceFallbackRoots 的顺序保持一致。
    if (preferLocal)
        appendReadRoot(selectionActiveRoot);
    for (const QString& fallback : uiFallbackRoots)
        appendReadRoot(fallback);
    if (!preferLocal)
        appendReadRoot(selectionActiveRoot);
    if (!commonRoot.isEmpty() && QDir(commonRoot).exists())
        appendReadRoot(commonRoot);
    if (uiReadRoots.isEmpty())
        appendReadRoot(assetsBasePath);

    // 记录共享解析未覆盖活动包的退化情况，便于诊断；不阻止编辑器工作。
    if (!selectionResolved)
    {
        qDebug() << "MenuEditorWindow: shared catalog selection did not resolve"
                 << assetsBasePath << "; using active root as the single UI root";
    }

    previewCanvas->setResourceRoots(uiReadRoots);
    propertyEditor->setAssetsBasePath(assetsBasePath);
    propertyEditor->setAssetDropRoots(uiReadRoots);

    QComboBox* assetRootCombo = findChild<QComboBox*>("assetRootCombo");
    if (assetRootCombo)
    {
        const bool blocked = assetRootCombo->blockSignals(true);
        assetRootCombo->clear();
        for (int i = 0; i < uiReadRoots.size(); ++i)
        {
            const QString label = i == 0 &&
                EditorAssetPath::logicalComparisonKey(uiReadRoots[i]) ==
                    EditorAssetPath::logicalComparisonKey(assetsBasePath)
                ? tr("本地 UI — %1").arg(QFileInfo(uiReadRoots[i]).fileName())
                : tr("只读回退 %1 — %2").arg(i).arg(QFileInfo(uiReadRoots[i]).fileName());
            assetRootCombo->addItem(label, uiReadRoots[i]);
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
    for (const QString& root : uiReadRoots)
    {
        QString candidate;
        if (EditorAssetPath::resolveLogicalResourcePath(
                root, resourcePath, candidate) &&
            QFileInfo::exists(candidate))
        {
            absolutePath = candidate;
            return true;
        }
    }
    absolutePath.clear();
    return false;
}

bool MenuEditorWindow::resourcePathForFile(const QString& filePath, QString& resourcePath) const
{
    const QString absolute = EditorAssetPath::normalizedAbsolutePath(filePath);
    for (const QString& root : uiReadRoots)
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

    QString sourcePath;
    if (resolveReadableResourcePath(resourcePath, sourcePath))
        readComponentIniProperties(sourcePath, properties);
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

bool MenuEditorWindow::stageComponentProperties(const QString& ownerId,
                                                const QString& resourcePath,
                                                const ComponentIniProperties& properties,
                                                const QString& componentType)
{
    QString normalized = normalizedResourcePath(resourcePath);
    QString localPath;
    if (ownerId.isEmpty() || normalized.isEmpty() ||
        !resolveLocalResourcePath(normalized, localPath))
    {
        return false;
    }

    QString sourcePath;
    resolveReadableResourcePath(normalized, sourcePath);
    if (sourcePath.isEmpty())
        sourcePath = localPath;

    clearModifiedOwner(ownerId);
    ModifiedComponentIni& modified = modifiedComponentInis[localPath];
    modified.properties = properties;
    modified.resourcePath = normalized;
    modified.sourcePath = sourcePath;
    modified.owners.insert(ownerId);
    modified.ownerComponentTypes.insert(ownerId, componentType);
    modified.stagedForWrite = true;
    componentIniCache.remove(localPath);
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
    if (confirmSaveIfModified() == SaveConfirmResult::Cancelled)
        return false;

    const MenuDefinition previousDefinition = currentDefinition;
    const auto previousModified = modifiedComponentInis;
    const auto previousCache = componentIniCache;
    const auto previousSubMenus = modifiedSubMenuIds;
    const QStringList previousLines = originalMenuFileLines;
    const QString previousPath = currentFilePath;
    const QString previousTitle = windowTitle();
    const bool previousDirty = hasUnsavedChanges;
    const QString previousAssetsBasePath = assetsBasePath;
    const bool previousWriteRootLocked = activeWriteRootLocked;

    rebuildResourceContext(filePath);
    modifiedComponentInis.clear();
    componentIniCache.clear();
    modifiedSubMenuIds.clear();
    hasUnsavedChanges = false;
    if (!loadFromMenuFile(filePath))
    {
        currentDefinition = previousDefinition;
        modifiedComponentInis = previousModified;
        componentIniCache = previousCache;
        modifiedSubMenuIds = previousSubMenus;
        originalMenuFileLines = previousLines;
        currentFilePath = previousPath;
        hasUnsavedChanges = previousDirty;
        setWindowTitle(previousTitle);
        assetsBasePath = previousAssetsBasePath;
        activeWriteRootLocked = previousWriteRootLocked;
        rebuildResourceContext(previousPath);
        updateComponentTree();
        updatePreview();
        return false;
    }
    currentFilePath = EditorAssetPath::normalizedAbsolutePath(filePath);
    updateComponentTree();
    updatePreview();
    setWindowTitle(tr("菜单编辑器 - ") + QFileInfo(filePath).fileName());
    resetHistory();
    return true;
}

void MenuEditorWindow::newMenuDefinition()
{
    if (confirmSaveIfModified() == SaveConfirmResult::Cancelled) return;

    currentDefinition = MenuDefinition();
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

    updateComponentTree();
    previewCanvas->setSelectedComponent({});
    previewCanvas->clear();
    setWindowTitle(tr("菜单编辑器 - 新建"));
    resetHistory();
}

void MenuEditorWindow::onAddComponent()
{
    QStringList componentTypes = {
        "ImageContainer", "Button", "CheckBox", "Item", "Label",
        "Scrollbar", "ColumnImage", "ListBox", "TalkLabel",
        "TextButton", "ChooseTextButton", "RoundButton", "DragRoundButton",
        "DragButton", "MemoText", "FadeMask", "TransImage",
        "Joystick"
    };

    bool ok = false;
    QString selectedType = QInputDialog::getItem(this,
        tr("添加组件"),
        tr("选择组件类型:"),
        componentTypes, 0, false, &ok);

    if (!ok || selectedType.isEmpty())
    {
        return;
    }

    bool addToSubMenu = (selectedItemType == SelectedItemType::SubMenu
                         || selectedItemType == SelectedItemType::SubMenuComponent);
    int targetSubMenuIndex = addToSubMenu ? selectedSubMenuIndex : -1;

    if (addToSubMenu && (targetSubMenuIndex < 0 || targetSubMenuIndex >= currentDefinition.subMenus.size()))
    {
        return;
    }

    QSet<QString> existingNames;
    if (addToSubMenu)
    {
        for (const auto& comp : currentDefinition.subMenus[targetSubMenuIndex].components)
        {
            existingNames.insert(comp.name.toLower());
        }
    }
    else
    {
        for (const auto& comp : currentDefinition.components)
        {
            existingNames.insert(comp.name.toLower());
        }
    }

    QString baseName = selectedType.toLower();
    QString componentName = baseName;
    int nameIndex = 1;
    while (existingNames.contains(componentName))
    {
        nameIndex++;
        componentName = baseName + QString::number(nameIndex);
    }

    MenuComponentDefinition definition;
    definition.editorId = "component/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    definition.type = selectedType;
    definition.name = componentName;
    definition.file = "";
    definition.bind = "";
    definition.format = "%d";

    if (addToSubMenu)
    {
        auto& subMenu = currentDefinition.subMenus[targetSubMenuIndex];
        subMenu.components.append(definition);
        markSubMenuModified(targetSubMenuIndex);

        PreviewComponent pc;
        pc.editorId = definition.editorId;
        pc.type = definition.type;
        pc.name = definition.name;
        subMenu.previewComponents.append(pc);
    }
    else
    {
        currentDefinition.components.append(definition);
    }

    markModified();

    updateComponentTree();

    if (addToSubMenu)
    {
        int parentTopLevelIndex = currentDefinition.components.size() + 1 + targetSubMenuIndex;
        QTreeWidgetItem* parentItem = ui->componentTree->topLevelItem(parentTopLevelIndex);
        if (parentItem)
        {
            int childIndex = parentItem->childCount() - 1;
            QTreeWidgetItem* childItem = parentItem->child(childIndex);
            if (childItem)
            {
                ui->componentTree->setCurrentItem(childItem);
            }
        }
    }
    else
    {
        int newIndex = currentDefinition.components.size() - 1;
        selectComponentByIndex(newIndex);
    }

    updatePreview();
}

void MenuEditorWindow::onAddSubMenu()
{
    QString filePath = QFileDialog::getOpenFileName(this,
        tr("选择子菜单定义文件"), assetsBasePath,
        "Menu INI Files (*.menu.ini);;INI Files (*.ini);;All Files (*)",
        nullptr, QFileDialog::DontResolveSymlinks);

    if (filePath.isEmpty())
    {
        return;
    }

    QString relativePath;
    if (!resourcePathForFile(filePath, relativePath))
    {
        QMessageBox::warning(this, tr("无法添加子菜单"),
            tr("所选文件不在当前资源包或 UI 回退根中。"));
        return;
    }
    relativePath.replace("/", "\\");

    SubMenuDefinition subMenu;
    subMenu.editorId = "submenu/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    subMenu.file = relativePath;
    loadSubMenuDefinition(subMenu);

    currentDefinition.subMenus.append(subMenu);
    markModified();

    updateComponentTree();
    updatePreview();
}

void MenuEditorWindow::onRemoveComponent()
{
    if (selectedItemType == SelectedItemType::None
        || selectedItemType == SelectedItemType::MenuWindow
        || selectedItemType == SelectedItemType::SubComponent)
    {
        return;
    }

    QString itemName;
    if (selectedItemType == SelectedItemType::Component)
    {
        if (selectedComponentIndex < 0 || selectedComponentIndex >= currentDefinition.components.size())
        {
            return;
        }
        itemName = currentDefinition.components[selectedComponentIndex].name;
    }
    else if (selectedItemType == SelectedItemType::SubMenu)
    {
        if (selectedSubMenuIndex < 0 || selectedSubMenuIndex >= currentDefinition.subMenus.size())
        {
            return;
        }
        itemName = currentDefinition.subMenus[selectedSubMenuIndex].name;
    }
    else if (selectedItemType == SelectedItemType::SubMenuComponent)
    {
        if (selectedSubMenuIndex < 0 || selectedSubMenuIndex >= currentDefinition.subMenus.size())
        {
            return;
        }
        auto& subMenu = currentDefinition.subMenus[selectedSubMenuIndex];
        if (selectedSubMenuComponentIndex < 0 || selectedSubMenuComponentIndex >= subMenu.components.size())
        {
            return;
        }
        itemName = subMenu.components[selectedSubMenuComponentIndex].name;
    }

    QMessageBox::StandardButton result = QMessageBox::question(this,
        tr("确认删除"),
        tr("确定要删除组件 \"%1\" 吗？").arg(itemName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (result != QMessageBox::Yes)
    {
        return;
    }

    if (selectedItemType == SelectedItemType::Component)
    {
        clearModifiedOwner(currentDefinition.components[selectedComponentIndex].editorId);
        currentDefinition.components.removeAt(selectedComponentIndex);
    }
    else if (selectedItemType == SelectedItemType::SubMenu)
    {
        const auto& subMenu = currentDefinition.subMenus[selectedSubMenuIndex];
        clearModifiedOwner(subMenu.editorId + "/window");
        for (const auto& component : subMenu.components)
            clearModifiedOwner(component.editorId);
        modifiedSubMenuIds.remove(subMenu.editorId);
        currentDefinition.subMenus.removeAt(selectedSubMenuIndex);
    }
    else if (selectedItemType == SelectedItemType::SubMenuComponent)
    {
        auto& subMenu = currentDefinition.subMenus[selectedSubMenuIndex];
        clearModifiedOwner(subMenu.components[selectedSubMenuComponentIndex].editorId);
        subMenu.components.removeAt(selectedSubMenuComponentIndex);
        subMenu.previewComponents.removeAt(selectedSubMenuComponentIndex);
        markSubMenuModified(selectedSubMenuIndex);
    }

    selectedComponentIndex = -1;
    selectedSubMenuIndex = -1;
    selectedSubMenuComponentIndex = -1;
    selectedItemType = SelectedItemType::None;
    markModified();
    updateComponentTree();
    updatePreview();
    propertyEditor->clearAll();
}

void MenuEditorWindow::onComponentSelected(QTreeWidgetItem* current, QTreeWidgetItem* previous)
{
    Q_UNUSED(previous);

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
}

void MenuEditorWindow::onPropertyChanged()
{
    if (updatingFromCode || selectedItemType == SelectedItemType::None)
        return;

    if (selectedItemType == SelectedItemType::MenuWindow)
    {
        if (!currentDefinition.windowFile.isEmpty())
        {
            ComponentIniProperties windowProps = propertiesForResource(currentDefinition.windowFile);
            windowProps.width = propertyEditor->getMenuWindowWidth();
            windowProps.height = propertyEditor->getMenuWindowHeight();
            windowProps.image = propertyEditor->getMenuWindowImage();
            windowProps.bitmap = propertyEditor->getMenuWindowBitmap();
            windowProps.align = propertyEditor->getMenuWindowAlign();
            windowProps.alignX = propertyEditor->getMenuWindowAlignX();
            windowProps.alignY = propertyEditor->getMenuWindowAlignY();
            windowProps.stretch = propertyEditor->getMenuWindowStretch();
            windowProps.stretchSet = true;
            stageComponentProperties("menu/window", currentDefinition.windowFile,
                                     windowProps, "MenuWindow");
        }
    }
    else if (selectedItemType == SelectedItemType::Component)
    {
        if (selectedComponentIndex < 0 || selectedComponentIndex >= currentDefinition.components.size())
            return;

        const auto& definition = currentDefinition.components[selectedComponentIndex];
        if (!definition.file.isEmpty())
            stageComponentProperties(definition.editorId, definition.file,
                                     propertyEditor->getProperties(), definition.type);
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
}

void MenuEditorWindow::onDefinitionPropertyChanged()
{
    if (updatingFromCode || selectedItemType == SelectedItemType::None)
        return;

    if (selectedItemType == SelectedItemType::MenuWindow)
    {
        const QString oldFile = currentDefinition.windowFile;
        currentDefinition.windowFile = propertyEditor->getMenuWindowFile();
        if (normalizedResourcePath(oldFile) != normalizedResourcePath(currentDefinition.windowFile))
            clearModifiedOwner("menu/window");
    }
    else if (selectedItemType == SelectedItemType::Component &&
             selectedComponentIndex >= 0 && selectedComponentIndex < currentDefinition.components.size())
    {
        auto& definition = currentDefinition.components[selectedComponentIndex];
        const QString oldFile = definition.file;
        definition.name = propertyEditor->getComponentName();
        definition.file = propertyEditor->getComponentFile();
        definition.bind = propertyEditor->getComponentBind();
        definition.format = propertyEditor->getComponentFormat();
        definition.controllerUp = propertyEditor->getControllerUp();
        definition.controllerDown = propertyEditor->getControllerDown();
        definition.controllerLeft = propertyEditor->getControllerLeft();
        definition.controllerRight = propertyEditor->getControllerRight();
        if (normalizedResourcePath(oldFile) != normalizedResourcePath(definition.file))
            clearModifiedOwner(definition.editorId);
    }
    else if (selectedItemType == SelectedItemType::SubMenu &&
             selectedSubMenuIndex >= 0 && selectedSubMenuIndex < currentDefinition.subMenus.size())
    {
        auto& subMenu = currentDefinition.subMenus[selectedSubMenuIndex];
        const QString oldWindow = subMenu.windowFile;
        subMenu.name = propertyEditor->getComponentName();
        subMenu.file = propertyEditor->getComponentFile();
        subMenu.windowFile = propertyEditor->getSubMenuWindowFile();
        if (normalizedResourcePath(oldWindow) != normalizedResourcePath(subMenu.windowFile))
            clearModifiedOwner(subMenu.editorId + "/window");
        markSubMenuModified(selectedSubMenuIndex);
    }
    else if (selectedItemType == SelectedItemType::SubMenuComponent &&
             selectedSubMenuIndex >= 0 && selectedSubMenuIndex < currentDefinition.subMenus.size())
    {
        auto& subMenu = currentDefinition.subMenus[selectedSubMenuIndex];
        if (selectedSubMenuComponentIndex < 0 || selectedSubMenuComponentIndex >= subMenu.components.size())
            return;
        auto& definition = subMenu.components[selectedSubMenuComponentIndex];
        const QString oldFile = definition.file;
        definition.name = propertyEditor->getComponentName();
        definition.file = propertyEditor->getComponentFile();
        definition.bind = propertyEditor->getComponentBind();
        definition.format = propertyEditor->getComponentFormat();
        definition.controllerUp = propertyEditor->getControllerUp();
        definition.controllerDown = propertyEditor->getControllerDown();
        definition.controllerLeft = propertyEditor->getControllerLeft();
        definition.controllerRight = propertyEditor->getControllerRight();
        if (normalizedResourcePath(oldFile) != normalizedResourcePath(definition.file))
            clearModifiedOwner(definition.editorId);
        markSubMenuModified(selectedSubMenuIndex);
    }

    markModified();
    updatingFromCode = true;
    if (selectedItemType == SelectedItemType::MenuWindow)
    {
        if (QTreeWidgetItem* item = ui->componentTree->topLevelItem(0))
            item->setText(2, currentDefinition.windowFile);
    }
    else if (selectedItemType == SelectedItemType::Component)
    {
        if (QTreeWidgetItem* item = ui->componentTree->topLevelItem(selectedComponentIndex + 1))
        {
            const auto& definition = currentDefinition.components[selectedComponentIndex];
            item->setText(1, definition.name);
            item->setText(2, definition.file);
        }
    }
    else if (selectedItemType == SelectedItemType::SubMenu)
    {
        const int index = currentDefinition.components.size() + 1 + selectedSubMenuIndex;
        if (QTreeWidgetItem* item = ui->componentTree->topLevelItem(index))
        {
            const auto& subMenu = currentDefinition.subMenus[selectedSubMenuIndex];
            item->setText(1, subMenu.name);
            item->setText(2, subMenu.file);
        }
    }
    else if (selectedItemType == SelectedItemType::SubMenuComponent)
    {
        const int index = currentDefinition.components.size() + 1 + selectedSubMenuIndex;
        if (QTreeWidgetItem* parent = ui->componentTree->topLevelItem(index))
        {
            if (QTreeWidgetItem* item = parent->child(selectedSubMenuComponentIndex))
            {
                const auto& definition = currentDefinition.subMenus[selectedSubMenuIndex]
                    .components[selectedSubMenuComponentIndex];
                item->setText(1, definition.name);
                item->setText(2, definition.file);
            }
        }
    }
    updatingFromCode = false;
}

void MenuEditorWindow::onResourceFileEditingFinished()
{
    if (updatingFromCode)
        return;

    QString previousSubMenuFile;
    QString previousSubMenuWindow;
    QString subMenuEditorId;
    QList<MenuComponentDefinition> previousSubMenuComponents;
    if (selectedItemType == SelectedItemType::SubMenu &&
        selectedSubMenuIndex >= 0 && selectedSubMenuIndex < currentDefinition.subMenus.size())
    {
        const auto& subMenu = currentDefinition.subMenus[selectedSubMenuIndex];
        previousSubMenuFile = subMenu.file;
        previousSubMenuWindow = subMenu.windowFile;
        subMenuEditorId = subMenu.editorId;
        previousSubMenuComponents = subMenu.components;
    }

    onDefinitionPropertyChanged();
    componentIniCache.clear();

    bool subMenuSnapshotNeedsRefresh = false;
    if (!subMenuEditorId.isEmpty() &&
        selectedSubMenuIndex >= 0 && selectedSubMenuIndex < currentDefinition.subMenus.size())
    {
        auto& subMenu = currentDefinition.subMenus[selectedSubMenuIndex];
        if (normalizedResourcePath(previousSubMenuFile) != normalizedResourcePath(subMenu.file))
        {
            clearModifiedOwner(subMenuEditorId + "/window");
            for (const MenuComponentDefinition& component : previousSubMenuComponents)
                clearModifiedOwner(component.editorId);

            SubMenuDefinition replacement;
            replacement.editorId = subMenuEditorId;
            replacement.file = subMenu.file;
            loadSubMenuDefinition(replacement);
            subMenu = replacement;
            modifiedSubMenuIds.remove(subMenuEditorId);
            updateComponentTree();
            subMenuSnapshotNeedsRefresh = true;
        }
        else if (normalizedResourcePath(previousSubMenuWindow) !=
                 normalizedResourcePath(subMenu.windowFile) && !subMenu.windowFile.isEmpty())
        {
            const ComponentIniProperties window = propertiesForResource(subMenu.windowFile);
            subMenu.windowRect = QRect(window.left, window.top, window.width, window.height);
            subMenu.backgroundImage = window.image.isEmpty() ? window.bitmap : window.image;
            subMenu.windowStretch = window.stretch;
            subMenuSnapshotNeedsRefresh = true;
        }
    }

    if (subMenuSnapshotNeedsRefresh)
    {
        // onDefinitionPropertyChanged() records the edited file reference before
        // the destination submenu/window has been loaded. Replace that transient
        // snapshot so Undo/Redo never restores a new path with stale contents.
        const EditorSnapshot snapshot = createSnapshot();
        if (historyIndex >= 0)
        {
            history[historyIndex] = snapshot;
            hasUnsavedChanges = snapshotFingerprint(snapshot) != savedSnapshotFingerprint;
            updateUndoRedoState();
            emit documentStatesChanged();
        }
        else
        {
            markModified();
        }
    }

    updatePropertyEditor();
    updatePreview();
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
        stream << component.editorId << component.type << component.name
               << component.file << component.bind << component.format
               << component.controllerUp << component.controllerDown
               << component.controllerLeft << component.controllerRight;
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
}

void MenuEditorWindow::resetHistory()
{
    history.clear();
    history.append(createSnapshot());
    historyIndex = 0;
    savedSnapshotFingerprint = snapshotFingerprint(history.first());
    hasUnsavedChanges = false;
    updateUndoRedoState();
    emit documentStatesChanged();
}

void MenuEditorWindow::establishSavePoint()
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

    QList<FileTransaction> transactions;
    QSet<QString> targetKeys;
    QString error;

    if (!validateControllerNavigationOverrides(error))
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
    establishSavePoint();
    return true;
}

void MenuEditorWindow::onSaveFile()
{
    if (currentFilePath.isEmpty())
    {
        onSaveAsFile();
        return;
    }

    QString resourcePath;
    QString localPath;
    if (!resourcePathForFile(currentFilePath, resourcePath) ||
        !resolveLocalResourcePath(resourcePath, localPath))
    {
        QMessageBox::warning(this, tr("保存失败"),
            tr("当前菜单不在可识别的 UI 资源根中。"));
        return;
    }
    saveDocumentToPath(localPath, true);
}

void MenuEditorWindow::onSaveAsFile()
{
    const QString filePath = QFileDialog::getSaveFileName(this,
        tr("保存菜单定义文件"), assetsBasePath,
        "Menu INI Files (*.menu.ini);;INI Files (*.ini);;All Files (*)");
    if (filePath.isEmpty())
        return;
    saveMenuDefinitionAs(filePath);
}

bool MenuEditorWindow::saveMenuDefinitionAs(const QString& filePath)
{
    if (filePath.trimmed().isEmpty())
        return false;
    if (!EditorAssetPath::isInside(assetsBasePath, filePath))
    {
        QMessageBox::warning(this, tr("保存失败"),
            tr("菜单只能保存到当前可写资源包内:\n%1").arg(assetsBasePath));
        return false;
    }
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
    if (selectedItemType == SelectedItemType::SubMenuComponent)
    {
        if (selectedSubMenuIndex < 0 || selectedSubMenuIndex >= currentDefinition.subMenus.size())
            return;
        auto& subMenu = currentDefinition.subMenus[selectedSubMenuIndex];
        if (selectedSubMenuComponentIndex <= 0 || selectedSubMenuComponentIndex >= subMenu.components.size())
            return;

        subMenu.components.swapItemsAt(selectedSubMenuComponentIndex, selectedSubMenuComponentIndex - 1);
        subMenu.previewComponents.swapItemsAt(selectedSubMenuComponentIndex, selectedSubMenuComponentIndex - 1);
        markSubMenuModified(selectedSubMenuIndex);
        markModified();
        int newIndex = selectedSubMenuComponentIndex - 1;
        updateComponentTree();

        int parentTopLevelIndex = currentDefinition.components.size() + 1 + selectedSubMenuIndex;
        QTreeWidgetItem* parentItem = ui->componentTree->topLevelItem(parentTopLevelIndex);
        if (parentItem)
        {
            QTreeWidgetItem* childItem = parentItem->child(newIndex);
            if (childItem)
                ui->componentTree->setCurrentItem(childItem);
        }
        updatePreview();
        return;
    }

    if (selectedComponentIndex <= 0 || selectedComponentIndex >= currentDefinition.components.size())
    {
        return;
    }

    currentDefinition.components.swapItemsAt(selectedComponentIndex, selectedComponentIndex - 1);
    markModified();
    int newIndex = selectedComponentIndex - 1;
    updateComponentTree();
    selectComponentByIndex(newIndex);
    updatePreview();
}

void MenuEditorWindow::onComponentMovedDown()
{
    if (selectedItemType == SelectedItemType::SubMenuComponent)
    {
        if (selectedSubMenuIndex < 0 || selectedSubMenuIndex >= currentDefinition.subMenus.size())
            return;
        auto& subMenu = currentDefinition.subMenus[selectedSubMenuIndex];
        if (selectedSubMenuComponentIndex < 0 || selectedSubMenuComponentIndex >= subMenu.components.size() - 1)
            return;

        subMenu.components.swapItemsAt(selectedSubMenuComponentIndex, selectedSubMenuComponentIndex + 1);
        subMenu.previewComponents.swapItemsAt(selectedSubMenuComponentIndex, selectedSubMenuComponentIndex + 1);
        markSubMenuModified(selectedSubMenuIndex);
        markModified();
        int newIndex = selectedSubMenuComponentIndex + 1;
        updateComponentTree();

        int parentTopLevelIndex = currentDefinition.components.size() + 1 + selectedSubMenuIndex;
        QTreeWidgetItem* parentItem = ui->componentTree->topLevelItem(parentTopLevelIndex);
        if (parentItem)
        {
            QTreeWidgetItem* childItem = parentItem->child(newIndex);
            if (childItem)
                ui->componentTree->setCurrentItem(childItem);
        }
        updatePreview();
        return;
    }

    if (selectedComponentIndex < 0 || selectedComponentIndex >= currentDefinition.components.size() - 1)
    {
        return;
    }

    currentDefinition.components.swapItemsAt(selectedComponentIndex, selectedComponentIndex + 1);
    markModified();
    int newIndex = selectedComponentIndex + 1;
    updateComponentTree();
    selectComponentByIndex(newIndex);
    updatePreview();
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
        item->setText(0, definition.type);
        item->setText(1, definition.name);
        item->setText(2, definition.file);
        QColor itemColor = typeColors.value(definition.type, QColor(212, 212, 212));
        for (int col = 0; col < 3; col++)
        {
            item->setForeground(col, itemColor);
        }
        ui->componentTree->addTopLevelItem(item);

        if (definition.type == "Scrollbar" && !definition.file.isEmpty())
        {
            const ComponentIniProperties scrollbarProps = propertiesForResource(definition.file);

            if (!scrollbarProps.slideBtn.isEmpty())
            {
                auto childItem = new QTreeWidgetItem();
                childItem->setText(0, "DragButton");
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
            child->setText(0, comp.type);
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
}

void MenuEditorWindow::updatePropertyEditor()
{
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
            const ComponentIniProperties windowProps = propertiesForResource(currentDefinition.windowFile);
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
        propertyEditor->setComponentType(definition.type);
        propertyEditor->setDefinitionProperties(definition.name, definition.file, definition.bind, definition.format);
        propertyEditor->setControllerNavigationOverrides(
            definition.controllerUp, definition.controllerDown,
            definition.controllerLeft, definition.controllerRight);

        if (!definition.file.isEmpty())
        {
            propertyEditor->setProperties(propertiesForResource(definition.file));
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
    previewCanvas->clear();

    if (!currentDefinition.windowFile.isEmpty())
    {
        const ComponentIniProperties windowProps = propertiesForResource(currentDefinition.windowFile);

        QRect menuRect(0, 0, windowProps.width, windowProps.height);
        previewCanvas->setMenuRect(menuRect);

        const QString background = windowProps.image.isEmpty() ? windowProps.bitmap : windowProps.image;
        if (!background.isEmpty())
            previewCanvas->setBackgroundImage(background);
        previewCanvas->setWindowStretch(windowProps.stretch);
    }

    QList<PreviewComponent> previewComponents;
    for (const auto& definition : currentDefinition.components)
    {
        PreviewComponent previewComponent;
        previewComponent.editorId = definition.editorId;
        previewComponent.type = definition.type;
        previewComponent.name = definition.name;

        if (!definition.file.isEmpty())
        {
            const ComponentIniProperties props = propertiesForResource(definition.file);
            previewComponent.rect = QRect(props.left, props.top, props.width, props.height);
            setPreviewComponentImages(definition.type, props, previewComponent);
            previewComponent.stretch = resolveStretch(props, definition.type);

            if (definition.type == "ListBox")
            {
                previewComponent.listBoxItems = props.items;
                previewComponent.itemHeight = props.itemHeight;
                previewComponent.color = props.color;
                previewComponent.selColor = props.selColor;
            }

            if (definition.type == "Scrollbar" && !props.slideBtn.isEmpty())
            {
                const QString slideResource = normalizedResourcePath(QDir::cleanPath(
                    QFileInfo(normalizedResourcePath(definition.file)).path() + "/" + props.slideBtn));
                const ComponentIniProperties slideBtnProps = propertiesForResource(slideResource);

                PreviewComponent slideBtnPreview;
                slideBtnPreview.editorId = definition.editorId + "/slideBtn";
                slideBtnPreview.type = "DragButton";
                slideBtnPreview.name = definition.name + "_slideBtn";
                slideBtnPreview.rect = QRect(
                    props.left + slideBtnProps.left,
                    props.top + slideBtnProps.top,
                    slideBtnProps.width,
                    slideBtnProps.height);
                slideBtnPreview.imagePath = slideBtnProps.image;
                slideBtnPreview.stretch = slideBtnProps.stretch;

                previewComponent.children.append(slideBtnPreview);
            }
        }

        previewComponents.append(previewComponent);
    }

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
            component.editorId = definition.editorId;
            component.type = definition.type;
            component.name = definition.name;
            if (!definition.file.isEmpty())
            {
                const ComponentIniProperties properties = propertiesForResource(definition.file);
                component.rect = QRect(properties.left, properties.top,
                                       properties.width, properties.height);
                setPreviewComponentImages(definition.type, properties, component);
                component.stretch = resolveStretch(properties, definition.type);
                if (definition.type == "ListBox")
                {
                    component.listBoxItems = properties.items;
                    component.itemHeight = properties.itemHeight;
                    component.color = properties.color;
                    component.selColor = properties.selColor;
                }
                if (definition.type == "Scrollbar" && !properties.slideBtn.isEmpty())
                {
                    const QString slideResource = normalizedResourcePath(QDir::cleanPath(
                        QFileInfo(normalizedResourcePath(definition.file)).path() + "/" + properties.slideBtn));
                    const ComponentIniProperties slide = propertiesForResource(slideResource);
                    PreviewComponent child;
                    child.editorId = definition.editorId + "/slideBtn";
                    child.type = "DragButton";
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
    QString menuResourcePath;
    if (!resourcePathForFile(filePath, menuResourcePath))
    {
        QMessageBox::warning(this, tr("错误"),
            tr("菜单文件不在当前资源包或其 UI 回退根中，已拒绝打开:\n%1").arg(filePath));
        return false;
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
    for (int key : sortedKeys)
    {
        componentMap[key].editorId =
            "component/" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        currentDefinition.components.append(componentMap[key]);
    }

    QList<int> sortedSubMenuKeys = subMenuMap.keys();
    std::sort(sortedSubMenuKeys.begin(), sortedSubMenuKeys.end());
    for (int key : sortedSubMenuKeys)
    {
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
    QStringList outputLines;

    QSet<QString> menuKeys = {"name", "visible", "window"};
    QSet<QString> componentKeys = {
        "type", "name", "file", "bind", "format",
        "controllerup", "controllerdown", "controllerleft", "controllerright"};
    const QSet<QString> controllerNavigationKeys = {
        "controllerup", "controllerdown", "controllerleft", "controllerright"};
    QSet<QString> subMenuKeys = {"name", "file"};

    QMap<QString, QString> menuValues;
    menuValues["name"] = currentDefinition.menuName;
    menuValues["visible"] = currentDefinition.visible ? "true" : "false";
    menuValues["window"] = currentDefinition.windowFile;

    QMap<int, QMap<QString, QString>> componentValues;
    for (int i = 0; i < currentDefinition.components.size(); i++)
    {
        const auto& definition = currentDefinition.components[i];
        componentValues[i + 1]["type"] = definition.type;
        componentValues[i + 1]["name"] = definition.name;
        componentValues[i + 1]["file"] = definition.file;
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
    for (int i = 0; i < currentDefinition.subMenus.size(); i++)
    {
        const auto& subMenu = currentDefinition.subMenus[i];
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

        for (const QString& origLine : originalMenuFileLines)
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
                    if (ok && index >= 1 && index <= currentDefinition.components.size())
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
                    if (ok && index >= 1 && index <= currentDefinition.subMenus.size())
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
        outputLines << "name=" + currentDefinition.menuName;
        outputLines << QString("visible=") + (currentDefinition.visible ? "true" : "false");
        outputLines << "window=" + currentDefinition.windowFile;
        outputLines << "";
    }

    for (int i = 0; i < currentDefinition.components.size(); i++)
    {
        int index = i + 1;
        if (writtenComponents.contains(index))
            continue;

        outputLines << "";
        outputLines << "[component" + QString::number(index) + "]";
        const auto& definition = currentDefinition.components[i];
        outputLines << "type=" + definition.type;
        outputLines << "name=" + definition.name;
        outputLines << "file=" + definition.file;
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

    for (int i = 0; i < currentDefinition.subMenus.size(); i++)
    {
        int index = i + 1;
        if (writtenSubMenus.contains(index))
            continue;

        outputLines << "";
        outputLines << "[submenu" + QString::number(index) + "]";
        const auto& subMenu = currentDefinition.subMenus[i];
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

        if (line.startsWith("[") && line.endsWith("]"))
        {
            currentSection = line.mid(1, line.length() - 2).toLower();
            continue;
        }

        int equalPos = line.indexOf('=');
        if (equalPos < 0)
        {
            continue;
        }

        QString key = line.left(equalPos).trimmed().toLower();
        QString value = line.mid(equalPos + 1).trimmed();

        if (currentSection == "items")
        {
            bool ok = false;
            const int index = key.toInt(&ok);
            if (ok && index > 0)
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

    props.items = indexedItems.values();

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
    typeSpecificKeys["Button"] = QSet<QString>{"kind", "up", "down", "track", "sound", "stretch", "hoversound", "animate"};
    typeSpecificKeys["CheckBox"] = QSet<QString>{"up", "down", "sound"};
    typeSpecificKeys["ImageContainer"] = QSet<QString>{"stretch", "keepaspect", "cropcontent", "cropblack", "frame"};
    typeSpecificKeys["Scrollbar"] = QSet<QString>{"style", "min", "max", "position", "linesize", "pagesize", "slidebegin", "slideend", "slidebtn"};
    typeSpecificKeys["ColumnImage"] = QSet<QString>{"percent"};
    typeSpecificKeys["ListBox"] = QSet<QString>{"sound", "itemheight", "itemcount", "color", "selcolor"};
    typeSpecificKeys["Item"] = QSet<QString>{"font", "color", "stretch", "keepaspect", "centerimage", "frame", "backimage1", "backimage2"};
    typeSpecificKeys["Label"] = QSet<QString>{"font", "color", "stretch", "keepaspect", "centerimage", "frame", "backimage1", "backimage2", "autoshrink"};
    typeSpecificKeys["TalkLabel"] = QSet<QString>{"font", "color", "stretch", "keepaspect", "centerimage", "frame", "backimage1", "backimage2", "autoshrink"};
    typeSpecificKeys["TextButton"] = QSet<QString>{"kind", "up", "down", "track", "sound", "font", "color", "stretch", "hoversound", "animate"};
    typeSpecificKeys["RoundButton"] = QSet<QString>{"kind", "up", "down", "track", "sound", "range", "text", "stretch", "hoversound", "animate", "icon", "iconimage"};
    typeSpecificKeys["DragRoundButton"] = QSet<QString>{"kind", "up", "down", "track", "sound", "range", "text", "indicatetype", "indicateimage", "stretch", "hoversound", "animate", "icon", "iconimage"};
    typeSpecificKeys["DragButton"] = QSet<QString>{"kind", "up", "down", "track", "sound", "stretch", "hoversound", "animate"};
    typeSpecificKeys["MemoText"] = QSet<QString>{"font", "color"};
    typeSpecificKeys["FadeMask"] = QSet<QString>{};
    typeSpecificKeys["Joystick"] = QSet<QString>{"range", "baseimage", "thumbimage"};
    typeSpecificKeys["ChooseTextButton"] = QSet<QString>{"kind", "up", "down", "track", "sound", "font", "color", "normalcolor", "hovercolor", "presscolor", "stretch", "hoversound", "animate"};
    typeSpecificKeys["TransImage"] = QSet<QString>{"stretch", "keepaspect", "cropcontent", "cropblack", "frame"};
    typeSpecificKeys["VideoPlayer"] = QSet<QString>{};

    QSet<QString> windowExtraKeys;
    windowExtraKeys << "align" << "alignx" << "aligny" << "stretch" << "bitmap";

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

    QMap<QString, QString> newValues;
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

        if (trimmed.startsWith("[") && trimmed.endsWith("]"))
        {
            QString sectionName = trimmed.mid(1, trimmed.length() - 2).toLower();

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

        int equalPos = line.indexOf('=');
        if (equalPos < 0)
        {
            outputLines << line;
            continue;
        }

        QString key = line.left(equalPos).trimmed().toLower();
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
            outputLines << line.left(equalPos).trimmed() + "=" + newValues[key];
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
            if (browserRoot.isEmpty() && !uiReadRoots.isEmpty())
                browserRoot = uiReadRoots.first();
            QString fullPath;
            if (EditorAssetPath::resolveLogicalResourcePath(
                    browserRoot, relativePath, fullPath) &&
                QFileInfo::exists(fullPath) && QFileInfo(fullPath).isFile())
            {
                openMenuDefinition(fullPath);
            }
        }
        else if (selectedItemType == SelectedItemType::MenuWindow)
        {
            clearModifiedOwner("menu/window");
            currentDefinition.windowFile = relativePath;
            updatingFromCode = true;
            QTreeWidgetItem* item = ui->componentTree->topLevelItem(0);
            if (item)
            {
                item->setText(2, relativePath);
            }
            updatingFromCode = false;
            markModified();
            updatePropertyEditor();
            updatePreview();
        }
        else if (selectedComponentIndex >= 0 && selectedComponentIndex < currentDefinition.components.size())
        {
            clearModifiedOwner(currentDefinition.components[selectedComponentIndex].editorId);
            currentDefinition.components[selectedComponentIndex].file = relativePath;
            updatingFromCode = true;
            QTreeWidgetItem* item = ui->componentTree->topLevelItem(selectedComponentIndex + 1);
            if (item)
            {
                item->setText(2, relativePath);
            }
            propertyEditor->setDefinitionProperties(
                currentDefinition.components[selectedComponentIndex].name,
                currentDefinition.components[selectedComponentIndex].file,
                currentDefinition.components[selectedComponentIndex].bind,
                currentDefinition.components[selectedComponentIndex].format);
            updatingFromCode = false;
            markModified();
            updatePropertyEditor();
            updatePreview();
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
